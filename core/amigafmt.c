/*
 * amigafmt.c - the Amiga's other picture files, and a few of its neighbours', to RGBA (see
 * amigafmt.h): IFF ACBM, RGBN/RGB8, DEEP and ANIM; ILBMs whose palette changes line by line (SHAM,
 * CTBL, PCHG); Workbench .info icons (classic planar images with the 1.x, 2.x and MagicWB
 * palettes, NewIcons, OS 3.5 GlowIcons, PNG icons); Sun raster, SGI, and the Atari ST's Degas and
 * NEOchrome. ilbm.c keeps plain ILBM and PBM; this file only calls it for an ANIM's first frame.
 *
 * Parts ported from PhotoCraft (https://github.com/storytold/photocraft), Copyright (c) 2026
 * ArtCraft Team and the PhotoCraft contributors, used under the MIT License (see Licenses.txt):
 * the IFF chunk walk (a chunk running past its FORM is cut short, chunks padded to even lengths)
 * and the ByteRun1 unpacker, from crates/codecs/src/codecs/iff.rs.
 *
 * Everything else is written from the published format descriptions: EA IFF 85 and the ILBM,
 * ACBM, ANIM, RGBN/RGB8 (Impulse), DEEP (TVPaint/XiPaint) and PCHG (Sebastiano Vigna) specs, the
 * SHAM and CTBL notes, the Workbench DiskObject layout (icon.library), the NewIcons and OS 3.5
 * ColorIcon (FORM ICON) descriptions, the Sun rasterfile and SGI image file format documents, and
 * the Degas and NEOchrome file layouts. The 1.x, 2.x and MagicWB palettes are plain colour data.
 *
 * Every number is big-endian (the Amiga's, Sun's, SGI's and the ST's order) and read a byte at a
 * time. Input is hostile: every length and offset is checked, and nothing big goes on the stack.
 */
#include <stdlib.h>
#include <string.h>
#include "codec.h"
#include "amigafmt.h"

static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static u32 be16(const u8 *p) { return ((u32)p[0] << 8) | p[1]; }
static int fits(i32 w, i32 h) { return w >= 1 && h >= 1 && w <= MAXDIM && h <= MAXDIM && (long)w * h <= CODEC_MAXPIXELS; }
static int fail(Rgba *out, char *err, size_t errcap, const char *msg) { rgba_free(out); codec_err(err, errcap, msg); return 0; }
static const char *oom(void) { codec_oom = 1; return "Out of memory"; }

/* ================================================================== IFF */
/* the chunks of the FORM at d (n bytes): returns the next chunk at or after *o, its id and data
   (cut short if it runs past the FORM, which the reader then notices); 0 at the end */
typedef struct { const u8 *d; size_t end, o; } IffWalk;
static void iff_begin(IffWalk *w, const u8 *d, size_t n)
{
  size_t end = n >= 8 ? be32(d + 4) : 0;
  w->d = d; w->end = end > n - 8 ? n : end + 8; w->o = 12;
}
static int iff_next(IffWalk *w, const u8 **id, const u8 **c, size_t *len)
{
  size_t l, avail;
  if (w->o + 8 > w->end) return 0;
  l = be32(w->d + w->o + 4); avail = w->end - w->o - 8;
  if (l > avail) l = avail;
  *id = w->d + w->o; *c = w->d + w->o + 8; *len = l;
  w->o += 8 + l + (l & 1);
  return 1;
}
static int is_form(const u8 *d, size_t n, const char *type) { return n >= 12 && !memcmp(d, "FORM", 4) && !memcmp(d + 8, type, 4); }

/* ByteRun1 from s[0..n) until out holds size bytes: 0..127 copies the next n + 1 bytes, -1..-127
   repeats the next byte 1 - n times, -128 does nothing. Runs may cross rows. Returns the bytes
   of s used, or 0 if s runs out first */
static size_t unpack1(const u8 *s, size_t n, u8 *out, size_t size)
{
  size_t i = 0, o = 0;
  while (o < size) {
    int c;
    size_t k;
    if (i >= n) return 0;
    c = s[i++];
    if (c < 128) {
      k = (size_t)c + 1;
      if (k > n - i) return 0;
      if (k > size - o) k = size - o;
      memcpy(out + o, s + i, k);
      o += k; i += (size_t)c + 1;
    } else if (c > 128) {
      if (i >= n) return 0;
      k = 257 - (size_t)c;
      if (k > size - o) k = size - o;
      memset(out + o, s[i++], k);
      o += k;
    }
  }
  return i;
}

/* ------------------------------------------------------------------ bitplane pictures */
#define CAMG_EHB 0x80
#define CAMG_HAM 0x800

/* what a FORM ILBM or ACBM says about its picture */
typedef struct {
  i32 w, h;
  int np, masking, comp, tcol, haveh, ncmap;
  u32 camg;
  u8 pal[256 * 3];
  const u8 *body; size_t bodylen;               /* BODY or ABIT */
  const u8 *sham; size_t shamlen; int shamkind; /* 1 SHAM, 2 CTBL */
  const u8 *pchg; size_t pchglen;
} Planar;

static void planar_scan(const u8 *d, size_t n, const char *bodyid, Planar *f)
{
  IffWalk w;
  const u8 *id, *c;
  size_t len;
  int i;
  memset(f, 0, sizeof *f);
  iff_begin(&w, d, n);
  while (iff_next(&w, &id, &c, &len)) {
    if (!memcmp(id, "BMHD", 4) && len >= 20) {
      f->w = (i32)be16(c); f->h = (i32)be16(c + 2); f->np = c[8]; f->masking = c[9]; f->comp = c[10]; f->tcol = (int)be16(c + 12);
      f->haveh = 1;
    } else if (!memcmp(id, "CMAP", 4)) {
      f->ncmap = (int)(len / 3 > 256 ? 256 : len / 3);
      memcpy(f->pal, c, (size_t)f->ncmap * 3);
    } else if (!memcmp(id, "CAMG", 4) && len >= 4) f->camg = be32(c);
    else if (!memcmp(id, "SHAM", 4) && !f->sham) { f->sham = c; f->shamlen = len; f->shamkind = 1; }
    else if (!memcmp(id, "CTBL", 4) && !f->sham) { f->sham = c; f->shamlen = len; f->shamkind = 2; }
    else if (!memcmp(id, "PCHG", 4) && !f->pchg) { f->pchg = c; f->pchglen = len; }
    else if (!memcmp(id, bodyid, 4) && !f->body) { f->body = c; f->bodylen = len; }
  }
  /* no CMAP: a grey ramp, as ilbm.c does */
  if (!f->ncmap && f->np >= 1 && f->np <= 8)
    for (i = 0; i < (1 << f->np); i++) f->pal[i * 3] = f->pal[i * 3 + 1] = f->pal[i * 3 + 2] = (u8)(i * 255 / ((1 << f->np) - 1));
}

/* a palette change: from line `line` on, register reg is r, g, b */
typedef struct { i32 line; u16 reg; u8 r, g, b; } PalChange;

/* the PCHG Huffman decompression (from the PCHG description): the tree is an array of 16-bit
   words walked from its last; a 1 bit takes the current word as a leaf if it is >= 0, else
   moves back by half its (negative, byte) value; a 0 bit steps back one word, which is a leaf
   when it is positive with bit 8 set */
