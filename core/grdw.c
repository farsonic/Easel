/*
 * grdw.c - Easel round 3: Photoshop gradient files written (.grd version 5: a descriptor holding a
 * GrdL list of custom gradients, each with its name, colour stops (RGB, or the foreground or
 * background colour) and opacity stops, as Photoshop saves them). brushx.c's grd_read reads them
 * back; psd-tools' descriptor reader checks them in tests/check_files.py.
 */
#include <stdlib.h>
#include <string.h>
#include "brushx.h"
#include "gradient.h"

static void w32(Buf *b, u32 v) { buf_addc(b, (int)(v >> 24 & 255)); buf_addc(b, (int)(v >> 16 & 255)); buf_addc(b, (int)(v >> 8 & 255)); buf_addc(b, (int)(v & 255)); }
static void key(Buf *b, const char *k) { size_t l = strlen(k); if (l == 4) { w32(b, 0); buf_add(b, k, 4); } else { w32(b, (u32)l); buf_add(b, k, l); } }
static void ustr(Buf *b, const char *s)
{
  /* UTF-16 big-endian with its terminating zero, the count first */
  u16 u[130];
  int n = 0, used, k;
  size_t o = 0, sl = strlen(s);
  while (o < sl && n < 126) {
    u32 cp = utf8_get(s + o, sl - o, &used);
    o += (size_t)used;
    u[n++] = (u16)(cp > 0xFFFF ? '?' : cp);
  }
  u[n++] = 0;
  w32(b, (u32)n);
  for (k = 0; k < n; k++) { buf_addc(b, u[k] >> 8); buf_addc(b, u[k] & 255); }
}
static void dbl(Buf *b, double v)
{
  /* an IEEE double, big-endian, built without the machine's own byte order */
  u32 hi = 0, lo = 0;
  int e = 0;
  double m;
  if (v != 0) {
    int neg = v < 0;
    if (neg) v = -v;
    m = v;
    while (m >= 2) { m /= 2; e++; }
    while (m < 1) { m *= 2; e--; }
    m -= 1;
    m *= 1048576.0;                                       /* 20 bits in hi */
    hi = (u32)m; m -= hi;
    lo = (u32)(m * 4294967296.0);
    hi |= (u32)(e + 1023) << 20;
    if (neg) hi |= 0x80000000UL;
  }
  w32(b, hi); w32(b, lo);
}
static void item_long(Buf *b, const char *k, long v) { key(b, k); buf_add(b, "long", 4); w32(b, (u32)v); }
static void item_doub(Buf *b, const char *k, double v) { key(b, k); buf_add(b, "doub", 4); dbl(b, v); }
static void item_enum(Buf *b, const char *k, const char *t, const char *v) { key(b, k); buf_add(b, "enum", 4); key(b, t); key(b, v); }
static void obj(Buf *b, const char *name, const char *cls, u32 n) { ustr(b, name); key(b, cls); w32(b, n); }

u8 *grd_write(const struct Gradient *gs, int n, size_t *len)
{
  Buf b;
  int i, k;
  buf_init(&b);
  buf_add(&b, "8BGR", 4); buf_addc(&b, 0); buf_addc(&b, 5);
  w32(&b, 16);
  obj(&b, "", "null", 1);
  key(&b, "GrdL"); buf_add(&b, "VlLs", 4); w32(&b, (u32)n);
  for (i = 0; i < n; i++) {
    const Gradient *g = &gs[i];
    buf_add(&b, "Objc", 4); obj(&b, "", "Grdn", 1);
    key(&b, "Grad"); buf_add(&b, "Objc", 4); obj(&b, "Gradient", "Grdn", 5);
    key(&b, "Nm  "); buf_add(&b, "TEXT", 4); ustr(&b, g->name);
    item_enum(&b, "GrdF", "GrdF", "CstS");
    item_doub(&b, "Intr", 4096);
    key(&b, "Clrs"); buf_add(&b, "VlLs", 4); w32(&b, (u32)g->ncolor);
    for (k = 0; k < g->ncolor; k++) {
      const GradColorStop *c = &g->color[k];
      buf_add(&b, "Objc", 4); obj(&b, "", "Clrt", c->kind == GS_RGB ? 4 : 3);
      if (c->kind == GS_RGB) {
        key(&b, "Clr "); buf_add(&b, "Objc", 4); obj(&b, "", "RGBC", 3);
        item_doub(&b, "Rd  ", (double)(c->rgb >> 16 & 255)); item_doub(&b, "Grn ", (double)(c->rgb >> 8 & 255)); item_doub(&b, "Bl  ", (double)(c->rgb & 255));
      }
      item_enum(&b, "Type", "Clry", c->kind == GS_FG ? "FrgC" : c->kind == GS_BG ? "BckC" : "UsrS");
      item_long(&b, "Lctn", (long)(c->pos * 4096 + 0.5));
      item_long(&b, "Mdpn", (long)(c->mid * 100 + 0.5));
    }
    key(&b, "Trns"); buf_add(&b, "VlLs", 4); w32(&b, (u32)g->nopacity);
    for (k = 0; k < g->nopacity; k++) {
      const GradOpacityStop *o = &g->opacity[k];
      buf_add(&b, "Objc", 4); obj(&b, "", "TrnS", 3);
      key(&b, "Opct"); buf_add(&b, "UntF", 4); buf_add(&b, "#Prc", 4); dbl(&b, o->opacity * 100);
      item_long(&b, "Lctn", (long)(o->pos * 4096 + 0.5));
      item_long(&b, "Mdpn", (long)(o->mid * 100 + 0.5));
    }
  }
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return (u8 *)buf_take(&b);
}
