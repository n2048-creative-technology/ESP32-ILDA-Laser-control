// ESP32 ILDA receiver node.
//
// Receives ESP-NOW frame/control packets from the sender node and drives an
// external 3x MCP4922 SPI DAC bank feeding the ILDA analog front end
// described in docs/HARDWARE.md.
//
// Safety-critical behavior (see docs/SAFETY.md):
//   - Boots with the shutter closed and PATTERN_BLANK selected.
//   - Any bad/short/unrecognized packet is dropped, never partially applied.
//   - If no packet (frame or control) has been received for LINK_TIMEOUT_MS,
//     the output task forces the shutter closed and centers the galvos,
//     overriding whatever pattern was last selected.
#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <math.h>

#include "config.h"
#include "ilda_protocol.h"

using namespace ilda;

// ---------------------------------------------------------------------------
// Display buffers (double-buffered: one being displayed, one being filled by
// incoming frame chunks). Swapped under bufferMux once a frame is complete.
// ---------------------------------------------------------------------------
static IldaPoint bufferA[MAX_POINTS_BUFFERED];
static IldaPoint bufferB[MAX_POINTS_BUFFERED];

static IldaPoint* frontBuffer = bufferA;
static volatile uint16_t frontCount = 0;

static IldaPoint* backBuffer = bufferB;
static volatile uint16_t backExpectedTotal = 0;
static volatile uint16_t backReceivedPoints = 0;
static volatile uint16_t backFrameId = 0xFFFF;

static portMUX_TYPE bufferMux = portMUX_INITIALIZER_UNLOCKED;

// ---------------------------------------------------------------------------
// Live control state, updated from ESP-NOW control packets and consumed by
// the DAC output task.
// ---------------------------------------------------------------------------
static volatile uint32_t lastPacketMillis = 0;
static volatile uint8_t g_shutterOpen = 0;
static volatile uint8_t g_brightness = 255;
static volatile uint8_t g_patternId = PATTERN_BLANK;
static volatile uint16_t g_pointsPerSecond = DEFAULT_POINTS_PER_SECOND;

// ---------------------------------------------------------------------------
// MCP4922 SPI DAC driver
// ---------------------------------------------------------------------------
static SPIClass dacSpi(VSPI);

static void dacWriteChannel(int csPin, bool channelB, uint16_t value12) {
  value12 &= 0x0FFF;
  // bit15 A/B select, bit14 BUF=1 (buffered), bit13 GA=1 (1x gain),
  // bit12 SHDN=1 (active mode), bits11-0 data.
  uint16_t cmd = (channelB ? 0x8000 : 0x0000) | 0x7000 | value12;
  digitalWrite(csPin, LOW);
  dacSpi.transfer16(cmd);
  digitalWrite(csPin, HIGH);
}

// Loads all 5 channels into the DACs' input registers, then pulses the
// shared LDAC line once so every channel updates in lockstep.
static void dacWritePoint(int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
  uint16_t xCode = static_cast<uint16_t>(x + 2048) & 0x0FFF;
  uint16_t yCode = static_cast<uint16_t>(y + 2048) & 0x0FFF;
  uint16_t rCode = static_cast<uint16_t>(((uint32_t)r * brightness / 255) * 16) & 0x0FFF;
  uint16_t gCode = static_cast<uint16_t>(((uint32_t)g * brightness / 255) * 16) & 0x0FFF;
  uint16_t bCode = static_cast<uint16_t>(((uint32_t)b * brightness / 255) * 16) & 0x0FFF;

  dacWriteChannel(PIN_DAC_CS_XY, false, xCode);
  dacWriteChannel(PIN_DAC_CS_XY, true, yCode);
  dacWriteChannel(PIN_DAC_CS_RG, false, rCode);
  dacWriteChannel(PIN_DAC_CS_RG, true, gCode);
  dacWriteChannel(PIN_DAC_CS_B, false, bCode);

  digitalWrite(PIN_DAC_LDAC, LOW);
  delayMicroseconds(1);
  digitalWrite(PIN_DAC_LDAC, HIGH);
}

