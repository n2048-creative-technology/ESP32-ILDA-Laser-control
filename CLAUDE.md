# Working rules for this repo

This is a hardware project (ESP32 firmware + an analog electronics front
end for a physical laser projector). The rules below exist because
"looks correct" and "builds and runs correctly" are different claims, and
the gap between them matters more here than in most software projects -
mistakes drive a real laser.

## Firmware (PlatformIO / ESP32)

- **Never claim firmware "should work" without building it.** After any
  change to `firmware/*/src`, `firmware/*/include`, `firmware/common`, or a
  `platformio.ini`, run `pio run` for every affected environment before
  reporting the change as done. A change that hasn't been built is not
  finished - say so explicitly rather than implying otherwise.
  - From a firmware project directory: `pio run` (or `pio run -e <env>` for
    a specific environment).
  - `scripts/build_all.sh` builds every firmware target in this repo in one
    shot - prefer it when more than one target could be affected (e.g. a
    change to `firmware/common/ilda_protocol`).
  - If a build cannot actually be run (no network access to PlatformIO's
    package registry, no toolchain installed, etc.), say so plainly and
    explain what's blocking it instead of presenting untested code as
    verified. Don't quietly skip the build and let it pass as implied.
- **Check library/framework compatibility before adding a dependency.**
  Before adding anything to `lib_deps`, confirm it supports the `framework`
  and `platform` declared in that project's `platformio.ini` (this repo
  uses `framework = arduino` on `platform = espressif32`) and the specific
  board/chip variant (ESP32 classic vs. S2/S3/C3 have different
  peripheral support - e.g. DAC/touch availability, SPI pin defaults). A
  library that only lists ESP-IDF or a different MCU family is not a valid
  choice here even if the API looks like a fit.
- **Match warnings seriously.** Both firmware projects build with
  `-Wall -Wextra`. Don't add code that introduces new warnings (implicit
  narrowing, signed/unsigned comparisons, format-string mismatches, etc.)
  without a specific reason, and note the reason in a comment if it's not
  obvious.

## Automated testing

Every piece of behavior needs a way to verify it that doesn't rely on
"reading the code and believing it" - a bug in reasoning about embedded
timing/concurrency looks exactly like correct code until it's exercised.
Pick whichever fits the situation, and add new tooling under `scripts/`
when an existing option doesn't fit:

- **Serial monitor output**: firmware should log enough state (link
  up/down, current mode, key transitions) that `pio device monitor` or a
  scripted serial read tells you whether it's doing the right thing, not
  just that it's running. Prefer plain, greppable log lines over decorative
  output.
- **Scripted host-side tests**: for anything with expected request/response
  or timing behavior, prefer a small script (see
  `scripts/test_espnow_link.py` for the pattern used in this repo) that
  drives one device over serial and asserts on what the other device
  reports, over asking a human to eyeball two terminal windows.
- **Multi-device communication**: whenever two or more devices are
  supposed to talk to each other (ESP-NOW here, but the same applies to any
  future radio/bus link), test the actual link, not each side in isolation.
  Confirm the specific thing that would fail silently: a receiver believing
  it's fine while actually hearing nothing (e.g. this project's link-loss
  watchdog and its timeout), a sender assuming delivery when there is no
  ack, protocol version mismatches, etc.
- When hardware isn't available to actually run a test (as in a cloud
  sandbox with no attached ESP32), say so explicitly, and still write the
  test/tooling so the person with hardware can run it - don't skip writing
  the test just because it can't be executed right now.

## Documentation and repo structure

Every project in this account should carry the same shape, so a new
contributor (or a fresh Claude session) can navigate any of them the same
way:

- **`README.md`** at the repo root - what the project is, current status,
  build/flash instructions, and how to run whatever automated tests exist.
  Keep it current when the structure or workflow changes.
- **`docs/`** - datasheets (PDFs are fine to commit if not huge) and any
  documentation more detailed than the README: protocol specs, safety
  notes, reference designs, pinouts.
- **`firmware/`** - PlatformIO project(s).
- **`kicad/`** - KiCad schematic/PCB projects.
- **`openscad/`** - OpenSCAD source for parametric mechanical parts.
- **`blender/`** - Blender project files for models/renders that aren't
  parametric OpenSCAD sources.
- **`3d_models/`** - exported/printable model files (STL/STEP/etc.),
  separate from the OpenSCAD/Blender sources that generate them.
- **`images/`** - photos, renders, diagrams not embedded elsewhere.
- **`scripts/`** - build/test/automation tooling (shell, Python, etc.).
- **`tools/`** - standalone utility programs that are more than a script
  (e.g. a small host-side GUI or CLI companion app), as distinct from the
  one-off automation in `scripts/`.

Not every project needs every folder populated on day one - create the
ones that are actually relevant, but keep the same names/locations so the
layout is predictable across projects. Add a short `README.md` inside an
otherwise-empty folder explaining what belongs there rather than leaving it
with no explanation.

## This repo specifically

See `docs/SAFETY.md` before touching anything related to actual analog
output levels, the shutter line, or the interlock loop - this is a real
laser projector interface, not just a wireless demo.
