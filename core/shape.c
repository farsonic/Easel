/*
 * shape.c - shape layers and the shape generators (see shape.h). Easel 0.9.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "shape.h"
#include "live.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define KAPPA 0.5522847498307936          /* a quarter circle's handles, as a part of its radius */

void shape_defaults(ShapeParams *sp, int kind, double x0, double y0, double x1, double y1, u32 fill)
{
  memset(sp, 0, sizeof *sp);
  sp->kind = (u8)kind;
  sp->fill_on = 1; sp->fill_color = fill & 0xFFFFFF;
  sp->stroke_color = 0; sp->stroke_width = 3;
  sp->antialias = 1; sp->join = JOIN_MITER; sp->cap = CAP_BUTT; sp->align = SA_CENTER;
  sp->x0 = x0; sp->y0 = y0; sp->x1 = x1; sp->y1 = y1;
  sp->sides = 5; sp->indent = 50;
  sp->line_width = 4;
  if (kind == SH_ROUNDRECT) {
    double w = fabs(x1 - x0), h = fabs(y1 - y0);
    sp->radius = (w < h ? w : h) / 8;
  }
}
static void order(double *a, double *b) { if (*a > *b) { double t = *a; *a = *b; *b = t; } }
int shape_rect_path(Path *p, double x0, double y0, double x1, double y1, double r)
{
  double k;
  order(&x0, &x1); order(&y0, &y1);
  if (r > (x1 - x0) / 2) r = (x1 - x0) / 2;
  if (r > (y1 - y0) / 2) r = (y1 - y0) / 2;
  if (r <= 0) {
    return path_move_to(p, x0, y0) && path_line_to(p, x1, y0) && path_line_to(p, x1, y1) && path_line_to(p, x0, y1) && (path_close(p), 1);
  }
  k = r * (1 - KAPPA);
  if (!path_move_to(p, x0 + r, y0) || !path_line_to(p, x1 - r, y0) ||
      !path_curve_to(p, x1 - k, y0, x1, y0 + k, x1, y0 + r) || !path_line_to(p, x1, y1 - r) ||
      !path_curve_to(p, x1, y1 - k, x1 - k, y1, x1 - r, y1) || !path_line_to(p, x0 + r, y1) ||
      !path_curve_to(p, x0 + k, y1, x0, y1 - k, x0, y1 - r) || !path_line_to(p, x0, y0 + r) ||
      !path_curve_to(p, x0, y0 + k, x0 + k, y0, x0 + r, y0)) return 0;
  path_close(p);
  return 1;
}
int shape_ellipse_path(Path *p, double x0, double y0, double x1, double y1)
{
  double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, rx = fabs(x1 - x0) / 2, ry = fabs(y1 - y0) / 2, kx = rx * KAPPA, ky = ry * KAPPA;
  if (!path_move_to(p, cx, cy - ry) ||
      !path_curve_to(p, cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy) ||
      !path_curve_to(p, cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry) ||
      !path_curve_to(p, cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy) ||
      !path_curve_to(p, cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry)) return 0;
  path_close(p);
  {
    i32 i;
    for (i = p->na - 4; i < p->na; i++) if (i >= 0) p->a[i].type = PA_SMOOTH;
  }
  return 1;
}
int shape_triangle_path(Path *p, double x0, double y0, double x1, double y1)
{
  order(&x0, &x1); order(&y0, &y1);
  if (!path_move_to(p, (x0 + x1) / 2, y0) || !path_line_to(p, x1, y1) || !path_line_to(p, x0, y1)) return 0;
  path_close(p);
  return 1;
}
int shape_polygon_path(Path *p, double x0, double y0, double x1, double y1, int sides, int star, double indent, double angle)
{
  double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, rx = fabs(x1 - x0) / 2, ry = fabs(y1 - y0) / 2, in;
  int n, i;
  if (sides < 3) sides = 3;
  if (sides > 100) sides = 100;
  if (indent < 0) indent = 0;
  if (indent > 99) indent = 99;
  in = 1 - indent / 100;
  n = star ? 2 * sides : sides;
  for (i = 0; i < n; i++) {
    double a = (angle + 360.0 * i / n) * M_PI / 180, f = star && (i & 1) ? in : 1;
    double x = cx + rx * f * sin(a), y = cy - ry * f * cos(a);
    if (!(i ? path_line_to(p, x, y) : path_move_to(p, x, y) != 0)) return 0;
  }
  path_close(p);
  return 1;
}
int shape_line_path(Path *p, double x0, double y0, double x1, double y1, double w, int as, int ae, double aw, double al, double concave)
{
  double dx = x1 - x0, dy = y1 - y0, L = hypot(dx, dy), hw = w / 2, ah = aw / 2, nx, ny, j;
  double pt[2 * 12];
  int n = 0, i;
  if (L <= 0 || w <= 0) return 1;
  dx /= L; dy /= L; nx = -dy; ny = dx;
  if (ah < hw) ah = hw;
  if (concave < 0) concave = 0;
  if (concave > 50) concave = 50;
  if (al * ((as != 0) + (ae != 0)) > L) al = L / ((as != 0) + (ae != 0));
  j = al * (1 - concave / 100);
#define PT(x, y) (pt[2 * n] = (x), pt[2 * n + 1] = (y), n++)
  /* the + side forwards, then the - side back */
  if (as) { PT(x0, y0); PT(x0 + dx * al + nx * ah, y0 + dy * al + ny * ah); PT(x0 + dx * j + nx * hw, y0 + dy * j + ny * hw); }
  else PT(x0 + nx * hw, y0 + ny * hw);
  if (ae) { PT(x1 - dx * j + nx * hw, y1 - dy * j + ny * hw); PT(x1 - dx * al + nx * ah, y1 - dy * al + ny * ah); PT(x1, y1); }
  else PT(x1 + nx * hw, y1 + ny * hw);
  if (ae) { PT(x1 - dx * al - nx * ah, y1 - dy * al - ny * ah); PT(x1 - dx * j - nx * hw, y1 - dy * j - ny * hw); }
  else PT(x1 - nx * hw, y1 - ny * hw);
  if (as) { PT(x0 + dx * j - nx * hw, y0 + dy * j - ny * hw); PT(x0 + dx * al - nx * ah, y0 + dy * al - ny * ah); }
  else PT(x0 - nx * hw, y0 - ny * hw);
#undef PT
  for (i = 0; i < n; i++) if (!(i ? path_line_to(p, pt[2 * i], pt[2 * i + 1]) : path_move_to(p, pt[0], pt[1]) != 0)) return 0;
  path_close(p);
  return 1;
}
int shape_make_path(const ShapeParams *sp, Path *p)
{
  switch (sp->kind) {
  case SH_RECT: return shape_rect_path(p, sp->x0, sp->y0, sp->x1, sp->y1, sp->radius);
  case SH_ROUNDRECT: return shape_rect_path(p, sp->x0, sp->y0, sp->x1, sp->y1, sp->radius);
  case SH_ELLIPSE: return shape_ellipse_path(p, sp->x0, sp->y0, sp->x1, sp->y1);
  case SH_TRIANGLE: return shape_triangle_path(p, sp->x0, sp->y0, sp->x1, sp->y1);
  case SH_POLYGON: return shape_polygon_path(p, sp->x0, sp->y0, sp->x1, sp->y1, sp->sides, sp->star, sp->indent, sp->angle);
  case SH_LINE: {
    double w = sp->line_width > 0 ? sp->line_width : 1;
    double aw = (sp->arrow_w > 0 ? sp->arrow_w : 500) * w / 100, al = (sp->arrow_len > 0 ? sp->arrow_len : 1000) * w / 100;
    return shape_line_path(p, sp->x0, sp->y0, sp->x1, sp->y1, w, sp->arrow_start, sp->arrow_end, aw, al, sp->arrow_concave);
  }
  default: return 1;
  }
}

