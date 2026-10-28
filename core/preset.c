/*
 * preset.c - brush presets (see preset.h): the list, Easel's built-in presets, Define Brush Preset,
 * the text file and Photoshop .abr files.
 *
 * Parts ported from PhotoCraft (https://github.com/storytold/photocraft), Copyright (c) 2026
 * ArtCraft Team and the PhotoCraft contributors, used under the MIT License (see Licenses.txt):
 * the built-in presets (crates/paint/src/presets.rs), picked_over (crates/paint/src/brush.rs) and
 * the .abr reader (crates/psd/src/abr.rs: the v1/v2 brush records, the v6+ sections and samp
 * entries, the sample bounds, depth and PackBits rows).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include "preset.h"

void presets_init(PresetList *l) { l->p = NULL; l->n = l->cap = 0; }
void presets_free(PresetList *l)
{
  int i;
  for (i = 0; i < l->n; i++) tip_free(l->p[i].tip);
  free(l->p);
  presets_init(l);
}
int presets_add(PresetList *l, const char *name, const char *group, const Brush *b, const Tip *tip)
{
  BrushPreset *p;
  Tip *t = NULL;
  if (tip && !(t = tip_copy(tip))) return 0;
  if (l->n == l->cap) {
    int nc = l->cap ? l->cap * 2 : 32, i;
    BrushPreset *np = (BrushPreset *)realloc(l->p, (size_t)nc * sizeof *np);
    if (!np) { tip_free(t); return 0; }
    l->p = np; l->cap = nc;
    for (i = 0; i < l->n; i++) l->p[i].b.tip = l->p[i].tip;    /* (the brushes point at their own tips) */
  }
  p = &l->p[l->n++];
  memset(p, 0, sizeof *p);
  strncpy(p->name, name ? name : "Brush", sizeof p->name - 1);
  strncpy(p->group, group ? group : "", sizeof p->group - 1);
  p->b = *b;
  p->tip = t;
  p->b.tip = t; p->b.custom = NULL; p->b.pattern = NULL; p->b.snapshot = NULL;
  return 1;
}
void presets_remove(PresetList *l, int i)
{
  int k;
  if (i < 0 || i >= l->n) return;
  tip_free(l->p[i].tip);
  memmove(&l->p[i], &l->p[i + 1], (size_t)(l->n - i - 1) * sizeof l->p[0]);
  l->n--;
  for (k = 0; k < l->n; k++) l->p[k].b.tip = l->p[k].tip;
}
Brush preset_pick(const BrushPreset *p, const Brush *cur)
{
  Brush b = p->b;
  b.tool = cur->tool;
  b.clone_dx = cur->clone_dx; b.clone_dy = cur->clone_dy; b.strength = cur->strength;
  b.custom = cur->custom; b.custom_mode = cur->custom_mode; b.bg = cur->bg;
  b.eraser_mode = cur->eraser_mode; b.auto_erase = cur->auto_erase; b.sample_all = cur->sample_all;
  b.pattern = cur->pattern; b.pattern_aligned = cur->pattern_aligned; b.snapshot = cur->snapshot;
  b.sampling = cur->sampling; b.limits = cur->limits; b.tolerance = cur->tolerance; b.protect_fg = cur->protect_fg; b.replace_mode = cur->replace_mode;
  /* smoothing and symmetry are the tool's (PhotoCraft) */
  b.dyn.smoothing = cur->dyn.smoothing; b.dyn.pulled_string = cur->dyn.pulled_string;
  b.dyn.sym_mode = cur->dyn.sym_mode; b.dyn.sym_segments = cur->dyn.sym_segments; b.dyn.sym_center_set = cur->dyn.sym_center_set;
  b.dyn.sym_angle = cur->dyn.sym_angle; b.dyn.sym_cx = cur->dyn.sym_cx; b.dyn.sym_cy = cur->dyn.sym_cy;
  b.tip = p->tip;
  return b;
}

