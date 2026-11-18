/*
 * cafill.c - Easel 1.2's content-aware fill (cafill.h): PatchMatch over a pyramid.
 *
 * At each level every pixel whose patch touches the hole (the targets) has an offset to a source
 * patch (one wholly outside the hole and in the allowed area). PatchMatch improves the offsets by
 * trying the neighbours' (propagation, alternating scan order) and random ones in shrinking windows;
 * then each hole pixel takes the mean of what the patches over it say (voting). The coarsest level
 * starts from the hole filled inwards from its edge; each finer level starts from the coarser one's
 * result and offsets doubled.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cafill.h"
#include "brushx.h"
#include "retouch.h"
#include "paint.h"

typedef struct {
  i32 w, h;
  u8 *img;                            /* RGBA */
  u8 *hole;                           /* 1 in the hole */
  u8 *ok;                             /* 1: a valid source centre */
} Level;

static u32 rnd_state;
static u32 rnd(void) { rnd_state = rnd_state * 1664525u + 1013904223u; return rnd_state >> 8; }

static void level_free(Level *l) { free(l->img); free(l->hole); free(l->ok); memset(l, 0, sizeof *l); }
/* the valid source centres: the whole patch inside, nothing in it bad (hole or not allowed) */
static long find_ok(Level *l, const u8 *bad, int r, int relax)
{
  i32 w = l->w, h = l->h, x, y;
  long *in, n = 0;
  if (!(l->ok = (u8 *)calloc((size_t)w * h, 1))) return -1;
  if (relax) {
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) if (!bad[(long)y * w + x]) { l->ok[(long)y * w + x] = 1; n++; }
    return n;
  }
  if (!(in = (long *)calloc((size_t)(w + 1) * (h + 1), sizeof(long)))) return -1;
  for (y = 0; y < h; y++) for (x = 0; x < w; x++)
    in[(long)(y + 1) * (w + 1) + x + 1] = bad[(long)y * w + x] + in[(long)y * (w + 1) + x + 1] + in[(long)(y + 1) * (w + 1) + x] - in[(long)y * (w + 1) + x];
  for (y = r; y < h - r; y++) for (x = r; x < w - r; x++) {
    long s = in[(long)(y + r + 1) * (w + 1) + x + r + 1] - in[(long)(y - r) * (w + 1) + x + r + 1] - in[(long)(y + r + 1) * (w + 1) + x - r] + in[(long)(y - r) * (w + 1) + x - r];
    if (!s) { l->ok[(long)y * w + x] = 1; n++; }
  }
  free(in);
  return n;
}
/* patch distance between target p and source q (sum of squared differences), stopping past best */
static long pdist(const Level *l, i32 px, i32 py, i32 qx, i32 qy, int r, long best)
{
  long d = 0;
  int dy, dx;
  for (dy = -r; dy <= r; dy++) {
    i32 ay = py + dy, by = qy + dy;
    const u8 *ra, *rb;
    if (ay < 0 || ay >= l->h || by < 0 || by >= l->h) continue;
    ra = l->img + (long)ay * l->w * 4; rb = l->img + (long)by * l->w * 4;
    for (dx = -r; dx <= r; dx++) {
      i32 ax = px + dx, bx = qx + dx;
      const u8 *a, *b;
      int e0, e1, e2;
      if (ax < 0 || ax >= l->w || bx < 0 || bx >= l->w) continue;
      a = ra + ax * 4; b = rb + bx * 4;
      e0 = a[0] - b[0]; e1 = a[1] - b[1]; e2 = a[2] - b[2];
      d += e0 * e0 + e1 * e1 + e2 * e2;
    }
    if (d >= best) return d;
  }
  return d;
}
/* the hole filled inwards from its edge: each ring the mean of the known neighbours */
static void onion(Level *l)
{
  i32 w = l->w, h = l->h, x, y;
  u8 *known = (u8 *)malloc((size_t)w * h);
  int changed = 1, guard = 0;
  long k;
  if (!known) return;
  for (k = 0; k < (long)w * h; k++) known[k] = !l->hole[k];
  while (changed && guard++ < 4096) {
    changed = 0;
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
      long s[4] = { 0, 0, 0, 0 };
      int n = 0, dx, dy, c;
      if (known[(long)y * w + x]) continue;
      for (dy = -1; dy <= 1; dy++) for (dx = -1; dx <= 1; dx++) {
        i32 xx = x + dx, yy = y + dy;
        if (xx < 0 || yy < 0 || xx >= w || yy >= h || known[(long)yy * w + xx] != 1) continue;
        for (c = 0; c < 4; c++) s[c] += l->img[((long)yy * w + xx) * 4 + c];
        n++;
      }
      if (!n) continue;
      for (c = 0; c < 4; c++) l->img[((long)y * w + x) * 4 + c] = (u8)(s[c] / n);
      known[(long)y * w + x] = 2;
      changed = 1;
    }
    for (k = 0; k < (long)w * h; k++) if (known[k] == 2) known[k] = 1;
  }
  free(known);
}
/* PatchMatch and voting at one level; nnf (2 i32 a pixel, the source centre for each target) given
   (from the coarser level) or made. 0 if aborted or out of memory */
