/*
 * live.c - type and shape layers' shared parts (see live.h). Easel 0.9.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "live.h"
#include "type.h"
#include "shape.h"
#include "path.h"

int live_render(Doc *d, Layer *l)
{
  u8 *px = NULL;
  Box b;
  int ok = 0;
  l->live_ok = 1;                                         /* (not tried again for every row when it fails) */
  if (layer_text(l)) ok = text_render((const TextParams *)l->live, d->w, d->h, &px, &b);
  else if (layer_shape(l)) ok = shape_render((const ShapeParams *)l->live, d->w, d->h, &px, &b);
  else { free(l->livepx); l->livepx = NULL; return 1; }
  if (!ok) return 0;
  free(l->livepx);
  l->livepx = px; l->livebox = px ? b : box_make(0, 0, 0, 0);
  return 1;
}

/* ------------------------------------------------------------------ as text */
static void enc(Buf *b, const char *s)
{
  static const char hex[] = "0123456789ABCDEF";
  for (; *s; s++) {
    u8 c = (u8)*s;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' || c == ' ') buf_addc(b, c);
    else { buf_addc(b, '%'); buf_addc(b, hex[c >> 4]); buf_addc(b, hex[c & 15]); }
  }
}
static void kv_num(Buf *b, const char *k, double v) { buf_printf(b, "&%s=%.10g", k, v); }
static void kv_str(Buf *b, const char *k, const char *s) { buf_printf(b, "&%s=", k); enc(b, s); }
/* a value decoded in place */
static void dec(char *s)
{
  char *o = s;
  for (; *s; s++) {
    if (*s == '%' && s[1] && s[2]) {
      char h[3];
      h[0] = s[1]; h[1] = s[2]; h[2] = 0;
      *o++ = (char)strtol(h, NULL, 16);
      s += 2;
    } else *o++ = *s;
  }
  *o = 0;
}
/* up to n comma-separated numbers */
static int nums(const char *s, double *v, int n)
{
  int k = 0;
  char *e;
  while (*s && k < n) {
    v[k] = strtod(s, &e);
    if (e == s) break;
    if (!(v[k] == v[k])) v[k] = 0;                        /* (NaN) */
    k++;
    s = e;
    if (*s == ',') s++;
  }
  return k;
}
static void save_text(Buf *b, const TextParams *tp)
{
  int i;
  buf_adds(b, "v=1");
  kv_num(b, "x", tp->x); kv_num(b, "y", tp->y);
  if (tp->box) { kv_num(b, "box", 1); kv_num(b, "bw", tp->bw); kv_num(b, "bh", tp->bh); }
  kv_num(b, "align", tp->align); kv_num(b, "aa", tp->aa); kv_num(b, "kern", tp->kern);
  if (tp->vertical) kv_num(b, "vert", 1);
  if (tp->leading) kv_num(b, "lead", tp->leading);
  if (tp->indent) kv_num(b, "indent", tp->indent);
  if (tp->space_before) kv_num(b, "sb", tp->space_before);
  if (tp->space_after) kv_num(b, "sa", tp->space_after);
  if (tp->indent_left) kv_num(b, "il", tp->indent_left);      /* 1.2 */
  if (tp->indent_right) kv_num(b, "ir", tp->indent_right);
  if (tp->warp) {
    kv_num(b, "warp", tp->warp); kv_num(b, "bend", tp->warp_bend); kv_num(b, "wh", tp->warp_h); kv_num(b, "wv", tp->warp_v);
    if (tp->warp_vertical) kv_num(b, "warpv", 1);
  }
  if (tp->xf_on) buf_printf(b, "&xf=%.10g,%.10g,%.10g,%.10g,%.10g,%.10g", tp->xf[0], tp->xf[1], tp->xf[2], tp->xf[3], tp->xf[4], tp->xf[5]);
  kv_str(b, "text", tp->text);
  for (i = 0; i < tp->nruns; i++) {
    const TextRun *r = &tp->run[i];
    int fl = r->bold | r->italic << 1 | r->faux_bold << 2 | r->faux_italic << 3 | r->underline << 4 | r->strike << 5 | (int)r->ot << 6;   /* (1.2: ot) */
    buf_printf(b, "&r=%ld,%.10g,%ld,%.10g,%.10g,%d,", (long)r->start, r->size, (long)(r->color & 0xFFFFFF), r->tracking, r->baseline, fl);
    enc(b, r->family);
  }
}
static void save_shape(Buf *b, const ShapeParams *sp)
{
  Path p = shape_path(sp);
  char *svg = path_to_svg(&p), *ty = path_types(&p);
  int i;
  buf_adds(b, "v=1");
  kv_num(b, "kind", sp->kind); kv_num(b, "fill", sp->fill_on); kv_num(b, "stroke", sp->stroke_on);
  kv_num(b, "align", sp->align); kv_num(b, "cap", sp->cap); kv_num(b, "join", sp->join); kv_num(b, "aa", sp->antialias);
  kv_num(b, "eo", sp->evenodd);
  kv_num(b, "fc", sp->fill_color & 0xFFFFFF); kv_num(b, "sc", sp->stroke_color & 0xFFFFFF);
  kv_num(b, "fa", sp->fill_alpha); kv_num(b, "sa", sp->stroke_alpha);
  kv_num(b, "sw", sp->stroke_width); kv_num(b, "miter", sp->miter);
  if (sp->ndash > 0) {
    buf_adds(b, "&dash=");
    for (i = 0; i < sp->ndash && i < 8; i++) buf_printf(b, i ? ",%.10g" : "%.10g", sp->dash[i]);
    kv_num(b, "doff", sp->dash_off);
  }
  kv_num(b, "x0", sp->x0); kv_num(b, "y0", sp->y0); kv_num(b, "x1", sp->x1); kv_num(b, "y1", sp->y1);
  kv_num(b, "r", sp->radius);
  if (sp->kind == SH_POLYGON) { kv_num(b, "sides", sp->sides); kv_num(b, "star", sp->star); kv_num(b, "indent", sp->indent); kv_num(b, "angle", sp->angle); }
  if (sp->kind == SH_LINE) {
    kv_num(b, "lw", sp->line_width); kv_num(b, "as", sp->arrow_start); kv_num(b, "ae", sp->arrow_end);
    kv_num(b, "aw", sp->arrow_w); kv_num(b, "al", sp->arrow_len); kv_num(b, "ac", sp->arrow_concave);
  }
  kv_num(b, "peo", sp->evenodd_path);
  if (svg) kv_str(b, "path", svg);
  if (ty) kv_str(b, "types", ty);
  free(svg); free(ty);
}
char *live_save(const Layer *l)
{
  Buf b;
  buf_init(&b);
  if (layer_text(l)) save_text(&b, (const TextParams *)l->live);
  else if (layer_shape(l)) save_shape(&b, (const ShapeParams *)l->live);
  else return NULL;
  if (b.oom) { buf_free(&b); return NULL; }
  return buf_take(&b);
}
static int load_text(Layer *l, char *s)
{
  TextParams *tp = (TextParams *)calloc(1, sizeof *tp);
  char *p = s;
  int nr = 0;
  if (!tp) return 0;
  text_params_init(tp, "", 24, 0, 0, 0);
  tp->nruns = 0;
  while (p && *p) {
    char *amp = strchr(p, '&'), *eq, *k, *v;
    double x;
    if (amp) *amp = 0;
    k = p; p = amp ? amp + 1 : NULL;
    if (!(eq = strchr(k, '='))) continue;
    *eq = 0; v = eq + 1;
    x = strtod(v, NULL);
    if (!(x == x)) x = 0;
    if (!strcmp(k, "x")) tp->x = x;
    else if (!strcmp(k, "y")) tp->y = x;
    else if (!strcmp(k, "box")) tp->box = x != 0;
    else if (!strcmp(k, "bw")) tp->bw = x;
    else if (!strcmp(k, "bh")) tp->bh = x;
    else if (!strcmp(k, "align")) tp->align = (u8)(x >= 0 && x <= 6 ? x : 0);   /* (1.2: up to Justify Last Right) */
    else if (!strcmp(k, "aa")) tp->aa = (u8)(x >= 0 && x <= 2 ? x : 2);
    else if (!strcmp(k, "kern")) tp->kern = x != 0;
    else if (!strcmp(k, "vert")) tp->vertical = x != 0;
    else if (!strcmp(k, "lead")) tp->leading = x;
    else if (!strcmp(k, "indent")) tp->indent = x;
    else if (!strcmp(k, "sb")) tp->space_before = x;
    else if (!strcmp(k, "sa")) tp->space_after = x;
    else if (!strcmp(k, "il")) tp->indent_left = x;             /* 1.2 */
    else if (!strcmp(k, "ir")) tp->indent_right = x;
    else if (!strcmp(k, "warp")) tp->warp = (u8)(x >= 0 && x < TW_N ? x : 0);
    else if (!strcmp(k, "bend")) tp->warp_bend = x;
    else if (!strcmp(k, "wh")) tp->warp_h = x;
    else if (!strcmp(k, "wv")) tp->warp_v = x;
    else if (!strcmp(k, "warpv")) tp->warp_vertical = x != 0;
    else if (!strcmp(k, "xf")) { if (nums(v, tp->xf, 6) == 6) tp->xf_on = 1; }
    else if (!strcmp(k, "text")) { dec(v); strncpy(tp->text, v, TP_MAXTEXT - 1); }
    else if (!strcmp(k, "r") && nr < TP_MAXRUNS) {
      double a[6];
      char *c = v;
      int i, fl;
      TextRun *r = &tp->run[nr];
      if (nums(v, a, 6) < 6) continue;
      for (i = 0; i < 6 && c; i++) { c = strchr(c, ','); if (c) c++; }
      memset(r, 0, sizeof *r);
      r->start = (i32)a[0]; r->size = a[1]; r->color = (u32)a[2] & 0xFFFFFF; r->tracking = a[3]; r->baseline = a[4];
      fl = (int)a[5];
      r->bold = fl & 1; r->italic = (fl >> 1) & 1; r->faux_bold = (fl >> 2) & 1; r->faux_italic = (fl >> 3) & 1;
      r->underline = (fl >> 4) & 1; r->strike = (fl >> 5) & 1;
      r->ot = (u16)((fl >> 6) & 0x3FFF);                  /* 1.2 */
      if (c) { dec(c); strncpy(r->family, c, sizeof r->family - 1); }
      nr++;
    }
  }
  tp->nruns = nr;
  if (!nr) { tp->nruns = 1; tp->run[0].size = 24; }
  /* (text_layout sanitizes the rest whenever it is used; here the essentials) */
  {
    i32 n = (i32)strlen(tp->text), i;
    tp->run[0].start = 0;
    for (i = 1; i < tp->nruns; i++) {
      if (tp->run[i].start < tp->run[i - 1].start) tp->run[i].start = tp->run[i - 1].start;
      if (tp->run[i].start > n) tp->run[i].start = n;
    }
    for (i = 0; i < tp->nruns; i++) if (!(tp->run[i].size >= 1 && tp->run[i].size <= 2000)) tp->run[i].size = 24;
  }
  free(l->live);
  l->live = tp; l->livelen = sizeof *tp;
  return 1;
}
static int load_shape(Layer *l, char *s)
{
  ShapeParams sp;
  Path *path = NULL;
  char *p = s, *types = NULL;
  ShapeParams *blk;
  u32 len;
  memset(&sp, 0, sizeof sp);
  sp.antialias = 1;
  while (p && *p) {
    char *amp = strchr(p, '&'), *eq, *k, *v;
    double x;
    if (amp) *amp = 0;
    k = p; p = amp ? amp + 1 : NULL;
    if (!(eq = strchr(k, '='))) continue;
    *eq = 0; v = eq + 1;
    x = strtod(v, NULL);
    if (!(x == x)) x = 0;
    if (!strcmp(k, "kind")) sp.kind = (u8)(x >= 0 && x < SH_N ? x : SH_CUSTOM);
    else if (!strcmp(k, "fill")) sp.fill_on = x != 0;
    else if (!strcmp(k, "stroke")) sp.stroke_on = x != 0;
    else if (!strcmp(k, "align")) sp.align = (u8)(x >= 0 && x <= 2 ? x : 0);
    else if (!strcmp(k, "cap")) sp.cap = (u8)(x >= 0 && x <= 2 ? x : 0);
    else if (!strcmp(k, "join")) sp.join = (u8)(x >= 0 && x <= 2 ? x : 0);
    else if (!strcmp(k, "aa")) sp.antialias = x != 0;
    else if (!strcmp(k, "eo")) sp.evenodd = x != 0;
    else if (!strcmp(k, "fc")) sp.fill_color = (u32)x & 0xFFFFFF;
    else if (!strcmp(k, "sc")) sp.stroke_color = (u32)x & 0xFFFFFF;
    else if (!strcmp(k, "fa")) sp.fill_alpha = (u8)(x >= 0 && x <= 255 ? x : 0);
    else if (!strcmp(k, "sa")) sp.stroke_alpha = (u8)(x >= 0 && x <= 255 ? x : 0);
    else if (!strcmp(k, "sw")) sp.stroke_width = x >= 0 && x < 10000 ? x : 1;
    else if (!strcmp(k, "miter")) sp.miter = x;
    else if (!strcmp(k, "dash")) sp.ndash = nums(v, sp.dash, 8);
    else if (!strcmp(k, "doff")) sp.dash_off = x;
    else if (!strcmp(k, "x0")) sp.x0 = x;
    else if (!strcmp(k, "y0")) sp.y0 = x;
    else if (!strcmp(k, "x1")) sp.x1 = x;
    else if (!strcmp(k, "y1")) sp.y1 = x;
    else if (!strcmp(k, "r")) sp.radius = x;
    else if (!strcmp(k, "sides")) sp.sides = (i32)(x >= 3 && x <= 100 ? x : 5);
    else if (!strcmp(k, "star")) sp.star = x != 0;
    else if (!strcmp(k, "indent")) sp.indent = x;
    else if (!strcmp(k, "angle")) sp.angle = x;
    else if (!strcmp(k, "lw")) sp.line_width = x;
    else if (!strcmp(k, "as")) sp.arrow_start = x != 0;
    else if (!strcmp(k, "ae")) sp.arrow_end = x != 0;
    else if (!strcmp(k, "aw")) sp.arrow_w = x;
    else if (!strcmp(k, "al")) sp.arrow_len = x;
    else if (!strcmp(k, "ac")) sp.arrow_concave = x;
    else if (!strcmp(k, "path")) { dec(v); path_free(path); path = path_from_svg(v); }
    else if (!strcmp(k, "types")) { dec(v); types = v; }
  }
  if (path && types) path_set_types(path, types);
  if (path) path->evenodd = sp.evenodd;
  blk = shape_alloc(&sp, path, &len);
  path_free(path);
  if (!blk) return 0;
  free(l->live);
  l->live = blk; l->livelen = len;
  return 1;
}
int live_load(Layer *l, int kind, const char *s)
{
  char *c = str_dup(s ? s : "");
  int ok;
  if (!c) return 0;
  ok = kind == LK_TEXT ? load_text(l, c) : kind == LK_SHAPE ? load_shape(l, c) : 0;
  free(c);
  if (ok) { l->kind = (u8)kind; l->live_ok = 0; }
  return ok;
}

