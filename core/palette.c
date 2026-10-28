/*
 * palette.c - Easel 0.4's indexed documents (see palette.h): the colour modes, three quantizers
 * (median cut, octree and Wu), the dithers, the nearest-colour search, palette editing, colour
 * cycling (with IFF CRNG, CCRT and DRNG) and palette files (.act, .aco, .ase, .gpl, JASC and RIFF
 * .pal, IFF CMAP and PNG PLTE).
 *
 * The file formats are written from their published descriptions: EA IFF 85 and the ILBM CRNG and
 * CCRT chunks, DPaint IV's DRNG description, Adobe's Photoshop File Formats specification (ACT and
 * ACO), the ASE layout as Adobe documents it, GIMP's palette format, Paint Shop Pro's JASC-PAL and
 * Microsoft's RIFF palette. Median cut follows Heckbert (1982), the octree Gervautz and Purgathofer
 * (1988), and the variance-minimising cut Wu (Graphics Gems II, 1991); all three are written here
 * from those descriptions.
 *
 * Memory and speed on the 68k: the quantizers work from a 32 x 32 x 32 histogram (with each bin's
 * colour sums, so the means are exact), built from at most QSAMPLES pixels; the nearest-colour
 * search keeps a cache keyed on the full 24-bit colour and searches the palette outward from the
 * nearest green, and every per-pixel loop is integer.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "palette.h"
#include "codec.h"

const char *const quant_names[QM_N + 1] = { "Median Cut", "Octree", "Wu (Variance)", "Fixed Palette", NULL };
const char *const dither_names[DI_N + 1] = { "None", "Floyd-Steinberg", "Atkinson", "Burkes", "Sierra", "Sierra Lite", "Stucki",
  "Jarvis-Judice-Ninke", "Bayer 2 x 2", "Bayer 4 x 4", "Bayer 8 x 8", "Pattern", "Random", NULL };
const char *const bitmap_names[BMM_N + 1] = { "50% Threshold", "Pattern Dither", "Diffusion Dither", "Halftone Screen", NULL };
const char *const preset_names[PP_N + 1] = { "Workbench 1.3 (4)", "Workbench 2/3 (4)", "MagicWB (8)", "Web Safe (216)", "Grey (2)",
  "Grey (4)", "Grey (16)", "Grey (256)", "Uniform 3-3-2 (256)", "OCS Cube 4 x 4 x 4 (64)", "EHB Starter (64)", NULL };

static int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static int luma(int r, int g, int b) { return (r * 77 + g * 150 + b * 29 + 128) >> 8; }
static int is_pixel_layer(const Layer *l) { return l && l->px && (l->kind == LK_PIXEL); }

/* ================================================================== nearest colour */
#define NC_BITS 12
#define NC_SIZE (1 << NC_BITS)
typedef struct {
  const u8 *pal; int n; const u8 *lock;
  u8 order[256]; u8 gval[256];        /* the entries by green, and their greens */
  u32 key[NC_SIZE]; u8 val[NC_SIZE];  /* key: 0xRRGGBB, bit 24 set when empty */
} Near;
static Near *near_new(const u8 *pal, int n, const u8 *lock)
{
  Near *c = (Near *)malloc(sizeof *c);
  int i, j;
  if (!c) return NULL;
  c->pal = pal; c->n = n < 1 ? 1 : n; c->lock = lock;
  for (i = 0; i < NC_SIZE; i++) c->key[i] = 0x1000000UL;
  /* insertion sort of the entries by green (at most 256) */
  for (i = 0; i < c->n; i++) {
    u8 g = pal[i * 3 + 1];
    for (j = i; j > 0 && c->gval[j - 1] > g; j--) { c->gval[j] = c->gval[j - 1]; c->order[j] = c->order[j - 1]; }
    c->gval[j] = g; c->order[j] = (u8)i;
  }
  return c;
}
/* the weighted distance: 3 dR^2 + 4 dG^2 + 2 dB^2 (a cheap approximation of perceived difference) */
static int near_search(const Near *c, int r, int g, int b)
{
  int lo, hi, mid, best = c->order[0], bl;
  long bd = 0x7FFFFFFFL;
  /* the first entry with green >= g, then outward both ways while the green alone can still win */
  lo = 0; hi = c->n;
  while (lo < hi) { mid = (lo + hi) >> 1; if (c->gval[mid] < g) lo = mid + 1; else hi = mid; }
  hi = lo; lo = lo - 1;
  bl = c->lock ? c->lock[best] : 0;
  while (lo >= 0 || hi < c->n) {
    int k;
    for (k = 0; k < 2; k++) {
      int j = k ? lo : hi, i;
      long dg, dd, dr, db;
      if (j < 0 || j >= c->n) continue;
      dg = (long)c->gval[j] - g;
      if (dg * dg * 4 > bd) { if (k) lo = -1; else hi = c->n; continue; }
      i = c->order[j];
      dr = (long)c->pal[i * 3] - r; db = (long)c->pal[i * 3 + 2] - b;
      dd = dr * dr * 3 + dg * dg * 4 + db * db * 2;
      if (dd < bd || (dd == bd && c->lock && c->lock[i] && !bl) || (dd == bd && i < best && !(c->lock && c->lock[best] && !c->lock[i]))) {
        bd = dd; best = i; bl = c->lock ? c->lock[i] : 0;
      }
      if (k) lo--; else hi++;
    }
  }
  return best;
}
static int near_get(Near *c, int r, int g, int b)
{
  u32 k = ((u32)r << 16) | ((u32)g << 8) | (u32)b, h = ((k * 2654435761UL) >> (32 - NC_BITS)) & (NC_SIZE - 1);
  if (c->key[h] != k) { c->key[h] = k; c->val[h] = (u8)near_search(c, r, g, b); }
  return c->val[h];
}
int pal_nearest(const Doc *d, int r, int g, int b)
{
  Near *c = near_new(d->pal, d->npal, d->pal_lock);
  int i;
  if (!c) return 0;
  i = near_search(c, r, g, b);
  free(c);
  return i;
}

/* ================================================================== mapping with dithers */
typedef struct { signed char dx, dy, w; } KTap;
typedef struct { const KTap *t; int n, div; } Kernel;
static const KTap k_fs[] = { {1,0,7}, {-1,1,3}, {0,1,5}, {1,1,1} };
static const KTap k_atk[] = { {1,0,1}, {2,0,1}, {-1,1,1}, {0,1,1}, {1,1,1}, {0,2,1} };
static const KTap k_burkes[] = { {1,0,8}, {2,0,4}, {-2,1,2}, {-1,1,4}, {0,1,8}, {1,1,4}, {2,1,2} };
static const KTap k_sierra[] = { {1,0,5}, {2,0,3}, {-2,1,2}, {-1,1,4}, {0,1,5}, {1,1,4}, {2,1,2}, {-1,2,2}, {0,2,3}, {1,2,2} };
static const KTap k_slite[] = { {1,0,2}, {-1,1,1}, {0,1,1} };
static const KTap k_stucki[] = { {1,0,8}, {2,0,4}, {-2,1,2}, {-1,1,4}, {0,1,8}, {1,1,4}, {2,1,2}, {-2,2,1}, {-1,2,2}, {0,2,4}, {1,2,2}, {2,2,1} };
static const KTap k_jjn[] = { {1,0,7}, {2,0,5}, {-2,1,3}, {-1,1,5}, {0,1,7}, {1,1,5}, {2,1,3}, {-2,2,1}, {-1,2,3}, {0,2,5}, {1,2,3}, {2,2,1} };
static int kernel_of(int dither, Kernel *k)
{
  switch (dither) {
  case DI_FS: k->t = k_fs; k->n = 4; k->div = 16; return 1;
  case DI_ATKINSON: k->t = k_atk; k->n = 6; k->div = 8; return 1;
  case DI_BURKES: k->t = k_burkes; k->n = 7; k->div = 32; return 1;
  case DI_SIERRA: k->t = k_sierra; k->n = 10; k->div = 32; return 1;
  case DI_SIERRA_LITE: k->t = k_slite; k->n = 3; k->div = 4; return 1;
  case DI_STUCKI: k->t = k_stucki; k->n = 12; k->div = 42; return 1;
  case DI_JJN: k->t = k_jjn; k->n = 12; k->div = 48; return 1;
  }
  return 0;
}
/* an ordered matrix's threshold at x, y in -128..127: Bayer n x n (made recursively), or the
   clustered-dot 4 x 4 "pattern" */
static const u8 cluster4[16] = { 12, 5, 6, 13, 4, 0, 1, 7, 11, 3, 2, 8, 15, 10, 9, 14 };
static int bayer_at(int n, int x, int y)
{
  int v = 0, bit;
  /* the standard recursion M(2n) = [4M, 4M+2; 4M+3, 4M+1], done bit by bit */
  for (bit = n >> 1; bit > 0; bit >>= 1) {
    int xb = (x & bit) != 0, yb = (y & bit) != 0;
    v = v * 4 + (xb ^ yb ? (yb ? 3 : 2) : (yb ? 1 : 0));
  }
  return v;
}
static int ordered_at(int dither, int x, int y)
{
  int n, v;
  if (dither == DI_PATTERN) return (cluster4[(y & 3) * 4 + (x & 3)] * 2 + 1) * 8 - 128;
  n = dither == DI_BAYER2 ? 2 : dither == DI_BAYER4 ? 4 : 8;
  v = bayer_at(n, x & (n - 1), y & (n - 1));
  return (v * 2 + 1) * 128 / (n * n) - 128;
}
/* how far apart the palette's colours are (the mean over entries of the largest channel difference
   to the nearest other entry): how strong an ordered or random dither should be */
static int pal_spacing(const u8 *pal, int n)
{
  long sum = 0;
  int i, j;
  if (n < 2) return 255;
  for (i = 0; i < n; i++) {
    int best = 255;
    for (j = 0; j < n; j++) {
      int a, b2, c, m;
      if (j == i) continue;
      a = abs(pal[i * 3] - pal[j * 3]); b2 = abs(pal[i * 3 + 1] - pal[j * 3 + 1]); c = abs(pal[i * 3 + 2] - pal[j * 3 + 2]);
      m = a > b2 ? a : b2; if (c > m) m = c;
      if (m > 0 && m < best) best = m;
    }
    sum += best;
  }
  sum /= n;
  return sum < 8 ? 8 : sum > 255 ? 255 : (int)sum;
}

/* one picture (w x h RGBA; pixels with alpha 0 left out of the error diffusion) mapped to the
   palette in c: idx gets the entries; if snap, px's RGB is set to them (alpha kept). gray: colours
   are taken as their luminance. 0 if out of memory */
static int map_pixels(u8 *px, const u8 *src, i32 w, i32 h, u8 *idx, Near *c, int dither, int gray, int snap)
{
  Kernel k;
  int diffuse = kernel_of(dither, &k), spread = 0;
  long *err = NULL, *rows[3];
  i32 x, y;
  u32 seed = 12345;
  if (diffuse) {
    /* three rows of accumulated error (weight times error), with two pixels of margin each side */
    if (!(err = (long *)calloc((size_t)(w + 4) * 9, sizeof(long)))) return 0;
    rows[0] = err; rows[1] = err + (w + 4) * 3; rows[2] = err + (w + 4) * 6;
  } else if (dither != DI_NONE) spread = pal_spacing(c->pal, c->n);
  for (y = 0; y < h; y++) {
    int serp = diffuse && (y & 1), step = serp ? -1 : 1;
    x = serp ? w - 1 : 0;
    for (; serp ? x >= 0 : x < w; x += step) {
      long o = (long)y * w + x;
      const u8 *s = src + o * 4;
      int r = s[0], g = s[1], b = s[2], q;
      if (gray) r = g = b = luma(r, g, b);
      if (diffuse && s[3]) {
        long *e = rows[0] + (x + 2) * 3;
        r = clamp255(r + (int)((e[0] >= 0 ? e[0] + k.div / 2 : e[0] - k.div / 2) / k.div));
        g = clamp255(g + (int)((e[1] >= 0 ? e[1] + k.div / 2 : e[1] - k.div / 2) / k.div));
        b = clamp255(b + (int)((e[2] >= 0 ? e[2] + k.div / 2 : e[2] - k.div / 2) / k.div));
        if (gray) r = g = b = luma(r, g, b);
      } else if (spread && s[3]) {
        int t;
        if (dither == DI_RANDOM) { seed = seed * 1103515245UL + 12345; t = (int)((seed >> 16) & 255) - 128; }
        else t = ordered_at(dither, x, y);
        t = t * spread / 256;
        r = clamp255(r + t); g = clamp255(g + t); b = clamp255(b + t);
      }
      q = near_get(c, r, g, b);
      idx[o] = (u8)q;
      if (diffuse && s[3]) {
        int er = r - c->pal[q * 3], eg = g - c->pal[q * 3 + 1], eb = b - c->pal[q * 3 + 2], t;
        for (t = 0; t < k.n; t++) {
          int xx = x + (serp ? -k.t[t].dx : k.t[t].dx);
          long *e;
          if (xx < -2 || xx >= w + 2) continue;
          e = rows[k.t[t].dy] + (xx + 2) * 3;
          e[0] += (long)er * k.t[t].w; e[1] += (long)eg * k.t[t].w; e[2] += (long)eb * k.t[t].w;
        }
      }
      if (snap) { u8 *p = px + o * 4; p[0] = c->pal[q * 3]; p[1] = c->pal[q * 3 + 1]; p[2] = c->pal[q * 3 + 2]; }
    }
    if (diffuse) {
      long *t = rows[0];
      rows[0] = rows[1]; rows[1] = rows[2]; rows[2] = t;
      memset(rows[2], 0, sizeof(long) * (size_t)(w + 4) * 3);
    }
  }
  free(err);
  return 1;
}