static int solve(Level *l, i32 *nnf, int r, int rounds, const i32 *okl, long nok, int first)
{
  i32 w = l->w, h = l->h, x, y;
  long k, n = (long)w * h, *acc = NULL;
  u8 *tgt;
  long *dist;
  int round, it, c;
  i32 hx0 = w, hy0 = h, hx1 = -1, hy1 = -1;
  if (!(tgt = (u8 *)calloc((size_t)n, 1)) || !(dist = (long *)malloc((size_t)n * sizeof(long))) || !(acc = (long *)malloc((size_t)n * 5 * sizeof(long)))) {
    free(tgt); free(acc); return 0;
  }
  /* the targets: within r of the hole */
  for (y = 0; y < h; y++) for (x = 0; x < w; x++) if (l->hole[(long)y * w + x]) {
    i32 a, b;
    for (b = y - r < 0 ? 0 : y - r; b <= y + r && b < h; b++) for (a = x - r < 0 ? 0 : x - r; a <= x + r && a < w; a++) tgt[(long)b * w + a] = 1;
    if (x < hx0) hx0 = x;
    if (x > hx1) hx1 = x;
    if (y < hy0) hy0 = y;
    if (y > hy1) hy1 = y;
  }
  if (hx1 < 0) { free(tgt); free(dist); free(acc); return 1; }
  hx0 = hx0 - r < 0 ? 0 : hx0 - r; hy0 = hy0 - r < 0 ? 0 : hy0 - r; hx1 = hx1 + r >= w ? w - 1 : hx1 + r; hy1 = hy1 + r >= h ? h - 1 : hy1 + r;
  for (k = 0; k < n; k++) if (tgt[k]) {
    i32 qx = nnf[k * 2], qy = nnf[k * 2 + 1];
    if (first || qx < 0 || qy < 0 || qx >= w || qy >= h || !l->ok[(long)qy * w + qx]) {
      long q = okl[(rnd() % (u32)nok) ];
      qx = (i32)(q % w); qy = (i32)(q / w);
      nnf[k * 2] = qx; nnf[k * 2 + 1] = qy;
    }
    dist[k] = pdist(l, (i32)(k % w), (i32)(k / w), qx, qy, r, 0x7FFFFFFFL);
  }
  for (round = 0; round < rounds; round++) {
    for (it = 0; it < 2; it++) {
      int rev = (round * 2 + it) & 1;
      i32 rows = hy1 - hy0 + 1, row;
      for (row = 0; row < rows; row++) {
        i32 xs, xe, st;
        y = rev ? hy1 - row : hy0 + row;
        if (progress((long)round * 2 * rows + (long)it * rows + row, (long)rounds * 2 * rows)) { free(tgt); free(dist); free(acc); return 0; }
        xs = rev ? hx1 : hx0; xe = rev ? hx0 - 1 : hx1 + 1; st = rev ? -1 : 1;
        for (x = xs; x != xe; x += st) {
          long p = (long)y * w + x, d;
          i32 bx, by, rad;
          if (!tgt[p]) continue;
          bx = nnf[p * 2]; by = nnf[p * 2 + 1];
          /* propagation: the neighbour before's source, moved one */
          {
            i32 nx = x - st, ny = y - (rev ? -1 : 1);
            if (nx >= 0 && nx < w && tgt[(long)y * w + nx]) {
              i32 cx = nnf[((long)y * w + nx) * 2] + st, cy = nnf[((long)y * w + nx) * 2 + 1];
              if (cx >= 0 && cx < w && l->ok[(long)cy * w + cx] && (d = pdist(l, x, y, cx, cy, r, dist[p])) < dist[p]) { dist[p] = d; bx = cx; by = cy; }
            }
            if (ny >= 0 && ny < h && tgt[(long)ny * w + x]) {
              i32 cx = nnf[((long)ny * w + x) * 2], cy = nnf[((long)ny * w + x) * 2 + 1] + (rev ? -1 : 1);
              if (cy >= 0 && cy < h && l->ok[(long)cy * w + cx] && (d = pdist(l, x, y, cx, cy, r, dist[p])) < dist[p]) { dist[p] = d; bx = cx; by = cy; }
            }
          }
          /* random search round the best so far, the window halving */
          for (rad = w > h ? w : h; rad >= 1; rad /= 2) {
            i32 cx = bx + (i32)(rnd() % (u32)(2 * rad + 1)) - rad, cy = by + (i32)(rnd() % (u32)(2 * rad + 1)) - rad;
            if (cx < 0 || cy < 0 || cx >= w || cy >= h || !l->ok[(long)cy * w + cx]) continue;
            if ((d = pdist(l, x, y, cx, cy, r, dist[p])) < dist[p]) { dist[p] = d; bx = cx; by = cy; }
          }
          nnf[p * 2] = bx; nnf[p * 2 + 1] = by;
        }
      }
    }
    /* voting: each hole pixel the mean of what the targets' patches over it hold, weighted by fit */
    memset(acc, 0, (size_t)n * 5 * sizeof(long));
    for (y = hy0; y <= hy1; y++) for (x = hx0; x <= hx1; x++) {
      long p = (long)y * w + x, wt;
      i32 qx, qy, dx, dy;
      if (!tgt[p]) continue;
      qx = nnf[p * 2]; qy = nnf[p * 2 + 1];
      wt = 1 + 4096L * 49 / (49 + dist[p] / ((2 * r + 1) * (2 * r + 1) * 3 + 1));
      for (dy = -r; dy <= r; dy++) for (dx = -r; dx <= r; dx++) {
        i32 tx = x + dx, ty = y + dy, sx = qx + dx, sy = qy + dy;
        long t, s;
        if (tx < 0 || ty < 0 || tx >= w || ty >= h || sx < 0 || sy < 0 || sx >= w || sy >= h) continue;
        t = (long)ty * w + tx;
        if (!l->hole[t]) continue;
        s = ((long)sy * w + sx) * 4;
        for (c = 0; c < 4; c++) acc[t * 5 + c] += l->img[s + c] * wt;
        acc[t * 5 + 4] += wt;
      }
    }
    for (k = 0; k < n; k++) if (l->hole[k] && acc[k * 5 + 4]) for (c = 0; c < 4; c++) l->img[k * 4 + c] = (u8)(acc[k * 5 + c] / acc[k * 5 + 4]);
    /* (the distances against the new estimate) */
    for (k = 0; k < n; k++) if (tgt[k]) dist[k] = pdist(l, (i32)(k % w), (i32)(k / w), nnf[k * 2], nnf[k * 2 + 1], r, 0x7FFFFFFFL);
  }
  free(tgt); free(dist); free(acc);
  return 1;
}
int ca_fill_rgba(u8 *img, i32 w, i32 h, const u8 *hole, const u8 *allow, const CaOpts *o)
{
  Level lv[8];
  int nl = 0, i, r, rounds, ok = 1;
  i32 *nnf = NULL, *okl = NULL;
  long k, holes = 0;
  long saved;
  if (w < 3 || h < 3) return 0;
  r = (o && o->patch >= 5 ? (o->patch > 15 ? 15 : o->patch) : 7) / 2;
  rounds = o && o->rounds > 0 ? (o->rounds > 10 ? 10 : o->rounds) : 3;
  rnd_state = o ? o->seed * 2654435761u + 12345 : 12345;
  for (k = 0; k < (long)w * h; k++) holes += hole[k] > 127;
  if (!holes) return 1;
  memset(lv, 0, sizeof lv);
  /* level 0: the picture */
  lv[0].w = w; lv[0].h = h;
  lv[0].img = (u8 *)malloc((size_t)w * h * 4); lv[0].hole = (u8 *)malloc((size_t)w * h);
  if (!lv[0].img || !lv[0].hole) { level_free(&lv[0]); return 0; }
  memcpy(lv[0].img, img, (size_t)w * h * 4);
  for (k = 0; k < (long)w * h; k++) lv[0].hole[k] = hole[k] > 127;
  nl = 1;
  /* the pyramid: halved while both sides stay over four patches */
  while (nl < 8 && lv[nl - 1].w / 2 > 4 * (2 * r + 1) && lv[nl - 1].h / 2 > 4 * (2 * r + 1)) {
    Level *a = &lv[nl - 1], *b = &lv[nl];
    i32 x, y;
    b->w = a->w / 2; b->h = a->h / 2;
    b->img = (u8 *)malloc((size_t)b->w * b->h * 4); b->hole = (u8 *)malloc((size_t)b->w * b->h);
    if (!b->img || !b->hole) { level_free(b); break; }
    for (y = 0; y < b->h; y++) for (x = 0; x < b->w; x++) {
      long s[4] = { 0, 0, 0, 0 }, n = 0;
      int c, any = 0, dx, dy;
      for (dy = 0; dy < 2; dy++) for (dx = 0; dx < 2; dx++) {
        long q = (long)(y * 2 + dy) * a->w + x * 2 + dx;
        if (a->hole[q]) { any = 1; continue; }
        for (c = 0; c < 4; c++) s[c] += a->img[q * 4 + c];
        n++;
      }
      b->hole[(long)y * b->w + x] = (u8)any;
      for (c = 0; c < 4; c++) b->img[((long)y * b->w + x) * 4 + c] = (u8)(n ? s[c] / n : 0);
    }
    nl++;
  }
  saved = progress_push(0, 1000);
  for (i = nl - 1; i >= 0 && ok; i--) {
    Level *l = &lv[i];
    u8 *bad;
    long nok, j;
    i32 x, y;
    /* the allowed area at this level (sampled) */
    if (!(bad = (u8 *)malloc((size_t)l->w * l->h))) { ok = 0; break; }
    for (y = 0; y < l->h; y++) for (x = 0; x < l->w; x++) {
      long q = (long)y * l->w + x;
      int s = i, al = 1;
      if (allow) { i32 fx = x << s, fy = y << s; if (fx >= w) fx = w - 1; if (fy >= h) fy = h - 1; al = allow[(long)fy * w + fx] > 127; }
      bad[q] = (u8)(l->hole[q] || !al);
    }
    nok = find_ok(l, bad, r, 0);
    if (nok == 0) { free(l->ok); l->ok = NULL; nok = find_ok(l, bad, r, 1); }
    free(bad);
    if (nok <= 0) { ok = 0; break; }
    free(okl);
    if (!(okl = (i32 *)malloc((size_t)nok * sizeof(i32)))) { ok = 0; break; }
    for (j = 0, k = 0; k < (long)l->w * l->h; k++) if (l->ok[k]) okl[j++] = (i32)k;
    if (i == nl - 1) {
      onion(l);
      if (!(nnf = (i32 *)malloc((size_t)l->w * l->h * 2 * sizeof(i32)))) { ok = 0; break; }
      for (k = 0; k < (long)l->w * l->h * 2; k++) nnf[k] = -1;
    } else {
      /* up from the coarser level: its result in the hole, its offsets doubled */
      Level *c = &lv[i + 1];
      i32 *nn = (i32 *)malloc((size_t)l->w * l->h * 2 * sizeof(i32));
      if (!nn) { ok = 0; break; }
      for (y = 0; y < l->h; y++) for (x = 0; x < l->w; x++) {
        i32 cx = x / 2 < c->w ? x / 2 : c->w - 1, cy = y / 2 < c->h ? y / 2 : c->h - 1;
        long q = (long)y * l->w + x, cq = (long)cy * c->w + cx;
        if (l->hole[q]) memcpy(l->img + q * 4, c->img + cq * 4, 4);
        nn[q * 2] = nnf[cq * 2] * 2 + (x - cx * 2); nn[q * 2 + 1] = nnf[cq * 2 + 1] * 2 + (y - cy * 2);
      }
      free(nnf); nnf = nn;
    }
    progress_pop(saved);
    saved = progress_push((long)(nl - 1 - i) * 1000 / nl, (long)(nl - i) * 1000 / nl);
    ok = solve(l, nnf, r, i == 0 ? rounds + 1 : rounds, okl, nok, i == nl - 1);
  }
  progress_pop(saved);
  if (ok) for (k = 0; k < (long)w * h; k++) if (hole[k] > 127) memcpy(img + k * 4, lv[0].img + k * 4, 4);
  for (i = 0; i < nl; i++) level_free(&lv[i]);
  free(nnf); free(okl);
  return ok;
}

