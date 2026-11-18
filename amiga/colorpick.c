/*
 * colorpick.c - Easel 0.5's colour picker (see colorpick.h): the square and hue strip drawn as
 * the Color panel draws them, the new and old swatches, and the HSB, RGB, Lab, CMYK (round 3) and
 * hex fields kept in step with each other; Only Web Colors keeps the colour to the 216 web-safe ones.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "colorpick.h"
#include "../core/colr3.h"            /* r3: CMYK fields, Only Web Colors */

#define SQ 200                        /* the square's side */
#define STRIP 16
#define SW 46                         /* the swatches' width */
/* the fields' commands (run_modal hands them to each_id) */
enum { CP_FIELD = 0x5800, CP_PICKER = 0x5820 };

/* ------------------------------------------------------------------ Lab */
static double lin(double c) { c /= 255.0; return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
static int gam(double c) { double v = c <= 0.0031308 ? 12.92 * c : 1.055 * pow(c, 1 / 2.4) - 0.055; v = v * 255 + 0.5; return v < 0 ? 0 : v > 255 ? 255 : (int)v; }
static double fxyz(double t) { return t > 216.0 / 24389 ? pow(t, 1.0 / 3) : (24389.0 / 27 * t + 16) / 116; }
static double fixyz(double t) { return t * t * t > 216.0 / 24389 ? t * t * t : (116 * t - 16) / (24389.0 / 27); }
void rgb_to_lab(int r, int g, int b, double *L, double *A, double *B)
{
  double R = lin(r), G = lin(g), Bl = lin(b);
  double X = (0.4124 * R + 0.3576 * G + 0.1805 * Bl) / 0.95047, Y = 0.2126 * R + 0.7152 * G + 0.0722 * Bl, Z = (0.0193 * R + 0.1192 * G + 0.9505 * Bl) / 1.08883;
  double fx = fxyz(X), fy = fxyz(Y), fz = fxyz(Z);
  *L = 116 * fy - 16; *A = 500 * (fx - fy); *B = 200 * (fy - fz);
}
void lab_to_rgb(double L, double A, double B, int *r, int *g, int *b)
{
  double fy = (L + 16) / 116, fx = fy + A / 500, fz = fy - B / 200;
  double X = fixyz(fx) * 0.95047, Y = fixyz(fy), Z = fixyz(fz) * 1.08883;
  *r = gam(3.2406 * X - 1.5372 * Y - 0.4986 * Z);
  *g = gam(-0.9689 * X + 1.8758 * Y + 0.0415 * Z);
  *b = gam(0.0557 * X - 0.2040 * Y + 1.0570 * Z);
}

/* ------------------------------------------------------------------ the picker */
struct KData { struct MUI_EventHandlerNode ehn; int shown, drag; ULONG rgb, old; double hue; UBYTE *buf; };
static struct MUI_CustomClass *kcls;
static LONG kx_strip(Object *obj) { return _mleft(obj) + SQ + 8; }
static LONG kx_sw(Object *obj) { return kx_strip(obj) + STRIP + 10; }
static void k_draw(struct KData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  struct Screen *scr = _screen(obj);
  LONG l = _mleft(obj), t = _mtop(obj), x, y, hy;
  int r = (int)(d->rgb >> 16) & 255, g = (int)(d->rgb >> 8) & 255, b = (int)d->rgb & 255, hr, hg, hb;
  double h, s, v;
  theme_fill(rp, l, t, _mright(obj), _mbottom(obj), tpen(T_PANEL));
  rgb_to_hsv(r, g, b, &h, &s, &v);
  if (s > 0.001 && v > 0.001) d->hue = h;
  if (!d->buf && !(d->buf = (UBYTE *)AllocVec(SQ * SQ * 3, MEMF_ANY))) return;
  hsv_to_rgb(d->hue, 1, 1, &hr, &hg, &hb);
  for (y = 0; y < SQ; y++) {
    int vv = (int)(255 - y * 255 / (SQ - 1));
    UBYTE *o = d->buf + y * SQ * 3;
    for (x = 0; x < SQ; x++, o += 3) {
      int ss = (int)(x * 255 / (SQ - 1));
      o[0] = (UBYTE)(((255 * (255 - ss) + hr * ss) / 255) * vv / 255);
      o[1] = (UBYTE)(((255 * (255 - ss) + hg * ss) / 255) * vv / 255);
      o[2] = (UBYTE)(((255 * (255 - ss) + hb * ss) / 255) * vv / 255);
    }
  }
  draw_rgb(rp, scr, d->buf, SQ * 3, l, t, SQ, SQ);
  for (y = 0; y < SQ; y++) {
    int pr, pg, pb, k;
    hsv_to_rgb(360.0 * y / SQ, 1, 1, &pr, &pg, &pb);
    for (k = 0; k < STRIP; k++) { UBYTE *o = d->buf + (y * STRIP + k) * 3; o[0] = (UBYTE)pr; o[1] = (UBYTE)pg; o[2] = (UBYTE)pb; }
  }
  draw_rgb(rp, scr, d->buf, STRIP * 3, kx_strip(obj), t, STRIP, SQ);
  theme_rect(rp, l - 1, t - 1, l + SQ, t + SQ, tpen(T_BORDER2));
  /* the colour's ring, the hue's arrows */
  x = l + (LONG)(s * (SQ - 1)); y = t + (LONG)((1 - v) * (SQ - 1));
  SetAPen(rp, v > 0.6 && s < 0.5 ? tpen(T_TEXT) : tpen(T_WHITE));
  DrawEllipse(rp, x, y, 5, 5);
  SetAPen(rp, v > 0.6 && s < 0.5 ? tpen(T_WHITE) : tpen(T_TEXT));
  DrawEllipse(rp, x, y, 6, 6);
  hy = t + (LONG)(d->hue / 360.0 * SQ);
  SetAPen(rp, tpen(T_TEXT));
  x = kx_strip(obj);
  Move(rp, x - 4, hy - 4); Draw(rp, x - 1, hy); Draw(rp, x - 4, hy + 4);
  Move(rp, x + STRIP + 3, hy - 4); Draw(rp, x + STRIP, hy); Draw(rp, x + STRIP + 3, hy + 4);
  /* new above old */
  x = kx_sw(obj);
  theme_text(rp, theme_small(), x, t, tr("new"), tpen(T_DIM));
  y = t + theme_small()->tf_YSize + 2;
  draw_rgb_box(rp, scr, x, y, x + SW - 1, y + 34, d->rgb);
  draw_rgb_box(rp, scr, x, y + 35, x + SW - 1, y + 69, d->old);
  theme_rect(rp, x - 1, y - 1, x + SW, y + 70, tpen(T_BORDER2));
  theme_text(rp, theme_small(), x, y + 72, tr("old"), tpen(T_DIM));
}
static void k_set(struct KData *d, Object *obj, ULONG c)
{
  d->rgb = c & 0xFFFFFF;
  if (d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
  SetAttrs(obj, MUIA_CPick_Color, d->rgb, TAG_DONE);   /* (OM_SET: the notification goes) */
}
static ULONG kdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct KData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->rgb = GetTagData(MUIA_CPick_Color, 0, ((struct opSet *)msg)->ops_AttrList) & 0xFFFFFF;
    d->old = GetTagData(MUIA_CPick_Old, d->rgb, ((struct opSet *)msg)->ops_AttrList) & 0xFFFFFF;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
    return (ULONG)obj;
  case OM_DISPOSE:
    d = INST_DATA(cl, obj);
    if (d->buf) FreeVec(d->buf);
    break;
  case OM_SET: {
    struct TagItem *t;
    d = INST_DATA(cl, obj);
    if ((t = FindTagItem(MUIA_CPick_Color, ((struct opSet *)msg)->ops_AttrList)) && (t->ti_Data & 0xFFFFFF) != d->rgb) {
      d->rgb = t->ti_Data & 0xFFFFFF;
      if (d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
    }
    if ((t = FindTagItem(MUIA_CPick_Old, ((struct opSet *)msg)->ops_AttrList))) d->old = t->ti_Data & 0xFFFFFF;
    break;
  }
  case OM_GET: {
    struct opGet *g = (struct opGet *)msg;
    if (g->opg_AttrID == MUIA_CPick_Color) { *g->opg_Storage = ((struct KData *)INST_DATA(cl, obj))->rgb; return TRUE; }
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    LONG w = SQ + 8 + STRIP + 10 + SW + 2, h = SQ + 1;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += w; m->MinMaxInfo->DefWidth += w; m->MinMaxInfo->MaxWidth += w;
    m->MinMaxInfo->MinHeight += h; m->MinMaxInfo->DefHeight += h; m->MinMaxInfo->MaxHeight += h;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct KData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup: theme_cleanup(obj); break;
  case MUIM_Show: ((struct KData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct KData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) k_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    LONG mx, my, l, t;
    d = INST_DATA(cl, obj);
    if (!im || !d->shown) return 0;
    mx = im->MouseX; my = im->MouseY; l = _mleft(obj); t = _mtop(obj);
    if (im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTUP && d->drag) { d->drag = 0; return MUI_EventHandlerRC_Eat; }
    if (im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTDOWN) {
      if (mx >= l && mx < l + SQ && my >= t && my < t + SQ) d->drag = 1;
      else if (mx >= kx_strip(obj) - 4 && mx < kx_strip(obj) + STRIP + 4 && my >= t && my < t + SQ) d->drag = 2;
      else if (mx >= kx_sw(obj) && mx < kx_sw(obj) + SW && my >= t + theme_small()->tf_YSize + 37 && my < t + theme_small()->tf_YSize + 72) { k_set(d, obj, d->old); return MUI_EventHandlerRC_Eat; }
      else return 0;
    } else if (im->Class != IDCMP_MOUSEMOVE || !d->drag) return 0;
    {
      int r, g, b;
      double h, s, v;
      rgb_to_hsv((int)(d->rgb >> 16) & 255, (int)(d->rgb >> 8) & 255, (int)d->rgb & 255, &h, &s, &v);
      if (d->drag == 1) {
        s = (double)(mx - l) / (SQ - 1); v = 1 - (double)(my - t) / (SQ - 1);
        if (s < 0) s = 0;
        if (s > 1) s = 1;
        if (v < 0) v = 0;
        if (v > 1) v = 1;
      } else {
        double hh = 360.0 * (my - t) / SQ;
        if (hh < 0) hh = 0;
        if (hh > 359.9) hh = 359.9;
        d->hue = hh;
        if (s < 0.01) s = 1;
        if (v < 0.01) v = 1;
      }
      hsv_to_rgb(d->hue, s, v, &r, &g, &b);
      k_set(d, obj, ((ULONG)r << 16) | ((ULONG)g << 8) | (ULONG)b);
    }
    return MUI_EventHandlerRC_Eat;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}

/* ------------------------------------------------------------------ the dialog */
#define NF 14
#define FHEX 13                       /* (the hex field: the last) */
static Object *picker, *field[NF], *webonly;
static const char *const flabel[NF] = { "H", "S", "B", "R", "G", "B", "L", "a", "b", "C", "M", "Y", "K", "#" };
static const char *const fsuffix[NF] = { "\xb0", "%", "%", "", "", "", "", "", "", "%", "%", "%", "%", "" };
static int updating;
static ULONG xget(Object *o, ULONG a) { ULONG v = 0; get(o, a, &v); return v; }
/* every field shown for colour c (except the one being typed in, skip) */
static void fill_fields(ULONG c, int skip)
{
  char buf[16];
  int r = (int)(c >> 16) & 255, g = (int)(c >> 8) & 255, b = (int)c & 255, i;
  double h, s, v, L, A, B, vals[13];
  int cc, cm, cy, ck;
  rgb_to_hsv(r, g, b, &h, &s, &v);
  rgb_to_lab(r, g, b, &L, &A, &B);
  rgb_to_cmyk100(c, &cc, &cm, &cy, &ck);
  vals[0] = h; vals[1] = s * 100; vals[2] = v * 100; vals[3] = r; vals[4] = g; vals[5] = b; vals[6] = L; vals[7] = A; vals[8] = B;
  vals[9] = cc; vals[10] = cm; vals[11] = cy; vals[12] = ck;
  updating = 1;
  for (i = 0; i < NF; i++) {
    if (i == skip) continue;
    if (i == FHEX) sprintf(buf, "%06lX", (unsigned long)(c & 0xFFFFFF));
    else sprintf(buf, "%ld", (long)floor(vals[i] + 0.5));
    set(field[i], MUIA_String_Contents, (ULONG)buf);
  }
  updating = 0;
}
static int clampi(double v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : (int)floor(v + 0.5); }
static double fval(int i) { return atof((const char *)xget(field[i], MUIA_String_Contents)); }
static void each_id(ULONG id)
{
  ULONG c = xget(picker, MUIA_CPick_Color);
  int r, g, b;
  if (id == CP_PICKER || id == CP_FIELD + NF) {
    /* (r3: Only Web Colors: the colour moved to the nearest web-safe one) */
    if (updating) return;
    if (webonly && xget(webonly, MUIA_Selected) && !is_web_safe(c)) { c = web_safe(c); updating = 1; set(picker, MUIA_CPick_Color, c); updating = 0; }
    fill_fields(c, -1);
    return;
  }
  if (id < CP_FIELD || id >= CP_FIELD + NF) return;
  switch ((int)(id - CP_FIELD)) {
  case 0: case 1: case 2:
    hsv_to_rgb(fmod(fval(0) + 3600, 360), clampi(fval(1), 0, 100) / 100.0, clampi(fval(2), 0, 100) / 100.0, &r, &g, &b);
    break;
  case 3: case 4: case 5:
    r = clampi(fval(3), 0, 255); g = clampi(fval(4), 0, 255); b = clampi(fval(5), 0, 255);
    break;
  case 6: case 7: case 8:
    lab_to_rgb(fval(6), fval(7), fval(8), &r, &g, &b);
    break;
  case 9: case 10: case 11: case 12:
    c = cmyk100_to_rgb(clampi(fval(9), 0, 100), clampi(fval(10), 0, 100), clampi(fval(11), 0, 100), clampi(fval(12), 0, 100));
    r = (int)(c >> 16) & 255; g = (int)(c >> 8) & 255; b = (int)c & 255;
    break;
  default: {
    const char *s = (const char *)xget(field[FHEX], MUIA_String_Contents);
    c = strtoul(s[0] == '#' ? s + 1 : s, NULL, 16) & 0xFFFFFF;
    r = (int)(c >> 16) & 255; g = (int)(c >> 8) & 255; b = (int)c & 255;
  }
  }
  c = ((ULONG)r << 16) | ((ULONG)g << 8) | (ULONG)b;
  if (webonly && xget(webonly, MUIA_Selected)) c = web_safe(c);
  updating = 1;
  set(picker, MUIA_CPick_Color, c);
  updating = 0;
  fill_fields(c, (webonly && xget(webonly, MUIA_Selected)) || (int)(id - CP_FIELD) == FHEX || ((int)(id - CP_FIELD) >= 3 && (int)(id - CP_FIELD) <= 5) ? -1 : (int)(id - CP_FIELD));
}
LONG colorpick_run(const char *title, ULONG start)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *cols;
  LONG res = -1;
  int i;
  if (!kcls && !(kcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct KData), (APTR)kdispatch))) return pick_color(title, start);
  picker = NewObject(kcls->mcc_Class, NULL, MUIA_FillArea, FALSE, MUIA_CPick_Color, start & 0xFFFFFF, MUIA_CPick_Old, start & 0xFFFFFF, TAG_DONE);
  cols = ColGroup(3), End;
  if (!picker || !cols) { if (picker) MUI_DisposeObject(picker); if (cols) MUI_DisposeObject(cols); return -1; }
  for (i = 0; i < NF; i++) {
    field[i] = StringObject, StringFrame, MUIA_String_MaxLen, 8, MUIA_CycleChain, 1,
      MUIA_String_Accept, (ULONG)(i == FHEX ? "0123456789abcdefABCDEF#" : "0123456789-."), MUIA_FixWidthTxt, (ULONG)"MMMMMM", End;
    DoMethod(cols, OM_ADDMEMBER, (ULONG)QLabel((char *)flabel[i], MUIO_Label_SingleFrame));
    DoMethod(cols, OM_ADDMEMBER, (ULONG)field[i]);
    DoMethod(cols, OM_ADDMEMBER, (ULONG)QLabel((char *)fsuffix[i], MUIO_Label_SingleFrame));
  }
  webonly = ImageObject, ImageButtonFrame, MUIA_InputMode, MUIV_InputMode_Toggle, MUIA_Image_Spec, MUII_CheckMark,
    MUIA_Background, MUII_ButtonBack, MUIA_ShowSelState, FALSE, MUIA_Selected, FALSE, MUIA_CycleChain, 1, End;
  w = dialog_window(title, HGroup, Child, picker, Child, VGroup, Child, cols,
                    Child, HGroup, Child, webonly, Child, QLabel(tr("Only Web Colors"), 0), Child, HSpace(0), End, Child, VSpace(0), End, End, NULL);
  if (!w) { webonly = NULL; return -1; }
  DoMethod(webonly, MUIM_Notify, MUIA_Selected, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, (ULONG)(CP_FIELD + NF));
  for (i = 0; i < NF; i++)
    DoMethod(field[i], MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, (ULONG)(CP_FIELD + i));
  DoMethod(picker, MUIM_Notify, MUIA_CPick_Color, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, (ULONG)CP_PICKER);
  fill_fields(start & 0xFFFFFF, -1);
  if (run_modal(w, ends, each_id) == ID_DLGOK) {
    /* (a number typed but not entered counts too) */
    res = (LONG)(xget(picker, MUIA_CPick_Color) & 0xFFFFFF);
  }
  MUI_DisposeObject(w);
  picker = NULL; webonly = NULL;
  return res;
}
void colorpick_free(void) { if (kcls) { MUI_DeleteCustomClass(kcls); kcls = NULL; } }
