// ESP32 ILDA sender ("controller") node.
//
// Generates simple test-pattern vector graphics locally and streams them to
// the receiver node over ESP-NOW, along with shutter/brightness/pattern/rate
// control state. Driven interactively over USB serial - see the `help`
// command below, or docs/PROTOCOL.md for the wire format if you want to
// drive it from something other than a terminal.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <math.h>

#include "config.h"
#include "ilda_protocol.h"

using namespace ilda;

static constexpr size_t kMaxPatternPoints = 512;
static IldaPoint g_patternPoints[kMaxPatternPoints];
static size_t g_patternCount = 0;

static uint8_t g_shutterOpen = 0;
static uint8_t g_brightness = DEFAULT_BRIGHTNESS;
static uint8_t g_patternId = PATTERN_BLANK;
static uint16_t g_pointsPerSecond = DEFAULT_POINTS_PER_SECOND;
static uint16_t g_frameId = 0;

static uint8_t g_color_r = 255, g_color_g = 255, g_color_b = 255;

// ---------------------------------------------------------------------------
// Local pattern generation. These fill g_patternPoints/g_patternCount; the
// receiver just displays whatever it's told (it also has its own built-in
// PATTERN_TEST_* generators for wiring checks independent of this).
// ---------------------------------------------------------------------------
static constexpr int16_t kRadius = 1800;

static void genCircle() {
  g_patternCount = 180;
  for (size_t i = 0; i < g_patternCount; i++) {
    float t = i * (2.0f * PI / g_patternCount);
    g_patternPoints[i] = {static_cast<int16_t>(kRadius * cosf(t)),
                           static_cast<int16_t>(kRadius * sinf(t)),
                           g_color_r, g_color_g, g_color_b};
  }
}

static void genSquare() {
  g_patternCount = 160;
  size_t perSide = g_patternCount / 4;
  size_t i = 0;
  for (size_t s = 0; s < perSide; s++, i++) {
    int16_t d = static_cast<int16_t>(-kRadius + (2 * kRadius) * s / perSide);
    g_patternPoints[i] = {d, static_cast<int16_t>(-kRadius), g_color_r, g_color_g, g_color_b};
  }
  for (size_t s = 0; s < perSide; s++, i++) {
    int16_t d = static_cast<int16_t>(-kRadius + (2 * kRadius) * s / perSide);
    g_patternPoints[i] = {static_cast<int16_t>(kRadius), d, g_color_r, g_color_g, g_color_b};
  }
  for (size_t s = 0; s < perSide; s++, i++) {
    int16_t d = static_cast<int16_t>(kRadius - (2 * kRadius) * s / perSide);
    g_patternPoints[i] = {d, static_cast<int16_t>(kRadius), g_color_r, g_color_g, g_color_b};
  }
  for (size_t s = 0; s < perSide; s++, i++) {
    int16_t d = static_cast<int16_t>(kRadius - (2 * kRadius) * s / perSide);
    g_patternPoints[i] = {static_cast<int16_t>(-kRadius), d, g_color_r, g_color_g, g_color_b};
  }
  g_patternCount = i;
}

static void genCross() {
  size_t half = 80;
  g_patternCount = half * 4;
  size_t i = 0;
  for (size_t s = 0; s < half; s++, i++) {
    int16_t d = static_cast<int16_t>(-kRadius + (2 * kRadius) * s / half);
    g_patternPoints[i] = {d, 0, g_color_r, g_color_g, g_color_b};
  }
  for (size_t s = 0; s < half; s++, i++) {
    int16_t d = static_cast<int16_t>(kRadius - (2 * kRadius) * s / half);
    g_patternPoints[i] = {d, 0, g_color_r, g_color_g, g_color_b};
  }
  for (size_t s = 0; s < half; s++, i++) {
    int16_t d = static_cast<int16_t>(-kRadius + (2 * kRadius) * s / half);
    g_patternPoints[i] = {0, d, g_color_r, g_color_g, g_color_b};
  }
  for (size_t s = 0; s < half; s++, i++) {
    int16_t d = static_cast<int16_t>(kRadius - (2 * kRadius) * s / half);
    g_patternPoints[i] = {0, d, g_color_r, g_color_g, g_color_b};
  }
  g_patternCount = i;
}

