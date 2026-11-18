/*
 * colorx.c - colour management on documents (see colorx.h).
 */
#include <stdlib.h>
#include <string.h>
#include "colorx.h"
#include "codec.h"
#include "palette.h"
#include "xwrite.h"
#include "zip.h"

IccProfile *colorx_doc_profile(const Doc *d)
{
  return d && d->icc ? icc_parse(d->icc, d->icclen, NULL, 0) : NULL;
}
/* the bytes of the profile chosen (malloc'd), or NULL for none */
static u8 *profile_bytes(int which, const u8 *bytes, u32 len, u32 *outlen, int *ok)
{
  u8 *b = NULL;
  *ok = 1; *outlen = 0;
  if (which == CX_NONE) return NULL;
  if (which == CX_BYTES) {
    if (!bytes || len < 132 || !(b = (u8 *)malloc(len))) { *ok = 0; return NULL; }
    memcpy(b, bytes, len); *outlen = len;
    return b;
  }
  if (!(b = icc_builtin_bytes(which, outlen))) *ok = 0;
  return b;
}
/* 1 if a profile suits the document: RGB for RGB, Indexed and Bitmap; grey or RGB for Grayscale */
static int suits(const Doc *d, const u8 *b, u32 len)
{
  IccProfile *p = icc_parse(b, len, NULL, 0);
  u32 s = p ? icc_space(p) : 0;
  icc_free(p);
  return s == ICC_RGB || (s == ICC_GRAY && d->mode == MODE_GRAY);
}
int colorx_assign(Doc *d, int which, const u8 *bytes, u32 len)
{
  u32 n;
  int ok;
  u8 *b = profile_bytes(which, bytes, len, &n, &ok);
  if (!d || !ok || (b && !suits(d, b, n))) { free(b); return 0; }
  doc_begin(d, which == CX_NONE ? "Don't Color Manage" : "Assign Profile");
  doc_note_pal(d);
  free(d->icc);
  d->icc = b; d->icclen = n;
  d->dirty = 1;
  doc_end(d);
  return 1;
}
static void xf_color(const CmsXform *x, u32 *c)
{
  u8 p[4];
  p[0] = (u8)(*c >> 16); p[1] = (u8)(*c >> 8); p[2] = (u8)*c; p[3] = 255;
  cms_xform_apply(x, p, 1);
  *c = (u32)p[0] << 16 | (u32)p[1] << 8 | p[2];
}
/* every pixel layer, fill layer and the palette through x (inside an open step that noted all) */
static void apply_all(Doc *d, const CmsXform *x)
{
  int i;
  long np = (long)d->w * d->h;
  for (i = 0; i < d->nlay; i++) {
    Layer *l = d->lay[i];
    long pp = progress_push(1000L * i / d->nlay, 1000L * (i + 1) / d->nlay);
    if (l->px) {
      long y;
      for (y = 0; y < d->h && !progress(y, d->h); y++) cms_xform_apply(x, l->px + y * (long)d->w * 4, d->w);
    }
    if (l->kind == LK_FILL) { xf_color(x, &l->fillp.c0); xf_color(x, &l->fillp.c1); }
    progress_pop(pp);
  }
  (void)np;
  if (d->mode == MODE_INDEXED) {
    int k;
    for (k = 0; k < d->npal; k++) {
      u32 c = (u32)d->pal[3 * k] << 16 | (u32)d->pal[3 * k + 1] << 8 | d->pal[3 * k + 2];
      xf_color(x, &c);
      d->pal[3 * k] = (u8)(c >> 16); d->pal[3 * k + 1] = (u8)(c >> 8); d->pal[3 * k + 2] = (u8)c;
    }
  }
  if (d->mode != MODE_RGB) doc_index_sync_all(d);
  xf_color(x, &d->fg); xf_color(x, &d->bg);
}
int colorx_convert(Doc *d, int which, const u8 *bytes, u32 len, int intent, int bpc)
{
  u32 n;
  int ok;
  u8 *b = profile_bytes(which, bytes, len, &n, &ok);
  IccProfile *src, *dst;
  CmsXform *x;
  if (!d || !ok || !b || !suits(d, b, n) || d->mode == MODE_GRAY) { free(b); return 0; }
  src = colorx_doc_profile(d);
  dst = icc_parse(b, n, NULL, 0);
  x = dst ? cms_xform_new(src, dst, intent, bpc) : NULL;
  if (!x) { icc_free(src); icc_free(dst); free(b); return 0; }
  doc_begin(d, "Convert to Profile");
  doc_note_all(d);
  apply_all(d, x);
  free(d->icc);
  d->icc = b; d->icclen = n;
  d->dirty = 1;
  doc_end(d);
  cms_xform_free(x); icc_free(src); icc_free(dst);
  return 1;
}

