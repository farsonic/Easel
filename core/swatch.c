/*
 * swatch.c - the Swatches panel's colours (see swatch.h): the list, the defaults, GIMP palettes
 * and Easel's own swatch files, and the hooks for the palette formats palette.c reads and writes.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "swatch.h"

const char *const swatch_ext[SWF_N] = { ".swatches", ".gpl", ".aco", ".act", ".ase", ".iff" };
SwatchReader swatch_hook_read[SWF_N];
SwatchWriter swatch_hook_write[SWF_N];
#ifdef EASEL_HAVE_PALETTE
void palette_swatch_hooks(void);   /* HOOK: palette.c fills in SWF_ACO, SWF_ACT, SWF_ASE and SWF_CMAP */
#endif

void swatch_init_hooks(void)
{
  static int done;
  if (done) return;
  done = 1;
  swatch_hook_read[SWF_EASEL] = swatches_read_easel; swatch_hook_write[SWF_EASEL] = swatches_write_easel;
  swatch_hook_read[SWF_GPL] = swatches_read_gpl; swatch_hook_write[SWF_GPL] = swatches_write_gpl;
#ifdef EASEL_HAVE_PALETTE
  palette_swatch_hooks();
#endif
}

void swatches_init(SwatchSet *s) { memset(s, 0, sizeof *s); }
void swatches_free(SwatchSet *s) { free(s->s); s->s = NULL; s->n = s->cap = 0; }
void swatches_clear(SwatchSet *s) { s->n = 0; }
int swatches_insert(SwatchSet *s, int at, u32 rgb, const char *name)
{
  if (at < 0 || at > s->n) at = s->n;
  if (s->n == s->cap) {
    int nc = s->cap ? s->cap * 2 : 64;
    Swatch *ns = (Swatch *)realloc(s->s, (size_t)nc * sizeof *ns);
    if (!ns) return 0;
    s->s = ns; s->cap = nc;
  }
  memmove(&s->s[at + 1], &s->s[at], (size_t)(s->n - at) * sizeof s->s[0]);
  s->s[at].rgb = rgb & 0xFFFFFF;
  memset(s->s[at].name, 0, sizeof s->s[at].name);
  if (name) strncpy(s->s[at].name, name, sizeof s->s[at].name - 1);
  s->n++;
  return 1;
}
int swatches_add(SwatchSet *s, u32 rgb, const char *name) { return swatches_insert(s, s->n, rgb, name); }
void swatches_remove(SwatchSet *s, int i)
{
  if (i < 0 || i >= s->n) return;
  memmove(&s->s[i], &s->s[i + 1], (size_t)(s->n - i - 1) * sizeof s->s[0]);
  s->n--;
}
int swatches_find(const SwatchSet *s, u32 rgb)
{
  int i;
  for (i = 0; i < s->n; i++) if (s->s[i].rgb == (rgb & 0xFFFFFF)) return i;
  return -1;
}
int swatches_defaults(SwatchSet *s)
{
  static const u32 c[] = {
    0x000000, 0x404040, 0x808080, 0xC0C0C0, 0xFFFFFF, 0xFF0000, 0xFF8000, 0xFFFF00, 0x80FF00, 0x00FF00, 0x00FF80, 0x00FFFF,
    0x0080FF, 0x0000FF, 0x8000FF, 0xFF00FF, 0xFF0080, 0x800000, 0x804000, 0x808000, 0x008000, 0x008080, 0x000080, 0x800080,
    0xF4C9A0, 0xE0A070, 0xA06030, 0x603010, 0xFFC0C0, 0xC0FFC0, 0xC0C0FF, 0xFFE0A0,
    /* the Workbench's own four, 1.3 and 2.0 */
    0x0055AA, 0xFFFFFF, 0x000020, 0xFF8800, 0xAAAAAA, 0x000000, 0xFFFFFF, 0x6688BB
  };
  int i, n0 = s->n;
  if (!s->name[0]) strcpy(s->name, "Default");
  for (i = 0; i < (int)(sizeof c / sizeof c[0]); i++) swatches_add(s, c[i], i >= 32 ? (i < 36 ? "Workbench 1.3" : "Workbench 2.0") : "");
  return s->n - n0;
}

/* ------------------------------------------------------------------ text helpers */
static int next_line(const u8 *t, size_t n, size_t *o, char *line, size_t cap)
{
  size_t k = 0;
  if (*o >= n) return 0;
  while (*o < n && t[*o] != '\n') { if (k < cap - 1 && t[*o] != '\r') line[k++] = (char)t[*o]; (*o)++; }
  if (*o < n) (*o)++;
  line[k] = 0;
  return 1;
}
static const char *skip_ws(const char *p) { while (*p == ' ' || *p == '\t') p++; return p; }

