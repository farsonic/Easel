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
int resample_for_key(int digit)
{
  static const int map[9] = { -1, 0, 3, 1, 2, 0, 5, 4, 3 };
  return digit >= 1 && digit <= 8 ? map[digit] : -1;
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

/* ================================================================== Preset Manager */
#include "preset.h"
#include "swatch.h"
#include "gradient.h"
#include "pattern.h"
const char *const pm_kind_names[PM_N + 1] = { "Brushes", "Swatches", "Gradients", "Patterns", NULL };
int pm_count(int kind, const void *list)
{
  if (!list) return 0;
  switch (kind) {
  case PM_BRUSHES: return ((const PresetList *)list)->n;
  case PM_SWATCHES: return ((const SwatchSet *)list)->n;
  case PM_GRADIENTS: return ((const GradientList *)list)->n;
  case PM_PATTERNS: return ((const PatternList *)list)->n;
  }
  return 0;
}
/* the name field of item i, or NULL */
static char *name_of(int kind, void *list, int i, size_t *cap)
{
  if (i < 0 || i >= pm_count(kind, list)) return NULL;
  switch (kind) {
  case PM_BRUSHES: *cap = sizeof ((PresetList *)list)->p[i].name; return ((PresetList *)list)->p[i].name;
  case PM_SWATCHES: *cap = sizeof ((SwatchSet *)list)->s[i].name; return ((SwatchSet *)list)->s[i].name;
  case PM_GRADIENTS: *cap = sizeof ((GradientList *)list)->g[i].name; return ((GradientList *)list)->g[i].name;
  case PM_PATTERNS: *cap = sizeof ((PatternList *)list)->p[i]->name; return ((PatternList *)list)->p[i]->name;
  }
  return NULL;
}
const char *pm_name(int kind, const void *list, int i)
{
  size_t cap;
  const char *n = name_of(kind, (void *)list, i, &cap);
  return n ? n : "";
}
int pm_rename(int kind, void *list, int i, const char *name)
{
  size_t cap;
  char *n = name_of(kind, list, i, &cap);
  if (!n || !name || !name[0]) return 0;
  snprintf(n, cap, "%s", name);
  return 1;
}
int pm_delete(int kind, void *list, int i)
{
  if (i < 0 || i >= pm_count(kind, list)) return 0;
  switch (kind) {
  case PM_BRUSHES: presets_remove((PresetList *)list, i); break;
  case PM_SWATCHES: swatches_remove((SwatchSet *)list, i); break;
  case PM_GRADIENTS: gradlist_remove((GradientList *)list, i); break;
  case PM_PATTERNS: patlist_remove((PatternList *)list, i); break;
  default: return 0;
  }
  return 1;
}
int pm_move(int kind, void *list, int i, int dir)
{
  int j = i + (dir < 0 ? -1 : 1), n = pm_count(kind, list);
  if (i < 0 || i >= n || j < 0 || j >= n) return -1;
  switch (kind) {
  case PM_BRUSHES: { PresetList *l = (PresetList *)list; BrushPreset t = l->p[i]; l->p[i] = l->p[j]; l->p[j] = t; break; }
  case PM_SWATCHES: { SwatchSet *l = (SwatchSet *)list; Swatch t = l->s[i]; l->s[i] = l->s[j]; l->s[j] = t; break; }
  case PM_GRADIENTS: { GradientList *l = (GradientList *)list; Gradient t = l->g[i]; l->g[i] = l->g[j]; l->g[j] = t; break; }
  case PM_PATTERNS: { PatternList *l = (PatternList *)list; Pattern *t = l->p[i]; l->p[i] = l->p[j]; l->p[j] = t; break; }
  default: return -1;
  }
  return j;
}
int pm_load(int kind, void *list, const char *path)
{
  switch (kind) {
  case PM_BRUSHES: {
    size_t k = strlen(path);
    char err[80];
    if (k > 4 && !strcmp(path + k - 4, ".abr")) return presets_load_abr((PresetList *)list, path, err, sizeof err);
    return presets_load((PresetList *)list, path); }
  case PM_SWATCHES: return swatches_load((SwatchSet *)list, path);
  case PM_GRADIENTS: return gradlist_load((GradientList *)list, path);
  case PM_PATTERNS: return patlist_load((PatternList *)list, path);
  }
  return 0;
}
int pm_save(int kind, const void *list, const char *path)
{
  switch (kind) {
  case PM_BRUSHES: return presets_save((const PresetList *)list, path);
  case PM_SWATCHES: return swatches_save((const SwatchSet *)list, path);
  case PM_GRADIENTS: return gradlist_save((const GradientList *)list, path);
  case PM_PATTERNS: return patlist_save((const PatternList *)list, path);
  }
  return 0;
}
const char *pm_pattern(int kind)
{
  static const char *const pats[PM_N] = { "#?.(brushes|abr)", "#?.(swatches|gpl|aco|act|ase|iff|ilbm)", "#?.(gradients|ggr|grd)", "#?.(patterns|pat|iff|ilbm)" };
  return kind >= 0 && kind < PM_N ? pats[kind] : "#?";
}

/* ================================================================== Character and Paragraph Styles */
#include "type.h"
static long th(double v) { return (long)(v * 1000 + (v < 0 ? -0.5 : 0.5)); }
int tstyle_char_new(TypeStyles *s, const TextParams *tp, const char *name)
{
  CharStyle *c;
  const TextRun *r;
  if (!tp || s->nc >= TSTYLE_MAX) return -1;
  r = &tp->run[0];
  c = &s->c[s->nc];
  memset(c, 0, sizeof *c);
  snprintf(c->name, sizeof c->name, "%s", name && name[0] ? name : "Character Style");
  snprintf(c->family, sizeof c->family, "%s", r->family);
  c->bold = r->bold; c->italic = r->italic; c->faux_bold = r->faux_bold; c->faux_italic = r->faux_italic;
  c->underline = r->underline; c->strike = r->strike; c->ot = r->ot; c->color = r->color;
  c->size = th(r->size); c->tracking = th(r->tracking); c->baseline = th(r->baseline); c->hscale = th(r->hscale); c->vscale = th(r->vscale);
  return s->nc++;
}
int tstyle_para_new(TypeStyles *s, const TextParams *tp, const char *name)
{
  ParaStyle *p;
  if (!tp || s->np >= TSTYLE_MAX) return -1;
  p = &s->p[s->np];
  memset(p, 0, sizeof *p);
  snprintf(p->name, sizeof p->name, "%s", name && name[0] ? name : "Paragraph Style");
  p->align = tp->align; p->hyphenate = tp->hyphenate;
  p->leading = th(tp->leading); p->indent = th(tp->indent); p->space_before = th(tp->space_before); p->space_after = th(tp->space_after);
  p->indent_left = th(tp->indent_left); p->indent_right = th(tp->indent_right);
  return s->np++;
}
void tstyle_delete(TypeStyles *s, int para, int i)
{
  if (para) { if (i < 0 || i >= s->np) return; memmove(s->p + i, s->p + i + 1, sizeof s->p[0] * (size_t)(s->np - i - 1)); s->np--; }
  else { if (i < 0 || i >= s->nc) return; memmove(s->c + i, s->c + i + 1, sizeof s->c[0] * (size_t)(s->nc - i - 1)); s->nc--; }
}
int doc_char_style(Doc *d, int li, const CharStyle *c)
{
  const TextParams *cur;
  TextParams *tp;
  TextRun r;
  int ok = 0;
  if (li < 0 || li >= d->nlay || !(cur = layer_text(d->lay[li])) || !(tp = (TextParams *)malloc(sizeof *tp))) return 0;
  *tp = *cur;
  memset(&r, 0, sizeof r);
  snprintf(r.family, sizeof r.family, "%s", c->family);
  r.bold = c->bold; r.italic = c->italic; r.faux_bold = c->faux_bold; r.faux_italic = c->faux_italic;
  r.underline = c->underline; r.strike = c->strike; r.ot = c->ot; r.color = c->color;
  r.size = c->size / 1000.0; r.tracking = c->tracking / 1000.0; r.baseline = c->baseline / 1000.0; r.hscale = c->hscale / 1000.0; r.vscale = c->vscale / 1000.0;
  if (r.size <= 0) r.size = tp->run[0].size;
  if (tp->text[0]) { if (!text_style(tp, 0, (int)strlen(tp->text), &r, TS_ALL)) { free(tp); return 0; } }
  else { r.start = 0; tp->run[0] = r; tp->nruns = 1; }      /* (no text yet: the first run takes it) */
  doc_begin(d, "Character Style"); ok = doc_set_text(d, li, tp); doc_end(d);
  free(tp);
  return ok;
}
int doc_para_style(Doc *d, int li, const ParaStyle *p)
{
  const TextParams *cur;
  TextParams *tp;
  int ok;
  if (li < 0 || li >= d->nlay || !(cur = layer_text(d->lay[li])) || !(tp = (TextParams *)malloc(sizeof *tp))) return 0;
  *tp = *cur;
  tp->align = p->align; tp->hyphenate = p->hyphenate;
  tp->leading = p->leading / 1000.0; tp->indent = p->indent / 1000.0; tp->space_before = p->space_before / 1000.0;
  tp->space_after = p->space_after / 1000.0; tp->indent_left = p->indent_left / 1000.0; tp->indent_right = p->indent_right / 1000.0;
  doc_begin(d, "Paragraph Style"); ok = doc_set_text(d, li, tp); doc_end(d);
  free(tp);
  return ok;
}
char *tstyles_save(const TypeStyles *s)
{
  size_t cap = 64 + (size_t)(s->nc + s->np) * 400, o = 0;
  char *t = (char *)malloc(cap);
  int i;
  if (!t) return NULL;
  o += (size_t)snprintf(t + o, cap - o, "EaselTypeStyles 1\n");
  for (i = 0; i < s->nc; i++) {
    const CharStyle *c = &s->c[i];
    o += (size_t)snprintf(t + o, cap - o, "C\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%u\t%06lX\t%ld\t%ld\t%ld\t%ld\t%ld\n", c->name, c->family, c->bold, c->italic,
                          c->faux_bold, c->faux_italic, c->underline, c->strike, (unsigned)c->ot, (unsigned long)c->color, c->size, c->tracking, c->baseline, c->hscale, c->vscale);
  }
  for (i = 0; i < s->np; i++) {
    const ParaStyle *p = &s->p[i];
    o += (size_t)snprintf(t + o, cap - o, "P\t%s\t%d\t%d\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\n", p->name, p->align, p->hyphenate, p->leading, p->indent,
                          p->space_before, p->space_after, p->indent_left, p->indent_right);
  }
  return t;
}
/* the next tab-separated field of a line into out (cap bytes); the line moves past it */
static const char *field(const char *l, char *out, size_t cap)
{
  size_t k = 0;
  while (*l && *l != '\t' && *l != '\n') { if (k + 1 < cap) out[k++] = *l; l++; }
  out[k] = 0;
  return *l == '\t' ? l + 1 : l;
}
int tstyles_load(TypeStyles *s, const char *text)
{
  const char *l = text;
  int n = 0;
  if (strncmp(text, "EaselTypeStyles", 15)) return 0;
  while (l && *l) {
    char f[16][64];
    int k = 0;
    const char *q = l;
    while (k < 16 && *q && *q != '\n') q = field(q, f[k++], sizeof f[0]);
    if (k == 16 && !strcmp(f[0], "C") && s->nc < TSTYLE_MAX) {
      CharStyle *c = &s->c[s->nc++];
      memset(c, 0, sizeof *c);
      snprintf(c->name, sizeof c->name, "%s", f[1]); snprintf(c->family, sizeof c->family, "%s", f[2]);
      c->bold = (u8)atoi(f[3]); c->italic = (u8)atoi(f[4]); c->faux_bold = (u8)atoi(f[5]); c->faux_italic = (u8)atoi(f[6]);
      c->underline = (u8)atoi(f[7]); c->strike = (u8)atoi(f[8]); c->ot = (u16)atoi(f[9]); c->color = (u32)strtoul(f[10], NULL, 16);
      c->size = atol(f[11]); c->tracking = atol(f[12]); c->baseline = atol(f[13]); c->hscale = atol(f[14]); c->vscale = atol(f[15]);
      n++;
    } else if (k == 10 && !strcmp(f[0], "P") && s->np < TSTYLE_MAX) {
      ParaStyle *p = &s->p[s->np++];
      memset(p, 0, sizeof *p);
      snprintf(p->name, sizeof p->name, "%s", f[1]);
      p->align = (u8)atoi(f[2]); p->hyphenate = (u8)atoi(f[3]); p->leading = atol(f[4]); p->indent = atol(f[5]);
      p->space_before = atol(f[6]); p->space_after = atol(f[7]); p->indent_left = atol(f[8]); p->indent_right = atol(f[9]);
      n++;
    }
    l = strchr(l, '\n'); if (l) l++;
  }
  return n;
}

/* ================================================================== Select and Mask's views */
const char *const sm_view_names[SMV_N + 1] = { "Marching Ants", "Overlay", "On Black", "On White", "Black & White", "On Layers", NULL };
int doc_sel_view_layer(Doc *d, int mode)
{
  static const int crv[SMV_N] = { -1, CRV_QUICKMASK, CRV_BLACKMATTE, CRV_WHITEMATTE, CRV_SELECTION, -1 };
  Layer *l;
  long k, n = (long)d->w * d->h;
  int at = d->nlay;
  if (mode <= SMV_ANTS || mode >= SMV_N || !(l = layer_new(d, "Select and Mask View"))) return -1;
  doc_composite(d, doc_box(d), l->px, (long)d->w * 4);
  for (k = 0; k < n; k++) {
    u8 *p = l->px + k * 4, rgb[3];
    int m = d->sel ? d->sel[k] : 255;
    if (mode == SMV_ONLAYERS) {
      /* what the selection keeps over a checkerboard (as transparency is shown) */
      int x = (int)(k % d->w), y = (int)(k / d->w), c = ((x >> 3) ^ (y >> 3)) & 1 ? 204 : 255, a = p[3] * m / 255, j;
      for (j = 0; j < 3; j++) p[j] = (u8)((p[j] * a + c * (255 - a) + 127) / 255);
      p[3] = 255;
      continue;
    }
    cr_view_pixel(crv[mode], m, p, rgb);
    p[0] = rgb[0]; p[1] = rgb[1]; p[2] = rgb[2]; p[3] = 255;
  }
  doc_begin(d, "Select and Mask View");
  if (!doc_add_layer(d, l, at)) { doc_end(d); return -1; }
  doc_end(d);
  return at;
}