/* ================================================================== the histogram the quantizers share */
#define QSAMPLES (4L * 1024 * 1024)        /* at most this many pixels are counted (evenly sampled) */
#define HB(r, g, b) ((((r) >> 3) << 10) | (((g) >> 3) << 5) | ((b) >> 3))
typedef struct { u32 *cnt, *sr, *sg, *sb; } Hist;
typedef struct { const u8 *px; long n; } Run;
static void hist_free(Hist *h) { free(h->cnt); free(h->sr); free(h->sg); free(h->sb); h->cnt = h->sr = h->sg = h->sb = NULL; }
static int hist_build(Hist *h, const Run *runs, int nruns, int gray)
{
  long total = 0, step, i;
  int k;
  h->cnt = (u32 *)calloc(32768, sizeof(u32)); h->sr = (u32 *)calloc(32768, sizeof(u32));
  h->sg = (u32 *)calloc(32768, sizeof(u32)); h->sb = (u32 *)calloc(32768, sizeof(u32));
  if (!h->cnt || !h->sr || !h->sg || !h->sb) { hist_free(h); return 0; }
  for (k = 0; k < nruns; k++) total += runs[k].n;
  step = total > QSAMPLES ? (total + QSAMPLES - 1) / QSAMPLES : 1;
  for (k = 0; k < nruns; k++)
    for (i = 0; i < runs[k].n; i += step) {
      const u8 *p = runs[k].px + i * 4;
      int r = p[0], g = p[1], b = p[2], bin;
      if (p[3] < 128) continue;
      if (gray) r = g = b = luma(r, g, b);
      bin = HB(r, g, b);
      h->cnt[bin]++; h->sr[bin] += (u32)r; h->sg[bin] += (u32)g; h->sb[bin] += (u32)b;
    }
  return 1;
}
static void bin_mean(const Hist *h, int bin, int *r, int *g, int *b)
{
  u32 c = h->cnt[bin];
  *r = (int)((h->sr[bin] + c / 2) / c); *g = (int)((h->sg[bin] + c / 2) / c); *b = (int)((h->sb[bin] + c / 2) / c);
}

/* ------------------------------------------------------------------ the exact colours, if few enough */
/* every different colour (alpha >= 128) of the runs, if there are at most max: into pal, the count
   returned; -1 if there are more (or no memory) */
static int exact_colours(const Run *runs, int nruns, int max, u8 *pal, int gray)
{
  enum { HS = 1024 };
  u32 *key = (u32 *)malloc(sizeof(u32) * HS);
  int nc = 0, k;
  long i;
  if (!key) return -1;
  for (i = 0; i < HS; i++) key[i] = 0x1000000UL;
  for (k = 0; k < nruns && nc >= 0; k++)
    for (i = 0; i < runs[k].n; i++) {
      const u8 *p = runs[k].px + i * 4;
      u32 c, hsh;
      if (p[3] < 128) continue;
      c = gray ? (u32)luma(p[0], p[1], p[2]) * 0x010101UL : ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];
      hsh = (c * 2654435761UL) >> 22;
      while (key[hsh & (HS - 1)] != 0x1000000UL && key[hsh & (HS - 1)] != c) hsh++;
      hsh &= HS - 1;
      if (key[hsh] == c) continue;
      if (nc >= max) { nc = -1; break; }
      key[hsh] = c;
      pal[nc * 3] = (u8)(c >> 16); pal[nc * 3 + 1] = (u8)(c >> 8); pal[nc * 3 + 2] = (u8)c;
      nc++;
    }
  free(key);
  return nc;
}

/* ------------------------------------------------------------------ median cut */
typedef struct { int lo[3], hi[3]; unsigned long count; } QBox;
static void qbox_fit(QBox *b, const u32 *cnt)
{
  int lo[3] = { 31, 31, 31 }, hi[3] = { 0, 0, 0 }, r, g, bl;
  b->count = 0;
  for (r = b->lo[0]; r <= b->hi[0]; r++)
    for (g = b->lo[1]; g <= b->hi[1]; g++)
      for (bl = b->lo[2]; bl <= b->hi[2]; bl++) {
        u32 c = cnt[(r << 10) | (g << 5) | bl];
        if (!c) continue;
        b->count += c;
        if (r < lo[0]) lo[0] = r;
        if (r > hi[0]) hi[0] = r;
        if (g < lo[1]) lo[1] = g;
        if (g > hi[1]) hi[1] = g;
        if (bl < lo[2]) lo[2] = bl;
        if (bl > hi[2]) hi[2] = bl;
      }
  if (b->count) { memcpy(b->lo, lo, sizeof lo); memcpy(b->hi, hi, sizeof hi); }
}
/* (adapted from ilbm.c's quantize): the box with the most pixels times its longest side split at
   the median of that side, until there are maxc; each colour the true mean of its box */
static int q_median(const Hist *h, int maxc, u8 *pal)
{
  QBox *box = (QBox *)malloc(sizeof(QBox) * (size_t)maxc);
  int nb = 1, k;
  if (!box) return 0;
  box[0].lo[0] = box[0].lo[1] = box[0].lo[2] = 0; box[0].hi[0] = box[0].hi[1] = box[0].hi[2] = 31;
  qbox_fit(&box[0], h->cnt);
  if (!box[0].count) { free(box); return 0; }
  while (nb < maxc) {
    int best = -1, ax = 0, a, cut, v;
    unsigned long bestscore = 0, acc, half;
    QBox *b, *nbx;
    for (k = 0; k < nb; k++) {
      int side = 0, s;
      for (a = 0; a < 3; a++) { s = box[k].hi[a] - box[k].lo[a]; if (s > side) side = s; }
      if (side > 0 && box[k].count * (unsigned long)side > bestscore) { bestscore = box[k].count * (unsigned long)side; best = k; }
    }
    if (best < 0) break;
    b = &box[best];
    for (a = 0; a < 3; a++) if (b->hi[a] - b->lo[a] > b->hi[ax] - b->lo[ax]) ax = a;
    half = b->count / 2; acc = 0; cut = b->lo[ax];
    for (v = b->lo[ax]; v < b->hi[ax]; v++) {
      int r0 = ax == 0 ? v : b->lo[0], r1 = ax == 0 ? v : b->hi[0], g0 = ax == 1 ? v : b->lo[1], g1 = ax == 1 ? v : b->hi[1];
      int b0 = ax == 2 ? v : b->lo[2], b1 = ax == 2 ? v : b->hi[2], r, g, bl;
      for (r = r0; r <= r1; r++) for (g = g0; g <= g1; g++) for (bl = b0; bl <= b1; bl++) acc += h->cnt[(r << 10) | (g << 5) | bl];
      cut = v;
      if (acc >= half) break;
    }
    nbx = &box[nb++];
    *nbx = *b;
    b->hi[ax] = cut; nbx->lo[ax] = cut + 1;
    qbox_fit(b, h->cnt); qbox_fit(nbx, h->cnt);
  }
  for (k = 0; k < nb; k++) {
    unsigned long n = 0, sr = 0, sg = 0, sb = 0;
    int r, g, bl;
    for (r = box[k].lo[0]; r <= box[k].hi[0]; r++)
      for (g = box[k].lo[1]; g <= box[k].hi[1]; g++)
        for (bl = box[k].lo[2]; bl <= box[k].hi[2]; bl++) {
          int bin = (r << 10) | (g << 5) | bl;
          n += h->cnt[bin]; sr += h->sr[bin]; sg += h->sg[bin]; sb += h->sb[bin];
        }
    if (!n) n = 1;
    pal[k * 3] = (u8)((sr + n / 2) / n); pal[k * 3 + 1] = (u8)((sg + n / 2) / n); pal[k * 3 + 2] = (u8)((sb + n / 2) / n);
  }
  free(box);
  return nb;
}

/* ------------------------------------------------------------------ octree */
/* each histogram bin's mean colour put in a tree 6 levels deep (the top 6 bits of each channel);
   while there are more than maxc leaves, the reducible node at the deepest level with the fewest
   pixels has its children merged into it */
typedef struct { u32 n, r, g, b; i32 child[8]; u8 leaf, level, nkids; } ONode;
typedef struct { ONode *node; long nnodes, cap; int leaves; } Octree;
#define OCT_DEPTH 6
static long oct_alloc(Octree *t, int level)
{
  ONode *o;
  if (t->nnodes == t->cap) {
    long nc = t->cap ? t->cap * 2 : 1024;
    ONode *nn = (ONode *)realloc(t->node, sizeof(ONode) * (size_t)nc);
    if (!nn) return -1;
    t->node = nn; t->cap = nc;
  }
  o = &t->node[t->nnodes];
  memset(o, 0, sizeof *o);
  { int i; for (i = 0; i < 8; i++) o->child[i] = -1; }
  o->level = (u8)level;
  o->leaf = level == OCT_DEPTH;
  if (o->leaf) t->leaves++;
  return t->nnodes++;
}
static int oct_insert(Octree *t, int r, int g, int b, u32 n, u32 sr, u32 sg, u32 sb)
{
  long cur = 0;
  int level;
  for (level = 0; ; level++) {
    ONode *o = &t->node[cur];
    int ci, sh;
    if (o->leaf) { o->n += n; o->r += sr; o->g += sg; o->b += sb; return 1; }
    sh = 7 - level;
    ci = (((r >> sh) & 1) << 2) | (((g >> sh) & 1) << 1) | ((b >> sh) & 1);
    if (t->node[cur].child[ci] < 0) {
      long nn = oct_alloc(t, level + 1);
      if (nn < 0) return 0;
      t->node[cur].child[ci] = (i32)nn;
      t->node[cur].nkids++;
    }
    t->node[cur].n += n;
    cur = t->node[cur].child[ci];
  }
}
/* the fold: one node's subtree merged into it (its leaves' sums added up) */
static void oct_fold(Octree *t, long i)
{
  ONode *o = &t->node[i];
  int k;
  u32 r = 0, g = 0, b = 0, n = 0;
  for (k = 0; k < 8; k++) {
    long c = o->child[k];
    if (c < 0) continue;
    if (!t->node[c].leaf) oct_fold(t, c);
    o = &t->node[i];
    r += t->node[c].r; g += t->node[c].g; b += t->node[c].b; n += t->node[c].n;
    t->node[c].leaf = 0; t->node[c].n = 0;
    o->child[k] = -1;
    t->leaves--;
  }
  o->r = r; o->g = g; o->b = b; o->n = n; o->leaf = 1; o->nkids = 0;
  t->leaves++;
}
static int q_octree(const Hist *h, int maxc, u8 *pal)
{
  Octree t;
  long i;
  int bin, nc = 0;
  memset(&t, 0, sizeof t);
  if (oct_alloc(&t, 0) < 0) return 0;
  for (bin = 0; bin < 32768; bin++) {
    int r, g, b;
    if (!h->cnt[bin]) continue;
    bin_mean(h, bin, &r, &g, &b);
    if (!oct_insert(&t, r, g, b, h->cnt[bin], h->sr[bin], h->sg[bin], h->sb[bin])) { free(t.node); return 0; }
  }
  /* reduce: the deepest non-leaf nodes whose children are all leaves, the smallest first */
  while (t.leaves > maxc) {
    long best = -1;
    int bestlevel = -1;
    u32 bestn = 0;
    for (i = 0; i < t.nnodes; i++) {
      ONode *o = &t.node[i];
      int k, allleaf = 1, any = 0;
      if (o->leaf || !o->nkids) continue;
      for (k = 0; k < 8; k++) if (o->child[k] >= 0) { any = 1; if (!t.node[o->child[k]].leaf) allleaf = 0; }
      if (!any || !allleaf) continue;
      if (o->level > bestlevel || (o->level == bestlevel && o->n < bestn)) { best = i; bestlevel = o->level; bestn = o->n; }
    }
    if (best < 0) break;
    oct_fold(&t, best);
  }
  for (i = 0; i < t.nnodes && nc < maxc; i++) {
    ONode *o = &t.node[i];
    if (!o->leaf || !o->n) continue;
    pal[nc * 3] = (u8)((o->r + o->n / 2) / o->n); pal[nc * 3 + 1] = (u8)((o->g + o->n / 2) / o->n); pal[nc * 3 + 2] = (u8)((o->b + o->n / 2) / o->n);
    nc++;
  }
  free(t.node);
  return nc;
}

