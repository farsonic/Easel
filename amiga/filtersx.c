/*
 * filtersx.c - the Filter Gallery (a stack of up to four effects) and each gallery effect's own
 * dialog, Image > Adjustments > Color Lookup, Equalize's choice when part of the layer is
 * selected, Select > Focus Area and Select > Select and Mask. Each previews on the picture through
 * dialogs.c's slider dialog; the work itself is in core/gallery.c, adjustx.c and selmask.c, run as
 * the command's job (progress and Abort, amiga/jobs.c).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "lang.h"
#include "ids.h"
#include "theme.h"
#include "dlgx.h"
#include "brushwin.h"
#include "../core/util.h"
#include "../core/gallery.h"
#include "../core/adjustx.h"
#include "../core/selmask.h"
#include "../core/selmore.h"
#include "../core/layers.h"

static ULONG xget(Object *o, ULONG a) { ULONG v = 0; get(o, a, &v); return v; }
static void out_of_memory(void) { if (!progress_aborted) ask("OK", "%s", tr("There isn't enough memory for that.")); }
/* enough memory for need bytes? If not, the user is told how much is needed */
static int mem_ok(long need)
{
  ULONG total = AvailMem(MEMF_ANY);
  char msg[300];
  if (need <= 0 || (ULONG)need + 65536UL <= total) return 1;
  snprintf(msg, sizeof msg, tr("That needs about %lu KB of memory, and %lu KB is free.\nClose other pictures or programs and try again."),
           (unsigned long)(need / 1024), (unsigned long)(total / 1024));
  ask("OK", "%s", msg);
  return 0;
}
static long area_bytes(Doc *d)
{
  Box b = d->sel ? box_clip(d->selbox, doc_box(d)) : doc_box(d);
  return box_empty(b) ? 0 : (long)(b.x1 - b.x0) * (b.y1 - b.y0) * 4;
}

/* ------------------------------------------------------------------ one gallery effect */
static LONG gx_saved[GX_N][8]; static int gx_saved_c[GX_N], gx_saved_t[GX_N], gx_have[GX_N];
static LONG gx_now[8]; static int gx_now_c, gx_now_t;
static void gx_effect(int fx, Doc *d, const LONG *v, int c, int t, GalleryEffect *e)
{
  const GalleryDef *g = &gallery_defs[fx];
  int i;
  gallery_default(fx, e);
  for (i = 0; i < g->nsl; i++) e->v[i] = (int)v[i];
  e->list1 = c; e->list2 = g->list2 ? (int)v[7] : 0; e->tick = t;
  e->fg = d->fg & 0xFFFFFF; e->bg = d->bg & 0xFFFFFF;
}
static void gx_apply(int fx, Doc *d, const LONG *v, int c, int t)
{
  GalleryEffect e;
  memcpy(gx_now, v, sizeof gx_now); gx_now_c = c; gx_now_t = t;
  gx_effect(fx, d, v, c, t, &e);
  if (flt_gallery(d, &e, 1, NULL) < 0) out_of_memory();
}
/* one call for each effect, so Filter > Last Filter knows which */
#define GXF(k) static void gxf##k(Doc *d, const LONG *v, int c, int t) { gx_apply(k, d, v, c, t); }
GXF(0) GXF(1) GXF(2) GXF(3) GXF(4) GXF(5) GXF(6) GXF(7) GXF(8) GXF(9) GXF(10) GXF(11) GXF(12) GXF(13) GXF(14) GXF(15)
GXF(16) GXF(17) GXF(18) GXF(19) GXF(20) GXF(21) GXF(22) GXF(23) GXF(24) GXF(25) GXF(26) GXF(27) GXF(28) GXF(29) GXF(30) GXF(31)
static const DlgApplyFn gxfns[32] = { gxf0, gxf1, gxf2, gxf3, gxf4, gxf5, gxf6, gxf7, gxf8, gxf9, gxf10, gxf11, gxf12, gxf13, gxf14, gxf15,
  gxf16, gxf17, gxf18, gxf19, gxf20, gxf21, gxf22, gxf23, gxf24, gxf25, gxf26, gxf27, gxf28, gxf29, gxf30, gxf31 };