/* the box round the hole, with the margin */
static int hole_box(const u8 *hole, i32 w, i32 h, const CaOpts *o, Box *hb, Box *wb)
{
  i32 x, y, x0 = w, y0 = h, x1 = -1, y1 = -1, m;
  for (y = 0; y < h; y++) for (x = 0; x < w; x++) if (hole[(long)y * w + x] > 127) {
    if (x < x0) x0 = x;
    if (x > x1) x1 = x;
    if (y < y0) y0 = y;
    if (y > y1) y1 = y;
  }
  if (x1 < 0) return 0;
  *hb = box_make(x0, y0, x1 + 1, y1 + 1);
  m = o && o->margin > 0 ? o->margin : 2 * ((x1 - x0 > y1 - y0 ? x1 - x0 : y1 - y0) + 1);
  if (m < 32) m = 32;
  *wb = box_clip(box_make(x0 - m, y0 - m, x1 + 1 + m, y1 + 1 + m), box_make(0, 0, w, h));
  return 1;
}
int ca_fill_canvas(u8 *px, i32 w, i32 h, const u8 *hole, const u8 *allow, const CaOpts *o, Box *changed)
{
  Box hb, b;
  i32 bw, bh, y;
  u8 *img, *ho = NULL, *al = NULL;
  int ok;
  if (changed) *changed = box_make(0, 0, 0, 0);
  if (!hole_box(hole, w, h, o, &hb, &b)) return 1;
  bw = b.x1 - b.x0; bh = b.y1 - b.y0;
  img = (u8 *)malloc((size_t)bw * bh * 4); ho = (u8 *)malloc((size_t)bw * bh);
  if (allow) al = (u8 *)malloc((size_t)bw * bh);
  if (!img || !ho || (allow && !al)) { free(img); free(ho); free(al); return 0; }
  for (y = 0; y < bh; y++) {
    memcpy(img + (size_t)y * bw * 4, px + ((long)(y + b.y0) * w + b.x0) * 4, (size_t)bw * 4);
    memcpy(ho + (size_t)y * bw, hole + (long)(y + b.y0) * w + b.x0, (size_t)bw);
    if (al) memcpy(al + (size_t)y * bw, allow + (long)(y + b.y0) * w + b.x0, (size_t)bw);
  }
  ok = ca_fill_rgba(img, bw, bh, ho, al, o);
  if (ok) {
    for (y = 0; y < bh; y++) memcpy(px + ((long)(y + b.y0) * w + b.x0) * 4, img + (size_t)y * bw * 4, (size_t)bw * 4);
    if (changed) *changed = hb;
  }
  free(img); free(ho); free(al);
  return ok;
}

