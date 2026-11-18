/*
 * gsub.c - Easel 1.2: OpenType glyph substitution (the GSUB table) for type layers: the features a
 * run asks for (standard and discretionary ligatures, swashes, oldstyle figures, stylistic and
 * titling alternates, ornaments, ordinals, fractions, small caps) looked up in the font's
 * script ('latn', else 'DFLT', else its first) and applied in lookup order. Lookup types 1 (single),
 * 3 (alternate: the first one), 4 (ligature), 7 (extension) and, since round 3, the contextual ones
 * 5 (context) and 6 (chained context), in all three formats each, are done; 8 (reverse chained,
 * used by Arabic scripts) is not. Every offset is checked against the table: fonts are files from
 * anywhere.
 */
#include <stdlib.h>
#include <string.h>
#include "gsub.h"

static u32 be16(const u8 *p) { return (u32)p[0] << 8 | p[1]; }
static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

static int cov(const u8 *t, u32 len, u32 o, u32 g)
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
/* the LangSys the font uses for Latin text; 0 if none */
static u32 langsys(const u8 *t, u32 len)
{
  u32 sl, n, i, pick = 0;
  if (len < 10) return 0;
  sl = be16(t + 4);
  if (sl + 2 > len) return 0;
  n = be16(t + sl);
  if (sl + 2 + 6 * n > len) return 0;
  for (i = 0; i < n; i++) {
    const u8 *r = t + sl + 2 + 6 * i;
    if (!memcmp(r, "latn", 4)) { pick = sl + be16(r + 4); break; }
    if (!memcmp(r, "DFLT", 4) || !pick) pick = sl + be16(r + 4);
  }
  if (!pick || pick + 4 > len) return 0;
  if (be16(t + pick)) return pick + be16(t + pick);    /* the default LangSys */
  if (be16(t + pick + 2) && pick + 10 <= len) return pick + be16(t + pick + 6);   /* (else the first one) */
  return 0;
}
/* the lookups of the features named (tags 4 bytes each), sorted; how many */
static int lookups_for(const u8 *t, u32 len, const char *const *tags, int ntags, u16 *out, int max)
{
  u32 ls = langsys(t, len), fl, nf, nfi, i;
  int n = 0, k;
  if (!ls || ls + 6 > len) return 0;
  fl = be16(t + 6);
  if (fl + 2 > len) return 0;
  nf = be16(t + fl);
  if (fl + 2 + 6 * nf > len) return 0;
  nfi = be16(t + ls + 4);
  if (ls + 6 + 2 * nfi > len) return 0;
  for (i = 0; i < nfi; i++) {
    u32 fi = be16(t + ls + 6 + 2 * i), fo, nl, j;
    const u8 *rec;
    int want = 0;
    if (fi >= nf) continue;
    rec = t + fl + 2 + 6 * fi;
    for (k = 0; k < ntags; k++) if (!memcmp(rec, tags[k], 4)) want = 1;
    if (!want) continue;
    fo = fl + be16(rec + 4);
    if (fo + 4 > len) continue;
    nl = be16(t + fo + 2);
    if (fo + 4 + 2 * nl > len) continue;
    for (j = 0; j < nl && n < max; j++) {
      u16 li = (u16)be16(t + fo + 4 + 2 * j);
      int dup = 0, m;
      for (m = 0; m < n; m++) if (out[m] == li) dup = 1;
      if (!dup) out[n++] = li;
    }
  }
  /* in lookup list order */
  for (k = 1; k < n; k++) { u16 v = out[k]; int m = k - 1; while (m >= 0 && out[m] > v) { out[m + 1] = out[m]; m--; } out[m + 1] = v; }
  return n;
}
int gsub_has(const u8 *t, u32 len, const char *tag)
{
  u16 l[4];
  return t && lookups_for(t, len, &tag, 1, l, 4) > 0;
}
/* a glyph's class in the ClassDef at o (0 when it isn't listed) */
static u32 class_of(const u8 *t, u32 len, u32 o, u32 g)
{
  u32 n, k;
  if (o + 4 > len) return 0;
  if (be16(t + o) == 1) {
    u32 first = be16(t + o + 2);
    n = be16(t + o + 4);
    if (o + 6 + 2 * n > len || g < first || g - first >= n) return 0;
    return be16(t + o + 6 + 2 * (g - first));
  }
  if (be16(t + o) == 2) {
    n = be16(t + o + 2);
    if (o + 4 + 6 * n > len) return 0;
    for (k = 0; k < n; k++) {
      const u8 *r = t + o + 4 + 6 * k;
      if (g >= be16(r) && g <= be16(r + 2)) return be16(r + 4);
    }
  }
  return 0;
}
/* the glyph after (dir 1) or before (-1) position i that isn't dead; -1 if none */
static int step(const u8 *dead, int n, int i, int dir)
{
  do i += dir; while (i >= 0 && i < n && dead[i]);
  return i >= 0 && i < n ? i : -1;
}
/* the LookupList (set by gsub_apply for the contextual lookups' nested ones) */
static u32 cur_ll, cur_nll;
static int lookup_at(const u8 *t, u32 len, u32 li, u16 *g, u8 *dead, int n, int i, int depth);
/* SubstLookupRecords at r (count of them) applied to the matched input positions pos[0..npos) */
static int apply_records(const u8 *t, u32 len, u32 r, u32 count, const int *pos, int npos, u16 *g, u8 *dead, int n, int depth)
{
  u32 k;
  int any = 0;
  if (r + 4 * count > len) return 0;
  for (k = 0; k < count; k++) {
    u32 si = be16(t + r + 4 * k), li = be16(t + r + 4 * k + 2);
    if ((int)si < npos && !dead[pos[si]]) any |= lookup_at(t, len, li, g, dead, n, pos[si], depth + 1);
  }
  return any || count == 0;
}
/* how a sequence element is matched: a glyph, a class or a coverage table */
enum { M_GLYPH, M_CLASS, M_COVER };
static int match1(const u8 *t, u32 len, int how, u32 want, u32 base, u32 g)
{
  if (how == M_GLYPH) return g == want;
  if (how == M_CLASS) return class_of(t, len, base, g) == want;
  return cov(t, len, base + want, g) >= 0;
}
/* the input sequence from i: first at i (already matched), then count - 1 values at v (each 2
   bytes; coverage offsets from base); positions into pos. 1 if it matches */
