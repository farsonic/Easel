#!/usr/bin/env python3
"""check_codecs.py - checks the files Easel's encoders wrote with other people's decoders.

Run codectest first (it writes its output to /tmp/easel_codec_out, or the folder given to it),
then: python3 tests/check_codecs.py [folder]

  PNG, BMP, JPEG  opened with Pillow and compared with the source pixels (.rgba files codectest
                  wrote alongside): exact for PNG and BMP, PSNR for JPEG; PNG pHYs and JFIF density
                  and the JPEG chroma subsampling read back with Pillow; ImageMagick's identify
                  and `file` must recognise each file.
  IFF ILBM        decoded with ffmpeg (iff_ilbm), compared exactly (24-bit, 8-bit exact palette)
                  or by PSNR (quantized); the hand-made references in tests/data (EHB, HAM6, HAM8,
                  deep, PBM, ...) are checked against ffmpeg too, as an independent opinion on
                  what they should decode to.
  OpenRaster      zip integrity (unzip -t), mimetype first and stored (zipinfo), stack.xml
                  well-formed (xmllint), every layer PNG and the merged image open in Pillow.
Tools that aren't installed are skipped and reported as such.
"""
import math, os, shutil, struct, subprocess, sys, zipfile, io

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/easel_codec_out"
results = []


def check(name, cond, detail=""):
    results.append((name, bool(cond)))
    print("%s  %s%s" % ("ok  " if cond else "FAIL", name, ("  (" + detail + ")") if detail else ""))


def skip(name, why):
    print("skip  %s  (%s)" % (name, why))


def rgba_file(path):
    d = open(path, "rb").read()
    assert d[:4] == b"RGBA"
    w, h = struct.unpack(">II", d[4:12])
    return w, h, d[12:]


def psnr(a, b):
    se = n = 0
    for i in range(0, len(a), 4):
        for c in range(3):
            se += (a[i + c] - b[i + c]) ** 2
        n += 3
    return 99.0 if se == 0 else 10 * math.log10(255 * 255 / (se / n))


def same(a, b):
    """pixel-exact, two fully transparent pixels counting as equal"""
    if len(a) != len(b):
        return False
    for i in range(0, len(a), 4):
        if a[i:i + 4] != b[i:i + 4] and not (a[i + 3] == 0 and b[i + 3] == 0):
            return False
    return True


def ffmpeg_rgba(path):
    r = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "rgba", "-"], capture_output=True)
    return r.stdout if r.returncode == 0 else None


