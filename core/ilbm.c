/*
 * ilbm.c - IFF ILBM, the Amiga's own picture format: reading bitplane pictures of 1-8 planes
 * (with Extra Half-Brite and Hold-And-Modify, HAM6 and HAM8), 24- and 32-bit "deep" ILBMs, and
 * Deluxe Paint's chunky PBM form; writing 24-bit deep ILBMs or 8-plane ones with a 256-colour
 * palette. Both use ByteRun1 compression. Also here: the median-cut colour quantizer (with optional
 * Floyd-Steinberg dithering) that makes the 8-plane palette.
 *
 * 0.4 adds the full writer (ilbm_write: 1-8 planes, EHB, HAM6, HAM8, SHAM, 24-bit deep, a mask plane
 * or transparent colour, CAMG, aspect and CRNG; an indexed document's palette and indices written
 * exactly) and ilbm_read_indexed, which keeps an ILBM's palette, indices and cycle ranges.
 *
 * IFF numbers are big-endian, as the 68k's are, but are still read a byte at a time so the code
 * runs the same anywhere. Chunks are padded to even lengths.
 */
#include <stdlib.h>
#include <string.h>
#include "codec.h"
#include "palette.h"

static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static u32 be16(const u8 *p) { return ((u32)p[0] << 8) | p[1]; }

#define CAMG_EHB 0x80
#define CAMG_HAM 0x800

/* ------------------------------------------------------------------ reading */
/* ByteRun1 from s[0..n) into out until it holds size bytes: n >= 0 copies the next n + 1 bytes,
   -127..-1 repeats the next byte 1 - n times, -128 is a no-op. A run may cross rows (some writers
   do that). 0 if the data runs out first */
