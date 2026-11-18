/*
 * xcf.c - Easel round 3: GIMP's .xcf documents with their layers (foreign.h). Written from the
 * public XCF description (developer.gimp.org/core/standards/xcf); no GIMP code. Every pointer,
 * length and count is checked against the file.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "foreign.h"

#define XCF_TILE 64
#define XCF_MAXITEMS 1024

typedef struct {
  const u8 *d; size_t n;
  int ver, ptr8;                      /* version, 64-bit pointers (version 11 and later) */
  int base;                           /* 0 RGB, 1 grey, 2 indexed */
  int bpc, fl, linear;                /* bytes a component, 1 float, linear light */
  int comp;                           /* 0 none, 1 RLE, 2 zlib */
  u8 cmap[256 * 3]; int ncmap;
  i32 w, h;
  char *err; size_t errcap;
} Xcf;

static u32 rd32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static int fail(Xcf *x, const char *m) { codec_err(x->err, x->errcap, m); return 0; }

/* a pointer at *pos (4 or 8 bytes); 0 if past the end. Pointers beyond the file come back as such
   and are refused where used */
static size_t rdptr(Xcf *x, size_t *pos)
{
  size_t v;
  if (x->ptr8) {
    if (x->n - *pos < 8 || *pos > x->n) { *pos = x->n; return 0; }
    if (rd32(x->d + *pos)) v = (size_t)-1; else v = rd32(x->d + *pos + 4);
    *pos += 8;
  } else {
    if (*pos > x->n || x->n - *pos < 4) { *pos = x->n; return 0; }
    v = rd32(x->d + *pos);
    *pos += 4;
  }
  return v;
}
/* a string (u32 length with its NUL, then the bytes) into out (cap); 0 if it runs off the file */
static int rdstr(Xcf *x, size_t *pos, char *out, size_t cap)
{
  u32 l;
  if (*pos > x->n || x->n - *pos < 4) return 0;
  l = rd32(x->d + *pos); *pos += 4;
  if (l > x->n - *pos) return 0;
  if (out && cap) {
    size_t k = l ? l - 1 : 0;
    if (k > cap - 1) k = cap - 1;
    memcpy(out, x->d + *pos, k); out[k] = 0;
  }
  *pos += l;
  return 1;
}

/* one component (bpc bytes, big-endian) as 0..255 */
static u8 comp8(const Xcf *x, const u8 *p, int colour)
{
  int linear = x->linear && colour;
  double v;
  if (!x->fl) {
    if (x->bpc == 1) { if (!linear) return p[0]; v = p[0] / 255.0; }
    else if (x->bpc == 2) { unsigned s = (p[0] << 8) | p[1]; if (!linear) return (u8)((s * 255UL + 32767) / 65535); v = s / 65535.0; }
    else { u32 s = rd32(p); if (!linear) return (u8)(s >> 24); v = s / 4294967295.0; }
  } else if (x->bpc == 2) v = half_to_double((p[0] << 8) | p[1]);
  else if (x->bpc == 4) { u32 u = rd32(p); float f; memcpy(&f, &u, 4); v = f; }
  else {
    /* a double from its big-endian bytes, built from the two halves so it works on any machine */
    u32 hi = rd32(p), lo = rd32(p + 4);
    int e = (int)((hi >> 20) & 2047);
    double m = (double)(hi & 0xFFFFF) * 4294967296.0 + lo;
    if (e == 0) v = 0;
    else if (e == 2047) v = 1;
    else v = ldexp(1 + m / 4503599627370496.0, e - 1023);
    if (hi & 0x80000000UL) v = -v;
  }
  return linear ? lin_to_srgb8(v) : unit_to8(v);
}

/* a hierarchy (at ptr) of a drawable with ch channels, w x h: its pixels, 8 bits a channel, into
   out (w * h * ch). 0 on a damaged file */
