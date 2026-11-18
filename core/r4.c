/*
 * r4.c - Easel round 4 (see r4.h).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "r4.h"
#include "paint.h"
#include "layers.h"
#include "layerx.h"

/* ================================================================== Find Layers, Isolate Layers */
const char *const layer_filter_kinds[LFK_N + 1] = { "All", "Pixel", "Adjustment", "Type", "Shape", "Smart Object", "Group", "Fill", NULL };
LayerFilter layer_filter;

int layer_filter_on(const Doc *d)
{
  return d && layer_filter.doc == d && (layer_filter.name[0] || layer_filter.kind != LFK_ALL || layer_filter.isolate);
}
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
/* (ASCII letters without case; other bytes as they are) */
static int has_part(const char *s, const char *part)
{
  size_t n = strlen(part), i, k;
  if (!n) return 1;
  for (i = 0; s[i]; i++) {
    for (k = 0; k < n && s[i + k] && lower((unsigned char)s[i + k]) == lower((unsigned char)part[k]); k++) ;
    if (k == n) return 1;
  }
  return 0;
}
static int kind_of(const Layer *l)
{
  if (l->kind == LK_GROUP) return LFK_GROUP;
  if (l->kind == LK_ADJUST) return LFK_ADJUST;
  if (l->kind == LK_FILL) return LFK_FILL;
  if (l->kind == LK_TEXT) return LFK_TYPE;
  if (l->kind == LK_SHAPE) return LFK_SHAPE;
  if (layer_is_smart(l)) return LFK_SMART;
  return LFK_PIXEL;
}
int layer_filter_pass(const Doc *d, int i)
{
  const Layer *l;
  if (!layer_filter_on(d)) return 1;
  if (i < 0 || i >= d->nlay) return 0;
  l = d->lay[i];
  if (layer_filter.isolate) {
    int k, in = 0;
    for (k = 0; k < layer_filter.niso; k++) if (layer_filter.iso[k] == l) in = 1;
    if (!in) return 0;
  }
  if (layer_filter.kind != LFK_ALL && kind_of(l) != layer_filter.kind) return 0;
  return has_part(l->name, layer_filter.name);
}
void layer_filter_find(const Doc *d, const char *name, int kind)
{
  layer_filter.doc = d;
  snprintf(layer_filter.name, sizeof layer_filter.name, "%s", name ? name : "");
  layer_filter.kind = kind >= 0 && kind < LFK_N ? kind : LFK_ALL;
}
int layer_filter_isolate(const Doc *d)
{
  int i;
  if (layer_filter.doc == d && layer_filter.isolate) { layer_filter.isolate = 0; layer_filter.niso = 0; return 0; }
  if (layer_filter.doc != d) { memset(&layer_filter, 0, sizeof layer_filter); layer_filter.doc = d; }
  layer_filter.niso = 0;
  for (i = 0; i < d->nlay; i++) {
    const Layer *l = d->lay[i];
    if (i == d->active || l->picked) {
      int j;
      layer_filter.iso[layer_filter.niso++] = l;
      /* a group brings its members (the layers below it that are deeper) */
      if (l->kind == LK_GROUP) for (j = i - 1; j >= 0 && d->lay[j]->depth > l->depth; j--) {
        int k, dup = 0;
        for (k = 0; k < layer_filter.niso; k++) if (layer_filter.iso[k] == d->lay[j]) dup = 1;
        if (!dup && layer_filter.niso < MAXLAYERS) layer_filter.iso[layer_filter.niso++] = d->lay[j];
      }
    }
    if (layer_filter.niso >= MAXLAYERS) break;
  }
  layer_filter.isolate = 1;
  return 1;
}
void layer_filter_clear(void) { memset(&layer_filter, 0, sizeof layer_filter); }
int layer_filter_count(const Doc *d)
{
  int i, n = 0;
  for (i = 0; i < d->nlay; i++) if (layer_filter_pass(d, i)) n++;
  return n;
}

/* ================================================================== Float, Drop */
int layer_is_floating(const Layer *l) { return l && l->kind == LK_PIXEL && !strncmp(l->name, FLOAT_NAME, strlen(FLOAT_NAME)); }
int sel_float(Doc *d)
{
  Layer *src, *l;
  Box b;
  i32 x, y;
  int at;
  if (!d->sel || d->active < 0 || d->active >= d->nlay) return -1;
  src = d->lay[d->active];
  if (src->kind != LK_PIXEL || !src->px || src->locked || src->lock_all) return -1;
  b = box_clip(d->selbox, doc_box(d));
  if (box_empty(b) || !(l = layer_new(d, FLOAT_NAME))) return -1;
  for (y = b.y0; y < b.y1; y++)
    for (x = b.x0; x < b.x1; x++) {
      long k = (long)y * d->w + x;
      int s = d->sel[k];
      if (!s) continue;
      memcpy(l->px + k * 4, src->px + k * 4, 3);
      l->px[k * 4 + 3] = (u8)((src->px[k * 4 + 3] * s + 127) / 255);
    }
  l->depth = src->depth;
  at = d->active + 1;
  /* (the layer added first: adding one keeps the whole picture for undo as it is then) */
  doc_begin(d, "Float");
  if (!doc_add_layer(d, l, at)) { doc_end(d); return -1; }
  d->active = at - 1;
  clear_selection(d);
  d->active = at;
  doc_end(d);
  return at;
}
int sel_drop(Doc *d)
{
  int i = d->active, ok;
  if (i <= 0 || i >= d->nlay || !layer_is_floating(d->lay[i])) return 0;
  doc_begin(d, "Drop");
  ok = doc_merge_down(d, i);
  doc_end(d);
  return ok;
}

