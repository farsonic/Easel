/*
 * type.c - type layers (see type.h): runs and editing, fonts and kerning (the kern table and
 * GPOS pair adjustment), layout with word wrap, alignment and justification, the caret, Warp
 * Text, and drawing through ttraster.c's glyphs (or their outlines and path.c's filler when the
 * text is warped, transformed, faux italic or bigger than the rasteriser draws). Easel 0.9.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "type.h"
#include "ttraster.h"
#include "amifont.h"
#include "live.h"
#include "gsub.h"                      /* 1.2: OpenType features */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const char *const text_warp_names[TW_N] = { "None", "Arc", "Arch", "Bulge", "Flag", "Wave", "Fish", "Rise", "Squeeze", "Twist",
  "Arc Lower", "Arc Upper", "Shell Lower", "Shell Upper", "Fisheye", "Inflate" };

static u32 be16(const u8 *p) { return ((u32)p[0] << 8) | p[1]; }
static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static int sbe16(const u8 *p) { int v = (int)be16(p); return v >= 32768 ? v - 65536 : v; }

/* ------------------------------------------------------------------ fonts */
FontSet *text_fonts;
static Face *default_lookup(const char *family, int bold, int italic)
{
  return text_fonts ? fonts_face(text_fonts, family, bold, italic) : NULL;
}
Face *(*text_face_lookup)(const char *family, int bold, int italic) = default_lookup;
struct AmiFont *(*text_amifont_lookup)(const char *family, int size);

/* font files kept open between renderings, and each face's kerning tables */
#define NCACHE 6
typedef struct { Face *face; int hint, ok; TTRaster r; unsigned use; } RasterCache;
typedef struct { Face *face; u8 *kern, *gpos, *gsub; u32 lkern, lgpos, lgsub; unsigned use; } KernCache;   /* (1.2: and GSUB) */
static RasterCache rcache[NCACHE];
static KernCache kcache[NCACHE];
static unsigned use_clock;
void text_cache_flush(void)
{
  int i;
  for (i = 0; i < NCACHE; i++) {
    if (rcache[i].face) ttr_close(&rcache[i].r);
    free(kcache[i].kern); free(kcache[i].gpos); free(kcache[i].gsub);
  }
  memset(rcache, 0, sizeof rcache);
  memset(kcache, 0, sizeof kcache);
}
static TTRaster *raster_for(Face *f, int hint)
{
  int i, lru = 0;
  for (i = 0; i < NCACHE; i++)
    if (rcache[i].face == f && rcache[i].hint == hint) { rcache[i].use = ++use_clock; return rcache[i].ok ? &rcache[i].r : NULL; }
  for (i = 1; i < NCACHE; i++) if (rcache[i].use < rcache[lru].use) lru = i;
  if (rcache[lru].face) ttr_close(&rcache[lru].r);
  memset(&rcache[lru], 0, sizeof rcache[lru]);
  rcache[lru].face = f; rcache[lru].hint = hint; rcache[lru].use = ++use_clock;
  rcache[lru].ok = ttr_open(&rcache[lru].r, f->path, f->index);
  if (rcache[lru].ok && hint) ttr_hint(&rcache[lru].r, f->latin_glyph);
  return rcache[lru].ok ? &rcache[lru].r : NULL;
}
/* one table of a face's file, malloc'd (at most 4 MB); NULL if it hasn't one */
static u8 *read_table(Face *f, const char *tag, u32 *len)
{
  FILE *fp = fopen(f->path, "rb");
  u8 hdr[12], *dir = NULL, *t = NULL;
  u32 ntab, i;
  long fsize;
  int cff;
  *len = 0;
  if (!fp) return NULL;
  fseek(fp, 0, SEEK_END); fsize = ftell(fp);
  if (!sfnt_header(fp, f->index, hdr, &cff, fsize)) goto done;
  ntab = be16(hdr + 4);
  if (ntab > 200 || !(dir = (u8 *)malloc(ntab * 16)) || fread(dir, 16, ntab, fp) != ntab) goto done;
  for (i = 0; i < ntab; i++)
    if (!memcmp(dir + 16 * i, tag, 4)) {
      u32 off = be32(dir + 16 * i + 8), l = be32(dir + 16 * i + 12);
      if (l < 8 || l > 4L * 1024 * 1024 || (long)off > fsize || (long)l > fsize - (long)off) break;
      if (!(t = (u8 *)malloc(l)) || fseek(fp, (long)off, SEEK_SET) || fread(t, 1, l, fp) != l) { free(t); t = NULL; break; }
      *len = l;
      break;
    }
done:
  free(dir);
  fclose(fp);
  return t;
}
static KernCache *kern_for(Face *f)
{
  int i, lru = 0;
  for (i = 0; i < NCACHE; i++) if (kcache[i].face == f) { kcache[i].use = ++use_clock; return &kcache[i]; }
  for (i = 1; i < NCACHE; i++) if (kcache[i].use < kcache[lru].use) lru = i;
  free(kcache[lru].kern); free(kcache[lru].gpos); free(kcache[lru].gsub);
  memset(&kcache[lru], 0, sizeof kcache[lru]);
  kcache[lru].face = f; kcache[lru].use = ++use_clock;
  kcache[lru].gpos = read_table(f, "GPOS", &kcache[lru].lgpos);
  kcache[lru].kern = read_table(f, "kern", &kcache[lru].lkern);
  kcache[lru].gsub = read_table(f, "GSUB", &kcache[lru].lgsub);
  return &kcache[lru];
}
/* GPOS: a glyph's index in a Coverage table, -1 if it isn't covered */
static int coverage(const u8 *t, u32 len, u32 o, u16 g)
{
  u32 n, lo, hi;
  if (o + 4 > len) return -1;
  n = be16(t + o + 2);
  if (be16(t + o) == 1) {
    if (o + 4 + 2 * n > len) return -1;
    lo = 0; hi = n;
    while (lo < hi) { u32 m = (lo + hi) / 2, v = be16(t + o + 4 + 2 * m); if (v == g) return (int)m; if (v < g) lo = m + 1; else hi = m; }
  } else if (be16(t + o) == 2) {
    if (o + 4 + 6 * n > len) return -1;
    lo = 0; hi = n;
    while (lo < hi) {
      u32 m = (lo + hi) / 2, s = be16(t + o + 4 + 6 * m), e = be16(t + o + 6 + 6 * m);
      if (g >= s && g <= e) return (int)(be16(t + o + 8 + 6 * m) + g - s);
      if (g > e) lo = m + 1; else hi = m;
    }
  }
  return -1;
}
static u32 class_of(const u8 *t, u32 len, u32 o, u16 g)
{
  u32 n, i;
  if (o + 6 > len) return 0;
  if (be16(t + o) == 1) {
    u32 s = be16(t + o + 2);
    n = be16(t + o + 4);
    if (g >= s && g < s + n && o + 6 + 2 * (g - s) + 2 <= len) return be16(t + o + 6 + 2 * (g - s));
  } else if (be16(t + o) == 2) {
    n = be16(t + o + 2);
    for (i = 0; i < n && o + 4 + 6 * i + 6 <= len; i++)
      if (g >= be16(t + o + 4 + 6 * i) && g <= be16(t + o + 6 + 6 * i)) return be16(t + o + 8 + 6 * i);
  }
  return 0;
}
static int popc(u32 v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }
/* a PairPos subtable: the first glyph's x advance change, if the pair is in it */
static int pairpos(const u8 *t, u32 len, u32 so, u16 l, u16 r, int *val)
{
  u32 fmt, vf1, vf2, s1, s2, xo;
  int ci;
  if (so + 16 > len) return 0;
  fmt = be16(t + so); vf1 = be16(t + so + 4); vf2 = be16(t + so + 6);
  if (!(vf1 & 4)) return 0;
  if ((ci = coverage(t, len, so + be16(t + so + 2), l)) < 0) return 0;
  s1 = 2 * (u32)popc(vf1 & 0xFF); s2 = 2 * (u32)popc(vf2 & 0xFF); xo = 2 * (u32)popc(vf1 & 3);
  if (fmt == 1) {
    u32 n = be16(t + so + 8), ps, cnt, lo, hi, rs = 2 + s1 + s2;
    if ((u32)ci >= n || so + 10 + 2 * (u32)ci + 2 > len) return 0;
    ps = so + be16(t + so + 10 + 2 * ci);
    if (ps + 2 > len) return 0;
    cnt = be16(t + ps);
    if (ps + 2 + cnt * rs > len) return 0;
    lo = 0; hi = cnt;
    while (lo < hi) {
      u32 m = (lo + hi) / 2, g2 = be16(t + ps + 2 + m * rs);
      if (g2 == r) { *val = sbe16(t + ps + 2 + m * rs + 2 + xo); return 1; }
      if (g2 < r) lo = m + 1; else hi = m;
    }
  } else if (fmt == 2) {
    u32 c1 = class_of(t, len, so + be16(t + so + 8), l), c2 = class_of(t, len, so + be16(t + so + 10), r);
    u32 n1 = be16(t + so + 12), n2 = be16(t + so + 14), rec;
    if (c1 >= n1 || c2 >= n2) return 0;
    rec = so + 16 + (c1 * n2 + c2) * (s1 + s2);
    if (rec + s1 > len) return 0;
    *val = sbe16(t + rec + xo);
    return *val != 0;
  }
  return 0;
}
static int gpos_kern(const u8 *t, u32 len, u16 l, u16 r, int *val)
{
  u32 fl, ll, nf, i, k, j;
  if (len < 10) return 0;
  fl = be16(t + 6); ll = be16(t + 8);
  if (fl + 2 > len || ll + 2 > len) return 0;
  nf = be16(t + fl);
  for (i = 0; i < nf && fl + 2 + 6 * i + 6 <= len; i++) {
    u32 fo, nl;
    if (memcmp(t + fl + 2 + 6 * i, "kern", 4)) continue;
    fo = fl + be16(t + fl + 2 + 6 * i + 4);
    if (fo + 4 > len) continue;
    nl = be16(t + fo + 2);
    for (k = 0; k < nl && fo + 4 + 2 * k + 2 <= len; k++) {
      u32 li = be16(t + fo + 4 + 2 * k), lo, type, ns;
      if (li >= be16(t + ll) || ll + 2 + 2 * li + 2 > len) continue;
      lo = ll + be16(t + ll + 2 + 2 * li);
      if (lo + 6 > len) continue;
      type = be16(t + lo); ns = be16(t + lo + 4);
      for (j = 0; j < ns && lo + 6 + 2 * j + 2 <= len; j++) {
        u32 so = lo + be16(t + lo + 6 + 2 * j), ty = type;
        if (ty == 9) {                                        /* an extension: the real subtable further on */
          if (so + 8 > len) continue;
          ty = be16(t + so + 2); so += be32(t + so + 4);
        }
        if (ty == 2 && pairpos(t, len, so, l, r, val)) return 1;
      }
    }
  }
  return 0;
}
static int kern_table(const u8 *t, u32 len, u16 l, u16 r)
{
  u32 n, off = 4, i, key = ((u32)l << 16) | r;
  if (len < 4 || be16(t) != 0) return 0;
  n = be16(t + 2);
  for (i = 0; i < n && off + 14 <= len; i++) {
    u32 sl = be16(t + off + 2), cov = be16(t + off + 4);
    if ((cov >> 8) == 0 && (cov & 1)) {
      u32 np = be16(t + off + 6), lo = 0, hi = np;
      if (off + 14 + np * 6 > len) return 0;
      while (lo < hi) {
        u32 m = (lo + hi) / 2, k = be32(t + off + 14 + 6 * m);
        if (k == key) return sbe16(t + off + 14 + 6 * m + 4);
        if (k < key) lo = m + 1; else hi = m;
      }
    }
    if (sl < 6) break;
    off += sl;
  }
  return 0;
}
int text_kern(Face *f, u16 left, u16 right)
{
  KernCache *k;
  int v = 0;
  if (!f || !left || !right) return 0;
  k = kern_for(f);
  if (k->gpos && gpos_kern(k->gpos, k->lgpos, left, right, &v)) return v;
  if (k->kern) return kern_table(k->kern, k->lkern, left, right);
  return 0;
}

