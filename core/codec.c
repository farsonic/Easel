/*
 * codec.c - telling picture files apart, decoding any of them to RGBA, loading any of them as a
 * document, and the small picture helpers the format modules share (see codec.h).
 */
#include <stdlib.h>
#include <string.h>
#include "codec.h"
#include "avif.h"
#include "rasters.h"
#include "amigafmt.h"
#include "zip.h"
#include "xml.h"

/* copy a message into the caller's error buffer (which may be absent), always NUL-terminated */
void codec_err(char *err, size_t errcap, const char *msg)
{
  size_t l;
  if (!err || !errcap) return;
  l = strlen(msg);
  if (l >= errcap) l = errcap - 1;
  memcpy(err, msg, l);
  err[l] = 0;
}

/* a transparent picture of w x h; 0 (and an empty picture) if the size is out of range or memory
   runs out. The pixel cap keeps w * h * 4 well inside 32 bits on the Amiga */
int codec_oom;
int rgba_alloc(Rgba *im, i32 w, i32 h)
{
  im->w = im->h = 0; im->px = NULL;
  if (w < 1 || h < 1 || w > MAXDIM || h > MAXDIM || (long)w * h > CODEC_MAXPIXELS) return 0;
  if (!(im->px = (u8 *)calloc((size_t)w * h, 4))) { codec_oom = 1; return 0; }
  im->w = w; im->h = h;
  return 1;
}
void rgba_free(Rgba *im) { free(im->px); im->px = NULL; im->w = im->h = 0; }

/* every pixel laid over white, so the picture can go into a format without transparency */
void rgba_flatten_white(Rgba *im)
{
  long i, n = (long)im->w * im->h;
  for (i = 0; i < n; i++) {
    u8 *p = im->px + i * 4;
    int a = p[3];
    if (a != 255) {
      p[0] = (u8)((p[0] * a + 255 * (255 - a) + 127) / 255);
      p[1] = (u8)((p[1] * a + 255 * (255 - a) + 127) / 255);
      p[2] = (u8)((p[2] * a + 255 * (255 - a) + 127) / 255);
      p[3] = 255;
    }
  }
}

/* shrink to fit max x max, keeping the shape: each new pixel averages the block of old ones under
   it, the colours weighted by their alpha so transparent pixels don't darken the edges */
int rgba_thumbnail(const Rgba *im, i32 max, Rgba *out)
{
  i32 tw = im->w, th = im->h, x, y;
  if (max < 1) max = 1;
  if (tw > max || th > max) {
    if (tw >= th) { th = (i32)((long)th * max / tw); tw = max; } else { tw = (i32)((long)tw * max / th); th = max; }
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
  }
  if (!rgba_alloc(out, tw, th)) return 0;
  if (tw == im->w && th == im->h) { memcpy(out->px, im->px, (size_t)tw * th * 4); return 1; }
  for (y = 0; y < th; y++) {
    i32 y0 = (i32)((long)y * im->h / th), y1 = (i32)((long)(y + 1) * im->h / th);
    if (y1 <= y0) y1 = y0 + 1;
    for (x = 0; x < tw; x++) {
      i32 x0 = (i32)((long)x * im->w / tw), x1 = (i32)((long)(x + 1) * im->w / tw), sx, sy;
      double r = 0, g = 0, b = 0, a = 0;
      long cnt = 0;
      u8 *o = out->px + ((long)y * tw + x) * 4;
      if (x1 <= x0) x1 = x0 + 1;
      for (sy = y0; sy < y1; sy++)
        for (sx = x0; sx < x1; sx++) {
          const u8 *p = im->px + ((long)sy * im->w + sx) * 4;
          r += (double)p[0] * p[3]; g += (double)p[1] * p[3]; b += (double)p[2] * p[3]; a += p[3];
          cnt++;
        }
      if (a > 0) { o[0] = (u8)(r / a + 0.5); o[1] = (u8)(g / a + 0.5); o[2] = (u8)(b / a + 0.5); }
      o[3] = (u8)(a / cnt + 0.5);
    }
  }
  return 1;
}

/* the format from the file's signature. A zip counts as OpenRaster when it starts with a stored
   "mimetype" of image/openraster, or failing that has a stack.xml */
