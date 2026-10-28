/*
 * avif.c - AVIF pictures (AV1 stills in a HEIF container), read only.
 *
 * The container is ISO base media: a 'meta' box lists the file's items (pitm the primary one, iinf
 * their types, iloc where their bytes are, iref how they relate, iprp/ipma their properties). The
 * primary item is an 'av01' image, or a 'grid' of av01 tiles. An auxiliary item whose auxC names
 * alpha carries the transparency. The AV1 data itself is decoded by dav1d (the av1/ drawer, BSD
 * licence) built for one thread. Then the YUV planes become RGB with the matrix and range the file
 * gives (its nclx colour box, else the AV1 sequence header), chroma upsampled bilinearly, and the
 * clean aperture, rotation and mirroring properties are applied in that order.
 *
 * Every box size, item extent and property index is checked against the data.
 */
#include <stdlib.h>
#include <string.h>
#include "codec.h"
#include "avif.h"
#include "dav1d/dav1d.h"

#define MAXITEMS 1024
#define MAXPROPS 512
#define MAXASSOC 16

typedef struct { u32 off, len; } Extent;

typedef struct {
  u32 id;
  char type[4];
  int method;                 /* iloc construction method: 0 file offset, 1 inside idat */
  int nextents;
  Extent ext[4];
  u32 total;                  /* sum of the extent lengths */
  int nprops;
  u16 props[MAXASSOC];        /* 1-based indices into the property list */
  u32 alpha_for;              /* auxl reference: this item is the alpha of that one (if auxC says alpha) */
  int is_alpha;
  u32 *tiles;                 /* dimg references (a grid's tiles, in order) */
  int ntiles;
  int prem;                   /* the colour is premultiplied by this alpha (prem reference) */
} Item;

typedef struct { const u8 *p; u32 len; char type[4]; } Prop;

typedef struct {
  const u8 *d; size_t n;
  const u8 *idat; u32 idatlen;
  Item *items; int nitems;
  Prop props[MAXPROPS]; int nprops;
  u32 primary;
} Avif;

static u32 be16(const u8 *p) { return (u32)p[0] << 8 | p[1]; }
static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

/* a box header at p (within end): its type in t, its payload in *body and *blen; 0 if it doesn't fit */
static int box(const u8 *p, const u8 *end, char *t, const u8 **body, u32 *blen, u32 *total)
{
  u32 sz, h = 8;
  if (end - p < 8) return 0;
  sz = be32(p);
  memcpy(t, p + 4, 4);
  if (sz == 1) {
    if (end - p < 16 || be32(p + 8)) return 0;     /* a 64-bit size over 4 GB */
    sz = be32(p + 12); h = 16;
  } else if (sz == 0) sz = (u32)(end - p);
  if (sz < h || sz > (u32)(end - p)) return 0;
  *body = p + h; *blen = sz - h; *total = sz;
  return 1;
}

static Item *item_get(Avif *a, u32 id)
{
  int i;
  for (i = 0; i < a->nitems; i++) if (a->items[i].id == id) return &a->items[i];
  return NULL;
}

/* the item with this id, made if it isn't listed yet; NULL when there are too many */
static Item *item_make(Avif *a, u32 id)
{
  Item *it = item_get(a, id);
  if (it) return it;
  if (a->nitems >= MAXITEMS) return NULL;
  it = &a->items[a->nitems++];
  memset(it, 0, sizeof *it);
  it->id = id;
  return it;
}

/* an unsigned number of sz bytes (0, 4 or 8; 8 must fit 32 bits) at *p, advancing it */
static int readn(const u8 **p, const u8 *end, int sz, u32 *v)
{
  if (end - *p < sz) return 0;
  if (sz == 0) *v = 0;
  else if (sz == 2) *v = be16(*p);
  else if (sz == 4) *v = be32(*p);
  else if (sz == 8) { if (be32(*p)) return 0; *v = be32(*p + 4); }
  else return 0;
  *p += sz;
  return 1;
}

