/*
 * menux.c - Easel 1.2's menu additions: View > Pixel Aspect Ratio (presets, from the Amiga screen
 * mode, custom, and the correction), Show > Pixel Grid, Selection Edges, Layer Edges, Target Path,
 * All and None, Pattern Preview, Fit Layer(s) on Screen, Print Size, New Guide Layout and New
 * Guides From Shape; Window > Arrange > Match Zoom, Location and All; Image > Trim...; Layer >
 * New > Layer..., Align > Distribute by edges and spacing, Rasterize > All Layers, Quick Export as
 * PNG; File > Scripts > Flatten All Layer Effects and Automate > Conditional Mode Change; the
 * Info panel's histogram channel; recording into an action; the canvas's
 * context menu. The work is in the core (core/viewx.c, core/layers.c, core/actions.c); main.c hands
 * the menu IDs in mxids.h here. The windows are in panelsx.c.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "mxids.h"
#include "fxids.h"
#include "../core/viewx.h"
#include "../core/actions.h"
#include "../core/layers.h"
#include "../core/layerops.h"
#include "../core/fx.h"
#include "../core/guides.h"
#include "../core/amigamodes.h"
#include "../core/toolsx.h"                /* (1.2: notes, counts and slices for Show All/None) */

int vx_par_correct = 1, vx_pixgrid, vx_tile, vx_seledges = 1, vx_layeredges, vx_path = 1, vx_hist_channel;

static ULONG xget(Object *o, ULONG a) { ULONG v = 0; get(o, a, &v); return v; }
static Object *slider(LONG min, LONG max, LONG v) { return SliderObject, MUIA_Numeric_Min, min, MUIA_Numeric_Max, max, MUIA_Numeric_Value, v, MUIA_CycleChain, 1, End; }
static Object *check(int on)
{
  return ImageObject, ImageButtonFrame, MUIA_InputMode, MUIV_InputMode_Toggle, MUIA_Image_Spec, MUII_CheckMark,
    MUIA_Background, MUII_ButtonBack, MUIA_ShowSelState, FALSE, MUIA_Selected, on, MUIA_CycleChain, 1, End;
}
static Object *cycle(const char *const *labels, int active) { return CycleObject, MUIA_Cycle_Entries, (ULONG)labels, MUIA_Cycle_Active, active, MUIA_CycleChain, 1, End; }
static Object *label(const char *s) { return QLabel(tr(s), MUIO_Label_Centered); }
static Object *numstr(LONG v)
{
  char b[16];
  sprintf(b, "%ld", (long)v);
  return StringObject, StringFrame, MUIA_String_Contents, (ULONG)b, MUIA_String_Accept, (ULONG)"0123456789", MUIA_String_MaxLen, 6, MUIA_CycleChain, 1, End;
}
static LONG numof(Object *o) { return atol((const char *)xget(o, MUIA_String_Contents)); }
static void trlist(const char **out, const char *const *in, int n) { int i; for (i = 0; i < n; i++) out[i] = tr(in[i]); out[n] = NULL; }

/* ------------------------------------------------------------------ dialogs */
/* View > New Guide Layout: columns, rows, margins (in pixels; the column and gutter widths start at
   the preferences' Units & Rulers values) */
