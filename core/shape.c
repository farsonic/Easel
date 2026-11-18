/*
 * shape.c - shape layers and the shape generators (see shape.h). Easel 0.9.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "shape.h"
#include "live.h"
#include "gradient.h"
#include "paint.h"
#include "layerx.h"
#include "pattern.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define KAPPA 0.5522847498307936          /* a quarter circle's handles, as a part of its radius */
#define KAPPA_C (1 - KAPPA)               /* (r3: the handle's distance from the corner, as a part of the radius) */

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
/* r3: a radius for each corner (top left, top right, bottom right, bottom left), each held to what
   fits: two neighbours together no more than the side between them */
int shape_rect_path4(Path *p, double x0, double y0, double x1, double y1, const double *rin)
{
  double r[4], w, h, f = 1;
  int i;
  order(&x0, &x1); order(&y0, &y1);
  w = x1 - x0; h = y1 - y0;
  for (i = 0; i < 4; i++) r[i] = rin[i] > 0 && rin[i] == rin[i] ? rin[i] : 0;
  /* (CSS's rule: every radius scaled by the one factor that makes the tightest side fit) */
  if (r[0] + r[1] > w && w / (r[0] + r[1]) < f) f = w / (r[0] + r[1]);
  if (r[3] + r[2] > w && w / (r[3] + r[2]) < f) f = w / (r[3] + r[2]);
  if (r[0] + r[3] > h && h / (r[0] + r[3]) < f) f = h / (r[0] + r[3]);
  if (r[1] + r[2] > h && h / (r[1] + r[2]) < f) f = h / (r[1] + r[2]);
  for (i = 0; i < 4; i++) r[i] *= f;
  if (!path_move_to(p, x0 + r[0], y0) || !path_line_to(p, x1 - r[1], y0)) return 0;
  if (r[1] > 0 && !path_curve_to(p, x1 - r[1] * KAPPA_C, y0, x1, y0 + r[1] * KAPPA_C, x1, y0 + r[1])) return 0;
  if (!path_line_to(p, x1, y1 - r[2])) return 0;
  if (r[2] > 0 && !path_curve_to(p, x1, y1 - r[2] * KAPPA_C, x1 - r[2] * KAPPA_C, y1, x1 - r[2], y1)) return 0;
  if (!path_line_to(p, x0 + r[3], y1)) return 0;
  if (r[3] > 0 && !path_curve_to(p, x0 + r[3] * KAPPA_C, y1, x0, y1 - r[3] * KAPPA_C, x0, y1 - r[3])) return 0;
  if (!path_line_to(p, x0, y0 + r[0])) return 0;
  if (r[0] > 0 && !path_curve_to(p, x0, y0 + r[0] * KAPPA_C, x0 + r[0] * KAPPA_C, y0, x0 + r[0], y0)) return 0;
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
  case SH_RECT: case SH_ROUNDRECT:
    if (sp->radii_on) return shape_rect_path4(p, sp->x0, sp->y0, sp->x1, sp->y1, sp->radii);       /* (r3) */
    return shape_rect_path(p, sp->x0, sp->y0, sp->x1, sp->y1, sp->radius);
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
/* ------------------------------------------------------------------ r3: gradient and pattern fills */
void shape_set_gradient(ShapeParams *sp, const Gradient *g, int style, double angle, double scale, int reverse)
{
  int i;
  sp->fill_kind = SFILL_GRADIENT;
  sp->grad_style = (u8)(style >= GR_LINEAR && style <= GR_DIAMOND ? style : GR_LINEAR);
  sp->grad_angle = angle == angle ? angle : 0;
  sp->grad_scale = scale >= 1 && scale <= 1000 ? scale : 100;
  sp->grad_reverse = (u8)(reverse != 0);
  sp->grad_nc = (u8)(g->ncolor < 0 ? 0 : g->ncolor > SH_GSTOPS ? SH_GSTOPS : g->ncolor);
  sp->grad_no = (u8)(g->nopacity < 0 ? 0 : g->nopacity > SH_GSTOPS ? SH_GSTOPS : g->nopacity);
  for (i = 0; i < sp->grad_nc; i++) {
    const GradColorStop *c = &g->color[i];
    sp->grad_cpos[i] = c->pos; sp->grad_cmid[i] = c->mid;
    sp->grad_rgb[i] = (c->kind == GS_FG ? g->fg : c->kind == GS_BG ? g->bg : c->rgb) & 0xFFFFFF;
  }
  for (i = 0; i < sp->grad_no; i++) { sp->grad_opos[i] = g->opacity[i].pos; sp->grad_omid[i] = g->opacity[i].mid; sp->grad_op[i] = g->opacity[i].opacity; }
}
void shape_get_gradient(const ShapeParams *sp, Gradient *g)
{
  int i, nc = sp->grad_nc > SH_GSTOPS ? SH_GSTOPS : sp->grad_nc, no = sp->grad_no > SH_GSTOPS ? SH_GSTOPS : sp->grad_no;
  gradient_init(g, "Shape", sp->fill_color, 0xFFFFFF);
  if (nc < 1) return;
  g->ncolor = nc;
  for (i = 0; i < nc; i++) {
    g->color[i].pos = sp->grad_cpos[i]; g->color[i].mid = sp->grad_cmid[i] >= 0.05 && sp->grad_cmid[i] <= 0.95 ? sp->grad_cmid[i] : 0.5;
    g->color[i].kind = GS_RGB; g->color[i].rgb = sp->grad_rgb[i] & 0xFFFFFF;
  }
  if (no >= 1) {
    g->nopacity = no;
    for (i = 0; i < no; i++) {
      g->opacity[i].pos = sp->grad_opos[i]; g->opacity[i].mid = sp->grad_omid[i] >= 0.05 && sp->grad_omid[i] <= 0.95 ? sp->grad_omid[i] : 0.5;
      g->opacity[i].opacity = sp->grad_op[i] < 0 ? 0 : sp->grad_op[i] > 1 ? 1 : sp->grad_op[i];
    }
  }
  gradient_sort(g);
}
/* where pixel centre x, y falls on the gradient (0-1), across the shape's box x0..x1, y0..y1 */
static double grad_t(const ShapeParams *sp, double bx0, double by0, double bx1, double by1, double x, double y)
{
  double cx = (bx0 + bx1) / 2, cy = (by0 + by1) / 2, hw = (bx1 - bx0) / 2, hh = (by1 - by0) / 2;
  double a = sp->grad_angle * M_PI / 180, ca = cos(a), sa = -sin(a), sc = (sp->grad_scale > 0 ? sp->grad_scale : 100) / 100.0;
  double px = x - cx, py = y - cy, t, len, half;
  if (hw < 0.5) hw = 0.5;
  if (hh < 0.5) hh = 0.5;
  len = (fabs(hw * ca) + fabs(hh * sa)) * sc;
  half = sqrt(hw * hw + hh * hh) * sc;
  if (len < 0.5) len = 0.5;
  switch (sp->grad_style) {
  case GR_RADIAL: t = sqrt(px * px + py * py) / half; break;
  case GR_ANGLE: t = fmod(atan2(py, px) - a + 4 * M_PI, 2 * M_PI) / (2 * M_PI); break;
  case GR_REFLECTED: t = fabs(px * ca + py * sa) / len; break;
  case GR_DIAMOND: t = (fabs(px) / hw > fabs(py) / hh ? fabs(px) / hw : fabs(py) / hh) / sc; break;
  default: t = (px * ca + py * sa) / (2 * len) + 0.5; break;
  }
  if (sp->grad_reverse) t = 1 - t;
  return t < 0 ? 0 : t > 1 ? 1 : t;
}
int doc_set_shape_fill(Doc *d, int i, int kind, const Gradient *g, int style, double angle, double scale, int reverse, const Pattern *pat)
{
  const ShapeParams *cur;
  ShapeParams sp;
  Path keep;
  ShapeParams *b;
  Layer *l;
  u32 len;
  int ok = 1;
  if (i < 0 || i >= d->nlay || !(cur = layer_shape(l = d->lay[i])) || kind < 0 || kind >= SFILL_N) return 0;
  if (kind == SFILL_PATTERN && !pat && !layerx_pattern_at(l, 0, 0)) return 0;
  sp = *cur;
  sp.fill_on = 1;
  if (kind == SFILL_GRADIENT) {
    if (g) shape_set_gradient(&sp, g, style, angle, scale, reverse);
    else { sp.fill_kind = SFILL_GRADIENT; sp.grad_style = (u8)style; sp.grad_angle = angle; sp.grad_scale = scale; sp.grad_reverse = (u8)(reverse != 0); }
  } else {
    sp.fill_kind = (u8)kind;
    if (kind == SFILL_PATTERN && scale >= 1) sp.grad_scale = scale;
  }
  keep = shape_path(cur);
  if (!(b = shape_alloc(&sp, &keep, &len))) return 0;
  doc_begin(d, "Shape Fill");
  doc_note_live(d, i);
  free(l->live);
  l->live = b; l->livelen = len; l->live_ok = 0;
  if (kind == SFILL_PATTERN) {
    /* the pattern kept with the layer (layerx.h's, which Pattern Overlay shares) */
    ok = layerx_set_pattern(d, i, pat, (int)(scale >= 1 ? scale : 0));
  }
  doc_end(d);
  return ok;
}

int shape_render(const ShapeParams *sp, i32 w, i32 h, u8 **px, Box *bx) { return shape_render_layer(sp, NULL, w, h, px, bx); }
int shape_render_layer(const ShapeParams *sp, const Layer *l, i32 w, i32 h, u8 **px, Box *bx)
{
  Path p = shape_path(sp);
  Poly fill, st;
  Box b;
  u8 *fc = NULL, *sc = NULL, *o = NULL, *lut = NULL;
  Box fb;
  double gx0 = 0, gy0 = 0, gx1 = 1, gy1 = 1;
  long n, k;
  int ok = 0, stroke = sp->stroke_on && sp->stroke_width > 0, filled = sp->fill_on;
  double sw = sp->stroke_width;
  *px = NULL; *bx = box_make(0, 0, 0, 0);
  if (!p.na || (!stroke && !filled)) return 1;
  poly_init(&fill); poly_init(&st);
  if (!path_flatten(&p, 0.05, &fill)) goto done;
  b = fb = poly_box(&fill);
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
  if (filled && sp->fill_kind == SFILL_GRADIENT) {
    /* r3: the gradient across the shape's own box (aligned with the layer, as PhotoCraft's) */
    Gradient g;
    shape_get_gradient(sp, &g);
    if (!(lut = (u8 *)malloc(256 * 4))) goto done;
    gradient_lut(&g, lut, 256);
    if (sp->kind == SH_CUSTOM || sp->kind == SH_LINE) { gx0 = fb.x0; gy0 = fb.y0; gx1 = fb.x1; gy1 = fb.y1; }
    else { gx0 = sp->x0 < sp->x1 ? sp->x0 : sp->x1; gx1 = sp->x0 < sp->x1 ? sp->x1 : sp->x0; gy0 = sp->y0 < sp->y1 ? sp->y0 : sp->y1; gy1 = sp->y0 < sp->y1 ? sp->y1 : sp->y0; }
  }
  for (k = 0; k < n; k++) {
    u8 *q = o + k * 4;
    int f = filled ? fc[k] : 0, s = sc ? sc[k] : 0;
    if (!sp->antialias) { f = f >= 128 ? 255 : 0; s = s >= 128 ? 255 : 0; }
    f = f * (255 - sp->fill_alpha) / 255; s = s * (255 - sp->stroke_alpha) / 255;
    if (f && lut) {
      i32 x = b.x0 + (i32)(k % (b.x1 - b.x0)), y = b.y0 + (i32)(k / (b.x1 - b.x0));
      int ti = (int)(grad_t(sp, gx0, gy0, gx1, gy1, x + 0.5, y + 0.5) * 255 + 0.5);
      const u8 *c = lut + ti * 4;
      q[0] = c[0]; q[1] = c[1]; q[2] = c[2]; f = f * c[3] / 255; q[3] = (u8)f;
    } else if (f && filled && sp->fill_kind == SFILL_PATTERN && l) {
      i32 x = b.x0 + (i32)(k % (b.x1 - b.x0)), y = b.y0 + (i32)(k / (b.x1 - b.x0));
      const u8 *c = layerx_pattern_at(l, x, y);
      if (c) { q[0] = c[0]; q[1] = c[1]; q[2] = c[2]; f = f * c[3] / 255; q[3] = (u8)f; }
      else { q[0] = (u8)(sp->fill_color >> 16); q[1] = (u8)(sp->fill_color >> 8); q[2] = (u8)sp->fill_color; q[3] = (u8)f; }
    } else if (f) { q[0] = (u8)(sp->fill_color >> 16); q[1] = (u8)(sp->fill_color >> 8); q[2] = (u8)sp->fill_color; q[3] = (u8)f; }
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
  free(fc); free(sc); free(o); free(lut);
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
