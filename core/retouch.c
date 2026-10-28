/*
 * retouch.c - Easel 1.0's healing, patching and red eye (see retouch.h).
 *
 * The membrane is found in two steps: push-pull (the known differences at the edge averaged down a
 * pyramid until every pixel has one, then interpolated back up into the holes) gives a smooth first
 * guess quickly, and over-relaxed Gauss-Seidel sweeps then take it to the harmonic solution. Floats
 * throughout; the work is the area times the sweeps, so brush-sized areas take a moment.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "retouch.h"

static int clamp255(double v) { return v < 0 ? 0 : v > 255 ? 255 : (int)(v + 0.5); }

/* ------------------------------------------------------------------ the membrane */
/* push-pull: f (n x 3) known where kw[k] is set, filled in elsewhere */
static int push_pull(float *f, const u8 *kw, i32 w, i32 h)
{
  float *cv, *cw;
  i32 cwid = (w + 1) / 2, chei = (h + 1) / 2, x, y;
  int c, any = 0;
  long k;
  for (k = 0; k < (long)w * h; k++) if (kw[k]) { any = 1; break; }
  if (!any) return 1;
  if (w <= 1 && h <= 1) return 1;
  cv = (float *)calloc((size_t)cwid * chei * 3, sizeof(float));
  cw = (float *)calloc((size_t)cwid * chei, sizeof(float));
  if (!cv || !cw) { free(cv); free(cw); return 0; }
  /* down: each coarse pixel the average of its known children */
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      long src = (long)y * w + x, dst = (long)(y / 2) * cwid + x / 2;
      if (!kw[src]) continue;
      for (c = 0; c < 3; c++) cv[dst * 3 + c] += f[src * 3 + c];
      cw[dst] += 1;
    }
  {
    u8 *ck = (u8 *)malloc((size_t)cwid * chei);
    int ok;
    if (!ck) { free(cv); free(cw); return 0; }
    for (k = 0; k < (long)cwid * chei; k++) {
      if (cw[k] > 0) { for (c = 0; c < 3; c++) cv[k * 3 + c] /= cw[k]; ck[k] = 1; }
      else ck[k] = 0;
    }
    ok = (cwid == w && chei == h) ? 1 : push_pull(cv, ck, cwid, chei);
    free(ck);
    if (!ok) { free(cv); free(cw); return 0; }
  }
  /* up: each unknown pixel interpolated from the coarse level around it */
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      long k2 = (long)y * w + x;
      double sx = (x + 0.5) / 2 - 0.5, sy = (y + 0.5) / 2 - 0.5, fx, fy;
      i32 ix, iy, i0, i1, j0, j1;
      if (kw[k2]) continue;
      if (sx < 0) sx = 0;
      if (sy < 0) sy = 0;
      ix = (i32)sx; iy = (i32)sy; fx = sx - ix; fy = sy - iy;
      i0 = ix; i1 = ix + 1 < cwid ? ix + 1 : ix; j0 = iy; j1 = iy + 1 < chei ? iy + 1 : iy;
      for (c = 0; c < 3; c++) {
        double a = cv[((long)j0 * cwid + i0) * 3 + c] * (1 - fx) + cv[((long)j0 * cwid + i1) * 3 + c] * fx;
        double b = cv[((long)j1 * cwid + i0) * 3 + c] * (1 - fx) + cv[((long)j1 * cwid + i1) * 3 + c] * fx;
        f[k2 * 3 + c] = (float)(a * (1 - fy) + b * fy);
      }
    }
  free(cv); free(cw);
  return 1;
}
int heal_blend(u8 *out, const u8 *base, const u8 *src, const u8 *mask, i32 bw, i32 bh)
{
  long n = (long)bw * bh, k;
  float *f;
  u8 *known;
  int c, it, iters, anyknown = 0;
  i32 x, y;
  if (bw < 1 || bh < 1) return 1;
  f = (float *)malloc((size_t)n * 3 * sizeof(float));
  known = (u8 *)malloc((size_t)n);
  if (!f || !known) { free(f); free(known); return 0; }
  for (k = 0; k < n; k++) {
    known[k] = mask[k] == 0;
    anyknown |= known[k];
    for (c = 0; c < 3; c++) f[k * 3 + c] = known[k] ? (float)(base[k * 4 + c] - src[k * 4 + c]) : 0;
  }
  if (!anyknown) {
    /* (all of it healed: its own border is the edge) */
    for (y = 0; y < bh; y++) for (x = 0; x < bw; x++) if (x == 0 || y == 0 || x == bw - 1 || y == bh - 1) {
      k = (long)y * bw + x; known[k] = 1;
      for (c = 0; c < 3; c++) f[k * 3 + c] = (float)(base[k * 4 + c] - src[k * 4 + c]);
    }
  }
  if (!push_pull(f, known, bw, bh)) { free(f); free(known); return 0; }
  /* relaxation towards the harmonic membrane */
  iters = (bw > bh ? bw : bh) + 20;
  if (iters > 160) iters = 160;
  for (it = 0; it < iters; it++)
    for (y = 0; y < bh; y++)
      for (x = 0; x < bw; x++) {
        long kk = (long)y * bw + x, l = x > 0 ? kk - 1 : kk, r = x < bw - 1 ? kk + 1 : kk, u = y > 0 ? kk - bw : kk, dn = y < bh - 1 ? kk + bw : kk;
        if (known[kk]) continue;
        for (c = 0; c < 3; c++) {
          float avg = (f[l * 3 + c] + f[r * 3 + c] + f[u * 3 + c] + f[dn * 3 + c]) * 0.25f;
          f[kk * 3 + c] += 1.7f * (avg - f[kk * 3 + c]);
        }
      }
  for (k = 0; k < n; k++) {
    int m = mask[k];
    if (!m) { if (out != base) memcpy(out + k * 4, base + k * 4, 4); continue; }
    for (c = 0; c < 3; c++) {
      double hv = src[k * 4 + c] + f[k * 3 + c];
      out[k * 4 + c] = (u8)clamp255(base[k * 4 + c] + (hv - base[k * 4 + c]) * m / 255.0);
    }
    out[k * 4 + 3] = base[k * 4 + 3];
  }
  free(f); free(known);
  return 1;
}

