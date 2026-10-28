/*
 * path.c - vector paths (see path.h): the anchor model, flattening, an anti-aliased polygon filler,
 * stroke outlines, hit testing, SVG path data, and the document's list of paths. Easel 0.9.
 * Easel's own code (no FreeType or Potrace in it).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "path.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ the model */
Path *path_new(void) { return (Path *)calloc(1, sizeof(Path)); }
void path_free(Path *p)
{
  if (!p) return;
  if (!p->view) { free(p->a); free(p->s); }
  free(p);
}
Path *path_copy(const Path *p)
{
  Path *c;
  if (!p || !(c = path_new())) return NULL;
  c->evenodd = p->evenodd;
  if (p->na && !(c->a = (PathAnchor *)malloc((size_t)p->na * sizeof(PathAnchor)))) { free(c); return NULL; }
  if (p->ns && !(c->s = (SubPath *)malloc((size_t)p->ns * sizeof(SubPath)))) { free(c->a); free(c); return NULL; }
  if (p->na) memcpy(c->a, p->a, (size_t)p->na * sizeof(PathAnchor));
  if (p->ns) memcpy(c->s, p->s, (size_t)p->ns * sizeof(SubPath));
  c->na = c->capa = p->na; c->ns = c->caps = p->ns;
  return c;
}
long path_bytes(const Path *p) { return p ? (long)sizeof *p + (long)p->na * (long)sizeof(PathAnchor) + (long)p->ns * (long)sizeof(SubPath) : 0; }
void path_clear(Path *p) { p->na = p->ns = 0; }
/* a view's arrays made its own before it is changed */
static int own(Path *p)
{
  if (!p->view) return 1;
  {
    PathAnchor *a = p->na ? (PathAnchor *)malloc((size_t)p->na * sizeof *a) : NULL;
    SubPath *s = p->ns ? (SubPath *)malloc((size_t)p->ns * sizeof *s) : NULL;
    if ((p->na && !a) || (p->ns && !s)) { free(a); free(s); return 0; }
    if (a) memcpy(a, p->a, (size_t)p->na * sizeof *a);
    if (s) memcpy(s, p->s, (size_t)p->ns * sizeof *s);
    p->a = a; p->s = s; p->capa = p->na; p->caps = p->ns; p->view = 0;
  }
  return 1;
}
static int grow_a(Path *p, i32 more)
{
  if (!own(p)) return 0;
  if (p->na + more > p->capa) {
    i32 nc = p->capa ? p->capa * 2 : 16;
    PathAnchor *n;
    while (nc < p->na + more) nc *= 2;
    if (nc > 4000000 || !(n = (PathAnchor *)realloc(p->a, (size_t)nc * sizeof *n))) return 0;
    p->a = n; p->capa = nc;
  }
  return 1;
}
static int grow_s(Path *p)
{
  if (!own(p)) return 0;
  if (p->ns + 1 > p->caps) {
    i32 nc = p->caps ? p->caps * 2 : 4;
    SubPath *n = (SubPath *)realloc(p->s, (size_t)nc * sizeof *n);
    if (!n) return 0;
    p->s = n; p->caps = nc;
  }
  return 1;
}
static void set_anchor(PathAnchor *a, double x, double y)
{
  memset(a, 0, sizeof *a);
  a->x = a->ix = a->ox = x; a->y = a->iy = a->oy = y;
}
/* room for one anchor at global index g (anchors after it moved up, subpaths after it adjusted) */
static int ins_anchor(Path *p, int s, i32 g)
{
  int j;
  if (!grow_a(p, 1)) return 0;
  memmove(&p->a[g + 1], &p->a[g], (size_t)(p->na - g) * sizeof(PathAnchor));
  p->na++;
  p->s[s].n++;
  for (j = s + 1; j < p->ns; j++) p->s[j].first++;
  return 1;
}
static void del_anchor(Path *p, int s, i32 g)
{
  int j;
  memmove(&p->a[g], &p->a[g + 1], (size_t)(p->na - g - 1) * sizeof(PathAnchor));
  p->na--;
  p->s[s].n--;
  for (j = s + 1; j < p->ns; j++) p->s[j].first--;
}
int path_move_to(Path *p, double x, double y)
{
  SubPath *sp;
  if (!grow_s(p) || !grow_a(p, 1)) return 0;
  sp = &p->s[p->ns];
  memset(sp, 0, sizeof *sp);
  sp->first = p->na; sp->n = 1;
  set_anchor(&p->a[p->na++], x, y);
  return ++p->ns;
}
int path_line_to(Path *p, double x, double y)
{
  if (!p->ns || p->s[p->ns - 1].closed) return path_move_to(p, x, y) ? 1 : 0;
  if (!grow_a(p, 1)) return 0;
  set_anchor(&p->a[p->na++], x, y);
  p->s[p->ns - 1].n++;
  return 1;
}
int path_curve_to(Path *p, double c1x, double c1y, double c2x, double c2y, double x, double y)
{
  if (!p->ns || p->s[p->ns - 1].closed) { if (!path_move_to(p, c1x, c1y)) return 0; }
  if (!own(p)) return 0;
  p->a[p->na - 1].ox = c1x; p->a[p->na - 1].oy = c1y;
  if (!path_line_to(p, x, y)) return 0;
  p->a[p->na - 1].ix = c2x; p->a[p->na - 1].iy = c2y;
  return 1;
}
void path_close(Path *p)
{
  SubPath *sp;
  if (!p->ns || !own(p)) return;
  sp = &p->s[p->ns - 1];
  if (sp->closed) return;
  if (sp->n > 1) {
    PathAnchor *f = &p->a[sp->first], *l = &p->a[sp->first + sp->n - 1];
    if (fabs(f->x - l->x) < 1e-9 && fabs(f->y - l->y) < 1e-9) {
      /* the last anchor on the first: one anchor, with the last's handle in */
      f->ix = l->ix; f->iy = l->iy;
      del_anchor(p, p->ns - 1, sp->first + sp->n - 1);
    }
  }
  sp->closed = 1;
}
int path_append(Path *p, const Path *q)
{
  int s;
  i32 k;
  for (s = 0; s < q->ns; s++) {
    const SubPath *qs = &q->s[s];
    if (!qs->n || !path_move_to(p, q->a[qs->first].x, q->a[qs->first].y)) { if (!qs->n) continue; return 0; }
    for (k = 1; k < qs->n; k++) if (!path_line_to(p, 0, 0)) return 0;
    memcpy(&p->a[p->s[p->ns - 1].first], &q->a[qs->first], (size_t)qs->n * sizeof(PathAnchor));
    p->s[p->ns - 1].closed = qs->closed;
  }
  return 1;
}
static int valid(const Path *p, int s, int k) { return p && s >= 0 && s < p->ns && k >= 0 && k < p->s[s].n; }
int path_add_anchor(Path *p, int s, int k, double x, double y)
{
  if (!p || s < 0 || s >= p->ns || k < 0 || k > p->s[s].n) return 0;
  if (!ins_anchor(p, s, p->s[s].first + k)) return 0;
  set_anchor(&p->a[p->s[s].first + k], x, y);
  return 1;
}
int path_nsegs(const Path *p, int s)
{
  if (s < 0 || s >= p->ns || p->s[s].n < 1) return 0;
  return p->s[s].closed ? (p->s[s].n > 1 ? p->s[s].n : 0) : p->s[s].n - 1;
}
void path_seg_ctrl(const Path *p, int s, int seg, double *c)
{
  const SubPath *sp = &p->s[s];
  const PathAnchor *a = &p->a[sp->first + seg], *b = &p->a[sp->first + (seg + 1) % sp->n];
  c[0] = a->x; c[1] = a->y; c[2] = a->ox; c[3] = a->oy; c[4] = b->ix; c[5] = b->iy; c[6] = b->x; c[7] = b->y;
}
void bez_point(const double *c, double t, double *x, double *y)
{
  double u = 1 - t, a = u * u * u, b = 3 * u * u * t, cc = 3 * u * t * t, d = t * t * t;
  *x = a * c[0] + b * c[2] + cc * c[4] + d * c[6];
  *y = a * c[1] + b * c[3] + cc * c[5] + d * c[7];
}
static int is_line(const double *c)
{
  return c[2] == c[0] && c[3] == c[1] && c[4] == c[6] && c[5] == c[7];
}
int path_split_segment(Path *p, int s, int seg, double t)
{
  double c[8], l1x, l1y, mx, my, r1x, r1y, ax, ay, bx, by;
  i32 at;
  int straight;
  if (!p || s < 0 || s >= p->ns || seg < 0 || seg >= path_nsegs(p, s)) return 0;
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  path_seg_ctrl(p, s, seg, c);
  straight = is_line(c);
  /* de Casteljau */
  ax = c[0] + (c[2] - c[0]) * t; ay = c[1] + (c[3] - c[1]) * t;
  mx = c[2] + (c[4] - c[2]) * t; my = c[3] + (c[5] - c[3]) * t;
  bx = c[4] + (c[6] - c[4]) * t; by = c[5] + (c[7] - c[5]) * t;
  l1x = ax + (mx - ax) * t; l1y = ay + (my - ay) * t;
  r1x = mx + (bx - mx) * t; r1y = my + (by - my) * t;
  mx = l1x + (r1x - l1x) * t; my = l1y + (r1y - l1y) * t;
  at = seg + 1;
  if (!ins_anchor(p, s, p->s[s].first + at)) return 0;
  {
    SubPath *sp = &p->s[s];
    PathAnchor *pa = &p->a[sp->first + seg], *na = &p->a[sp->first + at], *nb = &p->a[sp->first + (at + 1) % sp->n];
    set_anchor(na, mx, my);
    if (!straight) {
      pa->ox = ax; pa->oy = ay;
      na->ix = l1x; na->iy = l1y; na->ox = r1x; na->oy = r1y;
      nb->ix = bx; nb->iy = by;
      na->type = PA_SMOOTH;
    }
  }
  return at;
}
int path_delete_subpath(Path *p, int s)
{
  i32 n, f;
  int j;
  if (!p || s < 0 || s >= p->ns || !own(p)) return 0;
  n = p->s[s].n; f = p->s[s].first;
  memmove(&p->a[f], &p->a[f + n], (size_t)(p->na - f - n) * sizeof(PathAnchor));
  p->na -= n;
  for (j = s + 1; j < p->ns; j++) p->s[j].first -= n;
  memmove(&p->s[s], &p->s[s + 1], (size_t)(p->ns - s - 1) * sizeof(SubPath));
  p->ns--;
  return 1;
}
int path_delete_anchor(Path *p, int s, int k)
{
  if (!valid(p, s, k) || !own(p)) return 0;
  del_anchor(p, s, p->s[s].first + k);
  if (p->s[s].n == 0) return path_delete_subpath(p, s);
  if (p->s[s].n < 2) p->s[s].closed = 0;
  return 1;
}
int path_move_anchor(Path *p, int s, int k, double dx, double dy)
{
  PathAnchor *a;
  if (!valid(p, s, k) || !own(p)) return 0;
  a = &p->a[p->s[s].first + k];
  a->x += dx; a->y += dy; a->ix += dx; a->iy += dy; a->ox += dx; a->oy += dy;
  return 1;
}
int path_move_handle(Path *p, int s, int k, int which, double x, double y, int alone)
{
  PathAnchor *a;
  double *hx, *hy, *ox, *oy;
  if (!valid(p, s, k) || !own(p)) return 0;
  a = &p->a[p->s[s].first + k];
  hx = which ? &a->ox : &a->ix; hy = which ? &a->oy : &a->iy;
  ox = which ? &a->ix : &a->ox; oy = which ? &a->iy : &a->oy;
  *hx = x; *hy = y;
  if (alone) a->type = PA_CORNER;
  else if (a->type == PA_SMOOTH) {
    double len = hypot(*ox - a->x, *oy - a->y), d = hypot(x - a->x, y - a->y);
    if (len > 0 && d > 0) { *ox = a->x - (x - a->x) * len / d; *oy = a->y - (y - a->y) * len / d; }
  }
  return 1;
}
int path_drag_handles(Path *p, int s, int k, double x, double y)
{
  PathAnchor *a;
  if (!valid(p, s, k) || !own(p)) return 0;
  a = &p->a[p->s[s].first + k];
  a->ox = x; a->oy = y; a->ix = 2 * a->x - x; a->iy = 2 * a->y - y;
  a->type = (x == a->x && y == a->y) ? PA_CORNER : PA_SMOOTH;
  return 1;
}
int path_convert_anchor(Path *p, int s, int k, int type)
{
  SubPath *sp;
  PathAnchor *a;
  if (!valid(p, s, k) || !own(p)) return 0;
  sp = &p->s[s];
  a = &p->a[sp->first + k];
  if (type == PA_CORNER) { a->ix = a->ox = a->x; a->iy = a->oy = a->y; a->type = PA_CORNER; return 1; }
  {
    int hasp = k > 0 || sp->closed, hasn = k < sp->n - 1 || sp->closed;
    const PathAnchor *pv = hasp ? &p->a[sp->first + (k + sp->n - 1) % sp->n] : a;
    const PathAnchor *nx = hasn ? &p->a[sp->first + (k + 1) % sp->n] : a;
    double dx = nx->x - pv->x, dy = nx->y - pv->y, d = hypot(dx, dy);
    double lp = hypot(a->x - pv->x, a->y - pv->y) / 3, ln = hypot(nx->x - a->x, nx->y - a->y) / 3;
    if (sp->n < 2) return 1;
    if (d <= 0) { dx = 1; dy = 0; d = 1; }
    dx /= d; dy /= d;
    a->ix = a->x - dx * lp; a->iy = a->y - dy * lp;
    a->ox = a->x + dx * ln; a->oy = a->y + dy * ln;
    a->type = PA_SMOOTH;
  }
  return 1;
}
void path_reverse(Path *p, int s)
{
  SubPath *sp;
  i32 i, j;
  if (!p || s < 0 || s >= p->ns || !own(p)) return;
  sp = &p->s[s];
  for (i = 0, j = sp->n - 1; i < j; i++, j--) {
    PathAnchor t = p->a[sp->first + i];
    p->a[sp->first + i] = p->a[sp->first + j];
    p->a[sp->first + j] = t;
  }
  for (i = 0; i < sp->n; i++) {
    PathAnchor *a = &p->a[sp->first + i];
    double tx = a->ix, ty = a->iy;
    a->ix = a->ox; a->iy = a->oy; a->ox = tx; a->oy = ty;
  }
  if (sp->closed && sp->n > 1) {
    /* closed: keep the same first anchor (rotate so it leads again) */
    PathAnchor t = p->a[sp->first + sp->n - 1];
    memmove(&p->a[sp->first + 1], &p->a[sp->first], (size_t)(sp->n - 1) * sizeof(PathAnchor));
    p->a[sp->first] = t;
  }
}
void path_transform(Path *p, const double *m)
{
  i32 i;
  if (!p || !own(p)) return;
  for (i = 0; i < p->na; i++) {
    PathAnchor *a = &p->a[i];
    double x, y;
    x = a->x; y = a->y; a->x = m[0] * x + m[1] * y + m[2]; a->y = m[3] * x + m[4] * y + m[5];
    x = a->ix; y = a->iy; a->ix = m[0] * x + m[1] * y + m[2]; a->iy = m[3] * x + m[4] * y + m[5];
    x = a->ox; y = a->oy; a->ox = m[0] * x + m[1] * y + m[2]; a->oy = m[3] * x + m[4] * y + m[5];
  }
}

