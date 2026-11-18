/*
 * lut3dl.c - Easel 1.2: Autodesk .3dl 3D LUTs for Color Lookup (adjustx.h): the input shaper line
 * (n levels, 17 to 65), then n^3 lines of three integers with blue changing fastest, their range
 * found from the largest value (10, 12 or 16 bits). Made into adjustx.h's table (red fastest).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "adjustx.h"

static void seterr(char *err, int cap, const char *m) { if (err && cap > 0) { strncpy(err, m, (size_t)cap - 1); err[cap - 1] = 0; } }
/* the integers on one line into v (at most max); how many; *p moves past the line */
static int line_ints(const char **p, const char *end, long *v, int max)
{
  const char *s = *p;
  int n = 0, bad = 0;
  while (s < end && *s != '\n') {
    if (*s == '#') { while (s < end && *s != '\n') s++; break; }
    if (*s >= '0' && *s <= '9') {
      long x = 0;
      while (s < end && *s >= '0' && *s <= '9') { if (x < 100000000L) x = x * 10 + (*s - '0'); s++; }
      if (n < max) v[n] = x;
      n++;
    } else if (*s == ' ' || *s == '\t' || *s == '\r') s++;
    else { bad = 1; s++; }
  }
  if (s < end) s++;
  *p = s;
  return bad ? -1 : n;
}
Lut3D *lut_parse_3dl(const char *text, long len, char *err, int errcap)
{
  const char *p = text, *end = text + len;
  long v[70], mx = 0, k, n3;
  int n = 0, got;
  Lut3D *l;
  long *raw;
  /* the shaper line */
  while (p < end) {
    got = line_ints(&p, end, v, 70);
    if (got == 0) continue;
    if (got < 2 || got > LUT_MAXN) { seterr(err, errcap, "Not a .3dl LUT"); return NULL; }
    n = got;
    break;
  }
  if (n < 2) { seterr(err, errcap, "Not a .3dl LUT"); return NULL; }
  n3 = (long)n * n * n;
  if (!(raw = (long *)malloc((size_t)n3 * 3 * sizeof(long)))) { seterr(err, errcap, "Not enough memory for the LUT"); return NULL; }
  for (k = 0; k < n3 && p < end; ) {
    got = line_ints(&p, end, v, 4);
    if (got == 0) continue;
    if (got != 3) break;
    raw[k * 3] = v[0]; raw[k * 3 + 1] = v[1]; raw[k * 3 + 2] = v[2];
    if (v[0] > mx) mx = v[0];
    if (v[1] > mx) mx = v[1];
    if (v[2] > mx) mx = v[2];
    k++;
  }
  if (k != n3) { free(raw); seterr(err, errcap, "The .3dl LUT is short of entries"); return NULL; }
  if (!(l = lut_identity(n))) { free(raw); seterr(err, errcap, "Not enough memory for the LUT"); return NULL; }
  {
    double full = mx <= 1023 ? 1023 : mx <= 4095 ? 4095 : 65535;
    int r, g, b;
    for (r = 0; r < n; r++) for (g = 0; g < n; g++) for (b = 0; b < n; b++) {
      long s = ((long)r * n + g) * n + b, d = ((long)b * n + g) * n + r;
      int c;
      for (c = 0; c < 3; c++) l->t[d * 3 + c] = (float)(raw[s * 3 + c] / full);
    }
  }
  strcpy(l->title, "3DL");
  free(raw);
  return l;
}
