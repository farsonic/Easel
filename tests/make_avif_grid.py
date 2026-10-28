#!/usr/bin/env python3
"""make_avif_grid.py - AVIF files Pillow can't write, for avif.c's tests: a 2 x 3 grid of tiles
(with an alpha grid), and single pictures with irot / imir. Each is built from tiles encoded by
Pillow, with a .png beside it holding what it should decode to.

usage: make_avif_grid.py outdir"""
import io, struct, sys, os
from PIL import Image, ImageDraw

def boxes(d, o=0, e=None):
    e = len(d) if e is None else e
    while o + 8 <= e:
        sz, t = struct.unpack('>I4s', d[o:o + 8])
        yield t.decode('latin1'), d[o + 8:o + sz]
        o += sz

def tile_parts(img):
    """the AV1 data and the av1C and colr boxes Pillow wrote for img"""
    b = io.BytesIO(); img.save(b, 'AVIF', quality=95, subsampling='4:4:4'); d = b.getvalue()
    top = dict(boxes(d)); meta = top['meta'][4:]
    m = dict(boxes(meta))
    ipco = dict(boxes(dict(boxes(m['iprp']))['ipco']))
    il = m['iloc']                        # version 0, offset/length 4 bytes, one item, one extent
    off, ln = struct.unpack('>II', il[14:22])
    av1c = struct.pack('>I', 8 + len(ipco['av1C'])) + b'av1C' + ipco['av1C']
    return d[off:off + ln], av1c

def box(t, payload): return struct.pack('>I', 8 + len(payload)) + t + payload
def full(t, ver, flags, payload): return box(t, struct.pack('>I', ver << 24 | flags) + payload)

def build(items, refs, props, assoc, primary):
    """items: [(id, type, data)], refs: [(type, from, [to])], props: [box], assoc: {id: [(idx, essential)]}"""
    infe = b''.join(full(b'infe', 2, 0, struct.pack('>HH', i, 0) + t + b'\0') for i, t, _ in items)
    iinf = full(b'iinf', 0, 0, struct.pack('>H', len(items)) + infe)
    iref = full(b'iref', 0, 0, b''.join(box(t, struct.pack('>HH', f, len(to)) + b''.join(struct.pack('>H', x) for x in to)) for t, f, to in refs)) if refs else b''
    ipco = box(b'ipco', b''.join(props))
    ipma = full(b'ipma', 0, 0, struct.pack('>I', len(assoc)) + b''.join(
        struct.pack('>HB', i, len(a)) + bytes((e << 7 | k) for k, e in a) for i, a in sorted(assoc.items())))
    iprp = box(b'iprp', ipco + ipma)
    def meta_with(base):
        ents, o = b'', base
        for i, t, data in items:
            ents += struct.pack('>HHHII', i, 0, 1, o, len(data)); o += len(data)
        iloc = full(b'iloc', 0, 0, bytes([0x44, 0x00]) + struct.pack('>H', len(items)) + ents)
        hdlr = full(b'hdlr', 0, 0, b'\0' * 4 + b'pict' + b'\0' * 12 + b'\0')
        pitm = full(b'pitm', 0, 0, struct.pack('>H', primary))
        return full(b'meta', 0, 0, hdlr + pitm + iloc + iinf + iref + iprp)
    ftyp = box(b'ftyp', b'avif' + b'\0\0\0\0' + b'avifmif1miaf')
    m = meta_with(0)
    base = len(ftyp) + len(m) + 8
    m = meta_with(base)
    return ftyp + m + box(b'mdat', b''.join(d for _, _, d in items))

