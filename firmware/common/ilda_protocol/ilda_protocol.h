// Shared wire format for the ESP32 <-> ESP32 ESP-NOW link between the
// "sender" (controller) node and the "receiver" (ILDA output) node.
//
// Kept dependency-free (no Arduino headers) so it can be unit tested and
// included from both firmware targets unchanged.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ilda {

constexpr uint8_t PROTOCOL_VERSION = 1;

// Conservative ESP-NOW payload ceiling. The classic ESP-NOW API (used here
// via the Arduino-ESP32 core) caps a single send at 250 bytes regardless of
// IDF version, so we target that rather than the larger limits some
// newer/ESP-NOW-v2-only stacks allow.
constexpr size_t ESPNOW_MAX_PAYLOAD = 250;

enum PacketType : uint8_t {
  PKT_FRAME_CHUNK = 0x01,
  PKT_CONTROL = 0x02,
};

// Selects what the receiver actually outputs. PATTERN_STREAM shows whatever
// has been received via PKT_FRAME_CHUNK; the PATTERN_TEST_* entries are
// generated locally by the receiver so the analog front end / wiring can be
// verified without the sender running at all.
enum PatternId : uint8_t {
  PATTERN_BLANK = 0,
  PATTERN_STREAM = 1,
  PATTERN_TEST_SQUARE = 2,
  PATTERN_TEST_CIRCLE = 3,
  PATTERN_TEST_CROSS = 4,
};

#pragma pack(push, 1)

// One ILDA point. x/y are signed, DAC-centered coordinates in the range
// [-2048, 2047] (0 = galvo center). r/g/b are 8-bit color/intensity values
// scaled up to the DAC's native resolution by the receiver.
struct IldaPoint {
  int16_t x;
  int16_t y;
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

// A slice of a larger frame. Frames are split across multiple packets
// because a full frame will not fit ESP-NOW's payload limit; the receiver
// reassembles them by frameId/pointOffset before swapping display buffers.
struct FrameChunkHeader {
  uint8_t type;     // PKT_FRAME_CHUNK
  uint8_t version;  // PROTOCOL_VERSION
  uint16_t frameId;
  uint16_t totalPoints;
  uint16_t pointOffset;
  uint8_t pointCount;
  uint8_t checksum;  // checksum8() over the pointCount IldaPoints that follow
};

// Out-of-band control state. Sent repeatedly (not just on change) so it also
// serves as a link keepalive/heartbeat for the receiver's link-loss watchdog.
struct ControlMessage {
  uint8_t type;     // PKT_CONTROL
  uint8_t version;  // PROTOCOL_VERSION
  uint8_t shutterOpen;
  uint8_t brightness;  // 0-255, global scale applied to R/G/B
  uint8_t patternId;   // PatternId
  uint16_t pointsPerSecond;
  uint8_t checksum;  // checksum8() over the bytes preceding this field
};

#pragma pack(pop)

constexpr size_t MAX_POINTS_PER_CHUNK =
    (ESPNOW_MAX_PAYLOAD - sizeof(FrameChunkHeader)) / sizeof(IldaPoint);

// Deliberately not a cryptographic checksum: this only needs to catch
// truncated/malformed packets before they reach the DAC output path. Link
// integrity against tampering is handled separately via ESP-NOW's own
// optional AES-CCM peer encryption (see docs/PROTOCOL.md).
inline uint8_t checksum8(const uint8_t* data, size_t len) {
  uint8_t sum = 0xA5;
  for (size_t i = 0; i < len; i++) {
    sum = static_cast<uint8_t>(sum + data[i]);
  }
  return sum;
}

}  // namespace ilda