static void genLine() {
  g_patternCount = 100;
  for (size_t i = 0; i < g_patternCount; i++) {
    int16_t d = static_cast<int16_t>(-kRadius + (2 * kRadius) * i / (g_patternCount - 1));
    g_patternPoints[i] = {d, 0, g_color_r, g_color_g, g_color_b};
  }
}

// ---------------------------------------------------------------------------
// ESP-NOW send helpers
// ---------------------------------------------------------------------------
static uint8_t g_destAddr[6];

static void sendFrame() {
  if (g_patternId != PATTERN_STREAM || g_patternCount == 0) return;
  g_frameId++;

  uint16_t offset = 0;
  while (offset < g_patternCount) {
    uint8_t count = static_cast<uint8_t>(
        min(g_patternCount - offset, MAX_POINTS_PER_CHUNK));

    uint8_t packet[sizeof(FrameChunkHeader) + MAX_POINTS_PER_CHUNK * sizeof(IldaPoint)];
    FrameChunkHeader hdr{};
    hdr.type = PKT_FRAME_CHUNK;
    hdr.version = PROTOCOL_VERSION;
    hdr.frameId = g_frameId;
    hdr.totalPoints = static_cast<uint16_t>(g_patternCount);
    hdr.pointOffset = offset;
    hdr.pointCount = count;
    hdr.checksum = checksum8(reinterpret_cast<const uint8_t*>(&g_patternPoints[offset]),
                              count * sizeof(IldaPoint));

    memcpy(packet, &hdr, sizeof(hdr));
    memcpy(packet + sizeof(hdr), &g_patternPoints[offset], count * sizeof(IldaPoint));

    esp_now_send(g_destAddr, packet, sizeof(hdr) + count * sizeof(IldaPoint));
    offset += count;
  }
}