/* ------------------------------------------------------------------ built-in presets */
static Brush base_brush(int size, int hardness)
{
  Brush b;
  memset(&b, 0, sizeof b);
  b.tool = PT_BRUSH; b.size = size; b.hardness = hardness; b.opacity = 100; b.flow = 100; b.spacing = 25; b.strength = 50;
  b.bg = 0xFFFFFF;
  return b;
}
int presets_builtin(PresetList *l)
{
  int n0 = l->n;
  Brush b;
  Tip *t;
  /* General (PhotoCraft's set) */
  b = base_brush(30, 100); b.spacing = 10; presets_add(l, "Hard Round", "General", &b, NULL);
  b = base_brush(45, 0); presets_add(l, "Soft Round", "General", &b, NULL);
  b = base_brush(5, 100); b.spacing = 10; presets_add(l, "Hard Round Fine", "General", &b, NULL);
  b = base_brush(300, 0); b.spacing = 15; presets_add(l, "Soft Round Large", "General", &b, NULL);
  b = base_brush(30, 100); b.spacing = 10; b.dyn.shape_on = 1; b.dyn.size.control = CTL_PRESSURE; presets_add(l, "Hard Round Pressure Size", "General", &b, NULL);
  b = base_brush(45, 0); b.dyn.shape_on = 1; b.dyn.size.control = CTL_PRESSURE; presets_add(l, "Soft Round Pressure Size", "General", &b, NULL);
  b = base_brush(30, 100); b.spacing = 10; b.dyn.transfer_on = 1; b.dyn.opacity.control = CTL_PRESSURE; presets_add(l, "Hard Round Pressure Opacity", "General", &b, NULL);
  b = base_brush(45, 0); b.dyn.transfer_on = 1; b.dyn.opacity.control = CTL_PRESSURE; presets_add(l, "Soft Round Pressure Opacity", "General", &b, NULL);
  b = base_brush(80, 0); b.flow = 10; b.spacing = 10; b.dyn.buildup = 1; b.dyn.buildup_rate = 25; presets_add(l, "Airbrush Soft", "General", &b, NULL);
  b = base_brush(120, 0); b.flow = 15; b.spacing = 10; b.dyn.buildup = 1; b.dyn.buildup_rate = 30; b.dyn.transfer_on = 1; b.dyn.flow.control = CTL_PRESSURE;
  presets_add(l, "Airbrush Pressure Flow", "General", &b, NULL);
  b = base_brush(28, 100); b.roundness = 20; b.angle = 45; b.spacing = 5; presets_add(l, "Calligraphy Flat", "General", &b, NULL);
  b = base_brush(20, 100); b.spacing = 300; b.dyn.shape_on = 1; b.dyn.size.control = CTL_FADE; b.dyn.size.fade_steps = 20; presets_add(l, "Fading Dots", "General", &b, NULL);
  /* Dry Media */
  b = base_brush(3, 100); b.tool = PT_BRUSH; b.spacing = 10; presets_add(l, "Hard Pencil", "Dry Media", &b, NULL);
  if ((t = tip_procedural(PTIP_CHALK, 64, 7))) {
    b = base_brush(40, 100); b.spacing = 20; b.dyn.shape_on = 1; b.dyn.angle.jitter = 100; b.dyn.size.jitter = 15;
    presets_add(l, "Chalk", "Dry Media", &b, t); tip_free(t);
  }
  if ((t = tip_procedural(PTIP_BRISTLE, 64, 1))) {
    b = base_brush(45, 100); b.spacing = 8; b.dyn.shape_on = 1; b.dyn.angle.control = CTL_DIRECTION; b.dyn.size.control = CTL_PRESSURE; b.dyn.size.minimum = 50;
    presets_add(l, "Bristle", "Wet Media", &b, t); tip_free(t);
  }
  /* Special Effects */
  if ((t = tip_procedural(PTIP_SPATTER, 48, 23))) {
    b = base_brush(50, 100); b.spacing = 40; b.dyn.shape_on = 1; b.dyn.size.jitter = 40; b.dyn.angle.jitter = 100;
    b.dyn.scatter_on = 1; b.dyn.scatter.jitter = 150; b.dyn.both_axes = 1; b.dyn.count = 2;
    presets_add(l, "Spatter", "Special Effects", &b, t); tip_free(t);
  }
  if ((t = tip_procedural(PTIP_STAR, 64, 1))) {
    b = base_brush(40, 100); b.spacing = 120; b.dyn.shape_on = 1; b.dyn.size.jitter = 70; b.dyn.angle.jitter = 25;
    b.dyn.scatter_on = 1; b.dyn.scatter.jitter = 200; b.dyn.both_axes = 1; b.dyn.color_on = 1; b.dyn.bri_jitter = 20;
    presets_add(l, "Sparkle", "Special Effects", &b, t); tip_free(t);
  }
  if ((t = tip_procedural(PTIP_LEAF, 64, 1))) {
    b = base_brush(60, 100); b.spacing = 60; b.dyn.shape_on = 1; b.dyn.size.jitter = 40; b.dyn.angle.control = CTL_DIRECTION; b.dyn.angle.jitter = 20;
    b.dyn.scatter_on = 1; b.dyn.scatter.jitter = 100; b.dyn.color_on = 1; b.dyn.fgbg.jitter = 100; b.dyn.hue_jitter = 5;
    presets_add(l, "Scattered Leaves", "Special Effects", &b, t); tip_free(t);
  }
  if ((t = tip_procedural(PTIP_GRASS, 64, 3))) {
    b = base_brush(70, 100); b.spacing = 25; b.dyn.shape_on = 1; b.dyn.size.jitter = 30; b.dyn.angle.jitter = 6;
    b.dyn.scatter_on = 1; b.dyn.scatter.jitter = 60; b.dyn.color_on = 1; b.dyn.fgbg.jitter = 60;
    presets_add(l, "Grass", "Special Effects", &b, t); tip_free(t);
  }
  /* Amiga: DPaint's built-in shapes */
  b = base_brush(1, 100); b.spacing = 100; presets_add(l, "Single Pixel", "Deluxe Paint", &b, NULL);
  b = base_brush(7, 100); b.square = 1; b.spacing = 15; presets_add(l, "Square", "Deluxe Paint", &b, NULL);
  b = base_brush(9, 100); b.spacing = 15; b.dyn.sym_mode = 0; presets_add(l, "Round", "Deluxe Paint", &b, NULL);
  b = base_brush(12, 0); b.flow = 8; b.spacing = 20; b.dyn.scatter_on = 1; b.dyn.scatter.jitter = 120; b.dyn.both_axes = 1; b.dyn.count = 4; b.dyn.buildup = 1; b.dyn.buildup_rate = 30;
  presets_add(l, "Spray", "Deluxe Paint", &b, NULL);
  return l->n - n0;
}
int presets_define(PresetList *l, Doc *d, Box b, int merged, const char *name, const Brush *base)
{
  Tip *t = tip_from_doc(d, b, merged);
  Brush nb;
  if (!t) return -1;
  nb = *base;
  nb.size = t->w > t->h ? t->w : t->h;
  if (nb.size > 2500) nb.size = 2500;
  nb.angle = 0; nb.roundness = 0; nb.flip_x = nb.flip_y = 0; nb.square = 0;
  if (!presets_add(l, name ? name : "Sampled Brush", "", &nb, t)) { tip_free(t); return -1; }
  tip_free(t);
  return l->n - 1;
}

