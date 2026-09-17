"""Generate the commissioning label for a device.

The label carries what someone needs to add the unit to Apple Home and to tell
two identical units apart:

    device name, Matter QR code, manual pairing code, serial number

It is generated from the device's *actual* commissioning payload, which you
read off the serial console on first boot.  Nothing here invents a passcode:
pass in what the device printed.

    python3 tools/commissioning/make_label.py \
        --manual 3497-011-2332 \
        --qr "MT:Y.K90AFN00KA0648G00" \
        --serial AC-3F2A-91C4 \
        --name "3D Printer" \
        --out label.svg

For anything beyond your own workshop, generate per-device passcodes and
discriminators with esp-matter's mfg_tool and flash them into the `fctry`
partition - a fleet sharing one passcode is a fleet with no security.
"""
from __future__ import annotations

import argparse
import html
import sys

LABEL_W_MM = 54.0
LABEL_H_MM = 34.0


def qr_modules(payload: str):
    """Return the QR code as a list of rows of booleans, or None.

    Uses the `segno` package when it is available.  It deliberately does not
    fall back to drawing something QR-shaped: a label with a decorative square
    that does not scan is worse than a label that says the code is missing.
    """
    try:
        import segno
    except ImportError:
        return None
    qr = segno.make(payload, error="m")
    return [[bool(m) for m in row] for row in qr.matrix]


def svg(name: str, manual: str, qr: str, serial: str) -> str:
    mods = qr_modules(qr)
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{LABEL_W_MM}mm" '
        f'height="{LABEL_H_MM}mm" viewBox="0 0 {LABEL_W_MM} {LABEL_H_MM}">',
        '<rect width="100%" height="100%" fill="#fff"/>',
        f'<text x="3" y="6" font-family="Helvetica,Arial" font-size="3.6" '
        f'font-weight="bold">{html.escape(name)}</text>',
        '<text x="3" y="10" font-family="Helvetica,Arial" font-size="2.4" '
        'fill="#555">3D Printing AIR CHECK</text>',
    ]

    if mods:
        n = len(mods)
        size = 20.0
        cell = size / n
        x0, y0 = LABEL_W_MM - size - 3, 3
        parts.append(f'<rect x="{x0-1:.2f}" y="{y0-1:.2f}" width="{size+2:.2f}" '
                     f'height="{size+2:.2f}" fill="#fff"/>')
        for r, row in enumerate(mods):
            for c, on in enumerate(row):
                if on:
                    parts.append(
                        f'<rect x="{x0 + c*cell:.3f}" y="{y0 + r*cell:.3f}" '
                        f'width="{cell:.3f}" height="{cell:.3f}" fill="#000"/>')
    else:
        parts.append(
            f'<text x="{LABEL_W_MM-23:.1f}" y="14" font-family="Helvetica,Arial" '
            f'font-size="2.2" fill="#a00">QR needs `pip install segno`</text>')

    parts += [
        f'<text x="3" y="19" font-family="Helvetica,Arial" font-size="2.2" '
        f'fill="#555">Manual pairing code</text>',
        f'<text x="3" y="24" font-family="Helvetica,Arial" font-size="4.2" '
        f'font-weight="bold" letter-spacing="0.3">{html.escape(manual)}</text>',
        f'<text x="3" y="29" font-family="Helvetica,Arial" font-size="2.4">'
        f'{html.escape(serial)}</text>',
        f'<text x="3" y="32.5" font-family="Helvetica,Arial" font-size="2.0" '
        f'fill="#777">Needs a HomePod or Apple TV as Thread border router</text>',
        "</svg>",
    ]
    return "\n".join(parts)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--manual", required=True, help="11 digit manual pairing code")
    ap.add_argument("--qr", required=True, help="MT:... payload from the console")
    ap.add_argument("--serial", required=True)
    ap.add_argument("--name", default="AIR CHECK")
    ap.add_argument("--out", default="label.svg")
    a = ap.parse_args()

    if not a.qr.startswith("MT:"):
        print("warning: a Matter QR payload normally starts with 'MT:'",
              file=sys.stderr)

    with open(a.out, "w") as f:
        f.write(svg(a.name, a.manual, a.qr, a.serial))
    print(f"wrote {a.out} ({LABEL_W_MM:.0f} x {LABEL_H_MM:.0f} mm)")
    if qr_modules(a.qr) is None:
        print("note: install `segno` for a real QR code: pip install segno")
    return 0


if __name__ == "__main__":
    sys.exit(main())