static int pchg_unhuff(const u8 *tree, size_t tsize, const u8 *src, size_t slen, u8 *dst, size_t osize)
{
  long nt = (long)(tsize / 2), p = nt - 1;
  size_t i = 0, sb = 0;
  int bits = 0, byte = 0;
  if (nt < 1) return 0;
  while (i < osize) {
    int v;
    if (!bits) { if (sb >= slen) return 0; byte = src[sb++]; bits = 8; }
    if (byte & 0x80) {
      v = (int)(i16)be16(tree + p * 2);
      if (v >= 0) { dst[i++] = (u8)v; p = nt - 1; }
      else { p += v / 2; if (p < 0) return 0; }
    } else {
      if (--p < 0) return 0;
      v = (int)(i16)be16(tree + p * 2);
      if (v > 0 && (v & 0x100)) { dst[i++] = (u8)v; p = nt - 1; }
    }
    byte = (byte << 1) & 255;
    bits--;
  }
  return 1;
}

/* a PCHG chunk as a list of changes in line order; NULL with *msg on a damaged one (an empty list
   is a valid result: *nchg 0) */
static PalChange *pchg_parse(const u8 *c, size_t len, long *nchg, const char **msg)
{
  int compression, flags, start, lines, i;
  const u8 *data;
  u8 *dec = NULL;
  size_t dlen, o, masklen;
  PalChange *ch = NULL;
  long n = 0, cap;
  *nchg = 0; *msg = "Damaged PCHG palette changes";
  if (len < 20) return NULL;
  compression = (int)be16(c); flags = (int)be16(c + 2); start = (int)(i16)be16(c + 4); lines = (int)be16(c + 6);
  data = c + 20; dlen = len - 20;
  if (compression == 1) {
    u32 tsize, osize;
    if (dlen < 8) return NULL;
    tsize = be32(data); osize = be32(data + 4);
    if (tsize > dlen - 8 || osize > 16 * 1024 * 1024 || osize > (dlen - 8 - tsize) * 8 + 8) return NULL;
    if (!(dec = (u8 *)malloc(osize ? osize : 1))) { *msg = oom(); return NULL; }
    if (!pchg_unhuff(data + 8, tsize, data + 8 + tsize, dlen - 8 - tsize, dec, osize)) { free(dec); return NULL; }
    data = dec; dlen = osize;
  } else if (compression != 0) return NULL;
  masklen = (size_t)((lines + 31) / 32) * 4;
  if (masklen > dlen) { free(dec); return NULL; }
  /* at most one change per two bytes of change data */
  cap = (long)(dlen / 2) + 1;
  if (!(ch = (PalChange *)malloc(sizeof(PalChange) * (size_t)cap))) { free(dec); *msg = oom(); return NULL; }
  o = masklen;
  for (i = 0; i < lines; i++) {
    int k, cnt;
    if (!((data[i >> 3] >> (7 - (i & 7))) & 1)) continue;
    if (flags & 2) {                                  /* 32-bit: register, alpha, red, blue, green */
      if (o + 2 > dlen) goto bad;
      cnt = (int)be16(data + o); o += 2;
      for (k = 0; k < cnt; k++, o += 6) {
        if (o + 6 > dlen || n >= cap) goto bad;
        ch[n].line = start + i; ch[n].reg = (u16)be16(data + o);
        ch[n].r = data[o + 3]; ch[n].b = data[o + 4]; ch[n].g = data[o + 5];
        n++;
      }
    } else {                                          /* 12-bit: counts for registers 0-15 and 16-31 */
      int c16, c32;
      if (o + 2 > dlen) goto bad;
      c16 = data[o]; c32 = data[o + 1]; o += 2;
      for (k = 0; k < c16 + c32; k++, o += 2) {
        u32 wd;
        if (o + 2 > dlen || n >= cap) goto bad;
        wd = be16(data + o);
        ch[n].line = start + i; ch[n].reg = (u16)((wd >> 12) + (k >= c16 ? 16 : 0));
        ch[n].r = (u8)(((wd >> 8) & 15) * 17); ch[n].g = (u8)(((wd >> 4) & 15) * 17); ch[n].b = (u8)((wd & 15) * 17);
        n++;
      }
    }
  }
  free(dec);
  *nchg = n; *msg = NULL;
  return ch;
bad:
  free(dec); free(ch);
  return NULL;
}

/* the planes (unpacked; plane p of row y at buf + y * ystride + p * pstride) as RGBA: 1-8 planes
   through the palette (with EHB or HAM6/8), 24 or 32 deep, a mask plane (masking 1) or
   transparent colour (masking 2), and the per-line palettes of SHAM, CTBL and PCHG */
static const char *planar_render(const Planar *f, const u8 *buf, size_t ystride, size_t pstride, Rgba *out)
{
  u8 pal[256 * 3];
  u32 *vals = NULL;
  PalChange *chg = NULL;
  long nchg = 0, next = 0;
  int np = f->np, deep = np == 24 || np == 32, ham, ehb, nsham = 0, p;
  i32 x, y, w = f->w;
  const char *msg = NULL;
  memcpy(pal, f->pal, sizeof pal);
  ham = !deep && (f->camg & CAMG_HAM) && (np == 6 || np == 8);
  if (f->sham && np == 6) ham = 1;                   /* SHAM and CTBL are HAM6 pictures with 16 colours a line */
  ehb = !deep && !ham && np == 6 && ((f->camg & CAMG_EHB) || (f->ncmap > 0 && f->ncmap <= 32));
  if (f->sham) { size_t hdr = f->shamkind == 1 ? 2 : 0; nsham = f->shamlen >= hdr ? (int)((f->shamlen - hdr) / 32) : 0; }
  if (f->pchg && !(chg = pchg_parse(f->pchg, f->pchglen, &nchg, &msg))) return msg;
  if (!(vals = (u32 *)malloc(sizeof(u32) * (size_t)w))) { free(chg); return oom(); }
  if (!rgba_alloc(out, w, f->h)) { free(chg); free(vals); return "Out of memory"; }
  for (y = 0; y < f->h && !progress(y, f->h); y++) {
    const u8 *row = buf + (size_t)y * ystride;
    u8 *px = out->px + (size_t)y * w * 4, cr, cg, cb;
    if (nsham) {                                      /* this line's 16 colours (laced pictures share them in pairs) */
      int k = (int)(nsham >= f->h ? y : (long)y * nsham / f->h), i;
      const u8 *s = f->sham + (f->shamkind == 1 ? 2 : 0) + (size_t)k * 32;
      for (i = 0; i < 16; i++) {
        u32 wd = be16(s + i * 2);
        pal[i * 3] = (u8)(((wd >> 8) & 15) * 17); pal[i * 3 + 1] = (u8)(((wd >> 4) & 15) * 17); pal[i * 3 + 2] = (u8)((wd & 15) * 17);
      }
    }
    while (next < nchg && chg[next].line <= y) {
      if (chg[next].reg < 256) { u8 *q = pal + chg[next].reg * 3; q[0] = chg[next].r; q[1] = chg[next].g; q[2] = chg[next].b; }
      next++;
    }
    memset(vals, 0, sizeof(u32) * (size_t)w);
    for (p = 0; p < np; p++) {
      const u8 *pl = row + (size_t)p * pstride;
      for (x = 0; x < w; x++) vals[x] |= (u32)((pl[x >> 3] >> (7 - (x & 7))) & 1) << p;
    }
    cr = pal[0]; cg = pal[1]; cb = pal[2];            /* HAM starts each line from colour 0 */
    for (x = 0; x < w; x++, px += 4) {
      u32 v = vals[x];
      px[3] = 255;
      if (deep) {
        px[0] = (u8)v; px[1] = (u8)(v >> 8); px[2] = (u8)(v >> 16);
        if (np == 32) px[3] = (u8)(v >> 24);
      } else if (ham) {
        int hb = np - 2, ctl = (int)(v >> hb);
        u32 dv = v & ((1u << hb) - 1), m = dv << (8 - hb);
        m |= m >> hb;
        if (ctl == 0) { cr = pal[dv * 3]; cg = pal[dv * 3 + 1]; cb = pal[dv * 3 + 2]; }
        else if (ctl == 1) cb = (u8)m;
        else if (ctl == 2) cr = (u8)m;
        else cg = (u8)m;
        px[0] = cr; px[1] = cg; px[2] = cb;
      } else if (ehb && v >= 32) {
        px[0] = (u8)(pal[(v - 32) * 3] >> 1); px[1] = (u8)(pal[(v - 32) * 3 + 1] >> 1); px[2] = (u8)(pal[(v - 32) * 3 + 2] >> 1);
      } else { px[0] = pal[v * 3]; px[1] = pal[v * 3 + 1]; px[2] = pal[v * 3 + 2]; }
      if (f->masking == 1) { const u8 *ml = row + (size_t)np * pstride; if (!((ml[x >> 3] >> (7 - (x & 7))) & 1)) px[3] = 0; }
      else if (f->masking == 2 && !deep && (int)v == f->tcol) px[3] = 0;
    }
  }
  free(chg); free(vals);
  return NULL;
}