static int parse_iloc(Avif *a, const u8 *p, u32 len)
{
  const u8 *end = p + len;
  int ver, osz, lsz, bsz, isz = 0;
  u32 count, i;
  if (len < 8) return 0;
  ver = p[0]; p += 4;
  if (ver > 2) return 0;
  osz = p[0] >> 4; lsz = p[0] & 15; bsz = p[1] >> 4;
  if (ver >= 1) isz = p[1] & 15;
  p += 2;
  if (ver < 2) { if (end - p < 2) return 0; count = be16(p); p += 2; }
  else { if (end - p < 4) return 0; count = be32(p); p += 4; }
  for (i = 0; i < count; i++) {
    u32 id, base, next, e, v;
    int method = 0;
    Item *it;
    if (ver < 2) { if (!readn(&p, end, 2, &id)) return 0; }
    else if (!readn(&p, end, 4, &id)) return 0;
    if (ver >= 1) { if (end - p < 2) return 0; method = p[1] & 15; p += 2; }
    if (end - p < 2) return 0;
    if (be16(p)) return 0;                  /* data_reference_index: only this file */
    p += 2;
    if (!readn(&p, end, bsz, &base) || end - p < 2) return 0;
    next = be16(p); p += 2;
    it = item_make(a, id);
    if (!it) return 0;
    it->method = method;
    it->nextents = 0; it->total = 0;
    for (e = 0; e < next; e++) {
      u32 off, l;
      if (isz && !readn(&p, end, isz, &v)) return 0;
      if (!readn(&p, end, osz, &off) || !readn(&p, end, lsz, &l)) return 0;
      if (it->nextents >= 4) return 0;
      if (off > 0xFFFFFFFFUL - base) return 0;
      it->ext[it->nextents].off = base + off;
      it->ext[it->nextents].len = l;
      it->nextents++;
      if (l > 0xFFFFFFFFUL - it->total) return 0;
      it->total += l;
    }
  }
  return 1;
}

static int parse_iinf(Avif *a, const u8 *p, u32 len)
{
  const u8 *end = p + len, *b;
  u32 count, i, bl, tot;
  char t[4];
  if (len < 6) return 0;
  if (p[0] == 0) { count = be16(p + 4); p += 6; }
  else { if (len < 8) return 0; count = be32(p + 4); p += 8; }
  for (i = 0; i < count && p < end; i++) {
    Item *it;
    u32 id;
    if (!box(p, end, t, &b, &bl, &tot)) return 0;
    p += tot;
    if (memcmp(t, "infe", 4) || bl < 4) continue;
    if (b[0] < 2) continue;                       /* version 0/1 entries carry no item type */
    if (b[0] == 2) { if (bl < 12) return 0; id = be16(b + 4); memcpy(t, b + 8, 4); }
    else { if (bl < 14) return 0; id = be32(b + 4); memcpy(t, b + 10, 4); }
    it = item_make(a, id);
    if (!it) return 0;
    memcpy(it->type, t, 4);
  }
  return 1;
}

static int parse_iref(Avif *a, const u8 *p, u32 len)
{
  const u8 *end = p + len, *b;
  int idsz;
  u32 bl, tot;
  char t[4];
  if (len < 4) return 0;
  idsz = p[0] ? 4 : 2;
  p += 4;
  while (p < end) {
    const u8 *q, *qe;
    u32 from, n, i, to;
    Item *it;
    if (!box(p, end, t, &b, &bl, &tot)) return 0;
    p += tot;
    q = b; qe = b + bl;
    if (!readn(&q, qe, idsz, &from) || qe - q < 2) return 0;
    n = be16(q); q += 2;
    if ((u32)(qe - q) < n * (u32)idsz) return 0;
    it = item_make(a, from);
    if (!it) return 0;
    if (!memcmp(t, "dimg", 4)) {
      free(it->tiles);
      it->tiles = n ? malloc(n * sizeof(u32)) : NULL;
      if (n && !it->tiles) { codec_oom = 1; return 0; }
      it->ntiles = (int)n;
    }
    for (i = 0; i < n; i++) {
      readn(&q, qe, idsz, &to);
      if (!memcmp(t, "dimg", 4)) it->tiles[i] = to;
      else if (!memcmp(t, "auxl", 4) && i == 0) it->alpha_for = to;
      else if (!memcmp(t, "prem", 4)) {
        Item *c = item_make(a, to);
        if (c) c->prem = 1;
      }
    }
  }
  return 1;
}

static int parse_iprp(Avif *a, const u8 *p, u32 len)
{
  const u8 *end = p + len, *b, *q, *qe;
  u32 bl, tot, cl, ct;
  char t[4];
  while (p < end) {
    if (!box(p, end, t, &b, &bl, &tot)) return 0;
    p += tot;
    if (!memcmp(t, "ipco", 4)) {
      q = b; qe = b + bl;
      while (q < qe) {
        const u8 *cb;
        char ctp[4];
        if (!box(q, qe, ctp, &cb, &cl, &ct)) return 0;
        q += ct;
        if (a->nprops >= MAXPROPS) return 0;
        a->props[a->nprops].p = cb;
        a->props[a->nprops].len = cl;
        memcpy(a->props[a->nprops].type, ctp, 4);
        a->nprops++;
      }
    } else if (!memcmp(t, "ipma", 4)) {
      int ver, flags;
      u32 count, i;
      if (bl < 8) return 0;
      ver = b[0]; flags = b[3];
      count = be32(b + 4);
      q = b + 8; qe = b + bl;
      for (i = 0; i < count; i++) {
        u32 id, k, na;
        Item *it;
        if (!readn(&q, qe, ver < 1 ? 2 : 4, &id) || q >= qe) return 0;
        na = *q++;
        it = item_make(a, id);
        if (!it) return 0;
        for (k = 0; k < na; k++) {
          u32 idx;
          if (flags & 1) { if (qe - q < 2) return 0; idx = be16(q) & 0x7FFF; q += 2; }
          else { if (q >= qe) return 0; idx = *q++ & 0x7F; }
          if (idx && it->nprops < MAXASSOC) it->props[it->nprops++] = (u16)idx;
        }
      }
    }
  }
  return 1;
}

