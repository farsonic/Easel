/*
 * layerops.c - more of the Layer menu (Easel 1.1); see layerops.h.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "layerops.h"
#include "layers.h"
#include "fx.h"
#include "live.h"

const char *const label_names[NLABELS] = { "None", "Red", "Orange", "Yellow", "Green", "Blue", "Violet", "Gray", "Seafoam", "Indigo", "Magenta", "Fuchsia" };
const u32 label_colors[NLABELS] = { 0, 0xC84646, 0xD8892B, 0xD7C43C, 0x4BA34B, 0x4878C8, 0x9466C3, 0x8C8C8C, 0x5EC4A8, 0x4B4FA6, 0xC2408F, 0xE05BD0 };
const char *const stack_names[STK_N] = { "Mean", "Median", "Minimum", "Maximum", "Range", "Summation", "Variance",
  "Standard Deviation", "Entropy", "Skewness", "Kurtosis" };

static int chosen(const Doc *d, int i) { return i == d->active || d->lay[i]->picked; }
int doc_set_label(Doc *d, int label)
{
  int i, n = 0;
  if (label < 0 || label >= NLABELS) return 0;
  doc_begin(d, "Layer Color");
  for (i = 0; i < d->nlay; i++) if (chosen(d, i)) { d->lay[i]->label = (u8)label; n++; }
  doc_end(d);
  return n;
}
/* layers lo..hi-1 taken out of the stack (freed); the active layer kept in step */
static void take_out(Doc *d, int lo, int hi)
{
  int k, n = hi - lo;
  for (k = lo; k < hi; k++) layer_free(d->lay[k]);
  memmove(&d->lay[lo], &d->lay[hi], sizeof(Layer *) * (size_t)(d->nlay - hi));
  d->nlay -= n;
  if (d->active >= hi) d->active -= n;
  else if (d->active >= lo) d->active = lo > 0 ? lo - 1 : 0;
  if (d->active >= d->nlay) d->active = d->nlay - 1;
}
int doc_delete_hidden(Doc *d)
{
  int i, gone = 0;
  /* would anything be left? */
  for (i = 0; i < d->nlay; i++) if (layer_shown(d, i) && d->lay[i]->kind != LK_GROUP) break;
  if (i == d->nlay) return 0;
  doc_begin(d, "Delete Hidden Layers");
  doc_note_all(d);
  for (i = d->nlay - 1; i >= 0; i--) {
    Layer *l;
    if (i >= d->nlay) continue;
    l = d->lay[i];
    if (l->visible) continue;
    if (l->kind == LK_GROUP) { int lo = block_start(d, i); gone += i + 1 - lo; take_out(d, lo, i + 1); i = lo; }
    else { take_out(d, i, i + 1); gone++; }
  }
  doc_end(d);
  return gone;
}
int doc_reverse_layers(Doc *d)
{
  int idx[MAXLAYERS], n = 0, i, depth;
  if (d->active < 0 || d->active >= d->nlay) return 0;
  depth = d->lay[d->active]->depth;
  for (i = 0; i < d->nlay; i++)
    if (chosen(d, i) && d->lay[i]->depth == depth && d->lay[i]->kind != LK_GROUP) idx[n++] = i;
  if (n < 2) return 0;
  doc_begin(d, "Reverse");
  doc_note_all(d);
  for (i = 0; i < n / 2; i++) {
    Layer *t = d->lay[idx[i]];
    d->lay[idx[i]] = d->lay[idx[n - 1 - i]]; d->lay[idx[n - 1 - i]] = t;
    if (d->active == idx[i]) d->active = idx[n - 1 - i];
    else if (d->active == idx[n - 1 - i]) d->active = idx[i];
  }
  doc_end(d);
  return n;
}
int doc_background_from_layer(Doc *d)
{
  Layer *l;
  long k, n = (long)d->w * d->h;
  int i = d->active;
  u8 bg[4];
  if (i < 0 || i >= d->nlay || d->lay[i]->kind != LK_PIXEL || !d->lay[i]->px) return 0;
  l = d->lay[i];
  bg[0] = (u8)(d->bg >> 16); bg[1] = (u8)(d->bg >> 8); bg[2] = (u8)d->bg; bg[3] = 255;
  doc_begin(d, "Background From Layer");
  doc_note_all(d);
  for (k = 0; k < n; k++) {
    u8 *p = l->px + k * 4, o[4];
    memcpy(o, bg, 4);
    if (p[3]) blend_pixel(o, p, 255, BM_NORMAL);
    memcpy(p, o, 4);
  }
  memmove(&d->lay[1], &d->lay[0], sizeof(Layer *) * (size_t)i);
  d->lay[0] = l; d->active = 0;
  strcpy(l->name, "Background");
  l->opacity = 255; l->blend = BM_NORMAL; l->visible = 1; l->depth = 0; l->clip = 0; l->fill_cut = 0;
  l->alpha_lock = 0; l->lock_pos = 1;
  memset(l->blendif, 0, sizeof l->blendif);
  if (l->mask) { free(l->mask); l->mask = NULL; free(l->maskfx); l->maskfx = NULL; l->maskfx_ok = 0; }
  if (d->nlay > 1) d->lay[1]->clip = 0;      /* (nothing can be clipped to what is below the bottom) */
  doc_end(d);
  return 1;
}