/* ------------------------------------------------------------------ settings */
static double xf_scale(const TextParams *tp)
{
  double s;
  if (!tp->xf_on) return 1;
  s = sqrt(fabs(tp->xf[0] * tp->xf[4] - tp->xf[1] * tp->xf[3]));
  return s > 0 ? s : 1;
}
/* settings made safe to use (as read from a file, or by a careless caller) */
static void sanitize(TextParams *tp)
{
  i32 n, i;
  tp->text[TP_MAXTEXT - 1] = 0;
  n = (i32)strlen(tp->text);
  if (tp->nruns < 1) { tp->nruns = 1; if (tp->run[0].size <= 0) tp->run[0].size = 24; }
  if (tp->nruns > TP_MAXRUNS) tp->nruns = TP_MAXRUNS;
  tp->run[0].start = 0;
  for (i = 0; i < tp->nruns; i++) {
    TextRun *r = &tp->run[i];
    r->family[sizeof r->family - 1] = 0;
    if (i && r->start < tp->run[i - 1].start) r->start = tp->run[i - 1].start;
    if (r->start > n) r->start = n;
    if (!(r->size >= 1)) r->size = r->size > 0 ? 1 : 24;
    if (r->size > 2000) r->size = 2000;
    if (!(fabs(r->tracking) <= 5000)) r->tracking = 0;
    if (!(fabs(r->baseline) <= 10000)) r->baseline = 0;
    r->color &= 0xFFFFFF;
  }
  if (tp->align > TA_JUSTIFY_RIGHT) tp->align = TA_LEFT;
  if (!(tp->indent_left >= 0 && tp->indent_left < 100000)) tp->indent_left = 0;      /* (1.2) */
  if (!(tp->indent_right >= 0 && tp->indent_right < 100000)) tp->indent_right = 0;
  if (tp->warp >= TW_N) tp->warp = TW_NONE;
  if (!(tp->bw >= 0)) tp->bw = 0;
  if (!(tp->bh >= 0)) tp->bh = 0;
  if (!(tp->leading >= 0 && tp->leading < 100000)) tp->leading = 0;
}
void text_params_init(TextParams *tp, const char *family, double size, u32 color, double x, double y)
{
  memset(tp, 0, sizeof *tp);
  tp->x = x; tp->y = y;
  tp->aa = TAA_SMOOTH; tp->kern = 1;
  tp->xf[0] = tp->xf[4] = 1;
  tp->nruns = 1;
  if (family) { strncpy(tp->run[0].family, family, sizeof tp->run[0].family - 1); }
  tp->run[0].size = size > 0 ? size : 24;
  tp->run[0].color = color & 0xFFFFFF;
  tp->run[0].ot = TO_LIGA;                 /* (1.2: standard ligatures on, as PhotoCraft has them) */
}
int text_set_string(TextParams *tp, const char *s)
{
  size_t n = strlen(s);
  i32 i;
  if (n >= TP_MAXTEXT) {
    n = TP_MAXTEXT - 1;
    while (n > 0 && ((u8)s[n] & 0xC0) == 0x80) n--;        /* (cut between characters) */
  }
  memcpy(tp->text, s, n);
  tp->text[n] = 0;
  for (i = 1; i < tp->nruns; i++) if (tp->run[i].start >= (i32)n) { tp->nruns = i; break; }
  return n == strlen(s);
}
int text_run_at(const TextParams *tp, int at)
{
  int i;
  for (i = tp->nruns - 1; i > 0; i--) if (tp->run[i].start < at || (tp->run[i].start == at && at == 0)) return i;
  return 0;
}
/* runs that cover nothing dropped, and neighbours alike merged */
static int same_style(const TextRun *a, const TextRun *b)
{
  return !strcmp(a->family, b->family) && a->bold == b->bold && a->italic == b->italic && a->faux_bold == b->faux_bold &&
         a->faux_italic == b->faux_italic && a->underline == b->underline && a->strike == b->strike && a->color == b->color &&
         a->size == b->size && a->tracking == b->tracking && a->baseline == b->baseline && a->ot == b->ot;
}
static void tidy_runs(TextParams *tp)
{
  i32 n = (i32)strlen(tp->text), i, k = 0;
  for (i = 0; i < tp->nruns; i++) {
    TextRun *r = &tp->run[i];
    i32 end = i + 1 < tp->nruns ? tp->run[i + 1].start : n;
    if (n > 0 && end <= r->start) continue;                /* (covers nothing) */
    if (n == 0 && k > 0) continue;                          /* (empty text: one run) */
    if (k > 0 && same_style(&tp->run[k - 1], r)) continue;
    tp->run[k++] = *r;
  }
  if (!k) k = 1;
  tp->nruns = k;
  tp->run[0].start = 0;
}
int text_insert(TextParams *tp, int at, const char *s, int len)
{
  i32 n = (i32)strlen(tp->text), i;
  if (len < 0) len = (int)strlen(s);
  if (at < 0) at = 0;
  if (at > n) at = n;
  if (n + len >= TP_MAXTEXT) return 0;
  memmove(tp->text + at + len, tp->text + at, (size_t)(n - at) + 1);
  memcpy(tp->text + at, s, (size_t)len);
  for (i = 1; i < tp->nruns; i++) if (tp->run[i].start >= at && at > 0) tp->run[i].start += len;
    else if (tp->run[i].start > at) tp->run[i].start += len;
  return 1;
}
int text_delete(TextParams *tp, int from, int to)
{
  i32 n = (i32)strlen(tp->text), i;
  if (from < 0) from = 0;
  if (to > n) to = n;
  if (to <= from) return 0;
  memmove(tp->text + from, tp->text + to, (size_t)(n - to) + 1);
  for (i = 1; i < tp->nruns; i++) {
    if (tp->run[i].start >= to) tp->run[i].start -= to - from;
    else if (tp->run[i].start > from) tp->run[i].start = from;
  }
  /* runs now starting together: the later one wins */
  for (i = 1; i < tp->nruns; i++)
    if (tp->run[i].start == tp->run[i - 1].start) {
      i32 s = tp->run[i - 1].start;
      memmove(&tp->run[i - 1], &tp->run[i], (size_t)(tp->nruns - i) * sizeof(TextRun));
      tp->nruns--; i--;
      tp->run[i].start = s;
    }
  tidy_runs(tp);
  return 1;
}
/* a run boundary at byte at (the run there split); its index, -1 if no room */
static int split_at(TextParams *tp, i32 at)
{
  int r, i;
  for (i = 0; i < tp->nruns; i++) if (tp->run[i].start == at) return i;
  if (tp->nruns >= TP_MAXRUNS) return -1;
  /* r holds the character at 'at': the new run is a copy of it from at */
  for (r = tp->nruns - 1; r > 0 && tp->run[r].start > at; r--) ;
  memmove(&tp->run[r + 2], &tp->run[r + 1], (size_t)(tp->nruns - r - 1) * sizeof(TextRun));
  tp->run[r + 1] = tp->run[r];
  tp->run[r + 1].start = at;
  tp->nruns++;
  return r + 1;
}
/* 1.2: bits of the runs' ot set (on) or cleared on from..to; the whole text when from >= to */
int text_ot_set(TextParams *tp, int from, int to, u16 bits, u16 value)
{
  i32 n = (i32)strlen(tp->text), i;
  if (from >= to) { from = 0; to = n; }
  if (from < 0) from = 0;
  if (to > n) to = n;
  if (n == 0) { tp->run[0].ot = (u16)((tp->run[0].ot & ~bits) | (value & bits)); return 1; }
  if (to <= from || split_at(tp, from) < 0 || (to < n && split_at(tp, to) < 0)) return 0;
  for (i = 0; i < tp->nruns; i++) {
    TextRun *r = &tp->run[i];
    if (r->start < from || r->start >= to) continue;
    r->ot = (u16)((r->ot & ~bits) | (value & bits));
  }
  tidy_runs(tp);
  sanitize(tp);
  return 1;
}
int text_style(TextParams *tp, int from, int to, const TextRun *st, u32 m)
{
  i32 n = (i32)strlen(tp->text), i;
  int a, b;
  if (from < 0) from = 0;
  if (to > n) to = n;
  if (n == 0) { from = 0; to = 0; }
  else if (to <= from) return 0;
  if ((a = split_at(tp, from)) < 0) return 0;
  b = to < n ? split_at(tp, to) : tp->nruns;
  if (b < 0) return 0;
  for (i = 0; i < tp->nruns; i++) {
    TextRun *r = &tp->run[i];
    if (r->start < from || (r->start >= to && !(n == 0 && i == 0))) continue;
    if (m & TS_FAMILY) memcpy(r->family, st->family, sizeof r->family);
    if (m & TS_BOLD) r->bold = st->bold;
    if (m & TS_ITALIC) r->italic = st->italic;
    if (m & TS_FAUXBOLD) r->faux_bold = st->faux_bold;
    if (m & TS_FAUXITALIC) r->faux_italic = st->faux_italic;
    if (m & TS_UNDERLINE) r->underline = st->underline;
    if (m & TS_STRIKE) r->strike = st->strike;
    if (m & TS_COLOR) r->color = st->color & 0xFFFFFF;
    if (m & TS_SIZE) r->size = st->size;
    if (m & TS_TRACKING) r->tracking = st->tracking;
    if (m & TS_BASELINE) r->baseline = st->baseline;
    if (m & TS_OT) r->ot = st->ot;                       /* 1.2 */
  }
  (void)a; (void)b;
  tidy_runs(tp);
  sanitize(tp);
  return 1;
}

