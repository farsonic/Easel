/*
 * panels2.c - the Navigator and Info panels (0.6), tabs beside Layers.
 *
 *   Navigator  the whole picture small, with a red box round what the canvas shows; click or drag
 *              in it to look somewhere else
 *   Info       the pointer's place and the colour under it (RGB, HSB, hex), the picture's size and
 *              mode, the selection's size, and a histogram of the picture's brightness
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

static int fh(void) { return theme_font()->tf_YSize; }
static struct MUI_CustomClass *navcls, *infcls;

/* a picture's composite at reduced size: tw x th RGB (over the checkerboard), malloc'd */
static UBYTE *small_composite(Doc *doc, LONG tw, LONG th)
{
  UBYTE *out = (UBYTE *)malloc((size_t)tw * th * 3);
  u8 *row = (u8 *)malloc((size_t)doc->w * 4);
  LONG x, y;
  if (!out || !row) { free(out); free(row); return NULL; }
  for (y = 0; y < th; y++) {
    i32 iy = (i32)((long)y * doc->h / th);
    doc_composite(doc, box_make(0, iy, doc->w, iy + 1), row, (long)doc->w * 4);
    for (x = 0; x < tw; x++) {
      const u8 *p = row + (long)((long)x * doc->w / tw) * 4;
      int c = (((x >> 2) + (y >> 2)) & 1) ? 0xCC : 0xFF, a = p[3], k;
      for (k = 0; k < 3; k++) out[(y * tw + x) * 3 + k] = (UBYTE)((p[k] * a + c * (255 - a) + 127) / 255);
    }
  }
  free(row);
  return out;
}