/* the item's property of this type (the first associated), or NULL */
static const Prop *prop(const Avif *a, const Item *it, const char *type)
{
  int i;
  for (i = 0; i < it->nprops; i++) {
    int k = it->props[i] - 1;
    if (k < a->nprops && !memcmp(a->props[k].type, type, 4)) return &a->props[k];
  }
  return NULL;
}

/* auxC says alpha: "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha" or HEVC's "urn:mpeg:hevc:2015:auxid:1" */
static int aux_is_alpha(const Avif *a, const Item *it)
{
  static const char *names[] = { "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha", "urn:mpeg:hevc:2015:auxid:1" };
  const Prop *p = prop(a, it, "auxC");
  int i;
  if (!p || p->len < 5) return 0;
  for (i = 0; i < 2; i++) {
    size_t l = strlen(names[i]);
    if (p->len - 4 >= l + 1 && !memcmp(p->p + 4, names[i], l) && p->p[4 + l] == 0) return 1;
    if (p->len - 4 == l && !memcmp(p->p + 4, names[i], l)) return 1;
  }
  return 0;
}

/* an item's bytes gathered into one malloc'd buffer (NULL when they lie outside the file) */
static u8 *item_bytes(const Avif *a, const Item *it, u32 *len)
{
  u8 *buf;
  u32 at = 0;
  int e;
  if (!it->nextents || !it->total) return NULL;
  for (e = 0; e < it->nextents; e++) {
    u32 o = it->ext[e].off, l = it->ext[e].len;
    size_t lim = it->method == 1 ? a->idatlen : a->n;
    if (it->method > 1 || o > lim || l > lim - o) return NULL;
    if (l == 0 && it->nextents == 1) return NULL;
  }
  buf = malloc(it->total);
  if (!buf) { codec_oom = 1; return NULL; }
  for (e = 0; e < it->nextents; e++) {
    const u8 *src = (it->method == 1 ? a->idat : a->d) + it->ext[e].off;
    memcpy(buf + at, src, it->ext[e].len);
    at += it->ext[e].len;
  }
  *len = it->total;
  return buf;
}

static void nofree(const uint8_t *buf, void *cookie) { (void)buf; (void)cookie; }

/* one av01 item decoded into *pic (released with dav1d_picture_unref); 0 with a message */
static int decode_item(const Avif *a, const Item *it, Dav1dPicture *pic, char *err, size_t errcap)
{
  Dav1dSettings s;
  Dav1dContext *c = NULL;
  Dav1dData data;
  u8 *bytes;
  u32 len = 0;
  int r = 0, ok = 0;
  if (memcmp(it->type, "av01", 4)) { codec_err(err, errcap, "This AVIF picture isn't AV1 coded"); return 0; }
  bytes = item_bytes(a, it, &len);
  if (!bytes) { codec_err(err, errcap, codec_oom ? "Not enough memory for this picture" : "Damaged AVIF file (picture data missing)"); return 0; }
  dav1d_default_settings(&s);
  s.n_threads = 1;
  s.max_frame_delay = 1;
  s.all_layers = 0;
  s.frame_size_limit = CODEC_MAXPIXELS;
  memset(pic, 0, sizeof *pic);
  memset(&data, 0, sizeof data);
  if (dav1d_open(&c, &s) < 0) { codec_oom = 1; codec_err(err, errcap, "Not enough memory for the AV1 decoder"); free(bytes); return 0; }
  if (dav1d_data_wrap(&data, bytes, len, nofree, NULL) < 0) goto done;
  r = dav1d_send_data(c, &data);
  if (r < 0 && r != DAV1D_ERR(EAGAIN)) goto done;
  for (;;) {
    r = dav1d_get_picture(c, pic);
    if (r == 0) { ok = 1; break; }
    if (r != DAV1D_ERR(EAGAIN)) break;
    if (data.sz) { r = dav1d_send_data(c, &data); if (r < 0 && r != DAV1D_ERR(EAGAIN)) break; }
    else {
      r = dav1d_get_picture(c, pic);   /* drain */
      ok = r == 0;
      break;
    }
  }
done:
  if (!ok) {
    if (r == DAV1D_ERR(ENOMEM)) { codec_oom = 1; codec_err(err, errcap, "Not enough memory for this picture"); }
    else codec_err(err, errcap, "Damaged or unsupported AV1 data in this AVIF file");
  }
  dav1d_data_unref(&data);
  dav1d_close(&c);
  free(bytes);
  return ok;
}

