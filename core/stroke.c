/*
 * stroke.c - Easel's brush strokes (paint.h's stroke_*): from the pointer's path to dabs, and the
 * dabs laid on a layer or a mask.
 *
 * Strokes follow Photoshop's rules: flow builds up dab by dab, but a stroke never covers more than
 * its opacity, however often it crosses itself. For that a stroke keeps, in tiles made as it
 * reaches them, how much it has covered each pixel and what the pixel was before it began.
 *
 * 0.5 adds PhotoCraft's brush model on top: tips with angle, roundness and hardness, sampled
 * tips, Deluxe Paint colour brushes and their modes, the dynamics sections (Shape, Scattering,
 * Color, Transfer), airbrush build-up, smoothing, painting symmetry and pen pressure, and the new
 * tools (Pattern Stamp, History Brush, Color Replacement, Background Eraser, the Eraser's modes,
 * the Pencil's Auto Erase, the Clone Stamp sampling all layers).
 *
 * For the 68040: the dab images are made once and kept in a small cache (keyed by size, angle,
 * roundness and flips, each rounded to what can be seen), so laying a dab is integer work on
 * bytes; the dynamics work in floating point once a dab, never a pixel.
 *
 * What a stroke paints into is a PaintTarget: a layer's RGBA, or any 8-bit mask the canvas's size
 * (a layer mask), with an undo callback.
 *
 * Parts ported from PhotoCraft (https://github.com/storytold/photocraft), Copyright (c) 2026
 * ArtCraft Team and the PhotoCraft contributors, used under the MIT License (see Licenses.txt):
 * the per-dab dynamics and smoothing (crates/paint/src/dynamics.rs), the Background Eraser
 * (bg_erase.rs) and Color Replacement (replace.rs).
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "paint.h"
#include "pattern.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TS 64
#define NCACHE 12
static int mul255(int a, int b) { int t = a * b + 128; return (t + (t >> 8)) >> 8; }
static int clamp255(double v) { return v < 0 ? 0 : v > 255 ? 255 : (int)(v + 0.5); }
static void rgb_of(u32 c, u8 *p) { p[0] = (u8)(c >> 16); p[1] = (u8)(c >> 8); p[2] = (u8)c; }
static int grey_of(const u8 *p) { return (p[0] * 77 + p[1] * 150 + p[2] * 29 + 128) >> 8; }

/* ------------------------------------------------------------------ dab images */
/* a dab ready to lay: coverage (and a colour brush's colours) in a w x h box whose centre, or
   handle, is ox, oy in it: the box goes at floor(cx - ox + 0.5) */
typedef struct {
  i32 w, h; double ox, oy;
  u8 *a, *rgba;                       /* rgba: a custom brush's (owned unless shared) */
  int shared;                         /* rgba points into the custom brush itself */
  int kd, ka, kr, kfx, kfy, used;     /* the key: diameter x4, angle x2, roundness x100, flips */
  unsigned long clock;
} DabImg;

/* the dynamics' random streams (PhotoCraft's rng.rs) */
enum { R_SIZE = 1, R_ANGLE, R_ROUND, R_FLIPX, R_FLIPY, R_SCX, R_SCY, R_COUNT, R_OPACITY, R_FLOW, R_FGBG, R_HUE, R_SAT, R_BRI };

/* one dab's settings once the dynamics have had their say */
typedef struct {
  double cx, cy, diam, angle, round;
  int fx, fy;
  int flow;                           /* 0-255 */
  int ceil;                           /* 0-255: the most this dab may take coverage to */
  u8 col[3];
} DabParams;

/* a dab placed on the canvas (one symmetry copy). Its pixel i, j shows the dab image's pixel
   (a i + b j + e - x0, c i + d j + f - y0): the identity for a dab laid as it is; one of the eight
   turns and mirrors of the pixel grid for a copy that is an exact image of the first */
typedef struct {
  DabImg *di; i32 x0, y0; Box b;
  int ma, mb, mc, md; i32 me, mf;
  i32 hx, hy;                         /* its hotspot pixel (sampling tools) */
  i32 sdx, sdy;                       /* smudge: where it pulls from, relative */
} Place;

struct Stroke {
  Doc *d; PaintTarget t; Brush b; u8 col[3]; u32 color;
  i32 ntx, nty;
  u8 **mask, **orig, **merged;        /* per tile: coverage, the pixels before, the merged picture (clone) */
  DabImg cache[NCACHE]; unsigned long clock;
  Tip *mip[10]; int nmip;             /* a sampled tip and its halvings (mip[0] is b.tip, not owned) */
  double x, y, p;                     /* the brush's place and pressure: the last point walked to */
  double next;                        /* the distance from there to the next dab */
  double px, py;                      /* the dab before (smudge, smear) */
  double ix, iy, ip;                  /* the last point given (smoothing) */
  int smoothing, cmode, finished, coarse;
  long step, ndabs; u32 dabi;
  double dir, dir0; int have_dir;
  u8 scol[3]; int have_scol;          /* Color Dynamics once a stroke */
  u8 *w8; long w8cap; i32 *stk; long stkcap;   /* the sampling tools' weights and flood stack */
  int once_set; u8 once[3];
  i32 pox, poy;                       /* the pattern's origin */
  int cycle;                          /* CB_CYCLE: the step through the colours */
  Place pl[SYM_MAXCOPIES];
  double sox[SYM_MAXCOPIES], soy[SYM_MAXCOPIES], srot[SYM_MAXCOPIES];    /* a dab's symmetry copies */
  double qox[SYM_MAXCOPIES], qoy[SYM_MAXCOPIES], qrot[SYM_MAXCOPIES];    /* and the dab before's */
  int smir[SYM_MAXCOPIES], qmir[SYM_MAXCOPIES];
  u8 gen[SYM_MAXCOPIES];              /* copies that need their own dab image */
};

static void dab_free(DabImg *c)
{
  free(c->a);
  if (!c->shared) free(c->rgba);
  memset(c, 0, sizeof *c);
}

/* the round tip: a disc (an ellipse when roundness < 1, turned by angle) with a soft edge that
   starts at the hardness; aliased: hard; square: every pixel of the box */
static int make_round(DabImg *di, double diam, double angle, double round, int fx, int fy, int hardness, int aliased, int square)
{
  double r = diam / 2.0, inner, ca = cos(angle * M_PI / 180), sa = sin(angle * M_PI / 180);
  int i, j, n = (int)ceil(diam) + 2;
  if (n < 3) n = 3;
  if (!(di->a = (u8 *)calloc((size_t)n * n, 1))) return 0;
  di->w = di->h = n; di->ox = di->oy = n / 2.0;
  inner = aliased ? r : r * hardness / 100.0;
  for (j = 0; j < n; j++)
    for (i = 0; i < n; i++) {
      double a, dx = i + 0.5 - n / 2.0, dy = j + 0.5 - n / 2.0, u, v, dist;
      if (square) { di->a[j * n + i] = (u8)(fabs(dx) <= r && fabs(dy) <= r ? 255 : 0); continue; }
      /* into the tip's own frame: angle is anticlockwise on the screen (y down) */
      u = dx * ca - dy * sa; v = (dx * sa + dy * ca) / round;
      if (fx) u = -u;
      if (fy) v = -v;
      dist = sqrt(u * u + v * v);
      if (aliased) a = diam <= 1 ? (i == n / 2 && j == n / 2) : dist <= r ? 1 : 0;
      else if (dist <= inner) a = 1;
      else if (dist >= r + 0.5) a = 0;
      else if (inner >= r - 1) a = r + 0.5 - dist;           /* a hard edge: smoothed over a pixel */
      else { double t = (dist - inner) / (r - inner); if (t > 1) t = 1; a = 1 - t * t * (3 - 2 * t); }
      if (a < 0) a = 0;
      if (a > 1) a = 1;
      di->a[j * n + i] = (u8)(a * 255 + 0.5);
    }
  return 1;
}
/* a tip halved (box filter) */
static Tip *tip_half(const Tip *t)
{
  i32 w = (t->w + 1) / 2, h = (t->h + 1) / 2, x, y;
  Tip *o = tip_new(w, h);
  if (!o) return NULL;
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      int s = 0, k = 0, dx, dy;
      for (dy = 0; dy < 2; dy++) for (dx = 0; dx < 2; dx++) {
        i32 xx = x * 2 + dx, yy = y * 2 + dy;
        if (xx < t->w && yy < t->h) s += t->a[(long)yy * t->w + xx];
        k++;
      }
      o->a[(long)y * w + x] = (u8)((s + k / 2) / k);
    }
  return o;
}
static int tip_px(const Tip *t, i32 x, i32 y) { return x < 0 || y < 0 || x >= t->w || y >= t->h ? 0 : t->a[(long)y * t->w + x]; }
/* a sampled tip: its larger side the diameter, squashed by roundness, turned, flipped; sampled
   bilinearly from the halving nearest the scale */