typedef char gx_count_check[GX_N == 32 ? 1 : -1];
static DlgParam gx_params[GX_SLIDERS];
static const char *gx_note(int fx)
{
  if (!strcmp(gallery_defs[fx].category, "Sketch")) return "Drawn in the foreground colour on the background colour.";
  if (fx == GX_NEON_GLOW) return "The picture in the foreground and background colours, glowing.";
  if (fx == GX_GRAIN) return "Sprinkles use the background colour, Stippled and Speckle the foreground.";
  return NULL;
}
/* the effect's dialog: its sliders, lists and tick (the last settings used), preview, apply */
static int gx_dialog(int fx, const LONG *init, int c1, int c2, int t, DlgApplyFn fn, int remember)
{
  const GalleryDef *g = &gallery_defs[fx];
  const char *note = gx_note(fx);
  int i;
  for (i = 0; i < g->nsl; i++) { gx_params[i].label = g->sl[i].label; gx_params[i].min = g->sl[i].min; gx_params[i].max = g->sl[i].max; gx_params[i].def = g->sl[i].def; }
  return dlg_sliders(g->name, gx_params, g->nsl, g->list1, g->list1label, c1, g->list2, g->list2label, c2, g->tick, t, init, fn,
                     note ? TextObject, MUIA_Text_Contents, (ULONG)tr(note), MUIA_Font, MUIV_Font_Tiny, End : NULL, remember);
}
static void gx_command(int fx)
{
  Doc *d = cur_doc();
  GalleryEffect e;
  const GalleryDef *g = &gallery_defs[fx];
  if (!d) return;
  gallery_default(fx, &e);
  if (!mem_ok(gallery_memory(d, &e, 1))) return;
  if (!gx_have[fx]) {
    int i;
    for (i = 0; i < 8; i++) gx_saved[fx][i] = i < g->nsl ? g->sl[i].def : 0;
    gx_saved[fx][7] = g->list2def; gx_saved_c[fx] = g->list1def; gx_saved_t[fx] = 0;
  }
  memcpy(gx_now, gx_saved[fx], sizeof gx_now); gx_now_c = gx_saved_c[fx]; gx_now_t = gx_saved_t[fx];
  if (gx_dialog(fx, gx_saved[fx], gx_saved_c[fx], (int)gx_saved[fx][7], gx_saved_t[fx], gxfns[fx], 1)) {
    memcpy(gx_saved[fx], gx_now, sizeof gx_now); gx_saved_c[fx] = gx_now_c; gx_saved_t[fx] = gx_now_t; gx_have[fx] = 1;
  }
}

