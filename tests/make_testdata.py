#!/usr/bin/env python3
"""make_testdata.py - builds the reference pictures codectest decodes, into tests/data/.

Each picture comes with the RGBA its decoding must give (a .rgba file: "RGBA", width and height as
big-endian 32-bit numbers, then the pixels), worked out here independently of Easel's C code, and
a line in manifest.txt: "<picture> <expected> <exact|psnr:N>".

PNGs of every colour type, depth, interlace and filter are written by a small encoder here (Pillow
can't write most of them); GIF, BMP and JPEG come from Pillow (and ImageMagick for 4-bit and RLE8
BMP where it is installed), with Pillow's decoding as the expected result; the IFF ILBM variants
(EHB, HAM6, HAM8, deep, masks, PBM) and some BMP oddities (RLE4, top-down, 16-bit) are built by
hand. Run from anywhere: python3 tests/make_testdata.py
"""
import os, random, shutil, struct, subprocess, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "data")
manifest = []


def write_rgba(name, w, h, px):
    assert len(px) == w * h * 4, (name, len(px), w, h)
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(b"RGBA" + struct.pack(">II", w, h) + bytes(px))


def add(pic, w, h, px, mode="exact"):
    exp = os.path.splitext(pic)[0] + ".rgba"
    write_rgba(exp, w, h, px)
    manifest.append("%s %s %s" % (pic, exp, mode))