def picture(w, h, seed):
    im = Image.new('RGBA', (w, h), (seed * 40 % 256, 90, 200, 255)); d = ImageDraw.Draw(im)
    for k in range(8):
        d.rectangle([k * w // 9, k * h // 11, k * w // 9 + w // 4, k * h // 11 + h // 5], fill=((k * 70 + seed * 13) % 256, k * 30, 255 - k * 20, 255))
    d.text((3, 3), str(seed), fill=(255, 255, 255, 255))
    return im

def ispe(w, h): return full(b'ispe', 0, 0, struct.pack('>II', w, h))
AUXC = full(b'auxC', 0, 0, b'urn:mpeg:mpegB:cicp:systems:auxiliary:alpha\0')

def main(out):
    os.makedirs(out, exist_ok=True)
    # a 2 x 3 grid of 64 x 48 tiles making 180 x 90 (the last column and row cropped), with alpha
    tw, th, cols, rows, W, H = 64, 48, 3, 2, 180, 90
    whole = Image.new('RGBA', (cols * tw, rows * th))
    items, tiles, atiles, props = [], [], [], []
    props += [ispe(W, H), ispe(tw, th)]          # 1, 2
    av1c_c = av1c_a = None
    nid = 10
    for r in range(rows):
        for c in range(cols):
            t = picture(tw, th, r * cols + c)
            alpha = Image.linear_gradient('L').resize((tw, th)).rotate(90 * (r * cols + c))
            t.putalpha(alpha)
            whole.paste(t, (c * tw, r * th))
            data, av1c_c = tile_parts(t.convert('RGB'))
            items.append((nid, b'av01', data)); tiles.append(nid); nid += 1
            adata, av1c_a = tile_parts(Image.merge('RGB', (alpha, alpha, alpha)))
            items.append((nid, b'av01', adata)); atiles.append(nid); nid += 1
    props += [av1c_c, AUXC]                       # 3, 4
    grid = bytes([0, 0, rows - 1, cols - 1]) + struct.pack('>HH', W, H)
    items = [(1, b'grid', grid), (2, b'grid', grid)] + items
    assoc = {1: [(1, 0)], 2: [(1, 0), (4, 1)]}
    for i in tiles: assoc[i] = [(2, 0), (3, 1)]
    for i in atiles: assoc[i] = [(2, 0), (3, 1), (4, 1)]
    refs = [(b'dimg', 1, tiles), (b'dimg', 2, atiles), (b'auxl', 2, [1])]
    open(os.path.join(out, 'grid_alpha.avif'), 'wb').write(build(items, refs, props, assoc, 1))
    # the alpha grid is coded as RGB gray (studio-free full range) so its luma is the alpha
    whole.crop((0, 0, W, H)).save(os.path.join(out, 'grid_alpha.png'))
    # rotations and mirrors of one picture
    src = picture(70, 40, 7).convert('RGB')
    data, av1c = tile_parts(src)
    for name, extra, f in [
            ('irot1', [full(b'irot', 0, 0, b'')[:8] + b'\x01'], lambda i: i.transpose(Image.ROTATE_90)),
            ('irot3', [box(b'irot', b'\x03')], lambda i: i.transpose(Image.ROTATE_270)),
            ('imir0', [box(b'imir', b'\x00')], lambda i: i.transpose(Image.FLIP_TOP_BOTTOM)),
            ('imir1', [box(b'imir', b'\x01')], lambda i: i.transpose(Image.FLIP_LEFT_RIGHT)),
            ('irot1imir1', [box(b'irot', b'\x01'), box(b'imir', b'\x01')], lambda i: i.transpose(Image.ROTATE_90).transpose(Image.FLIP_LEFT_RIGHT))]:
        extra = [box(b'irot', e[8:]) if e[4:8] == b'irot' else e for e in extra]
        props = [ispe(70, 40), av1c] + extra
        assoc = {1: [(1, 0), (2, 1)] + [(3 + k, 1) for k in range(len(extra))]}
        open(os.path.join(out, name + '.avif'), 'wb').write(build([(1, b'av01', data)], [], props, assoc, 1))
        ref = Image.open(io.BytesIO(_enc(src)))
        f(ref.convert('RGB')).save(os.path.join(out, name + '.png'))

def _enc(img):
    b = io.BytesIO(); img.save(b, 'AVIF', quality=95, subsampling='4:4:4'); return b.getvalue()

if __name__ == '__main__':
    main(sys.argv[1])