/* ------------------------------------------------------------------ the block */
ShapeParams *shape_alloc(const ShapeParams *sp, const Path *path, u32 *len)
{
  Path *gen = NULL;
  ShapeParams *b;
  size_t n;
  if (!path) {
    if (!(gen = path_new()) || !shape_make_path(sp, gen)) { path_free(gen); return NULL; }
    path = gen;
  }
  n = sizeof(ShapeParams) + (size_t)path->na * sizeof(PathAnchor) + (size_t)path->ns * sizeof(SubPath);
  if (!(b = (ShapeParams *)malloc(n))) { path_free(gen); return NULL; }
  *b = *sp;
  b->na = path->na; b->ns = path->ns; b->evenodd_path = path->evenodd;
  if (path->na) memcpy((u8 *)b + sizeof(ShapeParams), path->a, (size_t)path->na * sizeof(PathAnchor));
  if (path->ns) memcpy((u8 *)b + sizeof(ShapeParams) + (size_t)path->na * sizeof(PathAnchor), path->s, (size_t)path->ns * sizeof(SubPath));
  path_free(gen);
  *len = (u32)n;
  return b;
}
Path shape_path(const ShapeParams *sp)
{
  Path p;
  memset(&p, 0, sizeof p);
  p.view = 1;
  p.na = p.capa = sp->na; p.ns = p.caps = sp->ns;
  p.a = sp->na ? (PathAnchor *)((u8 *)sp + sizeof(ShapeParams)) : NULL;
  p.s = sp->ns ? (SubPath *)((u8 *)sp + sizeof(ShapeParams) + (size_t)sp->na * sizeof(PathAnchor)) : NULL;
  p.evenodd = sp->evenodd;
  return p;
}
const ShapeParams *layer_shape(const Layer *l)
{
  return l && l->kind == LK_SHAPE && l->live && l->livelen >= sizeof(ShapeParams) ? (const ShapeParams *)l->live : NULL;
}