static int unpack(const u8 *s, size_t n, u8 *out, size_t size)
{
  size_t i = 0, o = 0;
  while (o < size) {
    int c;
    size_t k;
    if (i >= n) return 0;
    c = s[i++];
    if (c < 128) {
      k = (size_t)c + 1;
      if (i + k > n) return 0;
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
  return 1;
}

/* walks the FORM's chunks (a chunk running past the end of the file is cut short, which the BODY
   decoding then notices), unpacks the whole BODY, then turns each row's planes into pixels */
int ilbm_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  size_t end, o, rowsize, rowbytes = 0, size;
  const u8 *body = NULL;
  size_t bodylen = 0;
  u8 pal[256][3], *buf = NULL;
  u32 camg = 0, *vals = NULL;
  int pbm, haveh = 0, ncmap = 0, np = 0, masking = 0, comp = 0, tcol = 0, ham = 0, ehb = 0, deep, nplanes, ok = 0, i;
  i32 w = 0, h = 0, x, y;
  const char *msg = "Damaged ILBM file";
  out->w = out->h = 0; out->px = NULL;
  if (n < 12 || memcmp(d, "FORM", 4) || (memcmp(d + 8, "ILBM", 4) && memcmp(d + 8, "PBM ", 4))) { codec_err(err, errcap, "Not an IFF ILBM file"); return 0; }
  pbm = !memcmp(d + 8, "PBM ", 4);
  end = be32(d + 4);
  end = end > n - 8 ? n : end + 8;
  for (o = 12; o + 8 <= end; ) {
    size_t len = be32(d + o + 4), avail = end - o - 8;
    const u8 *c = d + o + 8;
    if (len > avail) len = avail;
    if (!memcmp(d + o, "BMHD", 4) && len >= 20) {
      w = (i32)be16(c); h = (i32)be16(c + 2); np = c[8]; masking = c[9]; comp = c[10]; tcol = (int)be16(c + 12);
      haveh = 1;
    } else if (!memcmp(d + o, "CMAP", 4)) {
      ncmap = (int)(len / 3 > 256 ? 256 : len / 3);
      for (i = 0; i < ncmap; i++) { pal[i][0] = c[i * 3]; pal[i][1] = c[i * 3 + 1]; pal[i][2] = c[i * 3 + 2]; }
    } else if (!memcmp(d + o, "CAMG", 4) && len >= 4) camg = be32(c);
    else if (!memcmp(d + o, "BODY", 4)) { body = c; bodylen = len; break; }
    o += 8 + len + (len & 1);
  }
  if (!haveh) { codec_err(err, errcap, "The ILBM file has no BMHD header"); return 0; }
  if (!body) { codec_err(err, errcap, "The ILBM file has no BODY"); return 0; }
  if (w < 1 || h < 1 || w > MAXDIM || h > MAXDIM || (long)w * h > CODEC_MAXPIXELS) { codec_err(err, errcap, "Bad ILBM picture size"); return 0; }
  deep = !pbm && (np == 24 || np == 32);
  if (!deep && (np < 1 || np > 8)) { codec_err(err, errcap, "Unsupported number of ILBM bitplanes"); return 0; }
  if (comp > 1) { codec_err(err, errcap, "Unsupported ILBM compression"); return 0; }
  ham = !pbm && !deep && (camg & CAMG_HAM) && (np == 6 || np == 8);
  ehb = !pbm && !ham && np == 6 && ((camg & CAMG_EHB) || (ncmap > 0 && ncmap <= 32));
  /* without a CMAP: a grey ramp */
  for (i = ncmap; i < 256; i++) pal[i][0] = pal[i][1] = pal[i][2] = 0;
  if (!ncmap && !deep) for (i = 0; i < (1 << np) && i < 256; i++) pal[i][0] = pal[i][1] = pal[i][2] = (u8)(i * 255 / ((1 << np) - 1));
  nplanes = np + (masking == 1 && !pbm);                 /* the mask plane follows the colour planes */
  if (pbm) rowsize = (size_t)w + (w & 1);
  else { rowbytes = (size_t)((w + 15) >> 4) << 1; rowsize = rowbytes * nplanes; }
  size = rowsize * (size_t)h;
  /* ByteRun1 makes at most 128 bytes from 2, so a short BODY can't claim a huge picture */
  if ((comp == 0 && bodylen < size) || (comp == 1 && bodylen < size / 64)) { codec_err(err, errcap, "Truncated ILBM file"); return 0; }
  if (!(buf = (u8 *)malloc(size)) || !(vals = (u32 *)malloc(sizeof(u32) * (size_t)w))) { msg = "Out of memory"; goto done; }
  if (comp == 0) memcpy(buf, body, size);
  else if (!unpack(body, bodylen, buf, size)) { msg = "Truncated ILBM file"; goto done; }
  if (!rgba_alloc(out, w, h)) { msg = "Out of memory"; goto done; }
  for (y = 0; y < h; y++) {
    const u8 *row = buf + (size_t)y * rowsize;
    u8 *px = out->px + (size_t)y * w * 4, cr, cg, cb;
    int p;
    if (pbm) for (x = 0; x < w; x++) vals[x] = row[x];
    else {
      memset(vals, 0, sizeof(u32) * (size_t)w);
      for (p = 0; p < np; p++) {
        const u8 *pl = row + (size_t)p * rowbytes;
        for (x = 0; x < w; x++) vals[x] |= (u32)((pl[x >> 3] >> (7 - (x & 7))) & 1) << p;
      }
    }
    cr = pal[0][0]; cg = pal[0][1]; cb = pal[0][2];      /* HAM starts each row from colour 0 */
    for (x = 0; x < w; x++, px += 4) {
      u32 v = vals[x];
      px[3] = 255;
      if (deep) {
        px[0] = (u8)v; px[1] = (u8)(v >> 8); px[2] = (u8)(v >> 16);
        if (np == 32) px[3] = (u8)(v >> 24);
        continue;
      }
      if (ham) {
        int hb = np - 2, ctl = (int)(v >> hb);
        u32 dv = v & ((1u << hb) - 1), m = dv << (8 - hb);
        m |= m >> hb;                                    /* HAM6 4 bits -> x17; HAM8 6 bits -> top bits repeated */
        if (ctl == 0) { cr = pal[dv][0]; cg = pal[dv][1]; cb = pal[dv][2]; }
        else if (ctl == 1) cb = (u8)m;
        else if (ctl == 2) cr = (u8)m;
        else cg = (u8)m;
        px[0] = cr; px[1] = cg; px[2] = cb;
        continue;
      }
      if (ehb && v >= 32) { px[0] = (u8)(pal[v - 32][0] >> 1); px[1] = (u8)(pal[v - 32][1] >> 1); px[2] = (u8)(pal[v - 32][2] >> 1); }
      else { px[0] = pal[v & 255][0]; px[1] = pal[v & 255][1]; px[2] = pal[v & 255][2]; }
      if (masking == 2 && (int)v == tcol) px[3] = 0;
    }
  }
  ok = 1;
done:
  if (!ok) { rgba_free(out); codec_err(err, errcap, msg); }
  free(buf); free(vals);
  return ok;
}

/* ------------------------------------------------------------------ quantizing */
/* pixel i's RGB (the picture is taken as opaque: callers flatten it first) */
#define RGB_OF(im, i) (((u32)(im)->px[(i) * 4] << 16) | ((u32)(im)->px[(i) * 4 + 1] << 8) | (im)->px[(i) * 4 + 2])

/* the exact palette, if the picture has maxc colours or fewer: an open-addressed hash of the
   colours met so far. Returns the count, or 0 if there are more (or no memory) */
static int exact_colours(const Rgba *im, int maxc, u8 *pal, u8 *idx)
{
  enum { HS = 4096 };
  u32 *key = (u32 *)malloc(sizeof(u32) * HS);
  short *ent = (short *)malloc(sizeof(short) * HS);
  long i, np = (long)im->w * im->h;
  int nc = 0;
  if (!key || !ent) { free(key); free(ent); return 0; }
  for (i = 0; i < HS; i++) ent[i] = -1;
  for (i = 0; i < np; i++) {
    u32 c = RGB_OF(im, i), hsh = (c * 2654435761UL) >> 20;
    while (ent[hsh & (HS - 1)] >= 0 && key[hsh & (HS - 1)] != c) hsh++;
    hsh &= HS - 1;
    if (ent[hsh] < 0) {
      if (nc == maxc) { nc = 0; break; }
      key[hsh] = c; ent[hsh] = (short)nc;
      pal[nc * 3] = (u8)(c >> 16); pal[nc * 3 + 1] = (u8)(c >> 8); pal[nc * 3 + 2] = (u8)c;
      nc++;
    }
    if (idx) idx[i] = (u8)ent[hsh];
  }
  free(key); free(ent);
  return nc;
}

/* a box of the 32 x 32 x 32 colour cube (5 bits a channel) for median cut */
typedef struct { int lo[3], hi[3]; unsigned long count; } QBox;
#define HIDX(r, g, b) (((r) << 10) | ((g) << 5) | (b))
/* shrink a box to the bins it really uses, and count its pixels */
static void qbox_fit(QBox *b, const u32 *hist)
{
  int lo[3] = { 31, 31, 31 }, hi[3] = { 0, 0, 0 }, r, g, bl;
  b->count = 0;
  for (r = b->lo[0]; r <= b->hi[0]; r++)
    for (g = b->lo[1]; g <= b->hi[1]; g++)
      for (bl = b->lo[2]; bl <= b->hi[2]; bl++) {
        u32 c = hist[HIDX(r, g, bl)];
        if (!c) continue;
        b->count += c;
        if (r < lo[0]) lo[0] = r;
        if (r > hi[0]) hi[0] = r;
        if (g < lo[1]) lo[1] = g;
        if (g > hi[1]) hi[1] = g;
        if (bl < lo[2]) lo[2] = bl;
        if (bl > hi[2]) hi[2] = bl;
      }
  if (b->count) { memcpy(b->lo, lo, sizeof lo); memcpy(b->hi, hi, sizeof hi); }
}
/* the nearest palette entry to a colour (squared distance) */
static int nearest(const u8 *pal, int nc, int r, int g, int b)
{
  int i, best = 0;
  long bd = 0x7FFFFFFFL;
  for (i = 0; i < nc; i++) {
    long dr = r - pal[i * 3], dg = g - pal[i * 3 + 1], db = b - pal[i * 3 + 2], dd = dr * dr * 3 + dg * dg * 4 + db * db * 2;
    if (dd < bd) { bd = dd; best = i; }
  }
  return best;
}

/* exact if it can be; otherwise median cut on a 5-bit histogram: the box with the most pixels
   times its longest side is split at the median of that side, until there are maxc boxes. Each
   palette entry is the true mean of its box's pixels. Pixels are mapped through a cache of the
   nearest entry per histogram bin, with Floyd-Steinberg error diffusion if asked */
int quantize(const Rgba *im, int maxc, int dither, u8 *pal, u8 *idx)
{
  u32 *hist = NULL;
  QBox *box = NULL;
  short *cache = NULL;
  unsigned long *sum = NULL;
  int *err = NULL;
  long i, np = (long)im->w * im->h;
  int nb = 1, nc = 0, k;
  if (maxc < 1) return 0;
  if (maxc > 256) maxc = 256;
  if ((nc = exact_colours(im, maxc, pal, idx)) > 0) return nc;
  hist = (u32 *)calloc(32768, sizeof(u32));
  box = (QBox *)malloc(sizeof(QBox) * (size_t)maxc);
  cache = (short *)malloc(sizeof(short) * 32768);
  sum = (unsigned long *)calloc((size_t)maxc * 4, sizeof(unsigned long));
  if (!hist || !box || !cache || !sum) goto done;
  for (i = 0; i < np; i++) { const u8 *p = im->px + i * 4; hist[HIDX(p[0] >> 3, p[1] >> 3, p[2] >> 3)]++; }
  box[0].lo[0] = box[0].lo[1] = box[0].lo[2] = 0; box[0].hi[0] = box[0].hi[1] = box[0].hi[2] = 31;
  qbox_fit(&box[0], hist);
  while (nb < maxc) {
    int best = -1, ax = 0, a, cut, v;
    unsigned long bestscore = 0, acc, half;
    QBox *b, *nbx;
    for (k = 0; k < nb; k++) {
      int side = 0, s;
      for (a = 0; a < 3; a++) { s = box[k].hi[a] - box[k].lo[a]; if (s > side) side = s; }
      if (side > 0 && box[k].count * (unsigned long)side > bestscore) { bestscore = box[k].count * (unsigned long)side; best = k; }
    }
    if (best < 0) break;                                 /* every box is a single bin */
    b = &box[best];
    for (a = 0; a < 3; a++) if (b->hi[a] - b->lo[a] > b->hi[ax] - b->lo[ax]) ax = a;
    /* the median along ax: the slice where half the box's pixels have been passed */
    half = b->count / 2; acc = 0; cut = b->lo[ax];
    for (v = b->lo[ax]; v < b->hi[ax]; v++) {
      int r0 = ax == 0 ? v : b->lo[0], r1 = ax == 0 ? v : b->hi[0], g0 = ax == 1 ? v : b->lo[1], g1 = ax == 1 ? v : b->hi[1];
      int b0 = ax == 2 ? v : b->lo[2], b1 = ax == 2 ? v : b->hi[2], r, g, bl;
      for (r = r0; r <= r1; r++) for (g = g0; g <= g1; g++) for (bl = b0; bl <= b1; bl++) acc += hist[HIDX(r, g, bl)];
      cut = v;
      if (acc >= half) break;
    }
    nbx = &box[nb++];
    *nbx = *b;
    b->hi[ax] = cut; nbx->lo[ax] = cut + 1;
    qbox_fit(b, hist); qbox_fit(nbx, hist);
  }
  /* each bin's box, through the cache array for now; then the true means */
  for (k = 0; k < nb; k++) {
    int r, g, bl;
    for (r = box[k].lo[0]; r <= box[k].hi[0]; r++)
      for (g = box[k].lo[1]; g <= box[k].hi[1]; g++)
        for (bl = box[k].lo[2]; bl <= box[k].hi[2]; bl++) cache[HIDX(r, g, bl)] = (short)k;
  }
  for (i = 0; i < np; i++) {
    const u8 *p = im->px + i * 4;
    unsigned long *s = sum + cache[HIDX(p[0] >> 3, p[1] >> 3, p[2] >> 3)] * 4;
    s[0] += p[0]; s[1] += p[1]; s[2] += p[2]; s[3]++;
  }
  for (k = 0; k < nb; k++) {
    unsigned long c = sum[k * 4 + 3] ? sum[k * 4 + 3] : 1;
    pal[k * 3] = (u8)((sum[k * 4] + c / 2) / c); pal[k * 3 + 1] = (u8)((sum[k * 4 + 1] + c / 2) / c); pal[k * 3 + 2] = (u8)((sum[k * 4 + 2] + c / 2) / c);
  }
  nc = nb;
  if (!idx) goto done;
  for (i = 0; i < 32768; i++) cache[i] = -1;
  if (!dither) {
    for (i = 0; i < np; i++) {
      const u8 *p = im->px + i * 4;
      int hb = HIDX(p[0] >> 3, p[1] >> 3, p[2] >> 3);
      if (cache[hb] < 0) cache[hb] = (short)nearest(pal, nc, (p[0] & 0xF8) | 4, (p[1] & 0xF8) | 4, (p[2] & 0xF8) | 4);
      idx[i] = (u8)cache[hb];
    }
  } else {
    /* errors (in 16ths) for this row and the next, with a pixel of margin each side */
    i32 x, y, w = im->w;
    int *cur, *nxt;
    if (!(err = (int *)calloc((size_t)(w + 2) * 6, sizeof(int)))) { nc = 0; goto done; }
    cur = err; nxt = err + (w + 2) * 3;
    for (y = 0; y < im->h; y++) {
      memset(nxt, 0, sizeof(int) * (size_t)(w + 2) * 3);
      for (x = 0; x < w; x++) {
        const u8 *p = im->px + ((long)y * w + x) * 4;
        int c[3], ch, e, hb, q;
        for (ch = 0; ch < 3; ch++) {
          c[ch] = p[ch] + (cur[(x + 1) * 3 + ch] + 8) / 16;
          c[ch] = c[ch] < 0 ? 0 : c[ch] > 255 ? 255 : c[ch];
        }
        hb = HIDX(c[0] >> 3, c[1] >> 3, c[2] >> 3);
        if (cache[hb] < 0) cache[hb] = (short)nearest(pal, nc, (c[0] & 0xF8) | 4, (c[1] & 0xF8) | 4, (c[2] & 0xF8) | 4);
        q = cache[hb];
        idx[(long)y * w + x] = (u8)q;
        for (ch = 0; ch < 3; ch++) {
          e = c[ch] - pal[q * 3 + ch];
          cur[(x + 2) * 3 + ch] += e * 7;
          nxt[x * 3 + ch] += e * 3;
          nxt[(x + 1) * 3 + ch] += e * 5;
          nxt[(x + 2) * 3 + ch] += e;
        }
      }
      { int *t = cur; cur = nxt; nxt = t; }
    }
  }
done:
  free(hist); free(box); free(cache); free(sum); free(err);
  return nc;
}

/* ------------------------------------------------------------------ writing */
/* ByteRun1-pack one row of one plane: runs of 3 or more equal bytes repeated, the rest literal */
static void pack(Buf *b, const u8 *s, size_t n)
{
  size_t i = 0;
  while (i < n) {
    size_t j = i;
    while (j + 1 < n && s[j + 1] == s[i] && j - i < 127) j++;
    if (j - i + 1 >= 3) {
      buf_addc(b, (int)(257 - (j - i + 1)));
      buf_addc(b, s[i]);
      i = j + 1;
    } else {
      size_t st = i;
      while (i < n && i - st < 128 && !(i + 2 < n && s[i] == s[i + 1] && s[i] == s[i + 2])) i++;
      buf_addc(b, (int)(i - st - 1));
      buf_add(b, s + st, i - st);
    }
  }
}
static void put32(Buf *b, u32 v) { buf_addc(b, (int)(v >> 24)); buf_addc(b, (int)((v >> 16) & 255)); buf_addc(b, (int)((v >> 8) & 255)); buf_addc(b, (int)(v & 255)); }
static void put16(Buf *b, u32 v) { buf_addc(b, (int)((v >> 8) & 255)); buf_addc(b, (int)(v & 255)); }

/* the picture over white; 24 planes (red bits 0-7, green, blue) or 8 planes of palette indexes,
   each plane's row packed separately as the format expects */
u8 *ilbm_encode(const Rgba *im, int indexed256, int dither, size_t *len)
{
  Rgba flat;
  u8 pal[768], *idx = NULL, *plane = NULL;
  int np = indexed256 ? 8 : 24, p;
  size_t rowbytes = (size_t)((im->w + 15) >> 4) << 1, sizepos, bodypos;
  i32 x, y;
  Buf b;
  if (!im->px || im->w < 1 || im->h < 1 || im->w > 65535 || im->h > 65535) return NULL;
  if (!rgba_alloc(&flat, im->w, im->h)) return NULL;
  memcpy(flat.px, im->px, (size_t)im->w * im->h * 4);
  rgba_flatten_white(&flat);
  memset(pal, 0, sizeof pal);
  if (!(plane = (u8 *)malloc(rowbytes))) { rgba_free(&flat); return NULL; }
  if (indexed256 && (!(idx = (u8 *)malloc((size_t)im->w * im->h)) || !quantize(&flat, 256, dither, pal, idx))) {
    free(idx); free(plane); rgba_free(&flat); return NULL;
  }
  buf_init(&b);
  buf_add(&b, "FORM", 4); sizepos = b.len; put32(&b, 0); buf_add(&b, "ILBM", 4);
  buf_add(&b, "BMHD", 4); put32(&b, 20);
  put16(&b, (u32)im->w); put16(&b, (u32)im->h); put16(&b, 0); put16(&b, 0);
  buf_addc(&b, np); buf_addc(&b, 0); buf_addc(&b, 1); buf_addc(&b, 0);   /* planes, no mask, ByteRun1 */
  put16(&b, 0); buf_addc(&b, 1); buf_addc(&b, 1);                          /* transparent colour; square pixels */
  put16(&b, (u32)(im->w < 32767 ? im->w : 32767)); put16(&b, (u32)(im->h < 32767 ? im->h : 32767));
  if (indexed256) {
    buf_add(&b, "CMAP", 4); put32(&b, 768); buf_add(&b, pal, 768);
    buf_add(&b, "CAMG", 4); put32(&b, 4); put32(&b, 0);
  }
  buf_add(&b, "BODY", 4); bodypos = b.len; put32(&b, 0);
  for (y = 0; y < im->h; y++) {
    const u8 *s = flat.px + (size_t)y * im->w * 4;
    for (p = 0; p < np; p++) {
      memset(plane, 0, rowbytes);
      if (indexed256) {
        const u8 *ir = idx + (size_t)y * im->w;
        for (x = 0; x < im->w; x++) if ((ir[x] >> p) & 1) plane[x >> 3] |= (u8)(0x80 >> (x & 7));
      } else {
        int ch = p >> 3, bit = p & 7;
        for (x = 0; x < im->w; x++) if ((s[x * 4 + ch] >> bit) & 1) plane[x >> 3] |= (u8)(0x80 >> (x & 7));
      }
      pack(&b, plane, rowbytes);
    }
  }
  free(idx); free(plane); rgba_free(&flat);
  if (b.oom) { buf_free(&b); return NULL; }
  {
    size_t bl = b.len - bodypos - 4;
    u8 *q = (u8 *)b.data + bodypos;
    q[0] = (u8)(bl >> 24); q[1] = (u8)(bl >> 16); q[2] = (u8)(bl >> 8); q[3] = (u8)bl;
    if (bl & 1) buf_addc(&b, 0);
    bl = b.len - 8;
    q = (u8 *)b.data + sizepos;
    q[0] = (u8)(bl >> 24); q[1] = (u8)(bl >> 16); q[2] = (u8)(bl >> 8); q[3] = (u8)bl;
  }
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return (u8 *)buf_take(&b);
}

/* ================================================================== 0.4: the full writer */
#define CAMG_HIRES 0x8000
#define CAMG_LACE 0x4
void ilbm_opts_default(IlbmOpts *o)
{
  memset(o, 0, sizeof *o);
  o->kind = IK_NORMAL; o->mask = IMK_NONE; o->transparent = -1; o->cycles = 1;
  o->method = QM_MEDIAN; o->dither = DI_FS; o->compress = 1;
}
static int bits_for(int n) { int k = 1; while ((1 << k) < n && k < 8) k++; return k; }

/* HAM: the value a modify code gives for each 8-bit target, and the code (hb bits) */
static void ham_levels(int hb, u8 *code, u8 *val)
{
  int t, v;
  for (t = 0; t < 256; t++) {
    int best = 0, bd = 999;
    for (v = 0; v < (1 << hb); v++) {
      int m = hb == 4 ? v * 17 : (v << 2) | (v >> 4), dd = abs(m - t);
      if (dd < bd) { bd = dd; best = v; }
    }
    code[t] = (u8)best;
    val[t] = (u8)(hb == 4 ? best * 17 : (best << 2) | (best >> 4));
  }
}
static long cdist(int r0, int g0, int b0, int r1, int g1, int b1)
{
  long dr = r0 - r1, dg = g0 - g1, db = b0 - b1;
  return dr * dr * 3 + dg * dg * 4 + db * db * 2;
}
/* one row of HAM: for each pixel the best of setting a base colour (the nearest of the whole base
   palette, found exactly) or holding two channels and modifying red, green or blue to the nearest
   level; codes get the hb + 2 bit values */
static void ham_row(const u8 *row, i32 w, const u8 *base, int nb, int hb, const u8 *lcode, const u8 *lval, u8 *codes)
{
  int r = base[0], g = base[1], b = base[2];
  i32 x;
  for (x = 0; x < w; x++) {
    const u8 *p = row + x * 4;
    int tr = p[0], tg = p[1], tb = p[2], i, bi = 0, c, nr, ng, nbl;
    long bd = 0x7FFFFFFFL, dd;
    for (i = 0; i < nb; i++) {
      dd = cdist(base[i * 3], base[i * 3 + 1], base[i * 3 + 2], tr, tg, tb);
      if (dd < bd) { bd = dd; bi = i; }
    }
    c = bi; nr = base[bi * 3]; ng = base[bi * 3 + 1]; nbl = base[bi * 3 + 2];
    dd = cdist(r, g, lval[tb], tr, tg, tb);
    if (dd < bd) { bd = dd; c = (1 << hb) | lcode[tb]; nr = r; ng = g; nbl = lval[tb]; }
    dd = cdist(lval[tr], g, b, tr, tg, tb);
    if (dd < bd) { bd = dd; c = (2 << hb) | lcode[tr]; nr = lval[tr]; ng = g; nbl = b; }
    dd = cdist(r, lval[tg], b, tr, tg, tb);
    if (dd < bd) { bd = dd; c = (3 << hb) | lcode[tg]; nr = r; ng = lval[tg]; nbl = b; }
    codes[x] = (u8)c; r = nr; g = ng; b = nbl;
  }
}
/* sixteen colours for one line (SHAM): its own colours if it has 16 or fewer, else a few rounds of
   k-means from colours spread through the line by brightness; 12-bit, as SHAM keeps them */
static void line_palette(const u8 *row, i32 w, u8 *pal)
{
  long acc[16][4];
  int i, it, n = 0;
  i32 x;
  memset(pal, 0, 48);
  for (x = 0; x < w && n <= 16; x++) {
    int r = (row[x * 4] + 8) / 17 * 17, g = (row[x * 4 + 1] + 8) / 17 * 17, b = (row[x * 4 + 2] + 8) / 17 * 17;
    for (i = 0; i < n; i++) if (pal[i * 3] == r && pal[i * 3 + 1] == g && pal[i * 3 + 2] == b) break;
    if (i == n) { if (n == 16) { n = 17; break; } pal[n * 3] = (u8)r; pal[n * 3 + 1] = (u8)g; pal[n * 3 + 2] = (u8)b; n++; }
  }
  if (n <= 16) return;
  for (i = 0; i < 16; i++) { const u8 *p = row + (long)((w - 1) * i / 15) * 4; pal[i * 3] = p[0]; pal[i * 3 + 1] = p[1]; pal[i * 3 + 2] = p[2]; }
  for (it = 0; it < 5; it++) {
    memset(acc, 0, sizeof acc);
    for (x = 0; x < w; x++) {
      const u8 *p = row + x * 4;
      int bi = 0;
      long bd = 0x7FFFFFFFL;
      for (i = 0; i < 16; i++) { long dd = cdist(pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2], p[0], p[1], p[2]); if (dd < bd) { bd = dd; bi = i; } }
      acc[bi][0] += p[0]; acc[bi][1] += p[1]; acc[bi][2] += p[2]; acc[bi][3]++;
    }
    for (i = 0; i < 16; i++) if (acc[i][3]) { int c; for (c = 0; c < 3; c++) pal[i * 3 + c] = (u8)((acc[i][c] + acc[i][3] / 2) / acc[i][3]); }
  }
  for (i = 0; i < 48; i++) pal[i] = (u8)((pal[i] + 8) / 17 * 17);
}
/* an Extra Half-Brite palette (32 base colours; 32-63 are their halves): median cut to 32, then a
   few rounds of k-means over a sample of the pixels where a pixel nearest a half-bright entry pulls
   its base toward twice its colour */
