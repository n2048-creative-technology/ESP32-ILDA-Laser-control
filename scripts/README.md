# Scripts

- **`build_all.sh`** - runs `pio run` for every firmware project under
  `../firmware/` and reports a pass/fail summary. Run this after any
  firmware change before calling it done (see `../CLAUDE.md`).
- **`test_espnow_link.py`** - hardware-in-the-loop test that drives the
  sender over serial and asserts the receiver's serial status log reflects
  it correctly, including the link-loss watchdog (see the file's docstring
  for usage). Requires two flashed, USB-connected ESP32 boards and
  `pip install pyserial`. This does not check DAC output voltages/ILDA
  wiring - it only verifies the two firmwares are actually talking to each
  other.