static int make_sampled(Stroke *s, DabImg *di, double diam, double angle, double round, int fx, int fy)
{
  const Tip *t = s->mip[0];
  double sc = diam / (t->w > t->h ? t->w : t->h), W, H, ca = cos(angle * M_PI / 180), sa = sin(angle * M_PI / 180), lsc;
  int i, j, n, lv = 0;
  W = t->w * sc; H = t->h * sc * round;
  n = (int)ceil(sqrt(W * W + H * H)) + 2;
  if (n < 3) n = 3;
  /* the halving to sample: a source step of at most 2 pixels */
  while (sc * (1 << lv) < 0.5 && lv < 9) {
    if (lv + 1 >= s->nmip) {
      Tip *h = tip_half(s->mip[lv]);
      if (!h) break;
      s->mip[s->nmip++] = h;
    }
    lv++;
  }
  t = s->mip[lv];
  lsc = sc * (1 << lv);
  if (!(di->a = (u8 *)calloc((size_t)n * n, 1))) return 0;
  di->w = di->h = n; di->ox = di->oy = n / 2.0;
  for (j = 0; j < n; j++)
    for (i = 0; i < n; i++) {
      double dx = i + 0.5 - n / 2.0, dy = j + 0.5 - n / 2.0, u, v, tx, ty, fx0, fy0;
      i32 ix, iy;
      int a00, a10, a01, a11;
      u = dx * ca - dy * sa; v = (dx * sa + dy * ca) / round;
      if (fx) u = -u;
      if (fy) v = -v;
      tx = u / lsc + t->w / 2.0 - 0.5; ty = v / lsc + t->h / 2.0 - 0.5;
      if (tx < -1 || ty < -1 || tx > t->w || ty > t->h) continue;
      ix = (i32)floor(tx); iy = (i32)floor(ty); fx0 = tx - ix; fy0 = ty - iy;
      a00 = tip_px(t, ix, iy); a10 = tip_px(t, ix + 1, iy); a01 = tip_px(t, ix, iy + 1); a11 = tip_px(t, ix + 1, iy + 1);
      di->a[j * n + i] = (u8)clamp255((a00 * (1 - fx0) + a10 * fx0) * (1 - fy0) + (a01 * (1 - fx0) + a11 * fx0) * fy0);
    }
  return 1;
}
/* a DPaint brush, turned by angle (anticlockwise) and flipped: nearest pixel, so no new colours.
   Replace mode: its background painted in the background colour */
static int make_custom(Stroke *s, DabImg *di, double angle, int fx, int fy)
{
  const CustomBrush *cb = s->b.custom;
  int replace = s->b.custom_mode == CB_REPLACE;
  u8 bgc[3];
  i32 w, h, i, j;
  long k, n;
  rgb_of(s->b.bg, bgc);
  if (fabs(angle) < 0.01 && !fx && !fy) {
    w = cb->w; h = cb->h; n = (long)w * h;
    if (!(di->a = (u8 *)malloc((size_t)n))) return 0;
    if (replace) {
      if (!(di->rgba = (u8 *)malloc((size_t)n * 4))) { free(di->a); di->a = NULL; return 0; }
      for (k = 0; k < n; k++) {
        const u8 *q = cb->px + k * 4;
        u8 *o = di->rgba + k * 4;
        if (q[3]) memcpy(o, q, 4); else { o[0] = bgc[0]; o[1] = bgc[1]; o[2] = bgc[2]; }
        o[3] = 255; di->a[k] = 255;
      }
    } else {
      di->rgba = cb->px; di->shared = 1;
      for (k = 0; k < n; k++) di->a[k] = cb->px[k * 4 + 3];
    }
    di->w = w; di->h = h; di->ox = cb->hx + 0.5; di->oy = cb->hy + 0.5;
    return 1;
  } else {
    double ca = cos(angle * M_PI / 180), sa = sin(angle * M_PI / 180), cx = cb->hx + 0.5, cy = cb->hy + 0.5;
    double corners[4][2] = { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } }, minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
    int c;
    for (c = 0; c < 4; c++) {
      double x = (c & 1 ? cb->w : 0) - cx, y = (c & 2 ? cb->h : 0) - cy;
      corners[c][0] = x * ca + y * sa; corners[c][1] = -x * sa + y * ca;
      if (corners[c][0] < minx) minx = corners[c][0];
      if (corners[c][0] > maxx) maxx = corners[c][0];
      if (corners[c][1] < miny) miny = corners[c][1];
      if (corners[c][1] > maxy) maxy = corners[c][1];
    }
    w = (i32)ceil(maxx - minx) + 1; h = (i32)ceil(maxy - miny) + 1;
    n = (long)w * h;
    if (!(di->a = (u8 *)calloc((size_t)n, 1)) || !(di->rgba = (u8 *)calloc((size_t)n, 4))) { free(di->a); di->a = NULL; return 0; }
    di->w = w; di->h = h; di->ox = -minx; di->oy = -miny;
    for (j = 0; j < h; j++)
      for (i = 0; i < w; i++) {
        /* back into the brush: the inverse turn */
        double dx = i + 0.5 + minx, dy = j + 0.5 + miny, u = dx * ca - dy * sa, v = dx * sa + dy * ca;
        i32 bx, by;
        if (fx) u = -u;
        if (fy) v = -v;
        bx = (i32)floor(u + cx); by = (i32)floor(v + cy);
        if (bx < 0 || by < 0 || bx >= cb->w || by >= cb->h) continue;
        k = (long)j * w + i;
        {
          const u8 *q = cb->px + ((long)by * cb->w + bx) * 4;
          u8 *o = di->rgba + k * 4;
          if (replace) {
            if (q[3]) memcpy(o, q, 3); else { o[0] = bgc[0]; o[1] = bgc[1]; o[2] = bgc[2]; }
            o[3] = 255; di->a[k] = 255;
          } else { memcpy(o, q, 4); di->a[k] = q[3]; }
        }
      }
    return 1;
  }
}
/* the dab image for these settings, from the cache or made (NULL if out of memory) */
static DabImg *get_dab(Stroke *s, double diam, double angle, double round, int fx, int fy)
{
  int kd, ka, kr, i, oldest = 0, ok, aliased, square;
  DabImg *c;
  if (s->b.custom) { kd = 0; kr = 100; }
  else {
    if (diam < 0.5) diam = 0.5;
    if (diam > 5000) diam = 5000;
    kd = diam < 8 ? (int)floor(diam * 4 + 0.5) : (int)floor(diam + 0.5) * 4;
    kr = (int)floor(round * 100 + 0.5);
    if (kr < 1) kr = 1;
    if (kr > 100) kr = 100;
  }
  while (angle > 180) angle -= 360;
  while (angle <= -180) angle += 360;
  /* (half degrees; but small dabs with a jittered or steered angle look the same in 2 degree
     steps, and then find their dab in the cache far more often) */
  ka = kd >= 4 * 64 || s->b.custom || !s->coarse ? (int)floor(angle * 2 + 0.5) : (int)floor(angle / 2 + 0.5) * 4;
  if (kr == 100 && !s->b.tip && !s->b.custom && !fx && !fy) ka = 0;     /* a disc looks the same at any angle */
  for (i = 0; i < NCACHE; i++) {
    c = &s->cache[i];
    if (c->used && c->kd == kd && c->ka == ka && c->kr == kr && c->kfx == fx && c->kfy == fy) { c->clock = ++s->clock; return c; }
    if (!c->used) { oldest = i; break; }
    if (c->clock < s->cache[oldest].clock) oldest = i;
  }
  c = &s->cache[oldest];
  if (c->used) dab_free(c);
  /* a big dab is costly to keep: only two of them at a time */
  if (kd > 4 * 300) for (i = 0; i < NCACHE; i++) if (&s->cache[i] != c && s->cache[i].used && s->cache[i].kd > 4 * 300) dab_free(&s->cache[i]);
  aliased = s->b.tool == PT_PENCIL || (s->b.tool == PT_ERASER && s->b.eraser_mode != ER_BRUSH);
  square = s->b.square || (s->b.tool == PT_ERASER && s->b.eraser_mode == ER_BLOCK);
  if (s->b.custom) ok = make_custom(s, c, ka / 2.0, fx, fy);
  else if (s->b.tip && s->mip[0]) ok = make_sampled(s, c, kd / 4.0, ka / 2.0, kr / 100.0, fx, fy);
  else ok = make_round(c, kd / 4.0, ka / 2.0, kr / 100.0, fx, fy, s->b.hardness, aliased, square);
  if (!ok) { memset(c, 0, sizeof *c); return NULL; }
  c->kd = kd; c->ka = ka; c->kr = kr; c->kfx = fx; c->kfy = fy; c->used = 1; c->clock = ++s->clock;
  return c;
}

