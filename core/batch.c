/*
 * batch.c - converting picture files (see batch.h): what the EaselConvert Shell command does for
 * each file, kept portable so the native tests can drive it.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "batch.h"
#include "palette.h"
#include "paint.h"
#include "iconwrite.h"
#include "xwrite.h"
#include "colorx.h"
#include "xread2.h"

void batch_opts_default(BatchOpts *o)
{
  memset(o, 0, sizeof *o);
  o->format = FMT_UNKNOWN;
  o->quality = 85;
  o->dither = 1;
}

int batch_format_named(const char *s)
{
  static const struct { const char *name; int fmt; } names[] = {
    { "PNG", FMT_PNG }, { "JPEG", FMT_JPEG }, { "JPG", FMT_JPEG }, { "BMP", FMT_BMP }, { "ILBM", FMT_ILBM },
    { "IFF", FMT_ILBM }, { "LBM", FMT_ILBM }, { "GIF", FMT_GIF }, { "ORA", FMT_ORA },
    /* 1.1: every other format Easel writes (and xwrite.h's extensions) */
    { "TARGA", FMT_TGA }, { "NETPBM", FMT_PNM }, { "ICON", FMT_ICO } };
  size_t i;
  if (!s) return FMT_UNKNOWN;
  if (*s == '.') s++;
  for (i = 0; i < sizeof names / sizeof names[0]; i++) if (str_ieq(s, names[i].name)) return names[i].fmt;
  return xwrite_format_of_ext(s);
}
int batch_format_of_path(const char *path)
{
  const char *e = strrchr(path, '.'), *slash = strrchr(path, '/'), *colon = strrchr(path, ':');
  if (!e || (slash && e < slash) || (colon && e < colon)) return FMT_UNKNOWN;
  return batch_format_named(e + 1);
}
const char *batch_ext(int fmt)
{
  switch (fmt) {
  case FMT_JPEG: return ".jpg";
  case FMT_BMP: return ".bmp";
  case FMT_ILBM: return ".iff";
  case FMT_GIF: return ".gif";
  case FMT_ORA: return ".ora";
  case FMT_TIFF: return ".tif";
  case FMT_TGA: return ".tga";
  case FMT_PSD: return ".psd";
  default: return xwrite_handles(fmt) ? xwrite_ext(fmt) : ".png";
  }
}

Doc *batch_load(const u8 *data, size_t len, char *err, size_t errcap)
{
  Doc *d = NULL;
  codec_oom = 0;
  if (ilbm_indexable(data, len)) d = ilbm_read_indexed(data, len, err, errcap);
  if (!d) d = codec_load_doc(data, len, err, errcap);
  return d;
}

int batch_resize(Doc *d, const BatchOpts *o)
{
  i32 w = o->width, h = o->height;
  int ok;
  if (w <= 0 && h <= 0) {
    if (o->scale <= 0 || o->scale == 100) return 1;
    w = (i32)((long)d->w * o->scale / 100); h = (i32)((long)d->h * o->scale / 100);
  } else if (w <= 0) w = (i32)(((long)d->w * h + d->h / 2) / d->h);
  else if (h <= 0) h = (i32)(((long)d->h * w + d->w / 2) / d->w);
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (w > MAXDIM) w = MAXDIM;
  if (h > MAXDIM) h = MAXDIM;
  if (w == d->w && h == d->h) return 1;
  ok = image_resize(d, w, h, w < d->w && h < d->h ? RS_BILINEAR : RS_BICUBIC);
  doc_forget_undo(d);
  return ok;
}