/* ------------------------------------------------------------------ polygons */
void poly_init(Poly *pl) { memset(pl, 0, sizeof *pl); }
void poly_free(Poly *pl) { free(pl->xy); free(pl->end); free(pl->closed); memset(pl, 0, sizeof *pl); }
int poly_add(Poly *pl, double x, double y)
{
  if (pl->n >= pl->cap) {
    i32 nc = pl->cap ? pl->cap * 2 : 64;
    double *n;
    if (nc > 50000000 || !(n = (double *)realloc(pl->xy, (size_t)nc * 2 * sizeof(double)))) return 0;
    pl->xy = n; pl->cap = nc;
  }
  pl->xy[pl->n * 2] = x; pl->xy[pl->n * 2 + 1] = y;
  pl->n++;
  return 1;
}
int poly_end(Poly *pl, int closed)
{
  i32 start = pl->nc ? pl->end[pl->nc - 1] : 0;
  if (pl->n == start) return 1;                            /* (nothing in it) */
  if (pl->nc >= pl->capc) {
    i32 nc = pl->capc ? pl->capc * 2 : 16;
    i32 *e = (i32 *)realloc(pl->end, (size_t)nc * sizeof(i32));
    u8 *c;
    if (!e) return 0;
    pl->end = e;
    if (!(c = (u8 *)realloc(pl->closed, (size_t)nc))) return 0;
    pl->closed = c; pl->capc = nc;
  }
  pl->end[pl->nc] = pl->n; pl->closed[pl->nc] = (u8)(closed != 0);
  pl->nc++;
  return 1;
}
/* one cubic as lines (the start point is in pl already) */
static int flat_cubic(Poly *pl, const double *c, double tol)
{
  double m1, m2, m;
  int n, i;
  if (is_line(c)) return poly_add(pl, c[6], c[7]);
  m1 = hypot(c[0] - 2 * c[2] + c[4], c[1] - 2 * c[3] + c[5]);
  m2 = hypot(c[2] - 2 * c[4] + c[6], c[3] - 2 * c[5] + c[7]);
  m = m1 > m2 ? m1 : m2;
  n = (int)ceil(sqrt(0.75 * m / tol));                   /* (the second differences bound how far a chord strays) */
  if (n < 1) n = 1;
  if (n > 1000) n = 1000;
  for (i = 1; i <= n; i++) {
    double x, y;
    bez_point(c, (double)i / n, &x, &y);
    if (!poly_add(pl, x, y)) return 0;
  }
  return 1;
}
int path_flatten(const Path *p, double tol, Poly *pl)
{
  int s, k;
  if (tol <= 0) tol = 0.25;
  for (s = 0; s < p->ns; s++) {
    const SubPath *sp = &p->s[s];
    int ns = path_nsegs(p, s);
    if (!sp->n) continue;
    if (!poly_add(pl, p->a[sp->first].x, p->a[sp->first].y)) return 0;
    for (k = 0; k < ns; k++) {
      double c[8];
      path_seg_ctrl(p, s, k, c);
      if (!flat_cubic(pl, c, tol)) return 0;
    }
    if (sp->closed && ns > 0) pl->n--;                     /* (the last point is the first again) */
    if (!poly_end(pl, sp->closed)) return 0;
  }
  return 1;
}
double poly_area(const Poly *pl)
{
  double a = 0;
  i32 c, i, start = 0;
  for (c = 0; c < pl->nc; start = pl->end[c], c++) {
    i32 n = pl->end[c] - start;
    for (i = 0; i < n; i++) {
      const double *p = pl->xy + 2 * (start + i), *q = pl->xy + 2 * (start + (i + 1) % n);
      a += p[0] * q[1] - q[0] * p[1];
    }
  }
  return a / 2;
}
Box poly_box(const Poly *pl)
{
  double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
  i32 i;
  if (!pl->n) return box_make(0, 0, 0, 0);
  for (i = 0; i < pl->n; i++) {
    double x = pl->xy[2 * i], y = pl->xy[2 * i + 1];
    if (x < x0) x0 = x;
    if (x > x1) x1 = x;
    if (y < y0) y0 = y;
    if (y > y1) y1 = y;
  }
  if (x0 < -1e7) x0 = -1e7;
  if (y0 < -1e7) y0 = -1e7;
  if (x1 > 1e7) x1 = 1e7;
  if (y1 > 1e7) y1 = 1e7;
  return box_make((i32)floor(x0), (i32)floor(y0), (i32)ceil(x1), (i32)ceil(y1));
}

