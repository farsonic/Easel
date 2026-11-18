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
# OpenType substitutions: the glyphs Easel drew for each feature against fontTools' reading of the
# same GSUB lookups (the feature's lookups for Latin, in order: single, alternate, ligature)
gpath = os.path.join(d, "gsub.txt")
if os.path.exists(gpath):
    try:
        from fontTools.ttLib import TTFont
    except ImportError:
        TTFont = None
    if TTFont:
        fonts = {}
        for line in open(gpath):
            parts = line.split()
            path, tag, text, got = parts[0], parts[1], parts[2], [int(v) for v in parts[3:]]
            if path not in fonts:
                fonts[path] = TTFont(path)
            f = fonts[path]
            gsub = f["GSUB"].table
            cmap = f.getBestCmap()
            names = [cmap[ord(ch)] for ch in text]
            if tag == "smcp":
                pass
            script = None
            for sr in gsub.ScriptList.ScriptRecord:
                if sr.ScriptTag == "latn":
                    script = sr.Script
            if script is None:
                script = gsub.ScriptList.ScriptRecord[0].Script
            ls = script.DefaultLangSys or script.LangSysRecord[0].LangSys
            lookups = set()
            for fi in ls.FeatureIndex:
                fr = gsub.FeatureList.FeatureRecord[fi]
                if fr.FeatureTag == tag:
                    lookups.update(fr.Feature.LookupListIndex)
            seq = [[n, False] for n in names]
            for li in sorted(lookups):
                lk = gsub.LookupList.Lookup[li]
                for i in range(len(seq)):
                    if seq[i][1]:
                        continue
                    for st in lk.SubTable:
                        t = lk.LookupType
                        if t == 7:
                            t, st = st.ExtensionLookupType, st.ExtSubTable
                        done = False
                        if t == 1 and seq[i][0] in st.mapping:
                            seq[i][0] = st.mapping[seq[i][0]]; done = True
                        elif t == 3 and seq[i][0] in st.alternates:
                            seq[i][0] = st.alternates[seq[i][0]][0]; done = True
                        elif t == 4 and seq[i][0] in st.ligatures:
                            for lig in st.ligatures[seq[i][0]]:
                                rest = [j for j in range(i + 1, len(seq)) if not seq[j][1]][:len(lig.Component)]
                                if len(rest) == len(lig.Component) and [seq[j][0] for j in rest] == lig.Component:
                                    seq[i][0] = lig.LigGlyph
                                    for j in rest:
                                        seq[j][1] = True
                                    done = True
                                    break
                        if done:
                            break
            want = [-1 if dead else f.getGlyphID(n) for n, dead in seq]
            check(want == got, "%s %s %r: Easel %s, fontTools %s" % (os.path.basename(path), tag, text, got, want))

# progressive JPEGs: Pillow (libjpeg) sees them as progressive and decodes them to exactly the
# pixels of the baseline JPEG with the same coefficients
for si, size in ((0, (37, 23)), (1, (16, 16)), (2, (1, 1))):
    for q in (85, 95):
        jp = os.path.join(d, "prog_%d_%d.jpg" % (si, q))
        if not os.path.exists(jp):
            continue
        im = Image.open(jp)
        im.load()
        ok = im.info.get("progressive") or im.info.get("progression")
        base = Image.open(os.path.join(d, "base_%d_%d.jpg" % (si, q)))
        mx, mean = cmp_rgb(im.convert("RGB"), base.convert("RGB"))
        check(bool(ok) and im.size == size and mx == 0, "Pillow decodes Easel's progressive JPEG %s at %d exactly as its baseline one (max %d)" % (size, q, mx))

# lossy WebP: libwebp (through Pillow) decodes Easel's VP8 to exactly Easel's own decoding, and the
# picture is within a few decibels of what libwebp itself makes at the same quality
import math
def psnr(a, b):
    e = [(x - y) ** 2 for x, y in zip(a.tobytes(), b.tobytes())]
    m = sum(e) / len(e)
    return 99.0 if m == 0 else 10 * math.log10(255 * 255 / m)
for si, size in ((0, (61, 45)), (1, (16, 16)), (2, (3, 2))):
    for q in (30, 75, 95):
        wp = os.path.join(d, "lossy_%d_%d.webp" % (si, q))
        if not os.path.exists(wp):
            continue
        im = Image.open(wp).convert("RGBA")
        ours = Image.frombytes("RGBA", im.size, rd("lossy_%d_%d.rgba" % (si, q)))
        mx, mean = cmp_rgb(im, ours)
        check(im.size == size and mx == 0, "libwebp decodes Easel's lossy WebP %s at %d as Easel does (max %d)" % (size, q, mx))
        if si == 0:
            src = Image.frombytes("RGBA", size, rd("lossy_src_%d.rgba" % q)).convert("RGB")
            b = io.BytesIO()
            src.save(b, "WEBP", quality=q)
            lib = Image.open(io.BytesIO(b.getvalue())).convert("RGB")
            pe, pl = psnr(im.convert("RGB"), src), psnr(lib, src)
            check(pe >= pl - 3, "Easel's lossy WebP at %d: %.1f dB, libwebp's %.1f dB" % (q, pe, pl))

# PSD layer styles: psd-tools reads the lfx2 block Easel wrote
fxp = os.path.join(d, "fx.psd")
try:
    from psd_tools import PSDImage
except ImportError:
    PSDImage = None
if PSDImage and os.path.exists(fxp):
    psd = PSDImage.open(fxp)
    layer = [l for l in psd if l.name == "Styled"]
    check(len(layer) == 1 and layer[0].has_effects(), "psd-tools: the styled layer has effects")
    if layer:
        got = {type(e).__name__: e for e in layer[0].effects}
        want = ("DropShadow", "InnerGlow", "BevelEmboss", "Satin", "ColorOverlay", "GradientOverlay", "Stroke")
        check(all(k in got for k in want) and len(got) == len(want), "psd-tools: the seven effects (%s)" % sorted(got))
        e = got.get("DropShadow")
        check(e is not None and e.size == 9 and e.distance == 7 and e.angle == 135 and abs(e.opacity - 74.9) < 0.2
              and e.color[b"Rd  "] == 16 and e.color[b"Bl  "] == 48 and str(e.blend_mode).endswith("MULTIPLY"), "psd-tools: the drop shadow")
        e = got.get("Stroke")
        check(e is not None and e.size == 3 and str(e.position).endswith("INSIDE") and e.color[b"Grn "] == 255, "psd-tools: the stroke")
        e = got.get("GradientOverlay")
        check(e is not None and e.angle == 45 and e.scale == 80 and e.offset[b"Hrzn"] == 10, "psd-tools: the gradient overlay")
        e = got.get("BevelEmboss")
        check(e is not None and e.size == 6 and e.depth == 250 and e.altitude == 40, "psd-tools: the bevel")

print("%d checks, %d failed" % (checks, fails))
sys.exit(1 if fails else 0)
