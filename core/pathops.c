/*
 * pathops.c - paths and the picture (see pathops.h): selections to and from paths, filling and
 * stroking. The selection's outline is traced along pixel edges and fitted with Bezier curves by
 * Easel's own simple method (the edges' midpoints simplified, then smooth curves through what is
 * left, corners where it turns sharply); no Potrace code. Easel 0.9.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pathops.h"
#include "channels.h"
#include "layers.h"

/* a box blur of a mask, radius r, three passes (as the marquees feather) */
static void blur(u8 *m, i32 w, i32 h, int r)
{
  u8 *t;
  i32 x, y;
  int pass;
  if (r < 1 || !(t = (u8 *)malloc((size_t)(w > h ? w : h)))) return;
  for (pass = 0; pass < 3; pass++) {
    for (y = 0; y < h; y++) {
      u8 *row = m + (long)y * w;
      long sum = 0;
      memcpy(t, row, (size_t)w);
      for (x = -r; x <= r; x++) sum += t[x < 0 ? 0 : x >= w ? w - 1 : x];
      for (x = 0; x < w; x++) {
        row[x] = (u8)(sum / (2 * r + 1));
        sum += t[x + r + 1 >= w ? w - 1 : x + r + 1] - t[x - r < 0 ? 0 : x - r];
      }
    }
    for (x = 0; x < w; x++) {
      long sum = 0;
      for (y = 0; y < h; y++) t[y] = m[(long)y * w + x];
      for (y = -r; y <= r; y++) sum += t[y < 0 ? 0 : y >= h ? h - 1 : y];
      for (y = 0; y < h; y++) {
        m[(long)y * w + x] = (u8)(sum / (2 * r + 1));
        sum += t[y + r + 1 >= h ? h - 1 : y + r + 1] - t[y - r < 0 ? 0 : y - r];
      }
    }
  }
  free(t);
}
void path_to_selection(Doc *d, const Path *p, int op, double feather, int antialias)
{
  Poly pl;
  u8 *m;
  long k, n = (long)d->w * d->h;
  if (!(m = (u8 *)calloc((size_t)n, 1))) return;
  poly_init(&pl);
  if (p && path_flatten(p, 0.2, &pl)) poly_raster(&pl, p->evenodd, doc_box(d), m, d->w);
  poly_free(&pl);
  if (!antialias) for (k = 0; k < n; k++) m[k] = m[k] >= 128 ? 255 : 0;
  if (feather > 0) blur(m, d->w, d->h, (int)(feather + 1) / 2);
  sel_apply_mask(d, m, op, "Make Selection");
}
void sel_from_text(Doc *d, const TextParams *tp, int op)
{
  u8 *m = (u8 *)calloc((size_t)d->w * d->h, 1);
  if (!m) return;
  text_coverage(tp, d->w, d->h, m);
  sel_apply_mask(d, m, op, "Type Mask");
}

/* ------------------------------------------------------------------ tracing */
/* the outline's edges between pixel corners, the inside on their right: per corner, the directions
   leaving it (1 right, 2 down, 4 left, 8 up) */
