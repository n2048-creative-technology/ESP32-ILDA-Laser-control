#!/usr/bin/env python3
"""Hardware-in-the-loop test for the sender <-> receiver ESP-NOW link.

Requires two flashed ESP32 boards (sender + receiver, see
firmware/sender and firmware/receiver) each connected over USB, and
`pip install pyserial`.

This does NOT verify DAC output voltages or ILDA wiring - it only checks
that the two firmwares are actually talking to each other and that the
receiver's link-loss watchdog behaves as documented in docs/SAFETY.md.
Run it after flashing new firmware to either board, before doing anything
with a real projector attached.

Usage:
    python3 scripts/test_espnow_link.py --sender /dev/ttyUSB0 --receiver /dev/ttyUSB1
"""
import argparse
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is required: pip install pyserial")

STATUS_RE = re.compile(
    r"link=(?P<link>\w+) pattern=(?P<pattern>\d+) shutter=(?P<shutter>\d+) "
    r"brightness=(?P<brightness>\d+) pps=(?P<pps>\d+) frontCount=(?P<frontCount>\d+)"
)

PATTERN_STREAM = 1


class Fail(Exception):
    pass


def send_cmd(ser: "serial.Serial", cmd: str) -> None:
    ser.write((cmd + "\n").encode("ascii"))
    ser.flush()


def wait_for_status(ser: "serial.Serial", predicate, timeout: float, description: str):
    """Reads lines from `ser` until one matches STATUS_RE and satisfies
    `predicate(fields)`, or `timeout` seconds elapse."""
    deadline = time.monotonic() + timeout
    last_seen = None
    while time.monotonic() < deadline:
        line = ser.readline().decode("ascii", errors="replace").strip()
        if not line:
            continue
        m = STATUS_RE.search(line)
        if not m:
            continue
        fields = m.groupdict()
        last_seen = fields
        if predicate(fields):
            return fields
    raise Fail(f"timed out waiting for: {description} (last status seen: {last_seen})")


def drain(ser: "serial.Serial", duration: float = 0.3) -> None:
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        ser.readline()


def run(sender_port: str, receiver_port: str, baud: int, timeout: float) -> None:
    with serial.Serial(sender_port, baud, timeout=0.3) as sender, \
         serial.Serial(receiver_port, baud, timeout=0.3) as receiver:

        # Boards may already be mid-boot; give them a moment and clear
        # whatever's buffered.
        time.sleep(0.5)
        drain(sender)
        drain(receiver)

        print("[1/4] Starting a stream pattern and confirming the receiver sees it...")
        send_cmd(sender, "resume")
        send_cmd(sender, "color 255 255 255")
        send_cmd(sender, "pattern circle")
        send_cmd(sender, "shutter open")
        fields = wait_for_status(
            receiver,
            lambda f: f["link"] == "up" and int(f["pattern"]) == PATTERN_STREAM
            and int(f["shutter"]) == 1 and int(f["frontCount"]) > 0,
            timeout,
            "receiver reporting link=up, pattern=stream, shutter=open, frontCount>0",
        )
        print(f"    OK: {fields}")

        print("[2/4] Closing the shutter and confirming the receiver reflects it...")
        send_cmd(sender, "shutter closed")
        fields = wait_for_status(
            receiver,
            lambda f: int(f["shutter"]) == 0,
            timeout,
            "receiver reporting shutter=closed",
        )
        print(f"    OK: {fields}")

        print("[3/4] Halting sender transmission and confirming the receiver's "
              "link-loss watchdog trips...")
        send_cmd(sender, "halt")
        fields = wait_for_status(
            receiver,
            lambda f: f["link"] == "DOWN",
            timeout + 1.0,  # watchdog timeout + one receiver status interval
            "receiver reporting link=DOWN after sender halt",
        )
        print(f"    OK: {fields}")

        print("[4/4] Resuming sender transmission and confirming the link recovers...")
        send_cmd(sender, "resume")
        fields = wait_for_status(
            receiver,
            lambda f: f["link"] == "up",
            timeout,
            "receiver reporting link=up after sender resume",
        )
        print(f"    OK: {fields}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--sender", required=True, help="Serial port for the sender board")
    parser.add_argument("--receiver", required=True, help="Serial port for the receiver board")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0, help="Per-step timeout in seconds")
    args = parser.parse_args()

    try:
        run(args.sender, args.receiver, args.baud, args.timeout)
    except Fail as e:
        print(f"FAIL: {e}", file=sys.stderr)
        return 1
    except serial.SerialException as e:
        print(f"FAIL: serial error: {e}", file=sys.stderr)
        return 1

    print("PASS: sender/receiver ESP-NOW link and watchdog behave as expected.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
