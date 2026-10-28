/*
 * amifont.c - Amiga bitmap fonts (see amifont.h). Easel 0.9.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "amifont.h"

static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static u32 be16(const u8 *p) { return ((u32)p[0] << 8) | p[1]; }

/* the first code or data hunk of a hunk executable */
static const u8 *first_hunk(const u8 *d, size_t len, u32 *hlen)
{
  size_t at = 4;
  u32 n, first, last;
  if (len < 24 || be32(d) != 0x3F3) return NULL;
  /* resident library names (none in practice) */
  for (;;) {
    if (at + 4 > len) return NULL;
    n = be32(d + at); at += 4;
    if (!n) break;
    if (n > len / 4 || at + (size_t)n * 4 > len) return NULL;
    at += (size_t)n * 4;
  }
  if (at + 12 > len) return NULL;
  first = be32(d + at + 4); last = be32(d + at + 8); at += 12;
  if (last < first || last - first > 1000) return NULL;
  at += (size_t)(last - first + 1) * 4;                    /* (the hunk sizes) */
  while (at + 8 <= len) {
    u32 type = be32(d + at) & 0x3FFFFFFF;
    if (type == 0x3E9 || type == 0x3EA) {
      n = be32(d + at + 4) & 0x3FFFFFFF;
      at += 8;
      if (n > (len - at) / 4) return NULL;
      *hlen = n * 4;
      return d + at;
    }
    if (type == 0x3E8) {                                   /* HUNK_NAME: skipped */
      n = be32(d + at + 4);
      if (n > (len - at - 8) / 4) return NULL;
      at += 8 + (size_t)n * 4;
      continue;
    }
    return NULL;
  }
  return NULL;
}
AmiFont *amifont_read(const u8 *data, size_t len)
{
  const u8 *h;
  u32 hl, chardata, charloc, charspace, charkern, n, i;
  AmiFont *f;
  if (!data || !(h = first_hunk(data, len, &hl)) || hl < 110) return NULL;
  /* the DiskFontHeader after MOVEQ #-1,D0 / RTS */
  if (be16(h + 18) != 0x0F80) return NULL;
  if (!(f = (AmiFont *)calloc(1, sizeof *f))) return NULL;
  memcpy(f->name, h + 26, 31); f->name[31] = 0;
  f->ysize = (int)be16(h + 78); f->style = h[80]; f->flags = h[81];
  f->xsize = (int)be16(h + 82); f->baseline = (int)be16(h + 84); f->boldsmear = (int)be16(h + 86);
  f->lo = h[90]; f->hi = h[91];
  chardata = be32(h + 92); f->modulo = (int)be16(h + 96);
  charloc = be32(h + 98); charspace = be32(h + 102); charkern = be32(h + 106);
  n = (u32)(f->hi - f->lo + 2);
  if (f->hi < f->lo || f->ysize < 1 || f->ysize > 255 || !f->modulo ||
      chardata > hl || (u32)f->modulo * f->ysize > hl - chardata || charloc > hl || n * 4 > hl - charloc ||
      (charspace && (charspace > hl || n * 2 > hl - charspace)) || (charkern && (charkern > hl || n * 2 > hl - charkern))) goto bad;
  if (!(f->strike = (u8 *)malloc((size_t)f->modulo * f->ysize)) || !(f->loc = (u16 *)malloc(n * 4))) goto bad;
  memcpy(f->strike, h + chardata, (size_t)f->modulo * f->ysize);
  for (i = 0; i < n; i++) {
    f->loc[2 * i] = (u16)be16(h + charloc + 4 * i); f->loc[2 * i + 1] = (u16)be16(h + charloc + 4 * i + 2);
    if ((u32)f->loc[2 * i] + f->loc[2 * i + 1] > (u32)f->modulo * 8) f->loc[2 * i + 1] = 0;     /* (past the strike: blank) */
  }
  if (charspace && (f->flags & 0x20)) {
    if (!(f->space = (i16 *)malloc(n * 2))) goto bad;
    for (i = 0; i < n; i++) f->space[i] = (i16)be16(h + charspace + 2 * i);
  }
  if (charkern) {
    if (!(f->kern = (i16 *)malloc(n * 2))) goto bad;
    for (i = 0; i < n; i++) f->kern[i] = (i16)be16(h + charkern + 2 * i);
  }
  return f;
bad:
  amifont_free(f);
  return NULL;
}
void amifont_free(AmiFont *f)
{
  if (!f) return;
  free(f->strike); free(f->loc); free(f->space); free(f->kern);
  free(f);
}
AmiFont *amifont_load(const char *fontfile, int ysize)
{
  size_t len, dl;
  u8 *d = file_read(fontfile, &len);
  u32 n, i, best = 0;
  int bestd = 1 << 30;
  char path[512];
  AmiFont *f = NULL;
  if (!d) return NULL;
  if (len < 4 || (be16(d) != 0x0F00 && be16(d) != 0x0F02 && be16(d) != 0x0F03)) { free(d); return NULL; }
  n = be16(d + 2);
  if (n > (len - 4) / 260) n = (u32)((len - 4) / 260);
  for (i = 0; i < n; i++) {
    int ys = (int)be16(d + 4 + 260 * i + 256), dd = ys > ysize ? ys - ysize : ysize - ys;
    if (dd < bestd) { bestd = dd; best = i; }
  }
  if (n) {
    char name[257];
    const char *slash;
    u8 *s;
    size_t sl;
    memcpy(name, d + 4 + 260 * best, 256); name[256] = 0;
    if (be16(d) != 0x0F00) name[254] = 0;                  /* (TFontContents: 254 bytes of name) */
    slash = strrchr(fontfile, '/');
    if (!slash) slash = strrchr(fontfile, ':');
    dl = slash ? (size_t)(slash - fontfile + 1) : 0;
    if (dl + strlen(name) < sizeof path) {
      memcpy(path, fontfile, dl); strcpy(path + dl, name);
      if ((s = file_read(path, &sl)) != NULL) { f = amifont_read(s, sl); free(s); }
    }
  }
  free(d);
  return f;
}
int amifont_char(const AmiFont *f, int c, int *adv, int *kern, int *w, u8 *cov)
{
  int i, x, y, off, wd;
  if (c < f->lo || c > f->hi) i = f->hi - f->lo + 1;
  else i = c - f->lo;
  off = f->loc[2 * i]; wd = f->loc[2 * i + 1];
  *kern = f->kern ? f->kern[i] : 0;
  *adv = (f->space ? f->space[i] : f->xsize) + *kern;
  *w = wd;
  if (cov)
    for (y = 0; y < f->ysize; y++)
      for (x = 0; x < wd; x++) {
        int b = off + x;
        cov[y * wd + x] = (u8)(f->strike[(size_t)y * f->modulo + (b >> 3)] & (0x80 >> (b & 7)) ? 255 : 0);
      }
  return 1;
}