/* ---------------------------------------------------------------- colour */

/* YUV to RGB, in 16.16 fixed point, per code value; tables for the bit depth in use */
typedef struct {
  int depth, shift;           /* code value bits; shift takes chroma tables down to 8-bit indices */
  int identity, mono;
  long *ty, *tvr, *tug, *tvg, *tub;   /* contributions, scaled to 0..255 << 16 */
  long *tbuf;
} Conv;

static void conv_free(Conv *cv) { free(cv->tbuf); cv->tbuf = NULL; }

/* tables for matrix mc (H.273 matrix_coefficients) and range */
static int conv_init(Conv *cv, int depth, int mc, int full, int mono)
{
  double kr, kb, kg, ys, yo, cs, mx = (double)((1L << depth) - 1);
  long n = 1L << depth, i;
  memset(cv, 0, sizeof *cv);
  cv->depth = depth; cv->mono = mono;
  cv->identity = mc == 0;
  switch (mc) {
  case 1: kr = 0.2126; kb = 0.0722; break;
  case 4: kr = 0.30; kb = 0.11; break;
  case 7: kr = 0.212; kb = 0.087; break;
  case 9: case 10: kr = 0.2627; kb = 0.0593; break;
  default: kr = 0.299; kb = 0.114; break;     /* 5, 6, unspecified */
  }
  kg = 1.0 - kr - kb;
  if (full) { ys = 255.0 / mx; yo = 0; cs = 255.0 / mx; }
  else {
    ys = 255.0 / (219.0 * (1 << (depth - 8)));
    yo = 16.0 * (1 << (depth - 8));
    cs = 255.0 / (224.0 * (1 << (depth - 8)));
  }
  cv->tbuf = malloc(5 * n * sizeof(long));
  if (!cv->tbuf) { codec_oom = 1; return 0; }
  cv->ty = cv->tbuf; cv->tvr = cv->ty + n; cv->tug = cv->tvr + n; cv->tvg = cv->tug + n; cv->tub = cv->tvg + n;
  for (i = 0; i < n; i++) {
    double y = ((double)i - yo) * ys, c = ((double)i - (double)(n >> 1)) * cs;
    if (cv->identity) {
      /* GBR: the planes are G, B, R; each scaled like luma (chroma ranges don't apply) */
      cv->ty[i] = (long)(y * 65536.0);
      cv->tvr[i] = cv->tug[i] = cv->tvg[i] = cv->tub[i] = 0;
      continue;
    }
    cv->ty[i] = (long)(y * 65536.0 + (y >= 0 ? 0.5 : -0.5));
    cv->tvr[i] = (long)(2.0 * (1.0 - kr) * c * 65536.0);
    cv->tub[i] = (long)(2.0 * (1.0 - kb) * c * 65536.0);
    cv->tug[i] = (long)(2.0 * kb * (1.0 - kb) / kg * c * 65536.0);
    cv->tvg[i] = (long)(2.0 * kr * (1.0 - kr) / kg * c * 65536.0);
  }
  return 1;
}

static u8 clamp16(long v)
{
  v = (v + 32768) >> 16;
  return (u8)(v < 0 ? 0 : v > 255 ? 255 : v);
}

/* sample (x, y) of a plane: 8-bit or 16-bit storage */
#define PX(pl, st, x, y, hbd) ((hbd) ? ((const u16 *)((const u8 *)(pl) + (ptrdiff_t)(y) * (st)))[x] \
                                      : ((const u8 *)(pl) + (ptrdiff_t)(y) * (st))[x])