enum { DR = 1, DD = 2, DL = 4, DU = 8 };
static const int ddx[4] = { 1, 0, -1, 0 }, ddy[4] = { 0, 1, 0, -1 };
/* closed polygon simplified (Douglas-Peucker) within tol: keep[] set for the points kept */
static void simplify(const double *p, i32 a, i32 b, i32 n, double tol, u8 *keep, i32 *stack)
{
  i32 sp = 0;
  stack[sp++] = a; stack[sp++] = b;
  while (sp) {
    i32 e = stack[--sp], s = stack[--sp], i, best = -1;
    double bd = tol, ax = p[2 * (s % n)], ay = p[2 * (s % n) + 1], bx = p[2 * (e % n)], by = p[2 * (e % n) + 1];
    double dx = bx - ax, dy = by - ay, l = hypot(dx, dy);
    for (i = s + 1; i < e; i++) {
      double x = p[2 * (i % n)], y = p[2 * (i % n) + 1], dd;
      dd = l > 1e-9 ? fabs((x - ax) * dy - (y - ay) * dx) / l : hypot(x - ax, y - ay);
      if (dd > bd) { bd = dd; best = i; }
    }
    if (best >= 0) {
      keep[best % n] = 1;
      stack[sp++] = s; stack[sp++] = best;
      stack[sp++] = best; stack[sp++] = e;
    }
  }
}
/* one loop of points (edge midpoints) fitted as a closed subpath */
static int fit_loop(Path *path, const double *p, i32 n, double tol)
{
  u8 *keep;
  i32 *stack, *idx, m = 0, i, far = 0;
  double fd = -1;
  int ok = 0;
  if (n < 3) return 1;
  keep = (u8 *)calloc((size_t)n, 1);
  stack = (i32 *)malloc((size_t)(4 * n + 8) * sizeof(i32));
  idx = (i32 *)malloc((size_t)n * sizeof(i32));
  if (!keep || !stack || !idx) goto done;
  /* split at point 0 and the point furthest from it */
  for (i = 1; i < n; i++) { double dd = hypot(p[2 * i] - p[0], p[2 * i + 1] - p[1]); if (dd > fd) { fd = dd; far = i; } }
  keep[0] = 1; keep[far] = 1;
  simplify(p, 0, far, n, tol, keep, stack);
  simplify(p, far, n, n, tol, keep, stack);
  for (i = 0; i < n; i++) if (keep[i]) idx[m++] = i;
  if (m < 3) { ok = 1; goto done; }
  /* anchors at the points kept: smooth (handles along the line through their neighbours) unless the
     outline turns sharply there */
  {
    i32 first = path->na;
    for (i = 0; i < m; i++) {
      const double *c = p + 2 * idx[i];
      if (!(i ? path_line_to(path, c[0], c[1]) : path_move_to(path, c[0], c[1]) != 0)) goto done;
    }
    path_close(path);
    for (i = 0; i < m; i++) {
      PathAnchor *a = &path->a[first + i];
      const double *pv = p + 2 * idx[(i + m - 1) % m], *nx = p + 2 * idx[(i + 1) % m];
      double ux = a->x - pv[0], uy = a->y - pv[1], vx = nx[0] - a->x, vy = nx[1] - a->y;
      double lu = hypot(ux, uy), lv = hypot(vx, vy), cosang, tx, ty, tl;
      if (lu < 1e-9 || lv < 1e-9) continue;
      cosang = (ux * vx + uy * vy) / (lu * lv);
      if (cosang < 0.35) continue;                       /* (a turn of more than about 70 degrees: a corner) */
      tx = nx[0] - pv[0]; ty = nx[1] - pv[1]; tl = hypot(tx, ty);
      if (tl < 1e-9) continue;
      tx /= tl; ty /= tl;
      a->ix = a->x - tx * lu / 3; a->iy = a->y - ty * lu / 3;
      a->ox = a->x + tx * lv / 3; a->oy = a->y + ty * lv / 3;
      a->type = PA_SMOOTH;
    }
  }
  ok = 1;
done:
  free(keep); free(stack); free(idx);
  return ok;
}
Path *path_from_selection(Doc *d, double tol)
{
  i32 w = d->w, h = d->h, W = w + 1, x, y;
  u8 *out, *in;
  Path *path;
  double *pts = NULL;
  i32 cap = 0;
  int ok = 1;
  Box b;
  if (!d->sel) return NULL;
  if (tol <= 0) tol = 2;
  if (tol < 0.5) tol = 0.5;
  if (tol > 10) tol = 10;
  b = d->selbox;
  if (!(out = (u8 *)calloc((size_t)W * (h + 1), 1))) return NULL;
  if (!(path = path_new())) { free(out); return NULL; }
#define IN(px, py) ((px) >= 0 && (py) >= 0 && (px) < w && (py) < h && d->sel[(long)(py) * w + (px)] >= 128)
  for (y = b.y0; y < b.y1; y++)
    for (x = b.x0; x < b.x1; x++) {
      if (!IN(x, y)) continue;
      if (!IN(x - 1, y)) out[(long)(y + 1) * W + x] |= DU;
      if (!IN(x, y - 1)) out[(long)y * W + x] |= DR;
      if (!IN(x + 1, y)) out[(long)y * W + x + 1] |= DD;
      if (!IN(x, y + 1)) out[(long)(y + 1) * W + x + 1] |= DL;
    }
#undef IN
  in = out;
  for (y = b.y0; ok && y <= b.y1; y++)
    for (x = b.x0; ok && x <= b.x1; x++) {
      while (ok && in[(long)y * W + x]) {
        /* a loop from here, turning right where there is a choice */
        i32 cx = x, cy = y, n = 0;
        int dir = 0, k;
        for (k = 0; k < 4; k++) if (in[(long)y * W + x] & (1 << k)) { dir = k; break; }
        do {
          i32 nx2, ny2;
          in[(long)cy * W + cx] &= (u8)~(1 << dir);
          nx2 = cx + ddx[dir]; ny2 = cy + ddy[dir];
          if (n + 1 >= cap) {
            i32 nc = cap ? cap * 2 : 1024;
            double *np = (double *)realloc(pts, (size_t)nc * 2 * sizeof(double));
            if (!np) { ok = 0; break; }
            pts = np; cap = nc;
          }
          pts[2 * n] = (cx + nx2) / 2.0; pts[2 * n + 1] = (cy + ny2) / 2.0;   /* the edge's middle */
          n++;
          cx = nx2; cy = ny2;
          {
            int o = in[(long)cy * W + cx], t, nd = -1;
            static const int pref[3] = { 1, 0, 3 };           /* right, straight on, left */
            for (t = 0; t < 3; t++) if (o & (1 << ((dir + pref[t]) & 3))) { nd = (dir + pref[t]) & 3; break; }
            if (nd < 0) break;
            dir = nd;
          }
        } while (1);
        if (ok && !fit_loop(path, pts, n, tol)) ok = 0;
      }
    }
  free(pts); free(out);
  if (!ok || !path->na) { path_free(path); return NULL; }
  return path;
}

