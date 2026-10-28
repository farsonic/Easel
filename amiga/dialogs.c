/*
 * dialogs.c - Easel's dialogs: New, Image Size, Canvas Size, Rotate, the adjustments and filters
 * (each shown on the picture as its sliders move, and taken back on Cancel), Levels with its
 * histogram, the Curves editor, Layer Properties, Fill, Stroke, the selection's Modify items and
 * the JPEG quality asked for on export.
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
#include "../core/codec.h"

static ULONG xget(Object *o, ULONG a) { ULONG v = 0; get(o, a, &v); return v; }
static Object *slider(LONG min, LONG max, LONG v)
{
  return SliderObject, MUIA_Numeric_Min, min, MUIA_Numeric_Max, max, MUIA_Numeric_Value, v, MUIA_CycleChain, 1, End;
}
static Object *string_num(LONG v)
{
  char buf[24];
  sprintf(buf, "%ld", (long)v);
  return StringObject, StringFrame, MUIA_String_Contents, (ULONG)buf, MUIA_String_Accept, (ULONG)"0123456789.-", MUIA_String_MaxLen, 12, MUIA_CycleChain, 1, End;
}
static Object *cycle(const char *const *labels, int active)
{
  return CycleObject, MUIA_Cycle_Entries, (ULONG)labels, MUIA_Cycle_Active, active, MUIA_CycleChain, 1, End;
}
static Object *check(int on)
{
  return ImageObject, ImageButtonFrame, MUIA_InputMode, MUIV_InputMode_Toggle, MUIA_Image_Spec, MUII_CheckMark,
    MUIA_Background, MUII_ButtonBack, MUIA_ShowSelState, FALSE, MUIA_Selected, on, MUIA_CycleChain, 1, End;
}
static LONG str_val(Object *s) { return atol((const char *)xget(s, MUIA_String_Contents)); }
static double str_dbl(Object *s) { return atof((const char *)xget(s, MUIA_String_Contents)); }
static void notify_change(Object *o, ULONG attr)
{
  if (o) DoMethod(o, MUIM_Notify, attr, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGCHANGE);
}
static void out_of_memory(void) { ask("OK", "%s", tr("There isn't enough memory for that.")); }

/* ------------------------------------------------------------------ previews */
/* an adjustment or filter shown on the picture: applied again at each change, the step before it
   taken back first. prev: the undo step that was on top before the preview's own */
typedef struct { Doc *d; void *prev; int shown; } Preview;
static void *top_step(Doc *d) { return (void *)d->undo; }
static void pv_start(Preview *p) { p->d = cur_doc(); p->prev = top_step(p->d); p->shown = 0; }
static void pv_drop(Preview *p)
{
  if (p->shown && top_step(p->d) != p->prev) doc_discard_last(p->d);
  p->shown = 0;
}
static void pv_done(Preview *p) { p->shown = top_step(p->d) != p->prev; DoMethod(canvas, MUIM_Canvas_Refresh); }