/* ------------------------------------------------------------------ the Spot Healing Brush's hook */
static int spot_content(const u8 *px, i32 w, i32 h, Box b, const u8 *mask, u8 *out)
{
  i32 bw = b.x1 - b.x0, bh = b.y1 - b.y0, m = (bw > bh ? bw : bh), y, x;
  Box wb = box_clip(box_make(b.x0 - m, b.y0 - m, b.x1 + m, b.y1 + m), box_make(0, 0, w, h));
  i32 ww = wb.x1 - wb.x0, wh = wb.y1 - wb.y0;
  u8 *img = (u8 *)malloc((size_t)ww * wh * 4), *ho = (u8 *)calloc((size_t)ww * wh, 1);
  CaOpts o;
  int ok;
  if (!img || !ho) { free(img); free(ho); return 0; }
  for (y = 0; y < wh; y++) memcpy(img + (size_t)y * ww * 4, px + ((long)(y + wb.y0) * w + wb.x0) * 4, (size_t)ww * 4);
  for (y = 0; y < bh; y++) for (x = 0; x < bw; x++) if (mask[(long)y * bw + x] > 20) ho[(long)(y + b.y0 - wb.y0) * ww + x + b.x0 - wb.x0] = 255;
  memset(&o, 0, sizeof o);
  o.patch = 7; o.rounds = 3; o.seed = (u32)(b.x0 * 31 + b.y0);
  ok = ca_fill_rgba(img, ww, wh, ho, NULL, &o);
  if (ok)
    for (y = 0; y < bh; y++) for (x = 0; x < bw; x++) {
      const u8 *s = img + ((long)(y + b.y0 - wb.y0) * ww + x + b.x0 - wb.x0) * 4, *p0 = px + ((long)(y + b.y0) * w + x + b.x0) * 4;
      u8 *q = out + ((long)y * bw + x) * 4;
      int a = mask[(long)y * bw + x], c;
      for (c = 0; c < 4; c++) q[c] = (u8)(p0[c] + ((s[c] - p0[c]) * a) / 255);
    }
  free(img); free(ho);
  return ok;
}
void cafill_init(void) { spot_content_hook = spot_content; }

