/*
 * menu3.c - Easel 1.3's menu, tool and automation additions (see menu3.h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "menu3.h"
#include "paint.h"
#include "palette.h"
#include "layers.h"
#include "layerops.h"
#include "layerx.h"
#include "exportx.h"
#include "batch.h"
#include "xwrite.h"
#include "iconwrite.h"
#include "resample.h"
#include "type.h"
#include "text.h"
#include "guides.h"
#include "toolsx.h"
#include "adjust.h"
#include "channels.h"
#include "cms.h"
#include "colorx.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static int mul255(int a, int b) { int t = a * b + 128; return (t + (t >> 8)) >> 8; }

/* px (w x h RGBA, taken) scaled to nw x nh with Image Size's bicubic; malloc'd, NULL if out of memory */
static u8 *scale_px(u8 *px, i32 w, i32 h, i32 nw, i32 nh)
{
  Doc *t;
  u8 *out;
  if (nw == w && nh == h) return px;
  if (!(t = doc_adopt(w, h, px))) { free(px); return NULL; }
  if (!image_resize(t, nw, nh, RS_BICUBIC)) { doc_free(t); return NULL; }
  out = t->lay[0]->px; t->lay[0]->px = NULL;
  doc_free(t);
  return out;
}

/* ================================================================== Save for Web */
const char *const sfw_names[SFW_N + 1] = { "GIF", "PNG-8", "PNG-24", "JPEG", "IFF ILBM", NULL };
void sfw_defaults(SfwOpts *o)
{
  memset(o, 0, sizeof *o);
  o->fmt = SFW_GIF; o->colors = 128; o->method = QM_MEDIAN; o->dither = DI_FS; o->quality = 60;
  o->transparency = 1; o->matte = 0xFFFFFF; o->pct = 100;
}
const char *sfw_ext(int fmt)
{
  static const char *const e[SFW_N] = { ".gif", ".png", ".png", ".jpg", ".iff" };
  return fmt >= 0 && fmt < SFW_N ? e[fmt] : ".gif";
}
/* the picture scaled, flattened (and over the matte when transparency isn't kept) */
static int sfw_picture(Doc *d, const SfwOpts *o, Rgba *im)
{
  ExportOpts eo;
  Doc *t;
  long k, n;
  int keep = o->transparency && o->fmt != SFW_JPEG;
  exportx_defaults(&eo);
  eo.pct = o->pct < 1 ? 100 : o->pct > 1000 ? 1000 : o->pct;
  eo.metadata = EXD_NONE;
  if (!(t = exportx_prepare(d, &eo))) return 0;
  im->w = t->w; im->h = t->h; im->px = doc_flat_rgba(t);
  doc_free(t);
  if (!im->px) return 0;
  n = (long)im->w * im->h;
  for (k = 0; k < n; k++) {
    u8 *p = im->px + k * 4;
    int a = p[3], c;
    if (keep && (o->fmt == SFW_PNG24 || a == 0 || a == 255)) continue;
    if (keep && a < 128) { p[3] = 0; continue; }       /* (one transparent entry: half-transparent goes either way) */
    for (c = 0; c < 3; c++) {
      int m = (int)((o->matte >> (16 - 8 * c)) & 255);
      p[c] = (u8)((p[c] * a + m * (255 - a) + 127) / 255);
    }
    p[3] = 255;
  }
  return 1;
}
u8 *sfw_encode(Doc *d, const SfwOpts *o, size_t *len)
{
  Rgba im;
  u8 *out = NULL;
  int colors = o->colors < 2 ? 2 : o->colors > 256 ? 256 : o->colors;
  *len = 0;
  if (!sfw_picture(d, o, &im)) return NULL;
  switch (o->fmt) {
  case SFW_GIF: out = gif_encode_rgba(&im, o->transparency ? (colors > 255 ? 255 : colors) : colors, o->method, o->dither, o->progressive, len); break;
  case SFW_PNG24: out = png_encode(&im, d->dpi, len); break;
  case SFW_JPEG: {
    int q = o->quality < 1 ? 1 : o->quality > 100 ? 100 : o->quality;
    out = o->progressive ? jpeg_encode_progressive(&im, q, d->dpi, len) : jpeg_encode(&im, q, d->dpi, len);
    break; }
  case SFW_PNG8: case SFW_ILBM: {
    long n = (long)im.w * im.h, k;
    u8 *idx = (u8 *)malloc((size_t)n), pal[256 * 3], trns[256];
    int npal, tr = -1, ntrns = 0, anyclear = 0;
    if (!idx) break;
    for (k = 0; k < n; k++) if (im.px[k * 4 + 3] < 128) { anyclear = 1; break; }
    if (anyclear && colors > 255) colors = 255;
    npal = quantize_rgba(im.px, im.w, im.h, anyclear ? colors : colors, o->method, o->dither, pal, 0, idx);
    if (!npal) { free(idx); break; }
    if (anyclear) {
      tr = npal < 256 ? npal : 255;
      pal[tr * 3] = pal[tr * 3 + 1] = pal[tr * 3 + 2] = 0;
      if (tr == npal) npal++;
      for (k = 0; k < n; k++) if (im.px[k * 4 + 3] < 128) idx[k] = (u8)tr;
    }
    if (o->fmt == SFW_PNG8) {
      if (tr >= 0) { memset(trns, 255, sizeof trns); trns[tr] = 0; ntrns = tr + 1; }
      out = png_encode_indexed(idx, im.w, im.h, pal, npal, trns, ntrns, d->dpi, len);
    } else {
      IlbmOpts io;
      char err[80];
      ilbm_opts_default(&io);
      io.kind = IK_NORMAL; io.planes = 0; io.method = o->method; io.dither = o->dither;
      if (tr >= 0) { io.mask = IMK_TRANSPARENT; io.transparent = tr; }
      io.aspect_x = d->aspect_x; io.aspect_y = d->aspect_y; io.modeid = d->modeid;
      out = ilbm_write(&im, pal, npal, idx, NULL, 0, &io, len, err, sizeof err);
    }
    free(idx);
    break; }
  }
  rgba_free(&im);
  if (!out) *len = 0;
  return out;
}
int sfw_preview(const u8 *file, size_t len, Rgba *out)
{
  char err[80];
  memset(out, 0, sizeof *out);
  return file && len && codec_decode(file, len, out, err, sizeof err);
}
long sfw_seconds(size_t len, long bps)
{
  double s;
  if (bps < 1) bps = 1;
  s = (double)len * 8.0 / (double)bps;
  return s < 1 ? 1 : (long)ceil(s);
}
void size_text(char *out, size_t len)
{
  if (len < 1024) sprintf(out, "%luB", (unsigned long)len);
  else if (len < 1024L * 1024) { unsigned long t = (unsigned long)((len * 10 + 512) / 1024); sprintf(out, "%lu.%luK", t / 10, t % 10); }
  else { unsigned long t = (unsigned long)(((double)len * 100) / (1024.0 * 1024) + 0.5); sprintf(out, "%lu.%02luM", t / 100, t % 100); }
}

/* ================================================================== Export As's estimate */
size_t export_estimate(Doc *d, const void *exportopts, int fmt, int quality)
{
  Doc *t;
  ExportOpts eo;
  size_t len = 0;
  u8 *data = NULL;
  if (exportopts) eo = *(const ExportOpts *)exportopts;
  else exportx_defaults(&eo);
  if (!(t = exportx_prepare(d, &eo))) return 0;
  if (xwrite_handles(fmt)) data = xwrite_doc(t, fmt, -1, quality, &len);
  else {
    BatchOpts bo;
    char err[80];
    batch_opts_default(&bo);
    bo.format = fmt; bo.quality = quality;
    if (fmt == FMT_GIF) bo.colors = 256;
    data = batch_encode(t, &bo, &len, err, sizeof err);
  }
  free(data);
  doc_free(t);
  return data ? len : 0;
}

