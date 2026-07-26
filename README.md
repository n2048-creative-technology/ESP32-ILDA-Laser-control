# ESP32 ILDA Laser Control

Wireless (ESP-NOW) control of a Laserworld DS-1000RGB - or any laser
projector with a standard analog ILDA interface - using a pair of ESP32
boards:

- **sender** - a controller node you interact with over USB serial, that
  generates simple vector test patterns and streams them, plus
  shutter/brightness/point-rate commands, over ESP-NOW.
- **receiver** - sits at the projector, receives that stream, and drives an
  external SPI DAC bank feeding the ILDA port's analog X/Y/R/G/B inputs and
  TTL shutter line.

**Read `docs/SAFETY.md` before wiring anything to the projector.** This
controls a real laser; getting the wiring or voltage levels wrong can damage
the projector or, more importantly, be a genuine eye/fire hazard.

## Repository layout

```
firmware/
  common/ilda_protocol/   Shared ESP-NOW packet definitions (used by both targets)
  receiver/               PlatformIO project: the ILDA-output node
  sender/                 PlatformIO project: the controller node
docs/
  SAFETY.md               Read this first
  ILDA_INTERFACE.md        DB25 pinout reference (verify against your unit's manual)
  HARDWARE.md             DAC/op-amp analog front-end reference design + BOM
  PROTOCOL.md             ESP-NOW packet format and design rationale
```

## Status / scope

This currently targets locally-generated test-pattern vector graphics
(circle, square, cross, line) rather than loading arbitrary `.ild` show
files - the ESP-NOW protocol, frame buffering, and DAC output pipeline are
all real and driveable, but there's no `.ild` file parser yet. That's a
natural next step; see the code in `firmware/sender/src/main.cpp` for where
pattern data comes from if you want to add one.

## Quick start

Requires [PlatformIO](https://platformio.org/) (CLI or the VS Code
extension) and two ESP32 dev boards.

```sh
# Flash the receiver (the board wired to the DAC front end / ILDA port)
cd firmware/receiver
pio run -t upload -t monitor

# Flash the sender (the board you'll talk to over USB serial)
cd firmware/sender
pio run -t upload -t monitor
```

Both boards must agree on `WIFI_CHANNEL` (default channel 1, set in each
`include/config.h`) and, before wiring to a real projector, you'll want the
DAC/op-amp front end from `docs/HARDWARE.md` connected to the receiver.

Out of the box (no front end connected), you can still exercise the whole
link over the sender's serial console:

```
> pattern circle
> shutter open
> brightness 200
> pps 15000
> status
```

The receiver logs link/pattern state to its own serial console once a
second, and drives its shutter TTL/DAC outputs according to what it
receives - useful for confirming the wireless link and frame reassembly
before any hardware is attached; probe `PIN_SHUTTER_TTL` and the DAC outputs
with a meter/scope to confirm.

You can also make the receiver draw one of its own built-in calibration
patterns, independent of anything the sender streams, with:

```
> pattern test_circle
```

which is useful for checking the analog front end's wiring in isolation.

## Safety

Do not skip `docs/SAFETY.md`. In short: keep the projector's own interlock
and shutter systems intact and untouched by this project, verify voltage
levels on a bench before connecting to the projector, and don't operate a
real beam unattended or without appropriate laser safety precautions.
