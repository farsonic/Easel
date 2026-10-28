/*
 * matte.c - Defringe and the matte removals (see matte.h).
 *
 * Defringe works out each pixel's distance (8-connected steps) to the nearest transparent one; the
 * pixels within width of it are the fringe, the visible pixels further in its sources. Colour then
 * spreads from the sources into the fringe a ring at a time, each fringe pixel taking the average of
 * its neighbours already coloured, so it ends up with the colour of the nearest solid pixels.
 */
#include <stdlib.h>
#include <string.h>
#include "matte.h"

static Layer *active_px(Doc *d)
{
  Layer *l;
  if (d->active < 0 || d->active >= d->nlay) return NULL;
  l = d->lay[d->active];
  return !l->px || l->kind != LK_PIXEL || l->locked || l->lock_all ? NULL : l;
}
/* new colour c mixed into p by selection amount s */
static void put_rgb(u8 *p, const int *c, int s)
{
  int k;
  for (k = 0; k < 3; k++) {
    int v = c[k] < 0 ? 0 : c[k] > 255 ? 255 : c[k];
    p[k] = (u8)((p[k] * (255 - s) + v * s + 127) / 255);
  }
}
Box layer_defringe(Doc *d, int width)
{
  Layer *l = active_px(d);
  i32 w = d->w, h = d->h, x, y;
  long n = (long)w * h, k, npend = 0, i;
  u16 *dist;
  u8 *state;                          /* 0 none, 1 fringe waiting, 2 coloured (a source or done), 3 coloured this ring */
  u8 *col;
  long *pend;
  Box b = box_make(w, h, 0, 0);
  int ring, maxring;
  if (!l || width < 1) return box_make(0, 0, 0, 0);
  if (width > 200) width = 200;
  dist = (u16 *)malloc((size_t)n * sizeof(u16));
  state = (u8 *)calloc((size_t)n, 1);
  col = (u8 *)malloc((size_t)n * 3);
  pend = (long *)malloc((size_t)n * sizeof(long));
  if (!dist || !state || !col || !pend) { free(dist); free(state); free(col); free(pend); return box_make(0, 0, 0, 0); }
  /* chessboard distance to transparency, two passes (past the canvas counts as solid) */
  for (k = 0; k < n; k++) dist[k] = l->px[k * 4 + 3] ? 60000 : 0;
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      long o = (long)y * w + x;
      u16 m = dist[o];
      if (!m) continue;
      if (x > 0 && dist[o - 1] + 1 < m) m = (u16)(dist[o - 1] + 1);
      if (y > 0) {
        if (dist[o - w] + 1 < m) m = (u16)(dist[o - w] + 1);
        if (x > 0 && dist[o - w - 1] + 1 < m) m = (u16)(dist[o - w - 1] + 1);
        if (x < w - 1 && dist[o - w + 1] + 1 < m) m = (u16)(dist[o - w + 1] + 1);
      }
      dist[o] = m;
    }
  for (y = h - 1; y >= 0; y--)
    for (x = w - 1; x >= 0; x--) {
      long o = (long)y * w + x;
      u16 m = dist[o];
      if (!m) continue;
      if (x < w - 1 && dist[o + 1] + 1 < m) m = (u16)(dist[o + 1] + 1);
      if (y < h - 1) {
        if (dist[o + w] + 1 < m) m = (u16)(dist[o + w] + 1);
        if (x < w - 1 && dist[o + w + 1] + 1 < m) m = (u16)(dist[o + w + 1] + 1);
        if (x > 0 && dist[o + w - 1] + 1 < m) m = (u16)(dist[o + w - 1] + 1);
      }
      dist[o] = m;
    }
  for (k = 0; k < n; k++) {
    if (!l->px[k * 4 + 3]) continue;
    if (dist[k] > width) { state[k] = 2; memcpy(col + k * 3, l->px + k * 4, 3); }
    else if (sel_at(d, (i32)(k % w), (i32)(k / w))) { state[k] = 1; pend[npend++] = k; }
  }
  /* rings of colour spreading in; enough of them to cross the fringe twice */
  maxring = 2 * width + 2;
  for (ring = 0; ring < maxring && npend > 0; ring++) {
    long kept = 0, done = 0;
    for (i = 0; i < npend; i++) {
      long o = pend[i];
      int acc[3] = { 0, 0, 0 }, cnt = 0, dx, dy;
      x = (i32)(o % w); y = (i32)(o / w);
      for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++) {
          long q;
          if ((!dx && !dy) || x + dx < 0 || y + dy < 0 || x + dx >= w || y + dy >= h) continue;
          q = o + (long)dy * w + dx;
          if (state[q] != 2) continue;
          acc[0] += col[q * 3]; acc[1] += col[q * 3 + 1]; acc[2] += col[q * 3 + 2]; cnt++;
        }
      if (!cnt) continue;
      col[o * 3] = (u8)((acc[0] + cnt / 2) / cnt); col[o * 3 + 1] = (u8)((acc[1] + cnt / 2) / cnt); col[o * 3 + 2] = (u8)((acc[2] + cnt / 2) / cnt);
      state[o] = 3;
    }
    /* this ring's pixels become sources for the next; the rest wait */
    for (i = 0; i < npend; i++) {
      long o = pend[i];
      if (state[o] == 3) { state[o] = 2; done++; } else pend[kept++] = o;
    }
    npend = kept;
    if (!done) break;
  }
  /* the fringe's new colours on the layer */
  for (k = 0; k < n; k++)
    if (l->px[k * 4 + 3] && dist[k] <= width && state[k] == 2) {
      x = (i32)(k % w); y = (i32)(k / w);
      if (x < b.x0) b.x0 = x;
      if (x >= b.x1) b.x1 = x + 1;
      if (y < b.y0) b.y0 = y;
      if (y >= b.y1) b.y1 = y + 1;
    }
  if (!box_empty(b)) {
    doc_begin(d, "Defringe");
    doc_note_layer(d, d->active, b);
    for (y = b.y0; y < b.y1; y++)
      for (x = b.x0; x < b.x1; x++) {
        long o = (long)y * w + x;
        int c[3];
        if (!l->px[o * 4 + 3] || dist[o] > width || state[o] != 2) continue;
        c[0] = col[o * 3]; c[1] = col[o * 3 + 1]; c[2] = col[o * 3 + 2];
        put_rgb(l->px + o * 4, c, sel_at(d, x, y));
      }
    doc_end(d);
  } else b = box_make(0, 0, 0, 0);
  free(dist); free(state); free(col); free(pend);
  return b;
}
static Box remove_matte(Doc *d, int white, const char *name)
{
  Layer *l = active_px(d);
  i32 x, y;
  Box b;
  if (!l) return box_make(0, 0, 0, 0);
  b = d->sel ? box_clip(d->selbox, doc_box(d)) : doc_box(d);
  doc_begin(d, name);
  doc_note_layer(d, d->active, b);
  for (y = b.y0; y < b.y1; y++)
    for (x = b.x0; x < b.x1; x++) {
      u8 *p = l->px + ((long)y * d->w + x) * 4;
      int a = p[3], s = sel_at(d, x, y), c[3], k;
      if (!a || a == 255 || !s) continue;
      for (k = 0; k < 3; k++) c[k] = white ? ((p[k] - 255 + a) * 255 + a / 2) / a : (p[k] * 255 + a / 2) / a;
      put_rgb(p, c, s);
    }
  doc_end(d);
  return b;
}
Box layer_remove_black_matte(Doc *d) { return remove_matte(d, 0, "Remove Black Matte"); }
Box layer_remove_white_matte(Doc *d) { return remove_matte(d, 1, "Remove White Matte"); }