/* the document's colours brought into the CMYK gamut: through the separation and back */
typedef struct { CmsSep *s; const IccProfile *cmyk, *doc; } GamutMap;
int colorx_convert_model(Doc *d, int cm, const IccProfile *cmyk)
{
  if (!d || cm < CM_RGB || cm > CM_LAB) return 0;
  if (d->mode != MODE_RGB && !doc_to_rgb(d)) return 0;
  if (cm == CM_CMYK) {
    IccProfile *prof = colorx_doc_profile(d);
    CmsSep *s = cms_sep_new(prof, cmyk, CI_RELATIVE);
    int i;
    u8 *ink = (u8 *)malloc((size_t)d->w * 4);
    if (!s || !ink) { cms_sep_free(s); free(ink); icc_free(prof); return 0; }
    doc_begin(d, "CMYK Color");
    doc_note_all(d);
    for (i = 0; i < d->nlay; i++) {
      Layer *l = d->lay[i];
      long y, pp = progress_push(1000L * i / d->nlay, 1000L * (i + 1) / d->nlay);
      if (l->px)
        for (y = 0; y < d->h && !progress(y, d->h); y++) {
          u8 *row = l->px + y * (long)d->w * 4, *a = (u8 *)malloc((size_t)d->w);
          long x;
          if (!a) continue;
          for (x = 0; x < d->w; x++) a[x] = row[x * 4 + 3];
          cms_sep_apply(s, row, ink, d->w);
          cms_cmyk_to_rgb(cmyk, prof, ink, row, d->w);
          for (x = 0; x < d->w; x++) row[x * 4 + 3] = a[x];
          free(a);
        }
      progress_pop(pp);
    }
    d->cmode = CM_CMYK;
    d->dirty = 1;
    doc_end(d);
    cms_sep_free(s); free(ink); icc_free(prof);
    return 1;
  }
  doc_begin(d, cm == CM_LAB ? "Lab Color" : "RGB Color");
  doc_note_pal(d);
  d->cmode = (u8)cm;
  d->dirty = 1;
  doc_end(d);
  return 1;
}

/* ------------------------------------------------------------------ writing in the model */
static void w16(Buf *b, u32 v) { buf_addc(b, (int)(v >> 8 & 255)); buf_addc(b, (int)(v & 255)); }
static void w32(Buf *b, u32 v) { w16(b, v >> 16); w16(b, v & 0xFFFF); }
typedef struct { u16 tag, type; u32 count, val; } Ent;
/* a big-endian TIFF of one Deflate-compressed strip: photo 5 (CMYK) or 8 (CIELab), spp samples
   of 8 bits (the last alpha when extra) */