/* ================================================================== Image Processor */
void improc_defaults(ImProcOpts *o)
{
  memset(o, 0, sizeof *o);
  o->jpeg = 1; o->jpeg_quality = 80; o->jpeg_w = o->psd_w = o->tiff_w = o->png_w = o->ilbm_w = 640;
  o->jpeg_h = o->psd_h = o->tiff_h = o->png_h = o->ilbm_h = 512;
}
void fit_size(i32 w, i32 h, i32 fw, i32 fh, int shrink_only, i32 *nw, i32 *nh)
{
  double s;
  *nw = w; *nh = h;
  if (w < 1 || h < 1 || fw < 1 || fh < 1) return;
  s = (double)fw / w;
  if ((double)fh / h < s) s = (double)fh / h;
  if (shrink_only && s >= 1) return;
  *nw = (i32)(w * s + 0.5); *nh = (i32)(h * s + 0.5);
  if (*nw < 1) *nw = 1;
  if (*nh < 1) *nh = 1;
}
void path_join(char *out, size_t cap, const char *drawer, const char *part)
{
  size_t n;
  if (!cap) return;
  snprintf(out, cap, "%s", drawer ? drawer : "");
  n = strlen(out);
  if (n && out[n - 1] != '/' && out[n - 1] != ':' && n + 1 < cap) { out[n++] = '/'; out[n] = 0; }
  if (n < cap) snprintf(out + n, cap - n, "%s", part);
}
static int improc_one(Doc *d, const char *name, const char *drawer, const char *sub, int fmt, int quality, int colors,
                      int resize, i32 fw, i32 fh, const ImProcOpts *o, int (*make_drawer)(const char *), char *err, size_t errcap)
{
  char dir[512], file[600], fname[160];
  Doc *t;
  ExportOpts eo;
  BatchOpts bo;
  u8 *data;
  size_t len = 0;
  const char *dot;
  path_join(dir, sizeof dir, drawer, sub);
  if (make_drawer && !make_drawer(dir)) { snprintf(err, errcap, "The drawer %s couldn't be made", dir); return 0; }
  exportx_defaults(&eo);
  eo.to_srgb = o->to_srgb;
  eo.metadata = EXD_ALL;
  if (!(t = exportx_prepare(d, &eo))) { snprintf(err, errcap, "Not enough memory"); return 0; }
  if (o->copyright[0]) snprintf(t->info.copyright, sizeof t->info.copyright, "%s", o->copyright);
  batch_opts_default(&bo);
  bo.format = fmt; bo.quality = quality; bo.colors = colors;
  if (resize) {
    fit_size(t->w, t->h, fw, fh, 0, &bo.width, &bo.height);
    if (!batch_resize(t, &bo)) { doc_free(t); snprintf(err, errcap, "Not enough memory"); return 0; }
  }
  bo.width = bo.height = 0;
  data = batch_encode(t, &bo, &len, err, errcap);
  if (data && fmt == FMT_PNG) data = png_add_info(data, &len, &t->info);
  else if (data && fmt == FMT_JPEG) data = jpeg_add_info(data, &len, &t->info);
  dot = strrchr(name, '.');
  snprintf(fname, sizeof fname, "%.*s%s", (int)(dot && dot != name ? dot - name : (long)strlen(name)), name, batch_ext(fmt));
  path_join(file, sizeof file, dir, fname);
  if (data && !file_write(file, data, len)) { free(data); data = NULL; snprintf(err, errcap, "%s couldn't be written", file); }
  free(data);
  if (data && o->icon) {
    Rgba im;
    u8 *icon;
    size_t il = 0;
    char ip[620];
    im.w = t->w; im.h = t->h;
    if ((im.px = doc_flat_rgba(t)) != NULL) {
      if ((icon = icon_write(&im, ICON_GLOW, NULL, &il)) != NULL) { snprintf(ip, sizeof ip, "%s.info", file); file_write(ip, icon, il); free(icon); }
      free(im.px);
    }
  }
  doc_free(t);
  return data != NULL;
}
int improc_doc(Doc *d, const char *name, const char *drawer, const ImProcOpts *o, int (*make_drawer)(const char *), char *err, size_t errcap)
{
  int n = 0;
  if (err && errcap) err[0] = 0;
  if (o->jpeg) n += improc_one(d, name, drawer, "JPEG", FMT_JPEG, o->jpeg_quality, 0, o->jpeg_resize, o->jpeg_w, o->jpeg_h, o, make_drawer, err, errcap);
  if (o->psd) n += improc_one(d, name, drawer, "PSD", FMT_PSD, 0, 0, o->psd_resize, o->psd_w, o->psd_h, o, make_drawer, err, errcap);
  if (o->tiff) n += improc_one(d, name, drawer, "TIFF", FMT_TIFF, 0, 0, o->tiff_resize, o->tiff_w, o->tiff_h, o, make_drawer, err, errcap);
  if (o->png) n += improc_one(d, name, drawer, "PNG", FMT_PNG, 0, 0, o->png_resize, o->png_w, o->png_h, o, make_drawer, err, errcap);
  if (o->ilbm) n += improc_one(d, name, drawer, "ILBM", FMT_ILBM, 0, o->ilbm_colors, o->ilbm_resize, o->ilbm_w, o->ilbm_h, o, make_drawer, err, errcap);
  return n;
}

/* ================================================================== File > New */
const char *const newunit_names[NU_N + 1] = { "Pixels", "Inches", "Centimeters", "Millimeters", "Points", "Picas", NULL };
const char *const newmode_names[NM_N + 1] = { "Bitmap", "Grayscale", "RGB Color", "CMYK Color", "Lab Color", NULL };
const NewPreset newdoc_builtin[] = {
  { "Default Easel Size", 800, 600, NU_PIXELS, 72, NM_RGB, NB_WHITE, 1, 1 },
  { "PAL Low Res (320 x 256)", 320, 256, NU_PIXELS, 72, NM_RGB, NB_WHITE, 10, 11 },
  { "PAL High Res (640 x 512)", 640, 512, NU_PIXELS, 72, NM_RGB, NB_WHITE, 5, 11 },
  { "NTSC Low Res (320 x 200)", 320, 200, NU_PIXELS, 72, NM_RGB, NB_WHITE, 44, 52 },
  { "NTSC High Res (640 x 400)", 640, 400, NU_PIXELS, 72, NM_RGB, NB_WHITE, 22, 52 },
  { "VGA (640 x 480)", 640, 480, NU_PIXELS, 72, NM_RGB, NB_WHITE, 1, 1 },
  { "HD (1920 x 1080)", 1920, 1080, NU_PIXELS, 72, NM_RGB, NB_WHITE, 1, 1 },
  { "A4 (300 ppi)", 210, 297, NU_MM, 300, NM_RGB, NB_WHITE, 1, 1 },
  { "Letter (300 ppi)", 8.5, 11, NU_INCHES, 300, NM_RGB, NB_WHITE, 1, 1 },
  { "Postcard (6 x 4 in)", 6, 4, NU_INCHES, 300, NM_RGB, NB_WHITE, 1, 1 },
  { "Icon (64 x 64)", 64, 64, NU_PIXELS, 72, NM_RGB, NB_TRANSPARENT, 1, 1 },
  { "Fax (1 bit, 200 ppi)", 210, 297, NU_MM, 200, NM_BITMAP, NB_WHITE, 1, 1 },
};
const int newdoc_nbuiltin = (int)(sizeof newdoc_builtin / sizeof newdoc_builtin[0]);
static double unit_per_inch(int unit, int dpi)
{
  switch (unit) {
  case NU_INCHES: return 1;
  case NU_CM: return 2.54;
  case NU_MM: return 25.4;
  case NU_POINTS: return 72;
  case NU_PICAS: return 6;
  default: return dpi > 0 ? dpi : 72;
  }
}
double units_to_px(double v, int unit, int dpi)
{
  if (unit == NU_PIXELS) return v;
  return v * (dpi > 0 ? dpi : 72) / unit_per_inch(unit, dpi);
}
double px_to_units(double px, int unit, int dpi)
{
  if (unit == NU_PIXELS) return px;
  return px * unit_per_inch(unit, dpi) / (dpi > 0 ? dpi : 72);
}
void newpreset_format(const NewPreset *p, char *out, size_t cap)
{
  char name[40];
  int i;
  for (i = 0; p->name[i] && i < 39; i++) name[i] = p->name[i] == '|' ? '/' : p->name[i];
  name[i] = 0;
  snprintf(out, cap, "%s|%g|%g|%d|%d|%d|%d|%d|%d", name, p->w, p->h, p->unit, p->dpi, p->mode, p->bg, p->ax, p->ay);
}
int newpreset_parse(const char *line, NewPreset *p)
{
  const char *bar = strchr(line, '|');
  int u, dpi, m, bg, ax, ay;
  double w, h;
  size_t n;
  if (!bar || (n = (size_t)(bar - line)) == 0 || n >= sizeof p->name) return 0;
  if (sscanf(bar + 1, "%lf|%lf|%d|%d|%d|%d|%d|%d", &w, &h, &u, &dpi, &m, &bg, &ax, &ay) != 8) return 0;
  if (w <= 0 || h <= 0 || u < 0 || u >= NU_N || dpi < 1 || dpi > 9999 || m < 0 || m >= NM_N || bg < 0 || bg >= NB_N || ax < 1 || ay < 1 || ax > 255 || ay > 255) return 0;
  memset(p, 0, sizeof *p);
  memcpy(p->name, line, n); p->name[n] = 0;
  p->w = w; p->h = h; p->unit = u; p->dpi = dpi; p->mode = m; p->bg = bg; p->ax = (u8)ax; p->ay = (u8)ay;
  return 1;
}
Doc *newdoc_make(const NewPreset *p, u32 bgcolor, char *err, size_t errcap)
{
  double fw = units_to_px(p->w, p->unit, p->dpi), fh = units_to_px(p->h, p->unit, p->dpi);
  i32 w = (i32)(fw + 0.5), h = (i32)(fh + 0.5);
  int transparent = p->bg == NB_TRANSPARENT && p->mode != NM_BITMAP;
  u32 fill = p->bg == NB_BLACK ? 0 : p->bg == NB_BGCOLOR ? bgcolor : 0xFFFFFF;
  Doc *d;
  if (w < 1 || h < 1 || w > MAXDIM || h > MAXDIM) { snprintf(err, errcap, "The width and height must be between 1 and 16384 pixels."); return NULL; }
  if (!(d = doc_new(w, h, fill, transparent))) { snprintf(err, errcap, "There isn't enough memory for a picture that size."); return NULL; }
  d->dpi = (u16)p->dpi;
  d->aspect_x = p->ax ? p->ax : 1; d->aspect_y = p->ay ? p->ay : 1;
  if (p->mode == NM_GRAY) doc_to_gray(d);
  else if (p->mode == NM_BITMAP) doc_to_bitmap(d, 0);
  else if (p->mode == NM_CMYK) colorx_convert_model(d, CM_CMYK, NULL);
  else if (p->mode == NM_LAB) colorx_convert_model(d, CM_LAB, NULL);
  doc_forget_undo(d);
  d->dirty = 0;
  strcpy(d->last_action, "New");
  return d;
}