/* ------------------------------------------------------------------ the target's pixels */
static void note_layer(void *ctx, Box r) { Doc *d = (Doc *)ctx; doc_note_layer(d, d->active, r); }
/* the tile holding pixel x, y made ready: its pixels saved for undo and as the stroke's original */
static int tile_ready(Stroke *s, i32 x, i32 y)
{
  i32 t = (y / TS) * s->ntx + x / TS, x0 = (x / TS) * TS, y0 = (y / TS) * TS, row;
  int bpp = s->t.bpp;
  if (s->mask[t]) return 1;
  if (!(s->mask[t] = (u8 *)calloc(TS * TS, 1))) return 0;
  if (!(s->orig[t] = (u8 *)malloc((size_t)TS * TS * bpp))) { free(s->mask[t]); s->mask[t] = NULL; return 0; }
  if (s->t.note) s->t.note(s->t.ctx, box_make(x0, y0, x0 + TS, y0 + TS));
  for (row = 0; row < TS && y0 + row < s->t.h; row++) {
    i32 n = s->t.w - x0 < TS ? s->t.w - x0 : TS;
    memcpy(s->orig[t] + (long)row * TS * bpp, s->t.px + ((long)(y0 + row) * s->t.w + x0) * bpp, (size_t)n * bpp);
  }
  return 1;
}
/* a pixel as it was before the stroke, as RGBA (into tmp for a mask) */
static const u8 *orig_px(Stroke *s, i32 x, i32 y, u8 *tmp)
{
  i32 t;
  const u8 *p;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x >= s->t.w) x = s->t.w - 1;
  if (y >= s->t.h) y = s->t.h - 1;
  t = (y / TS) * s->ntx + x / TS;
  if (s->orig[t]) p = s->orig[t] + ((y % TS) * TS + x % TS) * s->t.bpp;
  else p = s->t.px + ((long)y * s->t.w + x) * s->t.bpp;
  if (s->t.bpp == 4) return p;
  tmp[0] = tmp[1] = tmp[2] = *p; tmp[3] = 255;
  return tmp;
}
/* a pixel as it is now, as RGBA */
static void cur_px(Stroke *s, i32 x, i32 y, u8 *out)
{
  const u8 *p;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x >= s->t.w) x = s->t.w - 1;
  if (y >= s->t.h) y = s->t.h - 1;
  p = s->t.px + ((long)y * s->t.w + x) * s->t.bpp;
  if (s->t.bpp == 4) memcpy(out, p, 4);
  else { out[0] = out[1] = out[2] = *p; out[3] = 255; }
}
/* the merged picture's pixel (the Clone Stamp sampling all layers), its tile made as reached */
static const u8 *merged_px(Stroke *s, i32 x, i32 y)
{
  i32 t, x0, y0;
  Doc *d = s->d;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x >= d->w) x = d->w - 1;
  if (y >= d->h) y = d->h - 1;
  t = (y / TS) * s->ntx + x / TS;
  if (!s->merged[t]) {
    x0 = (x / TS) * TS; y0 = (y / TS) * TS;
    if (!(s->merged[t] = (u8 *)calloc(TS * TS, 4))) return NULL;
    doc_composite(d, box_clip(box_make(x0, y0, x0 + TS, y0 + TS), doc_box(d)), s->merged[t], TS * 4);
  }
  return s->merged[t] + ((y % TS) * TS + x % TS) * 4;
}
/* src laid on p (straight RGBA) at coverage cov in mode; with alpha lock its transparency stays */
static void put_px(int alpha_lock, u8 *p, const u8 *col, int cov, int blend)
{
  u8 sp[4];
  int a = p[3];
  if (cov <= 0) return;
  sp[0] = col[0]; sp[1] = col[1]; sp[2] = col[2]; sp[3] = (u8)cov;
  blend_pixel(p, sp, 255, blend);
  if (alpha_lock) {
    if (!a) { p[0] = p[1] = p[2] = 0; }
    p[3] = (u8)a;
  }
}

/* ------------------------------------------------------------------ one pixel of a dab */
/* pixel i, j given dab coverage a (and a colour brush's colour src): the stroke's coverage
   there built up, and the tool's work done */