/* chroma at luma position (x, y), bilinear with the chroma sited centrally between the luma */
static void chroma_at(const Dav1dPicture *p, int hbd, int sx, int sy, int x, int y, int *u, int *v)
{
  const void *pu = p->data[1], *pv = p->data[2];
  ptrdiff_t st = p->stride[1];
  int cw = (p->p.w + sx) >> sx, ch = (p->p.h + sy) >> sy;
  int x0, x1, y0, y1, wx, wy;   /* wx, wy: the weight of x0, y0 in quarters */
  if (!sx) { x0 = x1 = x; wx = 0; }
  else {
    /* luma x sits at chroma (x - 0.5) / 2: an even x takes 1/4 of the chroma to its left and 3/4
       of its own, an odd x 3/4 of its own and 1/4 of the next */
    if (x & 1) { x0 = x >> 1; x1 = x0 + 1; wx = 3; } else { x0 = (x >> 1) - 1; x1 = x >> 1; wx = 1; }
    if (x0 < 0) x0 = 0;
    if (x1 >= cw) x1 = cw - 1;
  }
  if (!sy) { y0 = y1 = y; wy = 0; }
  else {
    if (y & 1) { y0 = y >> 1; y1 = y0 + 1; wy = 3; } else { y0 = (y >> 1) - 1; y1 = y >> 1; wy = 1; }
    if (y0 < 0) y0 = 0;
    if (y1 >= ch) y1 = ch - 1;
  }
  {
    int a0 = sx ? wx : 4, a1 = 4 - a0, b0 = sy ? wy : 4, b1 = 4 - b0;
    long su = (long)PX(pu, st, x0, y0, hbd) * a0 * b0 + (long)PX(pu, st, x1, y0, hbd) * a1 * b0
            + (long)PX(pu, st, x0, y1, hbd) * a0 * b1 + (long)PX(pu, st, x1, y1, hbd) * a1 * b1;
    long sv = (long)PX(pv, st, x0, y0, hbd) * a0 * b0 + (long)PX(pv, st, x1, y0, hbd) * a1 * b0
            + (long)PX(pv, st, x0, y1, hbd) * a0 * b1 + (long)PX(pv, st, x1, y1, hbd) * a1 * b1;
    *u = (int)((su + 8) >> 4);
    *v = (int)((sv + 8) >> 4);
  }
}

/* picture p into out at (ox, oy), clipped to out; alpha left alone */
static void put_rgb(const Conv *cv, const Dav1dPicture *p, Rgba *out, int ox, int oy)
{
  int hbd = p->p.bpc > 8, sx, sy, x, y;
  int lay = p->p.layout;
  sx = lay == DAV1D_PIXEL_LAYOUT_I420 || lay == DAV1D_PIXEL_LAYOUT_I422;
  sy = lay == DAV1D_PIXEL_LAYOUT_I420;
  for (y = 0; y < p->p.h; y++) {
    u8 *o;
    if (oy + y < 0 || oy + y >= out->h) continue;
    o = out->px + ((size_t)(oy + y) * out->w) * 4;
    for (x = 0; x < p->p.w; x++) {
      int Y = PX(p->data[0], p->stride[0], x, y, hbd), U, V;
      u8 *q;
      if (ox + x < 0 || ox + x >= out->w) continue;
      q = o + (size_t)(ox + x) * 4;
      if (cv->mono || lay == DAV1D_PIXEL_LAYOUT_I400) { q[0] = q[1] = q[2] = clamp16(cv->ty[Y]); continue; }
      chroma_at(p, hbd, sx, sy, x, y, &U, &V);
      if (cv->identity) { q[0] = clamp16(cv->ty[V]); q[1] = clamp16(cv->ty[Y]); q[2] = clamp16(cv->ty[U]); continue; }
      {
        long yy = cv->ty[Y];
        q[0] = clamp16(yy + cv->tvr[V]);
        q[1] = clamp16(yy - cv->tug[U] - cv->tvg[V]);
        q[2] = clamp16(yy + cv->tub[U]);
      }
    }
  }
}

/* an alpha picture's luma into out's alpha at (ox, oy) */
static void put_alpha(const Dav1dPicture *p, Rgba *out, int ox, int oy)
{
  int hbd = p->p.bpc > 8, x, y, d = p->p.bpc;
  int full = p->seq_hdr ? p->seq_hdr->color_range : 1;
  long mx = (1L << d) - 1;
  for (y = 0; y < p->p.h; y++) {
    if (oy + y < 0 || oy + y >= out->h) continue;
    for (x = 0; x < p->p.w; x++) {
      long v = PX(p->data[0], p->stride[0], x, y, hbd), a;
      if (ox + x < 0 || ox + x >= out->w) continue;
      if (full) a = (v * 255 + mx / 2) / mx;
      else {
        long lo = 16L << (d - 8), span = 219L << (d - 8);
        a = ((v - lo) * 255 + span / 2) / span;
        if (a < 0) a = 0;
        if (a > 255) a = 255;
      }
      out->px[((size_t)(oy + y) * out->w + ox + x) * 4 + 3] = (u8)a;
    }
  }
}

