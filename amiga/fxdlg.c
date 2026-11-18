/*
 * fxdlg.c - Easel 1.1's additions to the Layer menu: Layer > Layer Style (the Layer Style window:
 * the nine effects listed down the left, each with a tick to turn it on, and the settings of the one
 * chosen on the right, shown on the canvas as they change; Copy, Paste and Clear Layer Style, Global
 * Light, Create Layers, Hide All Effects, Scale Effects), Layer > Layer Color, Delete Hidden Layers,
 * Arrange > Reverse, New > Background from Layer, Copy CSS (to the clipboard as text) and Stack Mode.
 * The work is in the core (core/fx.c, core/layerops.c); main.c hands the menu IDs in fxids.h here.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "fxids.h"
#include "clipboard.h"
#include "../core/fx.h"
#include "../core/layerops.h"
#include "../core/layers.h"

static ULONG xget(Object *o, ULONG a) { ULONG v = 0; get(o, a, &v); return v; }

/* ------------------------------------------------------------------ the settings each effect shows */
enum { F_BLEND, F_OPACITY, F_STYLE, F_COLOR, F_COLOR2, F_ANGLE, F_DIST, F_SIZE, F_SPREAD, F_ALT, F_DEPTH,
       F_GLOBAL, F_INVERT, F_KNOCK, F_BLEND2, F_OPACITY2, F_DX, F_DY, F_END };