/* checks shared by ILBM and ACBM */
static const char *planar_check(const Planar *f)
{
  if (!f->haveh) return "The IFF picture has no BMHD header";
  if (!f->body) return "The IFF picture has no picture data";
  if (!fits(f->w, f->h)) return "Bad IFF picture size";
  if (!((f->np >= 1 && f->np <= 8) || f->np == 24 || f->np == 32)) return "Unsupported number of IFF bitplanes";
  if (f->comp > 1) return "Unsupported IFF compression";
  return NULL;
}

/* ------------------------------------------------------------------ ILBM with line palettes */
int sham_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  Planar *f;
  u8 *buf = NULL;
  size_t rowbytes, ystride, size;
  int nplanes;
  const char *msg;
  out->w = out->h = 0; out->px = NULL;
  if (!is_form(d, n, "ILBM")) return fail(out, err, errcap, "Not an IFF ILBM file");
  if (!(f = (Planar *)malloc(sizeof(Planar)))) return fail(out, err, errcap, oom());
  planar_scan(d, n, "BODY", f);
  if ((msg = planar_check(f))) goto done;
  nplanes = f->np + (f->masking == 1);
  rowbytes = (size_t)((f->w + 15) >> 4) << 1;
  ystride = rowbytes * (size_t)nplanes;
  size = ystride * (size_t)f->h;
  msg = "Truncated ILBM file";
  if ((f->comp == 0 && f->bodylen < size) || (f->comp == 1 && f->bodylen < size / 128)) goto done;
  if (!(buf = (u8 *)malloc(size))) { msg = oom(); goto done; }
  if (f->comp == 0) memcpy(buf, f->body, size);
  else if (!unpack1(f->body, f->bodylen, buf, size)) goto done;
  msg = planar_render(f, buf, ystride, rowbytes, out);
done:
  free(buf); free(f);
  if (msg) return fail(out, err, errcap, msg);
  return 1;
}

/* ------------------------------------------------------------------ ACBM */
/* each plane whole, one after another (ABIT), normally uncompressed */
int acbm_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  Planar *f;
  u8 *buf = NULL;
  size_t rowbytes, size;
  int nplanes;
  const char *msg;
  out->w = out->h = 0; out->px = NULL;
  if (!is_form(d, n, "ACBM")) return fail(out, err, errcap, "Not an IFF ACBM file");
  if (!(f = (Planar *)malloc(sizeof(Planar)))) return fail(out, err, errcap, oom());
  planar_scan(d, n, "ABIT", f);
  if ((msg = planar_check(f))) goto done;
  nplanes = f->np + (f->masking == 1);
  rowbytes = (size_t)((f->w + 15) >> 4) << 1;
  size = rowbytes * (size_t)f->h * (size_t)nplanes;
  msg = "Truncated ACBM file";
  if ((f->comp == 0 && f->bodylen < size) || (f->comp == 1 && f->bodylen < size / 128)) goto done;
  if (!(buf = (u8 *)malloc(size))) { msg = oom(); goto done; }
  if (f->comp == 0) memcpy(buf, f->body, size);
  else if (!unpack1(f->body, f->bodylen, buf, size)) goto done;
  msg = planar_render(f, buf, rowbytes, rowbytes * (size_t)f->h, out);
done:
  free(buf); free(f);
  if (msg) return fail(out, err, errcap, msg);
  return 1;
}

/* ------------------------------------------------------------------ RGBN and RGB8 */
/* Impulse's run-length pixels: RGB8 is R, G, B and a byte (genlock bit 7, count 0-127); RGBN a
   word of 4-bit R, G, B, the genlock bit and a 3-bit count. A count of 0 means the count is in the
   next byte, or if that is 0 the next word. Runs carry across lines */
int rgbn_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  IffWalk w;
  const u8 *id, *c, *body = NULL;
  size_t len, bl = 0, i = 0;
  i32 bw = 0, bh = 0;
  int comp = -1, rgb8;
  long pos, total;
  out->w = out->h = 0; out->px = NULL;
  rgb8 = is_form(d, n, "RGB8");
  if (!rgb8 && !is_form(d, n, "RGBN")) return fail(out, err, errcap, "Not an IFF RGBN or RGB8 file");
  iff_begin(&w, d, n);
  while (iff_next(&w, &id, &c, &len)) {
    if (!memcmp(id, "BMHD", 4) && len >= 20) { bw = (i32)be16(c); bh = (i32)be16(c + 2); comp = c[10]; }
    else if (!memcmp(id, "BODY", 4) && !body) { body = c; bl = len; }
  }
  if (comp < 0) return fail(out, err, errcap, "The IFF picture has no BMHD header");
  if (!body) return fail(out, err, errcap, "The IFF picture has no picture data");
  if (!fits(bw, bh)) return fail(out, err, errcap, "Bad IFF picture size");
  if (comp != 4) return fail(out, err, errcap, "Unsupported RGBN/RGB8 compression");
  if (!rgba_alloc(out, bw, bh)) return fail(out, err, errcap, "Out of memory");
  total = (long)bw * bh;
  for (pos = 0; pos < total; ) {
    u8 r, g, b, a;
    u32 cnt;
    if (rgb8) {
      if (bl - i < 4) break;
      r = body[i]; g = body[i + 1]; b = body[i + 2]; a = body[i + 3] & 0x80 ? 0 : 255; cnt = body[i + 3] & 0x7F; i += 4;
    } else {
      u32 wd;
      if (bl - i < 2) break;
      wd = be16(body + i); i += 2;
      r = (u8)((wd >> 12) * 17); g = (u8)(((wd >> 8) & 15) * 17); b = (u8)(((wd >> 4) & 15) * 17); a = wd & 8 ? 0 : 255; cnt = wd & 7;
    }
    if (!cnt) {
      if (i >= bl) break;
      if (!(cnt = body[i++])) { if (bl - i < 2) break; cnt = be16(body + i); i += 2; }
      if (!cnt) return fail(out, err, errcap, "Damaged RGBN/RGB8 picture");
    }
    for (; cnt && pos < total; cnt--, pos++) { u8 *q = out->px + pos * 4; q[0] = r; q[1] = g; q[2] = b; q[3] = a; }
  }
  if (pos < total) return fail(out, err, errcap, "Truncated RGBN/RGB8 picture");
  return 1;
}