/* ------------------------------------------------------------------ Wu's variance-minimising cut */
/* cumulative moments over a 33 x 33 x 33 grid (index 0 is the empty border): weight, the three
   colour sums (32-bit: at most QSAMPLES pixels of 255) and the sum of squares (float). Each box is
   cut where the two halves' summed variance is least, the box with the most variance first */
#define WI(r, g, b) (((r) * 33 + (g)) * 33 + (b))
typedef struct { int r0, r1, g0, g1, b0, b1; long vol; } WBox;
typedef struct { long *wt, *mr, *mg, *mb; float *m2; } WMom;
static long wvol(const WBox *c, const long *m)
{
  return m[WI(c->r1, c->g1, c->b1)] - m[WI(c->r1, c->g1, c->b0)] - m[WI(c->r1, c->g0, c->b1)] + m[WI(c->r1, c->g0, c->b0)]
       - m[WI(c->r0, c->g1, c->b1)] + m[WI(c->r0, c->g1, c->b0)] + m[WI(c->r0, c->g0, c->b1)] - m[WI(c->r0, c->g0, c->b0)];
}
static double wvolf(const WBox *c, const float *m)
{
  return (double)m[WI(c->r1, c->g1, c->b1)] - m[WI(c->r1, c->g1, c->b0)] - m[WI(c->r1, c->g0, c->b1)] + m[WI(c->r1, c->g0, c->b0)]
       - m[WI(c->r0, c->g1, c->b1)] + m[WI(c->r0, c->g1, c->b0)] + m[WI(c->r0, c->g0, c->b1)] - m[WI(c->r0, c->g0, c->b0)];
}
/* the part of a box's moment below (bottom) or at (top) position pos along dir */
static long wbottom(const WBox *c, int dir, const long *m)
{
  switch (dir) {
  case 0: return -m[WI(c->r0, c->g1, c->b1)] + m[WI(c->r0, c->g1, c->b0)] + m[WI(c->r0, c->g0, c->b1)] - m[WI(c->r0, c->g0, c->b0)];
  case 1: return -m[WI(c->r1, c->g0, c->b1)] + m[WI(c->r1, c->g0, c->b0)] + m[WI(c->r0, c->g0, c->b1)] - m[WI(c->r0, c->g0, c->b0)];
  default: return -m[WI(c->r1, c->g1, c->b0)] + m[WI(c->r1, c->g0, c->b0)] + m[WI(c->r0, c->g1, c->b0)] - m[WI(c->r0, c->g0, c->b0)];
  }
}
static long wtop(const WBox *c, int dir, int pos, const long *m)
{
  switch (dir) {
  case 0: return m[WI(pos, c->g1, c->b1)] - m[WI(pos, c->g1, c->b0)] - m[WI(pos, c->g0, c->b1)] + m[WI(pos, c->g0, c->b0)];
  case 1: return m[WI(c->r1, pos, c->b1)] - m[WI(c->r1, pos, c->b0)] - m[WI(c->r0, pos, c->b1)] + m[WI(c->r0, pos, c->b0)];
  default: return m[WI(c->r1, c->g1, pos)] - m[WI(c->r1, c->g0, pos)] - m[WI(c->r0, c->g1, pos)] + m[WI(c->r0, c->g0, pos)];
  }
}
static double wvar(const WBox *c, const WMom *m)
{
  double dr = (double)wvol(c, m->mr), dg = (double)wvol(c, m->mg), db = (double)wvol(c, m->mb), w = (double)wvol(c, m->wt);
  if (w <= 0) return 0;
  return wvolf(c, m->m2) - (dr * dr + dg * dg + db * db) / w;
}
static double wmaximize(const WBox *c, int dir, int first, int last, int *cut, long wr, long wg, long wb, long ww, const WMom *m)
{
  long br = wbottom(c, dir, m->mr), bg = wbottom(c, dir, m->mg), bb = wbottom(c, dir, m->mb), bw = wbottom(c, dir, m->wt);
  double maxv = 0;
  int i;
  *cut = -1;
  for (i = first; i < last; i++) {
    double hr = (double)(br + wtop(c, dir, i, m->mr)), hg = (double)(bg + wtop(c, dir, i, m->mg)), hb = (double)(bb + wtop(c, dir, i, m->mb));
    double hw = (double)(bw + wtop(c, dir, i, m->wt)), t;
    if (hw <= 0) continue;
    t = (hr * hr + hg * hg + hb * hb) / hw;
    hr = wr - hr; hg = wg - hg; hb = wb - hb; hw = ww - hw;
    if (hw <= 0) continue;
    t += (hr * hr + hg * hg + hb * hb) / hw;
    if (t > maxv) { maxv = t; *cut = i; }
  }
  return maxv;
}
static int wcut(WBox *a, WBox *b, const WMom *m)
{
  int cr, cg, cb, dir;
  long wr = wvol(a, m->mr), wg = wvol(a, m->mg), wb = wvol(a, m->mb), ww = wvol(a, m->wt);
  double mr = wmaximize(a, 0, a->r0 + 1, a->r1, &cr, wr, wg, wb, ww, m);
  double mg = wmaximize(a, 1, a->g0 + 1, a->g1, &cg, wr, wg, wb, ww, m);
  double mb = wmaximize(a, 2, a->b0 + 1, a->b1, &cb, wr, wg, wb, ww, m);
  if (mr >= mg && mr >= mb) { dir = 0; if (cr < 0) return 0; }
  else if (mg >= mr && mg >= mb) dir = 1;
  else dir = 2;
  b->r1 = a->r1; b->g1 = a->g1; b->b1 = a->b1;
  switch (dir) {
  case 0: b->r0 = a->r1 = cr; b->g0 = a->g0; b->b0 = a->b0; break;
  case 1: if (cg < 0) return 0; b->g0 = a->g1 = cg; b->r0 = a->r0; b->b0 = a->b0; break;
  default: if (cb < 0) return 0; b->b0 = a->b1 = cb; b->r0 = a->r0; b->g0 = a->g0; break;
  }
  a->vol = (long)(a->r1 - a->r0) * (a->g1 - a->g0) * (a->b1 - a->b0);
  b->vol = (long)(b->r1 - b->r0) * (b->g1 - b->g0) * (b->b1 - b->b0);
  return 1;
}
static int q_wu(const Hist *h, int maxc, u8 *pal)
{
  WMom m;
  WBox *box = NULL;
  double *vv = NULL;
  int r, g, b, nb = 1, k, nc = 0;
  long n33 = 33L * 33 * 33;
  m.wt = (long *)calloc((size_t)n33, sizeof(long)); m.mr = (long *)calloc((size_t)n33, sizeof(long));
  m.mg = (long *)calloc((size_t)n33, sizeof(long)); m.mb = (long *)calloc((size_t)n33, sizeof(long));
  m.m2 = (float *)calloc((size_t)n33, sizeof(float));
  box = (WBox *)malloc(sizeof(WBox) * (size_t)maxc); vv = (double *)malloc(sizeof(double) * (size_t)maxc);
  if (!m.wt || !m.mr || !m.mg || !m.mb || !m.m2 || !box || !vv) goto done;
  for (r = 0; r < 32; r++) for (g = 0; g < 32; g++) for (b = 0; b < 32; b++) {
    int bin = (r << 10) | (g << 5) | b;
    u32 c = h->cnt[bin];
    long i = WI(r + 1, g + 1, b + 1);
    if (!c) continue;
    m.wt[i] = (long)c; m.mr[i] = (long)h->sr[bin]; m.mg[i] = (long)h->sg[bin]; m.mb[i] = (long)h->sb[bin];
    m.m2[i] = (float)(((double)h->sr[bin] * h->sr[bin] + (double)h->sg[bin] * h->sg[bin] + (double)h->sb[bin] * h->sb[bin]) / c);
  }
  /* the moments made cumulative, as Wu does: an area sum per red slice, then down red */
  for (r = 1; r <= 32; r++) {
    long area[33], ar[33], ag[33], ab[33];
    double a2[33];
    for (k = 0; k <= 32; k++) { area[k] = ar[k] = ag[k] = ab[k] = 0; a2[k] = 0; }
    for (g = 1; g <= 32; g++) {
      long line = 0, lr = 0, lg = 0, lb = 0;
      double l2 = 0;
      for (b = 1; b <= 32; b++) {
        long i = WI(r, g, b), p = WI(r - 1, g, b);
        line += m.wt[i]; lr += m.mr[i]; lg += m.mg[i]; lb += m.mb[i]; l2 += m.m2[i];
        area[b] += line; ar[b] += lr; ag[b] += lg; ab[b] += lb; a2[b] += l2;
        m.wt[i] = m.wt[p] + area[b]; m.mr[i] = m.mr[p] + ar[b]; m.mg[i] = m.mg[p] + ag[b]; m.mb[i] = m.mb[p] + ab[b];
        m.m2[i] = (float)(m.m2[p] + a2[b]);
      }
    }
  }
  /* cut the box with the largest variance each time */
  nb = 1;
  box[0].r0 = box[0].g0 = box[0].b0 = 0; box[0].r1 = box[0].g1 = box[0].b1 = 32;
  vv[0] = wvar(&box[0], &m);
  while (nb < maxc) {
    int next = 0, i;
    double best = vv[0];
    for (i = 1; i < nb; i++) if (vv[i] > best) { best = vv[i]; next = i; }
    if (best <= 0) break;
    if (wcut(&box[next], &box[nb], &m)) {
      vv[next] = box[next].vol > 1 ? wvar(&box[next], &m) : 0;
      vv[nb] = box[nb].vol > 1 ? wvar(&box[nb], &m) : 0;
      nb++;
    } else vv[next] = 0;                                    /* it can't be cut: never chosen again */
  }
  for (k = 0; k < nb; k++) {
    long w = wvol(&box[k], m.wt);
    if (w <= 0) continue;
    pal[nc * 3] = (u8)clamp255((int)((wvol(&box[k], m.mr) + w / 2) / w));
    pal[nc * 3 + 1] = (u8)clamp255((int)((wvol(&box[k], m.mg) + w / 2) / w));
    pal[nc * 3 + 2] = (u8)clamp255((int)((wvol(&box[k], m.mb) + w / 2) / w));
    nc++;
  }
done:
  free(m.wt); free(m.mr); free(m.mg); free(m.mb); free(m.m2); free(box); free(vv);
  return nc;
}

/* ------------------------------------------------------------------ refinement */
/* two rounds of k-means over the histogram's bins: each free entry moved to the mean of the bins
   nearest it (locked or forced entries, fixed[i] set, stay) */