static int read_hierarchy(Xcf *x, size_t ptr, int ch, int ncol, i32 w, i32 h, u8 *out)
{
  size_t pos = ptr, lev, tp;
  u32 hw, hh, bpp;
  i32 lw, lh, tx, ty, ntx, nty;
  u8 *tile = NULL;
  long t;
  if (ptr >= x->n || x->n - ptr < 12) return fail(x, "Damaged GIMP file (a layer's pixels are missing)");
  hw = rd32(x->d + pos); hh = rd32(x->d + pos + 4); bpp = rd32(x->d + pos + 8); pos += 12;
  if ((i32)hw != w || (i32)hh != h || bpp != (u32)(ch * x->bpc)) return fail(x, "Damaged GIMP file (a layer's size or depth doesn't match)");
  lev = rdptr(x, &pos);
  if (!lev || lev >= x->n || x->n - lev < 8) return fail(x, "Damaged GIMP file (a layer's pixels are missing)");
  pos = lev;
  lw = (i32)rd32(x->d + pos); lh = (i32)rd32(x->d + pos + 4); pos += 8;
  if (lw != w || lh != h) return fail(x, "Damaged GIMP file (a level's size doesn't match)");
  ntx = (w + XCF_TILE - 1) / XCF_TILE; nty = (h + XCF_TILE - 1) / XCF_TILE;
  if (!(tile = (u8 *)malloc((size_t)XCF_TILE * XCF_TILE * bpp))) { codec_oom = 1; return fail(x, "Not enough memory for the GIMP file"); }
  for (t = 0; t < (long)ntx * nty; t++) {
    i32 x0, y0, tw, th, px, py;
    long np, k;
    int c;
    tp = rdptr(x, &pos);
    if (!tp || tp >= x->n) { free(tile); return fail(x, "Damaged GIMP file (a tile is missing)"); }
    tx = (i32)(t % ntx); ty = (i32)(t / ntx);
    x0 = tx * XCF_TILE; y0 = ty * XCF_TILE;
    tw = w - x0 < XCF_TILE ? w - x0 : XCF_TILE; th = h - y0 < XCF_TILE ? h - y0 : XCF_TILE;
    np = (long)tw * th;
    if (x->comp == 0) {
      if ((size_t)np * bpp > x->n - tp) { free(tile); return fail(x, "Damaged GIMP file (a tile runs off the end)"); }
      memcpy(tile, x->d + tp, (size_t)np * bpp);
    } else if (x->comp == 1) {
      /* RLE: one stream per byte of a pixel */
      size_t ip = tp;
      for (c = 0; c < (int)bpp; c++) {
        long o = 0;
        while (o < np) {
          unsigned v, len;
          if (ip >= x->n) goto bad;
          v = x->d[ip++];
          if (v >= 128) {
            if (v == 128) { if (x->n - ip < 2) goto bad; len = (x->d[ip] << 8) | x->d[ip + 1]; ip += 2; }
            else len = 256 - v;
            if ((long)len > np - o || len > x->n - ip) goto bad;
            for (k = 0; k < (long)len; k++) tile[(o + k) * bpp + c] = x->d[ip + k];
            ip += len; o += len;
          } else {
            u8 b;
            if (v == 127) { if (x->n - ip < 2) goto bad; len = (x->d[ip] << 8) | x->d[ip + 1]; ip += 2; }
            else len = v + 1;
            if (ip >= x->n || (long)len > np - o) goto bad;
            b = x->d[ip++];
            for (k = 0; k < (long)len; k++) tile[(o + k) * bpp + c] = b;
            o += len;
          }
        }
      }
    } else {
      if (zlib_decompress(x->d + tp, x->n - tp, tile, (size_t)np * bpp) < 0) goto bad;
    }
    for (py = 0; py < th; py++)
      for (px = 0; px < tw; px++) {
        const u8 *s = tile + ((long)py * tw + px) * bpp;
        u8 *dd = out + (((long)(y0 + py)) * w + x0 + px) * ch;
        for (c = 0; c < ch; c++) dd[c] = (x->base == 2 && c == 0) ? s[0] : comp8(x, s + c * x->bpc, c < ncol);
      }
    if (progress(t, (long)ntx * nty)) { free(tile); return 0; }
  }
  free(tile);
  return 1;
bad:
  free(tile);
  return fail(x, "Damaged GIMP file (a tile's data is damaged)");
}