/* ------------------------------------------------------------------ Proximity Match */
int spot_heal_fill(const u8 *px, i32 w, i32 h, Box b, const u8 *mask, u8 *out)
{
  i32 bw = b.x1 - b.x0, bh = b.y1 - b.y0, x, y, mx0 = bw, my0 = bh, mx1 = -1, my1 = -1, sz;
  long k, nring = 0, step;
  u8 *src;
  double best = -1;
  i32 bdx = 0, bdy = 0;
  int ring, dir, ok;
  static const double mult[4] = { 1.0, 1.5, 2.2, 3.2 };
  for (y = 0; y < bh; y++) memcpy(out + (long)y * bw * 4, px + ((long)(y + b.y0) * w + b.x0) * 4, (size_t)bw * 4);
  for (y = 0; y < bh; y++) for (x = 0; x < bw; x++) if (mask[(long)y * bw + x]) {
    if (x < mx0) mx0 = x;
    if (x > mx1) mx1 = x;
    if (y < my0) my0 = y;
    if (y > my1) my1 = y;
  }
  if (mx1 < 0) return 1;
  sz = (mx1 - mx0 > my1 - my0 ? mx1 - mx0 : my1 - my0) + 1;
  for (k = 0; k < (long)bw * bh; k++) if (!mask[k]) nring++;
  step = nring > 3000 ? nring / 3000 : 1;
  /* the candidates: sixteen directions at four distances; each must take its patch from outside
     the area and inside the canvas, and is scored by how well the surroundings match */
  for (ring = 0; ring < 4; ring++)
    for (dir = 0; dir < 16; dir++) {
      double an = dir * 3.14159265358979 / 8, dist = mult[ring] * sz + 2, cost = 0;
      i32 dx = (i32)floor(dist * cos(an) + 0.5), dy = (i32)floor(dist * sin(an) + 0.5);
      long cnt = 0, seen = 0;
      ok = 1;
      for (y = 0; y < bh && ok; y++)
        for (x = 0; x < bw; x++) {
          i32 sx = x + b.x0 + dx, sy = y + b.y0 + dy;
          long kk = (long)y * bw + x;
          if (mask[kk]) {
            i32 lx = x + dx, ly = y + dy;
            if (sx < 0 || sy < 0 || sx >= w || sy >= h) { ok = 0; break; }
            if (lx >= 0 && ly >= 0 && lx < bw && ly < bh && mask[(long)ly * bw + lx]) { ok = 0; break; }
            continue;
          }
          if (seen++ % step) continue;
          if (sx < 0 || sy < 0 || sx >= w || sy >= h) continue;
          {
            const u8 *p = px + ((long)(y + b.y0) * w + x + b.x0) * 4, *q = px + ((long)sy * w + sx) * 4;
            int c;
            for (c = 0; c < 3; c++) { double dd = p[c] - q[c]; cost += dd * dd; }
            cnt++;
          }
        }
      if (!ok || cnt == 0) continue;
      cost = cost / cnt * (1 + 0.05 * ring);      /* (nearer patches a little favoured) */
      if (best < 0 || cost < best) { best = cost; bdx = dx; bdy = dy; }
    }
  if (!(src = (u8 *)malloc((size_t)bw * bh * 4))) return 0;
  if (best >= 0) {
    for (y = 0; y < bh; y++)
      for (x = 0; x < bw; x++) {
        i32 sx = x + b.x0 + bdx, sy = y + b.y0 + bdy;
        if (sx < 0) sx = 0;
        if (sy < 0) sy = 0;
        if (sx >= w) sx = w - 1;
        if (sy >= h) sy = h - 1;
        memcpy(src + ((long)y * bw + x) * 4, px + ((long)sy * w + sx) * 4, 4);
      }
  } else memset(src, 0, (size_t)bw * bh * 4);     /* nothing fits: a smooth fill from the edge */
  ok = heal_blend(out, out, src, mask, bw, bh);
  free(src);
  return ok;
}