static void refine(const Hist *h, u8 *pal, int n, const u8 *fixed, int rounds)
{
  unsigned long *acc = (unsigned long *)malloc(sizeof(unsigned long) * 4 * (size_t)n);
  int bin, i, it;
  if (!acc) return;
  for (it = 0; it < rounds; it++) {
    Near *c = near_new(pal, n, NULL);
    if (!c) break;
    memset(acc, 0, sizeof(unsigned long) * 4 * (size_t)n);
    for (bin = 0; bin < 32768; bin++) {
      int r, g, b, q;
      if (!h->cnt[bin]) continue;
      bin_mean(h, bin, &r, &g, &b);
      q = near_search(c, r, g, b);
      acc[q * 4] += h->sr[bin]; acc[q * 4 + 1] += h->sg[bin]; acc[q * 4 + 2] += h->sb[bin]; acc[q * 4 + 3] += h->cnt[bin];
    }
    free(c);
    for (i = 0; i < n; i++) {
      unsigned long w = acc[i * 4 + 3];
      if (fixed[i] || !w) continue;
      pal[i * 3] = (u8)((acc[i * 4] + w / 2) / w); pal[i * 3 + 1] = (u8)((acc[i * 4 + 1] + w / 2) / w); pal[i * 3 + 2] = (u8)((acc[i * 4 + 2] + w / 2) / w);
    }
  }
  free(acc);
}

/* ------------------------------------------------------------------ the palette for some runs */
/* pal: entries with fixed[i] set are given (locked or forced); the free ones (up to n) are made
   from the runs by method. Returns the number of entries used (the highest used + 1), 0 if out of
   memory. ocs12: the made colours rounded to 4 bits a gun */
static int build_palette(const Run *runs, int nruns, int n, int method, u8 *pal, u8 *fixed, int gray, int *exact)
{
  int nfree = 0, i, k, got, used = 0, ocs = (method & QM_OCS12) != 0;
  u8 *made = (u8 *)malloc(768);
  Hist h;
  method &= QM_MASK;
  if (!made) return 0;
  for (i = 0; i < n; i++) if (!fixed[i]) nfree++;
  got = 0;
  if (nfree > 0) {
    /* exact when the colours not already in the palette fit the free places */
    int ne = exact_colours(runs, nruns, 256, made, gray);
    if (ne >= 0) {
      int j, m = 0;
      for (j = 0; j < ne; j++) {
        int have = 0;
        for (i = 0; i < n && !have; i++) if (fixed[i] && !memcmp(pal + i * 3, made + j * 3, 3)) have = 1;
        if (!have) { memmove(made + m * 3, made + j * 3, 3); m++; }
      }
      if (m <= nfree) { got = m; ocs = 0; method = -1; if (exact) *exact = 1; }
    }
    if (method >= 0) {
      if (!hist_build(&h, runs, nruns, gray)) { free(made); return 0; }
      if (method == QM_OCTREE) got = q_octree(&h, nfree, made);
      else if (method == QM_WU) got = q_wu(&h, nfree, made);
      else got = q_median(&h, nfree, made);
    }
  }
  /* the made colours into the free places, in order */
  for (i = 0, k = 0; i < n; i++) {
    if (fixed[i]) { used = i + 1; continue; }
    if (k < got) { memcpy(pal + i * 3, made + k * 3, 3); k++; used = i + 1; }
    else { pal[i * 3] = pal[i * 3 + 1] = pal[i * 3 + 2] = 0; }
  }
  if (method >= 0 && nfree > 0) {
    if (got > 1) refine(&h, pal, used, fixed, 2);
    hist_free(&h);
  }
  if (ocs)
    for (i = 0; i < used; i++)
      if (!fixed[i]) { int c; for (c = 0; c < 3; c++) pal[i * 3 + c] = (u8)(((pal[i * 3 + c] + 8) / 17) * 17); }
  free(made);
  return used < 2 ? 2 : used;
}

/* ================================================================== modes */
/* the visible pixel layers as runs (all pixel layers if none is visible) */
static int doc_runs(Doc *d, Run *runs)
{
  int i, n = 0;
  long np = (long)d->w * d->h;
  for (i = 0; i < d->nlay; i++) if (is_pixel_layer(d->lay[i]) && d->lay[i]->visible) { runs[n].px = d->lay[i]->px; runs[n].n = np; n++; }
  if (!n) for (i = 0; i < d->nlay; i++) if (is_pixel_layer(d->lay[i])) { runs[n].px = d->lay[i]->px; runs[n].n = np; n++; }
  return n;
}
/* every pixel layer mapped to pal (n entries, lock for the tie-break) with dither: idx made for
   each (allocated first, so running out of memory changes nothing); all inside one undo step */
static int apply_palette(Doc *d, const char *action, const u8 *pal, int n, const u8 *lock, int dither, int gray, int mode)
{
  long np = (long)d->w * d->h;
  u8 *nidx[MAXLAYERS];
  Near *c;
  int i, ok = 1;
  u8 *pcopy = (u8 *)malloc(768), *lcopy = (u8 *)malloc(256);
  if (!pcopy || !lcopy) { free(pcopy); free(lcopy); return 0; }
  memset(pcopy, 0, 768); memcpy(pcopy, pal, (size_t)n * 3);
  memset(lcopy, 0, 256); if (lock) memcpy(lcopy, lock, (size_t)n);
  for (i = 0; i < d->nlay; i++) {
    nidx[i] = NULL;
    if (is_pixel_layer(d->lay[i]) && !(nidx[i] = (u8 *)malloc((size_t)np))) ok = 0;
  }
  c = ok ? near_new(pcopy, n, lcopy) : NULL;
  if (!c) { for (i = 0; i < d->nlay; i++) free(nidx[i]); free(pcopy); free(lcopy); return 0; }
  doc_begin(d, action);
  doc_note_all(d);
  for (i = 0; i < d->nlay; i++) {
    Layer *l = d->lay[i];
    if (!nidx[i]) continue;
    if (!map_pixels(l->px, l->px, d->w, d->h, nidx[i], c, dither, gray, 1)) map_pixels(l->px, l->px, d->w, d->h, nidx[i], c, DI_NONE, gray, 1);
    free(l->idx);
    l->idx = nidx[i];
  }
  memcpy(d->pal, pcopy, 768);
  memcpy(d->pal_lock, lcopy, 256);
  d->npal = n; d->mode = (u8)mode;
  doc_end(d);
  free(c); free(pcopy); free(lcopy);
  return 1;
}
int doc_to_indexed(Doc *d, int ncolors, int method, int dither, const u8 *fixed_pal, int nfixed)
{
  Run *runs;
  u8 *pal, fixed[256];
  int n, i, k, ok, nr, exact = 0;
  if (!d) return 0;
  if ((method & QM_MASK) == QM_FIXED) {
    if (!fixed_pal || nfixed < 1) return 0;
    n = nfixed > 256 ? 256 : nfixed;
    return apply_palette(d, "Indexed Color", fixed_pal, n < 2 ? 2 : n, NULL, dither, 0, MODE_INDEXED);
  }
  n = ncolors < 2 ? 2 : ncolors > 256 ? 256 : ncolors;
  if (!(pal = (u8 *)calloc(768, 1))) return 0;
  if (!(runs = (Run *)malloc(sizeof(Run) * MAXLAYERS))) { free(pal); return 0; }
  memset(fixed, 0, sizeof fixed);
  /* the locked entries keep their places, then the forced colours take the next free ones */
  for (i = 0; i < n && i < d->npal; i++) if (d->pal_lock[i]) { fixed[i] = 1; memcpy(pal + i * 3, d->pal + i * 3, 3); }
  for (k = 0, i = 0; fixed_pal && k < nfixed; k++) {
    while (i < n && fixed[i]) i++;
    if (i >= n) break;
    fixed[i] = 2; memcpy(pal + i * 3, fixed_pal + k * 3, 3);
  }
  nr = doc_runs(d, runs);
  n = build_palette(runs, nr, n, method, pal, fixed, 0, &exact);
  free(runs);
  if (!n) { free(pal); return 0; }
  for (i = 0; i < 256; i++) fixed[i] = fixed[i] == 1;     /* only the locked ones stay locked */
  ok = apply_palette(d, "Indexed Color", pal, n, fixed, exact ? DI_NONE : dither, 0, MODE_INDEXED);
  free(pal);
  return ok;
}
int doc_to_rgb(Doc *d)
{
  int i;
  if (!d) return 0;
  doc_begin(d, "RGB Color");
  doc_note_all(d);
  for (i = 0; i < d->nlay; i++) { free(d->lay[i]->idx); d->lay[i]->idx = NULL; }
  d->mode = MODE_RGB;
  doc_end(d);
  return 1;
}
int doc_to_gray(Doc *d)
{
  int i;
  long k, np;
  if (!d) return 0;
  np = (long)d->w * d->h;
  doc_begin(d, "Grayscale");
  doc_note_all(d);
  for (i = 0; i < d->nlay; i++) {
    Layer *l = d->lay[i];
    free(l->idx); l->idx = NULL;
    if (!l->px) continue;
    for (k = 0; k < np; k++) { u8 *p = l->px + k * 4; p[0] = p[1] = p[2] = (u8)luma(p[0], p[1], p[2]); }
  }
  for (i = 0; i < 256; i++) d->pal[i * 3] = d->pal[i * 3 + 1] = d->pal[i * 3 + 2] = (u8)i;
  memset(d->pal_lock, 0, sizeof d->pal_lock);
  d->npal = 256; d->mode = MODE_GRAY;
  doc_end(d);
  return 1;
}
int doc_to_bitmap(Doc *d, int method)
{
  static const u8 bw[6] = { 0, 0, 0, 255, 255, 255 };
  int dither = method == BMM_PATTERN ? DI_BAYER8 : method == BMM_DIFFUSION ? DI_FS : method == BMM_HALFTONE ? DI_PATTERN : DI_NONE;
  if (!d) return 0;
  return apply_palette(d, "Bitmap", bw, 2, NULL, dither, 1, MODE_BITMAP);
}

/* ================================================================== keeping layers in step */
void doc_index_sync(Doc *d, int li, Box b)
{
  Layer *l;
  Near *c;
  i32 x, y;
  if (!d || li < 0 || li >= d->nlay || d->mode == MODE_RGB) return;
  l = d->lay[li];
  if (!is_pixel_layer(l)) return;
  b = box_clip(b, doc_box(d));
  if (d->mode == MODE_GRAY) {
    for (y = b.y0; y < b.y1; y++)
      for (x = b.x0; x < b.x1; x++) { u8 *p = l->px + ((long)y * d->w + x) * 4; p[0] = p[1] = p[2] = (u8)luma(p[0], p[1], p[2]); }
    return;
  }
  if (!l->idx) {
    if (!(l->idx = (u8 *)calloc((size_t)d->w * d->h, 1))) return;
    b = doc_box(d);
  }
  if (box_empty(b) || !(c = near_new(d->pal, d->npal, d->pal_lock))) return;
  for (y = b.y0; y < b.y1; y++)
    for (x = b.x0; x < b.x1; x++) {
      long o = (long)y * d->w + x;
      u8 *p = l->px + o * 4;
      int q, r = p[0], g = p[1], bl = p[2];
      if (d->mode == MODE_BITMAP) r = g = bl = luma(r, g, bl);
      q = near_get(c, r, g, bl);
      l->idx[o] = (u8)q;
      p[0] = d->pal[q * 3]; p[1] = d->pal[q * 3 + 1]; p[2] = d->pal[q * 3 + 2];
    }
  free(c);
}
void doc_index_sync_all(Doc *d)
{
  int i;
  if (!d || d->mode == MODE_RGB) return;
  for (i = 0; i < d->nlay; i++) doc_index_sync(d, i, doc_box(d));
}
void doc_pal_changed(Doc *d, int first, int last)
{
  int i;
  long k, np;
  if (!d) return;
  if (first < 0) first = 0;
  if (last > 255) last = 255;
  if (first > last) return;
  np = (long)d->w * d->h;
  for (i = 0; i < d->nlay; i++) {
    Layer *l = d->lay[i];
    if (!l->px || !l->idx) continue;
    for (k = 0; k < np; k++) {
      int q = l->idx[k];
      if (q >= first && q <= last) { u8 *p = l->px + k * 4; const u8 *c = d->pal + q * 3; p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; }
    }
  }
}