# ------------------------------------------------------------------ PNG
def png_chunk(t, data):
    c = zlib.crc32(t + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + t + data + struct.pack(">I", c)


def pack_row(samples, depth):
    if depth == 8:
        return bytes(samples)
    if depth == 16:
        return b"".join(struct.pack(">H", s) for s in samples)
    out, acc, n = bytearray(), 0, 0
    for s in samples:
        acc = (acc << depth) | s
        n += depth
        if n == 8:
            out.append(acc); acc = 0; n = 0
    if n:
        out.append(acc << (8 - n))
    return bytes(out)


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def filter_rows(rows, bpp, start):
    """filter each row with type (start + row) % 5, so every filter is exercised"""
    out, prev = bytearray(), bytes(len(rows[0])) if rows else b""
    for i, r in enumerate(rows):
        f = (start + i) % 5
        fr = bytearray()
        for x in range(len(r)):
            a = r[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            pred = [0, a, b, (a + b) >> 1, paeth(a, b, c)][f]
            fr.append((r[x] - pred) & 255)
        out.append(f); out += fr
        prev = r
    return bytes(out)


ADAM7 = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]


def png_write(path, w, h, ctype, depth, pix, plte=None, trns=None, interlace=0):
    """pix: rows of per-pixel sample tuples"""
    chans = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bpp = max(1, chans * depth // 8)
    data = bytearray()
    if not interlace:
        rows = [pack_row([s for p in pix[y] for s in p], depth) for y in range(h)]
        data += filter_rows(rows, bpp, 0)
    else:
        for k, (sx, sy, dx, dy) in enumerate(ADAM7):
            if w <= sx or h <= sy:
                continue
            rows = [pack_row([s for x in range(sx, w, dx) for s in pix[y][x]], depth) for y in range(sy, h, dy)]
            data += filter_rows(rows, bpp, k)
    f = b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, ctype, 0, 0, interlace))
    if plte:
        f += png_chunk(b"PLTE", bytes(c for e in plte for c in e))
    if trns is not None:
        f += png_chunk(b"tRNS", trns)
    # split the image data over two IDAT chunks to exercise joining them
    z = zlib.compress(bytes(data), 9)
    f += png_chunk(b"IDAT", z[: len(z) // 2]) + png_chunk(b"IDAT", z[len(z) // 2:]) + png_chunk(b"IEND", b"")
    with open(path, "wb") as fp:
        fp.write(f)


def png_cases():
    rnd = random.Random(7)
    combos = [(0, 1), (0, 2), (0, 4), (0, 8), (0, 16), (2, 8), (2, 16), (3, 1), (3, 2), (3, 4), (3, 8), (4, 8), (4, 16), (6, 8), (6, 16)]
    for (w, h) in [(13, 11), (3, 2), (1, 1)]:
        for ctype, depth in combos:
            for interlace in (0, 1):
                if (w, h) != (13, 11) and not interlace:
                    continue
                mx = (1 << depth) - 1
                chans = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
                plte = None
                if ctype == 3:
                    n = 1 << depth
                    plte = [(rnd.randrange(256), rnd.randrange(256), rnd.randrange(256)) for _ in range(n)]
                pix = [[tuple(rnd.randrange(mx + 1) for _ in range(chans)) for x in range(w)] for y in range(h)]
                trns = None
                exp = bytearray()
                # a transparent colour / palette alphas for some types
                key = None
                if ctype == 3:
                    alphas = [rnd.choice([0, 128, 255, 77]) for _ in range(len(plte) // 2 + 1)]
                    trns = bytes(alphas)
                elif ctype in (0, 2) and depth in (2, 8, 16):
                    key = pix[0][0]
                    trns = b"".join(struct.pack(">H", s) for s in key)
                for y in range(h):
                    for x in range(w):
                        p = pix[y][x]
                        if ctype == 3:
                            r, g, b = plte[p[0]]
                            a = alphas[p[0]] if p[0] < len(alphas) else 255
                        else:
                            conv = (lambda v: v >> 8) if depth == 16 else (lambda v: v * 255 // mx)
                            if ctype in (0, 4):
                                r = g = b = conv(p[0])
                            else:
                                r, g, b = conv(p[0]), conv(p[1]), conv(p[2])
                            a = 255
                            if ctype == 4:
                                a = conv(p[1])
                            elif ctype == 6:
                                a = conv(p[3])
                            elif key is not None and p == key:
                                a = 0
                        exp += bytes((r, g, b, a))
                name = "png_c%d_d%d%s_%dx%d.png" % (ctype, depth, "_i" if interlace else "", w, h)
                png_write(os.path.join(OUT, name), w, h, ctype, depth, pix, plte, trns, interlace)
                add(name, w, h, exp)


# ------------------------------------------------------------------ pictures via Pillow
def gradient(w, h):
    from PIL import Image
    im = Image.new("RGB", (w, h))
    im.putdata([((x * 255) // (w - 1), (y * 255) // (h - 1), ((x + y) * 255) // (w + h - 2)) for y in range(h) for x in range(w)])
    return im


def pillow_rgba(path):
    from PIL import Image
    im = Image.open(path)
    im.load()
    return im.size, im.convert("RGBA").tobytes()


def pillow_cases():
    from PIL import Image
    rnd = random.Random(11)
    # GIF: palette pictures with and without transparency, interlaced and not, and a 256-colour
    # noise picture that fills the LZW table (12-bit codes, clears)
    p = Image.new("P", (37, 23))
    p.putpalette([rnd.randrange(256) for _ in range(16 * 3)] + [0] * (240 * 3))
    p.putdata([rnd.randrange(16) for _ in range(37 * 23)])
    for inter in (True, False):
        name = "gif_16%s.gif" % ("_i" if inter else "")
        p.save(os.path.join(OUT, name), interlace=inter)
        (w, h), px = pillow_rgba(os.path.join(OUT, name))
        add(name, w, h, px)
    name = "gif_trans.gif"
    p.save(os.path.join(OUT, name), transparency=3, interlace=False)
    (w, h), px = pillow_rgba(os.path.join(OUT, name))
    add(name, w, h, px)
    noise = Image.new("P", (97, 61))
    noise.putpalette([rnd.randrange(256) for _ in range(768)])
    noise.putdata([rnd.randrange(256) for _ in range(97 * 61)])
    noise.save(os.path.join(OUT, "gif_noise256.gif"), interlace=False)
    (w, h), px = pillow_rgba(os.path.join(OUT, "gif_noise256.gif"))
    add("gif_noise256.gif", w, h, px)
    # BMP from Pillow: 1-bit, 8-bit palette, greyscale, 24-bit, 32-bit with alpha
    g = gradient(29, 17)
    for mode, name in (("1", "bmp_1bit.bmp"), ("P", "bmp_8bit.bmp"), ("L", "bmp_grey.bmp"), ("RGB", "bmp_24.bmp")):
        im = g.convert(mode) if mode != "P" else g.convert("P", palette=Image.ADAPTIVE, colors=200)
        im.save(os.path.join(OUT, name))
        (w, h), px = pillow_rgba(os.path.join(OUT, name))
        add(name, w, h, px)
    ga = g.convert("RGBA")
    ga.putalpha(Image.linear_gradient("L").resize(g.size))
    ga.save(os.path.join(OUT, "bmp_32a.bmp"))
    # Pillow writes the alpha into a plain (BI_RGB) 32-bit BMP but ignores it when reading; Easel
    # keeps a non-zero fourth byte as alpha, so the source picture is the expected result
    add("bmp_32a.bmp", ga.size[0], ga.size[1], ga.tobytes())
    # BMP from ImageMagick: 4-bit and RLE8 (Pillow can't write them)
    if shutil.which("convert"):
        src = os.path.join(OUT, "_src.png")
        g.convert("P", palette=Image.ADAPTIVE, colors=12).convert("RGB").save(src)
        for args, name in ((["-colors", "12", "-type", "Palette"], "bmp_4bit.bmp"),
                           (["-colors", "200", "-type", "Palette", "-compress", "RLE"], "bmp_rle8.bmp")):
            subprocess.run(["convert", src] + args + ["BMP3:" + os.path.join(OUT, name)], check=True)
            (w, h), px = pillow_rgba(os.path.join(OUT, name))
            add(name, w, h, px)
        os.remove(src)
    # JPEG from Pillow (libjpeg): baseline 4:2:0, progressive, greyscale; compared by PSNR
    big = gradient(120, 80)
    big.save(os.path.join(OUT, "jpeg_base.jpg"), quality=90)
    big.save(os.path.join(OUT, "jpeg_prog.jpg"), quality=85, progressive=True)
    big.convert("L").save(os.path.join(OUT, "jpeg_grey.jpg"), quality=90)
    for name in ("jpeg_base.jpg", "jpeg_prog.jpg", "jpeg_grey.jpg"):
        (w, h), px = pillow_rgba(os.path.join(OUT, name))
        add(name, w, h, px, "psnr:38")


# ------------------------------------------------------------------ BMP by hand
def bmp_cases():
    rnd = random.Random(5)
    # top-down 24-bit
    w, h = 7, 5
    px = [(rnd.randrange(256), rnd.randrange(256), rnd.randrange(256)) for _ in range(w * h)]
    stride = (w * 3 + 3) & ~3
    pixels = b""
    for y in range(h):
        row = b"".join(bytes((b, g, r)) for (r, g, b) in px[y * w:(y + 1) * w])
        pixels += row + bytes(stride - len(row))
    hdr = struct.pack("<IiiHHIIiiII", 40, w, -h, 1, 24, 0, len(pixels), 2835, 2835, 0, 0)
    f = b"BM" + struct.pack("<IHHI", 14 + 40 + len(pixels), 0, 0, 54) + hdr + pixels
    open(os.path.join(OUT, "bmp_topdown.bmp"), "wb").write(f)
    add("bmp_topdown.bmp", w, h, b"".join(bytes((r, g, b, 255)) for (r, g, b) in px))
    # 16-bit RGB565 with bitfields, bottom up
    w, h = 6, 4
    vals = [rnd.randrange(65536) for _ in range(w * h)]
    stride = (w * 2 + 3) & ~3
    pixels = b""
    for y in range(h - 1, -1, -1):
        row = b"".join(struct.pack("<H", v) for v in vals[y * w:(y + 1) * w])
        pixels += row + bytes(stride - len(row))
    hdr = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 16, 3, len(pixels), 0, 0, 0, 0) + struct.pack("<III", 0xF800, 0x07E0, 0x001F)
    f = b"BM" + struct.pack("<IHHI", 14 + len(hdr) + len(pixels), 0, 0, 14 + len(hdr)) + hdr + pixels
    open(os.path.join(OUT, "bmp_565.bmp"), "wb").write(f)
    exp = b"".join(bytes((((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31, 255)) for v in vals)
    add("bmp_565.bmp", w, h, exp)
    # RLE4: runs, an absolute stretch, a delta and end of line / picture
    w, h = 10, 3
    pal = [(rnd.randrange(256), rnd.randrange(256), rnd.randrange(256)) for _ in range(16)]
    idx = [[0] * w for _ in range(h)]
    data = bytearray()
    # bottom row (stored first): a run of 6 alternating 1,2 then absolute 3,4,5 then end of line
    data += bytes((6, 0x12, 0, 3, 0x34, 0x50, 0, 0))
    for x, v in enumerate([1, 2, 1, 2, 1, 2, 3, 4, 5]):
        idx[h - 1][x] = v
    # middle row: delta right 2 (and up 0), a run of 4 sevens, end of line
    data += bytes((0, 2, 2, 0, 4, 0x77, 0, 0))
    for x in range(2, 6):
        idx[1][x] = 7
    # top row: a run of 10 of 15/14, end of picture
    data += bytes((10, 0xFE, 0, 1))
    for x in range(10):
        idx[0][x] = 15 if x % 2 == 0 else 14
    hdr = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 4, 2, len(data), 0, 0, 16, 0)
    palb = b"".join(bytes((b, g, r, 0)) for (r, g, b) in pal)
    off = 14 + 40 + len(palb)
    f = b"BM" + struct.pack("<IHHI", off + len(data), 0, 0, off) + hdr + palb + bytes(data)
    open(os.path.join(OUT, "bmp_rle4.bmp"), "wb").write(f)
    add("bmp_rle4.bmp", w, h, b"".join(bytes(pal[idx[y][x]] + (255,)) for y in range(h) for x in range(w)))


# ------------------------------------------------------------------ IFF ILBM by hand
def byterun1(data):
    """a straightforward ByteRun1 packer (independent of Easel's)"""
    out, i, n = bytearray(), 0, len(data)
    while i < n:
        j = i
        while j + 1 < n and data[j + 1] == data[i] and j - i < 127:
            j += 1
        if j - i >= 2:
            out += bytes((257 - (j - i + 1), data[i])); i = j + 1
        else:
            s = i
            while i < n and i - s < 128 and not (i + 2 < n and data[i] == data[i + 1] == data[i + 2]):
                i += 1
            out.append(i - s - 1); out += data[s:i]
    return bytes(out)


def iff_chunk(t, data):
    return t + struct.pack(">I", len(data)) + data + (b"\0" if len(data) & 1 else b"")


def ilbm_file(w, h, nplanes, vals, cmap=None, camg=None, compress=True, masking=0, tcol=0, mask=None, pbm=False):
    rowbytes = ((w + 15) >> 4) << 1
    body = bytearray()
    for y in range(h):
        planes = []
        if pbm:
            planes.append(bytes(vals[y]) + (b"\0" if w & 1 else b""))
        else:
            for p in range(nplanes):
                row = bytearray(rowbytes)
                for x in range(w):
                    if (vals[y][x] >> p) & 1:
                        row[x >> 3] |= 0x80 >> (x & 7)
                planes.append(bytes(row))
            if masking == 1:
                row = bytearray(rowbytes)
                for x in range(w):
                    if mask[y][x]:
                        row[x >> 3] |= 0x80 >> (x & 7)
                planes.append(bytes(row))
        for pl in planes:
            body += byterun1(pl) if compress else pl
    bmhd = struct.pack(">HHhhBBBBHBBhh", w, h, 0, 0, nplanes, masking, 1 if compress else 0, 0, tcol, 10, 11, w, h)
    f = iff_chunk(b"BMHD", bmhd)
    if cmap is not None:
        f += iff_chunk(b"CMAP", bytes(c for e in cmap for c in e))
    if camg is not None:
        f += iff_chunk(b"CAMG", struct.pack(">I", camg))
    f += iff_chunk(b"BODY", bytes(body))
    form = (b"PBM " if pbm else b"ILBM") + f
    return b"FORM" + struct.pack(">I", len(form)) + form


def ilbm_cases():
    rnd = random.Random(3)

    def rcmap(n):
        return [(rnd.randrange(256), rnd.randrange(256), rnd.randrange(256)) for _ in range(n)]

    def save(name, data):
        open(os.path.join(OUT, name), "wb").write(data)

    # 4 planes, 16 colours, ByteRun1, with long runs and noise
    w, h = 37, 9
    cm = rcmap(16)
    vals = [[(x // 6) % 16 if y % 2 else rnd.randrange(16) for x in range(w)] for y in range(h)]
    save("ilbm_4plane.iff", ilbm_file(w, h, 4, vals, cm))
    add("ilbm_4plane.iff", w, h, b"".join(bytes(cm[v] + (255,)) for r in vals for v in r))
    # uncompressed 1 plane with no CMAP: black and white
    vals = [[rnd.randrange(2) for x in range(w)] for y in range(h)]
    save("ilbm_1plane_nocmap.iff", ilbm_file(w, h, 1, vals, None, compress=False))
    add("ilbm_1plane_nocmap.iff", w, h, b"".join(bytes((v * 255,) * 3 + (255,)) for r in vals for v in r))
    # EHB: 6 planes, 32 colours; with the CAMG flag and without a CAMG at all
    cm = rcmap(32)
    vals = [[rnd.randrange(64) for x in range(w)] for y in range(h)]
    ehb = lambda v: cm[v] if v < 32 else tuple(c >> 1 for c in cm[v - 32])
    exp = b"".join(bytes(ehb(v) + (255,)) for r in vals for v in r)
    save("ilbm_ehb.iff", ilbm_file(w, h, 6, vals, cm, camg=0x80))
    add("ilbm_ehb.iff", w, h, exp)
    save("ilbm_ehb_nocamg.iff", ilbm_file(w, h, 6, vals, cm))
    add("ilbm_ehb_nocamg.iff", w, h, exp)
    # HAM6 and HAM8
    for nplanes, ncol in ((6, 16), (8, 64)):
        cm = rcmap(ncol)
        hb = nplanes - 2
        vals = [[rnd.randrange(1 << nplanes) for x in range(w)] for y in range(h)]
        exp = bytearray()
        for r in vals:
            cur = list(cm[0])
            for v in r:
                ctl, d = v >> hb, v & ((1 << hb) - 1)
                m = d << (8 - hb)
                m |= m >> hb
                if ctl == 0:
                    cur = list(cm[d])
                else:
                    cur[{1: 2, 2: 0, 3: 1}[ctl]] = m
                exp += bytes(cur + [255])
        name = "ilbm_ham%d.iff" % nplanes
        save(name, ilbm_file(w, h, nplanes, vals, cm, camg=0x800))
        add(name, w, h, exp)
    # deep 24-bit (uncompressed) and 32-bit (compressed)
    for nplanes, comp in ((24, False), (32, True)):
        vals = [[rnd.randrange(1 << nplanes) if x % 5 else 0x80FF4020 & ((1 << nplanes) - 1) for x in range(w)] for y in range(h)]
        exp = b"".join(bytes((v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >> 24) & 255 if nplanes == 32 else 255)) for r in vals for v in r)
        name = "ilbm_deep%d.iff" % nplanes
        save(name, ilbm_file(w, h, nplanes, vals, None, compress=comp))
        add(name, w, h, exp)
    # a mask plane (skipped) and a transparent colour
    cm = rcmap(8)
    vals = [[rnd.randrange(8) for x in range(w)] for y in range(h)]
    mask = [[rnd.randrange(2) for x in range(w)] for y in range(h)]
    save("ilbm_maskplane.iff", ilbm_file(w, h, 3, vals, cm, masking=1, mask=mask))
    add("ilbm_maskplane.iff", w, h, b"".join(bytes(cm[v] + (255,)) for r in vals for v in r))
    save("ilbm_tcol.iff", ilbm_file(w, h, 3, vals, cm, masking=2, tcol=5))
    add("ilbm_tcol.iff", w, h, b"".join(bytes(cm[v] + (0 if v == 5 else 255,)) for r in vals for v in r))
    # PBM (chunky, Deluxe Paint PC), odd width
    w = 23
    cm = rcmap(256)
    vals = [[rnd.randrange(256) if y % 3 else 9 for x in range(w)] for y in range(h)]
    save("ilbm_pbm.iff", ilbm_file(w, h, 8, vals, cm, pbm=True))
    add("ilbm_pbm.iff", w, h, b"".join(bytes(cm[v] + (255,)) for r in vals for v in r))
    save("ilbm_pbm_raw.iff", ilbm_file(w, h, 8, vals, cm, pbm=True, compress=False))
    add("ilbm_pbm_raw.iff", w, h, b"".join(bytes(cm[v] + (255,)) for r in vals for v in r))


def main():
    os.makedirs(OUT, exist_ok=True)
    png_cases()
    bmp_cases()
    ilbm_cases()
    try:
        pillow_cases()
    except ImportError:
        print("Pillow not installed: GIF, BMP and JPEG references skipped", file=sys.stderr)
    with open(os.path.join(OUT, "manifest.txt"), "w") as f:
        f.write("\n".join(manifest) + "\n")
    # sanity: Pillow must agree with the expected results for the files it can read losslessly
    try:
        from PIL import Image
        bad = 0
        for line in manifest:
            pic, exp, mode = line.split()
            if mode != "exact" or not pic.endswith(".png"):
                continue
            im = Image.open(os.path.join(OUT, pic))
            if im.mode not in ("RGB", "RGBA", "P", "L", "LA", "1", "PA"):
                continue
            # Pillow doesn't scale a 1/2/4-bit grey tRNS key, nor apply a 16-bit RGB one
            depth = int(pic.split("_d")[1].split("_")[0].split(".")[0])
            if "transparency" in im.info and (depth == 16 or (pic.startswith("png_c0_") and depth < 8)):
                continue
            got = im.convert("RGBA").tobytes()
            want = open(os.path.join(OUT, exp), "rb").read()[12:]
            same = all(got[i:i + 4] == want[i:i + 4] or (got[i + 3] == 0 and want[i + 3] == 0) for i in range(0, len(want), 4))
            if not same:
                bad += 1
                print("generator/Pillow mismatch:", pic, file=sys.stderr)
        print("%d reference pictures written to %s (%d disagreements with Pillow)" % (len(manifest), OUT, bad))
    except ImportError:
        print("%d reference pictures written to %s" % (len(manifest), OUT))


if __name__ == "__main__":
    main()