/* ================================================================== Levels: Show Clipping */
void clip_view_rgba(u8 *px, long n, int white, const int *lo, const int *hi)
{
  long k;
  int c;
  for (k = 0; k < n; k++, px += 4) {
    for (c = 0; c < 3; c++) px[c] = white ? (px[c] >= hi[c] ? 255 : 0) : (px[c] <= lo[c] ? 0 : 255);
    px[3] = 255;
  }
}
int doc_clip_view_layer(Doc *d, int white, const int *lo, const int *hi)
{
  Layer *l = layer_new(d, "Clipping");
  int at = d->nlay;
  if (!l) return -1;
  doc_composite(d, doc_box(d), l->px, (long)d->w * 4);
  clip_view_rgba(l->px, (long)d->w * d->h, white, lo, hi);
  doc_begin(d, "Show Clipping");
  if (!doc_add_layer(d, l, at)) { doc_end(d); return -1; }
  doc_end(d);
  return at;
}
int levels_clip_end(const long *before, const long *after)
{
  if (after[2] != before[2] || after[4] != before[4]) return 1;
  if (after[0] != before[0] || after[3] != before[3] || after[1] != before[1]) return 0;
  return -1;
}

/* ================================================================== Alt+digit */
int channel_for_key(int digit, int nchan)
{
  int c = digit - 2;
  return c >= 0 && c < nchan ? c : -1;
}
int rawkey_digit(int code) { return code >= 0x01 && code <= 0x0A ? code % 10 : -1; }

/* ================================================================== Color Range's preview */
const char *const cr_view_names[CRV_N + 1] = { "Selection", "Image", "Grayscale", "Black Matte", "White Matte", "Quick Mask", NULL };
void cr_view_pixel(int mode, int m, const u8 *p, u8 *out)
{
  int c;
  switch (mode) {
  case CRV_IMAGE: for (c = 0; c < 3; c++) out[c] = p[c]; break;
  case CRV_BLACKMATTE: for (c = 0; c < 3; c++) out[c] = (u8)((p[c] * m + 127) / 255); break;
  case CRV_WHITEMATTE: for (c = 0; c < 3; c++) out[c] = (u8)((p[c] * m + 255 * (255 - m) + 127) / 255); break;
  case CRV_QUICKMASK:
    /* unselected parts under Quick Mask's half-strength red */
    for (c = 0; c < 3; c++) {
      int red = c == 0 ? 255 : 0, a = (255 - m) / 2;
      out[c] = (u8)((p[c] * (255 - a) + red * a + 127) / 255);
    }
    break;
  case CRV_GRAY: { int g = (p[0] * 77 + p[1] * 150 + p[2] * 29 + 128) >> 8; out[0] = out[1] = out[2] = (u8)((g * m + 127) / 255); break; }
  default: out[0] = out[1] = out[2] = (u8)m;
  }
}

/* ================================================================== smart quotes */
static const char LDQ[] = "\xE2\x80\x9C", RDQ[] = "\xE2\x80\x9D", LSQ[] = "\xE2\x80\x98", RSQ[] = "\xE2\x80\x99";
/* an opening quote goes after nothing, white space, an opening bracket or dash, or another opening quote */
static int opens_after(int prev)
{
  return prev == 0 || prev == ' ' || prev == '\t' || prev == '\n' || prev == '\r' || prev == '(' || prev == '[' || prev == '{' || prev == '-';
}
int smart_quote_for(int c, int prev, char *out)
{
  const char *q = c == '"' ? (opens_after(prev) ? LDQ : RDQ) : (opens_after(prev) ? LSQ : RSQ);
  memcpy(out, q, 3); out[3] = 0;
  return 3;
}
int smart_quotes(const char *in, char *out, int cap)
{
  int o = 0, prev = 0, opened = 0;            /* opened: the last thing written was an opening quote */
  const unsigned char *s = (const unsigned char *)in;
  if (cap <= 0) return 0;
  for (; *s; s++) {
    if (*s == '"' || *s == '\'') {
      char q[4];
      if (o + 3 >= cap) break;
      smart_quote_for(*s, opened ? ' ' : prev, q);
      memcpy(out + o, q, 3); o += 3; prev = (unsigned char)q[2];
      opened = q[2] == LDQ[2] || q[2] == LSQ[2];
    } else {
      if (o + 1 >= cap) break;
      out[o++] = (char)*s; prev = *s; opened = 0;
    }
  }
  out[o] = 0;
  return o;
}