static int ehb_palette(const Rgba *flat, int method, u8 *pal)
{
  long n = (long)flat->w * flat->h, step = n > 65536 ? n / 65536 : 1, k;
  long acc[32][4];
  int i, it;
  if (!quantize_rgba(flat->px, flat->w, flat->h, 32, method, DI_NONE, pal, 0, NULL)) return 0;
  for (i = 0; i < 96; i++) pal[96 + i] = (u8)(pal[i] >> 1);
  for (it = 0; it < 6; it++) {
    memset(acc, 0, sizeof acc);
    for (k = 0; k < n; k += step) {
      const u8 *p = flat->px + k * 4;
      int bi = 0, c;
      long bd = 0x7FFFFFFFL;
      for (i = 0; i < 64; i++) { long dd = cdist(pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2], p[0], p[1], p[2]); if (dd < bd) { bd = dd; bi = i; } }
      for (c = 0; c < 3; c++) { int v = bi >= 32 ? p[c] * 2 + 1 : p[c]; acc[bi & 31][c] += v > 255 ? 255 : v; }
      acc[bi & 31][3]++;
    }
    for (i = 0; i < 32; i++) if (acc[i][3]) { int c; for (c = 0; c < 3; c++) pal[i * 3 + c] = (u8)((acc[i][c] + acc[i][3] / 2) / acc[i][3]); }
    for (i = 0; i < 96; i++) pal[96 + i] = (u8)(pal[i] >> 1);
  }
  return 64;
}
/* a value's planes into one row of one plane */
static void plane_row(const u8 *vals, i32 w, int p, u8 *plane, size_t rowbytes)
{
  i32 x;
  memset(plane, 0, rowbytes);
  for (x = 0; x < w; x++) if ((vals[x] >> p) & 1) plane[x >> 3] |= (u8)(0x80 >> (x & 7));
}
static void put_row(Buf *b, const u8 *plane, size_t rowbytes, int compress)
{
  if (compress) pack(b, plane, rowbytes); else buf_add(b, plane, rowbytes);
}
static u8 *ilbm_fail(char *err, size_t cap, const char *msg) { codec_err(err, cap, msg); return NULL; }