int codec_detect(const u8 *d, size_t n)
{
  static const u8 pngsig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
  if (n >= 8 && !memcmp(d, pngsig, 8)) return FMT_PNG;
  if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return FMT_JPEG;
  if (n >= 26 && d[0] == 'B' && d[1] == 'M') return FMT_BMP;
  if (n >= 12 && !memcmp(d, "FORM", 4) && (!memcmp(d + 8, "ILBM", 4) || !memcmp(d + 8, "PBM ", 4))) return FMT_ILBM;
  if (n >= 13 && (!memcmp(d, "GIF87a", 6) || !memcmp(d, "GIF89a", 6))) return FMT_GIF;
  if (avif_detect(d, n)) return FMT_AVIF;
  if (n >= 4 && d[0] == 'P' && d[1] == 'K' && d[2] == 3 && d[3] == 4) {
    ZipReader z;
    int ora;
    if (n >= 54 && !memcmp(d + 30, "mimetype", 8) && !memcmp(d + 38, "image/openraster", 16)) return FMT_ORA;
    if (!zip_open(&z, d, n)) return FMT_UNKNOWN;
    ora = zip_find(&z, "stack.xml") >= 0;
    zip_close(&z);
    return ora ? FMT_ORA : FMT_UNKNOWN;
  }
  {
    int r = amigafmt_detect(d, n);      /* WebP, the other IFF forms, icons, Sun, SGI, Atari */
    if (r) return FMT_DPX + r;
    r = rasters_detect(d, n, 0);        /* last: it includes Targa, which has no signature */
    if (r) return FMT_AVIF + r;
  }
  return FMT_UNKNOWN;
}
const char *codec_name(int fmt)
{
  static const char *const names[] = { "unknown", "PNG", "JPEG", "BMP", "IFF ILBM", "GIF", "OpenRaster", "AVIF" };
  if (fmt > FMT_DPX && fmt < FMT_N) return amigafmt_name(fmt - FMT_DPX);
  if (fmt > FMT_AVIF && fmt <= FMT_DPX) return rasters_name(fmt - FMT_AVIF);
  return fmt >= 0 && fmt <= FMT_AVIF ? names[fmt] : names[0];
}

/* an OpenRaster file's merged image, decoded (a nested OpenRaster is refused, so a hostile file
   can't make this recurse) */
static int ora_merged(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  ZipReader z;
  u8 *p;
  size_t len;
  int i, ok = 0;
  if (!zip_open(&z, d, n)) { codec_err(err, errcap, "Not a zip archive"); return 0; }
  i = zip_find(&z, "mergedimage.png");
  if (i < 0 || !(p = zip_read(&z, i, &len))) codec_err(err, errcap, "The OpenRaster file has no readable merged image");
  else {
    ok = codec_detect(p, len) != FMT_ORA && codec_decode(p, len, out, err, errcap);
    if (!ok && codec_detect(p, len) == FMT_ORA) codec_err(err, errcap, "Damaged OpenRaster file");
    free(p);
  }
  zip_close(&z);
  return ok;
}

/* by format; every decoder leaves *out empty when it fails */
int codec_decode(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  out->w = out->h = 0; out->px = NULL;
  codec_oom = 0;
  switch (codec_detect(d, n)) {
  case FMT_PNG: return png_read(d, n, out, err, errcap);
  case FMT_JPEG: return jpeg_read(d, n, out, err, errcap);
  case FMT_BMP: return bmp_read(d, n, out, err, errcap);
  case FMT_ILBM:   /* one whose palette changes line by line (SHAM, CTBL, PCHG) goes to amigafmt.c */
    if (amigafmt_detect(d, n) == AMF_ILBM_LINEPAL) return sham_read(d, n, out, err, errcap);
    return ilbm_read(d, n, out, err, errcap);
  case FMT_GIF: return gif_read(d, n, out, err, errcap);
  case FMT_ORA: return ora_merged(d, n, out, err, errcap);
  case FMT_AVIF: return avif_read(d, n, out, err, errcap);
  default: {
    int f = codec_detect(d, n);
    if (f > FMT_DPX) return amigafmt_decode(d, n, out, err, errcap);
    if (f > FMT_AVIF) return rasters_read(d, n, out, err, errcap);
    break; }
  }
  codec_err(err, errcap, "Unknown file format");
  return 0;
}

