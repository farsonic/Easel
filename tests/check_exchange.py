#!/usr/bin/env python3
"""check_exchange.py - independent checks of the files tests/exchangetest.c writes (Easel 0.6).

Run after exchangetest, with the same folder (default /tmp/easel_exchange_out):
    python3 tests/check_exchange.py [folder]

- Every TIFF (each compression with each colour mode) and Targa (raw and RLE; true colour, grey
  and colour-mapped) is decoded by Pillow and must give exactly the reference picture Easel saved
  beside it, with the compression and resolution Easel wrote.
- The layered PSD's composite, as Pillow reads it, must match Easel's own composite; with
  psd-tools installed its layers are checked too: names (from luni), kinds, the group nesting,
  opacity, blend modes, visibility, clipping, masks and their density and feather.
- ImageMagick, when installed, must decode the same TIFFs, Targas and PSD composite identically.
- The File Info files: PNG text chunks, the JPEG comment and XMP, TIFF tags and PSD's XMP resource.
psd-tools is optional: without it those checks are skipped (and said to be).
"""
import os
import shutil
import struct
import subprocess
import sys

from PIL import Image

out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/easel_exchange_out"
fails = 0
tests = 0


def ok(name, cond):
    global fails, tests
    tests += 1
    if not cond:
        fails += 1
        print("FAIL:", name)


def path(name):
    return os.path.join(out, name)


def read_rgba(name):
    with open(path(name), "rb") as f:
        d = f.read()
    assert d[:4] == b"RGBA"
    w, h = struct.unpack(">II", d[4:12])
    return Image.frombytes("RGBA", (w, h), d[12:])


# ------------------------------------------------------------------ TIFF
COMP = {"None": "raw", "PackBits": "packbits", "LZW": "tiff_lzw", "Deflate": "tiff_adobe_deflate"}
PIL_MODE = {"rgb": "RGB", "rgba": "RGBA", "gray": "L", "graya": "LA", "indexed": "P", "bitmap": "1"}
for mode in PIL_MODE:
    ref = read_rgba("ref_%s.rgba" % mode)
    for comp, pil_comp in COMP.items():
        name = "%s_%s.tif" % (mode, comp)
        im = Image.open(path(name))
        ok(name + " mode " + im.mode, im.mode == PIL_MODE[mode])
        ok(name + " compression", im.info.get("compression") == pil_comp)
        ok(name + " dpi", tuple(round(v) for v in im.info.get("dpi", (0, 0))) == (150, 150))
        ok(name + " pixels", im.convert("RGBA").tobytes() == ref.tobytes())
im = Image.open(path("smooth_lzw.tif"))
ok("smooth_lzw.tif", im.convert("RGBA").tobytes() == read_rgba("ref_smooth.rgba").tobytes())

# ------------------------------------------------------------------ Targa
def tga_mapped(name):
    """a colour-mapped Targa decoded here from the TGA 2.0 specification (Pillow can't take a
    32-bit colour map): header, map, then raw or RLE indices"""
    with open(path(name), "rb") as f:
        d = f.read()
    idl, cmt, typ = d[0], d[1], d[2]
    cmfirst, cmlen, cmdepth = struct.unpack("<HHB", d[3:8])
    w, h, depth, desc = struct.unpack("<HHBB", d[12:18])
    assert cmt == 1 and typ in (1, 9) and depth == 8
    pos = 18 + idl
    eb = cmdepth // 8
    cmap = []
    for i in range(cmlen):
        e = d[pos + i * eb:pos + i * eb + eb]
        cmap.append(bytes((e[2], e[1], e[0], e[3] if eb == 4 else 255)))
    pos += cmlen * eb
    idx = bytearray()
    while len(idx) < w * h:
        if typ == 1:
            idx += d[pos:pos + w * h]
            break
        c = d[pos]
        pos += 1
        n = (c & 0x7F) + 1
        if c & 0x80:
            idx += bytes([d[pos]]) * n
            pos += 1
        else:
            idx += d[pos:pos + n]
            pos += n
    rows = [b"".join(cmap[i - cmfirst] for i in idx[y * w:(y + 1) * w]) for y in range(h)]
    if not desc & 0x20:
        rows.reverse()
    return Image.frombytes("RGBA", (w, h), b"".join(rows))


for mode in ("rgb", "rgba", "gray", "graya", "indexed", "indexed_trans"):
    ref = read_rgba("tref_%s.rgba" % mode)
    for rle in ("raw", "rle"):
        name = "%s_%s.tga" % (mode, rle)
        im = Image.open(path(name))
        ok(name + " compression", (im.info.get("compression") == "tga_rle") == (rle == "rle"))
        try:
            got = im.convert("RGBA")
        except ValueError:            # Pillow and 32-bit colour maps
            got = tga_mapped(name)
        if mode == "indexed_trans":   # transparent pixels: only their alpha matters
            a = bytearray(got.tobytes())
            for k in range(0, len(a), 4):
                if a[k + 3] == 0:
                    a[k:k + 4] = b"\0\0\0\0"
            got = Image.frombytes("RGBA", got.size, bytes(a))
        ok(name + " pixels", got.tobytes() == ref.tobytes())

# ------------------------------------------------------------------ PSD
ref = read_rgba("psd_composite.rgba")
im = Image.open(path("layers.psd"))
ok("layers.psd size", im.size == ref.size)
ok("layers.psd merged image", im.convert("RGBA").tobytes() == ref.tobytes())

