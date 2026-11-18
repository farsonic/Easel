/*
 * legacy.c - Easel 1.2's Amiga-era extras (legacy.h): DPaint's Shear and Bend, animated brushes, the
 * coder's export (bitplanes, palettes, sprites, copper gradients), overscan sizes and TV safe areas,
 * and frames with onion skins.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include "legacy.h"
#include "paint.h"
#include "guides.h"

/* ------------------------------------------------------------------ Shear and Bend */
CustomBrush *cbrush_shear(const CustomBrush *b, double kx, double ky)
{
  /* the new box: every corner moved */
  double cx[4], cy[4], minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
  CustomBrush *o;
  i32 w, h, x, y;
  int k;
  if (!b || !b->px || fabs(kx) > 10 || fabs(ky) > 10) return NULL;
  for (k = 0; k < 4; k++) {
    double px = (k & 1) ? b->w : 0, py = (k & 2) ? b->h : 0, dx = px - b->hx, dy = py - b->hy;
    cx[k] = b->hx + dx + kx * dy; cy[k] = b->hy + dy + ky * dx;
    if (cx[k] < minx) minx = cx[k];
    if (cx[k] > maxx) maxx = cx[k];
    if (cy[k] < miny) miny = cy[k];
    if (cy[k] > maxy) maxy = cy[k];
  }
  w = (i32)ceil(maxx - minx); h = (i32)ceil(maxy - miny);
  if (w < 1 || h < 1 || w > 4096 || h > 4096 || !(o = cbrush_new(w, h))) return NULL;
  {
    double det = 1 - kx * ky;
    if (fabs(det) < 1e-6) { cbrush_free(o); return NULL; }
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
      /* back: (dx', dy') = (dx + kx dy, dy + ky dx) solved for dx, dy */
      double qx = x + 0.5 + minx - b->hx, qy = y + 0.5 + miny - b->hy;
      double dx = (qx - kx * qy) / det, dy = (qy - ky * qx) / det;
      i32 sx = (i32)floor(dx + b->hx), sy = (i32)floor(dy + b->hy);
      if (sx >= 0 && sy >= 0 && sx < b->w && sy < b->h) memcpy(o->px + ((long)y * w + x) * 4, b->px + ((long)sy * b->w + sx) * 4, 4);
    }
  }
  o->hx = (i32)floor(b->hx - minx); o->hy = (i32)floor(b->hy - miny);
  if (o->hx >= w) o->hx = w - 1;
  if (o->hy >= h) o->hy = h - 1;
  return o;
}
CustomBrush *cbrush_bend(const CustomBrush *b, double amount, int vertical)
{
  CustomBrush *o;
  i32 w, h, x, y, ext = (i32)ceil(fabs(amount));
  if (!b || !b->px || fabs(amount) > 2048) return NULL;
  w = vertical ? b->w : b->w + ext; h = vertical ? b->h + ext : b->h;
  if (!(o = cbrush_new(w, h))) return NULL;
  for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
    /* rows (or columns) moved along a parabola: none in the middle, amount at the ends */
    double t, off;
    i32 sx, sy;
    if (vertical) {
      t = b->w > 1 ? (x - (b->w - 1) / 2.0) / ((b->w - 1) / 2.0) : 0;
      off = amount * t * t + (amount < 0 ? ext : 0);
      sx = x; sy = (i32)floor(y - off + 0.5);
    } else {
      t = b->h > 1 ? (y - (b->h - 1) / 2.0) / ((b->h - 1) / 2.0) : 0;
      off = amount * t * t + (amount < 0 ? ext : 0);
      sx = (i32)floor(x - off + 0.5); sy = y;
    }
    if (sx >= 0 && sy >= 0 && sx < b->w && sy < b->h) memcpy(o->px + ((long)y * w + x) * 4, b->px + ((long)sy * b->w + sx) * 4, 4);
  }
  o->hx = vertical ? b->hx : b->hx + (amount < 0 ? ext : 0); o->hy = vertical ? b->hy + (amount < 0 ? ext : 0) : b->hy;
  return o;
}

