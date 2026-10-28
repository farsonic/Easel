/*
 * dialogs.c - Easel's dialogs: New, Image Size, Canvas Size, Rotate, the adjustments and filters
 * (each shown on the picture as its sliders move, and taken back on Cancel), Levels with its
 * histogram, the Curves editor, Layer Properties, Fill, Stroke, the selection's Modify items and
 * the JPEG quality asked for on export; (0.7) Color Range with its preview, Save and Load Selection.
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
#include "../core/filters2.h"
#include "../core/layers.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "../core/codec.h"
#include "../core/selx.h"
#include "../core/channels.h"
#include "../core/util.h"

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
/* (not after Abort was pressed: that is why the operation stopped) */
static void out_of_memory(void) { if (!progress_aborted) ask("OK", "%s", tr("There isn't enough memory for that.")); }

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
/* the starting values of the sliders, list and tick (set before a dialog to show a layer's settings) */
static LONG sd_init_v[6]; static int sd_init_set, sd_init_choice, sd_init_tick = -1;
static int sd_remember;              /* the dialog is a filter's: remembered for Last Filter */
/* Filter > Last Filter: the last filter applied through a dialog, with its settings */
static ApplyFn last_fn; static LONG last_v[6]; static int last_choice, last_tick; static ULONG last_plain;
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
  job_begin(sd.pv.d, NULL);                 /* (0.8: progress and Abort; an aborted preview leaves nothing) */
  sd.fn(sd.pv.d, sd.v, sd.cyc ? (int)xget(sd.cyc, MUIA_Cycle_Active) : 0, sd.chk ? (int)xget(sd.chk, MUIA_Selected) : 0);
  job_end();
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
    DoMethod(grp, OM_ADDMEMBER, (ULONG)(sd.cyc = cycle(choices, sd_init_set ? sd_init_choice : 0)));
  }
  for (i = 0; i < n; i++) {
    DoMethod(grp, OM_ADDMEMBER, (ULONG)QLabel(tr(p[i].label), MUIO_Label_Centered));
    DoMethod(grp, OM_ADDMEMBER, (ULONG)(sd.sl[i] = SliderObject, MUIA_Numeric_Min, p[i].min, MUIA_Numeric_Max, p[i].max,
                                         MUIA_Numeric_Value, sd_init_set && i < 6 ? sd_init_v[i] : p[i].def,
                                         MUIA_Numeric_Default, p[i].def, MUIA_CycleChain, 1, MUIA_FixWidthTxt, (ULONG)"MMMMMMMMMMMMMMMMMMMMMM", End));
  }
  if (ticklabel) {
    DoMethod(grp, OM_ADDMEMBER, (ULONG)(sd.chk = check(sd_init_tick >= 0 ? sd_init_tick : tickdef)));
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
  if (ok && sd_remember) {
    sd_read();
    last_fn = fn; memcpy(last_v, sd.v, sizeof last_v); last_plain = 0;
    last_choice = sd.cyc ? (int)xget(sd.cyc, MUIA_Cycle_Active) : 0; last_tick = sd.chk ? (int)xget(sd.chk, MUIA_Selected) : 0;
  }
  if (!ok) pv_drop(&sd.pv);
  MUI_DisposeObject(w);
  DoMethod(canvas, MUIM_Canvas_Refresh);
  return ok;
}

/* each adjustment's call with the sliders' values. Each builds the adjustment's settings and hands
   them to put_adj: on an adjustment layer of that kind they become its settings (0.3), on a pixel
   layer they're applied to its pixels. Either way it's one undo step, so the preview works alike */