try:
    from psd_tools import PSDImage
except ImportError:
    PSDImage = None
    print("psd-tools isn't installed: the PSD layer checks are skipped")
if PSDImage:
    psd = PSDImage.open(path("layers.psd"))
    ok("psd-tools: size and depth", psd.size == (48, 40) and psd.depth == 8)
    # (name, kind, depth, opacity, blend, visible, clipping) from the bottom, as exchangetest makes them
    want = [("Background", "pixel", 0, 255, "NORMAL", True, False),
            ("Ünïcödé layer", "pixel", 0, 128, "MULTIPLY", True, False),
            ("Clipped", "pixel", 0, 255, "NORMAL", False, True),
            ("Inner A", "pixel", 2, 255, "NORMAL", True, False),
            ("Masked", "pixel", 2, 255, "NORMAL", True, False),
            ("Inner", "group", 1, 200, "SCREEN", True, False),
            ("Outer B", "pixel", 1, 255, "LINEAR_LIGHT", True, False),
            ("Outer", "group", 0, 255, "PASS_THROUGH", True, False),
            ("Top", "pixel", 0, 255, "DIFFERENCE", True, False)]
    got = []

    def walk(group, depth):
        # psd-tools lists each group's layers bottom first; a group comes after its members, as in Easel
        for layer in group:
            if layer.is_group():
                walk(layer, depth + 1)
            got.append((layer.name, layer.kind, depth, layer.opacity, layer.blend_mode.name, layer.visible,
                        bool(layer.clipping)))
    walk(psd, 0)
    ok("psd-tools: layers %r" % got, got == want)
    layers = {l.name: l for l in psd.descendants()}
    m = layers["Masked"]
    ok("psd-tools: mask", m.has_mask() and not m.mask.disabled)
    p = m.mask.parameters
    ok("psd-tools: mask density and feather", p is not None and p.user_mask_density == 204 and abs(p.user_mask_feather - 3) < 1e-9)
    ok("psd-tools: mask box", m.mask.bbox == (21, 16, 48, 40))
    t = layers["Top"]
    ok("psd-tools: mask turned off", t.has_mask() and t.mask.disabled and t.mask.background_color == 0)
    ok("psd-tools: fill opacity", abs(layers["Ünïcödé layer"].fill_opacity - 153) <= 1)
    ok("psd-tools: bounding boxes", layers["Clipped"].bbox == (10, 10, 40, 30) and layers["Inner A"].bbox == (0, 0, 48, 12))
    ok("psd-tools: merged image", psd.topil().convert("RGBA").tobytes() == ref.tobytes())
    ok("psd-tools: layer pixels", layers["Clipped"].topil().size == (30, 20))
    info = PSDImage.open(path("info.psd"))
    xmp = info.image_resources.get_data(1060)
    ok("psd-tools: XMP resource", xmp is not None and "Jürgen" in bytes(xmp).decode("utf-8"))

# ------------------------------------------------------------------ ImageMagick, when it is there
def magick(name):
    try:
        r = subprocess.run(["convert", path(name), "-depth", "8", "rgba:-"], capture_output=True, timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return r.stdout if r.returncode == 0 else b""


if shutil.which("convert"):
    for mode in PIL_MODE:
        ref = read_rgba("ref_%s.rgba" % mode).tobytes()
        for comp in COMP:
            name = "%s_%s.tif" % (mode, comp)
            ok("ImageMagick " + name, magick(name) == ref)
    for mode in ("rgb", "rgba", "gray", "graya", "indexed"):
        ref = read_rgba("tref_%s.rgba" % mode).tobytes()
        for rle in ("raw", "rle"):
            name = "%s_%s.tga" % (mode, rle)
            ok("ImageMagick " + name, magick(name) == ref)
    ok("ImageMagick layers.psd", magick("layers.psd[0]") == read_rgba("psd_composite.rgba").tobytes())
else:
    print("ImageMagick isn't installed: its checks are skipped")

# ------------------------------------------------------------------ File Info
TITLE = 'Café <night> & "day"'
im = Image.open(path("info.png"))
t = im.text
ok("png text", t.get("Title") == TITLE and t.get("Author") == "Jürgen Łukasz" and t.get("Keywords") == "sea, boats, evening"
   and t.get("Copyright") == "© 2026 Somebody" and t.get("Description") == "A picture\nof two lines"
   and t.get("URL") == "https://example.org/pictures?a=1&b=2" and "dc:subject" in t.get("XML:com.adobe.xmp", ""))
im = Image.open(path("info.jpg"))
ok("jpeg comment", im.info.get("comment") == b"A picture\nof two lines")
xmp = [d for m, d in im.applist if m == "APP1" and d.startswith(b"http://ns.adobe.com/xap/1.0/\0")]
ok("jpeg XMP", len(xmp) == 1 and "Café &lt;night&gt;".encode() in xmp[0])
im = Image.open(path("info.tif"))
tags = im.tag_v2
ok("tiff tags", tags.get(270) == "A picture\nof two lines" and tags.get(269) == TITLE and tags.get(315) == "Jürgen ?ukasz"
   and tags.get(33432) == "© 2026 Somebody" and tags.get(305) == "Easel")
ok("tiff XMP", b"<dc:rights>" in bytes(tags.get(700, b"")))

print("%d checks, %d failed" % (tests, fails))
sys.exit(1 if fails else 0)