static int gimp_mode(u32 m, int group)
{
  static const u8 map[62] = {
    BM_NORMAL, BM_DISSOLVE, BM_NORMAL, BM_MULTIPLY, BM_SCREEN, BM_OVERLAY, BM_DIFFERENCE, BM_ADD, BM_SUBTRACT,
    BM_DARKEN, BM_LIGHTEN, BM_HUE, BM_SATURATION, BM_COLOR, BM_LUMINOSITY, BM_DIVIDE, BM_DODGE, BM_BURN,
    BM_HARDLIGHT, BM_SOFTLIGHT, BM_NORMAL, BM_NORMAL, BM_NORMAL,
    /* 23: the 2.10 set */
    BM_OVERLAY, BM_HUE, BM_SATURATION, BM_COLOR, BM_LUMINOSITY, BM_NORMAL, BM_NORMAL, BM_MULTIPLY, BM_SCREEN,
    BM_DIFFERENCE, BM_ADD, BM_SUBTRACT, BM_DARKEN, BM_LIGHTEN, BM_HUE, BM_SATURATION, BM_COLOR, BM_LUMINOSITY,
    BM_DIVIDE, BM_DODGE, BM_BURN, BM_HARDLIGHT, BM_SOFTLIGHT, BM_NORMAL, BM_NORMAL, BM_VIVIDLIGHT, BM_PINLIGHT,
    BM_LINEARLIGHT, BM_HARDMIX, BM_EXCLUSION, BM_LINEARBURN, BM_DARKERCOLOR, BM_LIGHTERCOLOR, BM_LUMINOSITY,
    BM_NORMAL, BM_NORMAL, BM_NORMAL, BM_NORMAL, BM_PASS };
  if (m == 61) return group ? BM_PASS : BM_NORMAL;
  return m < 61 ? map[m] : BM_NORMAL;
}

