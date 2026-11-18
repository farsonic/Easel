/*
 * filesx.c - Easel round 3: EXIF kept from file to file (filesx.h).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "filesx.h"
#include "zip.h"

static u32 g16(const u8 *p, int le) { return le ? (u32)p[0] | ((u32)p[1] << 8) : ((u32)p[0] << 8) | p[1]; }
static u32 g32(const u8 *p, int le) { return le ? g16(p, 1) | (g16(p + 2, 1) << 16) : (g16(p, 0) << 16) | g16(p + 2, 0); }
static void p16(u8 *p, int le, u32 v) { if (le) { p[0] = (u8)v; p[1] = (u8)(v >> 8); } else { p[0] = (u8)(v >> 8); p[1] = (u8)v; } }
static void p32(u8 *p, int le, u32 v) { if (le) { p16(p, 1, v & 0xFFFF); p16(p + 2, 1, v >> 16); } else { p16(p, 0, v >> 16); p16(p + 2, 0, v & 0xFFFF); } }

static u8 *dup_block(const u8 *p, u32 n)
{
  u8 *e = (u8 *)malloc(n ? n : 1);
  if (e) memcpy(e, p, n);
  return e;
}
u8 *exif_from_file(const u8 *d, size_t n, u32 *len)
{
  static const u8 pngsig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
  size_t pos;
  if (n >= 4 && d[0] == 0xFF && d[1] == 0xD8) {
    pos = 2;
    while (pos + 4 <= n && d[pos] == 0xFF) {
      int m = d[pos + 1];
      u32 l = ((u32)d[pos + 2] << 8) | d[pos + 3];
      if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) { pos += 2; continue; }
      if (m == 0xDA || m == 0xD9 || l < 2 || l > n - pos - 2) break;
      if (m == 0xE1 && l >= 16 && !memcmp(d + pos + 4, "Exif\0", 5)) { *len = l - 8; return dup_block(d + pos + 10, l - 8); }
      pos += 2 + l;
    }
    return NULL;
  }
  if (n >= 8 && !memcmp(d, pngsig, 8)) {
    pos = 8;
    while (pos + 12 <= n) {
      u32 l = g32(d + pos, 0);
      if (l > n - pos - 12) break;
      if (!memcmp(d + pos + 4, "eXIf", 4) && l >= 8) { *len = l; return dup_block(d + pos + 8, l); }
      if (!memcmp(d + pos + 4, "IDAT", 4)) break;
      pos += 12 + l;
    }
    return NULL;
  }
  if (n >= 20 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4)) {
    pos = 12;
    while (pos + 8 <= n) {
      u32 l = g32(d + pos + 4, 1);
      if (l > n - pos - 8) break;
      if (!memcmp(d + pos, "EXIF", 4) && l >= 8) {
        const u8 *e = d + pos + 8;
        if (l >= 14 && !memcmp(e, "Exif\0", 5)) { e += 6; l -= 6; }   /* (some writers keep JPEG's prefix) */
        *len = l; return dup_block(e, l);
      }
      pos += 8 + l + (l & 1);
    }
  }
  return NULL;
}