/* ------------------------------------------------------------------ filling and stroking */
/* coverage cov (box b) laid on the active layer in color, opacity 0-100, blend mode, inside the selection */
static Box lay_on(Doc *d, const u8 *cov, Box b, u32 color, int opacity, int blend, const char *name)
{
  Layer *l;
  i32 x, y;
  u8 src[4];
  Box changed = box_make(0, 0, 0, 0);
  if (!layer_paintable(d, d->active) || box_empty(b)) return changed;
  l = d->lay[d->active];
  src[0] = (u8)(color >> 16); src[1] = (u8)(color >> 8); src[2] = (u8)color; src[3] = 255;
  if (opacity < 0) opacity = 0;
  if (opacity > 100) opacity = 100;
  doc_begin(d, name);
  doc_note_layer(d, d->active, b);
  for (y = b.y0; y < b.y1; y++)
    for (x = b.x0; x < b.x1; x++) {
      int c = cov[(long)(y - b.y0) * (b.x1 - b.x0) + (x - b.x0)], op;
      u8 *p;
      if (!c) continue;
      op = c * sel_at(d, x, y) / 255 * opacity / 100;
      if (!op) continue;
      p = l->px + ((long)y * d->w + x) * 4;
      if (l->alpha_lock) { u8 a = p[3]; blend_pixel_at(p, src, op, blend, x, y); p[3] = a; }
      else blend_pixel_at(p, src, op, blend, x, y);
      changed = box_union(changed, box_make(x, y, x + 1, y + 1));
    }
  doc_end(d);
  return changed;
}
static Box fill_poly_on(Doc *d, Poly *pl, int evenodd, u32 color, int opacity, int blend, int aa, const char *name)
{
  Box b = poly_box(pl), r = box_make(0, 0, 0, 0);
  u8 *cov;
  long k, n;
  b = box_clip(box_make(b.x0 - 1, b.y0 - 1, b.x1 + 1, b.y1 + 1), doc_box(d));
  if (box_empty(b)) return r;
  n = (long)(b.x1 - b.x0) * (b.y1 - b.y0);
  if (!(cov = (u8 *)malloc((size_t)n))) return r;
  if (poly_raster(pl, evenodd, b, cov, b.x1 - b.x0)) {
    if (!aa) for (k = 0; k < n; k++) cov[k] = cov[k] >= 128 ? 255 : 0;
    r = lay_on(d, cov, b, color, opacity, blend, name);
  }
  free(cov);
  return r;
}
Box path_fill(Doc *d, const Path *p, u32 color, int opacity, int blend, int antialias)
{
  Poly pl;
  Box r = box_make(0, 0, 0, 0);
  poly_init(&pl);
  if (p && path_flatten(p, 0.2, &pl)) r = fill_poly_on(d, &pl, p->evenodd, color, opacity, blend, antialias, "Fill Path");
  poly_free(&pl);
  return r;
}
Box path_stroke_simple(Doc *d, const Path *p, double width, u32 color, int opacity, int blend)
{
  Poly pl, st;
  StrokeStyle ss;
  Box r = box_make(0, 0, 0, 0);
  memset(&ss, 0, sizeof ss);
  ss.width = width > 0 ? width : 1; ss.cap = CAP_ROUND; ss.join = JOIN_ROUND;
  poly_init(&pl); poly_init(&st);
  if (p && path_flatten(p, 0.2, &pl) && poly_stroke(&pl, &ss, &st)) r = fill_poly_on(d, &st, 0, color, opacity, blend, 1, "Stroke Path");
  poly_free(&pl); poly_free(&st);
  return r;
}
Box path_stroke(Doc *d, const Path *p, const Brush *b, u32 color, int simulate)
{
  Poly pl;
  Box all = box_make(0, 0, 0, 0), ch;
  i32 c, s = 0, i;
  if (!p) return all;
  poly_init(&pl);
  if (!path_flatten(p, 0.25, &pl)) { poly_free(&pl); return all; }
  doc_begin(d, "Stroke Path");
  for (c = 0; c < pl.nc; s = pl.end[c], c++) {
    i32 n = pl.end[c] - s, m = pl.closed[c] ? n + 1 : n;
    double total = 0, along = 0;
    Stroke *st;
    for (i = 1; i < m; i++) {
      const double *a = pl.xy + 2 * (s + (i - 1) % n), *q = pl.xy + 2 * (s + i % n);
      total += hypot(q[0] - a[0], q[1] - a[1]);
    }
    if (!(st = stroke_begin_p(d, b, color, pl.xy[2 * s], pl.xy[2 * s + 1], simulate ? 0.0 : 1.0, &ch))) break;
    all = box_union(all, ch);
    for (i = 1; i < m; i++) {
      const double *a = pl.xy + 2 * (s + (i - 1) % n), *q = pl.xy + 2 * (s + i % n);
      double pr = 1;
      along += hypot(q[0] - a[0], q[1] - a[1]);
      if (simulate && total > 0) pr = sin(3.14159265358979 * along / total);
      if (pr < 0) pr = 0;
      stroke_to_p(st, q[0], q[1], pr, &ch);
      all = box_union(all, ch);
    }
    stroke_end(st);
  }
  doc_end(d);
  poly_free(&pl);
  return all;
}