/* ------------------------------------------------------------------ layout */
typedef struct {
  Face *face; AmiFont *ami;
  double size, asc, desc, lead;       /* pixels */
} RunFont;
static void run_font(const TextRun *r, RunFont *f)
{
  memset(f, 0, sizeof *f);
  f->size = r->size;
  if (text_amifont_lookup && (f->ami = text_amifont_lookup(r->family, (int)(r->size + 0.5))) != NULL) {
    double s = r->size / f->ami->ysize;
    f->asc = (f->ami->baseline + 1) * s; f->desc = (f->ami->ysize - f->ami->baseline - 1) * s; f->lead = f->ami->ysize * s;
    return;
  }
  if (text_face_lookup && (f->face = text_face_lookup(r->family, r->bold, r->italic)) != NULL && !ttf_load(f->face)) f->face = NULL;
  if (f->face && f->face->upm) {
    f->asc = (double)f->face->ascent * r->size / f->face->upm;
    f->desc = (double)f->face->descent * r->size / f->face->upm;
    f->lead = (double)(f->face->ascent + f->face->descent + f->face->linegap) * r->size / f->face->upm;
    if (f->lead <= 0) f->lead = r->size * 1.2;
  } else { f->asc = r->size * 0.8; f->desc = r->size * 0.2; f->lead = r->size * 1.2; }
}
/* a glyph's id and its advance along the line, pixels (as text.c works it out: whole 1/64ths) */
static u16 glyph_of(const RunFont *f, u32 cp)
{
  if (f->ami) return (u16)(cp < 256 ? cp : '?');
  return f->face ? ttf_glyph(f->face, cp) : 0;
}
static double hadv(const RunFont *f, u16 g)
{
  if (f->ami) { int a, k, w; amifont_char(f->ami, g, &a, &k, &w, NULL); return a * f->size / f->ami->ysize; }
  if (f->face && f->face->upm && f->face->adv && f->face->nmetrics) {
    u16 a = f->face->adv[g < f->face->nmetrics ? g : f->face->nmetrics - 1];
    return floor((double)a * f->size * 64 / f->face->upm) / 64;
  }
  return f->size * 0.5;
}
static int is_space(u32 c) { return c == ' ' || c == '\t' || c == 0xA0 || c == 0x3000; }
/* ---- 1.2: case, position and OpenType features ---- */
const char *const text_ot_tags[TO_NFEAT] = { "liga", "dlig", "swsh", "onum", "salt", "titl", "ornm", "ordn", "frac", "calt" };
const char *const text_ot_names[TO_NFEAT] = { "Standard Ligatures", "Discretionary Ligatures", "Swash", "Oldstyle", "Stylistic Alternates",
  "Titling Alternates", "Ornaments", "Ordinals", "Fractions", "Contextual Alternates" };
/* a letter's capital (Latin-1, Latin Extended-A's pairs, Greek and Cyrillic's basic letters) */
u32 text_upper(u32 c)
{
  if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) return c - 32;
  if (c == 0xFF) return 0x178;
  if (c >= 0x100 && c <= 0x17F && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17F) {
    /* pairs: the capital first (0x139-0x148 and 0x179-0x17E the other way round) */
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c & 1) ? c : c - 1;
    return (c & 1) ? c - 1 : c;
  }
  if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return c - 32;
  if (c >= 0x430 && c <= 0x44F) return c - 32;
  if (c >= 0x450 && c <= 0x45F) return c - 80;
  return c;
}
static double glyph_rise(const TextRun *r)
{
  int p = r->ot & TO_POS;
  return p == TO_SUPER ? r->size * 0.333 : p == TO_SUB ? -r->size * 0.333 : 0;
}
/* 1 if the face has true small capitals (smcp) */
static int small_caps_of(Face *f)
{
  KernCache *k = kern_for(f);
  return k->gsub && gsub_has(k->gsub, k->lgsub, "smcp");
}
/* the run's features applied to its glyphs */
static void ot_apply(Face *f, const TextRun *run, TGlyph *g, i32 n)
{
  KernCache *k = kern_for(f);
  const char *tags[TO_NFEAT + 1];
  int nt = 0, b;
  u16 gl[256];
  u8 dead[256];
  i32 at, m;
  if (!k->gsub) return;
  for (b = 0; b < TO_NFEAT; b++) if (run->ot & (TO_LIGA << b)) tags[nt++] = text_ot_tags[b];
  if ((run->ot & TO_CASE) == TO_SMALLCAPS) tags[nt++] = "smcp";
  if (!nt) return;
  for (at = 0; at < n; at += 256) {
    i32 c = n - at < 256 ? n - at : 256;
    for (m = 0; m < c; m++) { gl[m] = g[at + m].glyph; dead[m] = g[at + m].dead; }
    gsub_apply(k->gsub, k->lgsub, tags, nt, gl, dead, (int)c);
    for (m = 0; m < c; m++) { g[at + m].glyph = gl[m]; g[at + m].dead = dead[m]; }
  }
}