/* ------------------------------------------------------------------ DEEP */
/* TVPaint's chunky deep pixels. DPEL lists the elements of a pixel (type, bit depth); red, green,
   blue and alpha of 8 or 16 bits are used (16 keeps the high byte), others skipped. Compression
   (in DGBL): 0 none; 1 run length, ByteRun1 counting whole pixels; 5 TVDC, each line of each
   element as 4-bit codes through the TVDC chunk's 16 deltas, a delta of 0 meaning the next code
   + 1 repeats of the value, the value starting at 0 and the code stream at a byte for every line
   of every element. DLOC places the body on the DGBL canvas */
int deep_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  IffWalk w;
  const u8 *id, *c, *body = NULL, *dpel = NULL;
  size_t len, bl = 0, dpl = 0;
  i32 cw = 0, ch = 0, bw = 0, bh = 0, bx = 0, by = 0, x, y;
  int comp = 0, haveg = 0, haveloc = 0, ne = 0, e, bpp = 0, hasalpha = 0, i;
  int etype[16], ebytes[16], eoff[16];
  i16 tvdc[16];
  int havetvdc = 0;
  u8 *px = NULL;
  const char *msg = NULL;
  out->w = out->h = 0; out->px = NULL;
  if (!is_form(d, n, "DEEP")) return fail(out, err, errcap, "Not an IFF DEEP file");
  iff_begin(&w, d, n);
  while (iff_next(&w, &id, &c, &len)) {
    if (!memcmp(id, "DGBL", 4) && len >= 8) { cw = (i32)be16(c); ch = (i32)be16(c + 2); comp = (int)be16(c + 4); haveg = 1; }
    else if (!memcmp(id, "DPEL", 4) && !dpel) { dpel = c; dpl = len; }
    else if (!memcmp(id, "DLOC", 4) && len >= 8 && !haveloc) { bw = (i32)be16(c); bh = (i32)be16(c + 2); bx = (i16)be16(c + 4); by = (i16)be16(c + 6); haveloc = 1; }
    else if (!memcmp(id, "TVDC", 4) && len >= 32) { for (i = 0; i < 16; i++) tvdc[i] = (i16)be16(c + i * 2); havetvdc = 1; }
    else if (!memcmp(id, "DBOD", 4) && !body) { body = c; bl = len; }
  }
  if (!dpel || dpl < 4) return fail(out, err, errcap, "The DEEP file has no DPEL chunk");
  if (!body) return fail(out, err, errcap, "The DEEP file has no picture data");
  if (!haveg) { cw = bw; ch = bh; }
  if (!haveloc) { bw = cw; bh = ch; }
  if (!fits(cw, ch) || !fits(bw, bh)) return fail(out, err, errcap, "Bad DEEP picture size");
  ne = (int)be32(dpel);
  if (ne < 1 || ne > 16 || (size_t)ne * 4 > dpl - 4) return fail(out, err, errcap, "Unsupported DEEP pixel layout");
  for (e = 0; e < ne; e++) {
    int depth = (int)be16(dpel + 4 + e * 4 + 2);
    etype[e] = (int)be16(dpel + 4 + e * 4);
    if (depth != 8 && depth != 16) return fail(out, err, errcap, "Unsupported DEEP element depth");
    ebytes[e] = depth / 8; eoff[e] = bpp; bpp += ebytes[e];
    if (etype[e] == 4) hasalpha = 1;
  }
  if (!rgba_alloc(out, cw, ch)) return fail(out, err, errcap, "Out of memory");
  if (!(px = (u8 *)calloc((size_t)bw * bh, (size_t)bpp))) return fail(out, err, errcap, oom());
  if (comp == 0) {
    if (bl < (size_t)bw * bh * bpp) msg = "Truncated DEEP picture";
    else memcpy(px, body, (size_t)bw * bh * bpp);
  } else if (comp == 1) {
    long pos = 0, total = (long)bw * bh;
    size_t i2 = 0;
    while (pos < total) {
      int op;
      long k;
      if (i2 >= bl) { msg = "Truncated DEEP picture"; break; }
      op = body[i2++];
      if (op < 128) {
        k = op + 1;
        if ((size_t)(k * bpp) > bl - i2) { msg = "Truncated DEEP picture"; break; }
        if (k > total - pos) k = total - pos;
        memcpy(px + pos * bpp, body + i2, (size_t)(k * bpp));
        i2 += (size_t)(op + 1) * bpp; pos += k;
      } else if (op > 128) {
        k = 257 - op;
        if ((size_t)bpp > bl - i2) { msg = "Truncated DEEP picture"; break; }
        for (; k > 0 && pos < total; k--, pos++) memcpy(px + pos * bpp, body + i2, (size_t)bpp);
        i2 += (size_t)bpp;
      }
    }
  } else if (comp == 5) {
    size_t nib = 0, nn = bl * 2;
    if (!havetvdc) msg = "The DEEP file has no TVDC table";
    for (y = 0; y < bh && !msg; y++)
      for (e = 0; e < ne && !msg; e++)
        for (i = 0; i < ebytes[e] && !msg; i++) {
          int v = 0;
          x = 0;
          while (x < bw) {
            int code, dlt;
            if (nib >= nn) { msg = "Truncated DEEP picture"; break; }
            code = (body[nib >> 1] >> ((nib & 1) ? 0 : 4)) & 15; nib++;
            dlt = tvdc[code];
            if (dlt) { v = (v + dlt) & 255; px[((size_t)y * bw + x) * bpp + eoff[e] + i] = (u8)v; x++; }
            else {
              int rep;
              if (nib >= nn) { msg = "Truncated DEEP picture"; break; }
              rep = ((body[nib >> 1] >> ((nib & 1) ? 0 : 4)) & 15) + 1; nib++;
              for (; rep > 0 && x < bw; rep--, x++) px[((size_t)y * bw + x) * bpp + eoff[e] + i] = (u8)v;
            }
          }
          nib = (nib + 1) & ~(size_t)1;
        }
  } else msg = "Unsupported DEEP compression";
  if (!msg) {
    for (y = 0; y < bh; y++) {
      if (by + y < 0 || by + y >= ch) continue;
      for (x = 0; x < bw; x++) {
        const u8 *s = px + ((size_t)y * bw + x) * bpp;
        u8 *o;
        if (bx + x < 0 || bx + x >= cw) continue;
        o = out->px + ((size_t)(by + y) * cw + bx + x) * 4;
        o[3] = 255;
        for (e = 0; e < ne; e++) if (etype[e] >= 1 && etype[e] <= 4) o[etype[e] - 1] = s[eoff[e]];
        if (!hasalpha) o[3] = 255;
      }
    }
  }
  free(px);
  if (msg) return fail(out, err, errcap, msg);
  return 1;
}

/* ------------------------------------------------------------------ ANIM */
/* the first frame: the first picture FORM inside (the ones after it hold only deltas) */
int anim_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  IffWalk w;
  const u8 *id, *c;
  size_t len;
  out->w = out->h = 0; out->px = NULL;
  if (!is_form(d, n, "ANIM")) return fail(out, err, errcap, "Not an IFF ANIM file");
  iff_begin(&w, d, n);
  while (iff_next(&w, &id, &c, &len)) {
    const u8 *f = id;
    size_t fl = len + 8;
    if (memcmp(id, "FORM", 4) || len < 4) continue;
    if (!memcmp(c, "ILBM", 4)) {
      if (amigafmt_detect(f, fl) == AMF_ILBM_LINEPAL) return sham_read(f, fl, out, err, errcap);
      return ilbm_read(f, fl, out, err, errcap);
    }
    if (!memcmp(c, "PBM ", 4)) return ilbm_read(f, fl, out, err, errcap);
    if (!memcmp(c, "ACBM", 4)) return acbm_read(f, fl, out, err, errcap);
  }
  return fail(out, err, errcap, "The ANIM file has no first frame");
}