/* ------------------------------------------------------------------ the graph: histogram and curves */
#define MUIA_Graph_Mode   (QTAG + 0x800)   /* 0 a histogram (with the levels' markers), 1 the curves editor */
#define MUIA_Graph_Hist   (QTAG + 0x801)   /* const u32 *: 256 counts */
#define MUIA_Graph_Levels (QTAG + 0x802)   /* const LONG *: in black, in white, gamma x100 (markers drawn) */
#define MUIA_Graph_Changed (QTAG + 0x803)  /* (notify) a curve point moved */
#define MAXPTS 16
struct GData {
  int mode, shown, npts, drag;
  int pts[MAXPTS * 2];
  const u32 *hist; const LONG *lev;
  struct MUI_EventHandlerNode ehn;
};
static struct MUI_CustomClass *gcls;
#define GS 256
static LONG gx(Object *obj, int v) { return _mleft(obj) + 1 + v * (_mwidth(obj) - 3) / 255; }
static LONG gy(Object *obj, int v) { return _mbottom(obj) - 1 - v * (_mheight(obj) - 3) / 255; }
static void graph_draw(struct GData *g, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG l = _mleft(obj), t = _mtop(obj), r = _mright(obj), b = _mbottom(obj);
  int i;
  theme_fill(rp, l, t, r, b, tpen(T_WHITE));
  theme_rect(rp, l, t, r, b, tpen(T_BORDER2));
  SetAPen(rp, tpen(T_BORDER));
  for (i = 1; i < 4; i++) { Move(rp, l + (r - l) * i / 4, t + 1); Draw(rp, l + (r - l) * i / 4, b - 1); if (g->mode) { Move(rp, l + 1, t + (b - t) * i / 4); Draw(rp, r - 1, t + (b - t) * i / 4); } }
  if (g->hist) {
    u32 mx = 1;
    for (i = 0; i < 256; i++) if (g->hist[i] > mx) mx = g->hist[i];
    SetAPen(rp, tpen(g->mode ? T_BORDER2 : T_DIM));
    for (i = 0; i < 256; i++) {
      /* (square-root scaled, so a tall spike doesn't flatten everything else) */
      LONG h = (LONG)(sqrt((double)g->hist[i] / mx) * (b - t - 2));
      if (h > 0) { Move(rp, gx(obj, i), b - 1); Draw(rp, gx(obj, i), b - 1 - h); }
    }
  }
  if (!g->mode && g->lev) {
    /* the levels' input markers: black, grey (gamma), white */
    LONG xs[3];
    double gm = g->lev[2] / 100.0;
    xs[0] = gx(obj, (int)g->lev[0]); xs[2] = gx(obj, (int)g->lev[1]);
    xs[1] = gx(obj, (int)(g->lev[0] + (g->lev[1] - g->lev[0]) * pow(0.5, gm)));
    for (i = 0; i < 3; i++) {
      SetAPen(rp, i == 0 ? tpen(T_TEXT) : i == 1 ? tpen(T_DIM) : tpen(T_ACCENT));
      Move(rp, xs[i], t + 1); Draw(rp, xs[i], b - 1);
    }
  }
  if (g->mode) {
    u8 t[256];
    curve_lut(t, g->pts, g->npts);
    SetAPen(rp, tpen(T_TEXT));
    for (i = 0; i < 256; i += 2) { LONG x = gx(obj, i), y = gy(obj, t[i]); if (i) Draw(rp, x, y); else Move(rp, x, y); }
    Draw(rp, gx(obj, 255), gy(obj, t[255]));
    for (i = 0; i < g->npts; i++) {
      LONG x = gx(obj, g->pts[i * 2]), y = gy(obj, g->pts[i * 2 + 1]);
      theme_fill(rp, x - 3, y - 3, x + 3, y + 3, tpen(i == g->drag ? T_ACCENT : T_TEXT));
    }
  }
}
static ULONG gdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct GData *g;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    g = INST_DATA(cl, obj);
    memset(g, 0, sizeof *g);
    g->mode = (int)GetTagData(MUIA_Graph_Mode, 0, ((struct opSet *)msg)->ops_AttrList);
    g->hist = (const u32 *)GetTagData(MUIA_Graph_Hist, 0, ((struct opSet *)msg)->ops_AttrList);
    g->lev = (const LONG *)GetTagData(MUIA_Graph_Levels, 0, ((struct opSet *)msg)->ops_AttrList);
    g->npts = 2; g->pts[0] = 0; g->pts[1] = 0; g->pts[2] = 255; g->pts[3] = 255;
    g->drag = -1;
    g->ehn.ehn_Object = obj; g->ehn.ehn_Class = cl; g->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
    return (ULONG)obj;
  case OM_SET: {
    struct TagItem *t;
    g = INST_DATA(cl, obj);
    if ((t = FindTagItem(MUIA_Graph_Hist, ((struct opSet *)msg)->ops_AttrList))) g->hist = (const u32 *)t->ti_Data;
    if (g->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
    break;
  }
  case OM_GET:
    if (((struct opGet *)msg)->opg_AttrID == MUIA_Graph_Changed) { *((struct opGet *)msg)->opg_Storage = TRUE; return TRUE; }
    break;
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += GS + 3; m->MinMaxInfo->DefWidth += GS + 3; m->MinMaxInfo->MaxWidth += GS + 3;
    g = INST_DATA(cl, obj);
    m->MinMaxInfo->MinHeight += g->mode ? GS + 3 : 100; m->MinMaxInfo->DefHeight += g->mode ? GS + 3 : 100; m->MinMaxInfo->MaxHeight += g->mode ? GS + 3 : 100;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct GData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup: theme_cleanup(obj); break;
  case MUIM_Show: ((struct GData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct GData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) graph_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    int x, y, i;
    g = INST_DATA(cl, obj);
    if (!im || !g->shown || !g->mode) return 0;
    x = (int)((im->MouseX - _mleft(obj) - 1) * 255 / (_mwidth(obj) - 3));
    y = (int)((_mbottom(obj) - 1 - im->MouseY) * 255 / (_mheight(obj) - 3));
    if (im->Class == IDCMP_MOUSEMOVE && g->drag >= 0) {
      int k = g->drag, lo = k > 0 ? g->pts[(k - 1) * 2] + 1 : 0, hi = k < g->npts - 1 ? g->pts[(k + 1) * 2] - 1 : 255;
      if (y < 0) y = 0;
      if (y > 255) y = 255;
      /* dragged well out of the box (not an end point): taken away */
      if (k > 0 && k < g->npts - 1 && (im->MouseY > _mbottom(obj) + 20 || im->MouseY < _mtop(obj) - 20)) {
        memmove(&g->pts[k * 2], &g->pts[k * 2 + 2], (size_t)(g->npts - k - 1) * 2 * sizeof(int));
        g->npts--; g->drag = -1;
      } else {
        if (x < lo) x = lo;
        if (x > hi) x = hi;
        g->pts[k * 2] = x; g->pts[k * 2 + 1] = y;
      }
      MUI_Redraw(obj, MADF_DRAWOBJECT);
      return MUI_EventHandlerRC_Eat;
    }
    if (im->Class != IDCMP_MOUSEBUTTONS) return 0;
    if (im->Code == SELECTUP && g->drag >= 0) { g->drag = -1; MUI_Redraw(obj, MADF_DRAWOBJECT); SetAttrs(obj, MUIA_Graph_Changed, TRUE, TAG_DONE); return MUI_EventHandlerRC_Eat; }
    if (im->Code != SELECTDOWN || im->MouseX < _mleft(obj) || im->MouseX > _mright(obj) || im->MouseY < _mtop(obj) || im->MouseY > _mbottom(obj)) return 0;
    for (i = 0; i < g->npts; i++) if (abs(g->pts[i * 2] - x) <= 6 && abs(g->pts[i * 2 + 1] - y) <= 10) { g->drag = i; break; }
    if (g->drag < 0 && g->npts < MAXPTS) {
      /* a new point on the curve where it was clicked */
      for (i = 0; i < g->npts && g->pts[i * 2] < x; i++) ;
      if (i > 0 && i < g->npts && g->pts[i * 2] != x) {
        memmove(&g->pts[i * 2 + 2], &g->pts[i * 2], (size_t)(g->npts - i) * 2 * sizeof(int));
        g->pts[i * 2] = x; g->pts[i * 2 + 1] = y < 0 ? 0 : y > 255 ? 255 : y;
        g->npts++; g->drag = i;
      }
    }
    MUI_Redraw(obj, MADF_DRAWOBJECT);
    return MUI_EventHandlerRC_Eat;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
static Object *graph(int mode, const u32 *hist, const LONG *lev)
{
  if (!gcls && !(gcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct GData), (APTR)gdispatch))) return NULL;
  return NewObject(gcls->mcc_Class, NULL, MUIA_Graph_Mode, mode, MUIA_Graph_Hist, (ULONG)hist, MUIA_Graph_Levels, (ULONG)lev, MUIA_FillArea, FALSE, TAG_DONE);
}

