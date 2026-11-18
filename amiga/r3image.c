/*
 * r3image.c - round 3's picture commands: Layer > Combine Shapes, Fill Path's dialog (colour,
 * pattern or history), the new filters' dialogs, Select Sky and Subject, and the type additions.
 * The work is in the core (core/vecx.c, ...); the dialogs are dialogs.c's slider dialog (dlgx.h),
 * previewed on the picture, and long work runs as the command's job (progress and Abort).
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
#include "dlgx.h"
#include "r3ids.h"
#include "../core/util.h"
#include "../core/vecx.h"
#include "../core/pathops.h"
#include "../core/shape.h"
#include "../core/brush.h"
#include "../core/filtr3.h"
#include "../core/layers.h"
#include "../core/selr3.h"
#include "../core/hyphen.h"
#include "../core/type.h"

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

/* ------------------------------------------------------------------ Fill Path */
static const Path *fp_path;
static void fp_apply(Doc *d, const LONG *v, int c, int t)
{
  static const int modes[] = { BM_NORMAL, BM_MULTIPLY, BM_SCREEN, BM_OVERLAY, BM_DARKEN, BM_LIGHTEN };
  const Snapshot *snap = curdoc >= 0 && curdoc < MAXDOCS ? docs[curdoc].snap : NULL;
  int m = (int)v[7];
  if (!fp_path) return;
  path_fill_source(d, fp_path, c, d->fg, tool_pattern(), snap, (int)v[0], modes[m >= 0 && m < 6 ? m : 0], t);
}
void r3_fill_path(Doc *d, const Path *p)
{
  static const DlgParam ps[1] = { { "Opacity (%):", 1, 100, 100 } };
  static const char *const contents[] = { "Foreground Color", "Pattern", "History", NULL };
  static const char *const modes[] = { "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten", NULL };
  static LONG init[8] = { 100, 0, 0, 0, 0, 0, 0, 0 };
  if (!d || !p) return;
  fp_path = p;
  if (!dlg_sliders("Fill Path", ps, 1, contents, "Contents:", 0, modes, "Mode:", 0, "Anti-alias", 1, init, fp_apply,
                   TextObject, MUIA_Text_Contents, (ULONG)tr("Pattern: the pattern chosen for the tools.\nHistory: the picture as it was opened (the History Brush's source)."), MUIA_Font, MUIV_Font_Tiny, End, 0))
    ;
  fp_path = NULL;
}

/* ------------------------------------------------------------------ the filters */
static int need_pixels(Doc *d)
{
  if (d->active < 0 || d->active >= d->nlay || !layer_paintable(d, d->active)) {
    ask("OK", "%s", tr("The layer can't be painted: choose a pixel layer that isn't locked or hidden."));
    return 0;
  }
  return 1;
}
static void apply_result(int r) { if (r < 0) out_of_memory(); }

/* Field Blur: four pins, each with its place and blur; the first list picks the pin shown */
#define FB_UI 4
static LONG fb_v[FB_UI][3] = { { 50, 50, 15 }, { 20, 50, 0 }, { 80, 50, 30 }, { 50, 20, 15 } };
static int fb_cur;
static void fb_show(int c) { if (c >= 0 && c < FB_UI) { fb_cur = c; dlg_show_values(fb_v[c], 3); } }
static void f_field(Doc *d, const LONG *v, int c, int t)
{
  BlurPin pins[FB_UI];
  int k, n = (int)v[7] + 1;
  (void)t;
  if (c >= 0 && c < FB_UI && v[0] + v[1] + v[2] >= 0) { fb_v[c][0] = v[0]; fb_v[c][1] = v[1]; fb_v[c][2] = v[2]; }
  if (n < 1) n = 1;
  if (n > FB_UI) n = FB_UI;
  for (k = 0; k < n; k++) { pins[k].x = d->w * fb_v[k][0] / 100.0; pins[k].y = d->h * fb_v[k][1] / 100.0; pins[k].blur = (double)fb_v[k][2]; }
  apply_result(flt_field_blur(d, pins, n));
}
static void field_blur(Doc *d)
{
  static const DlgParam ps[3] = { { "Pin X (%):", 0, 100, 50 }, { "Pin Y (%):", 0, 100, 50 }, { "Blur (px):", 0, 200, 15 } };
  static const char *const pinl[] = { "Pin 1", "Pin 2", "Pin 3", "Pin 4", NULL };
  static const char *const npins[] = { "1", "2", "3", "4", NULL };
  static int npin = 1;
  LONG init[8];
  if (!need_pixels(d) || !mem_ok(r3_memory(d, R3M_BAND, 160))) return;
  fb_cur = 0;
  memcpy(init, fb_v[0], sizeof fb_v[0]);
  dlg_set_choice_hook(fb_show);
  dlg_sliders("Field Blur", ps, 3, pinl, "Pin:", 0, npins, "Pins:", npin, NULL, 0, init, f_field,
              TextObject, MUIA_Text_Contents, (ULONG)tr("Each pin blurs the picture around it; between the pins\nthe blur changes smoothly. Place them in per cent of the picture."), MUIA_Font, MUIV_Font_Tiny, End, 1);
}