/* matrix and range for item it, from its nclx colour box or else the decoded sequence header */
static void colour_of(const Avif *a, const Item *it, const Dav1dPicture *p, int *mc, int *full)
{
  const Prop *c = NULL;
  int i;
  for (i = 0; i < it->nprops; i++) {
    int k = it->props[i] - 1;
    if (k < a->nprops && !memcmp(a->props[k].type, "colr", 4) && a->props[k].len >= 11
        && !memcmp(a->props[k].p, "nclx", 4)) { c = &a->props[k]; break; }
  }
  if (c) { *mc = (int)be16(c->p + 8); *full = c->p[10] >> 7; return; }
  *mc = p->seq_hdr ? p->seq_hdr->mtrx : 2;
  *full = p->seq_hdr ? p->seq_hdr->color_range : 0;
}

/* ---------------------------------------------------------------- the picture */

/* an av01 item or a grid into out (allocated here at the item's size); alpha 255 */
static int decode_colour(const Avif *a, const Item *it, Rgba *out, char *err, size_t errcap)
{
  Dav1dPicture pic;
  Conv cv;
  int mc, full, ok = 0;
  if (!memcmp(it->type, "grid", 4)) {
    u8 *g;
    u32 gl = 0, ow, oh;
    int rows, cols, t, wide;
    i32 tx = 0, ty = 0, rowh = 0, x;
    g = item_bytes(a, it, &gl);
    if (!g) { codec_err(err, errcap, codec_oom ? "Not enough memory for this picture" : "Damaged AVIF file (grid missing)"); return 0; }
    if (gl < 8 || g[0] != 0) { free(g); codec_err(err, errcap, "Damaged AVIF file (bad grid)"); return 0; }
    wide = g[1] & 1;
    rows = g[2] + 1; cols = g[3] + 1;
    if (gl < (u32)(wide ? 12 : 8)) { free(g); codec_err(err, errcap, "Damaged AVIF file (bad grid)"); return 0; }
    ow = wide ? be32(g + 4) : be16(g + 4);
    oh = wide ? be32(g + 8) : be16(g + 6);
    free(g);
    if (it->ntiles != rows * cols) { codec_err(err, errcap, "Damaged AVIF file (grid tiles missing)"); return 0; }
    if (!ow || !oh || ow > MAXDIM || oh > MAXDIM || (double)ow * oh > CODEC_MAXPIXELS) { codec_err(err, errcap, "This AVIF picture is too big"); return 0; }
    if (!rgba_alloc(out, (i32)ow, (i32)oh)) { codec_err(err, errcap, "Not enough memory for a picture this size"); return 0; }
    for (t = 0; t < it->ntiles; t++) {
      Item *ti = item_get((Avif *)a, it->tiles[t]);
      if (progress(t, it->ntiles)) { if (t) conv_free(&cv); rgba_free(out); codec_err(err, errcap, "Opening the picture was stopped"); return 0; }   /* (0.8: Abort) */
      if (!ti || !decode_item(a, ti, &pic, err, errcap)) { if (!ti) codec_err(err, errcap, "Damaged AVIF file (grid tile missing)"); rgba_free(out); return 0; }
      if (t == 0) {
        colour_of(a, ti, &pic, &mc, &full);
        if (prop(a, it, "colr")) colour_of(a, it, &pic, &mc, &full);
        if (!conv_init(&cv, pic.p.bpc, mc, full, pic.p.layout == DAV1D_PIXEL_LAYOUT_I400)) {
          dav1d_picture_unref(&pic); rgba_free(out); codec_err(err, errcap, "Not enough memory for this picture"); return 0;
        }
      }
      if (t % cols == 0) { tx = 0; ty += t ? rowh : 0; rowh = pic.p.h; }
      put_rgb(&cv, &pic, out, tx, ty);
      tx += pic.p.w;
      dav1d_picture_unref(&pic);
    }
    conv_free(&cv);
    for (x = 0; x < out->w * out->h; x++) out->px[(size_t)x * 4 + 3] = 255;
    return 1;
  }
  if (!decode_item(a, it, &pic, err, errcap)) return 0;
  colour_of(a, it, &pic, &mc, &full);
  if ((double)pic.p.w * pic.p.h > CODEC_MAXPIXELS || pic.p.w > MAXDIM || pic.p.h > MAXDIM) codec_err(err, errcap, "This AVIF picture is too big");
  else if (!conv_init(&cv, pic.p.bpc, mc, full, pic.p.layout == DAV1D_PIXEL_LAYOUT_I400)) codec_err(err, errcap, "Not enough memory for this picture");
  else {
    if (!rgba_alloc(out, pic.p.w, pic.p.h)) codec_err(err, errcap, "Not enough memory for a picture this size");
    else {
      i32 i;
      put_rgb(&cv, &pic, out, 0, 0);
      for (i = 0; i < out->w * out->h; i++) out->px[(size_t)i * 4 + 3] = 255;
      ok = 1;
    }
    conv_free(&cv);
  }
  dav1d_picture_unref(&pic);
  return ok;
}