/* big- and little-endian fields */
static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static u32 be16(const u8 *p) { return ((u32)p[0] << 8) | p[1]; }
static u32 le32(const u8 *p) { return p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
/* pixels per metre as dots per inch, rounded */
static int ppm_dpi(u32 ppm) { return ppm > 0 && ppm < 4000000UL ? (int)((ppm * 254UL / 100 + 50) / 100) : 0; }

/* each format's resolution field, where it has one */
int codec_dpi(const u8 *d, size_t n)
{
  int fmt = codec_detect(d, n);
  if (fmt == FMT_PNG) {
    size_t o = 8;
    while (o + 12 <= n) {
      u32 len = be32(d + o);
      if (len > n - o - 12) break;
      if (!memcmp(d + o + 4, "pHYs", 4) && len >= 9 && d[o + 16] == 1) return ppm_dpi(be32(d + o + 8));
      if (!memcmp(d + o + 4, "IDAT", 4) || !memcmp(d + o + 4, "IEND", 4)) break;
      o += 12 + len;
    }
  } else if (fmt == FMT_JPEG) {
    if (n >= 18 && d[2] == 0xFF && d[3] == 0xE0 && !memcmp(d + 6, "JFIF", 5)) {
      u32 x = be16(d + 14);
      if (d[13] == 1) return (int)x;
      if (d[13] == 2) return (int)((x * 254 + 50) / 100);
    }
  } else if (fmt == FMT_BMP) {
    if (n >= 42 && le32(d + 14) >= 40) return ppm_dpi(le32(d + 38));
  } else if (fmt == FMT_ILBM) {
    /* the DPI chunk some Amiga programs write */
    size_t o = 12;
    while (o + 8 <= n) {
      u32 len = be32(d + o + 4);
      if (len > n - o - 8) break;
      if (!memcmp(d + o, "DPI ", 4) && len >= 2) return (int)be16(d + o + 8);
      if (!memcmp(d + o, "BODY", 4)) break;
      o += 8 + len + (len & 1);
    }
  } else if (fmt == FMT_ORA) {
    ZipReader z;
    int i, dpi = 0;
    u8 *p;
    size_t len;
    if (!zip_open(&z, d, n)) return 0;
    if ((i = zip_find(&z, "stack.xml")) >= 0 && (p = zip_read(&z, i, &len))) {
      Xml x;
      xml_init(&x, (const char *)p, len);
      while (xml_next(&x) != XT_EOF)
        if (x.type == XT_START && !strcmp(x.name, "image")) { const char *r = xml_attr(&x, "xres"); if (r) dpi = atoi(r); break; }
      xml_free(&x);
      free(p);
    }
    zip_close(&z);
    return dpi > 0 && dpi < 100000 ? dpi : 0;
  }
  return 0;
}

/* an ORA keeps its layers; anything else becomes one layer called "Background", its pixels taken
   over from the decoded picture rather than copied */
static Doc *doc_of(Rgba *im, const u8 *d, size_t n, char *err, size_t errcap);
Doc *codec_load_doc(const u8 *d, size_t n, char *err, size_t errcap)
{
  Rgba im;
  if (codec_detect(d, n) == FMT_ORA) return ora_read(d, n, err, errcap);
  if (!codec_decode(d, n, &im, err, errcap)) return NULL;
  return doc_of(&im, d, n, err, errcap);
}
/* a decoded picture made the one layer of a new document (its pixels taken over, not copied) */
static Doc *doc_of(Rgba *im, const u8 *d, size_t n, char *err, size_t errcap)
{
  Doc *doc;
  int dpi;
  if (!(doc = doc_adopt(im->w, im->h, im->px))) { rgba_free(im); codec_oom = 1; codec_err(err, errcap, "Not enough memory for the picture"); return NULL; }
  im->px = NULL;
  if ((dpi = codec_dpi(d, n)) > 0 && dpi < 65536) doc->dpi = (u16)dpi;
  doc_forget_undo(doc);
  doc->dirty = 0;
  return doc;
}
/* a JPEG decoded straight to w x h (no bigger than it is): for opening a picture too big for the
   memory there is at its own size */
Doc *codec_load_doc_scaled(const u8 *d, size_t n, i32 w, i32 h, char *err, size_t errcap)
{
  Rgba im;
  codec_oom = 0;
  if (codec_detect(d, n) != FMT_JPEG) { codec_err(err, errcap, "Only JPEG pictures can be opened smaller"); return NULL; }
  if (!jpeg_read_scaled(d, n, w, h, &im, err, errcap)) return NULL;
  return doc_of(&im, d, n, err, errcap);
}