/* ------------------------------------------------------------------ the slider dialogs */
/* each slider: its label, range and starting value; a value /scale is what the core function gets */
typedef struct { const char *label; LONG min, max, def; } Param;
typedef void (*ApplyFn)(Doc *d, const LONG *v, int choice, int tick);
static struct {
  Object *sl[6]; int n; Object *cyc, *chk, *pre; ApplyFn fn; Preview pv; LONG v[6]; int pending;
} sd;
static void sd_read(void)
{
  int i;
  for (i = 0; i < sd.n; i++) sd.v[i] = (LONG)xget(sd.sl[i], MUIA_Numeric_Value);
}
static void sd_apply(void)
{
  sd_read();
  pv_drop(&sd.pv);
  set(app, MUIA_Application_Sleep, TRUE);
  sd.fn(sd.pv.d, sd.v, sd.cyc ? (int)xget(sd.cyc, MUIA_Cycle_Active) : 0, sd.chk ? (int)xget(sd.chk, MUIA_Selected) : 0);
  set(app, MUIA_Application_Sleep, FALSE);
  pv_done(&sd.pv);
}
static void sd_each(ULONG id)
{
  if (id == ID_DLGCHANGE) { sd.pending = 1; return; }      /* (shown once the input has been dealt with) */
  if (id == 0 && sd.pending) {
    sd.pending = 0;
    if (xget(sd.pre, MUIA_Selected)) sd_apply();
    else if (sd.pv.shown) { pv_drop(&sd.pv); DoMethod(canvas, MUIM_Canvas_Refresh); }
  } else if (id == ID_DLGRESET) {
    int i;
    for (i = 0; i < sd.n; i++) nnset(sd.sl[i], MUIA_Numeric_Value, xget(sd.sl[i], MUIA_Numeric_Default));
    sd.pending = 1;
  }
}
/* the dialog itself: title, n sliders, an optional list (choice) and tick; the function applies
   them. 1 if it was applied */
static int slider_dialog(const char *title, const Param *p, int n, const char *const *choices, const char *choicelabel, const char *ticklabel, int tickdef, ApplyFn fn, Object *extra)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *grp, *w, *reset;
  int i, ok;
  memset(&sd, 0, sizeof sd);
  sd.n = n; sd.fn = fn;
  pv_start(&sd.pv);
  grp = ColGroup(2), End;
  if (!grp) return 0;
  if (choices) {
    DoMethod(grp, OM_ADDMEMBER, (ULONG)QLabel(tr(choicelabel), MUIO_Label_Centered));
    DoMethod(grp, OM_ADDMEMBER, (ULONG)(sd.cyc = cycle(choices, 0)));
  }
  for (i = 0; i < n; i++) {
    DoMethod(grp, OM_ADDMEMBER, (ULONG)QLabel(tr(p[i].label), MUIO_Label_Centered));
    DoMethod(grp, OM_ADDMEMBER, (ULONG)(sd.sl[i] = SliderObject, MUIA_Numeric_Min, p[i].min, MUIA_Numeric_Max, p[i].max, MUIA_Numeric_Value, p[i].def,
                                         MUIA_Numeric_Default, p[i].def, MUIA_CycleChain, 1, MUIA_FixWidthTxt, (ULONG)"MMMMMMMMMMMMMMMMMMMMMM", End));
  }
  if (ticklabel) {
    DoMethod(grp, OM_ADDMEMBER, (ULONG)(sd.chk = check(tickdef)));
    DoMethod(grp, OM_ADDMEMBER, (ULONG)QLabel(tr(ticklabel), MUIO_Label_Centered));
  }
  w = dialog_window(title, VGroup,
        Child, extra ? extra : (Object *)HVSpace,
        Child, grp,
        Child, HGroup, Child, sd.pre = check(1), Child, QLabel(tr("Preview"), 0), Child, HSpace(0), Child, reset = QButton(tr("Reset")), End,
      End, NULL);
  if (!w) return 0;
  for (i = 0; i < n; i++) notify_change(sd.sl[i], MUIA_Numeric_Value);
  notify_change(sd.cyc, MUIA_Cycle_Active);
  notify_change(sd.chk, MUIA_Selected);
  notify_change(sd.pre, MUIA_Selected);
  DoMethod(reset, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGRESET);
  /* (the starting values change nothing: the preview is shown once something moves) */
  ok = run_modal(w, ends, sd_each) == ID_DLGOK;
  if (ok && (!sd.pv.shown || sd.pending)) sd_apply();
  if (!ok) pv_drop(&sd.pv);
  MUI_DisposeObject(w);
  DoMethod(canvas, MUIM_Canvas_Refresh);
  return ok;
}

