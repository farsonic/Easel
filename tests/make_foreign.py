#!/usr/bin/env python3
"""make_foreign.py OUTDIR - test documents for Easel's readers of other programs' files (core/foreign.h),
written from the formats' public descriptions (no GIMP, Krita or Paint.NET needed): GIMP .xcf in several
versions, precisions and compressions, Krita .kra and Paint.NET .pdn. Next to each file, NAME.json
lists what Easel must find: the layers (bottom first: name, kind, depth, opacity, visible, mode) and
probes (layer, x, y, r, g, b, a; or mask probes). Made at test time, so nothing third-party is committed.
"""
import gzip, json, os, struct, sys, zlib

OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/easel_foreign"
os.makedirs(OUT, exist_ok=True)

def pat(x, y, k):
    """layer k's straight RGBA at (x, y)"""
    return ((x * 7 + k * 40) & 255, (y * 5 + k * 60) & 255, ((x ^ y) * 3 + k * 20) & 255, 255 if (x + y + k) % 5 else 128)

def save(name, data, meta):
    with open(os.path.join(OUT, name), "wb") as f: f.write(data)
    with open(os.path.join(OUT, name + ".json"), "w") as f: json.dump(meta, f)
    # the same as plain lines for the C test (tests/filestest.c)
    with open(os.path.join(OUT, name + ".txt"), "w") as f:
        f.write("S %d %d %d\n" % (meta["w"], meta["h"], meta.get("dpi", 0)))
        for l in meta["layers"]: f.write("L %s %s %d %d %d %s\n" % (l["name"], l["kind"], l["depth"], l["opacity"], l["visible"], l["mode"]))
        for p in meta["probes"]: f.write("P %s %d %d %d %d %d %d\n" % (p["layer"], p["x"], p["y"], *p["rgba"]))
        for p in meta.get("maskprobes", []): f.write("M %s %d %d %d\n" % (p["layer"], p["x"], p["y"], p["v"]))
        for p in meta.get("chanprobes", []): f.write("C %d %d %d\n" % (p["x"], p["y"], p["v"]))
        for p in meta.get("selection", []): f.write("Q %d %d %d\n" % tuple(p))

# ---------------------------------------------------------------- GIMP XCF
def rle_stream(b):
    out = bytearray(); i = 0; n = len(b)
    while i < n:
        j = i
        while j < n and b[j] == b[i] and j - i < 127: j += 1
        if j - i >= 3:
            out.append(j - i - 1); out.append(b[i]); i = j; continue
        j = i
        while j < n and j - i < 127 and not (j + 2 < n and b[j] == b[j + 1] == b[j + 2]): j += 1
        if j == i: j = i + 1
        out.append(256 - (j - i)); out += b[i:j]; i = j
    return bytes(out)

def enc_comp(v, prec):
    """a component 0..255 (sRGB-encoded) in a precision: (bytes, linear?)"""
    k = prec // 100; lin = prec % 100 != 50
    f = v / 255.0
    if lin: f = f / 12.92 if f <= 0.04045 else ((f + 0.055) / 1.055) ** 2.4
    if k == 1: return bytes([round(f * 255)])
    if k == 2: return struct.pack(">H", round(f * 65535))
    if k == 3: return struct.pack(">I", round(f * 4294967295))
    if k == 5: return struct.pack(">e", f)
    if k == 6: return struct.pack(">f", f)
    if k == 7: return struct.pack(">d", f)
    raise ValueError(prec)

class XcfW:
    def __init__(s, ver): s.b = bytearray(); s.ver = ver
    def u32(s, v): s.b += struct.pack(">I", v & 0xFFFFFFFF)
    def ptr(s, v): s.b += struct.pack(">Q" if s.ver >= 11 else ">I", v)
    def psize(s): return 8 if s.ver >= 11 else 4
    def string(s, t):
        e = t.encode() + b"\0"; s.u32(len(e)); s.b += e
    def prop(s, t, payload): s.u32(t); s.u32(len(payload)); s.b += payload
    def patch(s, at, v): s.b[at:at + s.psize()] = struct.pack(">Q" if s.ver >= 11 else ">I", v)