/* ================================================================== Pixel Art Preview */
long pixelart_zoom(long z, int dir)
{
  /* the steps: 1000 / n (n = 32 .. 2) below 100 %, then 1000 * n (n = 1 .. 64) */
  long steps[95], best = 0;
  int ns = 0, i;
  for (i = 32; i >= 2; i--) steps[ns++] = 1000 / i;
  for (i = 1; i <= 64; i++) steps[ns++] = 1000L * i;
  if (dir > 0) { for (i = 0; i < ns; i++) if (steps[i] > z) return steps[i]; return steps[ns - 1]; }
  if (dir < 0) { for (i = ns - 1; i >= 0; i--) if (steps[i] < z) return steps[i]; return steps[0]; }
  for (i = 1; i < ns; i++) if (labs(steps[i] - z) < labs(steps[best] - z)) best = i;
  return steps[best];
}

/* ================================================================== Reset Tool */
int tool_reset_tip(const char *name, ToolTip *t)
{
  /* PhotoCraft's Reset Tool tips (#11856 #12243 #12253 #12383 #12691 #12862 #12989 #13109 #13151 #14935) */
  static const struct { const char *name; ToolTip t; } tips[] = {
    { "Brush", { 13, 0, 25, 10, 100, 100 } },
    { "Pencil", { 1, 100, 25, 0, 100, 100 } },
    { "Eraser", { 13, 100, 25, 0, 100, 100 } },
    { "Background Eraser", { 13, 100, 25, 0, 100, 100 } },
    { "Healing Brush", { 19, 100, 25, 0, 100, 100 } },
    { "Spot Healing Brush", { 19, 100, 25, 0, 100, 100 } },
    { "Clone Stamp", { 21, 0, 25, 0, 100, 100 } },
    { "Pattern Stamp", { 21, 0, 25, 0, 100, 100 } },
    { "Smudge", { 13, 0, 25, 0, 100, 100 } },
    { "Blur", { 13, 0, 25, 0, 100, 100 } },
    { "Sharpen", { 13, 0, 25, 0, 100, 100 } },
    { "Dodge", { 65, 0, 25, 0, 100, 100 } },
    { "Burn", { 65, 0, 25, 0, 100, 100 } },
    { "Sponge", { 65, 0, 25, 0, 100, 100 } },
    { "Remove", { 19, 100, 25, 0, 100, 100 } },
    { "History Brush", { 21, 0, 25, 0, 100, 100 } },
    { "Art History Brush", { 4, 100, 25, 0, 100, 100 } },
    { "Mixer Brush", { 13, 0, 25, 0, 100, 100 } },
    { "Color Replacement", { 13, 100, 25, 0, 100, 100 } },
  };
  size_t i;
  ToolTip def = { 13, 100, 25, 0, 100, 100 };
  for (i = 0; i < sizeof tips / sizeof tips[0]; i++)
    if (str_ieq(name, tips[i].name)) { *t = tips[i].t; return 1; }
  *t = def;
  return 0;
}

/* ================================================================== colours as text */
void color_html(u32 rgb, char *out) { sprintf(out, "color: #%06lx;", (unsigned long)(rgb & 0xFFFFFF)); }
void color_hex(u32 rgb, char *out) { sprintf(out, "%06lx", (unsigned long)(rgb & 0xFFFFFF)); }

/* ================================================================== Fill and Stroke */
static Layer *active_pixels(Doc *d)
{
  Layer *l;
  if (d->active < 0 || d->active >= d->nlay) return NULL;
  l = d->lay[d->active];
  if (!l->px || l->locked || l->lock_all || l->idx) return NULL;
  return l;
}
Box fill_from_pixels(Doc *d, const u8 *src, int opacity, int blend, int keep)
{
  Layer *l = active_pixels(d);
  Box b;
  i32 x, y;
  int op = opacity * 255 / 100;
  if (!l || !src) return box_make(0, 0, 0, 0);
  b = d->sel ? d->selbox : doc_box(d);
  b = box_clip(b, doc_box(d));
  if (box_empty(b)) return b;
  doc_begin(d, "Fill");
  doc_note_layer(d, d->active, b);
  for (y = b.y0; y < b.y1; y++)
    for (x = b.x0; x < b.x1; x++) {
      long k = ((long)y * d->w + x) * 4;
      u8 *p = l->px + k, s[4];
      int a = p[3], c = sel_at(d, x, y);
      if (!c) continue;
      memcpy(s, src + k, 4);
      s[3] = (u8)mul255(s[3], c);
      blend_pixel_at(p, s, op, blend, x, y);
      if (keep || l->alpha_lock) { if (!a) p[0] = p[1] = p[2] = 0; p[3] = (u8)a; }
    }
  doc_end(d);
  return b;
}
Box fill_keep(Doc *d, u32 color, int opacity, int blend, int keep)
{
  Layer *l = active_pixels(d);
  u8 was;
  Box b;
  if (!l) return fill_selection(d, color, opacity, blend);
  was = l->alpha_lock;
  if (keep) l->alpha_lock = 1;
  b = fill_selection(d, color, opacity, blend);
  l->alpha_lock = was;
  return b;
}
Box stroke_keep(Doc *d, int width, int where, u32 color, int opacity, int blend, int keep)
{
  Layer *l = active_pixels(d);
  u8 was;
  Box b;
  if (!l) return stroke_selection_mode(d, width, where, color, opacity, blend);
  was = l->alpha_lock;
  if (keep) l->alpha_lock = 1;
  b = stroke_selection_mode(d, width, where, color, opacity, blend);
  l->alpha_lock = was;
  return b;
}