TextLayout *text_layout(const TextParams *tpin)
{
  TextLayout *tl = (TextLayout *)calloc(1, sizeof *tl);
  TextParams *tp;
  RunFont *rf = NULL;
  i32 n, ps, cap, li = 0;
  double base = 0, prev_after = 0;
  int first_line = 1, hide = 0;
  if (!tl) return NULL;
  if (!(tp = tl->tp = (TextParams *)malloc(sizeof *tp))) { free(tl); return NULL; }
  *tp = *tpin;
  sanitize(tp);
  tl->vertical = tp->vertical;
  n = (i32)strlen(tp->text);
  cap = n + 1;
  if (!(rf = (RunFont *)malloc((size_t)tp->nruns * sizeof *rf)) ||
      !(tl->g = (TGlyph *)calloc((size_t)cap, sizeof(TGlyph))) || !(tl->ln = (TLine *)calloc((size_t)cap + 1, sizeof(TLine)))) {
    free(rf); text_layout_free(tl); return NULL;
  }
  {
    int r;
    for (r = 0; r < tp->nruns; r++) run_font(&tp->run[r], &rf[r]);
  }
  tl->bx0 = tl->by0 = 1e30; tl->bx1 = tl->by1 = -1e30;
  /* paragraph by paragraph */
  for (ps = 0; ps <= n; ) {
    i32 pe = ps, g0 = tl->ng, i, k;
    double avail;
    while (pe < n && tp->text[pe] != '\n') pe++;
    /* the paragraph's glyphs: 1.2: their case and size (caps, small caps, super- and subscript),
       the OpenType features of each run, then their advances and kerning */
    for (i = ps; i < pe; ) {
      int used, r, cs;
      TGlyph *g = &tl->g[tl->ng];
      u32 cp = utf8_get(tp->text + i, (size_t)(pe - i), &used), shown;
      for (r = tp->nruns - 1; r > 0 && tp->run[r].start > i; r--) ;
      g->byte = i; g->cp = cp; g->run = (u16)r; g->scale = 1; g->dead = 0;
      cs = tp->run[r].ot & TO_CASE;
      shown = cs ? text_upper(cp) : cp;
      if (cs == TO_SMALLCAPS && shown != cp) {
        /* the font's small capitals if it has them, else a capital at 75% */
        if (rf[r].face && small_caps_of(rf[r].face)) shown = cp;
        else g->scale = 0.75;
      }
      if (tp->run[r].ot & TO_POS) g->scale *= 0.583;
      g->glyph = glyph_of(&rf[r], shown == '\t' ? ' ' : shown);
      tl->ng++;
      i += used;
    }
    for (i = g0; i < tl->ng; ) {
      /* a stretch of one run: its features through GSUB */
      i32 e = i;
      int r = tl->g[i].run;
      while (e < tl->ng && tl->g[e].run == r) e++;
      if (rf[r].face) ot_apply(rf[r].face, &tp->run[r], tl->g + i, e - i);
      i = e;
    }
    for (i = g0; i < tl->ng; i++) {
      TGlyph *g = &tl->g[i];
      int r = g->run;
      if (g->dead) { g->adv = 0; continue; }
      if (tp->vertical) g->adv = (rf[r].asc + rf[r].desc) * g->scale + tp->run[r].tracking * tp->run[r].size / 1000;
      else {
        g->adv = hadv(&rf[r], g->glyph) * g->scale * (g->cp == '\t' ? 4 : 1) + tp->run[r].tracking * tp->run[r].size / 1000;
        if (tp->kern && i > g0) {
          i32 pi = i - 1;
          TGlyph *pg;
          while (pi > g0 && tl->g[pi].dead) pi--;
          pg = &tl->g[pi];
          if (!pg->dead && pg->run == g->run && pg->scale == g->scale && rf[r].face && rf[r].face->upm)
            pg->adv += (double)text_kern(rf[r].face, pg->glyph, g->glyph) * rf[r].size * g->scale / rf[r].face->upm;
        }
      }
    }
    /* lines */
    k = g0;
    do {
      i32 j = k, brk = -1;
      double w = 0, asc = 0, desc = 0, lead = 0, start, lw, extra = 0;
      int nsp = 0, last_of_par;
      TLine *L = &tl->ln[tl->nl];
      avail = tp->box ? (tp->vertical ? tp->bh : tp->bw - tp->indent_left - tp->indent_right - (k == g0 ? tp->indent : 0)) : 1e30;
      while (j < tl->ng) {
        TGlyph *g = &tl->g[j];
        if (w + g->adv > avail + 1e-9 && j > k && !is_space(g->cp)) { if (brk > k) j = brk; break; }
        w += g->adv;
        if (is_space(g->cp) || g->cp == '-') brk = j + 1;
        j++;
      }
      last_of_par = j >= tl->ng;
      L->g0 = k; L->g1 = j;
      L->start = k < tl->ng ? tl->g[k].byte : pe;
      L->end = j < tl->ng ? tl->g[j].byte : pe;
      /* its height: the largest of its runs */
      if (k == j) {
        int r = text_run_at(tp, L->start);
        asc = rf[r].asc; desc = rf[r].desc; lead = rf[r].lead;
      }
      for (i = k; i < j; i++) {
        const RunFont *f = &rf[tl->g[i].run];
        double sh = tp->run[tl->g[i].run].baseline;
        if (f->asc + sh > asc) asc = f->asc + sh;
        if (f->desc - sh > desc) desc = f->desc - sh;
        if (f->lead > lead) lead = f->lead;
      }
      if (tp->vertical) { asc = desc = lead / 2; }
      /* its width without the spaces at its end */
      lw = w;
      for (i = j - 1; i >= k && is_space(tl->g[i].cp); i--) lw -= tl->g[i].adv;
      for (i = k; i < j && i <= j - 1; i++) if (is_space(tl->g[i].cp)) nsp++;
      for (i = j - 1; i >= k && is_space(tl->g[i].cp); i--) nsp--;
      /* where it starts */
      if (tp->box) {
        double lo = tp->vertical ? tp->y : tp->x + tp->indent_left + (k == g0 ? tp->indent : 0), room = avail - lw;
        int al = tp->align;
        start = lo;
        /* 1.2: a justified paragraph's last line left, centred or right */
        if (last_of_par && (al == TA_JUSTIFY_CENTER || al == TA_JUSTIFY_RIGHT)) al = al == TA_JUSTIFY_CENTER ? TA_CENTER : TA_RIGHT;
        if (al == TA_CENTER) start = lo + room / 2;
        else if (al == TA_RIGHT) start = lo + room;
        else if (((al == TA_JUSTIFY || al == TA_JUSTIFY_CENTER || al == TA_JUSTIFY_RIGHT) && !last_of_par) || al == TA_JUSTIFY_ALL) { if (nsp > 0 && room > 0) extra = room / nsp; }
      } else {
        double at = tp->vertical ? tp->y : tp->x;
        start = tp->align == TA_CENTER ? at - lw / 2 : tp->align == TA_RIGHT ? at - lw : at;
      }
      /* its baseline (vertical: its column) */
      if (first_line) {
        if (tp->vertical) base = tp->box ? tp->x + tp->bw - lead / 2 : tp->x;
        else base = tp->box ? tp->y + tp->space_before + asc : tp->y;
        first_line = 0;
      } else {
        volatile double step = lead;                   /* (volatile: vbcc's -O2 lost the step here, putting every line on the first) */
        if (tp->leading > 0) step = tp->leading;
        if (k == g0) step += prev_after + tp->space_before;
        base += tp->vertical ? -step : step;
      }
      L->base = base; L->ascent = asc; L->descent = desc;
      if (tp->box && !tp->vertical && tp->bh > 0 && base + desc > tp->y + tp->bh + 0.5) hide = 1;
      if (tp->box && tp->vertical && tp->bw > 0 && base - asc < tp->x - 0.5) hide = 1;
      L->hidden = (u8)hide;
      /* the glyphs placed */
      {
        double pen = start;
        for (i = k; i < j; i++) {
          TGlyph *g = &tl->g[i];
          if (extra > 0 && is_space(g->cp) && i < j && lw > 0) {
            i32 t;
            int trailing = 1;
            for (t = i; t < j; t++) if (!is_space(tl->g[t].cp)) { trailing = 0; break; }
            if (!trailing) g->adv += extra;
          }
          g->line = li;
          if (tp->vertical) { g->x = base; g->y = pen; }
          else { g->x = pen; g->y = base - tp->run[g->run].baseline - glyph_rise(&tp->run[g->run]); }
          pen += g->adv;
        }
        L->x0 = start; L->x1 = start + (lw + extra * nsp);
      }
      if (!hide) {
        if (tp->vertical) {
          if (base - asc < tl->bx0) tl->bx0 = base - asc;
          if (base + desc > tl->bx1) tl->bx1 = base + desc;
          if (L->x0 < tl->by0) tl->by0 = L->x0;
          if (L->x1 > tl->by1) tl->by1 = L->x1;
        } else {
          if (L->x0 < tl->bx0) tl->bx0 = L->x0;
          if (L->x1 > tl->bx1) tl->bx1 = L->x1;
          if (base - asc < tl->by0) tl->by0 = base - asc;
          if (base + desc > tl->by1) tl->by1 = base + desc;
        }
      }
      tl->nl++; li++;
      k = j;
    } while (k < tl->ng);
    prev_after = tp->space_after;
    ps = pe + 1;
    if (pe >= n) break;
  }
  if (tl->bx0 > tl->bx1) { tl->bx0 = tl->bx1 = tp->x; tl->by0 = tl->by1 = tp->y; }
  free(rf);
  return tl;
}
void text_layout_free(TextLayout *tl)
{
  if (!tl) return;
  free(tl->g); free(tl->ln); free(tl->tp);
  free(tl);
}
static void xf_apply(const TextParams *tp, double *x, double *y)
{
  if (tp->xf_on) {
    double a = *x, b = *y;
    *x = tp->xf[0] * a + tp->xf[1] * b + tp->xf[2];
    *y = tp->xf[3] * a + tp->xf[4] * b + tp->xf[5];
  }
}
static void xf_invert(const TextParams *tp, double *x, double *y)
{
  if (tp->xf_on) {
    double det = tp->xf[0] * tp->xf[4] - tp->xf[1] * tp->xf[3], a = *x - tp->xf[2], b = *y - tp->xf[5];
    if (det == 0) return;
    *x = (tp->xf[4] * a - tp->xf[1] * b) / det;
    *y = (-tp->xf[3] * a + tp->xf[0] * b) / det;
  }
}
/* the line a caret position belongs to (a wrapped line's end is the next one's start) */
static int line_of(const TextLayout *tl, int index)
{
  int l;
  for (l = 0; l < tl->nl; l++) {
    const TLine *L = &tl->ln[l];
    if (index < L->start) continue;
    if (index < L->end) return l;
    if (index == L->end && !(l + 1 < tl->nl && tl->ln[l + 1].start == index)) return l;
  }
  return tl->nl - 1;
}
/* where along its line caret position index is (layout space) */
static double caret_along(const TextLayout *tl, int l, int index)
{
  const TLine *L = &tl->ln[l];
  i32 i;
  for (i = L->g0; i < L->g1; i++) if (tl->g[i].byte >= index) return tl->vertical ? tl->g[i].y : tl->g[i].x;
  if (L->g1 > L->g0) { const TGlyph *g = &tl->g[L->g1 - 1]; return (tl->vertical ? g->y : g->x) + g->adv; }
  return L->x0;
}
int text_caret_pos(const TextLayout *tl, int index, double *x0, double *y0, double *x1, double *y1)
{
  int l;
  const TLine *L;
  double a;
  if (!tl || !tl->nl || index < 0 || index > (int)strlen(tl->tp->text)) return 0;
  l = line_of(tl, index);
  L = &tl->ln[l];
  a = caret_along(tl, l, index);
  if (tl->vertical) { *x0 = L->base - L->ascent; *x1 = L->base + L->descent; *y0 = *y1 = a; }
  else { *x0 = *x1 = a; *y0 = L->base - L->ascent; *y1 = L->base + L->descent; }
  xf_apply(tl->tp, x0, y0); xf_apply(tl->tp, x1, y1);
  return 1;
}
/* the caret position in line l nearest 'along' */
static int hit_line(const TextLayout *tl, int l, double along)
{
  const TLine *L = &tl->ln[l];
  i32 i;
  for (i = L->g0; i < L->g1; i++) {
    const TGlyph *g = &tl->g[i];
    if (along < (tl->vertical ? g->y : g->x) + g->adv / 2) return g->byte;
  }
  /* past the end: a wrapped line's caret before its last (trailing) character */
  if (l + 1 < tl->nl && tl->ln[l + 1].start == L->end && L->g1 > L->g0) return tl->g[L->g1 - 1].byte;
  return L->end;
}
int text_hit(const TextLayout *tl, double x, double y)
{
  int l, best = 0;
  double bd = 1e30;
  if (!tl || !tl->nl) return 0;
  xf_invert(tl->tp, &x, &y);
  for (l = 0; l < tl->nl; l++) {
    const TLine *L = &tl->ln[l];
    double c = tl->vertical ? x : y, lo = L->base - L->ascent, hi = L->base + L->descent, d;
    if (L->hidden) continue;
    d = c < lo ? lo - c : c > hi ? c - hi : 0;
    if (d < bd) { bd = d; best = l; }
  }
  return hit_line(tl, best, tl->vertical ? y : x);
}
static int word_char(u32 c) { return !(is_space(c) || c == '\n' || (c < 128 && strchr(".,;:!?\"'()[]{}<>/\\-", (int)c))); }
int text_caret_move(const TextLayout *tl, int index, int how)
{
  const char *s = tl->tp->text;
  int n = (int)strlen(s), l;
  if (index < 0) index = 0;
  if (index > n) index = n;
  switch (how) {
  case TCM_LEFT: return (int)utf8_prev(s, (size_t)index);
  case TCM_RIGHT: return index < n ? (int)utf8_next(s, (size_t)n, (size_t)index) : n;
  case TCM_START: return 0;
  case TCM_END_ALL: return n;
  case TCM_HOME: l = line_of(tl, index); return tl->ln[l].start;
  case TCM_END:
    l = line_of(tl, index);
    if (l + 1 < tl->nl && tl->ln[l + 1].start == tl->ln[l].end && tl->ln[l].g1 > tl->ln[l].g0) return tl->g[tl->ln[l].g1 - 1].byte;
    return tl->ln[l].end;
  case TCM_UP: case TCM_DOWN: {
    int t;
    l = line_of(tl, index);
    t = l + (how == TCM_DOWN ? 1 : -1);
    if (t < 0) return 0;
    if (t >= tl->nl) return n;
    return hit_line(tl, t, caret_along(tl, l, index));
  }
  case TCM_WORDLEFT: {
    int used;
    while (index > 0) { int p = (int)utf8_prev(s, (size_t)index); if (word_char(utf8_get(s + p, (size_t)(n - p), &used))) break; index = p; }
    while (index > 0) { int p = (int)utf8_prev(s, (size_t)index); if (!word_char(utf8_get(s + p, (size_t)(n - p), &used))) break; index = p; }
    return index;
  }
  case TCM_WORDRIGHT: {
    int used;
    while (index < n && word_char(utf8_get(s + index, (size_t)(n - index), &used))) index += used;
    while (index < n && !word_char(utf8_get(s + index, (size_t)(n - index), &used))) index += used;
    return index;
  }
  }
  return index;
}
int text_sel_quads(const TextLayout *tl, int from, int to, double *q, int max)
{
  int l, k = 0;
  if (!tl || to <= from) return 0;
  for (l = 0; l < tl->nl && k < max; l++) {
    const TLine *L = &tl->ln[l];
    int a = from > L->start ? from : L->start, b = to < L->end ? to : L->end;
    double pa, pb, lo = L->base - L->ascent, hi = L->base + L->descent, c[8];
    int i;
    if (L->hidden || a > b || (a == b && !(to > L->end && from <= L->end && tl->tp->text[L->end] == '\n'))) continue;
    pa = caret_along(tl, l, a); pb = caret_along(tl, l, b);
    if (to > L->end && tl->tp->text[L->end] == '\n') pb += (L->ascent + L->descent) / 4;   /* (the newline shown selected) */
    if (tl->vertical) { c[0] = lo; c[1] = pa; c[2] = hi; c[3] = pa; c[4] = hi; c[5] = pb; c[6] = lo; c[7] = pb; }
    else { c[0] = pa; c[1] = lo; c[2] = pb; c[3] = lo; c[4] = pb; c[5] = hi; c[6] = pa; c[7] = hi; }
    for (i = 0; i < 4; i++) xf_apply(tl->tp, &c[2 * i], &c[2 * i + 1]);
    memcpy(q + 8 * k, c, sizeof c);
    k++;
  }
  return k;
}
int text_extent(const TextParams *tp, double *w, double *h)
{
  TextLayout *tl = text_layout(tp);
  if (!tl) return 0;
  *w = tl->bx1 - tl->bx0; *h = tl->by1 - tl->by0;
  text_layout_free(tl);
  return 1;
}