u8 *ilbm_write(const Rgba *im, const u8 *inpal, int innpal, const u8 *inidx, const CycleRange *cyc, int ncyc,
               const IlbmOpts *o, size_t *len, char *err, size_t errcap)
{
  IlbmOpts def;
  Rgba flat;
  u8 *pal = NULL, *idx = NULL, *alpha = NULL, *plane = NULL, *vals = NULL, *sham = NULL, lcode[256], lval[256];
  int kind, np = 0, npal = 0, maskmode, tslot = -1, i, p, ham = 0, hb = 0, anyt = 0;
  long n, k;
  size_t rowbytes, sizepos, bodypos;
  i32 x, y;
  u32 camg;
  Buf b;
  if (!o) { ilbm_opts_default(&def); o = &def; }
  if (!im || !im->px || im->w < 1 || im->h < 1 || im->w > 32767 || im->h > 32767) return ilbm_fail(err, errcap, "Bad picture size for ILBM");
  n = (long)im->w * im->h;
  kind = o->kind >= 0 && o->kind < IK_N ? o->kind : IK_NORMAL;
  maskmode = o->mask;
  if (maskmode == IMK_TRANSPARENT && (kind == IK_HAM6 || kind == IK_HAM8 || kind == IK_SHAM || kind == IK_DEEP24)) maskmode = IMK_PLANE;
  if (!rgba_alloc(&flat, im->w, im->h)) return ilbm_fail(err, errcap, "Out of memory");
  memcpy(flat.px, im->px, (size_t)n * 4);
  rgba_flatten_white(&flat);
  pal = (u8 *)calloc(768, 1); idx = (u8 *)malloc((size_t)n); alpha = (u8 *)malloc((size_t)n);
  rowbytes = (size_t)((im->w + 15) >> 4) << 1;
  plane = (u8 *)malloc(rowbytes); vals = (u8 *)malloc((size_t)im->w + 1);
  if (!pal || !idx || !alpha || !plane || !vals) goto oom;
  for (k = 0; k < n; k++) { alpha[k] = im->px[k * 4 + 3]; if (alpha[k] < 128) anyt = 1; }
  if (!anyt && maskmode == IMK_TRANSPARENT) maskmode = IMK_NONE;

  switch (kind) {
  case IK_DEEP24: np = 24; break;
  case IK_HAM6: case IK_HAM8: case IK_SHAM: {
    int nb = kind == IK_HAM8 ? 64 : 16;
    ham = 1; hb = kind == IK_HAM8 ? 6 : 4; np = hb + 2;
    npal = quantize_rgba(flat.px, flat.w, flat.h, nb, (o->method & QM_MASK) == QM_FIXED ? QM_MEDIAN : (o->method & QM_MASK), DI_NONE, pal, 0, NULL);
    if (!npal) goto oom;
    for (i = npal; i < nb; i++) pal[i * 3] = pal[i * 3 + 1] = pal[i * 3 + 2] = 0;
    npal = nb;
    if (hb == 4) for (i = 0; i < 48; i++) pal[i] = (u8)((pal[i] + 8) / 17 * 17);   /* HAM6: OCS 12-bit colours */
    ham_levels(hb, lcode, lval);
    if (kind == IK_SHAM && !(sham = (u8 *)malloc((size_t)im->h * 48))) goto oom;
    break;
  }
  case IK_EHB: {
    int exact = 0;
    np = 6;
    if (inpal && inidx && innpal <= 64) {
      /* exact when every used entry from 32 up is the half of its base */
      u8 used[64];
      memset(used, 0, sizeof used);
      for (k = 0; k < n; k++) if (inidx[k] < 64) used[inidx[k]] = 1; else break;
      exact = k == n;
      for (i = 32; exact && i < 64; i++)
        if (used[i] && (inpal[i * 3] != inpal[(i - 32) * 3] >> 1 || inpal[i * 3 + 1] != inpal[(i - 32) * 3 + 1] >> 1 || inpal[i * 3 + 2] != inpal[(i - 32) * 3 + 2] >> 1)) exact = 0;
      if (exact) { memcpy(pal, inpal, (size_t)(innpal < 32 ? innpal : 32) * 3); memcpy(idx, inidx, (size_t)n); }
    }
    if (!exact) {
      if (!ehb_palette(&flat, (o->method & QM_MASK) == QM_FIXED ? QM_MEDIAN : (o->method & QM_MASK), pal)) goto oom;
      if (!map_rgba(flat.px, flat.w, flat.h, pal, 64, o->dither, idx)) goto oom;
    }
    for (i = 0; i < 96; i++) pal[96 + i] = (u8)(pal[i] >> 1);
    npal = 64;
    break;
  }
  default: {
    int maxent = o->planes >= 1 && o->planes <= 8 ? 1 << o->planes : 256, reserve = maskmode == IMK_TRANSPARENT && o->transparent < 0 ? 1 : 0, used = 0;
    if (inpal && inidx && innpal >= 1) {
      for (k = 0; k < n; k++) if (inidx[k] + 1 > used && (maskmode != IMK_TRANSPARENT || alpha[k] >= 128)) used = inidx[k] + 1;
      if (used <= maxent - (innpal >= maxent ? reserve : 0)) {
        npal = innpal > maxent ? maxent : innpal;
        if (npal < used) npal = used;
        memcpy(pal, inpal, (size_t)npal * 3); memcpy(idx, inidx, (size_t)n);
      }
    }
    if (!npal) {
      npal = quantize_rgba(flat.px, flat.w, flat.h, maxent - reserve < 2 ? 2 : maxent - reserve, (o->method & QM_MASK) == QM_FIXED ? QM_MEDIAN : o->method, o->dither, pal, 0, idx);
      if (!npal) goto oom;
    }
    if (maskmode == IMK_TRANSPARENT) {
      if (o->transparent >= 0 && o->transparent < 256) {
        tslot = o->transparent;
        for (k = 0; k < n; k++) if (alpha[k] < 128) idx[k] = (u8)tslot;
        if (tslot >= npal) npal = tslot + 1;
      } else {
        int cap = npal;
        tslot = flat_transparent(idx, alpha, n, pal, &cap);
        if (tslot < 0) maskmode = IMK_PLANE; else npal = cap;
      }
    }
    np = o->planes >= 1 && o->planes <= 8 ? o->planes : bits_for(npal);
    if ((1 << np) < npal) np = bits_for(npal);
    break;
  }
  }
  /* the header */
  buf_init(&b);
  buf_add(&b, "FORM", 4); sizepos = b.len; put32(&b, 0); buf_add(&b, "ILBM", 4);
  buf_add(&b, "BMHD", 4); put32(&b, 20);
  put16(&b, (u32)im->w); put16(&b, (u32)im->h); put16(&b, 0); put16(&b, 0);
  buf_addc(&b, np); buf_addc(&b, maskmode == IMK_PLANE ? 1 : maskmode == IMK_TRANSPARENT ? 2 : 0); buf_addc(&b, o->compress ? 1 : 0); buf_addc(&b, 0);
  put16(&b, tslot >= 0 ? (u32)tslot : 0);
  buf_addc(&b, o->aspect_x ? o->aspect_x : 1); buf_addc(&b, o->aspect_y ? o->aspect_y : 1);
  put16(&b, (u32)im->w); put16(&b, (u32)im->h);
  if (np <= 8) {
    int nc = kind == IK_EHB ? 32 : ham ? npal : (1 << np) < npal ? (1 << np) : npal;
    if (kind == IK_NORMAL && nc < (1 << np) && np < 8) nc = 1 << np;   /* (a full CMAP for the depth, unused entries black) */
    buf_add(&b, "CMAP", 4); put32(&b, (u32)nc * 3); buf_add(&b, pal, (size_t)nc * 3);
    if ((nc * 3) & 1) buf_addc(&b, 0);
  }
  camg = o->modeid;
  if (!camg) camg = (im->w > 400 ? CAMG_HIRES : 0) | (im->h > 300 ? CAMG_LACE : 0);
  camg &= ~(u32)(CAMG_HAM | CAMG_EHB);
  if (ham) camg |= CAMG_HAM;
  if (kind == IK_EHB) camg |= CAMG_EHB;
  buf_add(&b, "CAMG", 4); put32(&b, 4); put32(&b, camg);
  if (o->cycles && cyc && ncyc > 0 && np <= 8) cycle_write_chunks(&b, cyc, ncyc, 0);
  if (sham) {
    /* every line's palette first, so the chunk can go before the BODY */
    for (y = 0; y < im->h; y++) line_palette(flat.px + (size_t)y * im->w * 4, im->w, sham + (size_t)y * 48);
    /* (the CMAP written above is the whole picture's; SHAM readers take each line's from SHAM) */
    buf_add(&b, "SHAM", 4); put32(&b, 2 + (u32)im->h * 32); put16(&b, 0);
    for (y = 0; y < im->h; y++)
      for (i = 0; i < 16; i++) { const u8 *c = sham + (size_t)y * 48 + i * 3; put16(&b, ((u32)(c[0] / 17) << 8) | ((u32)(c[1] / 17) << 4) | (u32)(c[2] / 17)); }
  }
  buf_add(&b, "BODY", 4); bodypos = b.len; put32(&b, 0);
  for (y = 0; y < im->h; y++) {
    const u8 *s = flat.px + (size_t)y * im->w * 4;
    if (np == 24) {
      for (p = 0; p < 24; p++) {
        int ch = p >> 3, bit = p & 7;
        memset(plane, 0, rowbytes);
        for (x = 0; x < im->w; x++) if ((s[x * 4 + ch] >> bit) & 1) plane[x >> 3] |= (u8)(0x80 >> (x & 7));
        put_row(&b, plane, rowbytes, o->compress);
      }
    } else {
      if (ham) ham_row(s, im->w, sham ? sham + (size_t)y * 48 : pal, sham ? 16 : npal, hb, lcode, lval, vals);
      else memcpy(vals, idx + (size_t)y * im->w, (size_t)im->w);
      for (p = 0; p < np; p++) { plane_row(vals, im->w, p, plane, rowbytes); put_row(&b, plane, rowbytes, o->compress); }
    }
    if (maskmode == IMK_PLANE) {
      memset(plane, 0, rowbytes);
      for (x = 0; x < im->w; x++) if (alpha[(size_t)y * im->w + x] >= 128) plane[x >> 3] |= (u8)(0x80 >> (x & 7));
      put_row(&b, plane, rowbytes, o->compress);
    }
  }
  free(pal); free(idx); free(alpha); free(plane); free(vals); free(sham); rgba_free(&flat);
  if (b.oom) { buf_free(&b); return ilbm_fail(err, errcap, "Out of memory"); }
  {
    size_t bl = b.len - bodypos - 4;
    u8 *q = (u8 *)b.data + bodypos;
    q[0] = (u8)(bl >> 24); q[1] = (u8)(bl >> 16); q[2] = (u8)(bl >> 8); q[3] = (u8)bl;
    if (bl & 1) buf_addc(&b, 0);
    bl = b.len - 8;
    q = (u8 *)b.data + sizepos;
    q[0] = (u8)(bl >> 24); q[1] = (u8)(bl >> 16); q[2] = (u8)(bl >> 8); q[3] = (u8)bl;
  }
  if (b.oom) { buf_free(&b); return ilbm_fail(err, errcap, "Out of memory"); }
  *len = b.len;
  return (u8 *)buf_take(&b);
oom:
  free(pal); free(idx); free(alpha); free(plane); free(vals); free(sham); rgba_free(&flat);
  return ilbm_fail(err, errcap, "Out of memory");
}