/* the properties of an item at *pos, until PROP_END */
typedef struct {
  int opacity, visible, mode, lock_alpha, lock_content, lock_pos, group, depth, tag, apply_mask, selection, expanded;
  i32 ox, oy;
} Props;
static int read_props(Xcf *x, size_t *pos, Props *p, Doc *doc)
{
  int guard = 0;
  for (;;) {
    u32 type, len;
    size_t at;
    if (x->n - *pos < 8 || *pos > x->n) return fail(x, "Damaged GIMP file (its properties run off the end)");
    type = rd32(x->d + *pos); len = rd32(x->d + *pos + 4); *pos += 8;
    at = *pos;
    if (type == 0) return 1;
    /* (the colormap's length is worked out, not trusted: old GIMPs wrote it wrong) */
    if (type == 1 && x->n - at >= 4) { u32 nc = rd32(x->d + at); if (nc <= 256) len = 4 + nc * 3; }
    if (len > x->n - at) return fail(x, "Damaged GIMP file (a property runs off the end)");
    switch (type) {
    case 1: {                                            /* COLORMAP */
      u32 nc = rd32(x->d + at);
      if (nc <= 256) { x->ncmap = (int)nc; memcpy(x->cmap, x->d + at + 4, nc * 3); }
      break; }
    case 4: p->selection = 1; break;                     /* SELECTION */
    case 6: if (len >= 4) { u32 o = rd32(x->d + at); p->opacity = o > 255 ? 255 : (int)o; } break;
    case 7: if (len >= 4) p->mode = (int)rd32(x->d + at); break;
    case 8: if (len >= 4) p->visible = rd32(x->d + at) != 0; break;
    case 10: if (len >= 4) p->lock_alpha = rd32(x->d + at) != 0; break;
    case 11: if (len >= 4) p->apply_mask = rd32(x->d + at) != 0; break;
    case 15: if (len >= 8) { p->ox = (i32)rd32(x->d + at); p->oy = (i32)rd32(x->d + at + 4); } break;
    case 17: if (len >= 1) x->comp = x->d[at]; break;   /* COMPRESSION */
    case 19:                                             /* RESOLUTION (floats, dots per inch) */
      if (len >= 8 && doc) { u32 u = rd32(x->d + at); float f; memcpy(&f, &u, 4); if (f >= 1 && f < 65536) doc->dpi = (u16)(f + 0.5f); }
      break;
    case 28: if (len >= 4) p->lock_content = rd32(x->d + at) != 0; break;
    case 29: p->group = 1; break;                        /* GROUP_ITEM */
    case 30: p->depth = (int)(len / 4) - 1; break;       /* ITEM_PATH: one index per level */
    case 31: if (len >= 4) p->expanded = (rd32(x->d + at) & 1) != 0; break;
    case 32: if (len >= 4) p->lock_pos = rd32(x->d + at) != 0; break;
    case 33: if (len >= 4) { u32 u = rd32(x->d + at); float f; memcpy(&f, &u, 4); p->opacity = f <= 0 ? 0 : f >= 1 ? 255 : (int)(f * 255 + 0.5f); } break;
    case 34: if (len >= 4) p->tag = (int)rd32(x->d + at); break;
    case 21:                                             /* PARASITES: the icc-profile */
      if (doc && !doc->icc) {
        size_t q = at, e = at + len;
        while (e - q >= 4) {
          char name[64];
          u32 fl, sz;
          size_t save = x->n;
          x->n = e;
          if (!rdstr(x, &q, name, sizeof name)) { x->n = save; break; }
          x->n = save;
          if (e - q < 8) break;
          fl = rd32(x->d + q); sz = rd32(x->d + q + 4); q += 8; (void)fl;
          if (sz > e - q) break;
          if (!strcmp(name, "icc-profile") && sz >= 128 && (doc->icc = (u8 *)malloc(sz)) != NULL) { memcpy(doc->icc, x->d + q, sz); doc->icclen = sz; }
          q += sz;
        }
      }
      break;
    default: break;
    }
    *pos = at + len;
    if (++guard > 100000) return fail(x, "Damaged GIMP file (too many properties)");
  }
}

static void props_default(Props *p) { memset(p, 0, sizeof *p); p->opacity = 255; p->visible = 1; p->apply_mask = 1; p->expanded = 1; p->mode = 28; }

/* GIMP's colour tags (none, blue, green, yellow, orange, brown, red, violet, grey) as Easel's labels */
static u8 gimp_tag(int t)
{
  static const u8 m[9] = { 0, 5, 4, 3, 2, 2, 1, 6, 7 };
  return t >= 0 && t < 9 ? m[t] : 0;
}