/* the TIFF header's byte order and IFD0's offset; 0 if it isn't one */
static int header(const u8 *e, u32 len, int *le, u32 *ifd0)
{
  if (len < 8) return 0;
  if (e[0] == 'I' && e[1] == 'I') *le = 1; else if (e[0] == 'M' && e[1] == 'M') *le = 0; else return 0;
  if (g16(e + 2, *le) != 42) return 0;
  *ifd0 = g32(e + 4, *le);
  return *ifd0 >= 8 && *ifd0 <= len - 2;
}
/* the entry for tag in the IFD at off (its offset in e), 0 if absent */
static u32 find_entry(const u8 *e, u32 len, int le, u32 off, int tag)
{
  u32 k, cnt;
  if (off < 8 || off > len - 2) return 0;
  cnt = g16(e + off, le);
  if (cnt > (len - off - 2) / 12) return 0;
  for (k = 0; k < cnt; k++) if ((int)g16(e + off + 2 + k * 12, le) == tag) return off + 2 + k * 12;
  return 0;
}
static int entry_value(const u8 *e, int le, u32 at, long *v)
{
  u32 type = g16(e + at + 2, le);
  if (type == 3) { *v = (long)g16(e + at + 8, le); return 1; }
  if (type == 4) { *v = (long)g32(e + at + 8, le); return 1; }
  return 0;
}
static int entry_set(u8 *e, int le, u32 at, u32 v)
{
  u32 type = g16(e + at + 2, le);
  if (g32(e + at + 4, le) != 1) return 0;
  if (type == 3 && v <= 0xFFFF) { p16(e + at + 8, le, v); return 1; }
  if (type == 4) { p32(e + at + 8, le, v); return 1; }
  return 0;
}
int exif_normalize(u8 *e, u32 len)
{
  int le;
  u32 ifd0, at, cnt;
  if (!header(e, len, &le, &ifd0)) return 0;
  cnt = g16(e + ifd0, le);
  if (cnt > (len - ifd0 - 2) / 12) return 0;
  if ((at = find_entry(e, len, le, ifd0, 0x0112)) != 0) entry_set(e, le, at, 1);
  if (len - ifd0 - 2 - cnt * 12 >= 4) p32(e + ifd0 + 2 + cnt * 12, le, 0);   /* no IFD1 (the thumbnail) */
  return 1;
}
int exif_get(const u8 *e, u32 len, int tag, long *val)
{
  int le;
  u32 ifd0, at;
  long sub;
  if (!e || !header(e, len, &le, &ifd0)) return 0;
  if ((at = find_entry(e, len, le, ifd0, tag)) != 0) return entry_value(e, le, at, val);
  if ((at = find_entry(e, len, le, ifd0, 0x8769)) != 0 && entry_value(e, le, at, &sub) && sub > 0 && (u32)sub < len &&
      (at = find_entry(e, len, le, (u32)sub, tag)) != 0) return entry_value(e, le, at, val);
  return 0;
}
/* the pixel sizes in the EXIF IFD set to w x h */
static void set_size(u8 *e, u32 len, i32 w, i32 h)
{
  int le;
  u32 ifd0, at;
  long sub;
  if (!header(e, len, &le, &ifd0)) return;
  if ((at = find_entry(e, len, le, ifd0, 0x8769)) == 0 || !entry_value(e, le, at, &sub) || sub <= 0 || (u32)sub >= len) return;
  if ((at = find_entry(e, len, le, (u32)sub, 0xA002)) != 0) entry_set(e, le, at, (u32)w);
  if ((at = find_entry(e, len, le, (u32)sub, 0xA003)) != 0) entry_set(e, le, at, (u32)h);
}

u8 *exif_embed(const Doc *d, int fmt, u8 *file, size_t *len)
{
  u8 *e, *out;
  u32 el;
  size_t at;
  if (!file || !d || !d->exif || d->exiflen < 8) return file;
  if (fmt == FMT_JPEG && (d->exiflen > 65533 - 6 || *len < 4 || file[0] != 0xFF || file[1] != 0xD8)) return file;
  if (fmt != FMT_JPEG && fmt != FMT_PNG) return file;
  el = d->exiflen;
  if (!(e = dup_block(d->exif, el))) { free(file); return NULL; }
  exif_normalize(e, el);
  set_size(e, el, d->w, d->h);
  if (fmt == FMT_JPEG) {
    /* APP1 just after SOI, or after a JFIF APP0 */
    at = 2;
    if (*len >= 6 && file[2] == 0xFF && file[3] == 0xE0) { size_t l = ((size_t)file[4] << 8) | file[5]; if (l + 4 <= *len) at = 4 + l; }
    if (!(out = (u8 *)malloc(*len + el + 10))) { free(e); free(file); return NULL; }
    memcpy(out, file, at);
    out[at] = 0xFF; out[at + 1] = 0xE1; out[at + 2] = (u8)((el + 8) >> 8); out[at + 3] = (u8)(el + 8);
    memcpy(out + at + 4, "Exif\0\0", 6);
    memcpy(out + at + 10, e, el);
    memcpy(out + at + 10 + el, file + at, *len - at);
    *len += el + 10;
  } else {
    /* eXIf before the first IDAT */
    u32 crc;
    at = 8;
    while (at + 12 <= *len) {
      u32 l = g32(file + at, 0);
      if (!memcmp(file + at + 4, "IDAT", 4) || l > *len - at - 12) break;
      at += 12 + l;
    }
    if (!(out = (u8 *)malloc(*len + el + 12))) { free(e); free(file); return NULL; }
    memcpy(out, file, at);
    p32(out + at, 0, el);
    memcpy(out + at + 4, "eXIf", 4);
    memcpy(out + at + 8, e, el);
    crc = crc32_update(0, out + at + 4, el + 4);
    p32(out + at + 8 + el, 0, crc);
    memcpy(out + at + 12 + el, file + at, *len - at);
    *len += el + 12;
  }
  free(e);
  free(file);
  return out;
}