/* the filler: edges with their winding direction, sorted by their tops */
typedef struct { double x0, y0, y1, dxdy; int dir; } Edge;
static int edge_cmp(const void *a, const void *b)
{
  double x = ((const Edge *)a)->y0, y = ((const Edge *)b)->y0;
  return x < y ? -1 : x > y;
}
typedef struct { double x; int dir; } Cross;
#define SUBROWS 16
int poly_raster(const Poly *pl, int evenodd, Box clip, u8 *cov, long stride)
{
  Edge *e = NULL;
  i32 ne = 0, c, i, start = 0, next = 0, nact = 0, *act = NULL;
  i32 W = clip.x1 - clip.x0, y;
  float *acc = NULL, *dl = NULL;
  Cross *cr = NULL;
  int ok = 0;
  if (box_empty(clip)) return 1;
  for (y = clip.y0; y < clip.y1; y++) memset(cov + (long)(y - clip.y0) * stride, 0, (size_t)W);
  if (!pl->n) return 1;
  if (!(e = (Edge *)malloc((size_t)pl->n * sizeof(Edge)))) return 0;
  for (c = 0; c < pl->nc; start = pl->end[c], c++) {
    i32 n = pl->end[c] - start;
    for (i = 0; i < n; i++) {
      const double *p = pl->xy + 2 * (start + i), *q = pl->xy + 2 * (start + (i + 1) % n);
      Edge *g;
      if (p[1] == q[1] || !(p[1] == p[1]) || !(q[1] == q[1])) continue;   /* (horizontal, or NaN) */
      g = &e[ne++];
      if (p[1] < q[1]) { g->x0 = p[0]; g->y0 = p[1]; g->y1 = q[1]; g->dir = 1; }
      else { g->x0 = q[0]; g->y0 = q[1]; g->y1 = p[1]; g->dir = -1; }
      g->dxdy = (q[0] - p[0]) / (q[1] - p[1]);
    }
  }
  if (!ne) { ok = 1; goto done; }
  qsort(e, (size_t)ne, sizeof(Edge), edge_cmp);
  act = (i32 *)malloc((size_t)ne * sizeof(i32));
  cr = (Cross *)malloc((size_t)ne * sizeof(Cross));
  acc = (float *)malloc((size_t)(W + 2) * sizeof(float));
  dl = (float *)malloc((size_t)(W + 2) * sizeof(float));
  if (!act || !cr || !acc || !dl) goto done;
  for (y = clip.y0; y < clip.y1; y++) {
    int sub, any = 0;
    i32 k, keep;
    u8 *o = cov + (long)(y - clip.y0) * stride;
    while (next < ne && e[next].y0 < y + 1) act[nact++] = next++;
    for (k = keep = 0; k < nact; k++) if (e[act[k]].y1 > y) act[keep++] = act[k];
    nact = keep;
    if (!nact) { if (next >= ne) break; continue; }
    memset(acc, 0, (size_t)(W + 2) * sizeof(float));
    memset(dl, 0, (size_t)(W + 2) * sizeof(float));
    for (sub = 0; sub < SUBROWS; sub++) {
      double sy = y + (sub + 0.5) / SUBROWS;
      int nc = 0, wind = 0, j;
      for (k = 0; k < nact; k++) {
        const Edge *g = &e[act[k]];
        int m;
        double x;
        if (sy < g->y0 || sy >= g->y1) continue;
        x = g->x0 + (sy - g->y0) * g->dxdy;
        for (m = nc++; m > 0 && cr[m - 1].x > x; m--) cr[m] = cr[m - 1];
        cr[m].x = x; cr[m].dir = g->dir;
      }
      for (j = 0; j + 1 < nc; j++) {
        int in;
        wind += cr[j].dir;
        in = evenodd ? (wind & 1) : wind != 0;
        if (in) {
          double xa = cr[j].x - clip.x0, xb = cr[j + 1].x - clip.x0;
          i32 ia, ib;
          const float w = 1.0f / SUBROWS;
          if (xa < 0) xa = 0;
          if (xb > W) xb = W;
          if (xb <= xa) continue;
          ia = (i32)xa; ib = (i32)xb;
          any = 1;
          if (ia == ib) acc[ia] += (float)(xb - xa) * w;
          else {
            acc[ia] += (float)(ia + 1 - xa) * w;
            dl[ia + 1] += w; dl[ib] -= w;
            if (ib < W) acc[ib] += (float)(xb - ib) * w;
          }
        }
      }
    }
    if (any) {
      float run = 0;
      for (k = 0; k < W; k++) {
        float v;
        run += dl[k];
        v = (acc[k] + run) * 255.0f + 0.5f;
        o[k] = (u8)(v >= 255 ? 255 : v <= 0 ? 0 : v);
      }
    }
  }
  ok = 1;
done:
  free(e); free(act); free(cr); free(acc); free(dl);
  return ok;
}

