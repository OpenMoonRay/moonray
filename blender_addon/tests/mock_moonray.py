#!/usr/bin/env python3
"""Fake moonray binary: emits \r-separated progress and writes a real EXR.

Used to test the add-on's renderer process plumbing without a real build.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mock_exr import write_exr  # noqa: E402


def main():
    args = sys.argv[1:]
    out = "/tmp/mock_moonray.exr"
    rdla = None
    if "-in" in args:
        rdla = args[args.index("-in") + 1]
    if "-out" in args:
        out = args[args.index("-out") + 1]

    # derive the render resolution from the RDLA scene variables
    w, h = 64, 64
    if rdla and os.path.isfile(rdla):
        import re
        with open(rdla) as f:
            text = f.read()
        mw = re.search(r'\["image_width"\]\s*=\s*(\d+)', text)
        mh = re.search(r'\["image_height"\]\s*=\s*(\d+)', text)
        if mw and mh:
            w, h = int(mw.group(1)), int(mh.group(1))

    total = 30
    for i in range(total + 1):
        pct = int(i * 100.0 / total)
        sys.stdout.write("\rRendering [%3d%%] %02d:%02d ETA" % (pct, 0, total - i))
        sys.stdout.flush()
        time.sleep(0.03)
    sys.stdout.write("\n")
    sys.stdout.flush()

    # a real EXR: red -> blue gradient
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            row.append([x / (w - 1), 0.2, 1.0 - x / (w - 1), 1.0])
        rows.append(row)
    write_exr(out, w, h, rows)
    print("Wrote", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
