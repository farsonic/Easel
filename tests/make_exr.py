#!/usr/bin/env python3
"""make_exr.py - OpenEXR test files for Easel 1.2's reader (tests/colourtest.c), written here from
the OpenEXR file layout with nothing but struct, zlib and numpy: scanline files, uncompressed, RLE,
ZIPS, ZIP and PIZ (Huffman and the Haar wavelet as OpenEXR's ImfHuf.cpp and ImfWav.cpp do them),
half and float channels. Every picture is W x H with channel c at (x, y) = f(c, x, y) below, values a
half holds exactly, so the C test can check each one. Writes tests/data/exr/*.exr."""
import heapq, os, struct, sys, zlib
import numpy as np

W, H = 37, 41
def value(c, x, y):
    # R, G, B, A (or Y): small multiples of 1/16 and 1/8, exact in a half
    return [x / 16.0, y / 8.0, ((x + y) % 7) / 4.0, 1.0 - (x % 4) / 8.0, (x + 2 * y) / 32.0][c]

def attr(name, typ, data):
    return name.encode() + b"\0" + typ.encode() + b"\0" + struct.pack("<i", len(data)) + data

def chlist(chans):
    out = b""
    for name, t in chans:
        out += name.encode() + b"\0" + struct.pack("<iB3xii", t, 0, 1, 1)
    return out + b"\0"

def line_bytes(chans, y):
    out = b""
    for name, t in chans:
        c = "RGBAY".index(name)
        vals = [value(c, x, y) for x in range(W)]
        if t == 1:
            out += np.array(vals, dtype="<f2").tobytes()
        else:
            out += np.array(vals, dtype="<f4").tobytes()
    return out