/* ------------------------------------------------------------------ GIMP .gpl */
int swatches_read_gpl(const u8 *d, size_t n, SwatchSet *out)
{
  char line[256];
  size_t o = 0;
  int count = 0;
  if (!next_line(d, n, &o, line, sizeof line) || strncmp(line, "GIMP Palette", 12)) return 0;
  while (next_line(d, n, &o, line, sizeof line)) {
    const char *p = skip_ws(line);
    int r, g, b, used = 0;
    if (!*p || *p == '#') continue;
    if (!strncmp(p, "Name:", 5)) { strncpy(out->name, skip_ws(p + 5), sizeof out->name - 1); continue; }
    if (!strncmp(p, "Columns:", 8)) continue;
    if (sscanf(p, "%d %d %d%n", &r, &g, &b, &used) >= 3) {
      if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) continue;
      p = skip_ws(p + used);
      if (swatches_add(out, ((u32)r << 16) | ((u32)g << 8) | (u32)b, !strcmp(p, "Untitled") ? "" : p)) count++;
    }
  }
  return count;
}
u8 *swatches_write_gpl(const SwatchSet *s, size_t *len)
{
  Buf b;
  int i;
  buf_init(&b);
  buf_printf(&b, "GIMP Palette\nName: %s\nColumns: 16\n#\n", s->name[0] ? s->name : "Easel");
  for (i = 0; i < s->n; i++)
    buf_printf(&b, "%3d %3d %3d\t%s\n", (int)(s->s[i].rgb >> 16) & 255, (int)(s->s[i].rgb >> 8) & 255, (int)s->s[i].rgb & 255, s->s[i].name[0] ? s->s[i].name : "Untitled");
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return (u8 *)buf_take(&b);
}

/* ------------------------------------------------------------------ Easel's own: "RRGGBB name" lines */
int swatches_read_easel(const u8 *d, size_t n, SwatchSet *out)
{
  char line[256];
  size_t o = 0;
  int count = 0;
  if (!next_line(d, n, &o, line, sizeof line) || strncmp(line, "EaselSwatches", 13)) return 0;
  while (next_line(d, n, &o, line, sizeof line)) {
    const char *p = skip_ws(line);
    char *e;
    unsigned long v;
    if (!*p || *p == '#') continue;
    if (!strncmp(p, "name ", 5)) { strncpy(out->name, skip_ws(p + 5), sizeof out->name - 1); continue; }
    v = strtoul(p, &e, 16);
    if (e - p != 6) continue;
    if (swatches_add(out, (u32)v, skip_ws(e))) count++;
  }
  return count;
}
u8 *swatches_write_easel(const SwatchSet *s, size_t *len)
{
  Buf b;
  int i;
  buf_init(&b);
  buf_adds(&b, "EaselSwatches 1\n");
  if (s->name[0]) buf_printf(&b, "name %s\n", s->name);
  for (i = 0; i < s->n; i++) buf_printf(&b, "%06lX %s\n", (unsigned long)s->s[i].rgb, s->s[i].name);
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return (u8 *)buf_take(&b);
}

/* ------------------------------------------------------------------ files */
int swatch_format_of(const char *path)
{
  size_t n = strlen(path);
  int f;
  for (f = 0; f < SWF_N; f++) {
    size_t e = strlen(swatch_ext[f]);
    if (n >= e && str_ieq(path + n - e, swatch_ext[f])) return f;
  }
  if (n >= 5 && str_ieq(path + n - 5, ".cmap")) return SWF_CMAP;
  if (n >= 5 && str_ieq(path + n - 5, ".ilbm")) return SWF_CMAP;
  return SWF_EASEL;
}
int swatch_supported(int fmt, int writing)
{
  swatch_init_hooks();
  if (fmt < 0 || fmt >= SWF_N) return 0;
  return writing ? swatch_hook_write[fmt] != NULL : swatch_hook_read[fmt] != NULL;
}
int swatches_load(SwatchSet *s, const char *path)
{
  size_t len;
  u8 *d;
  int fmt, n = 0;
  swatch_init_hooks();
  if (!(d = file_read(path, &len))) return 0;
  /* the content first (a text file says what it is), then the extension */
  if (len >= 12 && !strncmp((const char *)d, "GIMP Palette", 12)) n = swatches_read_gpl(d, len, s);
  else if (len >= 13 && !strncmp((const char *)d, "EaselSwatches", 13)) n = swatches_read_easel(d, len, s);
  else {
    fmt = swatch_format_of(path);
    if (swatch_hook_read[fmt]) n = swatch_hook_read[fmt](d, len, s);
  }
  free(d);
  return n;
}
int swatches_save(const SwatchSet *s, const char *path)
{
  int fmt = swatch_format_of(path), ok;
  size_t len;
  u8 *d;
  swatch_init_hooks();
  if (!swatch_hook_write[fmt] || !(d = swatch_hook_write[fmt](s, &len))) return 0;
  ok = file_write(path, d, len);
  free(d);
  return ok;
}
