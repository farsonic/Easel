#!/usr/bin/env python3
"""make_amigafmt.py - builds the test files amigafmttest decodes, into tests/data/amigafmt/.

Every file is generated here; nothing comes from outside. Each comes with the RGBA its decoding must
give, stored as a PNG (8-bit RGBA, so png_read gives it back exactly), and a line in manifest.txt:
"<file> <expected.png> <exact|maxdiff:N>".

WebP: Pillow writes lossy (many qualities, sizes, odd dimensions), lossless, alpha and animated files;
encoder settings Pillow doesn't expose (the simple loop filter, several token partitions, segment
counts, sharpness, alpha filtering and raw alpha, near-lossless, every method) go through libwebp's
own advanced API via ctypes, using the libwebp Pillow ships. The expected pixels are Pillow's
(libwebp's) decoding of the very file written, so a lossy file is checked against libwebp exactly.
Some animations are put together by hand here (frame offsets, a lossless frame) from still files.

The Amiga (and Sun, SGI, Atari) files are synthesised here from the format descriptions, with the
expected pixels worked out independently of Easel's C code.

Run from anywhere: python3 tests/make_amigafmt.py [outdir]
"""
import ctypes, glob, io, os, random, struct, sys, zlib

import numpy as np
from PIL import Image, ImageDraw, features

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "data", "amigafmt")
manifest = []


def save_expected(name, arr):
    """arr: h x w x 4 uint8 RGBA"""
    Image.fromarray(np.ascontiguousarray(arr, dtype=np.uint8), "RGBA").save(os.path.join(OUT, name), optimize=True)


def add(name, data, expected, mode="exact"):
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(data)
    exp = name + ".exp.png"
    save_expected(exp, expected)
    manifest.append("%s %s %s" % (name, exp, mode))


