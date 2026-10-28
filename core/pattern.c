/*
 * pattern.c - Easel's patterns (see pattern.h): Define Pattern, the built-in patterns (drawn in
 * code, Easel's own), the list and its files. A set of patterns is an IFF CAT of ILBMs, each with
 * a NAME chunk, written and read with brush.c's ILBM brush code.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pattern.h"
#include "brush.h"

#define MAXPAT 4096
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static void put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

Pattern *pattern_new(const char *name, i32 w, i32 h)
{
  Pattern *p;
  if (w < 1 || h < 1 || w > MAXPAT || h > MAXPAT) return NULL;
  if (!(p = (Pattern *)calloc(1, sizeof *p))) return NULL;
  if (!(p->px = (u8 *)calloc((size_t)w * h, 4))) { free(p); return NULL; }
  p->w = w; p->h = h;
  strncpy(p->name, name ? name : "Pattern", sizeof p->name - 1);
  return p;
}
Pattern *pattern_copy(const Pattern *s)
{
  Pattern *p;
  if (!s || !(p = pattern_new(s->name, s->w, s->h))) return NULL;
  memcpy(p->px, s->px, (size_t)s->w * s->h * 4);
  return p;
}
void pattern_free(Pattern *p) { if (p) { free(p->px); free(p); } }
Pattern *pattern_from_rgba(const char *name, const u8 *px, i32 w, i32 h)
{
  Pattern *p = pattern_new(name, w, h);
  if (p) memcpy(p->px, px, (size_t)w * h * 4);
  return p;
}
Pattern *pattern_from_doc(Doc *d, Box b, int merged)
{
  Pattern *p;
  i32 y, x, w, h;
  if (d->sel) b = box_clip(b, d->selbox);
  b = box_clip(b, doc_box(d));
  if (box_empty(b) || d->active < 0 || d->active >= d->nlay) return NULL;
  w = b.x1 - b.x0; h = b.y1 - b.y0;
  if (!(p = pattern_new("Pattern", w, h))) return NULL;
  if (merged || !d->lay[d->active]->px) doc_composite(d, b, p->px, (long)w * 4);
  else for (y = 0; y < h; y++) memcpy(p->px + (long)y * w * 4, d->lay[d->active]->px + ((long)(b.y0 + y) * d->w + b.x0) * 4, (size_t)w * 4);
  if (d->sel)
    for (y = 0; y < h; y++)
      for (x = 0; x < w; x++) {
        u8 *q = p->px + ((long)y * w + x) * 4;
        q[3] = (u8)((q[3] * d->sel[(long)(b.y0 + y) * d->w + b.x0 + x] + 127) / 255);
      }
  return p;
}
Pattern *pattern_builtin(int kind, int n)
{
  static const char *const names[PAT_N] = { "Checkerboard", "Dots", "Bricks", "Weave", "Noise", "Stripes", "Diamonds" };
  Pattern *p;
  i32 x, y;
  if (n < 4) n = 4;
  if (kind < 0 || kind >= PAT_N || !(p = pattern_new(names[kind], n, n))) return NULL;
  for (y = 0; y < n; y++)
    for (x = 0; x < n; x++) {
      u8 *q = p->px + ((long)y * n + x) * 4;
      int v = 255;
      double fx = (x + 0.5) / n, fy = (y + 0.5) / n;
      switch (kind) {
      case PAT_CHECKER: v = ((x * 2 / n) ^ (y * 2 / n)) & 1 ? 64 : 224; break;
      case PAT_DOTS: { double dx = fx - 0.5, dy = fy - 0.5, d = sqrt(dx * dx + dy * dy); v = d < 0.3 ? 40 : d < 0.33 ? 40 + (int)((d - 0.3) / 0.03 * 200) : 240; break; }
      case PAT_BRICKS: {
        i32 row = y * 4 / n, off = (row & 1) ? n / 4 : 0, xx = (x + off) % n;
        v = (y % (n / 4) == 0 || xx % (n / 2) == 0) ? 200 : 150 + (int)(brush_rand01(7, (u32)(row * 8 + xx * 2 / n), 1) * 30);
        q[0] = (u8)(v * 0.85 + 30); q[1] = (u8)(v * 0.45); q[2] = (u8)(v * 0.35); q[3] = 255;
        continue; }
      case PAT_WEAVE: {
        i32 cx = x * 4 / n, cy = y * 4 / n;
        double u = fx * 4 - cx, w = fy * 4 - cy, top = ((cx + cy) & 1) ? sin(M_PI * u) : sin(M_PI * w);
        v = (int)(70 + 150 * top);
        break; }
      case PAT_NOISE: v = (int)(brush_rand01(11, (u32)(y * n + x), 3) * 255); break;
      case PAT_STRIPES: v = (x + y) % (n / 2) < n / 4 ? 60 : 220; break;
      case PAT_DIAMONDS: { double d = fabs(fx - 0.5) + fabs(fy - 0.5); v = d < 0.35 ? 80 : 230; break; }
      }
      q[0] = q[1] = q[2] = (u8)(v < 0 ? 0 : v > 255 ? 255 : v); q[3] = 255;
    }
  return p;
}

/* ------------------------------------------------------------------ the list */
void patlist_init(PatternList *l) { l->p = NULL; l->n = l->cap = 0; }
void patlist_free(PatternList *l)
{
  int i;
  for (i = 0; i < l->n; i++) pattern_free(l->p[i]);
  free(l->p);
  patlist_init(l);
}
int patlist_add(PatternList *l, Pattern *p)
{
  if (!p) return 0;
  if (l->n == l->cap) {
    int nc = l->cap ? l->cap * 2 : 16;
    Pattern **np = (Pattern **)realloc(l->p, (size_t)nc * sizeof *np);
    if (!np) { pattern_free(p); return 0; }
    l->p = np; l->cap = nc;
  }
  l->p[l->n++] = p;
  return 1;
}
void patlist_remove(PatternList *l, int i)
{
  if (i < 0 || i >= l->n) return;
  pattern_free(l->p[i]);
  memmove(&l->p[i], &l->p[i + 1], (size_t)(l->n - i - 1) * sizeof l->p[0]);
  l->n--;
}
int patlist_builtin(PatternList *l)
{
  int k, n = 0;
  for (k = 0; k < PAT_N; k++) n += patlist_add(l, pattern_builtin(k, 32));
  return n;
}