/* ------------------------------------------------------------------ transforms */
static void mat_mul(const double *a, const double *b, double *o)     /* o = a after b */
{
  double r[6];
  r[0] = a[0] * b[0] + a[1] * b[3]; r[1] = a[0] * b[1] + a[1] * b[4]; r[2] = a[0] * b[2] + a[1] * b[5] + a[2];
  r[3] = a[3] * b[0] + a[4] * b[3]; r[4] = a[3] * b[1] + a[4] * b[4]; r[5] = a[3] * b[2] + a[4] * b[5] + a[5];
  memcpy(o, r, sizeof r);
}
static void pt(const double *m, double *x, double *y)
{
  double a = *x, b = *y;
  *x = m[0] * a + m[1] * b + m[2]; *y = m[3] * a + m[4] * b + m[5];
}
static void transform_text(TextParams *tp, const double *m)
{
  double s = 1, X[6], S[6];
  int i;
  int uniform = fabs(m[1]) < 1e-12 && fabs(m[3]) < 1e-12 && m[0] > 0 && fabs(m[0] - m[4]) < 1e-9 * m[0];
  if (uniform && m[0] != 1) {
    /* sizes scaled: the text is laid out again at the new size, as sharp as ever */
    s = m[0];
    tp->x *= s; tp->y *= s; tp->bw *= s; tp->bh *= s;
    tp->leading *= s; tp->indent *= s; tp->space_before *= s; tp->space_after *= s;
    for (i = 0; i < tp->nruns; i++) {
      tp->run[i].size *= s; tp->run[i].baseline *= s;
      if (tp->run[i].size > 2000) tp->run[i].size = 2000;
    }
  }
  /* the rest: X' = M X S^-1 */
  if (tp->xf_on) memcpy(X, tp->xf, sizeof X);
  else { X[0] = X[4] = 1; X[1] = X[2] = X[3] = X[5] = 0; }
  S[0] = S[4] = 1 / s; S[1] = S[2] = S[3] = S[5] = 0;
  mat_mul(X, S, X);
  mat_mul(m, X, X);
  if (fabs(X[0] - 1) < 1e-9 && fabs(X[4] - 1) < 1e-9 && fabs(X[1]) < 1e-9 && fabs(X[3]) < 1e-9) {
    tp->x += X[2]; tp->y += X[5];                          /* (a move only: kept in the position) */
    tp->xf_on = 0;
    tp->xf[0] = tp->xf[4] = 1; tp->xf[1] = tp->xf[2] = tp->xf[3] = tp->xf[5] = 0;
  } else { memcpy(tp->xf, X, sizeof X); tp->xf_on = 1; }
}
static void transform_shape(ShapeParams *sp, const double *m)
{
  PathAnchor *a = (PathAnchor *)((u8 *)sp + sizeof(ShapeParams));
  double s = sqrt(fabs(m[0] * m[4] - m[1] * m[3]));
  int axis = fabs(m[1]) < 1e-12 && fabs(m[3]) < 1e-12, i;
  i32 k;
  for (k = 0; k < sp->na; k++) { pt(m, &a[k].x, &a[k].y); pt(m, &a[k].ix, &a[k].iy); pt(m, &a[k].ox, &a[k].oy); }
  if (m[0] * m[4] - m[1] * m[3] < 0) {
    /* (mirrored: the corner order of generated shapes no longer matters, the path is what counts) */
  }
  sp->stroke_width *= s; sp->line_width *= s; sp->dash_off *= s;
  for (i = 0; i < sp->ndash && i < 8; i++) sp->dash[i] *= s;
  if (axis && (sp->kind != SH_POLYGON || fabs(fabs(m[0]) - fabs(m[4])) < 1e-9) && sp->kind != SH_CUSTOM) {
    double r = sp->radius * sqrt(fabs(m[0] * m[4]));
    pt(m, &sp->x0, &sp->y0); pt(m, &sp->x1, &sp->y1);
    sp->radius = r;
    if (sp->kind == SH_POLYGON && (m[0] < 0) != (m[4] < 0)) sp->kind = SH_CUSTOM;   /* (a mirrored star turns the other way) */
  } else sp->kind = SH_CUSTOM;                             /* (turned or sheared: the generator can't say it; the path can) */
}
void live_transform(Layer *l, const double *m)
{
  if (layer_text(l)) transform_text((TextParams *)l->live, m);
  else if (layer_shape(l)) transform_shape((ShapeParams *)l->live, m);
  else return;
  free(l->livepx); l->livepx = NULL; l->live_ok = 0; l->livebox = box_make(0, 0, 0, 0);
}
void live_remap(Doc *d, const double *m)
{
  int i;
  for (i = 0; i < d->nlay; i++) if (d->lay[i]->live) live_transform(d->lay[i], m);
  for (i = 0; i < d->npaths; i++) path_transform(d->path[i], m);
  if (d->workpath) path_transform(d->workpath, m);
  if (layerx_remap_hook) layerx_remap_hook(d, m);         /* 1.2: vector masks, smart objects, artboards, frames */
}
int doc_live_transform(Doc *d, int i, const double *m)
{
  if (i < 0 || i >= d->nlay || !d->lay[i]->live || d->lay[i]->lock_pos || d->lay[i]->lock_all) return 0;
  doc_begin(d, fabs(m[0] - 1) < 1e-12 && fabs(m[4] - 1) < 1e-12 && !m[1] && !m[3] ? "Move" : "Free Transform");
  doc_note_live(d, i);
  live_transform(d->lay[i], m);
  doc_end(d);
  return 1;
}