/* ================================================================== the Navigator */
struct NData { struct MUI_EventHandlerNode ehn; int shown, drag; UBYTE *thumb; LONG tw, th; Doc *tdoc; LONG x0, y0; };
/* the thumbnail's size and place to fit the panel */
static void nav_fit(struct NData *d, Object *obj, Doc *doc)
{
  LONG aw = _mwidth(obj) - 12, ah = _mheight(obj) - fh() - 16;
  if (aw < 8) aw = 8;
  if (ah < 8) ah = 8;
  if ((long)doc->w * ah > (long)doc->h * aw) { d->tw = aw; d->th = (LONG)((long)doc->h * aw / doc->w); }
  else { d->th = ah; d->tw = (LONG)((long)doc->w * ah / doc->h); }
  if (d->tw < 1) d->tw = 1;
  if (d->th < 1) d->th = 1;
  d->x0 = _mleft(obj) + (_mwidth(obj) - d->tw) / 2; d->y0 = _mtop(obj) + 6;
}
static void nav_draw(struct NData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  DocView *v = cur_view();
  Doc *doc = v ? v->doc : NULL;
  char buf[32];
  theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_PANEL));
  if (!doc) return;
  nav_fit(d, obj, doc);
  if (!d->thumb) d->thumb = small_composite(doc, d->tw, d->th);
  if (!d->thumb) return;
  draw_rgb(rp, _screen(obj), d->thumb, d->tw * 3, d->x0, d->y0, d->tw, d->th);
  theme_rect(rp, d->x0 - 1, d->y0 - 1, d->x0 + d->tw, d->y0 + d->th, tpen(T_BORDER2));
  /* what the canvas shows */
  if (canvas && v->zoom > 0) {
    double s = (double)d->tw / doc->w;
    LONG rx0 = d->x0 + (LONG)(v->sx / v->zoom * s), ry0 = d->y0 + (LONG)(v->sy / v->zoom * s);
    LONG rx1 = rx0 + (LONG)(_mwidth(canvas) / v->zoom * s), ry1 = ry0 + (LONG)(_mheight(canvas) / v->zoom * s);
    if (rx0 < d->x0) rx0 = d->x0;
    if (ry0 < d->y0) ry0 = d->y0;
    if (rx1 > d->x0 + d->tw - 1) rx1 = d->x0 + d->tw - 1;
    if (ry1 > d->y0 + d->th - 1) ry1 = d->y0 + d->th - 1;
    theme_rect(rp, rx0, ry0, rx1, ry1, tpen(T_RED));
    if (rx1 - rx0 > 2 && ry1 - ry0 > 2) theme_rect(rp, rx0 + 1, ry0 + 1, rx1 - 1, ry1 - 1, tpen(T_RED));
  }
  sprintf(buf, "%ld%%", (long)(v->zoom * 100 + 0.5));
  theme_text(rp, theme_font(), _mleft(obj) + 8, _mbottom(obj) - fh() - 4, buf, tpen(T_TEXT));
}
/* the canvas shown centred on the thumbnail's point mx, my */
static void nav_go(struct NData *d, LONG mx, LONG my)
{
  DocView *v = cur_view();
  Doc *doc = v ? v->doc : NULL;
  double ix, iy;
  if (!doc || d->tw < 1) return;
  ix = (double)(mx - d->x0) * doc->w / d->tw; iy = (double)(my - d->y0) * doc->h / d->th;
  DoMethod(canvas, MUIM_Canvas_ScrollTo, (LONG)ix, (LONG)iy);
}
static ULONG navdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct NData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    memset(INST_DATA(cl, obj), 0, sizeof(struct NData));
    return (ULONG)obj;
  case OM_DISPOSE: d = INST_DATA(cl, obj); free(d->thumb); break;
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 100; m->MinMaxInfo->DefWidth += 200; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += 60; m->MinMaxInfo->DefHeight += 160; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    d = INST_DATA(cl, obj);
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
    DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn);
    return TRUE;
  case MUIM_Cleanup:
    d = INST_DATA(cl, obj);
    DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn);
    theme_cleanup(obj);
    break;
  case MUIM_Show: ((struct NData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: d = INST_DATA(cl, obj); d->shown = 0; free(d->thumb); d->thumb = NULL; break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) nav_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Navigator_Refresh: {
    struct MUIP_Navigator_Refresh *m = (struct MUIP_Navigator_Refresh *)msg;
    d = INST_DATA(cl, obj);
    if (m->picture || d->tdoc != cur_doc()) { free(d->thumb); d->thumb = NULL; d->tdoc = cur_doc(); }
    if (d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0; }
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    LONG mx, my;
    d = INST_DATA(cl, obj);
    if (!im || !d->shown) return 0;
    mx = im->MouseX; my = im->MouseY;
    if (im->Class == IDCMP_MOUSEBUTTONS) {
      if (im->Code == SELECTUP && d->drag) { d->drag = 0; return MUI_EventHandlerRC_Eat; }
      if (im->Code != SELECTDOWN || mx < d->x0 || my < d->y0 || mx >= d->x0 + d->tw || my >= d->y0 + d->th) return 0;
      d->drag = 1;
      nav_go(d, mx, my);
      MUI_Redraw(obj, MADF_DRAWOBJECT);
      return MUI_EventHandlerRC_Eat;
    }
    if (im->Class == IDCMP_MOUSEMOVE && d->drag) {
      if (mx < d->x0) mx = d->x0;
      if (my < d->y0) my = d->y0;
      if (mx >= d->x0 + d->tw) mx = d->x0 + d->tw - 1;
      if (my >= d->y0 + d->th) my = d->y0 + d->th - 1;
      nav_go(d, mx, my);
      MUI_Redraw(obj, MADF_DRAWOBJECT);
      return MUI_EventHandlerRC_Eat;
    }
    return 0; }
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *navigator_class(void)
{
  if (!navcls) navcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct NData), (APTR)navdispatch);
  return navcls;
}