/* ------------------------------------------------------------------ strokes */
/* a polygon added as a contour turning clockwise (positive area) */
static int add_pos(Poly *out, const double *xy, int n)
{
  double a = 0;
  int i;
  for (i = 0; i < n; i++) a += xy[2 * i] * xy[2 * ((i + 1) % n) + 1] - xy[2 * ((i + 1) % n)] * xy[2 * i + 1];
  if (a == 0) return 1;
  for (i = 0; i < n; i++) {
    int j = a > 0 ? i : n - 1 - i;
    if (!poly_add(out, xy[2 * j], xy[2 * j + 1])) return 0;
  }
  return poly_end(out, 1);
}
static int add_disc(Poly *out, double cx, double cy, double r)
{
  int n = (int)ceil(M_PI / acos(r > 0.5 ? 1 - 0.2 / r : 0.6)), i;
  double xy[2 * 256];
  if (n < 8) n = 8;
  if (n > 256) n = 256;
  for (i = 0; i < n; i++) { xy[2 * i] = cx + r * cos(2 * M_PI * i / n); xy[2 * i + 1] = cy + r * sin(2 * M_PI * i / n); }
  return add_pos(out, xy, n);
}
static int stroke_contour(const double *pts, int n, int closed, const StrokeStyle *st, Poly *out)
{
  double hw = st->width / 2, lim = st->miter > 0 ? st->miter : 4;
  double *q;
  int m = 0, i, ok = 1, nseg;
  if (hw <= 0 || n < 1) return 1;
  /* consecutive duplicates dropped */
  if (!(q = (double *)malloc((size_t)n * 2 * sizeof(double)))) return 0;
  for (i = 0; i < n; i++)
    if (!m || fabs(pts[2 * i] - q[2 * m - 2]) > 1e-9 || fabs(pts[2 * i + 1] - q[2 * m - 1]) > 1e-9) { q[2 * m] = pts[2 * i]; q[2 * m + 1] = pts[2 * i + 1]; m++; }
  if (closed && m > 1 && fabs(q[0] - q[2 * m - 2]) < 1e-9 && fabs(q[1] - q[2 * m - 1]) < 1e-9) m--;
  if (m < 2) {
    if (m == 1 && st->cap == CAP_ROUND) ok = add_disc(out, q[0], q[1], hw);
    else if (m == 1 && st->cap == CAP_SQUARE) {
      double r[8] = { q[0] - hw, q[1] - hw, q[0] + hw, q[1] - hw, q[0] + hw, q[1] + hw, q[0] - hw, q[1] + hw };
      ok = add_pos(out, r, 4);
    }
    free(q);
    return ok;
  }
  if (closed && m < 3) closed = 0;
  nseg = closed ? m : m - 1;
  for (i = 0; ok && i < nseg; i++) {
    const double *a = q + 2 * i, *b = q + 2 * ((i + 1) % m);
    double dx = b[0] - a[0], dy = b[1] - a[1], d = hypot(dx, dy), nx, ny, r[8];
    nx = -dy / d * hw; ny = dx / d * hw;
    r[0] = a[0] + nx; r[1] = a[1] + ny; r[2] = b[0] + nx; r[3] = b[1] + ny;
    r[4] = b[0] - nx; r[5] = b[1] - ny; r[6] = a[0] - nx; r[7] = a[1] - ny;
    ok = add_pos(out, r, 4);
  }
  /* joins */
  for (i = closed ? 0 : 1; ok && i < (closed ? m : m - 1); i++) {
    const double *v = q + 2 * i, *pv = q + 2 * ((i + m - 1) % m), *nx = q + 2 * ((i + 1) % m);
    double d1x = v[0] - pv[0], d1y = v[1] - pv[1], d2x = nx[0] - v[0], d2y = nx[1] - v[1];
    double l1 = hypot(d1x, d1y), l2 = hypot(d2x, d2y), n1x, n1y, n2x, n2y, cr, sgn, t[8];
    d1x /= l1; d1y /= l1; d2x /= l2; d2y /= l2;
    n1x = -d1y; n1y = d1x; n2x = -d2y; n2y = d2x;
    cr = d1x * d2y - d1y * d2x;
    if (fabs(cr) < 1e-12 && d1x * d2x + d1y * d2y > 0) continue;      /* (straight on) */
    if (st->join == JOIN_ROUND) { ok = add_disc(out, v[0], v[1], hw); continue; }
    sgn = cr > 0 ? -1 : 1;                                             /* the outer side */
    t[0] = v[0]; t[1] = v[1];
    t[2] = v[0] + sgn * n1x * hw; t[3] = v[1] + sgn * n1y * hw;
    t[6] = v[0] + sgn * n2x * hw; t[7] = v[1] + sgn * n2y * hw;
    if (st->join == JOIN_MITER) {
      double mx = n1x + n2x, my = n1y + n2y, ml = hypot(mx, my), cosh;
      if (ml > 1e-12) {
        mx /= ml; my /= ml;
        cosh = mx * n1x + my * n1y;
        if (cosh > 1e-9 && 1 / cosh <= lim) {
          t[4] = v[0] + sgn * mx * hw / cosh; t[5] = v[1] + sgn * my * hw / cosh;
          ok = add_pos(out, t, 4);
          continue;
        }
      }
    }
    t[4] = t[6]; t[5] = t[7];
    ok = add_pos(out, t, 3);
  }
  /* caps */
  if (ok && !closed && st->cap != CAP_BUTT) {
    int e;
    for (e = 0; ok && e < 2; e++) {
      const double *p = e ? q + 2 * (m - 1) : q, *o = e ? q + 2 * (m - 2) : q + 2;
      double dx = p[0] - o[0], dy = p[1] - o[1], d = hypot(dx, dy), nx, ny, r[8];
      if (st->cap == CAP_ROUND) { ok = add_disc(out, p[0], p[1], hw); continue; }
      dx /= d; dy /= d; nx = -dy * hw; ny = dx * hw;
      r[0] = p[0] + nx; r[1] = p[1] + ny; r[2] = p[0] + nx + dx * hw; r[3] = p[1] + ny + dy * hw;
      r[4] = p[0] - nx + dx * hw; r[5] = p[1] - ny + dy * hw; r[6] = p[0] - nx; r[7] = p[1] - ny;
      ok = add_pos(out, r, 4);
    }
  }
  free(q);
  return ok;
}
int poly_dash(const Poly *pl, const double *dash, int ndash, double off, Poly *out)
{
  double total = 0;
  i32 c, start = 0;
  int i;
  for (i = 0; i < ndash; i++) total += dash[i] > 0 ? dash[i] : 0;
  for (c = 0; c < pl->nc; start = pl->end[c], c++) {
    i32 n = pl->end[c] - start, k, nseg = pl->closed[c] ? n : n - 1;
    int di = 0, on = 1;
    double left;
    if (total <= 0) {                                       /* (no pattern: the contour as it is) */
      for (k = 0; k < n; k++) if (!poly_add(out, pl->xy[2 * (start + k)], pl->xy[2 * (start + k) + 1])) return 0;
      if (pl->closed[c] && n > 0 && !poly_add(out, pl->xy[2 * start], pl->xy[2 * start + 1])) return 0;
      if (!poly_end(out, 0)) return 0;
      continue;
    }
    /* where the pattern starts */
    off = fmod(off, total);
    if (off < 0) off += total;
    left = dash[0];
    while (off > 0) {
      if (off < left) { left -= off; break; }
      off -= left; di = (di + 1) % ndash; on = !on; left = dash[di];
    }
    if (on && !poly_add(out, pl->xy[2 * start], pl->xy[2 * start + 1])) return 0;
    for (k = 0; k < nseg; k++) {
      const double *a = pl->xy + 2 * (start + k), *b = pl->xy + 2 * (start + (k + 1) % n);
      double len = hypot(b[0] - a[0], b[1] - a[1]), pos = 0;
      while (len - pos > left) {
        double t;
        pos += left;
        t = pos / len;
        if (on) { if (!poly_add(out, a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t) || !poly_end(out, 0)) return 0; }
        else if (!poly_add(out, a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)) return 0;
        on = !on; di = (di + 1) % ndash; left = dash[di] > 0 ? dash[di] : 0;
        if (left <= 0 && total > 0) continue;
      }
      left -= len - pos;
      if (on && !poly_add(out, b[0], b[1])) return 0;
    }
    if (on && !poly_end(out, 0)) return 0;
  }
  return 1;
}
int poly_stroke(const Poly *pl, const StrokeStyle *st, Poly *out)
{
  i32 c, start = 0;
  Poly dashed;
  if (st->ndash > 0) {
    int ok;
    poly_init(&dashed);
    ok = poly_dash(pl, st->dash, st->ndash > 8 ? 8 : st->ndash, st->dash_off, &dashed);
    if (ok) {
      for (c = 0, start = 0; ok && c < dashed.nc; start = dashed.end[c], c++)
        ok = stroke_contour(dashed.xy + 2 * start, dashed.end[c] - start, 0, st, out);
    }
    poly_free(&dashed);
    return ok;
  }
  for (c = 0; c < pl->nc; start = pl->end[c], c++)
    if (!stroke_contour(pl->xy + 2 * start, pl->end[c] - start, pl->closed[c], st, out)) return 0;
  return 1;
}