def main():
    if not os.path.isdir(OUT):
        sys.exit("no %s: run codectest first" % OUT)
    try:
        from PIL import Image, JpegImagePlugin
    except ImportError:
        sys.exit("Pillow is needed")
    P = lambda n: os.path.join(OUT, n)

    # PNG
    for png, src in (("rgba.png", "rgba.rgba"), ("gradient.png", "gradient.rgba")):
        w, h, px = rgba_file(P(src))
        im = Image.open(P(png))
        im.load()
        check("Pillow reads %s (%s) exactly" % (png, im.mode), im.size == (w, h) and same(im.convert("RGBA").tobytes(), px))
    dpi = Image.open(P("gradient.png")).info.get("dpi")
    check("Pillow reads the PNG pHYs as 150 dpi", dpi and abs(dpi[0] - 150) < 0.5, str(dpi))
    # JPEG
    w, h, px = rgba_file(P("q.rgba"))
    for q, want, sub in ((90, 35, 0), (75, 32, 2), (50, 30, 2), (100, 40, 0)):
        im = Image.open(P("q%d.jpg" % q))
        im.load()
        p = psnr(im.convert("RGBA").tobytes(), px)
        check("libjpeg (Pillow) reads q%d.jpg: PSNR %.1f dB >= %d" % (q, p, want), im.size == (w, h) and p >= want)
        s = JpegImagePlugin.get_sampling(im)
        check("q%d.jpg chroma sampling is %s" % (q, "4:4:4" if sub == 0 else "4:2:0"), s == sub, "Pillow says %d" % s)
        check("q%d.jpg JFIF density 300 dpi" % q, im.info.get("dpi") == (300, 300) and im.info.get("jfif_density") == (300, 300), str(im.info.get("dpi")))
    # BMP
    for bmp, src in (("out24.bmp", "out24.rgba"), ("out32.bmp", "out32.rgba")):
        w, h, px = rgba_file(P(src))
        im = Image.open(P(bmp))
        im.load()
        check("Pillow reads %s (%s) exactly" % (bmp, im.mode), im.size == (w, h) and im.convert("RGBA").tobytes() == px)
    # ImageMagick and file
    if shutil.which("identify"):
        for f, fmt in (("rgba.png", "PNG"), ("gradient.png", "PNG"), ("q90.jpg", "JPEG"), ("q50.jpg", "JPEG"), ("out24.bmp", "BMP"), ("out32.bmp", "BMP")):
            r = subprocess.run(["identify", "-regard-warnings", P(f)], capture_output=True, text=True)
            check("ImageMagick identify accepts %s" % f, r.returncode == 0 and fmt in r.stdout, (r.stdout + r.stderr).strip()[:100])
        # ImageMagick decoding the 32-bit BMP with its alpha
        w, h, px = rgba_file(P("out32.rgba"))
        r = subprocess.run(["convert", P("out32.bmp"), "rgba:-"], capture_output=True)
        check("ImageMagick decodes out32.bmp with alpha exactly", r.returncode == 0 and r.stdout == px)
    else:
        skip("ImageMagick checks", "identify not installed")
    if shutil.which("file"):
        for f, word in (("rgba.png", "PNG image data"), ("q90.jpg", "JPEG image data"), ("out24.bmp", "PC bitmap"), ("out24.iff", "IFF data, ILBM"),
                        ("out8.iff", "IFF data, ILBM"), ("out.ora", "openraster")):
            r = subprocess.run(["file", "-b", P(f)], capture_output=True, text=True)
            check("file recognises %s" % f, word.lower() in r.stdout.lower(), r.stdout.strip()[:90])
    else:
        skip("file checks", "file not installed")

    # IFF ILBM through ffmpeg
    if shutil.which("ffmpeg"):
        for iff, src, mode in (("out24.iff", "out24iff.rgba", "exact"), ("out8.iff", "out8iff.rgba", "exact"),
                               ("out8q.iff", "out8q.rgba", 33), ("out8d.iff", "out8q.rgba", 30)):
            w, h, px = rgba_file(P(src))
            got = ffmpeg_rgba(P(iff))
            if mode == "exact":
                ok = got is not None and len(got) == len(px) and all(got[i:i + 3] == px[i:i + 3] for i in range(0, len(px), 4))
                check("ffmpeg reads %s exactly" % iff, ok)
            else:
                p = psnr(got, px) if got and len(got) == len(px) else 0
                check("ffmpeg reads %s: PSNR %.1f dB >= %d" % (iff, p, mode), p >= mode)
        # the hand-made references: does ffmpeg agree with what codectest expects?
        data = os.path.join(HERE, "data")
        # where ffmpeg is known to read differently, by design on Easel's side:
        known = {
            "ilbm_1plane_nocmap.iff": "no CMAP: ffmpeg's grey ramp is i*255>>planes (1 -> 127), Easel's is full range (1 -> 255)",
            "ilbm_ehb_nocamg.iff": "ffmpeg needs the CAMG EHB flag; Easel also takes 6 planes with a 32-colour CMAP as EHB",
            "ilbm_pbm.iff": "odd-width ByteRun1 PBM: ffmpeg drops the row padding byte only when uncompressed (see ilbm_pbm_raw.iff)",
        }
        for line in open(os.path.join(data, "manifest.txt")):
            pic, exp, mode = line.split()
            if not pic.endswith(".iff"):
                continue
            w, h, px = rgba_file(os.path.join(data, exp))
            got = ffmpeg_rgba(os.path.join(data, pic))
            if got is None or len(got) != len(px):
                check("ffmpeg agrees on tests/data/%s" % pic, False, "ffmpeg could not decode it")
                continue
            # ffmpeg ignores a transparent colour and fills a 24-bit picture's alpha: compare RGB, plus alpha for 32-bit
            alpha = "deep32" in pic
            ok = all(got[i:i + 3] == px[i:i + 3] and (not alpha or got[i + 3] == px[i + 3]) for i in range(0, len(px), 4))
            if pic in known:
                print("note  ffmpeg %s on tests/data/%s: %s" % ("agrees" if ok else "differs", pic, known[pic]))
                continue
            check("ffmpeg agrees on tests/data/%s" % pic, ok)
    else:
        skip("ILBM checks", "ffmpeg not installed")

    # OpenRaster
    ora = P("out.ora")
    if shutil.which("unzip"):
        r = subprocess.run(["unzip", "-tq", ora], capture_output=True, text=True)
        check("unzip -t: out.ora is a sound zip", r.returncode == 0, r.stdout.strip()[:80])
    if shutil.which("zipinfo"):
        r = subprocess.run(["zipinfo", ora], capture_output=True, text=True)
        lines = [l for l in r.stdout.splitlines() if l[:1] in "-d"]
        check("zipinfo: mimetype is the first entry, stored", lines and lines[0].split()[-1] == "mimetype" and " stor " in lines[0], lines[0] if lines else "")
    z = zipfile.ZipFile(ora)
    check("mimetype says image/openraster", z.read("mimetype") == b"image/openraster")
    stack = z.read("stack.xml")
    if shutil.which("xmllint"):
        r = subprocess.run(["xmllint", "--noout", "-"], input=stack, capture_output=True)
        check("xmllint: stack.xml is well-formed", r.returncode == 0, r.stderr.decode()[:100])
    import xml.etree.ElementTree as ET
    root = ET.fromstring(stack)
    layers = root.findall("./stack/layer")
    bad = 0
    for l in layers:
        im = Image.open(io.BytesIO(z.read(l.get("src"))))
        im.load()
        x, y = int(l.get("x")), int(l.get("y"))
        if x < 0 or y < 0 or x + im.size[0] > int(root.get("w")) or y + im.size[1] > int(root.get("h")):
            bad += 1
    check("Pillow opens all %d layer PNGs, each inside the canvas" % len(layers), len(layers) > 0 and bad == 0)
    m = Image.open(io.BytesIO(z.read("mergedimage.png")))
    t = Image.open(io.BytesIO(z.read("Thumbnails/thumbnail.png")))
    check("merged image is canvas-size, thumbnail within 256", m.size == (int(root.get("w")), int(root.get("h"))) and max(t.size) <= 256)

    failed = sum(1 for _, c in results if not c)
    print("%d external checks, %d failed" % (len(results), failed))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