/* ------------------------------------------------------------------ animated brushes */
const CustomBrush *animbrush_frame(const AnimBrush *a, long dab)
{
  int st = a->step > 0 ? a->step : 1;
  if (!a || a->n < 1) return NULL;
  return a->frame[(dab / st) % a->n];
}
int animbrush_pickup(Doc *d, Box b, int key_bg, u32 key, CustomBrush **out, int max)
{
  int i, n = 0, act = d->active;
  for (i = 0; i < d->nlay && n < max; i++) {
    Layer *l = d->lay[i];
    CustomBrush *c;
    if (l->depth || l->kind != LK_PIXEL || !l->px) continue;
    d->active = i;
    if ((c = cbrush_pickup(d, b, 0, 0, key_bg, key)) != NULL) out[n++] = c;
  }
  d->active = act;
  return n;
}
Box animbrush_stroke(Doc *d, const Brush *b0, const AnimBrush *a, const double *xy, int n)
{
  Box all = box_make(0, 0, 0, 0), ch;
  Brush b = *b0;
  int k;
  if (!a || a->n < 1 || n < 1) return all;
  doc_begin(d, "AnimBrush");
  for (k = 0; k < n; k++) {
    Stroke *s;
    b.custom = animbrush_frame(a, k);
    if (b.tool != PT_BRUSH && b.tool != PT_PENCIL) b.tool = PT_BRUSH;
    if (!(s = stroke_begin(d, &b, d->fg, xy[k * 2], xy[k * 2 + 1], &ch))) break;
    all = box_union(all, ch);
    stroke_end(s);
  }
  doc_end(d);
  return all;
}