static int dlg_guide_layout(Doc *d)
{
  static GuideLayout g = { 8, 0, 12, 0, 0, 12, 0, 0, 0, 0, 0, 1 };
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *cols, *colw, *colg, *rows, *rowh, *rowg, *mt, *ml, *mb, *mr, *cen, *clr;
  int ok;
  if (!g.colgut) g.colgut = prefs.gutter_w;
  w = dialog_window("New Guide Layout", VGroup,
        Child, ColGroup(4),
          Child, label("Columns:"), Child, cols = numstr(g.cols), Child, label("Rows:"), Child, rows = numstr(g.rows),
          Child, label("Width (0 to fit):"), Child, colw = numstr(g.colw), Child, label("Height (0 to fit):"), Child, rowh = numstr(g.rowh),
          Child, label("Gutter:"), Child, colg = numstr(g.colgut), Child, label("Gutter:"), Child, rowg = numstr(g.rowgut),
          Child, label("Margin top:"), Child, mt = numstr(g.mtop), Child, label("Left:"), Child, ml = numstr(g.mleft),
          Child, label("Bottom:"), Child, mb = numstr(g.mbottom), Child, label("Right:"), Child, mr = numstr(g.mright),
        End,
        Child, HGroup, Child, cen = check(g.centre), Child, label("Center columns"), Child, HSpace(8),
          Child, clr = check(g.clear), Child, label("Clear existing guides"), Child, HSpace(0), End,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("(All sizes in pixels.)"), End,
      End, "OK");
  if (!w) return 0;
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  if (ok) {
    g.cols = (int)numof(cols); g.colw = (int)numof(colw); g.colgut = (int)numof(colg);
    g.rows = (int)numof(rows); g.rowh = (int)numof(rowh); g.rowgut = (int)numof(rowg);
    g.mtop = (int)numof(mt); g.mleft = (int)numof(ml); g.mbottom = (int)numof(mb); g.mright = (int)numof(mr);
    g.centre = xget(cen, MUIA_Selected) != 0; g.clear = xget(clr, MUIA_Selected) != 0;
    guide_layout(d, &g);
  }
  MUI_DisposeObject(w);
  return ok;
}
/* Image > Trim...: based on transparent pixels or a corner's colour, and which sides */
static void dlg_trim(Doc *d)
{
  static int based = TRIM_TRANSPARENT, sides = TRIM_ALL;
  static const char *const bases[] = { "Transparent Pixels", "Top Left Pixel Color", "Bottom Right Pixel Color" };
  static const ULONG ends[] = { ID_DLGOK, 0 };
  const char *bl[4];
  Object *w, *cy, *t, *l, *b, *r;
  trlist(bl, bases, 3);
  w = dialog_window("Trim", VGroup,
        Child, HGroup, Child, label("Based On:"), Child, cy = cycle(bl, based), End,
        Child, QTitleObject(tr("Trim Away")),
        Child, HGroup, Child, t = check(sides & TRIM_TOP), Child, label("Top"), Child, l = check(sides & TRIM_LEFT), Child, label("Left"),
          Child, b = check(sides & TRIM_BOTTOM), Child, label("Bottom"), Child, r = check(sides & TRIM_RIGHT), Child, label("Right"), Child, HSpace(0), End,
      End, "OK");
  if (!w) return;
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    based = (int)xget(cy, MUIA_Cycle_Active);
    sides = (xget(t, MUIA_Selected) ? TRIM_TOP : 0) | (xget(l, MUIA_Selected) ? TRIM_LEFT : 0) | (xget(b, MUIA_Selected) ? TRIM_BOTTOM : 0) | (xget(r, MUIA_Selected) ? TRIM_RIGHT : 0);
    job_begin(d, "Trim");
    if (!image_trim_ex(d, based, sides)) ask("OK", "%s", tr("There is nothing to trim away."));
    job_end();
  }
  MUI_DisposeObject(w);
}
/* Layer > New > Layer...: name, mode, opacity, clipping and colour label before it is made */
static void dlg_new_layer(Doc *d)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *modes[BM_N + 1], *labels[NLABELS + 1];
  static int mode, lab;
  Object *w, *nm, *md, *op, *cl, *lb;
  char name[64];
  int i, n = d->nlay + 1;
  Layer *l;
  for (i = 0; i < BM_N; i++) modes[i] = tr(blend_names[i]);
  modes[BM_N] = NULL;
  trlist(labels, label_names, NLABELS);
  sprintf(name, "%s %d", tr("Layer"), n);
  w = dialog_window("New Layer", ColGroup(2),
        Child, label("Name:"), Child, nm = StringObject, StringFrame, MUIA_String_Contents, (ULONG)name, MUIA_String_MaxLen, 60, MUIA_CycleChain, 1, End,
        Child, label("Use Previous Layer to Create Clipping Mask:"), Child, HGroup, Child, cl = check(0), Child, HSpace(0), End,
        Child, label("Color:"), Child, lb = cycle(labels, lab),
        Child, label("Mode:"), Child, md = cycle(modes, mode),
        Child, label("Opacity (%):"), Child, op = slider(0, 100, 100),
      End, "OK");
  if (!w) return;
  set(w, MUIA_Window_ActiveObject, (ULONG)nm);
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    mode = (int)xget(md, MUIA_Cycle_Active); lab = (int)xget(lb, MUIA_Cycle_Active);
    if ((l = layer_new(d, (const char *)xget(nm, MUIA_String_Contents)))) {
      l->blend = (u8)mode;
      l->opacity = (u8)((xget(op, MUIA_Numeric_Value) * 255 + 50) / 100);
      l->clip = xget(cl, MUIA_Selected) != 0;
      l->label = (u8)lab;
      if (d->nlay && d->active >= 0) l->depth = d->lay[d->active]->kind == LK_GROUP && !d->lay[d->active]->collapsed ? d->lay[d->active]->depth + 1 : d->lay[d->active]->depth;
      if (!doc_add_layer(d, l, d->active + 1)) ask("OK", "%s", tr("There isn't enough memory for that."));
    } else ask("OK", "%s", tr("There isn't enough memory for that."));
  }
  MUI_DisposeObject(w);
}
/* File > Automate > Conditional Mode Change: the picture changed to one mode if it is in one of the others */
static void dlg_cond_mode(Doc *d)
{
  static int from = 0xF, to = 0;
  static const char *const modes[] = { "RGB Color", "Grayscale", "Indexed Color", "Bitmap" };
  static const ULONG mode_of[4] = { MODE_RGB, MODE_GRAY, MODE_INDEXED, MODE_BITMAP }, id_of[4] = { ID_MODE_RGB, ID_MODE_GRAY, ID_MODE_INDEXED, ID_MODE_BITMAP };
  static const ULONG ends[] = { ID_DLGOK, 0 };
  const char *ml[5];
  Object *w, *c[4], *tc;
  int k;
  trlist(ml, modes, 4);
  w = dialog_window("Conditional Mode Change", VGroup,
        Child, QTitleObject(tr("Source Mode")),
        Child, HGroup, Child, c[0] = check(from & 1), Child, label("RGB Color"), Child, c[1] = check(from & 2), Child, label("Grayscale"),
          Child, c[2] = check(from & 4), Child, label("Indexed Color"), Child, c[3] = check(from & 8), Child, label("Bitmap"), End,
        Child, HGroup, Child, label("Target Mode:"), Child, tc = cycle(ml, to), End,
      End, "OK");
  if (!w) return;
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    for (from = 0, k = 0; k < 4; k++) if (xget(c[k], MUIA_Selected)) from |= 1 << k;
    to = (int)xget(tc, MUIA_Cycle_Active);
    MUI_DisposeObject(w);
    for (k = 0; k < 4; k++) if (d->mode == mode_of[k] && (from & (1 << k)) && k != to) { do_command(id_of[to]); break; }
    return;
  }
  MUI_DisposeObject(w);
}
/* View > Pixel Aspect Ratio > Custom...: a ratio (1.09) or width:height (10:11) */
static void dlg_par_custom(Doc *d)
{
  char buf[32];
  int ax = 0, ay = 0;
  sprintf(buf, "%d:%d", d->aspect_x ? d->aspect_x : 1, d->aspect_y ? d->aspect_y : 1);
  if (!ask_string("Pixel Aspect Ratio", "Width : height of a pixel (or a ratio, as 1.09):", buf, sizeof buf)) return;
  if (sscanf(buf, "%d:%d", &ax, &ay) == 2 && ax > 0 && ay > 0) {
    if (ax > 255 || ay > 255) { u8 x, y; par_approx((double)ax / ay, &x, &y); ax = x; ay = y; }
  } else {
    /* a ratio, read without the C library's floating point scanf: digits, a point, digits */
    long ip = 0, fp = 0, sc = 1;
    const char *p = buf;
    u8 x, y;
    while (*p == ' ') p++;
    while (*p >= '0' && *p <= '9') ip = ip * 10 + (*p++ - '0');
    if (*p == '.' || *p == ',') for (p++; *p >= '0' && *p <= '9' && sc < 100000; p++) { fp = fp * 10 + (*p - '0'); sc *= 10; }
    if (ip <= 0 && fp <= 0) return;
    par_approx(ip + (double)fp / sc, &x, &y);
    ax = x; ay = y;
  }
  par_set(d, ax, ay);
}

