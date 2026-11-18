/*
 * vecx.c - Combine Shapes and the custom shape library (see vecx.h). Easel round 3.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vecx.h"
#include "pathops.h"
#include "shape.h"
#include "live.h"

#define MASK_BUDGET 4000000L

/* ------------------------------------------------------------------ booleans through a fine mask */
typedef struct { double x0, y0, s; i32 w, h; } Grid;
/* the grid covering box b (canvas units) at up to 4 mask pixels a canvas pixel */
static int grid_for(Grid *g, double x0, double y0, double x1, double y1)
{
  double bw = x1 - x0 + 2, bh = y1 - y0 + 2, s = 4;
  if (!(bw > 0 && bh > 0) || bw > 1e6 || bh > 1e6) return 0;
  while (s > 0.25 && bw * s * bh * s > MASK_BUDGET) s /= 2;
  g->s = s; g->x0 = x0 - 1; g->y0 = y0 - 1;
  g->w = (i32)ceil(bw * s) + 2; g->h = (i32)ceil(bh * s) + 2;
  return (long)g->w * g->h <= MASK_BUDGET * 2;
}
/* path p's fill on the grid, 0 or 255 */
static int draw(const Grid *g, const Path *p, u8 *m)
{
  Path *q = path_copy(p);
  Poly pl;
  double mt[6];
  long k, n = (long)g->w * g->h;
  int ok;
  if (!q) return 0;
  mt[0] = g->s; mt[1] = 0; mt[2] = -g->x0 * g->s; mt[3] = 0; mt[4] = g->s; mt[5] = -g->y0 * g->s;
  path_transform(q, mt);
  poly_init(&pl);
  ok = path_flatten(q, 0.1, &pl) && poly_raster(&pl, p->evenodd, box_make(0, 0, g->w, g->h), m, g->w);
  for (k = 0; ok && k < n; k++) m[k] = m[k] >= 128 ? 255 : 0;
  poly_free(&pl); path_free(q);
  return ok;
}
static void combine(u8 *acc, const u8 *m, long n, int op)
{
  long k;
  for (k = 0; k < n; k++)
    switch (op) {
    case CS_SUBTRACT: if (m[k]) acc[k] = 0; break;
    case CS_INTERSECT: if (!m[k]) acc[k] = 0; break;
    case CS_EXCLUDE: acc[k] ^= m[k]; break;
    default: acc[k] |= m[k]; break;
    }
}
/* the mask traced back to canvas units */
static Path *trace(const Grid *g, const u8 *acc)
{
  Path *p = path_from_mask(acc, g->w, g->h, box_make(0, 0, g->w, g->h), g->s >= 1 ? 0.3 * g->s : 0.5);
  double mt[6];
  if (!p) return NULL;
  mt[0] = 1 / g->s; mt[1] = 0; mt[2] = g->x0; mt[3] = 0; mt[4] = 1 / g->s; mt[5] = g->y0;
  path_transform(p, mt);
  p->evenodd = 1;
  return p;
}
static int bounds_of(const Path *const *ps, int n, double *x0, double *y0, double *x1, double *y1)
{
  int i, any = 0;
  for (i = 0; i < n; i++) {
    double a, b, c, e;
    if (!ps[i] || !path_bounds(ps[i], &a, &b, &c, &e)) continue;
    if (!any || a < *x0) *x0 = a;
    if (!any || b < *y0) *y0 = b;
    if (!any || c > *x1) *x1 = c;
    if (!any || e > *y1) *y1 = e;
    any = 1;
  }
  return any;
}
static Path *boolean_n(const Path *const *ps, int n, int op)
{
  Grid g;
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  u8 *acc, *m;
  long cells;
  int i, ok = 1;
  Path *r = NULL;
  if (!bounds_of(ps, n, &x0, &y0, &x1, &y1) || !grid_for(&g, x0, y0, x1, y1)) return NULL;
  cells = (long)g.w * g.h;
  acc = (u8 *)calloc((size_t)cells, 1); m = (u8 *)malloc((size_t)cells);
  if (!acc || !m) { free(acc); free(m); return NULL; }
  for (i = 0; ok && i < n; i++) {
    if (!ps[i]) continue;
    if (!(ok = draw(&g, ps[i], m))) break;
    if (!i) memcpy(acc, m, (size_t)cells); else combine(acc, m, cells, op);
  }
  if (ok) r = trace(&g, acc);
  free(acc); free(m);
  return r;
}
Path *path_boolean(const Path *a, const Path *b, int op)
{
  const Path *ps[2];
  ps[0] = a; ps[1] = b;
  return boolean_n(ps, b ? 2 : 1, op);
}

