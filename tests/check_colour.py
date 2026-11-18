#!/usr/bin/env python3
"""check_colour.py - Easel 1.2's colour management checked by independent readers: Pillow reads the
ICC profiles Easel embedded in PNG, JPEG, TIFF, PSD and WebP files byte for byte; LittleCMS (through
Pillow's ImageCms) accepts Easel's built-in sRGB and Adobe RGB profiles and its test LUT profiles,
and converts the test ramp to Adobe RGB within a level or two of Easel's own CMS; Pillow reads the
CMYK TIFF and JPEG, the CIELab TIFF and the palette BMP in their modes; ImageMagick, where installed,
agrees on the colour spaces. Run with the directory tests/colourtest wrote its files to."""
import io, os, subprocess, sys, shutil
from PIL import Image, ImageCms

d = sys.argv[1] if len(sys.argv) > 1 else "/tmp/easel_colour_out"
fails = checks = 0
def check(ok, msg):
    global fails, checks
    checks += 1
    if not ok:
        fails += 1
        print("FAIL", msg)
def rd(name):
    with open(os.path.join(d, name), "rb") as f:
        return f.read()

adobe = rd("adobergb.icc")
for name in ("icc.png", "icc.jpg", "icc.tif", "icc.psd", "icc.webp"):
    im = Image.open(os.path.join(d, name))
    im.load()
    check(im.size == (40, 30), name + " size")
    check(im.info.get("icc_profile") == adobe, name + " carries Easel's Adobe RGB profile")

profs = {}
for name in ("srgb.icc", "adobergb.icc", "lutxyz.icc", "lutlab.icc"):
    try:
        profs[name] = ImageCms.getOpenProfile(io.BytesIO(rd(name)))
        ok = True
    except Exception as e:
        ok = False
        print(name, e)
    check(ok, "LittleCMS opens " + name)
desc = ImageCms.getProfileDescription(profs["srgb.icc"]).strip()
check(desc.startswith("sRGB"), "the sRGB profile's description (%r)" % desc)

W, H = 64, 48
ramp = Image.frombytes("RGBA", (W, H), rd("ramp.rgba")).convert("RGB")
def compare(easel_file, src, dst, intent, limit_max, limit_mean):
    ours = Image.frombytes("RGBA", (W, H), rd(easel_file)).convert("RGB")
    t = ImageCms.buildTransform(src, dst, "RGB", "RGB", renderingIntent=intent)
    lc = ImageCms.applyTransform(ramp, t)
    a, b = ours.tobytes(), lc.tobytes()
    diffs = [abs(x - y) for x, y in zip(a, b)]
    mx, mean = max(diffs), sum(diffs) / len(diffs)
    check(mx <= limit_max and mean <= limit_mean, "%s against LittleCMS: max %d, mean %.2f" % (easel_file, mx, mean))
compare("ramp_adobe.rgba", profs["srgb.icc"], profs["adobergb.icc"], ImageCms.Intent.RELATIVE_COLORIMETRIC, 2, 0.6)
compare("ramp_lutlab_adobe.rgba", profs["lutlab.icc"], profs["adobergb.icc"], ImageCms.Intent.RELATIVE_COLORIMETRIC, 10, 1.0)

im = Image.open(os.path.join(d, "cmyk.tif")); im.load()
check(im.mode == "CMYK" and im.size == (32, 24), "CMYK TIFF read as CMYK (%s)" % im.mode)
c, m, y, k = im.getpixel((0, 0))
check(k > 200 and c + m + y + k <= 3 * 255 + 2, "its top left is a rich black within 300%% ink (%d %d %d %d)" % (c, m, y, k))
im = Image.open(os.path.join(d, "cmyk.jpg")); im.load()
check(im.mode == "CMYK", "CMYK JPEG read as CMYK (%s)" % im.mode)
c, m, y, k = im.getpixel((0, 0))
check(k > 190, "its top left is mostly black ink (%d %d %d %d)" % (c, m, y, k))
im = Image.open(os.path.join(d, "lab.tif")); im.load()
check(im.mode == "LAB", "CIELab TIFF read as LAB (%s)" % im.mode)
im = Image.open(os.path.join(d, "pal.bmp")); im.load()
check(im.mode == "P" and len(im.getpalette()) // 3 >= 7, "palette BMP read as P")

def cmp_rgb(a, b):
    A, B = a.convert("RGB").tobytes(), b.convert("RGB").tobytes()
    if len(A) != len(B):
        return 999, 999
    diffs = [abs(x - y) for x, y in zip(A, B)]
    return max(diffs), sum(diffs) / len(diffs)
if shutil.which("convert"):
    tmp = os.path.join(d, "im_check.png")
    # Cineon: ImageMagick's log-to-linear (95/685, gamma 1.7) against Easel's
    subprocess.run(["convert", os.path.join(d, "cineon.cin"), tmp], check=True)
    mx, mean = cmp_rgb(Image.open(tmp), Image.open(os.path.join(d, "cineon_easel.png")))
    check(mx <= 2 and mean <= 0.5, "ImageMagick reads Easel's Cineon file as Easel does (max %d, mean %.2f)" % (mx, mean))
    mx, mean = cmp_rgb(Image.open(tmp), Image.open(os.path.join(d, "ramp50.png")))
    check(mx <= 6 and mean <= 1, "and as the picture written (max %d, mean %.2f)" % (mx, mean))
    # Radiance HDR: linear RGBE, made sRGB again by ImageMagick
    subprocess.run(["convert", os.path.join(d, "ramp50.hdr"), "-colorspace", "sRGB", tmp], check=True)
    mx, mean = cmp_rgb(Image.open(tmp), Image.open(os.path.join(d, "ramp50.png")))
    # (RGBE shares one exponent: a dark channel beside a bright one keeps few bits)
    check(mx <= 24 and mean <= 1, "ImageMagick reads Easel's HDR file as the picture (max %d, mean %.2f)" % (mx, mean))
    # DICOM: ImageMagick gives the stored values; the rescale (-1024) and window (1000/2000) applied here
    subprocess.run(["convert", os.path.join(d, "test.dcm"), tmp], check=True)
    raw = Image.open(tmp)
    ours = Image.open(os.path.join(d, "dicom_easel.png")).convert("L")
    bad = 0
    for y in range(raw.size[1]):
        for x in range(raw.size[0]):
            v = raw.getpixel((x, y)) - 1024
            t = (v - (1000 - 0.5 - 1999 / 2.0)) / 1999.0
            want = int(max(0, min(1, t)) * 255 + 0.5)
            if abs(want - ours.getpixel((x, y))) > 1:
                bad += 1
    check(raw.size == (40, 24) and bad == 0, "ImageMagick's DICOM samples, windowed, are Easel's (%d differ)" % bad)
    os.remove(tmp)

if shutil.which("identify"):
    for name, want in (("cmyk.jpg", ("CMYK",)), ("cmyk.tif", ("CMYK",)), ("lab.tif", ("Lab", "CIELab"))):
        out = subprocess.run(["identify", "-format", "%[colorspace]", os.path.join(d, name)], capture_output=True, text=True).stdout
        check(out.strip() in want, "ImageMagick: %s is %s (%s)" % (name, want[0], out.strip()))
print("%d checks, %d failed" % (checks, fails))
sys.exit(1 if fails else 0)