/* ------------------------------------------------------------------ warps */
typedef struct {
  const TextParams *tp;
  int warp;
  double cx, cy, W, H, b, hd, vd;
} Warp;
static void warp_init(Warp *w, const TextParams *tp, const TextLayout *tl)
{
  w->tp = tp; w->warp = tp->warp;
  w->cx = (tl->bx0 + tl->bx1) / 2; w->cy = (tl->by0 + tl->by1) / 2;
  w->W = tl->bx1 - tl->bx0; w->H = tl->by1 - tl->by0;
  if (w->W < 1) w->W = 1;
  if (w->H < 1) w->H = 1;
  w->b = tp->warp_bend / 100; w->hd = tp->warp_h / 100; w->vd = tp->warp_v / 100;
  if (w->b > 1) w->b = 1;
  if (w->b < -1) w->b = -1;
}
/* a layout-space point to the canvas: warped, then through xf */
static void map_pt(const Warp *w, double *px, double *py)
{
  double x = *px, y = *py;
  if (w->warp != TW_NONE) {
    double u = (x - w->cx) / (w->W / 2), v = (y - w->cy) / (w->H / 2), b = w->b, H = w->H;
    /* the distortions: horizontal (height grows across) and vertical (width grows down) */
    x = w->cx + (x - w->cx) * (1 + w->vd * v * 0.5);
    y = w->cy + (y - w->cy) * (1 + w->hd * u * 0.5);
    if (u > 1.5) u = 1.5;
    if (u < -1.5) u = -1.5;
    switch (w->warp) {
    case TW_ARC:
      if (fabs(b) > 1e-6) {
        double R = w->W / (b * M_PI), th = (x - w->cx) / R, r = R - (y - w->cy);
        x = w->cx + r * sin(th); y = w->cy + R - r * cos(th);
      }
      break;
    case TW_ARCH: y -= b * H * 0.5 * (1 - u * u); break;
    case TW_BULGE: y += v * b * H * 0.5 * (1 - u * u); break;
    case TW_FLAG: y += b * H * 0.5 * sin(M_PI * u); break;
    case TW_WAVE: y += b * H * 0.35 * sin(M_PI * u + v * M_PI / 2); break;
    case TW_FISH: y += v * b * H * 0.5 * sin(M_PI * (u + 1) / 2) * (u < 0.5 ? 1 : 1 - (u - 0.5) * 2.5); break;
    case TW_RISE: y -= b * H * 0.5 * u; break;
    case TW_SQUEEZE: y = w->cy + (y - w->cy) * (1 - b * 0.5 * (1 - u * u)); break;
    case TW_TWIST: {
      double a = b * M_PI / 4 * u, dy = y - w->cy;
      x -= dy * sin(a); y = w->cy + dy * cos(a);
      break;
    }
    /* 1.1: one edge bent (Arc, Shell: the bent edge also drawn in), the whole bulged from the middle */
    case TW_ARCLOWER: y += b * H * 0.5 * (1 - u * u) * (v + 1) / 2; break;
    case TW_ARCUPPER: y -= b * H * 0.5 * (1 - u * u) * (1 - v) / 2; break;
    case TW_SHELLLOWER: case TW_SHELLUPPER: {
      double t = w->warp == TW_SHELLLOWER ? (v + 1) / 2 : (1 - v) / 2, c = cos(u * M_PI / 2);
      if (t < 0) t = 0;
      if (t > 1) t = 1;
      y += (w->warp == TW_SHELLLOWER ? 1 : -1) * b * H * 0.5 * c * t;
      x = w->cx + (x - w->cx) * (1 - 0.3 * b * t);
      break;
    }
    case TW_FISHEYE: {
      double r2 = u * u + v * v, k = r2 < 1 ? 1 + b * 0.5 * (1 - r2) : 1;
      x = w->cx + (x - w->cx) * k; y = w->cy + (y - w->cy) * k;
      break;
    }
    case TW_INFLATE: {
      double vv = v > 1 ? 1 : v < -1 ? -1 : v;
      x += (x - w->cx) * b * 0.5 * (1 - vv * vv); y += (y - w->cy) * b * 0.5 * (1 - u * u);
      break;
    }
    }
  }
  xf_apply(w->tp, &x, &y);
  *px = x; *py = y;
}
Box text_bounds(const TextParams *tp)
{
  TextLayout *tl = text_layout(tp);
  Warp w;
  double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30, m = 0;
  int i, k;
  Box b = { 0, 0, 0, 0 };
  if (!tl) return b;
  if (!tl->ng) { text_layout_free(tl); return b; }
  for (i = 0; i < tl->tp->nruns; i++) if (tl->tp->run[i].size > m) m = tl->tp->run[i].size;
  m *= 0.3;                                               /* (overhangs, italics, faux bold) */
  warp_init(&w, tl->tp, tl);
  for (k = 0; k < 4; k++)
    for (i = 0; i <= 32; i++) {
      double t = i / 32.0, x, y;
      double ax = tl->bx0 - m, ay = tl->by0 - m, bx = tl->bx1 + m, by = tl->by1 + m;
      if (k == 0) { x = ax + (bx - ax) * t; y = ay; }
      else if (k == 1) { x = bx; y = ay + (by - ay) * t; }
      else if (k == 2) { x = ax + (bx - ax) * t; y = by; }
      else { x = ax; y = ay + (by - ay) * t; }
      map_pt(&w, &x, &y);
      if (x < x0) x0 = x;
      if (x > x1) x1 = x;
      if (y < y0) y0 = y;
      if (y > y1) y1 = y;
    }
  text_layout_free(tl);
  if (x0 < -1e7) x0 = -1e7;
  if (y0 < -1e7) y0 = -1e7;
  if (x1 > 1e7) x1 = 1e7;
  if (y1 > 1e7) y1 = 1e7;
  b.x0 = (i32)floor(x0); b.y0 = (i32)floor(y0); b.x1 = (i32)ceil(x1); b.y1 = (i32)ceil(y1);
  return b;
}