static AdjParams adj_base;           /* the settings the dialog started from (the layer's, or defaults) */
static int adj_editing;              /* the active layer is an adjustment layer being edited */
static void put_adj(Doc *d, const AdjParams *p)
{
  if (adj_editing && d->lay[d->active]->kind == LK_ADJUST) doc_set_adjust(d, d->active, p);
  else adj_apply(d, p, adj_names[p->kind]);
}
static void a_bc(Doc *d, const LONG *v, int c, int t) { AdjParams p = adj_base; (void)c; (void)t; p.v[0] = (int)v[0]; p.v[1] = (int)v[1]; put_adj(d, &p); }
static void a_exposure(Doc *d, const LONG *v, int c, int t) { AdjParams p = adj_base; (void)c; (void)t; p.v[0] = (int)v[0]; p.v[1] = (int)v[1]; p.v[2] = (int)v[2]; put_adj(d, &p); }
static void a_vibrance(Doc *d, const LONG *v, int c, int t) { AdjParams p = adj_base; (void)c; (void)t; p.v[0] = (int)v[0]; p.v[1] = (int)v[1]; put_adj(d, &p); }
static void a_huesat(Doc *d, const LONG *v, int c, int t) { AdjParams p = adj_base; (void)c; p.v[0] = (int)v[0]; p.v[1] = (int)v[1]; p.v[2] = (int)v[2]; p.v[3] = t; put_adj(d, &p); }
static void a_bw(Doc *d, const LONG *v, int c, int t) { AdjParams p = adj_base; (void)c; (void)t; p.v[0] = (int)v[0]; p.v[1] = (int)v[1]; p.v[2] = (int)v[2]; put_adj(d, &p); }
static void a_posterize(Doc *d, const LONG *v, int c, int t) { AdjParams p = adj_base; (void)c; (void)t; p.v[0] = (int)v[0]; put_adj(d, &p); }
static void a_threshold(Doc *d, const LONG *v, int c, int t) { AdjParams p = adj_base; (void)c; (void)t; p.v[0] = (int)v[0]; put_adj(d, &p); }
static void a_balance(Doc *d, const LONG *v, int c, int t)
{
  AdjParams p = adj_base;
  int k;
  if (!adj_editing) for (k = 0; k < 9; k++) p.v[k] = 0;  /* (applied to pixels: one tone range at a time) */
  for (k = 0; k < 3; k++) p.v[(c >= 0 && c < 3 ? c : 0) * 3 + k] = (int)v[k];
  p.v[9] = t;
  put_adj(d, &p);
}
static void a_photo(Doc *d, const LONG *v, int c, int t)
{
  static const ULONG cols[] = { 0xEC8A00, 0xFA9600, 0x006DFF, 0x005DFF, 0xEA1A1A, 0x19C900, 0x6B0ACD };
  AdjParams p = adj_base;
  p.v[0] = (int)cols[c >= 0 && c < 7 ? c : 0]; p.v[1] = (int)v[0]; p.v[2] = t;
  put_adj(d, &p);
}
/* Levels: the five numbers and the channel; Curves: the editor's points */
static LONG lev[3] = { 0, 255, 100 };
static Object *lev_graph;
static u32 lev_hist[1024];
static void a_levels(Doc *d, const LONG *v, int c, int t)
{
  AdjParams p = adj_base;
  int q[5], k;
  (void)t;
  lev[0] = v[0]; lev[1] = v[2] > v[0] ? v[2] : v[0] + 1; lev[2] = v[1];
  if (lev_graph) set(lev_graph, MUIA_Graph_Hist, (ULONG)(lev_hist + 256 * c));
  q[0] = (int)v[0]; q[1] = (int)lev[1]; q[2] = (int)v[1]; q[3] = (int)v[3]; q[4] = (int)v[4];
  if (c == 0) for (k = 0; k < 5; k++) p.v[k] = q[k];
  else {
    if (p.npts < 15) { for (k = 0; k < 3; k++) { p.pts[k * 5] = 0; p.pts[k * 5 + 1] = 255; p.pts[k * 5 + 2] = 100; p.pts[k * 5 + 3] = 0; p.pts[k * 5 + 4] = 255; } p.npts = 15; }
    for (k = 0; k < 5; k++) p.pts[(c - 1) * 5 + k] = q[k];
  }
  put_adj(d, &p);
}
static Object *curve_obj;
static void a_curves(Doc *d, const LONG *v, int c, int t)
{
  struct GData *g = INST_DATA(gcls->mcc_Class, curve_obj);
  AdjParams p = adj_base;
  int k;
  (void)v; (void)t;
  p.v[0] = c; p.npts = g->npts > 32 ? 32 : g->npts;
  for (k = 0; k < p.npts * 2; k++) p.pts[k] = g->pts[k];
  put_adj(d, &p);
}
/* Channel Mixer: one output channel's row at a time */
static void a_mixer(Doc *d, const LONG *v, int c, int t)
{
  AdjParams p = adj_base;
  int row = c >= 0 && c < 3 ? c : 0, k;
  for (k = 0; k < 4; k++) p.v[row * 4 + k] = (int)v[k];
  p.v[12] = t;
  put_adj(d, &p);
}
/* Gradient Map: a preset gradient (the colours in use, black to white, and some classic ramps) */
static void gradmap_preset(int k, AdjParams *p)
{
  Doc *d = cur_doc();
  static const ULONG sets[][4] = { { 0x000000, 0xFFFFFF, 0, 0 }, { 0x000000, 0x800000, 0xFFA000, 0xFFFFE0 }, { 0x001040, 0x2080C0, 0xE0F0FF, 0 },
                                   { 0x200040, 0xC02060, 0xFFE080, 0 }, { 0x402000, 0xC08040, 0xFFF0D0, 0 } };
  int n, i;
  p->npts = 0;
  if (k == 0 && d) { p->pts[0] = 0; p->pts[1] = (int)d->fg; p->pts[2] = 255; p->pts[3] = (int)d->bg; p->npts = 2; return; }
  k = k - 1;
  if (k < 0 || k > 4) k = 0;
  for (n = 0; n < 4 && (n < 2 || sets[k][n]); n++) continue;
  for (i = 0; i < n; i++) { p->pts[2 * i] = i * 255 / (n - 1); p->pts[2 * i + 1] = (int)sets[k][i]; }
  p->npts = n;
}
static void a_gradmap(Doc *d, const LONG *v, int c, int t)
{
  AdjParams p = adj_base;
  (void)v;
  gradmap_preset(c, &p);
  p.v[0] = t;
  put_adj(d, &p);
}
/* Selective Color: one colour range at a time (cyan, magenta, yellow, black) */
static void a_selcolor(Doc *d, const LONG *v, int c, int t)
{
  AdjParams p = adj_base;
  int r = c >= 0 && c < 9 ? c : 0, k;
  for (k = 0; k < 4; k++) p.pts[r * 4 + k] = (int)v[k];
  p.v[0] = t;
  put_adj(d, &p);
}
/* the adjustment a menu item stands for */
static int adj_kind_of(ULONG id)
{
  switch (id) {
  case ID_ADJ_BC: return AK_BRIGHTNESS;
  case ID_ADJ_LEVELS: return AK_LEVELS;
  case ID_ADJ_CURVES: return AK_CURVES;
  case ID_ADJ_EXPOSURE: return AK_EXPOSURE;
  case ID_ADJ_VIBRANCE: return AK_VIBRANCE;
  case ID_ADJ_HUESAT: return AK_HUESAT;
  case ID_ADJ_BALANCE: return AK_COLORBALANCE;
  case ID_ADJ_BW: return AK_BLACKWHITE;
  case ID_ADJ_PHOTO: return AK_PHOTOFILTER;
  case ID_ADJ_MIXER: return AK_CHANNELMIXER;
  case ID_ADJ_INVERT: return AK_INVERT;
  case ID_ADJ_POSTERIZE: return AK_POSTERIZE;
  case ID_ADJ_THRESHOLD: return AK_THRESHOLD;
  case ID_ADJ_GRADMAP: return AK_GRADIENTMAP;
  case ID_ADJ_SELCOLOR: return AK_SELECTIVECOLOR;
  default: return -1;
  }
}
ULONG adj_menu_id(int kind)
{
  static const ULONG ids[AK_N] = { ID_ADJ_BC, ID_ADJ_LEVELS, ID_ADJ_CURVES, ID_ADJ_EXPOSURE, ID_ADJ_VIBRANCE, ID_ADJ_HUESAT, ID_ADJ_BALANCE,
    ID_ADJ_BW, ID_ADJ_PHOTO, ID_ADJ_MIXER, ID_ADJ_INVERT, ID_ADJ_POSTERIZE, ID_ADJ_THRESHOLD, ID_ADJ_GRADMAP, ID_ADJ_SELCOLOR };
  return kind >= 0 && kind < AK_N ? ids[kind] : 0;
}
void dlg_adjust(ULONG id)
{
  Doc *d = cur_doc();
  static const char *const ranges[] = { "Shadows", "Midtones", "Highlights", NULL };
  static const char *const channels[] = { "RGB", "Red", "Green", "Blue", NULL };
  static const char *const outs[] = { "Red", "Green", "Blue", NULL };
  static const char *const photos[] = { "Warming (85)", "Warming (LBA)", "Cooling (80)", "Cooling (LBB)", "Red", "Green", "Violet", NULL };
  static const char *const gmaps[] = { "Foreground to Background", "Black to White", "Fire", "Ocean", "Sunset", "Sepia", NULL };
  static const char *const scolors[] = { "Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas", "Whites", "Neutrals", "Blacks", NULL };
  int kind = adj_kind_of(id);
  Layer *l;
  if (!d || kind < 0) return;
  l = d->lay[d->active];
  adj_editing = l->kind == LK_ADJUST && l->adj.kind == kind;
  if (adj_editing) adj_base = l->adj; else adj_defaults(kind, &adj_base);
  /* a layer's settings shown as the starting values */
  sd_init_set = adj_editing; sd_init_choice = 0; sd_init_tick = -1;
  if (adj_editing) {
    const int *v = l->adj.v;
    int k;
    for (k = 0; k < 6; k++) sd_init_v[k] = v[k];
    switch (kind) {
    case AK_HUESAT: sd_init_tick = v[3]; break;
    case AK_COLORBALANCE: sd_init_v[0] = v[3]; sd_init_v[1] = v[4]; sd_init_v[2] = v[5]; sd_init_choice = 1; sd_init_tick = v[9]; break;
    case AK_PHOTOFILTER: sd_init_v[0] = v[1]; sd_init_tick = v[2]; break;
    case AK_LEVELS: sd_init_v[0] = v[0]; sd_init_v[1] = v[2] ? v[2] : 100; sd_init_v[2] = v[1] ? v[1] : 255; sd_init_v[3] = v[3]; sd_init_v[4] = v[4] ? v[4] : 255; break;
    case AK_CHANNELMIXER: for (k = 0; k < 4; k++) sd_init_v[k] = v[k]; sd_init_tick = v[12]; break;
    case AK_GRADIENTMAP: sd_init_tick = v[0]; break;
    case AK_SELECTIVECOLOR: for (k = 0; k < 4; k++) sd_init_v[k] = l->adj.pts[k]; sd_init_tick = v[0]; break;
    default: break;
    }
  }
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
  case ID_ADJ_MIXER: { static const Param p[] = { { "Red (%):", -200, 200, 100 }, { "Green (%):", -200, 200, 0 }, { "Blue (%):", -200, 200, 0 }, { "Constant (%):", -200, 200, 0 } };
    slider_dialog("Channel Mixer", p, 4, outs, "Output Channel:", "Monochrome", 0, a_mixer, NULL); break; }
  case ID_ADJ_GRADMAP: slider_dialog("Gradient Map", NULL, 0, gmaps, "Gradient:", "Reverse", 0, a_gradmap, NULL); break;
  case ID_ADJ_SELCOLOR: { static const Param p[] = { { "Cyan (%):", -100, 100, 0 }, { "Magenta (%):", -100, 100, 0 }, { "Yellow (%):", -100, 100, 0 }, { "Black (%):", -100, 100, 0 } };
    slider_dialog("Selective Color", p, 4, scolors, "Colors:", "Absolute", 0, a_selcolor, NULL); break; }
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
    if (adj_editing && l->adj.npts >= 2) {
      struct GData *g = INST_DATA(gcls->mcc_Class, curve_obj);
      int k;
      g->npts = l->adj.npts;
      for (k = 0; k < l->adj.npts * 2; k++) g->pts[k] = l->adj.pts[k];
    }
    notify_change(curve_obj, MUIA_Graph_Changed);
    slider_dialog("Curves", NULL, 0, channels, "Channel:", NULL, 0, a_curves, VGroup, Child, curve_obj,
                  Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Click the line to add a point, drag points to bend it;\ndrag a point out of the box to take it away."), MUIA_Font, MUIV_Font_Tiny, End, End);
    break; }
  case ID_ADJ_INVERT: if (!adj_editing) adj_invert(d); break;
  case ID_ADJ_DESAT: adj_desaturate(d); break;
  case ID_ADJ_EQUALIZE: adj_equalize(d); break;
  }
  sd_init_set = 0; sd_init_tick = -1; sd_init_choice = 0;
  adj_editing = 0;
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
/* 0.2.1's filters (filters2.c) */
static void f_dust(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_dust_scratches(d, (int)v[0], (int)v[1]); }
static void f_rnoise(Doc *d, const LONG *v, int c, int t) { (void)c; flt_reduce_noise(d, (int)v[0], (int)v[1], t); }
static void f_smart(Doc *d, const LONG *v, int c, int t) { (void)t; flt_smart_blur(d, (int)v[0], (int)v[1], c); }
static void f_surface(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_surface_blur(d, (int)v[0], (int)v[1]); }
static void f_lens(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_lens_blur(d, (int)v[0], (int)v[1], (int)v[2]); }
static void f_shape(Doc *d, const LONG *v, int c, int t) { (void)t; flt_shape_blur(d, (int)v[0], c); }
static void f_spin(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_spin_blur(d, (i32)(d->w * v[0] / 100), (i32)(d->h * v[1] / 100), (int)v[2]); }
static void f_polar(Doc *d, const LONG *v, int c, int t) { (void)v; (void)t; flt_polar_coordinates(d, c == 0); }
static void f_shear(Doc *d, const LONG *v, int c, int t)
{
  int pts[6];
  (void)c;
  pts[0] = 0; pts[1] = 128 + (int)v[0]; pts[2] = 128; pts[3] = 128 + (int)v[1]; pts[4] = 255; pts[5] = 128 + (int)v[2];
  flt_shear(d, pts, 3, t);
}
static void f_zigzag(Doc *d, const LONG *v, int c, int t) { (void)t; flt_zigzag(d, (int)v[0], (int)v[1], c); }
static void f_glass(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_glass(d, (int)v[0], (int)v[1], (int)v[2], 1234); }
static void f_ocean(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_ocean_ripple(d, (int)v[0], (int)v[1], 4321); }
static void f_crystal(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_crystallize(d, (int)v[0], 777); }
static void f_pointil(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_pointillize(d, (int)v[0], 778); }
static void f_halftone(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_color_halftone(d, (int)v[0], (int)v[1], (int)v[2], (int)v[3]); }
static void f_mezzo(Doc *d, const LONG *v, int c, int t) { (void)v; (void)t; flt_mezzotint(d, c, 991); }
static void f_contour(Doc *d, const LONG *v, int c, int t) { (void)t; flt_trace_contour(d, (int)v[0], c == 0); }
static void f_diffuse(Doc *d, const LONG *v, int c, int t) { (void)v; (void)t; flt_diffuse(d, c, 555); }
static void f_tiles(Doc *d, const LONG *v, int c, int t) { (void)t; flt_tiles(d, (int)v[0], (int)v[1], c, 556); }
static void f_wind(Doc *d, const LONG *v, int c, int t) { (void)v; flt_wind(d, c, t, 557); }
static void f_fibers(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_fibers(d, (int)v[0], (int)v[1], (unsigned)v[2]); }
static void f_flare(Doc *d, const LONG *v, int c, int t) { (void)t; flt_lens_flare(d, (i32)(d->w * v[0] / 100), (i32)(d->h * v[1] / 100), (int)v[2], c); }
static void f_lighting(Doc *d, const LONG *v, int c, int t) { (void)c; (void)t; flt_lighting_basic(d, (i32)(d->w * v[0] / 100), (i32)(d->h * v[1] / 100), (int)(v[2] * (d->w > d->h ? d->w : d->h) / 100), (int)v[3], 0xFFFFFF, (int)v[4]); }
static void f_deint(Doc *d, const LONG *v, int c, int t) { (void)v; flt_de_interlace(d, c == 1, t); }
static void f_custom(Doc *d, const LONG *v, int c, int t)
{
  /* the 3 x 3 middle of the 5 x 5 kernel from the sliders: centre, edges, corners */
  int k[25], i;
  (void)c; (void)t;
  for (i = 0; i < 25; i++) k[i] = 0;
  k[12] = (int)v[0]; k[7] = k[11] = k[13] = k[17] = (int)v[1]; k[6] = k[8] = k[16] = k[18] = (int)v[2];
  flt_custom(d, k, (int)v[3], (int)v[4]);
}
/* Filter > Last Filter: the last one again, with the same settings and no dialog */
void dlg_filter_again(void)
{
  Doc *d = cur_doc();
  if (!d) return;
  set(app, MUIA_Application_Sleep, TRUE);
  if (last_fn) last_fn(d, last_v, last_choice, last_tick);
  else if (last_plain) dlg_filter(last_plain);
  set(app, MUIA_Application_Sleep, FALSE);
}
void dlg_filter(ULONG id)
{
  Doc *d = cur_doc();
  static const char *const radial[] = { "Spin", "Zoom", NULL };
  static const char *const dist[] = { "Uniform", "Gaussian", NULL };
  static const char *const rsize[] = { "Small", "Medium", "Large", NULL };
  static const char *const smodes[] = { "Normal", "Edge Only", "Overlay Edge", NULL };
  static const char *const shapes[] = { "Square", "Diamond", "Hexagon", "Triangle", "Disc", NULL };
  static const char *const polar[] = { "Rectangular to Polar", "Polar to Rectangular", NULL };
  static const char *const zz[] = { "Around Centre", "Out from Centre", "Pond Ripples", NULL };
  static const char *const mezz[] = { "Fine Dots", "Medium Dots", "Grainy Dots", "Coarse Dots", "Short Lines", "Medium Lines", "Long Lines", "Short Strokes", "Medium Strokes", "Long Strokes", NULL };
  static const char *const edges[] = { "Upper", "Lower", NULL };
  static const char *const diffs[] = { "Normal", "Darken Only", "Lighten Only", "Anisotropic", NULL };
  static const char *const tfill[] = { "Background Colour", "Foreground Colour", "Inverse Image", "Unaltered Image", NULL };
  static const char *const winds[] = { "Wind", "Blast", "Stagger", NULL };
  static const char *const lenses[] = { "50-300mm Zoom", "35mm Prime", "105mm Prime", "Movie Prime", NULL };
  static const char *const deint[] = { "Odd Fields", "Even Fields", NULL };
  if (!d) return;
  sd_remember = 1;
  if (id == ID_F_BLUR || id == ID_F_SHARPEN || id == ID_F_SHARPENMORE || id == ID_F_DESPECKLE || id == ID_F_EDGES || id == ID_F_SOLARIZE ||
      id == ID_F_CLOUDS || id == ID_F_DIFFCLOUDS || id == ID_F_FACET || id == ID_F_FRAGMENT || id == ID_F_NTSC) { last_fn = NULL; last_plain = id; }
  switch (id) {
  case ID_F_DUST: { static const Param p[] = { { "Radius (px):", 1, 100, 1 }, { "Threshold:", 0, 255, 0 } }; slider_dialog("Dust & Scratches", p, 2, NULL, NULL, NULL, 0, f_dust, NULL); break; }
  case ID_F_REDUCENOISE: { static const Param p[] = { { "Strength:", 0, 10, 6 }, { "Preserve Details (%):", 0, 100, 60 } }; slider_dialog("Reduce Noise", p, 2, NULL, NULL, "Reduce Colour Noise", 1, f_rnoise, NULL); break; }
  case ID_F_SMARTBLUR: { static const Param p[] = { { "Radius (px):", 1, 100, 3 }, { "Threshold:", 1, 100, 25 } }; slider_dialog("Smart Blur", p, 2, smodes, "Mode:", NULL, 0, f_smart, NULL); break; }
  case ID_F_SURFACEBLUR: { static const Param p[] = { { "Radius (px):", 1, 100, 5 }, { "Threshold:", 2, 255, 15 } }; slider_dialog("Surface Blur", p, 2, NULL, NULL, NULL, 0, f_surface, NULL); break; }
  case ID_F_LENSBLUR: { static const Param p[] = { { "Radius (px):", 1, 100, 15 }, { "Specular Brightness:", 0, 100, 0 }, { "Threshold:", 0, 255, 255 } }; slider_dialog("Lens Blur", p, 3, NULL, NULL, NULL, 0, f_lens, NULL); break; }
  case ID_F_SHAPEBLUR: { static const Param p[] = { { "Radius (px):", 1, 100, 10 } }; slider_dialog("Shape Blur", p, 1, shapes, "Shape:", NULL, 0, f_shape, NULL); break; }
  case ID_F_SPINBLUR: { static const Param p[] = { { "Centre X (%):", 0, 100, 50 }, { "Centre Y (%):", 0, 100, 50 }, { "Blur Angle:", 1, 360, 15 } }; slider_dialog("Spin Blur", p, 3, NULL, NULL, NULL, 0, f_spin, NULL); break; }
  case ID_F_POLAR: slider_dialog("Polar Coordinates", NULL, 0, polar, "Conversion:", NULL, 0, f_polar, NULL); break;
  case ID_F_SHEAR: { static const Param p[] = { { "Top Shift:", -127, 127, 40 }, { "Middle Shift:", -127, 127, 0 }, { "Bottom Shift:", -127, 127, -40 } }; slider_dialog("Shear", p, 3, NULL, NULL, "Wrap Around", 1, f_shear, NULL); break; }
  case ID_F_ZIGZAG: { static const Param p[] = { { "Amount:", -100, 100, 10 }, { "Ridges:", 1, 20, 5 } }; slider_dialog("ZigZag", p, 2, zz, "Style:", NULL, 0, f_zigzag, NULL); break; }
  case ID_F_GLASS: { static const Param p[] = { { "Distortion:", 0, 20, 5 }, { "Smoothness:", 1, 15, 3 }, { "Scaling (%):", 50, 200, 100 } }; slider_dialog("Glass", p, 3, NULL, NULL, NULL, 0, f_glass, NULL); break; }
  case ID_F_OCEAN: { static const Param p[] = { { "Ripple Size:", 1, 15, 9 }, { "Ripple Magnitude:", 0, 20, 9 } }; slider_dialog("Ocean Ripple", p, 2, NULL, NULL, NULL, 0, f_ocean, NULL); break; }
  case ID_F_CRYSTALLIZE: { static const Param p[] = { { "Cell Size:", 3, 300, 10 } }; slider_dialog("Crystallize", p, 1, NULL, NULL, NULL, 0, f_crystal, NULL); break; }
  case ID_F_POINTILLIZE: { static const Param p[] = { { "Cell Size:", 3, 300, 5 } }; slider_dialog("Pointillize", p, 1, NULL, NULL, NULL, 0, f_pointil, NULL); break; }
  case ID_F_HALFTONE: { static const Param p[] = { { "Max. Radius (px):", 4, 127, 8 }, { "Red Angle:", -360, 360, 108 }, { "Green Angle:", -360, 360, 162 }, { "Blue Angle:", -360, 360, 90 } };
    slider_dialog("Color Halftone", p, 4, NULL, NULL, NULL, 0, f_halftone, NULL); break; }
  case ID_F_MEZZOTINT: slider_dialog("Mezzotint", NULL, 0, mezz, "Type:", NULL, 0, f_mezzo, NULL); break;
  case ID_F_CONTOUR: { static const Param p[] = { { "Level:", 0, 255, 128 } }; slider_dialog("Trace Contour", p, 1, edges, "Edge:", NULL, 0, f_contour, NULL); break; }
  case ID_F_DIFFUSE: slider_dialog("Diffuse", NULL, 0, diffs, "Mode:", NULL, 0, f_diffuse, NULL); break;
  case ID_F_TILES: { static const Param p[] = { { "Number of Tiles:", 1, 99, 10 }, { "Maximum Offset (%):", 1, 99, 10 } }; slider_dialog("Tiles", p, 2, tfill, "Fill Empty Area With:", NULL, 0, f_tiles, NULL); break; }
  case ID_F_WIND: slider_dialog("Wind", NULL, 0, winds, "Method:", "From the Left", 0, f_wind, NULL); break;
  case ID_F_FIBERS: { static const Param p[] = { { "Variance:", 1, 64, 16 }, { "Strength:", 1, 64, 4 }, { "Seed:", 1, 9999, 1 } }; slider_dialog("Fibers", p, 3, NULL, NULL, NULL, 0, f_fibers, NULL); break; }
  case ID_F_FLARE: { static const Param p[] = { { "Centre X (%):", 0, 100, 30 }, { "Centre Y (%):", 0, 100, 30 }, { "Brightness (%):", 10, 300, 100 } };
    slider_dialog("Lens Flare", p, 3, lenses, "Lens Type:", NULL, 0, f_flare, NULL); break; }
  case ID_F_LIGHTING: { static const Param p[] = { { "Light X (%):", 0, 100, 35 }, { "Light Y (%):", 0, 100, 35 }, { "Radius (%):", 5, 200, 60 }, { "Intensity:", 0, 100, 60 }, { "Ambience:", 0, 100, 20 } };
    slider_dialog("Lighting Effects", p, 5, NULL, NULL, NULL, 0, f_lighting, NULL); break; }
  case ID_F_DEINTERLACE: slider_dialog("De-Interlace", NULL, 0, deint, "Eliminate:", "Interpolate (else duplicate)", 1, f_deint, NULL); break;
  case ID_F_CUSTOM: { static const Param p[] = { { "Centre:", -99, 99, 5 }, { "Edges:", -99, 99, -1 }, { "Corners:", -99, 99, 0 }, { "Scale:", 1, 99, 1 }, { "Offset:", -255, 255, 0 } };
    slider_dialog("Custom", p, 5, NULL, NULL, NULL, 0, f_custom, NULL); break; }
  case ID_F_DIFFCLOUDS: flt_difference_clouds(d, d->fg, d->bg, (unsigned)rand()); break;
  case ID_F_FACET: flt_facet(d); break;
  case ID_F_FRAGMENT: flt_fragment(d); break;
  case ID_F_NTSC: flt_ntsc_colors(d, 0); break;
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
  sd_remember = 0;
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
  "Color Dodge", "Color Burn", "Soft Light", "Hard Light", "Exclusion", "Hue", "Saturation", "Color", "Luminosity",
  "Dissolve", "Linear Burn", "Darker Color", "Lighter Color", "Vivid Light", "Linear Light", "Pin Light", "Hard Mix", "Divide", NULL };
static const char *const gblends[] = { "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten", "Difference", "Add", "Subtract",
  "Color Dodge", "Color Burn", "Soft Light", "Hard Light", "Exclusion", "Hue", "Saturation", "Color", "Luminosity",
  "Dissolve", "Linear Burn", "Darker Color", "Lighter Color", "Vivid Light", "Linear Light", "Pin Light", "Hard Mix", "Divide", "Pass Through", NULL };
/* Layer Properties: name, opacity and fill, blend mode (Pass Through for a group), visibility,
   clipping, Blend If, and the mask's density and feather. One undo step */
void dlg_layer_props(void)
{
  Doc *d = cur_doc();
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *ns, *os, *fs, *bc, *vc, *cc, *b0, *b1, *b2, *b3, *md = NULL, *mf = NULL, *grp;
  Layer *l;
  char name[200];
  int ok, isgroup;
  if (!d) return;
  l = d->lay[d->active];
  isgroup = l->kind == LK_GROUP;
  utf8_to_latin1(l->name, (u32)strlen(l->name), name, sizeof name);
  grp = ColGroup(2),
        Child, QLabel(tr("Name:"), MUIO_Label_SingleFrame),
        Child, ns = StringObject, StringFrame, MUIA_String_Contents, (ULONG)name, MUIA_String_MaxLen, 60, MUIA_CycleChain, 1, End,
        Child, QLabel(tr("Opacity (%):"), MUIO_Label_Centered), Child, os = slider(0, 100, (l->opacity * 100 + 127) / 255),
        Child, QLabel(tr("Fill Opacity (%):"), MUIO_Label_Centered), Child, fs = slider(0, 100, ((255 - l->fill_cut) * 100 + 127) / 255),
        Child, QLabel(tr("Blend Mode:"), MUIO_Label_Centered),
        Child, bc = cycle(isgroup ? gblends : blends, l->blend == BM_PASS ? BM_N : l->blend < BM_N ? l->blend : 0),
        Child, QLabel(tr("Blend If This Layer:"), MUIO_Label_Centered),
        Child, HGroup, Child, b0 = slider(0, 255, l->blendif[0]), Child, b1 = slider(0, 255, 255 - l->blendif[1]), End,
        Child, QLabel(tr("Underlying Layer:"), MUIO_Label_Centered),
        Child, HGroup, Child, b2 = slider(0, 255, l->blendif[2]), Child, b3 = slider(0, 255, 255 - l->blendif[3]), End,
        Child, vc = check(l->visible), Child, QLabel(tr("Visible"), 0),
        Child, cc = check(l->clip), Child, QLabel(tr("Clipped to the layer below"), 0),
      End;
  if (grp && l->mask) {
    DoMethod(grp, OM_ADDMEMBER, (ULONG)QLabel(tr("Mask Density (%):"), MUIO_Label_Centered));
    DoMethod(grp, OM_ADDMEMBER, (ULONG)(md = slider(0, 100, ((255 - l->mask_fade) * 100 + 127) / 255)));
    DoMethod(grp, OM_ADDMEMBER, (ULONG)QLabel(tr("Mask Feather (px):"), MUIO_Label_Centered));
    DoMethod(grp, OM_ADDMEMBER, (ULONG)(mf = slider(0, 250, l->mask_feather)));
  }
  w = dialog_window("Layer Properties", grp, NULL);
  if (!w) return;
  set(w, MUIA_Window_ActiveObject, (ULONG)ns);
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  if (ok) {
    char u[200];
    const char *s = (const char *)xget(ns, MUIA_String_Contents);
    size_t o = 0;
    int bl = (int)xget(bc, MUIA_Cycle_Active);
    for (; *s && o + 3 < sizeof u; s++) {
      UBYTE c = (UBYTE)*s;
      if (c < 0x80) u[o++] = (char)c; else { u[o++] = (char)(0xC0 | (c >> 6)); u[o++] = (char)(0x80 | (c & 63)); }
    }
    u[o] = 0;
    doc_begin(d, "Layer Properties");
    doc_set_layer_props(d, d->active, u, (int)((xget(os, MUIA_Numeric_Value) * 255 + 50) / 100), isgroup && bl == BM_N ? BM_PASS : bl, (int)xget(vc, MUIA_Selected));
    l->fill_cut = (u8)(255 - (xget(fs, MUIA_Numeric_Value) * 255 + 50) / 100);
    l->blendif[0] = (u8)xget(b0, MUIA_Numeric_Value); l->blendif[1] = (u8)(255 - xget(b1, MUIA_Numeric_Value));
    l->blendif[2] = (u8)xget(b2, MUIA_Numeric_Value); l->blendif[3] = (u8)(255 - xget(b3, MUIA_Numeric_Value));
    if (!isgroup && (int)xget(cc, MUIA_Selected) != l->clip) doc_set_clip(d, d->active, (int)xget(cc, MUIA_Selected));
    if (md) doc_mask_set(d, d->active, -1, -1, (int)(255 - (xget(md, MUIA_Numeric_Value) * 255 + 50) / 100), (int)xget(mf, MUIA_Numeric_Value));
    doc_end(d);
  }
  MUI_DisposeObject(w);
}
/* a fill layer's settings: a solid colour, or a gradient from the colours in use (new: make one) */
int dlg_fill_layer(int make_kind)
{
  Doc *d = cur_doc();
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *const kinds[] = { "Solid Colour", "Gradient", NULL };
  static const char *const styles[] = { "Linear", "Radial", "Angle", "Reflected", "Diamond", NULL };
  static const char *const cols[] = { "Foreground to Background", "Background to Foreground", "Black to White", "Keep the Colours", NULL };
  Object *w, *kc, *sc, *cc, *as, *ss, *rc;
  FillParams f;
  int ok, editing;
  if (!d) return 0;
  editing = make_kind < 0 && d->lay[d->active]->kind == LK_FILL;
  if (make_kind < 0 && !editing) return 0;
  if (editing) f = d->lay[d->active]->fillp;
  else { memset(&f, 0, sizeof f); f.kind = (u8)make_kind; f.c0 = d->fg; f.c1 = d->bg; f.scale = 100; f.angle = 90; }
  if (f.kind == 0 && !editing) {
    LONG c = pick_color("Fill Layer Colour", d->fg);
    if (c < 0) return 0;
    f.c0 = (u32)c;
    return doc_new_fill(d, &f, "Color Fill 1") >= 0;
  }
  w = dialog_window(editing ? "Layer Content Options" : "Gradient Fill", ColGroup(2),
        Child, QLabel(tr("Fill:"), MUIO_Label_Centered), Child, kc = cycle(kinds, f.kind),
        Child, QLabel(tr("Colours:"), MUIO_Label_Centered), Child, cc = cycle(cols, editing ? 3 : 0),
        Child, QLabel(tr("Style:"), MUIO_Label_Centered), Child, sc = cycle(styles, f.gkind),
        Child, QLabel(tr("Angle:"), MUIO_Label_Centered), Child, as = slider(-180, 180, f.angle > 180 ? f.angle - 360 : f.angle),
        Child, QLabel(tr("Scale (%):"), MUIO_Label_Centered), Child, ss = slider(10, 150, f.scale ? f.scale : 100),
        Child, rc = check(f.reverse), Child, QLabel(tr("Reverse"), 0),
      End, NULL);
  if (!w) return 0;
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  if (ok) {
    int c = (int)xget(cc, MUIA_Cycle_Active);
    f.kind = (u8)xget(kc, MUIA_Cycle_Active);
    if (c == 0) { f.c0 = d->fg; f.c1 = d->bg; }
    else if (c == 1) { f.c0 = d->bg; f.c1 = d->fg; }
    else if (c == 2) { f.c0 = 0; f.c1 = 0xFFFFFF; }
    f.gkind = (int)xget(sc, MUIA_Cycle_Active);
    f.angle = (int)(LONG)xget(as, MUIA_Numeric_Value); if (f.angle < 0) f.angle += 360;
    f.scale = (int)xget(ss, MUIA_Numeric_Value);
    f.reverse = (int)xget(rc, MUIA_Selected);
    if (editing) doc_set_fill(d, d->active, &f);
    else doc_new_fill(d, &f, f.kind ? "Gradient Fill 1" : "Color Fill 1");
  }
  MUI_DisposeObject(w);
  return ok;
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
/* a Latin-1 string (from a gadget) as UTF-8 */
static void latin1_utf8(const char *s, char *out, size_t cap)
{
  size_t o = 0;
  for (; *s && o + 3 < cap; s++) {
    unsigned char c = (unsigned char)*s;
    if (c < 0x80) out[o++] = (char)c;
    else { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 0x3F)); }
  }
  out[o] = 0;
}
static Object *string_text(const char *utf8, int maxlen)
{
  char buf[520];
  utf8_to_latin1(utf8, strlen(utf8), buf, sizeof buf);
  return StringObject, StringFrame, MUIA_String_Contents, (ULONG)buf, MUIA_String_MaxLen, maxlen, MUIA_CycleChain, 1, End;
}
/* File > File Info: the picture's title, author, description, keywords, copyright and web address,
   kept in the files that carry them (PNG, JPEG, TIFF, PSD, ORA) */