void filesx_load_extras(Doc *doc, const u8 *d, size_t n)
{
  u32 el;
  u8 *e;
  if (!doc || doc->exif) return;
  if ((e = exif_from_file(d, n, &el)) != NULL) {
    if (exif_normalize(e, el)) { doc->exif = e; doc->exiflen = el; } else free(e);
  }
}

/* ------------------------------------------------------------------ layered TIFF */
#define ISD_SIG "Adobe Photoshop Document Data Block"
static u32 rb32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }

u8 *tiff_add_tag(u8 *t, size_t *len, int tag, int type, const u8 *data, u32 count)
{
  int le;
  u32 ifd, ne, i, k, nnext, ins = 0;
  size_t n = *len, dat, ifdat, total;
  u8 *o;
  if (!t || n < 8) return t;
  if (t[0] == 'I' && t[1] == 'I') le = 1; else if (t[0] == 'M' && t[1] == 'M') le = 0; else return t;
  ifd = g32(t + 4, le);
  if (ifd < 8 || ifd > n - 2) return t;
  ne = g16(t + ifd, le);
  if (ne > (n - ifd - 6) / 12) return t;
  nnext = g32(t + ifd + 2 + 12 * ne, le);
  dat = (n + 1) & ~(size_t)1;
  ifdat = (dat + count + 1) & ~(size_t)1;
  total = ifdat + 2 + 12 * (ne + 1) + 4;
  if (!(o = (u8 *)calloc(1, total))) { free(t); return NULL; }
  memcpy(o, t, n);
  memcpy(o + dat, data, count);
  p32(o + 4, le, (u32)ifdat);
  p16(o + ifdat, le, ne + 1);
  for (i = 0, k = 0; i <= ne; i++) {
    const u8 *e = t + ifd + 2 + 12 * k;
    u8 *dd = o + ifdat + 2 + 12 * i;
    u32 tg = k < ne ? g16(e, le) : 0x10000UL;
    if (tg == (u32)tag) { free(o); return t; }           /* (one there already) */
    if (!ins && tg > (u32)tag) {
      p16(dd, le, (u32)tag); p16(dd + 2, le, (u32)type); p32(dd + 4, le, count);
      if (count <= 4) { memset(dd + 8, 0, 4); memcpy(dd + 8, data, count); } else p32(dd + 8, le, (u32)dat);
      ins = 1;
      continue;
    }
    memcpy(dd, e, 12);
    k++;
  }
  p32(o + ifdat + 2 + 12 * (ne + 1), le, nnext);
  free(t);
  *len = total;
  return o;
}

/* the layer information a PSD of the document holds (its layer info section's contents); malloc'd */
static u8 *psd_layer_info_of(Doc *d, u32 *len)
{
  size_t pl, pos = 26;
  u8 *p = psd_write_doc(d, &pl), *r = NULL;
  u32 l;
  if (!p) return NULL;
  if (pl - pos < 4) goto out;
  pos += 4 + rb32(p + pos);                              /* colour mode data */
  if (pos > pl - 4) goto out;
  pos += 4 + rb32(p + pos);                              /* image resources */
  if (pos > pl - 8) goto out;
  pos += 4;                                              /* the layer and mask section's length */
  l = rb32(p + pos); pos += 4;                           /* the layer info's */
  if (l > pl - pos || !(r = (u8 *)malloc(l ? l : 1))) goto out;
  memcpy(r, p + pos, l);
  *len = l;
out:
  free(p);
  return r;
}
u8 *tiff_encode_layered(Doc *d, int compression, size_t *len)
{
  u8 *t = tiff_encode_doc(d, compression, len), *li, *blk;
  u32 ll, bl;
  if (!t || d->nlay < 2) return t;
  if (!(li = psd_layer_info_of(d, &ll))) { free(t); return NULL; }
  bl = (u32)sizeof ISD_SIG + 12 + ((ll + 3) & ~3u);
  if (!(blk = (u8 *)calloc(1, bl))) { free(li); free(t); return NULL; }
  memcpy(blk, ISD_SIG, sizeof ISD_SIG);                  /* (with its NUL) */
  memcpy(blk + sizeof ISD_SIG, "8BIMLayr", 8);
  p32(blk + sizeof ISD_SIG + 8, 0, (ll + 3) & ~3u);
  memcpy(blk + sizeof ISD_SIG + 12, li, ll);
  free(li);
  t = tiff_add_tag(t, len, 37724, 7, blk, bl);
  free(blk);
  return t;
}

