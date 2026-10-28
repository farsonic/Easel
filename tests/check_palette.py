#!/usr/bin/env python3
"""check_palette.py - independent checks of the files tests/palettetest.c writes (Easel 0.4).

Run after palettetest, with the same folder (default /tmp/easel_pal_out):
    python3 tests/check_palette.py [folder]

- GIFs and palette PNGs are decoded by Pillow and must give exactly the indices, palette and
  transparency Easel wrote; the quantized GIF and the grey PNGs must decode to what Easel's own
  readers gave.
- GIMP .gpl palettes are read by Pillow's GimpPaletteFile; .act through Pillow's putpalette; the
  other palette formats by small parsers written here from the format descriptions.
- ILBMs are decoded by a decoder written here (ByteRun1, planes, EHB, HAM6, HAM8, SHAM line
  palettes, 24-bit) and must match what Easel's readers gave pixel for pixel; HAM pictures must also
  be within the PSNR bound of the source picture.
"""
import io
import math
import os
import struct
import sys

from PIL import Image, GimpPaletteFile

out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/easel_pal_out"
fails = 0
tests = 0


def ok(name, cond):
    global fails, tests
    tests += 1
    if not cond:
        fails += 1
        print("FAIL:", name)


def rd(name):
    with open(os.path.join(out, name), "rb") as f:
        return f.read()


def expected_palette(n):
    # the palette palettetest writes: i, i*7, 255-i
    return bytes(b for i in range(n) for b in (i & 255, (i * 7) & 255, (255 - i) & 255))


# ------------------------------------------------------------------ palette files
def parse_palette(data, fmt):
    """returns the colours as bytes, by a parser written from each format's description"""
    if fmt == 0:  # ACT: 768 bytes, then count and transparent index
        n = struct.unpack(">H", data[768:770])[0] if len(data) >= 772 else 256
        return data[:n * 3]
    if fmt == 1:  # ACO: version 1 section first
        ver, n = struct.unpack(">HH", data[:4])
        cols = bytearray()
        o = 4
        for _ in range(n):
            sp, w, x, y, z = struct.unpack(">HHHHH", data[o:o + 10])
            o += 10
            if ver == 2:
                ln = struct.unpack(">I", data[o:o + 4])[0]
                o += 4 + 2 * ln
            assert sp == 0
            cols += bytes((w >> 8, x >> 8, y >> 8))
        return bytes(cols)
    if fmt == 2:  # ASE
        assert data[:4] == b"ASEF"
        nb = struct.unpack(">I", data[8:12])[0]
        o = 12
        cols = bytearray()
        for _ in range(nb):
            t, ln = struct.unpack(">HI", data[o:o + 6])
            body = data[o + 6:o + 6 + ln]
            o += 6 + ln
            if t == 1:
                nl = struct.unpack(">H", body[:2])[0]
                q = 2 + nl * 2
                assert body[q:q + 4] == b"RGB "
                r, g, b = struct.unpack(">fff", body[q + 4:q + 16])
                cols += bytes(int(round(v * 255)) for v in (r, g, b))
        return bytes(cols)
    if fmt == 3:  # GIMP, through Pillow
        pal, mode = GimpPaletteFile.GimpPaletteFile(io.BytesIO(data)).getpalette()
        return pal
    if fmt == 4:  # JASC
        lines = data.decode("ascii").splitlines()
        assert lines[0] == "JASC-PAL" and lines[1] == "0100"
        n = int(lines[2])
        return bytes(int(v) for ln in lines[3:3 + n] for v in ln.split())
    if fmt == 5:  # RIFF PAL
        assert data[:4] == b"RIFF" and data[8:12] == b"PAL " and data[12:16] == b"data"
        ver, n = struct.unpack("<HH", data[20:24])
        return bytes(b for i in range(n) for b in data[24 + i * 4:24 + i * 4 + 3])
    if fmt == 6:  # IFF CMAP
        o = 12
        while o + 8 <= len(data):
            cid, ln = data[o:o + 4], struct.unpack(">I", data[o + 4:o + 8])[0]
            if cid == b"CMAP":
                return data[o + 8:o + 8 + ln]
            o += 8 + ln + (ln & 1)
    if fmt == 7:  # PNG, through Pillow
        im = Image.open(io.BytesIO(data))
        return bytes(im.getpalette()[:im.size[0] * 3])
    return None


# ------------------------------------------------------------------ ILBM, written from the EA IFF ILBM description
def unpack_byterun1(src, size):
    o, i = bytearray(), 0
    while len(o) < size:
        c = src[i]
        i += 1
        if c < 128:
            o += src[i:i + c + 1]
            i += c + 1
        elif c > 128:
            o += bytes([src[i]]) * (257 - c)
            i += 1
    return bytes(o[:size])