/* the alpha item (an av01 or a grid) into out's alpha channel */
static int decode_alpha(const Avif *a, const Item *it, Rgba *out, char *err, size_t errcap)
{
  Dav1dPicture pic;
  if (!memcmp(it->type, "grid", 4)) {
    int t, cols;
    u8 *g;
    u32 gl = 0;
    i32 tx = 0, ty = 0, rowh = 0;
    g = item_bytes(a, it, &gl);
    if (!g || gl < 8) { free(g); return 0; }
    cols = g[3] + 1;
    free(g);
    for (t = 0; t < it->ntiles; t++) {
      Item *ti = item_get((Avif *)a, it->tiles[t]);
      if (!ti || !decode_item(a, ti, &pic, err, errcap)) return 0;
      if (t % cols == 0) { tx = 0; ty += t ? rowh : 0; rowh = pic.p.h; }
      put_alpha(&pic, out, tx, ty);
      tx += pic.p.w;
      dav1d_picture_unref(&pic);
    }
    return 1;
  }
  if (!decode_item(a, it, &pic, err, errcap)) return 0;
  put_alpha(&pic, out, 0, 0);
  dav1d_picture_unref(&pic);
  return 1;
}

/* crop to the clean aperture (clap: fractions for width, height and the centre offset) */
static void apply_clap(const Prop *p, Rgba *im)
{
  long wn, wd, hn, hd, xn, xd, yn, yd;
  double cw, ch, cx, cy;
  i32 x0, y0, w, h, y;
  Rgba c;
  if (p->len < 32) return;
  wn = (long)be32(p->p); wd = (long)be32(p->p + 4);
  hn = (long)be32(p->p + 8); hd = (long)be32(p->p + 12);
  xn = (long)(i32)be32(p->p + 16); xd = (long)be32(p->p + 20);
  yn = (long)(i32)be32(p->p + 24); yd = (long)be32(p->p + 28);
  if (wd <= 0 || hd <= 0 || xd <= 0 || yd <= 0) return;
  cw = (double)wn / wd; ch = (double)hn / hd;
  cx = (double)xn / xd + (im->w - 1) / 2.0; cy = (double)yn / yd + (im->h - 1) / 2.0;
  w = (i32)(cw + 0.5); h = (i32)(ch + 0.5);
  x0 = (i32)(cx - (cw - 1) / 2.0 + 0.5); y0 = (i32)(cy - (ch - 1) / 2.0 + 0.5);
  if (w <= 0 || h <= 0 || x0 < 0 || y0 < 0 || x0 + w > im->w || y0 + h > im->h) return;
  if (w == im->w && h == im->h) return;
  if (!rgba_alloc(&c, w, h)) return;
  for (y = 0; y < h; y++) memcpy(c.px + (size_t)y * w * 4, im->px + ((size_t)(y0 + y) * im->w + x0) * 4, (size_t)w * 4);
  rgba_free(im);
  *im = c;
}

/* rotate anticlockwise by quarter turns (irot) */
static void apply_irot(int q, Rgba *im)
{
  Rgba r;
  i32 x, y, w = im->w, h = im->h;
  q &= 3;
  if (!q) return;
  if (!rgba_alloc(&r, q == 2 ? w : h, q == 2 ? h : w)) return;
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      i32 nx, ny;
      if (q == 1) { nx = y; ny = w - 1 - x; }
      else if (q == 2) { nx = w - 1 - x; ny = h - 1 - y; }
      else { nx = h - 1 - y; ny = x; }
      memcpy(r.px + ((size_t)ny * r.w + nx) * 4, im->px + ((size_t)y * w + x) * 4, 4);
    }
  rgba_free(im);
  *im = r;
}

/* imir: mode 0 swaps top and bottom, mode 1 left and right */
static void apply_imir(int mode, Rgba *im)
{
  i32 x, y;
  u8 t[4];
  if (mode == 0) {
    for (y = 0; y < im->h / 2; y++)
      for (x = 0; x < im->w * 4; x++) {
        u8 *p = im->px + (size_t)y * im->w * 4 + x, *q = im->px + (size_t)(im->h - 1 - y) * im->w * 4 + x;
        t[0] = *p; *p = *q; *q = t[0];
      }
  } else {
    for (y = 0; y < im->h; y++)
      for (x = 0; x < im->w / 2; x++) {
        u8 *p = im->px + ((size_t)y * im->w + x) * 4, *q = im->px + ((size_t)y * im->w + im->w - 1 - x) * 4;
        memcpy(t, p, 4); memcpy(p, q, 4); memcpy(q, t, 4);
      }
  }
}