/* ------------------------------------------------------------------ rendering */
int shape_render(const ShapeParams *sp, i32 w, i32 h, u8 **px, Box *bx)
{
  Path p = shape_path(sp);
  Poly fill, st;
  Box b;
  u8 *fc = NULL, *sc = NULL, *o = NULL;
  long n, k;
  int ok = 0, stroke = sp->stroke_on && sp->stroke_width > 0, filled = sp->fill_on;
  double sw = sp->stroke_width;
  *px = NULL; *bx = box_make(0, 0, 0, 0);
  if (!p.na || (!stroke && !filled)) return 1;
  poly_init(&fill); poly_init(&st);
  if (!path_flatten(&p, 0.05, &fill)) goto done;
  b = poly_box(&fill);
  if (stroke) {
    StrokeStyle ss;
    memset(&ss, 0, sizeof ss);
    ss.width = sp->align == SA_CENTER ? sw : 2 * sw;
    ss.cap = sp->cap; ss.join = sp->join; ss.miter = sp->miter;
    ss.ndash = sp->ndash < 0 ? 0 : sp->ndash > 8 ? 8 : sp->ndash;
    memcpy(ss.dash, sp->dash, sizeof ss.dash); ss.dash_off = sp->dash_off;
    if (!poly_stroke(&fill, &ss, &st)) goto done;
    if (st.n) b = box_union(b, poly_box(&st));
  }
  b.x0 -= 1; b.y0 -= 1; b.x1 += 1; b.y1 += 1;
  b = box_clip(b, box_make(0, 0, w, h));
  if (box_empty(b)) { ok = 1; goto done; }
  n = (long)(b.x1 - b.x0) * (b.y1 - b.y0);
  if (!(fc = (u8 *)malloc((size_t)n)) || !(o = (u8 *)calloc((size_t)n, 4))) goto done;
  if (!poly_raster(&fill, sp->evenodd, b, fc, b.x1 - b.x0)) goto done;
  if (stroke) {
    if (!(sc = (u8 *)malloc((size_t)n)) || !poly_raster(&st, 0, b, sc, b.x1 - b.x0)) goto done;
    for (k = 0; k < n; k++) {
      if (sp->align == SA_INSIDE) sc[k] = sc[k] < fc[k] ? sc[k] : fc[k];
      else if (sp->align == SA_OUTSIDE) sc[k] = (u8)((sc[k] * (255 - fc[k]) + 127) / 255);
    }
  }
  for (k = 0; k < n; k++) {
    u8 *q = o + k * 4;
    int f = filled ? fc[k] : 0, s = sc ? sc[k] : 0;
    if (!sp->antialias) { f = f >= 128 ? 255 : 0; s = s >= 128 ? 255 : 0; }
    f = f * (255 - sp->fill_alpha) / 255; s = s * (255 - sp->stroke_alpha) / 255;
    if (f) { q[0] = (u8)(sp->fill_color >> 16); q[1] = (u8)(sp->fill_color >> 8); q[2] = (u8)sp->fill_color; q[3] = (u8)f; }
    if (s) {
      /* the stroke over the fill */
      int a = s + f * (255 - s) / 255, c;
      for (c = 0; c < 3; c++) {
        int sv = (int)((sp->stroke_color >> (16 - 8 * c)) & 255), fv = q[c];
        q[c] = (u8)(a ? (sv * s + fv * f * (255 - s) / 255) / a : 0);
      }
      q[3] = (u8)a;
    }
  }
  *px = o; o = NULL; *bx = b;
  ok = 1;
done:
  free(fc); free(sc); free(o);
  poly_free(&fill); poly_free(&st);
  return ok;
}