u8 *batch_encode(Doc *d, const BatchOpts *o, size_t *len, char *err, size_t errcap)
{
  u8 *data = NULL, *px;
  Rgba im;
  int dither = o->dither ? DI_FS : DI_NONE;
  *len = 0;
  switch (o->format) {
  case FMT_ORA: data = ora_write(d, len); break;
  /* 1.1: TIFF (LZW), Targa (run-length), PSD with its layers, and xwrite.h's formats */
  case FMT_TIFF: data = tiff_encode_doc(d, TIFFC_LZW, len); break;
  case FMT_TGA: data = tga_write_doc(d, 1, len); break;
  case FMT_PSD: data = psd_write_doc(d, len); break;
  case FMT_WEBP: case FMT_PNM: case FMT_QOI: case FMT_ICO: case FMT_DDS: case FMT_DPX: case FMT_FITS: case FMT_PDF_OUT:
  case FMT_CINEON_OUT: case FMT_HDR_OUT:                     /* 1.2 */
    data = xwrite_doc(d, o->format, o->variant, 0, len); break;
  case FMT_GIF: data = gif_write_doc(d, o->colors >= 2 && o->colors <= 256 ? o->colors : 256, QM_WU, dither, 0, len); break;
  case FMT_PNG: if (d->mode != MODE_RGB) { data = doc_png_encode(d, d->dpi, len); break; } /* fall through */
  default:
    if (o->format == FMT_ILBM && !o->colors && d->mode != MODE_RGB) {
      IlbmOpts io;
      ilbm_opts_default(&io);
      io.dither = dither;
      return ilbm_write_doc(d, &io, len, err, errcap);
    }
    if (!(px = doc_flat_rgba(d))) break;
    im.w = d->w; im.h = d->h; im.px = px;
    switch (o->format) {
    case FMT_JPEG: data = jpeg_encode(&im, o->quality >= 1 && o->quality <= 100 ? o->quality : 85, d->dpi, len); break;
    case FMT_BMP: data = d->mode == MODE_INDEXED || d->mode == MODE_BITMAP ? colorx_bmp_indexed(d, len) : bmp_encode(&im, len); break;   /* (1.2: palette BMPs) */
    case FMT_ILBM:
      if (o->colors >= 2 && o->colors <= 256) {
        IlbmOpts io;
        u8 pal[768], *idx = (u8 *)malloc((size_t)im.w * im.h);
        int npal;
        ilbm_opts_default(&io);
        io.dither = dither; io.method = QM_WU;
        if (idx) {
          Rgba flat;
          /* the colours chosen from the picture as ILBM shows it (over white) */
          if (rgba_alloc(&flat, im.w, im.h)) {
            memcpy(flat.px, im.px, (size_t)im.w * im.h * 4);
            rgba_flatten_white(&flat);
            if ((npal = quantize_rgba(flat.px, im.w, im.h, o->colors, QM_WU, dither, pal, 0, idx)))
              data = ilbm_write(&flat, pal, npal, idx, NULL, 0, &io, len, err, errcap);
            rgba_free(&flat);
          }
          free(idx);
        }
        rgba_free(&im);
        if (!data && err && !err[0]) codec_err(err, errcap, "Out of memory");
        return data;
      }
      data = ilbm_encode(&im, 0, 0, len);
      break;
    default: data = png_encode(&im, d->dpi, len); break;
    }
    rgba_free(&im);
    break;
  }
  if (data) data = colorx_embed(d, o->format, data, len);    /* 1.2: the picture's profile kept */
  if (!data) codec_err(err, errcap, "Out of memory");
  return data;
}

int batch_convert(const char *in, const char *out, const BatchOpts *opts, char *err, size_t errcap)
{
  BatchOpts o = *opts;
  size_t len = 0, olen = 0;
  u8 *data, *enc;
  Doc *d;
  int ok;
  if (err && errcap) err[0] = 0;
  if (!(data = file_read(in, &len))) { codec_err(err, errcap, "The file couldn't be read"); return 0; }
  d = batch_load(data, len, err, errcap);
  free(data);
  if (!d && !codec_oom && o.fallback) {
    char err2[160];
    err2[0] = 0;
    d = o.fallback(in, err2, sizeof err2);           /* failing, the decoders' message is kept */
  }
  if (!d) { if (err && !err[0]) codec_err(err, errcap, "Not a picture Easel knows"); return 0; }
  if (o.format == FMT_UNKNOWN) {
    const char *e = strrchr(out, '.');
    o.format = batch_format_of_path(out);
    if (o.format == FMT_PNM && !o.variant && e) o.variant = xwrite_sub_of_ext(e);   /* (.pbm, .pgm, .ppm, .pam) */
  }
  if ((o.format == FMT_DDS || o.format == FMT_PNM) && o.variant < 0) o.variant = 0;
  if (o.format == FMT_UNKNOWN) o.format = FMT_PNG;
  if (!batch_resize(d, &o)) { doc_free(d); codec_err(err, errcap, "Not enough memory to resize the picture"); return 0; }
  if (!(enc = batch_encode(d, &o, &olen, err, errcap))) { doc_free(d); return 0; }
  ok = file_write(out, enc, olen);
  free(enc);
  if (!ok) { doc_free(d); codec_err(err, errcap, "The file couldn't be written (the disk is full or write-protected?)"); return 0; }
  if (o.icon > 0 && o.icon <= ICON_N) {
    Rgba im;
    size_t ilen = 0;
    u8 *icon = NULL;
    char *ipath = (char *)malloc(strlen(out) + 6);
    im.w = d->w; im.h = d->h; im.px = doc_flat_rgba(d);
    if (im.px && ipath) {
      IconOpts io;
      icon_opts_default(&io, o.icon - 1);
      io.deftool = o.deftool;
      icon = icon_write_ex(&im, &io, &ilen, err, errcap);
      strcpy(ipath, out); strcat(ipath, ".info");
      if (icon && !file_write(ipath, icon, ilen)) { codec_err(err, errcap, "The icon couldn't be written"); free(icon); icon = NULL; }
    } else codec_err(err, errcap, "Out of memory for the icon");
    ok = icon != NULL;
    free(icon); free(ipath); free(im.px);
  }
  doc_free(d);
  return ok;
}