/* HDR Toning (Local Adaptation): the last slider is Saturation or Vibrance, as the list says */
static LONG hdr_sat = 20, hdr_vib = 0;
static int hdr_list;
static LONG hdr_v[8] = { 15, 5, 100, 0, 30, 0, 0, 20 };
static void hdr_show(int c) { LONG v[8]; memcpy(v, hdr_v, sizeof v); v[7] = c ? hdr_vib : hdr_sat; hdr_list = c; dlg_show_values(v, 8); }
static void f_hdr(Doc *d, const LONG *v, int c, int t)
{
  HdrParams p;
  (void)t;
  memcpy(hdr_v, v, sizeof hdr_v);
  if (c == hdr_list) { if (c) hdr_vib = v[7]; else hdr_sat = v[7]; }
  hdr_defaults(&p);
  p.radius = (int)v[0]; p.strength = (int)v[1]; p.gamma = (int)v[2]; p.exposure = (int)v[3]; p.detail = (int)v[4];
  p.shadow = (int)v[5]; p.highlight = (int)v[6]; p.saturation = (int)hdr_sat; p.vibrance = (int)hdr_vib;
  apply_result(flt_hdr_toning(d, &p));
}
static void hdr_toning(Doc *d)
{
  static const DlgParam ps[8] = { { "Radius (px):", 1, 500, 15 }, { "Strength (x0.1):", 1, 40, 5 }, { "Gamma (x0.01):", 10, 200, 100 },
    { "Exposure (x0.01):", -500, 500, 0 }, { "Detail (%):", -100, 300, 30 }, { "Shadow:", -100, 100, 0 }, { "Highlight:", -100, 100, 0 }, { "Amount:", -100, 100, 20 } };
  static const char *const which[] = { "Saturation", "Vibrance", NULL };
  LONG init[8];
  if (!need_pixels(d) || !mem_ok(r3_memory(d, R3M_BAND, 2 * hdr_v[0] + 2))) return;
  memcpy(init, hdr_v, sizeof init); init[7] = hdr_sat; hdr_list = 0;
  dlg_set_choice_hook(hdr_show);
  dlg_sliders("HDR Toning", ps, 8, which, "Amount:", 0, NULL, NULL, 0, NULL, 0, init, f_hdr, NULL, 0);
}

/* Oil Paint with PhotoCraft's controls */
static void f_oil3(Doc *d, const LONG *v, int c, int t)
{
  OilParams p;
  (void)c;
  oil_defaults(&p);
  p.stylization = (int)v[0]; p.cleanliness = (int)v[1]; p.scale = (int)v[2]; p.bristle = (int)v[3]; p.angle = (int)v[4]; p.shine = (int)v[5]; p.lighting = t;
  apply_result(flt_oil_paint_ex(d, &p));
}
static void oil_paint(Doc *d)
{
  static const DlgParam ps[6] = { { "Stylization (x0.1):", 1, 100, 23 }, { "Cleanliness (x0.1):", 0, 100, 67 }, { "Scale (x0.1):", 1, 100, 10 },
    { "Bristle Detail (x0.1):", 0, 100, 30 }, { "Angle:", -180, 180, -60 }, { "Shine (x0.1):", 0, 100, 10 } };
  if (!need_pixels(d) || !mem_ok(r3_memory(d, R3M_BAND, 40))) return;
  dlg_sliders("Oil Paint", ps, 6, NULL, NULL, 0, NULL, NULL, 0, "Lighting", 1, NULL, f_oil3, NULL, 1);
}