/* ------------------------------------------------------------------ in the document */
static const char *const shape_names[SH_N] = { "Rectangle", "Rounded Rectangle", "Ellipse", "Triangle", "Polygon", "Line", "Shape" };
int doc_new_shape(Doc *d, const ShapeParams *sp, const Path *path, const char *name)
{
  u32 len;
  ShapeParams *b = shape_alloc(sp, path, &len);
  Layer *l;
  int at;
  if (!b) return -1;
  if (!(l = (Layer *)calloc(1, sizeof *l))) { free(b); return -1; }
  strncpy(l->name, name ? name : sp->kind < SH_N ? shape_names[sp->kind] : "Shape", sizeof l->name - 1);
  if (sp->kind == SH_POLYGON && sp->star && !name) strcpy(l->name, "Star");
  l->opacity = 255; l->visible = 1; l->kind = LK_SHAPE;
  l->live = b; l->livelen = len;
  at = d->active + 1;
  if (at > d->nlay) at = d->nlay;
  l->depth = d->nlay && d->active >= 0 && d->active < d->nlay ? d->lay[d->active]->depth : 0;
  doc_begin(d, "New Shape Layer");                         /* (doc_add_layer's step, under this name) */
  if (!doc_add_layer(d, l, at)) { doc_end(d); return -1; }
  doc_end(d);
  return at;
}
int doc_set_shape(Doc *d, int i, const ShapeParams *sp, const Path *path)
{
  Layer *l;
  ShapeParams *b;
  u32 len;
  Path keep;
  if (i < 0 || i >= d->nlay || !layer_shape(l = d->lay[i])) return 0;
  keep = shape_path((const ShapeParams *)l->live);
  if (!path && sp->kind == SH_CUSTOM) path = &keep;
  if (!(b = shape_alloc(sp, path, &len))) return 0;
  doc_begin(d, "Edit Shape");
  doc_note_live(d, i);
  free(l->live);
  l->live = b; l->livelen = len; l->live_ok = 0;
  doc_end(d);
  return 1;
}
