"""Read and write an AIR CHECK's configuration over the USB-C serial console.

The device keeps everything the user can change in one NVS blob, so nothing in
normal use needs a firmware rebuild.  This tool talks to the console that the
firmware exposes over USB Serial/JTAG.

    python3 tools/configuration/aircheck_config.py --port /dev/tty.usbmodem* get
    python3 tools/configuration/aircheck_config.py --port ... set name "3D Printer"
    python3 tools/configuration/aircheck_config.py --port ... set default_mode NORMAL
    python3 tools/configuration/aircheck_config.py --port ... baseline-reset
    python3 tools/configuration/aircheck_config.py --port ... events
    python3 tools/configuration/aircheck_config.py --port ... frc 425

The console only runs while the device has USB power - which it does, since
this tool needs the cable.

Every setting is validated on the device as well: ac_config_validate() clamps
anything the sensor datasheets do not allow, so a typo here cannot produce a
measurement schedule the energy model never accounted for.
"""
from __future__ import annotations

import argparse
import sys
import time

SETTINGS = {
    "name":                 ("string", "published as Matter NodeLabel; Apple Home keeps its own name"),
    "location":             ("string", "free text"),
    "default_mode":         ("ECO|NORMAL|ACTIVE|POST_PRINT|CONTINUOUS", "the mode to fall back to"),
    "pm25_elevated":        ("ug/m3", "threshold for ELEVATED"),
    "pm25_high":            ("ug/m3", "threshold for HIGH"),
    "pm25_very_high":       ("ug/m3", "threshold for VERY HIGH"),
    "voc_elevated":         ("index", "VOC index threshold, 100 = 24 h average"),
    "co2_elevated":         ("ppm", "CO2 threshold"),
    "ev_sensitivity":       ("1..5", "event detection; 3 reproduces the defaults"),
    "post_event_s":         ("s", "how long POST_PRINT lasts"),
    "led_show_air_quality": ("bool", "a short press flashes the air quality colour"),
    "battery_interval_s":   ("s", "how often the battery voltage is read (60..3600)"),
    "voc_publish_index_as_ppb": ("bool", "publish the VOC index as a Matter number"),
    "co2_self_calibration": ("bool", "Sunrise ABC: 180 h background calibration (needs fresh air ~1x/week)"),
    "altitude_m":           ("m", "site altitude; CO2 pressure compensation (1.6 %/kPa)"),
    "auto_escalate":        ("bool", "switch to ACTIVE on a detected event"),
}


def open_port(path: str, baud: int = 115200):
    try:
        import serial
    except ImportError:
        print("this needs pyserial:  pip install pyserial", file=sys.stderr)
        raise SystemExit(2)
    return serial.Serial(path, baud, timeout=2)


def command(port, line: str) -> str:
    port.write((line + "\n").encode())
    port.flush()
    time.sleep(0.4)
    out = []
    while port.in_waiting:
        out.append(port.read(port.in_waiting).decode("utf-8", "replace"))
        time.sleep(0.1)
    return "".join(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=False, help="serial device")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list", help="list the settings and what they mean")
    sub.add_parser("get", help="read the whole configuration")
    s = sub.add_parser("set", help="change one setting")
    s.add_argument("key"); s.add_argument("value")
    sub.add_parser("baseline-reset", help="adopt the current air as the baseline")
    sub.add_parser("events", help="dump the stored event log")
    sub.add_parser("diag", help="dump the diagnostics")
    f = sub.add_parser("frc", help="fresh-air CO2 calibration - outdoors only")
    f.add_argument("ppm", nargs="?", default="425")
    a = ap.parse_args()

    if a.cmd == "list":
        w = max(len(k) for k in SETTINGS)
        for k, (unit, why) in SETTINGS.items():
            print(f"  {k:<{w}}  {unit:<28}  {why}")
        return 0

    if not a.port:
        print("--port is required for this command", file=sys.stderr)
        return 2
    port = open_port(a.port)

    if a.cmd == "get":
        print(command(port, "config get"))
    elif a.cmd == "set":
        if a.key not in SETTINGS:
            print(f"unknown setting {a.key!r}; try `list`", file=sys.stderr)
            return 2
        print(command(port, f"config set {a.key} {a.value}"))
    elif a.cmd == "baseline-reset":
        print("This sets the CURRENT air as the reference. It does not calibrate")
        print("any sensor. Make sure the room is in its normal state.")
        if input("continue? [y/N] ").strip().lower() != "y":
            return 1
        print(command(port, "baseline reset"))
    elif a.cmd == "events":
        print(command(port, "events dump"))
    elif a.cmd == "diag":
        print(command(port, "diag"))
    elif a.cmd == "frc":
        print("This rewrites the Sunrise's CO2 calibration. The device must be")
        print("OUTDOORS or at a wide-open window, away from people and traffic.")
        print("It runs 3 minutes first; the LED blinks cyan, then green or red.")
        if input("continue? [y/N] ").strip().lower() != "y":
            return 1
        print(command(port, f"co2 frc {a.ppm}"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
