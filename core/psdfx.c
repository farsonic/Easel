/*
 * psdfx.c - Easel 1.2: layer styles in Photoshop files: the "lfx2" tagged block (Object Effects,
 * an action descriptor) written from a layer's LayerFx and read back into one (see psdfx.h). The
 * nine effects Easel draws (core/fx.c) map onto Photoshop's DrSh, IrSh, OrGl, IrGl, ebbl, ChFX,
 * SoFi, GrFl and FrFX objects; what Easel hasn't (contours, noise, jitter, textures, gradients of
 * more than two stops) is written at Photoshop's defaults and read as near as Easel can.
 * Every count and length in a file is checked: a damaged descriptor is passed over.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "psdfx.h"
#include "fx.h"
#include "paint.h"

/* ------------------------------------------------------------------ blend modes */
/* Photoshop's BlnM enumeration by BM_ (doc.h) */
static const char *const blnm[BM_N] = { "Nrml", "Mltp", "Scrn", "Ovrl", "Drkn", "Lghn", "Dfrn", "linearDodge",
  "blendSubtraction", "CDdg", "CBrn", "SftL", "HrdL", "Xclu", "H   ", "Strt", "Clr ", "Lmns",
  "Dslv", "linearBurn", "darkerColor", "lighterColor", "vividLight", "linearLight", "pinLight", "hardMix", "blendDivide",
  "Nrml", "Nrml", "Nrml", "Nrml", "Nrml", "Nrml" };
static int blend_of(const char *s)
{
  int i;
  for (i = 0; i < BM_N && i < BM_REFLECT; i++) if (!strcmp(blnm[i], s)) return i;
  return BM_NORMAL;
}