/* Lens Correction: distortion, fringes, vignette, the Transform group and the scale */
static void f_lens3(Doc *d, const LONG *v, int c, int t)
{
  static const int scales[] = { 0, 50, 75, 90, 100, 110, 125, 150 };
  LensParams p;
  (void)t;
  lens_defaults(&p);
  p.distortion = (int)v[0]; p.fringe_rc = (int)v[1]; p.fringe_by = (int)v[2]; p.vig_amount = (int)v[3]; p.vig_mid = (int)v[4];
  p.vperspective = (int)v[5]; p.hperspective = (int)v[6]; p.angle = (double)v[7];
  p.auto_scale = c == 0; p.scale = c > 0 && c < 8 ? scales[c] : 100;
  apply_result(flt_lens_correction_ex(d, &p));
}
static void lens_correction(Doc *d)
{
  static const DlgParam ps[8] = { { "Remove Distortion:", -100, 100, 0 }, { "Fix Red/Cyan Fringe:", -100, 100, 0 }, { "Fix Blue/Yellow Fringe:", -100, 100, 0 },
    { "Vignette Amount:", -100, 100, 0 }, { "Vignette Midpoint:", 0, 100, 50 }, { "Vertical Perspective:", -100, 100, 0 },
    { "Horizontal Perspective:", -100, 100, 0 }, { "Angle:", -180, 180, 0 } };
  static const char *const scl[] = { "Auto Scale", "50%", "75%", "90%", "100%", "110%", "125%", "150%", NULL };
  if (!need_pixels(d) || !mem_ok(r3_memory(d, R3M_WHOLE, 0))) return;
  dlg_sliders("Lens Correction", ps, 8, scl, "Scale:", 4, NULL, NULL, 0, NULL, 0, NULL, f_lens3, NULL, 1);
}

/* Blur > Lens Blur: the iris, a depth map and its focal distance, specular highlights */
static void f_lensblur(Doc *d, const LONG *v, int c, int t)
{
  LensBlurParams p;
  lens_blur_defaults(&p);
  p.radius = (int)v[0]; p.blades = v[1] < 3 ? 0 : (int)v[1]; p.rotation = (int)v[2]; p.focal = (int)v[3];
  p.brightness = (int)v[4]; p.threshold = (int)v[5]; p.invert = t;
  p.depth = c <= 0 ? LBD_NONE : c == 1 ? LBD_TRANSPARENCY : c == 2 ? LBD_MASK : LBD_CHANNEL; p.channel = c - 3;
  apply_result(flt_lens_blur_ex(d, &p));
}
static void lens_blur(Doc *d)
{
  static const DlgParam ps[6] = { { "Radius (px):", 1, 100, 15 }, { "Blades (0 round):", 0, 8, 6 }, { "Rotation:", -180, 180, 0 },
    { "Blur Focal Distance:", 0, 255, 0 }, { "Specular Brightness:", 0, 100, 0 }, { "Threshold:", 0, 255, 255 } };
  static const char *src[4 + MAXCHANNELS + 1];
  static char names[MAXCHANNELS][40];
  int k, n = 0;
  if (!need_pixels(d) || !mem_ok(r3_memory(d, R3M_BAND, 101))) return;
  src[n++] = "None"; src[n++] = "Transparency"; src[n++] = "Layer Mask";
  for (k = 0; k < d->nchan && k < MAXCHANNELS; k++) { utf8_to_latin1(d->chan_name[k], strlen(d->chan_name[k]), names[k], sizeof names[k]); src[n++] = names[k]; }
  src[n] = NULL;
  dlg_sliders("Lens Blur", ps, 6, src, "Depth Map:", 0, NULL, NULL, 0, "Invert", 0, NULL, f_lensblur,
              TextObject, MUIA_Text_Contents, (ULONG)tr("With a depth map the blur grows with the distance from the\nfocal distance; without one everything is blurred alike."), MUIA_Font, MUIV_Font_Tiny, End, 1);
}