/* ================================================================== Workbench icons */
/* the Workbench's own colours: 1.x (blue, white, black, orange), 2.x and later (grey, black,
   white, blue) and MagicWB's eight (2.x's four and dark grey, light grey, beige, pink) */
static const u8 pal13[4 * 3] = { 0, 85, 170, 255, 255, 255, 0, 0, 34, 255, 136, 0 };
static const u8 palmwb[8 * 3] = { 170, 170, 170, 0, 0, 0, 255, 255, 255, 102, 136, 187, 153, 153, 153, 187, 187, 187, 187, 170, 153, 255, 170, 153 };

typedef struct { const u8 *d; size_t n, o; int bad; } Cur;
static u32 cur32(Cur *c) { u32 v; if (c->bad || c->n - c->o < 4) { c->bad = 1; return 0; } v = be32(c->d + c->o); c->o += 4; return v; }
static void cur_skip(Cur *c, size_t k) { if (c->bad || c->n - c->o < k) { c->bad = 1; return; } c->o += k; }

/* an icon's classic Image (struct Image, then its planes one after another) at the cursor */
typedef struct { i32 w, h; int depth, pick, onoff; const u8 *data; } IconImage;
static void read_image(Cur *c, IconImage *im)
{
  const u8 *p;
  u32 dataptr;
  size_t size;
  memset(im, 0, sizeof *im);
  if (c->bad || c->n - c->o < 20) { c->bad = 1; return; }
  p = c->d + c->o;
  im->w = (i32)(i16)be16(p + 4); im->h = (i32)(i16)be16(p + 6); im->depth = (int)(i16)be16(p + 8);
  dataptr = be32(p + 10); im->pick = p[14]; im->onoff = p[15];
  c->o += 20;
  if (im->w < 0 || im->h < 0 || im->depth < 0 || im->depth > 8) { c->bad = 1; return; }
  size = (size_t)((im->w + 15) >> 4) * 2 * (size_t)im->h * (size_t)im->depth;
  if (dataptr) { if (c->n - c->o < size) { c->bad = 1; return; } im->data = c->d + c->o; c->o += size; }
}

/* a classic image through the chosen palette; PlanePick says which colour planes the stored
   planes become (the rest are PlaneOnOff's bits) */
static const char *classic_icon(const IconImage *im, int rev, int palchoice, int clear0, Rgba *out)
{
  u8 pal[256 * 3];
  size_t rowbytes = (size_t)((im->w + 15) >> 4) * 2;
  int i, pick = im->pick, depth = im->depth, ndest;
  i32 x, y;
  if (!im->data || !fits(im->w, im->h) || depth < 1) return "The icon has no picture";
  if (palchoice == INFO_PAL_AUTO) palchoice = rev < 1 ? INFO_PAL_13 : depth > 2 ? INFO_PAL_MWB : INFO_PAL_20;
  for (i = 0; i < 256; i++) pal[i * 3] = pal[i * 3 + 1] = pal[i * 3 + 2] = (u8)i;   /* colours past the palette: a grey ramp */
  if (palchoice == INFO_PAL_13) memcpy(pal, pal13, sizeof pal13);
  else if (palchoice == INFO_PAL_20) memcpy(pal, palmwb, 4 * 3);
  else memcpy(pal, palmwb, sizeof palmwb);
  if (!pick) pick = (1 << depth) - 1;                /* no planes picked: take them all */
  ndest = 8;
  if (!rgba_alloc(out, im->w, im->h)) return "Out of memory";
  for (y = 0; y < im->h; y++)
    for (x = 0; x < im->w; x++) {
      int v = 0, sp = 0, dp;
      u8 *o = out->px + ((size_t)y * im->w + x) * 4;
      for (dp = 0; dp < ndest; dp++) {
        if (pick & (1 << dp)) {
          if (sp < depth) { const u8 *pl = im->data + ((size_t)sp * im->h + y) * rowbytes; v |= ((pl[x >> 3] >> (7 - (x & 7))) & 1) << dp; }
          sp++;
        } else v |= ((im->onoff >> dp) & 1) << dp;
      }
      o[0] = pal[v * 3]; o[1] = pal[v * 3 + 1]; o[2] = pal[v * 3 + 2]; o[3] = clear0 && !v ? 0 : 255;
    }
  return NULL;
}

/* NewIcons: the image in the tooltypes "IM1=" (normal) or "IM2=" (selected). The first line
   starts with the header - 'B' (colour 0 transparent) or 'C', width and height + 0x21, the colour
   count in two characters of 6 bits each + 0x21 - then the palette; the pixels start on the next
   line. Each character is 7 bits of a big-endian bit stream: 0x20-0x6F are 0x00-0x4F, 0xA1-0xD0
   are 0x50-0x7F, and 0xD1-0xFF are runs of 1-47 sevens of zero bits. What is left over at the
   end of a line is padding */
typedef struct { const char *const *lines; const size_t *lens; int nl, li; size_t ci; u32 acc; int nacc, zeros, bad; } NiBits;
static int ni_char(NiBits *b)
{
  while (b->li < b->nl) {
    if (b->ci < b->lens[b->li]) return (u8)b->lines[b->li][b->ci++];
    return -1;
  }
  return -2;
}
static int ni_bits(NiBits *b, int k)                  /* k <= 8; -1 when the line runs out */
{
  while (b->nacc < k) {
    if (b->zeros) { b->zeros--; b->acc <<= 7; b->nacc += 7; continue; }
    {
      int c = ni_char(b);
      if (c < 0) return -1;
      if (c >= 0x20 && c <= 0x6F) { b->acc = (b->acc << 7) | (u32)(c - 0x20); b->nacc += 7; }
      else if (c >= 0xA1 && c <= 0xD0) { b->acc = (b->acc << 7) | (u32)(c - 0x51); b->nacc += 7; }
      else if (c >= 0xD1) b->zeros = c - 0xD0;
      else { b->bad = 1; return -1; }
    }
  }
  b->nacc -= k;
  return (int)((b->acc >> b->nacc) & ((1u << k) - 1));
}
static void ni_newline(NiBits *b) { b->li++; b->ci = 0; b->nacc = 0; b->zeros = 0; b->acc = 0; }