static void paint_px(Stroke *s, const Place *pl, const DabParams *dp, i32 i, i32 j, int a, const u8 *src)
{
  Doc *d = s->d;
  int m, cov, sel, tool = s->b.tool, bpp = s->t.bpp, lock = s->t.alpha_lock;
  i32 t;
  u8 *mp, *tp, p[4], tmp[4], c4[4];
  const u8 *o;
  if (!a || !(sel = sel_at(d, i, j))) return;
  if (!tile_ready(s, i, j)) return;
  t = (j / TS) * s->ntx + i / TS;
  mp = s->mask[t] + (j % TS) * TS + i % TS;
  m = *mp;
  if ((tool >= PT_BLUR && tool <= PT_SPONGE) || (s->b.custom && (s->b.custom_mode == CB_SMEAR || s->b.custom_mode == CB_SHADE))) {
    /* the retouching tools act on each pass */
    cov = mul255(mul255(mul255(a, dp->flow), dp->ceil), sel);
  } else if (s->cmode) {
    /* (transfer: each dab its own ceiling) */
    if (dp->ceil > m) { m = m + mul255(mul255(a, dp->flow), dp->ceil - m); *mp = (u8)m; }
    cov = mul255(*mp, sel);
  } else {
    m = m + mul255(mul255(a, dp->flow), 255 - m);
    if (m > *mp) *mp = (u8)m; else m = *mp;
    cov = mul255(m * s->b.opacity / 100, sel);
  }
  if (cov <= 0) return;
  o = orig_px(s, i, j, tmp);
  tp = s->t.px + ((long)j * s->t.w + i) * bpp;
  if (bpp == 4) memcpy(p, tp, 4); else { p[0] = p[1] = p[2] = *tp; p[3] = 255; }
  if (s->b.custom) {
    switch (s->b.custom_mode) {
    case CB_COLOR: case CB_CYCLE: memcpy(p, o, 4); put_px(lock, p, dp->col, cov, s->b.blend); break;
    case CB_BLEND: memcpy(p, o, 4); put_px(lock, p, src, cov / 2, s->b.blend); break;
    case CB_SMEAR: {
      int k;
      cur_px(s, i - pl->sdx, j - pl->sdy, c4);
      for (k = 0; k < 4; k++) p[k] = (u8)(p[k] + ((c4[k] - p[k]) * cov) / 255);
      break; }
    case CB_SHADE: {
      int k, lighten = s->b.blend == BM_LIGHTEN || s->b.blend == BM_SCREEN || s->b.blend == BM_DODGE, f = cov / 8;
      for (k = 0; k < 3; k++) p[k] = (u8)(lighten ? p[k] + ((255 - p[k]) * f) / 255 : p[k] - (p[k] * f) / 255);
      break; }
    default: memcpy(p, o, 4); put_px(lock, p, src, cov, s->b.blend);    /* matte, replace */
    }
  } else switch (tool) {
  case PT_ERASER:
    memcpy(p, o, 4);
    if (bpp == 1) { u8 bgc[3]; rgb_of(s->b.bg, bgc); put_px(0, p, bgc, cov, BM_NORMAL); }
    else if (!lock) p[3] = (u8)mul255(o[3], 255 - cov);
    break;
  case PT_CLONE: {
    const u8 *c = s->b.sample_all && bpp == 4 ? merged_px(s, i + s->b.clone_dx, j + s->b.clone_dy) : orig_px(s, i + s->b.clone_dx, j + s->b.clone_dy, c4);
    memcpy(p, o, 4);
    if (c) put_px(lock, p, c, mul255(cov, c[3]), BM_NORMAL);
    break; }
  case PT_PATTERN: {
    const Pattern *pt = s->b.pattern;
    const u8 *c = pattern_at(pt, i, j, s->pox, s->poy);
    memcpy(p, o, 4);
    put_px(lock, p, c, mul255(cov, c[3]), s->b.blend);
    break; }
  case PT_HISTORY: {
    const u8 *c = s->b.snapshot->px + ((long)j * s->b.snapshot->w + i) * 4;
    int k;
    for (k = 0; k < 4; k++) p[k] = (u8)(o[k] + ((c[k] - o[k]) * cov + (c[k] > o[k] ? 127 : -127)) / 255);
    if (lock) p[3] = o[3];
    break; }
  case PT_BLUR: case PT_SHARPEN: {
    /* the 3x3 average of what is there now, or away from it */
    int k, sum[4] = { 0, 0, 0, 0 }, dx, dy, f = mul255(cov, s->b.strength * 255 / 100);
    for (dy = -1; dy <= 1; dy++) for (dx = -1; dx <= 1; dx++) {
      cur_px(s, i + dx, j + dy, c4);
      for (k = 0; k < 4; k++) sum[k] += c4[k];
    }
    for (k = 0; k < 4; k++) {
      int avg = sum[k] / 9, v = tool == PT_BLUR ? avg : clamp255(p[k] + (p[k] - avg) * 0.6);
      if (k == 3 && lock) continue;
      p[k] = (u8)(p[k] + ((v - p[k]) * f) / 255);
    }
    break; }
  case PT_SMUDGE: {
    const u8 *q = orig_px(s, i - pl->sdx, j - pl->sdy, c4);
    int k, f = mul255(cov, s->b.strength * 255 / 100);
    for (k = 0; k < 4; k++) p[k] = (u8)(p[k] + ((q[k] - p[k]) * f) / 255);
    break; }
  case PT_DODGE: case PT_BURN: case PT_SPONGE: {
    int k, f = mul255(cov, s->b.strength * 255 / 100);
    if (tool == PT_SPONGE) {
      int g = (p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8;
      for (k = 0; k < 3; k++) p[k] = (u8)(p[k] + ((g - p[k]) * f) / 510);
    } else
      for (k = 0; k < 3; k++) {
        int v = tool == PT_DODGE ? p[k] + ((255 - p[k]) * f) / 400 : p[k] - (p[k] * f) / 400;
        p[k] = (u8)(v < 0 ? 0 : v > 255 ? 255 : v);
      }
    break; }
  case PT_BGERASE:
    memcpy(p, o, 4);
    if (bpp == 1) { u8 bgc[3]; rgb_of(s->b.bg, bgc); put_px(0, p, bgc, cov, BM_NORMAL); }
    else if (!lock) { p[3] = (u8)mul255(o[3], 255 - cov); if (!p[3]) p[0] = p[1] = p[2] = 0; }
    break;
  case PT_REPLACE: {
    static const int modes[4] = { BM_HUE, BM_SATURATION, BM_COLOR, BM_LUMINOSITY };
    int rm = s->b.replace_mode >= 0 && s->b.replace_mode < 4 ? s->b.replace_mode : RPL_COLOR;
    memcpy(p, o, 4);
    put_px(0, p, dp->col, cov, modes[rm]);
    p[3] = o[3];
    break; }
  default:
    memcpy(p, o, 4);
    put_px(lock, p, dp->col, cov, s->b.blend);
  }
  if (bpp == 4) memcpy(tp, p, 4); else *tp = (u8)grey_of(p);
}

/* ------------------------------------------------------------------ the sampling tools' weights */
/* Color Replacement and the Background Eraser: which pixels under the dab match the sampled
   colour (0-255 each, soft over the last quarter of the tolerance unless Find Edges), kept to those
   joined to the hotspot unless Discontiguous. Into s->w8, the dab's box; 0 if there is nothing */
static int sample_weights(Stroke *s, const Place *pl)
{
  i32 bw = pl->b.x1 - pl->b.x0, bh = pl->b.y1 - pl->b.y0, x, y;
  long n = (long)bw * bh, k;
  u8 smp[3], fg[3], tmp[4];
  const u8 *q;
  int tol = s->b.tolerance, soft = s->b.limits != LIM_FINDEDGES;
  if (n <= 0) return 0;
  if (n > s->w8cap) {
    u8 *nw = (u8 *)realloc(s->w8, (size_t)n);
    if (!nw) return 0;
    s->w8 = nw; s->w8cap = n;
  }
  /* what is sampled */
  if (s->b.sampling == SMP_SWATCH) rgb_of(s->b.bg, smp);
  else {
    if (s->b.sampling == SMP_ONCE && s->once_set) memcpy(smp, s->once, 3);
    else {
      q = orig_px(s, pl->hx, pl->hy, tmp);
      if (s->b.tool == PT_BGERASE && !q[3]) return 0;   /* (where it was transparent before the stroke) */
      memcpy(smp, q, 3);
      if (s->b.sampling == SMP_ONCE) { memcpy(s->once, smp, 3); s->once_set = 1; }
    }
  }
  rgb_of(s->color, fg);
  for (y = 0; y < bh; y++)
    for (x = 0; x < bw; x++) {
      int dist, w;
      q = orig_px(s, pl->b.x0 + x, pl->b.y0 + y, tmp);
      k = (long)y * bw + x;
      s->w8[k] = 0;
      if (!q[3]) continue;
      if (s->b.tool == PT_BGERASE && s->b.protect_fg) {
        int df = abs(q[0] - fg[0]);
        if (abs(q[1] - fg[1]) > df) df = abs(q[1] - fg[1]);
        if (abs(q[2] - fg[2]) > df) df = abs(q[2] - fg[2]);
        if (df <= tol) continue;
      }
      dist = abs(q[0] - smp[0]);
      if (abs(q[1] - smp[1]) > dist) dist = abs(q[1] - smp[1]);
      if (abs(q[2] - smp[2]) > dist) dist = abs(q[2] - smp[2]);
      if (dist > tol) continue;
      if (soft && tol > 0) { w = (tol - dist) * 255 * 4 / tol; if (w > 255) w = 255; if (w < 1) w = 1; }
      else w = 255;
      s->w8[k] = (u8)w;
    }
  if (s->b.limits != LIM_DISCONTIGUOUS) {
    /* keep what is 4-joined to the hotspot: marked by setting bit... (a second pass with a stack) */
    i32 sx = pl->hx < pl->b.x0 ? 0 : pl->hx >= pl->b.x1 ? bw - 1 : pl->hx - pl->b.x0;
    i32 sy = pl->hy < pl->b.y0 ? 0 : pl->hy >= pl->b.y1 ? bh - 1 : pl->hy - pl->b.y0;
    u8 *keep;
    long top = 0;
    if (!s->w8[(long)sy * bw + sx]) { memset(s->w8, 0, (size_t)n); return 0; }
    if (!(keep = (u8 *)calloc((size_t)n, 1))) return 0;
    if (n > s->stkcap) {
      i32 *ns = (i32 *)realloc(s->stk, (size_t)n * sizeof(i32));
      if (!ns) { free(keep); return 0; }
      s->stk = ns; s->stkcap = n;
    }
    k = (long)sy * bw + sx;
    keep[k] = 1; s->stk[top++] = (i32)k;
    while (top > 0) {
      long c = s->stk[--top], nb[4];
      int e;
      x = (i32)(c % bw); y = (i32)(c / bw);
      nb[0] = x > 0 ? c - 1 : -1; nb[1] = x < bw - 1 ? c + 1 : -1; nb[2] = y > 0 ? c - bw : -1; nb[3] = y < bh - 1 ? c + bw : -1;
      for (e = 0; e < 4; e++) if (nb[e] >= 0 && !keep[nb[e]] && s->w8[nb[e]]) { keep[nb[e]] = 1; s->stk[top++] = (i32)nb[e]; }
    }
    for (k = 0; k < n; k++) if (!keep[k]) s->w8[k] = 0;
    free(keep);
  }
  return 1;
}

/* ------------------------------------------------------------------ laying a step's dabs */
static int mask_at(const Place *pl, i32 i, i32 j, const u8 **col)
{
  const DabImg *di = pl->di;
  i32 u = pl->ma * i + pl->mb * j + pl->me - pl->x0, v = pl->mc * i + pl->md * j + pl->mf - pl->y0;
  long k;
  if (u < 0 || v < 0 || u >= di->w || v >= di->h) { if (col) *col = NULL; return 0; }
  k = (long)v * di->w + u;
  if (col) *col = di->rgba ? di->rgba + k * 4 : NULL;
  return di->a[k];
}
static int in_box(Box b, i32 x, i32 y) { return x >= b.x0 && x < b.x1 && y >= b.y0 && y < b.y1; }
/* the copies of one dab (symmetry) laid together. Where copies overlap, each pixel takes their
   coverage in one order (the largest first), so a mirrored stroke is a pixel-exact mirror */
static void lay_places(Stroke *s, int np, const DabParams *dp, Box *changed)
{
  Box area = s->d->sel ? box_clip(s->d->selbox, doc_box(s->d)) : doc_box(s->d);
  int k, q, sampling = s->b.tool == PT_REPLACE || s->b.tool == PT_BGERASE;
  i32 i, j;
  for (k = 0; k < np; k++) s->pl[k].b = box_clip(s->pl[k].b, area);
  for (k = 0; k < np; k++) {
    Place *pl = &s->pl[k];
    int over = 0;
    if (box_empty(pl->b)) continue;
    s->ndabs++;
    if (changed) *changed = box_union(*changed, pl->b);
    if (sampling) {
      i32 bw = pl->b.x1 - pl->b.x0;
      if (!sample_weights(s, pl)) continue;
      for (j = pl->b.y0; j < pl->b.y1; j++)
        for (i = pl->b.x0; i < pl->b.x1; i++) {
          int w = s->w8[(long)(j - pl->b.y0) * bw + (i - pl->b.x0)];
          if (w) paint_px(s, pl, dp, i, j, mul255(mask_at(pl, i, j, NULL), w), NULL);
        }
      continue;
    }
    for (q = 0; q < np; q++) if (q != k && !box_empty(box_clip(pl->b, s->pl[q].b))) { over = 1; break; }
    if (!over) {
      for (j = pl->b.y0; j < pl->b.y1; j++)
        for (i = pl->b.x0; i < pl->b.x1; i++) {
          const u8 *c;
          int a = mask_at(pl, i, j, &c);
          if (a) paint_px(s, pl, dp, i, j, a, c);
        }
      continue;
    }
    for (j = pl->b.y0; j < pl->b.y1; j++)
      for (i = pl->b.x0; i < pl->b.x1; i++) {
        int n = 0, av[8], ix[8], x, y, skip = 0;
        const u8 *cv[8];
        for (q = 0; q < k; q++) if (in_box(s->pl[q].b, i, j)) { skip = 1; break; }   /* (an earlier copy did it) */
        if (skip) continue;
        for (q = k; q < np && n < 8; q++) {
          const u8 *c;
          int a;
          if (!in_box(s->pl[q].b, i, j)) continue;
          if (!(a = mask_at(&s->pl[q], i, j, &c))) continue;
          /* sorted in: the largest coverage first, then by colour */
          for (x = n; x > 0; x--) {
            int before = av[x - 1] > a || (av[x - 1] == a && (!c || (cv[x - 1] && memcmp(cv[x - 1], c, 4) >= 0)));
            if (before) break;
            av[x] = av[x - 1]; cv[x] = cv[x - 1]; ix[x] = ix[x - 1];
          }
          av[x] = a; cv[x] = c; ix[x] = q; n++;
        }
        for (y = 0; y < n; y++) paint_px(s, &s->pl[ix[y]], dp, i, j, av[y], cv[y]);
      }
  }
}

/* ------------------------------------------------------------------ dynamics */
static double rnd(Stroke *s, u32 index, u32 stream) { return brush_rand01(s->b.dyn.seed, index, stream); }
static double rnds(Stroke *s, u32 index, u32 stream) { return brush_rand01(s->b.dyn.seed, index, stream) * 2 - 1; }
static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
/* a control's value 0-1 (PhotoCraft's control_value) */
static double ctl_value(Stroke *s, const Dynamic *dy, double pressure)
{
  int fs = dy->fade_steps > 0 ? dy->fade_steps : 25;
  switch (dy->control) {
  case CTL_FADE: return 1 - clampd((double)s->step / fs, 0, 1);
  case CTL_PRESSURE: return clampd(pressure, 0, 1);
  default: return 1;
  }
}
/* a 0-1 multiplier: the control takes it from the minimum to 1, jitter lowers it at random,
   never below the minimum (PhotoCraft's scalar) */
static double scalar(Stroke *s, const Dynamic *dy, double pressure, double r)
{
  double mn, v;
  if (dy->jitter <= 0 && dy->control == CTL_OFF) return 1;
  mn = clampd(dy->minimum / 100.0, 0, 1);
  v = (mn + (1 - mn) * ctl_value(s, dy, pressure)) * (1 - clampd(dy->jitter / 100.0, 0, 1) * r);
  return v < mn ? mn : v;
}
/* the angle a control adds, degrees */
static double ctl_angle(Stroke *s, const Dynamic *dy, double pressure)
{
  int fs = dy->fade_steps > 0 ? dy->fade_steps : 25;
  switch (dy->control) {
  case CTL_FADE: return 360 * clampd((double)s->step / fs, 0, 1);
  case CTL_PRESSURE: return 360 * clampd(pressure, 0, 1);
  case CTL_INITIAL: return s->dir0;
  case CTL_DIRECTION: return s->dir;
  default: return 0;
  }
}
/* Color Dynamics for one dab (PhotoCraft's dynamic_color) */
static void dyn_color(Stroke *s, u32 index, double pressure, u8 *out)
{
  const BrushDynamics *dy = &s->b.dyn;
  u8 fg[3], bg[3];
  double base, t, h, sat, v, pu;
  int k, r, g, b;
  rgb_of(s->color, fg); rgb_of(s->b.bg, bg);
  base = dy->fgbg.control != CTL_OFF ? 1 - ctl_value(s, &dy->fgbg, pressure) : 0;
  t = clampd(base + (1 - base) * clampd(dy->fgbg.jitter / 100.0, 0, 1) * rnd(s, index, R_FGBG), 0, 1);
  for (k = 0; k < 3; k++) out[k] = (u8)clamp255(fg[k] + (bg[k] - fg[k]) * t);
  if (!dy->hue_jitter && !dy->sat_jitter && !dy->bri_jitter && !dy->purity) return;
  rgb_to_hsv(out[0], out[1], out[2], &h, &sat, &v);
  h += rnds(s, index, R_HUE) * clampd(dy->hue_jitter / 100.0, 0, 1) * 180;
  while (h < 0) h += 360;
  while (h >= 360) h -= 360;
  sat = clampd(sat + rnds(s, index, R_SAT) * clampd(dy->sat_jitter / 100.0, 0, 1), 0, 1);
  pu = clampd(dy->purity / 100.0, -1, 1);
  sat = pu >= 0 ? sat + (1 - sat) * pu : sat * (1 + pu);
  v = clampd(v + rnds(s, index, R_BRI) * clampd(dy->bri_jitter / 100.0, 0, 1), 0, 1);
  hsv_to_rgb(h, sat, v, &r, &g, &b);
  out[0] = (u8)r; out[1] = (u8)g; out[2] = (u8)b;
}
/* the diameter before jitter: size times the size control (or the pressure button) */
static double base_diam(Stroke *s, double pressure)
{
  const BrushDynamics *dy = &s->b.dyn;
  double f = 1;
  if (s->b.custom) return s->b.custom->w > s->b.custom->h ? s->b.custom->w : s->b.custom->h;
  if (dy->shape_on && dy->size.control != CTL_OFF) {
    double mn = clampd(dy->size.minimum / 100.0, 0, 1);
    f = mn + (1 - mn) * ctl_value(s, &dy->size, pressure);
  } else if (dy->pressure_size) f = clampd(pressure, 0, 1);
  return s->b.size * f;
}
/* the next colour of DPaint's Cycle mode: through the document's first cycling range when it has
   a palette, else round the hue circle from the foreground colour */
static void cycle_color(Stroke *s, u8 *out)
{
  Doc *d = s->d;
  int step = s->cycle++;
  if (d->npal > 1 && d->ncycle > 0 && d->cycle[0].hi > d->cycle[0].lo) {
    int lo = d->cycle[0].lo, n = d->cycle[0].hi - lo + 1, e = lo + step % n;
    out[0] = d->pal[e * 3]; out[1] = d->pal[e * 3 + 1]; out[2] = d->pal[e * 3 + 2];
  } else {
    double h, sat, v;
    int r, g, b;
    rgb_to_hsv(s->col[0], s->col[1], s->col[2], &h, &sat, &v);
    if (sat < 0.05) sat = 1;
    if (v < 0.05) v = 1;
    h = fmod(h + step * 15.0, 360);
    hsv_to_rgb(h, sat, v, &r, &g, &b);
    out[0] = (u8)r; out[1] = (u8)g; out[2] = (u8)b;
  }
}

/* symmetry copy k as an exact image of the dab placed at x0, y0: when its turn or mirror is one of
   the pixel grid's own (whole quarter turns, mirrors across rows, columns or diagonals) about a
   centre that falls on the grid, its pixels are the dab's pixels moved. 0 if it isn't */
static int near_int(double v, int *out) { double r = floor(v + 0.5); if (fabs(v - r) > 1e-6) return 0; *out = (int)r; return 1; }
static int exact_copy(Stroke *s, int k, Place *pl, DabImg *di, i32 x0, i32 y0)
{
  const BrushDynamics *dy = &s->b.dyn;
  double cx = dy->sym_center_set ? dy->sym_cx : s->d->w / 2.0, cy = dy->sym_center_set ? dy->sym_cy : s->d->h / 2.0;
  double r = s->srot[k] * M_PI / 180, m[4], E, F;
  int a, b, c, d, e, f, i;
  i32 cxp[4], cyp[4], ex, ey;
  if (k == 0) { m[0] = 1; m[1] = 0; m[2] = 0; m[3] = 1; }
  else if (s->smir[k]) { m[0] = cos(r); m[1] = -sin(r); m[2] = -sin(r); m[3] = -cos(r); }
  else { m[0] = cos(r); m[1] = sin(r); m[2] = -sin(r); m[3] = cos(r); }
  if (!near_int(m[0], &a) || !near_int(m[1], &b) || !near_int(m[2], &c) || !near_int(m[3], &d)) return 0;
  /* forward: i' = a i + b j + E (pixel centres moved round the centre) */
  E = cx - 0.5 + a * (0.5 - cx) + b * (0.5 - cy);
  F = cy - 0.5 + c * (0.5 - cx) + d * (0.5 - cy);
  if (!near_int(E, &e) || !near_int(F, &f)) return 0;
  pl->di = di; pl->x0 = x0; pl->y0 = y0;
  /* back (the grid's turns and mirrors are orthogonal: the inverse is the transpose) */
  pl->ma = a; pl->mb = c; pl->mc = b; pl->md = d;
  pl->me = -(a * e + c * f); pl->mf = -(b * e + d * f);
  /* the copy's box: the dab's corners moved */
  for (i = 0; i < 4; i++) {
    i32 px = x0 + ((i & 1) ? di->w - 1 : 0), py = y0 + ((i & 2) ? di->h - 1 : 0);
    cxp[i] = a * px + b * py + e; cyp[i] = c * px + d * py + f;
  }
  ex = cxp[0]; ey = cyp[0];
  pl->b = box_make(ex, ey, ex + 1, ey + 1);
  for (i = 1; i < 4; i++) pl->b = box_union(pl->b, box_make(cxp[i], cyp[i], cxp[i] + 1, cyp[i] + 1));
  return 1;
}

/* one step of the path at x, y: the dabs the dynamics make there (scattering's count), each with
   its symmetry copies */
static void emit_step(Stroke *s, double x, double y, double pressure, Box *changed)
{
  const BrushDynamics *dy = &s->b.dyn;
  int count = 1, c, np, ne, k;
  u8 *gen = s->gen;
  double bd = base_diam(s, pressure), *sox = s->sox, *soy = s->soy, *srot = s->srot, *pox = s->qox, *poy = s->qoy, *prot = s->qrot;
  int *smir = s->smir, *pmir = s->qmir;
  if (dy->scatter_on) {
    int n = dy->count < 1 ? 1 : dy->count > 16 ? 16 : dy->count;
    double kk = scalar(s, &dy->count_jitter, pressure, rnd(s, (u32)s->step, R_COUNT));
    count = (int)floor(n * kk + 0.5);
    if (count < 1) count = 1;
    if (count > 16) count = 16;
  }
  for (c = 0; c < count; c++) {
    DabParams dp;
    u32 i = s->dabi++;
    double radius, opac = 1, flow = s->b.flow / 100.0;
    DabImg *di;
    i32 x0, y0;
    dp.diam = bd; dp.angle = s->b.angle; dp.round = s->b.roundness > 0 ? clampd(s->b.roundness / 100.0, 0.01, 1) : 1;
    dp.fx = s->b.flip_x != 0; dp.fy = s->b.flip_y != 0;
    if (dy->shape_on && !s->b.custom) {
      if (dy->size.jitter > 0) {
        double mn = clampd(dy->size.minimum / 100.0, 0, 1), f = 1 - clampd(dy->size.jitter / 100.0, 0, 1) * rnd(s, i, R_SIZE);
        dp.diam *= f < mn ? mn : f;
      }
      dp.angle += ctl_angle(s, &dy->angle, pressure) + rnds(s, i, R_ANGLE) * clampd(dy->angle.jitter / 100.0, 0, 1) * 180;
      dp.round = clampd(dp.round * scalar(s, &dy->roundness, pressure, rnd(s, i, R_ROUND)), 0.01, 1);
      if (dy->flip_x_jitter && rnd(s, i, R_FLIPX) < 0.5) dp.fx = !dp.fx;
      if (dy->flip_y_jitter && rnd(s, i, R_FLIPY) < 0.5) dp.fy = !dp.fy;
    }
    if (dp.diam < 0.5 && !s->b.custom) continue;      /* (faded or pressed to nothing: no paint) */
    radius = dp.diam / 2 < 0.5 ? 0.5 : dp.diam / 2;
    dp.cx = x; dp.cy = y;
    if (dy->scatter_on && dy->scatter.jitter > 0) {
      double amt = clampd(dy->scatter.jitter / 100.0, 0, 10) * ctl_value(s, &dy->scatter, pressure) * radius;
      double ox = rnds(s, i, R_SCX) * amt, oy = rnds(s, i, R_SCY) * amt;
      if (dy->both_axes) { dp.cx += ox; dp.cy += oy; }
      else {
        /* across the direction of travel */
        double dx = cos(s->dir * M_PI / 180), dyy = -sin(s->dir * M_PI / 180);
        dp.cx += -dyy * ox; dp.cy += dx * ox;
      }
    }
    if (dy->transfer_on) {
      opac = scalar(s, &dy->opacity, pressure, rnd(s, i, R_OPACITY));
      flow *= scalar(s, &dy->flow, pressure, rnd(s, i, R_FLOW));
    }
    if (dy->pressure_opacity && !(dy->transfer_on && dy->opacity.control != CTL_OFF)) opac *= clampd(pressure, 0, 1);
    dp.flow = clamp255(clampd(flow, 0, 1) * 255);
    dp.ceil = s->cmode ? clamp255(opac * s->b.opacity / 100.0 * 255) : 255;
    if (s->b.tool >= PT_BLUR && s->b.tool <= PT_SPONGE) dp.ceil = clamp255(opac * s->b.opacity / 100.0 * 255);
    if (s->b.custom && (s->b.custom_mode == CB_SMEAR || s->b.custom_mode == CB_SHADE)) dp.ceil = clamp255(opac * s->b.opacity / 100.0 * 255);
    /* the colour */
    if (s->b.custom && s->b.custom_mode == CB_CYCLE) cycle_color(s, dp.col);
    else if (dy->color_on) {
      if (dy->per_stroke) {
        if (!s->have_scol) { dyn_color(s, 0, pressure, s->scol); s->have_scol = 1; }
        memcpy(dp.col, s->scol, 3);
      } else dyn_color(s, i, pressure, dp.col);
    } else memcpy(dp.col, s->col, 3);
    /* the copies */
    np = brush_symmetry(dy, s->d->w, s->d->h, dp.cx, dp.cy, sox, soy, srot, smir);
    brush_symmetry(dy, s->d->w, s->d->h, s->px, s->py, pox, poy, prot, pmir);
    if (!(di = get_dab(s, dp.diam, dp.angle, dp.round, dp.fx, dp.fy))) continue;
    x0 = (i32)floor(dp.cx - di->ox + 0.5); y0 = (i32)floor(dp.cy - di->oy + 0.5);
    /* first the dab and every copy that is an exact image of it on the pixel grid, laid together */
    ne = 0;
    for (k = 0; k < np; k++) {
      Place *pl = &s->pl[ne];
      if (!exact_copy(s, k, pl, di, x0, y0)) { gen[k] = 1; continue; }
      gen[k] = 0;
      pl->hx = (i32)floor(sox[k]); pl->hy = (i32)floor(soy[k]);
      pl->sdx = (i32)floor(sox[k] - pox[k] + 0.5); pl->sdy = (i32)floor(soy[k] - poy[k] + 0.5);
      ne++;
    }
    lay_places(s, ne, &dp, changed);
    /* then the turned copies, each with its own dab image, one at a time */
    for (k = 1; k < np; k++) {
      Place *pl = &s->pl[0];
      double a;
      DabImg *dk;
      if (!gen[k]) continue;
      a = smir[k] ? srot[k] - dp.angle : dp.angle + srot[k];
      if (!(dk = get_dab(s, dp.diam, a, dp.round, dp.fx, smir[k] ? !dp.fy : dp.fy))) break;
      pl->di = dk; pl->x0 = (i32)floor(sox[k] - dk->ox + 0.5); pl->y0 = (i32)floor(soy[k] - dk->oy + 0.5);
      pl->ma = pl->md = 1; pl->mb = pl->mc = 0; pl->me = pl->mf = 0;
      pl->b = box_make(pl->x0, pl->y0, pl->x0 + dk->w, pl->y0 + dk->h);
      pl->hx = (i32)floor(sox[k]); pl->hy = (i32)floor(soy[k]);
      pl->sdx = (i32)floor(sox[k] - pox[k] + 0.5); pl->sdy = (i32)floor(soy[k] - poy[k] + 0.5);
      lay_places(s, 1, &dp, changed);
    }
  }
  s->px = x; s->py = y;
  s->step++;
}

/* ------------------------------------------------------------------ walking the path */
static double step_len(Stroke *s, double pressure)
{
  double st = base_diam(s, pressure);
  if (st < 1) st = 1;
  st = st * s->b.spacing / 100.0;
  if (st < 1) st = 1;
  if ((s->b.tool == PT_PENCIL || (s->b.tool == PT_ERASER && s->b.eraser_mode == ER_PENCIL)) && st > 1) st = 1;
  return st;
}
/* the brush taken from where it is to x, y, dabs laid every step on the way */
static void walk(Stroke *s, double x, double y, double p, Box *changed)
{
  double dx = x - s->x, dy = y - s->y, len = sqrt(dx * dx + dy * dy), t;
  if (len <= 0) { s->p = p; return; }
  s->dir = atan2(-dy, dx) * 180 / M_PI;
  if (!s->have_dir) { s->dir0 = s->dir; s->have_dir = 1; }
  /* (a segment far longer than the canvas is cut to it) */
  t = s->next;
  while (t <= len) {
    double f = t / len, pp = s->p + (p - s->p) * f;
    emit_step(s, s->x + dx * f, s->y + dy * f, pp, changed);
    t += step_len(s, pp);
    if (s->ndabs > 50000000L) break;
  }
  s->next = t - len;
  s->x = x; s->y = y; s->p = p;
}
/* smoothing: the brush pulled towards the pointer (PhotoCraft's Smoother) */
static void smooth_to(Stroke *s, double x, double y, double p, Box *changed)
{
  int sm = s->b.dyn.smoothing;
  s->ix = x; s->iy = y; s->ip = p;
  if (sm <= 0) { walk(s, x, y, p, changed); return; }
  if (s->b.dyn.pulled_string) {
    double dx = x - s->x, dy = y - s->y, d = sqrt(dx * dx + dy * dy), len = sm;
    if (d > len) { double k = (d - len) / d; walk(s, s->x + dx * k, s->y + dy * k, s->p + (p - s->p) * k, changed); }
    return;
  } else {
    double a = 1 - (sm > 95 ? 0.95 : sm / 100.0);
    walk(s, s->x + (x - s->x) * a, s->y + (y - s->y) * a, s->p + (p - s->p) * a, changed);
  }
}

/* ------------------------------------------------------------------ the stroke */
Stroke *stroke_begin_target(Doc *d, const PaintTarget *t, const Brush *b, u32 color, double x, double y, double pressure, Box *changed)
{
  static const char *const names[PT_N] = { "Brush Tool", "Pencil", "Eraser", "Clone Stamp", "Blur Tool", "Sharpen Tool", "Smudge Tool", "Dodge Tool", "Burn Tool", "Sponge Tool",
                                           "Pattern Stamp", "History Brush", "Color Replacement Tool", "Background Eraser Tool" };
  Stroke *s;
  long nt;
  if (changed) *changed = box_make(0, 0, 0, 0);
  if (!t || !t->px || (t->bpp != 1 && t->bpp != 4) || t->w != d->w || t->h != d->h) return NULL;
  if (b->tool < 0 || b->tool >= PT_N) return NULL;
  if (b->tool == PT_PATTERN && (!b->pattern || !b->pattern->px || b->pattern->w < 1 || b->pattern->h < 1)) return NULL;
  if (b->tool == PT_HISTORY && (!b->snapshot || b->snapshot->w != d->w || b->snapshot->h != d->h)) return NULL;
  if (!(s = (Stroke *)calloc(1, sizeof *s))) return NULL;
  s->d = d; s->t = *t; s->b = *b; s->color = color & 0xFFFFFF;
  if (s->b.size < 1) s->b.size = 1;
  if (s->b.size > 5000) s->b.size = 5000;
  if (s->b.spacing < 1) s->b.spacing = 1;
  if (s->b.flow < 1) s->b.flow = 1;
  if (s->b.opacity < 0) s->b.opacity = 0;
  if (s->b.opacity > 100) s->b.opacity = 100;
  if (s->b.tool == PT_ERASER && s->b.eraser_mode == ER_BLOCK) { s->b.opacity = 100; s->b.flow = 100; }
  if (s->b.tip && (!s->b.tip->a || s->b.tip->w < 1 || s->b.tip->h < 1)) s->b.tip = NULL;
  if (s->b.custom && (!s->b.custom->px || s->b.custom->w < 1 || s->b.custom->h < 1)) s->b.custom = NULL;
  if (s->b.tool != PT_BRUSH && s->b.tool != PT_PENCIL) s->b.custom = NULL;   /* (colour brushes paint with the brush tools) */
  if (s->b.tip) { s->mip[0] = (Tip *)s->b.tip; s->nmip = 1; }
  /* Auto Erase: a pencil stroke starting on the foreground colour paints the background colour */
  if (s->b.tool == PT_PENCIL && s->b.auto_erase && x >= 0 && y >= 0 && x < d->w && y < d->h) {
    const u8 *q = t->px + ((long)floor(y) * t->w + (long)floor(x)) * t->bpp;
    u8 fg[3];
    rgb_of(s->color, fg);
    if (t->bpp == 4 ? q[3] && q[0] == fg[0] && q[1] == fg[1] && q[2] == fg[2] : *q == grey_of(fg)) s->color = s->b.bg & 0xFFFFFF;
  }
  rgb_of(s->color, s->col);
  s->cmode = s->b.dyn.transfer_on || s->b.dyn.pressure_opacity;
  s->coarse = s->b.dyn.shape_on && (s->b.dyn.angle.jitter > 0 || s->b.dyn.angle.control != CTL_OFF);
  s->ntx = (d->w + TS - 1) / TS; s->nty = (d->h + TS - 1) / TS;
  nt = (long)s->ntx * s->nty;
  s->mask = (u8 **)calloc((size_t)nt, sizeof(u8 *));
  s->orig = (u8 **)calloc((size_t)nt, sizeof(u8 *));
  if (s->b.tool == PT_CLONE && s->b.sample_all) s->merged = (u8 **)calloc((size_t)nt, sizeof(u8 *));
  if (!s->mask || !s->orig || (s->b.tool == PT_CLONE && s->b.sample_all && !s->merged) || !get_dab(s, base_diam(s, pressure), s->b.angle, 1, 0, 0)) {
    free(s->mask); free(s->orig); free(s->merged); dab_free(&s->cache[0]); free(s);
    return NULL;
  }
  if (s->b.tool == PT_PATTERN && !s->b.pattern_aligned) { s->pox = (i32)floor(x); s->poy = (i32)floor(y); }
  doc_begin(d, names[s->b.tool]);
  s->x = s->px = s->ix = x; s->y = s->py = s->iy = y; s->p = s->ip = pressure;
  emit_step(s, x, y, pressure, changed);
  s->next = step_len(s, pressure);
  return s;
}
Stroke *stroke_begin_p(Doc *d, const Brush *b, u32 color, double x, double y, double pressure, Box *changed)
{
  PaintTarget t;
  Layer *l;
  if (changed) *changed = box_make(0, 0, 0, 0);
  if (d->active < 0 || d->active >= d->nlay) return NULL;
  l = d->lay[d->active];
  if (l->locked || !l->visible || !l->px) return NULL;
  t.px = l->px; t.w = d->w; t.h = d->h; t.bpp = 4; t.alpha_lock = l->alpha_lock; t.note = note_layer; t.ctx = d;
  return stroke_begin_target(d, &t, b, color, x, y, pressure, changed);
}
Stroke *stroke_begin(Doc *d, const Brush *b, u32 color, double x, double y, Box *changed) { return stroke_begin_p(d, b, color, x, y, 1.0, changed); }
void stroke_to_p(Stroke *s, double x, double y, double pressure, Box *changed)
{
  if (changed) *changed = box_make(0, 0, 0, 0);
  if (!s) return;
  if (!(pressure >= 0)) pressure = 0;      /* (NaN too) */
  if (pressure > 1) pressure = 1;
  if (!(x > -1e6 && x < 1e6 && y > -1e6 && y < 1e6)) return;
  smooth_to(s, x, y, pressure, changed);
}
void stroke_to(Stroke *s, double x, double y, Box *changed) { stroke_to_p(s, x, y, 1.0, changed); }
int stroke_wants_tick(const Brush *b) { return b->dyn.buildup || (b->dyn.smoothing > 0 && !b->dyn.pulled_string); }
int stroke_tick_ms(const Brush *b)
{
  int rate = b->dyn.buildup_rate > 0 ? b->dyn.buildup_rate : 20, ms;
  if (!stroke_wants_tick(b)) return 0;
  if (!b->dyn.buildup) return 16;
  ms = 1000 / (rate > 0 ? rate : 1);
  return ms < 10 ? 10 : ms;
}
void stroke_tick(Stroke *s, Box *changed)
{
  if (changed) *changed = box_make(0, 0, 0, 0);
  if (!s) return;
  /* smoothing catches up with a pointer that has stopped */
  if (s->b.dyn.smoothing > 0 && !s->b.dyn.pulled_string && fabs(s->x - s->ix) + fabs(s->y - s->iy) > 0.01) smooth_to(s, s->ix, s->iy, s->ip, changed);
  /* the airbrush keeps spraying */
  if (s->b.dyn.buildup) emit_step(s, s->x, s->y, s->p, changed);
}
long stroke_dabs(const Stroke *s) { return s ? s->ndabs : 0; }
int stroke_string(const Stroke *s, double *bx, double *by)
{
  if (!s || s->b.dyn.smoothing <= 0 || (s->x == s->ix && s->y == s->iy)) return 0;
  *bx = s->x; *by = s->y;
  return 1;
}
void stroke_end(Stroke *s)
{
  i32 i;
  if (!s) return;
  /* (smoothing: the brush finishes where the pointer did) */
  if (s->b.dyn.smoothing > 0 && (s->x != s->ix || s->y != s->iy)) walk(s, s->ix, s->iy, s->ip, NULL);
  for (i = 0; i < s->ntx * s->nty; i++) { free(s->mask[i]); free(s->orig[i]); if (s->merged) free(s->merged[i]); }
  for (i = 0; i < NCACHE; i++) if (s->cache[i].used) dab_free(&s->cache[i]);
  for (i = 1; i < s->nmip; i++) tip_free(s->mip[i]);
  free(s->mask); free(s->orig); free(s->merged); free(s->w8); free(s->stk);
  doc_end(s->d);
  free(s);
}

/* ------------------------------------------------------------------ previews */
u8 *brush_dab_preview(const Brush *b, int *w, int *h)
{
  Stroke *s = (Stroke *)calloc(1, sizeof *s);    /* (too big for a 68k stack) */
  DabImg *di;
  u8 *out = NULL;
  int i;
  if (!s) return NULL;
  s->b = *b;
  if (s->b.size < 1) s->b.size = 1;
  if (s->b.size > 5000) s->b.size = 5000;
  if (s->b.tip && (!s->b.tip->a || s->b.tip->w < 1)) s->b.tip = NULL;
  if (s->b.custom && (!s->b.custom->px || s->b.custom->w < 1)) s->b.custom = NULL;
  if (s->b.tip) { s->mip[0] = (Tip *)s->b.tip; s->nmip = 1; }
  di = get_dab(s, s->b.custom ? 0 : s->b.size, s->b.angle, s->b.roundness > 0 ? s->b.roundness / 100.0 : 1, b->flip_x != 0, b->flip_y != 0);
  if (di && (out = (u8 *)malloc((size_t)di->w * di->h))) { memcpy(out, di->a, (size_t)di->w * di->h); *w = di->w; *h = di->h; }
  for (i = 0; i < NCACHE; i++) if (s->cache[i].used) dab_free(&s->cache[i]);
  for (i = 1; i < s->nmip; i++) tip_free(s->mip[i]);
  free(s);
  return out;
}

/* ------------------------------------------------------------------ the Magic Eraser */
Box magic_erase(Doc *d, i32 x, i32 y, int tol, int contiguous, int all_layers, int opacity, int antialias)
{
  Layer *l;
  u8 *img = NULL, *m, seed[4];
  const u8 *src;
  long n = (long)d->w * d->h, k;
  Box b = box_make(d->w, d->h, 0, 0);
  i32 xx, yy;
  if (d->active < 0 || d->active >= d->nlay || x < 0 || y < 0 || x >= d->w || y >= d->h) return box_make(0, 0, 0, 0);
  l = d->lay[d->active];
  if (l->locked || !l->visible || !l->px) return box_make(0, 0, 0, 0);
  if (all_layers) {
    if (!(img = (u8 *)malloc((size_t)n * 4))) return box_make(0, 0, 0, 0);
    doc_composite(d, doc_box(d), img, (long)d->w * 4);
    src = img;
  } else src = l->px;
  if (!(m = (u8 *)calloc((size_t)n, 1))) { free(img); return box_make(0, 0, 0, 0); }
  memcpy(seed, src + ((long)y * d->w + x) * 4, 4);
#define LIKE(q) (abs((q)[0] - seed[0]) <= tol && abs((q)[1] - seed[1]) <= tol && abs((q)[2] - seed[2]) <= tol && abs((q)[3] - seed[3]) <= tol)
  if (!contiguous) {
    for (k = 0; k < n; k++) if (LIKE(src + k * 4)) m[k] = 255;
  } else {
    long cap = 1024, top = 0;
    i32 *st = (i32 *)malloc((size_t)cap * 2 * sizeof(i32));
    if (!st) { free(m); free(img); return box_make(0, 0, 0, 0); }
    st[0] = x; st[1] = y; top = 1;
    while (top > 0) {
      i32 sx, sy, lft, rgt, i2;
      top--; sx = st[top * 2]; sy = st[top * 2 + 1];
      if (m[(long)sy * d->w + sx] || !LIKE(src + ((long)sy * d->w + sx) * 4)) continue;
      for (lft = sx; lft > 0 && !m[(long)sy * d->w + lft - 1] && LIKE(src + ((long)sy * d->w + lft - 1) * 4); lft--) ;
      for (rgt = sx; rgt < d->w - 1 && !m[(long)sy * d->w + rgt + 1] && LIKE(src + ((long)sy * d->w + rgt + 1) * 4); rgt++) ;
      for (i2 = lft; i2 <= rgt; i2++) m[(long)sy * d->w + i2] = 255;
      for (i2 = lft; i2 <= rgt; i2++) {
        int dyy;
        for (dyy = -1; dyy <= 1; dyy += 2) {
          i32 ny = sy + dyy;
          if (ny < 0 || ny >= d->h || m[(long)ny * d->w + i2] || !LIKE(src + ((long)ny * d->w + i2) * 4)) continue;
          if (top == cap) { i32 *ns = (i32 *)realloc(st, (size_t)cap * 4 * sizeof(i32)); if (!ns) { top = 0; break; } st = ns; cap *= 2; }
          st[top * 2] = i2; st[top * 2 + 1] = ny; top++;
        }
      }
    }
    free(st);
  }
#undef LIKE
  free(img);
  if (antialias) {
    u8 *t2 = (u8 *)malloc((size_t)n);
    if (t2) {
      memcpy(t2, m, (size_t)n);
      for (yy = 1; yy < d->h - 1; yy++)
        for (xx = 1; xx < d->w - 1; xx++) {
          long q = (long)yy * d->w + xx;
          if (t2[q] != t2[q - 1] || t2[q] != t2[q + 1] || t2[q] != t2[q - d->w] || t2[q] != t2[q + d->w])
            m[q] = (u8)((t2[q] * 4 + t2[q - 1] + t2[q + 1] + t2[q - d->w] + t2[q + d->w]) / 8);
        }
      free(t2);
    }
  }
  for (yy = 0; yy < d->h; yy++)
    for (xx = 0; xx < d->w; xx++)
      if (m[(long)yy * d->w + xx]) {
        if (xx < b.x0) b.x0 = xx;
        if (xx >= b.x1) b.x1 = xx + 1;
        if (yy < b.y0) b.y0 = yy;
        if (yy >= b.y1) b.y1 = yy + 1;
      }
  if (d->sel) b = box_clip(b, d->selbox);
  if (box_empty(b)) { free(m); return box_make(0, 0, 0, 0); }
  doc_begin(d, "Magic Eraser");
  doc_note_layer(d, d->active, b);
  for (yy = b.y0; yy < b.y1; yy++)
    for (xx = b.x0; xx < b.x1; xx++) {
      long q = (long)yy * d->w + xx;
      int c = mul255(mul255(m[q], opacity * 255 / 100), sel_at(d, xx, yy));
      u8 *p = l->px + q * 4;
      if (!c) continue;
      if (l->alpha_lock) {
        /* (transparency locked: rubbed out to the background colour instead) */
        u8 bgc[3];
        rgb_of(d->bg, bgc);
        put_px(1, p, bgc, c, BM_NORMAL);
      } else {
        p[3] = (u8)mul255(p[3], 255 - c);
        if (!p[3]) p[0] = p[1] = p[2] = 0;
      }
    }
  doc_end(d);
  free(m);
  return b;
}