/* Render > Flame: along the active path (or a line across the lower part) */
static void f_flame(Doc *d, const LONG *v, int c, int t)
{
  FlameParams p;
  flame_defaults(&p);
  p.type = c; p.length = (int)v[0]; p.width = (int)v[1]; p.interval = (int)v[2]; p.angle = (int)v[3]; p.turbulence = (int)v[4]; p.jag = (int)v[5];
  p.opacity = (int)v[6]; p.style = (int)v[7]; p.lines = p.width / 3 < 4 ? 4 : p.width / 3 > 20 ? 20 : p.width / 3;
  p.custom_color = t; p.color = d->fg & 0xFFFFFF;
  apply_result(flt_flame(d, active_path(d), &p));
}
static void flame(Doc *d)
{
  static const DlgParam ps[7] = { { "Length (px):", 4, 2000, 120 }, { "Width (px):", 2, 500, 30 }, { "Interval (px):", 2, 1000, 40 }, { "Angle:", -180, 180, 0 },
    { "Turbulence:", 0, 100, 40 }, { "Jag:", 0, 100, 20 }, { "Opacity (%):", 1, 100, 80 } };
  static const char *const types[] = { "One Flame Along Path", "Multiple Flames Along Path", "Multiple Flames One Direction", NULL };
  static const char *const styles[] = { "Normal", "Violent", "Flat", NULL };
  if (!need_pixels(d) || !mem_ok(r3_memory(d, R3M_BAND, 0) + 2000000L)) return;
  dlg_sliders("Flame", ps, 7, types, "Flame Type:", 2, styles, "Style:", 0, "Use Foreground Color", 0, NULL, f_flame,
              TextObject, MUIA_Text_Contents, (ULONG)tr("The flames follow the path chosen in the Paths panel,\nor a line across the lower part of the picture."), MUIA_Font, MUIV_Font_Tiny, End, 1);
}

/* Render > Tree */
static void f_tree(Doc *d, const LONG *v, int c, int t)
{
  TreeParams p;
  tree_defaults(&p);
  p.type = c; p.base_x = (int)v[0]; p.size = (int)v[1]; p.branch_height = (int)v[2]; p.thickness = (int)v[3]; p.leaves = (int)v[4];
  p.leaf_size = (int)v[5]; p.light = (int)v[6]; p.seed = (unsigned)v[7];
  p.custom_colors = t; p.leaf_color = d->fg & 0xFFFFFF; p.branch_color = d->bg & 0xFFFFFF;
  apply_result(flt_tree(d, &p));
}
static void tree(Doc *d)
{
  static const DlgParam ps[8] = { { "Base Position (%):", 0, 100, 50 }, { "Size (%):", 5, 100, 80 }, { "Branch Height (%):", 10, 300, 100 },
    { "Branch Thickness (%):", 10, 300, 100 }, { "Leaves Amount (%):", 0, 300, 100 }, { "Leaves Size (%):", 10, 300, 100 },
    { "Light Direction:", -90, 90, -45 }, { "Arrangement:", 1, 999, 1 } };
  static const char *tl[TREE_N + 1];
  int k;
  for (k = 0; k < TREE_N; k++) tl[k] = tree_names[k];
  tl[TREE_N] = NULL;
  if (!need_pixels(d) || !mem_ok(r3_memory(d, R3M_BAND, 0) + 2000000L)) return;
  dlg_sliders("Tree", ps, 8, tl, "Base Tree Type:", 0, NULL, NULL, 0, "Custom Colors (foreground leaves, background branches)", 0, NULL, f_tree, NULL, 1);
}