/* ------------------------------------------------------------------ drawing */
/* the picture being built: RGBA in box b, grown as glyphs land outside it */
typedef struct { u8 *px; Box b; i32 cw, ch; int oom; } Canvas;
static int cv_need(Canvas *c, Box need)
{
  Box nb;
  u8 *np;
  i32 y;
  need = box_clip(need, box_make(0, 0, c->cw, c->ch));
  if (box_empty(need)) return 0;
  if (c->px && need.x0 >= c->b.x0 && need.y0 >= c->b.y0 && need.x1 <= c->b.x1 && need.y1 <= c->b.y1) return 1;
  nb = c->px ? box_union(c->b, need) : need;
  /* some room to spare, so growing one glyph at a time stays cheap */
  if (c->px) {
    i32 mx = (nb.x1 - nb.x0) / 4 + 16, my = (nb.y1 - nb.y0) / 4 + 16;
    if (nb.x0 < c->b.x0) nb.x0 -= mx;
    if (nb.x1 > c->b.x1) nb.x1 += mx;
    if (nb.y0 < c->b.y0) nb.y0 -= my;
    if (nb.y1 > c->b.y1) nb.y1 += my;
    nb = box_clip(nb, box_make(0, 0, c->cw, c->ch));
  }
  if (!(np = (u8 *)calloc((size_t)(nb.x1 - nb.x0) * (nb.y1 - nb.y0), 4))) { c->oom = 1; return 0; }
  if (c->px) {
    for (y = c->b.y0; y < c->b.y1; y++)
      memcpy(np + ((size_t)(y - nb.y0) * (nb.x1 - nb.x0) + (c->b.x0 - nb.x0)) * 4, c->px + (size_t)(y - c->b.y0) * (c->b.x1 - c->b.x0) * 4, (size_t)(c->b.x1 - c->b.x0) * 4);
    free(c->px);
  }
  c->px = np; c->b = nb;
  return 1;
}
/* coverage w x h at x0, y0 laid on in colour col */
static void cv_put(Canvas *c, i32 x0, i32 y0, i32 w, i32 h, const u8 *cov, u32 col, int aa)
{
  i32 x, y, bw;
  u8 cr = (u8)(col >> 16), cg = (u8)(col >> 8), cb = (u8)col;
  if (w <= 0 || h <= 0 || !cv_need(c, box_make(x0, y0, x0 + w, y0 + h))) return;
  bw = c->b.x1 - c->b.x0;
  for (y = 0; y < h; y++) {
    i32 py = y0 + y;
    if (py < c->b.y0 || py >= c->b.y1) continue;
    for (x = 0; x < w; x++) {
      i32 px = x0 + x;
      int a = cov[(size_t)y * w + x];
      u8 *p;
      if (!a || px < c->b.x0 || px >= c->b.x1) continue;
      if (!aa) { if (a < 128) continue; a = 255; }
      p = c->px + ((size_t)(py - c->b.y0) * bw + (px - c->b.x0)) * 4;
      if (!p[3]) { p[0] = cr; p[1] = cg; p[2] = cb; p[3] = (u8)a; }
      else if (p[0] == cr && p[1] == cg && p[2] == cb) { if (a > p[3]) p[3] = (u8)a; }
      else {
        int oa = a + p[3] * (255 - a) / 255;
        p[0] = (u8)((cr * a + p[0] * p[3] * (255 - a) / 255) / oa);
        p[1] = (u8)((cg * a + p[1] * p[3] * (255 - a) / 255) / oa);
        p[2] = (u8)((cb * a + p[2] * p[3] * (255 - a) / 255) / oa);
        p[3] = (u8)oa;
      }
    }
  }
}
/* faux bold: coverage spread r pixels every way (a new w + 2r x h + 2r buffer) */
static u8 *dilate(const u8 *cov, int w, int h, int r)
{
  int W = w + 2 * r, H = h + 2 * r, x, y, k;
  u8 *t = (u8 *)calloc((size_t)W * H, 1), *o = (u8 *)calloc((size_t)W * H, 1);
  if (!t || !o) { free(t); free(o); return NULL; }
  for (y = 0; y < h; y++)
    for (x = 0; x < W; x++) {
      int m = 0;
      for (k = x - 2 * r; k <= x; k++) if (k >= 0 && k < w && cov[y * w + k] > m) m = cov[y * w + k];
      t[(y + r) * W + x] = (u8)m;
    }
  for (x = 0; x < W; x++)
    for (y = 0; y < H; y++) {
      int m = 0;
      for (k = y - r; k <= y + r; k++) if (k >= 0 && k < H && t[k * W + x] > m) m = t[k * W + x];
      o[y * W + x] = (u8)m;
    }
  free(t);
  return o;
}
typedef struct {
  const TextParams *tp;
  const TextLayout *tl;
  RunFont *rf;
  Canvas cv;
  Warp w;
  int complex;                        /* through the outlines and the filler */
  double ox, oy;                      /* a translation-only xf */
  Poly *collect;                      /* text_to_path: outlines gathered here instead of drawn */
} Render;
/* a polygon (layout space, mapped here) filled into the canvas in colour col */
static void fill_poly(Render *R, Poly *pl, u32 col, int bold, int aa)
{
  Box b;
  u8 *cov;
  i32 i;
  for (i = 0; i < pl->n; i++) {
    if (R->complex) map_pt(&R->w, &pl->xy[2 * i], &pl->xy[2 * i + 1]);
    else { pl->xy[2 * i] += R->ox; pl->xy[2 * i + 1] += R->oy; }
  }
  if (R->collect) {
    i32 c, s = 0;
    for (c = 0; c < pl->nc; s = pl->end[c], c++) {
      for (i = s; i < pl->end[c]; i++) poly_add(R->collect, pl->xy[2 * i], pl->xy[2 * i + 1]);
      poly_end(R->collect, 1);
    }
    return;
  }
  b = poly_box(pl);
  b.x0 -= 1 + bold; b.y0 -= 1 + bold; b.x1 += 1 + bold; b.y1 += 1 + bold;
  b = box_clip(b, box_make(-bold, -bold, R->cv.cw + bold, R->cv.ch + bold));
  if (box_empty(b)) return;
  if (!(cov = (u8 *)malloc((size_t)(b.x1 - b.x0) * (b.y1 - b.y0)))) { R->cv.oom = 1; return; }
  if (poly_raster(pl, 0, b, cov, b.x1 - b.x0)) {
    if (bold) {
      u8 *d = dilate(cov, b.x1 - b.x0, b.y1 - b.y0, bold);
      if (d) cv_put(&R->cv, b.x0 - bold, b.y0 - bold, b.x1 - b.x0 + 2 * bold, b.y1 - b.y0 + 2 * bold, d, col, aa);
      free(d);
    } else cv_put(&R->cv, b.x0, b.y0, b.x1 - b.x0, b.y1 - b.y0, cov, col, aa);
  }
  free(cov);
}
static int bold_radius(const TextRun *r) { int b = (int)(r->size / 30 + 0.5); return r->faux_bold ? (b < 1 ? 1 : b) : 0; }
static void draw_glyph(Render *R, const TGlyph *g)
{
  const TextParams *tp = R->tp;
  const TextRun *run = &tp->run[g->run];
  const RunFont *f = &R->rf[g->run];
  int bold = bold_radius(run), aa = tp->aa != TAA_NONE;
  u32 col = run->color;
  double ox = g->x, oy = g->y;                             /* the glyph's origin (pen on the baseline) */
  double size = run->size * (g->scale > 0 ? g->scale : 1);   /* (1.2: small caps, super- and subscript) */
  if (is_space(g->cp) || g->cp < 32 || g->dead) return;
  if (tp->vertical) { ox = g->x - hadv(f, g->glyph) * g->scale / 2; oy = g->y + f->asc * g->scale; }
  if (f->ami) {
    /* an Amiga bitmap font: its glyph scaled (nearest), placed as it lies */
    int adv, kern, w, sw, sh, x, y, top;
    double s = size / f->ami->ysize;
    u8 *cov, *sc;
    if (R->collect) return;
    amifont_char(f->ami, g->glyph, &adv, &kern, &w, NULL);
    if (w <= 0 || !(cov = (u8 *)malloc((size_t)w * f->ami->ysize))) return;
    amifont_char(f->ami, g->glyph, &adv, &kern, &w, cov);
    sw = (int)ceil(w * s - 1e-9); sh = (int)ceil(f->ami->ysize * s - 1e-9);
    if ((sc = (u8 *)malloc((size_t)sw * sh)) != NULL) {
      for (y = 0; y < sh; y++) for (x = 0; x < sw; x++) {
        int sx = (int)(x / s), sy = (int)(y / s);
        sc[y * sw + x] = cov[(sy < f->ami->ysize ? sy : f->ami->ysize - 1) * w + (sx < w ? sx : w - 1)];
      }
      top = (int)floor(oy + R->oy + 0.5) - (int)floor((f->ami->baseline + 1) * s + 0.5);
      x = (int)floor(ox + R->ox + kern * s);
      if (bold) { u8 *d = dilate(sc, sw, sh, bold); if (d) cv_put(&R->cv, x - bold, top - bold, sw + 2 * bold, sh + 2 * bold, d, col, aa); free(d); }
      else cv_put(&R->cv, x, top, sw, sh, sc, col, aa);
      free(sc);
    }
    free(cov);
    return;
  }
  if (!f->face) return;
  if (!R->complex && !R->collect) {
    TTRaster *r = raster_for(f->face, tp->aa == TAA_SHARP);
    TTBitmap bm;
    i32 gx, gy;
    if (!r) return;
    memset(&bm, 0, sizeof bm);
    if (ttr_glyph(r, g->glyph, (i32)floor(size * 64 + 0.5), &bm) && bm.w > 0 && bm.cov) {
      if (tp->aa == TAA_NONE) ttr_dropout(&bm, 128);
      gx = (i32)floor(ox + R->ox) + bm.left; gy = (i32)floor(oy + R->oy + 0.5) - bm.top;
      if (bold) { u8 *d = dilate(bm.cov, bm.w, bm.h, bold); if (d) cv_put(&R->cv, gx - bold, gy - bold, bm.w + 2 * bold, bm.h + 2 * bold, d, col, aa); free(d); }
      else cv_put(&R->cv, gx, gy, bm.w, bm.h, bm.cov, col, aa);
    }
    free(bm.cov);
    return;
  }
  {
    /* through the outline: at a size the rasteriser reads well, scaled, sheared, warped */
    TTRaster *r = raster_for(f->face, 0);
    double ext = size * xf_scale(tp), k, shear = run->faux_italic ? 0.2126 : 0;   /* (tan 12 degrees) */
    i32 *seg = NULL;
    u32 ns = 0, i;
    Poly pl;
    if (!r) return;
    if (ext < 64) ext = 64;
    if (ext > 400) ext = 400;
    k = size / ext / 64;
    if (!ttr_outline(r, g->glyph, (i32)floor(ext * 64 + 0.5), &seg, &ns) || !ns) { free(seg); return; }
    poly_init(&pl);
    for (i = 0; i < ns; i++) {
      i32 *s = seg + 4 * i;
      double lx0 = s[0] * k, ly0 = s[1] * k, lx1 = s[2] * k, ly1 = s[3] * k, len;
      int sub, j;
      if (i == 0 || s[0] != seg[4 * i - 2] || s[1] != seg[4 * i - 1]) {
        if (i) poly_end(&pl, 1);
        poly_add(&pl, ox + lx0 - ly0 * shear, oy + ly0);
      }
      /* long lines cut up so a warp can bend them */
      len = fabs(lx1 - lx0) + fabs(ly1 - ly0);
      sub = R->w.warp != TW_NONE ? (int)(len / 2) + 1 : 1;
      if (sub > 64) sub = 64;
      for (j = 1; j <= sub; j++) {
        double t = (double)j / sub, x = lx0 + (lx1 - lx0) * t, y = ly0 + (ly1 - ly0) * t;
        poly_add(&pl, ox + x - y * shear, oy + y);
      }
    }
    poly_end(&pl, 1);
    free(seg);
    fill_poly(R, &pl, col, bold, aa);
    poly_free(&pl);
  }
}
/* underline and strikethrough: bars under each run's stretch of a line */
static void draw_lines(Render *R)
{
  const TextLayout *tl = R->tl;
  int l, pass;
  if (tl->vertical) return;
  for (l = 0; l < tl->nl; l++) {
    const TLine *L = &tl->ln[l];
    if (L->hidden) continue;
    for (pass = 0; pass < 2; pass++) {
      i32 i = L->g0;
      while (i < L->g1) {
        const TextRun *run = &R->tp->run[tl->g[i].run];
        i32 j = i;
        double x0, x1, t, y;
        Poly pl;
        if (!(pass ? run->strike : run->underline)) { i++; continue; }
        while (j < L->g1 && tl->g[j].run == tl->g[i].run) j++;
        /* not under trailing spaces */
        {
          i32 e = j;
          while (e > i && is_space(tl->g[e - 1].cp) && e == L->g1) e--;
          if (e == i) { i = j; continue; }
          x0 = tl->g[i].x; x1 = tl->g[e - 1].x + tl->g[e - 1].adv;
        }
        t = run->size / 15; if (t < 1) t = 1;
        y = tl->g[i].y + (pass ? -run->size * 0.28 : run->size * 0.12);
        poly_init(&pl);
        poly_add(&pl, x0, y - t / 2); poly_add(&pl, x1, y - t / 2);
        poly_add(&pl, x1, y + t / 2); poly_add(&pl, x0, y + t / 2);
        poly_end(&pl, 1);
        {
          /* (straight bars stay crisp on whole pixels when nothing bends them) */
          int k;
          if (!R->complex) for (k = 0; k < 4; k++) pl.xy[2 * k + 1] = floor(pl.xy[2 * k + 1] + R->oy + 0.5) - R->oy;
        }
        fill_poly(R, &pl, run->color, 0, R->tp->aa != TAA_NONE);
        poly_free(&pl);
        i = j;
      }
    }
  }
}
static int render(const TextParams *tp, i32 w, i32 h, Canvas *cv, Poly *collect)
{
  Render R;
  TextLayout *tl;
  i32 i;
  int r;
  memset(&R, 0, sizeof R);
  if (!(tl = text_layout(tp))) return 0;
  R.tp = tl->tp; R.tl = tl; R.collect = collect;
  R.cv.cw = w; R.cv.ch = h;
  if (!(R.rf = (RunFont *)malloc((size_t)tl->tp->nruns * sizeof(RunFont)))) { text_layout_free(tl); return 0; }
  for (r = 0; r < tl->tp->nruns; r++) run_font(&tl->tp->run[r], &R.rf[r]);
  warp_init(&R.w, tl->tp, tl);
  R.complex = tl->tp->warp != TW_NONE;
  if (tl->tp->xf_on) {
    const double *m = tl->tp->xf;
    if (m[0] == 1 && m[1] == 0 && m[3] == 0 && m[4] == 1) { R.ox = m[2]; R.oy = m[5]; }
    else R.complex = 1;
  }
  for (r = 0; r < tl->tp->nruns; r++) if (tl->tp->run[r].faux_italic || tl->tp->run[r].size > 400) R.complex = 1;
  if (R.complex) R.ox = R.oy = 0;
  for (i = 0; i < tl->ng && !R.cv.oom; i++) if (!tl->ln[tl->g[i].line].hidden) draw_glyph(&R, &tl->g[i]);
  draw_lines(&R);
  free(R.rf);
  text_layout_free(tl);
  *cv = R.cv;
  if (R.cv.oom) { free(R.cv.px); cv->px = NULL; return 0; }
  return 1;
}
int text_render(const TextParams *tp, i32 w, i32 h, u8 **px, Box *b)
{
  Canvas cv;
  *px = NULL; *b = box_make(0, 0, 0, 0);
  if (!render(tp, w, h, &cv, NULL)) return 0;
  *px = cv.px; *b = cv.px ? cv.b : box_make(0, 0, 0, 0);
  return 1;
}
int text_coverage(const TextParams *tp, i32 w, i32 h, u8 *mask)
{
  u8 *px;
  Box b;
  i32 x, y;
  memset(mask, 0, (size_t)w * h);
  if (!text_render(tp, w, h, &px, &b)) return 0;
  for (y = b.y0; y < b.y1; y++)
    for (x = b.x0; x < b.x1; x++) mask[(size_t)y * w + x] = px[((size_t)(y - b.y0) * (b.x1 - b.x0) + (x - b.x0)) * 4 + 3];
  free(px);
  return 1;
}
Path *text_to_path(const TextParams *tp)
{
  Poly pl;
  Canvas cv;
  Path *p = path_new();
  i32 c, s = 0, i;
  if (!p) return NULL;
  poly_init(&pl);
  if (!render(tp, MAXDIM, MAXDIM, &cv, &pl)) { poly_free(&pl); path_free(p); return NULL; }
  free(cv.px);
  for (c = 0; c < pl.nc; s = pl.end[c], c++) {
    for (i = s; i < pl.end[c]; i++)
      if (!(i == s ? path_move_to(p, pl.xy[2 * i], pl.xy[2 * i + 1]) != 0 : path_line_to(p, pl.xy[2 * i], pl.xy[2 * i + 1]))) { poly_free(&pl); path_free(p); return NULL; }
    path_close(p);
  }
  poly_free(&pl);
  return p;
}