def predict(raw):
    n = len(raw)
    t = bytearray(n)
    t[0:(n + 1) // 2] = raw[0::2]
    t[(n + 1) // 2:] = raw[1::2]
    out = bytearray(t)
    p = t[0]
    for i in range(1, n):
        d = (t[i] - p + 128 + 256) & 255
        p = t[i]
        out[i] = d
    return bytes(out)

def rle(data):
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        r = 1
        while i + r < n and r < 128 and data[i + r] == data[i]:
            r += 1
        if r >= 3:
            out += bytes([r - 1, data[i]]); i += r; continue
        j = i
        while j < n and j - i < 127:
            if j + 2 < n and data[j] == data[j + 1] == data[j + 2]:
                break
            j += 1
        out += bytes([(256 - (j - i)) & 255]) + data[i:j]
        i = j
    return bytes(out)

# ---- PIZ ----
def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v

def wenc14(a, b):
    as_, bs = s16(a), s16(b)
    ms = s16((as_ + bs) >> 1)
    ds = s16(as_ - bs)
    return ms & 0xFFFF, ds & 0xFFFF

def wav2_encode(buf, start, nx, ox, ny, oy):
    n = min(nx, ny)
    p, p2 = 1, 2
    while p2 <= n:
        oy1, oy2, ox1, ox2 = oy * p, oy * p2, ox * p, ox * p2
        py = start
        ey = start + oy * (ny - p2)
        while py <= ey:
            px = py
            ex = py + ox * (nx - p2)
            while px <= ex:
                p01, p10 = px + ox1, px + oy1
                p11 = p10 + ox1
                i00, i01 = wenc14(buf[px], buf[p01])
                i10, i11 = wenc14(buf[p10], buf[p11])
                buf[px], buf[p10] = wenc14(i00, i10)
                buf[p01], buf[p11] = wenc14(i01, i11)
                px += ox2
            if nx & p:
                p10 = px + oy1
                i00, buf[p10] = wenc14(buf[px], buf[p10])
                buf[px] = i00
            py += oy2
        if ny & p:
            px = py
            ex = py + ox * (nx - p2)
            while px <= ex:
                p01 = px + ox1
                i00, buf[p01] = wenc14(buf[px], buf[p01])
                buf[px] = i00
                px += ox2
        p, p2 = p2, p2 << 1

def huff_lengths(freq):
    # plain Huffman code lengths (the sizes stay far under 58 bits for these files)
    heap = [(f, i, (s,)) for i, (s, f) in enumerate(sorted(freq.items()))]
    heapq.heapify(heap)
    length = {s: 0 for s in freq}
    if len(heap) == 1:
        length[heap[0][2][0]] = 1
        return length
    k = len(heap)
    while len(heap) > 1:
        f1, _, a = heapq.heappop(heap)
        f2, _, b = heapq.heappop(heap)
        for s in a + b:
            length[s] += 1
        heapq.heappush(heap, (f1 + f2, k, a + b)); k += 1
    return length

def huf_compress(raw):
    freq = {}
    for v in raw:
        freq[v] = freq.get(v, 0) + 1
    im, iM = min(freq), max(freq) + 1          # iM: the run-length pseudo-symbol (unused here)
    freq[iM] = 1
    length = huff_lengths(freq)
    # canonical codes as hufCanonicalCodeTable makes them
    n = [0] * 59
    for s, l in length.items():
        n[l] += 1
    c = 0
    for l in range(58, 0, -1):
        nc = (c + n[l]) >> 1
        n[l] = c
        c = nc
    code = {}
    for s in sorted(length):
        l = length[s]
        code[s] = (n[l], l)
        n[l] += 1
    bits = []
    def put(v, nb):
        for i in range(nb - 1, -1, -1):
            bits.append((v >> i) & 1)
    # the table: 6-bit lengths from im to iM, runs of zeros shortened
    s = im
    while s <= iM:
        l = length.get(s, 0)
        if l == 0:
            z = 0
            while s + z <= iM and length.get(s + z, 0) == 0:
                z += 1
            if z >= 6:
                z = min(z, 255 + 6); put(63, 6); put(z - 6, 8); s += z; continue
            if z >= 2:
                put(59 + z - 2, 6); s += z; continue
        put(l, 6); s += 1
    while len(bits) % 8:
        bits.append(0)
    table = bytes(int("".join(map(str, bits[i:i + 8])), 2) for i in range(0, len(bits), 8))
    bits = []
    for v in raw:
        cv, l = code[v]
        put(cv, l)
    nbits = len(bits)
    while len(bits) % 8:
        bits.append(0)
    data = bytes(int("".join(map(str, bits[i:i + 8])), 2) for i in range(0, len(bits), 8))
    return struct.pack("<IIIII", im, iM, len(table), nbits, 0) + table + data

def piz(chans, y0, lines):
    # the block as u16s, channel by channel, each channel all its lines
    vals = []
    for name, t in chans:
        for y in range(y0, y0 + lines):
            c = "RGBAY".index(name)
            v = [value(c, x, y) for x in range(W)]
            a = np.array(v, dtype="<f2" if t == 1 else "<f4").view("<u2")
            vals += list(int(u) for u in a)
    used = sorted(set(vals) | {0})
    bitmap = bytearray(8192)
    for u in used:
        bitmap[u >> 3] |= 1 << (u & 7)
    fwd = {u: i for i, u in enumerate(used)}
    buf = [fwd[v] for v in vals]
    maxv = len(used) - 1
    assert maxv < (1 << 14)
    at = 0
    for name, t in chans:
        size = 1 if t == 1 else 2
        for j in range(size):
            wav2_encode(buf, at + j, W, size, lines, W * size)
        at += W * lines * size
    nz = [i for i in range(8192) if bitmap[i]]
    mn, mx = nz[0], nz[-1]
    h = huf_compress(buf)
    return struct.pack("<HH", mn, mx) + bytes(bitmap[mn:mx + 1]) + struct.pack("<i", len(h)) + h

def write(path, chans, comp):
    lpb = {0: 1, 1: 1, 2: 1, 3: 16, 4: 32}[comp]
    hdr = b"\x76\x2f\x31\x01" + struct.pack("<I", 2)
    hdr += attr("channels", "chlist", chlist(chans))
    hdr += attr("compression", "compression", bytes([comp]))
    hdr += attr("dataWindow", "box2i", struct.pack("<iiii", 0, 0, W - 1, H - 1))
    hdr += attr("displayWindow", "box2i", struct.pack("<iiii", 0, 0, W - 1, H - 1))
    hdr += attr("lineOrder", "lineOrder", b"\0")
    hdr += attr("pixelAspectRatio", "float", struct.pack("<f", 1.0))
    hdr += attr("screenWindowCenter", "v2f", struct.pack("<ff", 0, 0))
    hdr += attr("screenWindowWidth", "float", struct.pack("<f", 1.0))
    hdr += b"\0"
    nchunks = (H + lpb - 1) // lpb
    chunks = []
    for k in range(nchunks):
        y0 = k * lpb
        lines = min(lpb, H - y0)
        raw = b"".join(line_bytes(chans, y) for y in range(y0, y0 + lines))
        if comp == 0:
            data = raw
        elif comp == 1:
            data = rle(predict(raw))
        elif comp in (2, 3):
            data = zlib.compress(predict(raw))
        else:
            data = piz(chans, y0, lines)
        if len(data) >= len(raw):
            data = raw
        chunks.append(struct.pack("<ii", y0, len(data)) + data)
    off = len(hdr) + 8 * nchunks
    table = b""
    for c in chunks:
        table += struct.pack("<Q", off)
        off += len(c)
    with open(path, "wb") as f:
        f.write(hdr + table + b"".join(chunks))

d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "exr")
os.makedirs(d, exist_ok=True)
A, B, G, R, Y = ("A", 1), ("B", 1), ("G", 1), ("R", 1), ("Y", 1)
write(os.path.join(d, "none_half_rgba.exr"), [A, B, G, R], 0)
write(os.path.join(d, "rle_half_y.exr"), [Y], 1)
write(os.path.join(d, "zips_half_rgb.exr"), [B, G, R], 2)
write(os.path.join(d, "zip_float_rgb.exr"), [("B", 2), ("G", 2), ("R", 2)], 3)
write(os.path.join(d, "piz_half_rgba.exr"), [A, B, G, R], 4)
write(os.path.join(d, "piz_float_y.exr"), [("Y", 2)], 4)
print("OpenEXR test files written to", d)