int path_bounds(const Path *p, double *x0, double *y0, double *x1, double *y1)
{
  Poly pl;
  i32 i;
  if (!p || !p->na) return 0;
  poly_init(&pl);
  if (!path_flatten(p, 0.1, &pl) || !pl.n) { poly_free(&pl); return 0; }
  *x0 = *x1 = pl.xy[0]; *y0 = *y1 = pl.xy[1];
  for (i = 1; i < pl.n; i++) {
    double x = pl.xy[2 * i], y = pl.xy[2 * i + 1];
    if (x < *x0) *x0 = x;
    if (x > *x1) *x1 = x;
    if (y < *y0) *y0 = y;
    if (y > *y1) *y1 = y;
  }
  poly_free(&pl);
  return 1;
}

/* ------------------------------------------------------------------ hit testing */
static double seg_dist(double px, double py, double ax, double ay, double bx, double by, double *t)
{
  double dx = bx - ax, dy = by - ay, l = dx * dx + dy * dy, u = l > 0 ? ((px - ax) * dx + (py - ay) * dy) / l : 0;
  if (u < 0) u = 0;
  if (u > 1) u = 1;
  *t = u;
  return hypot(px - ax - u * dx, py - ay - u * dy);
}
int path_hit(const Path *p, double x, double y, double tol, int handles_all, PathHit *h)
{
  int s, k;
  double best = tol;
  memset(h, 0, sizeof *h);
  h->what = PH_NONE; h->dist = 1e30;
  if (!p) return PH_NONE;
  for (s = 0; s < p->ns; s++)
    for (k = 0; k < p->s[s].n; k++) {
      const PathAnchor *a = &p->a[p->s[s].first + k];
      double d = hypot(a->x - x, a->y - y);
      if (d <= best && (h->what != PH_ANCHOR || d < h->dist)) { h->what = PH_ANCHOR; h->s = s; h->k = k; h->dist = d; best = d; }
    }
  if (h->what == PH_ANCHOR) return h->what;
  for (s = 0; s < p->ns; s++)
    for (k = 0; k < p->s[s].n; k++) {
      const PathAnchor *a = &p->a[p->s[s].first + k];
      double d;
      if (!handles_all && !a->sel) continue;
      if ((a->ix != a->x || a->iy != a->y) && (d = hypot(a->ix - x, a->iy - y)) <= best) { h->what = PH_IN; h->s = s; h->k = k; h->dist = best = d; }
      if ((a->ox != a->x || a->oy != a->y) && (d = hypot(a->ox - x, a->oy - y)) <= best) { h->what = PH_OUT; h->s = s; h->k = k; h->dist = best = d; }
    }
  if (h->what != PH_NONE) return h->what;
  for (s = 0; s < p->ns; s++) {
    int nseg = path_nsegs(p, s);
    for (k = 0; k < nseg; k++) {
      double c[8], px, py, qx, qy, t;
      int i, n = 48;
      path_seg_ctrl(p, s, k, c);
      if (is_line(c)) n = 1;
      px = c[0]; py = c[1];
      for (i = 1; i <= n; i++) {
        double d;
        bez_point(c, (double)i / n, &qx, &qy);
        d = seg_dist(x, y, px, py, qx, qy, &t);
        if (d <= best) { h->what = PH_SEGMENT; h->s = s; h->k = k; h->t = (i - 1 + t) / n; h->dist = best = d; }
        px = qx; py = qy;
      }
    }
  }
  return h->what;
}