/* Render > Picture Frame */
static void f_frame(Doc *d, const LONG *v, int c, int t)
{
  FrameParams p;
  frame_defaults(&p, d->w, d->h);
  p.kind = c; p.margin = (int)v[0]; p.size = (int)v[1]; p.count = (int)v[2]; p.arrangement = (int)v[3]; p.seed = (unsigned)v[4];
  p.vine_color_on = t; p.flower_color = d->fg & 0xFFFFFF;
  apply_result(flt_picture_frame(d, &p));
}
static void picture_frame(Doc *d)
{
  static DlgParam ps[5] = { { "Margin (px):", 0, 1000, 20 }, { "Size (px):", 1, 500, 15 }, { "Number:", 1, 200, 12 }, { "Arrangement (%):", 0, 100, 30 }, { "Seed:", 1, 999, 1 } };
  static const char *fl[PF_N + 1];
  FrameParams p;
  int k;
  for (k = 0; k < PF_N; k++) fl[k] = frame_names[k];
  fl[PF_N] = NULL;
  frame_defaults(&p, d->w, d->h);
  ps[0].def = p.margin; ps[1].def = p.size;
  if (!need_pixels(d) || !mem_ok(r3_memory(d, R3M_BAND, 0) + 2000000L)) return;
  dlg_sliders("Picture Frame", ps, 5, fl, "Frame:", 0, NULL, NULL, 0, "Vine", 1, NULL, f_frame,
              TextObject, MUIA_Text_Contents, (ULONG)tr("The flowers and stars take the foreground colour."), MUIA_Font, MUIV_Font_Tiny, End, 1);
}