/* ================================================================== Color Lookup Tables */
/* the grid (n^3 RGBA, red fastest) through the visible adjustment layers; 0 if there are none */
static u8 *lut_grid(Doc *d, int n)
{
  long cells = (long)n * n * n, k;
  u8 *g, *t;
  int i, any = 0;
  if (n < 2 || n > 64 || !adjlayer_make) return NULL;
  if (!(g = (u8 *)malloc((size_t)cells * 4))) return NULL;
  if (!(t = (u8 *)malloc((size_t)cells * 4))) { free(g); return NULL; }
  for (k = 0; k < cells; k++) {
    long r = k % n, gg = (k / n) % n, b = k / ((long)n * n);
    g[k * 4] = (u8)((r * 255 + (n - 1) / 2) / (n - 1)); g[k * 4 + 1] = (u8)((gg * 255 + (n - 1) / 2) / (n - 1));
    g[k * 4 + 2] = (u8)((b * 255 + (n - 1) / 2) / (n - 1)); g[k * 4 + 3] = 255;
  }
  for (i = 0; i < d->nlay; i++) {
    Layer *l = d->lay[i];
    void *prog;
    int op;
    if (l->kind != LK_ADJUST || !layer_shown(d, i) || !(op = mul255(l->opacity, 255 - l->fill_cut))) continue;
    if (!(prog = adjlayer_make(&l->adj))) continue;
    memcpy(t, g, (size_t)cells * 4);
    adjlayer_row(prog, t, (int)cells);
    if (adjlayer_free) adjlayer_free(prog);
    for (k = 0; k < cells * 4; k++) if ((k & 3) != 3) g[k] = (u8)(g[k] + ((t[k] - g[k]) * op + (t[k] >= g[k] ? 127 : -127)) / 255);
    any = 1;
  }
  free(t);
  if (!any) { free(g); return NULL; }
  return g;
}
char *lut_cube_from_doc(Doc *d, int n, const char *title)
{
  u8 *g = lut_grid(d, n);
  Buf b;
  long k, cells = (long)n * n * n;
  if (!g) return NULL;
  buf_init(&b);
  buf_printf(&b, "# Created by Easel\nTITLE \"%s\"\nLUT_3D_SIZE %d\nDOMAIN_MIN 0.0 0.0 0.0\nDOMAIN_MAX 1.0 1.0 1.0\n", title && *title ? title : "Easel", n);
  for (k = 0; k < cells; k++) {
    int c;
    for (c = 0; c < 3; c++) {
      long v = (long)g[k * 4 + c] * 1000000L / 255;          /* six decimals, no printf %f (vbcc's is slow) */
      buf_printf(&b, "%ld.%06ld%c", v / 1000000L, v % 1000000L, c < 2 ? ' ' : '\n');
    }
  }
  free(g);
  if (b.oom) { buf_free(&b); return NULL; }
  return buf_take(&b);
}
char *lut_3dl_from_doc(Doc *d, int n)
{
  u8 *g = lut_grid(d, n);
  Buf b;
  int r, gg, bb;
  if (!g) return NULL;
  buf_init(&b);
  for (r = 0; r < n; r++) buf_printf(&b, "%d%c", r * 1023 / (n - 1), r < n - 1 ? ' ' : '\n');
  for (r = 0; r < n; r++) for (gg = 0; gg < n; gg++) for (bb = 0; bb < n; bb++) {
    long k = ((long)bb * n + gg) * n + r;
    buf_printf(&b, "%d %d %d\n", g[k * 4] * 1023 / 255, g[k * 4 + 1] * 1023 / 255, g[k * 4 + 2] * 1023 / 255);
  }
  free(g);
  if (b.oom) { buf_free(&b); return NULL; }
  return buf_take(&b);
}

/* ================================================================== Contact Sheet II */
void contact_defaults(ContactOpts *o)
{
  memset(o, 0, sizeof *o);
  o->w = 2480; o->h = 3508; o->dpi = 300; o->cols = 5; o->rows = 6; o->across = 1; o->spacing = 20; o->bg = 0xFFFFFF;
  o->captions = 1; o->font_px = 24; strcpy(o->family, "Sans");
}
int contact_pages(int n, const ContactOpts *o)
{
  int per = (o->cols < 1 ? 1 : o->cols) * (o->rows < 1 ? 1 : o->rows);
  return n <= 0 ? 0 : (n + per - 1) / per;
}
/* a picture's composite scaled into w x h (bilinear-ish box filter via resample), malloc'd */
static u8 *scaled_flat(Doc *p, i32 w, i32 h, int turn)
{
  u8 *flat = doc_flat_rgba(p);
  i32 sw = p->w, sh = p->h;
  if (!flat) return NULL;
  if (turn) {
    /* a quarter turn clockwise first */
    u8 *t = (u8 *)malloc((size_t)p->w * p->h * 4);
    i32 x, y;
    if (!t) { free(flat); return NULL; }
    for (y = 0; y < p->h; y++) for (x = 0; x < p->w; x++) memcpy(t + ((long)x * p->h + (p->h - 1 - y)) * 4, flat + ((long)y * p->w + x) * 4, 4);
    free(flat); flat = t; sw = p->h; sh = p->w;
  }
  return scale_px(flat, sw, sh, w, h);
}
Doc *contact_sheet(Doc **pics, const char *const *names, int n, int page, const ContactOpts *o)
{
  int cols = o->cols < 1 ? 1 : o->cols, rows = o->rows < 1 ? 1 : o->rows, per = cols * rows, i, first = page * per;
  i32 sp = o->spacing < 0 ? 0 : o->spacing, cw, ch;
  Doc *d;
  Layer *flatl = NULL;
  if (first >= n || first < 0) return NULL;
  i32 caph = o->captions ? o->font_px + o->font_px / 2 : 0;
  cw = (o->w - sp * (cols + 1)) / cols; ch = (o->h - sp * (rows + 1)) / rows - caph;
  if (cw < 1 || ch < 1) return NULL;
  if (!(d = doc_new(o->w, o->h, o->bg, 0))) return NULL;
  d->dpi = (u16)(o->dpi > 0 ? o->dpi : 72);
  if (o->flatten) {
    if (!(flatl = layer_new(d, "Contact Sheet II")) || !doc_add_layer(d, flatl, d->nlay)) { doc_free(d); return NULL; }   /* (doc_add_layer frees it on failure) */
    flatl = d->lay[d->nlay - 1];
  }
  for (i = first; i < n && i < first + per; i++) {
    int cell = i - first, cx = o->across ? cell % cols : cell / rows, cy = o->across ? cell / cols : cell % rows;
    Doc *p = pics[i];
    int turn = o->rotate && ((p->w > p->h) != (cw > ch)) && p->w != p->h;
    i32 pw = turn ? p->h : p->w, ph = turn ? p->w : p->h, nw, nh, x0, y0, x, y;
    u8 *px;
    Layer *l;
    fit_size(pw, ph, cw, ch, 0, &nw, &nh);
    if (!(px = scaled_flat(p, nw, nh, turn))) { doc_free(d); return NULL; }
    x0 = sp + cx * (cw + sp) + (cw - nw) / 2; y0 = sp + cy * (ch + caph + sp) + (ch - nh) / 2;
    if (flatl) l = flatl;
    else {
      if (!(l = layer_new(d, names && names[i] ? names[i] : "Picture"))) { free(px); doc_free(d); return NULL; }
      if (!doc_add_layer(d, l, d->nlay)) { free(px); doc_free(d); return NULL; }
      l = d->lay[d->nlay - 1];
    }
    for (y = 0; y < nh; y++) for (x = 0; x < nw; x++) {
      if (x0 + x < 0 || y0 + y < 0 || x0 + x >= d->w || y0 + y >= d->h) continue;
      blend_pixel(l->px + ((long)(y0 + y) * d->w + x0 + x) * 4, px + ((long)y * nw + x) * 4, 255, BM_NORMAL);
    }
    free(px);
    if (caph && names && names[i] && d->nlay < MAXLAYERS) {
      /* the caption: a type layer centred under the cell */
      TextParams *tp = (TextParams *)calloc(1, sizeof *tp);
      if (tp) {
        text_params_init(tp, o->family[0] ? o->family : "Sans", o->font_px, 0x000000, sp + cx * (cw + sp) + cw / 2.0, sp + cy * (ch + caph + sp) + ch + o->font_px);
        tp->align = TA_CENTER;
        text_set_string(tp, names[i]);
        doc_new_text(d, tp, names[i]);
        free(tp);
      }
    }
  }
  if (flatl) { doc_flatten(d); snprintf(d->lay[0]->name, sizeof d->lay[0]->name, "Contact Sheet II"); }
  d->active = d->nlay - 1;
  doc_forget_undo(d);
  d->dirty = 0;
  return d;
}