/* ------------------------------------------------------------------ writing */
static void w32(Buf *b, u32 v) { buf_addc(b, (int)(v >> 24)); buf_addc(b, (int)(v >> 16 & 255)); buf_addc(b, (int)(v >> 8 & 255)); buf_addc(b, (int)(v & 255)); }
static void wdouble(Buf *b, double v)
{
  /* IEEE 754 big-endian, built from the number (no union tricks: the 68k and x86 agree this way) */
  u8 o[8];
  u32 hi = 0, lo = 0;
  int e, k;
  double m;
  memset(o, 0, 8);
  if (v != 0) {
    int neg = v < 0;
    if (neg) v = -v;
    m = frexp(v, &e);                    /* v = m * 2^e, 0.5 <= m < 1 */
    e += 1022;
    m = m * 2 - 1;                       /* the fraction past the leading 1 */
    {
      double f = m * 1048576.0;          /* 20 bits in hi */
      hi = (u32)f;
      f = (f - hi) * 4294967296.0;
      lo = (u32)f;
    }
    hi |= (u32)e << 20;
    if (neg) hi |= 0x80000000UL;
  }
  for (k = 0; k < 4; k++) { o[k] = (u8)(hi >> (24 - 8 * k)); o[4 + k] = (u8)(lo >> (24 - 8 * k)); }
  buf_add(b, o, 8);
}
/* a key or class ID: four characters as a length of 0 and the characters, longer ones counted */
static void wid(Buf *b, const char *s)
{
  size_t n = strlen(s);
  if (n == 4) { w32(b, 0); buf_add(b, s, 4); }
  else { w32(b, (u32)n); buf_add(b, s, n); }
}
static void wkey(Buf *b, const char *k, const char *type) { wid(b, k); buf_add(b, type, 4); }
static void wbool(Buf *b, const char *k, int v) { wkey(b, k, "bool"); buf_addc(b, v ? 1 : 0); }
static void wenum(Buf *b, const char *k, const char *type, const char *v) { wkey(b, k, "enum"); wid(b, type); wid(b, v); }
static void wunit(Buf *b, const char *k, const char *unit, double v) { wkey(b, k, "UntF"); buf_add(b, unit, 4); wdouble(b, v); }
static void wdoub(Buf *b, const char *k, double v) { wkey(b, k, "doub"); wdouble(b, v); }
static void wlong(Buf *b, const char *k, u32 v) { wkey(b, k, "long"); w32(b, v); }
/* a descriptor's head: an empty name, its class and how many items follow */
static void wobj(Buf *b, const char *cls, u32 n) { w32(b, 0); wid(b, cls); w32(b, n); }
static void wcolor(Buf *b, const char *k, u32 c)
{
  wkey(b, k, "Objc"); wobj(b, "RGBC", 3);
  wdoub(b, "Rd  ", (double)(c >> 16 & 255)); wdoub(b, "Grn ", (double)(c >> 8 & 255)); wdoub(b, "Bl  ", (double)(c & 255));
}
static void wtext(Buf *b, const char *k, const char *s)
{
  size_t n = strlen(s), i;
  wkey(b, k, "TEXT"); w32(b, (u32)n + 1);
  for (i = 0; i < n; i++) { buf_addc(b, 0); buf_addc(b, s[i]); }
  buf_addc(b, 0); buf_addc(b, 0);
}
static double pct(int v255) { return v255 * 100.0 / 255; }
static void contour(Buf *b, const char *k)
{
  /* the linear contour */
  wkey(b, k, "Objc"); wobj(b, "ShpC", 2);
  wtext(b, "Nm  ", "Linear");
  wkey(b, "Crv ", "VlLs"); w32(b, 2);
  buf_add(b, "Objc", 4); wobj(b, "CrPt", 2); wdoub(b, "Hrzn", 0); wdoub(b, "Vrtc", 0);
  buf_add(b, "Objc", 4); wobj(b, "CrPt", 2); wdoub(b, "Hrzn", 255); wdoub(b, "Vrtc", 255);
}
static void write_one(Buf *b, int k, const FxOne *e)
{
  static const char *const ids[FX_N] = { "DrSh", "IrSh", "OrGl", "IrGl", "ebbl", "ChFX", "SoFi", "GrFl", "FrFX" };
  const char *md = blnm[e->blend < BM_N ? e->blend : 0];
  int global = (e->flags & FXF_GLOBAL) != 0;
  wkey(b, ids[k], "Objc");
  switch (k) {
  case FX_DROPSHADOW: case FX_INNERSHADOW:
    wobj(b, ids[k], (k == FX_DROPSHADOW ? 13 : 12) + 2); wbool(b, "present", 1); wbool(b, "showInDialog", 1);
    wbool(b, "enab", e->on); wenum(b, "Md  ", "BlnM", md); wcolor(b, "Clr ", e->color);
    wunit(b, "Opct", "#Prc", pct(e->opacity)); wbool(b, "uglg", global); wunit(b, "lagl", "#Ang", e->angle);
    wunit(b, "Dstn", "#Pxl", e->distance); wunit(b, "Ckmt", "#Pxl", e->size * e->spread / 100.0); wunit(b, "blur", "#Pxl", e->size);
    wunit(b, "Nose", "#Prc", 0); wbool(b, "AntA", 0); contour(b, "TrnS");
    if (k == FX_DROPSHADOW) wbool(b, "layerConceals", (e->flags & FXF_KNOCKOUT) != 0);
    break;
  case FX_OUTERGLOW: case FX_INNERGLOW:
    wobj(b, ids[k], (k == FX_INNERGLOW ? 12 : 11) + 2); wbool(b, "present", 1); wbool(b, "showInDialog", 1);
    wbool(b, "enab", e->on); wenum(b, "Md  ", "BlnM", md); wcolor(b, "Clr ", e->color);
    wunit(b, "Opct", "#Prc", pct(e->opacity)); wenum(b, "GlwT", "BETE", "SfBL"); wunit(b, "Ckmt", "#Pxl", e->size * e->spread / 100.0);
    wunit(b, "blur", "#Pxl", e->size); wunit(b, "Nose", "#Prc", 0); wunit(b, "ShdN", "#Prc", 0); wbool(b, "AntA", 0); contour(b, "TrnS");
    if (k == FX_INNERGLOW) wenum(b, "glwS", "IGSr", e->style == FXG_CENTER ? "SrcC" : "SrcE");
    break;
  case FX_BEVEL: {
    static const char *const st[FXB_N] = { "InrB", "OtrB", "Embs", "PlEb" };
    wobj(b, ids[k], 20); wbool(b, "present", 1); wbool(b, "showInDialog", 1);
    wbool(b, "enab", e->on);
    wenum(b, "hglM", "BlnM", md); wcolor(b, "hglC", e->color); wunit(b, "hglO", "#Prc", pct(e->opacity));
    wenum(b, "sdwM", "BlnM", blnm[e->blend2 < BM_N ? e->blend2 : 1]); wcolor(b, "sdwC", e->color2); wunit(b, "sdwO", "#Prc", pct(e->opacity2));
    wenum(b, "bvlT", "bvlT", "SfBL"); wenum(b, "bvlS", "BESl", st[e->style < FXB_N ? e->style : 0]);
    wbool(b, "uglg", global); wunit(b, "lagl", "#Ang", e->angle); wunit(b, "Lald", "#Ang", e->altitude);
    wunit(b, "srgR", "#Prc", e->depth); wunit(b, "blur", "#Pxl", e->size);
    wenum(b, "bvlD", "BESs", (e->flags & FXF_INVERT) ? "Out " : "In  "); contour(b, "TrnS"); wbool(b, "antialiasGloss", 0);
    wunit(b, "Sftn", "#Pxl", e->spread);
    break; }
  case FX_SATIN:
    wobj(b, ids[k], 12); wbool(b, "present", 1); wbool(b, "showInDialog", 1);
    wbool(b, "enab", e->on); wenum(b, "Md  ", "BlnM", md); wcolor(b, "Clr ", e->color);
    wbool(b, "AntA", 0); wbool(b, "Invr", (e->flags & FXF_INVERT) != 0); wunit(b, "Opct", "#Prc", pct(e->opacity));
    wunit(b, "lagl", "#Ang", e->angle); wunit(b, "Dstn", "#Pxl", e->distance); wunit(b, "blur", "#Pxl", e->size); contour(b, "MpgS");
    break;
  case FX_COLOR:
    wobj(b, ids[k], 6); wbool(b, "present", 1); wbool(b, "showInDialog", 1);
    wbool(b, "enab", e->on); wenum(b, "Md  ", "BlnM", md); wunit(b, "Opct", "#Prc", pct(e->opacity)); wcolor(b, "Clr ", e->color);
    break;
  case FX_GRADIENT: {
    static const char *const gt[5] = { "Lnr ", "Rdl ", "Angl", "Rflc", "Dmnd" };
    int i;
    wobj(b, ids[k], 12); wbool(b, "present", 1); wbool(b, "showInDialog", 1);
    wbool(b, "enab", e->on); wenum(b, "Md  ", "BlnM", md); wunit(b, "Opct", "#Prc", pct(e->opacity));
    wkey(b, "Grad", "Objc"); wobj(b, "Grdn", 5);
    wtext(b, "Nm  ", "Custom"); wenum(b, "GrdF", "GrdF", "CstS"); wdoub(b, "Intr", 4096);
    wkey(b, "Clrs", "VlLs"); w32(b, 2);
    for (i = 0; i < 2; i++) {
      buf_add(b, "Objc", 4); wobj(b, "Clrt", 4);
      wcolor(b, "Clr ", i ? e->color2 : e->color); wenum(b, "Type", "Clry", "UsrS"); wlong(b, "Lctn", i ? 4096 : 0); wlong(b, "Mdpn", 50);
    }
    wkey(b, "Trns", "VlLs"); w32(b, 2);
    for (i = 0; i < 2; i++) { buf_add(b, "Objc", 4); wobj(b, "TrnS", 3); wunit(b, "Opct", "#Prc", 100); wlong(b, "Lctn", i ? 4096 : 0); wlong(b, "Mdpn", 50); }
    wunit(b, "Angl", "#Ang", e->angle); wenum(b, "Type", "GrdT", gt[e->style < 5 ? e->style : 0]);
    wbool(b, "Rvrs", (e->flags & FXF_INVERT) != 0); wbool(b, "Dthr", 0); wunit(b, "Scl ", "#Prc", e->depth ? e->depth : 100);
    wkey(b, "Ofst", "Objc"); wobj(b, "Pnt ", 2); wunit(b, "Hrzn", "#Prc", e->dx); wunit(b, "Vrtc", "#Prc", e->dy);
    break; }
  case FX_STROKE: {
    static const char *const sp[3] = { "OutF", "InsF", "CtrF" };
    wobj(b, ids[k], 9); wbool(b, "present", 1); wbool(b, "showInDialog", 1);
    wbool(b, "enab", e->on); wenum(b, "Styl", "FStl", sp[e->style < 3 ? e->style : 0]); wenum(b, "PntT", "FrFl", "SClr");
    wenum(b, "Md  ", "BlnM", md); wunit(b, "Opct", "#Prc", pct(e->opacity)); wunit(b, "Sz  ", "#Pxl", e->size); wcolor(b, "Clr ", e->color);
    break; }
  }
}
void psdfx_write(Buf *b, const LayerFx *fx)
{
  Buf d;
  int k, n = 0;
  for (k = 0; k < FX_N; k++) if (fx->e[k].on) n++;
  if (!n) return;
  buf_init(&d);
  w32(&d, 0); w32(&d, 16);                               /* object effects version, descriptor version */
  wobj(&d, "null", (u32)(2 + n));
  wunit(&d, "Scl ", "#Prc", 100);
  wbool(&d, "masterFXSwitch", !fx->hidden);
  for (k = 0; k < FX_N; k++) if (fx->e[k].on) write_one(&d, k, &fx->e[k]);
  if (!d.oom) {
    buf_add(b, "8BIMlfx2", 8);
    w32(b, (u32)((d.len + 3) & ~(size_t)3));
    buf_add(b, d.data, d.len);
    while (d.len & 3) { buf_addc(b, 0); d.len++; }
  } else b->oom = 1;
  buf_free(&d);
}

