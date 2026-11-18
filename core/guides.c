/*
 * guides.c - guides, grid, snapping and ruler spacing (see guides.h).
 */
#include <math.h>
#include <string.h>
#include "guides.h"

static i32 *garr(Doc *d, int vertical, int **n) { *n = vertical ? &d->guides.nv : &d->guides.nh; return vertical ? d->guides.v : d->guides.h; }
int guide_add(Doc *d, int vertical, i32 pos)
{
  int *n, i;
  i32 *a = garr(d, vertical, &n);
  if (*n >= MAXGUIDES || pos < 0 || pos > (vertical ? d->w : d->h)) return 0;
  for (i = 0; i < *n; i++) if (a[i] == pos) return 0;
  a[(*n)++] = pos;
  return 1;
}
int guide_remove(Doc *d, int vertical, int i)
{
  int *n;
  i32 *a = garr(d, vertical, &n);
  if (i < 0 || i >= *n) return 0;
  memmove(a + i, a + i + 1, sizeof(i32) * (size_t)(*n - i - 1));
  (*n)--;
  return 1;
}
int guide_move(Doc *d, int vertical, int i, i32 pos)
{
  int *n;
  i32 *a = garr(d, vertical, &n);
  if (i < 0 || i >= *n || d->guides.lock_guides) return 0;
  if (pos < 0 || pos > (vertical ? d->w : d->h)) return guide_remove(d, vertical, i);
  a[i] = pos;
  return 1;
}
void guide_clear(Doc *d) { d->guides.nh = d->guides.nv = 0; }
int guide_find(const Doc *d, int vertical, double pos, double tol)
{
  int n = vertical ? d->guides.nv : d->guides.nh, i, best = -1;
  const i32 *a = vertical ? d->guides.v : d->guides.h;
  double bd = tol;
  for (i = 0; i < n; i++) { double dd = fabs(a[i] - pos); if (dd <= bd) { bd = dd; best = i; } }
  return best;
}
void grid_set(Doc *d, int sx, int sy, int ox, int oy, int sub)
{
  d->guides.grid_x = (u16)(sx < 1 ? 1 : sx > 65535 ? 65535 : sx);
  d->guides.grid_y = (u16)(sy < 1 ? 1 : sy > 65535 ? 65535 : sy);
  d->guides.grid_ox = (i16)ox; d->guides.grid_oy = (i16)oy;
  d->guides.grid_sub = (u8)(sub < 1 ? 1 : sub > 64 ? 64 : sub);
}
int grid_spacing_x(const Doc *d) { return d->guides.grid_x ? d->guides.grid_x : 16; }
int grid_spacing_y(const Doc *d) { return d->guides.grid_y ? d->guides.grid_y : 16; }
double grid_first(const Doc *d, int vertical, double p, int sub, double *step)
{
  double s = vertical ? grid_spacing_x(d) : grid_spacing_y(d), o = vertical ? d->guides.grid_ox : d->guides.grid_oy;
  if (sub && d->guides.grid_sub > 1) s /= d->guides.grid_sub;
  *step = s;
  return o + ceil((p - o) / s) * s;
}
double snap_tolerance(const Doc *d) { return d->guides.snap_tol ? d->guides.snap_tol / 16.0 : 8.0; }
/* the nearest candidate to v within tol: best keeps the distance so far */
static void consider(double v, double c, double *best, double *bd)
{
  double dd = fabs(c - v);
  if (dd < *bd) { *bd = dd; *best = c; }
}
int (*snap_extra)(Doc *d, int vertical, int flags, double *out, int max);
int snap_coord(Doc *d, double *v, int vertical, int flags)
{
  double tol = snap_tolerance(d), bd = tol + 1e-9, best = *v, size = vertical ? d->w : d->h;
  int i;
  if (flags & SNAP_DOC) flags = d->guides.snap;
  if (flags & SNAP_GUIDES) {
    int n = vertical ? d->guides.nv : d->guides.nh;
    const i32 *a = vertical ? d->guides.v : d->guides.h;
    for (i = 0; i < n; i++) consider(*v, a[i], &best, &bd);
  }
  if (flags & SNAP_GRID) {
    double step, g = grid_first(d, vertical, *v, (flags & SNAP_SUB) != 0, &step);
    consider(*v, g, &best, &bd);
    consider(*v, g - step, &best, &bd);
  }
  if (flags & SNAP_EDGES) { consider(*v, 0, &best, &bd); consider(*v, size, &best, &bd); }
  if (flags & SNAP_CENTER) consider(*v, size / 2, &best, &bd);
  if (snap_extra && (flags & (64 | 128))) {
    /* r3: the other layers' edges and centres, the slices' edges (menu3.c) */
    double e[600];
    int n = snap_extra(d, vertical, flags, e, 600);
    for (i = 0; i < n; i++) consider(*v, e[i], &best, &bd);
  }
  if (bd <= tol) { *v = best; return 1; }
  if (flags & SNAP_PIXEL) { *v = floor(*v + 0.5); return 0; }
  return 0;
}
int snap_point(Doc *d, double *x, double *y, int flags)
{
  int r = 0;
  if (flags & SNAP_DOC) flags = d->guides.snap;
  if (snap_coord(d, x, 1, flags)) r |= 1;
  if (snap_coord(d, y, 0, flags)) r |= 2;
  return r;
}
int ruler_step(double zoom, int minpx, int *minor)
{
  static const int nice[3] = { 1, 2, 5 };
  long mag = 1;
  int k;
  if (zoom <= 0) zoom = 1;
  if (minpx < 1) minpx = 1;
  for (;;) {
    for (k = 0; k < 3; k++) {
      long s = nice[k] * mag;
      if (s * zoom >= minpx) {
        if (minor) *minor = nice[k] == 2 ? 2 : (s * zoom >= minpx * 2 && s >= 10 ? 10 : 5);
        if (minor && s < *minor) *minor = (int)s;
        return (int)s;
      }
    }
    if (mag > 100000000L) return (int)mag;
    mag *= 10;
  }
}
double aspect_ratio(const Doc *d) { return d->aspect_x && d->aspect_y ? (double)d->aspect_y / d->aspect_x : 1.0; }
