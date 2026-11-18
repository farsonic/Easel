/*
 * timeline.c - Easel round 3: the Timeline window's frame commands (timeline.h).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "timeline.h"
#include "legacy.h"

/* frame k's block: layers lo..hi (hi the frame's top layer, a group's header) */
static int block(const Doc *d, int k, int *lo, int *hi)
{
  int f[MAXLAYERS], n = frames_list(d, f, MAXLAYERS), s;
  if (k < 0 || k >= n) return 0;
  s = f[k];
  *hi = s;
  while (s > 0 && d->lay[s - 1]->depth > 0) s--;
  *lo = s;
  return 1;
}
static int is_single(const Layer *l) { return l->kind != LK_GROUP && l->kind != LK_ADJUST; }

int tl_new_frame(Doc *d, int k)
{
  int lo, hi, n, f[MAXLAYERS];
  char name[64];
  if (!block(d, k, &lo, &hi) || lo != hi || !is_single(d->lay[hi]) || d->nlay >= MAXLAYERS) return 0;
  n = frames_list(d, f, MAXLAYERS);
  doc_begin(d, "New Frame");
  if (!doc_duplicate_layer(d, hi)) { doc_end(d); doc_discard_last(d); return 0; }
  /* the copy is just above: named as the next frame, with the same time */
  snprintf(name, sizeof name, "Frame %d", n + 1);
  {
    char timed[64];
    frame_set_time(name, frame_time(d->lay[hi]->name), timed, sizeof timed);
    doc_set_layer_props(d, hi + 1, timed, -1, -1, -1);
  }
  d->active = hi + 1;
  doc_end(d);
  return 1;
}

int tl_delete_frame(Doc *d, int k)
{
  int lo, hi, i, f[MAXLAYERS];
  if (frames_list(d, f, MAXLAYERS) < 2 || !block(d, k, &lo, &hi)) return 0;
  doc_begin(d, "Delete Frame");
  for (i = hi; i >= lo; i--) doc_delete_layer(d, i);
  if (d->active >= d->nlay) d->active = d->nlay - 1;
  doc_end(d);
  return 1;
}

int tl_tween(Doc *d, int k, int n)
{
  int alo, ahi, blo, bhi, j, cs;
  Layer *a, *b;
  long np = (long)d->w * d->h, q;
  if (n < 1 || n > 50 || !block(d, k, &alo, &ahi) || !block(d, k + 1, &blo, &bhi)) return 0;
  a = d->lay[ahi]; b = d->lay[bhi];
  if (alo != ahi || blo != bhi || a->kind != LK_PIXEL || b->kind != LK_PIXEL || !a->px || !b->px || d->nlay + n > MAXLAYERS) return 0;
  cs = frame_time(a->name);
  doc_begin(d, "Tween");
  doc_note_all(d);
  for (j = 1; j <= n; j++) {
    Layer *l = layer_new(d, "Tween");
    char name[64];
    long t = (long)j * 256 / (n + 1);                    /* how far towards the next frame, of 256 */
    if (!l) break;
    snprintf(name, sizeof name, "Tween %d", j);
    frame_set_time(name, cs, l->name, sizeof l->name);
    /* both frames' colours weighted by their alpha (premultiplied), so a fade in from nothing keeps its colour */
    for (q = 0; q < np; q++) {
      const u8 *p0 = a->px + q * 4, *p1 = b->px + q * 4;
      u8 *o = l->px + q * 4;
      long w0 = (256 - t) * p0[3], w1 = t * p1[3], al = w0 + w1;
      int c;
      if (al) for (c = 0; c < 3; c++) o[c] = (u8)((p0[c] * w0 + p1[c] * w1 + al / 2) / al);
      o[3] = (u8)((al + 128) >> 8);
    }
    l->opacity = (u8)(a->opacity + ((long)b->opacity - a->opacity) * t / 256);
    l->visible = 0;
    doc_add_layer(d, l, ahi + j);
  }
  doc_end(d);
  return 1;
}

int tl_reverse(Doc *d)
{
  int f[MAXLAYERS], n = frames_list(d, f, MAXLAYERS), i;
  Layer *t;
  if (n < 2 || n != d->nlay) return 0;                  /* (every layer a frame of its own) */
  for (i = 0; i < n; i++) if (!is_single(d->lay[i])) return 0;
  doc_begin(d, "Reverse Frames");
  doc_note_all(d);
  for (i = 0; i < n / 2; i++) { t = d->lay[i]; d->lay[i] = d->lay[n - 1 - i]; d->lay[n - 1 - i] = t; }
  d->active = n - 1 - d->active;
  doc_end(d);
  return 1;
}

int tl_set_all_times(Doc *d, int cs)
{
  int f[MAXLAYERS], n = frames_list(d, f, MAXLAYERS), k;
  if (n < 1) return 0;
  doc_begin(d, "Frame Time");
  for (k = 0; k < n; k++) {
    char name[64];
    frame_set_time(d->lay[f[k]]->name, cs, name, sizeof name);
    doc_set_layer_props(d, f[k], name, -1, -1, -1);
  }
  doc_end(d);
  return 1;
}

long tl_length(const Doc *d, int def)
{
  int f[MAXLAYERS], n = frames_list(d, f, MAXLAYERS), k;
  long t = 0;
  for (k = 0; k < n; k++) { int cs = frame_time(d->lay[f[k]]->name); t += cs > 0 ? cs : def; }
  return t;
}