static void sendControl() {
  ControlMessage msg{};
  msg.type = PKT_CONTROL;
  msg.version = PROTOCOL_VERSION;
  msg.shutterOpen = g_shutterOpen;
  msg.brightness = g_brightness;
  msg.patternId = g_patternId;
  msg.pointsPerSecond = g_pointsPerSecond;
  msg.checksum = checksum8(reinterpret_cast<const uint8_t*>(&msg), sizeof(msg) - sizeof(msg.checksum));
  esp_now_send(g_destAddr, reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
}

// ---------------------------------------------------------------------------
// Serial command interface
// ---------------------------------------------------------------------------
static void printHelp() {
  Serial.println(F(
      "Commands:\n"
      "  pattern <blank|stream|circle|square|cross|line|test_circle|test_square|test_cross>\n"
      "  shutter <open|closed>\n"
      "  brightness <0-255>\n"
      "  color <r> <g> <b>            (0-255 each, applied to the next locally-generated pattern)\n"
      "  pps <100-30000>              (points per second)\n"
      "  status\n"
      "  help\n"));
}

static void printStatus() {
  Serial.printf(
      "pattern=%u shutter=%u brightness=%u pps=%u points=%u color=(%u,%u,%u)\n",
      static_cast<unsigned>(g_patternId), static_cast<unsigned>(g_shutterOpen),
      static_cast<unsigned>(g_brightness), static_cast<unsigned>(g_pointsPerSecond),
      static_cast<unsigned>(g_patternCount), static_cast<unsigned>(g_color_r),
      static_cast<unsigned>(g_color_g), static_cast<unsigned>(g_color_b));
}

static void handleCommand(String line) {
  line.trim();
  if (line.length() == 0) return;

  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String rest = sp < 0 ? String("") : line.substring(sp + 1);
  cmd.toLowerCase();

  if (cmd == "help" || cmd == "?") {
    printHelp();
  } else if (cmd == "status") {
    printStatus();
  } else if (cmd == "shutter") {
    rest.trim();
    rest.toLowerCase();
    g_shutterOpen = (rest == "open") ? 1 : 0;
    Serial.printf("shutter=%s\n", g_shutterOpen ? "open" : "closed");
  } else if (cmd == "brightness") {
    int v = rest.toInt();
    g_brightness = static_cast<uint8_t>(constrain(v, 0, 255));
    Serial.printf("brightness=%u\n", static_cast<unsigned>(g_brightness));
  } else if (cmd == "pps") {
    int v = rest.toInt();
    g_pointsPerSecond = static_cast<uint16_t>(constrain(v, 100, 30000));
    Serial.printf("pps=%u\n", static_cast<unsigned>(g_pointsPerSecond));
  } else if (cmd == "color") {
    int r, g, b;
    if (sscanf(rest.c_str(), "%d %d %d", &r, &g, &b) == 3) {
      g_color_r = static_cast<uint8_t>(constrain(r, 0, 255));
      g_color_g = static_cast<uint8_t>(constrain(g, 0, 255));
      g_color_b = static_cast<uint8_t>(constrain(b, 0, 255));
      Serial.printf("color=(%u,%u,%u)\n", static_cast<unsigned>(g_color_r),
                    static_cast<unsigned>(g_color_g), static_cast<unsigned>(g_color_b));
    } else {
      Serial.println("usage: color <r> <g> <b>");
    }
  } else if (cmd == "pattern") {
    rest.trim();
    rest.toLowerCase();
    if (rest == "blank") {
      g_patternId = PATTERN_BLANK;
    } else if (rest == "test_circle") {
      g_patternId = PATTERN_TEST_CIRCLE;
    } else if (rest == "test_square") {
      g_patternId = PATTERN_TEST_SQUARE;
    } else if (rest == "test_cross") {
      g_patternId = PATTERN_TEST_CROSS;
    } else if (rest == "circle") {
      genCircle();
      g_patternId = PATTERN_STREAM;
    } else if (rest == "square") {
      genSquare();
      g_patternId = PATTERN_STREAM;
    } else if (rest == "cross") {
      genCross();
      g_patternId = PATTERN_STREAM;
    } else if (rest == "line") {
      genLine();
      g_patternId = PATTERN_STREAM;
    } else {
      Serial.println("unknown pattern - try 'help'");
      return;
    }
    Serial.printf("pattern=%u points=%u\n", static_cast<unsigned>(g_patternId),
                  static_cast<unsigned>(g_patternCount));
    sendFrame();
  } else {
    Serial.println("unknown command - try 'help'");
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

  esp_now_peer_info_t peer{};
  peer.channel = WIFI_CHANNEL;
  peer.ifidx = WIFI_IF_STA;

#if ESPNOW_ENCRYPT
  esp_now_set_pmk(ESPNOW_PMK);
  memcpy(peer.peer_addr, PEER_MAC_ADDR, 6);
  memcpy(peer.lmk, ESPNOW_LMK, 16);
  peer.encrypt = true;
  memcpy(g_destAddr, PEER_MAC_ADDR, 6);
#else
  uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  memcpy(peer.peer_addr, broadcast, 6);
  peer.encrypt = false;
  memcpy(g_destAddr, broadcast, 6);
#endif

  esp_now_add_peer(&peer);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  setupEspNow();
  printHelp();
  printStatus();
}

void loop() {
  static String lineBuf;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (lineBuf.length() > 0) {
        handleCommand(lineBuf);
        lineBuf = "";
      }
    } else {
      lineBuf += c;
    }
  }

  static uint32_t lastFrameSend = 0;
  static uint32_t lastControlSend = 0;
  uint32_t now = millis();

  if (now - lastFrameSend >= FRAME_RESEND_INTERVAL_MS) {
    lastFrameSend = now;
    sendFrame();
  }
  if (now - lastControlSend >= CONTROL_SEND_INTERVAL_MS) {
    lastControlSend = now;
    sendControl();
  }
}