/* one layer (at ptr) into l; 1 if it was read */
static int read_layer(Xcf *x, Doc *doc, size_t ptr, Layer **out, int *depth)
{
  size_t pos = ptr, hier, mptr;
  u32 lw, lh, type;
  int ch;
  Props p;
  char name[64];
  Layer *l;
  u8 *buf;
  i32 X, Y;
  *out = NULL;
  if (ptr >= x->n || x->n - ptr < 12) return fail(x, "Damaged GIMP file (a layer is missing)");
  lw = rd32(x->d + pos); lh = rd32(x->d + pos + 4); type = rd32(x->d + pos + 8); pos += 12;
  if (!rdstr(x, &pos, name, sizeof name)) return fail(x, "Damaged GIMP file (a layer's name)");
  props_default(&p);
  if (!read_props(x, &pos, &p, NULL)) return 0;
  hier = rdptr(x, &pos);
  mptr = rdptr(x, &pos);
  *depth = p.depth < 0 ? 0 : p.depth > 60 ? 60 : p.depth;
  if (p.group) {
    if (!(l = (Layer *)calloc(1, sizeof *l))) { codec_oom = 1; return fail(x, "Not enough memory for the GIMP file's layers"); }
    l->kind = LK_GROUP;
    l->collapsed = (u8)!p.expanded;
  } else {
    if (type > 5 || lw < 1 || lh < 1 || lw > 3 * MAXDIM || lh > 3 * MAXDIM) return fail(x, "Damaged GIMP file (a layer's size or kind)");
    if (!(l = layer_new(doc, name))) { codec_oom = 1; return fail(x, "Not enough memory for the GIMP file's layers"); }
  }
  strncpy(l->name, name[0] ? name : (p.group ? "Group" : "Layer"), sizeof l->name - 1);
  l->opacity = (u8)p.opacity; l->visible = (u8)p.visible;
  l->blend = (u8)gimp_mode((u32)p.mode, p.group);
  l->alpha_lock = (u8)p.lock_alpha; l->locked = (u8)p.lock_content; l->lock_pos = (u8)p.lock_pos;
  l->label = gimp_tag(p.tag);
  *out = l;
  if (p.group) return 1;
  ch = (int)(type / 2 == 0 ? 3 : type / 2 == 1 ? 1 : 1) + (int)(type & 1);
  if ((long)lw * lh > CODEC_MAXPIXELS) return fail(x, "The GIMP file's layer is too big");
  if (!(buf = (u8 *)malloc((size_t)lw * lh * ch))) { codec_oom = 1; return fail(x, "Not enough memory for the GIMP file's layers"); }
  if (!read_hierarchy(x, hier, ch, ch - (int)(type & 1), (i32)lw, (i32)lh, buf)) { free(buf); return 0; }
  for (Y = 0; Y < (i32)lh; Y++) {
    i32 cy = Y + p.oy;
    if (cy < 0 || cy >= doc->h) continue;
    for (X = 0; X < (i32)lw; X++) {
      i32 cx = X + p.ox;
      const u8 *s = buf + ((long)Y * lw + X) * ch;
      u8 *o;
      if (cx < 0 || cx >= doc->w) continue;
      o = l->px + ((long)cy * doc->w + cx) * 4;
      if (type / 2 == 0) { o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; }
      else if (type / 2 == 1) o[0] = o[1] = o[2] = s[0];
      else { int k = s[0] < x->ncmap ? s[0] : 0; o[0] = x->cmap[k * 3]; o[1] = x->cmap[k * 3 + 1]; o[2] = x->cmap[k * 3 + 2]; }
      o[3] = (type & 1) ? s[ch - 1] : 255;
    }
  }
  free(buf);
  /* the layer mask: a channel the layer's size */
  if (mptr) {
    size_t q = mptr, mh;
    u32 mw, mhh;
    Props mp;
    if (mptr >= x->n || x->n - mptr < 8) return fail(x, "Damaged GIMP file (a layer mask is missing)");
    mw = rd32(x->d + q); mhh = rd32(x->d + q + 4); q += 8;
    if (!rdstr(x, &q, NULL, 0)) return fail(x, "Damaged GIMP file (a mask's name)");
    props_default(&mp);
    if (!read_props(x, &q, &mp, NULL)) return 0;
    mh = rdptr(x, &q);
    if (mw != lw || mhh != lh) return fail(x, "Damaged GIMP file (a mask's size doesn't match its layer)");
    if (!(buf = (u8 *)malloc((size_t)lw * lh)) || !(l->mask = (u8 *)malloc((size_t)doc->w * doc->h))) {
      free(buf); codec_oom = 1; return fail(x, "Not enough memory for the GIMP file's layers");
    }
    {
      int saved = x->base;
      x->base = 0;                                       /* (a mask is grey, whatever the picture) */
      if (!read_hierarchy(x, mh, 1, 0, (i32)lw, (i32)lh, buf)) { x->base = saved; free(buf); return 0; }
      x->base = saved;
    }
    /* outside the layer, GIMP's mask hides (the layer has nothing there anyway) */
    memset(l->mask, 0, (size_t)doc->w * doc->h);
    for (Y = 0; Y < (i32)lh; Y++) {
      i32 cy = Y + p.oy;
      if (cy < 0 || cy >= doc->h) continue;
      for (X = 0; X < (i32)lw; X++) {
        i32 cx = X + p.ox;
        if (cx >= 0 && cx < doc->w) l->mask[(long)cy * doc->w + cx] = buf[(long)Y * lw + X];
      }
    }
    free(buf);
    l->mask_on = (u8)p.apply_mask;
    l->mask_linked = 1;
  }
  return 1;
}