int doc_combine_shapes(Doc *d, int op)
{
  int idx[MAXLAYERS], n = 0, i, ok;
  Path *ps[MAXLAYERS], view[MAXLAYERS], *r;
  ShapeParams sp;
  if (op < 0 || op >= CS_N || d->active < 0 || d->active >= d->nlay) return -1;
  for (i = 0; i < d->nlay; i++)
    if (layer_shape(d->lay[i]) && (i == d->active || d->lay[i]->picked)) idx[n++] = i;
  if (!n || (op != CS_MERGE && n < 2)) return -1;
  for (i = 0; i < n; i++) { view[i] = shape_path(layer_shape(d->lay[idx[i]])); view[i].evenodd = layer_shape(d->lay[idx[i]])->evenodd; ps[i] = &view[i]; }
  doc_begin(d, op == CS_MERGE ? "Merge Shape Components" : "Combine Shapes");
  if (op == CS_MERGE) {
    /* each layer by itself */
    ok = 1;
    for (i = 0; ok && i < n; i++) {
      r = boolean_n((const Path *const *)&ps[i], 1, CS_UNITE);
      if (!r) { ok = 0; break; }
      sp = *layer_shape(d->lay[idx[i]]);
      sp.kind = SH_CUSTOM; sp.evenodd = 1; sp.radii_on = 0;
      ok = doc_set_shape(d, idx[i], &sp, r);
      path_free(r);
      if (ok) view[i] = shape_path(layer_shape(d->lay[idx[i]]));     /* (the block moved) */
    }
    doc_end(d);
    if (!ok) { doc_undo(d); return -1; }
    return idx[0];
  }
  r = boolean_n((const Path *const *)ps, n, op);
  if (!r || !r->na) { path_free(r); doc_end(d); doc_undo(d); return -1; }
  /* (the upper layers deleted first: their step keeps every layer as it was) */
  for (i = n - 1, ok = 1; ok && i >= 1; i--) ok = doc_delete_layer(d, idx[i]);
  sp = *layer_shape(d->lay[idx[0]]);
  sp.kind = SH_CUSTOM; sp.evenodd = 1; sp.radii_on = 0;
  ok = ok && doc_set_shape(d, idx[0], &sp, r);
  path_free(r);
  if (ok) d->active = idx[0];
  for (i = 0; i < d->nlay; i++) d->lay[i]->picked = 0;
  doc_end(d);
  if (!ok) { doc_undo(d); return -1; }
  return idx[0];
}