/* ------------------------------------------------------------------ on a document */
static Layer *paintable(Doc *d)
{
  Layer *l;
  if (d->active < 0 || d->active >= d->nlay) return NULL;
  l = d->lay[d->active];
  if (!l->px || l->locked || l->lock_all || !l->visible || l->kind != LK_PIXEL) return NULL;
  return l;
}
Box patch_heal(Doc *d, i32 dx, i32 dy)
{
  Layer *l = paintable(d);
  Box b, none = box_make(0, 0, 0, 0);
  i32 bw, bh, x, y;
  u8 *base, *src, *mask;
  if (!l || !d->sel || (!dx && !dy)) return none;
  b = box_clip(box_make(d->selbox.x0 - 2, d->selbox.y0 - 2, d->selbox.x1 + 2, d->selbox.y1 + 2), doc_box(d));
  if (box_empty(b)) return none;
  bw = b.x1 - b.x0; bh = b.y1 - b.y0;
  base = (u8 *)calloc((size_t)bw * bh, 4); src = (u8 *)calloc((size_t)bw * bh, 4); mask = (u8 *)calloc((size_t)bw * bh, 1);
  if (!base || !src || !mask) { free(base); free(src); free(mask); return none; }
  for (y = 0; y < bh; y++)
    for (x = 0; x < bw; x++) {
      i32 sx = x + b.x0 + dx, sy = y + b.y0 + dy;
      long k = (long)y * bw + x;
      sx = sx < 0 ? 0 : sx >= d->w ? d->w - 1 : sx; sy = sy < 0 ? 0 : sy >= d->h ? d->h - 1 : sy;
      memcpy(base + k * 4, l->px + ((long)(y + b.y0) * d->w + x + b.x0) * 4, 4);
      memcpy(src + k * 4, l->px + ((long)sy * d->w + sx) * 4, 4);
      mask[k] = d->sel[(long)(y + b.y0) * d->w + x + b.x0];
    }
  if (!heal_blend(base, base, src, mask, bw, bh)) { free(base); free(src); free(mask); return none; }
  doc_begin(d, "Patch");
  doc_note_layer(d, d->active, b);
  for (y = 0; y < bh; y++) memcpy(l->px + ((long)(y + b.y0) * d->w + b.x0) * 4, base + (long)y * bw * 4, (size_t)bw * 4);
  doc_end(d);
  free(base); free(src); free(mask);
  return b;
}

