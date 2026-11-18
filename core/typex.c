/*
 * typex.c - Easel 1.2: Layer > Type > Convert to Shape (a type layer made a shape layer of its
 * outlines, in place, its style and settings kept) and Update All Text Layers (every type layer
 * drawn again, after the fonts change). See typex.h.
 */
#include <stdlib.h>
#include <string.h>
#include "typex.h"
#include "shape.h"
#include "live.h"

int doc_text_to_shape(Doc *d, int i)
{
  Layer *l;
  const TextParams *tp;
  Path *p;
  ShapeParams sp;
  ShapeParams *b;
  u32 len;
  double x0, y0, x1, y1;
  if (!d || i < 0 || i >= d->nlay || (l = d->lay[i])->kind != LK_TEXT || !(tp = (const TextParams *)l->live)) return 0;
  if (!(p = text_to_path(tp))) return 0;
  if (!path_bounds(p, &x0, &y0, &x1, &y1)) { x0 = y0 = 0; x1 = y1 = 1; }
  shape_defaults(&sp, SH_CUSTOM, x0, y0, x1, y1, tp->run[0].color);
  sp.antialias = tp->aa != TAA_NONE;
  if (!(b = shape_alloc(&sp, p, &len))) { path_free(p); return 0; }
  path_free(p);
  doc_begin(d, "Convert to Shape");
  doc_note_all(d);
  free(l->live);
  l->live = b; l->livelen = len;
  l->kind = LK_SHAPE;
  free(l->livepx); l->livepx = NULL; l->live_ok = 0;
  fx_free(l);
  d->dirty = 1;
  doc_end(d);
  return 1;
}
int doc_update_text(Doc *d)
{
  int i, n = 0;
  if (!d) return 0;
  text_cache_flush();
  for (i = 0; i < d->nlay; i++)
    if (d->lay[i]->kind == LK_TEXT) { Layer *l = d->lay[i]; free(l->livepx); l->livepx = NULL; l->live_ok = 0; fx_free(l); n++; }
  return n;
}