u8 *ilbm_write_doc(Doc *d, const IlbmOpts *o, size_t *len, char *err, size_t errcap)
{
  IlbmOpts op;
  Rgba im;
  u8 *idx = NULL, *out;
  if (!d) return NULL;
  if (o) op = *o; else ilbm_opts_default(&op);
  if (!op.modeid) op.modeid = d->modeid;
  if (!op.aspect_x || !op.aspect_y) { op.aspect_x = d->aspect_x ? d->aspect_x : 1; op.aspect_y = d->aspect_y ? d->aspect_y : 1; }
  im.w = d->w; im.h = d->h;
  if (!(im.px = doc_flat_rgba(d))) return ilbm_fail(err, errcap, "Out of memory");
  if (d->mode == MODE_INDEXED || d->mode == MODE_BITMAP) idx = doc_flat_indices(d, NULL);
  out = ilbm_write(&im, idx ? d->pal : NULL, d->npal, idx, d->cycle, d->ncycle, &op, len, err, errcap);
  free(im.px); free(idx);
  return out;
}

/* ================================================================== 0.4: reading into an indexed document */
typedef struct {
  int form;                           /* 0 ILBM, 1 PBM, 2 ACBM */
  i32 w, h; int np, masking, comp, tcol, haveh, ncmap, ax, ay;
  u32 camg; const u8 *cmap; const u8 *body; size_t bodylen;
} IlbmInfo;
static int ilbm_scan(const u8 *d, size_t n, IlbmInfo *f)
{
  size_t end, o;
  memset(f, 0, sizeof *f);
  if (n < 12 || memcmp(d, "FORM", 4)) return 0;
  if (!memcmp(d + 8, "ILBM", 4)) f->form = 0;
  else if (!memcmp(d + 8, "PBM ", 4)) f->form = 1;
  else if (!memcmp(d + 8, "ACBM", 4)) f->form = 2;
  else return 0;
  end = be32(d + 4);
  end = end > n - 8 ? n : end + 8;
  for (o = 12; o + 8 <= end; ) {
    size_t len = be32(d + o + 4), avail = end - o - 8;
    const u8 *c = d + o + 8;
    if (len > avail) len = avail;
    if (!memcmp(d + o, "BMHD", 4) && len >= 20) {
      f->w = (i32)be16(c); f->h = (i32)be16(c + 2); f->np = c[8]; f->masking = c[9]; f->comp = c[10]; f->tcol = (int)be16(c + 12);
      f->ax = c[14]; f->ay = c[15]; f->haveh = 1;
    } else if (!memcmp(d + o, "CMAP", 4)) { f->ncmap = (int)(len / 3 > 256 ? 256 : len / 3); f->cmap = c; }
    else if (!memcmp(d + o, "CAMG", 4) && len >= 4) f->camg = be32(c);
    else if (!memcmp(d + o, f->form == 2 ? "ABIT" : "BODY", 4) && !f->body) { f->body = c; f->bodylen = len; }
    o += 8 + len + (len & 1);
  }
  return f->haveh && f->body;
}
int ilbm_indexable(const u8 *d, size_t n)
{
  IlbmInfo f;
  if (!ilbm_scan(d, n, &f)) return 0;
  if (f.np < 1 || f.np > 8 || f.comp > 1) return 0;
  if (f.form != 1 && (f.camg & CAMG_HAM) && (f.np == 6 || f.np == 8)) return 0;
  return 1;
}
Doc *ilbm_read_indexed(const u8 *d, size_t n, char *err, size_t errcap)
{
  IlbmInfo f;
  size_t rowbytes, rowsize, size;
  u8 *buf = NULL, *px = NULL, *idx = NULL;
  Doc *doc = NULL;
  int nplanes, ehb, npal, i;
  i32 x, y;
  const char *msg = "Damaged ILBM file";
  if (!ilbm_scan(d, n, &f)) { codec_err(err, errcap, "Not an IFF ILBM, PBM or ACBM picture"); return NULL; }
  if (!ilbm_indexable(d, n)) { codec_err(err, errcap, "Only pictures of 1-8 planes without HAM open as indexed"); return NULL; }
  if (f.w < 1 || f.h < 1 || f.w > MAXDIM || f.h > MAXDIM || (long)f.w * f.h > CODEC_MAXPIXELS) { codec_err(err, errcap, "Bad ILBM picture size"); return NULL; }
  ehb = f.form != 1 && f.np == 6 && ((f.camg & CAMG_EHB) || (f.ncmap > 0 && f.ncmap <= 32));
  nplanes = f.np + (f.masking == 1 && f.form != 1);
  rowbytes = (size_t)((f.w + 15) >> 4) << 1;
  rowsize = f.form == 1 ? (size_t)f.w + (f.w & 1) : rowbytes * nplanes;
  size = rowsize * (size_t)f.h;
  if ((f.comp == 0 && f.bodylen < size) || (f.comp == 1 && f.bodylen < size / 64)) { codec_err(err, errcap, "Truncated ILBM file"); return NULL; }
  if (!(buf = (u8 *)malloc(size)) || !(px = (u8 *)malloc((size_t)f.w * f.h * 4)) || !(idx = (u8 *)malloc((size_t)f.w * f.h))) { msg = "Out of memory"; codec_oom = 1; goto fail; }
  if (f.comp == 0 || f.form == 2) {
    if (f.bodylen < size) { msg = "Truncated ILBM file"; goto fail; }
    memcpy(buf, f.body, size);
  } else if (!unpack(f.body, f.bodylen, buf, size)) { msg = "Truncated ILBM file"; goto fail; }
  if (!(doc = doc_adopt(f.w, f.h, px))) { msg = "Out of memory"; codec_oom = 1; goto fail; }
  px = NULL;
  /* the palette: the CMAP (a grey ramp without one), EHB's halves after its 32 */
  npal = 1 << f.np;
  if (f.ncmap > npal) npal = f.ncmap;
  if (ehb) npal = 64;
  memset(doc->pal, 0, sizeof doc->pal);
  if (f.ncmap) memcpy(doc->pal, f.cmap, (size_t)f.ncmap * 3);
  else for (i = 0; i < (1 << f.np); i++) doc->pal[i * 3] = doc->pal[i * 3 + 1] = doc->pal[i * 3 + 2] = (u8)(i * 255 / ((1 << f.np) - 1));
  if (ehb) for (i = 0; i < 96; i++) doc->pal[96 + i] = (u8)(doc->pal[i] >> 1);
  doc->npal = npal < 2 ? 2 : npal;
  for (y = 0; y < f.h; y++) {
    u8 *ir = idx + (size_t)y * f.w, *pr = doc->lay[0]->px + (size_t)y * f.w * 4;
    int p;
    if (f.form == 1) memcpy(ir, buf + (size_t)y * rowsize, (size_t)f.w);
    else {
      memset(ir, 0, (size_t)f.w);
      for (p = 0; p < f.np; p++) {
        /* ILBM: the planes of each row interleaved; ACBM: each plane whole, one after another */
        const u8 *pl = f.form == 2 ? buf + (size_t)p * rowbytes * f.h + (size_t)y * rowbytes : buf + (size_t)y * rowsize + (size_t)p * rowbytes;
        for (x = 0; x < f.w; x++) ir[x] |= (u8)(((pl[x >> 3] >> (7 - (x & 7))) & 1) << p);
      }
    }
    for (x = 0; x < f.w; x++) {
      const u8 *c = doc->pal + ir[x] * 3;
      u8 *q = pr + x * 4;
      q[0] = c[0]; q[1] = c[1]; q[2] = c[2]; q[3] = 255;
      if (f.masking == 2 && ir[x] == f.tcol) q[3] = 0;
      else if (f.masking == 1 && f.form != 1) {
        const u8 *ml = f.form == 2 ? buf + (size_t)f.np * rowbytes * f.h + (size_t)y * rowbytes : buf + (size_t)y * rowsize + (size_t)f.np * rowbytes;
        if (!((ml[x >> 3] >> (7 - (x & 7))) & 1)) q[3] = 0;
      }
    }
  }
  doc->lay[0]->idx = idx; idx = NULL;
  doc->mode = MODE_INDEXED;
  doc->modeid = f.camg;
  doc->aspect_x = (u8)(f.ax ? f.ax : 1); doc->aspect_y = (u8)(f.ay ? f.ay : 1);
  doc->ncycle = cycle_read_iff(d, n, doc->cycle, MAXCYCLES);
  free(buf);
  return doc;
fail:
  codec_err(err, errcap, msg);
  free(buf); free(px); free(idx);
  if (doc) doc_free(doc);
  return NULL;
}