/* a channel (at ptr): an alpha channel of the document, or (PROP_SELECTION) its selection */
static int read_channel(Xcf *x, Doc *doc, size_t ptr)
{
  size_t pos = ptr, hier;
  u32 cw, chh;
  char name[64];
  Props p;
  u8 *buf;
  int saved = x->base;
  if (ptr >= x->n || x->n - ptr < 8) return fail(x, "Damaged GIMP file (a channel is missing)");
  cw = rd32(x->d + pos); chh = rd32(x->d + pos + 4); pos += 8;
  if (!rdstr(x, &pos, name, sizeof name)) return fail(x, "Damaged GIMP file (a channel's name)");
  props_default(&p);
  if (!read_props(x, &pos, &p, NULL)) return 0;
  hier = rdptr(x, &pos);
  if ((i32)cw != doc->w || (i32)chh != doc->h) return 1;      /* (not the canvas's size: skipped) */
  if (!p.selection && doc->nchan >= MAXCHANNELS) return 1;
  if (!(buf = (u8 *)malloc((size_t)cw * chh))) { codec_oom = 1; return fail(x, "Not enough memory for the GIMP file's channels"); }
  x->base = 0;
  if (!read_hierarchy(x, hier, 1, 0, (i32)cw, (i32)chh, buf)) { x->base = saved; free(buf); return 0; }
  x->base = saved;
  if (p.selection) {
    /* the selection, with its box (none when nothing is selected) */
    Box b = box_make(doc->w, doc->h, 0, 0);
    i32 x, y;
    for (y = 0; y < doc->h; y++) for (x = 0; x < doc->w; x++) if (buf[(long)y * doc->w + x]) {
      if (x < b.x0) b.x0 = x;
      if (x >= b.x1) b.x1 = x + 1;
      if (y < b.y0) b.y0 = y;
      if (y >= b.y1) b.y1 = y + 1;
    }
    free(doc->sel);
    if (box_empty(b)) { free(buf); doc->sel = NULL; doc->selbox = doc_box(doc); }
    else { doc->sel = buf; doc->selbox = b; }
    return 1;
  }
  doc->chan[doc->nchan] = buf;
  strncpy(doc->chan_name[doc->nchan], name[0] ? name : "Alpha", sizeof doc->chan_name[0] - 1);
  doc->nchan++;
  return 1;
}