int tiff_pages(const u8 *d, size_t n, u32 *ifd, int max)
{
  int le, k = 0;
  u32 off;
  if (n < 8) return 0;
  if (d[0] == 'I' && d[1] == 'I') le = 1; else if (d[0] == 'M' && d[1] == 'M') le = 0; else return 0;
  if (g16(d + 2, le) != 42) return 0;
  off = g32(d + 4, le);
  while (off >= 8 && off <= n - 2 && k < max) {
    u32 ne = g16(d + off, le);
    int j;
    for (j = 0; j < k; j++) if (ifd[j] == off) return k;          /* (a loop) */
    if (ne > (n - off - 6) / 12) break;
    ifd[k++] = off;
    off = g32(d + off + 2 + 12 * ne, le);
  }
  return k;
}

/* tag 37724's bytes in the first IFD; NULL if none */
static const u8 *isd_of(const u8 *d, size_t n, u32 *len)
{
  int le = d[0] == 'I';
  u32 ifd = g32(d + 4, le), ne, k;
  if (ifd < 8 || ifd > n - 2) return NULL;
  ne = g16(d + ifd, le);
  if (ne > (n - ifd - 6) / 12) return NULL;
  for (k = 0; k < ne; k++) {
    const u8 *e = d + ifd + 2 + 12 * k;
    if (g16(e, le) == 37724) {
      u32 cnt = g32(e + 4, le), off = g32(e + 8, le);
      if (cnt <= 4 || off > n || cnt > n - off) return NULL;
      *len = cnt;
      return d + off;
    }
  }
  return NULL;
}

Doc *tiff_read_layers(const u8 *d, size_t n, char *err, size_t errcap)
{
  u32 il, pos, ifd[MAXLAYERS];
  const u8 *isd;
  Doc *doc = NULL;
  int np, k;
  if (n < 8 || codec_detect(d, n) != FMT_TIFF || camraw_detect(d, n)) return NULL;
  if ((isd = isd_of(d, n, &il)) != NULL && il > sizeof ISD_SIG + 12 && !memcmp(isd, ISD_SIG, sizeof ISD_SIG)) {
    /* the tagged blocks after the signature: Layr (8 bits) or Lr16 inside a PSD made around it */
    pos = (u32)sizeof ISD_SIG;
    while (il - pos >= 12) {
      u32 bl = rb32(isd + pos + 8);
      int is16 = !memcmp(isd + pos + 4, "Lr16", 4);
      if (memcmp(isd + pos, "8BIM", 4) && memcmp(isd + pos, "8B64", 4)) break;
      if (bl > il - pos - 12) break;
      if (!memcmp(isd + pos + 4, "Layr", 4) || is16) {
        Rgba im;
        u8 *p;
        size_t plen = 26 + 12 + 4 + bl + 4;
        if (!codec_decode(d, n, &im, err, errcap)) return NULL;      /* (the flat picture: its size) */
        if (!(p = (u8 *)calloc(1, plen))) { rgba_free(&im); codec_oom = 1; return NULL; }
        memcpy(p, "8BPS\0\1", 6);
        p[12] = 0; p[13] = 4;
        p32(p + 14, 0, (u32)im.h); p32(p + 18, 0, (u32)im.w);
        p[22] = 0; p[23] = (u8)(is16 ? 16 : 8); p[24] = 0; p[25] = 3;
        p32(p + 34, 0, bl + 8);                          /* (cmode and resources empty: 26..33) */
        p32(p + 38, 0, bl);
        memcpy(p + 42, isd + pos + 12, bl);
        rgba_free(&im);
        doc = psd_read_doc(p, plen, err, errcap);
        free(p);
        if (doc && doc->nlay >= 1) return doc;
        if (doc) doc_free(doc);
        return NULL;
      }
      pos += 12 + bl;
    }
  }
  /* several pages: a layer each */
  np = tiff_pages(d, n, ifd, MAXLAYERS);
  if (np < 2) return NULL;
  {
    u8 *c = (u8 *)malloc(n);
    if (!c) { codec_oom = 1; return NULL; }
    memcpy(c, d, n);
    for (k = 0; k < np; k++) {
      Rgba im;
      Layer *l;
      i32 y, cw;
      p32(c + 4, d[0] == 'I', ifd[k]);
      if (!codec_decode(c, n, &im, err, errcap)) { if (!k) break; continue; }
      if (!k) {
        if (!(doc = doc_new(im.w, im.h, 0, 1))) { rgba_free(&im); codec_oom = 1; break; }
        layer_free(doc->lay[0]); doc->nlay = 0;
      }
      if (doc->nlay >= MAXLAYERS || !(l = layer_new(doc, "Page"))) { rgba_free(&im); break; }
      snprintf(l->name, sizeof l->name, "Page %d", k + 1);
      cw = im.w < doc->w ? im.w : doc->w;
      for (y = 0; y < im.h && y < doc->h; y++) memcpy(l->px + (size_t)y * doc->w * 4, im.px + (size_t)y * im.w * 4, (size_t)cw * 4);
      /* the first page on top, as a stack of pages reads */
      memmove(doc->lay + 1, doc->lay, sizeof doc->lay[0] * (size_t)doc->nlay);
      doc->lay[0] = l; doc->nlay++;
      rgba_free(&im);
      if (progress_aborted) break;
    }
    free(c);
  }
  if (doc && !doc->nlay) { doc_free(doc); doc = NULL; }
  if (doc) { doc->active = doc->nlay - 1; doc_forget_undo(doc); doc->dirty = 0; }
  return doc;
}