void dlg_file_info(Doc *d)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *s[6];
  static const char *const labels[] = { "Title:", "Author:", "Description:", "Keywords:", "Copyright:", "Web address:" };
  char *fields[6];
  size_t caps[6];
  int i;
  fields[0] = d->info.title; caps[0] = sizeof d->info.title;
  fields[1] = d->info.author; caps[1] = sizeof d->info.author;
  fields[2] = d->info.description; caps[2] = sizeof d->info.description;
  fields[3] = d->info.keywords; caps[3] = sizeof d->info.keywords;
  fields[4] = d->info.copyright; caps[4] = sizeof d->info.copyright;
  fields[5] = d->info.url; caps[5] = sizeof d->info.url;
  w = dialog_window("File Info", ColGroup(2),
        Child, QLabel(tr(labels[0]), MUIO_Label_SingleFrame), Child, s[0] = string_text(fields[0], 120),
        Child, QLabel(tr(labels[1]), MUIO_Label_SingleFrame), Child, s[1] = string_text(fields[1], 120),
        Child, QLabel(tr(labels[2]), MUIO_Label_SingleFrame), Child, s[2] = string_text(fields[2], 500),
        Child, QLabel(tr(labels[3]), MUIO_Label_SingleFrame), Child, s[3] = string_text(fields[3], 250),
        Child, QLabel(tr(labels[4]), MUIO_Label_SingleFrame), Child, s[4] = string_text(fields[4], 250),
        Child, QLabel(tr(labels[5]), MUIO_Label_SingleFrame), Child, s[5] = string_text(fields[5], 250),
        Child, HSpace(0), Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Saved in PNG, JPEG, TIFF, PSD and ORA files; keywords separated by commas"), End,
      End, NULL);
  if (!w) return;
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    for (i = 0; i < 6; i++) latin1_utf8((const char *)xget(s[i], MUIA_String_Contents), fields[i], caps[i]);
    d->dirty = 1;
  }
  MUI_DisposeObject(w);
}
/* Edit > Fade: the last change mixed back by an opacity and blend mode */
void dlg_fade(void)
{
  Doc *d = cur_doc();
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *os, *bc;
  int ok, op, mode;
  if (!d) return;
  if (!doc_can_undo(d)) return;
  w = dialog_window("Fade", ColGroup(2),
        Child, QLabel(tr("Opacity (%):"), MUIO_Label_Centered), Child, os = slider(0, 100, 100),
        Child, QLabel(tr("Mode:"), MUIO_Label_Centered), Child, bc = cycle(blends, 0),
        Child, HSpace(0), Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Mixes the last change back with how it was before"), End,
      End, NULL);
  if (!w) return;
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  op = (int)xget(os, MUIA_Numeric_Value); mode = (int)xget(bc, MUIA_Cycle_Active);
  MUI_DisposeObject(w);
  if (!ok) return;
  if (!doc_fade(d, (op * 255 + 50) / 100, mode)) ask("OK", "%s", tr("The last step can't be faded: it didn't change pixels, or it changed the whole picture."));
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
/* ------------------------------------------------------------------ 0.7: Color Range, Save and Load Selection */
/* the Color Range preview: the mask it would make, small and grey (mv_rgb, mv_w x mv_h) */
static UBYTE *mv_rgb;
static LONG mv_w, mv_h;
static struct MUI_CustomClass *mvcls;
static ULONG mvdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  switch (msg->MethodID) {
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += mv_w; m->MinMaxInfo->DefWidth += mv_w; m->MinMaxInfo->MaxWidth += mv_w;
    m->MinMaxInfo->MinHeight += mv_h; m->MinMaxInfo->DefHeight += mv_h; m->MinMaxInfo->MaxHeight += mv_h;
    return 0; }
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if ((((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) && mv_rgb)
      draw_rgb(_rp(obj), _screen(obj), mv_rgb, mv_w * 3, _mleft(obj), _mtop(obj), mv_w, mv_h);
    return 0;
  }
  return DoSuperMethodA(cl, obj, msg);
}
/* the preview made again from Color Range's settings */
static void mv_make(Doc *d, const ColorRange *cr)
{
  u8 *m = color_range_mask(d, cr);
  LONG x, y;
  if (!m || !mv_rgb) { free(m); return; }
  for (y = 0; y < mv_h; y++)
    for (x = 0; x < mv_w; x++) {
      UBYTE v = m[(long)(y * d->h / mv_h) * d->w + (long)(x * d->w / mv_w)];
      mv_rgb[(y * mv_w + x) * 3] = mv_rgb[(y * mv_w + x) * 3 + 1] = mv_rgb[(y * mv_w + x) * 3 + 2] = v;
    }
  free(m);
}
static Object *cr_sel, *cr_fuzz, *cr_range, *cr_inv, *cr_view;
static ColorRange cr_set;
static int cr_dirty;
/* the dialog's settings read into cr_set */
static void cr_read(void)
{
  cr_set.preset = (int)xget(cr_sel, MUIA_Cycle_Active);
  cr_set.fuzziness = (int)xget(cr_fuzz, MUIA_Numeric_Value);
  cr_set.range = (int)xget(cr_range, MUIA_Numeric_Value);
  cr_set.invert = (int)xget(cr_inv, MUIA_Selected);
}
/* (run_modal's) a change marks the preview out of date; when idle it is made again */
static void cr_each(ULONG id)
{
  if (id == ID_DLGCHANGE) { cr_dirty = 1; return; }
  if (id || !cr_dirty) return;
  cr_dirty = 0;
  cr_read();
  mv_make(cur_doc(), &cr_set);
  if (cr_view) MUI_Redraw(cr_view, MADF_DRAWOBJECT);
}
/* Select > Color Range: the colours like the sampled one (the foreground colour) or a range of
   colours or tones, with fuzziness, made the selection; a preview shows what would be selected */
void dlg_color_range(void)
{
  Doc *d = cur_doc();
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *names[CR_N + 1];
  static int last_preset, last_fuzz = 40, last_range = 50, last_inv;
  char note[120];
  Object *w;
  int i, ok;
  if (!d) return;
  for (i = 0; i < CR_N; i++) names[i] = color_range_names[i];
  names[CR_N] = NULL;
  /* the preview fitted into 160 x 120 */
  if ((long)d->w * 120 > (long)d->h * 160) { mv_w = 160; mv_h = (LONG)((long)d->h * 160 / d->w); }
  else { mv_h = 120; mv_w = (LONG)((long)d->w * 120 / d->h); }
  if (mv_w < 1) mv_w = 1;
  if (mv_h < 1) mv_h = 1;
  mv_rgb = (UBYTE *)calloc((size_t)mv_w * mv_h, 3);
  if (!mvcls) mvcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, 4, (APTR)mvdispatch);
  memset(&cr_set, 0, sizeof cr_set);
  cr_set.colors[0] = d->fg; cr_set.ncolors = 1; cr_set.all_layers = 1;
  sprintf(note, tr("Sampled Colors: from the foreground colour, #%06lX\n(pick it with the Eyedropper first)"), (unsigned long)(d->fg & 0xFFFFFF));
  cr_view = mvcls && mv_rgb ? NewObject(mvcls->mcc_Class, NULL, TextFrame, MUIA_FillArea, FALSE, TAG_DONE) : NULL;
  w = dialog_window("Color Range", VGroup,
        Child, ColGroup(2),
          Child, QLabel(tr("Select:"), MUIO_Label_Centered), Child, cr_sel = cycle(names, last_preset),
          Child, QLabel(tr("Fuzziness:"), MUIO_Label_Centered), Child, cr_fuzz = slider(0, 200, last_fuzz),
          Child, QLabel(tr("Range (%):"), MUIO_Label_Centered), Child, cr_range = slider(1, 100, last_range),
          Child, QLabel(tr("Invert"), MUIO_Label_Centered), Child, HGroup, Child, cr_inv = check(last_inv), Child, HSpace(0), End,
        End,
        Child, TextObject, MUIA_Text_Contents, (ULONG)note, End,
        Child, HGroup, Child, HSpace(0), Child, cr_view ? cr_view : HSpace(0), Child, HSpace(0), End,
      End, NULL);
  if (!w) { free(mv_rgb); mv_rgb = NULL; return; }
  notify_change(cr_sel, MUIA_Cycle_Active); notify_change(cr_fuzz, MUIA_Numeric_Value); notify_change(cr_range, MUIA_Numeric_Value); notify_change(cr_inv, MUIA_Selected);
  cr_dirty = 1;
  ok = run_modal(w, ends, cr_each) == ID_DLGOK;
  cr_read();
  last_preset = cr_set.preset; last_fuzz = cr_set.fuzziness; last_range = cr_set.range; last_inv = cr_set.invert;
  MUI_DisposeObject(w);
  cr_view = NULL;
  free(mv_rgb); mv_rgb = NULL;
  if (!ok) return;
  set(app, MUIA_Application_Sleep, TRUE);
  sel_color_range_ex(d, &cr_set, SEL_REPLACE);
  set(app, MUIA_Application_Sleep, FALSE);
}
/* the channels' names in Latin-1 for a cycle gadget, after first (when not NULL); NULL ended */
static const char **chan_labels(Doc *d, const char *first, char names[][40])
{
  static const char *labels[MAXCHANNELS + 2];
  int i, n = 0;
  if (first) labels[n++] = first;
  for (i = 0; i < d->nchan; i++) { utf8_to_latin1(d->chan_name[i], strlen(d->chan_name[i]), names[i], 40); labels[n++] = names[i]; }
  labels[n] = NULL;
  return labels;
}
/* Select > Save Selection: as a new channel (named), or into one there combined by an operation */
void dlg_save_selection(void)
{
  Doc *d = cur_doc();
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *const ops[] = { "Replace Channel", "Add to Channel", "Subtract from Channel", "Intersect with Channel", NULL };
  static char names[MAXCHANNELS][40];
  char name[40], u[64];
  Object *w, *ch, *nm, *op;
  int ok, into, how, r;
  if (!d) return;
  sprintf(name, "Alpha %d", d->nchan + 1);
  w = dialog_window("Save Selection", ColGroup(2),
        Child, QLabel(tr("Channel:"), MUIO_Label_Centered), Child, ch = cycle(chan_labels(d, tr("New"), names), 0),
        Child, QLabel(tr("Name:"), MUIO_Label_SingleFrame), Child, nm = StringObject, StringFrame, MUIA_String_Contents, (ULONG)name, MUIA_String_MaxLen, 31, MUIA_CycleChain, 1, End,
        Child, QLabel(tr("Operation:"), MUIO_Label_Centered), Child, op = cycle(ops, 0),
        Child, HSpace(0), Child, TextObject, MUIA_Text_Contents, (ULONG)tr("The name is for a new channel; the operation for one already there.\nWith nothing selected, everything is saved."), End,
      End, NULL);
  if (!w) return;
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  into = (int)xget(ch, MUIA_Cycle_Active) - 1; how = (int)xget(op, MUIA_Cycle_Active);
  latin1_utf8((const char *)xget(nm, MUIA_String_Contents), u, sizeof u);
  u[31] = 0;
  MUI_DisposeObject(w);
  if (!ok) return;
  r = into < 0 ? chan_save_selection(d, -1, SEL_REPLACE, u) : chan_save_selection(d, into, how, NULL);
  if (r < 0) ask("OK", "%s", into < 0 && d->nchan >= MAXCHANNELS ? tr("A picture can keep 24 channels: delete one first.") : tr("There isn't enough memory for that."));
  else chan_pick = r;
}
/* Select > Load Selection: a channel (inverted or not) made the selection by an operation */
void dlg_load_selection(void)
{
  Doc *d = cur_doc();
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *const ops[] = { "New Selection", "Add to Selection", "Subtract from Selection", "Intersect with Selection", NULL };
  static char names[MAXCHANNELS][40];
  Object *w, *ch, *inv, *op;
  int ok, which, how, invert;
  if (!d) return;
  if (!d->nchan) { ask("OK", "%s", tr("There are no saved selections: Select > Save Selection keeps one as a channel.")); return; }
  w = dialog_window("Load Selection", ColGroup(2),
        Child, QLabel(tr("Channel:"), MUIO_Label_Centered), Child, ch = cycle(chan_labels(d, NULL, names), chan_pick >= 0 && chan_pick < d->nchan ? chan_pick : 0),
        Child, QLabel(tr("Invert"), MUIO_Label_Centered), Child, HGroup, Child, inv = check(0), Child, HSpace(0), End,
        Child, QLabel(tr("Operation:"), MUIO_Label_Centered), Child, op = cycle(ops, d->sel ? 0 : 0),
      End, "Load");
  if (!w) return;
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  which = (int)xget(ch, MUIA_Cycle_Active); how = (int)xget(op, MUIA_Cycle_Active); invert = (int)xget(inv, MUIA_Selected);
  MUI_DisposeObject(w);
  if (!ok) return;
  chan_load_selection(d, which, how, invert);
  chan_pick = which;
}

extern int export_quality;
void dlg_export(int fmt)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *q;
  if (fmt != FMT_JPEG) return;
  w = dialog_window("JPEG Options", ColGroup(2),
        Child, QLabel(tr("Quality:"), MUIO_Label_Centered), Child, q = slider(1, 100, prefs.jpeg_quality),
        Child, HSpace(0), Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Low quality makes small files; JPEG keeps no layers\nor transparency (it is laid over white)."), End,
      End, "Save");
  if (!w) return;
  if (run_modal(w, ends, NULL) == ID_DLGOK) export_quality = (int)xget(q, MUIA_Numeric_Value);
  else export_quality = 0;
  MUI_DisposeObject(w);
}
void dlg_new(void) { }