/* ------------------------------------------------------------------ in the document */
const TextParams *layer_text(const Layer *l)
{
  return l && l->kind == LK_TEXT && l->live && l->livelen == sizeof(TextParams) ? (const TextParams *)l->live : NULL;
}
int doc_new_text(Doc *d, const TextParams *tp, const char *name)
{
  TextParams *c = (TextParams *)malloc(sizeof *c);
  Layer *l;
  int at, n;
  char nm[64];
  if (!c) return -1;
  *c = *tp;
  sanitize(c);
  if (!(l = (Layer *)calloc(1, sizeof *l))) { free(c); return -1; }
  if (!name) {
    for (n = 0; c->text[n] && c->text[n] != '\n' && n < 40; n++) nm[n] = c->text[n];
    nm[n] = 0;
    while (n > 0 && ((u8)nm[n - 1] & 0xC0) == 0x80) nm[--n] = 0;
    if (n > 0 && (u8)nm[n - 1] >= 0xC0) nm[--n] = 0;
    name = nm[0] ? nm : "Text";
  }
  strncpy(l->name, name, sizeof l->name - 1);
  l->opacity = 255; l->visible = 1; l->kind = LK_TEXT;
  l->live = c; l->livelen = sizeof *c;
  at = d->active + 1;
  if (at > d->nlay) at = d->nlay;
  l->depth = d->nlay && d->active >= 0 && d->active < d->nlay ? d->lay[d->active]->depth : 0;
  doc_begin(d, "New Type Layer");
  if (!doc_add_layer(d, l, at)) { doc_end(d); return -1; }
  doc_end(d);
  return at;
}
int doc_set_text(Doc *d, int i, const TextParams *tp)
{
  Layer *l;
  if (i < 0 || i >= d->nlay || !layer_text(l = d->lay[i])) return 0;
  doc_begin(d, "Edit Type");
  doc_note_live(d, i);
  memcpy(l->live, tp, sizeof *tp);
  sanitize((TextParams *)l->live);
  l->live_ok = 0;
  doc_end(d);
  return 1;
}