/* ================================================================== preset palettes */
int pal_preset(int which, u8 *pal)
{
  /* Workbench 1.3 ($05A $FFF $002 $F80) and 2.0/3.x ($AAA $000 $FFF $68B) as their 12-bit values;
     MagicWB's eight as commonly published */
  static const u8 wb13[12] = { 0x00, 0x55, 0xAA, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x22, 0xFF, 0x88, 0x00 };
  static const u8 wb20[12] = { 0xAA, 0xAA, 0xAA, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x66, 0x88, 0xBB };
  static const u8 mwb[24] = { 149, 149, 149, 0, 0, 0, 255, 255, 255, 59, 103, 162, 123, 123, 123, 175, 175, 175, 170, 144, 124, 255, 169, 151 };
  int i, n = 0;
  switch (which) {
  case PP_WB13: memcpy(pal, wb13, 12); return 4;
  case PP_WB20: memcpy(pal, wb20, 12); return 4;
  case PP_MWB: memcpy(pal, mwb, 24); return 8;
  case PP_WEB216:
    for (i = 0; i < 216; i++) { pal[i * 3] = (u8)(i / 36 * 51); pal[i * 3 + 1] = (u8)(i / 6 % 6 * 51); pal[i * 3 + 2] = (u8)(i % 6 * 51); }
    return 216;
  case PP_GRAY2: n = 2; break;
  case PP_GRAY4: n = 4; break;
  case PP_GRAY16: n = 16; break;
  case PP_GRAY256: n = 256; break;
  case PP_RGB332:
    for (i = 0; i < 256; i++) { pal[i * 3] = (u8)((i >> 5) * 255 / 7); pal[i * 3 + 1] = (u8)(((i >> 2) & 7) * 255 / 7); pal[i * 3 + 2] = (u8)((i & 3) * 85); }
    return 256;
  case PP_OCS_CUBE:
    for (i = 0; i < 64; i++) { pal[i * 3] = (u8)((i >> 4) * 85); pal[i * 3 + 1] = (u8)(((i >> 2) & 3) * 85); pal[i * 3 + 2] = (u8)((i & 3) * 85); }
    return 64;
  case PP_EHB_DEFAULT:
    /* eight greys and 24 hues (12-bit values), then their half-bright twins */
    for (i = 0; i < 8; i++) pal[i * 3] = pal[i * 3 + 1] = pal[i * 3 + 2] = (u8)(i * 255 / 7 / 17 * 17);
    for (i = 0; i < 24; i++) {
      int r, g, b;
      pal_hsv_to_rgb(i * 15, i < 12 ? 255 : 150, 255, &r, &g, &b);
      pal[(8 + i) * 3] = (u8)((r + 8) / 17 * 17); pal[(8 + i) * 3 + 1] = (u8)((g + 8) / 17 * 17); pal[(8 + i) * 3 + 2] = (u8)((b + 8) / 17 * 17);
    }
    for (i = 0; i < 96; i++) pal[96 + i] = (u8)(pal[i] >> 1);
    return 64;
  default: return 0;
  }
  for (i = 0; i < n; i++) pal[i * 3] = pal[i * 3 + 1] = pal[i * 3 + 2] = (u8)(i * 255 / (n - 1));
  return n;
}

/* ================================================================== editing */
void pal_rgb_to_hsv(int r, int g, int b, int *h, int *s, int *v)
{
  int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b), d = mx - mn, hh;
  *v = mx;
  *s = mx ? (d * 255 + mx / 2) / mx : 0;
  if (!d) { *h = 0; return; }
  if (mx == r) hh = 60 * (g - b) / d;
  else if (mx == g) hh = 120 + 60 * (b - r) / d;
  else hh = 240 + 60 * (r - g) / d;
  *h = (hh + 360) % 360;
}
void pal_hsv_to_rgb(int h, int s, int v, int *r, int *g, int *b)
{
  int sec, f, p, q, t;
  h = ((h % 360) + 360) % 360; s = clamp255(s); v = clamp255(v);
  if (!s) { *r = *g = *b = v; return; }
  sec = h / 60; f = (h % 60) * 255 / 60;
  p = v * (255 - s) / 255; q = v * (255 - s * f / 255) / 255; t = v * (255 - s * (255 - f) / 255) / 255;
  switch (sec) {
  case 0: *r = v; *g = t; *b = p; break;
  case 1: *r = q; *g = v; *b = p; break;
  case 2: *r = p; *g = v; *b = t; break;
  case 3: *r = p; *g = q; *b = v; break;
  case 4: *r = t; *g = p; *b = v; break;
  default: *r = v; *g = p; *b = q; break;
  }
}
static int pal_ok(const Doc *d, int i) { return d && i >= 0 && i < 256; }
void pal_put_rgb(Doc *d, int i, int r, int g, int b)
{
  if (!pal_ok(d, i)) return;
  d->pal[i * 3] = (u8)clamp255(r); d->pal[i * 3 + 1] = (u8)clamp255(g); d->pal[i * 3 + 2] = (u8)clamp255(b);
  if (i >= d->npal) d->npal = i + 1;
  doc_pal_changed(d, i, i);
}
int pal_set_rgb(Doc *d, int i, int r, int g, int b)
{
  if (!pal_ok(d, i)) return 0;
  doc_begin(d, "Palette Colour");
  doc_note_pal(d);
  pal_put_rgb(d, i, r, g, b);
  doc_end(d);
  return 1;
}
int pal_set_hsv(Doc *d, int i, int h, int s, int v)
{
  int r, g, b;
  pal_hsv_to_rgb(h, s, v, &r, &g, &b);
  return pal_set_rgb(d, i, r, g, b);
}
int pal_spread(Doc *d, int a, int b)
{
  int lo = a < b ? a : b, hi = a < b ? b : a, i, c, n;
  if (!pal_ok(d, lo) || !pal_ok(d, hi) || hi - lo < 2) return 0;
  doc_begin(d, "Spread");
  doc_note_pal(d);
  n = hi - lo;
  for (i = lo + 1; i < hi; i++)
    for (c = 0; c < 3; c++) {
      int c0 = d->pal[lo * 3 + c], c1 = d->pal[hi * 3 + c];
      d->pal[i * 3 + c] = (u8)((c0 * (hi - i) + c1 * (i - lo) + n / 2) / n);
    }
  if (hi >= d->npal) d->npal = hi + 1;
  doc_pal_changed(d, lo, hi);
  doc_end(d);
  return 1;
}
int pal_swap(Doc *d, int a, int b, int remap)
{
  u8 t[3], tl;
  int i;
  long k, np;
  if (!pal_ok(d, a) || !pal_ok(d, b) || a == b) return 0;
  doc_begin(d, "Exchange Colours");
  if (remap) doc_note_all(d); else doc_note_pal(d);
  memcpy(t, d->pal + a * 3, 3); memcpy(d->pal + a * 3, d->pal + b * 3, 3); memcpy(d->pal + b * 3, t, 3);
  tl = d->pal_lock[a]; d->pal_lock[a] = d->pal_lock[b]; d->pal_lock[b] = tl;
  if (remap) {
    np = (long)d->w * d->h;
    for (i = 0; i < d->nlay; i++) {
      u8 *ix = d->lay[i]->idx;
      if (!ix) continue;
      for (k = 0; k < np; k++) { if (ix[k] == a) ix[k] = (u8)b; else if (ix[k] == b) ix[k] = (u8)a; }
    }
  } else {
    doc_pal_changed(d, a, a); doc_pal_changed(d, b, b);
  }
  doc_end(d);
  return 1;
}
int pal_copy(Doc *d, int from, int to)
{
  if (!pal_ok(d, from) || !pal_ok(d, to) || from == to) return 0;
  return pal_set_rgb(d, to, d->pal[from * 3], d->pal[from * 3 + 1], d->pal[from * 3 + 2]);
}
int pal_replace(Doc *d, const u8 *pal, int n)
{
  if (!d || !pal || n < 1) return 0;
  if (n > 256) n = 256;
  doc_begin(d, "Load Palette");
  doc_note_pal(d);
  memcpy(d->pal, pal, (size_t)n * 3);
  if (n > d->npal) d->npal = n;
  doc_pal_changed(d, 0, n - 1);
  doc_end(d);
  return 1;
}
int pal_remap(Doc *d, const u8 *pal, int n, int dither)
{
  if (!d || !pal || n < 1) return 0;
  return doc_to_indexed(d, n, QM_FIXED, dither, pal, n);
}
int pal_count_used(const Doc *d, unsigned long *counts)
{
  unsigned long *c = counts ? counts : (unsigned long *)malloc(sizeof(unsigned long) * 256);
  int i, used = 0;
  long k, np;
  if (!d || !c) return 0;
  memset(c, 0, sizeof(unsigned long) * 256);
  np = (long)d->w * d->h;
  for (i = 0; i < d->nlay; i++) {
    const u8 *ix = d->lay[i]->idx;
    const u8 *px = d->lay[i]->px;
    if (!ix || !px) continue;
    for (k = 0; k < np; k++) if (px[k * 4 + 3]) c[ix[k]]++;
  }
  for (i = 0; i < 256; i++) if (c[i]) used++;
  if (!counts) free(c);
  return used;
}
/* the indices of every layer sent through map (old entry -> new); in the step already begun */
static void remap_indices(Doc *d, const u8 *map)
{
  int i;
  long k, np = (long)d->w * d->h;
  for (i = 0; i < d->nlay; i++) {
    u8 *ix = d->lay[i]->idx;
    if (!ix) continue;
    for (k = 0; k < np; k++) ix[k] = map[ix[k]];
  }
}
static int sort_key(const u8 *c, int key, unsigned long use)
{
  int h, s, v;
  switch (key) {
  case PS_LUM: return luma(c[0], c[1], c[2]);
  case PS_SAT: pal_rgb_to_hsv(c[0], c[1], c[2], &h, &s, &v); return s * 256 + v;
  case PS_USE: return -(int)(use > 0x7FFFFF ? 0x7FFFFF : use);
  default:
    pal_rgb_to_hsv(c[0], c[1], c[2], &h, &s, &v);
    return s < 16 ? v - 1024 : h * 1024 + (s >> 2) * 4 + (v >> 6);   /* greys first, by brightness */
  }
}
typedef struct { unsigned long use[256]; int keyv[256], slot[256], order[256]; u8 map[256], np[768]; } SortWork;
int pal_sort(Doc *d, int key)
{
  SortWork *sw;                                              /* (off the stack: over 4 KB) */
  unsigned long *use;
  int *keyv, *slot, *order, ns = 0, i, j, n;
  u8 *np, *map;
  if (!d || d->npal < 2 || !(sw = (SortWork *)malloc(sizeof *sw))) return 0;
  use = sw->use; keyv = sw->keyv; slot = sw->slot; order = sw->order; map = sw->map; np = sw->np;
  n = d->npal;
  pal_count_used(d, use);
  for (i = 0; i < n; i++) if (!d->pal_lock[i]) { slot[ns] = i; order[ns] = i; keyv[i] = sort_key(d->pal + i * 3, key, use[i]); ns++; }
  /* insertion sort of the free entries by key (stable) */
  for (i = 1; i < ns; i++) { int t = order[i]; for (j = i; j > 0 && keyv[order[j - 1]] > keyv[t]; j--) order[j] = order[j - 1]; order[j] = t; }
  memcpy(np, d->pal, 768);
  for (i = 0; i < 256; i++) map[i] = (u8)i;
  for (i = 0; i < ns; i++) { memcpy(np + slot[i] * 3, d->pal + order[i] * 3, 3); map[order[i]] = (u8)slot[i]; }
  doc_begin(d, "Sort Palette");
  doc_note_all(d);
  memcpy(d->pal, np, 768);
  remap_indices(d, map);
  doc_end(d);
  free(sw);
  return 1;
}
int pal_remove_unused(Doc *d)
{
  unsigned long *use;
  u8 map[256], *np, nl[256];
  int i, k = 0, removed, c;
  if (!d || d->npal < 2 || !(use = (unsigned long *)malloc(sizeof(unsigned long) * 256))) return 0;
  pal_count_used(d, use);
  for (i = 0; i < d->npal; i++) if (use[i] || d->pal_lock[i]) k++;
  if (k == d->npal || !(np = (u8 *)calloc(768, 1))) { free(use); return 0; }
  memset(nl, 0, sizeof nl);
  for (i = 0, k = 0; i < 256; i++) map[i] = 0;
  for (i = 0; i < d->npal; i++)
    if (use[i] || d->pal_lock[i]) { map[i] = (u8)k; memcpy(np + k * 3, d->pal + i * 3, 3); nl[k] = d->pal_lock[i]; k++; }
  if (k < 2) { memcpy(np + k * 3, d->pal + (map[0] == 0 && !use[0] ? 0 : (d->npal - 1)) * 3, 3); k = 2; }
  removed = d->npal - k;
  doc_begin(d, "Remove Unused Colours");
  doc_note_all(d);
  remap_indices(d, map);
  /* the cycle ranges follow their ends to the entries they now fall on */
  for (c = 0; c < d->ncycle; c++) {
    int lo = d->cycle[c].lo, hi = d->cycle[c].hi, nlo = -1, nhi = -1;
    for (i = lo; i <= hi && i < 256; i++) if (use[i] || d->pal_lock[i]) { if (nlo < 0) nlo = map[i]; nhi = map[i]; }
    if (nlo < 0) nlo = nhi = 0;
    d->cycle[c].lo = (u8)nlo; d->cycle[c].hi = (u8)nhi;
  }
  memcpy(d->pal, np, 768); memcpy(d->pal_lock, nl, 256);
  d->npal = k;
  doc_end(d);
  free(np); free(use);
  return removed;
}
long doc_count_colors(const Doc *d, long max)
{
  u32 *key;
  long hs = 1024, n = 0, k, np;
  int i;
  if (!d) return 0;
  if (max < 1) max = 1;
  if (max > (1L << 20)) max = 1L << 20;
  while (hs < max * 2) hs <<= 1;
  if (!(key = (u32 *)malloc(sizeof(u32) * (size_t)hs))) return -1;
  for (k = 0; k < hs; k++) key[k] = 0x1000000UL;
  np = (long)d->w * d->h;
  for (i = 0; i < d->nlay && n < max; i++) {
    const Layer *l = d->lay[i];
    if (!is_pixel_layer(l) || !l->visible) continue;
    for (k = 0; k < np && n < max; k++) {
      const u8 *p = l->px + k * 4;
      u32 c, h;
      if (!p[3]) continue;
      c = ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];
      h = (u32)(((c * 2654435761UL) >> 8) & (u32)(hs - 1));
      while (key[h] != 0x1000000UL && key[h] != c) h = (h + 1) & (u32)(hs - 1);
      if (key[h] != c) { key[h] = c; n++; }
    }
  }
  free(key);
  return n;
}
int pal_make_ehb(Doc *d)
{
  int i;
  if (!d) return 0;
  doc_begin(d, "Extra Half-Brite");
  doc_note_pal(d);
  for (i = 0; i < 96; i++) d->pal[96 + i] = (u8)(d->pal[i] >> 1);
  d->npal = 64;
  doc_pal_changed(d, 32, 63);
  doc_end(d);
  return 1;
}