static u8 *tiff_model(const u8 *data, i32 w, i32 h, int spp, int photo, int extra, int dpi, size_t *len)
{
  Buf b;
  size_t zl;
  long rowb = (long)w * spp;
  u8 *pred = (u8 *)malloc((size_t)rowb * h), *z;
  Ent e[16];
  int ne = 0, k;
  u32 bitsat, resat, stripat, ifdat;
  if (!pred) return NULL;
  memcpy(pred, data, (size_t)rowb * h);
  if (!(z = zlib_compress(pred, (size_t)rowb * h, &zl))) { free(pred); return NULL; }
  free(pred);
  buf_init(&b);
  buf_add(&b, "MM\0*", 4); w32(&b, 0);
  stripat = (u32)b.len; buf_add(&b, z, zl); free(z);
  if (b.len & 1) buf_addc(&b, 0);
  bitsat = (u32)b.len; for (k = 0; k < spp; k++) w16(&b, 8);
  resat = (u32)b.len; w32(&b, (u32)(dpi > 0 ? dpi : 72)); w32(&b, 1);
  #define E(t, ty, c, v) do { e[ne].tag = (t); e[ne].type = (ty); e[ne].count = (c); e[ne].val = (v); ne++; } while (0)
  E(256, 4, 1, (u32)w); E(257, 4, 1, (u32)h);
  E(258, 3, (u32)spp, spp <= 2 ? 8 : bitsat);
  E(259, 3, 1, 8); E(262, 3, 1, (u32)photo); E(273, 4, 1, stripat); E(277, 3, 1, (u32)spp); E(278, 4, 1, (u32)h);
  E(279, 4, 1, (u32)zl); E(282, 5, 1, resat); E(283, 5, 1, resat); E(284, 3, 1, 1); E(296, 3, 1, 2);
  if (photo == 5) E(332, 3, 1, 1);
  if (extra) E(338, 3, 1, 2);
  ifdat = (u32)b.len;
  w16(&b, (u32)ne);
  for (k = 0; k < ne; k++) {
    w16(&b, e[k].tag); w16(&b, e[k].type); w32(&b, e[k].count);
    if (e[k].type == 3 && e[k].count == 1) { w16(&b, e[k].val); w16(&b, 0); } else w32(&b, e[k].val);
  }
  w32(&b, 0);
  if (b.oom) { buf_free(&b); return NULL; }
  b.data[4] = (char)(ifdat >> 24); b.data[5] = (char)(ifdat >> 16); b.data[6] = (char)(ifdat >> 8); b.data[7] = (char)ifdat;
  *len = b.len;
  return (u8 *)buf_take(&b);
}
u8 *colorx_write_model(Doc *d, int fmt, int quality, const IccProfile *cmyk, size_t *len)
{
  u8 *flat, *out = NULL, *buf;
  long k, np;
  int alpha = 0;
  IccProfile *prof;
  if (!d || d->mode != MODE_RGB || d->cmode == CM_RGB) return NULL;
  if (fmt != FMT_TIFF && !(fmt == FMT_JPEG && d->cmode == CM_CMYK)) return NULL;
  np = (long)d->w * d->h;
  if (!(flat = (u8 *)malloc((size_t)np * 4))) return NULL;
  doc_composite(d, doc_box(d), flat, (long)d->w * 4);
  for (k = 0; k < np; k++) if (flat[k * 4 + 3] != 255) { alpha = 1; break; }
  if (fmt == FMT_JPEG) alpha = 0;
  if (fmt == FMT_JPEG) for (k = 0; k < np; k++) {             /* (laid over white, as JPEG has no alpha) */
    u8 *p = flat + k * 4;
    int a = p[3], c;
    for (c = 0; c < 3; c++) p[c] = (u8)((p[c] * a + 255 * (255 - a) + 127) / 255);
    p[3] = 255;
  }
  prof = colorx_doc_profile(d);
  if (d->cmode == CM_CMYK) {
    CmsSep *s = cms_sep_new(prof, cmyk, CI_RELATIVE);
    int spp = alpha ? 5 : 4;
    if (s && (buf = (u8 *)malloc((size_t)np * (spp > 4 ? 5 : 4))) != NULL) {
      u8 *ink = (u8 *)malloc((size_t)np * 4);
      if (ink) {
        cms_sep_apply(s, flat, ink, np);
        if (fmt == FMT_JPEG) out = jpeg_encode_cmyk(ink, d->w, d->h, quality, d->dpi, len);
        else {
          for (k = 0; k < np; k++) { memcpy(buf + k * spp, ink + k * 4, 4); if (alpha) buf[k * spp + 4] = flat[k * 4 + 3]; }
          out = tiff_model(buf, d->w, d->h, spp, 5, alpha, d->dpi, len);
        }
        free(ink);
      }
      free(buf);
    }
    cms_sep_free(s);
    if (out && cmyk) {                                 /* the CMYK profile the separation was made for */
      u32 il;
      const u8 *ib = icc_bytes(cmyk, &il);
      if (ib) out = icc_embed(fmt, out, len, ib, il);
    }
  } else {
    int spp = alpha ? 4 : 3;
    if ((buf = (u8 *)malloc((size_t)np * 3)) != NULL) {
      cms_rgb_to_lab8(prof, flat, buf, np);
      if (alpha) {
        u8 *b4 = (u8 *)malloc((size_t)np * 4);
        if (b4) { for (k = 0; k < np; k++) { memcpy(b4 + k * 4, buf + k * 3, 3); b4[k * 4 + 3] = flat[k * 4 + 3]; } }
        free(buf); buf = b4;
      }
      if (buf) out = tiff_model(buf, d->w, d->h, spp, 8, alpha, d->dpi, len);
      free(buf);
    }
  }
  icc_free(prof);
  free(flat);
  return out;
}
u8 *colorx_embed(const Doc *d, int fmt, u8 *data, size_t *len)
{
  IccProfile *p;
  u32 s;
  if (!d || !data || !d->icc) return data;
  p = colorx_doc_profile(d);
  s = p ? icc_space(p) : 0;
  icc_free(p);
  if (s == ICC_GRAY ? d->mode != MODE_GRAY : s != ICC_RGB || d->mode == MODE_GRAY) return data;
  return icc_embed(fmt, data, len, d->icc, d->icclen);
}
u8 *colorx_write_doc(Doc *d, int fmt, size_t *len)
{
  u8 *data = colorx_write_model(d, fmt, 90, NULL, len);
  if (data) return data;
  switch (fmt) {
  case FMT_PNG: data = png_write_doc(d, len); break;
  case FMT_JPEG: data = jpeg_write_doc(d, 90, len); break;
  case FMT_TIFF: data = tiff_encode_doc(d, TIFFC_LZW, len); break;
  case FMT_PSD: data = psd_write_doc(d, len); break;
  case FMT_WEBP: data = xwrite_doc(d, FMT_WEBP, 0, 0, len); break;
  default: return NULL;
  }
  return colorx_embed(d, fmt, data, len);
}