typedef struct { int f; const char *label; int min, max; const char *const *choices; } Ctl;
static const char *const bevel_styles[] = { "Inner Bevel", "Outer Bevel", "Emboss", "Pillow Emboss", NULL };
static const char *const stroke_pos[] = { "Outside", "Inside", "Center", NULL };
static const char *const glow_src[] = { "Edge", "Center", NULL };
static const char *const grad_styles[] = { "Linear", "Radial", "Angle", "Reflected", "Diamond", NULL };
#define MAXC 15
static const Ctl ctls[FX_N][MAXC] = {
  { { F_BLEND, "Blend Mode:" }, { F_COLOR, "Color:" }, { F_OPACITY, "Opacity (%):", 0, 100 }, { F_ANGLE, "Angle:", -180, 180 },
    { F_GLOBAL, "Use Global Light" }, { F_DIST, "Distance (px):", 0, 250 }, { F_SPREAD, "Spread (%):", 0, 100 },
    { F_SIZE, "Size (px):", 0, 250 }, { F_KNOCK, "Layer Knocks Out Drop Shadow" }, { F_END } },
  { { F_BLEND, "Blend Mode:" }, { F_COLOR, "Color:" }, { F_OPACITY, "Opacity (%):", 0, 100 }, { F_ANGLE, "Angle:", -180, 180 },
    { F_GLOBAL, "Use Global Light" }, { F_DIST, "Distance (px):", 0, 250 }, { F_SPREAD, "Choke (%):", 0, 100 },
    { F_SIZE, "Size (px):", 0, 250 }, { F_END } },
  { { F_BLEND, "Blend Mode:" }, { F_COLOR, "Color:" }, { F_OPACITY, "Opacity (%):", 0, 100 }, { F_SPREAD, "Spread (%):", 0, 100 },
    { F_SIZE, "Size (px):", 0, 250 }, { F_END } },
  { { F_BLEND, "Blend Mode:" }, { F_COLOR, "Color:" }, { F_OPACITY, "Opacity (%):", 0, 100 }, { F_STYLE, "Source:", 0, 1, glow_src },
    { F_SPREAD, "Choke (%):", 0, 100 }, { F_SIZE, "Size (px):", 0, 250 }, { F_END } },
  { { F_STYLE, "Style:", 0, 3, bevel_styles }, { F_DEPTH, "Depth (%):", 1, 1000 }, { F_INVERT, "Direction Down" },
    { F_SIZE, "Size (px):", 0, 250 }, { F_SPREAD, "Soften (px):", 0, 16 }, { F_ANGLE, "Angle:", -180, 180 }, { F_GLOBAL, "Use Global Light" },
    { F_ALT, "Altitude:", 0, 90 }, { F_BLEND, "Highlight Mode:" }, { F_COLOR, "Highlight Color:" }, { F_OPACITY, "Highlight Opacity (%):", 0, 100 },
    { F_BLEND2, "Shadow Mode:" }, { F_COLOR2, "Shadow Color:" }, { F_OPACITY2, "Shadow Opacity (%):", 0, 100 }, { F_END } },
  { { F_BLEND, "Blend Mode:" }, { F_COLOR, "Color:" }, { F_OPACITY, "Opacity (%):", 0, 100 }, { F_ANGLE, "Angle:", -180, 180 },
    { F_DIST, "Distance (px):", 0, 250 }, { F_SIZE, "Size (px):", 0, 250 }, { F_INVERT, "Invert" }, { F_END } },
  { { F_BLEND, "Blend Mode:" }, { F_COLOR, "Color:" }, { F_OPACITY, "Opacity (%):", 0, 100 }, { F_END } },
  { { F_BLEND, "Blend Mode:" }, { F_OPACITY, "Opacity (%):", 0, 100 }, { F_COLOR, "From:" }, { F_COLOR2, "To:" }, { F_INVERT, "Reverse" },
    { F_STYLE, "Style:", 0, 4, grad_styles }, { F_ANGLE, "Angle:", -180, 180 }, { F_DEPTH, "Scale (%):", 10, 150 },
    { F_DX, "Offset across (px):", -500, 500 }, { F_DY, "Offset down (px):", -500, 500 }, { F_END } },
  { { F_SIZE, "Size (px):", 1, 250 }, { F_STYLE, "Position:", 0, 2, stroke_pos }, { F_BLEND, "Blend Mode:" },
    { F_OPACITY, "Opacity (%):", 0, 100 }, { F_COLOR, "Color:" }, { F_END } },
};
static Doc *ldoc;
static int fget(const FxOne *e, int f)
{
  int la, lalt;
  fx_light(ldoc, &la, &lalt);
  switch (f) {
  case F_BLEND: return e->blend;
  case F_BLEND2: return e->blend2;
  case F_OPACITY: return (e->opacity * 100 + 127) / 255;
  case F_OPACITY2: return (e->opacity2 * 100 + 127) / 255;
  case F_STYLE: return e->style;
  case F_ANGLE: return e->flags & FXF_GLOBAL ? la : e->angle;
  case F_ALT: return e->flags & FXF_GLOBAL ? lalt : e->altitude;
  case F_DIST: return e->distance;
  case F_SIZE: return e->size;
  case F_SPREAD: return e->spread;
  case F_DEPTH: return e->depth;
  case F_GLOBAL: return (e->flags & FXF_GLOBAL) != 0;
  case F_INVERT: return (e->flags & FXF_INVERT) != 0;
  case F_KNOCK: return (e->flags & FXF_KNOCKOUT) != 0;
  case F_DX: return e->dx;
  case F_DY: return e->dy;
  }
  return 0;
}
static void fset(FxOne *e, int f, int v)
{
  switch (f) {
  case F_BLEND: e->blend = (u8)v; break;
  case F_BLEND2: e->blend2 = (u8)v; break;
  case F_OPACITY: e->opacity = (u8)((v * 255 + 50) / 100); break;
  case F_OPACITY2: e->opacity2 = (u8)((v * 255 + 50) / 100); break;
  case F_STYLE: e->style = (u8)v; break;
  case F_ANGLE: e->angle = (i16)v; break;
  case F_ALT: e->altitude = (u8)v; break;
  case F_DIST: e->distance = (u16)v; break;
  case F_SIZE: e->size = (u16)v; break;
  case F_SPREAD: e->spread = (u8)v; break;
  case F_DEPTH: e->depth = (u16)v; break;
  case F_GLOBAL: e->flags = (u8)(v ? e->flags | FXF_GLOBAL : e->flags & ~FXF_GLOBAL); break;
  case F_INVERT: e->flags = (u8)(v ? e->flags | FXF_INVERT : e->flags & ~FXF_INVERT); break;
  case F_KNOCK: e->flags = (u8)(v ? e->flags | FXF_KNOCKOUT : e->flags & ~FXF_KNOCKOUT); break;
  case F_DX: e->dx = (i16)v; break;
  case F_DY: e->dy = (i16)v; break;
  }
}