/* ------------------------------------------------------------------ the Filter Gallery: a stack */
#define FG_ROWS 4
static int fg_fx[FG_ROWS] = { GX_DRY_BRUSH, -1, -1, -1 };
static GalleryEffect fg_eff[FG_ROWS], fg_last[FG_ROWS];
static int fg_init, fg_edit, fg_nlast;
static const char *fg_labels[GX_N + 2];
static int fg_stack(Doc *d, GalleryEffect *s)
{
  int k, n = 0;
  for (k = 0; k < FG_ROWS; k++) if (fg_fx[k] >= 0) { s[n] = fg_eff[k]; s[n].fx = fg_fx[k]; s[n].fg = d->fg & 0xFFFFFF; s[n].bg = d->bg & 0xFFFFFF; n++; }
  return n;
}
/* the settings dialog of row fg_edit: the whole stack previewed as its sliders move */
static void fg_preview(Doc *d, const LONG *v, int c, int t)
{
  GalleryEffect s[FG_ROWS];
  int n;
  gx_effect(fg_fx[fg_edit], d, v, c, t, &fg_eff[fg_edit]);
  if ((n = fg_stack(d, s)) > 0 && flt_gallery(d, s, n, "Filter Gallery") < 0) out_of_memory();
}
/* Filter > Last Filter after the Filter Gallery: the stack again */
static void fg_again(Doc *d, const LONG *v, int c, int t)
{
  int k;
  (void)v; (void)c; (void)t;
  for (k = 0; k < fg_nlast; k++) { fg_last[k].fg = d->fg & 0xFFFFFF; fg_last[k].bg = d->bg & 0xFFFFFF; }
  if (fg_nlast && flt_gallery(d, fg_last, fg_nlast, "Filter Gallery") < 0) out_of_memory();
}
static void filter_gallery(void)
{
  static const ULONG ends[] = { ID_DLGOK, ID_DLGBTN0, ID_DLGBTN0 + 1, ID_DLGBTN0 + 2, ID_DLGBTN0 + 3, 0 };
  Doc *d = cur_doc();
  int k, saved_fx[FG_ROWS];
  GalleryEffect saved_eff[FG_ROWS];
  if (!d) return;
  if (!fg_init) {
    for (k = 0; k < FG_ROWS; k++) gallery_default(fg_fx[k] >= 0 ? fg_fx[k] : 0, &fg_eff[k]);
    fg_init = 1;
  }
  fg_labels[0] = tr("None");
  for (k = 0; k < GX_N; k++) fg_labels[k + 1] = tr(gallery_defs[k].name);
  fg_labels[GX_N + 1] = NULL;
  memcpy(saved_fx, fg_fx, sizeof saved_fx); memcpy(saved_eff, fg_eff, sizeof saved_eff);
  for (;;) {
    Object *grp, *w, *cyc[FG_ROWS], *btn[FG_ROWS];
    ULONG id;
    char lab[FG_ROWS][40];
    grp = ColGroup(3), End;
    if (!grp) return;
    for (k = 0; k < FG_ROWS; k++) {
      snprintf(lab[k], sizeof lab[k], tr("Effect %d:"), k + 1);
      DoMethod(grp, OM_ADDMEMBER, (ULONG)QLabel(lab[k], MUIO_Label_Centered));
      DoMethod(grp, OM_ADDMEMBER, (ULONG)(cyc[k] = CycleObject, MUIA_Cycle_Entries, (ULONG)fg_labels, MUIA_Cycle_Active, fg_fx[k] + 1, MUIA_CycleChain, 1, End));
      DoMethod(grp, OM_ADDMEMBER, (ULONG)(btn[k] = QButton(tr("Settings..."))));
    }
    w = dialog_window("Filter Gallery", VGroup, Child, grp,
          Child, TextObject, MUIA_Text_Contents, (ULONG)tr("The effects are applied from the top down. Settings shows one's\nsliders, with the whole stack previewed on the picture."), MUIA_Font, MUIV_Font_Tiny, End,
        End, NULL);
    if (!w) return;
    for (k = 0; k < FG_ROWS; k++) DoMethod(btn[k], MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGBTN0 + k);
    id = run_modal(w, ends, NULL);
    for (k = 0; k < FG_ROWS; k++) {
      int f = (int)xget(cyc[k], MUIA_Cycle_Active) - 1;
      if (f != fg_fx[k] && f >= 0) gallery_default(f, &fg_eff[k]);
      fg_fx[k] = f;
    }
    MUI_DisposeObject(w);
    if (id == ID_DLGOK) {
      GalleryEffect s[FG_ROWS];
      int n = fg_stack(d, s);
      if (!n) return;
      if (!mem_ok(gallery_memory(d, s, n))) return;
      if (flt_gallery(d, s, n, "Filter Gallery") < 0) out_of_memory();
      memcpy(fg_last, s, sizeof s); fg_nlast = n;
      dlg_set_last(fg_again);
      return;
    }
    if (id >= ID_DLGBTN0 && id < ID_DLGBTN0 + FG_ROWS) {
      int r = (int)(id - ID_DLGBTN0);
      GalleryEffect keep;
      LONG init[8];
      void *top;
      GalleryEffect *e = &fg_eff[r];
      if (fg_fx[r] < 0) continue;
      keep = *e;
      for (k = 0; k < 8; k++) init[k] = k < GX_SLIDERS ? e->v[k] : 0;
      init[7] = e->list2;
      fg_edit = r;
      top = (void *)d->undo;
      if (gx_dialog(fg_fx[r], init, e->list1, e->list2, e->tick, fg_preview, 0)) {
        /* the settings are kept; the stack is applied when the Filter Gallery's OK is pressed */
        if ((void *)d->undo != top && doc_can_undo(d)) doc_discard_last(d);
      } else *e = keep;
      DoMethod(canvas, MUIM_Canvas_Refresh);
      continue;
    }
    /* Cancel: the stack as it was */
    memcpy(fg_fx, saved_fx, sizeof saved_fx); memcpy(fg_eff, saved_eff, sizeof saved_eff);
    return;
  }
}

