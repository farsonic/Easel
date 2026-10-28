#!/usr/bin/env python3
"""make_rasters.py - builds the reference pictures rasterstest decodes, into tests/data/rasters/.

Every picture here is generated (gradients and noise from a fixed seed); no outside sample files.
Each comes with the RGBA its decoding must give (a .rgba file: "RGBA", width and height as
big-endian 32-bit numbers, then the pixels) and a line in manifest.txt:
"<picture> <expected> <format> <tolerance>", tolerance being the largest difference allowed in
any channel ("psnr:N" for lossy JPEG-in-TIFF). Fully transparent pixels compare equal whatever
their colour.

Expected pixels are worked out here from the source arrays (by the conversions rasters.h
documents), and wherever Pillow (or tifffile) can read the file, its decoding is checked against
that expectation too, so the expectations aren't just this script agreeing with itself:

  TIFF   tifffile writes most variants (byte orders, BigTIFF, none/LZW/Deflate/PackBits/JPEG,
         predictors 2 and 3, strips and tiles, chunky and planar, 1-16-bit, float, CMYK, Lab,
         palette, alpha); Pillow+libtiff writes CCITT RLE/G3/G4; a few are built by hand (old-style
         LZW, a reduced-resolution first page, PackBits 4-bit, FillOrder 2). tifffile re-reads all.
  PSD    written by hand (Pillow can't write PSD): every mode, depth, compression, PSB, the
         merged-alpha markers; Pillow re-reads the ones it supports.
  TGA, PCX, PNM, QOI, ICO, DDS (uncompressed)  Pillow writes some, the rest by hand.
  DDS BC1-BC5, BC7  random block data under a hand-made header; Pillow's decoding is the reference.
  XPM, HDR, FITS, DPX  by hand.

Run from anywhere: python3 tests/make_rasters.py
"""
import io, math, os, random, struct, sys, zlib
import numpy as np
from PIL import Image
import tifffile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "data", "rasters")
os.makedirs(OUT, exist_ok=True)
# the previous run's pictures go first (only what its manifest lists: this script's own output)
if os.path.exists(os.path.join(OUT, "manifest.txt")):
    for line in open(os.path.join(OUT, "manifest.txt")):
        for f in line.split()[:2]:
            if os.path.exists(os.path.join(OUT, f)):
                os.remove(os.path.join(OUT, f))
manifest = []
rng = np.random.default_rng(20261028)
random.seed(20261028)
W, H = 37, 23


def write_rgba(name, a):
    a = np.ascontiguousarray(a, dtype=np.uint8)
    h, w = a.shape[:2]
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(b"RGBA" + struct.pack(">II", w, h) + a.tobytes())


def add(name, data, expect, fmt, tol=0):
    """data: the file's bytes; expect: h x w x 4 uint8"""
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(data)
    exp = os.path.splitext(name)[0] + ".rgba"
    write_rgba(exp, expect)
    manifest.append("%s %s %s %s" % (name, exp, fmt, tol))


def close(a, b, tol, what):
    a = np.asarray(a, dtype=np.int32); b = np.asarray(b, dtype=np.int32)
    assert a.shape == b.shape, (what, a.shape, b.shape)
    m = (a[..., 3] == 0) & (b[..., 3] == 0)
    d = np.abs(a - b).max(axis=-1)
    d[m] = 0
    assert d.max() <= tol, (what, int(d.max()))


def pil_rgba(data):
    return np.asarray(Image.open(io.BytesIO(data)).convert("RGBA"))


def rgba_of(rgb, a=None):
    h, w = rgb.shape[:2]
    out = np.full((h, w, 4), 255, np.uint8)
    out[..., :3] = rgb
    if a is not None:
        out[..., 3] = a
    return out


def gray4(g, a=None):
    return rgba_of(np.repeat(g[..., None], 3, axis=2), a)


