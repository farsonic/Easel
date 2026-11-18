/*
 * lutlook.c - Easel round 3: SpeedGrade .look files for Color Lookup (adjustx.h). A .look is XML
 * (UTF-8, or UTF-16 either way round with a byte-order mark) whose <LUT> element holds <size>
 * (the table's side, often in quotes) and <data>: n^3 RGB entries of little-endian 32-bit floats
 * written as hex, eight digits a float, red changing fastest (the .cube order). Everything else in
 * the file (the shaders it was graded with) is ignored, as Photoshop does.
 */
#include <stdlib.h>
#include <string.h>
#include "adjustx.h"

static void seterr(char *err, int cap, const char *m) { if (err && cap > 0) { strncpy(err, m, (size_t)cap - 1); err[cap - 1] = 0; } }

/* the first <tag> ... </tag> inside s (n bytes): where its text starts and its length; 0 if none */
static int element(const char *s, long n, const char *tag, long *at, long *len)
{
  long tl = (long)strlen(tag), i, j;
  for (i = 0; i + tl + 2 <= n; i++) {
    if (s[i] != '<' || memcmp(s + i + 1, tag, (size_t)tl) || (s[i + 1 + tl] != '>' && s[i + 1 + tl] != ' ')) continue;
    while (i < n && s[i] != '>') i++;
    if (i >= n) return 0;
    i++;
    for (j = i; j + tl + 3 <= n; j++)
      if (s[j] == '<' && s[j + 1] == '/' && !memcmp(s + j + 2, tag, (size_t)tl)) { *at = i; *len = j - i; return 1; }
    return 0;
  }
  return 0;
}
static int hexval(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

/* a little-endian IEEE single from its four bytes, as a float on any machine (vbcc's 68k too) */
static float le_float(const u8 *b)
{
  u32 u = (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24);
  float f;
  memcpy(&f, &u, 4);
  return f;
}

Lut3D *lut_parse_look(const char *text, long len, char *err, int errcap)
{
  char *s = NULL;
  const char *t = text;
  long n = len, at, el, i, k, n3, need;
  int size = 0, nib[8], got = 0;
  Lut3D *l = NULL;
  u8 b[4];
  /* UTF-16 (with a byte-order mark): the low bytes are the ASCII the parts read here are made of */
  if (len >= 2 && (((u8)text[0] == 0xFF && (u8)text[1] == 0xFE) || ((u8)text[0] == 0xFE && (u8)text[1] == 0xFF))) {
    int lo = (u8)text[0] == 0xFF ? 0 : 1;
    if (!(s = (char *)malloc((size_t)(len / 2) + 1))) { seterr(err, errcap, "Not enough memory for the LUT"); return NULL; }
    for (i = 2, n = 0; i + 1 < len; i += 2) s[n++] = (char)((u8)text[i + 1 - lo] ? '?' : text[i + lo]);
    t = s;
  }
  if (!element(t, n, "LUT", &at, &el)) { seterr(err, errcap, "Not a .look LUT"); goto out; }
  t += at; n = el;
  {
    long sa, sl;
    if (!element(t, n, "size", &sa, &sl)) { seterr(err, errcap, "The .look LUT has no size"); goto out; }
    for (i = sa; i < sa + sl; i++) if (t[i] >= '0' && t[i] <= '9') { if (size < 1000) size = size * 10 + (t[i] - '0'); }
    if (size < 2 || size > LUT_MAXN) { seterr(err, errcap, "The .look LUT's size is out of range"); goto out; }
    if (!element(t, n, "data", &at, &el)) { seterr(err, errcap, "The .look LUT has no data"); goto out; }
  }
  n3 = (long)size * size * size;
  need = n3 * 3;
  if (!(l = lut_identity(size))) { seterr(err, errcap, "Not enough memory for the LUT"); goto out; }
  for (i = at, k = 0; i < at + el && k < need; i++) {
    int v = hexval((u8)t[i]);
    if (v < 0) { if (t[i] == '"' || t[i] == ' ' || t[i] == '\n' || t[i] == '\r' || t[i] == '\t') continue; break; }
    nib[got++] = v;
    if (got == 8) {
      float f;
      b[0] = (u8)(nib[0] << 4 | nib[1]); b[1] = (u8)(nib[2] << 4 | nib[3]);
      b[2] = (u8)(nib[4] << 4 | nib[5]); b[3] = (u8)(nib[6] << 4 | nib[7]);
      f = le_float(b);
      if (!(f == f)) f = 0;                            /* NaN */
      if (f < -16) f = -16;
      if (f > 16) f = 16;
      l->t[k++] = f;
      got = 0;
    }
  }
  if (k != need) { lut_free(l); l = NULL; seterr(err, errcap, "The .look LUT is short of entries"); goto out; }
  strcpy(l->title, "LOOK");
out:
  free(s);
  return l;
}