/* ================================================================== colour cycling */
int cycle_step(Doc *d, int r, int dir)
{
  CycleRange *c;
  u8 t[3];
  int lo, hi;
  if (!d || r < 0 || r >= d->ncycle) return 0;
  c = &d->cycle[r];
  lo = c->lo; hi = c->hi;
  if (hi <= lo) return 0;
  if (c->reverse) dir = -dir;
  if (dir >= 0) {
    memcpy(t, d->pal + hi * 3, 3);
    memmove(d->pal + (lo + 1) * 3, d->pal + lo * 3, (size_t)(hi - lo) * 3);
    memcpy(d->pal + lo * 3, t, 3);
  } else {
    memcpy(t, d->pal + lo * 3, 3);
    memmove(d->pal + lo * 3, d->pal + (lo + 1) * 3, (size_t)(hi - lo) * 3);
    memcpy(d->pal + hi * 3, t, 3);
  }
  doc_pal_changed(d, lo, hi);
  return 1;
}
/* CRNG: 16384 is 60 steps a second, so one step takes 16384000 / (60 rate) ms */
#define CYC_UNIT 16384000UL
u32 cycle_interval_ms(u16 rate) { return rate ? (u32)((CYC_UNIT / 60 + rate / 2) / rate) : 0; }
u16 cycle_rate_from_ms(u32 ms)
{
  u32 r;
  if (!ms) return 16384;
  r = (u32)((CYC_UNIT / 60 + ms / 2) / ms);
  return (u16)(r < 1 ? 1 : r > 16384 ? 16384 : r);
}
u32 cycle_rate_hz100(u16 rate) { return (u32)((rate * 6000UL + 8192) / 16384); }
int cycle_advance(Doc *d, u32 *acc, u32 elapsed_ms)
{
  int i, steps = 0;
  if (!d || !acc) return 0;
  if (elapsed_ms > 1000) elapsed_ms = 1000;
  for (i = 0; i < d->ncycle && i < MAXCYCLES; i++) {
    CycleRange *c = &d->cycle[i];
    int k = 0;
    if (!c->active || !c->rate || c->hi <= c->lo) { acc[i] = 0; continue; }
    acc[i] += elapsed_ms * (u32)c->rate * 60;
    while (acc[i] >= CYC_UNIT && k < 8) { acc[i] -= CYC_UNIT; cycle_step(d, i, 1); k++; }
    if (acc[i] >= CYC_UNIT) acc[i] %= CYC_UNIT;            /* (fallen far behind: catch up no more) */
    steps += k;
  }
  return steps;
}
u32 cycle_timer_ms(const Doc *d)
{
  u32 best = 0;
  int i;
  if (!d) return 0;
  for (i = 0; i < d->ncycle; i++) {
    const CycleRange *c = &d->cycle[i];
    u32 ms;
    if (!c->active || !c->rate || c->hi <= c->lo) continue;
    ms = cycle_interval_ms(c->rate);
    if (!best || ms < best) best = ms;
  }
  return best;
}
static u32 rd16(const u8 *p) { return ((u32)p[0] << 8) | p[1]; }
static u32 rd32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static u32 rd16le(const u8 *p) { return ((u32)p[1] << 8) | p[0]; }
static u32 rd32le(const u8 *p) { return ((u32)p[3] << 24) | ((u32)p[2] << 16) | ((u32)p[1] << 8) | p[0]; }
static void wr16(Buf *b, u32 v) { buf_addc(b, (int)((v >> 8) & 255)); buf_addc(b, (int)(v & 255)); }
static void wr32(Buf *b, u32 v) { wr16(b, v >> 16); wr16(b, v & 0xFFFF); }
static void wr16le(Buf *b, u32 v) { buf_addc(b, (int)(v & 255)); buf_addc(b, (int)((v >> 8) & 255)); }
static void wr32le(Buf *b, u32 v) { wr16le(b, v & 0xFFFF); wr16le(b, v >> 16); }
int cycle_parse_chunk(const char *id, const u8 *c, size_t len, CycleRange *ranges, int *n)
{
  CycleRange r;
  memset(&r, 0, sizeof r);
  if (*n >= MAXCYCLES) return 0;
  if (!memcmp(id, "CRNG", 4) && len >= 8) {
    /* WORD pad, WORD rate, WORD flags (1 active, 2 reverse), UBYTE low, UBYTE high */
    r.rate = (u16)rd16(c + 2); r.active = (u8)(rd16(c + 4) & 1); r.reverse = (u8)((rd16(c + 4) >> 1) & 1);
    r.lo = c[6]; r.hi = c[7];
  } else if (!memcmp(id, "CCRT", 4) && len >= 14) {
    /* WORD direction (0 none, 1 forward, -1 back), UBYTE start, end, LONG seconds, LONG microseconds */
    int dir = (int)(rd16(c) ^ 0x8000) - 0x8000;
    double us = (double)rd32(c + 4) * 1e6 + (double)rd32(c + 8), rate;
    r.lo = c[2]; r.hi = c[3]; r.active = dir != 0; r.reverse = dir < 0;
    rate = us > 0 ? 16384.0 * 1e6 / (60.0 * us) : 16384;
    r.rate = (u16)(rate < 1 ? 1 : rate > 16384 ? 16384 : rate + 0.5);
  } else if (!memcmp(id, "DRNG", 4) && len >= 8) {
    /* UBYTE min, max, WORD rate, WORD flags, UBYTE ntrue, nregs; ntrue DColors (cell, r, g, b), nregs
       DIndexes (cell, index). Only the registers are kept: the range is their lowest to highest */
    int ntrue = c[6], nregs = c[7], i, lo = 256, hi = -1;
    size_t o = 8 + (size_t)ntrue * 4;
    if (o + (size_t)nregs * 2 > len || !nregs) return 0;
    for (i = 0; i < nregs; i++) { int ix = c[o + i * 2 + 1]; if (ix < lo) lo = ix; if (ix > hi) hi = ix; }
    r.lo = (u8)lo; r.hi = (u8)hi; r.rate = (u16)rd16(c + 2); r.active = (u8)(rd16(c + 4) & 1);
  } else return 0;
  if (r.hi <= r.lo) return 0;                                /* (DPaint saves its unused ranges too) */
  ranges[(*n)++] = r;
  return 1;
}
int cycle_read_iff(const u8 *d, size_t n, CycleRange *ranges, int max)
{
  size_t end, o;
  int got = 0;
  CycleRange tmp[MAXCYCLES];
  if (n < 12 || memcmp(d, "FORM", 4)) return 0;
  end = rd32(d + 4);
  end = end > n - 8 ? n : end + 8;
  for (o = 12; o + 8 <= end; ) {
    size_t len = rd32(d + o + 4), avail = end - o - 8;
    if (len > avail) len = avail;
    cycle_parse_chunk((const char *)d + o, d + o + 8, len, tmp, &got);
    o += 8 + len + (len & 1);
  }
  if (got > max) got = max;
  memcpy(ranges, tmp, sizeof(CycleRange) * (size_t)got);
  return got;
}
void cycle_write_chunks(Buf *b, const CycleRange *ranges, int n, int which)
{
  int i;
  for (i = 0; i < n; i++) {
    const CycleRange *r = &ranges[i];
    if (which == 1) {
      u32 ms = cycle_interval_ms(r->rate ? r->rate : 1);
      buf_add(b, "CCRT", 4); wr32(b, 14);
      wr16(b, !r->active ? 0 : r->reverse ? 0xFFFF : 1);
      buf_addc(b, r->lo); buf_addc(b, r->hi);
      wr32(b, ms / 1000); wr32(b, (ms % 1000) * 1000); wr16(b, 0);
    } else if (which == 2) {
      int k, nr = r->hi - r->lo + 1 > 255 ? 255 : r->hi - r->lo + 1;   /* (nregs is a byte) */
      u32 len = 8 + (u32)nr * 2;
      buf_add(b, "DRNG", 4); wr32(b, len);
      buf_addc(b, r->lo); buf_addc(b, r->hi); wr16(b, r->rate); wr16(b, r->active ? 1 : 0);
      buf_addc(b, 0); buf_addc(b, nr);
      for (k = 0; k < nr; k++) { buf_addc(b, r->lo + k); buf_addc(b, r->lo + k); }
      if (len & 1) buf_addc(b, 0);
    } else {
      buf_add(b, "CRNG", 4); wr32(b, 8);
      wr16(b, 0); wr16(b, r->rate); wr16(b, (u32)(r->active ? 1 : 0) | (r->reverse ? 2 : 0));
      buf_addc(b, r->lo); buf_addc(b, r->hi);
    }
  }
}

/* ================================================================== palette files */
const char *const palfile_names[PF_N + 1] = { "Adobe Color Table (.act)", "Adobe Color Swatches (.aco)", "Adobe Swatch Exchange (.ase)",
  "GIMP Palette (.gpl)", "JASC Palette (.pal)", "RIFF Palette (.pal)", "IFF ILBM Palette (CMAP)", "PNG Palette (PLTE)", NULL };
const char *const palfile_ext[PF_N] = { "act", "aco", "ase", "gpl", "pal", "pal", "iff", "png" };