/* ------------------------------------------------------------------ the text file */
/* every int setting by name and place in the Brush (Dynamic's four come as name.jitter ...) */
#define BF(n) { #n, offsetof(Brush, n) }
#define DF(n) { "dyn." #n, offsetof(Brush, dyn) + offsetof(BrushDynamics, n) }
#define DD(n) { "dyn." #n ".jitter", offsetof(Brush, dyn) + offsetof(BrushDynamics, n) + offsetof(Dynamic, jitter) }, \
              { "dyn." #n ".control", offsetof(Brush, dyn) + offsetof(BrushDynamics, n) + offsetof(Dynamic, control) }, \
              { "dyn." #n ".fade", offsetof(Brush, dyn) + offsetof(BrushDynamics, n) + offsetof(Dynamic, fade_steps) }, \
              { "dyn." #n ".minimum", offsetof(Brush, dyn) + offsetof(BrushDynamics, n) + offsetof(Dynamic, minimum) }
static const struct { const char *name; size_t off; } fields[] = {
  BF(tool), BF(size), BF(hardness), BF(opacity), BF(flow), BF(spacing), BF(blend), BF(strength),
  BF(angle), BF(roundness), BF(flip_x), BF(flip_y), BF(square), BF(custom_mode),
  BF(eraser_mode), BF(auto_erase), BF(sample_all), BF(pattern_aligned), BF(sampling), BF(limits), BF(tolerance), BF(protect_fg), BF(replace_mode),
  DF(shape_on), DD(size), DD(angle), DD(roundness), DF(flip_x_jitter), DF(flip_y_jitter),
  DF(scatter_on), DD(scatter), DF(both_axes), DF(count), DD(count_jitter),
  DF(color_on), DF(per_stroke), DD(fgbg), DF(hue_jitter), DF(sat_jitter), DF(bri_jitter), DF(purity),
  DF(transfer_on), DD(opacity), DD(flow), DF(pressure_size), DF(pressure_opacity),
  DF(buildup), DF(buildup_rate), DF(smoothing), DF(pulled_string),
  DF(sym_mode), DF(sym_segments), DF(sym_center_set), DF(sym_angle)
};
#define NFIELDS (int)(sizeof fields / sizeof fields[0])
static void quote(Buf *b, const char *s)
{
  buf_addc(b, '"');
  for (; *s; s++) buf_addc(b, *s == '"' || *s == '\n' ? '\'' : *s);
  buf_addc(b, '"');
}
char *presets_write(const PresetList *l, size_t *len)
{
  Buf b;
  int i, k;
  buf_init(&b);
  buf_adds(&b, "EaselBrushes 1\n");
  for (i = 0; i < l->n; i++) {
    const BrushPreset *p = &l->p[i];
    const u8 *base = (const u8 *)&p->b;
    buf_adds(&b, "preset "); quote(&b, p->name); buf_addc(&b, ' '); quote(&b, p->group); buf_addc(&b, '\n');
    for (k = 0; k < NFIELDS; k++) {
      int v;
      memcpy(&v, base + fields[k].off, sizeof v);
      if (v) buf_printf(&b, "%s %d\n", fields[k].name, v);
    }
    if (p->b.bg) buf_printf(&b, "bg %06lX\n", (unsigned long)p->b.bg);
    if (p->b.dyn.seed) buf_printf(&b, "dyn.seed %lu\n", (unsigned long)p->b.dyn.seed);
    if (p->b.dyn.sym_center_set) buf_printf(&b, "dyn.sym_cx %.3f\ndyn.sym_cy %.3f\n", p->b.dyn.sym_cx, p->b.dyn.sym_cy);
    if (p->tip) {
      i32 x, y;
      static const char hx[] = "0123456789ABCDEF";
      buf_printf(&b, "tip %ld %ld\n", (long)p->tip->w, (long)p->tip->h);
      for (y = 0; y < p->tip->h; y++) {
        for (x = 0; x < p->tip->w; x++) {
          u8 v = p->tip->a[(long)y * p->tip->w + x];
          buf_addc(&b, hx[v >> 4]); buf_addc(&b, hx[v & 15]);
        }
        buf_addc(&b, '\n');
      }
    }
    buf_adds(&b, "end\n");
  }
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return buf_take(&b);
}
/* the next line (at most cap - 1 bytes kept); 0 at the end */
static int next_line(const char *t, size_t n, size_t *o, char *line, size_t cap)
{
  size_t k = 0;
  if (*o >= n) return 0;
  while (*o < n && t[*o] != '\n') { if (k < cap - 1 && t[*o] != '\r') line[k++] = t[*o]; (*o)++; }
  if (*o < n) (*o)++;
  line[k] = 0;
  return 1;
}
/* a "quoted" word at *p into out, *p moved past it */
static void unquote(const char **p, char *out, size_t cap)
{
  const char *s = *p, *e;
  size_t k;
  out[0] = 0;
  while (*s == ' ') s++;
  if (*s != '"' || !(e = strchr(s + 1, '"'))) { *p = s; return; }
  k = (size_t)(e - s - 1) < cap - 1 ? (size_t)(e - s - 1) : cap - 1;
  memcpy(out, s + 1, k); out[k] = 0;
  *p = e + 1;
}
static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 0; }
int presets_read(PresetList *l, const char *text, size_t n)
{
  char *line;
  size_t o = 0, cap = 2 * 8192 + 64;
  int count = 0, in = 0, k;
  char name[64], group[32];
  Brush b;
  Tip *tip = NULL;
  if (!(line = (char *)malloc(cap))) return 0;
  if (!next_line(text, n, &o, line, cap) || strncmp(line, "EaselBrushes", 12)) { free(line); return 0; }
  memset(&b, 0, sizeof b);
  while (next_line(text, n, &o, line, cap)) {
    if (!strncmp(line, "preset", 6)) {
      const char *p = line + 6;
      unquote(&p, name, sizeof name); unquote(&p, group, sizeof group);
      memset(&b, 0, sizeof b);
      tip_free(tip); tip = NULL;
      in = 1;
    } else if (!in) continue;
    else if (!strncmp(line, "end", 3)) {
      if (presets_add(l, name, group, &b, tip)) count++;
      tip_free(tip); tip = NULL;
      in = 0;
    } else if (!strncmp(line, "tip ", 4)) {
      long w = 0, h = 0;
      i32 y, x;
      sscanf(line + 4, "%ld %ld", &w, &h);
      tip_free(tip);
      if (!(tip = tip_new((i32)w, (i32)h))) continue;
      for (y = 0; y < tip->h && next_line(text, n, &o, line, cap); y++)
        for (x = 0; x < tip->w && line[x * 2] && line[x * 2 + 1]; x++) tip->a[(long)y * tip->w + x] = (u8)(hexv(line[x * 2]) * 16 + hexv(line[x * 2 + 1]));
    } else if (!strncmp(line, "bg ", 3)) b.bg = (u32)strtoul(line + 3, NULL, 16) & 0xFFFFFF;
    else if (!strncmp(line, "dyn.seed ", 9)) b.dyn.seed = (u32)strtoul(line + 9, NULL, 10);
    else if (!strncmp(line, "dyn.sym_cx ", 11)) b.dyn.sym_cx = atof(line + 11);
    else if (!strncmp(line, "dyn.sym_cy ", 11)) b.dyn.sym_cy = atof(line + 11);
    else {
      char *sp = strchr(line, ' ');
      if (!sp) continue;
      *sp = 0;
      for (k = 0; k < NFIELDS; k++)
        if (!strcmp(line, fields[k].name)) { int v = atoi(sp + 1); memcpy((u8 *)&b + fields[k].off, &v, sizeof v); break; }
    }
  }
  tip_free(tip);
  free(line);
  return count;
}
int presets_save(const PresetList *l, const char *path)
{
  size_t len;
  char *t = presets_write(l, &len);
  int ok;
  if (!t) return 0;
  ok = file_write(path, t, len);
  free(t);
  return ok;
}
int presets_load(PresetList *l, const char *path)
{
  size_t len;
  u8 *t = file_read(path, &len);
  int n;
  if (!t) return 0;
  n = presets_read(l, (const char *)t, len);
  free(t);
  return n;
}

/* ------------------------------------------------------------------ Photoshop .abr */
typedef struct { const u8 *b; size_t n, p; int bad; } Rd;
static u32 rd8(Rd *r) { if (r->p + 1 > r->n) { r->bad = 1; return 0; } return r->b[r->p++]; }
static u32 rd16(Rd *r) { u32 v; if (r->p + 2 > r->n) { r->bad = 1; r->p = r->n; return 0; } v = ((u32)r->b[r->p] << 8) | r->b[r->p + 1]; r->p += 2; return v; }
static u32 rd32(Rd *r) { u32 v; if (r->p + 4 > r->n) { r->bad = 1; r->p = r->n; return 0; } v = ((u32)r->b[r->p] << 24) | ((u32)r->b[r->p + 1] << 16) | ((u32)r->b[r->p + 2] << 8) | r->b[r->p + 3]; r->p += 4; return v; }
static void skip(Rd *r, size_t k) { if (r->n - r->p < k) { r->bad = 1; r->p = r->n; } else r->p += k; }
static void set_err(char *err, size_t cap, const char *m) { if (err && cap) { strncpy(err, m, cap - 1); err[cap - 1] = 0; } }
/* a sampled tip: bounds, depth, compression, then its rows (8 or 16 bits: the high byte kept) */
static Tip *read_sample(Rd *r)
{
  i32 top = (i32)rd32(r), left = (i32)rd32(r), bottom = (i32)rd32(r), right = (i32)rd32(r), w, h, y;
  int depth = (int)rd16(r), comp = (int)rd8(r), bpp;
  Tip *t;
  long rowb;
  if (r->bad) return NULL;
  w = right - left; h = bottom - top;
  if (w < 1 || h < 1 || w > 8192 || h > 8192 || (depth != 8 && depth != 16) || comp > 1) return NULL;
  bpp = depth / 8; rowb = (long)w * bpp;
  if (!(t = tip_new(w, h))) return NULL;
  if (comp == 0) {
    if ((size_t)(rowb * h) > r->n - r->p) { tip_free(t); return NULL; }
    for (y = 0; y < h; y++) {
      i32 x;
      for (x = 0; x < w; x++) t->a[(long)y * w + x] = r->b[r->p + (size_t)(y * rowb + x * bpp)];
    }
    r->p += (size_t)(rowb * h);
  } else {
    /* PackBits: a table of each row's packed length (u16), then the rows */
    size_t tbl = r->p, data = r->p + (size_t)h * 2;
    u8 *row = (u8 *)malloc((size_t)rowb);
    if (data > r->n || !row) { free(row); tip_free(t); return NULL; }
    for (y = 0; y < h; y++) {
      size_t len = ((size_t)r->b[tbl + y * 2] << 8) | r->b[tbl + y * 2 + 1], i = 0;
      long o = 0;
      i32 x;
      if (data + len > r->n) { free(row); tip_free(t); return NULL; }
      while (o < rowb && i < len) {
        int c = r->b[data + i++];
        if (c < 128) {
          long k = c + 1;
          if (i + (size_t)k > len) k = (long)(len - i);
          if (k > rowb - o) k = rowb - o;
          memcpy(row + o, r->b + data + i, (size_t)k); i += (size_t)(c + 1); o += k;
        } else if (c > 128) {
          long k = 257 - c;
          if (i >= len) break;
          if (k > rowb - o) k = rowb - o;
          memset(row + o, r->b[data + i], (size_t)k); i++; o += k;
        }
      }
      if (o < rowb) memset(row + o, 0, (size_t)(rowb - o));
      for (x = 0; x < w; x++) t->a[(long)y * w + x] = row[x * bpp];
      data += len;
    }
    free(row);
    r->p = data;
  }
  return t;
}
static void add_tip(PresetList *l, Tip *t, int spacing, const char *name, const char *group, int *count)
{
  Brush b;
  memset(&b, 0, sizeof b);
  b.tool = PT_BRUSH; b.hardness = 100; b.opacity = 100; b.flow = 100; b.strength = 50; b.bg = 0xFFFFFF;
  b.spacing = spacing > 0 ? spacing : 25;
  b.size = t->w > t->h ? t->w : t->h;
  if (b.size > 2500) b.size = 2500;
  if (presets_add(l, name, group, &b, t)) (*count)++;
}
int presets_read_abr(PresetList *l, const u8 *data, size_t n, const char *group, char *err, size_t errcap)
{
  Rd r;
  u32 version;
  int count = 0;
  char name[64];
  r.b = data; r.n = n; r.p = 0; r.bad = 0;
  version = rd16(&r);
  if (version == 1 || version == 2) {
    u32 nb = rd16(&r), i;
    for (i = 0; i < nb && !r.bad && r.p < r.n; i++) {
      u32 type = rd16(&r), len = rd32(&r);
      Rd b;
      if (r.bad || len > r.n - r.p) break;
      b.b = r.b + r.p; b.n = len; b.p = 0; b.bad = 0;
      r.p += len;
      if (type == 1) {
        Brush br;
        int spacing, diam, round, angle, hard;
        rd32(&b); spacing = (int)rd16(&b); diam = (int)rd16(&b); round = (int)rd16(&b); angle = (int)(i16)rd16(&b); hard = (int)rd16(&b);
        if (b.bad) continue;
        memset(&br, 0, sizeof br);
        br.tool = PT_BRUSH; br.opacity = 100; br.flow = 100; br.strength = 50; br.bg = 0xFFFFFF;
        br.size = diam < 1 ? 1 : diam > 2500 ? 2500 : diam; br.hardness = hard > 100 ? 100 : hard;
        br.roundness = round >= 100 || round < 1 ? 0 : round; br.angle = angle; br.spacing = spacing > 0 ? spacing : 25;
        sprintf(name, "%s %d", group && group[0] ? group : "ABR", (int)i + 1);
        if (presets_add(l, name, group, &br, NULL)) count++;
      } else if (type == 2) {
        int spacing;
        Tip *t;
        rd32(&b); spacing = (int)rd16(&b);
        sprintf(name, "%s %d", group && group[0] ? group : "ABR", (int)i + 1);
        if (version == 2) {
          /* the name: UTF-16 (the Latin-1 part kept) */
          u32 nl = rd32(&b), k;
          size_t q = 0;
          if (nl > 4096) continue;
          for (k = 0; k < nl; k++) {
            u32 c = rd16(&b);
            if (c && q < sizeof name - 1) name[q++] = (char)(c < 256 ? c : '?');
          }
          if (q) name[q] = 0;
        }
        rd8(&b); skip(&b, 8);
        if (b.bad || !(t = read_sample(&b))) continue;
        add_tip(l, t, spacing, name, group, &count);
        tip_free(t);
      }
    }
  } else if (version >= 6 && version <= 10) {
    u32 sub = rd16(&r);
    if (sub != 1 && sub != 2) { set_err(err, errcap, "This brush file's layout isn't known"); return 0; }
    while (!r.bad && r.n - r.p >= 12) {
      u32 len;
      const u8 *key;
      if (memcmp(r.b + r.p, "8BIM", 4)) break;
      key = r.b + r.p + 4; r.p += 8;
      len = rd32(&r);
      if (len > r.n - r.p) len = (u32)(r.n - r.p);
      if (!memcmp(key, "samp", 4)) {
        Rd s;
        s.b = r.b + r.p; s.n = len; s.p = 0; s.bad = 0;
        while (!s.bad && s.n - s.p >= 4) {
          u32 el = rd32(&s), padded;
          Rd e;
          Tip *t;
          if (!el) break;
          padded = (el + 3) & ~3UL;
          if (el > s.n - s.p) break;
          e.b = s.b + s.p; e.n = el; e.p = 0; e.bad = 0;
          s.p += padded > s.n - s.p ? s.n - s.p : padded;
          skip(&e, rd8(&e));
          skip(&e, sub == 1 ? 10 : 264);
          if (e.bad || !(t = read_sample(&e))) continue;
          sprintf(name, "%s %d", group && group[0] ? group : "ABR", count + 1);
          add_tip(l, t, 25, name, group, &count);
          tip_free(t);
        }
      }
      r.p += len;
      /* (sections are padded to 4 bytes; some writers don't) */
      if (r.p < r.n && len % 4 && r.n - r.p >= 4 && memcmp(r.b + r.p, "8BIM", 4)) {
        size_t pad = 4 - len % 4;
        if (r.n - r.p >= pad + 4 && !memcmp(r.b + r.p + pad, "8BIM", 4)) r.p += pad;
      }
    }
  } else { set_err(err, errcap, "Not a brush file this version of Easel knows"); return 0; }
  if (!count) set_err(err, errcap, "The brush file holds no brushes Easel can read");
  return count;
}
int presets_load_abr(PresetList *l, const char *path, char *err, size_t errcap)
{
  size_t len;
  u8 *t = file_read(path, &len);
  char group[32];
  const char *f = path, *p;
  int n;
  size_t k;
  if (!t) { set_err(err, errcap, "The file can't be read"); return 0; }
  for (p = path; *p; p++) if (*p == '/' || *p == ':') f = p + 1;
  for (k = 0; f[k] && f[k] != '.' && k < sizeof group - 1; k++) group[k] = f[k];
  group[k] = 0;
  n = presets_read_abr(l, t, len, group, err, errcap);
  free(t);
  return n;
}