int avif_detect(const u8 *d, size_t n)
{
  u32 sz, i;
  if (n < 16 || memcmp(d + 4, "ftyp", 4)) return 0;
  sz = be32(d);
  if (sz < 16 || sz > n) return 0;
  if (!memcmp(d + 8, "avif", 4) || !memcmp(d + 8, "avis", 4)) return 1;
  for (i = 16; i + 4 <= sz; i += 4)
    if (!memcmp(d + i, "avif", 4) || !memcmp(d + i, "avis", 4)) return 1;
  return 0;
}

int avif_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  Avif *a;
  const u8 *p = d, *end = d + n, *b, *mb = NULL;
  u32 bl, tot, ml = 0;
  char t[4];
  int i, ok = 0;
  Item *prim, *alpha = NULL;
  const Prop *pr;
  out->px = NULL; out->w = out->h = 0;
  if (!avif_detect(d, n)) { codec_err(err, errcap, "Not an AVIF file"); return 0; }
  while (p < end && box(p, end, t, &b, &bl, &tot)) {
    if (!memcmp(t, "meta", 4)) { mb = b; ml = bl; }
    p += tot;
  }
  if (!mb || ml < 4) { codec_err(err, errcap, "This AVIF file has no still picture (only animation)"); return 0; }
  a = calloc(1, sizeof *a);
  if (a) a->items = calloc(MAXITEMS, sizeof(Item));
  if (!a || !a->items) { free(a); codec_oom = 1; codec_err(err, errcap, "Not enough memory"); return 0; }
  a->d = d; a->n = n;
  p = mb + 4; end = mb + ml;
  while (p < end && box(p, end, t, &b, &bl, &tot)) {
    int good = 1;
    if (!memcmp(t, "pitm", 4)) {
      if (bl >= 6 && b[0] == 0) a->primary = be16(b + 4);
      else if (bl >= 8) a->primary = be32(b + 4);
    } else if (!memcmp(t, "iloc", 4)) good = parse_iloc(a, b, bl);
    else if (!memcmp(t, "iinf", 4)) good = parse_iinf(a, b, bl);
    else if (!memcmp(t, "iref", 4)) good = parse_iref(a, b, bl);
    else if (!memcmp(t, "iprp", 4)) good = parse_iprp(a, b, bl);
    else if (!memcmp(t, "idat", 4)) { a->idat = b; a->idatlen = bl; }
    if (!good) { codec_err(err, errcap, codec_oom ? "Not enough memory" : "Damaged AVIF file"); goto fail; }
    p += tot;
  }
  prim = item_get(a, a->primary);
  if (!prim) { codec_err(err, errcap, "Damaged AVIF file (no primary picture)"); goto fail; }
  for (i = 0; i < a->nitems; i++) {
    Item *it = &a->items[i];
    if (it->alpha_for == prim->id && aux_is_alpha(a, it)) { alpha = it; break; }
  }
  if (!decode_colour(a, prim, out, err, errcap)) goto fail;
  if (alpha) {
    if (!decode_alpha(a, alpha, out, err, errcap)) {
      if (codec_oom) { rgba_free(out); codec_err(err, errcap, "Not enough memory for this picture"); goto fail; }
      for (i = 0; i < out->w * out->h; i++) out->px[(size_t)i * 4 + 3] = 255;   /* keep the picture */
    } else if (alpha->prem) {
      for (i = 0; i < out->w * out->h; i++) {
        u8 *q = out->px + (size_t)i * 4;
        int k;
        if (q[3] == 0) { q[0] = q[1] = q[2] = 0; continue; }
        for (k = 0; k < 3; k++) { int v = (q[k] * 255 + q[3] / 2) / q[3]; q[k] = (u8)(v > 255 ? 255 : v); }
      }
    }
  }
  if ((pr = prop(a, prim, "clap")) != NULL) apply_clap(pr, out);
  if ((pr = prop(a, prim, "irot")) != NULL && pr->len >= 1) apply_irot(pr->p[0] & 3, out);
  if ((pr = prop(a, prim, "imir")) != NULL && pr->len >= 1) apply_imir(pr->p[0] & 1, out);
  ok = 1;
fail:
  for (i = 0; i < a->nitems; i++) free(a->items[i].tiles);
  free(a->items);
  free(a);
  return ok;
}