/* ------------------------------------------------------------------ Color Lookup */
static Lut3D *lut_cur;
static char lut_path[300] = "PROGDIR:LUTs/";
static void f_lookup(Doc *d, const LONG *v, int c, int t) { if (lut_cur) adj_color_lookup(d, lut_cur, c, t, (int)v[0]); }
static void color_lookup(void)
{
  static const DlgParam p[] = { { "Opacity (%):", 1, 100, 100 } };
  static const char *const interp[] = { "Trilinear", "Tetrahedral", NULL };
  static char info[120];
  Doc *d = cur_doc();
  char err[120];
  Lut3D *l;
  if (!d || !mem_ok(area_bytes(d))) return;
  if (!ui_file_request(0, "Choose a 3D LUT (.cube)", lut_path, sizeof lut_path, "#?.cube")) return;
  if (!(l = lut_load_cube(lut_path, err, sizeof err))) { ask("OK", "%s", tr(err)); return; }
  lut_free(lut_cur); lut_cur = l;
  snprintf(info, sizeof info, "%s (%d x %d x %d)", l->title[0] ? l->title : (const char *)FilePart((STRPTR)lut_path), l->n, l->n, l->n);
  dlg_sliders("Color Lookup", p, 1, interp, "Interpolation:", 1, NULL, NULL, 0, "Dither", 0, NULL, f_lookup,
              TextObject, MUIA_Text_Contents, (ULONG)info, End, 1);
}

/* ------------------------------------------------------------------ Equalize with a selection */
static void f_equalize(Doc *d, const LONG *v, int c, int t) { (void)v; (void)t; adj_equalize_ex(d, c ? EQ_ENTIRE : EQ_SELECTED); }
void dlgx_equalize(void)
{
  static const char *const modes[] = { "Equalize Selected Area Only", "Equalize Entire Image Based on Selected Area", NULL };
  dlg_sliders("Equalize", NULL, 0, modes, "Selection:", 0, NULL, NULL, 0, NULL, 0, NULL, f_equalize, NULL, 0);
}

/* ------------------------------------------------------------------ Focus Area */
static const char *const sel_ops[] = { "New Selection", "Add to Selection", "Subtract from Selection", "Intersect with Selection", NULL };
static void f_focus(Doc *d, const LONG *v, int c, int t)
{
  if (!sel_focus_area(d, (int)v[0], (int)v[1], t, v[7] == 1, c)) out_of_memory();
}
static void focus_area(void)
{
  static const DlgParam p[] = { { "In-Focus Range:", 0, 100, 50 }, { "Image Noise Level:", 0, 100, 10 } };
  static const char *const src[] = { "Current Layer", "All Layers", NULL };
  Doc *d = cur_doc();
  if (!d || !mem_ok((long)d->w * d->h * 10)) return;
  dlg_sliders("Focus Area", p, 2, sel_ops, "Output:", 0, src, "Sample:", 0, "Soften Edge", 1, NULL, f_focus,
              TextObject, MUIA_Text_Contents, (ULONG)tr("Selects where the picture is sharp. A wider range takes in softer parts;\nfine grain below the noise level isn't counted as detail."), MUIA_Font, MUIV_Font_Tiny, End, 0);
}