/* ------------------------------------------------------------------ Liquify: a brush on the picture */
static struct { Doc *d; LiquifyMesh *m; u8 *orig; double lx, ly; Object *tool, *size, *dens, *pres, *rate; } lq;
static ULONG xget(Object *o, ULONG a) { ULONG v = 0; get(o, a, &v); return v; }
static Box lq_brush(int phase, double x, double y)
{
  Layer *l;
  Box b;
  double size;
  int tool;
  if (!lq.d || lq.d != cur_doc() || !lq.m) return box_make(0, 0, 0, 0);
  l = lq.d->lay[lq.d->active];
  size = (double)xget(lq.size, MUIA_Numeric_Value); tool = (int)xget(lq.tool, MUIA_Cycle_Active);
  if (phase == 0) { lq.lx = x; lq.ly = y; }
  if (phase == 2) return box_make(0, 0, 0, 0);
  liquify_stroke(lq.m, tool, lq.lx, lq.ly, x, y, size, (int)xget(lq.dens, MUIA_Numeric_Value), (int)xget(lq.pres, MUIA_Numeric_Value), (int)xget(lq.rate, MUIA_Numeric_Value));
  b = liquify_stroke_box(lq.m, lq.lx, lq.ly, x, y, size);
  /* the preview: the layer warped again there, from its copy */
  liquify_apply_box(lq.m, lq.orig, l->px, lq.d->w, lq.d->h, b);
  lq.lx = x; lq.ly = y;
  return b;
}
#define ID_LQRESTORE (ID_DLGBTN0 + 1)
static void liquify(Doc *d)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *tl[LQ_N + 1];
  static LONG last[5] = { 0, 100, 50, 100, 80 };
  Object *grp, *w, *restore;
  ULONG id = 0, sigs = 0;
  Layer *l;
  long bytes = (long)d->w * d->h * 4;
  int k, done = 0;
  if (!need_pixels(d)) return;
  if (!mem_ok(bytes * 2 + (long)(d->w / 4 + 2) * (d->h / 4 + 2) * 8)) return;
  l = d->lay[d->active];
  for (k = 0; k < LQ_N; k++) tl[k] = liquify_tool_names[k];
  tl[LQ_N] = NULL;
  memset(&lq, 0, sizeof lq);
  if (!(lq.m = liquify_new(d->w, d->h)) || !(lq.orig = (u8 *)malloc((size_t)bytes))) { liquify_free(lq.m); out_of_memory(); return; }
  memcpy(lq.orig, l->px, (size_t)bytes);
  lq.d = d;
  grp = ColGroup(2),
    Child, Label1(tr("Tool:")), Child, lq.tool = CycleObject, MUIA_Cycle_Entries, (ULONG)tr_list(tl), MUIA_Cycle_Active, last[0], MUIA_CycleChain, 1, End,
    Child, Label1(tr("Brush Size (px):")), Child, lq.size = SliderObject, MUIA_Numeric_Min, 2, MUIA_Numeric_Max, 1500, MUIA_Numeric_Value, last[1], MUIA_CycleChain, 1, End,
    Child, Label1(tr("Brush Density:")), Child, lq.dens = SliderObject, MUIA_Numeric_Min, 0, MUIA_Numeric_Max, 100, MUIA_Numeric_Value, last[2], MUIA_CycleChain, 1, End,
    Child, Label1(tr("Brush Pressure:")), Child, lq.pres = SliderObject, MUIA_Numeric_Min, 1, MUIA_Numeric_Max, 100, MUIA_Numeric_Value, last[3], MUIA_CycleChain, 1, End,
    Child, Label1(tr("Brush Rate:")), Child, lq.rate = SliderObject, MUIA_Numeric_Min, 1, MUIA_Numeric_Max, 100, MUIA_Numeric_Value, last[4], MUIA_CycleChain, 1, End,
    Child, HSpace(0), Child, restore = QButton(tr("Restore All")),
    Child, HSpace(0), Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Drag on the picture with the brush; OK applies the warp."), MUIA_Font, MUIV_Font_Tiny, End,
  End;
  if (!grp || !(w = dialog_window("Liquify", grp, NULL))) { liquify_free(lq.m); free(lq.orig); memset(&lq, 0, sizeof lq); return; }
  DoMethod(restore, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_LQRESTORE);
  /* (like run_modal, but the picture stays awake for the brush; other commands wait) */
  DoMethod(app, OM_ADDMEMBER, (ULONG)w);
  DoMethod(w, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGCANCEL);
  canvas_brush_hook = lq_brush;
  set(w, MUIA_Window_Open, TRUE);
  while (!done) {
    id = DoMethod(app, MUIM_Application_NewInput, (ULONG)&sigs);
    if (id == ID_DLGCANCEL || id == MUIV_Application_ReturnID_Quit) { id = ID_DLGCANCEL; break; }
    for (k = 0; ends[k]; k++) if (id == ends[k]) done = 1;
    if (done) break;
    if (id == ID_LQRESTORE) { liquify_reset(lq.m); memcpy(l->px, lq.orig, (size_t)bytes); DoMethod(canvas, MUIM_Canvas_Refresh); }
    if (sigs) sigs = Wait(sigs | SIGBREAKF_CTRL_C);
  }
  canvas_brush_hook = NULL;
  last[0] = (LONG)xget(lq.tool, MUIA_Cycle_Active); last[1] = (LONG)xget(lq.size, MUIA_Numeric_Value); last[2] = (LONG)xget(lq.dens, MUIA_Numeric_Value);
  last[3] = (LONG)xget(lq.pres, MUIA_Numeric_Value); last[4] = (LONG)xget(lq.rate, MUIA_Numeric_Value);
  set(w, MUIA_Window_Open, FALSE);
  DoMethod(app, OM_REMMEMBER, (ULONG)w);
  MUI_DisposeObject(w);
  /* the layer as it was; then, for OK, the warp as one step (progress and Abort) */
  memcpy(l->px, lq.orig, (size_t)bytes);
  if (id == ID_DLGOK && cur_doc() == d) {
    job_begin(d, tr("Liquify"));
    k = flt_liquify(d, lq.m);
    if (!job_end() && k < 0) out_of_memory();
  }
  liquify_free(lq.m); free(lq.orig); memset(&lq, 0, sizeof lq);
  DoMethod(canvas, MUIM_Canvas_Refresh);
}

/* ------------------------------------------------------------------ Select > Subject and Sky */
static void select_found(Doc *d, int sky)
{
  int ok;
  if (!mem_ok((long)d->w * d->h * 3 + 2000000L)) return;
  job_begin(d, tr(sky ? "Sky" : "Subject"));
  ok = sky ? sel_sky(d, SEL_REPLACE, 50, 50) : sel_subject(d, SEL_REPLACE);
  if (job_end()) return;
  if (!ok) ask("OK", "%s", tr(sky ? "No sky was found in the picture." : "Nothing stands out from the rest of the picture."));
}