/* each adjustment's call with the sliders' values */
static void a_bc(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; adj_brightness_contrast(d, (int)v[0], (int)v[1]); }
static void a_exposure(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; adj_exposure(d, v[0] / 100.0, v[1] / 1000.0, v[2] / 100.0); }
static void a_vibrance(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; adj_vibrance(d, (int)v[0], (int)v[1]); }
static void a_huesat(Doc *d, const LONG *v, int c, int t) { (void)c; adj_hue_saturation(d, (int)v[0], (int)v[1], (int)v[2], t); }
static void a_bw(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; adj_black_white(d, (int)v[0], (int)v[1], (int)v[2]); }
static void a_posterize(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; adj_posterize(d, (int)v[0]); }
static void a_threshold(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; adj_threshold(d, (int)v[0]); }
static void a_balance(Doc *d, const LONG *v, int c, int t)
{
  int zero[3] = { 0, 0, 0 }, val[3];
  val[0] = (int)v[0]; val[1] = (int)v[1]; val[2] = (int)v[2];
  adj_color_balance(d, c == 0 ? val : zero, c == 1 ? val : zero, c == 2 ? val : zero, t);
}
static ULONG photo_color;
static void a_photo(Doc *d, const LONG *v, int c, int t)
{
  static const ULONG cols[] = { 0xEC8A00, 0xFA9600, 0x006DFF, 0x005DFF, 0xEA1A1A, 0x19C900, 0x6B0ACD };
  (void)photo_color;
  adj_photo_filter(d, cols[c >= 0 && c < 7 ? c : 0], (int)v[0], t);
}
/* Levels: the five numbers and the channel; Curves: the editor's points */
static LONG lev[3] = { 0, 255, 100 };
static Object *lev_graph;
static u32 lev_hist[1024];
static void a_levels(Doc *d, const LONG *v, int c, int t)
{
  (void)t;
  lev[0] = v[0]; lev[1] = v[2] > v[0] ? v[2] : v[0] + 1; lev[2] = v[1];
  if (lev_graph) set(lev_graph, MUIA_Graph_Hist, (ULONG)(lev_hist + 256 * c));
  adj_levels(d, c, (int)v[0], (int)lev[1], v[1] / 100.0, (int)v[3], (int)v[4]);
}
static Object *curve_obj;
static void a_curves(Doc *d, const LONG *v, int c, int t)
{
  struct GData *g = INST_DATA(gcls->mcc_Class, curve_obj);
  (void)v; (void)t;
  adj_curves(d, c, g->pts, g->npts);
}
void dlg_adjust(ULONG id)
{
  Doc *d = cur_doc();
  static const char *const ranges[] = { "Shadows", "Midtones", "Highlights", NULL };
  static const char *const channels[] = { "RGB", "Red", "Green", "Blue", NULL };
  static const char *const photos[] = { "Warming (85)", "Warming (LBA)", "Cooling (80)", "Cooling (LBB)", "Red", "Green", "Violet", NULL };
  if (!d) return;
  switch (id) {
  case ID_ADJ_BC: { static const Param p[] = { { "Brightness:", -150, 150, 0 }, { "Contrast:", -50, 100, 0 } }; slider_dialog("Brightness/Contrast", p, 2, NULL, NULL, NULL, 0, a_bc, NULL); break; }
  case ID_ADJ_EXPOSURE: { static const Param p[] = { { "Exposure (x0.01):", -500, 500, 0 }, { "Offset (x0.001):", -500, 500, 0 }, { "Gamma (x0.01):", 1, 999, 100 } }; slider_dialog("Exposure", p, 3, NULL, NULL, NULL, 0, a_exposure, NULL); break; }
  case ID_ADJ_VIBRANCE: { static const Param p[] = { { "Vibrance:", -100, 100, 0 }, { "Saturation:", -100, 100, 0 } }; slider_dialog("Vibrance", p, 2, NULL, NULL, NULL, 0, a_vibrance, NULL); break; }
  case ID_ADJ_HUESAT: { static const Param p[] = { { "Hue:", -180, 180, 0 }, { "Saturation:", -100, 100, 0 }, { "Lightness:", -100, 100, 0 } }; slider_dialog("Hue/Saturation", p, 3, NULL, NULL, "Colorize", 0, a_huesat, NULL); break; }
  case ID_ADJ_BALANCE: { static const Param p[] = { { "Cyan - Red:", -100, 100, 0 }, { "Magenta - Green:", -100, 100, 0 }, { "Yellow - Blue:", -100, 100, 0 } };
    slider_dialog("Color Balance", p, 3, ranges, "Tone:", "Preserve Luminosity", 1, a_balance, NULL); break; }
  case ID_ADJ_BW: { static const Param p[] = { { "Reds (%):", -200, 300, 40 }, { "Greens (%):", -200, 300, 40 }, { "Blues (%):", -200, 300, 20 } }; slider_dialog("Black & White", p, 3, NULL, NULL, NULL, 0, a_bw, NULL); break; }
  case ID_ADJ_PHOTO: { static const Param p[] = { { "Density (%):", 1, 100, 25 } }; slider_dialog("Photo Filter", p, 1, photos, "Filter:", "Preserve Luminosity", 1, a_photo, NULL); break; }
  case ID_ADJ_POSTERIZE: { static const Param p[] = { { "Levels:", 2, 255, 4 } }; slider_dialog("Posterize", p, 1, NULL, NULL, NULL, 0, a_posterize, NULL); break; }
  case ID_ADJ_THRESHOLD: { static const Param p[] = { { "Threshold Level:", 1, 255, 128 } }; slider_dialog("Threshold", p, 1, NULL, NULL, NULL, 0, a_threshold, NULL); break; }
  case ID_ADJ_LEVELS: {
    static const Param p[] = { { "Input Black:", 0, 253, 0 }, { "Gamma (x0.01):", 10, 999, 100 }, { "Input White:", 2, 255, 255 }, { "Output Black:", 0, 255, 0 }, { "Output White:", 0, 255, 255 } };
    histogram(d, 0, lev_hist);
    lev[0] = 0; lev[1] = 255; lev[2] = 100;
    lev_graph = graph(0, lev_hist, lev);
    slider_dialog("Levels", p, 5, channels, "Channel:", NULL, 0, a_levels, lev_graph);
    lev_graph = NULL;
    break; }
  case ID_ADJ_CURVES: {
    static u32 hist[1024];
    histogram(d, 0, hist);
    curve_obj = graph(1, hist, NULL);
    if (!curve_obj) return;
    notify_change(curve_obj, MUIA_Graph_Changed);
    slider_dialog("Curves", NULL, 0, channels, "Channel:", NULL, 0, a_curves, VGroup, Child, curve_obj,
                  Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Click the line to add a point, drag points to bend it;\ndrag a point out of the box to take it away."), MUIA_Font, MUIV_Font_Tiny, End, End);
    break; }
  case ID_ADJ_INVERT: adj_invert(d); break;
  case ID_ADJ_DESAT: adj_desaturate(d); break;
  case ID_ADJ_EQUALIZE: adj_equalize(d); break;
  }
  if (id == ID_ADJ_CURVES || id == ID_ADJ_LEVELS) { (void)0; }
}
/* the Curves dialog's graph tells of a moved point through its notification */
/* (wired in slider_dialog by the extra object: see graph_notify) */

/* the filters' calls */
static void f_gauss(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_gaussian_blur(d, v[0] / 10.0); }
static void f_box(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_box_blur(d, (int)v[0]); }
static void f_motion(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_motion_blur(d, (double)v[0], (int)v[1]); }
static void f_radial(Doc *d, const LONG *v, int c, int t) { (void)t; flt_radial_blur(d, (int)v[0], c); }
static void f_unsharp(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_unsharp_mask(d, (int)v[0], v[1] / 10.0, (int)v[2]); }
static void f_noise(Doc *d, const LONG *v, int c, int t) { flt_add_noise(d, (double)v[0], c, t); }
static void f_median(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_median(d, (int)v[0]); }
static void f_emboss(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_emboss(d, (double)v[0], (int)v[1], (int)v[2]); }
static void f_highpass(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_high_pass(d, v[0] / 10.0); }
static void f_mosaic(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_mosaic(d, (int)v[0]); }
static void f_twirl(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_twirl(d, (double)v[0]); }
static void f_pinch(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_pinch(d, (int)v[0]); }
static void f_spherize(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_spherize(d, (int)v[0]); }
static void f_ripple(Doc *d, const LONG *v, int c, int t) { (void)t; flt_ripple(d, (int)v[0], c); }
static void f_wave(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_wave(d, (int)v[0], (int)v[1]); }
static void f_min(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_minimum(d, (int)v[0]); }
static void f_max(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_maximum(d, (int)v[0]); }
static void f_offset(Doc *d, const LONG *v, int c, int t) { (void)c; flt_offset(d, v[0], v[1], t); }
static void f_oil(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_oil_paint(d, (int)v[0], (int)v[1]); }
static void f_vignette(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_vignette(d, (int)v[0]); }
void dlg_filter(ULONG id)
{
  Doc *d = cur_doc();
  static const char *const radial[] = { "Spin", "Zoom", NULL };
  static const char *const dist[] = { "Uniform", "Gaussian", NULL };
  static const char *const rsize[] = { "Small", "Medium", "Large", NULL };
  if (!d) return;
  switch (id) {
  case ID_F_GAUSS: { static const Param p[] = { { "Radius (x0.1 px):", 1, 2500, 20 } }; slider_dialog("Gaussian Blur", p, 1, NULL, NULL, NULL, 0, f_gauss, NULL); break; }
  case ID_F_BOX: { static const Param p[] = { { "Radius (px):", 1, 500, 5 } }; slider_dialog("Box Blur", p, 1, NULL, NULL, NULL, 0, f_box, NULL); break; }
  case ID_F_MOTION: { static const Param p[] = { { "Angle:", -180, 180, 0 }, { "Distance (px):", 1, 500, 10 } }; slider_dialog("Motion Blur", p, 2, NULL, NULL, NULL, 0, f_motion, NULL); break; }
  case ID_F_RADIAL: { static const Param p[] = { { "Amount:", 1, 100, 10 } }; slider_dialog("Radial Blur", p, 1, radial, "Method:", NULL, 0, f_radial, NULL); break; }
  case ID_F_UNSHARP: { static const Param p[] = { { "Amount (%):", 1, 500, 100 }, { "Radius (x0.1 px):", 1, 2500, 10 }, { "Threshold:", 0, 255, 0 } }; slider_dialog("Unsharp Mask", p, 3, NULL, NULL, NULL, 0, f_unsharp, NULL); break; }
  case ID_F_NOISE: { static const Param p[] = { { "Amount (%):", 1, 400, 12 } }; slider_dialog("Add Noise", p, 1, dist, "Distribution:", "Monochromatic", 0, f_noise, NULL); break; }
  case ID_F_MEDIAN: { static const Param p[] = { { "Radius (px):", 1, 100, 1 } }; slider_dialog("Median", p, 1, NULL, NULL, NULL, 0, f_median, NULL); break; }
  case ID_F_EMBOSS: { static const Param p[] = { { "Angle:", -180, 180, 135 }, { "Height (px):", 1, 10, 3 }, { "Amount (%):", 1, 500, 100 } }; slider_dialog("Emboss", p, 3, NULL, NULL, NULL, 0, f_emboss, NULL); break; }
  case ID_F_HIGHPASS: { static const Param p[] = { { "Radius (x0.1 px):", 1, 2500, 100 } }; slider_dialog("High Pass", p, 1, NULL, NULL, NULL, 0, f_highpass, NULL); break; }
  case ID_F_MOSAIC: { static const Param p[] = { { "Cell Size (px):", 2, 200, 8 } }; slider_dialog("Mosaic", p, 1, NULL, NULL, NULL, 0, f_mosaic, NULL); break; }
  case ID_F_TWIRL: { static const Param p[] = { { "Angle:", -999, 999, 50 } }; slider_dialog("Twirl", p, 1, NULL, NULL, NULL, 0, f_twirl, NULL); break; }
  case ID_F_PINCH: { static const Param p[] = { { "Amount (%):", -100, 100, 50 } }; slider_dialog("Pinch", p, 1, NULL, NULL, NULL, 0, f_pinch, NULL); break; }
  case ID_F_SPHERIZE: { static const Param p[] = { { "Amount (%):", -100, 100, 100 } }; slider_dialog("Spherize", p, 1, NULL, NULL, NULL, 0, f_spherize, NULL); break; }
  case ID_F_RIPPLE: { static const Param p[] = { { "Amount (%):", -999, 999, 100 } }; slider_dialog("Ripple", p, 1, rsize, "Size:", NULL, 0, f_ripple, NULL); break; }
  case ID_F_WAVE: { static const Param p[] = { { "Wavelength (px):", 2, 500, 40 }, { "Amplitude (px):", 1, 200, 8 } }; slider_dialog("Wave", p, 2, NULL, NULL, NULL, 0, f_wave, NULL); break; }
  case ID_F_MIN: { static const Param p[] = { { "Radius (px):", 1, 100, 1 } }; slider_dialog("Minimum", p, 1, NULL, NULL, NULL, 0, f_min, NULL); break; }
  case ID_F_MAX: { static const Param p[] = { { "Radius (px):", 1, 100, 1 } }; slider_dialog("Maximum", p, 1, NULL, NULL, NULL, 0, f_max, NULL); break; }
  case ID_F_OFFSET: { static const Param p[] = { { "Horizontal (px):", -2000, 2000, 0 }, { "Vertical (px):", -2000, 2000, 0 } }; slider_dialog("Offset", p, 2, NULL, NULL, "Wrap Around", 1, f_offset, NULL); break; }
  case ID_F_OIL: { static const Param p[] = { { "Radius (px):", 1, 10, 3 }, { "Levels:", 2, 64, 20 } }; slider_dialog("Oil Paint", p, 2, NULL, NULL, NULL, 0, f_oil, NULL); break; }
  case ID_F_VIGNETTE: { static const Param p[] = { { "Amount (%):", -100, 100, 50 } }; slider_dialog("Vignette", p, 1, NULL, NULL, NULL, 0, f_vignette, NULL); break; }
  case ID_F_BLUR: flt_blur(d); break;
  case ID_F_SHARPEN: flt_sharpen(d); break;
  case ID_F_SHARPENMORE: flt_sharpen_more(d); break;
  case ID_F_DESPECKLE: flt_despeckle(d); break;
  case ID_F_EDGES: flt_find_edges(d); break;
  case ID_F_SOLARIZE: flt_solarize(d); break;
  case ID_F_CLOUDS: flt_clouds(d, d->fg, d->bg, (unsigned)rand()); break;
  }
}

/* ------------------------------------------------------------------ New, Image Size, Canvas Size, Rotate */
int dlg_new_values(LONG *w, LONG *h, LONG *dpi, int *bg)
{
  static const char *const presets[] = { "Custom", "640 x 480", "800 x 600", "1024 x 768", "1280 x 1024", "1920 x 1080", "320 x 256 (PAL low res)",
    "640 x 512 (PAL high res)", "A4 at 150 dpi", "Square 1000", NULL };
  static const LONG pw[] = { 0, 640, 800, 1024, 1280, 1920, 320, 640, 1240, 1000 }, ph[] = { 0, 480, 600, 768, 1024, 1080, 256, 512, 1754, 1000 };
  static const char *const bgs[] = { "White", "Background Colour", "Transparent", NULL };
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *wn, *ws, *hs, *ds, *bc, *pc;
  int ok = 0;
  for (;;) {
    wn = dialog_window("New", ColGroup(2),
           Child, QLabel(tr("Preset:"), MUIO_Label_Centered), Child, pc = cycle(presets, 0),
           Child, QLabel(tr("Width (px):"), MUIO_Label_SingleFrame), Child, ws = string_num(*w),
           Child, QLabel(tr("Height (px):"), MUIO_Label_SingleFrame), Child, hs = string_num(*h),
           Child, QLabel(tr("Resolution (dpi):"), MUIO_Label_SingleFrame), Child, ds = string_num(*dpi),
           Child, QLabel(tr("Background:"), MUIO_Label_Centered), Child, bc = cycle(bgs, *bg),
         End, "Create");
    if (!wn) return 0;
    notify_change(pc, MUIA_Cycle_Active);
    set(wn, MUIA_Window_ActiveObject, (ULONG)ws);
    {
      static const ULONG ends2[] = { ID_DLGOK, ID_DLGCHANGE, 0 };
      ULONG id;
      while ((id = run_modal(wn, ends2, NULL)) == ID_DLGCHANGE) {
        int k = (int)xget(pc, MUIA_Cycle_Active);
        char b[16];
        if (k <= 0) continue;
        sprintf(b, "%ld", (long)pw[k]); set(ws, MUIA_String_Contents, (ULONG)b);
        sprintf(b, "%ld", (long)ph[k]); set(hs, MUIA_String_Contents, (ULONG)b);
        if (k == 8) set(ds, MUIA_String_Contents, (ULONG)"150");
      }
      if (id == ID_DLGOK) {
        *w = str_val(ws); *h = str_val(hs); *dpi = str_val(ds); *bg = (int)xget(bc, MUIA_Cycle_Active);
        ok = 1;
      }
    }
    MUI_DisposeObject(wn);
    if (!ok) return 0;
    if (*w >= 1 && *h >= 1 && *w <= MAXDIM && *h <= MAXDIM) break;
    ask("OK", "%s", tr("The width and height must be between 1 and 16384 pixels."));
    ok = 0;
  }
  if (*dpi < 1 || *dpi > 9999) *dpi = 72;
  (void)ends;
  return 1;
}
void dlg_image_size(void)
{
  Doc *d = cur_doc();
  static const char *const methods[] = { "Bicubic (smooth)", "Bilinear", "Nearest Neighbour (hard edges)", NULL };
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *ws, *hs, *ps, *ck, *mc;
  LONG nw, nh;
  int ok, method;
  if (!d) return;
  w = dialog_window("Image Size", ColGroup(2),
        Child, QLabel(tr("Width (px):"), MUIO_Label_SingleFrame), Child, ws = string_num(d->w),
        Child, QLabel(tr("Height (px):"), MUIO_Label_SingleFrame), Child, hs = string_num(d->h),
        Child, QLabel(tr("or Percent:"), MUIO_Label_SingleFrame), Child, ps = string_num(100),
        Child, ck = check(1), Child, QLabel(tr("Constrain proportions (height follows width)"), 0),
        Child, QLabel(tr("Resample:"), MUIO_Label_Centered), Child, mc = cycle(methods, 0),
      End, NULL);
  if (!w) return;
  set(w, MUIA_Window_ActiveObject, (ULONG)ws);
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  nw = str_val(ws); nh = str_val(hs);
  if (str_val(ps) != 100 && str_val(ps) > 0) { nw = d->w * str_val(ps) / 100; nh = d->h * str_val(ps) / 100; }
  else if (xget(ck, MUIA_Selected) && nw != d->w && nh == d->h) nh = (LONG)((double)d->h * nw / d->w + 0.5);
  else if (xget(ck, MUIA_Selected) && nh != d->h && nw == d->w) nw = (LONG)((double)d->w * nh / d->h + 0.5);
  method = (int)xget(mc, MUIA_Cycle_Active);
  MUI_DisposeObject(w);
  if (!ok || (nw == d->w && nh == d->h)) return;
  if (nw < 1 || nh < 1 || nw > MAXDIM || nh > MAXDIM) { ask("OK", "%s", tr("The width and height must be between 1 and 16384 pixels.")); return; }
  set(app, MUIA_Application_Sleep, TRUE);
  if (!image_resize(d, (i32)nw, (i32)nh, method == 0 ? RS_BICUBIC : method == 1 ? RS_BILINEAR : RS_NEAREST)) out_of_memory();
  set(app, MUIA_Application_Sleep, FALSE);
}
void dlg_canvas_size(void)
{
  Doc *d = cur_doc();
  static const char *const anchors[] = { "Top Left", "Top", "Top Right", "Left", "Centre", "Right", "Bottom Left", "Bottom", "Bottom Right", NULL };
  static const char *const fills[] = { "Background Colour", "Foreground Colour", "White", "Black", "Transparent", NULL };
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *ws, *hs, *ac, *fc;
  LONG nw, nh;
  int ok, anchor, fill;
  u32 fillc;
  if (!d) return;
  w = dialog_window("Canvas Size", ColGroup(2),
        Child, QLabel(tr("Width (px):"), MUIO_Label_SingleFrame), Child, ws = string_num(d->w),
        Child, QLabel(tr("Height (px):"), MUIO_Label_SingleFrame), Child, hs = string_num(d->h),
        Child, QLabel(tr("Anchor:"), MUIO_Label_Centered), Child, ac = cycle(anchors, 4),
        Child, QLabel(tr("Extension colour:"), MUIO_Label_Centered), Child, fc = cycle(fills, 0),
      End, NULL);
  if (!w) return;
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  nw = str_val(ws); nh = str_val(hs); anchor = (int)xget(ac, MUIA_Cycle_Active); fill = (int)xget(fc, MUIA_Cycle_Active);
  MUI_DisposeObject(w);
  if (!ok || (nw == d->w && nh == d->h)) return;
  if (nw < 1 || nh < 1 || nw > MAXDIM || nh > MAXDIM) { ask("OK", "%s", tr("The width and height must be between 1 and 16384 pixels.")); return; }
  fillc = fill == 0 ? d->bg : fill == 1 ? d->fg : fill == 2 ? 0xFFFFFF : fill == 3 ? 0 : 0xFF000000UL;
  if (!canvas_resize(d, (i32)nw, (i32)nh, anchor, fillc)) out_of_memory();
}
void dlg_rotate(void)
{
  Doc *d = cur_doc();
  LONG a = 0;
  char buf[24] = "15";
  if (!d) return;
  if (!ask_string("Rotate Canvas", "Angle (degrees, clockwise):", buf, sizeof buf)) return;
  (void)a;
  set(app, MUIA_Application_Sleep, TRUE);
  if (atof(buf) != 0 && !image_rotate_angle(d, atof(buf))) out_of_memory();
  set(app, MUIA_Application_Sleep, FALSE);
}

/* ------------------------------------------------------------------ layer, fill, stroke, select */
static const char *const blends[] = { "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten", "Difference", "Add", "Subtract",
  "Color Dodge", "Color Burn", "Soft Light", "Hard Light", "Exclusion", "Hue", "Saturation", "Color", "Luminosity", NULL };
void dlg_layer_props(void)
{
  Doc *d = cur_doc();
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *ns, *os, *bc, *vc;
  Layer *l;
  char name[200];
  int ok;
  if (!d) return;
  l = d->lay[d->active];
  utf8_to_latin1(l->name, (u32)strlen(l->name), name, sizeof name);
  w = dialog_window("Layer Properties", ColGroup(2),
        Child, QLabel(tr("Name:"), MUIO_Label_SingleFrame),
        Child, ns = StringObject, StringFrame, MUIA_String_Contents, (ULONG)name, MUIA_String_MaxLen, 60, MUIA_CycleChain, 1, End,
        Child, QLabel(tr("Opacity (%):"), MUIO_Label_Centered), Child, os = slider(0, 100, (l->opacity * 100 + 127) / 255),
        Child, QLabel(tr("Blend Mode:"), MUIO_Label_Centered), Child, bc = cycle(blends, l->blend),
        Child, vc = check(l->visible), Child, QLabel(tr("Visible"), 0),
      End, NULL);
  if (!w) return;
  set(w, MUIA_Window_ActiveObject, (ULONG)ns);
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  if (ok) {
    char u[200];
    const char *s = (const char *)xget(ns, MUIA_String_Contents);
    size_t o = 0;
    for (; *s && o + 3 < sizeof u; s++) {
      UBYTE c = (UBYTE)*s;
      if (c < 0x80) u[o++] = (char)c; else { u[o++] = (char)(0xC0 | (c >> 6)); u[o++] = (char)(0x80 | (c & 63)); }
    }
    u[o] = 0;
    doc_set_layer_props(d, d->active, u, (int)((xget(os, MUIA_Numeric_Value) * 255 + 50) / 100), (int)xget(bc, MUIA_Cycle_Active), (int)xget(vc, MUIA_Selected));
  }
  MUI_DisposeObject(w);
}
void dlg_fill(void)
{
  Doc *d = cur_doc();
  static const char *const what[] = { "Foreground Colour", "Background Colour", "Black", "50% Grey", "White", "Colour...", NULL };
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *wc, *os, *bc;
  int ok, k, op, mode;
  u32 c;
  if (!d) return;
  w = dialog_window("Fill", ColGroup(2),
        Child, QLabel(tr("Contents:"), MUIO_Label_Centered), Child, wc = cycle(what, 0),
        Child, QLabel(tr("Mode:"), MUIO_Label_Centered), Child, bc = cycle(blends, 0),
        Child, QLabel(tr("Opacity (%):"), MUIO_Label_Centered), Child, os = slider(1, 100, 100),
      End, NULL);
  if (!w) return;
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  k = (int)xget(wc, MUIA_Cycle_Active); op = (int)xget(os, MUIA_Numeric_Value); mode = (int)xget(bc, MUIA_Cycle_Active);
  MUI_DisposeObject(w);
  if (!ok) return;
  c = k == 0 ? d->fg : k == 1 ? d->bg : k == 2 ? 0 : k == 3 ? 0x808080 : 0xFFFFFF;
  if (k == 5) { LONG p = pick_color("Fill Colour", d->fg); if (p < 0) return; c = (u32)p; }
  fill_selection(d, c, op, mode);
}
void dlg_stroke(void)
{
  Doc *d = cur_doc();
  static const char *const where[] = { "Inside", "Centre", "Outside", NULL };
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *ws, *lc, *os;
  int ok, width, loc, op;
  if (!d) return;
  if (!d->sel) { ask("OK", "%s", tr("Make a selection first: Stroke draws round its edge.")); return; }
  w = dialog_window("Stroke", ColGroup(2),
        Child, QLabel(tr("Width (px):"), MUIO_Label_SingleFrame), Child, ws = string_num(3),
        Child, QLabel(tr("Location:"), MUIO_Label_Centered), Child, lc = cycle(where, 1),
        Child, QLabel(tr("Opacity (%):"), MUIO_Label_Centered), Child, os = slider(1, 100, 100),
        Child, HSpace(0), Child, TextObject, MUIA_Text_Contents, (ULONG)tr("In the foreground colour"), End,
      End, NULL);
  if (!w) return;
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  width = (int)str_val(ws); loc = (int)xget(lc, MUIA_Cycle_Active); op = (int)xget(os, MUIA_Numeric_Value);
  MUI_DisposeObject(w);
  if (!ok || width < 1) return;
  stroke_selection(d, width > 250 ? 250 : width, loc, d->fg, op);
}
void dlg_select(ULONG id)
{
  Doc *d = cur_doc();
  LONG v = id == ID_COLORRANGE ? 40 : id == ID_FEATHER ? 5 : id == ID_SMOOTH ? 2 : 4;
  const char *t = id == ID_COLORRANGE ? "Color Range" : id == ID_FEATHER ? "Feather Selection" : id == ID_EXPAND ? "Expand Selection" :
                  id == ID_CONTRACT ? "Contract Selection" : id == ID_BORDER ? "Border Selection" : "Smooth Selection";
  const char *l = id == ID_COLORRANGE ? "Fuzziness (of the foreground colour):" : id == ID_FEATHER ? "Feather Radius (px):" :
                  id == ID_BORDER ? "Width (px):" : id == ID_SMOOTH ? "Sample Radius (px):" : id == ID_EXPAND ? "Expand By (px):" : "Contract By (px):";
  if (!d) return;
  if (id != ID_COLORRANGE && !d->sel) { ask("OK", "%s", tr("Nothing is selected.")); return; }
  if (!ask_number(t, l, &v, id == ID_COLORRANGE ? 0 : 1, id == ID_COLORRANGE ? 200 : 250)) return;
  set(app, MUIA_Application_Sleep, TRUE);
  switch (id) {
  case ID_COLORRANGE: sel_color_range(d, d->fg, (int)v); break;
  case ID_FEATHER: sel_feather(d, (double)v); break;
  case ID_EXPAND: sel_grow(d, (int)v); break;
  case ID_CONTRACT: sel_grow(d, -(int)v); break;
  case ID_BORDER: sel_border(d, (int)v); break;
  case ID_SMOOTH: sel_smooth(d, (int)v); break;
  }
  set(app, MUIA_Application_Sleep, FALSE);
}
extern int export_quality;
void dlg_export(int fmt)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *q;
  if (fmt != FMT_JPEG) return;
  w = dialog_window("JPEG Options", ColGroup(2),
        Child, QLabel(tr("Quality:"), MUIO_Label_Centered), Child, q = slider(1, 100, 85),
        Child, HSpace(0), Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Low quality makes small files; JPEG keeps no layers\nor transparency (it is laid over white)."), End,
      End, "Save");
  if (!w) return;
  if (run_modal(w, ends, NULL) == ID_DLGOK) export_quality = (int)xget(q, MUIA_Numeric_Value);
  else export_quality = 0;
  MUI_DisposeObject(w);
}
void dlg_new(void) { }
