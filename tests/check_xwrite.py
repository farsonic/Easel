#!/usr/bin/env python3
"""check_xwrite.py - independent checks of the files tests/xwritetest.c writes (Easel 1.1).

Run after xwritetest, with the same folder (default /tmp/easel_xwrite_out):
    python3 tests/check_xwrite.py [folder]

- Pillow decodes every WebP (lossless), PPM/PGM/PAM/PBM, QOI, ICO, uncompressed DDS and FITS
  exactly as the reference picture Easel saved beside it (name.rgba); its BC1/BC3 DDS decoding
  must be close.
- ImageMagick, when installed, decodes the DPX files (8 and 10 bits) the same.
- poppler's pdftoppm, when installed, renders each PDF at its own resolution to the reference
  picture (over white for the one with transparency); pdfimages lists one image (and a soft mask).
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

from PIL import Image

out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/easel_xwrite_out"
fails = 0
tests = 0
skipped = []


def ok(name, cond):
    global fails, tests
    tests += 1
    if not cond:
        fails += 1
        print("FAIL:", name)


def path(name):
    return os.path.join(out, name)


def ref(name):
    with open(path(name + ".rgba"), "rb") as f:
        d = f.read()
    w, h = struct.unpack(">II", d[4:12])
    return Image.frombytes("RGBA", (w, h), d[12:])


def diff(a, b):
    """the largest and the mean difference of two RGBA pictures"""
    if a.size != b.size:
        return 999, 999.0
    pa, pb = a.tobytes(), b.tobytes()
    big = 0
    tot = 0
    for x, y in zip(pa, pb):
        d = abs(x - y)
        tot += d
        if d > big:
            big = d
    return big, tot / max(1, len(pa))


def as_rgba(im):
    if im.mode == "1":
        im = im.convert("L")
    return im.convert("RGBA")


exact = ["pic.webp", "alpha.webp", "grey.webp", "flat.webp", "noise.webp", "stripes.webp", "one.webp", "wide.webp",
         "pic.ppm", "grey.pgm", "bw.pbm", "pic.qoi", "alpha.qoi", "noise.qoi", "sq48.ico",
         "alpha_rgba.dds", "alpha_mip.dds"]
for name in exact:
    try:
        im = Image.open(path(name))
        if name.endswith(".ico"):
            im.size = max(im.info.get("sizes", [im.size]))
        big, _ = diff(as_rgba(im), ref(name))
        ok(name + " (Pillow, largest difference %d)" % big, big == 0)
    except Exception as e:
        ok(name + " opens in Pillow: %s" % e, False)

# the WebP files really are lossless VP8L
for name in exact[:8]:
    with open(path(name), "rb") as f:
        d = f.read()
    ok(name + " is VP8L", d[12:16] == b"VP8L" and struct.unpack("<I", d[4:8])[0] == len(d) - 8)

# ICO: every size Pillow lists is there
im = Image.open(path("sq48.ico"))
ok("sq48.ico sizes", sorted(im.info.get("sizes", [])) == [(16, 16), (24, 24), (32, 32), (48, 48)])
im = Image.open(path("big.ico"))
ok("big.ico has 256", (256, 256) in im.info.get("sizes", []))

# FITS: Pillow keeps the values (8-bit), the bottom row first in the file (it reads one plane only;
# the three-plane file is checked through ImageMagick below)
for name in ("grey.fits",):
    try:
        im = Image.open(path(name))
        im.load()
        r = ref(name)
        if im.mode in ("L", "I", "F"):
            g = im.convert("L")
            big, _ = diff(Image.merge("RGBA", (g, g, g, Image.new("L", g.size, 255))), r)
        else:
            big, _ = diff(as_rgba(im), r)
        ok(name + " (Pillow, largest difference %d)" % big, big <= 1)
    except Exception as e:
        # Pillow reads only some FITS layouts (three planes may not be among them)
        if name == "pic.fits":
            skipped.append("pic.fits (%s)" % e)
        else:
            ok(name + " opens in Pillow: %s" % e, False)

# BC1/BC3: close to the picture
for name in ("pic_bc1.dds", "alpha_bc3.dds"):
    try:
        big, mean = diff(as_rgba(Image.open(path(name))), ref(name))
        ok(name + " (Pillow, mean difference %.2f)" % mean, mean < 6.0)
    except Exception as e:
        ok(name + " opens in Pillow: %s" % e, False)

# DPX through ImageMagick
magick = shutil.which("magick") or shutil.which("convert")
if magick:
    for name in ("pic8.dpx", "pic10.dpx", "alpha8.dpx", "alpha10.dpx"):
        with tempfile.TemporaryDirectory() as td:
            png = os.path.join(td, "o.png")
            r = subprocess.run([magick, path(name), "-depth", "8", png], capture_output=True)
            if r.returncode or not os.path.exists(png):
                ok(name + " opens in ImageMagick: " + r.stderr.decode(errors="replace")[:200], False)
                continue
            big, _ = diff(as_rgba(Image.open(png)), ref(name))
            ok(name + " (ImageMagick, largest difference %d)" % big, big <= 1)
    # PAM (Pillow has no PAM reader) and the three planes of the colour FITS
    with tempfile.TemporaryDirectory() as td:
        png = os.path.join(td, "o.png")
        r = subprocess.run([magick, path("alpha.pam"), png], capture_output=True)
        big = diff(as_rgba(Image.open(png)), ref("alpha.pam"))[0] if not r.returncode and os.path.exists(png) else 999
        ok("alpha.pam (ImageMagick, largest difference %d)" % big, big == 0)
        rr = ref("pic.fits")
        for c in range(3):
            g = os.path.join(td, "p%d.pgm" % c)
            r = subprocess.run([magick, path("pic.fits") + "[%d]" % c, "-depth", "8", g], capture_output=True)
            if r.returncode or not os.path.exists(g):
                ok("pic.fits plane %d opens in ImageMagick" % c, False)
                continue
            big = diff(Image.open(g).convert("L").convert("RGBA"), rr.getchannel(c).convert("RGBA"))[0]
            ok("pic.fits plane %d (ImageMagick, largest difference %d)" % (c, big), big <= 1)
else:
    skipped.append("DPX, PAM and colour FITS (no ImageMagick)")

# PDF through poppler
if shutil.which("pdftoppm"):
    for name in ("pic.pdf", "alpha.pdf", "jpeg.pdf"):
        with tempfile.TemporaryDirectory() as td:
            r = subprocess.run(["pdftoppm", "-r", "144", "-png", "-singlefile", path(name), os.path.join(td, "p")], capture_output=True)
            png = os.path.join(td, "p.png")
            if r.returncode or not os.path.exists(png):
                ok(name + " renders: " + r.stderr.decode(errors="replace")[:200], False)
                continue
            got = Image.open(png).convert("RGB")
            refname = "pic.pdf" if name == "jpeg.pdf" else name
            rr = ref(refname)
            white = Image.new("RGBA", rr.size, (255, 255, 255, 255))
            white.alpha_composite(rr)
            want = white.convert("RGB")
            ok(name + " page size %s" % (got.size,), got.size == want.size)
            if got.size == want.size:
                # (poppler resamples even at 1:1, so the rendering is only roughly the picture)
                big, mean = diff(got.convert("RGBA"), want.convert("RGBA"))
                ok(name + " (pdftoppm, mean difference %.2f)" % mean, mean < 12.0)
            if shutil.which("pdfimages") and name != "jpeg.pdf":
                # the image itself, exactly, through poppler's decoder (and its soft mask)
                subprocess.run(["pdfimages", "-png", path(name), os.path.join(td, "i")], capture_output=True)
                imgs = sorted(f for f in os.listdir(td) if f.startswith("i-"))
                if not imgs:
                    ok(name + " pdfimages extracts the image", False)
                    continue
                im = Image.open(os.path.join(td, imgs[0])).convert("RGB")
                if len(imgs) > 1:
                    im.putalpha(Image.open(os.path.join(td, imgs[1])).convert("L"))
                big = diff(im.convert("RGBA"), rr)[0]
                ok(name + " image (pdfimages, largest difference %d)" % big, big == 0)
    if shutil.which("pdfimages"):
        r = subprocess.run(["pdfimages", "-list", path("alpha.pdf")], capture_output=True, text=True)
        ok("alpha.pdf has an image and a soft mask", "smask" in r.stdout and "image" in r.stdout)
    if shutil.which("pdfinfo"):
        r = subprocess.run(["pdfinfo", path("pic.pdf")], capture_output=True, text=True)
        ok("pic.pdf title", "Easel été" in r.stdout)
else:
    skipped.append("PDF (no pdftoppm)")

# SVG: well-formed, every path's data there, and (ImageMagick) drawn where Easel put the shapes
import xml.etree.ElementTree as ET
try:
    root = ET.parse(path("shapes.svg")).getroot()
    ns = "{http://www.w3.org/2000/svg}"
    paths = root.findall(ns + "path")
    ok("shapes.svg: six paths", len(paths) == 6 and all(p.get("d", "").startswith("M ") for p in paths))
    ok("shapes.svg: size", root.get("width") == "200" and root.get("viewBox") == "0 0 200 100")
except Exception as e:
    ok("shapes.svg parses: %s" % e, False)
if magick:
    with tempfile.TemporaryDirectory() as td:
        png = os.path.join(td, "s.png")
        r = subprocess.run([magick, "-background", "white", path("shapes.svg"), png], capture_output=True)
        if r.returncode or not os.path.exists(png):
            skipped.append("SVG rendering (ImageMagick couldn't: %s)" % r.stderr.decode(errors="replace")[:80])
        else:
            im = Image.open(png).convert("RGB")
            ok("shapes.svg drawn: red box", im.size == (200, 100) and im.getpixel((35, 25)) == (255, 0, 0))
            ok("shapes.svg drawn: blue dot", im.getpixel((125, 15)) == (0, 0, 255))
            ok("shapes.svg drawn: green triangle", im.getpixel((120, 66)) == (0, 128, 0))

for s in skipped:
    print("skipped:", s)
print("%d checks, %d failed" % (tests, fails))
sys.exit(1 if fails else 0)
