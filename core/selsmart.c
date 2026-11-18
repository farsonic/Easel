/*
 * selsmart.c - Easel 1.2's Quick Selection and Object Selection (selsmart.h).
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "selsmart.h"
#include "paint.h"
#include "channels.h"

#define MAXK 6
typedef struct { double c[MAXK][3]; int k; } Model;

/* the pixels to work from: the active pixel layer, or the merged picture (malloc'd RGBA, or the
   layer's own pixels when *own is set) */
static const u8 *source(Doc *d, int all_layers, int *own)
{
  Layer *l = d->active >= 0 && d->active < d->nlay ? d->lay[d->active] : NULL;
  u8 *m;
  *own = 0;
  if (!all_layers && l && l->px && l->kind == LK_PIXEL) { *own = 1; return l->px; }
  if (!(m = (u8 *)malloc((size_t)d->w * d->h * 4))) return NULL;
  doc_composite(d, doc_box(d), m, (long)d->w * 4);
  return m;
}
/* k-means on a sample (3 doubles each), k clusters at most */
static void fit(Model *m, const double *s, long n, int k)
{
  int it, j, c;
  long i;
  if (n <= 0) { m->k = 0; return; }
  if (k > n) k = (int)n;
  m->k = k;
  for (j = 0; j < k; j++) for (c = 0; c < 3; c++) m->c[j][c] = s[(n * j / k) * 3 + c];
  for (it = 0; it < 6; it++) {
    double sum[MAXK][3];
    long cnt[MAXK];
    memset(sum, 0, sizeof sum); memset(cnt, 0, sizeof cnt);
    for (i = 0; i < n; i++) {
      int best = 0;
      double bd = 1e30;
      for (j = 0; j < k; j++) {
        double d0 = s[i * 3] - m->c[j][0], d1 = s[i * 3 + 1] - m->c[j][1], d2 = s[i * 3 + 2] - m->c[j][2], dd = d0 * d0 + d1 * d1 + d2 * d2;
        if (dd < bd) { bd = dd; best = j; }
      }
      for (c = 0; c < 3; c++) sum[best][c] += s[i * 3 + c];
      cnt[best]++;
    }
    for (j = 0; j < k; j++) if (cnt[j]) for (c = 0; c < 3; c++) m->c[j][c] = sum[j][c] / cnt[j];
  }
}
static double mdist(const Model *m, const u8 *p)
{
  double bd = 1e30;
  int j;
  for (j = 0; j < m->k; j++) {
    double d0 = p[0] - m->c[j][0], d1 = p[1] - m->c[j][1], d2 = p[2] - m->c[j][2], dd = d0 * d0 + d1 * d1 + d2 * d2;
    if (dd < bd) bd = dd;
  }
  return bd;
}
static int edge(const u8 *a, const u8 *b) { return abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2]); }
/* a sample buffer that grows */
typedef struct { double *s; long n, cap; } Samp;
static void sadd(Samp *s, const u8 *p)
{
  if (s->n >= s->cap) {
    long c = s->cap ? s->cap * 2 : 1024;
    double *ns;
    if (c > 200000) { if (s->n % 3) return; c = s->cap; }   /* (enough: every third one past that) */
    if (c == s->cap) return;
    if (!(ns = (double *)realloc(s->s, (size_t)c * 3 * sizeof(double)))) return;
    s->s = ns; s->cap = c;
  }
  s->s[s->n * 3] = p[0]; s->s[s->n * 3 + 1] = p[1]; s->s[s->n * 3 + 2] = p[2];
  s->n++;
}
/* the mask m smoothed a little (Auto-Enhance): a 3 x 3 box twice, kept to the grown area's edge */
static void soften(u8 *m, i32 w, i32 h)
{
  u8 *t = (u8 *)malloc((size_t)w * h);
  int pass;
  i32 x, y;
  if (!t) return;
  for (pass = 0; pass < 2; pass++) {
    memcpy(t, m, (size_t)w * h);
    for (y = 1; y < h - 1; y++) for (x = 1; x < w - 1; x++) {
      int s = 0, dx, dy;
      for (dy = -1; dy <= 1; dy++) for (dx = -1; dx <= 1; dx++) s += t[(long)(y + dy) * w + x + dx];
      m[(long)y * w + x] = (u8)(s / 9);
    }
  }
  free(t);
}