/* ------------------------------------------------------------------ the Layer Style window */
static struct {
  Object *pages, *on[FX_N], *ctl[FX_N][MAXC], *coltxt[FX_N][2], *pre;
  LayerFx work;
  int layer, pending, shown;
  void *step;                          /* the undo step the preview made */
  char hex[FX_N][2][10];
} ls;
static const char *blendlist[BM_N + 1];
static Object *check(int on)
{
  return ImageObject, ImageButtonFrame, MUIA_InputMode, MUIV_InputMode_Toggle, MUIA_Image_Spec, MUII_CheckMark,
    MUIA_Background, MUII_ButtonBack, MUIA_ShowSelState, FALSE, MUIA_Selected, on, MUIA_CycleChain, 1, End;
}
static void show_hex(int k, int which)
{
  u32 c = which ? ls.work.e[k].color2 : ls.work.e[k].color;
  sprintf(ls.hex[k][which], "#%06lX", (unsigned long)(c & 0xFFFFFF));
  if (ls.coltxt[k][which]) set(ls.coltxt[k][which], MUIA_Text_Contents, (ULONG)ls.hex[k][which]);
}
/* the controls read into the working style */
static void ls_read(void)
{
  int k, j, la, lalt, nla, nalt;
  fx_light(ldoc, &la, &lalt);
  nla = la; nalt = lalt;
  for (k = 0; k < FX_N; k++) {
    FxOne *e = &ls.work.e[k];
    e->on = (u8)(xget(ls.on[k], MUIA_Selected) != 0);
    for (j = 0; j < MAXC && ctls[k][j].f != F_END; j++) {
      const Ctl *c = &ctls[k][j];
      Object *o = ls.ctl[k][j];
      LONG v;
      if (!o || c->f == F_COLOR || c->f == F_COLOR2) continue;
      if (c->f == F_BLEND || c->f == F_BLEND2 || c->f == F_STYLE) v = (LONG)xget(o, MUIA_Cycle_Active);
      else if (c->f == F_GLOBAL || c->f == F_INVERT || c->f == F_KNOCK) v = xget(o, MUIA_Selected) != 0;
      else v = (LONG)xget(o, MUIA_Numeric_Value);
      fset(e, c->f, (int)v);
    }
  }
  /* an angle (or altitude) changed on an effect using the global light moves the global light */
  for (k = 0; k < FX_N; k++) {
    const FxOne *e = &ls.work.e[k];
    if (!(e->flags & FXF_GLOBAL)) continue;
    if (e->angle != la) nla = e->angle;
    if (k == FX_BEVEL && e->altitude != lalt) nalt = e->altitude;
  }
  if (nla != la || nalt != lalt) doc_global_light(ldoc, nla, nalt);
}
static void ls_drop(void)
{
  if (ls.shown && ldoc->undo == ls.step) doc_discard_last(ldoc);
  ls.shown = 0;
}
static void ls_apply(void)
{
  ls_drop();
  doc_set_fx(ldoc, ls.layer, &ls.work);
  ls.step = ldoc->undo; ls.shown = 1;
  DoMethod(canvas, MUIM_Canvas_Refresh);
}
static void ls_each(ULONG id)
{
  if (id == ID_DLGCHANGE) { ls.pending = 1; return; }
  if (id >= ID_FXPAGE0 && id < ID_FXPAGE0 + FX_N) { set(ls.pages, MUIA_Group_ActivePage, id - ID_FXPAGE0); return; }
  if (id >= ID_FXCOL0 && id < ID_FXCOL0 + 2 * FX_N) {
    int k = (int)(id - ID_FXCOL0) / 2, which = (int)(id - ID_FXCOL0) % 2;
    u32 *c = which ? &ls.work.e[k].color2 : &ls.work.e[k].color;
    LONG v = pick_color(fx_names[k], *c);
    if (v >= 0) { *c = (u32)v; show_hex(k, which); ls.pending = 1; }
    return;
  }
  if (id == 0 && ls.pending) {
    ls.pending = 0;
    ls_read();
    if (xget(ls.pre, MUIA_Selected)) ls_apply();
    else if (ls.shown) { ls_drop(); DoMethod(canvas, MUIM_Canvas_Refresh); }
  }
}
/* the window for layer i's style, open at effect page (turned on when it is opened from its menu item) */
static void dlg_layer_style(Doc *d, int page)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const FxOne zero;
  Object *list, *w, *grp;
  int k, j, ok;
  Layer *l = d->lay[d->active];
  if (l->kind == LK_GROUP || l->kind == LK_ADJUST) { ask("OK", "%s", tr("Layer styles go on pixel, type, shape and fill layers, not on groups or adjustment layers.")); return; }
  ldoc = d;
  memset(&ls, 0, sizeof ls);
  ls.layer = d->active;
  ls.work = l->fx;
  for (k = 0; k < FX_N; k++) if (!memcmp(&ls.work.e[k], &zero, sizeof zero)) fx_defaults(&ls.work.e[k], k);
  if (page >= 0 && page < FX_N) ls.work.e[page].on = 1; else page = 0;
  for (k = 0; k < BM_N; k++) blendlist[k] = tr(blend_names[k]);
  blendlist[BM_N] = NULL;
  list = ColGroup(2), End;
  ls.pages = VGroup, MUIA_Group_PageMode, TRUE, End;
  if (!list || !ls.pages) { if (list) MUI_DisposeObject(list); if (ls.pages) MUI_DisposeObject(ls.pages); return; }
  for (k = 0; k < FX_N; k++) {
    Object *b;
    FxOne *e = &ls.work.e[k];
    DoMethod(list, OM_ADDMEMBER, (ULONG)(ls.on[k] = check(e->on)));
    DoMethod(list, OM_ADDMEMBER, (ULONG)(b = QButton(tr(fx_names[k]))));
    DoMethod(b, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_FXPAGE0 + k);
    DoMethod(ls.on[k], MUIM_Notify, MUIA_Selected, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGCHANGE);
    grp = ColGroup(2), Child, HSpace(0), Child, QTitleObject(tr(fx_names[k])), End;
    for (j = 0; j < MAXC && ctls[k][j].f != F_END; j++) {
      const Ctl *c = &ctls[k][j];
      int f = c->f, v = fget(e, f);
      Object *o;
      if (f == F_GLOBAL || f == F_INVERT || f == F_KNOCK) {
        DoMethod(grp, OM_ADDMEMBER, (ULONG)(o = check(v)));
        DoMethod(grp, OM_ADDMEMBER, (ULONG)QLabel(tr(c->label), MUIO_Label_Centered));
        DoMethod(o, MUIM_Notify, MUIA_Selected, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGCHANGE);
      } else if (f == F_COLOR || f == F_COLOR2) {
        Object *bt;
        int which = f == F_COLOR2;
        show_hex(k, which);
        DoMethod(grp, OM_ADDMEMBER, (ULONG)QLabel(tr(c->label), MUIO_Label_Centered));
        DoMethod(grp, OM_ADDMEMBER, (ULONG)(o = HGroup, Child, ls.coltxt[k][which] = TextObject, MUIA_Text_Contents, (ULONG)ls.hex[k][which], End,
                                              Child, bt = QButton(tr("Choose...")), End));
        DoMethod(bt, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_FXCOL0 + 2 * k + which);
        o = NULL;
      } else {
        DoMethod(grp, OM_ADDMEMBER, (ULONG)QLabel(tr(c->label), MUIO_Label_Centered));
        if (f == F_BLEND || f == F_BLEND2 || f == F_STYLE) {
          o = CycleObject, MUIA_Cycle_Entries, (ULONG)(f == F_STYLE ? (const char *const *)c->choices : blendlist), MUIA_Cycle_Active, v, MUIA_CycleChain, 1, End;
          DoMethod(o, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGCHANGE);
        } else {
          o = SliderObject, MUIA_Numeric_Min, c->min, MUIA_Numeric_Max, c->max, MUIA_Numeric_Value, v < c->min ? c->min : v > c->max ? c->max : v,
                MUIA_CycleChain, 1, MUIA_FixWidthTxt, (ULONG)"MMMMMMMMMMMMMMMM", End;
          DoMethod(o, MUIM_Notify, MUIA_Numeric_Value, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGCHANGE);
        }
        DoMethod(grp, OM_ADDMEMBER, (ULONG)o);
      }
      ls.ctl[k][j] = o;
    }
    DoMethod(ls.pages, OM_ADDMEMBER, (ULONG)VGroup, Child, grp, Child, VSpace(0), End);
  }
  set(ls.pages, MUIA_Group_ActivePage, page);
  w = dialog_window("Layer Style", VGroup,
        Child, HGroup, Child, VGroup, Child, list, Child, VSpace(0), End, Child, ls.pages, End,
        Child, HGroup, Child, ls.pre = check(1), Child, QLabel(tr("Preview"), 0), Child, HSpace(0), End,
      End, NULL);
  if (!w) return;
  DoMethod(ls.pre, MUIM_Notify, MUIA_Selected, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGCHANGE);
  ls.pending = page >= 0;                                  /* (an effect just turned on shows at once) */
  ok = run_modal(w, ends, ls_each) == ID_DLGOK;
  if (ok) { ls_read(); if (!ls.shown || ls.pending || memcmp(&d->lay[ls.layer]->fx, &ls.work, sizeof ls.work)) ls_apply(); }
  else ls_drop();
  MUI_DisposeObject(w);
  DoMethod(canvas, MUIM_Canvas_Refresh);
}