/* ================================================================== Statistics */
Doc *doc_statistics(Doc **pics, int n, int mode)
{
  Doc *d;
  int i, r;
  Layer *keep;
  if (n < 2 || !pics[0]) return NULL;
  if (!(d = doc_new(pics[0]->w, pics[0]->h, 0, 1))) return NULL;
  free(d->lay[0]->px);
  if (!(d->lay[0]->px = scaled_flat(pics[0], d->w, d->h, 0))) { doc_free(d); return NULL; }
  for (i = 1; i < n; i++) {
    Layer *l = layer_new(d, "Picture");
    if (!l) { doc_free(d); return NULL; }
    free(l->px);
    if (!(l->px = scaled_flat(pics[i], d->w, d->h, 0))) { layer_free(l); doc_free(d); return NULL; }
    if (!doc_add_layer(d, l, d->nlay)) { doc_free(d); return NULL; }
  }
  for (i = 0; i < d->nlay; i++) d->lay[i]->picked = 0;
  if ((r = doc_stack_mode(d, mode)) < 0) { doc_free(d); return NULL; }
  /* only the result is kept */
  doc_forget_undo(d);
  keep = d->lay[r];
  for (i = 0; i < d->nlay; i++) if (i != r) layer_free(d->lay[i]);
  d->lay[0] = keep; d->nlay = 1; d->active = 0;
  keep->depth = 0; keep->visible = 1;
  snprintf(keep->name, sizeof keep->name, "%s", stack_names[mode]);
  d->dirty = 0;
  return d;
}

/* ================================================================== Image Assets */
int assets_parse(const char *nm, AssetSpec *out, int max)
{
  int n = 0;
  const char *p = nm;
  while (*p && n < max) {
    char tok[96];
    int len = 0, pct = 100, q = 0;
    const char *s, *dot;
    char *t;
    while (*p == ',' || *p == '+' || *p == ' ') p++;
    while (*p && *p != ',' && *p != '+' && len < 95) tok[len++] = *p++;
    while (len && tok[len - 1] == ' ') len--;
    tok[len] = 0;
    if (!len) continue;
    t = tok;
    /* "50% name.png" or "300x200 name.png" (a size: taken as per cent of the width is not known here: skipped) */
    if (isdigit((unsigned char)*t)) {
      char *e;
      long v = strtol(t, &e, 10);
      if (*e == '%' && e[1] == ' ') { pct = (int)v; t = e + 2; while (*t == ' ') t++; }
    }
    dot = strrchr(t, '.');
    if (!dot || dot == t) continue;
    s = dot + 1;
    {
      char ext[8];
      int k = 0;
      while (isalpha((unsigned char)s[k]) && k < 7) { ext[k] = (char)tolower((unsigned char)s[k]); k++; }
      ext[k] = 0;
      if (strcmp(ext, "png") && strcmp(ext, "jpg") && strcmp(ext, "jpeg") && strcmp(ext, "gif") && strcmp(ext, "iff")) continue;
      if (s[k]) {
        char *e;
        long v = strtol(s + k, &e, 10);
        if (*e == '%') q = (int)v;
        else if (!strcmp(ext, "png")) q = (int)v;               /* png8, png24, png32 */
        else q = (int)(v <= 10 ? v * 10 : v);                   /* jpg1..jpg10 */
        if (q < 0) q = 0;
        if (q > 100 && strcmp(ext, "png")) q = 100;
      }
      if (pct < 1 || pct > 1000) continue;
      out[n].pct = pct; out[n].quality = q;
      snprintf(out[n].file, sizeof out[n].file, "%.*s.%s", (int)(dot - t), t, ext);
      n++;
    }
  }
  return n;
}
Box layer_bounds(const Doc *d, int i)
{
  const Layer *l;
  Box b = box_make(0, 0, 0, 0);
  if (i < 0 || i >= d->nlay) return b;
  l = d->lay[i];
  if (l->kind == LK_GROUP) {
    int j;
    b = box_make(d->w, d->h, 0, 0);
    for (j = block_start(d, i); j < i; j++) {
      Box c;
      if (d->lay[j]->depth != l->depth + 1 && d->lay[j]->kind == LK_GROUP) continue;
      c = layer_bounds(d, j);
      if (box_empty(c)) continue;
      if (c.x0 < b.x0) b.x0 = c.x0;
      if (c.y0 < b.y0) b.y0 = c.y0;
      if (c.x1 > b.x1) b.x1 = c.x1;
      if (c.y1 > b.y1) b.y1 = c.y1;
    }
    if (l->artboard) b = l->abox;
    if (box_empty(b)) b = box_make(0, 0, 0, 0);
    return b;
  }
  if (l->px) { if (!layer_content_box(d, i, &b)) b = box_make(0, 0, 0, 0); return b; }
  if ((l->kind == LK_TEXT || l->kind == LK_SHAPE) && !box_empty(l->livebox)) return box_clip(l->livebox, doc_box((Doc *)d));
  if (l->kind == LK_FILL || l->kind == LK_ADJUST) return doc_box((Doc *)d);
  return b;
}
/* layer i (a group: its block) alone, composited */
static u8 *layer_alone(Doc *d, int i)
{
  u8 vis[MAXLAYERS], *out;
  int j, lo = block_start(d, i);
  if (!(out = (u8 *)calloc((size_t)d->w * d->h, 4))) return NULL;
  for (j = 0; j < d->nlay; j++) { vis[j] = d->lay[j]->visible; if (j < lo || j > i) d->lay[j]->visible = 0; }
  /* (the groups i is in stay shown) */
  for (j = layer_parent(d, i); j >= 0; j = layer_parent(d, j)) d->lay[j]->visible = vis[j];
  doc_composite(d, doc_box(d), out, (long)d->w * 4);
  for (j = 0; j < d->nlay; j++) d->lay[j]->visible = vis[j];
  return out;
}
int assets_generate(Doc *d, const char *drawer, char *log, size_t logcap)
{
  int i, files = 0;
  if (log && logcap) log[0] = 0;
  for (i = 0; i < d->nlay; i++) {
    AssetSpec sp[8];
    int ns = assets_parse(d->lay[i]->name, sp, 8), k;
    Box b;
    u8 *all;
    if (!ns) continue;
    b = layer_bounds(d, i);
    if (box_empty(b) || !(all = layer_alone(d, i))) continue;
    for (k = 0; k < ns; k++) {
      Rgba src;
      Doc *t;
      i32 y, nw = (i32)((double)(b.x1 - b.x0) * sp[k].pct / 100 + 0.5), nh = (i32)((double)(b.y1 - b.y0) * sp[k].pct / 100 + 0.5);
      BatchOpts bo;
      char err[80], path[512];
      u8 *data;
      size_t len = 0;
      const char *ext = strrchr(sp[k].file, '.') + 1;
      if (nw < 1) nw = 1;
      if (nh < 1) nh = 1;
      if (!rgba_alloc(&src, b.x1 - b.x0, b.y1 - b.y0)) continue;
      for (y = b.y0; y < b.y1; y++) memcpy(src.px + (long)(y - b.y0) * src.w * 4, all + ((long)y * d->w + b.x0) * 4, (size_t)src.w * 4);
      if (!(src.px = scale_px(src.px, src.w, src.h, nw, nh))) continue;
      src.w = nw; src.h = nh;
      if (!(t = doc_adopt(src.w, src.h, src.px))) { rgba_free(&src); continue; }
      t->dpi = d->dpi;
      batch_opts_default(&bo);
      bo.format = batch_format_of_path(sp[k].file);
      if (!strcmp(ext, "jpg") || !strcmp(ext, "jpeg")) bo.quality = sp[k].quality ? sp[k].quality : 90;
      if (!strcmp(ext, "gif")) bo.colors = 256;
      if (!strcmp(ext, "png") && sp[k].quality == 8) {
        SfwOpts so;
        sfw_defaults(&so); so.fmt = SFW_PNG8; so.colors = 256;
        data = sfw_encode(t, &so, &len);
      } else data = batch_encode(t, &bo, &len, err, sizeof err);
      path_join(path, sizeof path, drawer, sp[k].file);
      if (data && file_write(path, data, len)) {
        files++;
        if (log) { size_t l = strlen(log); snprintf(log + l, logcap - l, "%s (%ld x %ld)\n", sp[k].file, (long)t->w, (long)t->h); }
      }
      free(data);
      doc_free(t);
    }
    free(all);
  }
  return files;
}

