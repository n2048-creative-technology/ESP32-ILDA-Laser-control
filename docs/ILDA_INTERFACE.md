# ILDA DB25 interface reference

> **Verify before wiring.** The pinout below is the commonly published
> "standard" ILDA analog interface pinout used across most laser show
> projectors. Manufacturers occasionally deviate (different pin count in
> use, different polarity convention, vendor-specific extras). Cross-check
> this against the Laserworld DS-1000RGB manual and, ideally, probe the
> connector with a multimeter (projector powered, outputs unloaded) before
> connecting anything. Getting this wrong can damage the projector's input
> circuitry. See `docs/SAFETY.md` first.

## Typical ILDA DB25 pinout

| Pin | Signal      | Notes |
|-----|-------------|-------|
| 1   | Ground      | Shield/frame ground |
| 2   | X-          | Differential pair with pin 14 |
| 3   | Y-          | Differential pair with pin 15 |
| 4   | Red-        | Differential pair with pin 16 |
| 5   | Green-      | Differential pair with pin 17 |
| 6   | Blue-       | Differential pair with pin 18 |
| 7   | (Intensity- / spare on 3-color units) | |
| 8   | Ground      | |
| 9   | Ground      | |
| 10  | Ground      | |
| 11  | (spare)     | |
| 12  | (spare)     | |
| 13  | (spare)     | |
| 14  | X+          | |
| 15  | Y+          | |
| 16  | Red+        | |
| 17  | Green+      | |
| 18  | Blue+       | |
| 19  | (Intensity+ / spare on 3-color units) | |
| 20  | Shutter / blanking enable (TTL) | Often: high = beam enabled, low = forced blank |
| 21  | Interlock loop | Passive safety loop - see `docs/SAFETY.md`. Do NOT drive from the ESP32. |
| 22  | Interlock loop | |
| 23  | (spare)     | |
| 24  | (spare)     | |
| 25  | (spare)     | |

Many projectors (including a lot of the DIY/hobbyist ILDA interface designs
this project is modeled after) tolerate single-ended signals on the analog
lines - i.e. drive the `+` pin with the signal and tie the corresponding `-`
pin to signal ground rather than building a true differential driver. That's
the assumption this project's reference front end (`docs/HARDWARE.md`)
makes. If your DS-1000RGB specifically requires true differential drive,
you'll need to add a differential line driver stage (e.g. an op-amp
inverter per channel, or a dedicated differential driver IC) between the
front end described here and the connector.

## Signal semantics

- **X/Y**: analog position, galvo center at mid-scale. This project's
  firmware represents X/Y internally as signed 12-bit values (`-2048..2047`,
  0 = center) and expects the analog front end to convert that to whatever
  voltage swing the projector's galvo amplifiers expect (commonly around
  ±5V, but confirm for your unit).
- **R/G/B**: analog intensity per color channel, typically 0V (off) up to
  some max (often ~5V) for full intensity.
- **Shutter/blanking**: a TTL logic line, separate from the analog color
  channels, that the projector uses as a hard "beam off" signal independent
  of the RGB values. This project drives it low (closed) any time the
  firmware considers output unsafe/uninitialized (see `docs/SAFETY.md`), and
  otherwise follows the sender's shutter command.
- **Interlock loop**: not a data signal - a passive safety loop. Leave it to
  your venue/enclosure safety wiring, not the ESP32.

## Where this project's outputs come from

See `docs/HARDWARE.md` for how the ESP32's SPI DAC outputs map to these
pins, and the op-amp scaling stage needed to reach the projector's expected
voltage range.