/* ------------------------------------------------------------------ files */
u8 *patlist_write(const PatternList *l, size_t *len)
{
  Buf b;
  int i;
  buf_init(&b);
  buf_add(&b, "CAT \0\0\0\0ILBM", 12);
  for (i = 0; i < l->n; i++) {
    size_t n;
    u8 *f = ilbm_brush_encode(l->p[i]->px, l->p[i]->w, l->p[i]->h, 0, 0, l->p[i]->name, &n);
    if (!f) { buf_free(&b); return NULL; }
    buf_add(&b, f, n);
    if (n & 1) buf_addc(&b, 0);
    free(f);
  }
  if (b.oom) { buf_free(&b); return NULL; }
  put32((u8 *)b.data + 4, (u32)(b.len - 8));
  *len = b.len;
  return (u8 *)buf_take(&b);
}
static int read_one(PatternList *l, const u8 *d, size_t n)
{
  i32 w, h;
  char name[64];
  u8 *px = ilbm_brush_decode(d, n, &w, &h, NULL, NULL, name, sizeof name, NULL, 0);
  Pattern *p;
  if (!px) return 0;
  if (w > MAXPAT || h > MAXPAT || !(p = (Pattern *)calloc(1, sizeof *p))) { free(px); return 0; }
  p->px = px; p->w = w; p->h = h;
  strncpy(p->name, name[0] ? name : "Pattern", sizeof p->name - 1);
  return patlist_add(l, p);
}
int patlist_read(PatternList *l, const u8 *d, size_t n)
{
  size_t o, end;
  int count = 0;
  if (n < 12) return 0;
  if (!memcmp(d, "FORM", 4)) return read_one(l, d, n);
  if (memcmp(d, "CAT ", 4)) return 0;
  end = be32(d + 4);
  end = end > n - 8 ? n : end + 8;
  for (o = 12; o + 8 <= end; ) {
    size_t len = be32(d + o + 4), avail = end - o - 8;
    if (len > avail) len = avail;
    if (!memcmp(d + o, "FORM", 4)) count += read_one(l, d + o, len + 8);
    o += 8 + len + (len & 1);
  }
  return count;
}
int patlist_save(const PatternList *l, const char *path)
{
  size_t len;
  u8 *f = patlist_write(l, &len);
  int ok;
  if (!f) return 0;
  ok = file_write(path, f, len);
  free(f);
  return ok;
}
int patlist_load(PatternList *l, const char *path)
{
  size_t len;
  u8 *f = file_read(path, &len);
  int n;
  if (!f) return 0;
  n = patlist_read(l, f, len);
  free(f);
  return n;
}
int pattern_save_ilbm(const Pattern *p, const char *path)
{
  size_t len;
  u8 *f = ilbm_brush_encode(p->px, p->w, p->h, 0, 0, p->name, &len);
  int ok;
  if (!f) return 0;
  ok = file_write(path, f, len);
  free(f);
  return ok;
}