int layer_to_file(Doc *d, int i, const char *path)
{
  Box b = layer_bounds(d, i);
  u8 *all, *px;
  Doc *t;
  BatchOpts bo;
  i32 y;
  u8 *data;
  size_t len = 0;
  char err[80];
  int ok;
  if (box_empty(b) || !(all = layer_alone(d, i))) return 0;
  px = (u8 *)malloc((size_t)(b.x1 - b.x0) * (b.y1 - b.y0) * 4);
  if (px) for (y = b.y0; y < b.y1; y++) memcpy(px + (long)(y - b.y0) * (b.x1 - b.x0) * 4, all + ((long)y * d->w + b.x0) * 4, (size_t)(b.x1 - b.x0) * 4);
  free(all);
  if (!px) return 0;
  if (!(t = doc_adopt(b.x1 - b.x0, b.y1 - b.y0, px))) { free(px); return 0; }
  t->dpi = d->dpi;
  batch_opts_default(&bo);
  bo.format = batch_format_of_path(path);
  if (bo.format == FMT_UNKNOWN) bo.format = FMT_PNG;
  data = batch_encode(t, &bo, &len, err, sizeof err);
  ok = data && file_write(path, data, len);
  free(data);
  doc_free(t);
  return ok;
}

/* ================================================================== Find and Replace Text */
static int word_char(int c) { return isalnum(c) || c == '_' || c >= 0x80; }
static int match_at(const char *s, size_t n, size_t at, const char *find, size_t fl, int match_case, int whole)
{
  if (at + fl > n) return 0;
  if (match_case ? memcmp(s + at, find, fl) : str_ncasecmp(s + at, find, fl)) return 0;
  if (whole && ((at > 0 && word_char((unsigned char)s[at - 1])) || (at + fl < n && word_char((unsigned char)s[at + fl])))) return 0;
  return 1;
}
int type_find_next(Doc *d, int from, int at, const char *find, int match_case, int whole, int *layer, int *pos)
{
  size_t fl = strlen(find);
  int i;
  if (!fl) return 0;
  for (i = from < 0 ? 0 : from; i < d->nlay; i++) {
    const TextParams *tp = layer_text(d->lay[i]);
    size_t n, k;
    if (!tp) continue;
    n = strlen(tp->text);
    for (k = i == from ? (size_t)(at < 0 ? 0 : at) : 0; k + fl <= n; k++)
      if (match_at(tp->text, n, k, find, fl, match_case, whole)) { *layer = i; *pos = (int)k; return 1; }
  }
  return 0;
}
int type_find_replace(Doc *d, int only, const char *find, const char *repl, int match_case, int whole, int all)
{
  size_t fl = strlen(find), rl = strlen(repl);
  int i, count = 0;
  TextParams *tp = NULL;
  if (!fl) return 0;
  doc_begin(d, "Replace Text");
  for (i = 0; i < d->nlay; i++) {
    const TextParams *cur;
    size_t k;
    int changed = 0;
    if ((only >= 0 && i != only) || !(cur = layer_text(d->lay[i]))) continue;
    if (!tp && !(tp = (TextParams *)malloc(sizeof *tp))) break;
    *tp = *cur;
    for (k = 0; k + fl <= strlen(tp->text);) {
      if (match_at(tp->text, strlen(tp->text), k, find, fl, match_case, whole)) {
        if (strlen(tp->text) - fl + rl >= sizeof tp->text) break;
        text_delete(tp, (int)k, (int)(k + fl));
        if (rl && !text_insert(tp, (int)k, repl, (int)rl)) break;
        k += rl; count++; changed = 1;
        if (!all) break;
      } else k++;
    }
    if (changed) doc_set_text(d, i, tp);
    if (changed && !all) break;
  }
  free(tp);
  doc_end(d);
  return count;
}

/* ================================================================== Apply Image and Calculations */
const char *const ac_names[AC_N + 1] = { "RGB", "Red", "Green", "Blue", "Transparency", "Gray", "Selection", NULL };
/* the source's pixels (w * h * 4) and whether they must be freed */
static const u8 *source_pixels(const AiSource *s, u8 **tofree)
{
  Doc *d = s->doc;
  *tofree = NULL;
  if (s->layer >= 0 && s->layer < d->nlay && d->lay[s->layer]->px) return d->lay[s->layer]->px;
  if (s->layer >= 0 && s->layer < d->nlay) return *tofree = layer_alone(d, s->layer);
  return *tofree = doc_flat_rgba(d);
}
/* one pixel of the source as RGBA (a single channel as grey) */
static void source_px(const AiSource *s, const u8 *px, long k, u8 *out)
{
  const u8 *p = px + k * 4;
  int c = s->channel, v;
  switch (c) {
  case AC_RED: case AC_GREEN: case AC_BLUE: v = p[c - AC_RED]; break;
  case AC_ALPHA: v = p[3]; break;
  case AC_GRAY: v = (p[0] * 77 + p[1] * 150 + p[2] * 29 + 128) >> 8; break;
  case AC_SEL: v = s->doc->sel ? s->doc->sel[k] : 255; break;
  default: v = -1;
  }
  if (v < 0) { out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = c == AC_RGB ? 255 : p[3]; }
  else { out[0] = out[1] = out[2] = (u8)v; out[3] = 255; }
  if (s->invert) { out[0] = (u8)(255 - out[0]); out[1] = (u8)(255 - out[1]); out[2] = (u8)(255 - out[2]); }
}
int apply_image(Doc *d, const AiSource *s, int blend, int opacity, int keep, const u8 *mask)
{
  Layer *l = active_pixels(d);
  const u8 *src;
  u8 *tofree;
  Box b;
  i32 x, y;
  int op = opacity * 255 / 100;
  if (!l || !s->doc || s->doc->w != d->w || s->doc->h != d->h) return 0;
  if (!(src = source_pixels(s, &tofree))) return 0;
  b = d->sel ? d->selbox : doc_box(d);
  doc_begin(d, "Apply Image");
  doc_note_layer(d, d->active, b);
  for (y = b.y0; y < b.y1; y++) for (x = b.x0; x < b.x1; x++) {
    long k = (long)y * d->w + x;
    u8 *p = l->px + k * 4, q[4];
    int a = p[3], c = mul255(sel_at(d, x, y), mask ? mask[k] : 255);
    if (!c) continue;
    source_px(s, src, k, q);
    q[3] = (u8)mul255(q[3], c);
    blend_pixel_at(p, q, op, blend, x, y);
    if (keep) { if (!a) p[0] = p[1] = p[2] = 0; p[3] = (u8)a; }
  }
  doc_end(d);
  free(tofree);
  return 1;
}
u8 *calculations(const AiSource *a, const AiSource *b, int blend, int opacity, i32 w, i32 h)
{
  const u8 *pa, *pb;
  u8 *fa, *fb, *m;
  long k, n = (long)w * h;
  AiSource sa = *a, sb = *b;
  if (!a->doc || !b->doc || a->doc->w != w || a->doc->h != h || b->doc->w != w || b->doc->h != h) return NULL;
  if (sa.channel == AC_RGB) sa.channel = AC_GRAY;
  if (sb.channel == AC_RGB) sb.channel = AC_GRAY;
  pa = source_pixels(&sa, &fa); pb = source_pixels(&sb, &fb);
  if (!pa || !pb || !(m = (u8 *)malloc((size_t)n))) { free(fa); free(fb); return NULL; }
  for (k = 0; k < n; k++) {
    u8 x[4], y[4];
    source_px(&sb, pb, k, x);                 /* Source 2 is underneath, as in Photoshop */
    source_px(&sa, pa, k, y);
    blend_pixel(x, y, opacity * 255 / 100, blend);
    m[k] = x[0];
  }
  free(fa); free(fb);
  return m;
}
int calc_result(Doc *d, u8 *m, int as_selection, const char *name)
{
  if (!m) return -1;
  if (as_selection) { sel_apply_mask(d, m, SEL_REPLACE, "Calculations"); return -2; }
  if (d->nchan >= MAXCHANNELS) { free(m); return -1; }
  doc_begin(d, "Calculations");
  doc_note_channels(d);
  d->chan[d->nchan] = m;
  snprintf(d->chan_name[d->nchan], sizeof d->chan_name[0], "%s", name && *name ? name : "Alpha");
  d->nchan++;
  doc_end(d);
  return d->nchan - 1;
}