/* ------------------------------------------------------------------ the shape library */
/* Easel's own drawings, in a 100 x 100 box (SVG path data) */
static const struct { const char *name, *svg; } shape_lib[] = {
  { "Arrow", "M0 35 L55 35 L55 10 L100 50 L55 90 L55 65 L0 65 Z" },
  { "Double Arrow", "M0 50 L25 15 L25 35 L75 35 L75 15 L100 50 L75 85 L75 65 L25 65 L25 85 Z" },
  { "Heart", "M50 95 C20 70 0 52 0 30 C0 13 13 2 28 2 C38 2 46 8 50 17 C54 8 62 2 72 2 C87 2 100 13 100 30 C100 52 80 70 50 95 Z" },
  { "Speech Bubble", "M15 5 L85 5 C93 5 100 12 100 20 L100 60 C100 68 93 75 85 75 L45 75 L20 98 L25 75 L15 75 C7 75 0 68 0 60 L0 20 C0 12 7 5 15 5 Z" },
  { "Check Mark", "M0 55 L15 40 L38 62 L85 10 L100 25 L38 92 Z" },
  { "Cross", "M35 0 L65 0 L65 35 L100 35 L100 65 L65 65 L65 100 L35 100 L35 65 L0 65 L0 35 L35 35 Z" },
  { "Lightning", "M40 0 L80 0 L58 38 L85 38 L25 100 L42 55 L15 55 Z" },
  { "Crescent Moon", "M60 2 C30 8 10 30 10 52 C10 78 32 98 58 98 C75 98 89 91 98 78 C88 83 80 85 70 85 C46 85 28 67 28 45 C28 26 40 10 60 2 Z" },
  { "Raindrop", "M50 0 C60 25 85 45 85 65 C85 85 70 100 50 100 C30 100 15 85 15 65 C15 45 40 25 50 0 Z" },
  { "Ring", "M50 0 C78 0 100 22 100 50 C100 78 78 100 50 100 C22 100 0 78 0 50 C0 22 22 0 50 0 Z "
            "M50 25 C36 25 25 36 25 50 C25 64 36 75 50 75 C64 75 75 64 75 50 C75 36 64 25 50 25 Z" },
  { "Puzzle Piece", "M0 20 L35 20 C30 5 40 0 50 0 C60 0 70 5 65 20 L100 20 L100 50 C85 45 80 55 80 62 C80 70 85 80 100 75 "
                    "L100 100 L0 100 L0 75 C15 80 20 70 20 62 C20 55 15 45 0 50 Z" },
  { "Banner", "M0 20 L100 20 L85 50 L100 80 L0 80 L15 50 Z" },
  { "Shield", "M50 0 L100 15 C100 60 80 85 50 100 C20 85 0 60 0 15 Z" },
  { "Paw Print", "M50 45 C68 45 82 62 82 78 C82 92 70 98 50 92 C30 98 18 92 18 78 C18 62 32 45 50 45 Z "
                 "M22 22 C30 22 35 32 33 40 C31 48 22 50 16 44 C10 38 13 22 22 22 Z M78 22 C87 22 90 38 84 44 C78 50 69 48 67 40 C65 32 70 22 78 22 Z "
                 "M40 2 C48 2 51 14 48 22 C45 30 35 30 32 22 C29 14 32 2 40 2 Z M60 2 C68 2 71 14 68 22 C65 30 55 30 52 22 C49 14 52 2 60 2 Z" },
  { "Leaf", "M5 95 C5 40 40 5 95 5 C95 60 60 95 5 95 Z" },
  { "Gear", "M44 0 L56 0 L58 13 L68 17 L79 9 L88 18 L80 29 L85 40 L98 42 L98 56 L85 59 L81 69 L89 80 L80 89 L69 81 L59 85 L56 98 "
            "L44 98 L41 85 L31 81 L20 89 L11 80 L19 69 L15 59 L2 56 L2 42 L15 40 L19 29 L11 18 L20 9 L31 17 L41 13 Z "
            "M50 33 C40 33 33 40 33 49 C33 58 40 65 50 65 C60 65 67 58 67 49 C67 40 60 33 50 33 Z" },
};
#define NLIB ((int)(sizeof shape_lib / sizeof shape_lib[0]))
int shape_library_count(void) { return NLIB; }
const char *shape_library_name(int k) { return k >= 0 && k < NLIB ? shape_lib[k].name : ""; }
int shape_library_path(int k, Path *p, double x0, double y0, double x1, double y1)
{
  Path *q;
  double m[6];
  int ok;
  if (k < 0 || k >= NLIB) return 0;
  if (!(q = path_from_svg(shape_lib[k].svg))) return 0;
  m[0] = (x1 - x0) / 100; m[1] = 0; m[2] = x0; m[3] = 0; m[4] = (y1 - y0) / 100; m[5] = y0;
  path_transform(q, m);
  q->evenodd = 1;
  ok = path_append(p, q);
  p->evenodd = 1;
  path_free(q);
  return ok;
}