/* Global Light: angle and altitude */
static void dlg_global_light(Doc *d)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *a, *b;
  int la, lalt;
  ldoc = d;
  fx_light(d, &la, &lalt);
  w = dialog_window("Global Light", ColGroup(2),
        Child, QLabel(tr("Angle:"), MUIO_Label_Centered), Child, a = SliderObject, MUIA_Numeric_Min, -180, MUIA_Numeric_Max, 180, MUIA_Numeric_Value, la, MUIA_CycleChain, 1, MUIA_FixWidthTxt, (ULONG)"MMMMMMMMMMMMMMMM", End,
        Child, QLabel(tr("Altitude:"), MUIO_Label_Centered), Child, b = SliderObject, MUIA_Numeric_Min, 0, MUIA_Numeric_Max, 90, MUIA_Numeric_Value, lalt, MUIA_CycleChain, 1, End,
      End, NULL);
  if (!w) return;
  if (run_modal(w, ends, NULL) == ID_DLGOK) doc_global_light(d, (int)(LONG)xget(a, MUIA_Numeric_Value), (int)xget(b, MUIA_Numeric_Value));
  MUI_DisposeObject(w);
}

/* ------------------------------------------------------------------ the menu */
static LayerFx copied; static int have_copied;
/* (see easel.h) a menu item of fxids.h's done; 0 if id isn't one */
int layerstyle_command(Doc *d, ULONG id)
{
  Layer *l;
  if (!d || id < ID_LS_FIRST || id > ID_LS_LAST) return 0;
  l = d->lay[d->active];
  if (id < ID_FX0 + FX_N) { dlg_layer_style(d, (int)(id - ID_FX0)); return 1; }
  if (id >= ID_LABEL0 && id < ID_LABEL0 + NLABELS) { doc_set_label(d, (int)(id - ID_LABEL0)); return 1; }
  if (id >= ID_STACKMODE0 && id < ID_STACKMODE0 + STK_N) {
    int r;
    job_begin(d, tr("Stack Mode"));
    r = doc_stack_mode(d, (int)(id - ID_STACKMODE0));
    if (!job_end() && r < 0 && !progress_aborted) ask("OK", "%s", tr("Stack Mode needs at least two pixel layers (pick them in the Layers panel, or show them), and the memory for one more layer."));
    return 1;
  }
  switch (id) {
  case ID_FXBLENDOPT: dlg_layer_props(); break;
  case ID_FXCOPY: copied = l->fx; have_copied = 1; break;
  case ID_FXPASTE: if (have_copied) doc_paste_fx(d, &copied); break;
  case ID_FXCLEAR: doc_clear_fx(d, d->active); break;
  case ID_FXLIGHT: dlg_global_light(d); break;
  case ID_FXLAYERS:
    if (!fx_any(l)) { ask("OK", "%s", tr("This layer has no effects to turn into layers.")); break; }
    if (!doc_fx_to_layers(d, d->active)) ask("OK", "%s", tr("There isn't enough memory for that."));
    break;
  case ID_FXHIDEALL: {
    int i, shown = 0;
    for (i = 0; i < d->nlay; i++) if (fx_has(d->lay[i]) && !d->lay[i]->fx.hidden) shown = 1;
    doc_hide_all_fx(d, shown);
    break; }
  case ID_FXSCALE: {
    LONG v = 100;
    if (ask_number("Scale Effects", "Scale (%):", &v, 1, 1000)) doc_scale_fx(d, d->active, (int)v);
    break; }
  case ID_DELHIDDEN:
    if (!doc_delete_hidden(d)) ask("OK", "%s", tr("There are no hidden layers to delete (and a picture keeps at least one layer)."));
    break;
  case ID_REVERSE:
    if (!doc_reverse_layers(d)) ask("OK", "%s", tr("Pick two or more layers in the Layers panel (Shift- or Ctrl-click) to reverse them."));
    break;
  case ID_BGFROMLAYER:
    if (!doc_background_from_layer(d)) ask("OK", "%s", tr("Choose a pixel layer to make the Background."));
    break;
  case ID_COPYCSS: {
    char *css = layer_css(d, d->active);
    if (!css || !clipboard_put_text(css)) ask("OK", "%s", tr("The CSS couldn't be put on the clipboard."));
    free(css);
    break; }
  default: return 0;
  }
  return 1;
}
/* the items greyed out or ticked to suit the active layer */
void layerstyle_menus(Doc *d, void (*menu_set)(ULONG id, ULONG attr, ULONG val))
{
  Layer *l = d ? d->lay[d->active] : NULL;
  int styled = l && l->kind != LK_GROUP && l->kind != LK_ADJUST, i, any = 0, hidden = 0;
  ULONG id;
  for (id = ID_FX0; id < ID_FX0 + FX_N; id++) menu_set(id, MUIA_Menuitem_Enabled, styled);
  menu_set(ID_FXBLENDOPT, MUIA_Menuitem_Enabled, l != NULL);
  menu_set(ID_FXCOPY, MUIA_Menuitem_Enabled, l && fx_has(l));
  menu_set(ID_FXPASTE, MUIA_Menuitem_Enabled, styled && have_copied);
  menu_set(ID_FXCLEAR, MUIA_Menuitem_Enabled, l && fx_has(l));
  menu_set(ID_FXLAYERS, MUIA_Menuitem_Enabled, l && fx_any(l));
  menu_set(ID_FXSCALE, MUIA_Menuitem_Enabled, l && fx_has(l));
  menu_set(ID_FXLIGHT, MUIA_Menuitem_Enabled, d != NULL);
  if (d) for (i = 0; i < d->nlay; i++) if (fx_has(d->lay[i])) { any = 1; if (d->lay[i]->fx.hidden) hidden = 1; }
  menu_set(ID_FXHIDEALL, MUIA_Menuitem_Enabled, any);
  menu_set(ID_FXHIDEALL, MUIA_Menuitem_Checked, hidden);
  for (i = 0; i < NLABELS; i++) { menu_set(ID_LABEL0 + i, MUIA_Menuitem_Enabled, l != NULL); menu_set(ID_LABEL0 + i, MUIA_Menuitem_Checked, l && l->label == i); }
  menu_set(ID_DELHIDDEN, MUIA_Menuitem_Enabled, d != NULL);
  menu_set(ID_REVERSE, MUIA_Menuitem_Enabled, d && doc_npicked(d) > 0);
  menu_set(ID_BGFROMLAYER, MUIA_Menuitem_Enabled, l && l->kind == LK_PIXEL);
  menu_set(ID_COPYCSS, MUIA_Menuitem_Enabled, l != NULL);
  for (i = 0; i < STK_N; i++) menu_set(ID_STACKMODE0 + i, MUIA_Menuitem_Enabled, d && d->nlay > 1);
}
