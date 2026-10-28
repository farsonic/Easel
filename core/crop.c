/*
 * crop.c - crop presets, rotated crop, straightening and the Ruler (see crop.h).
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "crop.h"
#include "paint.h"
#include "xform.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ presets */
const CropPreset crop_presets[] = {
  { "Ratio", CP_FREE, 0, 0, 0 },
  { "Original Ratio", CP_ORIGINAL, 0, 0, 0 },
  { "1 : 1 (Square)", CP_RATIO, 1, 1, 0 },
  { "4 : 5 (8 : 10)", CP_RATIO, 4, 5, 0 },
  { "5 : 7", CP_RATIO, 5, 7, 0 },
  { "2 : 3 (4 : 6)", CP_RATIO, 2, 3, 0 },
  { "4 : 3", CP_RATIO, 4, 3, 0 },
  { "16 : 9", CP_RATIO, 16, 9, 0 },
  { "16 : 10", CP_RATIO, 16, 10, 0 },
  { "320 x 256 (PAL lores)", CP_PIXELS, 320, 256, 0 },
  { "320 x 200 (NTSC lores)", CP_PIXELS, 320, 200, 0 },
  { "640 x 512 (PAL hires laced)", CP_PIXELS, 640, 512, 0 },
  { "640 x 480 px", CP_PIXELS, 640, 480, 0 },
  { "1024 x 768 px", CP_PIXELS, 1024, 768, 0 },
  { "1920 x 1080 px", CP_PIXELS, 1920, 1080, 0 },
  { "4 x 5 in 300 ppi", CP_INCHES, 4, 5, 300 },
  { "8.5 x 11 in 300 ppi", CP_INCHES, 8.5, 11, 300 },
};
const int ncrop_presets = (int)(sizeof crop_presets / sizeof crop_presets[0]);

