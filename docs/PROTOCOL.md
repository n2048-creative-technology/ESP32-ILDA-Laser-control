# ESP-NOW protocol

Wire format lives in `firmware/common/ilda_protocol/ilda_protocol.h`, shared
by both firmware targets so they can't drift apart. This doc explains the
design; the header is the source of truth for exact byte layout.

## Packet types

Every packet's first byte is a `PacketType`:

- **`PKT_FRAME_CHUNK` (0x01)** - a slice of a frame's point data. A full
  frame (up to `MAX_POINTS_BUFFERED`, default 2000, points) is split across
  multiple packets because ESP-NOW payloads are capped at 250 bytes and a
  frame won't fit in one. Each chunk carries:
  - `frameId` - increments per full frame sent; lets the receiver detect a
    new frame starting mid-transfer of an old one.
  - `totalPoints` / `pointOffset` / `pointCount` - where this chunk fits in
    the full frame.
  - `checksum` - `checksum8()` over just the point bytes in this chunk.

  The receiver reassembles chunks into a back buffer and only swaps it in
  as the displayed frame once `pointOffset + pointCount` for all received
  chunks covers `totalPoints`. A malformed/truncated/checksum-failing chunk
  is dropped entirely rather than partially applied.

- **`PKT_CONTROL` (0x02)** - shutter/brightness/pattern/point-rate state.
  Sent repeatedly (every `CONTROL_SEND_INTERVAL_MS`, default 100ms) rather
  than only on change, so it doubles as a link-alive heartbeat: the
  receiver's `LINK_TIMEOUT_MS` watchdog resets on **any** valid packet, not
  just control ones, but control messages are the cheapest way to keep that
  watchdog fed when the displayed pattern isn't otherwise changing.

## Why frames are re-sent continuously

Rather than "send once, receiver keeps showing it forever," the sender
re-sends the current frame in full every `FRAME_RESEND_INTERVAL_MS` (default
200ms), whether or not it changed. Two reasons:

1. If the receiver reboots (power blip, crash) mid-show, it re-syncs within
   one interval instead of staying blank until the sender's content next
   changes.
2. It's a second, redundant link-alive signal alongside the control
   heartbeat.

The tradeoff is constant low-rate traffic even for a static image - at 200ms
intervals with typical pattern sizes (a few hundred points) this is a small
fraction of the ESP-NOW channel's capacity, so it's a reasonable default,
but you can raise the interval in `firmware/sender/include/config.h` if you
add much larger frames.

## Checksums vs. encryption

`checksum8()` is a simple additive checksum, not a cryptographic MAC. Its
only job is to catch truncated/corrupted packets before they reach the DAC
output path - it does not protect against a malicious sender crafting a
valid-looking packet.

For actual authentication/confidentiality, ESP-NOW supports AES-CCM
encryption between paired peers. Both firmwares have this wired up but
disabled by default (`ESPNOW_ENCRYPT 0` in each `config.h`) so you can get a
broadcast link working with zero pairing first. To enable it:

1. Generate your own random 16-byte PMK and LMK (don't reuse the placeholder
   values in the repo).
2. Set `ESPNOW_ENCRYPT 1` in **both** `firmware/receiver/include/config.h`
   and `firmware/sender/include/config.h`, with matching `ESPNOW_PMK`.
3. Set each side's `PEER_MAC_ADDR` to the *other* device's actual STA MAC
   address (`WiFi.macAddress()` at boot) - encrypted ESP-NOW peers must be
   unicast, not broadcast.

## Point coordinate convention

`IldaPoint.x`/`.y` are signed values in `[-2048, 2047]`, with `0` meaning
galvo center. This matches the MCP4922's 12-bit range directly (the
receiver just adds 2048 before writing to the DAC) and keeps "centered/off"
representable as the simple, safe all-zero point used by the link-loss
watchdog.
