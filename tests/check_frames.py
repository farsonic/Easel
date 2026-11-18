#!/usr/bin/env python3
"""check_frames.py - Pillow's view of the animated GIFs tests/frametest.c writes (Easel 1.1).

Run after frametest, with the same folder (default /tmp/easel_frames_out):
    python3 tests/check_frames.py [folder]

Each frame Pillow shows must be exactly the frame frametest drew (the same drawing is redone
here), with its time, and the GIF must loop for ever.
"""
import os
import sys

from PIL import Image

out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/easel_frames_out"
fails = 0
tests = 0
W, H = 37, 23


def ok(name, cond):
    global fails, tests
    tests += 1
    if not cond:
        fails += 1
        print("FAIL:", name)


def draw(k, alpha):
    im = Image.new("RGBA", (W, H))
    px = im.load()
    for y in range(H):
        for x in range(W):
            inside = 3 + k * 4 <= x < 13 + k * 4 and 5 <= y < 15
            p = (220, 10, 20, 255) if inside else (30 if (y // 4) & 1 else 200, 120, 60 if x < W // 2 else 160, 255)
            if alpha and y < 3 and not inside:
                p = (0, 0, 0, 0)
            px[x, y] = p
    return im


for name, n, alpha in (("anim.gif", 5, False), ("anim_alpha.gif", 4, True)):
    im = Image.open(os.path.join(out, name))
    ok(name + " frames %d" % im.n_frames, im.n_frames == n)
    ok(name + " loops for ever", im.info.get("loop") == 0)
    for k in range(min(n, im.n_frames)):
        im.seek(k)
        got = im.convert("RGBA")
        want = draw(k, alpha)
        g, w = got.load(), want.load()
        bad = 0
        for y in range(H):
            for x in range(W):
                a, b = g[x, y], w[x, y]
                if b[3] == 0:
                    bad += a[3] != 0
                else:
                    bad += a != b
        ok("%s frame %d (%d pixels differ)" % (name, k + 1, bad), bad == 0)
        ok("%s frame %d time %s" % (name, k + 1, im.info.get("duration")), im.info.get("duration") == (10 + 5 * (k % 3)) * 10)
print("%d checks, %d failed" % (tests, fails))
sys.exit(1 if fails else 0)
