/*
 * scripts.c - File > Scripts and Automate (Easel 1.1), see scripts.h.
 */
#include <stdlib.h>
#include <string.h>
#include "scripts.h"
#include "layers.h"
#include "paint.h"

static int layer_empty(const Doc *d, int i)
{
  const Layer *l = d->lay[i];
  long n = (long)d->w * d->h, k;
  if (l->kind == LK_GROUP) return i == 0 || d->lay[i - 1]->depth <= l->depth;   /* (no members just below it) */
  if (l->kind != LK_PIXEL || !l->px || l->mask) return 0;
  for (k = 0; k < n; k++) if (l->px[k * 4 + 3]) return 0;
  return 1;
}
int doc_delete_empty_layers(Doc *d)
{
  int i, n = 0;
  doc_begin(d, "Delete All Empty Layers");
  for (i = d->nlay - 1; i >= 0 && d->nlay > 1; i--)
    if (i < d->nlay && layer_empty(d, i) && doc_delete_layer(d, i)) n++;
  doc_end(d);
  return n;
}
int doc_flatten_all_masks(Doc *d)
{
  int i, n = 0;
  doc_begin(d, "Flatten All Masks");
  for (i = 0; i < d->nlay; i++)
    if (d->lay[i]->mask && d->lay[i]->kind == LK_PIXEL && doc_mask_delete(d, i, 1)) n++;
  doc_end(d);
  return n;
}
int doc_fit_image(Doc *d, i32 maxw, i32 maxh, int enlarge)
{
  double s;
  i32 w, h;
  if (maxw < 1 || maxh < 1) return 0;
  s = (double)maxw / d->w;
  if ((double)maxh / d->h < s) s = (double)maxh / d->h;
  if (s >= 1 && !enlarge) return 0;
  w = (i32)(d->w * s + 0.5); h = (i32)(d->h * s + 0.5);
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (w > MAXDIM) w = MAXDIM;
  if (h > MAXDIM) h = MAXDIM;
  if (w == d->w && h == d->h) return 0;
  return image_resize(d, w, h, RS_BICUBIC);
}
char *lorem_ipsum(int words)
{
  static const char *const text =
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore "
    "magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo "
    "consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. "
    "Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum. ";
  size_t tl = strlen(text), o = 0, cap;
  int w = 0;
  char *out;
  if (words < 1) words = 1;
  cap = (size_t)words * 16 + 8;
  if (!(out = (char *)malloc(cap))) return NULL;
  while (w < words && o + 16 < cap) {               /* the passage over again until there are enough words */
    size_t k;
    for (k = 0; k < tl && w < words && o + 16 < cap; k++) {
      out[o++] = text[k];
      if (text[k] == ' ') w++;
    }
  }
  while (o && out[o - 1] == ' ') o--;
  if (o && out[o - 1] == ',') o--;
  if (o && out[o - 1] != '.') out[o++] = '.';
  out[o] = 0;
  return out;
}
