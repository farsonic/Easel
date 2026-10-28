/*
 * selmore.c - Reselect, Grow and Similar (see selmore.h).
 *
 * d->lastsel holds the remembered selection: its width and height as four bytes each (most
 * significant first), then the w x h mask, so a canvas resized since can't be misread.
 */
#include <stdlib.h>
#include <string.h>
#include "selmore.h"
#include "paint.h"

static void put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static u32 get32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

/* the selection's box worked out again; an empty mask is no selection */
static void update_box(Doc *d)
{
  i32 x, y;
  Box b = box_make(d->w, d->h, 0, 0);
  if (!d->sel) { d->selbox = doc_box(d); return; }
  for (y = 0; y < d->h; y++) {
    const u8 *row = d->sel + (long)y * d->w;
    for (x = 0; x < d->w; x++)
      if (row[x]) {
        if (x < b.x0) b.x0 = x;
        if (x >= b.x1) b.x1 = x + 1;
        if (y < b.y0) b.y0 = y;
        b.y1 = y + 1;
      }
  }
  if (box_empty(b)) { free(d->sel); d->sel = NULL; d->selbox = doc_box(d); return; }
  d->selbox = b;
}
/* mask m (taken) made the selection, as one undo step */
static void set_sel(Doc *d, u8 *m, const char *name)
{
  doc_begin(d, name);
  doc_note_sel(d);
  free(d->sel);
  d->sel = m;
  update_box(d);
  doc_end(d);
}

void sel_remember(Doc *d)
{
  size_t n;
  u8 *m;
  if (!d->sel) return;
  n = (size_t)d->w * d->h;
  if (!(m = (u8 *)malloc(n + 8))) return;
  put32(m, (u32)d->w); put32(m + 4, (u32)d->h);
  memcpy(m + 8, d->sel, n);
  free(d->lastsel);
  d->lastsel = m;
}
void sel_deselect(Doc *d)
{
  sel_remember(d);
  sel_none(d);
}
int sel_reselect(Doc *d)
{
  size_t n = (size_t)d->w * d->h;
  u8 *m;
  if (!d->lastsel) return 0;
  if (get32(d->lastsel) != (u32)d->w || get32(d->lastsel + 4) != (u32)d->h) return 0;
  if (!(m = (u8 *)malloc(n))) return 0;
  memcpy(m, d->lastsel + 8, n);
  set_sel(d, m, "Reselect");
  return 1;
}

/* the range of colours (RGBA, low and high) under the selection on the active layer, widened by
   tol; 0 if nothing is selected enough */
static int sel_range(Doc *d, int tol, int *lo, int *hi)
{
  const u8 *px;
  long k, n = (long)d->w * d->h;
  int c, any = 0;
  if (!d->sel || d->active < 0 || d->active >= d->nlay || !(px = d->lay[d->active]->px)) return 0;
  for (c = 0; c < 4; c++) { lo[c] = 255; hi[c] = 0; }
  for (k = 0; k < n; k++) {
    if (d->sel[k] < 128) continue;
    any = 1;
    for (c = 0; c < 4; c++) { int v = px[k * 4 + c]; if (v < lo[c]) lo[c] = v; if (v > hi[c]) hi[c] = v; }
  }
  if (!any) return 0;
  if (tol < 0) tol = 0;
  for (c = 0; c < 4; c++) { lo[c] -= tol; hi[c] += tol; }
  return 1;
}
static int in_range(const u8 *p, const int *lo, const int *hi)
{
  return p[0] >= lo[0] && p[0] <= hi[0] && p[1] >= lo[1] && p[1] <= hi[1] && p[2] >= lo[2] && p[2] <= hi[2] && p[3] >= lo[3] && p[3] <= hi[3];
}
int sel_similar(Doc *d, int tolerance)
{
  int lo[4], hi[4];
  long k, n = (long)d->w * d->h, added = 0;
  const u8 *px;
  u8 *m;
  if (!sel_range(d, tolerance, lo, hi)) return 0;
  px = d->lay[d->active]->px;
  if (!(m = (u8 *)malloc((size_t)n))) return 0;
  memcpy(m, d->sel, (size_t)n);
  for (k = 0; k < n; k++) if (m[k] != 255 && in_range(px + k * 4, lo, hi)) { m[k] = 255; added++; }
  if (!added) { free(m); return 0; }
  set_sel(d, m, "Similar");
  return 1;
}
int sel_grow_similar(Doc *d, int tolerance)
{
  int lo[4], hi[4];
  i32 w = d->w, h = d->h;
  long k, n = (long)w * h, top = 0, cap = 1024, added = 0;
  const u8 *px;
  u8 *m;
  long *st;
  if (!sel_range(d, tolerance, lo, hi)) return 0;
  px = d->lay[d->active]->px;
  if (!(m = (u8 *)malloc((size_t)n))) return 0;
  if (!(st = (long *)malloc((size_t)cap * sizeof(long)))) { free(m); return 0; }
  memcpy(m, d->sel, (size_t)n);
  /* a flood outward from the selected pixels (at least half selected) on the selection's edge */
  for (k = 0; k < n; k++) {
    i32 x = (i32)(k % w), y = (i32)(k / w);
    if (d->sel[k] < 128) continue;
    if ((x > 0 && d->sel[k - 1] < 128) || (x < w - 1 && d->sel[k + 1] < 128) || (y > 0 && d->sel[k - w] < 128) || (y < h - 1 && d->sel[k + w] < 128)) {
      if (top == cap) { long *ns = (long *)realloc(st, (size_t)cap * 2 * sizeof(long)); if (!ns) break; st = ns; cap *= 2; }
      st[top++] = k;
    }
  }
  while (top > 0) {
    long q = st[--top], nb[4];
    i32 x = (i32)(q % w), y = (i32)(q / w);
    int i, nn = 0;
    if (x > 0) nb[nn++] = q - 1;
    if (x < w - 1) nb[nn++] = q + 1;
    if (y > 0) nb[nn++] = q - w;
    if (y < h - 1) nb[nn++] = q + w;
    for (i = 0; i < nn; i++) {
      long r = nb[i];
      if (m[r] == 255 || d->sel[r] >= 128 || !in_range(px + r * 4, lo, hi)) continue;
      m[r] = 255; added++;
      if (top == cap) { long *ns = (long *)realloc(st, (size_t)cap * 2 * sizeof(long)); if (!ns) continue; st = ns; cap *= 2; }
      st[top++] = r;
    }
  }
  free(st);
  if (!added) { free(m); return 0; }
  set_sel(d, m, "Grow");
  return 1;
}
