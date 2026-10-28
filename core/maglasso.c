/*
 * maglasso.c - the Magnetic Lasso's live wire (see maglasso.h). Written for Easel from the method of
 * Mortensen and Barrett, "Intelligent Scissors for Image Composition" (SIGGRAPH 1995): a cost per
 * pixel from the gradient, and the cheapest path by Dijkstra's method, here kept to a band around
 * the chord so a pointer move searches a few thousand pixels, not the picture.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "maglasso.h"

struct MagLasso {
  Doc *d;
  i32 w, h;
  u8 *cost;                           /* w * h: 1 (a strong edge) to 1 + CMAX (flat) */
  int width, freq;
  double *pts; int npts, cap;         /* the committed path, x, y pairs */
  int *anch; int nanch, capanch;      /* where in pts each anchor is */
  i32 ax, ay;                         /* the last anchor's pixel */
  double *live; int nlive, caplive;   /* the live path */
};
#define CMAX 60

/* ------------------------------------------------------------------ the cost of each pixel */
static int build_cost(MagLasso *ml, int contrast, int all_layers)
{
  Doc *d = ml->d;
  long n = (long)d->w * d->h, k;
  u8 *lum = (u8 *)malloc((size_t)n), *row = NULL;
  const u8 *src = NULL;
  i32 x, y, w = d->w, h = d->h;
  int thr = contrast < 1 ? 1 : contrast > 100 ? 100 : contrast;
  if (!lum) return 0;
  thr = thr * 255 / 100;
  if (all_layers || d->active < 0 || d->active >= d->nlay || !d->lay[d->active]->px) {
    /* what shows, a row at a time (no second copy of the picture) */
    if (!(row = (u8 *)malloc((size_t)w * 4))) { free(lum); return 0; }
    for (y = 0; y < h && !progress(y, 2L * h); y++) {     /* (0.8: progress, and Abort) */
      doc_composite(d, box_make(0, y, w, y + 1), row, (long)w * 4);
      for (x = 0; x < w; x++) { const u8 *p = row + x * 4; lum[(long)y * w + x] = (u8)(((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8) * p[3] / 255); }
    }
    free(row);
  } else {
    src = d->lay[d->active]->px;
    for (k = 0; k < n; k++) { const u8 *p = src + k * 4; lum[k] = (u8)(((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8) * p[3] / 255); }
  }
  for (y = 0; y < h && !progress(h + y, 2L * h); y++)
    for (x = 0; x < w; x++) {
      /* Sobel, the edge repeated */
      i32 xm = x > 0 ? x - 1 : 0, xp = x < w - 1 ? x + 1 : x, ym = y > 0 ? y - 1 : 0, yp = y < h - 1 ? y + 1 : y;
      const u8 *r0 = lum + (long)ym * w, *r1 = lum + (long)y * w, *r2 = lum + (long)yp * w;
      int gx = (r0[xp] + 2 * r1[xp] + r2[xp]) - (r0[xm] + 2 * r1[xm] + r2[xm]);
      int gy = (r2[xm] + 2 * r2[x] + r2[xp]) - (r0[xm] + 2 * r0[x] + r0[xp]);
      /* the magnitude without a square root: the larger part plus 3/8 of the smaller (within 7%) */
      int ax = gx < 0 ? -gx : gx, ay = gy < 0 ? -gy : gy, mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
      int g = (mx + mn * 3 / 8) * 255 / 1020;
      if (g > 255) g = 255;
      if (g < thr) g = g * g / thr;                         /* weaker than Contrast: much less pull */
      ml->cost[(long)y * w + x] = (u8)(1 + (255 - g) * CMAX / 255);
    }
  free(lum);
  return !progress_aborted;
}
MagLasso *maglasso_new(Doc *d, int width, int contrast, int frequency, int all_layers)
{
  MagLasso *ml = (MagLasso *)calloc(1, sizeof *ml);
  if (!ml) return NULL;
  ml->d = d; ml->w = d->w; ml->h = d->h;
  ml->width = width < 1 ? 1 : width > 256 ? 256 : width;
  ml->freq = frequency < 0 ? 0 : frequency;
  if (ml->freq && ml->freq < 4) ml->freq = 4;
  if (!(ml->cost = (u8 *)malloc((size_t)d->w * d->h)) || !build_cost(ml, contrast, all_layers)) { maglasso_free(ml); return NULL; }
  return ml;
}
void maglasso_free(MagLasso *ml)
{
  if (!ml) return;
  free(ml->cost); free(ml->pts); free(ml->anch); free(ml->live);
  free(ml);
}

/* ------------------------------------------------------------------ helpers */
static int grow(double **p, int *cap, int need)
{
  if (need > *cap) {
    int nc = *cap ? *cap : 256;
    double *np;
    while (nc < need) nc *= 2;
    if (!(np = (double *)realloc(*p, (size_t)nc * 2 * sizeof(double)))) return 0;
    *p = np; *cap = nc;
  }
  return 1;
}
static void add_pt(MagLasso *ml, i32 x, i32 y)
{
  if (!grow(&ml->pts, &ml->cap, ml->npts + 1)) return;
  ml->pts[ml->npts * 2] = x + 0.5; ml->pts[ml->npts * 2 + 1] = y + 0.5;
  ml->npts++;
}
static void add_anchor(MagLasso *ml)
{
  if (ml->nanch == ml->capanch) {
    int nc = ml->capanch ? ml->capanch * 2 : 32;
    int *na = (int *)realloc(ml->anch, (size_t)nc * sizeof(int));
    if (!na) return;
    ml->anch = na; ml->capanch = nc;
  }
  ml->anch[ml->nanch++] = ml->npts - 1;
}
/* the pixel within width of x, y on the strongest edge (the nearest of equals) */
static void snap(MagLasso *ml, double x, double y, i32 *ox, i32 *oy)
{
  i32 cx = (i32)floor(x), cy = (i32)floor(y), i, j, r = ml->width, bx, by;
  long best = -1;
  if (cx < 0) cx = 0;
  if (cy < 0) cy = 0;
  if (cx >= ml->w) cx = ml->w - 1;
  if (cy >= ml->h) cy = ml->h - 1;
  bx = cx; by = cy;
  for (j = cy - r; j <= cy + r; j++) {
    if (j < 0 || j >= ml->h) continue;
    for (i = cx - r; i <= cx + r; i++) {
      long dd = (long)(i - cx) * (i - cx) + (long)(j - cy) * (j - cy), score;
      if (i < 0 || i >= ml->w || dd > (long)r * r) continue;
      /* cost first, then distance */
      score = (long)(CMAX + 1 - ml->cost[(long)j * ml->w + i]) * 1000000L - dd;
      if (score > best) { best = score; bx = i; by = j; }
    }
  }
  *ox = bx; *oy = by;
}

/* ------------------------------------------------------------------ the search */
typedef struct { u32 dist; i32 at; } HeapE;
static void heap_push(HeapE *hp, long *n, u32 dist, i32 at)
{
  long i = (*n)++;
  while (i > 0) {
    long p = (i - 1) / 2;
    if (hp[p].dist <= dist) break;
    hp[i] = hp[p]; i = p;
  }
  hp[i].dist = dist; hp[i].at = at;
}
static HeapE heap_pop(HeapE *hp, long *n)
{
  HeapE top = hp[0], last = hp[--(*n)];
  long i = 0;
  for (;;) {
    long c = 2 * i + 1;
    if (c >= *n) break;
    if (c + 1 < *n && hp[c + 1].dist < hp[c].dist) c++;
    if (hp[c].dist >= last.dist) break;
    hp[i] = hp[c]; i = c;
  }
  if (*n > 0) hp[i] = last;
  return top;
}
/* the cheapest path from (x0, y0) to (x1, y1) inside the band around the chord, into ml->live as
   pixel centres from start to end; 0 if out of memory */
static int find_path(MagLasso *ml, i32 x0, i32 y0, i32 x1, i32 y1)
{
  i32 band = 2 * ml->width < 8 ? 8 : 2 * ml->width, bx0, by0, bx1, by1, bw, bh, x, y;
  double cdx = x1 - x0, cdy = y1 - y0, clen2 = cdx * cdx + cdy * cdy;
  long area, k, nheap = 0, capheap;
  u32 *dist;
  i32 *prev;
  u8 *in;
  HeapE *hp;
  int ok = 1, nsteps;
  static const int ddx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, ddy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 }, step[8] = { 10, 10, 10, 10, 14, 14, 14, 14 };
  ml->nlive = 0;
  bx0 = (x0 < x1 ? x0 : x1) - band; by0 = (y0 < y1 ? y0 : y1) - band;
  bx1 = (x0 > x1 ? x0 : x1) + band + 1; by1 = (y0 > y1 ? y0 : y1) + band + 1;
  if (bx0 < 0) bx0 = 0;
  if (by0 < 0) by0 = 0;
  if (bx1 > ml->w) bx1 = ml->w;
  if (by1 > ml->h) by1 = ml->h;
  bw = bx1 - bx0; bh = by1 - by0; area = (long)bw * bh;
  dist = (u32 *)malloc((size_t)area * sizeof(u32));
  prev = (i32 *)malloc((size_t)area * sizeof(i32));
  in = (u8 *)malloc((size_t)area);
  capheap = area * 2 + 16;
  hp = (HeapE *)malloc((size_t)capheap * sizeof(HeapE));
  if (!dist || !prev || !in || !hp) { free(dist); free(prev); free(in); free(hp); return 0; }
  /* which pixels of the box are in the band (within band of the chord) */
  for (y = 0; y < bh; y++)
    for (x = 0; x < bw; x++) {
      double px = bx0 + x - x0, py = by0 + y - y0, t = clen2 > 0 ? (px * cdx + py * cdy) / clen2 : 0, ex, ey;
      if (t < 0) t = 0;
      if (t > 1) t = 1;
      ex = px - t * cdx; ey = py - t * cdy;
      k = (long)y * bw + x;
      in[k] = ex * ex + ey * ey <= (double)band * band;
      dist[k] = 0xFFFFFFFFu; prev[k] = -1;
    }
  k = (long)(y0 - by0) * bw + (x0 - bx0);
  dist[k] = 0;
  heap_push(hp, &nheap, 0, (i32)k);
  while (nheap > 0) {
    HeapE e = heap_pop(hp, &nheap);
    i32 ex, ey;
    int dir;
    if (e.dist != dist[e.at]) continue;                    /* (a stale entry) */
    ex = e.at % bw; ey = e.at / bw;
    if (ex + bx0 == x1 && ey + by0 == y1) break;
    for (dir = 0; dir < 8; dir++) {
      i32 nx = ex + ddx[dir], ny = ey + ddy[dir];
      long nk;
      u32 nd;
      if (nx < 0 || ny < 0 || nx >= bw || ny >= bh) continue;
      nk = (long)ny * bw + nx;
      if (!in[nk]) continue;
      nd = e.dist + (u32)(ml->cost[(long)(ny + by0) * ml->w + nx + bx0] * step[dir]);
      if (nd < dist[nk]) {
        dist[nk] = nd; prev[nk] = e.at;
        if (nheap == capheap) {
          HeapE *nh = (HeapE *)realloc(hp, (size_t)capheap * 2 * sizeof(HeapE));
          if (!nh) { ok = 0; nheap = 0; break; }
          hp = nh; capheap *= 2;
        }
        heap_push(hp, &nheap, nd, (i32)nk);
      }
    }
  }
  /* back from the end */
  k = (long)(y1 - by0) * bw + (x1 - bx0);
  if (ok && dist[k] != 0xFFFFFFFFu) {
    long j;
    for (nsteps = 1, j = k; prev[j] >= 0; j = prev[j]) nsteps++;
    if (grow(&ml->live, &ml->caplive, nsteps)) {
      int i = nsteps;
      for (j = k; j >= 0; j = prev[j]) {
        i--;
        ml->live[i * 2] = bx0 + (double)(j % bw) + 0.5; ml->live[i * 2 + 1] = by0 + (double)(j / bw) + 0.5;
        if (prev[j] < 0) break;
      }
      ml->nlive = nsteps;
    } else ok = 0;
  } else if (ok && grow(&ml->live, &ml->caplive, 2)) {
    /* (can't happen: the chord is always inside the band) a straight line */
    ml->live[0] = x0 + 0.5; ml->live[1] = y0 + 0.5; ml->live[2] = x1 + 0.5; ml->live[3] = y1 + 0.5; ml->nlive = 2;
  }
  free(dist); free(prev); free(in); free(hp);
  return ok;
}
/* the live path's points 1..upto appended to the committed path, the last becoming an anchor */
static void commit(MagLasso *ml, int upto)
{
  int i;
  for (i = 1; i <= upto && i < ml->nlive; i++) add_pt(ml, (i32)floor(ml->live[i * 2]), (i32)floor(ml->live[i * 2 + 1]));
  add_anchor(ml);
  ml->ax = (i32)floor(ml->pts[(ml->npts - 1) * 2]); ml->ay = (i32)floor(ml->pts[(ml->npts - 1) * 2 + 1]);
}

/* ------------------------------------------------------------------ the tool */
void maglasso_anchor(MagLasso *ml, double x, double y)
{
  i32 sx, sy;
  if (!ml) return;
  snap(ml, x, y, &sx, &sy);
  if (!ml->nanch) {
    ml->npts = 0;
    add_pt(ml, sx, sy);
    add_anchor(ml);
    ml->ax = sx; ml->ay = sy;
    ml->nlive = 0;
    return;
  }
  if (sx == ml->ax && sy == ml->ay) return;
  if (find_path(ml, ml->ax, ml->ay, sx, sy)) commit(ml, ml->nlive - 1);
  ml->nlive = 0;
}
int maglasso_track(MagLasso *ml, double x, double y, double **pts, int *n)
{
  i32 sx, sy;
  int laid = 0;
  *pts = NULL; *n = 0;
  if (!ml || !ml->nanch) return 0;
  snap(ml, x, y, &sx, &sy);
  if (!find_path(ml, ml->ax, ml->ay, sx, sy)) return 0;
  while (ml->freq > 0 && laid < 64) {
    /* the path's length (diagonal steps 1.4), and where it passes the frequency */
    double len = 0;
    int i, cut = -1;
    for (i = 1; i < ml->nlive; i++) {
      double dx = ml->live[i * 2] - ml->live[i * 2 - 2], dy = ml->live[i * 2 + 1] - ml->live[i * 2 - 1];
      len += dx && dy ? 1.41421356 : 1;
      if (cut < 0 && len >= ml->freq) cut = i;
    }
    if (len < 1.5 * ml->freq || cut < 1) break;
    commit(ml, cut);
    laid++;
    if (!find_path(ml, ml->ax, ml->ay, sx, sy)) return laid;
  }
  *pts = ml->live; *n = ml->nlive;
  return laid;
}
void maglasso_points(MagLasso *ml, double **pts, int *n) { *pts = ml ? ml->pts : NULL; *n = ml ? ml->npts : 0; }
int maglasso_anchors(const MagLasso *ml) { return ml ? ml->nanch : 0; }
int maglasso_unanchor(MagLasso *ml)
{
  if (!ml || !ml->nanch) return 0;
  ml->nanch--;
  ml->nlive = 0;
  if (!ml->nanch) { ml->npts = 0; return 0; }
  ml->npts = ml->anch[ml->nanch - 1] + 1;
  ml->ax = (i32)floor(ml->pts[(ml->npts - 1) * 2]); ml->ay = (i32)floor(ml->pts[(ml->npts - 1) * 2 + 1]);
  return 1;
}
double *maglasso_close(MagLasso *ml, int *n)
{
  double *out;
  int m;
  *n = 0;
  if (!ml || !ml->nanch) return NULL;
  /* back to the first point along edges (its last point, the first, left off) */
  if (ml->npts > 1) {
    i32 fx = (i32)floor(ml->pts[0]), fy = (i32)floor(ml->pts[1]);
    if ((fx != ml->ax || fy != ml->ay) && find_path(ml, ml->ax, ml->ay, fx, fy)) {
      int i;
      for (i = 1; i < ml->nlive - 1; i++) add_pt(ml, (i32)floor(ml->live[i * 2]), (i32)floor(ml->live[i * 2 + 1]));
    }
  }
  ml->nlive = 0;
  m = ml->npts;
  if (m < 3 || !(out = (double *)malloc((size_t)m * 2 * sizeof(double)))) return NULL;
  memcpy(out, ml->pts, (size_t)m * 2 * sizeof(double));
  *n = m;
  return out;
}