def xcf_hier(W, w, h, bpp, pix, comp):
    """pix(x, y) -> bytes of one pixel (bpp of them); a hierarchy, its level and tiles; returns its offset"""
    at = len(W.b)
    W.u32(w); W.u32(h); W.u32(bpp)
    lp = len(W.b); W.ptr(0); W.ptr(0)
    W.patch(lp, len(W.b))
    W.u32(w); W.u32(h)
    ntx = (w + 63) // 64; nty = (h + 63) // 64
    tptr = len(W.b)
    for _ in range(ntx * nty): W.ptr(0)
    W.ptr(0)
    for t in range(ntx * nty):
        x0 = (t % ntx) * 64; y0 = (t // ntx) * 64
        tw = min(64, w - x0); th = min(64, h - y0)
        px = [pix(x0 + i % tw, y0 + i // tw) for i in range(tw * th)]
        W.patch(tptr + t * W.psize(), len(W.b))
        if comp == 0: W.b += b"".join(px)
        elif comp == 1:
            for c in range(bpp): W.b += rle_stream(bytes(p[c] for p in px))
        else: W.b += zlib.compress(b"".join(px))
    return at

def make_xcf(name, ver, prec, comp, gz=False, base=0, simple=False):
    W, H = 70, 66                      # more than one tile each way, with short edge tiles
    W_ = XcfW(ver)
    W_.b += b"gimp xcf " + (b"file\0" if ver == 0 else ("v%03d" % ver).encode() + b"\0")
    W_.u32(W); W_.u32(H); W_.u32(base)
    if ver >= 4: W_.u32(prec)
    bpc = len(enc_comp(0, prec))
    W_.prop(17, bytes([comp]))
    W_.prop(19, struct.pack(">ff", 144.0, 144.0))
    cmap = [(i * 9 % 256, i * 17 % 256, i * 33 % 256) for i in range(16)]
    if base == 2: W_.prop(1, struct.pack(">I", 16) + bytes(sum(cmap, ())))
    W_.u32(0); W_.u32(0)
    # the items, top first: a group (with two members), then a bottom layer with a mask, offset
    items = [
        dict(name="Group", group=True, path=[0], mode=61, opacity=200),
        dict(name="Top", k=1, path=[0, 0], w=40, h=30, ox=10, oy=12, mode=30, opacity=128),       # Multiply 2.10
        dict(name="Middle", k=2, path=[0, 1], w=W, h=H, ox=0, oy=0, mode=28, opacity=255, visible=0),
        dict(name="Bottom", k=3, path=[1], w=50, h=40, ox=-5, oy=30, mode=28, opacity=255, mask=True),
    ]
    if simple:     # for ImageMagick's reader (tests/check_files.py): no groups, masks or blend modes
        items = [dict(name="Top", k=1, path=[0], w=40, h=30, ox=10, oy=12, mode=28, opacity=255),   # (ImageMagick ignores the opacity)
                 dict(name="Bottom", k=3, path=[1], w=W, h=H, ox=0, oy=0, mode=28, opacity=255)]
    tab = len(W_.b)
    for _ in items: W_.ptr(0)
    W_.ptr(0)
    chtab = len(W_.b)
    W_.ptr(0); W_.ptr(0); W_.ptr(0)            # an alpha channel and the selection
    meta = dict(w=W, h=H, dpi=144, layers=[], probes=[], channels=["Spot"], selection=[[3, 4, 255], [60, 60, 0]])
    for i, it in enumerate(items):
        W_.patch(tab + i * W_.psize(), len(W_.b))
        grp = it.get("group", False)
        lw, lh = (1, 1) if grp else (it["w"], it["h"])
        if base == 0: typ = 1
        elif base == 1: typ = 3
        else: typ = 5
        W_.u32(lw); W_.u32(lh); W_.u32(typ); W_.string(it["name"])
        W_.prop(6, struct.pack(">I", it["opacity"]))
        W_.prop(7, struct.pack(">I", it["mode"]))
        W_.prop(8, struct.pack(">I", it.get("visible", 1)))
        W_.prop(15, struct.pack(">ii", it.get("ox", 0), it.get("oy", 0)))
        W_.prop(30, b"".join(struct.pack(">I", p) for p in it["path"]))
        if grp: W_.prop(29, b""); W_.prop(31, struct.pack(">I", 1))
        if it["name"] == "Bottom": W_.prop(34, struct.pack(">I", 6)); W_.prop(32, struct.pack(">I", 1))   # red tag, position locked
        W_.u32(0); W_.u32(0)
        hp = len(W_.b); W_.ptr(0); W_.ptr(0)
        k = it.get("k", 0)
        def colour(x, y, k=k):
            r, g, b, a = pat(x, y, k)
            if base == 1: r = g = b = (x * 3 + k * 50) & 255
            return r, g, b, a
        def apix(x, y, k=k):
            # alpha is stored linearly in every precision (it isn't a colour)
            r, g, b, a = colour(x, y)
            if base == 2: return bytes([(x + y + k) % 16, a])
            cs = [r, g, b] if base == 0 else [r]
            lin = prec % 100 != 50
            al = enc_comp(a, prec)
            # (enc_comp decodes linear colours: alpha is never decoded)
            if lin:
                kk = prec // 100; f = a / 255.0
                al = {1: lambda: bytes([round(f * 255)]), 2: lambda: struct.pack(">H", round(f * 65535)),
                      3: lambda: struct.pack(">I", round(f * 4294967295)), 5: lambda: struct.pack(">e", f),
                      6: lambda: struct.pack(">f", f), 7: lambda: struct.pack(">d", f)}[kk]()
            return b"".join(enc_comp(c, prec) for c in cs) + al
        if grp:
            meta["layers"].append(dict(name="Group", kind="group", depth=0, opacity=200, visible=1, mode="pass"))
            W_.patch(hp, xcf_hier(W_, 1, 1, (4 if base == 0 else 2) * bpc, lambda x, y: b"\0" * ((4 if base == 0 else 2) * bpc), comp))
            continue
        bpp = (2 if base else 4) * bpc
        W_.patch(hp, xcf_hier(W_, lw, lh, bpp, lambda x, y: apix(x, y), comp))
        if it.get("mask"):
            W_.patch(hp + W_.psize(), len(W_.b))
            W_.u32(lw); W_.u32(lh); W_.string("Bottom mask"); W_.u32(0); W_.u32(0)
            mh = len(W_.b); W_.ptr(0)
            W_.patch(mh, xcf_hier(W_, lw, lh, bpc, lambda x, y: enc_comp(255 if x < 25 else 0, prec) if prec % 100 == 50 else
                                  {1: lambda: bytes([255 if x < 25 else 0]), 2: lambda: struct.pack(">H", 65535 if x < 25 else 0),
                                   3: lambda: struct.pack(">I", 4294967295 if x < 25 else 0), 5: lambda: struct.pack(">e", 1.0 if x < 25 else 0.0),
                                   6: lambda: struct.pack(">f", 1.0 if x < 25 else 0.0), 7: lambda: struct.pack(">d", 1.0 if x < 25 else 0.0)}[prec // 100](), comp))
        mode = {30: "multiply", 28: "normal"}[it["mode"]]
        meta["layers"].append(dict(name=it["name"], kind="pixel", depth=len(it["path"]) - 1, opacity=it["opacity"], visible=it.get("visible", 1), mode=mode))
        ox, oy = it["ox"], it["oy"]
        for (px_, py_) in [(0, 0), (7, 3), (lw - 1, lh - 1), (33, 21)]:
            if px_ >= lw or py_ >= lh: continue
            cx, cy = px_ + ox, py_ + oy
            if not (0 <= cx < W and 0 <= cy < H): continue
            r, g, b, a = colour(px_, py_)
            if base == 2: r, g, b = cmap[(px_ + py_ + k) % 16]
            meta["probes"].append(dict(layer=it["name"], x=cx, y=cy, rgba=[r, g, b, a]))
        if it.get("mask"):
            meta["maskprobes"] = [dict(layer="Bottom", x=0 + ox + 5, y=oy + 1, v=255), dict(layer="Bottom", x=ox + 30, y=oy + 1, v=0)]
    meta["layers"].reverse()
    # channels: an alpha channel "Spot" and the selection
    for ci, (cname, sel) in enumerate([("Spot", False), ("Selection Mask", True)]):
        W_.patch(chtab + ci * W_.psize(), len(W_.b))
        W_.u32(W); W_.u32(H); W_.string(cname)
        if sel: W_.prop(4, b"")
        W_.u32(0); W_.u32(0)
        hp = len(W_.b); W_.ptr(0)
        def cpix(x, y, sel=sel):
            v = (255 if x < 30 and y < 30 else 0) if sel else (x * 3) & 255
            f = v / 255.0
            return {1: lambda: bytes([v]), 2: lambda: struct.pack(">H", round(f * 65535)), 3: lambda: struct.pack(">I", round(f * 4294967295)),
                    5: lambda: struct.pack(">e", f), 6: lambda: struct.pack(">f", f), 7: lambda: struct.pack(">d", f)}[prec // 100]()
        W_.patch(hp, xcf_hier(W_, W, H, bpc, cpix, comp))
    meta["chanprobes"] = [dict(x=10, y=5, v=30), dict(x=50, y=5, v=150)]
    data = bytes(W_.b)
    if gz: data = gzip.compress(data)
    save(name, data, meta)

make_xcf("v3_rle.xcf", 3, 150, 1)
make_xcf("v0_raw.xcf", 0, 150, 0)
make_xcf("v11_zlib.xcf", 11, 150, 2)
make_xcf("v12_16bit.xcf", 12, 250, 2)
make_xcf("v12_16lin.xcf", 12, 200, 1)
make_xcf("v12_half.xcf", 12, 500, 2)
make_xcf("v12_float.xcf", 12, 600, 1)
make_xcf("v12_double.xcf", 12, 750, 2)
make_xcf("v3_grey.xcf", 3, 150, 1, base=1)
make_xcf("v3_indexed.xcf", 3, 150, 1, base=2)
make_xcf("v11_gz.xcf.gz", 11, 150, 2, gz=True)
make_xcf("im_simple.xcf", 3, 150, 1, simple=True)
# ---------------------------------------------------------------- Krita KRA
def lzf(data):
    """liblzf's format: literal runs and back references found through a 3-byte table"""
    out = bytearray(); lit = bytearray(); i = 0; n = len(data); seen = {}
    def flush():
        while lit:
            chunk = lit[:32]; del lit[:32]
            out.append(len(chunk) - 1); out.extend(chunk)
    while i < n:
        key = bytes(data[i:i + 3]); j = seen.get(key) if len(key) == 3 else None
        if len(key) == 3: seen[key] = i
        if j is not None and i - j - 1 < 8192:
            l = 3
            while i + l < n and l < 264 and data[j + l] == data[i + l]: l += 1
            flush()
            off = i - j - 1; ln = l - 2
            if ln < 7: out.append((ln << 5) | (off >> 8))
            else: out.append((7 << 5) | (off >> 8)); out.append(ln - 7)
            out.append(off & 255)
            i += l
        else:
            lit.append(data[i]); i += 1
    flush()
    return bytes(out)

def kra_tiles(w, h, ps, pix, raw_every=3):
    """tiles of 64x64 covering w x h from (0, 0); pix(x, y) -> ps bytes (or None outside)"""
    tiles = []
    for ty in range(0, h, 64):
        for tx in range(0, w, 64):
            px = []
            for i in range(64 * 64):
                x, y = tx + i % 64, ty + i // 64
                px.append(pix(x, y) if x < w and y < h else bytes(ps))
            planes = b"".join(bytes(p[c] for p in px) for c in range(ps))
            tiles.append((tx, ty, planes))
    out = bytearray(b"VERSION 2\nTILEWIDTH 64\nTILEHEIGHT 64\nPIXELSIZE %d\nDATA %d\n" % (ps, len(tiles)))
    for k, (tx, ty, planes) in enumerate(tiles):
        body = (b"\0" + planes) if k % raw_every == 2 else (b"\1" + lzf(planes))
        out += b"%d,%d,LZF,%d\n" % (tx, ty, len(body)) + body
    return bytes(out)

def make_kra(name, depth16=False, grey=False):
    import zipfile, io
    W, H = 70, 66
    cs = ("GRAYA" if grey else "RGBA") + ("16" if depth16 else "")
    bps = 2 if depth16 else 1
    def enc(v):  # one 8-bit sample at the file's depth, little-endian
        return bytes([v]) if bps == 1 else struct.pack("<H", v * 257)
    def colour(x, y, k):
        r, g, b, a = pat(x, y, k)
        if grey: r = g = b = (x * 3 + k * 50) & 255
        return r, g, b, a
    def lpix(k):
        def f(x, y):
            r, g, b, a = colour(x, y, k)
            return (enc(r) + enc(a)) if grey else (enc(b) + enc(g) + enc(r) + enc(a))
        return f
    layers_xml = (
        '<layer name="Group" nodetype="grouplayer" filename="layer5" opacity="255" visible="1" compositeop="normal" passthrough="1" collapsed="1" x="0" y="0"><layers>'
        '<layer name="Top" nodetype="paintlayer" filename="layer4" colorspacename="%s" x="10" y="12" opacity="128" visible="1" compositeop="multiply"/>'
        '<layer name="Vector" nodetype="shapelayer" filename="layer6" x="0" y="0" opacity="255" visible="1"><shapes/></layer>'
        '<layer name="Middle" nodetype="paintlayer" filename="layer3" colorspacename="%s" x="0" y="0" opacity="255" visible="0" compositeop="screen"/>'
        '</layers></layer>'
        '<layer name="Bottom" nodetype="paintlayer" filename="layer2" colorspacename="%s" x="-5" y="30" opacity="255" visible="1" compositeop="normal" locked="1">'
        '<masks><mask name="Transparency Mask" nodetype="transparencymask" filename="mask1" colorspacename="GRAYA" x="-5" y="30"/></masks></layer>' % (cs, cs, cs))
    doc = ('<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE DOC PUBLIC \'-//KDE//DTD krita 2.0//EN\' \'http://www.calligra.org/DTD/krita-2.0.dtd\'>\n'
           '<DOC xmlns="http://www.calligra.org/DTD/krita" syntaxVersion="2.0" editor="Krita">'
           '<IMAGE width="%d" height="%d" name="Pic" mime="application/x-kra" x-res="150" y-res="150" colorspacename="%s"><layers>%s</layers></IMAGE></DOC>' % (W, H, cs, layers_xml))
    b = io.BytesIO()
    z = zipfile.ZipFile(b, "w")
    z.writestr(zipfile.ZipInfo("mimetype"), "application/x-krita")
    z.writestr("maindoc.xml", doc, compress_type=zipfile.ZIP_DEFLATED)
    ps = (2 if grey else 4) * bps
    z.writestr("Pic/layers/layer4", kra_tiles(40, 30, ps, lpix(1)), compress_type=zipfile.ZIP_DEFLATED)
    z.writestr("Pic/layers/layer3", kra_tiles(W, H, ps, lpix(2)), compress_type=zipfile.ZIP_DEFLATED)
    z.writestr("Pic/layers/layer2", kra_tiles(50, 40, ps, lpix(3)), compress_type=zipfile.ZIP_DEFLATED)
    z.writestr("Pic/layers/layer2.defaultpixel", bytes(ps))
    z.writestr("Pic/layers/mask1", kra_tiles(50, 40, 1, lambda x, y: bytes([255 if x < 25 else 0])), compress_type=zipfile.ZIP_DEFLATED)
    z.writestr("Pic/layers/mask1.defaultpixel", b"\0")
    # the middle layer's default pixel is a colour: where it has no tile (none here: it is full) it would show
    z.writestr("Pic/layers/layer3.defaultpixel", bytes(ps))
    z.close()
    meta = dict(w=W, h=H, dpi=150, layers=[
        dict(name="Bottom", kind="pixel", depth=0, opacity=255, visible=1, mode="normal"),
        dict(name="Middle", kind="pixel", depth=1, opacity=255, visible=0, mode="screen"),
        dict(name="Top", kind="pixel", depth=1, opacity=128, visible=1, mode="multiply"),
        dict(name="Group", kind="group", depth=0, opacity=255, visible=1, mode="pass")], probes=[],
        maskprobes=[dict(layer="Bottom", x=0, y=31, v=255), dict(layer="Bottom", x=25, y=31, v=0), dict(layer="Bottom", x=60, y=10, v=0)])
    for lname, k, ox, oy, lw, lh in [("Top", 1, 10, 12, 40, 30), ("Middle", 2, 0, 0, W, H), ("Bottom", 3, -5, 30, 50, 40)]:
        for (px_, py_) in [(0, 0), (7, 3), (lw - 1, lh - 1), (33, 21), (63, 63), (64, 0)]:
            if px_ >= lw or py_ >= lh: continue
            cx, cy = px_ + ox, py_ + oy
            if not (0 <= cx < W and 0 <= cy < H): continue
            meta["probes"].append(dict(layer=lname, x=cx, y=cy, rgba=list(colour(px_, py_, k))))
    meta["probes"].append(dict(layer="Top", x=5, y=5, rgba=[0, 0, 0, 0]))     # outside its tiles
    save(name, b.getvalue(), meta)

make_kra("rgba8.kra")
make_kra("rgba16.kra", depth16=True)
make_kra("graya8.kra", grey=True)

print("made", len([f for f in os.listdir(OUT) if not f.endswith((".json", ".txt", ".png"))]), "files in", OUT)
