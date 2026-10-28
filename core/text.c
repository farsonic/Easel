/*
 * text.c - the Text tool (see text.h).
 */
#include <string.h>
#include <stdlib.h>
#include "text.h"
#include "ttraster.h"

/* the next character of a UTF-8 string (a bad byte stands for itself) */
static u32 next_cp(const char **p)
{
  const u8 *s = (const u8 *)*p;
  u32 c = *s++;
  if (c >= 0xC0 && c < 0xE0 && (s[0] & 0xC0) == 0x80) { c = ((c & 31) << 6) | (s[0] & 63); s++; }
  else if (c >= 0xE0 && c < 0xF0 && (s[0] & 0xC0) == 0x80 && (s[1] & 0xC0) == 0x80) { c = ((c & 15) << 12) | ((s[0] & 63) << 6) | (s[1] & 63); s += 2; }
  else if (c >= 0xF0 && (s[0] & 0xC0) == 0x80 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) { c = ((c & 7) << 18) | ((s[0] & 63) << 12) | ((s[1] & 63) << 6) | (s[2] & 63); s += 3; }
  *p = (const char *)s;
  return c;
}
/* a glyph's advance in pixels (26.6) at px to the em */
static i32 adv64(Face *f, u16 g, int px)
{
  u16 a = f->adv && f->nglyphs ? f->adv[g < f->nmetrics ? g : (f->nmetrics ? f->nmetrics - 1 : 0)] : 0;
  return f->upm ? (i32)((long)a * px * 64 / f->upm) : 0;
}
/* one line's width in pixels (26.6), from s up to its end or a newline */
static i32 line_w64(Face *f, const char *s, int px)
{
  i32 w = 0;
  while (*s && *s != '\n') w += adv64(f, ttf_glyph(f, next_cp(&s)), px);
  return w;
}
static i32 line_height(Face *f, int px)
{
  long h = f->upm ? (long)(f->ascent + f->descent + f->linegap) * px / f->upm : px;
  return (i32)(h > 0 ? h : px);
}
int text_measure(Face *f, const char *utf8, int px, i32 *w, i32 *h)
{
  const char *s = utf8;
  i32 mw = 0, lines = 1;
  if (!f || !ttf_load(f) || px < 1) return 0;
  for (;;) {
    i32 lw = (line_w64(f, s, px) + 63) >> 6;
    if (lw > mw) mw = lw;
    while (*s && *s != '\n') s++;
    if (!*s) break;
    s++; lines++;
  }
  *w = mw; *h = lines * line_height(f, px);
  return 1;
}
Box text_layer(Doc *d, Face *f, const char *utf8, int px, u32 color, i32 x, i32 y, int align, int antialias)
{
  TTRaster r;
  Layer *l;
  Box drawn = { 0, 0, 0, 0 };
  const char *s = utf8;
  i32 base, lh, line = 0;
  char name[64];
  int i, n;
  if (!f || !ttf_load(f) || px < 1 || !utf8 || !*utf8) return drawn;
  memset(&r, 0, sizeof r);
  if (!ttr_open(&r, f->path, f->index)) return drawn;
  /* the layer's name: the text's first line, cut short */
  for (n = 0; utf8[n] && utf8[n] != '\n' && n < 40; n++) name[n] = utf8[n];
  name[n] = 0;
  while (n > 0 && ((u8)name[n - 1] & 0xC0) == 0x80) name[--n] = 0;     /* (not in the middle of a character) */
  if (n > 0 && (u8)name[n - 1] >= 0xC0) name[--n] = 0;
  if (!(l = layer_new(d, name[0] ? name : "Text"))) { ttr_close(&r); return drawn; }
  lh = line_height(f, px);
  base = y + (f->upm ? (i32)((long)f->ascent * px / f->upm) : px);
  for (;;) {
    i32 pen = (i32)x * 64, lw = line_w64(f, s, px);
    if (align == TA_CENTER) pen -= lw / 2; else if (align == TA_RIGHT) pen -= lw;
    while (*s && *s != '\n') {
      u16 g = ttf_glyph(f, next_cp(&s));
      TTBitmap bm;
      memset(&bm, 0, sizeof bm);
      if (ttr_glyph(&r, g, (i32)px * 64, &bm) && bm.w > 0 && bm.cov) {
        i32 gx = (pen >> 6) + bm.left, gy = base + line * lh - bm.top, xx, yy;
        for (yy = 0; yy < bm.h; yy++) {
          i32 py = gy + yy;
          if (py < 0 || py >= d->h) continue;
          for (xx = 0; xx < bm.w; xx++) {
            i32 pxx = gx + xx;
            int c = bm.cov[yy * bm.w + xx];
            u8 *p;
            if (pxx < 0 || pxx >= d->w || !c) continue;
            if (!antialias) c = c >= 128 ? 255 : 0;
            if (!c) continue;
            p = l->px + ((long)py * d->w + pxx) * 4;
            if (c > p[3]) { p[0] = (u8)(color >> 16); p[1] = (u8)(color >> 8); p[2] = (u8)color; p[3] = (u8)c; }
            if (box_empty(drawn)) drawn = box_make(pxx, py, pxx + 1, py + 1);
            else drawn = box_union(drawn, box_make(pxx, py, pxx + 1, py + 1));
          }
        }
      }
      free(bm.cov);
      pen += adv64(f, g, px);
    }
    if (!*s) break;
    s++; line++;
  }
  ttr_close(&r);
  if (box_empty(drawn)) { layer_free(l); return drawn; }
  i = d->active + 1;
  doc_add_layer(d, l, i);
  return drawn;
}