static const char *newicon_lines(const u8 *tt, size_t ttlen, int ntt, int which, const char **lines, size_t *lens, Rgba *out);
static const char *newicon(const u8 *tt, size_t ttlen, int ntt, int which, Rgba *out)
{
  const char *msg;
  const char **lines;
  size_t *lens;
  /* the tooltype strings (each a length, then the bytes with a NUL) were found by the caller */
  if (ntt > 4096) ntt = 4096;
  lines = (const char **)malloc(sizeof(char *) * (size_t)(ntt + 1));
  lens = (size_t *)malloc(sizeof(size_t) * (size_t)(ntt + 1));
  if (!lines || !lens) { free(lines); free(lens); return oom(); }
  msg = newicon_lines(tt, ttlen, ntt, which, lines, lens, out);
  free(lines); free(lens);
  return msg;
}
static const char *newicon_lines(const u8 *tt, size_t ttlen, int ntt, int which, const char **lines, size_t *lens, Rgba *out)
{
  int nl = 0, i, ncol, bpp, transp;
  i32 w, h, x, y;
  size_t o = 0;
  const char *tag = which ? "IM2=" : "IM1=";
  NiBits b;
  u8 *pal;
  for (i = 0; i < ntt && o + 4 <= ttlen; i++) {
    size_t l = be32(tt + o);
    if (l > ttlen - o - 4) break;
    if (l > 4 && !memcmp(tt + o + 4, tag, 4)) {
      size_t sl = l - 4;
      while (sl && tt[o + 8 + sl - 1] == 0) sl--;
      lines[nl] = (const char *)tt + o + 8; lens[nl] = sl; nl++;
    }
    o += 4 + l;
  }
  if (!nl) return NULL;                              /* not a NewIcon */
  if (lens[0] < 5) return "Damaged NewIcons image";
  transp = lines[0][0] == 'B';
  w = (u8)lines[0][1] - 0x21; h = (u8)lines[0][2] - 0x21;
  ncol = ((u8)lines[0][3] - 0x21) * 64 + ((u8)lines[0][4] - 0x21);
  if (!fits(w, h) || ncol < 1 || ncol > 256) return "Damaged NewIcons image";
  for (bpp = 1; (1 << bpp) < ncol; bpp++) ;
  memset(&b, 0, sizeof b);
  b.lines = lines; b.lens = lens; b.nl = nl; b.ci = 5;
  if (!(pal = (u8 *)calloc(256 * 3, 1))) return oom();
  for (i = 0; i < ncol * 3; i++) {
    int v;
    while ((v = ni_bits(&b, 8)) < 0) { if (b.bad || b.li + 1 >= nl) { free(pal); return "Damaged NewIcons image"; } ni_newline(&b); }
    pal[i] = (u8)v;
  }
  ni_newline(&b);
  if (!rgba_alloc(out, w, h)) { free(pal); return "Out of memory"; }
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      int v;
      u8 *q = out->px + ((size_t)y * w + x) * 4;
      while ((v = ni_bits(&b, bpp)) < 0) {
        if (b.bad || b.li + 1 >= nl) { free(pal); rgba_free(out); return "Truncated NewIcons image"; }
        ni_newline(&b);
      }
      if (v >= ncol) v = 0;
      q[0] = pal[v * 3]; q[1] = pal[v * 3 + 1]; q[2] = pal[v * 3 + 2]; q[3] = transp && !v ? 0 : 255;
    }
  free(pal);
  return "";                                         /* done (an empty message is success here) */
}

/* OS 3.5 GlowIcons: FORM ICON with FACE (width - 1, height - 1, ...) and one IMAG per image:
   transparent colour, colours - 1, flags (1 transparency, 2 palette), image and palette
   compression (0 none, 1 run length), bits per pixel, image and palette sizes - 1, then the
   data. Run length works on a big-endian bit stream: an 8-bit code n, then n + 1 literal values
   (n < 128) or one value repeated 257 - n times (n > 128), values being depth bits wide (8 for
   the palette) */
static int glow_unpack(const u8 *s, size_t n, int comp, int depth, u8 *out, size_t count)
{
  size_t bit = 0, nbits = n * 8, o = 0;
  if (comp == 0) { if (n < count) return 0; memcpy(out, s, count); return 1; }
#define GBITS(k, v) do { int _k = (k); (v) = 0; if (nbits - bit < (size_t)_k) return 0; while (_k--) { (v) = ((v) << 1) | ((s[bit >> 3] >> (7 - (bit & 7))) & 1); bit++; } } while (0)
  while (o < count) {
    int code, v, k;
    if (nbits - bit < 8) return 0;
    GBITS(8, code);
    if (code < 128) { for (k = 0; k <= code && o < count; k++) { GBITS(depth, v); out[o++] = (u8)v; } }
    else if (code > 128) { GBITS(depth, v); for (k = 0; k < 257 - code && o < count; k++) out[o++] = (u8)v; }
  }
#undef GBITS
  return 1;
}
static const char *glowicon(const u8 *d, size_t n, int which, Rgba *out)
{
  IffWalk w;
  const u8 *id, *c;
  size_t len;
  i32 gw = 0, gh = 0, x, y;
  int k = 0, haveface = 0, ncolpal = 0;
  u8 *pal = NULL, *idx = NULL;
  const char *msg = "The GlowIcon has no image";
  iff_begin(&w, d, n);
  if (!(pal = (u8 *)calloc(256 * 3, 1))) return oom();
  while (iff_next(&w, &id, &c, &len)) {
    if (!memcmp(id, "FACE", 4) && len >= 6) { gw = c[0] + 1; gh = c[1] + 1; haveface = 1; }
    else if (!memcmp(id, "IMAG", 4) && len >= 10 && haveface) {
      int tcol = c[0], ncol = c[1] + 1, flags = c[2], icomp = c[3], pcomp = c[4], depth = c[5];
      size_t isz = be16(c + 6) + 1, psz = be16(c + 8) + 1;
      if (depth < 1 || depth > 8 || icomp > 1 || pcomp > 1 || isz > len - 10) { msg = "Damaged GlowIcon"; break; }
      if (flags & 2) {
        if (psz > len - 10 - isz || !glow_unpack(c + 10 + isz, psz, pcomp, 8, pal, (size_t)ncol * 3)) { msg = "Damaged GlowIcon palette"; break; }
        ncolpal = ncol;
      }
      if (k++ != which) continue;
      if (!ncolpal) { msg = "The GlowIcon has no palette"; break; }
      if (!(idx = (u8 *)malloc((size_t)gw * gh))) { msg = oom(); break; }
      if (!glow_unpack(c + 10, isz, icomp, depth, idx, (size_t)gw * gh)) { msg = "Damaged GlowIcon image"; break; }
      if (!rgba_alloc(out, gw, gh)) { msg = "Out of memory"; break; }
      for (y = 0; y < gh; y++)
        for (x = 0; x < gw; x++) {
          int v = idx[(size_t)y * gw + x];
          u8 *q = out->px + ((size_t)y * gw + x) * 4;
          q[0] = pal[v * 3]; q[1] = pal[v * 3 + 1]; q[2] = pal[v * 3 + 2]; q[3] = (flags & 1) && v == tcol ? 0 : 255;
        }
      msg = NULL;
      break;
    }
  }
  free(pal); free(idx);
  return msg;
}

int info_read_ex(const u8 *d, size_t n, int which, int palchoice, int clear0, Rgba *out, char *err, size_t errcap)
{
  static const u8 pngsig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
  Cur c;
  IconImage im[2];
  u32 render, select, deftool, tooltypes, drawer, toolwin, ud;
  const u8 *tt = NULL;
  size_t ttlen = 0, o;
  int ntt = 0, rev;
  const char *msg;
  out->w = out->h = 0; out->px = NULL;
  if (n >= 8 && !memcmp(d, pngsig, 8)) return png_read(d, n, out, err, errcap);
  if (n < 78 || be16(d) != 0xE310 || be16(d + 2) != 1) return fail(out, err, errcap, "Not a Workbench icon");
  render = be32(d + 22); select = be32(d + 26); ud = be32(d + 44); rev = (int)(ud & 255);
  deftool = be32(d + 50); tooltypes = be32(d + 54); drawer = be32(d + 66); toolwin = be32(d + 70);
  c.d = d; c.n = n; c.o = 78; c.bad = 0;
  memset(im, 0, sizeof im);
  if (drawer) cur_skip(&c, 56);
  if (render) read_image(&c, &im[0]);
  if (select) read_image(&c, &im[1]);
  if (deftool) { u32 l = cur32(&c); cur_skip(&c, l); }
  if (tooltypes && !c.bad) {
    u32 cnt = cur32(&c), i;
    ntt = cnt >= 4 ? (int)(cnt / 4 - 1) : 0;
    tt = d + c.o;
    for (i = 0; i < (u32)ntt && !c.bad; i++) { u32 l = cur32(&c); cur_skip(&c, l); }
    ttlen = c.bad ? n - (size_t)(tt - d) : (size_t)(d + c.o - tt);
  }
  if (toolwin) { u32 l = cur32(&c); cur_skip(&c, l); }
  if (drawer && rev == 1) cur_skip(&c, 6);
  /* a GlowIcon: FORM ICON where the old parts end, or anywhere after the header */
  o = c.bad ? n : c.o;
  if (!(o + 12 <= n && is_form(d + o, n - o, "ICON")))
    for (o = 78; o + 12 <= n; o++) if (is_form(d + o, n - o, "ICON")) break;
  if (o + 12 <= n) {
    if (!(msg = glowicon(d + o, n - o, which, out))) return 1;
    rgba_free(out);
  }
  if (tt && (msg = newicon(tt, ttlen, ntt, which, out))) {
    if (!*msg) return 1;
    return fail(out, err, errcap, msg);
  }
  if ((msg = classic_icon(&im[which && im[1].data ? 1 : 0], rev, palchoice, clear0, out))) return fail(out, err, errcap, msg);
  return 1;
}
int info_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap) { return info_read_ex(d, n, 0, INFO_PAL_AUTO, 0, out, err, errcap); }