/* ================================================================== Analysis */
#define MAXSCALES 16
static struct { const Doc *d; MeasureScale s; } scales[MAXSCALES];
void measure_scale_get(const Doc *d, MeasureScale *s)
{
  int i;
  for (i = 0; i < MAXSCALES; i++) if (scales[i].d == d && d) { *s = scales[i].s; return; }
  s->px = 1; s->len = 1; strcpy(s->unit, "pixels");
}
void measure_scale_set(const Doc *d, const MeasureScale *s)
{
  int i, f = -1;
  for (i = 0; i < MAXSCALES; i++) { if (scales[i].d == d) { f = i; break; } if (f < 0 && !scales[i].d) f = i; }
  if (f < 0) f = 0;
  scales[f].d = d; scales[f].s = *s;
  if (scales[f].s.px <= 0) scales[f].s.px = 1;
  if (scales[f].s.len <= 0) scales[f].s.len = 1;
}
void measure_forget(const Doc *d)
{
  int i;
  for (i = 0; i < MAXSCALES; i++) if (scales[i].d == d) scales[i].d = NULL;
}
double measure_len(const Doc *d, double px)
{
  MeasureScale s;
  measure_scale_get(d, &s);
  return px * s.len / s.px;
}
static void num(char *out, double v)
{
  long t = (long)(v * 100 + (v < 0 ? -0.5 : 0.5));
  sprintf(out, "%s%ld.%02ld", t < 0 ? "-" : "", labs(t) / 100, labs(t) % 100);
}
static int nrecords;
int measure_record(Doc *d, const double *ruler, void *logbuf)
{
  Buf *b = (Buf *)logbuf;
  MeasureScale s;
  char a[24], p[24], w[24], h[24], cx[24], cy[24], rl[24], ra[24];
  double area = 0, per = 0, sx = 0, sy = 0, f;
  Box bb = box_make(0, 0, 0, 0);
  int counts = 0;
  Annots *an = doc_annots(d, 0);
  measure_scale_get(d, &s);
  f = s.len / s.px;
  if (d->sel && sel_bounds(d, &bb)) {
    i32 x, y;
    for (y = bb.y0; y < bb.y1; y++) for (x = bb.x0; x < bb.x1; x++) {
      int v = d->sel[(long)y * d->w + x];
      if (!v) continue;
      area += v / 255.0; sx += (x + 0.5) * v / 255.0; sy += (y + 0.5) * v / 255.0;
      if (v >= 128) {
        if (x == 0 || d->sel[(long)y * d->w + x - 1] < 128) per++;
        if (x == d->w - 1 || d->sel[(long)y * d->w + x + 1] < 128) per++;
        if (y == 0 || d->sel[(long)(y - 1) * d->w + x] < 128) per++;
        if (y == d->h - 1 || d->sel[(long)(y + 1) * d->w + x] < 128) per++;
      }
    }
  } else if (!d->sel) { bb = doc_box(d); area = (double)d->w * d->h; per = 2.0 * (d->w + d->h); sx = d->w * area / 2; sy = d->h * area / 2; }
  if (area > 0) { sx /= area; sy /= area; }
  if (an) counts = an->nmark;
  if (!nrecords++) buf_adds(b, "Record\tArea\tPerimeter\tWidth\tHeight\tCentroid X\tCentroid Y\tLength\tAngle\tCount\tScale\n");
  num(a, area * f * f); num(p, per * f); num(w, (bb.x1 - bb.x0) * f); num(h, (bb.y1 - bb.y0) * f); num(cx, sx * f); num(cy, sy * f);
  rl[0] = ra[0] = 0;
  if (ruler) {
    double dx = ruler[2] - ruler[0], dy = ruler[3] - ruler[1];
    num(rl, sqrt(dx * dx + dy * dy) * f); num(ra, atan2(-dy, dx) * 180 / M_PI);
  }
  buf_printf(b, "%d\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%d\t%s\n", nrecords, a, p, w, h, cx, cy, rl, ra, counts, s.unit);
  return nrecords;
}
int scale_marker(Doc *d, double len, int white, int height)
{
  MeasureScale s;
  Layer *l;
  i32 x, y, bw, x0, y0;
  u8 c = white ? 255 : 0;
  measure_scale_get(d, &s);
  bw = (i32)(len * s.px / s.len + 0.5);
  if (height < 1) height = 4;
  if (bw < 1 || bw > d->w || d->nlay >= MAXLAYERS) return -1;
  if (!(l = layer_new(d, "Measurement Scale Marker"))) return -1;
  x0 = d->w / 20 + 1; y0 = d->h - d->h / 20 - height - 1;
  if (y0 < 0) y0 = 0;
  for (y = y0; y < y0 + height && y < d->h; y++) for (x = x0; x < x0 + bw && x < d->w; x++) {
    u8 *p = l->px + ((long)y * d->w + x) * 4;
    p[0] = p[1] = p[2] = c; p[3] = 255;
  }
  /* the ticks at the ends */
  for (y = y0 - height; y < y0; y++) if (y >= 0) {
    i32 xs[2]; int k;
    xs[0] = x0; xs[1] = x0 + bw - 1;
    for (k = 0; k < 2; k++) if (xs[k] < d->w) { u8 *p = l->px + ((long)y * d->w + xs[k]) * 4; p[0] = p[1] = p[2] = c; p[3] = 255; }
  }
  if (!doc_add_layer(d, l, d->nlay)) return -1;
  snprintf(d->last_action, sizeof d->last_action, "Place Scale Marker");
  return d->active;
}

/* ================================================================== slices, artboards, frames */
int slice_from_layer(Doc *d, int i)
{
  Box b = layer_bounds(d, i);
  int k;
  if (box_empty(b)) return -1;
  k = slice_add(d, b);
  if (k >= 0) { Annots *a = doc_annots(d, 0); if (a) snprintf(a->slice[k].name, sizeof a->slice[k].name, "%.31s", d->lay[i]->name); }
  return k;
}
int artboard_from_layers(Doc *d)
{
  Box b = box_make(d->w, d->h, 0, 0);
  int i, g, any = 0;
  for (i = 0; i < d->nlay; i++) {
    Box c;
    if (!(d->lay[i]->picked || i == d->active)) continue;
    c = layer_bounds(d, i);
    if (box_empty(c)) continue;
    any = 1;
    if (c.x0 < b.x0) b.x0 = c.x0;
    if (c.y0 < b.y0) b.y0 = c.y0;
    if (c.x1 > b.x1) b.x1 = c.x1;
    if (c.y1 > b.y1) b.y1 = c.y1;
  }
  if (!any) b = doc_box(d);
  doc_begin(d, "Artboard from Layers");
  g = -1;
  if (doc_group_layers(d)) {
    for (g = d->nlay - 1; g >= 0; g--) if (g == d->active && d->lay[g]->kind == LK_GROUP) break;
    if (g >= 0 && !doc_artboard_from_group(d, g, b, -1)) g = -1;
  }
  doc_end(d);
  return g;
}
int frame_from_layer(Doc *d, int i)
{
  Box b = layer_bounds(d, i);
  int f, r;
  if (box_empty(b) || !d->lay[i]->px) return -1;
  doc_begin(d, "Frame from Layers");
  f = doc_new_frame(d, b, FR_RECT, 0, 0);
  if (f < 0) { doc_end(d); return -1; }
  if (i >= f) i++;          /* (the frame went in above the active layer: below it nothing moved) */
  if (i >= d->nlay || !d->lay[i]->px) { i = -1; }
  r = i >= 0 ? doc_frame_place(d, f, i, 1) : f;
  doc_end(d);
  return r;
}