/* ------------------------------------------------------------------ JPEG Options' file size */
int jpeg_size_thumb(Doc *d, Rgba *out)
{
  Rgba im;
  long np = (long)d->w * d->h;
  int tx, ty;
  out->w = out->h = 0; out->px = NULL;
  if (!rgba_alloc(&im, d->w, d->h)) return 0;
  doc_composite(d, doc_box(d), im.px, (long)d->w * 4);
  rgba_flatten_white(&im);
  if (np <= JSZ_MAX) { *out = im; return 1; }
  /* a mosaic of 6 x 6 blocks of 64 x 64 pixels from all over the picture: the same detail, so the
     same bytes a pixel (scaling it down would smooth it and guess low) */
  if (!rgba_alloc(out, 6 * 64, 6 * 64)) { rgba_free(&im); return 0; }
  for (ty = 0; ty < 6; ty++)
    for (tx = 0; tx < 6; tx++) {
      i32 sx = d->w <= 64 ? 0 : (i32)((long)(d->w - 64) * tx / 5) & ~7, sy = d->h <= 64 ? 0 : (i32)((long)(d->h - 64) * ty / 5) & ~7, y, x;
      for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
          i32 cx = sx + x < d->w ? sx + x : d->w - 1, cy = sy + y < d->h ? sy + y : d->h - 1;
          memcpy(out->px + (((long)ty * 64 + y) * out->w + tx * 64 + x) * 4, im.px + ((long)cy * d->w + cx) * 4, 4);
        }
    }
  rgba_free(&im);
  return 1;
}
long jpeg_size_estimate(const Rgba *thumb, long w, long h, int quality, int progressive)
{
  size_t len = 0;
  u8 *j;
  long head, body;
  double k;
  if (!thumb || !thumb->px) return 0;
  j = progressive ? jpeg_encode_progressive(thumb, quality, 72, &len) : NULL;
  if (!j) j = jpeg_encode(thumb, quality, 72, &len);
  if (!j) return 0;
  free(j);
  if (thumb->w == w && thumb->h == h) return (long)len;
  /* the headers and tables stay the same size; the coded data grows with the area */
  head = 620;
  body = (long)len > head ? (long)len - head : 1;
  k = (double)w * h / ((double)thumb->w * thumb->h);
  return head + (long)(body * k);
}