/* ================================================================== the Info panel */
struct IData { int shown; ULONG hist[64]; ULONG hmax; Doc *hdoc; int hok; };
static void rgb_hsb(int r, int g, int b, int *h, int *s, int *v)
{
  int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b), dd = mx - mn;
  double hh = 0;
  *v = mx * 100 / 255; *s = mx ? dd * 100 / mx : 0;
  if (dd) {
    if (mx == r) hh = fmod((double)(g - b) / dd, 6);
    else if (mx == g) hh = (double)(b - r) / dd + 2;
    else hh = (double)(r - g) / dd + 4;
    hh *= 60;
    if (hh < 0) hh += 360;
  }
  *h = (int)(hh + 0.5) % 360;
}
/* the brightness histogram of the picture, from up to 96 of its rows */
static void info_hist(struct IData *d, Doc *doc)
{
  u8 *row = (u8 *)malloc((size_t)doc->w * 4);
  i32 y, x, step = doc->h > 96 ? doc->h / 96 : 1;
  int k;
  memset(d->hist, 0, sizeof d->hist); d->hmax = 1; d->hok = 0;
  if (!row) return;
  for (y = 0; y < doc->h; y += step) {
    doc_composite(doc, box_make(0, y, doc->w, y + 1), row, (long)doc->w * 4);
    for (x = 0; x < doc->w; x++) {
      const u8 *p = row + x * 4;
      if (p[3] < 8) continue;
      d->hist[(p[0] * 77 + p[1] * 150 + p[2] * 29) >> 10]++;
    }
  }
  for (k = 0; k < 64; k++) if (d->hist[k] > d->hmax) d->hmax = d->hist[k];
  free(row);
  d->hok = 1; d->hdoc = doc;
}
static void info_draw(struct IData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  Doc *doc = cur_doc();
  char buf[120];
  LONG x = _mleft(obj) + 8, y = _mtop(obj) + 6, lh = fh() + 3, hx0, hy1, hw, k;
  theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_PANEL));
  if (!doc) return;
  if (canvas_pt.in) {
    int h, s, v;
    sprintf(buf, "X %ld   Y %ld", (long)canvas_pt.x, (long)canvas_pt.y);
    theme_text(rp, theme_font(), x, y, buf, tpen(T_TEXT)); y += lh;
    if (canvas_pt.has_rgb) {
      rgb_hsb(canvas_pt.rgba[0], canvas_pt.rgba[1], canvas_pt.rgba[2], &h, &s, &v);
      sprintf(buf, "R %d  G %d  B %d  A %d", canvas_pt.rgba[0], canvas_pt.rgba[1], canvas_pt.rgba[2], canvas_pt.rgba[3]);
      theme_text(rp, theme_font(), x, y, buf, tpen(T_TEXT)); y += lh;
      sprintf(buf, "H %d\260  S %d%%  B %d%%   #%02X%02X%02X", h, s, v, canvas_pt.rgba[0], canvas_pt.rgba[1], canvas_pt.rgba[2]);
      theme_text(rp, theme_font(), x, y, buf, tpen(T_TEXT)); y += lh;
    } else y += 2 * lh;
  } else { theme_text(rp, theme_font(), x, y, tr("Move the pointer over the picture"), tpen(T_DIM)); y += 3 * lh; }
  sprintf(buf, "%s: %ld x %ld, %s", tr("Picture"), (long)doc->w, (long)doc->h,
          doc->mode == MODE_INDEXED ? tr("Indexed") : doc->mode == MODE_GRAY ? tr("Grayscale") : doc->mode == MODE_BITMAP ? tr("Bitmap") : "RGB");
  theme_text(rp, theme_font(), x, y, buf, tpen(T_DIM)); y += lh;
  if (doc->sel) {
    Box b;
    if (sel_bounds(doc, &b)) sprintf(buf, "%s: %ld x %ld at %ld, %ld", tr("Selection"), (long)(b.x1 - b.x0), (long)(b.y1 - b.y0), (long)b.x0, (long)b.y0);
    else strcpy(buf, tr("Selection: empty"));
  } else strcpy(buf, tr("No selection"));
  theme_text(rp, theme_font(), x, y, buf, tpen(T_DIM)); y += lh + 4;
  /* the histogram */
  if (!d->hok || d->hdoc != doc) info_hist(d, doc);
  hx0 = x; hw = _mwidth(obj) - 16; hy1 = _mbottom(obj) - 6;
  if (hy1 - y < 20 || hw < 64) return;
  theme_fill(rp, hx0, y, hx0 + hw - 1, hy1, tpen(T_CANVAS));
  SetAPen(rp, tpen(T_TEXT));
  for (k = 0; k < hw; k++) {
    int bin = (int)(k * 64 / hw);
    LONG hgt = (LONG)((double)d->hist[bin] * (hy1 - y - 2) / d->hmax);
    if (hgt > 0) { Move(rp, hx0 + k, hy1 - 1); Draw(rp, hx0 + k, hy1 - 1 - hgt); }
  }
}
static ULONG infdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct IData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    memset(INST_DATA(cl, obj), 0, sizeof(struct IData));
    return (ULONG)obj;
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 100; m->MinMaxInfo->DefWidth += 200; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += 60; m->MinMaxInfo->DefHeight += 160; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    return TRUE;
  case MUIM_Cleanup: theme_cleanup(obj); break;
  case MUIM_Show: ((struct IData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct IData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) info_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Info_Refresh: {
    struct MUIP_Info_Refresh *m = (struct MUIP_Info_Refresh *)msg;
    d = INST_DATA(cl, obj);
    if (m->picture) d->hok = 0;
    if (d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0; }
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *info_class(void)
{
  if (!infcls) infcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct IData), (APTR)infdispatch);
  return infcls;
}
void panels2_free(void)
{
  if (navcls) { MUI_DeleteCustomClass(navcls); navcls = NULL; }
  if (infcls) { MUI_DeleteCustomClass(infcls); infcls = NULL; }
}
