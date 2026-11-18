#!/usr/bin/env python3
"""check_r3.py - independent checks of round 3's filters with Pillow and numpy.

Run after r3imagetest, with the folder it wrote to (its first argument, else $TMP, else /tmp):
    python3 tests/check_r3.py [folder]

- Field Blur with one pin blurs everything alike: r3_field_one.png must match Pillow's
  GaussianBlur of r3_field_src.png (sigma half the pin's radius of 8) away from the edges.
- Lens Correction's perspective and angle are one homography: r3_lens_persp.txt holds where four
  corners of the result are taken from; numpy solves the projective map through them and Pillow's
  own perspective transform (bilinear) redoes the picture, which must match r3_lens_persp.png
  wherever both are covered.
"""
import os
import sys

import numpy as np
from PIL import Image, ImageFilter

out = sys.argv[1] if len(sys.argv) > 1 else (os.environ.get("TMP") or "/tmp")
tests = fails = 0


def ok(name, cond, detail=""):
    global tests, fails
    tests += 1
    if not cond:
        fails += 1
        print("FAIL", name, detail)


# Field Blur, one pin of radius 8 (sigma 4)
src = Image.open(os.path.join(out, "r3_field_src.png")).convert("RGB")
easel = np.asarray(Image.open(os.path.join(out, "r3_field_one.png")).convert("RGB"), dtype=float)
pil = np.asarray(src.filter(ImageFilter.GaussianBlur(4)), dtype=float)
inner = (slice(14, -14), slice(14, -14))
d = np.abs(easel[inner] - pil[inner])
ok("Field Blur matches a Gaussian blur", d.mean() < 1.5 and np.percentile(d, 99) < 8, "mean %.2f p99 %.2f" % (d.mean(), np.percentile(d, 99)))
ok("and is blurred", np.abs(easel[inner] - np.asarray(src, dtype=float)[inner]).mean() > 3)

# Lens Correction: perspective and angle as Pillow's PERSPECTIVE transform
rows = [list(map(float, line.split())) for line in open(os.path.join(out, "r3_lens_persp.txt"))]
A, b = [], []
for x, y, sx, sy in rows:
    A.append([x, y, 1, 0, 0, 0, -sx * x, -sx * y]); b.append(sx)
    A.append([0, 0, 0, x, y, 1, -sy * x, -sy * y]); b.append(sy)
coef = np.linalg.solve(np.array(A), np.array(b))
src = Image.open(os.path.join(out, "r3_field_src.png")).convert("RGBA")
pil = np.asarray(src.transform(src.size, Image.Transform.PERSPECTIVE, tuple(coef), Image.Resampling.BILINEAR), dtype=float)
easel = np.asarray(Image.open(os.path.join(out, "r3_lens_persp.png")).convert("RGBA"), dtype=float)
both = (pil[..., 3] == 255) & (easel[..., 3] == 255)
d = np.abs(pil[..., :3] - easel[..., :3])[both]
ok("Lens Correction perspective covers most of the picture", both.mean() > 0.6, "%.2f" % both.mean())
ok("Lens Correction perspective matches Pillow's", d.mean() < 2.0 and np.percentile(d, 99) < 40, "mean %.2f p99 %.2f" % (d.mean(), np.percentile(d, 99)))
cover_e = easel[..., 3] > 0
cover_p = pil[..., 3] > 0
ok("the same area uncovered", (cover_e != cover_p).mean() < 0.02, "%.3f" % (cover_e != cover_p).mean())

print("check_r3: %d checks, %d failed" % (tests, fails))
sys.exit(1 if fails else 0)