/* ================================================================== Sun raster */
int sunras_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  i32 w, h, x, y;
  int depth, type, maptype, i;
  u32 maplen;
  size_t rowbytes, size, o;
  u8 *buf = NULL, pal[256 * 3];
  int ncol = 0;
  out->w = out->h = 0; out->px = NULL;
  if (n < 32 || be32(d) != 0x59A66A95UL) return fail(out, err, errcap, "Not a Sun raster file");
  w = (i32)be32(d + 4); h = (i32)be32(d + 8); depth = (int)be32(d + 12); type = (int)be32(d + 20);
  maptype = (int)be32(d + 24); maplen = be32(d + 28);
  if (be32(d + 4) > MAXDIM || be32(d + 8) > MAXDIM || !fits(w, h)) return fail(out, err, errcap, "Bad Sun raster picture size");
  if (depth != 1 && depth != 8 && depth != 24 && depth != 32) return fail(out, err, errcap, "Unsupported Sun raster depth");
  if (type > 3 || maptype > 1) return fail(out, err, errcap, "Unsupported Sun raster type");
  if (maplen > n - 32) return fail(out, err, errcap, "Truncated Sun raster file");
  if (maptype == 1) {
    ncol = (int)(maplen / 3 > 256 ? 256 : maplen / 3);
    for (i = 0; i < ncol; i++) { pal[i * 3] = d[32 + i]; pal[i * 3 + 1] = d[32 + maplen / 3 + i]; pal[i * 3 + 2] = d[32 + 2 * (maplen / 3) + i]; }
  }
  o = 32 + maplen;
  rowbytes = ((size_t)w * depth + 15) / 16 * 2;
  size = rowbytes * (size_t)h;
  if (!(buf = (u8 *)malloc(size))) return fail(out, err, errcap, oom());
  if (type == 2) {                                   /* 0x80 escapes: 0x80 0 is 0x80, 0x80 n v is n + 1 v's */
    size_t i2 = o, k = 0;
    while (k < size) {
      if (i2 >= n) break;
      if (d[i2] != 0x80) buf[k++] = d[i2++];
      else {
        size_t cnt;
        if (n - i2 < 2) break;
        if (d[i2 + 1] == 0) { buf[k++] = 0x80; i2 += 2; continue; }
        if (n - i2 < 3) break;
        for (cnt = (size_t)d[i2 + 1] + 1; cnt && k < size; cnt--) buf[k++] = d[i2 + 2];
        i2 += 3;
      }
    }
    if (k < size) { free(buf); return fail(out, err, errcap, "Truncated Sun raster file"); }
  } else {
    if (n - o < size) { free(buf); return fail(out, err, errcap, "Truncated Sun raster file"); }
    memcpy(buf, d + o, size);
  }
  if (!rgba_alloc(out, w, h)) { free(buf); return fail(out, err, errcap, "Out of memory"); }
  for (y = 0; y < h && !progress(y, h); y++) {
    const u8 *r = buf + (size_t)y * rowbytes;
    for (x = 0; x < w; x++) {
      u8 *q = out->px + ((size_t)y * w + x) * 4;
      q[3] = 255;
      if (depth == 1 || depth == 8) {
        int v = depth == 1 ? (r[x >> 3] >> (7 - (x & 7))) & 1 : r[x];
        if (ncol) { if (v >= ncol) v = 0; q[0] = pal[v * 3]; q[1] = pal[v * 3 + 1]; q[2] = pal[v * 3 + 2]; }
        else if (depth == 1) q[0] = q[1] = q[2] = v ? 0 : 255;  /* without a map, 1 is black */
        else q[0] = q[1] = q[2] = (u8)v;
      } else {
        const u8 *s = r + (size_t)x * (depth / 8) + (depth == 32);   /* 32 bits: a pad byte first */
        if (type == 3) { q[0] = s[0]; q[1] = s[1]; q[2] = s[2]; }
        else { q[0] = s[2]; q[1] = s[1]; q[2] = s[0]; }
      }
    }
  }
  free(buf);
  return 1;
}

/* ================================================================== SGI */
int sgi_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  int rle, bpc, zs, ch;
  i32 w, h, x, y;
  u8 *row = NULL;
  out->w = out->h = 0; out->px = NULL;
  if (n < 512 || be16(d) != 474) return fail(out, err, errcap, "Not an SGI image");
  rle = d[2]; bpc = d[3];
  w = (i32)be16(d + 6); h = (i32)be16(d + 8); zs = (int)be16(d + 10);
  if (be16(d + 4) < 3) zs = 1;
  if (be16(d + 4) < 2) h = 1;
  if (rle > 1 || (bpc != 1 && bpc != 2) || zs < 1) return fail(out, err, errcap, "Unsupported SGI image");
  if (be32(d + 104) != 0) return fail(out, err, errcap, "Unsupported SGI colour map image");
  if (!fits(w, h)) return fail(out, err, errcap, "Bad SGI picture size");
  if (zs > 4) zs = 4;                                /* channels past RGBA are ignored */
  if (!(row = (u8 *)malloc((size_t)w))) return fail(out, err, errcap, oom());
  if (!rgba_alloc(out, w, h)) { free(row); return fail(out, err, errcap, "Out of memory"); }
  for (ch = 0; ch < zs; ch++)
    for (y = 0; y < h && !progress(y, h); y++) {
      size_t ri = (size_t)ch * h + y;
      if (!rle) {
        size_t o = 512 + ri * (size_t)w * bpc;
        if (o > n || n - o < (size_t)w * bpc) { free(row); return fail(out, err, errcap, "Truncated SGI image"); }
        for (x = 0; x < w; x++) row[x] = d[o + (size_t)x * bpc];
      } else {
        size_t tab = 512 + ri * 4, o, k = 0;
        if ((size_t)zs * h * 4 > n - 512) { free(row); return fail(out, err, errcap, "Truncated SGI image"); }
        o = be32(d + tab);
        while (k < (size_t)w) {
          u32 c, cnt;
          if (o > n || n - o < (size_t)bpc) break;
          c = bpc == 2 ? be16(d + o) : d[o]; o += bpc;
          cnt = c & 0x7F;
          if (!cnt) break;
          if (c & 0x80) {
            for (; cnt && k < (size_t)w; cnt--, o += bpc) { if (n - o < (size_t)bpc) { cnt = 1; break; } row[k++] = d[o]; }
            if (cnt) break;
          } else {
            u8 v;
            if (n - o < (size_t)bpc) break;
            v = d[o]; o += bpc;
            for (; cnt && k < (size_t)w; cnt--) row[k++] = v;
          }
        }
        if (k < (size_t)w) { free(row); return fail(out, err, errcap, "Damaged SGI image"); }
      }
      {
        u8 *q = out->px + (size_t)(h - 1 - y) * w * 4;   /* rows run bottom up */
        for (x = 0; x < w; x++, q += 4) {
          if (zs <= 2) { if (ch == 0) q[0] = q[1] = q[2] = row[x]; else q[3] = row[x]; }
          else q[ch] = row[x];
          if (ch == 0 && (zs == 1 || zs == 3)) q[3] = 255;
        }
      }
    }
  free(row);
  return 1;
}

