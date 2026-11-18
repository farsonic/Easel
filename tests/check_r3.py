#!/usr/bin/env python3
"""check_r3.py - independent checks of round 3's filters with Pillow and numpy.

Run after r3imagetest, with the folder it wrote to (its first argument, else $TMP, else /tmp):
    python3 tests/check_r3.py [folder]

- Field Blur with one pin blurs everything alike: r3_field_one.png must match Pillow's
  GaussianBlur of r3_field_src.png (sigma half the pin's radius of 8) away from the edges.
- Contextual Alternates and Fractions (GSUB lookups 5 and 6): the glyphs Easel's layout chose are
  worked out again from fontTools' reading of the font's tables (r3_gsub.txt).
- Lens Correction's perspective and angle are one homography: r3_lens_persp.txt holds where four
  corners of the result are taken from; numpy solves the projective map through them and Pillow's
  own perspective transform (bilinear) redoes the picture, which must match r3_lens_persp.png
  wherever both are covered.
"""
import os
import sys

import numpy as np
from PIL import Image, ImageFilter

out = sys.argv[1] if len(sys.argv) > 1 else (os.environ.get("TMP") or "/tmp")
tests = fails = 0


def ok(name, cond, detail=""):
    global tests, fails
    tests += 1
    if not cond:
        fails += 1
        print("FAIL", name, detail)


# Field Blur, one pin of radius 8 (sigma 4)
src = Image.open(os.path.join(out, "r3_field_src.png")).convert("RGB")
easel = np.asarray(Image.open(os.path.join(out, "r3_field_one.png")).convert("RGB"), dtype=float)
pil = np.asarray(src.filter(ImageFilter.GaussianBlur(4)), dtype=float)
inner = (slice(14, -14), slice(14, -14))
d = np.abs(easel[inner] - pil[inner])
ok("Field Blur matches a Gaussian blur", d.mean() < 1.5 and np.percentile(d, 99) < 8, "mean %.2f p99 %.2f" % (d.mean(), np.percentile(d, 99)))
ok("and is blurred", np.abs(easel[inner] - np.asarray(src, dtype=float)[inner]).mean() > 3)

# Lens Correction: perspective and angle as Pillow's PERSPECTIVE transform
rows = [list(map(float, line.split())) for line in open(os.path.join(out, "r3_lens_persp.txt"))]
A, b = [], []
for x, y, sx, sy in rows:
    A.append([x, y, 1, 0, 0, 0, -sx * x, -sx * y]); b.append(sx)
    A.append([0, 0, 0, x, y, 1, -sy * x, -sy * y]); b.append(sy)
coef = np.linalg.solve(np.array(A), np.array(b))
src = Image.open(os.path.join(out, "r3_field_src.png")).convert("RGBA")
pil = np.asarray(src.transform(src.size, Image.Transform.PERSPECTIVE, tuple(coef), Image.Resampling.BILINEAR), dtype=float)
easel = np.asarray(Image.open(os.path.join(out, "r3_lens_persp.png")).convert("RGBA"), dtype=float)
both = (pil[..., 3] == 255) & (easel[..., 3] == 255)
d = np.abs(pil[..., :3] - easel[..., :3])[both]
ok("Lens Correction perspective covers most of the picture", both.mean() > 0.6, "%.2f" % both.mean())
ok("Lens Correction perspective matches Pillow's", d.mean() < 2.0 and np.percentile(d, 99) < 40, "mean %.2f p99 %.2f" % (d.mean(), np.percentile(d, 99)))
cover_e = easel[..., 3] > 0
cover_p = pil[..., 3] > 0
ok("the same area uncovered", (cover_e != cover_p).mean() < 0.02, "%.3f" % (cover_e != cover_p).mean())

# Lens Blur without a depth map: every pixel the mean of the disc of radius 6 round it (the iris's
# rows as Easel makes them: dx * dx + dy * dy <= r * r + r / 2), edges repeated
lb = os.path.join(out, "r3_lensblur.png")
if os.path.exists(lb):
    srcf = np.asarray(Image.open(os.path.join(out, "r3_field_src.png")).convert("RGB"), dtype=float)
    r = 6
    pad = np.pad(srcf, ((r, r), (r, r), (0, 0)), mode="edge")
    acc = np.zeros_like(srcf)
    cnt = 0
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            if dx * dx + dy * dy <= r * r + r // 2:
                acc += pad[r + dy:r + dy + srcf.shape[0], r + dx:r + dx + srcf.shape[1]]
                cnt += 1
    ref = acc / cnt
    got = np.asarray(Image.open(lb).convert("RGB"), dtype=float)
    d = np.abs(got - ref)
    ok("Lens Blur's disc matches numpy's", d.max() <= 1.01, "max %.2f" % d.max())

# OpenType contextual lookups (GSUB 5 and 6) as fontTools reads the font: r3_gsub.txt holds the
# glyphs Easel's layout gave for some text with Contextual Alternates or Fractions; the same lookups
# are applied here to the glyph names, from fontTools' own reading of the tables
gpath = os.path.join(out, "r3_gsub.txt")
try:
    from fontTools.ttLib import TTFont
except ImportError:
    TTFont = None
    print("fontTools isn't installed: the OpenType checks are skipped")