/* ------------------------------------------------------------------ the rest */
/* Layer > Quick Export as PNG: the active layer alone (with its style and mask) beside the picture */
static void layer_quick_png(Doc *d)
{
  DocView *v = cur_view();
  Layer *l = d->lay[d->active], *c;
  Doc *t;
  char path[512], name[80];
  size_t k;
  if (l->kind == LK_GROUP || l->kind == LK_ADJUST) { ask("OK", "%s", tr("Choose a pixel, type, shape or fill layer to export.")); return; }
  /* the file name from the layer's name, without characters a file name can't have */
  for (k = 0; k < sizeof name - 5 && l->name[k]; k++) name[k] = strchr("/:*?\"#", l->name[k]) ? '_' : l->name[k];
  name[k] = 0;
  strcat(name, ".png");
  if (v->path[0]) { strncpy(path, v->path, sizeof path - 1); path[sizeof path - 1] = 0; *PathPart((STRPTR)path) = 0; AddPart((STRPTR)path, (STRPTR)name, sizeof path); }
  else { strcpy(path, name); if (!mx_file_request(1, "Quick Export as PNG", path, sizeof path, 0)) return; }
  if (!(t = doc_new(d->w, d->h, 0, 1)) || !(c = layer_copy(l, d->w, d->h))) { if (t) doc_free(t); ask("OK", "%s", tr("There isn't enough memory for that.")); return; }
  layer_free(t->lay[0]);
  t->lay[0] = c; c->depth = 0; c->clip = 0; c->visible = 1;
  t->dpi = d->dpi; t->info = d->info;
  if (!mx_write(t, path)) ask("OK", "%s", tr("The file couldn't be written."));
  doc_free(t);
}
/* Layer > Rasterize > All Layers: every type, shape and fill layer made pixels, as one step */
static void rasterize_all(Doc *d)
{
  int i, n = 0;
  doc_begin(d, "Rasterize All Layers");
  for (i = 0; i < d->nlay; i++) if (d->lay[i]->kind == LK_TEXT || d->lay[i]->kind == LK_SHAPE || d->lay[i]->kind == LK_FILL) n += doc_rasterize(d, i) != 0;
  doc_end(d);
  if (!n) ask("OK", "%s", tr("There are no type, shape or fill layers to rasterize."));
}
/* File > Scripts > Flatten All Layer Effects: each layer's style drawn into its pixels, as one step */
static void flatten_all_fx(Doc *d)
{
  int i, n = 0, ok = 1;
  doc_begin(d, "Flatten All Layer Effects");
  for (i = 0; i < d->nlay && ok; i++) {
    Layer *l = d->lay[i];
    if (!fx_has(l) || l->kind == LK_GROUP || l->kind == LK_ADJUST) continue;
    if (l->kind != LK_PIXEL && !doc_rasterize(d, i)) { ok = 0; break; }
    if (!doc_rasterize_fx(d, i)) ok = 0; else n++;
  }
  doc_end(d);
  if (!ok) ask("OK", "%s", tr("There isn't enough memory for that."));
  else if (!n) ask("OK", "%s", tr("No layer has a layer style."));
}
/* View > Fit Layer(s) on Screen and Print Size: a zoom, then the view centred on a point */
static void zoom_centre(double z, double cx, double cy)
{
  if (z < 1.0 / 32) z = 1.0 / 32;
  if (z > 32) z = 32;
  DoMethod(canvas, MUIM_Canvas_Zoom, (LONG)(z * 1000 + 0.5), 0);
  DoMethod(canvas, MUIM_Canvas_ScrollTo, (LONG)cx, (LONG)cy);
}
/* Window > Arrange > Match Zoom, Location (the same part of each picture) or both */
static void match_views(int zoom, int loc)
{
  DocView *v = cur_view();
  int k;
  if (!v) return;
  for (k = 0; k < ndocs; k++) {
    DocView *o = &docs[k];
    double fx, fy;
    if (o == v) continue;
    fx = (v->sx + _mwidth(canvas) / 2.0) / (v->doc->w * v->zoom);
    fy = (v->sy + _mheight(canvas) / 2.0) / (v->doc->h * v->zoom * par_view_ratio(v->doc, vx_par_correct));
    if (zoom) o->zoom = v->zoom;
    if (loc) {
      o->sx = (LONG)(fx * o->doc->w * o->zoom - _mwidth(canvas) / 2.0);
      o->sy = (LONG)(fy * o->doc->h * o->zoom * par_view_ratio(o->doc, vx_par_correct) - _mheight(canvas) / 2.0);
    }
    o->placed = 1;
  }
}