/* ------------------------------------------------------------------ Copy CSS */
static const char *css_blend(int m)
{
  static const struct { int m; const char *s; } t[] = {
    { BM_MULTIPLY, "multiply" }, { BM_SCREEN, "screen" }, { BM_OVERLAY, "overlay" }, { BM_DARKEN, "darken" }, { BM_LIGHTEN, "lighten" },
    { BM_DODGE, "color-dodge" }, { BM_BURN, "color-burn" }, { BM_HARDLIGHT, "hard-light" }, { BM_SOFTLIGHT, "soft-light" },
    { BM_DIFFERENCE, "difference" }, { BM_EXCLUSION, "exclusion" }, { BM_HUE, "hue" }, { BM_SATURATION, "saturation" },
    { BM_COLOR, "color" }, { BM_LUMINOSITY, "luminosity" }, { BM_ADD, "plus-lighter" } };
  int k;
  for (k = 0; k < (int)(sizeof t / sizeof t[0]); k++) if (t[k].m == m) return t[k].s;
  return NULL;
}
/* a class name from the layer's: letters and digits kept, the rest '-', lower case */
static void css_class(const char *name, char *out, size_t cap)
{
  size_t j = 0;
  int dash = 0;
  for (; *name && j + 2 < cap; name++) {
    unsigned char ch = (unsigned char)*name;
    if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) { out[j++] = (char)ch; dash = 0; }
    else if (ch >= 'A' && ch <= 'Z') { out[j++] = (char)(ch + 32); dash = 0; }
    else if (!dash && j) { out[j++] = '-'; dash = 1; }
  }
  while (j && out[j - 1] == '-') j--;
  if (!j) { strcpy(out, "layer"); return; }
  out[j] = 0;
}
static void css_num(Buf *b, double v)
{
  long t = (long)floor(v * 100 + 0.5);
  if (t % 100 == 0) buf_printf(b, "%ld", t / 100);
  else if (t % 10 == 0) buf_printf(b, "%s%ld.%ld", t < 0 ? "-" : "", labs(t) / 100, labs(t) / 10 % 10);
  else buf_printf(b, "%s%ld.%02ld", t < 0 ? "-" : "", labs(t) / 100, labs(t) % 100);
}
static void css_rgba(Buf *b, u32 c, int a)
{
  if (a >= 255) { buf_printf(b, "#%06lx", (unsigned long)(c & 0xFFFFFF)); return; }
  buf_printf(b, "rgba(%d, %d, %d, ", (int)(c >> 16) & 255, (int)(c >> 8) & 255, (int)c & 255);
  css_num(b, a / 255.0);
  buf_adds(b, ")");
}
char *layer_css(Doc *d, int i)
{
  Layer *l;
  Box bx;
  Buf b;
  char cls[64];
  const char *bm;
  if (i < 0 || i >= d->nlay) return NULL;
  l = d->lay[i];
  if (l->kind == LK_PIXEL) { if (!layer_content_box(d, i, &bx)) bx = box_make(0, 0, 0, 0); }
  else if (l->kind == LK_TEXT || l->kind == LK_SHAPE) { if (!l->live_ok) live_render(d, l); bx = box_clip(l->livebox, doc_box(d)); }
  else bx = doc_box(d);
  css_class(l->name, cls, sizeof cls);
  buf_init(&b);
  buf_printf(&b, ".%s {\n  position: absolute;\n  left: %ldpx;\n  top: %ldpx;\n  width: %ldpx;\n  height: %ldpx;\n", cls,
             (long)bx.x0, (long)bx.y0, (long)(bx.x1 > bx.x0 ? bx.x1 - bx.x0 : 0), (long)(bx.y1 > bx.y0 ? bx.y1 - bx.y0 : 0));
  if (l->opacity < 255) { buf_adds(&b, "  opacity: "); css_num(&b, l->opacity / 255.0); buf_adds(&b, ";\n"); }
  if (!l->visible) buf_adds(&b, "  visibility: hidden;\n");
  if ((bm = css_blend(l->blend)) != NULL) buf_printf(&b, "  mix-blend-mode: %s;\n", bm);
  if (l->kind == LK_FILL && l->fillp.kind == 0) { buf_adds(&b, "  background-color: "); css_rgba(&b, l->fillp.c0, 255 - l->fill_cut); buf_adds(&b, ";\n"); }
  if (l->kind == LK_FILL && l->fillp.kind == 1) {
    u32 a = l->fillp.reverse ? l->fillp.c1 : l->fillp.c0, z = l->fillp.reverse ? l->fillp.c0 : l->fillp.c1;
    if (l->fillp.gkind == 1) buf_printf(&b, "  background-image: radial-gradient(#%06lx, #%06lx);\n", (unsigned long)a, (unsigned long)z);
    else buf_printf(&b, "  background-image: linear-gradient(%ddeg, #%06lx, #%06lx);\n", ((90 - l->fillp.angle) % 360 + 360) % 360, (unsigned long)a, (unsigned long)z);
  }
  if (fx_any(l)) {
    const LayerFx *f = &l->fx;
    int la, lalt, any = 0, k;
    fx_light(d, &la, &lalt);
    for (k = 0; k < 2; k++) {
      const FxOne *e = &f->e[k ? FX_INNERSHADOW : FX_DROPSHADOW];
      double a, dx, dy;
      if (!e->on) continue;
      a = (e->flags & FXF_GLOBAL ? la : e->angle) * 3.14159265358979 / 180;
      dx = -cos(a) * e->distance; dy = sin(a) * e->distance;
      buf_adds(&b, any ? ", " : "  box-shadow: ");
      if (k) buf_adds(&b, "inset ");
      css_num(&b, dx); buf_adds(&b, dx ? "px " : " "); css_num(&b, dy); buf_adds(&b, dy ? "px " : " ");
      buf_printf(&b, "%dpx %dpx ", e->size * (100 - e->spread) / 100, e->size * e->spread / 100);
      css_rgba(&b, e->color, e->opacity);
      any = 1;
    }
    if (f->e[FX_OUTERGLOW].on) {
      const FxOne *e = &f->e[FX_OUTERGLOW];
      buf_adds(&b, any ? ", " : "  box-shadow: ");
      buf_printf(&b, "0 0 %dpx %dpx ", e->size * (100 - e->spread) / 100, e->size * e->spread / 100);
      css_rgba(&b, e->color, e->opacity);
      any = 1;
    }
    if (any) buf_adds(&b, ";\n");
    if (f->e[FX_STROKE].on) {
      buf_printf(&b, "  %s: %dpx solid ", f->e[FX_STROKE].style == FXS_OUTSIDE ? "outline" : "border", f->e[FX_STROKE].size);
      css_rgba(&b, f->e[FX_STROKE].color, f->e[FX_STROKE].opacity);
      buf_adds(&b, ";\n");
    }
    if (f->e[FX_COLOR].on) { buf_adds(&b, "  background-color: "); css_rgba(&b, f->e[FX_COLOR].color, f->e[FX_COLOR].opacity); buf_adds(&b, ";\n"); }
  }
  buf_adds(&b, "}\n");
  if (b.oom) { buf_free(&b); return NULL; }
  return buf_take(&b);
}

