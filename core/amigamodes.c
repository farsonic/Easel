/*
 * amigamodes.c - the classic Amiga display modes and the checks against them (see amigamodes.h).
 * ModeIDs are graphics/modeid.h's (V39). Colour limits are the chipsets' documented ones: OCS/ECS
 * lores 32 colours (5 planes), 64 in EHB and 4096 in HAM6; hires 16; ECS super-hires and
 * productivity 4 (from a 64-colour palette); AGA 256 (8 planes) and HAM8 in every mode.
 */
#include <stdio.h>
#include <string.h>
#include "amigamodes.h"
#include "codec.h"

#define E AM_EST_OVERSCAN
#define A AM_EST_ASPECT
#define L AM_LACED
#define P AM_PAL
#define N AM_NTSC
static const AmigaMode modes[] = {
  /* name                          ModeID       chip   d ham ehb  aga d/ham/ehb   w     h    std w/h      max w/h      ax  ay  tx  ty  flags */
  { "PAL Low Res",                 0x00021000, AC_OCS, 5, 1, 1,  8, 1, 1,  320,  256,  352,  283,  368,  283, 10, 11, 44, 44, E | P },
  { "PAL Low Res Laced",           0x00021004, AC_OCS, 5, 1, 1,  8, 1, 1,  320,  512,  352,  566,  368,  566, 20, 11, 44, 22, E | L | P },
  { "PAL High Res",                0x00029000, AC_OCS, 4, 0, 0,  8, 1, 1,  640,  256,  704,  283,  736,  283,  5, 11, 22, 44, E | P },
  { "PAL High Res Laced",          0x00029004, AC_OCS, 4, 0, 0,  8, 1, 1,  640,  512,  704,  566,  736,  566, 10, 11, 22, 22, E | L | P },
  { "PAL Super-High Res",          0x00029020, AC_ECS, 2, 0, 0,  8, 1, 1, 1280,  256, 1408,  283, 1472,  283,  5, 22, 11, 44, E | A | P },
  { "PAL Super-High Res Laced",    0x00029024, AC_ECS, 2, 0, 0,  8, 1, 1, 1280,  512, 1408,  566, 1472,  566,  5, 11, 11, 22, E | A | L | P },
  { "NTSC Low Res",                0x00011000, AC_OCS, 5, 1, 1,  8, 1, 1,  320,  200,  352,  241,  368,  241, 44, 52, 44, 52, E | N },
  { "NTSC Low Res Laced",          0x00011004, AC_OCS, 5, 1, 1,  8, 1, 1,  320,  400,  352,  482,  368,  482, 44, 26, 44, 26, E | L | N },
  { "NTSC High Res",               0x00019000, AC_OCS, 4, 0, 0,  8, 1, 1,  640,  200,  704,  241,  736,  241, 22, 52, 22, 52, E | N },
  { "NTSC High Res Laced",         0x00019004, AC_OCS, 4, 0, 0,  8, 1, 1,  640,  400,  704,  482,  736,  482, 44, 52, 22, 26, E | L | N },
  { "NTSC Super-High Res",         0x00019020, AC_ECS, 2, 0, 0,  8, 1, 1, 1280,  200, 1408,  241, 1472,  241, 11, 52, 11, 52, E | N },
  { "NTSC Super-High Res Laced",   0x00019024, AC_ECS, 2, 0, 0,  8, 1, 1, 1280,  400, 1408,  482, 1472,  482, 22, 52, 11, 26, E | L | N },
  { "Multiscan Productivity",      0x00039024, AC_ECS, 2, 0, 0,  8, 1, 1,  640,  480,  640,  480,  680,  480,  1,  1, 22, 22, E | A },
  { "Multiscan Productivity Laced",0x00039025, AC_ECS, 2, 0, 0,  8, 1, 1,  640,  960,  640,  960,  680,  960,  2,  1, 22, 11, E | A | L },
  { "Multiscan Low Res",           0x00039004, AC_AGA, 0, 0, 0,  8, 1, 1,  320,  480,  320,  480,  340,  480,  2,  1, 44, 22, E | A },
  { "Multiscan Extra-Low Res",     0x00031004, AC_AGA, 0, 0, 0,  8, 1, 1,  160,  480,  160,  480,  170,  480,  4,  1, 88, 22, E | A },
  { "Euro72 Productivity",         0x00069024, AC_ECS, 2, 0, 0,  8, 1, 1,  640,  400,  640,  400,  680,  400,  1,  1, 22, 22, E | A },
  { "DblPAL Low Res",              0x000A1000, AC_AGA, 0, 0, 0,  8, 1, 1,  320,  256,  352,  283,  368,  283, 10, 11, 44, 44, E | P },
  { "DblPAL Low Res (Flicker-Free)",0x000A1004,AC_AGA, 0, 0, 0,  8, 1, 1,  320,  512,  352,  566,  368,  566, 20, 11, 44, 22, E | L | P },
  { "DblPAL High Res",             0x000A9000, AC_AGA, 0, 0, 0,  8, 1, 1,  640,  256,  704,  283,  736,  283,  5, 11, 22, 44, E | P },
  { "DblPAL High Res (Flicker-Free)",0x000A9004,AC_AGA,0, 0, 0,  8, 1, 1,  640,  512,  704,  566,  736,  566, 10, 11, 22, 22, E | L | P },
  { "DblNTSC Low Res",             0x00091000, AC_AGA, 0, 0, 0,  8, 1, 1,  320,  200,  352,  241,  368,  241, 44, 52, 44, 52, E | N },
  { "DblNTSC Low Res (Flicker-Free)",0x00091004,AC_AGA,0, 0, 0,  8, 1, 1,  320,  400,  352,  482,  368,  482, 44, 26, 44, 26, E | L | N },
  { "DblNTSC High Res",            0x00099000, AC_AGA, 0, 0, 0,  8, 1, 1,  640,  200,  704,  241,  736,  241, 22, 52, 22, 52, E | N },
  { "DblNTSC High Res (Flicker-Free)",0x00099004,AC_AGA,0,0, 0,  8, 1, 1,  640,  400,  704,  482,  736,  482, 44, 52, 22, 26, E | L | N },
};
#undef E
#undef A
#undef L
#undef P
#undef N
#define NMODES (int)(sizeof modes / sizeof modes[0])
#define CAMG_HAM 0x800
#define CAMG_EHB 0x80

