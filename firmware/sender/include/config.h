// Configuration for the sender ("controller") node.
#pragma once

#include <stdint.h>

// Must match the receiver's WIFI_CHANNEL.
#define WIFI_CHANNEL 1

// See firmware/receiver/include/config.h for what this toggles. Both nodes
// must agree: either both ESPNOW_ENCRYPT 0, or both 1 with matching
// PMK/LMK/MAC below.
#define ESPNOW_ENCRYPT 0

static const uint8_t PEER_MAC_ADDR[6] = {0xAA, 0xBB, 0xCC, 0x00, 0x00, 0x02};
static const uint8_t ESPNOW_PMK[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
static const uint8_t ESPNOW_LMK[16] = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
                                        0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20};

// How often the current frame is re-sent in full. This is deliberately not
// event-driven: continuous retransmission at a modest rate doubles as the
// receiver's link-loss heartbeat and means a receiver that reboots mid-show
// re-syncs within one interval instead of staying blank until content changes.
#define FRAME_RESEND_INTERVAL_MS 200

// Control state (shutter/brightness/pattern/pps) is sent this often,
// independent of frame data.
#define CONTROL_SEND_INTERVAL_MS 100

#define DEFAULT_POINTS_PER_SECOND 15000
#define DEFAULT_BRIGHTNESS 255