static int ext_is(const char *name, const char *ext)
{
  const char *dot = name ? strrchr(name, '.') : NULL;
  return dot && str_ieq(dot + 1, ext);
}
int palfile_detect(const u8 *d, size_t n, const char *name)
{
  static const u8 pngsig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
  if (n >= 8 && !memcmp(d, pngsig, 8)) return PF_PNG;
  if (n >= 12 && !memcmp(d, "FORM", 4) && (!memcmp(d + 8, "ILBM", 4) || !memcmp(d + 8, "PBM ", 4))) return PF_IFF;
  if (n >= 12 && !memcmp(d, "ASEF", 4)) return PF_ASE;
  if (n >= 12 && !memcmp(d, "GIMP Palette", 12)) return PF_GPL;
  if (n >= 8 && !memcmp(d, "JASC-PAL", 8)) return PF_JASC;
  if (n >= 12 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "PAL ", 4)) return PF_RIFF;
  if (n >= 4 && (rd16(d) == 1 || rd16(d) == 2) && (ext_is(name, "aco") || n == 4 + rd16(d + 2) * 10UL)) return PF_ACO;
  if (n == 768 || n == 772 || ext_is(name, "act")) return n >= 768 ? PF_ACT : -1;
  return -1;
}
/* IEEE 754 single precision, big-endian, without assuming the host's float layout */
static double f32_get(const u8 *p)
{
  u32 v = rd32(p), m = v & 0x7FFFFF;
  int e = (int)((v >> 23) & 255);
  double r;
  if (e == 255) return 0;
  r = e ? ldexp((double)(m | 0x800000), e - 150) : ldexp((double)m, -149);
  return (v >> 31) ? -r : r;
}
static void f32_put(Buf *b, double v)
{
  u32 bits = 0, m;
  int e;
  if (v < 0) { bits = 0x80000000UL; v = -v; }
  if (v > 0) {
    double f = frexp(v, &e);                                 /* v = f 2^e, f in [0.5, 1) */
    m = (u32)((f * 2 - 1) * 8388608.0 + 0.5);
    e += 126;
    if (m >= 0x800000) { m = 0; e++; }
    if (e <= 0) { m = 0; e = 0; }                            /* (too small: zero) */
    bits |= ((u32)e << 23) | m;
  }
  wr32(b, bits);
}
/* CIE Lab (D50) to sRGB, for ACO and ASE Lab swatches */
static void lab_to_rgb(double L, double A, double B, u8 *out)
{
  double fy = (L + 16) / 116, fx = fy + A / 500, fz = fy - B / 200, xyz[3], rgb[3];
  double t[3];
  int i;
  t[0] = fx; t[1] = fy; t[2] = fz;
  for (i = 0; i < 3; i++) t[i] = t[i] > 6.0 / 29 ? t[i] * t[i] * t[i] : 3 * (6.0 / 29) * (6.0 / 29) * (t[i] - 4.0 / 29);
  xyz[0] = t[0] * 0.9642; xyz[1] = t[1]; xyz[2] = t[2] * 0.8249;
  /* D50 XYZ straight to linear sRGB (Bradford-adapted matrix) */
  rgb[0] = 3.1339 * xyz[0] - 1.6169 * xyz[1] - 0.4906 * xyz[2];
  rgb[1] = -0.9788 * xyz[0] + 1.9161 * xyz[1] + 0.0335 * xyz[2];
  rgb[2] = 0.0719 * xyz[0] - 0.2290 * xyz[1] + 1.4052 * xyz[2];
  for (i = 0; i < 3; i++) {
    double c = rgb[i] <= 0 ? 0 : rgb[i] >= 1 ? 1 : rgb[i];
    c = c <= 0.0031308 ? 12.92 * c : 1.055 * pow(c, 1 / 2.4) - 0.055;
    out[i] = (u8)clamp255((int)(c * 255 + 0.5));
  }
}
static int pf_fail(char *err, size_t cap, const char *msg) { codec_err(err, cap, msg); return 0; }
/* a text file's next line (without its end) into line; 0 at the end */
static int next_line(const u8 *d, size_t n, size_t *o, char *line, size_t cap)
{
  size_t k = 0;
  if (*o >= n) return 0;
  while (*o < n && d[*o] != '\n' && d[*o] != '\r') { if (k + 1 < cap) line[k++] = (char)d[*o]; (*o)++; }
  if (*o < n && d[*o] == '\r') (*o)++;
  if (*o < n && d[*o] == '\n') (*o)++;
  line[k] = 0;
  return 1;
}
static int read_aco(const u8 *d, size_t n, PalFile *p, char *err, size_t cap)
{
  size_t o = 0;
  int ver = (int)rd16(d), cnt = (int)rd16(d + 2), i;
  o = 4;
  if (o + (size_t)cnt * 10 > n) return pf_fail(err, cap, "Damaged ACO file");
  for (i = 0; i < cnt; i++) {
    u32 sp = rd16(d + o), w = rd16(d + o + 2), x = rd16(d + o + 4), y = rd16(d + o + 6), z = rd16(d + o + 8);
    u8 *c = p->rgb + p->n * 3;
    o += 10;
    if (ver == 2) {                                          /* a name follows: DWORD length (UTF-16 units, with the 0) */
      u32 len;
      if (o + 4 > n) return pf_fail(err, cap, "Damaged ACO file");
      len = rd32(d + o); o += 4;
      if (len > (n - o) / 2) return pf_fail(err, cap, "Damaged ACO file");
      o += (size_t)len * 2;
      if (o + 10 > n && i + 1 < cnt) return pf_fail(err, cap, "Damaged ACO file");
    }
    if (p->n >= 256) continue;
    switch (sp) {
    case 0: c[0] = (u8)(w >> 8); c[1] = (u8)(x >> 8); c[2] = (u8)(y >> 8); break;               /* RGB 0-65535 */
    case 1: { int r, g, b; pal_hsv_to_rgb((int)((w * 360UL + 32768) / 65536), (int)(x >> 8), (int)(y >> 8), &r, &g, &b);   /* HSB */
              c[0] = (u8)r; c[1] = (u8)g; c[2] = (u8)b; break; }
    case 2: /* CMYK, 0 = 100% ink */
      c[0] = (u8)((unsigned long)w * z / 65535 * 255 / 65535); c[1] = (u8)((unsigned long)x * z / 65535 * 255 / 65535);
      c[2] = (u8)((unsigned long)y * z / 65535 * 255 / 65535); break;
    case 7: lab_to_rgb(w / 100.0, (double)(i16)x / 100.0, (double)(i16)y / 100.0, c); break;     /* Lab */
    case 8: c[0] = c[1] = c[2] = (u8)(255 - (w > 10000 ? 10000 : w) * 255UL / 10000); break;     /* grey, 10000 black */
    default: c[0] = c[1] = c[2] = 0; break;
    }
    p->n++;
  }
  /* a version 1 section may be followed by version 2 with the same colours and names: ignored */
  return p->n > 0 ? 1 : pf_fail(err, cap, "The ACO file has no colours");
}
static int read_ase(const u8 *d, size_t n, PalFile *p, char *err, size_t cap)
{
  size_t o = 12;
  u32 blocks = rd32(d + 8), b;
  for (b = 0; b < blocks && o + 6 <= n; b++) {
    u32 type = rd16(d + o), len = rd32(d + o + 2);
    const u8 *c = d + o + 6;
    o += 6;
    if (len > n - o) return pf_fail(err, cap, "Damaged ASE file");
    if ((type == 0x0001 || type == 0xC001) && len >= 2) {
      u32 nl = rd16(c), k;
      size_t q = 2 + (size_t)nl * 2;
      if (q > len) return pf_fail(err, cap, "Damaged ASE file");
      if (type == 0xC001 && !p->name[0])                     /* a group's name: the palette's */
        for (k = 0; k + 1 < nl && k < sizeof p->name - 1; k++) { u32 ch = rd16(c + 2 + k * 2); p->name[k] = (char)(ch < 128 ? ch : '?'); }
      if (type == 0x0001 && q + 4 <= len && p->n < 256) {
        const u8 *m = c + q;
        u8 *out = p->rgb + p->n * 3;
        q += 4;
        if (!memcmp(m, "RGB ", 4) && q + 12 <= len) { int i; for (i = 0; i < 3; i++) out[i] = (u8)clamp255((int)(f32_get(c + q + i * 4) * 255 + 0.5)); }
        else if (!memcmp(m, "CMYK", 4) && q + 16 <= len) {
          double k4 = 1 - f32_get(c + q + 12);
          int i;
          for (i = 0; i < 3; i++) out[i] = (u8)clamp255((int)((1 - f32_get(c + q + i * 4)) * k4 * 255 + 0.5));
        } else if (!memcmp(m, "Gray", 4) && q + 4 <= len) out[0] = out[1] = out[2] = (u8)clamp255((int)(f32_get(c + q) * 255 + 0.5));
        else if (!memcmp(m, "LAB ", 4) && q + 12 <= len) {
          double L = f32_get(c + q);
          lab_to_rgb(L <= 1.5 ? L * 100 : L, f32_get(c + q + 4), f32_get(c + q + 8), out);
        } else { o += len; continue; }
        p->n++;
      }
    }
    o += len;
  }
  return p->n > 0 ? 1 : pf_fail(err, cap, "The ASE file has no colours");
}
static int read_text_pal(const u8 *d, size_t n, PalFile *p, int fmt, char *err, size_t cap)
{
  char line[256];
  size_t o = 0;
  int ln = 0;
  while (next_line(d, n, &o, line, sizeof line)) {
    int r, g, b;
    ln++;
    if (fmt == PF_GPL) {
      if (ln == 1) continue;
      if (!strncmp(line, "Name:", 5)) {
        const char *s = line + 5;
        while (*s == ' ') s++;
        strncpy(p->name, s, sizeof p->name - 1);
        continue;
      }
      if (line[0] == '#' || !strncmp(line, "Columns:", 8)) continue;
    } else if (ln <= 3) continue;                            /* JASC-PAL, 0100, the count */
    if (sscanf(line, "%d %d %d", &r, &g, &b) == 3 && p->n < 256) {
      u8 *c = p->rgb + p->n * 3;
      c[0] = (u8)clamp255(r); c[1] = (u8)clamp255(g); c[2] = (u8)clamp255(b);
      p->n++;
    }
  }
  return p->n > 0 ? 1 : pf_fail(err, cap, "The palette file has no colours");
}
static int read_riff(const u8 *d, size_t n, PalFile *p, char *err, size_t cap)
{
  size_t o = 12, end = rd32le(d + 4) + 8;
  if (end > n) end = n;
  while (o + 8 <= end) {
    size_t len = rd32le(d + o + 4);
    if (len > end - o - 8) len = end - o - 8;
    if (!memcmp(d + o, "data", 4) && len >= 4) {
      int cnt = (int)rd16le(d + o + 10), i;
      if ((size_t)cnt * 4 > len - 4) cnt = (int)((len - 4) / 4);
      for (i = 0; i < cnt && i < 256; i++) memcpy(p->rgb + i * 3, d + o + 12 + i * 4, 3);
      p->n = cnt > 256 ? 256 : cnt;
      return p->n > 0 ? 1 : pf_fail(err, cap, "The palette file has no colours");
    }
    o += 8 + len + (len & 1);
  }
  return pf_fail(err, cap, "Damaged RIFF palette");
}
static int read_iffpal(const u8 *d, size_t n, PalFile *p, char *err, size_t cap)
{
  size_t end = rd32(d + 4), o;
  end = end > n - 8 ? n : end + 8;
  for (o = 12; o + 8 <= end; ) {
    size_t len = rd32(d + o + 4), avail = end - o - 8;
    if (len > avail) len = avail;
    if (!memcmp(d + o, "CMAP", 4)) { p->n = (int)(len / 3 > 256 ? 256 : len / 3); memcpy(p->rgb, d + o + 8, (size_t)p->n * 3); }
    o += 8 + len + (len & 1);
  }
  p->ncycle = cycle_read_iff(d, n, p->cycle, MAXCYCLES);
  return p->n > 0 ? 1 : pf_fail(err, cap, "The IFF file has no CMAP");
}
static int read_pngpal(const u8 *d, size_t n, PalFile *p, char *err, size_t cap)
{
  size_t o = 8;
  while (o + 12 <= n) {
    size_t len = rd32(d + o);
    const u8 *c = d + o + 8;
    if (len > n - o - 12) break;
    if (!memcmp(d + o + 4, "PLTE", 4)) { p->n = (int)(len / 3 > 256 ? 256 : len / 3); memcpy(p->rgb, c, (size_t)p->n * 3); }
    else if (!memcmp(d + o + 4, "tRNS", 4)) { size_t i; for (i = 0; i < len && i < 256; i++) if (c[i] == 0 && p->transparent < 0) p->transparent = (int)i; }
    else if (!memcmp(d + o + 4, "IEND", 4)) break;
    o += 12 + len;
  }
  return p->n > 0 ? 1 : pf_fail(err, cap, "The PNG file has no palette");
}
int palfile_read(const u8 *d, size_t n, const char *name, PalFile *p, char *err, size_t errcap)
{
  int fmt = palfile_detect(d, n, name);
  memset(p, 0, sizeof *p);
  p->transparent = -1;
  switch (fmt) {
  case PF_ACT: {
    int cnt = 256;
    if (n >= 772) {
      cnt = (int)rd16(d + 768);
      if (cnt < 1 || cnt > 256) cnt = 256;
      if (rd16(d + 770) < 256) p->transparent = (int)rd16(d + 770);
    }
    memcpy(p->rgb, d, (size_t)cnt * 3); p->n = cnt;
    return 1;
  }
  case PF_ACO: return read_aco(d, n, p, err, errcap);
  case PF_ASE: return read_ase(d, n, p, err, errcap);
  case PF_GPL: case PF_JASC: return read_text_pal(d, n, p, fmt, err, errcap);
  case PF_RIFF: return read_riff(d, n, p, err, errcap);
  case PF_IFF: return read_iffpal(d, n, p, err, errcap);
  case PF_PNG: return read_pngpal(d, n, p, err, errcap);
  }
  return pf_fail(err, errcap, "Not a palette file Easel knows");
}
static void utf16_name(Buf *b, const char *s, int with_len32)
{
  size_t l = strlen(s), i;
  if (with_len32) wr32(b, (u32)l + 1); else wr16(b, (u32)l + 1);
  for (i = 0; i < l; i++) wr16(b, (u8)s[i]);
  wr16(b, 0);
}
u8 *palfile_write(int fmt, const PalFile *p, size_t *len)
{
  Buf b;
  int i, n = p->n < 1 ? 1 : p->n > 256 ? 256 : p->n;
  char nm[40];
  buf_init(&b);
  switch (fmt) {
  case PF_ACT:
    buf_add(&b, p->rgb, (size_t)n * 3);
    for (i = n * 3; i < 768; i++) buf_addc(&b, 0);
    wr16(&b, (u32)n); wr16(&b, p->transparent >= 0 && p->transparent < n ? (u32)p->transparent : 0xFFFF);
    break;
  case PF_ACO: {
    int ver;
    for (ver = 1; ver <= 2; ver++) {
      wr16(&b, (u32)ver); wr16(&b, (u32)n);
      for (i = 0; i < n; i++) {
        wr16(&b, 0); wr16(&b, p->rgb[i * 3] * 257U); wr16(&b, p->rgb[i * 3 + 1] * 257U); wr16(&b, p->rgb[i * 3 + 2] * 257U); wr16(&b, 0);
        if (ver == 2) { sprintf(nm, "Colour %d", i); utf16_name(&b, nm, 1); }
      }
    }
    break;
  }
  case PF_ASE: {
    const char *gname = p->name[0] ? p->name : "Easel";
    buf_add(&b, "ASEF", 4); wr16(&b, 1); wr16(&b, 0); wr32(&b, (u32)n + 2);
    wr16(&b, 0xC001); wr32(&b, 2 + 2 * ((u32)strlen(gname) + 1)); utf16_name(&b, gname, 0);
    for (i = 0; i < n; i++) {
      sprintf(nm, "Colour %d", i);
      wr16(&b, 0x0001); wr32(&b, 2 + 2 * ((u32)strlen(nm) + 1) + 4 + 12 + 2);
      utf16_name(&b, nm, 0);
      buf_add(&b, "RGB ", 4);
      f32_put(&b, p->rgb[i * 3] / 255.0); f32_put(&b, p->rgb[i * 3 + 1] / 255.0); f32_put(&b, p->rgb[i * 3 + 2] / 255.0);
      wr16(&b, 2);                                             /* a normal (process) colour */
    }
    wr16(&b, 0xC002); wr32(&b, 0);
    break;
  }
  case PF_GPL:
    buf_printf(&b, "GIMP Palette\nName: %s\nColumns: 16\n#\n", p->name[0] ? p->name : "Easel");
    for (i = 0; i < n; i++) buf_printf(&b, "%3d %3d %3d\tIndex %d\n", p->rgb[i * 3], p->rgb[i * 3 + 1], p->rgb[i * 3 + 2], i);
    break;
  case PF_JASC:
    buf_printf(&b, "JASC-PAL\r\n0100\r\n%d\r\n", n);
    for (i = 0; i < n; i++) buf_printf(&b, "%d %d %d\r\n", p->rgb[i * 3], p->rgb[i * 3 + 1], p->rgb[i * 3 + 2]);
    break;
  case PF_RIFF:
    buf_add(&b, "RIFF", 4); wr32le(&b, 4 + 8 + 4 + (u32)n * 4); buf_add(&b, "PAL ", 4);
    buf_add(&b, "data", 4); wr32le(&b, 4 + (u32)n * 4); wr16le(&b, 0x300); wr16le(&b, (u32)n);
    for (i = 0; i < n; i++) { buf_add(&b, p->rgb + i * 3, 3); buf_addc(&b, 0); }
    break;
  case PF_IFF: {
    size_t lenpos;
    buf_add(&b, "FORM", 4); lenpos = b.len; wr32(&b, 0); buf_add(&b, "ILBM", 4);
    buf_add(&b, "CMAP", 4); wr32(&b, (u32)n * 3); buf_add(&b, p->rgb, (size_t)n * 3);
    if ((n * 3) & 1) buf_addc(&b, 0);
    cycle_write_chunks(&b, p->cycle, p->ncycle, 0);
    if (!b.oom) { u32 l = (u32)(b.len - 8); u8 *q = (u8 *)b.data + lenpos; q[0] = (u8)(l >> 24); q[1] = (u8)(l >> 16); q[2] = (u8)(l >> 8); q[3] = (u8)l; }
    break;
  }
  case PF_PNG: {
    u8 idx[256], trns[256];
    u8 *png;
    size_t pl;
    for (i = 0; i < n; i++) { idx[i] = (u8)i; trns[i] = 255; }
    if (p->transparent >= 0 && p->transparent < n) trns[p->transparent] = 0;
    png = png_encode_indexed(idx, n, 1, p->rgb, n, trns, p->transparent >= 0 && p->transparent < n ? p->transparent + 1 : 0, 0, &pl);
    buf_free(&b);
    if (png) *len = pl;
    return png;
  }
  default: buf_free(&b); return NULL;
  }
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return (u8 *)buf_take(&b);
}
void palfile_from_doc(const Doc *d, PalFile *p)
{
  memset(p, 0, sizeof *p);
  p->n = d->npal < 1 ? 1 : d->npal;
  memcpy(p->rgb, d->pal, (size_t)p->n * 3);
  p->transparent = -1;
  p->ncycle = d->ncycle > MAXCYCLES ? MAXCYCLES : d->ncycle;
  memcpy(p->cycle, d->cycle, sizeof(CycleRange) * (size_t)p->ncycle);
}