/* ------------------------------------------------------------------ SVG path data */
static void put_num(Buf *b, double v)
{
  char t[48];
  int n;
  if (fabs(v) < 0.0005) v = 0;
  n = sprintf(t, "%.3f", v);
  while (n > 0 && t[n - 1] == '0') t[--n] = 0;
  if (n > 0 && t[n - 1] == '.') t[--n] = 0;
  buf_adds(b, t);
}
static void put_pt(Buf *b, double x, double y) { buf_addc(b, ' '); put_num(b, x); buf_addc(b, ' '); put_num(b, y); }
char *path_to_svg(const Path *p)
{
  Buf b;
  int s, k;
  buf_init(&b);
  buf_adds(&b, "");
  for (s = 0; p && s < p->ns; s++) {
    const SubPath *sp = &p->s[s];
    int nseg = path_nsegs(p, s);
    if (!sp->n) continue;
    if (b.len) buf_addc(&b, ' ');
    buf_addc(&b, 'M'); put_pt(&b, p->a[sp->first].x, p->a[sp->first].y);
    for (k = 0; k < nseg; k++) {
      double c[8];
      path_seg_ctrl(p, s, k, c);
      if (sp->closed && k == nseg - 1 && is_line(c)) break;           /* (Z draws it) */
      if (is_line(c)) { buf_adds(&b, " L"); put_pt(&b, c[6], c[7]); }
      else { buf_adds(&b, " C"); put_pt(&b, c[2], c[3]); put_pt(&b, c[4], c[5]); put_pt(&b, c[6], c[7]); }
    }
    if (sp->closed) buf_adds(&b, " Z");
  }
  if (b.oom) { buf_free(&b); return NULL; }
  return buf_take(&b);
}
char *path_types(const Path *p)
{
  char *t = (char *)malloc((size_t)(p ? p->na : 0) + 1);
  i32 i;
  if (!t) return NULL;
  for (i = 0; p && i < p->na; i++) t[i] = p->a[i].type == PA_SMOOTH ? 's' : 'c';
  t[p ? p->na : 0] = 0;
  return t;
}
void path_set_types(Path *p, const char *t)
{
  i32 i;
  if (!t || !own(p)) return;
  for (i = 0; i < p->na && t[i]; i++) p->a[i].type = t[i] == 's' ? PA_SMOOTH : PA_CORNER;
}
static const char *skip_sep(const char *s) { while (*s == ' ' || *s == ',' || *s == '\t' || *s == '\n' || *s == '\r') s++; return s; }
static int num(const char **s, double *v)
{
  char *e;
  const char *t = skip_sep(*s);
  *v = strtod(t, &e);
  if (e == t) return 0;
  *s = e;
  return 1;
}
/* an SVG elliptical arc as cubics (the SVG spec's endpoint-to-centre conversion) */
static int svg_arc(Path *p, double x0, double y0, double rx, double ry, double phi, int large, int sweep, double x, double y)
{
  double cp = cos(phi), sp = sin(phi), dx = (x0 - x) / 2, dy = (y0 - y) / 2;
  double x1 = cp * dx + sp * dy, y1 = -sp * dx + cp * dy, lam, co, cx1, cy1, cx, cy, th, dth, ux, uy, vx, vy;
  int n, i;
  rx = fabs(rx); ry = fabs(ry);
  if (rx == 0 || ry == 0) return path_line_to(p, x, y);
  lam = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
  if (lam > 1) { rx *= sqrt(lam); ry *= sqrt(lam); }
  co = (rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1) / (rx * rx * y1 * y1 + ry * ry * x1 * x1);
  co = co < 0 ? 0 : sqrt(co);
  if (large == sweep) co = -co;
  cx1 = co * rx * y1 / ry; cy1 = -co * ry * x1 / rx;
  cx = cp * cx1 - sp * cy1 + (x0 + x) / 2; cy = sp * cx1 + cp * cy1 + (y0 + y) / 2;
  ux = (x1 - cx1) / rx; uy = (y1 - cy1) / ry; vx = (-x1 - cx1) / rx; vy = (-y1 - cy1) / ry;
  th = atan2(uy, ux);
  dth = atan2(ux * vy - uy * vx, ux * vx + uy * vy);
  if (!sweep && dth > 0) dth -= 2 * M_PI;
  else if (sweep && dth < 0) dth += 2 * M_PI;
  n = (int)ceil(fabs(dth) / (M_PI / 2) - 1e-9);
  if (n < 1) n = 1;
  for (i = 0; i < n; i++) {
    double a0 = th + dth * i / n, a1 = th + dth * (i + 1) / n, k = 4.0 / 3 * tan((a1 - a0) / 4);
    double e0x = cos(a0), e0y = sin(a0), e1x = cos(a1), e1y = sin(a1);
    double p1x = e0x - k * e0y, p1y = e0y + k * e0x, p2x = e1x + k * e1y, p2y = e1y - k * e1x;
#define TX(u, v) (cx + cp * rx * (u) - sp * ry * (v))
#define TY(u, v) (cy + sp * rx * (u) + cp * ry * (v))
    if (!path_curve_to(p, TX(p1x, p1y), TY(p1x, p1y), TX(p2x, p2y), TY(p2x, p2y), i == n - 1 ? x : TX(e1x, e1y), i == n - 1 ? y : TY(e1x, e1y))) return 0;
#undef TX
#undef TY
  }
  return 1;
}
Path *path_from_svg(const char *d)
{
  Path *p = path_new();
  const char *s = d ? d : "";
  double cx = 0, cy = 0, sx = 0, sy = 0, lcx = 0, lcy = 0;   /* the pen, the subpath's start, the last control point */
  char cmd = 0, last = 0;
  i32 i;
  if (!p) return NULL;
  for (;;) {
    double v[7];
    int rel, ok = 1;
    s = skip_sep(s);
    if (!*s) break;
    if ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')) cmd = *s++;
    else if (!cmd || cmd == 'Z' || cmd == 'z') break;      /* (numbers with no command: stop) */
    rel = cmd >= 'a';
    switch (cmd | 32) {
    case 'm':
      if (!num(&s, &v[0]) || !num(&s, &v[1])) goto end;
      if (rel) { v[0] += cx; v[1] += cy; }
      ok = path_move_to(p, v[0], v[1]) != 0;
      cx = sx = lcx = v[0]; cy = sy = lcy = v[1];
      cmd = rel ? 'l' : 'L';                               /* (more pairs are lines) */
      last = 'm';
      break;
    case 'l':
      if (!num(&s, &v[0]) || !num(&s, &v[1])) goto end;
      if (rel) { v[0] += cx; v[1] += cy; }
      ok = path_line_to(p, v[0], v[1]);
      cx = lcx = v[0]; cy = lcy = v[1]; last = 'l';
      break;
    case 'h': case 'v':
      if (!num(&s, &v[0])) goto end;
      if ((cmd | 32) == 'h') cx = rel ? cx + v[0] : v[0]; else cy = rel ? cy + v[0] : v[0];
      ok = path_line_to(p, cx, cy);
      lcx = cx; lcy = cy; last = 'l';
      break;
    case 'c': case 's': {
      int k, sm = (cmd | 32) == 's';
      for (k = sm ? 2 : 0; k < 6; k++) if (!num(&s, &v[k])) goto end;
      if (sm) { v[0] = last == 'c' ? 2 * cx - lcx : cx; v[1] = last == 'c' ? 2 * cy - lcy : cy; }
      if (rel) { for (k = sm ? 2 : 0; k < 6; k += 2) { v[k] += cx; v[k + 1] += cy; } }
      ok = path_curve_to(p, v[0], v[1], v[2], v[3], v[4], v[5]);
      lcx = v[2]; lcy = v[3]; cx = v[4]; cy = v[5]; last = 'c';
      break;
    }
    case 'q': case 't': {
      int k, sm = (cmd | 32) == 't';
      double qx, qy;
      for (k = sm ? 2 : 0; k < 4; k++) if (!num(&s, &v[k])) goto end;
      if (sm) { v[0] = last == 'q' ? 2 * cx - lcx : cx; v[1] = last == 'q' ? 2 * cy - lcy : cy; }
      else if (rel) { v[0] += cx; v[1] += cy; }
      if (rel) { v[2] += cx; v[3] += cy; }
      qx = v[0]; qy = v[1];
      ok = path_curve_to(p, cx + 2.0 / 3 * (qx - cx), cy + 2.0 / 3 * (qy - cy), v[2] + 2.0 / 3 * (qx - v[2]), v[3] + 2.0 / 3 * (qy - v[3]), v[2], v[3]);
      lcx = qx; lcy = qy; cx = v[2]; cy = v[3]; last = 'q';
      break;
    }
    case 'a': {
      int k;
      for (k = 0; k < 7; k++) if (!num(&s, &v[k])) goto end;
      if (rel) { v[5] += cx; v[6] += cy; }
      if (!p->ns) ok = path_move_to(p, cx, cy) != 0;
      ok = ok && svg_arc(p, cx, cy, v[0], v[1], v[2] * M_PI / 180, v[3] != 0, v[4] != 0, v[5], v[6]);
      cx = lcx = v[5]; cy = lcy = v[6]; last = 'a';
      break;
    }
    case 'z':
      path_close(p);
      cx = lcx = sx; cy = lcy = sy; last = 'z';
      break;
    default: goto end;
    }
    if (!ok) { path_free(p); return NULL; }
  }
end:
  /* smooth where the handles are in line */
  for (i = 0; i < p->na; i++) {
    PathAnchor *a = &p->a[i];
    double ax = a->ix - a->x, ay = a->iy - a->y, bx = a->ox - a->x, by = a->oy - a->y, la = hypot(ax, ay), lb = hypot(bx, by);
    if (la > 1e-6 && lb > 1e-6 && fabs(ax * by - ay * bx) < 1e-3 * la * lb && ax * bx + ay * by < 0) a->type = PA_SMOOTH;
  }
  return p;
}