void quick_select(Doc *d, const double *xy, int n, int r, int op, int all_layers, int enhance)
{
  int own, k;
  const u8 *px = source(d, all_layers, &own);
  i32 w = d->w, h = d->h, x, y, bx0, by0, bx1, by1, R;
  u8 *m, *seen;
  Samp fg, bg;
  Model mf, mb;
  i32 *q;
  long qh = 0, qt = 0;
  if (!px || n < 1) { if (!own) free((void *)px); return; }
  if (r < 1) r = 1;
  R = r * 4 + 8;
  memset(&fg, 0, sizeof fg); memset(&bg, 0, sizeof bg);
  m = (u8 *)calloc((size_t)w * h, 1); seen = (u8 *)calloc((size_t)w * h, 1);
  q = (i32 *)malloc((size_t)w * h * sizeof(i32));
  if (!m || !seen || !q) goto done;
  bx0 = w; by0 = h; bx1 = -1; by1 = -1;
  /* the brush's pixels (the object), and the ring round it (the background, where not selected) */
  for (k = 0; k < n; k++) {
    double cx = xy[k * 2], cy = xy[k * 2 + 1];
    i32 x0 = (i32)floor(cx - 3.5 * r), x1 = (i32)ceil(cx + 3.5 * r), y0 = (i32)floor(cy - 3.5 * r), y1 = (i32)ceil(cy + 3.5 * r);
    for (y = y0 < 0 ? 0 : y0; y <= y1 && y < h; y++) for (x = x0 < 0 ? 0 : x0; x <= x1 && x < w; x++) {
      double dd = sqrt((x + 0.5 - cx) * (x + 0.5 - cx) + (y + 0.5 - cy) * (y + 0.5 - cy));
      long i = (long)y * w + x;
      if (dd <= r) {
        if (!seen[i]) { seen[i] = 1; sadd(&fg, px + i * 4); q[qt++] = (i32)i; m[i] = 255; }
      } else if (dd >= 2.5 * r && dd <= 3.5 * r && (op == SEL_SUBTRACT ? sel_at(d, x, y) > 127 : sel_at(d, x, y) < 128 || !d->sel)) {
        if (op != SEL_SUBTRACT && d->sel && sel_at(d, x, y) > 127) continue;
        if (seen[i] != 2) sadd(&bg, px + i * 4);
      }
    }
    if (x0 - R < bx0) bx0 = x0 - R;
    if (y0 - R < by0) by0 = y0 - R;
    if (x1 + R > bx1) bx1 = x1 + R;
    if (y1 + R > by1) by1 = y1 + R;
  }
  if (!fg.n) goto done;
  fit(&mf, fg.s, fg.n, 4);
  fit(&mb, bg.s, bg.n, 6);
  {
    /* a step of more than this between neighbours is an edge the growing stops at */
    int thr = 90;
    while (qh < qt) {
      i32 p = q[qh++], px0 = p % w, py0 = p / w, dd;
      static const int ox[4] = { 1, -1, 0, 0 }, oy[4] = { 0, 0, 1, -1 };
      for (dd = 0; dd < 4; dd++) {
        i32 nx = px0 + ox[dd], ny = py0 + oy[dd];
        long ni;
        double df, db;
        if (nx < 0 || ny < 0 || nx >= w || ny >= h || nx < bx0 || ny < by0 || nx > bx1 || ny > by1) continue;
        ni = (long)ny * w + nx;
        if (seen[ni]) continue;
        seen[ni] = 1;
        if (edge(px + (long)p * 4, px + ni * 4) > thr) continue;
        df = mdist(&mf, px + ni * 4); db = mb.k ? mdist(&mb, px + ni * 4) : 1e30;
        if (df > db || df > 120.0 * 120.0) continue;
        m[ni] = 255;
        q[qt++] = (i32)ni;
      }
    }
  }
  if (enhance) soften(m, w, h);
  sel_apply_mask(d, m, op, "Quick Selection");
  m = NULL;
done:
  free(m); free(seen); free(q); free(fg.s); free(bg.s);
  if (!own) free((void *)px);
}