/* ================================================================== for the writers */
u8 *doc_flat_rgba(Doc *d)
{
  u8 *out = (u8 *)malloc((size_t)d->w * d->h * 4);
  if (out) doc_composite(d, doc_box(d), out, (long)d->w * 4);
  return out;
}
u8 *doc_flat_indices(Doc *d, u8 **alpha)
{
  long np = (long)d->w * d->h, k;
  u8 *rgba, *idx, *al = NULL;
  Near *c;
  if (alpha) *alpha = NULL;
  if (d->mode != MODE_INDEXED && d->mode != MODE_BITMAP) return NULL;
  if (!(rgba = doc_flat_rgba(d))) return NULL;
  idx = (u8 *)malloc((size_t)np);
  if (alpha) al = (u8 *)malloc((size_t)np);
  c = near_new(d->pal, d->npal, d->pal_lock);
  if (!idx || (alpha && !al) || !c) { free(rgba); free(idx); free(al); free(c); return NULL; }
  for (k = 0; k < np; k++) {
    const u8 *p = rgba + k * 4;
    int i, q = -1;
    /* the topmost layer showing here: its index, if the composite shows exactly its colour */
    for (i = d->nlay - 1; i >= 0; i--) {
      const Layer *l = d->lay[i];
      if (!is_pixel_layer(l) || !l->visible || !l->opacity || !l->px[k * 4 + 3]) continue;
      if (l->idx) q = l->idx[k];
      break;
    }
    if (q < 0 || q >= d->npal || memcmp(d->pal + q * 3, p, 3)) q = near_get(c, p[0], p[1], p[2]);
    idx[k] = (u8)q;
    if (al) al[k] = p[3];
  }
  free(rgba); free(c);
  if (alpha) *alpha = al;
  return idx;
}
int flat_transparent(u8 *idx, const u8 *alpha, long n, u8 *pal, int *npal)
{
  long k;
  int slot = -1, i;
  u8 used[256];
  if (!alpha) return -1;
  for (k = 0; k < n && alpha[k] >= 128; k++) ;
  if (k == n) return -1;
  memset(used, 0, sizeof used);
  for (k = 0; k < n; k++) if (alpha[k] >= 128) used[idx[k]] = 1;
  if (*npal < 256) { slot = (*npal)++; pal[slot * 3] = pal[slot * 3 + 1] = pal[slot * 3 + 2] = 0; }
  else for (i = 0; i < 256 && slot < 0; i++) if (!used[i]) slot = i;
  if (slot < 0) return -1;
  for (k = 0; k < n; k++) if (alpha[k] < 128) idx[k] = (u8)slot;
  return slot;
}
int quantize_rgba(const u8 *px, i32 w, i32 h, int n, int method, int dither, u8 *pal, int nfixed, u8 *idx)
{
  Run run;
  u8 fixed[256];
  Near *c;
  int i, got, exact = 0;
  run.px = px; run.n = (long)w * h;
  if ((method & QM_MASK) == QM_FIXED) got = nfixed < 1 ? 1 : nfixed > 256 ? 256 : nfixed;
  else {
    n = n < 2 ? 2 : n > 256 ? 256 : n;
    memset(fixed, 0, sizeof fixed);
    for (i = 0; i < nfixed && i < n; i++) fixed[i] = 1;
    if (!(got = build_palette(&run, 1, n, method, pal, fixed, 0, &exact))) return 0;
  }
  if (!idx) return got;
  if (!(c = near_new(pal, got, NULL))) return 0;
  if (!map_pixels(NULL, px, w, h, idx, c, exact ? DI_NONE : dither, 0, 0)) got = 0;
  free(c);
  return got;
}
int map_rgba(const u8 *px, i32 w, i32 h, const u8 *pal, int n, int dither, u8 *idx)
{
  Near *c = near_new(pal, n, NULL);
  int ok;
  if (!c) return 0;
  ok = map_pixels(NULL, px, w, h, idx, c, dither, 0, 0);
  free(c);
  return ok;
}
u8 *doc_png_encode(Doc *d, int dpi, size_t *len)
{
  u8 *out = NULL;
  if (d->mode == MODE_INDEXED || d->mode == MODE_BITMAP) {
    u8 *al = NULL, *idx = doc_flat_indices(d, &al), pal[768], trns[256];
    int npal = d->npal, slot, i;
    if (!idx) { free(al); return NULL; }
    memcpy(pal, d->pal, 768);
    slot = flat_transparent(idx, al, (long)d->w * d->h, pal, &npal);
    for (i = 0; i < 256; i++) trns[i] = 255;
    if (slot >= 0) trns[slot] = 0;
    out = png_encode_indexed(idx, d->w, d->h, pal, npal, trns, slot >= 0 ? slot + 1 : 0, dpi, len);
    free(idx); free(al);
  } else {
    Rgba im;
    im.w = d->w; im.h = d->h;
    if (!(im.px = doc_flat_rgba(d))) return NULL;
    out = d->mode == MODE_GRAY ? png_encode_gray(&im, dpi, len) : png_encode(&im, dpi, len);
    free(im.px);
  }
  return out;
}