/* ------------------------------------------------------------------ the coder's export */
static void pw16(u8 *p, u32 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void pw32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
u8 *bitplanes_raw(const u8 *idx, i32 w, i32 h, int depth, const u8 *pal, int npal, int flags, size_t *len)
{
  size_t rowb = (size_t)((w + 15) / 16) * 2, planes = rowb * h * depth, palb = 0, at;
  u8 *o;
  i32 x, y;
  int p, k;
  if (!idx || w < 1 || h < 1 || depth < 1 || depth > 8) return NULL;
  if (npal > (1 << depth)) npal = 1 << depth;
  if (flags & BP_PALETTE4) palb = (size_t)npal * 2;
  else if (flags & BP_PALETTE32) palb = 4 + (size_t)npal * 12 + 4;
  if (!(o = (u8 *)calloc(planes + palb + 1, 1))) return NULL;
  for (y = 0; y < h; y++)
    for (p = 0; p < depth; p++) {
      u8 *row = o + ((flags & BP_INTERLEAVED) ? ((size_t)y * depth + p) * rowb : ((size_t)p * h + y) * rowb);
      for (x = 0; x < w; x++) if ((idx[(long)y * w + x] >> p) & 1) row[x >> 3] |= (u8)(0x80 >> (x & 7));
    }
  at = planes;
  if (pal && (flags & BP_PALETTE4))
    for (k = 0; k < npal; k++, at += 2) pw16(o + at, (u32)(pal[k * 3] >> 4) << 8 | (u32)(pal[k * 3 + 1] >> 4) << 4 | (pal[k * 3 + 2] >> 4));
  else if (pal && (flags & BP_PALETTE32)) {
    /* LoadRGB32: count and first colour, then each gun left-justified in 32 bits, then 0 */
    pw32(o + at, (u32)npal << 16); at += 4;
    for (k = 0; k < npal; k++) { int c; for (c = 0; c < 3; c++, at += 4) pw32(o + at, (u32)pal[k * 3 + c] * 0x01010101UL); }
    pw32(o + at, 0); at += 4;
  }
  *len = planes + palb;
  return o;
}
/* a growing text */
typedef struct { char *s; size_t n, cap; int bad; } Txt;
static void tadd(Txt *t, const char *fmt, ...)
{
  va_list ap;
  char tmp[256];
  int k;
  va_start(ap, fmt);
  k = vsprintf(tmp, fmt, ap);
  va_end(ap);
  if (t->bad || k < 0) return;
  if (t->n + (size_t)k + 1 > t->cap) {
    size_t c = t->cap ? t->cap * 2 : 4096;
    char *ns;
    while (c < t->n + (size_t)k + 1) c *= 2;
    if (!(ns = (char *)realloc(t->s, c))) { t->bad = 1; return; }
    t->s = ns; t->cap = c;
  }
  memcpy(t->s + t->n, tmp, (size_t)k + 1); t->n += (size_t)k;
}
static char *tdone(Txt *t) { if (t->bad) { free(t->s); return NULL; } return t->s; }
/* words as source lines of 8 */
static void words(Txt *t, const u8 *b, size_t nb, int asm_)
{
  size_t i;
  for (i = 0; i + 1 < nb; i += 2) {
    u32 v = (u32)b[i] << 8 | b[i + 1];
    if (asm_) tadd(t, "%s$%04lx", i % 16 == 0 ? "\tdc.w\t" : ",", (unsigned long)v);
    else tadd(t, "%s0x%04lx,", i % 16 == 0 ? "\t" : " ", (unsigned long)v);
    if (i % 16 == 14 || i + 2 >= nb) tadd(t, "\n");
  }
}
char *bitplanes_source(const u8 *idx, i32 w, i32 h, int depth, const u8 *pal, int npal, int flags, int asm_, const char *name)
{
  size_t len, planes = (size_t)((w + 15) / 16) * 2 * h * depth;
  u8 *raw = bitplanes_raw(idx, w, h, depth, pal, npal, flags & ~(BP_PALETTE4 | BP_PALETTE32), &len);
  Txt t;
  int k;
  if (!raw) return NULL;
  memset(&t, 0, sizeof t);
  if (!name || !*name) name = "picture";
  if (npal > (1 << depth)) npal = 1 << depth;
  if (asm_) {
    tadd(&t, "; %s: %ld x %ld, %d planes%s (Easel)\n\tSECTION %s,DATA_C\n%s:\n", name, (long)w, (long)h, depth, flags & BP_INTERLEAVED ? ", interleaved" : "", name, name);
    words(&t, raw, planes, 1);
    if (pal && npal) {
      tadd(&t, "%s_palette:\n", name);
      for (k = 0; k < npal; k++) {
        tadd(&t, "%s$%03lx", k % 8 == 0 ? "\tdc.w\t" : ",", (unsigned long)((pal[k * 3] >> 4) << 8 | (pal[k * 3 + 1] >> 4) << 4 | (pal[k * 3 + 2] >> 4)));
        if (k % 8 == 7 || k == npal - 1) tadd(&t, "\n");
      }
    }
  } else {
    tadd(&t, "/* %s: %ld x %ld, %d planes%s (Easel) */\n#include <exec/types.h>\n\n#define %s_WIDTH %ld\n#define %s_HEIGHT %ld\n#define %s_DEPTH %d\n\n", name, (long)w, (long)h, depth, flags & BP_INTERLEAVED ? ", interleaved" : "", name, (long)w, name, (long)h, name, depth);
    tadd(&t, "__chip UWORD %s[%lu] = {\n", name, (unsigned long)(planes / 2));
    words(&t, raw, planes, 0);
    tadd(&t, "};\n");
    if (pal && npal) {
      tadd(&t, "\nUWORD %s_palette[%d] = {\n", name, npal);
      for (k = 0; k < npal; k++) tadd(&t, "%s0x%03lx,%s", k % 8 == 0 ? "\t" : " ", (unsigned long)((pal[k * 3] >> 4) << 8 | (pal[k * 3 + 1] >> 4) << 4 | (pal[k * 3 + 2] >> 4)), k % 8 == 7 || k == npal - 1 ? "\n" : "");
      tadd(&t, "};\n");
    }
  }
  free(raw);
  return tdone(&t);
}
char *sprites_source(const u8 *idx, i32 w, i32 h, int asm_, const char *name, i32 x0, i32 y0)
{
  Txt t;
  i32 s, y, x, ns = (w + 15) / 16;
  if (!idx || w < 1 || h < 1 || h > 255) return NULL;
  memset(&t, 0, sizeof t);
  if (!name || !*name) name = "sprite";
  if (!asm_) tadd(&t, "/* %s: %ld sprite%s 16 x %ld, colours 1-3 (0 is see-through) (Easel) */\n#include <exec/types.h>\n\n", name, (long)ns, ns > 1 ? "s" : "", (long)h);
  for (s = 0; s < ns; s++) {
    u8 cw[4];
    i32 hs = x0 + 128 + s * 16, vs = y0 + 44, ve = vs + h;
    /* the control words: VSTART, HSTART (high 8 bits), VSTOP and the low bits */
    cw[0] = (u8)vs; cw[1] = (u8)(hs >> 1); cw[2] = (u8)ve; cw[3] = (u8)(((vs >> 8) & 1) << 2 | ((ve >> 8) & 1) << 1 | (hs & 1));
    if (asm_) tadd(&t, "%s_%ld:\n\tdc.w\t$%02x%02x,$%02x%02x\n", name, (long)s, cw[0], cw[1], cw[2], cw[3]);
    else tadd(&t, "__chip UWORD %s_%ld[] = {\n\t0x%02x%02x, 0x%02x%02x,\n", name, (long)s, cw[0], cw[1], cw[2], cw[3]);
    for (y = 0; y < h; y++) {
      u32 a = 0, b = 0;
      for (x = 0; x < 16; x++) {
        i32 px = s * 16 + x;
        int v = px < w ? idx[(long)y * w + px] & 3 : 0;
        if (v & 1) a |= 0x8000u >> x;
        if (v & 2) b |= 0x8000u >> x;
      }
      if (asm_) tadd(&t, "\tdc.w\t$%04lx,$%04lx\n", (unsigned long)a, (unsigned long)b);
      else tadd(&t, "\t0x%04lx, 0x%04lx,\n", (unsigned long)a, (unsigned long)b);
    }
    if (asm_) tadd(&t, "\tdc.w\t0,0\n");
    else tadd(&t, "\t0, 0\n};\n");
  }
  return tdone(&t);
}
u8 *copper_gradient(const u32 *colors, int n, int y0, int reg, int aga, size_t *len)
{
  /* each line: WAIT (y << 8 | 0x07, 0xFFFE) then MOVE colour; AGA: the high and low nibbles with
     BPLCON3's LOCT bit; at line 256 the PAL wait for the end of line 255 first */
  size_t cap = (size_t)n * (aga ? 20 : 8) + 16, at = 0;
  u8 *o;
  int k, wrapped = 0;
  if (n < 1 || reg < 0 || reg > 31 || !(o = (u8 *)malloc(cap))) return NULL;
  for (k = 0; k < n; k++) {
    int y = y0 + k;
    u32 c = colors[k];
    u32 hi = ((c >> 20) & 15) << 8 | ((c >> 12) & 15) << 4 | ((c >> 4) & 15);
    if (y > 255 + 256) break;
    if (y >= 256 && !wrapped) { pw16(o + at, 0xFFDF); pw16(o + at + 2, 0xFFFE); at += 4; wrapped = 1; }
    pw16(o + at, (u32)((y & 255) << 8 | 0x07)); pw16(o + at + 2, 0xFFFE); at += 4;
    if (aga) {
      u32 lo = ((c >> 16) & 15) << 8 | ((c >> 8) & 15) << 4 | (c & 15);
      pw16(o + at, 0x106); pw16(o + at + 2, 0x0C00); at += 4;              /* BPLCON3: high nibbles */
      pw16(o + at, (u32)(0x180 + reg * 2)); pw16(o + at + 2, hi); at += 4;
      pw16(o + at, 0x106); pw16(o + at + 2, 0x0E00); at += 4;              /* LOCT: the low nibbles */
      pw16(o + at, (u32)(0x180 + reg * 2)); pw16(o + at + 2, lo); at += 4;
    } else { pw16(o + at, (u32)(0x180 + reg * 2)); pw16(o + at + 2, hi); at += 4; }
    if (at + 24 > cap) break;
  }
  pw16(o + at, 0xFFFF); pw16(o + at + 2, 0xFFFE); at += 4;
  *len = at;
  return o;
}
char *copper_gradient_source(const u32 *colors, int n, int y0, int reg, int aga, int asm_, const char *name)
{
  size_t len;
  u8 *cl = copper_gradient(colors, n, y0, reg, aga, &len);
  Txt t;
  if (!cl) return NULL;
  memset(&t, 0, sizeof t);
  if (!name || !*name) name = "copper";
  if (asm_) { tadd(&t, "; %s: a copper list setting COLOR%02d on %d lines from %d (Easel)\n\tSECTION %s,DATA_C\n%s:\n", name, reg, n, y0, name, name); words(&t, cl, len, 1); }
  else { tadd(&t, "/* %s: a copper list setting COLOR%02d on %d lines from %d (Easel) */\n#include <exec/types.h>\n\n__chip UWORD %s[%lu] = {\n", name, reg, n, y0, name, (unsigned long)(len / 2)); words(&t, cl, len, 0); tadd(&t, "};\n"); }
  free(cl);
  return tdone(&t);
}

/* ------------------------------------------------------------------ overscan and safe areas */
const ScreenSize overscan_sizes[] = {
  { "PAL Lores Overscan", 352, 283, 26, 22 }, { "PAL Lores Max Overscan", 368, 290, 26, 22 },
  { "PAL Hires Overscan", 704, 283, 13, 22 }, { "PAL Hires Laced Overscan", 704, 566, 26, 22 },
  { "PAL Super-Hires Laced Overscan", 1408, 566, 13, 22 },
  { "NTSC Lores Overscan", 352, 241, 22, 22 }, { "NTSC Lores Max Overscan", 368, 241, 22, 22 },
  { "NTSC Hires Overscan", 704, 241, 11, 22 }, { "NTSC Hires Laced Overscan", 704, 482, 22, 22 },
  { "NTSC Super-Hires Laced Overscan", 1408, 482, 11, 22 }
};
const int noverscan_sizes = (int)(sizeof overscan_sizes / sizeof overscan_sizes[0]);
void safe_areas(i32 w, i32 h, Box *action, Box *title)
{
  *action = box_make((i32)floor(w * 0.05 + 0.5), (i32)floor(h * 0.05 + 0.5), (i32)floor(w * 0.95 + 0.5), (i32)floor(h * 0.95 + 0.5));
  *title = box_make((i32)floor(w * 0.10 + 0.5), (i32)floor(h * 0.10 + 0.5), (i32)floor(w * 0.90 + 0.5), (i32)floor(h * 0.90 + 0.5));
}
int guides_safe_areas(Doc *d)
{
  Box a, t;
  int n = 0;
  i32 v[4], hh[4], k, j;
  safe_areas(d->w, d->h, &a, &t);
  v[0] = a.x0; v[1] = a.x1; v[2] = t.x0; v[3] = t.x1; hh[0] = a.y0; hh[1] = a.y1; hh[2] = t.y0; hh[3] = t.y1;
  for (k = 0; k < 4; k++) {
    int have = 0;
    for (j = 0; j < d->guides.nv; j++) if (d->guides.v[j] == v[k]) have = 1;
    if (!have && d->guides.nv < MAXGUIDES) { d->guides.v[d->guides.nv++] = v[k]; n++; }
    have = 0;
    for (j = 0; j < d->guides.nh; j++) if (d->guides.h[j] == hh[k]) have = 1;
    if (!have && d->guides.nh < MAXGUIDES) { d->guides.h[d->guides.nh++] = hh[k]; n++; }
  }
  d->guides.show_guides = 1;
  return n;
}

/* ------------------------------------------------------------------ frames */
int frames_list(const Doc *d, int *out, int max)
{
  int i, n = 0;
  for (i = 0; i < d->nlay && n < max; i++) if (!d->lay[i]->depth) out[n++] = i;
  return n;
}
int frame_time(const char *name)
{
  const char *p = name ? strrchr(name, '(') : NULL;
  double v;
  char *e;
  if (!p) return 0;
  v = strtod(p + 1, &e);
  if (e == p + 1 || !(v > 0)) return 0;
  while (*e == ' ') e++;
  if (!strncmp(e, "ms", 2)) return (int)floor(v / 10 + 0.5);
  if (*e == 's') return (int)floor(v * 100 + 0.5);
  return 0;
}
void frame_set_time(const char *name, int cs, char *out, size_t cap)
{
  const char *p = strrchr(name, '(');
  size_t keep = p && frame_time(name) ? (size_t)(p - name) : strlen(name);
  while (keep && name[keep - 1] == ' ') keep--;
  if (keep > cap - 16) keep = cap - 16;
  memcpy(out, name, keep);
  if (cs > 0) snprintf(out + keep, cap - keep, " (%d.%02d s)", cs / 100, cs % 100);
  else out[keep] = 0;
}
/* a top-level block's range: its layers lo..i */
static int block_lo(const Doc *d, int i) { int s = i; while (s > 0 && d->lay[s - 1]->depth > d->lay[i]->depth) s--; return s; }
void frames_onion(Doc *d, int k, int before, int after, int opacity, Box r, u8 *out, long stride)
{
  int f[MAXLAYERS], n = frames_list(d, f, MAXLAYERS), j;
  i32 w = r.x1 - r.x0, y, x;
  u8 *t;
  r = box_clip(r, doc_box(d));
  w = r.x1 - r.x0;
  if (box_empty(r) || k < 0 || k >= n) return;
  for (y = 0; y < r.y1 - r.y0; y++) memset(out + (long)y * stride, 0, (size_t)w * 4);
  if (!(t = (u8 *)malloc((size_t)w * (r.y1 - r.y0) * 4))) return;
  /* the frame itself, then the skins over it, the farthest first */
  for (j = -1; j <= (before > after ? before : after); j++) {
    int side;
    for (side = -1; side <= 1; side += 2) {
      int q, op, vis;
      Layer *l;
      int jj = j < 0 ? 0 : (before > after ? before : after) + 1 - j;
      if (j == 0) break;
      if (j < 0 && side == 1) continue;
      q = k + side * jj;
      if (q < 0 || q >= n || (side < 0 && jj > before) || (side > 0 && jj > after)) continue;
      l = d->lay[f[q]];
      op = jj == 0 ? 255 : (opacity < 1 ? 1 : opacity > 100 ? 100 : opacity) * 255 / 100 / jj;
      vis = l->visible; l->visible = 1;
      doc_composite_range(d, block_lo(d, f[q]), f[q] + 1, 0, r, t, (long)w * 4, 0);
      l->visible = (u8)vis;
      for (y = 0; y < r.y1 - r.y0; y++) for (x = 0; x < w; x++) {
        const u8 *s = t + ((long)y * w + x) * 4;
        if (s[3]) blend_pixel(out + (long)y * stride + x * 4, s, op, BM_NORMAL);
      }
    }
  }
  free(t);
}
void frames_show(Doc *d, int k)
{
  int f[MAXLAYERS], n = frames_list(d, f, MAXLAYERS), j;
  for (j = 0; j < n; j++) d->lay[f[j]]->visible = (u8)(j == k);
}
