// Board/wiring configuration for the receiver ("ILDA output") node.
//
// All pin numbers assume a generic ESP32 DevKitC-style board. Change them to
// match your actual wiring; nothing else in main.cpp needs to change.
#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// Wi-Fi / ESP-NOW
// ---------------------------------------------------------------------------

// Fixed Wi-Fi channel used for ESP-NOW. Must match the sender's WIFI_CHANNEL.
#define WIFI_CHANNEL 1

// When 0 (default): the receiver accepts unencrypted ESP-NOW broadcasts from
// any sender on WIFI_CHANNEL. Fine for bench testing, but anyone in range on
// that channel can send it (unauthenticated) control/frame packets.
//
// When 1: the receiver only accepts encrypted unicast packets from
// PEER_MAC_ADDR, using PMK/LMK below. Generate your own random keys before
// using this in anything other than a bench test - see docs/PROTOCOL.md.
#define ESPNOW_ENCRYPT 0

static const uint8_t PEER_MAC_ADDR[6] = {0xAA, 0xBB, 0xCC, 0x00, 0x00, 0x01};
static const uint8_t ESPNOW_PMK[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
static const uint8_t ESPNOW_LMK[16] = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
                                        0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20};

// ---------------------------------------------------------------------------
// Analog front end: 3x MCP4922 dual 12-bit SPI DACs sharing one SPI bus.
// DAC1 = X/Y, DAC2 = R/G, DAC3 = B/spare. See docs/HARDWARE.md for the full
// reference design (level shifting, op-amp buffering, ILDA signal levels).
// ---------------------------------------------------------------------------

#define PIN_SPI_SCK 18
#define PIN_SPI_MOSI 23

#define PIN_DAC_CS_XY 5   // DAC1: channel A = X, channel B = Y
#define PIN_DAC_CS_RG 17  // DAC2: channel A = R, channel B = G
#define PIN_DAC_CS_B 16   // DAC3: channel A = B, channel B = spare (tied low)

// Tie the LDAC pin of ALL THREE DACs together to this single GPIO so every
// channel updates simultaneously (sample-and-hold synchronization). This is
// what keeps the galvos and color channels in step with each other.
#define PIN_DAC_LDAC 4

// ---------------------------------------------------------------------------
// Digital control lines
// ---------------------------------------------------------------------------

// Drives the ILDA "Shutter"/blanking-enable line (through the front end
// described in docs/HARDWARE.md). LOW = beam forced off.
#define PIN_SHUTTER_TTL 15

#define PIN_STATUS_LED 2

// ---------------------------------------------------------------------------
// Safety
// ---------------------------------------------------------------------------

// If no ESP-NOW packet (frame or control) arrives for this long, the output
// task forces the shutter closed and centers the galvos, regardless of
// whatever was last displayed. See docs/SAFETY.md.
#define LINK_TIMEOUT_MS 500

#define DEFAULT_POINTS_PER_SECOND 15000
#define MIN_POINTS_PER_SECOND 100
#define MAX_POINTS_PER_SECOND 30000

// Maximum points held in a single displayed frame. 2000 points * 7 bytes *
// 2 buffers = ~28KB, comfortably inside the ESP32's RAM.
#define MAX_POINTS_BUFFERED 2000