static double redness(const u8 *p) { int m = p[1] > p[2] ? p[1] : p[2]; return p[0] > 40 && p[0] > m ? (p[0] - m) / (double)p[0] : 0; }
int redeye_pixels(u8 *px, i32 w, i32 h, Box b, int pupil, int darken, const u8 *sel)
{
  i32 bw, bh, x, y, sx = -1, sy = -1;
  double t, best = 0, cx, cy, maxd, f;
  u8 *in;
  i32 *stk;
  long top = 0, n;
  b = box_clip(b, box_make(0, 0, w, h));
  if (box_empty(b)) return 0;
  bw = b.x1 - b.x0; bh = b.y1 - b.y0; n = (long)bw * bh;
  if (pupil < 1) pupil = 1;
  if (pupil > 100) pupil = 100;
  if (darken < 1) darken = 1;
  if (darken > 100) darken = 100;
  t = 0.5 - pupil / 100.0 * 0.35;
  /* the reddest spot, the middle of the box favoured */
  cx = (b.x0 + b.x1) / 2.0; cy = (b.y0 + b.y1) / 2.0; maxd = sqrt((double)bw * bw + (double)bh * bh) / 2 + 1;
  for (y = b.y0; y < b.y1; y++)
    for (x = b.x0; x < b.x1; x++) {
      double r = redness(px + ((long)y * w + x) * 4), dd = sqrt((x + 0.5 - cx) * (x + 0.5 - cx) + (y + 0.5 - cy) * (y + 0.5 - cy)), sc;
      if (r < t) continue;
      sc = r * (1 - 0.5 * dd / maxd);
      if (sc > best) { best = sc; sx = x; sy = y; }
    }
  if (sx < 0) return 0;
  in = (u8 *)calloc((size_t)n, 1);
  stk = (i32 *)malloc((size_t)n * sizeof(i32));
  if (!in || !stk) { free(in); free(stk); return 0; }
  /* the pupil: what is joined to it and red enough (softly at the edge) */
  in[(long)(sy - b.y0) * bw + sx - b.x0] = 1; stk[top++] = (i32)((long)(sy - b.y0) * bw + sx - b.x0);
  while (top > 0) {
    long k = stk[--top];
    i32 kx = (i32)(k % bw), ky = (i32)(k / bw);
    int dx, dy;
    for (dy = -1; dy <= 1; dy++) for (dx = -1; dx <= 1; dx++) {
      i32 nx = kx + dx, ny = ky + dy;
      long nk;
      if (nx < 0 || ny < 0 || nx >= bw || ny >= bh) continue;
      nk = (long)ny * bw + nx;
      if (in[nk]) continue;
      if (redness(px + ((long)(ny + b.y0) * w + nx + b.x0) * 4) < t * 0.8) continue;
      in[nk] = 1; stk[top++] = (i32)nk;
    }
  }
  f = 1 - darken / 100.0 * 0.75;
  for (y = 0; y < bh; y++)
    for (x = 0; x < bw; x++) {
      u8 *p;
      double wgt, nr, ng, nb;
      if (!in[(long)y * bw + x]) continue;
      p = px + ((long)(y + b.y0) * w + x + b.x0) * 4;
      wgt = (redness(p) - t * 0.8) / (t * 0.2);
      if (wgt > 1) wgt = 1;
      if (wgt < 0) wgt = 0;
      if (sel) wgt *= sel[(long)(y + b.y0) * w + x + b.x0] / 255.0;
      /* red taken down to the other two, then all darkened */
      nr = (p[1] + p[2]) / 2.0 * f; ng = p[1] * f; nb = p[2] * f;
      p[0] = (u8)clamp255(p[0] + (nr - p[0]) * wgt);
      p[1] = (u8)clamp255(p[1] + (ng - p[1]) * wgt);
      p[2] = (u8)clamp255(p[2] + (nb - p[2]) * wgt);
    }
  free(in); free(stk);
  return 1;
}
Box redeye_fix(Doc *d, Box b, int pupil, int darken)
{
  Layer *l = paintable(d);
  Box none = box_make(0, 0, 0, 0);
  int found;
  if (!l) return none;
  b = box_clip(b, doc_box(d));
  if (box_empty(b)) return none;
  doc_begin(d, "Red Eye");
  doc_note_layer(d, d->active, b);
  found = redeye_pixels(l->px, d->w, d->h, b, pupil, darken, d->sel);
  doc_end(d);
  if (!found) { doc_discard_last(d); return none; }
  return b;
}