/* ------------------------------------------------------------------ hyphenation patterns */
int r3_hyphen_patterns(void)
{
  static int tried;
  static const char *const files[UI_NLANG] = { "hyph_en_GB.dic", "hyph_de_DE.dic", "hyph_fr_FR.dic", "hyph_es_ES.dic", "hyph_it_IT.dic" };
  char path[64];
  int l = lang_current();
  if (hyphen_ready() || tried) return hyphen_ready();
  tried = 1;
  if (l >= 0 && l < UI_NLANG) { snprintf(path, sizeof path, "PROGDIR:Dict/%s", files[l]); if (hyphen_load(path)) return 1; }
  return hyphen_load("PROGDIR:Dict/hyph_en_GB.dic");
}

/* ------------------------------------------------------------------ Combine Shapes */
static void combine_shapes(Doc *d, int op)
{
  int i, n = 0;
  for (i = 0; i < d->nlay; i++) if (layer_shape(d->lay[i]) && (i == d->active || d->lay[i]->picked)) n++;
  if (!n || (op != CS_MERGE && n < 2)) {
    ask("OK", "%s", tr(op == CS_MERGE ? "Choose a shape layer first." : "Pick two or more shape layers in the Layers panel first."));
    return;
  }
  job_begin(d, tr(op == CS_MERGE ? "Merge Shape Components" : "Combine Shapes"));
  i = doc_combine_shapes(d, op);
  if (job_end()) return;
  if (i < 0) ask("OK", "%s", tr("Nothing is left of the shapes, or there isn't enough memory for that."));
}

int r3_command(Doc *d, ULONG id)
{
  /* (1.0's Oil Paint and Lens Correction items, with round 3's dialogs) */
  if (id == ID_F_OIL) { if (d) oil_paint(d); return 1; }
  if (id == ID_F_LENSCORR) { if (d) lens_correction(d); return 1; }
  if (id == ID_F_LENSBLUR) { if (d) lens_blur(d); return 1; }
  if (id < ID_R3_FIRST || id > ID_R3_LAST) return 0;
  if (!d) return 1;
  if (id >= ID_COMBINE0 && id < ID_COMBINE0 + CS_N) { combine_shapes(d, (int)(id - ID_COMBINE0)); return 1; }
  switch (id) {
  case ID_FIELDBLUR: field_blur(d); break;
  case ID_HDRTONING: hdr_toning(d); break;
  case ID_FLAME: flame(d); break;
  case ID_TREE: tree(d); break;
  case ID_PICFRAME: picture_frame(d); break;
  case ID_LIQUIFY: liquify(d); break;
  case ID_SELSUBJECT: select_found(d, 0); break;
  case ID_SELSKY: select_found(d, 1); break;
  case ID_TYPEONPATH: case ID_TYPEOFFPATH: case ID_TYPEAREA: {
    /* Layer > Type > Type on Active Path, Type in Active Path (area type), and Type off the Path */
    Path *p = id != ID_TYPEOFFPATH ? active_path(d) : NULL;
    const TextParams *tp;
    if (d->active < 0 || d->active >= d->nlay || !(tp = layer_text(d->lay[d->active]))) { ask("OK", "%s", tr("Choose a type layer first.")); break; }
    if (id != ID_TYPEOFFPATH && (!p || !p->na)) { ask("OK", "%s", tr("Choose a path in the Paths panel first (or draw one with the Pen tool).")); break; }
    if (id == ID_TYPEOFFPATH) { if (tp->area) doc_text_in_area(d, d->active, NULL); else doc_text_on_path(d, d->active, NULL); }
    else if (!(id == ID_TYPEAREA ? doc_text_in_area(d, d->active, p) : doc_text_on_path(d, d->active, p))) ask("OK", "%s", tr("That path is too long to keep with the text."));
    break; }
  default: break;
  }
  return 1;
}