def decode_ilbm(data):
    assert data[:4] == b"FORM" and data[8:12] == b"ILBM"
    o, chunks = 12, {}
    while o + 8 <= len(data):
        cid, ln = data[o:o + 4].decode("latin-1"), struct.unpack(">I", data[o + 4:o + 8])[0]
        chunks.setdefault(cid, data[o + 8:o + 8 + ln])
        o += 8 + ln + (ln & 1)
    w, h, _, _, np_, masking, comp, _, tcol, _, _, _, _ = struct.unpack(">HHhhBBBBHBBhh", chunks["BMHD"])
    cmap = chunks.get("CMAP", b"")
    pal = [tuple(cmap[i * 3:i * 3 + 3]) for i in range(len(cmap) // 3)]
    camg = struct.unpack(">I", chunks["CAMG"])[0] if "CAMG" in chunks else 0
    rowbytes = ((w + 15) >> 4) << 1
    nplanes = np_ + (1 if masking == 1 else 0)
    body = chunks["BODY"]
    size = rowbytes * nplanes * h
    raw = unpack_byterun1(body, size) if comp == 1 else body[:size]
    ham = (camg & 0x800) and np_ in (6, 8)
    ehb = not ham and np_ == 6 and ((camg & 0x80) or len(pal) <= 32)
    sham = chunks.get("SHAM")
    out = bytearray(w * h * 4)
    for y in range(h):
        row = raw[y * rowbytes * nplanes:(y + 1) * rowbytes * nplanes]
        if sham is not None:
            lp = sham[2 + y * 32:2 + y * 32 + 32]
            pal = []
            for i in range(16):
                v = struct.unpack(">H", lp[i * 2:i * 2 + 2])[0]
                pal.append((((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17))
        cur = list(pal[0]) if pal else [0, 0, 0]
        for x in range(w):
            v = 0
            for p in range(np_):
                if (row[p * rowbytes + (x >> 3)] >> (7 - (x & 7))) & 1:
                    v |= 1 << p
            a = 255
            if np_ == 24:
                rgb = (v & 255, (v >> 8) & 255, (v >> 16) & 255)
            elif ham:
                hb = np_ - 2
                ctl, d = v >> hb, v & ((1 << hb) - 1)
                m = d * 17 if hb == 4 else (d << 2) | (d >> 4)
                if ctl == 0:
                    cur = list(pal[d])
                elif ctl == 1:
                    cur[2] = m
                elif ctl == 2:
                    cur[0] = m
                else:
                    cur[1] = m
                rgb = tuple(cur)
            elif ehb and v >= 32:
                rgb = tuple(c >> 1 for c in pal[v - 32])
            else:
                rgb = pal[v] if v < len(pal) else (0, 0, 0)
            if masking == 2 and v == tcol:
                a = 0
            out[(y * w + x) * 4:(y * w + x) * 4 + 4] = bytes((rgb[0], rgb[1], rgb[2], a))
    return w, h, bytes(out)


def psnr(a, b):
    se, n = 0, 0
    for i in range(0, len(a), 4):
        for c in range(3):
            d = a[i + c] - b[i + c]
            se += d * d
            n += 1
    return 99 if se == 0 else 10 * math.log10(255 * 255 * n / se)


# ------------------------------------------------------------------ the manifest
with open(os.path.join(out, "manifest.txt")) as mf:
    for line in mf:
        f = line.split()
        if not f:
            continue
        kind = f[0]
        try:
            if kind == "palette":
                name, fmt, n = f[1], int(f[2]), int(f[3])
                cols = parse_palette(rd(name), fmt)
                ok("%s: %d colours as written" % (name, n), cols == expected_palette(n))
                if fmt == 0:
                    # Pillow takes the 768 bytes as a palette and gives the same colours back
                    im = Image.new("P", (1, 1))
                    im.putpalette(rd(name)[:768])
                    ok("%s: Pillow putpalette agrees" % name, bytes(im.getpalette()[:n * 3]) == expected_palette(n))
            elif kind in ("ilbm", "ham", "sham"):
                name, ref = f[1], f[2]
                w, h, px = decode_ilbm(rd(name))
                easel = rd(ref)
                if kind == "ilbm" and len(f) <= 3:
                    same = all(px[i:i + 3] == easel[i:i + 3] for i in range(0, len(px), 4))
                else:
                    same = px == easel
                ok("%s: the independent decoder gives what Easel's reader gave" % name, same)
                if kind in ("ham", "sham"):
                    p = psnr(px, rd(f[3]))
                    ok("%s: PSNR %.1f against the source (at least %s)" % (name, p, f[4]), p >= float(f[4]))
            elif kind == "gif":
                name, idxf, palf, w, h, t = f[1], f[2], f[3], int(f[4]), int(f[5]), int(f[6])
                im = Image.open(os.path.join(out, name))
                im.load()
                idx = rd(idxf)
                pal = rd(palf)
                ok("%s: Pillow decodes the same indices" % name, im.mode == "P" and im.size == (w, h) and im.tobytes() == idx[:w * h])
                n = max(idx[:w * h]) + 1
                ok("%s: the same palette" % name, bytes(im.getpalette()[:n * 3]) == pal[:n * 3])
                ok("%s: transparency" % name, im.info.get("transparency", -1) == t)
            elif kind == "png":
                name, idxf, palf, w, h, npl, ntr = f[1], f[2], f[3], int(f[4]), int(f[5]), int(f[6]), int(f[7])
                im = Image.open(os.path.join(out, name))
                im.load()
                ok("%s: Pillow decodes the same indices" % name, im.mode == "P" and im.tobytes() == rd(idxf)[:w * h])
                ok("%s: the same palette" % name, bytes(im.getpalette()[:npl * 3]) == rd(palf)[:npl * 3])
                tr = im.info.get("transparency")
                if ntr:
                    ok("%s: tRNS" % name, isinstance(tr, bytes) and tr[:3] == bytes((255, 0, 128)))
                else:
                    ok("%s: no tRNS" % name, tr is None)
            elif kind in ("pngrgba", "gifrgba"):
                name, ref = f[1], f[2]
                im = Image.open(os.path.join(out, name)).convert("RGBA")
                px, easel = im.tobytes(), rd(ref)
                if kind == "gifrgba":
                    same = all(px[i + 3] == easel[i + 3] and (px[i + 3] == 0 or px[i:i + 3] == easel[i:i + 3]) for i in range(0, len(px), 4))
                else:
                    same = px == easel
                ok("%s: Pillow decodes what Easel's reader gave" % name, same)
        except Exception as e:  # a parse failure is a failure
            ok("%s: %s" % (line.strip(), e), False)

print("%d checks, %d failed" % (tests, fails))
sys.exit(1 if fails else 0)