/* ------------------------------------------------------------------ 1.1: Convert to Paragraph / Point Text */
int text_to_paragraph(TextParams *tp)
{
  TextLayout *tl;
  double lead;
  if (tp->box || tp->vertical) return 0;
  if (!(tl = text_layout(tp))) return 0;
  /* the box round the text as it is laid out, with a little room to spare so nothing wraps anew */
  lead = tl->nl ? tl->ln[0].ascent + tl->ln[0].descent : tp->run[0].size;
  tp->x = floor(tl->bx0); tp->y = floor(tl->by0);
  tp->bw = ceil(tl->bx1 - tp->x) + tp->run[0].size * 0.5 + 1;
  tp->bh = ceil(tl->by1 - tp->y) + lead;
  tp->box = 1;
  text_layout_free(tl);
  return 1;
}
int text_to_point(TextParams *tp)
{
  TextLayout *tl;
  int k;
  double bx, bw;
  if (!tp->box || tp->vertical) return 0;
  if (!(tl = text_layout(tp))) return 0;
  bx = tp->x; bw = tp->bw;
  /* a line break where each line wraps (in place of the space it wrapped at), from the end back */
  for (k = tl->nl - 2; k >= 0; k--) {
    i32 e = tl->ln[k].end;
    if (e < (i32)strlen(tp->text) && tp->text[e] == '\n') continue;
    if (tl->ln[k].hidden) continue;
    if (e > 0 && tp->text[e - 1] == ' ') tp->text[e - 1] = '\n';
    else if (e < (i32)strlen(tp->text) && tp->text[e] == ' ') tp->text[e] = '\n';
    else text_insert(tp, e, "\n", 1);
  }
  /* the first baseline, at the alignment point the lines keep */
  tp->y = tl->nl ? tl->ln[0].base : tp->y + tp->run[0].size;
  if (tp->align == TA_CENTER) tp->x = bx + bw / 2;
  else if (tp->align == TA_RIGHT) tp->x = bx + bw;
  else { tp->x = bx; if (tp->align > TA_RIGHT) tp->align = TA_LEFT; }
  tp->box = 0;
  text_layout_free(tl);
  return 1;
}