/* ------------------------------------------------------------------ recording (the History Log is tools2.c's, Edit > History Options) */
int mx_recording;
extern ActionLib mx_actions;           /* (panelsx.c) */
extern int mx_rec_action;
void mx_record(ULONG id, const char *label)
{
  if (!mx_recording || !label || mx_rec_action < 0 || mx_rec_action >= mx_actions.n) return;
  /* not recorded: the Actions window's own items, the view, the windows and the open, quit and recent items */
  if (id == ID_WIN_ACTIONS || id == ID_ACTSTOP || id == ID_ACTPLAY || id == ID_BATCH || id == ID_SEARCH || id == ID_QUIT || id == ID_ABOUT) return;
  if ((id >= ID_ZOOMIN && id <= ID_SCR_CHOOSE) || (id >= ID_NEXTDOC && id <= ID_WIN_DPAINT) || (id >= ID_RECENT0 && id < ID_RECENT0 + 10)) return;
  if (id == ID_NEW || id == ID_OPEN || id == ID_CLOSE || id == ID_CLOSEALL || id == ID_CLOSEOTHERS || id == ID_KEYS || id == ID_PREFS) return;
  if ((id >= ID_PARCORRECT && id <= ID_PRINTSIZE) || (id >= ID_MATCHZOOM && id <= ID_MATCHALL) || (id >= ID_WIN_ACTIONS && id <= ID_MX_LAST)) return;   /* (1.2's view items and windows) */
  act_add_step(&mx_actions, mx_rec_action, (u32)id, label, 1);
}