int amode_count(void) { return NMODES; }
const AmigaMode *amode_get(int i) { return i >= 0 && i < NMODES ? &modes[i] : NULL; }
int amode_find(u32 modeid)
{
  int i;
  u32 id = modeid & ~(u32)(CAMG_HAM | CAMG_EHB);
  for (i = 0; i < NMODES; i++) if (modes[i].modeid == id) return i;
  /* an old-style ViewModes CAMG (no monitor bits): as PAL */
  if (!(id & 0xFFFF0000UL)) {
    u32 k = id & 0x8024;
    for (i = 0; i < NMODES; i++) if ((modes[i].modeid & 0xFFFF1000UL) == 0x00021000UL && (modes[i].modeid & 0x8024) == k) return i;
  }
  return -1;
}
int amode_limits(const AmigaMode *m, int chipset, int *depth, long *colors, int *ham6, int *ham8, int *ehb)
{
  int dd, h6, h8, e;
  if (!m || chipset < m->chipset) return 0;
  if (chipset == AC_AGA) { dd = m->aga_depth; h6 = m->aga_ham; h8 = m->aga_ham; e = m->aga_ehb; }
  else { dd = m->depth; h6 = m->ham; h8 = 0; e = m->ehb; }
  if (!dd) return 0;
  if (depth) *depth = dd;
  if (colors) *colors = 1L << dd;
  if (ham6) *ham6 = h6;
  if (ham8) *ham8 = h8;
  if (ehb) *ehb = e;
  return 1;
}
int amode_aspect(u32 modeid, u8 *ax, u8 *ay)
{
  int i = amode_find(modeid);
  if (i < 0) { *ax = *ay = 1; return 0; }
  *ax = modes[i].ax; *ay = modes[i].ay;
  return 1;
}
void amode_size(const AmigaMode *m, int overscan, int *w, int *h)
{
  if (overscan == 2) { *w = m->max_w; *h = m->max_h; }
  else if (overscan == 1) { *w = m->std_w; *h = m->std_h; }
  else { *w = m->w; *h = m->h; }
}
int amode_suggest(const Doc *d, int chipset, int pal)
{
  int i, best = -1;
  long bestarea = 0;
  int need = d->mode == MODE_INDEXED || d->mode == MODE_BITMAP ? d->npal : d->mode == MODE_GRAY ? 256 : 4096;
  for (i = 0; i < NMODES; i++) {
    const AmigaMode *m = &modes[i];
    int dd, h6, h8, e;
    long cols, area;
    if (!amode_limits(m, chipset, &dd, &cols, &h6, &h8, &e)) continue;
    if ((m->flags & (AM_PAL | AM_NTSC)) && !(m->flags & (pal ? AM_PAL : AM_NTSC))) continue;
    if (m->max_w < d->w || m->max_h < d->h) continue;
    if (need > cols && !(need <= 64 && e) && !h6 && !h8) continue;
    area = (long)m->w * m->h;
    /* the smallest screen that holds it; among equals, the one with the most colours */
    if (best < 0 || area < bestarea) { best = i; bestarea = area; }
  }
  return best;
}
static void addw(char *warn, size_t cap, int *n, const char *s)
{
  size_t l = strlen(warn);
  if (l + strlen(s) + 2 < cap) { strcpy(warn + l, s); strcat(warn + l, "\n"); }
  (*n)++;
}
int amode_check(const Doc *d, const AmigaMode *m, int chipset, int kind, int planes, char *warn, size_t cap)
{
  char line[160];
  int n = 0, dd = 0, h6 = 0, h8 = 0, e = 0, need;
  long cols = 0;
  static const char *const chips[3] = { "OCS", "ECS", "AGA" };
  if (cap) warn[0] = 0;
  if (!d || !m) return 0;
  if (!amode_limits(m, chipset, &dd, &cols, &h6, &h8, &e)) {
    sprintf(line, "%s needs the %s chipset.", m->name, chips[m->chipset > AC_AGA ? AC_AGA : m->chipset]);
    addw(warn, cap, &n, line);
    return n;
  }
  if (d->w > m->max_w || d->h > m->max_h) {
    sprintf(line, "The picture (%ld x %ld) is larger than the mode's largest overscan (%d x %d): it will have to scroll.",
            (long)d->w, (long)d->h, (int)m->max_w, (int)m->max_h);
    addw(warn, cap, &n, line);
  }
  if (kind == IK_HAM6 && !h6) { sprintf(line, "HAM6 isn't available in %s on %s.", m->name, chips[chipset]); addw(warn, cap, &n, line); }
  if ((kind == IK_HAM8) && !h8) { sprintf(line, "HAM8 needs AGA (and isn't available in %s on %s).", m->name, chips[chipset]); addw(warn, cap, &n, line); }
  if (kind == IK_SHAM && (!h6 || chipset == AC_AGA)) { sprintf(line, "SHAM is an OCS/ECS HAM6 trick and needs a viewer that loads line palettes."); addw(warn, cap, &n, line); }
  if (kind == IK_EHB && !e) { sprintf(line, "Extra Half-Brite isn't available in %s on %s.", m->name, chips[chipset]); addw(warn, cap, &n, line); }
  if (kind == IK_DEEP24) addw(warn, cap, &n, "A 24-bit ILBM can't be shown on a native screen; it needs a converter or an RTG viewer.");
  if (kind == IK_NORMAL) {
    need = d->mode == MODE_INDEXED || d->mode == MODE_BITMAP ? d->npal : 0;
    if (planes > dd) { sprintf(line, "%d planes are more than %s allows on %s (%d).", planes, m->name, chips[chipset], dd); addw(warn, cap, &n, line); }
    if (need > cols) { sprintf(line, "The palette has %d colours; the mode shows at most %ld: it will be reduced.", need, cols); addw(warn, cap, &n, line); }
    if (!need) { sprintf(line, "The picture isn't indexed: it will be reduced to %ld colours.", planes > 0 && planes < dd ? 1L << planes : cols); addw(warn, cap, &n, line); }
  }
  if (d->aspect_x && d->aspect_y && (long)d->aspect_x * m->ay != (long)d->aspect_y * m->ax && !(d->aspect_x == 1 && d->aspect_y == 1)) {
    sprintf(line, "The picture's pixel aspect (%d:%d) differs from the mode's (%d:%d).", (int)d->aspect_x, (int)d->aspect_y, (int)m->ax, (int)m->ay);
    addw(warn, cap, &n, line);
  }
  return n;
}
u32 amode_camg(const AmigaMode *m, int kind)
{
  u32 id = m ? m->modeid : 0;
  if (kind == IK_HAM6 || kind == IK_HAM8 || kind == IK_SHAM) id |= CAMG_HAM;
  else if (kind == IK_EHB) id |= CAMG_EHB;
  return id;
}