/* ------------------------------------------------------------------ Select and Mask */
static LONG sm_now[8];
static void f_refine(Doc *d, const LONG *v, int c, int t)
{
  RefineParams r;
  (void)c; (void)t;
  memcpy(sm_now, v, sizeof sm_now);
  r.radius = (int)v[0]; r.smooth = (int)v[1]; r.feather10 = (int)v[2]; r.contrast = (int)v[3]; r.shift = (int)v[4];
  if (!sel_refine(d, &r)) out_of_memory();
}
static void select_and_mask(void)
{
  static const DlgParam p[] = { { "Edge Radius (px):", 0, 250, 0 }, { "Smooth:", 0, 100, 0 }, { "Feather (x0.1 px):", 0, 2500, 0 },
                                { "Contrast (%):", 0, 100, 0 }, { "Shift Edge (%):", -100, 100, 0 } };
  static const char *const outs[] = { "Selection", "Layer Mask", "New Layer with Layer Mask", NULL };
  static LONG last[8];
  static int have, last_out;
  Object *outc;
  Doc *d = cur_doc();
  int out;
  if (!d) return;
  if (!d->sel) { ask("OK", "%s", tr("Select something first: Select and Mask refines the selection.")); return; }
  if (!mem_ok((long)d->w * d->h * 9)) return;
  outc = CycleObject, MUIA_Cycle_Entries, (ULONG)outs, MUIA_Cycle_Active, last_out, MUIA_CycleChain, 1, End;
  if (!dlg_sliders("Select and Mask", p, 5, NULL, NULL, 0, NULL, NULL, 0, NULL, 0, have ? last : NULL, f_refine,
                   VGroup, Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Edge Radius: pixels this near the edge are decided again by their colour.\nThe preview shows the selection's edge."), MUIA_Font, MUIV_Font_Tiny, End,
                     Child, HGroup, Child, QLabel(tr("Output To:"), MUIO_Label_Centered), Child, outc, End, End, 0)) return;
  out = (int)xget(outc, MUIA_Cycle_Active);
  last_out = out;
  memcpy(last, sm_now, sizeof last); have = 1;       /* (the sliders as they were left, for next time) */
  if (out == 1 || out == 2) {
    Layer *l;
    if (out == 2 && !doc_duplicate_layer(d, d->active)) { out_of_memory(); return; }
    l = d->lay[d->active];
    if (l->mask && !doc_mask_delete(d, d->active, 1)) { out_of_memory(); return; }
    if (!doc_mask_add(d, d->active, MK_REVEAL_SEL)) { out_of_memory(); return; }
    sel_deselect(d);
  }
}

void dlgx_command(ULONG id)
{
  if (id >= ID_GX0 && id < ID_GX0 + GX_N) { gx_command((int)(id - ID_GX0)); return; }
  switch (id) {
  case ID_FX_GALLERY: filter_gallery(); break;
  case ID_FX_LOOKUP: color_lookup(); break;
  case ID_FX_FOCUS: focus_area(); break;
  case ID_FX_REFINE: select_and_mask(); break;
  case ID_FX_ALLLAYERS: if (cur_doc()) doc_pick_all_layers(cur_doc()); break;
  case ID_FX_NOLAYERS: if (cur_doc()) doc_pick_no_layers(cur_doc()); break;
  }
}