/* ------------------------------------------------------------------ the context menu */
static Object *ctx;
#define CI(label, id) { NM_ITEM, (STRPTR)label, 0, 0, 0, (APTR)(id) }
#define CB { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 }
static struct NewMenu ctxmenu[] = {
  { NM_TITLE, (STRPTR)"Easel", 0, 0, 0, 0 },
  CI("Undo", ID_UNDO), CI("Redo", ID_REDO), CB,
  CI("Cut", ID_CUT), CI("Copy", ID_COPY), CI("Copy Merged", ID_COPYMERGED), CI("Paste", ID_PASTE), CI("Paste in Place", ID_PASTEINPLACE), CB,
  CI("Select All", ID_SELALL), CI("Deselect", ID_DESELECT), CI("Select Inverse", ID_INVERSE), CI("Layer Transparency", ID_SELALPHA), CB,
  CI("Layer via Copy", ID_LAYERFROMSEL), CI("Layer via Cut", ID_LAYERCUTSEL), CI("Free Transform", ID_FREETRANSFORM), CB,
  CI("Fill...", ID_FILL), CI("Stroke...", ID_STROKE), CB,
  CI("Layer Properties...", ID_LAYERPROPS), CI("Duplicate Layer", ID_DUPLAYER), CI("Merge Down", ID_MERGEDOWN), CI("Blending Options...", ID_FXBLENDOPT), CB,
  CI("Fit on Screen", ID_FIT), CI("100%", ID_ACTUAL),
  { NM_END, 0, 0, 0, 0, 0 }
};
Object *mx_context_menu(void)
{
  int i;
  if (ctx) return ctx;
  for (i = 0; ctxmenu[i].nm_Type != NM_END; i++) if (ctxmenu[i].nm_Label != NM_BARLABEL) ctxmenu[i].nm_Label = (STRPTR)tr((const char *)ctxmenu[i].nm_Label);
  ctx = MUI_MakeObject(MUIO_MenustripNM, (ULONG)ctxmenu, 0);
  return ctx;
}
/* (MUI doesn't dispose a context menu with its object: done after the application is gone) */
void mx_context_free(void) { if (ctx) MUI_DisposeObject(ctx); ctx = NULL; }