/* ================================================================== Atari ST */
/* the 16-entry palette of 0RGB words: STE values (4 bits, the low bit stored at the top) if any
   uses that bit, else the ST's 3 bits per channel scaled to 0-255 */
static void st_palette(const u8 *p, u8 *pal)
{
  int i, k, ste = 0;
  for (i = 0; i < 16; i++) if (be16(p + i * 2) & 0x888) ste = 1;
  for (i = 0; i < 16; i++) {
    u32 wd = be16(p + i * 2);
    for (k = 0; k < 3; k++) {
      int v = (int)(wd >> (8 - k * 4)) & 15;
      pal[i * 3 + k] = ste ? (u8)((((v & 7) << 1) | (v >> 3)) * 17) : (u8)(((v & 7) * 255 + 3) / 7);
    }
  }
}
/* the screen (interleaved bitplane words: 4, 2 or 1 planes for resolution 0, 1, 2) as RGBA;
   planar_lines gives instead each line's planes one after another (Degas Elite compressed) */
static const char *st_screen(const u8 *scr, int res, const u8 *rawpal, int planar_lines, Rgba *out)
{
  int np = res == 0 ? 4 : res == 1 ? 2 : 1, p;
  i32 w = res == 0 ? 320 : 640, h = res == 2 ? 400 : 200, x, y;
  size_t bpl = (size_t)w / 8, line = bpl * np;
  u8 pal[16 * 3];
  /* monochrome: 1 is black on white, unless bit 0 of colour 0 is clear (then the other way) */
  int inv = !(be16(rawpal) & 1);
  st_palette(rawpal, pal);
  if (!rgba_alloc(out, w, h)) return "Out of memory";
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      int v = 0;
      u8 *q = out->px + ((size_t)y * w + x) * 4;
      for (p = 0; p < np; p++) {
        const u8 *b = planar_lines ? scr + y * line + p * bpl + (x >> 3) : scr + y * line + (size_t)(x >> 4) * np * 2 + p * 2 + ((x >> 3) & 1);
        v |= ((*b >> (7 - (x & 7))) & 1) << p;
      }
      if (res == 2) q[0] = q[1] = q[2] = (v ^ inv) ? 0 : 255;
      else { q[0] = pal[v * 3]; q[1] = pal[v * 3 + 1]; q[2] = pal[v * 3 + 2]; }
      q[3] = 255;
    }
  return NULL;
}

int degas_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  int res, comp;
  const char *msg;
  out->w = out->h = 0; out->px = NULL;
  if (n < 34) return fail(out, err, errcap, "Not a Degas picture");
  res = (int)(be16(d) & 3); comp = (be16(d) & 0x8000) != 0;
  if ((be16(d) & 0x7FFC) || res > 2) return fail(out, err, errcap, "Not a Degas picture");
  if (!comp) {
    if (n < 32034) return fail(out, err, errcap, "Truncated Degas picture");
    msg = st_screen(d + 34, res, d + 2, 0, out);
  } else {
    u8 *scr = (u8 *)malloc(32000);
    if (!scr) return fail(out, err, errcap, oom());
    msg = unpack1(d + 34, n - 34, scr, 32000) ? st_screen(scr, res, d + 2, 1, out) : "Truncated Degas picture";
    free(scr);
  }
  if (msg) return fail(out, err, errcap, msg);
  return 1;
}

int neo_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  const char *msg;
  out->w = out->h = 0; out->px = NULL;
  if (n < 32128 || be16(d) != 0 || be16(d + 2) > 2) return fail(out, err, errcap, "Not a NEOchrome picture");
  if ((msg = st_screen(d + 128, (int)be16(d + 2), d + 4, 0, out))) return fail(out, err, errcap, msg);
  return 1;
}

/* ================================================================== detection and dispatch */
int amigafmt_detect(const u8 *d, size_t n)
{
  if (n >= 16 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4)) return AMF_WEBP;
  if (n >= 12 && !memcmp(d, "FORM", 4)) {
    if (!memcmp(d + 8, "ACBM", 4)) return AMF_ACBM;
    if (!memcmp(d + 8, "RGBN", 4)) return AMF_RGBN;
    if (!memcmp(d + 8, "RGB8", 4)) return AMF_RGB8;
    if (!memcmp(d + 8, "DEEP", 4)) return AMF_DEEP;
    if (!memcmp(d + 8, "ANIM", 4)) return AMF_ANIM;
    if (!memcmp(d + 8, "ILBM", 4)) {
      IffWalk w;
      const u8 *id, *c;
      size_t len;
      iff_begin(&w, d, n);
      while (iff_next(&w, &id, &c, &len))
        if (!memcmp(id, "SHAM", 4) || !memcmp(id, "CTBL", 4) || !memcmp(id, "PCHG", 4)) return AMF_ILBM_LINEPAL;
    }
    return AMF_UNKNOWN;
  }
  if (n >= 78 && be16(d) == 0xE310 && be16(d + 2) == 1) return AMF_INFO;
  if (n >= 32 && be32(d) == 0x59A66A95UL) return AMF_SUNRAS;
  if (n >= 512 && be16(d) == 474 && d[2] <= 1 && (d[3] == 1 || d[3] == 2) && be16(d + 4) >= 1 && be16(d + 4) <= 3) return AMF_SGI;
  if (n == 32128 && be16(d) == 0 && be16(d + 2) <= 2) return AMF_NEO;
  if ((n == 32034 || n == 32066) && be16(d) <= 2) return AMF_DEGAS;
  if (n > 34 && n < 40000 && be16(d) >= 0x8000 && be16(d) <= 0x8002) return AMF_DEGAS;
  return AMF_UNKNOWN;
}

const char *amigafmt_name(int fmt)
{
  static const char *const names[] = { "unknown", "WebP", "IFF ACBM", "IFF RGBN", "IFF RGB8", "IFF DEEP", "IFF ILBM (line palettes)",
                                       "IFF ANIM", "Workbench icon", "Sun raster", "SGI image", "Degas", "NEOchrome" };
  return fmt >= 0 && fmt <= AMF_LAST ? names[fmt] : names[0];
}

int amigafmt_decode(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  out->w = out->h = 0; out->px = NULL;
  codec_oom = 0;
  switch (amigafmt_detect(d, n)) {
  case AMF_WEBP: return webp_read(d, n, out, err, errcap);
  case AMF_ACBM: return acbm_read(d, n, out, err, errcap);
  case AMF_RGBN: case AMF_RGB8: return rgbn_read(d, n, out, err, errcap);
  case AMF_DEEP: return deep_read(d, n, out, err, errcap);
  case AMF_ILBM_LINEPAL: return sham_read(d, n, out, err, errcap);
  case AMF_ANIM: return anim_read(d, n, out, err, errcap);
  case AMF_INFO: return info_read(d, n, out, err, errcap);
  case AMF_SUNRAS: return sunras_read(d, n, out, err, errcap);
  case AMF_SGI: return sgi_read(d, n, out, err, errcap);
  case AMF_DEGAS: return degas_read(d, n, out, err, errcap);
  case AMF_NEO: return neo_read(d, n, out, err, errcap);
  }
  codec_err(err, errcap, "Unknown file format");
  return 0;
}