Doc *xcf_read_doc(const u8 *d, size_t n, char *err, size_t errcap)
{
  Xcf x;
  size_t pos, lp[XCF_MAXITEMS], cp[XCF_MAXITEMS];
  int nl = 0, nc = 0, i, prec;
  Props ip;
  Doc *doc = NULL;
  memset(&x, 0, sizeof x);
  x.d = d; x.n = n; x.err = err; x.errcap = errcap;
  codec_oom = 0;
  if (n < 30 || memcmp(d, "gimp xcf ", 9)) { fail(&x, "Not a GIMP file"); return NULL; }
  if (!memcmp(d + 9, "file", 4)) x.ver = 0;
  else if (d[9] == 'v' && d[10] >= '0' && d[10] <= '9' && d[11] >= '0' && d[11] <= '9' && d[12] >= '0' && d[12] <= '9')
    x.ver = (d[10] - '0') * 100 + (d[11] - '0') * 10 + (d[12] - '0');
  else { fail(&x, "Not a GIMP file"); return NULL; }
  if (x.ver > 25) { fail(&x, "A GIMP file from a newer GIMP than Easel knows"); return NULL; }
  x.ptr8 = x.ver >= 11;
  pos = 14;
  x.w = (i32)rd32(d + pos); x.h = (i32)rd32(d + pos + 4); x.base = (int)rd32(d + pos + 8); pos += 12;
  if (x.w < 1 || x.h < 1 || x.w > MAXDIM || x.h > MAXDIM || (long)x.w * x.h > CODEC_MAXPIXELS) { fail(&x, "The GIMP picture is too big for Easel"); return NULL; }
  if (x.base > 2) { fail(&x, "Damaged GIMP file (an unknown colour mode)"); return NULL; }
  x.bpc = 1;
  if (x.ver >= 4) {
    if (n - pos < 4) { fail(&x, "Damaged GIMP file"); return NULL; }
    prec = (int)rd32(d + pos); pos += 4;
    if (x.ver == 4) {
      static const signed char b4[5] = { 1, 2, 4, 2, 4 }, f4[5] = { 0, 0, 0, 1, 1 }, l4[5] = { 0, 0, 1, 1, 1 };
      if (prec < 0 || prec > 4) { fail(&x, "Damaged GIMP file (an unknown precision)"); return NULL; }
      x.bpc = b4[prec]; x.fl = f4[prec]; x.linear = l4[prec];
    } else {
      int k = prec / 100, g = (prec % 100) == 50;
      if (prec % 50 || k < 1) { fail(&x, "Damaged GIMP file (an unknown precision)"); return NULL; }
      x.linear = !g;
      if (k == 1) x.bpc = 1;
      else if (k == 2) x.bpc = 2;
      else if (k == 3) x.bpc = 4;
      else if (x.ver <= 6 && k == 4) { x.bpc = 2; x.fl = 1; }
      else if (x.ver <= 6 && k == 5) { x.bpc = 4; x.fl = 1; }
      else if (x.ver >= 7 && k == 5) { x.bpc = 2; x.fl = 1; }
      else if (x.ver >= 7 && k == 6) { x.bpc = 4; x.fl = 1; }
      else if (x.ver >= 7 && k == 7) { x.bpc = 8; x.fl = 1; }
      else { fail(&x, "Damaged GIMP file (an unknown precision)"); return NULL; }
    }
  }
  if (x.base == 2 && x.bpc != 1) { fail(&x, "Damaged GIMP file (an indexed picture deeper than 8 bits)"); return NULL; }
  if (!(doc = doc_new(x.w, x.h, 0, 1))) { codec_oom = 1; fail(&x, "Not enough memory for the GIMP picture"); return NULL; }
  layer_free(doc->lay[0]);
  doc->nlay = 0;
  props_default(&ip);
  if (!read_props(&x, &pos, &ip, doc)) goto bad;
  if (x.comp > 2) { fail(&x, "The GIMP file uses a compression Easel doesn't know"); goto bad; }
  while (nl < XCF_MAXITEMS) { size_t p = rdptr(&x, &pos); if (!p) break; lp[nl++] = p; }
  if (nl == XCF_MAXITEMS) while (rdptr(&x, &pos)) ;
  while (nc < XCF_MAXITEMS) { size_t p = rdptr(&x, &pos); if (!p) break; cp[nc++] = p; }
  if (!nl) { fail(&x, "The GIMP file has no layers"); goto bad; }
  /* the file lists the layers top first, each group before its members: Easel keeps them bottom
     first with each group above its members, so the list is taken backwards */
  for (i = nl - 1; i >= 0; i--) {
    Layer *l;
    int depth;
    if (doc->nlay >= MAXLAYERS) break;
    if (!read_layer(&x, doc, lp[i], &l, &depth)) { layer_free(l); goto bad; }
    l->depth = (u8)depth;
    doc->lay[doc->nlay++] = l;
    if (progress_aborted) goto bad;
  }
  for (i = 0; i < nc; i++) if (!read_channel(&x, doc, cp[i])) goto bad;
  doc->active = doc->nlay - 1;
  if (x.base == 1 && doc->icc) { free(doc->icc); doc->icc = NULL; doc->icclen = 0; }   /* (a grey profile on RGB layers) */
  return doc;
bad:
  doc_free(doc);
  return NULL;
}