/* ------------------------------------------------------------------ the menu items */
int mx_command(Doc *d, ULONG id)
{
  DocView *v = cur_view();
  Box b;
  switch (id) {
  /* those that work without a picture */
  case ID_PARCORRECT: vx_par_correct = !vx_par_correct; DoMethod(canvas, MUIM_Canvas_Refresh); return 1;
  case ID_PIXGRID: vx_pixgrid = !vx_pixgrid; DoMethod(canvas, MUIM_Canvas_Refresh); return 1;
  case ID_PATPREVIEW: vx_tile = !vx_tile; DoMethod(canvas, MUIM_Canvas_Refresh); return 1;
  case ID_SHOWSELEDGES: vx_seledges = !vx_seledges; DoMethod(canvas, MUIM_Canvas_Refresh); return 1;
  case ID_SHOWLAYEREDGES: vx_layeredges = !vx_layeredges; DoMethod(canvas, MUIM_Canvas_Refresh); return 1;
  case ID_SHOWPATH: vx_path = !vx_path; DoMethod(canvas, MUIM_Canvas_Refresh); return 1;
  case ID_SHOWALL: case ID_SHOWNONE: {
    int on = id == ID_SHOWALL;
    extras_hidden = 0;
    vx_seledges = vx_path = vx_layeredges = vx_pixgrid = on;
    if (d && d->annot) d->annot->show_notes = d->annot->show_count = d->annot->show_slices = (u8)on;   /* (1.2: notes, counts, slices) */
    if (d) d->guides.show_grid = d->guides.show_guides = (u8)on;
    DoMethod(canvas, MUIM_Canvas_Refresh);
    return 1; }
  case ID_HISTCH0: case ID_HISTCH0 + 1: case ID_HISTCH0 + 2: case ID_HISTCH0 + 3: vx_hist_channel = (int)(id - ID_HISTCH0); return 1;
  case ID_WIN_ACTIONS: dlg_actions(); return 1;
  case ID_WIN_STYLES: dlg_styles(); return 1;
  case ID_ACTSTOP: mx_recording = 0; return 1;
  case ID_ACTPLAY: actions_play_current(); return 1;
  case ID_SEARCH: dlg_search(); return 1;
  case ID_BATCH: dlg_batch(); return 1;
  case ID_MATCHZOOM: match_views(1, 0); return 1;
  case ID_MATCHLOC: match_views(0, 1); return 1;
  case ID_MATCHALL: match_views(1, 1); return 1;
  }
  if (!d) return id >= ID_MX_FIRST && id <= ID_MX_LAST && id != ID_PASTEOUTSIDE;
  if (id >= ID_PAR0 && id < ID_PAR0 + PAR_N) { par_set(d, par_presets[id - ID_PAR0].ax, par_presets[id - ID_PAR0].ay); DoMethod(canvas, MUIM_Canvas_Refresh); return 1; }
  if (id >= ID_DIST_LEFT && id <= ID_DIST_LEFT + 5) {
    if (!doc_align(d, AL_DIST_LEFT + (int)(id - ID_DIST_LEFT))) ask("OK", "%s", tr("Pick three or more layers to distribute."));
    return 1;
  }
  switch (id) {
  case ID_PARSCREEN: {
    u8 ax, ay;
    if (!d->modeid || !amode_aspect(d->modeid, &ax, &ay)) { ask("OK", "%s", tr("The picture has no Amiga screen mode (Image > Mode > Amiga Screen Mode chooses one).")); return 1; }
    par_set(d, ax, ay);
    DoMethod(canvas, MUIM_Canvas_Refresh);
    return 1; }
  case ID_PARCUSTOM: dlg_par_custom(d); DoMethod(canvas, MUIM_Canvas_Refresh); return 1;
  case ID_FITLAYERS:
    if (!fit_layers_box(d, &b)) b = doc_box(d);
    zoom_centre(zoom_fit_box(b, _mwidth(canvas), _mheight(canvas), par_view_ratio(d, vx_par_correct)), (b.x0 + b.x1) / 2.0, (b.y0 + b.y1) / 2.0);
    return 1;
  case ID_PRINTSIZE: zoom_centre(zoom_print_size(d->dpi, prefs.screen_dpi), d->w / 2.0, d->h / 2.0); return 1;
  case ID_GUIDELAYOUT: dlg_guide_layout(d); DoMethod(canvas, MUIM_Canvas_Refresh); return 1;
  case ID_GUIDESHAPE:
    if (!guide_shape_box(d, &b)) { ask("OK", "%s", tr("The active layer is empty and there is no selection.")); return 1; }
    guides_from_box(d, b);
    DoMethod(canvas, MUIM_Canvas_Refresh);
    return 1;
  case ID_TRIMOPT: dlg_trim(d); return 1;
  case ID_NEWLAYERDLG: dlg_new_layer(d); return 1;
  case ID_RASTERALL: rasterize_all(d); return 1;
  case ID_LAYERQPNG: layer_quick_png(d); return 1;
  case ID_FLATTENFX: flatten_all_fx(d); return 1;
  case ID_CONDMODE: dlg_cond_mode(d); return 1;
  case ID_WIN_COMPS: dlg_comps(); return 1;
  case ID_COMPSTOFILES: dlg_comps(); return 1;
  }
  (void)v;
  return 0;
}
void mx_menus(Doc *d, void (*menu_set)(ULONG id, ULONG attr, ULONG val))
{
  static const ULONG needdoc[] = { ID_PARSCREEN, ID_PARCUSTOM, ID_FITLAYERS, ID_PRINTSIZE, ID_GUIDELAYOUT, ID_GUIDESHAPE, ID_TRIMOPT,
    ID_NEWLAYERDLG, ID_RASTERALL, ID_LAYERQPNG, ID_FLATTENFX, ID_CONDMODE, ID_WIN_COMPS, ID_MATCHZOOM, ID_MATCHLOC, ID_MATCHALL, 0 };
  int i, k = d ? par_find(d) : -1;
  for (i = 0; needdoc[i]; i++) menu_set(needdoc[i], MUIA_Menuitem_Enabled, d != NULL);
  for (i = 0; i < PAR_N; i++) { menu_set(ID_PAR0 + i, MUIA_Menuitem_Enabled, d != NULL); menu_set(ID_PAR0 + i, MUIA_Menuitem_Checked, i == k); }
  for (i = 0; i < 6; i++) menu_set(ID_DIST_LEFT + i, MUIA_Menuitem_Enabled, d != NULL);
  menu_set(ID_MATCHZOOM, MUIA_Menuitem_Enabled, ndocs > 1); menu_set(ID_MATCHLOC, MUIA_Menuitem_Enabled, ndocs > 1); menu_set(ID_MATCHALL, MUIA_Menuitem_Enabled, ndocs > 1);
  menu_set(ID_PARCORRECT, MUIA_Menuitem_Checked, vx_par_correct);
  menu_set(ID_PIXGRID, MUIA_Menuitem_Checked, vx_pixgrid);
  menu_set(ID_PATPREVIEW, MUIA_Menuitem_Checked, vx_tile);
  menu_set(ID_SHOWSELEDGES, MUIA_Menuitem_Checked, vx_seledges);
  menu_set(ID_SHOWLAYEREDGES, MUIA_Menuitem_Checked, vx_layeredges);
  menu_set(ID_SHOWPATH, MUIA_Menuitem_Checked, vx_path);
  menu_set(ID_SHOWEDGES, MUIA_Menuitem_Checked, !extras_hidden);
  for (i = 0; i < 4; i++) menu_set(ID_HISTCH0 + i, MUIA_Menuitem_Checked, vx_hist_channel == i);
  menu_set(ID_ACTSTOP, MUIA_Menuitem_Enabled, mx_recording);
}