if TTFont and os.path.exists(gpath):
    fonts = {}

    def apply_lookup(gsub, li, seq, i, depth=0):
        """lookup li at position i of seq ([name, dead]); True if something changed"""
        lk = gsub.LookupList.Lookup[li]
        for st in lk.SubTable:
            t = lk.LookupType
            if t == 7:
                t, st = st.ExtensionLookupType, st.ExtSubTable
            if apply_sub(gsub, t, st, seq, i, depth):
                return True
        return False

    def live(seq, i, d):
        i += d
        while 0 <= i < len(seq) and seq[i][1]:
            i += d
        return i if 0 <= i < len(seq) else -1

    def apply_records(gsub, recs, pos, seq, depth):
        for r in recs:
            if r.SequenceIndex < len(pos) and not seq[pos[r.SequenceIndex]][1]:
                apply_lookup(gsub, r.LookupListIndex, seq, pos[r.SequenceIndex], depth + 1)
        return True

    def apply_sub(gsub, t, st, seq, i, depth):
        g = seq[i][0]
        if depth > 4:
            return False
        if t == 1:
            if g in st.mapping:
                seq[i][0] = st.mapping[g]; return True
            return False
        if t == 3:
            if g in st.alternates:
                seq[i][0] = st.alternates[g][0]; return True
            return False
        if t == 4:
            for lig in st.ligatures.get(g, []):
                rest, at = [], i
                for _ in lig.Component:
                    at = live(seq, at, 1)
                    if at < 0:
                        break
                    rest.append(at)
                if len(rest) == len(lig.Component) and [seq[j][0] for j in rest] == lig.Component:
                    seq[i][0] = lig.LigGlyph
                    for j in rest:
                        seq[j][1] = True
                    return True
            return False
        if t in (5, 6) and st.Format == 3:
            back = st.BacktrackCoverage if t == 6 else []
            inp = st.InputCoverage if t == 6 else st.Coverage
            ahead = st.LookAheadCoverage if t == 6 else []
            at = i
            for c in back:
                at = live(seq, at, -1)
                if at < 0 or seq[at][0] not in c.glyphs:
                    return False
            pos, at = [], i
            for k, c in enumerate(inp):
                if k:
                    at = live(seq, at, 1)
                if at < 0 or seq[at][0] not in c.glyphs:
                    return False
                pos.append(at)
            for c in ahead:
                at = live(seq, at, 1)
                if at < 0 or seq[at][0] not in c.glyphs:
                    return False
            return apply_records(gsub, st.SubstLookupRecord if t == 6 else st.SubstLookupRecord, pos, seq, depth)
        if t in (5, 6) and st.Format in (1, 2):
            if g not in st.Coverage.glyphs:
                return False
            if t == 5 and st.Format == 1:
                sets, key = st.SubRuleSet, st.Coverage.glyphs.index(g)
            elif t == 5:
                sets, key = st.SubClassSet, st.ClassDef.classDefs.get(g, 0)
            elif st.Format == 1:
                sets, key = st.ChainSubRuleSet, st.Coverage.glyphs.index(g)
            else:
                sets, key = st.ChainSubClassSet, st.InputClassDef.classDefs.get(g, 0)
            if key >= len(sets) or sets[key] is None:
                return False
            rs = sets[key]
            rules = getattr(rs, "SubRule", None) or getattr(rs, "SubClassRule", None) or getattr(rs, "ChainSubRule", None) or getattr(rs, "ChainSubClassRule", None) or []
            for rule in rules:
                if st.Format == 1:
                    eq = lambda which, name, v: name == v
                else:
                    cds = {"b": getattr(st, "BacktrackClassDef", None), "i": getattr(st, "InputClassDef", None) or getattr(st, "ClassDef", None), "l": getattr(st, "LookAheadClassDef", None)}
                    eq = lambda which, name, v, cds=cds: cds[which].classDefs.get(name, 0) == v
                ok, at = True, i
                for v in (getattr(rule, "Backtrack", []) if t == 6 else []):
                    at = live(seq, at, -1)
                    if at < 0 or not eq("b", seq[at][0], v):
                        ok = False; break
                if not ok:
                    continue
                pos, at = [i], i
                for v in rule.Input:
                    at = live(seq, at, 1)
                    if at < 0 or not eq("i", seq[at][0], v):
                        ok = False; break
                    pos.append(at)
                if not ok:
                    continue
                for v in (getattr(rule, "LookAhead", []) if t == 6 else []):
                    at = live(seq, at, 1)
                    if at < 0 or not eq("l", seq[at][0], v):
                        ok = False; break
                if not ok:
                    continue
                return apply_records(gsub, rule.SubstLookupRecord, pos, seq, depth)
            return False
        return False

    for line in open(gpath, encoding="utf-8"):
        path, tag, text, got = line.rstrip("\n").split("\t")
        got = [int(v) for v in got.split()]
        if path not in fonts:
            fonts[path] = TTFont(path)
        f = fonts[path]
        gsub = f["GSUB"].table
        cmap = f.getBestCmap()
        seq = [[cmap[ord(ch)], False] for ch in text]
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
        # Fractions only on the figures either side of a slash (as HarfBuzz and Easel do)
        lo, hi = 0, len(seq)
        if tag == "frac":
            s_ = text.index("/")
            lo = s_
            while lo > 0 and text[lo - 1].isdigit():
                lo -= 1
            hi = s_ + 1
            while hi < len(text) and text[hi].isdigit():
                hi += 1
        part = seq[lo:hi]
        for li in sorted(lookups):
            for i in range(len(part)):
                if not part[i][1]:
                    apply_lookup(gsub, li, part, i)
        seq[lo:hi] = part
        want = [-1 if dead else f.getGlyphID(n) for n, dead in seq]
        ok("%s %s %r: Easel %s, fontTools %s" % (os.path.basename(path), tag, text, got, want), want == got)

print("check_r3: %d checks, %d failed" % (tests, fails))
sys.exit(1 if fails else 0)
