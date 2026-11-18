#!/usr/bin/env python3
"""check_files.py - independent checks of round 3's file work (tests/filestest.c), run after it with
the same folder:
    python3 tests/check_files.py FOLDER

- GIMP: ImageMagick (when installed) flattens the simple .xcf, and Easel's composite of the same file
  (NAME.easel.png, written by filestest) must match it.
- Every other check is listed by the files filestest leaves: each check_*.txt names a file and what
  an independent reader must find in it (see the functions below).
"""
import os
import shutil
import subprocess
import sys

from PIL import Image

out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/easel_foreign"
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


def maxdiff(a, b):
    a = a.convert("RGBA"); b = b.convert("RGBA")
    if a.size != b.size: return 999
    m = 0
    for p, q in zip(zip(*[iter(a.tobytes())] * 4), zip(*[iter(b.tobytes())] * 4)):
        if p[3] == 0 and q[3] == 0: continue
        m = max(m, max(abs(x - y) for x, y in zip(p, q)))
    return m


def magick_flat(src, dst):
    exe = shutil.which("magick") or shutil.which("convert")
    if not exe: return False
    args = [exe, src, "-background", "none", "-flatten", dst]
    return subprocess.run(args, capture_output=True).returncode == 0 and os.path.exists(dst)


# GIMP's .xcf through ImageMagick
if shutil.which("magick") or shutil.which("convert"):
    for name in ["im_simple.xcf"]:
        im = path(name + ".im.png")
        if magick_flat(path(name), im):
            d = maxdiff(Image.open(im), Image.open(path(name + ".easel.png")))
            ok("%s: Easel's composite matches ImageMagick's (off by %d)" % (name, d), d <= 1)
        else:
            ok("%s: ImageMagick reads it" % name, False)
else:
    skipped.append("ImageMagick (XCF)")

# the checks filestest asks for, one per line: KIND FILE ARGS...
listing = path("checks.txt")
if os.path.exists(listing):
    for line in open(listing):
        w = line.split()
        if not w: continue
        kind, f, args = w[0], path(w[1]), w[2:]
        if kind == "exif":                   # Pillow finds the EXIF tag (id) with the value
            try:
                ex = Image.open(f).getexif()
                v = ex.get(int(args[0]))
                if v is None:
                    v = ex.get_ifd(0x8769).get(int(args[0]))
                ok("%s: EXIF tag %s is %s (Pillow found %r)" % (w[1], args[0], args[1], v), str(v) == args[1])
            except Exception as e:
                ok("%s: Pillow reads its EXIF (%s)" % (w[1], e), False)
        elif kind == "same":                 # Pillow decodes the file to the reference picture's pixels
            try:
                d = maxdiff(Image.open(f), Image.open(path(args[0])))
                ok("%s: Pillow decodes it as %s (off by %d)" % (w[1], args[0], d), d <= int(args[1]))
            except Exception as e:
                ok("%s: Pillow reads it (%s)" % (w[1], e), False)
        elif kind == "magick":               # ImageMagick flattens the file to the reference picture
            if not (shutil.which("magick") or shutil.which("convert")):
                skipped.append("ImageMagick (%s)" % w[1]); continue
            im = f + ".im.png"
            if magick_flat(f, im):
                d = maxdiff(Image.open(im), Image.open(path(args[0])))
                ok("%s: ImageMagick flattens it as %s (off by %d)" % (w[1], args[0], d), d <= int(args[1]))
            else:
                ok("%s: ImageMagick reads it" % w[1], False)
        elif kind == "frames":               # ImageMagick finds N pictures (a layered TIFF's flat one and its layers) and the geometry
            exe = shutil.which("magick") or shutil.which("identify")
            if not exe:
                skipped.append("ImageMagick (%s)" % w[1]); continue
            cmd = [exe] + (["identify"] if exe.endswith("magick") else []) + ["-format", "%W %H %w %h %X %Y\n", f]
            res = subprocess.run(cmd, capture_output=True, text=True).stdout.split("\n")
            res = [r for r in res if r.strip()]
            ok("%s: ImageMagick finds %s pictures (found %d)" % (w[1], args[0], len(res)), len(res) == int(args[0]))
            if len(args) > 1:
                ok("%s: picture %s is %s (ImageMagick: %s)" % (w[1], args[1], " ".join(args[2:]), res[int(args[1])] if len(res) > int(args[1]) else None),
                   len(res) > int(args[1]) and res[int(args[1])].replace("+", "").split() == args[2:])
        elif kind == "grd":                  # psd-tools' descriptor reader takes a .grd: N gradients, the first's name
            try:
                from psd_tools.psd.descriptor import Descriptor
            except ImportError:
                skipped.append("psd-tools (%s)" % w[1]); continue
            try:
                data = open(f, "rb").read()
                ok("%s: a Photoshop gradient file, version 5" % w[1], data[:4] == b"8BGR" and data[4:6] == b"\0\5")
                import io
                d = Descriptor.read(io.BytesIO(data[10:]))
                grads = d[b"GrdL"]
                nm = grads[0][b"Grad"][b"Nm  "]
                name = str(getattr(nm, "value", nm)).strip("\0")
                ok("%s: psd-tools finds %s gradients (%d), the first %s (%r)" % (w[1], args[0], len(grads), " ".join(args[1:]), name),
                   len(grads) == int(args[0]) and name == " ".join(args[1:]))
            except Exception as e:
                ok("%s: psd-tools reads it (%s)" % (w[1], e), False)
        elif kind == "psdtools":             # psd-tools reads the file: N layers and the composite
            try:
                from psd_tools import PSDImage
            except ImportError:
                skipped.append("psd-tools (%s)" % w[1]); continue
            try:
                p = PSDImage.open(f)
                names = [l.name for l in p.descendants()]
                ok("%s: psd-tools finds %s layers (found %d: %s)" % (w[1], args[0], len(names), names), len(names) == int(args[0]))
                ok("%s: version %s" % (w[1], args[1]), p.version == int(args[1]))
                d = maxdiff(p.composite(), Image.open(path(args[2])))
                ok("%s: psd-tools' composite is %s (off by %d)" % (w[1], args[2], d), d <= int(args[3]))
            except Exception as e:
                ok("%s: psd-tools reads it (%s)" % (w[1], e), False)
        else:
            ok("unknown check " + kind, False)

if skipped: print("skipped (not installed):", ", ".join(sorted(set(skipped))))
print("%d checks, %d failed" % (tests, fails))
sys.exit(1 if fails else 0)