// ---------------------------------------------------------------------------
// Local calibration patterns (used when no sender is connected, or when
// explicitly selected via a control packet). Independent of the streamed
// frame buffer so they're useful for verifying wiring on their own.
// ---------------------------------------------------------------------------
static IldaPoint patternPoint(uint8_t patternId, uint32_t idx) {
  constexpr int kSteps = 360;
  constexpr int16_t kRadius = 1800;
  float t = (idx % kSteps) * (2.0f * PI / kSteps);

  IldaPoint p{0, 0, 255, 255, 255};
  switch (patternId) {
    case PATTERN_TEST_CIRCLE:
      p.x = static_cast<int16_t>(kRadius * cosf(t));
      p.y = static_cast<int16_t>(kRadius * sinf(t));
      break;
    case PATTERN_TEST_SQUARE: {
      int side = (idx / (kSteps / 4)) % 4;
      float frac = (idx % (kSteps / 4)) / static_cast<float>(kSteps / 4);
      int16_t d = static_cast<int16_t>(-kRadius + frac * (2 * kRadius));
      switch (side) {
        case 0: p.x = d; p.y = -kRadius; break;
        case 1: p.x = kRadius; p.y = d; break;
        case 2: p.x = -d; p.y = kRadius; break;
        default: p.x = -kRadius; p.y = -d; break;
      }
      break;
    }
    case PATTERN_TEST_CROSS: {
      bool horiz = (idx % (kSteps / 2)) < (kSteps / 4);
      float frac = (idx % (kSteps / 4)) / static_cast<float>(kSteps / 4);
      int16_t d = static_cast<int16_t>(-kRadius + frac * (2 * kRadius));
      if (horiz) { p.x = d; p.y = 0; } else { p.x = 0; p.y = d; }
      break;
    }
    default:
      break;
  }
  return p;
}

// ---------------------------------------------------------------------------
// ESP-NOW receive callback. Runs in the Wi-Fi task context - keep it short,
// validate before touching shared state, never block.
// ---------------------------------------------------------------------------
static void handleFrameChunk(const uint8_t* data, int len) {
  if (len < static_cast<int>(sizeof(FrameChunkHeader))) return;
  FrameChunkHeader hdr;
  memcpy(&hdr, data, sizeof(hdr));

  const uint8_t* pointBytes = data + sizeof(FrameChunkHeader);
  size_t pointBytesLen = static_cast<size_t>(hdr.pointCount) * sizeof(IldaPoint);
  if (len < static_cast<int>(sizeof(FrameChunkHeader) + pointBytesLen)) return;
  if (checksum8(pointBytes, pointBytesLen) != hdr.checksum) return;
  if (hdr.totalPoints == 0 || hdr.totalPoints > MAX_POINTS_BUFFERED) return;
  if (static_cast<uint32_t>(hdr.pointOffset) + hdr.pointCount > hdr.totalPoints) return;

  portENTER_CRITICAL(&bufferMux);
  if (hdr.frameId != backFrameId || hdr.pointOffset == 0) {
    // New frame starting (or the previous one was abandoned mid-transfer).
    backFrameId = hdr.frameId;
    backExpectedTotal = hdr.totalPoints;
    backReceivedPoints = 0;
  }
  if (hdr.totalPoints == backExpectedTotal) {
    memcpy(&backBuffer[hdr.pointOffset], pointBytes, pointBytesLen);
    backReceivedPoints += hdr.pointCount;
    if (backReceivedPoints >= backExpectedTotal) {
      IldaPoint* tmp = frontBuffer;
      frontBuffer = backBuffer;
      frontCount = backExpectedTotal;
      backBuffer = tmp;
      backReceivedPoints = 0;
      backExpectedTotal = 0;
    }
  }
  portEXIT_CRITICAL(&bufferMux);
}

static void handleControl(const uint8_t* data, int len) {
  if (len < static_cast<int>(sizeof(ControlMessage))) return;
  ControlMessage msg;
  memcpy(&msg, data, sizeof(msg));
  if (checksum8(data, sizeof(msg) - sizeof(msg.checksum)) != msg.checksum) return;

  g_shutterOpen = msg.shutterOpen ? 1 : 0;
  g_brightness = msg.brightness;
  g_patternId = msg.patternId;
  uint16_t pps = msg.pointsPerSecond;
  if (pps < MIN_POINTS_PER_SECOND) pps = MIN_POINTS_PER_SECOND;
  if (pps > MAX_POINTS_PER_SECOND) pps = MAX_POINTS_PER_SECOND;
  g_pointsPerSecond = pps;
}

#if defined(ESP_ARDUINO_VERSION) && defined(ESP_ARDUINO_VERSION_VAL) && \
    ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
