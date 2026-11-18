#!/usr/bin/env python3
"""check_fx.py - an independent check of Easel's Color Lookup (core/adjustx.c) with numpy.

Run after gallerytest, with the folder it wrote to (its first argument, else $TMP, else /tmp):
    python3 tests/check_fx.py [folder]

gallerytest writes a bent 9-point .cube LUT (bent.cube), a picture (in.rgba, size in size.txt) and
Easel's lookups of it through the LUT with trilinear and tetrahedral interpolation (out_tri.rgba,
out_tetra.rgba). Here the .cube file is read afresh and both interpolations are worked out with
numpy, in floating point; every colour must be within 1 of Easel's, and the alpha untouched.
"""
import os
import sys

import numpy as np

out = sys.argv[1] if len(sys.argv) > 1 else (os.environ.get("TMP") or "/tmp")
tests = fails = 0


def ok(name, cond):
    global tests, fails
    tests += 1
    if not cond:
        fails += 1
        print("FAIL", name)


def read_cube(path):
    n = 0
    dmin, dmax = np.zeros(3), np.ones(3)
    rows = []
    for line in open(path):
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        k = s.split()
        if k[0] == "LUT_3D_SIZE":
            n = int(k[1])
        elif k[0] == "DOMAIN_MIN":
            dmin = np.array([float(v) for v in k[1:4]])
        elif k[0] == "DOMAIN_MAX":
            dmax = np.array([float(v) for v in k[1:4]])
        elif k[0][0].isalpha():
            continue
        else:
            rows.append([float(v) for v in k[:3]])
    t = np.array(rows).reshape(n, n, n, 3)      # [b][g][r]: red changes fastest
    return n, dmin, dmax, t


def lookup(px, n, dmin, dmax, t, tetra):
    v = (px[..., :3] / 255.0 - dmin) / (dmax - dmin) * (n - 1)
    v = np.clip(v, 0, n - 1)
    i0 = np.minimum(np.floor(v).astype(int), n - 2)
    f = v - i0
    i1 = i0 + 1
    r0, g0, b0 = i0[..., 0], i0[..., 1], i0[..., 2]
    r1, g1, b1 = i1[..., 0], i1[..., 1], i1[..., 2]
    fr, fg, fb = f[..., 0:1], f[..., 1:2], f[..., 2:3]

    def E(r, g, b):
        return t[b, g, r]

    if not tetra:
        c00 = E(r0, g0, b0) * (1 - fr) + E(r1, g0, b0) * fr
        c10 = E(r0, g1, b0) * (1 - fr) + E(r1, g1, b0) * fr
        c01 = E(r0, g0, b1) * (1 - fr) + E(r1, g0, b1) * fr
        c11 = E(r0, g1, b1) * (1 - fr) + E(r1, g1, b1) * fr
        return ((c00 * (1 - fg) + c10 * fg) * (1 - fb) + (c01 * (1 - fg) + c11 * fg) * fb)
    # tetrahedral: walk from the cube's low corner to its high one, one axis at a time, largest
    # fraction first (the six orderings)
    res = np.zeros(px.shape[:-1] + (3,))
    fs = np.concatenate([fr, fg, fb], axis=-1)
    order = np.argsort(-fs, axis=-1, kind="stable")
    for idx in np.ndindex(px.shape[:-1]):
        o = order[idx]
        fv = fs[idx]
        corner = [i0[idx][0], i0[idx][1], i0[idx][2]]
        prev = E(*corner)
        acc = prev * (1 - fv[o[0]])
        for k in range(3):
            corner[o[k]] += 1
            c = E(*corner)
            w = fv[o[k]] - (fv[o[k + 1]] if k < 2 else 0.0)
            acc = acc + c * w
        res[idx] = acc
    return res


size = os.path.join(out, "size.txt")
if not os.path.exists(size):
    print("check_fx: no files from gallerytest in", out)
    sys.exit(1)
w, h = [int(v) for v in open(size).read().split()]
src = np.frombuffer(open(os.path.join(out, "in.rgba"), "rb").read(), np.uint8).reshape(h, w, 4).astype(float)
n, dmin, dmax, t = read_cube(os.path.join(out, "bent.cube"))
for tetra, name in ((0, "tri"), (1, "tetra")):
    got = np.frombuffer(open(os.path.join(out, "out_%s.rgba" % name), "rb").read(), np.uint8).reshape(h, w, 4).astype(int)
    want = np.clip(np.floor(lookup(src, n, dmin, dmax, t, tetra) * 255 + 0.5), 0, 255).astype(int)
    diff = np.abs(got[..., :3] - want)
    ok("Color Lookup (%s) within 1 of numpy (max %d)" % (name, diff.max()), diff.max() <= 1)
    ok("Color Lookup (%s) keeps the alpha" % name, (got[..., 3] == src[..., 3]).all())
    ok("Color Lookup (%s) changed the picture" % name, np.abs(got[..., :3] - src[..., :3]).max() > 20)
# Image Size's Preserve Details (Lanczos-3) against Pillow's LANCZOS enlargement, away from the edges
# (Pillow drops taps past the edge where Easel repeats the edge pixel)
rsz = os.path.join(out, "rs_size.txt")
if os.path.exists(rsz):
    from PIL import Image
    sw, sh, dw, dh = [int(v) for v in open(rsz).read().split()]
    im = Image.frombytes("RGBA", (sw, sh), open(os.path.join(out, "rs_in.rgba"), "rb").read()).convert("RGB")
    want = np.asarray(im.resize((dw, dh), Image.LANCZOS)).astype(int)
    got = np.frombuffer(open(os.path.join(out, "rs_lanczos.rgba"), "rb").read(), np.uint8).reshape(dh, dw, 4)[..., :3].astype(int)
    m = 9
    diff = np.abs(got[m:-m, m:-m] - want[m:-m, m:-m])
    ok("Preserve Details within 2 of Pillow's Lanczos (max %d)" % diff.max(), diff.max() <= 2)
else:
    ok("resampling files written", False)
print("check_fx: %d tests, %d failed" % (tests, fails))
sys.exit(1 if fails else 0)