# source pictures: smooth gradients plus noise, so every value and neighbour pattern turns up
def picture(w=W, h=H, ch=3, maxv=255):
    y, x = np.mgrid[0:h, 0:w]
    out = []
    for c in range(ch):
        base = (x * (c + 1) * 7 + y * (5 - c) * 11 + c * 50) % (maxv + 1)
        noise = rng.integers(0, max(2, (maxv + 1) // 8), (h, w))
        v = (base + noise) % (maxv + 1)
        if c % 2:
            v[:, : w // 3] = v[0, 0]                         # flat areas for the run-length coders
        out.append(v)
    a = np.stack(out, axis=-1)
    return a if ch > 1 else a[..., 0]


def to8_16(v):
    return ((v.astype(np.int64) * 255 + 32767) // 65535).astype(np.uint8)


def to8_f(v):
    v = np.nan_to_num(np.asarray(v, dtype=np.float64), nan=0.0)
    return np.clip(np.floor(np.clip(v, 0, 1) * 255 + 0.5), 0, 255).astype(np.uint8)


def to8_bits(v, b):
    m = (1 << b) - 1
    return ((v.astype(np.int64) * 255 + m // 2) // m).astype(np.uint8)


def cmyk_rgb(c, m, y, k):
    kk = 255 - k.astype(np.int32)
    return np.stack([((255 - c.astype(np.int32)) * kk + 127) // 255, ((255 - m.astype(np.int32)) * kk + 127) // 255,
                     ((255 - y.astype(np.int32)) * kk + 127) // 255], axis=-1).astype(np.uint8)


def unpremul(rgb, a):
    a = a.astype(np.int32)[..., None]
    c = rgb.astype(np.int32)
    out = np.where((a > 0) & (a < 255), np.minimum(255, (c * 255 + a // 2) // np.maximum(a, 1)), c)
    return out.astype(np.uint8)


def srgb8(v):
    v = np.asarray(v, dtype=np.float64)
    idx = np.floor(np.clip(v, 0, 1) * 4096 + 0.5).astype(np.int64)
    x = np.arange(4097) / 4096.0
    tab = np.floor(np.where(x <= 0.0031308, 12.92 * x, 1.055 * x ** (1 / 2.4) - 0.055) * 255 + 0.5).astype(np.uint8)
    out = tab[np.clip(idx, 0, 4096)]
    out = np.where(v > 0, out, 0)
    return np.where(v >= 1, 255, out).astype(np.uint8)


def lab_rgb(l, a, b):
    """l 0-255, a and b offset by 128, as rasters_lab"""
    l = l.astype(np.float64); a = a.astype(np.float64); b = b.astype(np.float64)
    fy = (l * 100 / 255 + 16) / 116; fx = fy + (a - 128) / 500; fz = fy - (b - 128) / 200
    inv = lambda t: np.where(t > 6 / 29, t ** 3, 3 * (6 / 29) ** 2 * (t - 4 / 29))
    X, Y, Z = 0.9642 * inv(fx), inv(fy), 0.8249 * inv(fz)
    r = 3.1339 * X - 1.6169 * Y - 0.4906 * Z
    g = -0.9788 * X + 1.9161 * Y + 0.0335 * Z
    bb = 0.0719 * X - 0.2290 * Y + 1.4052 * Z
    return np.stack([srgb8(r), srgb8(g), srgb8(bb)], axis=-1)


# ------------------------------------------------------------------ TIFF
def tif(**kw):
    b = io.BytesIO()
    data = kw.pop("data")
    if kw.get("planarconfig") == "separate":             # tifffile wants samples first then
        data = np.ascontiguousarray(np.moveaxis(data, -1, 0))
    tifffile.imwrite(b, data, **kw)
    return b.getvalue()


def add_tiff(name, data, expect, tol=0, reread=True):
    if reread:                                           # tifffile must read it back as written
        tifffile.imread(io.BytesIO(data))
    add(name, data, expect, "tiff", tol)


def make_tiff():
    rgb8 = picture().astype(np.uint8)
    g8 = picture(ch=1).astype(np.uint8)
    rgb16 = picture(maxv=65535).astype(np.uint16)
    g16 = picture(ch=1, maxv=65535).astype(np.uint16)
    a8 = picture(ch=1).astype(np.uint8)
    a8[0, :] = 0; a8[1, :] = 255
    exp_rgb8 = rgba_of(rgb8)
    for comp in [None, "lzw", "deflate", "adobe_deflate", "packbits"]:
        for bo in "<>":
            cn = comp or "none"
            add_tiff("tiff_rgb8_%s_%s.tif" % (cn, "le" if bo == "<" else "be"), tif(data=rgb8, photometric="rgb", compression=comp, byteorder=bo, rowsperstrip=5), exp_rgb8)
            add_tiff("tiff_gray16_%s_%s.tif" % (cn, "le" if bo == "<" else "be"), tif(data=g16, photometric="minisblack", compression=comp, byteorder=bo, rowsperstrip=7), gray4(to8_16(g16)))
    for comp in ["lzw", "deflate"]:
        for bo in "<>":
            sfx = "le" if bo == "<" else "be"
            add_tiff("tiff_rgb8_%s_pred_%s.tif" % (comp, sfx), tif(data=rgb8, photometric="rgb", compression=comp, predictor=2, byteorder=bo), exp_rgb8)
            add_tiff("tiff_rgb16_%s_pred_%s.tif" % (comp, sfx), tif(data=rgb16, photometric="rgb", compression=comp, predictor=2, byteorder=bo), rgba_of(to8_16(rgb16)))
            f32 = (picture(ch=3, maxv=1000).astype(np.float32) / 900.0 - 0.05).astype(np.float32)
            f32[0, 0, 0] = np.nan
            add_tiff("tiff_rgbf32_%s_fpred_%s.tif" % (comp, sfx), tif(data=f32, photometric="rgb", compression=comp, predictor=3, byteorder=bo), rgba_of(to8_f(f32)))
            f64 = (picture(ch=1, maxv=1000) / 900.0).astype(np.float64)
            add_tiff("tiff_grayf64_%s_fpred_%s.tif" % (comp, sfx), tif(data=f64, photometric="minisblack", compression=comp, predictor=3, byteorder=bo), gray4(to8_f(f64)))
            i32 = (picture(ch=1, maxv=65535).astype(np.uint32) * 65536 + 12345).astype(np.uint32)
            add_tiff("tiff_gray32_%s_pred_%s.tif" % (comp, sfx), tif(data=i32, photometric="minisblack", compression=comp, predictor=2, byteorder=bo), gray4((i32 >> 24).astype(np.uint8)))
    f16 = (picture(ch=3, maxv=1000) / 950.0).astype(np.float16)
    add_tiff("tiff_rgbf16.tif", tif(data=f16, photometric="rgb"), rgba_of(to8_f(f16.astype(np.float64))))
    add_tiff("tiff_rgbf16_fpred.tif", tif(data=f16, photometric="rgb", compression="deflate", predictor=3), rgba_of(to8_f(f16.astype(np.float64))))
    s16 = (picture(ch=1, maxv=65535).astype(np.int32) - 32768).astype(np.int16)
    add_tiff("tiff_gray_s16.tif", tif(data=s16, photometric="minisblack"), gray4(to8_16((s16.astype(np.int32) + 32768).astype(np.uint16))))
    s8 = (picture(ch=1).astype(np.int32) - 128).astype(np.int8)
    add_tiff("tiff_gray_s8_lzw_pred.tif", tif(data=s8, photometric="minisblack", compression="lzw", predictor=2), gray4((s8.astype(np.int32) + 128).astype(np.uint8)))
    # tiles and planar
    for comp in [None, "lzw", "deflate", "packbits"]:
        cn = comp or "none"
        add_tiff("tiff_rgb8_tiles_%s.tif" % cn, tif(data=rgb8, photometric="rgb", compression=comp, tile=(16, 16)), exp_rgb8)
        add_tiff("tiff_rgb8_planar_%s.tif" % cn, tif(data=rgb8, photometric="rgb", compression=comp, planarconfig="separate", rowsperstrip=4), exp_rgb8)
        add_tiff("tiff_rgb8_planar_tiles_%s.tif" % cn, tif(data=rgb8, photometric="rgb", compression=comp, planarconfig="separate", tile=(16, 32)), exp_rgb8)
    add_tiff("tiff_rgb16_tiles_pred_be.tif", tif(data=rgb16, photometric="rgb", compression="lzw", predictor=2, tile=(16, 16), byteorder=">"), rgba_of(to8_16(rgb16)))
    add_tiff("tiff_rgb8_planar_pred.tif", tif(data=rgb8, photometric="rgb", compression="deflate", predictor=2, planarconfig="separate"), exp_rgb8)
    add_tiff("tiff_bigtiff_le.tif", tif(data=rgb8, photometric="rgb", bigtiff=True, compression="lzw"), exp_rgb8)
    add_tiff("tiff_bigtiff_be_tiles.tif", tif(data=rgb16, photometric="rgb", bigtiff=True, byteorder=">", tile=(16, 16), compression="deflate"), rgba_of(to8_16(rgb16)))
    # alpha
    rgba8 = np.dstack([rgb8, a8])
    add_tiff("tiff_rgba8_unassoc.tif", tif(data=rgba8, photometric="rgb", extrasamples=["unassalpha"], compression="lzw"), rgba_of(rgb8, a8))
    pm = (rgb8.astype(np.int32) * a8[..., None] // 255).astype(np.uint8)
    add_tiff("tiff_rgba8_assoc.tif", tif(data=np.dstack([pm, a8]), photometric="rgb", extrasamples=["assocalpha"]), rgba_of(unpremul(pm, a8), a8))
    add_tiff("tiff_rgba8_planar.tif", tif(data=rgba8, photometric="rgb", extrasamples=["unassalpha"], planarconfig="separate", compression="deflate"), rgba_of(rgb8, a8))
    rgba16 = np.dstack([rgb16, (a8.astype(np.uint16) * 257)])
    add_tiff("tiff_rgba16.tif", tif(data=rgba16, photometric="rgb", extrasamples=["unassalpha"], compression="lzw", predictor=2), rgba_of(to8_16(rgb16), a8))
    add_tiff("tiff_graya8.tif", tif(data=np.dstack([g8, a8]), photometric="minisblack", extrasamples=["unassalpha"]), gray4(g8, a8))
    add_tiff("tiff_rgb_extra_unspec.tif", tif(data=rgba8, photometric="rgb", extrasamples=["unspecified"]), exp_rgb8)
    # gray polarities and small depths
    add_tiff("tiff_gray8_miniswhite.tif", tif(data=g8, photometric="miniswhite", compression="packbits"), gray4(255 - g8))
    for b in (2, 4):
        v = picture(ch=1, maxv=(1 << b) - 1).astype(np.uint8)
        add_tiff("tiff_gray%d.tif" % b, tif(data=v, photometric="minisblack", bitspersample=b), gray4(to8_bits(v, b)))
        add_tiff("tiff_gray%d_miniswhite.tif" % b, tif(data=v, photometric="miniswhite", bitspersample=b), gray4(255 - to8_bits(v, b)))
    v12 = picture(ch=1, maxv=4095).astype(np.uint16)
    add_tiff("tiff_gray12.tif", tif(data=v12, photometric="minisblack", bitspersample=12), gray4(to8_bits(v12, 12)))
    bw = picture(ch=1) > 128
    for comp in [None, "packbits", "lzw", "deflate"]:
        add_tiff("tiff_bilevel_%s.tif" % (comp or "none"), tif(data=bw, photometric="minisblack", compression=comp), gray4(np.where(bw, 255, 0).astype(np.uint8)))
    add_tiff("tiff_bilevel_miniswhite.tif", tif(data=bw, photometric="miniswhite"), gray4(np.where(bw, 0, 255).astype(np.uint8)))
    # palette
    for b in (8,):
        n = 1 << b
        cmap = (rng.integers(0, 256, (3, n)) * 257).astype(np.uint16)
        idx = picture(ch=1, maxv=n - 1).astype(np.uint8)
        exp = rgba_of(np.stack([(cmap[c][idx] >> 8) for c in range(3)], axis=-1).astype(np.uint8))
        kw = dict(bitspersample=b) if b < 8 else dict(compression="lzw")
        add_tiff("tiff_palette%d.tif" % b, tif(data=idx, photometric="palette", colormap=cmap, **kw), exp)
    # CMYK
    cmyk = picture(ch=4).astype(np.uint8)
    exp = rgba_of(cmyk_rgb(*[cmyk[..., i] for i in range(4)]))
    add_tiff("tiff_cmyk8.tif", tif(data=cmyk, photometric="separated", compression="lzw"), exp)
    add_tiff("tiff_cmyk8_planar.tif", tif(data=cmyk, photometric="separated", planarconfig="separate"), exp)
    cmyka = np.dstack([cmyk, a8])
    add_tiff("tiff_cmyka8_planar.tif", tif(data=cmyka, photometric="separated", planarconfig="separate", extrasamples=["unassalpha"]), rgba_of(exp[..., :3], a8))
    cmyk16 = picture(ch=4, maxv=65535).astype(np.uint16)
    c8 = to8_16(cmyk16)
    add_tiff("tiff_cmyk16.tif", tif(data=cmyk16, photometric="separated", compression="deflate", predictor=2), rgba_of(cmyk_rgb(*[c8[..., i] for i in range(4)])))
    # Lab (CIELab: a and b signed)
    lab = picture(ch=3).astype(np.uint8)
    labs = lab.copy(); labs[..., 1:] = (lab[..., 1:].astype(np.int32) - 128).astype(np.int8).view(np.uint8)
    add_tiff("tiff_cielab8.tif", tif(data=labs, photometric="cielab"), rgba_of(lab_rgb(lab[..., 0], lab[..., 1], lab[..., 2])), 1)
    # JPEG through Pillow + libtiff (YCbCr, with JPEGTables), a smooth picture, against the source
    yy, xx = np.mgrid[0:64, 0:80]
    smooth = np.dstack([xx * 3, yy * 3, (xx + yy) * 2]).astype(np.uint8)
    for mode, arr, exp in [("RGB", smooth, rgba_of(smooth)), ("L", smooth[..., 0].copy(), gray4(smooth[..., 0]))]:
        for q in (75, 95):
            b = io.BytesIO()
            Image.fromarray(arr, mode).save(b, "TIFF", compression="jpeg", quality=q)
            data = b.getvalue()
            close(pil_rgba(data), exp, 40, "pillow jpeg tiff")
            add_tiff("tiff_jpeg_%s_q%d.tif" % (mode.lower(), q), data, exp, "psnr:%d" % (30 if q == 75 else 36))
    # first page reduced resolution: the second (full) page is the one
    b = io.BytesIO()
    with tifffile.TiffWriter(b) as tw:
        tw.write(rgb8[::4, ::4].copy(), photometric="rgb", subfiletype=1)
        tw.write(rgb8, photometric="rgb", compression="lzw")
        tw.write(g8, photometric="minisblack")
    add_tiff("tiff_reduced_first.tif", b.getvalue(), exp_rgb8)
    # CCITT through Pillow + libtiff
    img = Image.fromarray(np.where(bw, 255, 0).astype(np.uint8)).convert("1")
    big = (picture(w=301, h=97, ch=1) > 100)
    big[40:60, :] = True; big[:, 200:205] = False
    bigimg = Image.fromarray(np.where(big, 255, 0).astype(np.uint8)).convert("1")
    for comp in ["tiff_ccitt", "group3", "group4"]:
        for src, sz in [(img, "s"), (bigimg, "l")]:
            b = io.BytesIO()
            src.save(b, "TIFF", compression=comp)
            data = b.getvalue()
            add_tiff("tiff_%s_%s.tif" % (comp, sz), data, pil_rgba(data), reread=False)
    # hand-made: old-style (LSB-first) LZW, 4-bit PackBits, FillOrder 2, missing photometric
    add_tiff("tiff_old_lzw.tif", hand_tiff(g8, 8, 1, 5, lzw_old(g8.tobytes())), gray4(g8), reread=False)
    v4 = picture(ch=1, maxv=15).astype(np.uint8)
    rows4 = b"".join(pack_bits_row(r, 4) for r in v4)
    add_tiff("tiff_gray4_packbits.tif", hand_tiff(v4, 4, 1, 32773, packbits(rows4)), gray4(to8_bits(v4, 4)), reread=False)
    rows1 = b"".join(pack_bits_row(r.astype(np.uint8), 1) for r in bw)
    rev = bytes(int("{:08b}".format(x)[::-1], 2) for x in rows1)
    add_tiff("tiff_bilevel_fill2.tif", hand_tiff(bw.astype(np.uint8), 1, 1, 1, rev, fill=2, photo=1), gray4(np.where(bw, 255, 0).astype(np.uint8)), reread=False)


def pack_bits_row(r, b):
    out, acc, n = bytearray(), 0, 0
    for s in r:
        acc = (acc << b) | int(s); n += b
        if n == 8:
            out.append(acc); acc = 0; n = 0
    if n:
        out.append(acc << (8 - n))
    return bytes(out)


def packbits(data):
    out = bytearray(); i = 0
    while i < len(data):
        j = i
        while j < len(data) and j - i < 128 and data[j] == data[i]:
            j += 1
        if j - i >= 2:
            out += bytes([257 - (j - i), data[i]]); i = j
        else:
            j = i
            while j < len(data) and j - i < 128 and not (j + 1 < len(data) and data[j] == data[j + 1]):
                j += 1
            j = max(j, i + 1)
            out += bytes([j - i - 1]) + data[i:j]; i = j
    return bytes(out)


def lzw_old(data):
    """old-style TIFF LZW: LSB-first codes, width grows when the table reaches 512, 1024, 2048"""
    out, acc, nb = bytearray(), 0, 0
    def emit(code, width):
        nonlocal acc, nb
        acc |= code << nb; nb += width
        while nb >= 8:
            out.append(acc & 255); acc >>= 8; nb -= 8
    table = {bytes([i]): i for i in range(256)}
    nxt, width = 258, 9
    emit(256, 9)
    w = b""
    for c in data:
        wc = w + bytes([c])
        if wc in table:
            w = wc; continue
        emit(table[w], width)
        table[wc] = nxt; nxt += 1
        if nxt > (1 << width) and width < 12:         # the decoder adds its entries one code behind
            width += 1
        if nxt >= 4093:
            emit(256, width); table = {bytes([i]): i for i in range(256)}; nxt, width = 258, 9
        w = bytes([c])
    if w:
        emit(table[w], width)
    emit(257, width)
    if nb:
        out.append(acc & 255)
    return bytes(out)


def hand_tiff(a, bits, spp, comp, strip, fill=1, photo=1):
    """a little-endian TIFF with one strip"""
    h, w = a.shape[:2]
    tags = [(256, 3, 1, w), (257, 3, 1, h), (258, 3, 1, bits), (259, 3, 1, comp), (262, 3, 1, photo),
            (266, 3, 1, fill), (273, 4, 1, 0), (277, 3, 1, spp), (278, 3, 1, h), (279, 4, 1, len(strip))]
    off_ifd = 8
    data_off = off_ifd + 2 + 12 * len(tags) + 4
    tags = [(t, ty, c, data_off if t == 273 else v) for t, ty, c, v in tags]
    b = bytearray(b"II*\x00" + struct.pack("<I", off_ifd))
    b += struct.pack("<H", len(tags))
    for t, ty, c, v in tags:
        b += struct.pack("<HHI", t, ty, c) + (struct.pack("<HH", v, 0) if ty == 3 else struct.pack("<I", v))
    b += struct.pack("<I", 0)
    return bytes(b) + strip


# ------------------------------------------------------------------ PSD
def psd_rle_rows(planes, rb, psb):
    counts, data = bytearray(), bytearray()
    for row in planes:
        p = packbits(row)
        counts += struct.pack(">I" if psb else ">H", len(p)); data += p
    return bytes(counts + data)


def psd(mode, depth, planes, w, h, comp=0, psb=False, cmd=b"", resources=b"", layer=b"", blocks=b""):
    """planes: list of per-channel big-endian sample bytes (rows of rb bytes)"""
    hdr = b"8BPS" + struct.pack(">H", 2 if psb else 1) + b"\0" * 6 + struct.pack(">HIIHH", len(planes), h, w, depth, mode)
    out = bytearray(hdr) + struct.pack(">I", len(cmd)) + cmd + struct.pack(">I", len(resources)) + resources
    lm = bytearray()
    if layer or blocks:
        lm += struct.pack(">Q" if psb else ">I", len(layer)) + layer + struct.pack(">I", 0) + blocks
    out += struct.pack(">Q" if psb else ">I", len(lm)) + lm
    out += struct.pack(">H", comp)
    rb = (w + 7) // 8 if depth == 1 else w * depth // 8
    rows = [p[y * rb:(y + 1) * rb] for p in planes for y in range(h)]
    raw = b"".join(planes)
    if comp == 0:
        out += raw
    elif comp == 1:
        out += psd_rle_rows(rows, rb, psb)
    elif comp == 2:
        out += zlib.compress(raw)
    else:
        pr = bytearray()
        for r in rows:
            r = bytearray(r)
            if depth == 8:
                for i in range(len(r) - 1, 0, -1):
                    r[i] = (r[i] - r[i - 1]) & 255
            elif depth == 16:
                v = [int.from_bytes(r[i:i + 2], "big") for i in range(0, len(r), 2)]
                v = [v[0]] + [(v[i] - v[i - 1]) & 0xFFFF for i in range(1, len(v))]
                r = bytearray(b"".join(x.to_bytes(2, "big") for x in v))
            else:
                n = len(r) // 4
                r = bytearray(r[4 * i + k] for k in range(4) for i in range(n))
                for i in range(len(r) - 1, 0, -1):
                    r[i] = (r[i] - r[i - 1]) & 255
            pr += r
        out += zlib.compress(bytes(pr))
    return bytes(out)


def planes_of(a, depth):
    """h x w x c array to big-endian planes"""
    if a.ndim == 2:
        a = a[..., None]
    dt = {8: ">u1", 16: ">u2", 32: ">f4"}[depth]
    return [np.ascontiguousarray(a[..., c]).astype(dt).tobytes() for c in range(a.shape[2])]


def make_psd():
    w, h = W, H
    rgb8 = picture().astype(np.uint8)
    for comp in range(4):
        for psb in (False, True):
            add("psd_rgb8_c%d%s.%s" % (comp, "_psb" if psb else "", "psb" if psb else "psd"), psd(3, 8, planes_of(rgb8, 8), w, h, comp, psb), rgba_of(rgb8), "psd")
    # Pillow agrees on the plain ones
    close(pil_rgba(psd(3, 8, planes_of(rgb8, 8), w, h, 1)), rgba_of(rgb8), 0, "pillow psd rgb")
    g8 = picture(ch=1).astype(np.uint8)
    add("psd_gray8_rle.psd", psd(1, 8, planes_of(g8, 8), w, h, 1), gray4(g8), "psd")
    close(pil_rgba(psd(1, 8, planes_of(g8, 8), w, h, 1)), gray4(g8), 0, "pillow psd gray")
    add("psd_duotone8.psd", psd(8, 8, planes_of(g8, 8), w, h, 0, cmd=b"\0" * 16), gray4(g8), "psd")
    add("psd_multichannel.psd", psd(7, 8, planes_of(np.dstack([g8, rgb8[..., 0]]), 8), w, h, 1), gray4(g8), "psd")
    g16 = picture(ch=1, maxv=65535).astype(np.uint16)
    for comp in range(4):
        add("psd_gray16_c%d.psd" % comp, psd(1, 16, planes_of(g16, 16), w, h, comp), gray4(to8_16(g16)), "psd")
    gf = (picture(ch=1, maxv=1000) / 900.0).astype(np.float32)
    for comp in (0, 3):
        add("psd_gray32_c%d.psd" % comp, psd(1, 32, planes_of(gf, 32), w, h, comp), gray4(to8_f(gf)), "psd")
    # bitmap: a set bit is black
    bw = picture(ch=1) > 128
    pl = b"".join(pack_bits_row(r.astype(np.uint8), 1) for r in bw)
    for comp in (0, 1):
        add("psd_bitmap_c%d.psd" % comp, psd(0, 1, [pl], w, h, comp), gray4(np.where(bw, 0, 255).astype(np.uint8)), "psd")
    # (not checked against Pillow, which reads a set bit as white; Photoshop, psd-tools and
    # PhotoCraft take it as black ink)
    # indexed, with and without a transparent index (resource 1047)
    pal = rng.integers(0, 256, (3, 256)).astype(np.uint8)
    idx = picture(ch=1).astype(np.uint8)
    exp = rgba_of(np.stack([pal[c][idx] for c in range(3)], axis=-1))
    cmd = pal.tobytes()
    add("psd_indexed.psd", psd(2, 8, planes_of(idx, 8), w, h, 1, cmd=cmd), exp, "psd")
    close(pil_rgba(psd(2, 8, planes_of(idx, 8), w, h, 1, cmd=cmd)), exp, 0, "pillow psd indexed")
    t = int(idx[3, 3])
    res = resource(1047, struct.pack(">H", t))
    exp2 = exp.copy(); exp2[idx == t, 3] = 0
    add("psd_indexed_trans.psd", psd(2, 8, planes_of(idx, 8), w, h, 1, cmd=cmd, resources=res), exp2, "psd")
    # CMYK (stored inverted: 255 is no ink)
    cm = picture(ch=4).astype(np.uint8)
    expc = rgba_of(np.stack([((cm[..., c].astype(np.int32) * cm[..., 3] + 127) // 255) for c in range(3)], axis=-1).astype(np.uint8))
    add("psd_cmyk8_rle.psd", psd(4, 8, planes_of(cm, 8), w, h, 1), expc, "psd")
    cm16 = picture(ch=4, maxv=65535).astype(np.uint16)
    c8 = to8_16(cm16)
    expc16 = rgba_of(np.stack([((c8[..., c].astype(np.int32) * c8[..., 3] + 127) // 255) for c in range(3)], axis=-1).astype(np.uint8))
    add("psd_cmyk16.psd", psd(4, 16, planes_of(cm16, 16), w, h, 0), expc16, "psd")
    # Lab
    lab = picture(ch=3).astype(np.uint8)
    add("psd_lab8.psd", psd(9, 8, planes_of(lab, 8), w, h, 1), rgba_of(lab_rgb(lab[..., 0], lab[..., 1], lab[..., 2])), "psd", 1)
    # RGB 16 and 32
    rgb16 = picture(maxv=65535).astype(np.uint16)
    for comp in range(4):
        add("psd_rgb16_c%d.psd" % comp, psd(3, 16, planes_of(rgb16, 16), w, h, comp), rgba_of(to8_16(rgb16)), "psd")
    rf = (picture(maxv=1000) / 900.0 - 0.02).astype(np.float32)
    for comp in (0, 2, 3):
        add("psd_rgb32_c%d.psd" % comp, psd(3, 32, planes_of(rf, 32), w, h, comp), rgba_of(to8_f(rf)), "psd")
    # merged transparency: matted against white in the file, made straight again
    a8 = picture(ch=1).astype(np.uint8); a8[0, :] = 0; a8[1, :] = 255
    straight = rgb8
    matted = ((straight.astype(np.int32) * a8[..., None] + 255 * (255 - a8[..., None].astype(np.int32)) + 127) // 255).astype(np.uint8)
    am = a8.astype(np.int32)[..., None]
    v = matted.astype(np.int32) + am - 255
    unm = np.where(am > 0, np.minimum(255, (np.maximum(v, 0) * 255 + am // 2) // np.maximum(am, 1)), matted).astype(np.uint8)
    exp_a = rgba_of(unm, a8)
    pl = planes_of(np.dstack([matted, a8]), 8)
    add("psd_rgba_nolayers.psd", psd(3, 8, pl, w, h, 1), exp_a, "psd")
    add("psd_rgba_negcount.psd", psd(3, 8, pl, w, h, 1, layer=struct.pack(">h", -1) + b"\0\0"), exp_a, "psd")
    add("psd_rgb_alphachan.psd", psd(3, 8, pl, w, h, 1, layer=struct.pack(">h", 1) + b"\0\0"), rgba_of(matted), "psd")
    add("psd_rgba_mtrn.psd", psd(3, 8, pl, w, h, 1, layer=struct.pack(">h", 1) + b"\0\0", blocks=b"8BIMMtrn" + struct.pack(">I", 0)), exp_a, "psd")
    add("psd_rgba_psb_mt16.psb", psd(3, 8, pl, w, h, 1, psb=True, layer=struct.pack(">h", 1) + b"\0\0",
                                     blocks=b"8BIMLMsk" + struct.pack(">Q", 4) + b"\0" * 4 + b"8BIMMt16" + struct.pack(">Q", 0)), exp_a, "psd")
    # 16-bit layers in an Lr16 block, the merged transparency named "Transparency"
    names = resource(1006, bytes([12]) + b"Transparency")
    add("psd_rgba16_lr16_named.psd", psd(3, 16, planes_of(np.dstack([matted, a8]).astype(np.uint16) * 257, 16), w, h, 1, resources=names,
                                        layer=b"", blocks=b"8BIMLr16" + struct.pack(">I", 4) + struct.pack(">h", 1) + b"\0\0"), exp_a, "psd", 1)
    ga = np.dstack([g8, a8])
    add("psd_graya.psd", psd(1, 8, planes_of(ga, 8), w, h, 0), gray4(g8, a8), "psd")


def resource(rid, data):
    return b"8BIM" + struct.pack(">H", rid) + b"\0\0" + struct.pack(">I", len(data)) + data + (b"\0" if len(data) & 1 else b"")


# ------------------------------------------------------------------ TGA
def tga_hdr(idlen, cmtype, typ, cmfirst, cmlen, cmdepth, w, h, depth, desc):
    return struct.pack("<BBBHHBHHHHBB", idlen, cmtype, typ, cmfirst, cmlen, cmdepth, 0, 0, w, h, depth, desc)


def tga_rle(pixels, bpp, cross=True):
    """pixels: list of bytes objects; packets may cross rows"""
    out = bytearray(); i = 0; n = len(pixels)
    while i < n:
        j = i
        while j < n and j - i < 128 and pixels[j] == pixels[i]:
            j += 1
        if j - i >= 2:
            out += bytes([0x80 | (j - i - 1)]) + pixels[i]; i = j
        else:
            j = i + 1
            while j < n and j - i < 128 and not (j + 1 < n and pixels[j] == pixels[j + 1]):
                j += 1
            out += bytes([j - i - 1]) + b"".join(pixels[i:j]); i = j
    return bytes(out)


def make_tga():
    rgb8 = picture().astype(np.uint8)
    a8 = picture(ch=1).astype(np.uint8); a8[0, :] = 0; a8[1, :] = 255
    g8 = picture(ch=1).astype(np.uint8)
    for mode, exp in [("RGB", rgba_of(rgb8)), ("RGBA", rgba_of(rgb8, a8)), ("L", gray4(g8)), ("LA", gray4(g8, a8))]:
        src = {"RGB": rgb8, "RGBA": np.dstack([rgb8, a8]), "L": g8, "LA": np.dstack([g8, a8])}[mode]
        for rle in (False, True):
            b = io.BytesIO()
            Image.fromarray(src, mode).save(b, "TGA", rle=rle)
            data = b.getvalue()
            close(pil_rgba(data), exp, 0, "pillow tga " + mode)
            add("tga_%s%s.tga" % (mode.lower(), "_rle" if rle else ""), data, exp, "tga")
    im = Image.fromarray(rgb8).convert("P", palette=Image.ADAPTIVE, colors=200)
    for rle in (False, True):
        b = io.BytesIO(); im.save(b, "TGA", rle=rle); data = b.getvalue()
        add("tga_pal%s.tga" % ("_rle" if rle else ""), data, pil_rgba(data), "tga")
    # by hand: every origin, 15/16-bit, colour maps of 16 and 32 bits, a map origin, RLE across rows
    h, w = rgb8.shape[:2]
    for desc, flipy, flipx in [(0x20, False, False), (0x00, True, False), (0x30, False, True), (0x10, True, True)]:
        rows = np.dstack([rgb8, a8])
        if flipy: rows = rows[::-1]
        if flipx: rows = rows[:, ::-1]
        px = [bytes([p[2], p[1], p[0], p[3]]) for p in rows.reshape(-1, 4)]
        hdr = tga_hdr(3, 0, 10, 0, 0, 0, w, h, 32, desc | 8) + b"abc"
        add("tga_32_rle_d%02x.tga" % desc, hdr + tga_rle(px, 4), rgba_of(rgb8, a8), "tga")
    v5 = picture(ch=3, maxv=31)
    al = (picture(ch=1) > 100)
    for depth in (15, 16):
        word = (v5[..., 0] << 10) | (v5[..., 1] << 5) | v5[..., 2] | (np.where(al, 0x8000, 0) if depth == 16 else 0)
        exp = rgba_of(((v5 * 255 + 15) // 31).astype(np.uint8), np.where(al, 255, 0).astype(np.uint8) if depth == 16 else None)
        data = tga_hdr(0, 0, 2, 0, 0, 0, w, h, depth, 0x20 | (1 if depth == 16 else 0)) + word.astype("<u2").tobytes()
        add("tga_%d.tga" % depth, data, exp, "tga")
        data = tga_hdr(0, 0, 2, 0, 0, 0, w, h, depth, 0x20) + word.astype("<u2").tobytes()
        add("tga_%d_noalphabits.tga" % depth, data, rgba_of(((v5 * 255 + 15) // 31).astype(np.uint8)), "tga")
    idx = picture(ch=1, maxv=99).astype(np.uint8) + 10                 # entries 10..109, map from 10
    pal = rng.integers(0, 256, (100, 4)).astype(np.uint8); pal[:, 3] = rng.integers(1, 256, 100)
    cm32 = bytes(pal[:, [2, 1, 0, 3]].tobytes())
    exp = pal[idx - 10]
    add("tga_cmap32_first10.tga", tga_hdr(0, 1, 1, 10, 100, 32, w, h, 8, 0x28) + cm32 + idx.tobytes(), exp, "tga")
    p5 = rng.integers(0, 32, (100, 3))
    cm16 = ((p5[:, 0] << 10) | (p5[:, 1] << 5) | p5[:, 2]).astype("<u2").tobytes()
    exp = rgba_of(((p5[idx - 10] * 255 + 15) // 31).astype(np.uint8))
    px = [bytes([v]) for v in idx[::-1].reshape(-1)]
    add("tga_cmap16_rle.tga", tga_hdr(0, 1, 9, 10, 100, 16, w, h, 8, 0) + cm16 + tga_rle(px, 1), exp, "tga")
    add("tga_footer.tga", tga_hdr(0, 0, 3, 0, 0, 0, w, h, 8, 0x20) + g8.tobytes() + b"\0" * 8 + b"TRUEVISION-XFILE.\0", gray4(g8), "tga")


# ------------------------------------------------------------------ PCX
def pcx_rle(line):
    out = bytearray(); i = 0
    while i < len(line):
        j = i
        while j < len(line) and j - i < 63 and line[j] == line[i]:
            j += 1
        if j - i > 1 or line[i] >= 0xC0:
            out += bytes([0xC0 | (j - i), line[i]])
        else:
            out.append(line[i])
        i = j
    return bytes(out)


def pcx(w, h, bits, planes, stride, lines, pal16=b"\0" * 48, vga=None, version=5):
    hdr = bytearray(128)
    hdr[0:4] = bytes([10, version, 1, bits])
    struct.pack_into("<HHHHHH", hdr, 4, 0, 0, w - 1, h - 1, 72, 72)
    hdr[16:64] = pal16
    hdr[65] = planes
    struct.pack_into("<HH", hdr, 66, stride, 1)
    data = bytes(hdr) + b"".join(pcx_rle(l) for l in lines)
    if vga is not None:
        data += b"\x0c" + vga
    return data


def make_pcx():
    rgb8 = picture().astype(np.uint8)
    g8 = picture(ch=1).astype(np.uint8)
    h, w = g8.shape
    for mode, src in [("RGB", rgb8), ("L", g8), ("1", None)]:
        b = io.BytesIO()
        img = Image.fromarray(g8 > 128).convert("1") if mode == "1" else Image.fromarray(src, mode)
        img.save(b, "PCX")
        data = b.getvalue()
        add("pcx_%s.pcx" % mode.lower(), data, pil_rgba(data), "pcx")
    im = Image.fromarray(rgb8).convert("P", palette=Image.ADAPTIVE, colors=256)
    b = io.BytesIO(); im.save(b, "PCX"); data = b.getvalue()
    add("pcx_p.pcx", data, pil_rgba(data), "pcx")
    stride = (w + 1) // 2 * 2
    # 8-bit gray without a palette, 24-bit and 32-bit planes, by hand
    lines = [bytes(r) + b"\0" * (stride - w) for r in g8]
    add("pcx_gray_nopal.pcx", pcx(w, h, 8, 1, stride, lines), gray4(g8), "pcx")
    a8 = picture(ch=1).astype(np.uint8)
    lines = [b"".join(bytes(rgb8[y, :, c]) + b"\0" * (stride - w) for c in range(3)) + bytes(a8[y]) + b"\0" * (stride - w) for y in range(h)]
    add("pcx_rgba.pcx", pcx(w, h, 8, 4, stride, lines), rgba_of(rgb8, a8), "pcx")
    # EGA: 1 bit x 4 planes, and 4 bits x 1 plane, 2 bits x 1 plane, with the header palette
    pal = rng.integers(0, 256, (16, 3)).astype(np.uint8)
    idx = picture(ch=1, maxv=15).astype(np.uint8)
    s1 = ((w + 7) // 8 + 1) // 2 * 2
    lines = [b"".join(pack_bits_row((idx[y] >> p) & 1, 1).ljust(s1, b"\0") for p in range(4)) for y in range(h)]
    d1 = pcx(w, h, 1, 4, s1, lines, pal.tobytes())
    add("pcx_ega_planes.pcx", d1, rgba_of(pal[idx]), "pcx")
    close(pil_rgba(d1), rgba_of(pal[idx]), 0, "pillow pcx ega")
    s4 = ((w * 4 + 7) // 8 + 1) // 2 * 2
    lines = [pack_bits_row(idx[y], 4).ljust(s4, b"\0") for y in range(h)]
    add("pcx_4bit.pcx", pcx(w, h, 4, 1, s4, lines, pal.tobytes()), rgba_of(pal[idx]), "pcx")
    i2 = idx & 3
    s2 = ((w * 2 + 7) // 8 + 1) // 2 * 2
    lines = [pack_bits_row(i2[y], 2).ljust(s2, b"\0") for y in range(h)]
    add("pcx_2bit.pcx", pcx(w, h, 2, 1, s2, lines, pal.tobytes()), rgba_of(pal[i2]), "pcx")


# ------------------------------------------------------------------ Netpbm
def make_pnm():
    rgb8 = picture().astype(np.uint8)
    g8 = picture(ch=1).astype(np.uint8)
    h, w = g8.shape
    bw = g8 > 128
    for mode, src in [("1", None), ("L", g8), ("RGB", rgb8)]:
        b = io.BytesIO()
        img = Image.fromarray(bw).convert("1") if mode == "1" else Image.fromarray(src, mode)
        img.save(b, "PPM")
        data = b.getvalue()
        add("pnm_%s.pnm" % mode.lower(), data, pil_rgba(data), "pnm")
    def asc(magic, w, h, maxv, vals, per_line=17, comment=True):
        s = "%s\n%s%d %d\n" % (magic, "# a comment\n" if comment else "", w, h)
        if maxv is not None:
            s += "%d\n" % maxv
        flat = [str(int(v)) for v in vals.reshape(-1)]
        s += "\n".join(" ".join(flat[i:i + per_line]) for i in range(0, len(flat), per_line)) + "\n"
        return s.encode()
    add("pnm_p1.pbm", asc("P1", w, h, None, bw.astype(int)), gray4(np.where(bw, 0, 255).astype(np.uint8)), "pnm")
    tight = ("P1\n%d %d\n" % (w, h) + "\n".join("".join("1" if v else "0" for v in r) for r in bw) + "\n").encode()
    add("pnm_p1_tight.pbm", tight, gray4(np.where(bw, 0, 255).astype(np.uint8)), "pnm")
    for maxv in (7, 255, 1000, 65535):
        v = picture(ch=1, maxv=maxv)
        add("pnm_p2_%d.pgm" % maxv, asc("P2", w, h, maxv, v), gray4(to8_bits(v, 0) if False else ((v.astype(np.int64) * 255 + maxv // 2) // maxv).astype(np.uint8)), "pnm")
        v3 = picture(ch=3, maxv=maxv)
        e3 = rgba_of(((v3.astype(np.int64) * 255 + maxv // 2) // maxv).astype(np.uint8))
        add("pnm_p3_%d.ppm" % maxv, asc("P3", w, h, maxv, v3), e3, "pnm")
        hdr = ("P6\n# c\n%d %d\n%d\n" % (w, h, maxv)).encode()
        body = v3.astype(">u2").tobytes() if maxv > 255 else v3.astype(np.uint8).tobytes()
        close(pil_rgba(hdr + body), e3, 1, "pillow p6 %d" % maxv)
        add("pnm_p6_%d.ppm" % maxv, hdr + body, e3, "pnm")
        hdr = ("P5 %d %d %d\n" % (w, h, maxv)).encode()
        body = v.astype(">u2").tobytes() if maxv > 255 else v.astype(np.uint8).tobytes()
        add("pnm_p5_%d.pgm" % maxv, hdr + body, gray4(((v.astype(np.int64) * 255 + maxv // 2) // maxv).astype(np.uint8)), "pnm")
    a8 = picture(ch=1).astype(np.uint8)
    for tt, arr, exp in [("GRAYSCALE", g8, gray4(g8)), ("GRAYSCALE_ALPHA", np.dstack([g8, a8]), gray4(g8, a8)),
                         ("RGB", rgb8, rgba_of(rgb8)), ("RGB_ALPHA", np.dstack([rgb8, a8]), rgba_of(rgb8, a8)),
                         ("BLACKANDWHITE", bw.astype(np.uint8), gray4(np.where(bw, 255, 0).astype(np.uint8)))]:
        depth = 1 if arr.ndim == 2 else arr.shape[2]
        maxv = 1 if tt == "BLACKANDWHITE" else 255
        hdr = ("P7\nWIDTH %d\nHEIGHT %d\nDEPTH %d\nMAXVAL %d\nTUPLTYPE %s\nENDHDR\n" % (w, h, depth, maxv, tt)).encode()
        add("pnm_pam_%s.pam" % tt.lower(), hdr + arr.astype(np.uint8).tobytes(), exp, "pnm")
    cm = picture(ch=4).astype(np.uint8)
    hdr = ("P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE CMYK\nENDHDR\n" % (w, h)).encode()
    add("pnm_pam_cmyk.pam", hdr + cm.tobytes(), rgba_of(cmyk_rgb(*[cm[..., i] for i in range(4)])), "pnm")
    hdr = ("P7\nWIDTH %d\nHEIGHT %d\nDEPTH 5\nMAXVAL 255\nTUPLTYPE CMYK_ALPHA\nENDHDR\n" % (w, h)).encode()
    add("pnm_pam_cmyka.pam", hdr + np.dstack([cm, a8]).tobytes(), rgba_of(cmyk_rgb(*[cm[..., i] for i in range(4)]), a8), "pnm")
    v16 = picture(ch=3, maxv=65535).astype(np.uint16)
    hdr = ("P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 65535\nTUPLTYPE RGB_ALPHA\nENDHDR\n" % (w, h)).encode()
    add("pnm_pam_rgba16.pam", hdr + np.dstack([v16, a8.astype(np.uint16) * 257]).astype(">u2").tobytes(), rgba_of(to8_16(v16), a8), "pnm")
    f = (picture(ch=3, maxv=1000) / 900.0 - 0.03).astype(np.float32)
    for kind, arr, exp in [("PF", f, rgba_of(to8_f(f))), ("Pf", f[..., 0].copy(), gray4(to8_f(f[..., 0])))]:
        for le in (True, False):
            hdr = ("%s\n%d %d\n%s\n" % (kind, w, h, "-1.0" if le else "1.0")).encode()
            body = arr[::-1].astype("<f4" if le else ">f4").tobytes()
            add("pnm_%s_%s.pfm" % (kind.lower() + ("c" if kind == "PF" else "g"), "le" if le else "be"), hdr + body, exp, "pnm")
    b = io.BytesIO(); Image.fromarray(f[..., 0].copy(), "F").save(b, "PPM")     # Pillow's own PFM
    add("pnm_pillow.pfm", b.getvalue(), gray4(to8_f(f[..., 0])), "pnm")


# ------------------------------------------------------------------ XPM
def make_xpm():
    h, w = 13, 19
    names = [("#FF0000", (255, 0, 0, 255)), ("#0f0", (0, 255, 0, 255)), ("#00000000FFFF", (0, 0, 255, 255)), ("None", (0, 0, 0, 0)),
             ("light goldenrod yellow", (250, 250, 210, 255)), ("SteelBlue3", (79, 148, 205, 255)), ("grey50", (127, 127, 127, 255)),
             ("Gray", (190, 190, 190, 255)), ("dark_olive-green", (85, 107, 47, 255)), ("#123456", (0x12, 0x34, 0x56, 255)),
             ("teal", (0, 128, 128, 255)), ("navy blue", (0, 0, 128, 255)), ("white", (255, 255, 255, 255))]
    for cpp in (1, 2):
        keys = [(" .+@#$%&*=-;"[i] if cpp == 1 else "a" + chr(65 + i)) if i < 12 else ("o" if cpp == 1 else "zz") for i in range(len(names))]
        idx = rng.integers(0, len(names), (h, w))
        exp = np.array([[names[i][1] for i in r] for r in idx], np.uint8)
        lines = ['"%d %d %d %d"' % (w, h, len(names), cpp)]
        for k, (nm, _) in zip(keys, names):
            # a "m" key before the "c" one must lose to it; a symbolic name is ignored
            lines.append('"%s\tm black s sym c %s"' % (k, nm))
        for r in idx:
            lines.append('"' + "".join(keys[i] for i in r) + '"')
        src = "/* XPM */\nstatic char *pic[] = {\n/* comment, with \"quotes\" */\n" + ",\n".join(lines) + "\n};\n"
        add("xpm_cpp%d.xpm" % cpp, src.encode(), exp, "xpm")
        x2 = "! XPM2\n" + "\n".join(l[1:-1] for l in lines) + "\n"
        add("xpm2_cpp%d.xpm" % cpp, x2.encode(), exp, "xpm")
    # Pillow reads simple XPMs: check it agrees on one
    lines = ['"4 2 2 1"', '"a c #FF8000"', '"b c #0000FF"', '"abab"', '"bbaa"']
    src = ("/* XPM */\nstatic char *x[] = {\n" + ",\n".join(lines) + "};\n").encode()
    exp = np.array([[(255, 128, 0, 255), (0, 0, 255, 255)] * 2, [(0, 0, 255, 255)] * 2 + [(255, 128, 0, 255)] * 2], np.uint8)
    close(pil_rgba(src), exp, 0, "pillow xpm")
    add("xpm_simple.xpm", src, exp, "xpm")


# ------------------------------------------------------------------ DDS
def dds_header(w, h, pflags, fourcc=b"\0\0\0\0", bits=0, masks=(0, 0, 0, 0), pitch=None, linsize=0, dx10=None):
    hdr = bytearray(128); hdr[0:4] = b"DDS "
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | (0x8 if pitch is not None else 0x80000)
    struct.pack_into("<IIIIIII", hdr, 4, 124, flags, h, w, pitch if pitch is not None else linsize, 0, 1)
    struct.pack_into("<II", hdr, 76, 32, pflags); hdr[84:88] = fourcc
    struct.pack_into("<IIIII", hdr, 88, bits, *masks)
    struct.pack_into("<I", hdr, 108, 0x1000)
    if dx10 is not None:
        hdr += struct.pack("<IIIII", dx10, 3, 0, 1, 0)
    return bytes(hdr)


def make_dds():
    w, h = 37, 23
    rgb8 = picture(w, h).astype(np.uint8)
    a8 = picture(w, h, ch=1).astype(np.uint8)
    bx, by = (w + 3) // 4, (h + 3) // 4
    for name, fcc, bsize, dx in [("bc1", b"DXT1", 8, None), ("bc2", b"DXT3", 16, None), ("bc3", b"DXT5", 16, None),
                                 ("bc4", b"ATI1", 8, None), ("bc5", b"ATI2", 16, None), ("bc4_dx10", b"DX10", 8, 80),
                                 ("bc5_dx10", b"DX10", 16, 83), ("bc7", b"DX10", 16, 98), ("bc1_dx10", b"DX10", 8, 71), ("bc3_dx10", b"DX10", 16, 77)]:
        blocks = bytearray(rng.integers(0, 256, bx * by * bsize).astype(np.uint8).tobytes())
        if name.startswith("bc1"):                         # some blocks of each BC1 mode
            for i in range(0, len(blocks), 16):
                blocks[i:i + 4] = bytes([0x10, 0x20, 0xF0, 0xF0])
        if name == "bc7":                                  # every mode, including the reserved one
            for i in range(bx * by):
                m = i % 9
                b0 = blocks[i * 16]
                blocks[i * 16] = 0 if m == 8 else ((b0 << (m + 1)) & 0xFF) | (1 << m)
        data = dds_header(w, h, 4, fcc, linsize=len(blocks), dx10=dx) + bytes(blocks)
        ref = pil_rgba(data).copy()
        if name == "bc7":                                  # the reserved mode is transparent black by the
            for i in range(8, bx * by, 9):                 # D3D specification (Pillow gives opaque black)
                ref[(i // bx) * 4:(i // bx) * 4 + 4, (i % bx) * 4:(i % bx) * 4 + 4] = 0
        tol = 0 if name == "bc7" else 2
        add("dds_%s.dds" % name, data, ref, "dds", tol)
    # Pillow writes BC1-BC3 and BC5 too: its own encodings, decoded by both
    for pf in ["DXT1", "DXT3", "DXT5"]:
        b = io.BytesIO(); Image.fromarray(np.dstack([rgb8, a8])).save(b, "DDS", pixel_format=pf); data = b.getvalue()
        add("dds_pillow_%s.dds" % pf.lower(), data, pil_rgba(data), "dds", 2)
    # uncompressed, by masks
    rgba = np.dstack([rgb8, a8])
    data = dds_header(w, h, 0x41, bits=32, masks=(0xFF0000, 0xFF00, 0xFF, 0xFF000000), pitch=w * 4) + rgba[..., [2, 1, 0, 3]].tobytes()
    add("dds_bgra.dds", data, rgba_of(rgb8, a8), "dds")
    close(pil_rgba(data), rgba_of(rgb8, a8), 0, "pillow dds bgra")
    pad = 8
    rows = b"".join(rgb8[y][:, [2, 1, 0]].tobytes() + b"\xAA" * pad for y in range(h))
    add("dds_bgr24_pitch.dds", dds_header(w, h, 0x40, bits=24, masks=(0xFF0000, 0xFF00, 0xFF, 0), pitch=w * 3 + pad) + rows, rgba_of(rgb8), "dds")
    v5 = picture(w, h, ch=3, maxv=63)
    r5, g6, b5 = v5[..., 0] >> 1, v5[..., 1], v5[..., 2] >> 1
    word = ((r5 << 11) | (g6 << 5) | b5).astype("<u2")
    exp = rgba_of(np.stack([(r5 * 255 + 15) // 31, (g6 * 255 + 31) // 63, (b5 * 255 + 15) // 31], axis=-1).astype(np.uint8))
    add("dds_565.dds", dds_header(w, h, 0x40, bits=16, masks=(0xF800, 0x7E0, 0x1F, 0)) + word.tobytes(), exp, "dds")
    g8 = picture(w, h, ch=1).astype(np.uint8)
    add("dds_lum8.dds", dds_header(w, h, 0x20000, bits=8, masks=(0xFF, 0, 0, 0)) + g8.tobytes(), gray4(g8), "dds")
    add("dds_lum8a8.dds", dds_header(w, h, 0x20001, bits=16, masks=(0xFF, 0, 0, 0xFF00)) + np.dstack([g8, a8]).tobytes(), gray4(g8, a8), "dds")
    add("dds_dx10_rgba8.dds", dds_header(w, h, 4, b"DX10", dx10=28) + rgba.tobytes(), rgba_of(rgb8, a8), "dds")


# ------------------------------------------------------------------ QOI
def make_qoi():
    rgb8 = picture().astype(np.uint8)
    a8 = picture(ch=1).astype(np.uint8); a8[0, :] = 0; a8[3, :] = 255
    for mode, arr, exp in [("RGB", rgb8, rgba_of(rgb8)), ("RGBA", np.dstack([rgb8, a8]), rgba_of(rgb8, a8))]:
        b = io.BytesIO(); Image.fromarray(arr, mode).save(b, "QOI"); data = b.getvalue()
        close(pil_rgba(data), exp, 0, "pillow qoi")
        add("qoi_%s.qoi" % mode.lower(), data, exp, "qoi")
    # a long run of one colour (several run ops) and small steps (diff and luma ops)
    y, x = np.mgrid[0:40, 0:70]
    arr = np.dstack([(x * 2) % 256, (x + y) % 256, (y * 3) % 256, np.full_like(x, 255)]).astype(np.uint8)
    arr[10:30] = (9, 8, 7, 255)
    b = io.BytesIO(); Image.fromarray(arr, "RGBA").save(b, "QOI")
    add("qoi_runs.qoi", b.getvalue(), arr, "qoi")


# ------------------------------------------------------------------ ICO / CUR
def dib(arr, bpp, mask, pal=None):
    h, w = arr.shape[:2]
    stride = (w * bpp + 31) // 32 * 4
    ncol = len(pal) if pal is not None else 0
    hdr = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, bpp, 0, 0, 0, 0, ncol if bpp <= 8 else 0, 0)
    p = b"".join(bytes([c[2], c[1], c[0], 0]) for c in pal) if pal is not None else b""
    rows = b""
    for r in arr[::-1]:
        if bpp == 32: line = r[:, [2, 1, 0, 3]].tobytes()
        elif bpp == 24: line = r[:, [2, 1, 0]].tobytes()
        else: line = pack_bits_row(r, bpp)
        rows += line.ljust(stride, b"\0")
    ms = (w + 31) // 32 * 4
    m = b"".join(pack_bits_row(r.astype(np.uint8), 1).ljust(ms, b"\0") for r in mask[::-1])
    return hdr + p + rows + m


def icondir(entries, typ=1):
    out = struct.pack("<HHH", 0, typ, len(entries))
    off = 6 + 16 * len(entries)
    body = b""
    for w, h, data, f1, f2 in entries:
        out += struct.pack("<BBBBHHII", w & 255, h & 255, 0, 0, f1, f2, len(data), off + len(body))
        body += data
    return out + body


def make_ico():
    rgb = picture(48, 48).astype(np.uint8)
    a = picture(48, 48, ch=1).astype(np.uint8); a[:4] = 0; a[4:8] = 255
    src = Image.fromarray(np.dstack([rgb, a]), "RGBA")
    for fmt in ("png", "bmp"):
        b = io.BytesIO(); src.save(b, "ICO", sizes=[(16, 16), (32, 32), (48, 48)], bitmap_format=fmt); data = b.getvalue()
        add("ico_%s.ico" % fmt, data, pil_rgba(data), "ico")
    # 8-bit and 4-bit palette entries with an AND mask; a 24-bit one; a cursor
    w = h = 32
    pal = rng.integers(0, 256, (256, 3)).astype(np.uint8)
    idx = picture(w, h, ch=1).astype(np.uint8)
    mask = picture(w, h, ch=1) > 180
    exp = rgba_of(pal[idx], np.where(mask, 0, 255).astype(np.uint8))
    data = icondir([(16, 16, dib(idx[:16, :16] & 15, 4, mask[:16, :16], [tuple(p) for p in pal[:16]]), 1, 4), (w, h, dib(idx, 8, mask, [tuple(p) for p in pal]), 1, 8)])
    close(pil_rgba(data), exp, 0, "pillow ico 8-bit")
    add("ico_pal8_mask.ico", data, exp, "ico")
    rgb2 = picture(w, h).astype(np.uint8)
    data = icondir([(w, h, dib(rgb2, 24, mask), 1, 24)])
    add("ico_24_mask.ico", data, rgba_of(rgb2, np.where(mask, 0, 255).astype(np.uint8)), "ico")
    rgba2 = np.dstack([rgb2, picture(w, h, ch=1).astype(np.uint8)])
    data = icondir([(w, h, dib(rgba2, 32, mask), 3, 7)], typ=2)     # hotspot 3,7: a cursor
    add("cur_32.cur", data, rgba2, "ico")
    data = icondir([(w, h, dib(np.dstack([rgb2, np.zeros((h, w), np.uint8)]), 32, mask), 0, 0)], typ=2)
    add("cur_32_noalpha.cur", data, rgba_of(rgb2, np.where(mask, 0, 255).astype(np.uint8)), "ico")
    b = io.BytesIO(); Image.fromarray(np.dstack([rgb, a]), "RGBA").resize((256, 256)).save(b, "PNG")
    data = icondir([(0, 0, b.getvalue(), 1, 32)])
    add("ico_png256.ico", data, pil_rgba(b.getvalue()), "ico")


# ------------------------------------------------------------------ HDR, FITS, DPX
def hdr_tonemap(rgbe):
    m = rgbe[..., :3].astype(np.float64); e = rgbe[..., 3].astype(np.int32)
    f = np.where(e > 0, np.ldexp(1.0, e - 136), 0.0)
    l = (0.2126 * m[..., 0] + 0.7152 * m[..., 1] + 0.0722 * m[..., 2]) * f
    scale = 0.18 / math.exp(np.log(1e-4 + l).mean())
    v = m * (f * scale)[..., None]
    return rgba_of(srgb8(v / (1 + v)))


def hdr_rle_line(line):
    out = bytearray([2, 2, len(line) >> 8, len(line) & 255])
    for c in range(4):
        ch = bytes(line[:, c]); i = 0
        while i < len(ch):
            j = i
            while j < len(ch) and j - i < 127 and ch[j] == ch[i]:
                j += 1
            if j - i >= 3:
                out += bytes([128 + j - i, ch[i]]); i = j
            else:
                j = i
                while j < len(ch) and j - i < 128 and not (j + 2 < len(ch) and ch[j] == ch[j + 1] == ch[j + 2]):
                    j += 1
                out += bytes([j - i]) + ch[i:j]; i = j
    return bytes(out)


def make_hdr():
    h, w = 23, 41
    m = picture(w, h, maxv=255).astype(np.uint8)
    m[..., 0] |= 0x80
    e = (picture(w, h, ch=1, maxv=7) + 124).astype(np.uint8)
    rgbe = np.dstack([m, e])
    rgbe[5, 5:9] = (0, 0, 0, 0)
    exp = hdr_tonemap(rgbe)
    head = b"#?RADIANCE\n# made by make_rasters.py\nFORMAT=32-bit_rle_rgbe\nEXPOSURE=1.0\n\n"
    add("hdr_flat.hdr", head + ("-Y %d +X %d\n" % (h, w)).encode() + rgbe.tobytes(), exp, "hdr", 2)
    add("hdr_rle.hdr", head + ("-Y %d +X %d\n" % (h, w)).encode() + b"".join(hdr_rle_line(r) for r in rgbe), exp, "hdr", 2)
    add("hdr_bottomup.hdr", head + ("+Y %d +X %d\n" % (h, w)).encode() + b"".join(hdr_rle_line(r) for r in rgbe[::-1]), exp, "hdr", 2)


def fits_file(cards, data):
    hdr = b"".join(c.ljust(80).encode() for c in cards + ["END"])
    hdr = hdr.ljust((len(hdr) + 2879) // 2880 * 2880, b" ")
    return hdr + data + b"\0" * ((-len(data)) % 2880)


def make_fits():
    h, w = 23, 37
    g8 = picture(w, h, ch=1).astype(np.uint8)
    base = ["SIMPLE  =                    T", "NAXIS1  = %20d" % w, "NAXIS2  = %20d" % h]
    add("fits_u8.fits", fits_file(["SIMPLE  =                    T", "BITPIX  =                    8", "NAXIS   =                    2"] + base[1:], g8[::-1].tobytes()), gray4(g8), "fits")
    g16 = picture(w, h, ch=1, maxv=65535).astype(np.uint16)
    stored = (g16.astype(np.int32) - 32768).astype(">i2")
    add("fits_u16_bzero.fits", fits_file([base[0], "BITPIX  =                   16", "NAXIS   =                    2", base[1], base[2], "BZERO   =              32768.0", "BSCALE  =                  1.0"], stored[::-1].tobytes()), gray4(to8_16(g16)), "fits")
    f = (picture(w, h, ch=1, maxv=1000) / 1000.0).astype(">f4")
    add("fits_f32_topdown.fits", fits_file([base[0], "BITPIX  =                  -32", "NAXIS   =                    2", base[1], base[2], "ROWORDER= 'TOP-DOWN'"], f.tobytes()), gray4(to8_f(f)), "fits")
    rgb = picture(w, h).astype(np.uint8)
    cube = np.stack([rgb[::-1, :, c] for c in range(3)])
    add("fits_rgb_cube.fits", fits_file([base[0], "BITPIX  =                    8", "NAXIS   =                    3", base[1], base[2], "NAXIS3  =                    3"], cube.tobytes()), rgba_of(rgb), "fits")
    big = (picture(w, h, ch=1, maxv=1000).astype(np.float64) * 300.0).astype(">f8")
    mx = float(big.max())
    add("fits_f64_big.fits", fits_file([base[0], "BITPIX  =                  -64", "NAXIS   =                    2", base[1], base[2]], big[::-1].tobytes()), gray4(to8_f(big / mx)), "fits")


def dpx_file(be, w, h, desc, bits, packing, body, pad=0):
    e = ">" if be else "<"
    hdr = bytearray(2048)
    hdr[0:4] = b"SDPX" if be else b"XPDS"
    struct.pack_into(e + "I", hdr, 4, 2048)
    struct.pack_into(e + "HHII", hdr, 768, 0, 1, w, h)
    struct.pack_into(e + "I", hdr, 780, 0)
    hdr[800] = desc; hdr[801] = 2; hdr[802] = 2; hdr[803] = bits
    struct.pack_into(e + "HHIII", hdr, 804, packing, 0, 2048, pad, 0)
    return bytes(hdr) + body


def make_dpx():
    h, w = 13, 21
    for bits in (8, 10, 12, 16):
        for desc, nc in ((50, 3), (51, 4), (6, 1)):
            v = picture(w, h, ch=nc, maxv=(1 << bits) - 1).reshape(h, w, nc).astype(np.int64)
            mx = (1 << bits) - 1
            e8 = ((v * 255 + mx // 2) // mx).astype(np.uint8)
            exp = gray4(e8[..., 0]) if nc == 1 else rgba_of(e8[..., :3], e8[..., 3] if nc == 4 else None)
            for be in (True, False):
                packs = {8: [0], 16: [0], 10: [0, 1, 2], 12: [0, 1]}[bits]
                for packing in packs:
                    lines = []
                    for r in v:
                        s = r.reshape(-1)
                        if bits == 8: line = bytes(s.astype(np.uint8))
                        elif bits == 16: line = s.astype(">u2" if be else "<u2").tobytes()
                        elif packing in (1, 2) and bits == 10:
                            words = []
                            for i in range(0, len(s), 3):
                                t = list(s[i:i + 3]) + [0] * (3 - len(s[i:i + 3]))
                                wd = (t[0] << 22 | t[1] << 12 | t[2] << 2) if packing == 1 else (t[0] << 20 | t[1] << 10 | t[2])
                                words.append(wd)
                            line = np.array(words, dtype=">u4" if be else "<u4").tobytes()
                        elif packing == 1 and bits == 12:
                            line = (s << 4).astype(">u2" if be else "<u2").tobytes()
                        else:
                            acc = 0; nbits = 0
                            for x in s:
                                acc = (acc << bits) | int(x); nbits += bits
                            acc <<= (-nbits) % 32; nbits += (-nbits) % 32
                            words = [(acc >> (nbits - 32 * (i + 1))) & 0xFFFFFFFF for i in range(nbits // 32)]
                            line = np.array(words, dtype=">u4" if be else "<u4").tobytes()
                        line = line.ljust((len(line) + 3) // 4 * 4, b"\0")
                        lines.append(line)
                    add("dpx_%d_d%d_p%d_%s.dpx" % (bits, desc, packing, "be" if be else "le"), dpx_file(be, w, h, desc, bits, packing, b"".join(lines)), exp, "dpx")


make_tiff(); make_psd(); make_tga(); make_pcx(); make_pnm(); make_xpm(); make_dds(); make_qoi(); make_ico(); make_hdr(); make_fits(); make_dpx()
with open(os.path.join(OUT, "manifest.txt"), "w") as f:
    f.write("\n".join(manifest) + "\n")
print("%d pictures in %s" % (len(manifest), OUT))