/* ------------------------------------------------------------------ the document's paths */
Path *doc_path(Doc *d, int i, int make)
{
  if (i == -1) {
    if (!d->workpath && make) d->workpath = path_new();
    return d->workpath;
  }
  return i >= 0 && i < d->npaths ? d->path[i] : NULL;
}
static void name_path(Doc *d, int i, const char *name)
{
  char tmp[32];
  size_t l;
  if (!name || !*name) {
    int n, k, used;
    for (n = 1; n < 1000; n++) {
      sprintf(tmp, "Path %d", n);
      for (used = 0, k = 0; k < d->npaths && !used; k++) used = k != i && !strcmp(d->path_name[k], tmp);
      if (!used) break;
    }
    name = tmp;
  }
  l = strlen(name);
  if (l > 31) { l = 31; while (l > 0 && ((u8)name[l] & 0xC0) == 0x80) l--; }
  memmove(d->path_name[i], name, l);
  d->path_name[i][l] = 0;
}
static int insert_path(Doc *d, Path *p, const char *name, const char *action)
{
  int i;
  if (!p) return -1;
  if (d->npaths >= MAXPATHS) { path_free(p); return -1; }
  doc_begin(d, action);
  doc_note_paths(d);
  i = d->npaths++;
  d->path[i] = p;
  name_path(d, i, name);
  d->path_active = i + 1;
  doc_end(d);
  return i;
}
int doc_path_new(Doc *d, const char *name) { return insert_path(d, path_new(), name, "New Path"); }
int doc_path_save_work(Doc *d, const char *name)
{
  int i;
  if (!d->workpath || d->npaths >= MAXPATHS) return -1;
  doc_begin(d, "Save Path");
  doc_note_paths(d);
  i = d->npaths++;
  d->path[i] = d->workpath; d->workpath = NULL;
  name_path(d, i, name);
  d->path_active = i + 1;
  doc_end(d);
  return i;
}
int doc_path_delete(Doc *d, int i)
{
  if (i == -1) {
    if (!d->workpath) return 0;
    doc_begin(d, "Delete Path");
    doc_note_paths(d);
    path_free(d->workpath); d->workpath = NULL;
    if (d->path_active == -1) d->path_active = 0;
    doc_end(d);
    return 1;
  }
  if (i < 0 || i >= d->npaths) return 0;
  doc_begin(d, "Delete Path");
  doc_note_paths(d);
  path_free(d->path[i]);
  memmove(&d->path[i], &d->path[i + 1], (size_t)(d->npaths - i - 1) * sizeof(Path *));
  memmove(d->path_name[i], d->path_name[i + 1], (size_t)(d->npaths - i - 1) * 32);
  d->npaths--;
  d->path[d->npaths] = NULL;
  if (d->path_active == i + 1) d->path_active = 0;
  else if (d->path_active > i + 1) d->path_active--;
  doc_end(d);
  return 1;
}
int doc_path_duplicate(Doc *d, int i)
{
  Path *p = doc_path(d, i, 0);
  char nm[40];
  if (!p) return -1;
  sprintf(nm, "%.24s copy", i == -1 ? "Work Path" : d->path_name[i]);
  return insert_path(d, path_copy(p), nm, "Duplicate Path");
}
int doc_path_rename(Doc *d, int i, const char *name)
{
  if (i < 0 || i >= d->npaths) return 0;
  doc_begin(d, "Rename Path");
  doc_note_paths(d);
  name_path(d, i, name);
  doc_end(d);
  return 1;
}
int doc_path_move(Doc *d, int from, int to)
{
  Path *p;
  char nm[32];
  if (from < 0 || from >= d->npaths || to < 0 || to >= d->npaths || from == to) return 0;
  doc_begin(d, "Move Path");
  doc_note_paths(d);
  p = d->path[from]; memcpy(nm, d->path_name[from], 32);
  if (to < from) { memmove(&d->path[to + 1], &d->path[to], (size_t)(from - to) * sizeof(Path *)); memmove(d->path_name[to + 1], d->path_name[to], (size_t)(from - to) * 32); }
  else { memmove(&d->path[from], &d->path[from + 1], (size_t)(to - from) * sizeof(Path *)); memmove(d->path_name[from], d->path_name[from + 1], (size_t)(to - from) * 32); }
  d->path[to] = p; memcpy(d->path_name[to], nm, 32);
  d->path_active = to + 1;
  doc_end(d);
  return 1;
}
int doc_path_set(Doc *d, int i, const Path *p, const char *action)
{
  Path *c = path_copy(p);
  if (!c || (i != -1 && (i < 0 || i >= d->npaths))) { path_free(c); return 0; }
  doc_begin(d, action ? action : "Edit Path");
  doc_note_paths(d);
  if (i == -1) { path_free(d->workpath); d->workpath = c; d->path_active = -1; }
  else { path_free(d->path[i]); d->path[i] = c; }
  doc_end(d);
  return 1;
}