/* ------------------------------------------------------------------ Stack Mode */
static int stat_of(int mode, const int *v, int n)
{
  int k, j;
  double m = 0, s2 = 0, s3 = 0, s4 = 0, sd;
  switch (mode) {
  case STK_MIN: { int r = 255; for (k = 0; k < n; k++) if (v[k] < r) r = v[k]; return r; }
  case STK_MAX: { int r = 0; for (k = 0; k < n; k++) if (v[k] > r) r = v[k]; return r; }
  case STK_RANGE: { int lo = 255, hi = 0; for (k = 0; k < n; k++) { if (v[k] < lo) lo = v[k]; if (v[k] > hi) hi = v[k]; } return hi - lo; }
  case STK_SUM: { int r = 0; for (k = 0; k < n; k++) r += v[k]; return r > 255 ? 255 : r; }
  case STK_MEDIAN: {
    int t[MAXLAYERS], a;
    for (k = 0; k < n; k++) t[k] = v[k];
    for (k = 1; k < n; k++) { a = t[k]; for (j = k - 1; j >= 0 && t[j] > a; j--) t[j + 1] = t[j]; t[j + 1] = a; }
    return n & 1 ? t[n / 2] : (t[n / 2 - 1] + t[n / 2] + 1) / 2;
  }
  case STK_ENTROPY: {
    /* the Shannon entropy of the values, in bits, as a share of the most there can be (all different) */
    double e = 0;
    if (n < 2) return 0;
    for (k = 0; k < n; k++) {
      int c = 0;
      for (j = 0; j < n; j++) if (v[j] == v[k]) c++;
      e -= log((double)c / n) / log(2.0) / n;
    }
    return (int)floor(e / (log((double)n) / log(2.0)) * 255 + 0.5);
  }
  default: break;
  }
  for (k = 0; k < n; k++) m += v[k];
  m /= n;
  for (k = 0; k < n; k++) { double t = v[k] - m; s2 += t * t; s3 += t * t * t; s4 += t * t * t * t; }
  s2 /= n; s3 /= n; s4 /= n;
  sd = sqrt(s2);
  switch (mode) {
  case STK_MEAN: return (int)floor(m + 0.5);
  case STK_VARIANCE: return s2 / 255 > 255 ? 255 : (int)floor(s2 / 255 + 0.5);    /* (in 255ths, so 0-255) */
  case STK_STDDEV: return sd > 255 ? 255 : (int)floor(sd + 0.5);
  case STK_SKEWNESS: { double g = sd > 0 ? s3 / (sd * sd * sd) : 0; int r = (int)floor(128 + g * 64 + 0.5); return r < 0 ? 0 : r > 255 ? 255 : r; }
  case STK_KURTOSIS: { double g = s2 > 0 ? s4 / (s2 * s2) : 0; int r = (int)floor(g * 32 + 0.5); return r < 0 ? 0 : r > 255 ? 255 : r; }
  }
  return 0;
}
int doc_stack_mode(Doc *d, int mode)
{
  Layer *src[MAXLAYERS], *nl;
  int n = 0, i, npick = 0;
  i32 x, y;
  char nm[64];
  if (mode < 0 || mode >= STK_N || d->nlay >= MAXLAYERS) return -1;
  for (i = 0; i < d->nlay; i++) if (chosen(d, i) && d->lay[i]->kind == LK_PIXEL && d->lay[i]->px) npick++;
  for (i = 0; i < d->nlay; i++) {
    Layer *l = d->lay[i];
    if (l->kind != LK_PIXEL || !l->px) continue;
    if (npick >= 2 ? chosen(d, i) : layer_shown(d, i)) src[n++] = l;
  }
  if (n < 2) return -1;
  snprintf(nm, sizeof nm, "Stack (%s)", stack_names[mode]);
  if (!(nl = layer_new(d, nm))) return -1;
  for (y = 0; y < d->h; y++) {
    if (progress(y, d->h)) { layer_free(nl); return -1; }
    for (x = 0; x < d->w; x++) {
      long q = ((long)y * d->w + x) * 4;
      int v[MAXLAYERS], c, k, m = 0, amax = 0;
      for (k = 0; k < n; k++) if (src[k]->px[q + 3]) { m++; if (src[k]->px[q + 3] > amax) amax = src[k]->px[q + 3]; }
      if (!m) continue;
      for (c = 0; c < 3; c++) {
        int j = 0;
        for (k = 0; k < n; k++) if (src[k]->px[q + 3]) v[j++] = src[k]->px[q + c];
        nl->px[q + c] = (u8)stat_of(mode, v, m);
      }
      nl->px[q + 3] = (u8)amax;
    }
  }
  doc_begin(d, "Stack Mode");
  i = doc_add_layer(d, nl, d->nlay);
  doc_end(d);
  return i ? d->active : -1;
}