/* ------------------------------------------------------------------ on the document */
static Layer *pix_layer(Doc *d)
{
  Layer *l;
  if (d->active < 0 || d->active >= d->nlay) return NULL;
  l = d->lay[d->active];
  if (!l->px || l->kind != LK_PIXEL || l->locked || l->lock_all || !l->visible) return NULL;
  return l;
}
Box doc_ca_fill(Doc *d, const CaOpts *o)
{
  Layer *l = pix_layer(d);
  Box ch = box_make(0, 0, 0, 0), hb, wb;
  u8 *px;
  if (!l || !d->sel) return ch;
  if (!hole_box(d->sel, d->w, d->h, o, &hb, &wb)) return ch;
  if (!(px = (u8 *)malloc((size_t)d->w * d->h * 4))) return ch;
  memcpy(px, l->px, (size_t)d->w * d->h * 4);
  if (!ca_fill_canvas(px, d->w, d->h, d->sel, NULL, o, &ch)) { free(px); return box_make(0, 0, 0, 0); }
  doc_begin(d, "Content-Aware Fill");
  doc_note_layer(d, d->active, ch);
  {
    /* blended in by how much is selected */
    i32 x, y;
    for (y = ch.y0; y < ch.y1; y++) for (x = ch.x0; x < ch.x1; x++) {
      long k = (long)y * d->w + x;
      int a = d->sel[k], c;
      u8 *p = l->px + k * 4;
      for (c = 0; c < 4; c++) p[c] = (u8)(p[c] + ((px[k * 4 + c] - p[c]) * a) / 255);
    }
  }
  doc_end(d);
  free(px);
  return ch;
}
/* the selection's box worked out again after it changed */
static void sel_recalc(Doc *d)
{
  i32 x, y, x0 = d->w, y0 = d->h, x1 = 0, y1 = 0;
  for (y = 0; y < d->h; y++) for (x = 0; x < d->w; x++) if (d->sel[(long)y * d->w + x]) {
    if (x < x0) x0 = x;
    if (x >= x1) x1 = x + 1;
    if (y < y0) y0 = y;
    y1 = y + 1;
  }
  d->selbox = x1 > x0 ? box_make(x0, y0, x1, y1) : box_make(0, 0, 0, 0);
}
/* the selection's bounds */
static int sel_box(Doc *d, Box *b) { return d->sel && sel_bounds(d, b) && !box_empty(*b); }
Box patch_dest(Doc *d, i32 dx, i32 dy, int transparent)
{
  Layer *l = pix_layer(d);
  Box sb, db;
  i32 bw, bh, x, y;
  u8 *base, *src, *mask, *out;
  if (!l || !sel_box(d, &sb) || (!dx && !dy)) return box_make(0, 0, 0, 0);
  sb = box_clip(box_make(sb.x0 - 2, sb.y0 - 2, sb.x1 + 2, sb.y1 + 2), doc_box(d));
  db = box_clip(box_make(sb.x0 + dx, sb.y0 + dy, sb.x1 + dx, sb.y1 + dy), doc_box(d));
  if (box_empty(db)) return db;
  bw = db.x1 - db.x0; bh = db.y1 - db.y0;
  base = (u8 *)malloc((size_t)bw * bh * 4); src = (u8 *)malloc((size_t)bw * bh * 4); out = (u8 *)malloc((size_t)bw * bh * 4); mask = (u8 *)malloc((size_t)bw * bh);
  if (!base || !src || !out || !mask) { free(base); free(src); free(out); free(mask); return box_make(0, 0, 0, 0); }
  for (y = 0; y < bh; y++) for (x = 0; x < bw; x++) {
    i32 cx = x + db.x0, cy = y + db.y0, sx = cx - dx, sy = cy - dy;
    long k = (long)y * bw + x;
    memcpy(base + k * 4, l->px + ((long)cy * d->w + cx) * 4, 4);
    memcpy(src + k * 4, l->px + ((long)sy * d->w + sx) * 4, 4);
    mask[k] = d->sel[(long)sy * d->w + sx];
  }
  if (transparent) {
    /* only the texture: the source's detail (less its local mean) on the destination */
    u8 *bl = (u8 *)malloc((size_t)bw * bh * 4);
    if (bl) {
      long k;
      int c;
      memcpy(bl, src, (size_t)bw * bh * 4);
      for (k = 0; k < (long)bw * bh; k++) {
        long s[3] = { 0, 0, 0 }, n = 0;
        i32 kx = (i32)(k % bw), ky = (i32)(k / bw), a, b;
        for (b = ky - 3; b <= ky + 3; b++) for (a = kx - 3; a <= kx + 3; a++) if (a >= 0 && b >= 0 && a < bw && b < bh) { for (c = 0; c < 3; c++) s[c] += src[((long)b * bw + a) * 4 + c]; n++; }
        for (c = 0; c < 3; c++) { int v = base[k * 4 + c] + src[k * 4 + c] - (int)(s[c] / n); bl[k * 4 + c] = (u8)(v < 0 ? 0 : v > 255 ? 255 : v); }
      }
      memcpy(src, bl, (size_t)bw * bh * 4);
      free(bl);
    }
  }
  if (!heal_blend(out, base, src, mask, bw, bh)) { free(base); free(src); free(out); free(mask); return box_make(0, 0, 0, 0); }
  doc_begin(d, "Patch");
  doc_note_layer(d, d->active, db);
  doc_note_sel(d);
  for (y = 0; y < bh; y++) memcpy(l->px + ((long)(y + db.y0) * d->w + db.x0) * 4, out + (size_t)y * bw * 4, (size_t)bw * 4);
  {
    /* the selection goes with the patch */
    u8 *ns = (u8 *)calloc((size_t)d->w * d->h, 1);
    if (ns) {
      for (y = 0; y < d->h; y++) for (x = 0; x < d->w; x++) {
        i32 sx = x - dx, sy = y - dy;
        if (sx >= 0 && sy >= 0 && sx < d->w && sy < d->h) ns[(long)y * d->w + x] = d->sel[(long)sy * d->w + sx];
      }
      memcpy(d->sel, ns, (size_t)d->w * d->h);
      free(ns);
      sel_recalc(d);
    }
  }
  doc_end(d);
  free(base); free(src); free(out); free(mask);
  return db;
}
Box patch_content(Doc *d, i32 dx, i32 dy, int structure, int color)
{
  Layer *l = pix_layer(d);
  Box sb, ch = box_make(0, 0, 0, 0), ab;
  u8 *allow, *px;
  CaOpts o;
  i32 x, y, m;
  if (!l || !sel_box(d, &sb)) return ch;
  /* it takes from round the place dragged to */
  m = (sb.x1 - sb.x0 > sb.y1 - sb.y0 ? sb.x1 - sb.x0 : sb.y1 - sb.y0) / 2 + 8;
  ab = box_clip(box_make(sb.x0 + dx - m, sb.y0 + dy - m, sb.x1 + dx + m, sb.y1 + dy + m), doc_box(d));
  if (!(allow = (u8 *)calloc((size_t)d->w * d->h, 1))) return ch;
  for (y = ab.y0; y < ab.y1; y++) for (x = ab.x0; x < ab.x1; x++) allow[(long)y * d->w + x] = 255;
  if (!(px = (u8 *)malloc((size_t)d->w * d->h * 4))) { free(allow); return ch; }
  memcpy(px, l->px, (size_t)d->w * d->h * 4);
  memset(&o, 0, sizeof o);
  if (structure < 1) structure = 4;
  if (structure > 7) structure = 7;
  o.patch = 3 + structure * 2;                             /* 1: 5 pixels ... 7: 17 (kept to 15) */
  o.margin = (sb.x1 - sb.x0) + abs(dx) + abs(dy) + m;
  o.seed = (u32)(dx * 7 + dy);
  if (!ca_fill_canvas(px, d->w, d->h, d->sel, allow, &o, &ch)) { free(px); free(allow); return box_make(0, 0, 0, 0); }
  free(allow);
  if (color > 0) {
    /* the colour matched to the edge: the fill healed in */
    i32 bw = ch.x1 - ch.x0 + 4, bh = ch.y1 - ch.y0 + 4;
    Box hb = box_clip(box_make(ch.x0 - 2, ch.y0 - 2, ch.x1 + 2, ch.y1 + 2), doc_box(d));
    u8 *base, *src, *mask, *out;
    bw = hb.x1 - hb.x0; bh = hb.y1 - hb.y0;
    base = (u8 *)malloc((size_t)bw * bh * 4); src = (u8 *)malloc((size_t)bw * bh * 4); out = (u8 *)malloc((size_t)bw * bh * 4); mask = (u8 *)malloc((size_t)bw * bh);
    if (base && src && out && mask) {
      for (y = 0; y < bh; y++) for (x = 0; x < bw; x++) {
        long k = (long)y * bw + x, c = (long)(y + hb.y0) * d->w + x + hb.x0;
        memcpy(base + k * 4, l->px + c * 4, 4); memcpy(src + k * 4, px + c * 4, 4);
        mask[k] = d->sel[c];
      }
      if (heal_blend(out, base, src, mask, bw, bh))
        for (y = 0; y < bh; y++) for (x = 0; x < bw; x++) {
          long k = (long)y * bw + x, c = (long)(y + hb.y0) * d->w + x + hb.x0;
          int q;
          for (q = 0; q < 3; q++) px[c * 4 + q] = (u8)(px[c * 4 + q] + ((out[k * 4 + q] - px[c * 4 + q]) * color) / 10);
        }
    }
    free(base); free(src); free(out); free(mask);
  }
  doc_begin(d, "Patch");
  doc_note_layer(d, d->active, ch);
  for (y = ch.y0; y < ch.y1; y++) for (x = ch.x0; x < ch.x1; x++) {
    long k = (long)y * d->w + x;
    int a = d->sel[k], c;
    for (c = 0; c < 4; c++) l->px[k * 4 + c] = (u8)(l->px[k * 4 + c] + ((px[k * 4 + c] - l->px[k * 4 + c]) * a) / 255);
  }
  doc_end(d);
  free(px);
  return ch;
}
Box doc_ca_move(Doc *d, i32 dx, i32 dy, int extend, int structure, int color)
{
  Layer *l = pix_layer(d);
  Box sb, ch = box_make(0, 0, 0, 0), db, fb;
  u8 *px, *hole, *moved;
  CaOpts o;
  i32 x, y;
  long n;
  if (!l || !sel_box(d, &sb) || (!dx && !dy)) return ch;
  n = (long)d->w * d->h;
  px = (u8 *)malloc((size_t)n * 4); hole = (u8 *)calloc((size_t)n, 1); moved = (u8 *)calloc((size_t)n, 1);
  if (!px || !hole || !moved) { free(px); free(hole); free(moved); return ch; }
  memcpy(px, l->px, (size_t)n * 4);
  for (y = 0; y < d->h; y++) for (x = 0; x < d->w; x++) {
    i32 tx = x + dx, ty = y + dy;
    long k = (long)y * d->w + x;
    if (d->sel[k] && tx >= 0 && ty >= 0 && tx < d->w && ty < d->h) moved[(long)ty * d->w + tx] = d->sel[k];
  }
  /* the hole: what was selected and isn't covered by what moves (Move); none for Extend */
  if (!extend) for (n = 0; n < (long)d->w * d->h; n++) hole[n] = (u8)(d->sel[n] > 127 && moved[n] < 128 ? 255 : 0);
  memset(&o, 0, sizeof o);
  if (structure < 1) structure = 4;
  if (structure > 7) structure = 7;
  o.patch = 3 + structure * 2; o.seed = (u32)(dx * 13 + dy * 7);
  if (!extend) {
    /* (the moved pixels are not taken from: what was there is) */
    u8 *allow = (u8 *)malloc((size_t)d->w * d->h);
    int ok;
    if (!allow) { free(px); free(hole); free(moved); return ch; }
    for (n = 0; n < (long)d->w * d->h; n++) allow[n] = (u8)(moved[n] > 127 ? 0 : 255);
    ok = ca_fill_canvas(px, d->w, d->h, hole, allow, &o, &fb);
    free(allow);
    if (!ok) { free(px); free(hole); free(moved); return ch; }
    ch = fb;
  }
  /* what moves, laid at its new place, its edge healed into what is there by color (0-10) */
  db = box_clip(box_make(sb.x0 + dx - 2, sb.y0 + dy - 2, sb.x1 + dx + 2, sb.y1 + dy + 2), doc_box(d));
  if (!box_empty(db)) {
    i32 bw = db.x1 - db.x0, bh = db.y1 - db.y0;
    u8 *base = (u8 *)malloc((size_t)bw * bh * 4), *src = (u8 *)malloc((size_t)bw * bh * 4), *mask = (u8 *)malloc((size_t)bw * bh), *out = (u8 *)malloc((size_t)bw * bh * 4);
    if (base && src && mask && out) {
      for (y = 0; y < bh; y++) for (x = 0; x < bw; x++) {
        i32 cx = x + db.x0, cy = y + db.y0, sx = cx - dx, sy = cy - dy;
        long k = (long)y * bw + x;
        memcpy(base + k * 4, px + ((long)cy * d->w + cx) * 4, 4);
        if (sx >= 0 && sy >= 0 && sx < d->w && sy < d->h) memcpy(src + k * 4, l->px + ((long)sy * d->w + sx) * 4, 4);
        else memcpy(src + k * 4, base + k * 4, 4);
        mask[k] = moved[(long)cy * d->w + cx];
      }
      if (color > 0 && heal_blend(out, base, src, mask, bw, bh)) {
        long k;
        int c;
        for (k = 0; k < (long)bw * bh; k++) for (c = 0; c < 3; c++) out[k * 4 + c] = (u8)(src[k * 4 + c] + ((out[k * 4 + c] - src[k * 4 + c]) * (color > 10 ? 10 : color)) / 10);
      } else memcpy(out, src, (size_t)bw * bh * 4);
      for (y = 0; y < bh; y++) for (x = 0; x < bw; x++) {
        long k = (long)y * bw + x, c = (long)(y + db.y0) * d->w + x + db.x0;
        int a = mask[k], q;
        for (q = 0; q < 4; q++) px[c * 4 + q] = (u8)(px[c * 4 + q] + ((out[k * 4 + q] - px[c * 4 + q]) * a) / 255);
      }
      ch = box_empty(ch) ? db : box_union(ch, db);
    }
    free(base); free(src); free(mask); free(out);
  }
  doc_begin(d, "Content-Aware Move");
  doc_note_layer(d, d->active, box_union(ch, sb));
  doc_note_sel(d);
  memcpy(l->px, px, (size_t)d->w * d->h * 4);
  /* the selection is now what moved */
  for (n = 0; n < (long)d->w * d->h; n++) d->sel[n] = moved[n];
  sel_recalc(d);
  doc_end(d);
  free(px); free(hole); free(moved);
  return box_union(ch, sb);
}
