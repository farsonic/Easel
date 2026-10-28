/*
 * swatchpal.c - the Swatches panel's palette formats (.aco, .act, .ase, IFF CMAP) read and written
 * by palette.c's palette files, through swatch.h's hooks.
 */
#include <string.h>
#include "swatch.h"
#include "palette.h"

static int read_as(const u8 *data, size_t n, SwatchSet *out, const char *name)
{
  PalFile p;
  char err[120];
  int i;
  if (!palfile_read(data, n, name, &p, err, sizeof err)) return 0;
  for (i = 0; i < p.n; i++) if (!swatches_add(out, ((u32)p.rgb[i * 3] << 16) | ((u32)p.rgb[i * 3 + 1] << 8) | p.rgb[i * 3 + 2], "")) break;
  if (p.name[0] && !out->name[0]) { strncpy(out->name, p.name, sizeof out->name - 1); out->name[sizeof out->name - 1] = 0; }
  return i;
}
static u8 *write_as(int fmt, const SwatchSet *s, size_t *len)
{
  PalFile p;
  int i;
  memset(&p, 0, sizeof p);
  p.n = s->n > 256 ? 256 : s->n;
  if (p.n < 1) return NULL;
  for (i = 0; i < p.n; i++) { p.rgb[i * 3] = (u8)(s->s[i].rgb >> 16); p.rgb[i * 3 + 1] = (u8)(s->s[i].rgb >> 8); p.rgb[i * 3 + 2] = (u8)s->s[i].rgb; }
  p.transparent = -1;
  strncpy(p.name, s->name, sizeof p.name - 1);
  return palfile_write(fmt, &p, len);
}
static int r_aco(const u8 *d, size_t n, SwatchSet *o) { return read_as(d, n, o, "swatches.aco"); }
static int r_act(const u8 *d, size_t n, SwatchSet *o) { return read_as(d, n, o, "swatches.act"); }
static int r_ase(const u8 *d, size_t n, SwatchSet *o) { return read_as(d, n, o, "swatches.ase"); }
static int r_iff(const u8 *d, size_t n, SwatchSet *o) { return read_as(d, n, o, "swatches.iff"); }
static u8 *w_aco(const SwatchSet *s, size_t *len) { return write_as(PF_ACO, s, len); }
static u8 *w_act(const SwatchSet *s, size_t *len) { return write_as(PF_ACT, s, len); }
static u8 *w_ase(const SwatchSet *s, size_t *len) { return write_as(PF_ASE, s, len); }
static u8 *w_iff(const SwatchSet *s, size_t *len) { return write_as(PF_IFF, s, len); }
/* the hooks filled in (once, at start-up) */
void swatch_palette_hooks(void)
{
  swatch_init_hooks();
  swatch_hook_read[SWF_ACO] = r_aco; swatch_hook_write[SWF_ACO] = w_aco;
  swatch_hook_read[SWF_ACT] = r_act; swatch_hook_write[SWF_ACT] = w_act;
  swatch_hook_read[SWF_ASE] = r_ase; swatch_hook_write[SWF_ASE] = w_ase;
  swatch_hook_read[SWF_CMAP] = r_iff; swatch_hook_write[SWF_CMAP] = w_iff;
}
