#!/usr/bin/env python3
"""check_avif.py - compare Easel's AVIF decoder (tests/avifdump) with Pillow's (libavif) on files.

usage: check_avif.py avifdump file.avif ...   Prints the size, the largest and mean channel
difference, and the share of channels more than 2 apart. Chroma upsampling filters differ a
little from libavif's at hard edges, so a pass is a mean under 1 and under 5% of channels more
than 2 apart."""
import subprocess, sys, tempfile, os
from PIL import Image

def check(tool, path):
    ref = Image.open(path).convert("RGBA")
    with tempfile.TemporaryDirectory() as t:
        out = os.path.join(t, "o.rgba")
        r = subprocess.run([tool, path, out], capture_output=True, text=True)
        if r.returncode:
            return f"{path}: decoder failed: {r.stdout.strip()}{r.stderr.strip()[-300:]}", False
        w, h = map(int, r.stdout.split())
        if (w, h) != ref.size:
            return f"{path}: size {w}x{h}, Pillow {ref.size}", False
        a = open(out, "rb").read()
    b = ref.tobytes()
    diffs = [abs(x - y) for x, y in zip(a, b)]
    mx = max(diffs); mean = sum(diffs) / len(diffs); big = sum(d > 2 for d in diffs) / len(diffs)
    return f"{path}: {w}x{h} max {mx} mean {mean:.3f} >2: {100*big:.2f}%", mean < 1.0 and big < 0.05

if __name__ == "__main__":
    bad = 0
    for p in sys.argv[2:]:
        msg, ok = check(sys.argv[1], p)
        print(("ok   " if ok else "BAD  ") + msg)
        bad += not ok
    sys.exit(1 if bad else 0)