/* ================================================================== Open As */
const char *const openas_names[OA_N + 1] = { "PNG", "JPEG", "BMP", "IFF ILBM", "GIF", "Photoshop Raw", NULL };
void raw_guess(size_t n, RawOpts *r)
{
  long px, s;
  memset(r, 0, sizeof *r);
  r->channels = 3; r->interleaved = 1;
  if (n % 3) r->channels = n % 4 ? 1 : 4;
  px = (long)(n / (size_t)r->channels);
  s = (long)(sqrt((double)px) + 0.5);
  if (s * s == px) { r->w = r->h = (i32)s; return; }
  for (s = (long)sqrt((double)px); s > 1; s--) if (px % s == 0) break;
  r->w = (i32)(px / (s > 0 ? s : 1)); r->h = (i32)(s > 0 ? s : 1);
}
Doc *open_as(const u8 *data, size_t n, int f, const RawOpts *raw, char *err, size_t errcap)
{
  Rgba im;
  int ok = 0;
  Doc *d;
  memset(&im, 0, sizeof im);
  switch (f) {
  case OA_PNG: ok = png_read(data, n, &im, err, errcap); break;
  case OA_JPEG: ok = jpeg_read(data, n, &im, err, errcap); break;
  case OA_BMP: ok = bmp_read(data, n, &im, err, errcap); break;
  case OA_ILBM: ok = ilbm_read(data, n, &im, err, errcap); break;
  case OA_GIF: ok = gif_read(data, n, &im, err, errcap); break;
  case OA_RAW: {
    int ch = raw ? raw->channels : 0, bps = raw && raw->bits16 ? 2 : 1;
    long k, px;
    i32 w = raw ? raw->w : 0, h = raw ? raw->h : 0;
    if (!raw || ch < 1 || ch > 4 || w < 1 || h < 1 || w > MAXDIM || h > MAXDIM || raw->header < 0) { codec_err(err, errcap, "Give the width, height and channels"); return NULL; }
    px = (long)w * h;
    if ((double)raw->header + (double)px * ch * bps > (double)n) { codec_err(err, errcap, "The file is smaller than that size"); return NULL; }
    if (!rgba_alloc(&im, w, h)) { codec_err(err, errcap, "Not enough memory"); return NULL; }
    for (k = 0; k < px; k++) {
      int c, v[4] = { 0, 0, 0, 255 };
      for (c = 0; c < ch; c++) {
        long at = raw->header + (raw->interleaved ? (k * ch + c) : (c * px + k)) * bps;
        v[c] = bps == 2 ? data[at + (raw->bigendian ? 0 : 1)] : data[at];
      }
      if (ch <= 2) { im.px[k * 4] = im.px[k * 4 + 1] = im.px[k * 4 + 2] = (u8)v[0]; im.px[k * 4 + 3] = (u8)(ch == 2 ? v[1] : 255); }
      else { im.px[k * 4] = (u8)v[0]; im.px[k * 4 + 1] = (u8)v[1]; im.px[k * 4 + 2] = (u8)v[2]; im.px[k * 4 + 3] = (u8)(ch == 4 ? v[3] : 255); }
    }
    ok = 1;
    break; }
  default: codec_err(err, errcap, "Unknown format"); return NULL;
  }
  if (!ok) return NULL;
  if (!(d = doc_adopt(im.w, im.h, im.px))) { rgba_free(&im); codec_err(err, errcap, "Not enough memory"); return NULL; }
  return d;
}

/* ================================================================== keys */
int blend_step(int mode, int dir)
{
  /* the Layers panel's order (Photoshop's groups) */
  static const int order[] = { BM_NORMAL, BM_DISSOLVE, BM_DARKEN, BM_MULTIPLY, BM_BURN, BM_LINEARBURN, BM_DARKERCOLOR,
    BM_LIGHTEN, BM_SCREEN, BM_DODGE, BM_ADD, BM_LIGHTERCOLOR, BM_OVERLAY, BM_SOFTLIGHT, BM_HARDLIGHT, BM_VIVIDLIGHT,
    BM_LINEARLIGHT, BM_PINLIGHT, BM_HARDMIX, BM_DIFFERENCE, BM_EXCLUSION, BM_SUBTRACT, BM_DIVIDE, BM_HUE, BM_SATURATION,
    BM_COLOR, BM_LUMINOSITY };
  int n = (int)(sizeof order / sizeof order[0]), i;
  for (i = 0; i < n; i++) if (order[i] == mode) break;
  if (i == n) return BM_NORMAL;
  i += dir > 0 ? 1 : -1;
  if (i < 0) i = 0;
  if (i >= n) i = n - 1;
  return order[i];
}
int opacity_key(OpacityKeys *k, int digit, unsigned long ms)
{
  if (digit < 0 || digit > 9) return -1;
  if (k->pending && ms - k->ms <= 333) {
    int v = k->first * 10 + digit;
    k->pending = 0;
    if (k->first == 0 && digit == 0) return 0;   /* "00" */
    return v;
  }
  k->pending = 1; k->first = digit; k->ms = ms;
  return digit == 0 ? 100 : digit * 10;
}

void toolkeys_format(const signed char *k, char *out, size_t cap)
{
  size_t n = 0;
  int i;
  if (cap) out[0] = 0;
  for (i = 0; i < 26 && n + 5 < cap; i++) n += (size_t)sprintf(out + n, "%s%d", i ? "," : "", k[i] < 0 ? 0 : k[i]);
}
int toolkeys_parse(const char *s, signed char *k, int ntools)
{
  signed char t[26];
  int i;
  for (i = 0; i < 26; i++) {
    char *e;
    long v = strtol(s, &e, 10);
    if (e == s) return 0;
    t[i] = (signed char)(v >= 1 && v <= ntools && v < 127 ? v : 0);
    s = e;
    if (i < 25) { if (*s != ',') return 0; s++; }
  }
  memcpy(k, t, 26);
  return 1;
}

/* ================================================================== snapping to layers and slices */
static struct { const Doc *d; const void *u, *r; int nlay, active; i32 v[3 * (MAXLAYERS + MAXSLICES)], h[3 * (MAXLAYERS + MAXSLICES)]; int nv, nh, flags; } sc;
static int snap_layers_hook(Doc *d, int vertical, int flags, double *out, int max)
{
  int i, n = 0;
  if (!(flags & (SNAP_LAYERS | SNAP_SLICES))) return 0;
  if (sc.d != d || sc.u != (const void *)d->undo || sc.r != (const void *)d->redo || sc.nlay != d->nlay || sc.active != d->active || sc.flags != flags) {
    sc.d = d; sc.u = d->undo; sc.r = d->redo; sc.nlay = d->nlay; sc.active = d->active; sc.flags = flags; sc.nv = sc.nh = 0;
    if (flags & SNAP_LAYERS) for (i = 0; i < d->nlay; i++) {
      Box b;
      if (i == d->active || !layer_shown(d, i) || d->lay[i]->kind == LK_GROUP || d->lay[i]->kind == LK_ADJUST) continue;
      b = layer_bounds(d, i);
      if (box_empty(b) || (b.x0 == 0 && b.y0 == 0 && b.x1 == d->w && b.y1 == d->h)) continue;
      sc.v[sc.nv++] = b.x0; sc.v[sc.nv++] = b.x1; sc.v[sc.nv++] = (b.x0 + b.x1) / 2;
      sc.h[sc.nh++] = b.y0; sc.h[sc.nh++] = b.y1; sc.h[sc.nh++] = (b.y0 + b.y1) / 2;
    }
    if (flags & SNAP_SLICES) {
      Annots *a = doc_annots(d, 0);
      for (i = 0; a && i < a->nslice; i++) {
        sc.v[sc.nv++] = a->slice[i].b.x0; sc.v[sc.nv++] = a->slice[i].b.x1;
        sc.h[sc.nh++] = a->slice[i].b.y0; sc.h[sc.nh++] = a->slice[i].b.y1;
      }
    }
  }
  for (i = 0; i < (vertical ? sc.nv : sc.nh) && n < max; i++) out[n++] = vertical ? sc.v[i] : sc.h[i];
  return n;
}
void menu3_init(void) { snap_extra = snap_layers_hook; }

/* ================================================================== a grabbed screen */
Doc *doc_from_screen(const u8 *pens, const u8 *pal, int npal, const u8 *rgb, i32 w, i32 h, int keep_palette)
{
  u8 *px;
  long k, n = (long)w * h;
  Doc *d;
  if (w < 1 || h < 1 || w > MAXDIM || h > MAXDIM || !(px = (u8 *)malloc((size_t)n * 4))) return NULL;
  for (k = 0; k < n; k++) {
    if (pens) { int c = pens[k] < npal ? pens[k] : 0; px[k * 4] = pal[c * 3]; px[k * 4 + 1] = pal[c * 3 + 1]; px[k * 4 + 2] = pal[c * 3 + 2]; }
    else { px[k * 4] = rgb[k * 3]; px[k * 4 + 1] = rgb[k * 3 + 1]; px[k * 4 + 2] = rgb[k * 3 + 2]; }
    px[k * 4 + 3] = 255;
  }
  if (!(d = doc_adopt(w, h, px))) { free(px); return NULL; }
  if (pens && keep_palette && npal >= 2 && npal <= 256) doc_to_indexed(d, npal, QM_FIXED, DI_NONE, pal, npal);
  doc_forget_undo(d);
  d->dirty = 0;
  return d;
}