int object_select(Doc *d, Box b, int op, int all_layers, int hard_edge)
{
  int own, round, found = 0;
  const u8 *px;
  i32 w = d->w, h = d->h, x, y, bw, bh, ix0, iy0, ix1, iy1, band;
  u8 *cls, *m, *t;
  Samp fg, bg;
  Model mf, mb;
  b = box_clip(b, doc_box(d));
  if (b.x1 - b.x0 < 3 || b.y1 - b.y0 < 3) return 0;
  if (!(px = source(d, all_layers, &own))) return 0;
  bw = b.x1 - b.x0; bh = b.y1 - b.y0;
  memset(&fg, 0, sizeof fg); memset(&bg, 0, sizeof bg);
  cls = (u8 *)calloc((size_t)bw * bh, 1); t = (u8 *)malloc((size_t)bw * bh);
  m = (u8 *)calloc((size_t)w * h, 1);
  if (!cls || !t || !m) goto done;
  /* the background: a band round the box (and the box's own edge); the object: the box's middle */
  band = (bw < bh ? bw : bh) / 8 + 3;
  for (y = b.y0 - band; y < b.y1 + band; y++) for (x = b.x0 - band; x < b.x1 + band; x++) {
    const u8 *p;
    if (x < 0 || y < 0 || x >= w || y >= h) continue;
    p = px + ((long)y * w + x) * 4;
    if (x < b.x0 + 2 || y < b.y0 + 2 || x >= b.x1 - 2 || y >= b.y1 - 2) { if ((x + y) % 2 == 0 || bw * bh < 4000) sadd(&bg, p); }
  }
  ix0 = b.x0 + bw / 4; ix1 = b.x1 - bw / 4; iy0 = b.y0 + bh / 4; iy1 = b.y1 - bh / 4;
  for (y = iy0; y < iy1; y++) for (x = ix0; x < ix1; x++) sadd(&fg, px + ((long)y * w + x) * 4);
  if (!fg.n || !bg.n) goto done;
  fit(&mf, fg.s, fg.n, 5); fit(&mb, bg.s, bg.n, 5);
  /* the transparent pixels of a layer are background (a layer with an object on clear) */
  for (round = 0; round < 3; round++) {
    long i;
    for (y = 0; y < bh; y++) {
      if (progress((long)round * bh + y, 3L * bh)) goto done;
      for (x = 0; x < bw; x++) {
        const u8 *p = px + ((long)(y + b.y0) * w + x + b.x0) * 4;
        cls[(long)y * bw + x] = (u8)(p[3] >= 128 && mdist(&mf, p) <= mdist(&mb, p));
      }
    }
    /* smoothed: the majority of each 5 x 5 */
    memcpy(t, cls, (size_t)bw * bh);
    for (y = 0; y < bh; y++) for (x = 0; x < bw; x++) {
      int s = 0, c = 0, dx, dy;
      for (dy = -2; dy <= 2; dy++) for (dx = -2; dx <= 2; dx++) {
        i32 xx = x + dx, yy = y + dy;
        if (xx < 0 || yy < 0 || xx >= bw || yy >= bh) continue;
        s += t[(long)yy * bw + xx]; c++;
      }
      cls[(long)y * bw + x] = (u8)(s * 2 > c);
    }
    if (round == 2) break;
    /* the clusters again, from the classes */
    fg.n = 0;
    for (i = 0; i < (long)bw * bh; i++) if (cls[i]) sadd(&fg, px + ((long)(i / bw + b.y0) * w + i % bw + b.x0) * 4);
    for (i = 0; i < (long)bw * bh; i++) if (!cls[i] && !t[i] && (i % 3) == 0) sadd(&bg, px + ((long)(i / bw + b.y0) * w + i % bw + b.x0) * 4);
    if (!fg.n) break;
    fit(&mf, fg.s, fg.n, 5); fit(&mb, bg.s, bg.n, 5);
  }
  {
    /* what is joined to the middle (the class pixels nearest it, outward), holes filled */
    i32 *q = (i32 *)malloc((size_t)bw * bh * sizeof(i32));
    long qh = 0, qt = 0, i;
    i32 cx = bw / 2, cy = bh / 2, rr;
    if (!q) goto done;
    memset(t, 0, (size_t)bw * bh);
    for (rr = 0; rr < (bw > bh ? bw : bh) && !qt; rr++)
      for (y = cy - rr; y <= cy + rr && !qt; y++) for (x = cx - rr; x <= cx + rr; x++) {
        if (x < 0 || y < 0 || x >= bw || y >= bh || !cls[(long)y * bw + x]) continue;
        q[qt++] = y * bw + x; t[(long)y * bw + x] = 1;
        break;
      }
    while (qh < qt) {
      i32 p = q[qh++], pxx = p % bw, pyy = p / bw, k;
      static const int ox[4] = { 1, -1, 0, 0 }, oy[4] = { 0, 0, 1, -1 };
      for (k = 0; k < 4; k++) {
        i32 nx = pxx + ox[k], ny = pyy + oy[k];
        if (nx < 0 || ny < 0 || nx >= bw || ny >= bh || t[(long)ny * bw + nx] || !cls[(long)ny * bw + nx]) continue;
        t[(long)ny * bw + nx] = 1; q[qt++] = ny * bw + nx;
      }
    }
    /* holes: what the outside can't reach through non-object pixels */
    qh = qt = 0;
    memset(cls, 0, (size_t)bw * bh);
    for (y = 0; y < bh; y++) for (x = 0; x < bw; x++)
      if ((x == 0 || y == 0 || x == bw - 1 || y == bh - 1) && !t[(long)y * bw + x]) { cls[(long)y * bw + x] = 1; q[qt++] = y * bw + x; }
    while (qh < qt) {
      i32 p = q[qh++], pxx = p % bw, pyy = p / bw, k;
      static const int ox[4] = { 1, -1, 0, 0 }, oy[4] = { 0, 0, 1, -1 };
      for (k = 0; k < 4; k++) {
        i32 nx = pxx + ox[k], ny = pyy + oy[k];
        if (nx < 0 || ny < 0 || nx >= bw || ny >= bh || cls[(long)ny * bw + nx] || t[(long)ny * bw + nx]) continue;
        cls[(long)ny * bw + nx] = 1; q[qt++] = ny * bw + nx;
      }
    }
    free(q);
    for (i = 0; i < (long)bw * bh; i++) if (!cls[i]) { m[(long)(i / bw + b.y0) * w + i % bw + b.x0] = 255; found = 1; }
  }
  if (found && !hard_edge) {
    /* a one-pixel soft edge */
    u8 *s = (u8 *)malloc((size_t)w * h);
    if (s) {
      memcpy(s, m, (size_t)w * h);
      for (y = b.y0; y < b.y1; y++) for (x = b.x0; x < b.x1; x++) {
        int sum = 0, c = 0, dx, dy;
        for (dy = -1; dy <= 1; dy++) for (dx = -1; dx <= 1; dx++) {
          i32 xx = x + dx, yy = y + dy;
          if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
          sum += s[(long)yy * w + xx]; c++;
        }
        m[(long)y * w + x] = (u8)((sum + c / 2) / c);
      }
      free(s);
    }
  }
  if (found) { sel_apply_mask(d, m, op, "Object Selection"); m = NULL; }
done:
  free(cls); free(t); free(m); free(fg.s); free(bg.s);
  if (!own) free((void *)px);
  return found;
}