/* ------------------------------------------------------------------ reading */
typedef struct { const u8 *p, *e; int bad; } Rd;
static u32 r32(Rd *r) { u32 v; if (r->e - r->p < 4) { r->bad = 1; r->p = r->e; return 0; } v = (u32)r->p[0] << 24 | (u32)r->p[1] << 16 | (u32)r->p[2] << 8 | r->p[3]; r->p += 4; return v; }
static double rdouble(Rd *r)
{
  u32 hi = r32(r), lo = r32(r);
  int e = (int)(hi >> 20 & 0x7FF);
  double m;
  if (r->bad || e == 0) return 0;
  if (e == 0x7FF) return 0;                              /* (infinities and NaN: nothing sensible) */
  m = 1 + (hi & 0xFFFFF) / 1048576.0 + lo / 4503599627370496.0;
  m = ldexp(m, e - 1023);
  return (hi & 0x80000000UL) ? -m : m;
}
/* an ID into out (cap bytes; longer ones cut) */
static void rid(Rd *r, char *out, size_t cap)
{
  u32 n = r32(r), k;
  if (r->bad) { out[0] = 0; return; }
  if (!n) n = 4;
  if (n > (u32)(r->e - r->p)) { r->bad = 1; out[0] = 0; return; }
  for (k = 0; k < n && k + 1 < cap; k++) out[k] = (char)r->p[k];
  out[k] = 0;
  r->p += n;
}
static void skip_value(Rd *r, const char *type, int depth);
static void skip_desc(Rd *r, int depth)
{
  u32 n, nm, i;
  char id[64], t[5];
  nm = r32(r);                                          /* the name, UTF-16 */
  if (nm > (u32)(r->e - r->p) / 2) { r->bad = 1; return; }
  r->p += nm * 2;
  rid(r, id, sizeof id);
  n = r32(r);
  if (n > 10000) { r->bad = 1; return; }
  for (i = 0; i < n && !r->bad; i++) {
    rid(r, id, sizeof id);
    if (r->e - r->p < 4) { r->bad = 1; return; }
    memcpy(t, r->p, 4); t[4] = 0; r->p += 4;
    skip_value(r, t, depth + 1);
  }
}
static void skip_value(Rd *r, const char *t, int depth)
{
  char id[64];
  if (depth > 12) { r->bad = 1; return; }
  if (!strcmp(t, "Objc") || !strcmp(t, "GlbO")) skip_desc(r, depth);
  else if (!strcmp(t, "VlLs")) {
    u32 n = r32(r), i;
    char tt[5];
    if (n > 100000) { r->bad = 1; return; }
    for (i = 0; i < n && !r->bad; i++) { if (r->e - r->p < 4) { r->bad = 1; return; } memcpy(tt, r->p, 4); tt[4] = 0; r->p += 4; skip_value(r, tt, depth + 1); }
  }
  else if (!strcmp(t, "doub")) { if (r->e - r->p < 8) r->bad = 1; else r->p += 8; }
  else if (!strcmp(t, "UntF")) { if (r->e - r->p < 12) r->bad = 1; else r->p += 12; }
  else if (!strcmp(t, "TEXT")) { u32 n = r32(r); if (n > (u32)(r->e - r->p) / 2) r->bad = 1; else r->p += n * 2; }
  else if (!strcmp(t, "enum")) { rid(r, id, sizeof id); rid(r, id, sizeof id); }
  else if (!strcmp(t, "long")) r32(r);
  else if (!strcmp(t, "comp")) { r32(r); r32(r); }
  else if (!strcmp(t, "bool")) { if (r->p >= r->e) r->bad = 1; else r->p++; }
  else if (!strcmp(t, "type") || !strcmp(t, "GlbC")) { u32 nm = r32(r); if (nm > (u32)(r->e - r->p) / 2) { r->bad = 1; return; } r->p += nm * 2; rid(r, id, sizeof id); }
  else if (!strcmp(t, "alis") || !strcmp(t, "tdta")) { u32 n = r32(r); if (n > (u32)(r->e - r->p)) r->bad = 1; else r->p += n; }
  else if (!strcmp(t, "UnFl")) { u32 n; r32(r); n = r32(r); if (n > (u32)(r->e - r->p) / 8) r->bad = 1; else r->p += n * 8; }
  else r->bad = 1;                                       /* (a reference or anything else unknown: stop) */
}
/* one effect object's items into e */
static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
static u32 read_color(Rd *r)
{
  /* an RGBC (or other) descriptor: its Rd, Grn and Bl */
  u32 nm, n, i, c = 0;
  char id[64], t[5];
  int rgb[3] = { 0, 0, 0 };
  nm = r32(r);
  if (nm > (u32)(r->e - r->p) / 2) { r->bad = 1; return 0; }
  r->p += nm * 2;
  rid(r, id, sizeof id);
  n = r32(r);
  if (n > 64) { r->bad = 1; return 0; }
  for (i = 0; i < n && !r->bad; i++) {
    char k[64];
    rid(r, k, sizeof k);
    if (r->e - r->p < 4) { r->bad = 1; return 0; }
    memcpy(t, r->p, 4); t[4] = 0; r->p += 4;
    if (!strcmp(t, "doub") && (!strcmp(k, "Rd  ") || !strcmp(k, "Grn ") || !strcmp(k, "Bl  "))) {
      double v = rdouble(r);
      rgb[k[0] == 'R' ? 0 : k[0] == 'G' ? 1 : 2] = (int)floor(clampd(v, 0, 255) + 0.5);
    } else skip_value(r, t, 1);
  }
  c = (u32)rgb[0] << 16 | (u32)rgb[1] << 8 | (u32)rgb[2];
  return c;
}
static void read_one(Rd *r, int k, FxOne *e, int depth)
{
  u32 nm, n, i;
  char id[64], t[5];
  int clr = 0;
  fx_defaults(e, k);
  e->on = 1;
  nm = r32(r);
  if (nm > (u32)(r->e - r->p) / 2) { r->bad = 1; return; }
  r->p += nm * 2;
  rid(r, id, sizeof id);
  n = r32(r);
  if (n > 256) { r->bad = 1; return; }
  for (i = 0; i < n && !r->bad; i++) {
    char key[64];
    rid(r, key, sizeof key);
    if (r->e - r->p < 4) { r->bad = 1; return; }
    memcpy(t, r->p, 4); t[4] = 0; r->p += 4;
    if (!strcmp(t, "bool")) {
      int v;
      if (r->p >= r->e) { r->bad = 1; return; }
      v = *r->p++ != 0;
      if (!strcmp(key, "enab")) e->on = (u8)v;
      else if (!strcmp(key, "uglg")) { if (v) e->flags |= FXF_GLOBAL; else e->flags &= ~FXF_GLOBAL; }
      else if (!strcmp(key, "layerConceals")) { if (v) e->flags |= FXF_KNOCKOUT; else e->flags &= ~FXF_KNOCKOUT; }
      else if (!strcmp(key, "Invr") || !strcmp(key, "Rvrs")) { if (v) e->flags |= FXF_INVERT; else e->flags &= ~FXF_INVERT; }
    } else if (!strcmp(t, "UntF")) {
      double v;
      if (r->e - r->p < 12) { r->bad = 1; return; }
      r->p += 4;
      v = rdouble(r);
      if (!strcmp(key, "Opct") || !strcmp(key, "hglO")) e->opacity = (u8)floor(clampd(v, 0, 100) * 255 / 100 + 0.5);
      else if (!strcmp(key, "sdwO")) e->opacity2 = (u8)floor(clampd(v, 0, 100) * 255 / 100 + 0.5);
      else if (!strcmp(key, "lagl") || !strcmp(key, "Angl")) e->angle = (i16)floor(clampd(v, -360, 360) + 0.5);
      else if (!strcmp(key, "Lald")) e->altitude = (u8)floor(clampd(v, 0, 90) + 0.5);
      else if (!strcmp(key, "Dstn")) e->distance = (u16)floor(clampd(v, 0, 30000) + 0.5);
      else if (!strcmp(key, "blur") || !strcmp(key, "Sz  ")) e->size = (u16)floor(clampd(v, 0, 1000) + 0.5);
      else if (!strcmp(key, "Ckmt")) e->dx = (i16)floor(clampd(v, 0, 1000) + 0.5);          /* (the spread in pixels, made a share below) */
      else if (!strcmp(key, "srgR")) e->depth = (u16)floor(clampd(v, 1, 1000) + 0.5);
      else if (!strcmp(key, "Sftn")) e->spread = (u8)floor(clampd(v, 0, 16) + 0.5);
      else if (!strcmp(key, "Scl ") && k == FX_GRADIENT) e->depth = (u16)floor(clampd(v, 10, 150) + 0.5);
    } else if (!strcmp(t, "enum")) {
      char type[64], v[64];
      rid(r, type, sizeof type); rid(r, v, sizeof v);
      if (!strcmp(key, "Md  ") || !strcmp(key, "hglM")) e->blend = (u8)blend_of(v);
      else if (!strcmp(key, "sdwM")) e->blend2 = (u8)blend_of(v);
      else if (!strcmp(key, "bvlS")) e->style = (u8)(!strcmp(v, "OtrB") ? FXB_OUTER : !strcmp(v, "Embs") ? FXB_EMBOSS : !strcmp(v, "PlEb") ? FXB_PILLOW : FXB_INNER);
      else if (!strcmp(key, "bvlD")) { if (!strcmp(v, "Out ")) e->flags |= FXF_INVERT; else e->flags &= ~FXF_INVERT; }
      else if (!strcmp(key, "glwS")) e->style = (u8)(!strcmp(v, "SrcC") ? FXG_CENTER : FXG_EDGE);
      else if (!strcmp(key, "Styl")) e->style = (u8)(!strcmp(v, "InsF") ? FXS_INSIDE : !strcmp(v, "CtrF") ? FXS_CENTER : FXS_OUTSIDE);
      else if (!strcmp(key, "Type") && k == FX_GRADIENT)
        e->style = (u8)(!strcmp(v, "Rdl ") ? GR_RADIAL : !strcmp(v, "Angl") ? GR_ANGLE : !strcmp(v, "Rflc") ? GR_REFLECTED : !strcmp(v, "Dmnd") ? GR_DIAMOND : GR_LINEAR);
    } else if (!strcmp(t, "Objc") && (!strcmp(key, "Clr ") || !strcmp(key, "hglC") || !strcmp(key, "sdwC"))) {
      u32 c = read_color(r);
      if (!strcmp(key, "sdwC")) e->color2 = c; else e->color = c;
      clr = 1;
    } else if (!strcmp(t, "Objc") && !strcmp(key, "Grad") && k == FX_GRADIENT) {
      /* the gradient's first and last colour stops */
      u32 gn, gi, gnm;
      char gid[64], gk[64], gt[5];
      gnm = r32(r);
      if (gnm > (u32)(r->e - r->p) / 2) { r->bad = 1; return; }
      r->p += gnm * 2;
      rid(r, gid, sizeof gid);
      gn = r32(r);
      if (gn > 64) { r->bad = 1; return; }
      for (gi = 0; gi < gn && !r->bad; gi++) {
        rid(r, gk, sizeof gk);
        if (r->e - r->p < 4) { r->bad = 1; return; }
        memcpy(gt, r->p, 4); gt[4] = 0; r->p += 4;
        if (!strcmp(gt, "VlLs") && !strcmp(gk, "Clrs")) {
          u32 cn = r32(r), ci;
          if (cn > 1000) { r->bad = 1; return; }
          for (ci = 0; ci < cn && !r->bad; ci++) {
            u32 sn, si, snm;
            char sid[64], sk[64], st[5];
            if (r->e - r->p < 4 || memcmp(r->p, "Objc", 4)) { r->bad = 1; return; }
            r->p += 4;
            snm = r32(r);
            if (snm > (u32)(r->e - r->p) / 2) { r->bad = 1; return; }
            r->p += snm * 2;
            rid(r, sid, sizeof sid);
            sn = r32(r);
            if (sn > 64) { r->bad = 1; return; }
            for (si = 0; si < sn && !r->bad; si++) {
              rid(r, sk, sizeof sk);
              if (r->e - r->p < 4) { r->bad = 1; return; }
              memcpy(st, r->p, 4); st[4] = 0; r->p += 4;
              if (!strcmp(st, "Objc") && !strcmp(sk, "Clr ")) { u32 c = read_color(r); if (ci == 0) e->color = c; if (ci == cn - 1) e->color2 = c; }
              else skip_value(r, st, depth + 3);
            }
          }
        } else skip_value(r, gt, depth + 2);
      }
    } else if (!strcmp(t, "Objc") && !strcmp(key, "Ofst") && k == FX_GRADIENT) {
      u32 on, oi, onm;
      char oid[64], ok[64], ot[5];
      onm = r32(r);
      if (onm > (u32)(r->e - r->p) / 2) { r->bad = 1; return; }
      r->p += onm * 2;
      rid(r, oid, sizeof oid);
      on = r32(r);
      if (on > 16) { r->bad = 1; return; }
      for (oi = 0; oi < on && !r->bad; oi++) {
        rid(r, ok, sizeof ok);
        if (r->e - r->p < 4) { r->bad = 1; return; }
        memcpy(ot, r->p, 4); ot[4] = 0; r->p += 4;
        if (!strcmp(ot, "UntF")) {
          double v;
          if (r->e - r->p < 12) { r->bad = 1; return; }
          r->p += 4; v = rdouble(r);
          if (!strcmp(ok, "Hrzn")) e->dx = (i16)floor(clampd(v, -1000, 1000) + 0.5);
          else if (!strcmp(ok, "Vrtc")) e->dy = (i16)floor(clampd(v, -1000, 1000) + 0.5);
        } else skip_value(r, ot, depth + 2);
      }
    } else skip_value(r, t, depth + 1);
  }
  (void)clr;
  /* the spread and choke as a share of the size, as Easel keeps them (Gradient Overlay's dx is its offset) */
  if (k == FX_DROPSHADOW || k == FX_INNERSHADOW || k == FX_OUTERGLOW || k == FX_INNERGLOW) {
    int px = e->dx;
    e->spread = (u8)(e->size ? (px * 100 + e->size / 2) / e->size > 100 ? 100 : (px * 100 + e->size / 2) / e->size : 0);
    e->dx = 0;
  }
}
int psdfx_read(const u8 *d, u32 len, LayerFx *fx)
{
  static const char *const ids[FX_N] = { "DrSh", "IrSh", "OrGl", "IrGl", "ebbl", "ChFX", "SoFi", "GrFl", "FrFX" };
  Rd r;
  u32 n, nm, i;
  char id[64], t[5];
  int any = 0;
  r.p = d; r.e = d + len; r.bad = 0;
  fx_defaults_all(fx);
  if (r32(&r) != 0 || r32(&r) != 16) return 0;           /* the versions */
  nm = r32(&r);
  if (nm > (u32)(r.e - r.p) / 2) return 0;
  r.p += nm * 2;
  rid(&r, id, sizeof id);
  n = r32(&r);
  if (r.bad || n > 256) return 0;
  for (i = 0; i < n && !r.bad; i++) {
    char key[64];
    int k, which = -1;
    rid(&r, key, sizeof key);
    if (r.e - r.p < 4) break;
    memcpy(t, r.p, 4); t[4] = 0; r.p += 4;
    for (k = 0; k < FX_N; k++) if (!strcmp(key, ids[k])) which = k;
    if (which >= 0 && !strcmp(t, "Objc")) { read_one(&r, which, &fx->e[which], 1); if (!r.bad && fx->e[which].on) any = 1; }
    else if (!strcmp(t, "bool") && !strcmp(key, "masterFXSwitch")) { if (r.p < r.e) fx->hidden = *r.p++ == 0; }
    else skip_value(&r, t, 1);
  }
  if (r.bad) {
    /* what was read before the damage is kept, if any of it */
    return any;
  }
  return any;
}