double crop_preset_ratio(const Doc *d, const CropPreset *p)
{
  if (!p) return 0;
  switch (p->kind) {
  case CP_ORIGINAL: return (double)d->w / d->h;
  case CP_RATIO: case CP_PIXELS: case CP_INCHES: return p->w > 0 && p->h > 0 ? p->w / p->h : 0;
  }
  return 0;
}
static i32 rnd(double v) { return (i32)floor(v + 0.5); }
Box crop_preset_box(const Doc *d, const CropPreset *p, int swap, double ax, double ay, double px, double py, int from_centre)
{
  double r = crop_preset_ratio(d, p), dx = px - ax, dy = py - ay, w = fabs(dx), h = fabs(dy), x0, y0, x1, y1;
  Box b;
  if (r > 0 && swap) r = 1 / r;
  if (r > 0) {
    /* the box reaches the pointer on its farther side */
    if (w / r > h) h = w / r; else w = h * r;
  }
  if (from_centre) { x0 = ax - w; x1 = ax + w; y0 = ay - h; y1 = ay + h; }
  else {
    x0 = ax; x1 = ax + (dx < 0 ? -w : w); y0 = ay; y1 = ay + (dy < 0 ? -h : h);
    if (x1 < x0) { double t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { double t = y0; y0 = y1; y1 = t; }
  }
  b = box_make(rnd(x0), rnd(y0), rnd(x1), rnd(y1));
  return b;
}
int image_crop_preset(Doc *d, Box b, const CropPreset *p, int swap, int method)
{
  i32 tw = 0, th = 0;
  int ok;
  if (p && p->kind == CP_PIXELS) { tw = rnd(p->w); th = rnd(p->h); }
  else if (p && p->kind == CP_INCHES && p->dpi > 0) { tw = rnd(p->w * p->dpi); th = rnd(p->h * p->dpi); }
  if (swap) { i32 t = tw; tw = th; th = t; }
  doc_begin(d, "Crop");
  ok = image_crop(d, b);
  if (ok && tw > 0 && th > 0 && (tw != d->w || th != d->h)) ok = image_resize(d, tw, th, method);
  if (ok && p && p->kind == CP_INCHES && p->dpi > 0 && p->dpi < 65536) d->dpi = (u16)p->dpi;
  doc_end(d);
  return ok;
}

/* ------------------------------------------------------------------ rotated crop, straightening */
int image_crop_rotated(Doc *d, double cx, double cy, double w, double h, double angle, int interp)
{
  double a = angle * M_PI / 180, c = cos(a), s = sin(a), inv[9], ncx, ncy;
  i32 nw = rnd(w), nh = rnd(h);
  if (nw < 1 || nh < 1 || nw > MAXDIM || nh > MAXDIM) return 0;
  ncx = nw / 2.0; ncy = nh / 2.0;
  /* old = centre + R(angle) (new - new centre) */
  inv[0] = c; inv[1] = -s; inv[2] = cx - c * ncx + s * ncy;
  inv[3] = s; inv[4] = c; inv[5] = cy - s * ncx - c * ncy;
  inv[6] = 0; inv[7] = 0; inv[8] = 1;
  return image_remap(d, nw, nh, inv, interp, "Crop");
}
double straighten_angle(double x0, double y0, double x1, double y1)
{
  double a = atan2(y1 - y0, x1 - x0) * 180 / M_PI;
  while (a > 45) a -= 90;
  while (a < -45) a += 90;
  return a;
}
void rotated_inner_box(double w, double h, double angle, double *rw, double *rh)
{
  double a = angle * M_PI / 180, sa = fabs(sin(a)), ca = fabs(cos(a)), lng = w >= h ? w : h, sht = w >= h ? h : w;
  *rw = *rh = 0;
  if (w <= 0 || h <= 0) return;
  if (sa < 1e-12) { *rw = w; *rh = h; return; }
  if (sht <= 2 * sa * ca * lng || fabs(sa - ca) < 1e-10) {
    double x = 0.5 * sht;
    if (w >= h) { *rw = x / sa; *rh = x / ca; } else { *rw = x / ca; *rh = x / sa; }
  } else {
    double c2 = ca * ca - sa * sa;
    *rw = (w * ca - h * sa) / c2; *rh = (h * ca - w * sa) / c2;
  }
}
int image_straighten(Doc *d, double x0, double y0, double x1, double y1)
{
  double a, rw, rh;
  if ((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) < 4) return 0;
  a = straighten_angle(x0, y0, x1, y1);
  if (fabs(a) < 1e-3) return 0;
  rotated_inner_box(d->w, d->h, a, &rw, &rh);
  rw = floor(rw + 1e-6); rh = floor(rh + 1e-6);
  if (rw < 1 || rh < 1) return 0;
  {
    int ok;
    doc_begin(d, "Straighten");
    ok = image_crop_rotated(d, d->w / 2.0, d->h / 2.0, rw, rh, a, RS_BICUBIC);
    doc_end(d);
    return ok;
  }
}
int layer_straighten(Doc *d, double x0, double y0, double x1, double y1, int interp)
{
  XForm t;
  u8 *sel = d->sel;
  Box sb = d->selbox, r;
  double a;
  if ((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) < 4) return 0;
  a = straighten_angle(x0, y0, x1, y1);
  if (fabs(a) < 1e-3) return 0;
  xform_init(&t, doc_box(d));
  xform_rotate_about(&t, -a, d->w / 2.0, d->h / 2.0);
  /* the whole layer, whatever is selected */
  d->sel = NULL; d->selbox = doc_box(d);
  doc_begin(d, "Straighten Layer");
  r = xform_layer(d, d->active, &t, interp);
  doc_end(d);
  d->sel = sel; d->selbox = sb;
  return !box_empty(r);
}

/* ------------------------------------------------------------------ the Ruler */
void ruler_measure(double x0, double y0, double x1, double y1, RulerInfo *r)
{
  r->w = x1 - x0; r->h = y1 - y0;
  r->length = sqrt(r->w * r->w + r->h * r->h);
  r->angle = r->length > 0 ? atan2(-(y1 - y0), x1 - x0) * 180 / M_PI : 0;
}
double ruler_protractor(double xv, double yv, double x0, double y0, double x1, double y1)
{
  double ax = x0 - xv, ay = y0 - yv, bx = x1 - xv, by = y1 - yv, la = sqrt(ax * ax + ay * ay), lb = sqrt(bx * bx + by * by), c;
  if (la <= 0 || lb <= 0) return 0;
  c = (ax * bx + ay * by) / (la * lb);
  if (c > 1) c = 1;
  if (c < -1) c = -1;
  return acos(c) * 180 / M_PI;
}