static void onDataRecv(const esp_now_recv_info_t* /*info*/, const uint8_t* data, int len) {
#else
static void onDataRecv(const uint8_t* /*mac*/, const uint8_t* data, int len) {
#endif
  if (len < 1) return;
  lastPacketMillis = millis();
  uint8_t type = data[0];
  if (type == PKT_FRAME_CHUNK) {
    handleFrameChunk(data, len);
  } else if (type == PKT_CONTROL) {
    handleControl(data, len);
  }
}

// ---------------------------------------------------------------------------
// DAC output task: continuously scans the current source (streamed frame or
// a local test pattern) at g_pointsPerSecond, applying the link-loss
// watchdog on every point.
// ---------------------------------------------------------------------------
static void dacOutputTask(void*) {
  uint32_t idx = 0;
  for (;;) {
    bool linkDead = (millis() - lastPacketMillis) > LINK_TIMEOUT_MS;
    uint8_t pattern = g_patternId;

    IldaPoint pt{0, 0, 0, 0, 0};
    bool shutterOpen = false;

    if (linkDead || pattern == PATTERN_BLANK) {
      // Safety default: centered, blanked, shutter closed.
    } else if (pattern == PATTERN_STREAM) {
      portENTER_CRITICAL(&bufferMux);
      uint16_t count = frontCount;
      if (count > 0) pt = frontBuffer[idx % count];
      portEXIT_CRITICAL(&bufferMux);
      shutterOpen = g_shutterOpen != 0;
    } else {
      pt = patternPoint(pattern, idx);
      shutterOpen = g_shutterOpen != 0;
    }

    digitalWrite(PIN_SHUTTER_TTL, shutterOpen ? HIGH : LOW);
    dacWritePoint(pt.x, pt.y, pt.r, pt.g, pt.b, g_brightness);

    idx++;
    uint16_t pps = g_pointsPerSecond;
    if (pps < MIN_POINTS_PER_SECOND) pps = MIN_POINTS_PER_SECOND;
    delayMicroseconds(1000000UL / pps);
  }
}

static void setupEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[FATAL] esp_now_init failed");
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(onDataRecv);

  esp_now_peer_info_t peer{};
  peer.channel = WIFI_CHANNEL;
  peer.ifidx = WIFI_IF_STA;

#if ESPNOW_ENCRYPT
  esp_now_set_pmk(ESPNOW_PMK);
  memcpy(peer.peer_addr, PEER_MAC_ADDR, 6);
  memcpy(peer.lmk, ESPNOW_LMK, 16);
  peer.encrypt = true;
#else
  uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  memcpy(peer.peer_addr, broadcast, 6);
  peer.encrypt = false;
#endif

  esp_now_add_peer(&peer);
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_DAC_CS_XY, OUTPUT);
  pinMode(PIN_DAC_CS_RG, OUTPUT);
  pinMode(PIN_DAC_CS_B, OUTPUT);
  digitalWrite(PIN_DAC_CS_XY, HIGH);
  digitalWrite(PIN_DAC_CS_RG, HIGH);
  digitalWrite(PIN_DAC_CS_B, HIGH);

  pinMode(PIN_DAC_LDAC, OUTPUT);
  digitalWrite(PIN_DAC_LDAC, HIGH);

  // Safety defaults: shutter closed before anything else runs.
  pinMode(PIN_SHUTTER_TTL, OUTPUT);
  digitalWrite(PIN_SHUTTER_TTL, LOW);

  pinMode(PIN_STATUS_LED, OUTPUT);

  dacSpi.begin(PIN_SPI_SCK, -1 /* MISO unused */, PIN_SPI_MOSI, -1 /* CS handled manually */);

  setupEspNow();

  xTaskCreatePinnedToCore(dacOutputTask, "dacOutput", 4096, nullptr, 2, nullptr, 1);

  Serial.println("ILDA receiver ready.");
}

void loop() {
  static uint32_t lastStatus = 0;
  if (millis() - lastStatus > 1000) {
    lastStatus = millis();
    bool linkDead = (millis() - lastPacketMillis) > LINK_TIMEOUT_MS;
    digitalWrite(PIN_STATUS_LED, linkDead ? LOW : HIGH);
    Serial.printf("link=%s pattern=%u shutter=%u brightness=%u pps=%u frontCount=%u\n",
                  linkDead ? "DOWN" : "up", static_cast<unsigned>(g_patternId),
                  static_cast<unsigned>(g_shutterOpen), static_cast<unsigned>(g_brightness),
                  static_cast<unsigned>(g_pointsPerSecond), static_cast<unsigned>(frontCount));
  }
  delay(50);
}
