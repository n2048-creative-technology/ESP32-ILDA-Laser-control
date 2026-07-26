# Safety

**Read this before connecting anything to the DS-1000RGB.**

A laser projector like the Laserworld DS-1000RGB is almost certainly a
Class 3B or Class 4 laser product. Both classes can cause permanent eye
damage, Class 4 can also burn skin/materials and start fires. This project
gives a microcontroller direct analog control over where the beam points and
how bright it is - treat every step of building and testing it accordingly.

This repository is firmware and a reference analog front-end design, not a
certified safety system. Nothing here substitutes for:

- Following your local regulations for operating a laser show / laser
  projector (in the US: FDA/CDRH variance requirements; other jurisdictions
  have their own equivalents). This is a legal requirement in most places for
  anything beyond private, beam-contained bench testing.
- The projector's own built-in safety systems (scanner-fail/galvo-feedback
  protection, key switch, physical shutter). This project must not be used to
  bypass, disable, or "help around" any of them.
- Basic laser safety practice: enclosed beam paths while developing, laser
  safety glasses rated for the projector's wavelengths when the beam is not
  fully enclosed, no aiming at eye level, no unattended operation.

## Interlock loop

The ILDA interface includes a physical interlock loop pin pair intended for
an emergency-stop / door-interlock circuit external to the projector. **Do
not wire the interlock loop to the ESP32 or let this project's firmware
participate in it.** It should remain a passive, physical loop (closed with
a wire link, a key switch, or a real e-stop/interlock chain) exactly as the
projector's manual and your venue's safety requirements dictate. A software
watchdog is not a substitute for a hardware interlock.

## Firmware watchdog behavior

The receiver firmware (`firmware/receiver`) treats "no signal" as the
default, safe state:

- On boot, before Wi-Fi/ESP-NOW even initializes, the shutter TTL output is
  driven low (closed) and stays that way until a valid control packet says
  otherwise.
- If no valid ESP-NOW packet (frame or control) has been received for
  `LINK_TIMEOUT_MS` (500 ms by default, see `firmware/receiver/include/config.h`),
  the output task forces the shutter closed and centers the galvo outputs,
  regardless of whatever pattern was previously selected. This covers the
  sender losing power, moving out of range, or crashing.
- Malformed, truncated, or checksum-failing packets are dropped rather than
  partially applied.

This reduces the chance of "wireless link dies, beam parks in one spot at
full brightness," but it cannot detect every failure mode (e.g. the ESP32
itself hanging with the shutter line left high). Keep the projector's own
physical shutter/key switch as your actual last line of defense, and don't
leave the system running unattended.

## Before connecting to the real projector

1. Build and bench-test the analog front end (see `docs/HARDWARE.md`) with a
   multimeter/oscilloscope on the outputs, without the projector connected,
   and confirm the voltage swings are within the range your projector's ILDA
   input actually expects.
2. Confirm the DS-1000RGB's actual DB25 pinout against its manual - see the
   caveat at the top of `docs/ILDA_INTERFACE.md`. Do not assume the "typical"
   pinout listed there is exactly correct for your unit without checking.
3. Test with the beam blocked/enclosed or at minimum output power first.
4. Only then test at higher power, still with an enclosed beam path, before
   ever projecting a visible show.