/* ------------------------------------------------------------------ palette BMP */
static void l16(Buf *b, u32 v) { buf_addc(b, (int)(v & 255)); buf_addc(b, (int)(v >> 8 & 255)); }
static void l32(Buf *b, u32 v) { l16(b, v & 0xFFFF); l16(b, v >> 16); }
u8 *colorx_bmp_indexed(Doc *d, size_t *len)
{
  u8 *idx, *al = NULL;
  int bpp, n, k;
  u32 stride, size, x;
  i32 y;
  Buf b;
  if (!d) return NULL;
  if (d->mode != MODE_INDEXED && d->mode != MODE_BITMAP) {
    Rgba im;
    u8 *r;
    if (!rgba_alloc(&im, d->w, d->h)) return NULL;
    doc_composite(d, doc_box(d), im.px, (long)d->w * 4);
    r = bmp_encode(&im, len);
    rgba_free(&im);
    return r;
  }
  if (!(idx = doc_flat_indices(d, &al))) return NULL;
  free(al);                                            /* (BMP palettes have no transparency) */
  n = d->npal < 2 ? 2 : d->npal;
  bpp = n <= 2 ? 1 : n <= 16 ? 4 : 8;
  stride = (((u32)d->w * bpp + 31) / 32) * 4;
  size = stride * (u32)d->h;
  buf_init(&b);
  buf_add(&b, "BM", 2); l32(&b, 14 + 40 + 4 * (u32)n + size); l32(&b, 0); l32(&b, 14 + 40 + 4 * (u32)n);
  l32(&b, 40); l32(&b, (u32)d->w); l32(&b, (u32)d->h); l16(&b, 1); l16(&b, (u32)bpp);
  l32(&b, 0); l32(&b, size);
  { u32 ppm = (u32)(d->dpi * 10000UL / 254); l32(&b, ppm); l32(&b, ppm); }
  l32(&b, (u32)n); l32(&b, 0);
  for (k = 0; k < n; k++) { buf_addc(&b, d->pal[3 * k + 2]); buf_addc(&b, d->pal[3 * k + 1]); buf_addc(&b, d->pal[3 * k]); buf_addc(&b, 0); }
  for (y = d->h - 1; y >= 0; y--) {
    const u8 *s = idx + (size_t)y * d->w;
    u32 acc = 0, nb = 0, written = 0;
    for (x = 0; x < (u32)d->w; x++) {
      acc = (acc << bpp) | (s[x] & ((1u << bpp) - 1)); nb += (u32)bpp;
      if (nb == 8) { buf_addc(&b, (int)acc); acc = 0; nb = 0; written++; }
    }
    if (nb) { buf_addc(&b, (int)(acc << (8 - nb))); written++; }
    for (; written < stride; written++) buf_addc(&b, 0);
  }
  free(idx);
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return (u8 *)buf_take(&b);
}