# ------------------------------------------------------------------ test pictures
def picture(w, h, seed=1, alpha=False, noise=20):
    """gradients, overlapping ellipses and some noise: enough for every VP8 mode and filter"""
    r = random.Random(seed)
    yy, xx = np.mgrid[0:h, 0:w]
    a = np.zeros((h, w, 4), np.uint8)
    a[..., 0] = xx * 255 // max(w - 1, 1)
    a[..., 1] = yy * 255 // max(h - 1, 1)
    a[..., 2] = ((xx ^ yy) * 3) & 255
    a[..., 3] = 255
    im = Image.fromarray(a, "RGBA")
    d = ImageDraw.Draw(im)
    for _ in range(20):
        x0, y0 = r.randrange(w), r.randrange(h)
        d.ellipse([x0, y0, x0 + r.randrange(1, w // 2 + 2), y0 + r.randrange(1, h // 2 + 2)],
                  fill=(r.randrange(256), r.randrange(256), r.randrange(256), r.randrange(256) if alpha else 255))
    n = np.array(im).astype(int)
    if noise:
        n[..., :3] += np.random.RandomState(seed).randint(-noise, noise + 1, (h, w, 3))
    n = np.clip(n, 0, 255).astype(np.uint8)
    if alpha:
        # a soft alpha ramp too, and some fully transparent pixels
        n[..., 3] = np.minimum(n[..., 3], (xx * 255 // max(w - 1, 1)).astype(np.uint8) | 15)
        n[: h // 5, : w // 5, 3] = 0
        return Image.fromarray(n, "RGBA")
    return Image.fromarray(n[..., :3], "RGB")


def few_colours(w, h, ncol, seed=3):
    r = random.Random(seed)
    pal = [(r.randrange(256), r.randrange(256), r.randrange(256), r.choice([0, 128, 255, 255])) for _ in range(ncol)]
    a = np.zeros((h, w, 4), np.uint8)
    for y in range(h):
        for x in range(w):
            a[y, x] = pal[((x // 3) + (y // 2) * 7 + (r.randrange(4) == 0)) % ncol]
    return Image.fromarray(a, "RGBA")


# ------------------------------------------------------------------ WebP
def libwebp():
    """libwebp's advanced encoding API through ctypes (Pillow's copy, else the system's)"""
    import PIL
    cands = sorted(glob.glob(os.path.join(os.path.dirname(PIL.__file__), "..", "pillow.libs", "libwebp-*.so*")))
    cands += ["libwebp.so.7"]
    for c in cands:
        try:
            return ctypes.CDLL(c)
        except OSError:
            pass
    return None


class WebPPicture(ctypes.Structure):
    _fields_ = [("use_argb", ctypes.c_int), ("colorspace", ctypes.c_int), ("width", ctypes.c_int), ("height", ctypes.c_int),
                ("y", ctypes.c_void_p), ("u", ctypes.c_void_p), ("v", ctypes.c_void_p), ("y_stride", ctypes.c_int),
                ("uv_stride", ctypes.c_int), ("a", ctypes.c_void_p), ("a_stride", ctypes.c_int), ("pad1", ctypes.c_uint32 * 2),
                ("argb", ctypes.c_void_p), ("argb_stride", ctypes.c_int), ("pad2", ctypes.c_uint32 * 3),
                ("writer", ctypes.c_void_p), ("custom_ptr", ctypes.c_void_p), ("extra_info_type", ctypes.c_int),
                ("extra_info", ctypes.c_void_p), ("stats", ctypes.c_void_p), ("error_code", ctypes.c_int),
                ("progress_hook", ctypes.c_void_p), ("user_data", ctypes.c_void_p), ("pad3", ctypes.c_uint32 * 3),
                ("pad4", ctypes.c_void_p), ("pad5", ctypes.c_void_p), ("pad6", ctypes.c_uint32 * 8),
                ("memory_", ctypes.c_void_p), ("memory_argb_", ctypes.c_void_p), ("pad7", ctypes.c_void_p * 2),
                ("spare", ctypes.c_uint8 * 256)]


class WebPMemoryWriter(ctypes.Structure):
    _fields_ = [("mem", ctypes.c_void_p), ("size", ctypes.c_size_t), ("max_size", ctypes.c_size_t), ("pad", ctypes.c_uint32 * 8)]


# WebPConfig fields by index (all 4 bytes; quality and target_PSNR are floats)
CFG = {k: i for i, k in enumerate("lossless quality method image_hint target_size target_PSNR segments sns_strength "
                                   "filter_strength filter_sharpness filter_type autofilter alpha_compression "
                                   "alpha_filtering alpha_quality pass show_compressed preprocessing partitions "
                                   "partition_limit emulate_jpeg_size thread_level low_memory near_lossless exact "
                                   "use_delta_palette use_sharp_yuv qmin qmax".split())}
LIB = libwebp()


def webp_encode(img, quality=75, lossless=0, **opts):
    """the RIFF file libwebp's WebPEncode makes with the given WebPConfig fields"""
    img = img.convert("RGBA")
    cfg = (ctypes.c_int32 * 64)()
    if not LIB.WebPConfigInitInternal(cfg, 0, ctypes.c_float(quality), 0x020F):
        raise RuntimeError("WebPConfigInit")
    fl = ctypes.cast(cfg, ctypes.POINTER(ctypes.c_float))
    fl[CFG["quality"]] = quality
    cfg[CFG["lossless"]] = lossless
    for k, v in opts.items():
        if k in ("target_PSNR",):
            fl[CFG[k]] = v
        else:
            cfg[CFG[k]] = v
    if not LIB.WebPValidateConfig(cfg):
        raise RuntimeError("bad config %r" % opts)
    pic = WebPPicture()
    if not LIB.WebPPictureInitInternal(ctypes.byref(pic), 0x020F):
        raise RuntimeError("WebPPictureInit")
    pic.use_argb = 1 if lossless else 0
    pic.width, pic.height = img.size
    buf = img.tobytes()
    if not LIB.WebPPictureImportRGBA(ctypes.byref(pic), buf, img.size[0] * 4):
        raise RuntimeError("import")
    wr = WebPMemoryWriter()
    LIB.WebPMemoryWriterInit(ctypes.byref(wr))
    pic.writer = ctypes.cast(LIB.WebPMemoryWrite, ctypes.c_void_p).value
    pic.custom_ptr = ctypes.addressof(wr)
    ok = LIB.WebPEncode(cfg, ctypes.byref(pic))
    data = ctypes.string_at(wr.mem, wr.size) if ok else None
    LIB.WebPPictureFree(ctypes.byref(pic))
    LIB.WebPMemoryWriterClear(ctypes.byref(wr))
    if not ok:
        raise RuntimeError("WebPEncode failed (%d) %r" % (pic.error_code, opts))
    return data


def pillow_webp(img, **kw):
    b = io.BytesIO()
    img.save(b, "WEBP", **kw)
    return b.getvalue()


def libwebp_decoding(data):
    im = Image.open(io.BytesIO(data))
    im.seek(0)
    return np.array(im.convert("RGBA"))


def riff_chunks(data):
    """the top-level chunks of a RIFF WEBP file as (fourcc, payload)"""
    o, out = 12, []
    while o + 8 <= len(data):
        n = struct.unpack("<I", data[o + 4:o + 8])[0]
        out.append((data[o:o + 4], data[o + 8:o + 8 + n]))
        o += 8 + n + (n & 1)
    return out


def chunk(cc, payload):
    return cc + struct.pack("<I", len(payload)) + payload + (b"\0" if len(payload) & 1 else b"")


def riff(body):
    return b"RIFF" + struct.pack("<I", 4 + len(body)) + b"WEBP" + body


def le24(v):
    return struct.pack("<I", v)[:3]


def hand_animation(cw, ch, frames, bg=0xFFFFFFFF):
    """VP8X + ANIM + ANMF for each (x, y, still webp data, blend, dispose); first frame matters"""
    body = chunk(b"VP8X", bytes([0x12, 0, 0, 0]) + le24(cw - 1) + le24(ch - 1))
    body += chunk(b"ANIM", struct.pack("<IH", bg, 0))
    for (x, y, still, flags) in frames:
        parts = [c for c in riff_chunks(still) if c[0] in (b"ALPH", b"VP8 ", b"VP8L")]
        w = h = None
        for cc, p in parts:
            if cc == b"VP8 ":
                w, h = struct.unpack("<HH", p[6:10]); w &= 0x3FFF; h &= 0x3FFF
            elif cc == b"VP8L":
                v = struct.unpack("<I", p[1:5])[0]; w, h = (v & 0x3FFF) + 1, ((v >> 14) & 0x3FFF) + 1
        payload = le24(x // 2) + le24(y // 2) + le24(w - 1) + le24(h - 1) + le24(100) + bytes([flags])
        payload += b"".join(chunk(cc, p) for cc, p in parts)
        body += chunk(b"ANMF", payload)
    return riff(body)


def add_webp(name, data):
    add(name, data, libwebp_decoding(data))


def make_webp():
    sizes = [(1, 1), (2, 3), (16, 16), (17, 9), (33, 47), (64, 64), (100, 75), (241, 131)]
    for i, (w, h) in enumerate(sizes):
        for q in ([0, 30, 75, 100] if w * h >= 256 else [50]):
            add_webp("lossy_%dx%d_q%d.webp" % (w, h, q), pillow_webp(picture(w, h, i + q), quality=q))
        add_webp("lossless_%dx%d.webp" % (w, h), pillow_webp(picture(w, h, i), lossless=True))
        if w * h >= 16:
            add_webp("alpha_%dx%d.webp" % (w, h), pillow_webp(picture(w, h, i, True), quality=60))
            add_webp("alphall_%dx%d.webp" % (w, h), pillow_webp(picture(w, h, i, True), lossless=True))
    # flat and smooth pictures (skip probabilities, DC-only blocks, the 16x16 modes)
    add_webp("lossy_flat.webp", pillow_webp(Image.new("RGB", (40, 30), (200, 30, 90)), quality=80))
    add_webp("lossy_smooth.webp", pillow_webp(picture(96, 64, 5, noise=0), quality=85))
    add_webp("lossless_palette4.webp", pillow_webp(few_colours(45, 33, 4), lossless=True))
    add_webp("lossless_palette16.webp", pillow_webp(few_colours(45, 33, 13), lossless=True))
    add_webp("lossless_palette200.webp", pillow_webp(few_colours(64, 40, 200), lossless=True))
    add_webp("lossless_palette2.webp", pillow_webp(few_colours(37, 21, 2), lossless=True))
    add_webp("lossless_exact.webp", pillow_webp(picture(50, 40, 9, True), lossless=True, exact=True))
    if LIB is None:
        print("no libwebp for ctypes: the advanced-option WebP files are skipped")
    else:
        base = picture(120, 88, 11)
        for ft in (0, 1):
            for fs in (0, 20, 60, 100):
                for sh in ((0, 3, 7) if fs == 60 else (0,)):
                    add_webp("vp8_f%d_s%d_sh%d.webp" % (ft, fs, sh),
                             webp_encode(base, 50, filter_type=ft, filter_strength=fs, filter_sharpness=sh, autofilter=0))
        for parts in (1, 2, 3):
            add_webp("vp8_parts%d.webp" % (1 << parts), webp_encode(picture(150, 140, parts), 70, partitions=parts))
        for seg in (1, 2, 3, 4):
            add_webp("vp8_seg%d.webp" % seg, webp_encode(picture(130, 70, seg), 40, segments=seg, sns_strength=100))
        add_webp("vp8_autofilter.webp", webp_encode(base, 30, autofilter=1))
        add_webp("vp8_sharpyuv.webp", webp_encode(base, 80, use_sharp_yuv=1))
        for m in (0, 6):
            add_webp("vp8_method%d.webp" % m, webp_encode(base, 60, method=m))
        add_webp("vp8_simple_q5.webp", webp_encode(picture(77, 55, 4), 5, filter_type=0, filter_strength=100))
        apic = picture(90, 70, 12, True)
        for ac in (0, 1):
            for af in (0, 1, 2, 3):
                add_webp("alph_c%d_f%d.webp" % (ac, af), webp_encode(apic, 60, alpha_compression=ac, alpha_filtering=af, alpha_quality=100))
        add_webp("alph_q30.webp", webp_encode(apic, 60, alpha_quality=30, alpha_filtering=2))
        for m in (0, 3, 6):
            add_webp("vp8l_method%d.webp" % m, webp_encode(picture(110, 66, 13 + m, True), 100, lossless=1, method=m))
        add_webp("vp8l_q0.webp", webp_encode(picture(110, 66, 21), 0, lossless=1, method=1))
        add_webp("vp8l_nearlossless.webp", webp_encode(picture(110, 66, 22), 90, lossless=1, near_lossless=40))
        add_webp("vp8l_big.webp", webp_encode(picture(300, 200, 23, True), 75, lossless=1, method=4))
    # animations: Pillow's encoder, and hand-made ones (offsets, a lossless first frame, alpha)
    frames = [picture(64, 48, 30 + k, k == 1) for k in range(3)]
    b = io.BytesIO()
    frames[0].save(b, "WEBP", save_all=True, append_images=frames[1:], duration=100, quality=70)
    add_webp("anim_pillow.webp", b.getvalue())
    b = io.BytesIO()
    frames[1].save(b, "WEBP", save_all=True, append_images=[frames[0]], duration=100, lossless=True)
    add_webp("anim_pillow_ll.webp", b.getvalue())
    f1 = pillow_webp(picture(30, 20, 40), quality=80)
    f2 = pillow_webp(picture(30, 20, 41, True), quality=80)
    f3 = pillow_webp(picture(25, 21, 42, True), lossless=True)
    add_webp("anim_offset.webp", hand_animation(64, 48, [(10, 6, f1, 0), (0, 0, f3, 0)]))
    add_webp("anim_alpha.webp", hand_animation(40, 30, [(4, 8, f2, 2)]))
    add_webp("anim_lossless.webp", hand_animation(50, 50, [(24, 28, f3, 0), (0, 0, f1, 0)]))
    # an extended still with extra chunks (ICCP, EXIF, XMP, unknown) around the picture
    st = pillow_webp(picture(31, 29, 50, True), quality=70)
    ch = riff_chunks(st)
    vp8x = bytearray(ch[0][1]); vp8x[0] |= 0x2C
    body = chunk(b"VP8X", bytes(vp8x)) + chunk(b"ICCP", b"\0" * 7) + b"".join(chunk(c, p) for c, p in ch[1:])
    body += chunk(b"EXIF", b"Exif\0\0junk") + chunk(b"XMP ", b"<x/>") + chunk(b"ZZZZ", b"1")
    add_webp("extended_chunks.webp", riff(body))


# ------------------------------------------------------------------ IFF helpers
def be16(v):
    return struct.pack(">H", v & 0xFFFF)


def be32(v):
    return struct.pack(">I", v & 0xFFFFFFFF)


def ichunk(cid, data):
    return cid + be32(len(data)) + data + (b"\0" if len(data) & 1 else b"")


def form(ftype, *chunks):
    body = ftype + b"".join(chunks)
    return b"FORM" + be32(len(body)) + body


def bmhd(w, h, np_, masking=0, comp=0, tcol=0):
    return ichunk(b"BMHD", struct.pack(">HHhhBBBBHBBhh", w, h, 0, 0, np_, masking, comp, 0, tcol, 10, 11, w, h))


def byterun1(data):
    out, i, n = bytearray(), 0, len(data)
    while i < n:
        j = i
        while j + 1 < n and data[j + 1] == data[i] and j - i < 127:
            j += 1
        if j - i + 1 >= 3:
            out += bytes([257 - (j - i + 1), data[i]]); i = j + 1
        else:
            st = i
            while i < n and i - st < 128 and not (i + 2 < n and data[i] == data[i + 1] == data[i + 2]):
                i += 1
            out += bytes([i - st - 1]) + data[st:i]
    return bytes(out)


def plane_rows(vals, w, nplanes):
    """vals: list of rows of integers -> per row, list of plane bytes (word-padded)"""
    rb = (w + 15) // 16 * 2
    rows = []
    for row in vals:
        planes = []
        for p in range(nplanes):
            b = bytearray(rb)
            for x, v in enumerate(row):
                if (v >> p) & 1:
                    b[x >> 3] |= 0x80 >> (x & 7)
            planes.append(bytes(b))
        rows.append(planes)
    return rows


def interleaved(vals, w, nplanes):
    return b"".join(b"".join(r) for r in plane_rows(vals, w, nplanes))


def contiguous(vals, w, nplanes):
    rows = plane_rows(vals, w, nplanes)
    return b"".join(rows[y][p] for p in range(nplanes) for y in range(len(rows)))


def rnd_pal(r, n):
    return [(r.randrange(256), r.randrange(256), r.randrange(256)) for _ in range(n)]


def cmap(pal):
    return ichunk(b"CMAP", bytes(c for rgb in pal for c in rgb))


def expect_indexed(vals, pal, masking=0, tcol=None, mask=None):
    h, w = len(vals), len(vals[0])
    a = np.zeros((h, w, 4), np.uint8)
    for y in range(h):
        for x in range(w):
            v = vals[y][x]
            a[y, x, :3] = pal[v] if not callable(pal) else pal(y)[v]
            a[y, x, 3] = 255
            if masking == 2 and v == tcol:
                a[y, x, 3] = 0
            if mask is not None and not mask[y][x]:
                a[y, x, 3] = 0
    return a


def ham_expect(vals, palfor, np_):
    hb = np_ - 2
    h, w = len(vals), len(vals[0])
    a = np.zeros((h, w, 4), np.uint8)
    for y in range(h):
        pal = palfor(y)
        c = list(pal[0])
        for x in range(w):
            v = vals[y][x]; ctl = v >> hb; dv = v & ((1 << hb) - 1)
            m = dv << (8 - hb); m |= m >> hb
            if ctl == 0:
                c = list(pal[dv])
            elif ctl == 1:
                c[2] = m
            elif ctl == 2:
                c[0] = m
            else:
                c[1] = m
            a[y, x] = c + [255]
    return a


def rand_vals(r, w, h, nb):
    return [[r.randrange(1 << nb) for _ in range(w)] for _ in range(h)]


# ------------------------------------------------------------------ ACBM
def make_acbm():
    r = random.Random(100)
    w, h = 37, 23
    vals = rand_vals(r, w, h, 4); pal = rnd_pal(r, 16)
    add("acbm_4plane.acbm", form(b"ACBM", bmhd(w, h, 4), cmap(pal), ichunk(b"ABIT", contiguous(vals, w, 4))), expect_indexed(vals, pal))
    add("acbm_4plane_rle.acbm", form(b"ACBM", bmhd(w, h, 4, comp=1), cmap(pal), ichunk(b"ABIT", byterun1(contiguous(vals, w, 4)))), expect_indexed(vals, pal))
    vals = rand_vals(r, w, h, 6); pal = rnd_pal(r, 32)
    full = pal + [(c[0] >> 1, c[1] >> 1, c[2] >> 1) for c in pal]
    add("acbm_ehb.acbm", form(b"ACBM", bmhd(w, h, 6), cmap(pal), ichunk(b"CAMG", be32(0x80)), ichunk(b"ABIT", contiguous(vals, w, 6))), expect_indexed(vals, full))
    pal = rnd_pal(r, 16)
    add("acbm_ham6.acbm", form(b"ACBM", bmhd(w, h, 6), cmap(pal), ichunk(b"CAMG", be32(0x800)), ichunk(b"ABIT", contiguous(vals, w, 6))), ham_expect(vals, lambda y: pal, 6))
    vals = rand_vals(r, w, h, 8); pal = rnd_pal(r, 64)
    add("acbm_ham8.acbm", form(b"ACBM", bmhd(w, h, 8), cmap(pal), ichunk(b"CAMG", be32(0x800)), ichunk(b"ABIT", contiguous(vals, w, 8))), ham_expect(vals, lambda y: pal, 8))
    vals = [[r.randrange(1 << 24) for _ in range(w)] for _ in range(h)]
    exp = np.array([[[v & 255, (v >> 8) & 255, v >> 16, 255] for v in row] for row in vals], np.uint8)
    add("acbm_deep24.acbm", form(b"ACBM", bmhd(w, h, 24), ichunk(b"ABIT", contiguous(vals, w, 24))), exp)
    vals = rand_vals(r, w, h, 3); pal = rnd_pal(r, 8); mask = rand_vals(r, w, h, 1)
    comb = [[vals[y][x] | (mask[y][x] << 3) for x in range(w)] for y in range(h)]
    add("acbm_mask.acbm", form(b"ACBM", bmhd(w, h, 3, masking=1), cmap(pal), ichunk(b"ABIT", contiguous(comb, w, 4))), expect_indexed(vals, pal, mask=mask))
    add("acbm_tcol.acbm", form(b"ACBM", bmhd(w, h, 3, masking=2, tcol=5), cmap(pal), ichunk(b"ABIT", contiguous(vals, w, 3))), expect_indexed(vals, pal, 2, 5))


# ------------------------------------------------------------------ SHAM, CTBL, PCHG
def rgb12(c):
    return ((c[0] >> 4) << 8) | ((c[1] >> 4) << 4) | (c[2] >> 4)


def c12(c):
    return tuple((v >> 4) * 17 for v in c)


def pchg_huffman(data):
    """the PCHG compressed form: (tree bytes, bit stream), the tree laid out for the decoder:
    a node's word says its 1-child (a leaf value >= 0, or minus twice the distance back to the
    1-child node), the word before it is its 0-child (a leaf as 0x100 | value, else that node)"""
    import heapq
    freq = {}
    for b in data:
        freq[b] = freq.get(b, 0) + 1
    if len(freq) == 1:
        freq[(data[0] + 1) & 255] = 1
    heap = [(f, i, b) for i, (b, f) in enumerate(sorted(freq.items()))]
    heapq.heapify(heap)
    k = len(heap)
    while len(heap) > 1:
        f0, _, a = heapq.heappop(heap); f1, _, b = heapq.heappop(heap)
        heapq.heappush(heap, (f0 + f1, k, (a, b))); k += 1
    root = heap[0][2]
    codes = {}

    def walk(n, pre):
        if isinstance(n, tuple):
            walk(n[0], pre + "0"); walk(n[1], pre + "1")
        else:
            codes[n] = pre
    walk(root, "")

    def ser(n):
        z, o = n
        s0 = [0x100 | z] if not isinstance(z, tuple) else ser(z)
        s1 = ser(o) if isinstance(o, tuple) else []
        own = o if not isinstance(o, tuple) else -2 * (len(s0) + 1)
        return s1 + s0 + [own]
    tree = ser(root)
    bits = "".join(codes[b] for b in data)
    bits += "0" * (-len(bits) % 8)
    stream = bytes(int(bits[i:i + 8], 2) for i in range(0, len(bits), 8))
    return b"".join(struct.pack(">h", v) for v in tree), stream


def pchg_chunk(changes, start, lines, big, compress):
    """changes: {line index (0-based from start): [(reg, (r,g,b))]}"""
    mask = bytearray((lines + 31) // 32 * 4)
    data = bytearray()
    nchg = 0
    for i in range(lines):
        if i in changes:
            mask[i >> 3] |= 0x80 >> (i & 7)
    body = bytearray()
    for i in range(lines):
        if i not in changes:
            continue
        ch = changes[i]
        nchg += len(ch)
        if big:
            body += be16(len(ch))
            for reg, c in ch:
                body += be16(reg) + bytes([0, c[0], c[2], c[1]])
        else:
            lo = [x for x in ch if x[0] < 16]; hi = [x for x in ch if x[0] >= 16]
            body += bytes([len(lo), len(hi)])
            for reg, c in lo + hi:
                body += be16(((reg & 15) << 12) | rgb12(c))
    data = bytes(mask) + bytes(body)
    hdr = struct.pack(">HHhHHHHHI", 1 if compress else 0, 2 if big else 1, start, lines, len(changes), 0, 255, 0, nchg)
    if compress:
        tree, stream = pchg_huffman(data)
        return ichunk(b"PCHG", hdr + be32(len(tree)) + be32(len(data)) + tree + stream)
    return ichunk(b"PCHG", hdr + data)


def make_linepal():
    r = random.Random(200)
    w, h = 40, 12
    # SHAM: HAM6 with a palette per line (version word first)
    vals = rand_vals(r, w, h, 6)
    pals = [[c12(c) for c in rnd_pal(r, 16)] for _ in range(h)]
    sham = be16(0) + b"".join(be16(rgb12(c)) for p in pals for c in p)
    add("sham.iff", form(b"ILBM", bmhd(w, h, 6), cmap(pals[0]), ichunk(b"CAMG", be32(0x800)), ichunk(b"SHAM", sham),
                         ichunk(b"BODY", interleaved(vals, w, 6))), ham_expect(vals, lambda y: pals[y], 6))
    # laced SHAM: one palette for each pair of lines, ByteRun1 body
    hl = 2 * h
    vals2 = rand_vals(r, w, hl, 6)
    add("sham_laced.iff", form(b"ILBM", bmhd(w, hl, 6, comp=1), cmap(pals[0]), ichunk(b"CAMG", be32(0x804)), ichunk(b"SHAM", sham),
                               ichunk(b"BODY", byterun1(interleaved(vals2, w, 6)))), ham_expect(vals2, lambda y: pals[y // 2], 6))
    # CTBL (Dynamic HiRes): 4 planes, 16 colours a line, no version word
    vals = rand_vals(r, w, h, 4)
    ctbl = b"".join(be16(rgb12(c)) for p in pals for c in p)
    add("ctbl.iff", form(b"ILBM", bmhd(w, h, 4), cmap(pals[0]), ichunk(b"CTBL", ctbl), ichunk(b"BODY", interleaved(vals, w, 4))),
        expect_indexed(vals, lambda y: pals[y]))
    # PCHG, 12-bit, registers 0-31 on a 5-plane picture, starting above the picture
    vals = rand_vals(r, w, h, 5)
    base = [c12(c) for c in rnd_pal(r, 32)]
    changes = {}
    for i in range(0, h + 3, 2):
        changes[i] = [(r.randrange(32), c12((r.randrange(256), r.randrange(256), r.randrange(256)))) for _ in range(r.randrange(1, 6))]

    def palfor(changes, start, base, nreg=256):
        cur = list(base) + [(0, 0, 0)] * (nreg - len(base))
        pl = {}
        events = sorted((start + i, ch) for i, ch in changes.items())
        k = 0
        out = []
        for y in range(h):
            while k < len(events) and events[k][0] <= y:
                for reg, c in events[k][1]:
                    if reg < len(cur):
                        cur[reg] = c
                k += 1
            out.append(list(cur))
        return lambda y: out[y]
    pf = palfor(changes, -3, base)
    add("pchg12.iff", form(b"ILBM", bmhd(w, h, 5), cmap(base), pchg_chunk(changes, -3, h + 3, False, False), ichunk(b"BODY", interleaved(vals, w, 5))),
        expect_indexed(vals, pf))
    # PCHG, 32-bit changes, Huffman compressed, on an 8-plane picture
    vals = rand_vals(r, w, h, 8)
    base = rnd_pal(r, 256)
    changes = {i: [(r.randrange(256), (r.randrange(256), r.randrange(256), r.randrange(256))) for _ in range(r.randrange(1, 9))] for i in range(h) if r.random() < 0.7}
    pf = palfor(changes, 0, base)
    add("pchg32_huff.iff", form(b"ILBM", bmhd(w, h, 8), cmap(base), pchg_chunk(changes, 0, h, True, True), ichunk(b"BODY", interleaved(vals, w, 8))),
        expect_indexed(vals, pf))
    # PCHG on HAM6: the base colours change down the picture
    vals = rand_vals(r, w, h, 6)
    base = [c12(c) for c in rnd_pal(r, 16)]
    changes = {i: [(r.randrange(16), c12((r.randrange(256), r.randrange(256), r.randrange(256))))] for i in range(1, h, 3)}
    pf = palfor(changes, 0, base)
    add("pchg_ham.iff", form(b"ILBM", bmhd(w, h, 6), cmap(base), ichunk(b"CAMG", be32(0x800)), pchg_chunk(changes, 0, h, False, True),
                             ichunk(b"BODY", interleaved(vals, w, 6))), ham_expect(vals, pf, 6))
    # ANIM: the first frame (a plain ILBM; one with SHAM), then a delta frame
    vals = rand_vals(r, w, h, 3); pal = rnd_pal(r, 8)
    f1 = form(b"ILBM", bmhd(w, h, 3), cmap(pal), ichunk(b"ANHD", b"\0" * 40), ichunk(b"BODY", interleaved(vals, w, 3)))
    f2 = form(b"ILBM", ichunk(b"ANHD", bytes([5]) + b"\0" * 39), ichunk(b"DLTA", b"\0" * 64))
    add("anim.anim", form(b"ANIM", f1, f2, f2), expect_indexed(vals, pal))
    vals = rand_vals(r, w, h, 6)
    f1 = form(b"ILBM", bmhd(w, h, 6), cmap(pals[0]), ichunk(b"CAMG", be32(0x800)), ichunk(b"SHAM", sham), ichunk(b"BODY", interleaved(vals, w, 6)))
    add("anim_sham.anim", form(b"ANIM", f1, f2), ham_expect(vals, lambda y: pals[y], 6))


# ------------------------------------------------------------------ RGBN, RGB8, DEEP
def make_impulse():
    r = random.Random(300)
    w, h = 29, 17
    px = []
    while len(px) < w * h:
        c = (r.randrange(256), r.randrange(256), r.randrange(256), r.random() < 0.1)
        px += [c] * r.choice([1, 1, 2, 5, 40, 127, 128, 300])
    px = px[:w * h]
    # runs (they cross lines), with the extended counts
    def runs(px):
        out, i = [], 0
        while i < len(px):
            j = i
            while j < len(px) and px[j] == px[i] and j - i < 70000:
                j += 1
            out.append((px[i], j - i)); i = j
        return out
    b8, bn = bytearray(), bytearray()
    for (c, n) in runs(px):
        g = 0x80 if c[3] else 0
        if n < 128:
            b8 += bytes([c[0], c[1], c[2], g | n])
        elif n < 256:
            b8 += bytes([c[0], c[1], c[2], g, n])
        else:
            b8 += bytes([c[0], c[1], c[2], g, 0]) + be16(n)
    exp8 = np.array([[c[0], c[1], c[2], 0 if c[3] else 255] for c in px], np.uint8).reshape(h, w, 4)
    add("rgb8.iff", form(b"RGB8", bmhd(w, h, 25, comp=4), ichunk(b"BODY", bytes(b8))), exp8)
    pxn = [((c[0] >> 4) * 17, (c[1] >> 4) * 17, (c[2] >> 4) * 17, c[3]) for c in px]
    for (c, n) in runs(pxn):
        word = ((c[0] // 17) << 12) | ((c[1] // 17) << 8) | ((c[2] // 17) << 4) | (8 if c[3] else 0)
        if n < 8:
            bn += be16(word | n)
        elif n < 256:
            bn += be16(word) + bytes([n])
        else:
            bn += be16(word) + bytes([0]) + be16(n)
    expn = np.array([[c[0], c[1], c[2], 0 if c[3] else 255] for c in pxn], np.uint8).reshape(h, w, 4)
    add("rgbn.iff", form(b"RGBN", bmhd(w, h, 13, comp=4), ichunk(b"BODY", bytes(bn))), expn)


def make_deep():
    r = random.Random(400)
    w, h = 23, 14
    img = np.array([[[x * 11 & 255, y * 17 & 255, (x * y) & 255, (x + y) * 9 & 255] for x in range(w)] for y in range(h)], np.uint8)
    img[3:9, 5:15, :] = (200, 100, 50, 255)

    def dgbl(comp):
        return ichunk(b"DGBL", struct.pack(">HHHBB", w, h, comp, 10, 11))

    def dpel(elems):
        return ichunk(b"DPEL", be32(len(elems)) + b"".join(struct.pack(">HH", t, d) for t, d in elems))
    rgb = img[..., :3].copy()
    exp = img.copy(); exp[..., 3] = 255
    add("deep_rgb.deep", form(b"DEEP", dgbl(0), dpel([(1, 8), (2, 8), (3, 8)]), ichunk(b"DBOD", rgb.tobytes())), exp)
    # RGBA, run length counted in pixels
    px = [bytes(img[y, x]) for y in range(h) for x in range(w)]
    out, i = bytearray(), 0
    while i < len(px):
        j = i
        while j < len(px) and px[j] == px[i] and j - i < 128:
            j += 1
        if j - i >= 2:
            out += bytes([257 - (j - i)]) + px[i]; i = j
        else:
            st = i
            while i < len(px) and i - st < 128 and not (i + 1 < len(px) and px[i] == px[i + 1]):
                i += 1
            out += bytes([i - st - 1]) + b"".join(px[st:i])
    add("deep_rgba_rle.deep", form(b"DEEP", dgbl(1), dpel([(1, 8), (2, 8), (3, 8), (4, 8)]), ichunk(b"DBOD", bytes(out))), img)
    # ARGB order with 16-bit elements, uncompressed
    a16 = bytearray()
    for y in range(h):
        for x in range(w):
            for ch in (3, 0, 1, 2):
                a16 += be16(int(img[y, x, ch]) * 257 + r.randrange(2) * (int(img[y, x, ch]) < 255))
    add("deep_argb16.deep", form(b"DEEP", dgbl(0), dpel([(4, 16), (1, 16), (2, 16), (3, 16)]), ichunk(b"DBOD", bytes(a16))), img)
    # TVDC: per line and element, 4-bit codes through the delta table; 0 is a run
    table = [0, 1, 2, 4, 8, 16, 32, 64, -1, -2, -4, -8, -16, -32, -64, 3]
    rev = {v: i for i, v in enumerate(table) if v}
    body = bytearray()
    exp = np.zeros((h, w, 4), np.uint8)
    for y in range(h):
        for e in range(3):
            nibs = []
            v = 0
            x = 0
            row = [int(img[y, xx, e]) for xx in range(w)]
            while x < w:
                if x > 0 and row[x] == v:
                    n = 1
                    while x + n < w and row[x + n] == v and n < 16:
                        n += 1
                    nibs += [0, n - 1]; x += n; continue
                # greedy deltas towards the target (may take several codes: every code writes a pixel,
                # so the target is reached only approximately; the expectation follows the codes)
                d = (row[x] - v + 128) % 256 - 128
                best = min((dv for dv in rev), key=lambda dv: abs(d - dv))
                v = (v + best) & 255
                nibs.append(rev[best])
                exp[y, x, e] = v
                x += 1
            # the runs: fill expected
            if len(nibs) & 1:
                nibs.append(0)
            body += bytes((nibs[i] << 4) | nibs[i + 1] for i in range(0, len(nibs), 2))
        exp[y, :, 3] = 255
    # recompute the expectation by decoding the codes (runs repeat v)
    exp2 = np.zeros((h, w, 4), np.uint8); exp2[..., 3] = 255
    pos = 0
    for y in range(h):
        for e in range(3):
            v = 0; x = 0; nib = pos * 2
            def gn(k):
                return (body[k >> 1] >> (0 if k & 1 else 4)) & 15
            while x < w:
                c = gn(nib); nib += 1
                if table[c]:
                    v = (v + table[c]) & 255; exp2[y, x, e] = v; x += 1
                else:
                    n = gn(nib) + 1; nib += 1
                    for _ in range(n):
                        if x < w:
                            exp2[y, x, e] = v; x += 1
            pos = (nib + 1) // 2
    tv = ichunk(b"TVDC", b"".join(struct.pack(">h", v) for v in table))
    add("deep_tvdc.deep", form(b"DEEP", dgbl(5), dpel([(1, 8), (2, 8), (3, 8)]), tv, ichunk(b"DBOD", bytes(body))), exp2)
    # DLOC: a smaller body placed on the canvas
    bw, bh, bx, by = 10, 6, 7, 5
    sub = img[:bh, :bw, :3].copy()
    exp = np.zeros((h, w, 4), np.uint8)
    exp[by:by + bh, bx:bx + bw, :3] = sub; exp[by:by + bh, bx:bx + bw, 3] = 255
    add("deep_dloc.deep", form(b"DEEP", dgbl(0), dpel([(1, 8), (2, 8), (3, 8)]), ichunk(b"DLOC", struct.pack(">HHhh", bw, bh, bx, by)),
                               ichunk(b"DBOD", sub.tobytes())), exp)


# ------------------------------------------------------------------ Workbench icons
PAL13 = [(0, 85, 170), (255, 255, 255), (0, 0, 34), (255, 136, 0)]
PALMWB = [(170, 170, 170), (0, 0, 0), (255, 255, 255), (102, 136, 187), (153, 153, 153), (187, 187, 187), (187, 170, 153), (255, 170, 153)]


def icon_image(w, h, depth, vals, pick=None, onoff=0):
    rb = (w + 15) // 16 * 2
    data = contiguous(vals, w, depth)
    assert len(data) == rb * h * depth
    hdr = struct.pack(">hhhhhIBBI", 0, 0, w, h, depth, 1, (1 << depth) - 1 if pick is None else pick, onoff, 0)
    return hdr + data


def istr(s):
    b = s + b"\0"
    return be32(len(b)) + b


def diskobject(img1, img2=None, rev=1, deftool=None, tooltypes=None, drawer=False, extra=b"", w=0, h=0):
    g = struct.pack(">IhhhhHHHIIIIIHI", 0, 0, 0, w, h, 6 if img2 else 4, 3, 1, 1 if img1 else 0, 1 if img2 else 0, 0, 0, 0, 0, rev)
    d = be16(0xE310) + be16(1) + g
    d += bytes([1 if not drawer else 2, 0]) + be32(1 if deftool else 0) + be32(1 if tooltypes else 0)
    d += be32(0x80000000) + be32(0x80000000) + be32(1 if drawer else 0) + be32(0) + be32(4096)
    assert len(d) == 78
    if drawer:
        d += b"\0" * 56
    if img1:
        d += img1
    if img2:
        d += img2
    if deftool:
        d += istr(deftool)
    if tooltypes is not None:
        d += be32((len(tooltypes) + 1) * 4) + b"".join(istr(t) for t in tooltypes)
    if drawer and rev == 1:
        d += b"\0" * 6
    return d + extra


def newicons_encode(w, h, pal, idx, transparent, linelen=60):
    """NewIcons tooltype lines (IM1=/IM2= prefix added by the caller)"""
    ncol = len(pal)
    bpp = max(1, (ncol - 1).bit_length())

    def enc(bits):
        bits += "0" * (-len(bits) % 7)
        out = []
        groups = [int(bits[i:i + 7], 2) for i in range(0, len(bits), 7)]
        i = 0
        while i < len(groups):
            if groups[i] == 0:
                j = i
                while j < len(groups) and groups[j] == 0 and j - i < 47:
                    j += 1
                if j - i >= 2:
                    out.append(0xD0 + (j - i)); i = j; continue
            g = groups[i]
            out.append(0x20 + g if g < 0x50 else 0x51 + g)
            i += 1
        return out
    hdr = [ord("B" if transparent else "C"), w + 0x21, h + 0x21, (ncol >> 6) + 0x21, (ncol & 63) + 0x21]
    lines = []
    # palette: lines of whole 7-bit groups, each line started afresh
    palbits = "".join(format(c, "08b") for rgb in pal for c in rgb)
    per = linelen * 7 // 8 * 8 // 8 * 8  # bits per line, a whole number of bytes and of 7-bit groups
    per = (linelen * 7) // 56 * 56
    first = True
    for i in range(0, len(palbits), per):
        chunkb = palbits[i:i + per]
        ln = (hdr if first else []) + enc(chunkb)
        first = False
        lines.append(bytes(ln))
    if not lines:
        lines.append(bytes(hdr))
    pixbits = "".join(format(v, "0%db" % bpp) for v in idx)
    perp = (linelen * 7) // (7 * bpp) * (7 * bpp)
    for i in range(0, len(pixbits), perp):
        lines.append(bytes(enc(pixbits[i:i + perp])))
    return lines


def glow_rle(values, depth):
    bits = []
    i = 0
    while i < len(values):
        j = i
        while j < len(values) and values[j] == values[i] and j - i < 128:
            j += 1
        if j - i >= 3:
            bits.append(format(257 - (j - i), "08b") + format(values[i], "0%db" % depth)); i = j
        else:
            st = i
            while i < len(values) and i - st < 128 and not (i + 2 < len(values) and values[i] == values[i + 1] == values[i + 2]):
                i += 1
            bits.append(format(i - st - 1, "08b") + "".join(format(v, "0%db" % depth) for v in values[st:i]))
    s = "".join(bits)
    s += "0" * (-len(s) % 8)
    return bytes(int(s[k:k + 8], 2) for k in range(0, len(s), 8))


def glow_form(w, h, images):
    """images: list of (idx list, palette or None, transparent index or None, rle)"""
    chunks = [ichunk(b"FACE", bytes([w - 1, h - 1, 0, 0x11]) + be16(255))]
    for idx, pal, tcol, rle in images:
        depth = max(1, (max(idx)).bit_length())
        if pal is not None:
            depth = max(depth, (len(pal) - 1).bit_length())
        img = glow_rle(idx, depth) if rle else bytes(idx)
        flags = (1 if tcol is not None else 0) | (2 if pal is not None else 0)
        pdat = b""
        if pal is not None:
            raw = [c for rgb in pal for c in rgb]
            pdat = glow_rle(raw, 8) if rle else bytes(raw)
        ncol = len(pal) if pal is not None else 1
        hdr = bytes([tcol or 0, ncol - 1, flags, 1 if rle else 0, 1 if (rle and pal is not None) else 0, depth]) + be16(len(img) - 1) + be16(max(len(pdat), 1) - 1)
        chunks.append(ichunk(b"IMAG", hdr + img + pdat))
    return form(b"ICON", *chunks)


def make_icons():
    r = random.Random(500)
    w, h = 21, 13
    v2 = rand_vals(r, w, h, 2)
    add("icon_13.info", diskobject(icon_image(w, h, 2, v2), rev=0, deftool=b"SYS:Tool", tooltypes=[b"FILETYPE=X"], w=w, h=h), expect_indexed(v2, PAL13))
    add("icon_20.info", diskobject(icon_image(w, h, 2, v2), rev=1, drawer=True, w=w, h=h), expect_indexed(v2, PALMWB[:4]))
    v3 = rand_vals(r, w, h, 3)
    v3s = rand_vals(r, w, h, 3)
    add("icon_mwb.info", diskobject(icon_image(w, h, 3, v3), icon_image(w, h, 3, v3s), rev=1, tooltypes=[b"A=1", b"B"], w=w, h=h), expect_indexed(v3, PALMWB))
    add("icon_mwb_sel.info", diskobject(icon_image(w, h, 3, v3), icon_image(w, h, 3, v3s), rev=1, w=w, h=h), expect_indexed(v3s, PALMWB), "exact,sel")
    # PlanePick: one stored plane drawn into plane 1, plane 0 always on (PlaneOnOff)
    v1 = rand_vals(r, w, h, 1)
    add("icon_pick.info", diskobject(icon_image(w, h, 1, v1, pick=2, onoff=1), rev=1, w=w, h=h), expect_indexed([[1 | (v << 1) for v in row] for row in v1], PALMWB))
    # NewIcons: a palette over several lines, zero runs, transparency, both images
    nw, nh = 30, 19
    npal = rnd_pal(r, 40)
    nidx = [r.choice([0, 0, 0, 1, 2, r.randrange(40)]) for _ in range(nw * nh)]
    nidx2 = [r.randrange(40) for _ in range(nw * nh)]
    tts = [b"*** DON'T EDIT THE FOLLOWING LINES!! ***"] + [b"IM1=" + l for l in newicons_encode(nw, nh, npal, nidx, True)]
    tts += [b"IM2=" + l for l in newicons_encode(nw, nh, npal, nidx2, False)]
    expn = np.array([[npal[v][0], npal[v][1], npal[v][2], 0 if v == 0 else 255] for v in nidx], np.uint8).reshape(nh, nw, 4)
    expn2 = np.array([[npal[v][0], npal[v][1], npal[v][2], 255] for v in nidx2], np.uint8).reshape(nh, nw, 4)
    ni = diskobject(icon_image(w, h, 2, v2), rev=1, deftool=b"C:More", tooltypes=tts, w=w, h=h)
    add("icon_newicons.info", ni, expn)
    add("icon_newicons_sel.info", ni, expn2, "exact,sel")
    pal2 = rnd_pal(r, 2)
    nidx3 = [r.randrange(2) for _ in range(9 * 7)]
    tts = [b"IM1=" + l for l in newicons_encode(9, 7, pal2, nidx3, False, 20)]
    add("icon_newicons_2col.info", diskobject(icon_image(w, h, 2, v2), rev=1, tooltypes=tts, w=w, h=h),
        np.array([[pal2[v][0], pal2[v][1], pal2[v][2], 255] for v in nidx3], np.uint8).reshape(7, 9, 4))
    # GlowIcons: run-length and plain images, the selected one sharing the first's palette
    gw, gh = 26, 22
    gpal = rnd_pal(r, 70)
    gidx = [((x // 4) + (y // 3)) % 70 if r.random() < 0.8 else r.randrange(70) for y in range(gh) for x in range(gw)]
    gidx2 = [r.randrange(70) for _ in range(gw * gh)]
    glow = glow_form(gw, gh, [(gidx, gpal, 0, True), (gidx2, None, None, True)])
    expg = np.array([[gpal[v][0], gpal[v][1], gpal[v][2], 0 if v == 0 else 255] for v in gidx], np.uint8).reshape(gh, gw, 4)
    expg2 = np.array([[gpal[v][0], gpal[v][1], gpal[v][2], 255] for v in gidx2], np.uint8).reshape(gh, gw, 4)
    gi = diskobject(icon_image(w, h, 2, v2), rev=1, tooltypes=[b"X=Y"], extra=glow, w=w, h=h)
    add("icon_glow.info", gi, expg)
    add("icon_glow_sel.info", gi, expg2, "exact,sel")
    glow = glow_form(gw, gh, [(gidx, gpal, None, False)])
    add("icon_glow_raw.info", diskobject(icon_image(w, h, 2, v2), rev=1, drawer=True, extra=glow, w=w, h=h),
        np.array([[gpal[v][0], gpal[v][1], gpal[v][2], 255] for v in gidx], np.uint8).reshape(gh, gw, 4))
    # a PNG icon (OS4 / AROS): plain PNG, here followed by a second (selected) one
    pim = picture(24, 20, 7, True)
    b = io.BytesIO(); pim.save(b, "PNG"); b2 = io.BytesIO(); picture(24, 20, 8).save(b2, "PNG")
    add("icon_png.info", b.getvalue() + b2.getvalue(), np.array(pim.convert("RGBA")))


# ------------------------------------------------------------------ Sun raster, SGI
def make_sun_sgi():
    r = random.Random(600)
    w, h = 19, 11

    def sun(depth, typ, maptype, mapdata, rows):
        rb = (w * depth + 15) // 16 * 2
        data = b"".join(row + b"\0" * (rb - len(row)) for row in rows)
        if typ == 2:
            enc, i = bytearray(), 0
            while i < len(data):
                j = i
                while j < len(data) and data[j] == data[i] and j - i < 256:
                    j += 1
                n = j - i
                if n >= 3 or data[i] == 0x80:
                    if n == 1:
                        enc += b"\x80\x00"
                    else:
                        enc += bytes([0x80, n - 1, data[i]])
                    i = j
                else:
                    enc.append(data[i]); i += 1
            data = bytes(enc)
        return struct.pack(">8I", 0x59A66A95, w, h, depth, len(data), typ, maptype, len(mapdata)) + mapdata + data
    bits = rand_vals(r, w, h, 1)
    rows = [bytes(sum(bits[y][x] << (7 - (x & 7)) for x in range(k, min(k + 8, w))) for k in range(0, w, 8)) for y in range(h)]
    add("sun_1bit.ras", sun(1, 1, 0, b"", rows), expect_indexed(bits, [(255, 255, 255), (0, 0, 0)]))
    idx = rand_vals(r, w, h, 4); pal = rnd_pal(r, 16)
    mp = bytes(c[0] for c in pal) + bytes(c[1] for c in pal) + bytes(c[2] for c in pal)
    idx[0][:6] = [0x0] * 6
    add("sun_8bit.ras", sun(8, 1, 1, mp, [bytes(row) for row in idx]), expect_indexed(idx, pal))
    add("sun_8bit_rle.ras", sun(8, 2, 1, mp, [bytes(row) for row in idx]), expect_indexed(idx, pal))
    grey = rand_vals(r, w, h, 8)
    grey[2][3:10] = [0x80] * 7; grey[4][0] = 0x80
    add("sun_grey_rle.ras", sun(8, 2, 0, b"", [bytes(row) for row in grey]), expect_indexed(grey, [(i, i, i) for i in range(256)]))
    px = np.array([[[r.randrange(256) for _ in range(3)] for _ in range(w)] for _ in range(h)], np.uint8)
    px[5, 2:9] = (0x80, 0x80, 0x80)
    exp = np.concatenate([px, np.full((h, w, 1), 255, np.uint8)], 2)
    add("sun_24.ras", sun(24, 1, 0, b"", [px[y, :, ::-1].tobytes() for y in range(h)]), exp)
    add("sun_24_rgb.ras", sun(24, 3, 0, b"", [px[y].tobytes() for y in range(h)]), exp)
    add("sun_24_rle.ras", sun(24, 2, 0, b"", [px[y, :, ::-1].tobytes() for y in range(h)]), exp)
    add("sun_32.ras", sun(32, 1, 0, b"", [b"".join(b"\0" + bytes(px[y, x, ::-1]) for x in range(w)) for y in range(h)]), exp)

    def sgi(zs, bpc, rle, img):
        hdr = struct.pack(">HBBHHHHII4s80sI", 474, rle, bpc, 3 if zs > 1 else 2, w, h, zs, 0, 255 if bpc == 1 else 65535, b"", b"test", 0)
        hdr += b"\0" * (512 - len(hdr))
        chans = []
        for c in range(zs):
            for y in range(h):
                vals = [int(img[h - 1 - y, x, c]) for x in range(w)]
                chans.append(vals)
        def val(v):
            return be16(v * 257) if bpc == 2 else bytes([v])
        if not rle:
            return hdr + b"".join(val(v) for row in chans for v in row)
        rows = []
        for vals in chans:
            o, i = bytearray(), 0
            while i < len(vals):
                j = i
                while j < len(vals) and vals[j] == vals[i] and j - i < 127:
                    j += 1
                if j - i >= 2:
                    o += val(j - i) + val(vals[i]); i = j
                else:
                    st = i
                    while i < len(vals) and i - st < 127 and not (i + 1 < len(vals) and vals[i] == vals[i + 1]):
                        i += 1
                    o += val(0x80 | (i - st)) + b"".join(val(v) for v in vals[st:i])
            o += val(0)
            rows.append(bytes(o))
        tabsize = len(rows) * 8
        starts, data, off = [], b"", 512 + tabsize
        for rw in rows:
            starts.append(off + len(data)); data += rw
        return hdr + b"".join(be32(s) for s in starts) + b"".join(be32(len(rw)) for rw in rows) + data
    img = np.array([[[(x * 13) & 255, (y * 23) & 255, (x * y * 5) & 255, (x * 40) & 255] for x in range(w)] for y in range(h)], np.uint8)
    img[3:7, 4:12] = (10, 20, 30, 40)
    exp = img.copy(); exp[..., 3] = 255
    add("sgi_rgb.rgb", sgi(3, 1, 0, img), exp)
    add("sgi_rgb_rle.rgb", sgi(3, 1, 1, img), exp)
    add("sgi_rgba_rle.rgba", sgi(4, 1, 1, img), img)
    add("sgi_rgb16.rgb", sgi(3, 2, 0, img), exp)
    add("sgi_rgb16_rle.rgb", sgi(3, 2, 1, img), exp)
    g = np.stack([img[..., 0]] * 3 + [np.full((h, w), 255, np.uint8)], 2)
    add("sgi_grey.bw", sgi(1, 1, 1, img), g)
    ga = np.stack([img[..., 0]] * 3 + [img[..., 1]], 2)
    add("sgi_greyalpha.sgi", sgi(2, 1, 0, np.stack([img[..., 0], img[..., 1]], 2)), ga)


# ------------------------------------------------------------------ Atari ST
def st_words(vals, w, h, np_):
    out = bytearray()
    for y in range(h):
        for gx in range(0, w, 16):
            for p in range(np_):
                word = 0
                for k in range(16):
                    if (vals[y][gx + k] >> p) & 1:
                        word |= 0x8000 >> k
                out += be16(word)
    return bytes(out)


def st_lines(vals, w, h, np_):
    out = bytearray()
    for y in range(h):
        for p in range(np_):
            b = bytearray(w // 8)
            for x in range(w):
                if (vals[y][x] >> p) & 1:
                    b[x >> 3] |= 0x80 >> (x & 7)
            out += b
    return bytes(out)


def make_atari():
    r = random.Random(700)
    stpal = [r.randrange(0x1000) & 0x777 for _ in range(16)]
    stpal[0] = 0x777
    ste = [r.randrange(0x1000) for _ in range(16)]

    def pal_of(words):
        is_ste = any(w & 0x888 for w in words)
        out = []
        for wd in words:
            c = []
            for k in range(3):
                v = (wd >> (8 - 4 * k)) & 15
                c.append((((v & 7) << 1) | (v >> 3)) * 17 if is_ste else ((v & 7) * 255 + 3) // 7)
            out.append(tuple(c))
        return out
    palb = b"".join(be16(x) for x in stpal)
    for res, (w, h, np_) in enumerate([(320, 200, 4), (640, 200, 2), (640, 400, 1)]):
        vals = [[(x // 7 + y // 5 + (r.random() < 0.1)) % (1 << np_) for x in range(w)] for y in range(h)]
        data = be16(res) + palb + st_words(vals, w, h, np_)
        if res == 2:
            exp = expect_indexed(vals, [(255, 255, 255), (0, 0, 0)])
        else:
            exp = expect_indexed(vals, pal_of(stpal))
        add("degas.pi%d" % (res + 1), data, exp)
        comp = be16(0x8000 | res) + palb + byterun1(st_lines(vals, w, h, np_)) + b"\0" * 32
        add("degas.pc%d" % (res + 1), comp, exp)
        if res == 0:
            palb2 = b"".join(be16(x) for x in ste)
            add("degas_ste.pi1", be16(0) + palb2 + st_words(vals, w, h, np_) + b"\0" * 32, expect_indexed(vals, pal_of(ste)))
            neo = be16(0) + be16(0) + palb + b"NEO.NEO     " + b"\0" * (128 - 48)
            assert len(neo) == 128
            add("neo.neo", neo + st_words(vals, w, h, np_), exp)
    # high resolution with colour 0 dark: white on black
    vals = [[(x ^ y) >> 3 & 1 for x in range(640)] for y in range(400)]
    add("degas_inv.pi3", be16(2) + be16(0) + palb[2:] + st_words(vals, 640, 400, 1), expect_indexed(vals, [(0, 0, 0), (255, 255, 255)]))


def make_amiga():
    make_acbm()
    make_linepal()
    make_impulse()
    make_deep()
    make_icons()
    make_sun_sgi()
    make_atari()


# ------------------------------------------------------------------ main
def main():
    os.makedirs(OUT, exist_ok=True)
    if not features.check("webp"):
        sys.exit("Pillow has no WebP support")
    make_webp()
    make_amiga()
    with open(os.path.join(OUT, "manifest.txt"), "w") as f:
        f.write("\n".join(manifest) + "\n")
    print("%d test files in %s" % (len(manifest), OUT))




if __name__ == "__main__":
    main()