static int match_seq(const u8 *t, u32 len, int how, u32 base, u32 v, u32 count, const u16 *g, const u8 *dead, int n, int i, int *pos, int dir)
{
  u32 k;
  int at = i;
  if (count > 32 || v + 2 * count > len) return 0;
  for (k = 0; k < count; k++) {
    at = step(dead, n, at, dir);
    if (at < 0 || !match1(t, len, how, be16(t + v + 2 * k), base, g[at])) return 0;
    if (pos) pos[k + 1] = at;
  }
  return 1;
}
/* type 5 and 6: the contextual substitutions at glyph i; 1 if one matched */
static int context(const u8 *t, u32 len, int type, u32 o, u16 *g, u8 *dead, int n, int i, int depth)
{
  u32 fmt = be16(t + o);
  int pos[33];
  pos[0] = i;
  if (fmt == 3) {
    if (type == 5) {
      /* glyphCount, substCount, coverages, records */
      u32 gc = be16(t + o + 2), sc = be16(t + o + 4), k;
      int at = i;
      if (gc < 1 || gc > 32 || o + 6 + 2 * gc > len) return 0;
      for (k = 0; k < gc; k++) {
        if (k) at = step(dead, n, at, 1);
        if (at < 0 || cov(t, len, o + be16(t + o + 6 + 2 * k), g[at]) < 0) return 0;
        pos[k] = at;
      }
      return apply_records(t, len, o + 6 + 2 * gc, sc, pos, (int)gc, g, dead, n, depth);
    } else {
      u32 p = o + 2, bc, ic, lc, sc, k;
      int at;
      if (p + 2 > len) return 0;
      bc = be16(t + p); p += 2;
      if (bc > 32 || p + 2 * bc + 2 > len) return 0;
      at = i;
      for (k = 0; k < bc; k++) {
        at = step(dead, n, at, -1);
        if (at < 0 || cov(t, len, o + be16(t + p + 2 * k), g[at]) < 0) return 0;
      }
      p += 2 * bc;
      ic = be16(t + p); p += 2;
      if (ic < 1 || ic > 32 || p + 2 * ic + 2 > len) return 0;
      at = i;
      for (k = 0; k < ic; k++) {
        if (k) at = step(dead, n, at, 1);
        if (at < 0 || cov(t, len, o + be16(t + p + 2 * k), g[at]) < 0) return 0;
        pos[k] = at;
      }
      p += 2 * ic;
      lc = be16(t + p); p += 2;
      if (lc > 32 || p + 2 * lc + 2 > len) return 0;
      for (k = 0; k < lc; k++) {
        at = step(dead, n, at, 1);
        if (at < 0 || cov(t, len, o + be16(t + p + 2 * k), g[at]) < 0) return 0;
      }
      p += 2 * lc;
      sc = be16(t + p); p += 2;
      return apply_records(t, len, p, sc, pos, (int)ic, g, dead, n, depth);
    }
  }
  if (fmt == 1 || fmt == 2) {
    /* rule sets by the first glyph's coverage index (format 1) or class (format 2) */
    int c = cov(t, len, o + be16(t + o + 2), g[i]);
    u32 bcd = 0, icd = 0, lcd = 0, nsets, sets, set, nr, k;
    if (c < 0) return 0;
    if (type == 5) {
      if (fmt == 1) { nsets = be16(t + o + 4); sets = o + 6; }
      else { if (o + 8 > len) return 0; icd = o + be16(t + o + 4); nsets = be16(t + o + 6); sets = o + 8; c = (int)class_of(t, len, icd, g[i]); }
    } else {
      if (fmt == 1) { nsets = be16(t + o + 4); sets = o + 6; }
      else {
        if (o + 12 > len) return 0;
        bcd = o + be16(t + o + 4); icd = o + be16(t + o + 6); lcd = o + be16(t + o + 8);
        nsets = be16(t + o + 10); sets = o + 12; c = (int)class_of(t, len, icd, g[i]);
      }
    }
    if ((u32)c >= nsets || sets + 2 * nsets > len || !be16(t + sets + 2 * c)) return 0;
    set = o + be16(t + sets + 2 * c);
    if (set + 2 > len) return 0;
    nr = be16(t + set);
    if (set + 2 + 2 * nr > len) return 0;
    for (k = 0; k < nr; k++) {
      u32 r = set + be16(t + set + 2 + 2 * k), how = fmt == 1 ? M_GLYPH : M_CLASS;
      if (r + 4 > len) continue;
      if (type == 5) {
        u32 gc = be16(t + r), sc = be16(t + r + 2);
        if (gc < 1 || gc > 32) continue;
        if (!match_seq(t, len, (int)how, icd, r + 4, gc - 1, g, dead, n, i, pos, 1)) continue;
        return apply_records(t, len, r + 4 + 2 * (gc - 1), sc, pos, (int)gc, g, dead, n, depth);
      } else {
        u32 p = r, bc, ic, lc, sc, last;
        int bpos[33];
        bc = be16(t + p); p += 2;
        if (bc > 32 || p + 2 * bc + 2 > len) continue;
        if (!match_seq(t, len, (int)how, bcd, p, bc, g, dead, n, i, bpos, -1)) continue;
        p += 2 * bc;
        ic = be16(t + p); p += 2;
        if (ic < 1 || ic > 32 || p + 2 * (ic - 1) + 2 > len) continue;
        if (!match_seq(t, len, (int)how, icd, p, ic - 1, g, dead, n, i, pos, 1)) continue;
        p += 2 * (ic - 1);
        last = (u32)pos[ic - 1];
        lc = be16(t + p); p += 2;
        if (lc > 32 || p + 2 * lc + 2 > len) continue;
        if (!match_seq(t, len, (int)how, lcd, p, lc, g, dead, n, (int)last, NULL, 1)) continue;
        p += 2 * lc;
        sc = be16(t + p); p += 2;
        return apply_records(t, len, p, sc, pos, (int)ic, g, dead, n, depth);
      }
    }
  }
  return 0;
}
/* one subtable of type at o tried at glyph i; 1 if it changed something */
static int subst(const u8 *t, u32 len, int type, u32 o, u16 *g, u8 *dead, int n, int i, int depth)
{
  int c;
  if (o + 6 > len || depth > 4) return 0;
  if (type == 5 || type == 6) return context(t, len, type, o, g, dead, n, i, depth);
  if (type == 7) {
    u32 et = be16(t + o + 2), eo = be32(t + o + 4);
    if (be16(t + o) != 1 || eo > len - o || et == 7) return 0;
    return subst(t, len, (int)et, o + eo, g, dead, n, i, depth + 1);
  }
  if ((c = cov(t, len, o + be16(t + o + 2), g[i])) < 0) return 0;
  if (type == 1) {
    if (be16(t + o) == 1) { g[i] = (u16)(g[i] + be16(t + o + 4)); return 1; }
    if (be16(t + o) == 2 && (u32)c < be16(t + o + 4) && o + 6 + 2 * (u32)c + 2 <= len) { g[i] = (u16)be16(t + o + 6 + 2 * c); return 1; }
    return 0;
  }
  if (type == 3) {
    u32 as;
    if ((u32)c >= be16(t + o + 4) || o + 6 + 2 * (u32)c + 2 > len) return 0;
    as = o + be16(t + o + 6 + 2 * c);
    if (as + 4 > len || !be16(t + as)) return 0;
    g[i] = (u16)be16(t + as + 2);
    return 1;
  }
  if (type == 4) {
    u32 ls, nl, k;
    if ((u32)c >= be16(t + o + 4) || o + 6 + 2 * (u32)c + 2 > len) return 0;
    ls = o + be16(t + o + 6 + 2 * c);
    if (ls + 2 > len) return 0;
    nl = be16(t + ls);
    if (ls + 2 + 2 * nl > len) return 0;
    for (k = 0; k < nl; k++) {
      u32 lo = ls + be16(t + ls + 2 + 2 * k), nc, m;
      int at = i, ok = 1, pos[16];
      if (lo + 4 > len) continue;
      nc = be16(t + lo + 2);
      if (nc < 1 || nc > 16 || lo + 4 + 2 * (nc - 1) > len) continue;
      for (m = 1; m < nc; m++) {
        do at++; while (at < n && dead[at]);
        if (at >= n || g[at] != be16(t + lo + 4 + 2 * (m - 1))) { ok = 0; break; }
        pos[m] = at;
      }
      if (!ok) continue;
      g[i] = (u16)be16(t + lo);
      for (m = 1; m < nc; m++) dead[pos[m]] = 1;
      return 1;
    }
  }
  return 0;
}
/* lookup li (of the LookupList) at glyph i: its first subtable that applies */
static int lookup_at(const u8 *t, u32 len, u32 li, u16 *g, u8 *dead, int n, int i, int depth)
{
  u32 lo, type, ns, s;
  if (li >= cur_nll || depth > 4) return 0;
  lo = cur_ll + be16(t + cur_ll + 2 + 2 * li);
  if (lo + 6 > len) return 0;
  type = be16(t + lo); ns = be16(t + lo + 4);
  if (lo + 6 + 2 * ns > len) return 0;
  for (s = 0; s < ns; s++) if (subst(t, len, (int)type, lo + be16(t + lo + 6 + 2 * s), g, dead, n, i, depth)) return 1;
  return 0;
}
void gsub_apply(const u8 *t, u32 len, const char *const *tags, int ntags, u16 *g, u8 *dead, int n)
{
  u16 lk[256];
  int nl, k, i;
  u32 ll, nll;
  if (!t || len < 10 || n <= 0) return;
  if (!(nl = lookups_for(t, len, tags, ntags, lk, 256))) return;
  ll = be16(t + 8);
  if (ll + 2 > len) return;
  nll = be16(t + ll);
  if (ll + 2 + 2 * nll > len) return;
  cur_ll = ll; cur_nll = nll;
  for (k = 0; k < nl; k++) {
    u32 lo, type, ns, s;
    if (lk[k] >= nll) continue;
    lo = ll + be16(t + ll + 2 + 2 * lk[k]);
    if (lo + 6 > len) continue;
    type = be16(t + lo); ns = be16(t + lo + 4);
    if (lo + 6 + 2 * ns > len || !(type == 1 || type == 3 || type == 4 || type == 5 || type == 6 || type == 7)) continue;
    for (i = 0; i < n; i++) {
      if (dead[i]) continue;
      for (s = 0; s < ns; s++) if (subst(t, len, (int)type, lo + be16(t + lo + 6 + 2 * s), g, dead, n, i, 0)) break;
    }
  }
}
