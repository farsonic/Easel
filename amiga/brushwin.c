/*
 * brushwin.c - Easel 0.5's brush windows (see brushwin.h): the Brushes panel, the Brush Settings
 * window and the DPaint Brush window, drawn in Easel's own theme like the options bar and the
 * panels (scrub fields dragged left and right or clicked to type, drop-down lists, ticks).
 *
 * Both windows are one class, a "sheet": a list of sections, each a title (clicked to fold it,
 * with a tick to switch it on where it has one) and its rows. What is in a sheet is built by a
 * function each time it is drawn, straight from brushui.b, so it always shows the settings as
 * they are.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <exec/memory.h>
#include <devices/inputevent.h>
#include <libraries/asl.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "theme.h"
#include "lang.h"
#include "brushwin.h"
#include "../core/toolopts.h"

#define BW_ID(a, b, c, d) (((ULONG)(a) << 24) | ((ULONG)(b) << 16) | ((ULONG)(c) << 8) | (ULONG)(d))
#define WHEEL_UP 0x7A
#define WHEEL_DOWN 0x7B

BrushUI brushui;
static Object *setwin, *dpwin, *setsheet, *dpsheet, *panel;
static struct MUI_CustomClass *scls, *pcls;
static int fh(void) { return theme_font()->tf_YSize; }
static void changed_all(void);

/* ------------------------------------------------------------------ the file requester */
int ui_file_request(int save, const char *title, char *path, size_t cap, const char *pattern)
{
  struct FileRequester *fr;
  char dir[400], file[108];
  int ok = 0;
  Object *w = win;
  strncpy(dir, path, sizeof dir - 1); dir[sizeof dir - 1] = 0;
  *PathPart((STRPTR)dir) = 0;
  strncpy(file, path[0] ? (const char *)FilePart((STRPTR)path) : "", sizeof file - 1); file[sizeof file - 1] = 0;
  if (!(fr = (struct FileRequester *)MUI_AllocAslRequestTags(ASL_FileRequest, ASLFR_TitleText, (ULONG)tr(title),
        ASLFR_DoSaveMode, save, ASLFR_InitialDrawer, (ULONG)dir, ASLFR_InitialFile, (ULONG)file,
        (ULONG)(pattern ? ASLFR_InitialPattern : TAG_IGNORE), (ULONG)pattern, (ULONG)(pattern ? ASLFR_DoPatterns : TAG_IGNORE), TRUE, TAG_DONE))) return 0;
  {
    ULONG wp = 0;
    if (w) get(w, MUIA_Window_Window, &wp);
    if (MUI_AslRequestTags(fr, (ULONG)(wp ? ASLFR_Window : TAG_IGNORE), wp, TAG_DONE)) {
      strncpy(path, (const char *)fr->fr_Drawer, cap - 1); path[cap - 1] = 0;
      ok = AddPart((STRPTR)path, fr->fr_File, cap) && fr->fr_File[0];
    }
  }
  MUI_FreeAslRequest(fr);
  return ok;
}
static int ends_with(const char *s, const char *e)
{
  size_t a = strlen(s), b = strlen(e);
  return a >= b && str_ieq(s + a - b, e);
}

/* ================================================================== the sheet */
enum { PI_SECTION, PI_SCRUB, PI_CHECK, PI_CHOICE, PI_BUTTONS, PI_LABEL, PI_PREVIEW };
typedef struct {
  int kind;
  const char *label;
  int *val;                           /* the setting; a section's switch (NULL: none) */
  int min, max;                       /* a scrub's range; a section's number; buttons: the first command */
  const char *suffix;
  const char *const *choices; int nchoices;   /* a choice's entries; the buttons' labels */
} PItem;
#define MAXITEMS 100
#define MAXSEC 16
struct SData {
  struct MUI_EventHandlerNode ehn;
  int shown, kind, n, drag, hot, hotsub, popitem, moved, grabv;
  LONG grabx, first, total, lastvis, lasttotal;
  Object *scroll;
  UBYTE open[MAXSEC];
  PItem it[MAXITEMS];
  LONG y0[MAXITEMS], ht[MAXITEMS];    /* each row's top (from the content's top, 0 if folded away) and height */
  UBYTE *buf; LONG bufcap;
};

/* the commands of buttons */
enum { C_ROUNDTIP = 1, C_SYMCENTRE, C_NEWSEED, C_PICKUP, C_PICKUPSEL, C_RELEASE, C_FLIPX, C_FLIPY, C_ROT90, C_ROT270,
       C_ROTANY, C_HALF, C_DOUBLE, C_SCALE, C_OUTLINE, C_LOAD, C_SAVE };

/* values shown differently from how they are kept */
static int ui_round, ui_fade, ui_seed, ui_rate, ui_count, ui_segments;
static const char *const ctl_list[CTL_N] = { "Off", "Fade", "Pen Pressure", "Direction", "Initial Direction", "Pen Tilt" };
static const char *const ctl_list3[3] = { "Off", "Fade", "Pen Pressure" };
static const char *const sym_list[SYM_N] = { "Off", "Vertical", "Horizontal", "Vertical and Horizontal", "Radial", "Mandala", "Diagonal", "Tile" };
static const char *const erase_list[3] = { "Brush", "Pencil", "Block" };
static const char *const smp_list[3] = { "Continuous", "Once", "Background Swatch" };
static const char *const stc_list[3] = { "Off", "Background Colour", "Locked Palette Colours" };   /* (1.1) */
static const char *const lim_list[3] = { "Contiguous", "Discontiguous", "Find Edges" };
static const char *const rpl_list[4] = { "Hue", "Saturation", "Color", "Luminosity" };
static const char *const cb_list[CB_N] = { "Matte", "Color", "Replace", "Smear", "Shade", "Blend", "Cycle", "Tint" };
static const char *const b_tip[] = { "Round Tip" };
static const char *const b_sym[] = { "Centre on Canvas" };
static const char *const b_seed[] = { "New Seed" };
static const char *const b_pick[] = { "Pick Up", "Pick Up Selection", "Release" };
static const char *const b_flip[] = { "Flip X", "Flip Y", "Rotate 90", "Rotate -90" };
static const char *const b_rot[] = { "Rotate...", "Half", "Double", "Scale..." };
static const char *const b_out[] = { "Outline" };
static const char *const b_file[] = { "Load ILBM...", "Save ILBM..." };
static char tiplabel[64], cblabel[64];

static void to_ui(void)
{
  Brush *b = &brushui.b;
  ui_round = b->roundness > 0 ? b->roundness : 100;
  ui_fade = b->dyn.size.fade_steps > 0 ? b->dyn.size.fade_steps : 25;
  ui_seed = (int)(b->dyn.seed % 100000UL);
  ui_rate = b->dyn.buildup_rate > 0 ? b->dyn.buildup_rate : 20;
  ui_count = b->dyn.count > 0 ? b->dyn.count : 1;
  ui_segments = b->dyn.sym_segments >= 2 ? b->dyn.sym_segments : 6;
}
static void from_ui(void)
{
  Brush *b = &brushui.b;
  BrushDynamics *dy = &b->dyn;
  b->roundness = ui_round >= 100 ? 0 : ui_round;
  dy->size.fade_steps = dy->angle.fade_steps = dy->roundness.fade_steps = dy->scatter.fade_steps = dy->count_jitter.fade_steps =
    dy->fgbg.fade_steps = dy->opacity.fade_steps = dy->flow.fade_steps = ui_fade;
  dy->seed = (u32)ui_seed;
  dy->buildup_rate = ui_rate; dy->count = ui_count; dy->sym_segments = ui_segments;
}

static int build_settings(PItem *it)
{
  Brush *b = &brushui.b;
  BrushDynamics *dy = &b->dyn;
  int n = 0, sec = 0;
#define SEC(l, v) (it[n].kind = PI_SECTION, it[n].label = l, it[n].val = v, it[n].min = sec++, n++)
#define SCRUB(l, v, a, z, s) (it[n].kind = PI_SCRUB, it[n].label = l, it[n].val = &(v), it[n].min = a, it[n].max = z, it[n].suffix = s, n++)
#define CHECK(l, v) (it[n].kind = PI_CHECK, it[n].label = l, it[n].val = &(v), n++)
#define CHOICE(l, v, c, k) (it[n].kind = PI_CHOICE, it[n].label = l, it[n].val = &(v), it[n].choices = c, it[n].nchoices = k, n++)
#define BUTTONS(c, k, cmd) (it[n].kind = PI_BUTTONS, it[n].label = "", it[n].choices = c, it[n].nchoices = k, it[n].min = cmd, n++)
#define LABEL(l) (it[n].kind = PI_LABEL, it[n].label = l, n++)
  to_ui();
  SEC("Brush Tip Shape", NULL);
  if (b->tip) sprintf(tiplabel, "%s %ld x %ld", tr("Sampled tip"), (long)b->tip->w, (long)b->tip->h);
  else strcpy(tiplabel, tr("Round tip"));
  LABEL(tiplabel);
  SCRUB("Size", b->size, 1, 2500, "px");
  SCRUB("Hardness", b->hardness, 0, 100, "%");
  SCRUB("Angle", b->angle, -180, 180, "");
  SCRUB("Roundness", ui_round, 1, 100, "%");
  SCRUB("Spacing", b->spacing, 1, 1000, "%");
  CHECK("Flip X", b->flip_x); CHECK("Flip Y", b->flip_y); CHECK("Square", b->square);
  if (b->tip) BUTTONS(b_tip, 1, C_ROUNDTIP);
  SEC("Shape Dynamics", &dy->shape_on);
  SCRUB("Size Jitter", dy->size.jitter, 0, 100, "%"); CHOICE("Control", dy->size.control, ctl_list3, 3);
  SCRUB("Minimum Diameter", dy->size.minimum, 0, 100, "%");
  SCRUB("Angle Jitter", dy->angle.jitter, 0, 100, "%"); CHOICE("Control", dy->angle.control, ctl_list, CTL_N);
  SCRUB("Roundness Jitter", dy->roundness.jitter, 0, 100, "%"); CHOICE("Control", dy->roundness.control, ctl_list3, 3);
  SCRUB("Minimum Roundness", dy->roundness.minimum, 0, 100, "%");
  CHECK("Flip X Jitter", dy->flip_x_jitter); CHECK("Flip Y Jitter", dy->flip_y_jitter);
  SCRUB("Fade Steps", ui_fade, 1, 9999, "");
  SEC("Scattering", &dy->scatter_on);
  SCRUB("Scatter", dy->scatter.jitter, 0, 1000, "%"); CHOICE("Control", dy->scatter.control, ctl_list3, 3);
  CHECK("Both Axes", dy->both_axes);
  SCRUB("Count", ui_count, 1, 16, "");
  SCRUB("Count Jitter", dy->count_jitter.jitter, 0, 100, "%"); CHOICE("Control", dy->count_jitter.control, ctl_list3, 3);
  SEC("Color Dynamics", &dy->color_on);
  CHECK("Apply Per Stroke", dy->per_stroke);
  SCRUB("Foreground/Background Jitter", dy->fgbg.jitter, 0, 100, "%"); CHOICE("Control", dy->fgbg.control, ctl_list3, 3);
  SCRUB("Hue Jitter", dy->hue_jitter, 0, 100, "%");
  SCRUB("Saturation Jitter", dy->sat_jitter, 0, 100, "%");
  SCRUB("Brightness Jitter", dy->bri_jitter, 0, 100, "%");
  SCRUB("Purity", dy->purity, -100, 100, "%");
  SEC("Transfer", &dy->transfer_on);
  SCRUB("Opacity Jitter", dy->opacity.jitter, 0, 100, "%"); CHOICE("Control", dy->opacity.control, ctl_list3, 3);
  SCRUB("Flow Jitter", dy->flow.jitter, 0, 100, "%"); CHOICE("Control", dy->flow.control, ctl_list3, 3);
  CHECK("Pressure Controls Size", dy->pressure_size); CHECK("Pressure Controls Opacity", dy->pressure_opacity);
  SEC("Smoothing", NULL);
  SCRUB("Smoothing", dy->smoothing, 0, 100, "%"); CHECK("Pulled String", dy->pulled_string);
  SEC("Symmetry", NULL);
  CHOICE("Symmetry", dy->sym_mode, sym_list, SYM_N);
  SCRUB("Segments", ui_segments, 2, 32, "");
  SCRUB("Axis Angle", dy->sym_angle, -180, 180, "");
  if (dy->sym_center_set) BUTTONS(b_sym, 1, C_SYMCENTRE);
  SEC("Airbrush", &dy->buildup);
  SCRUB("Rate", ui_rate, 1, 100, "/s");
  SEC("Tool Options", NULL);
  CHOICE("Eraser Mode", b->eraser_mode, erase_list, 3);
  CHECK("Pencil Auto Erase", b->auto_erase);
  CHECK("Clone: Sample All Layers", b->sample_all);
  CHECK("Pattern Stamp: Aligned", b->pattern_aligned);
  CHOICE("Sampling", b->sampling, smp_list, 3);
  CHOICE("Limits", b->limits, lim_list, 3);
  SCRUB("Tolerance", b->tolerance, 0, 255, "");
  CHECK("Protect Foreground Color", b->protect_fg);
  CHOICE("Replace Mode", b->replace_mode, rpl_list, 4);
  CHOICE("Stencil", brushui.stencil, stc_list, 3); CHECK("Stencil Inverted", b->stencil_invert);
  SCRUB("Seed (0: new each stroke)", ui_seed, 0, 99999, "");
  BUTTONS(b_seed, 1, C_NEWSEED);
  return n;
}
static int build_dpaint(PItem *it)
{
  int n = 0, sec = 0;
  CustomBrush *c = brushui.custom;
  SEC("Custom Brush", NULL);
  if (c) sprintf(cblabel, "%ld x %ld, %s %ld, %ld", (long)c->w, (long)c->h, tr("handle"), (long)c->hx, (long)c->hy);
  else strcpy(cblabel, tr("No brush picked up"));
  it[n].kind = PI_PREVIEW; it[n].label = ""; n++;
  LABEL(cblabel);
  CHECK("Paint with It", brushui.use_custom);
  CHOICE("Mode", brushui.b.custom_mode, cb_list, CB_N);
  BUTTONS(b_pick, 3, C_PICKUP);
  CHECK("Sample Merged", brushui.pick_merged);
  CHECK("Background Colour Transparent", brushui.pick_key);
  SEC("Transform", NULL);
  BUTTONS(b_flip, 4, C_FLIPX);
  BUTTONS(b_rot, 4, C_ROTANY);
  BUTTONS(b_out, 1, C_OUTLINE);
  SEC("File", NULL);
  BUTTONS(b_file, 2, C_LOAD);
  return n;
#undef SEC
#undef SCRUB
#undef CHECK
#undef CHOICE
#undef BUTTONS
#undef LABEL
}

static LONG row_h(const PItem *p)
{
  switch (p->kind) {
  case PI_SECTION: return fh() + 12;
  case PI_PREVIEW: return 96;
  case PI_LABEL: return fh() + 4;
  default: return fh() + 9;
  }
}
/* the rows built and laid out; folded sections' rows get no height */
static void sheet_layout(struct SData *d)
{
  int i, sec = -1;
  LONG y = 2;
  d->n = d->kind == SHEET_DPAINT ? build_dpaint(d->it) : build_settings(d->it);
  for (i = 0; i < d->n; i++) {
    if (d->it[i].kind == PI_SECTION) sec = d->it[i].min;
    if (d->it[i].kind != PI_SECTION && sec >= 0 && sec < MAXSEC && !d->open[sec]) { d->y0[i] = y; d->ht[i] = 0; continue; }
    d->y0[i] = y; d->ht[i] = row_h(&d->it[i]);
    y += d->ht[i];
  }
  d->total = y + 4;
}
static LONG box_x0(Object *obj) { return _mleft(obj) + _mwidth(obj) * 52 / 100; }
static void draw_check(struct RastPort *rp, LONG x, LONG my, int on, int hot)
{
  theme_fill(rp, x, my - 5, x + 10, my + 5, tpen(T_WHITE));
  theme_rect(rp, x, my - 5, x + 10, my + 5, tpen(hot ? T_ACCENT : T_BORDER2));
  if (on) {
    SetAPen(rp, tpen(T_TEXT));
    Move(rp, x + 2, my); Draw(rp, x + 4, my + 2); Draw(rp, x + 8, my - 3);
    Move(rp, x + 2, my + 1); Draw(rp, x + 4, my + 3); Draw(rp, x + 8, my - 2);
  }
}
static void text_cut(struct RastPort *rp, LONG x, LONG y, const char *s, LONG maxw, LONG pen)
{
  struct TextExtent te;
  ULONG fit;
  if (maxw <= 0) return;
  SetFont(rp, theme_font());
  fit = TextFit(rp, (STRPTR)s, (ULONG)strlen(s), &te, NULL, 1, (ULONG)maxw, 100);
  SetAPen(rp, pen); SetDrMd(rp, JAM1);
  Move(rp, x, y + theme_font()->tf_Baseline);
  Text(rp, (STRPTR)s, fit);
}
/* the DPaint brush over a chequer, scaled to fit */
static void draw_preview(struct SData *d, Object *obj, LONG x0, LONG y0, LONG w, LONG h)
{
  CustomBrush *c = brushui.custom;
  struct RastPort *rp = _rp(obj);
  LONG x, y, need = w * h * 3, sw, shh;
  double sc;
  theme_rect(rp, x0 - 1, y0 - 1, x0 + w, y0 + h, tpen(T_BORDER2));
  if (w <= 0 || h <= 0) return;
  if (need > d->bufcap) {
    if (d->buf) FreeVec(d->buf);
    d->buf = (UBYTE *)AllocVec((ULONG)need, MEMF_ANY);
    d->bufcap = d->buf ? need : 0;
  }
  if (!d->buf) return;
  sc = 1;
  if (c) { sc = (double)w / c->w; if ((double)h / c->h < sc) sc = (double)h / c->h; if (sc > 4) sc = 4; }
  sw = c ? (LONG)(c->w * sc) : 0; shh = c ? (LONG)(c->h * sc) : 0;
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      UBYTE *o = d->buf + (y * w + x) * 3;
      int chk = ((x >> 3) ^ (y >> 3)) & 1 ? 0x99 : 0x66;
      LONG bx = x - (w - sw) / 2, by = y - (h - shh) / 2;
      o[0] = o[1] = o[2] = (UBYTE)chk;
      if (c && bx >= 0 && by >= 0 && bx < sw && by < shh) {
        LONG ix = (LONG)(bx / sc), iy = (LONG)(by / sc);
        const u8 *q;
        if (ix >= c->w) ix = c->w - 1;
        if (iy >= c->h) iy = c->h - 1;
        q = c->px + ((long)iy * c->w + ix) * 4;
        if (q[3]) { o[0] = q[0]; o[1] = q[1]; o[2] = q[2]; }
      }
    }
  draw_rgb(rp, _screen(obj), d->buf, w * 3, x0, y0, w, h);
  if (c) {
    /* the handle: a small cross */
    LONG hx = x0 + (w - sw) / 2 + (LONG)((c->hx + 0.5) * sc), hy = y0 + (h - shh) / 2 + (LONG)((c->hy + 0.5) * sc);
    SetAPen(rp, tpen(T_ACCENT));
    Move(rp, hx - 4, hy); Draw(rp, hx + 4, hy); Move(rp, hx, hy - 4); Draw(rp, hx, hy + 4);
  }
}
static void sheet_draw(struct SData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG l = _mleft(obj), r = _mright(obj), t = _mtop(obj), bx0 = box_x0(obj);
  APTR clip;
  int i;
  char buf[48];
  sheet_layout(d);
  if (d->first > d->total - _mheight(obj)) d->first = d->total - _mheight(obj);
  if (d->first < 0) d->first = 0;
  clip = (APTR)MUI_AddClipping(muiRenderInfo(obj), _mleft(obj), _mtop(obj), _mwidth(obj), _mheight(obj));
  theme_fill(rp, l, t, r, _mbottom(obj), tpen(T_PANEL));
  for (i = 0; i < d->n; i++) {
    PItem *p = &d->it[i];
    LONG y = t + d->y0[i] - d->first, h = d->ht[i], my = y + h / 2;
    if (!h || y + h < t || y > _mbottom(obj)) continue;
    switch (p->kind) {
    case PI_SECTION: {
      int open = p->min < MAXSEC && d->open[p->min];
      LONG tx = l + 8;
      theme_fill(rp, l, y + 2, r, y + h - 2, tpen(d->hot == i ? T_HOVER : T_PRESSED));
      /* the fold triangle */
      SetAPen(rp, tpen(T_DIM));
      if (open) { Move(rp, tx, my - 2); Draw(rp, tx + 6, my - 2); Draw(rp, tx + 3, my + 2); Draw(rp, tx, my - 2); }
      else { Move(rp, tx + 1, my - 3); Draw(rp, tx + 1, my + 3); Draw(rp, tx + 5, my); Draw(rp, tx + 1, my - 3); }
      tx += 12;
      if (p->val) { draw_check(rp, tx, my, *p->val, d->hot == i && d->hotsub); tx += 16; }
      theme_text(rp, theme_bold(), tx, my - fh() / 2, tr(p->label), tpen(T_TEXT));
      break; }
    case PI_SCRUB: case PI_CHOICE: {
      LONG vt = my - fh() / 2 - 3, vb = my + fh() / 2 + 3, x1 = r - 8;
      text_cut(rp, l + 14, my - fh() / 2, tr(p->label), bx0 - l - 18, tpen(d->hot == i ? T_TEXT : T_DIM));
      theme_fill(rp, bx0, vt, x1, vb, tpen(T_WHITE));
      theme_rect(rp, bx0, vt, x1, vb, tpen(d->hot == i ? T_ACCENT : T_BORDER2));
      if (p->kind == PI_SCRUB) {
        LONG fw = (x1 - bx0 - 2) * (*p->val - p->min) / (p->max > p->min ? p->max - p->min : 1);
        if (fw > 0) theme_fill(rp, bx0 + 1, vb - 2, bx0 + 1 + fw, vb - 1, tpen(T_ACCENT));
        sprintf(buf, "%d%s", *p->val, p->suffix ? p->suffix : "");
        text_cut(rp, bx0 + 5, my - fh() / 2, buf, x1 - bx0 - 8, tpen(T_TEXT));
      } else {
        LONG ax = x1 - 10, ay = my - 1;
        text_cut(rp, bx0 + 5, my - fh() / 2, *p->val >= 0 && *p->val < p->nchoices ? tr(p->choices[*p->val]) : "", x1 - bx0 - 22, tpen(T_TEXT));
        SetAPen(rp, tpen(T_DIM));
        Move(rp, ax - 3, ay - 1); Draw(rp, ax + 3, ay - 1); Move(rp, ax - 2, ay); Draw(rp, ax + 2, ay); Move(rp, ax - 1, ay + 1); Draw(rp, ax + 1, ay + 1);
      }
      break; }
    case PI_CHECK:
      draw_check(rp, l + 14, my, *p->val, d->hot == i);
      text_cut(rp, l + 30, my - fh() / 2, tr(p->label), r - l - 36, tpen(T_TEXT));
      break;
    case PI_LABEL:
      text_cut(rp, l + 14, my - fh() / 2, p->label, r - l - 20, tpen(T_GROUPLABEL));
      break;
    case PI_BUTTONS: {
      int k;
      LONG bw = (r - l - 20 - (p->nchoices - 1) * 4) / (p->nchoices ? p->nchoices : 1);
      for (k = 0; k < p->nchoices; k++) {
        LONG x0 = l + 10 + k * (bw + 4), x1 = x0 + bw - 1, tw;
        int hot = d->hot == i && d->hotsub == k;
        theme_fill(rp, x0, my - fh() / 2 - 3, x1, my + fh() / 2 + 3, tpen(hot ? T_HOVER : T_WHITE));
        theme_rect(rp, x0, my - fh() / 2 - 3, x1, my + fh() / 2 + 3, tpen(T_BORDER2));
        tw = theme_textlen(rp, theme_font(), tr(p->choices[k]));
        text_cut(rp, tw < bw - 6 ? x0 + (bw - tw) / 2 : x0 + 3, my - fh() / 2, tr(p->choices[k]), bw - 6, tpen(T_TEXT));
      }
      break; }
    case PI_PREVIEW:
      draw_preview(d, obj, l + 10, y + 4, r - l - 20, h - 8);
      break;
    }
  }
  MUI_RemoveClipping(muiRenderInfo(obj), (struct Region *)clip);
  /* the scroll bar told how much there is */
  if (d->scroll && (d->lasttotal != d->total || d->lastvis != _mheight(obj))) {
    d->lasttotal = d->total; d->lastvis = _mheight(obj);
    SetAttrs(d->scroll, MUIA_Prop_Entries, d->total, MUIA_Prop_Visible, _mheight(obj), MUIA_Prop_First, d->first, TAG_DONE);
  }
}
/* the row under y (and, for buttons, which one; for a section, 1 on its tick) */
static int sheet_hit(struct SData *d, Object *obj, LONG mx, LONG my, int *sub)
{
  int i;
  *sub = 0;
  if (mx < _mleft(obj) || mx > _mright(obj) || my < _mtop(obj) || my > _mbottom(obj)) return -1;
  for (i = 0; i < d->n; i++) {
    LONG y = _mtop(obj) + d->y0[i] - d->first;
    if (!d->ht[i] || my < y || my >= y + d->ht[i]) continue;
    if (d->it[i].kind == PI_BUTTONS) {
      LONG bw = (_mwidth(obj) - 20 - (d->it[i].nchoices - 1) * 4) / (d->it[i].nchoices ? d->it[i].nchoices : 1);
      int k = (int)((mx - _mleft(obj) - 10) / (bw + 4));
      if (k < 0 || k >= d->it[i].nchoices) return -1;
      *sub = k;
    } else if (d->it[i].kind == PI_SECTION) *sub = d->it[i].val && mx >= _mleft(obj) + 18 && mx <= _mleft(obj) + 32;
    else if (d->it[i].kind == PI_LABEL || d->it[i].kind == PI_PREVIEW) return -1;
    return i;
  }
  return -1;
}
static void sheet_redraw(struct SData *d, Object *obj) { if (d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT); }

/* ------------------------------------------------------------------ the DPaint brush's commands */
static void set_custom(CustomBrush *n)
{
  if (!n) { ask("OK", "%s", tr("There isn't enough memory for that.")); return; }
  cbrush_free(brushui.custom);
  brushui.custom = n;
  brushui.use_custom = 1;
}
static void command(int cmd)
{
  CustomBrush *c = brushui.custom;
  char err[100];
  switch (cmd) {
  case C_ROUNDTIP: brushui.b.tip = NULL; brushui.current = -1; break;
  case C_SYMCENTRE: brushui.b.dyn.sym_center_set = 0; break;
  case C_NEWSEED: brushui.b.dyn.seed = (u32)(brush_hash(brushui.b.dyn.seed, 1, 99) % 99999UL) + 1; break;
  case C_PICKUP: if (brushui.request) brushui.request(BWR_PICKUP); return;
  case C_PICKUPSEL: if (brushui.request) brushui.request(BWR_PICKUP_SEL); return;
  case C_RELEASE: cbrush_free(brushui.custom); brushui.custom = NULL; brushui.use_custom = 0; break;
  case C_LOAD:
    if (!ui_file_request(0, "Load Brush", brushui.path, sizeof brushui.path, "#?")) return;
    if (!(c = cbrush_load(brushui.path, err, sizeof err))) { ask("OK", "%s", tr(err)); return; }
    set_custom(c);
    break;
  default:
    if (!c) { ask("OK", "%s", tr("Pick up a brush from the picture first.")); return; }
    switch (cmd) {
    case C_FLIPX: set_custom(cbrush_flip(c, 0)); break;
    case C_FLIPY: set_custom(cbrush_flip(c, 1)); break;
    case C_ROT90: set_custom(cbrush_rotate90(c, 1)); break;
    case C_ROT270: set_custom(cbrush_rotate90(c, 3)); break;
    case C_HALF: set_custom(cbrush_half(c)); break;
    case C_DOUBLE: if ((long)c->w * c->h * 4 > 4L * 1024 * 1024) { ask("OK", "%s", tr("The brush is too big to double.")); return; } set_custom(cbrush_double(c)); break;
    case C_OUTLINE: { Doc *d = cur_doc(); set_custom(cbrush_outline(c, d ? d->fg : 0)); break; }
    case C_ROTANY: {
      LONG deg = 45;
      if (!ask_number("Rotate Brush", "Degrees (clockwise):", &deg, -360, 360)) return;
      set_custom(cbrush_rotate(c, (double)deg));
      break; }
    case C_SCALE: {
      LONG pc = 100;
      if (!ask_number("Scale Brush", "Per cent:", &pc, 1, 1000)) return;
      set_custom(cbrush_scale(c, (i32)(c->w * pc / 100), (i32)(c->h * pc / 100)));
      break; }
    case C_SAVE:
      if (!ui_file_request(1, "Save Brush", brushui.path, sizeof brushui.path, "#?")) return;
      if (!cbrush_save(c, brushui.path)) ask("OK", "%s", tr("The brush couldn't be saved."));
      return;
    }
  }
  changed_all();
}
/* a setting changed in a sheet: kept, and everyone told */
static void sheet_changed(int picked_size)
{
  from_ui();
  if (picked_size && brushui.picked) brushui.picked(&brushui.b);
  changed_all();
}

static ULONG sdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct SData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->kind = (int)GetTagData(MUIA_Sheet_Kind, SHEET_SETTINGS, ((struct opSet *)msg)->ops_AttrList);
    d->scroll = (Object *)GetTagData(MUIA_Sheet_Scroll, 0, ((struct opSet *)msg)->ops_AttrList);
    d->hot = d->drag = d->popitem = -1;
    d->open[0] = 1; d->open[1] = d->kind == SHEET_DPAINT; d->open[2] = d->kind == SHEET_DPAINT;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY;
    return (ULONG)obj;
  case OM_DISPOSE:
    d = INST_DATA(cl, obj);
    if (d->buf) FreeVec(d->buf);
    break;
  case OM_SET: {
    struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList, *t;
    d = INST_DATA(cl, obj);
    while ((t = NextTagItem(&tags))) {
      if (t->ti_Tag == MUIA_Sheet_First && (LONG)t->ti_Data != d->first) { d->first = (LONG)t->ti_Data; sheet_redraw(d, obj); }
      else if (t->ti_Tag == MUIA_Sheet_Scroll) d->scroll = (Object *)t->ti_Data;
      else if (t->ti_Tag == MUIA_Popup_Chosen && d->popitem >= 0 && d->popitem < d->n && d->it[d->popitem].kind == PI_CHOICE) {
        *d->it[d->popitem].val = (int)t->ti_Data;
        sheet_changed(0);
      }
    }
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 240; m->MinMaxInfo->DefWidth += 300; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += 120; m->MinMaxInfo->DefHeight += 420; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct SData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup: theme_cleanup(obj); break;
  case MUIM_Show: ((struct SData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct SData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) sheet_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Sheet_Refresh: sheet_redraw(INST_DATA(cl, obj), obj); return 0;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    int h, sub;
    d = INST_DATA(cl, obj);
    if (!im || !d->shown) return 0;
    if (im->Class == IDCMP_RAWKEY) {
      if ((im->Code == WHEEL_UP || im->Code == WHEEL_DOWN) && im->MouseX >= _mleft(obj) && im->MouseX <= _mright(obj) && im->MouseY >= _mtop(obj) && im->MouseY <= _mbottom(obj)) {
        LONG f = d->first + (im->Code == WHEEL_UP ? -3 : 3) * (fh() + 9);
        if (f > d->total - _mheight(obj)) f = d->total - _mheight(obj);
        if (f < 0) f = 0;
        if (f != d->first) { d->first = f; sheet_redraw(d, obj); if (d->scroll) SetAttrs(d->scroll, MUIA_Prop_First, f, TAG_DONE); }
        return MUI_EventHandlerRC_Eat;
      }
      return 0;
    }
    if (im->Class == IDCMP_MOUSEMOVE) {
      if (d->drag >= 0 && d->drag < d->n) {
        PItem *p = &d->it[d->drag];
        LONG dx = im->MouseX - d->grabx, bw = _mright(obj) - 8 - box_x0(obj);
        int v = d->grabv + (int)(dx * (p->max - p->min) / (bw > 20 ? bw : 20));
        if (dx > 2 || dx < -2) d->moved = 1;
        if (v < p->min) v = p->min;
        if (v > p->max) v = p->max;
        if (d->moved && v != *p->val) { *p->val = v; from_ui(); sheet_redraw(d, obj); }
        return MUI_EventHandlerRC_Eat;
      }
      h = sheet_hit(d, obj, im->MouseX, im->MouseY, &sub);
      if (h != d->hot || sub != d->hotsub) { d->hot = h; d->hotsub = sub; sheet_redraw(d, obj); }
      return 0;
    }
    if (im->Class != IDCMP_MOUSEBUTTONS) return 0;
    if (im->Code == SELECTUP && d->drag >= 0) {
      PItem *p = &d->it[d->drag];
      int size = p->val == &brushui.b.size || p->val == &brushui.b.hardness;
      d->drag = -1;
      if (!d->moved) {
        LONG v = *p->val;
        if (ask_number("Brush Settings", tr(p->label), &v, p->min, p->max)) *p->val = (int)v;
      }
      sheet_changed(size);
      return MUI_EventHandlerRC_Eat;
    }
    if (im->Code != SELECTDOWN) return 0;
    if ((h = sheet_hit(d, obj, im->MouseX, im->MouseY, &sub)) < 0) return 0;
    {
      PItem *p = &d->it[h];
      switch (p->kind) {
      case PI_SECTION:
        if (sub && p->val) { *p->val = !*p->val; sheet_changed(0); }
        else if (p->min < MAXSEC) { d->open[p->min] = !d->open[p->min]; sheet_redraw(d, obj); }
        break;
      case PI_SCRUB: d->drag = h; d->grabx = im->MouseX; d->grabv = *p->val; d->moved = 0; break;
      case PI_CHECK: *p->val = !*p->val; sheet_changed(0); break;
      case PI_CHOICE: {
        struct Window *w = _window(obj);
        LONG y = _mtop(obj) + d->y0[h] - d->first + d->ht[h];
        d->popitem = h;
        popup_list(obj, w->LeftEdge + box_x0(obj), w->TopEdge + y, _mright(obj) - 8 - box_x0(obj), p->choices, p->nchoices, *p->val, MUIA_Popup_Chosen);
        break; }
      case PI_BUTTONS: command(p->min + sub); break;
      }
    }
    return MUI_EventHandlerRC_Eat;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
static struct MUI_CustomClass *sheet_class(void)
{
  if (!scls) scls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct SData), (APTR)sdispatch);
  return scls;
}
/* a window holding a sheet and its scroll bar */
static Object *sheet_window(const char *title, ULONG id, int kind, Object **sheetp)
{
  Object *sh, *sc, *w;
  if (!app || !sheet_class() || !flatscroll_class()) return NULL;
  sc = NewObject(flatscroll_class()->mcc_Class, NULL, TAG_DONE);
  sh = NewObject(sheet_class()->mcc_Class, NULL, MUIA_Sheet_Kind, kind, MUIA_Sheet_Scroll, (ULONG)sc, MUIA_FillArea, FALSE, TAG_DONE);
  if (!sh || !sc) { if (sh) MUI_DisposeObject(sh); if (sc) MUI_DisposeObject(sc); return NULL; }
  w = QWindowObject, MUIA_Window_Title, (ULONG)tr(title), MUIA_Window_ID, id,
    WindowContents, HGroup, InnerSpacing(0, 0), MUIA_Group_Spacing, 0, Child, sh, Child, sc, End,
  End;
  if (!w) return NULL;
  DoMethod(app, OM_ADDMEMBER, (ULONG)w);
  DoMethod(w, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)w, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  DoMethod(sc, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, (ULONG)sh, 3, MUIM_Set, MUIA_Sheet_First, MUIV_TriggerValue);
  *sheetp = sh;
  return w;
}
Object *brushwin_settings_window(void)
{
  if (!setwin) setwin = sheet_window("Brush Settings", BW_ID('E', 'B', 'R', 'S'), SHEET_SETTINGS, &setsheet);
  return setwin;
}
Object *brushwin_dpaint_window(void)
{
  if (!dpwin) dpwin = sheet_window("DPaint Brush", BW_ID('E', 'B', 'R', 'D'), SHEET_DPAINT, &dpsheet);
  return dpwin;
}

/* ================================================================== the Brushes panel */
#define CELL 46
struct PData {
  struct MUI_EventHandlerNode ehn;
  int shown, hot, firstrow, nprev, hotbtn;
  UBYTE **prev;                       /* each preset's tip as CELL x CELL coverages (made when needed) */
  UBYTE *buf; LONG bufcap;
  Object *scroll;
};
static LONG cell_h(void) { return CELL + theme_small()->tf_YSize + 4; }
static LONG bar_h(void) { return fh() + 14; }
static int pcols(Object *obj) { int c = (int)((_mwidth(obj) - 4) / (CELL + 4)); return c < 1 ? 1 : c; }
static int prows(Object *obj) { LONG h = _mheight(obj) - bar_h(); int r = (int)(h / cell_h()); return r < 1 ? 1 : r; }
static const int pbtn_icons[4] = { IC_NEWLAYER, IC_TRASH, IC_OPEN, IC_SAVE };
static void prev_free(struct PData *d)
{
  int i;
  for (i = 0; i < d->nprev; i++) free(d->prev[i]);
  free(d->prev);
  d->prev = NULL; d->nprev = 0;
}
/* a preset's tip, shrunk to fit a cell */
static UBYTE *make_prev(const BrushPreset *p)
{
  Brush b = p->b;
  u8 *a, *o;
  int w, h, x, y, side = CELL - 6;
  double sc;
  b.tip = p->tip;
  if (b.size > side) b.size = side;
  if (b.size < 1) b.size = 1;
  if (!(a = brush_dab_preview(&b, &w, &h))) return NULL;
  if (!(o = (u8 *)calloc(CELL * CELL, 1))) { free(a); return NULL; }
  sc = w > side || h > side ? (double)side / (w > h ? w : h) : 1;
  for (y = 0; y < CELL; y++)
    for (x = 0; x < CELL; x++) {
      int sx = (int)((x - CELL / 2) / sc + w / 2.0), sy = (int)((y - CELL / 2) / sc + h / 2.0);
      if (sx >= 0 && sy >= 0 && sx < w && sy < h) o[y * CELL + x] = a[sy * w + sx];
    }
  free(a);
  return o;
}
static void panel_draw(struct PData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  struct Screen *scr = _screen(obj);
  LONG l = _mleft(obj), t = _mtop(obj), r = _mright(obj), b = _mbottom(obj), by = b - bar_h() + 1;
  int cols = pcols(obj), rows = prows(obj), i, n = brushui.presets.n, dark = theme_is_dark();
  UBYTE bgc = (UBYTE)(dark ? 0x2E : 0xFA), ink = (UBYTE)(dark ? 0xE6 : 0x1E);
  theme_fill(rp, l, t, r, b, tpen(T_PANEL));
  if (d->nprev != n) {
    prev_free(d);
    if (n && (d->prev = (UBYTE **)calloc((size_t)n, sizeof(UBYTE *)))) d->nprev = n;
  }
  if (CELL * CELL * 3 > d->bufcap) {
    if (d->buf) FreeVec(d->buf);
    d->buf = (UBYTE *)AllocVec(CELL * CELL * 3, MEMF_ANY);
    d->bufcap = d->buf ? CELL * CELL * 3 : 0;
  }
  if (d->firstrow > (n + cols - 1) / cols - rows) d->firstrow = (n + cols - 1) / cols - rows;
  if (d->firstrow < 0) d->firstrow = 0;
  for (i = d->firstrow * cols; i < n && i < (d->firstrow + rows) * cols; i++) {
    int k = i - d->firstrow * cols;
    LONG x = l + 4 + (k % cols) * (CELL + 4), y = t + 4 + (k / cols) * cell_h();
    char sz[12];
    if (i == brushui.current) theme_fill(rp, x - 2, y - 2, x + CELL + 1, y + cell_h() - 3, tpen(T_ACCENTDARK));
    else if (i == d->hot) theme_fill(rp, x - 2, y - 2, x + CELL + 1, y + cell_h() - 3, tpen(T_HOVER));
    if (d->prev && !d->prev[i]) d->prev[i] = make_prev(&brushui.presets.p[i]);
    if (d->buf && d->prev && d->prev[i]) {
      int q;
      for (q = 0; q < CELL * CELL; q++) {
        int a = d->prev[i][q];
        d->buf[q * 3] = d->buf[q * 3 + 1] = d->buf[q * 3 + 2] = (UBYTE)(bgc + ((ink - bgc) * a) / 255);
      }
      draw_rgb(rp, scr, d->buf, CELL * 3, x, y, CELL, CELL);
    }
    sprintf(sz, "%d", brushui.presets.p[i].b.size);
    theme_text(rp, theme_small(), x + (CELL - theme_textlen(rp, theme_small(), sz)) / 2, y + CELL + 1, sz, tpen(i == brushui.current ? T_WHITE : T_DIM));
  }
  /* the bar: the preset's name, then New, Delete, Load, Save */
  SetAPen(rp, tpen(T_BORDER)); Move(rp, l, by); Draw(rp, r, by);
  for (i = 0; i < 4; i++) {
    LONG bx = r - 4 - (4 - i) * 24;
    if (d->hotbtn == i) theme_fill(rp, bx, by + 3, bx + 21, b - 2, tpen(T_HOVER));
    theme_icon(rp, pbtn_icons[i], bx + 3, by + (bar_h() - 16) / 2, 16, 0);
  }
  if (brushui.current >= 0 && brushui.current < n) {
    char nm[64];
    struct TextExtent te;
    ULONG fit;
    utf8_to_latin1(brushui.presets.p[brushui.current].name, strlen(brushui.presets.p[brushui.current].name), nm, sizeof nm);
    SetFont(rp, theme_font());
    fit = TextFit(rp, (STRPTR)nm, (ULONG)strlen(nm), &te, NULL, 1, (ULONG)(r - l - 4 * 24 - 16), 100);
    SetAPen(rp, tpen(T_TEXT)); SetDrMd(rp, JAM1);
    Move(rp, l + 6, by + (bar_h() - fh()) / 2 + theme_font()->tf_Baseline);
    Text(rp, (STRPTR)nm, fit);
  }
  if (d->scroll) {
    LONG ent = (n + cols - 1) / cols, first = 0, vis = 0;
    get(d->scroll, MUIA_Prop_Entries, &first);
    get(d->scroll, MUIA_Prop_Visible, &vis);
    if (first != ent || vis != rows) SetAttrs(d->scroll, MUIA_Prop_Entries, ent, MUIA_Prop_Visible, rows, MUIA_Prop_First, d->firstrow, TAG_DONE);
  }
}
static int panel_hit(struct PData *d, Object *obj, LONG mx, LONG my, int *btn)
{
  int cols = pcols(obj), rows = prows(obj), c, r, i;
  LONG by = _mbottom(obj) - bar_h() + 1;
  *btn = -1;
  if (mx < _mleft(obj) || mx > _mright(obj) || my < _mtop(obj) || my > _mbottom(obj)) return -1;
  if (my > by) {
    for (i = 0; i < 4; i++) { LONG bx = _mright(obj) - 4 - (4 - i) * 24; if (mx >= bx && mx <= bx + 21) *btn = i; }
    return -1;
  }
  c = (int)((mx - _mleft(obj) - 4) / (CELL + 4)); r = (int)((my - _mtop(obj) - 4) / cell_h());
  if (c < 0 || c >= cols || r < 0 || r >= rows) return -1;
  i = (d->firstrow + r) * cols + c;
  return i < brushui.presets.n ? i : -1;
}
static void pick_preset(int i)
{
  if (i < 0 || i >= brushui.presets.n) return;
  brushui.b = preset_pick(&brushui.presets.p[i], &brushui.b);
  brushui.current = i;
  if (brushui.picked) brushui.picked(&brushui.b);
  changed_all();
}
static void panel_button(int k)
{
  char err[100];
  switch (k) {
  case 0: if (brushui.request) brushui.request(BWR_DEFINE); break;
  case 1:
    if (brushui.current < 0 || brushui.current >= brushui.presets.n) return;
    if (!ask(tr("Delete|Cancel"), "%s", tr("Delete this brush preset?"))) return;
    if (brushui.b.tip == brushui.presets.p[brushui.current].tip) brushui.b.tip = NULL;
    presets_remove(&brushui.presets, brushui.current);
    brushui.current = -1;
    changed_all();
    break;
  case 2: {
    int n;
    if (!ui_file_request(0, "Load Brushes", brushui.path, sizeof brushui.path, "#?.(brushes|abr)")) return;
    err[0] = 0;
    n = ends_with(brushui.path, ".abr") ? presets_load_abr(&brushui.presets, brushui.path, err, sizeof err) : presets_load(&brushui.presets, brushui.path);
    if (!n) ask("OK", "%s", err[0] ? tr(err) : tr("No brushes could be read from that file."));
    changed_all();
    break; }
  case 3:
    if (!ui_file_request(1, "Save Brushes", brushui.path, sizeof brushui.path, "#?.brushes")) return;
    if (!presets_save(&brushui.presets, brushui.path)) ask("OK", "%s", tr("The brushes couldn't be saved."));
    break;
  }
}
static ULONG pdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct PData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->hot = d->hotbtn = -1;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY;
    return (ULONG)obj;
  case OM_DISPOSE:
    d = INST_DATA(cl, obj);
    prev_free(d);
    if (d->buf) FreeVec(d->buf);
    break;
  case OM_SET: {
    struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList, *t;
    d = INST_DATA(cl, obj);
    while ((t = NextTagItem(&tags))) {
      if (t->ti_Tag == MUIA_BrushPanel_First && (int)t->ti_Data != d->firstrow) { d->firstrow = (int)t->ti_Data; if (d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT); }
      else if (t->ti_Tag == MUIA_Sheet_Scroll) d->scroll = (Object *)t->ti_Data;
    }
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 4 * (CELL + 4) + 4; m->MinMaxInfo->DefWidth += 5 * (CELL + 4) + 4; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += cell_h() + bar_h() + 8; m->MinMaxInfo->DefHeight += 3 * cell_h() + bar_h() + 8; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct PData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup: theme_cleanup(obj); break;
  case MUIM_Show: ((struct PData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct PData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) panel_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_BrushPanel_Refresh:
    d = INST_DATA(cl, obj);
    prev_free(d);
    if (d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  case MUIM_CreateShortHelp: {
    struct MUIP_CreateShortHelp *m = (struct MUIP_CreateShortHelp *)msg;
    static char name[96];
    static const char *const bn[4] = { "New Brush Preset", "Delete Brush Preset", "Load Brushes", "Save Brushes" };
    static const char *const bh[4] = { "Makes a brush tip from the selection (Define Brush Preset).", "Deletes the brush preset picked.",
      "Adds the brushes in a file: Easel's own, or Photoshop .abr.", "Saves these brush presets to a file." };
    int btn, i;
    d = INST_DATA(cl, obj);
    i = panel_hit(d, obj, m->mx, m->my, &btn);
    if (btn >= 0) return (ULONG)theme_bubble(tr(bn[btn]), tr(bh[btn]));
    if (i >= 0) {
      const BrushPreset *p = &brushui.presets.p[i];
      char nm[64];
      utf8_to_latin1(p->name, strlen(p->name), nm, sizeof nm);
      sprintf(name, "%s", nm);
      return (ULONG)theme_bubble(name, p->group[0] ? p->group : tr("Click to paint with this brush."));
    }
    return 0;
  }
  case MUIM_DeleteShortHelp: return 0;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    int i, btn;
    d = INST_DATA(cl, obj);
    if (!im || !d->shown) return 0;
    if (im->Class == IDCMP_RAWKEY) {
      if ((im->Code == WHEEL_UP || im->Code == WHEEL_DOWN) && im->MouseX >= _mleft(obj) && im->MouseX <= _mright(obj) && im->MouseY >= _mtop(obj) && im->MouseY <= _mbottom(obj)) {
        d->firstrow += im->Code == WHEEL_UP ? -1 : 1;
        MUI_Redraw(obj, MADF_DRAWOBJECT);
        if (d->scroll) SetAttrs(d->scroll, MUIA_Prop_First, d->firstrow, TAG_DONE);
        return MUI_EventHandlerRC_Eat;
      }
      return 0;
    }
    i = panel_hit(d, obj, im->MouseX, im->MouseY, &btn);
    if (im->Class == IDCMP_MOUSEMOVE) {
      if (i != d->hot || btn != d->hotbtn) { d->hot = i; d->hotbtn = btn; MUI_Redraw(obj, MADF_DRAWOBJECT); }
      return 0;
    }
    if (im->Class != IDCMP_MOUSEBUTTONS || im->Code != SELECTDOWN) return 0;
    if (btn >= 0) { panel_button(btn); return MUI_EventHandlerRC_Eat; }
    if (i >= 0) { pick_preset(i); return MUI_EventHandlerRC_Eat; }
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
Object *brushwin_panel(void)
{
  Object *sc, *g;
  if (!pcls && !(pcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct PData), (APTR)pdispatch))) return NULL;
  if (!flatscroll_class()) return NULL;
  sc = NewObject(flatscroll_class()->mcc_Class, NULL, TAG_DONE);
  panel = NewObject(pcls->mcc_Class, NULL, MUIA_FillArea, FALSE, MUIA_Sheet_Scroll, (ULONG)sc, TAG_DONE);
  if (!panel || !sc) { if (panel) MUI_DisposeObject(panel); if (sc) MUI_DisposeObject(sc); panel = NULL; return NULL; }
  SetAttrs(panel, MUIA_Sheet_Scroll, (ULONG)sc, TAG_DONE);
  g = HGroup, InnerSpacing(0, 0), MUIA_Group_Spacing, 0, Child, panel, Child, sc, End;
  if (!g) { panel = NULL; return NULL; }
  DoMethod(sc, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, (ULONG)panel, 3, MUIM_Set, MUIA_BrushPanel_First, MUIV_TriggerValue);
  return g;
}

/* ================================================================== the rest of the API */
static void changed_all(void)
{
  brushwin_refresh();
  if (brushui.changed) brushui.changed();
}
void brushwin_refresh(void)
{
  if (setsheet) DoMethod(setsheet, MUIM_Sheet_Refresh);
  if (dpsheet) DoMethod(dpsheet, MUIM_Sheet_Refresh);
  if (panel) DoMethod(panel, MUIM_BrushPanel_Refresh);
}
void brushwin_init(void)
{
  memset(&brushui, 0, sizeof brushui);
  presets_init(&brushui.presets);
  presets_builtin(&brushui.presets);
  brushui.current = -1;
  brushui.b.tool = PT_BRUSH; brushui.b.size = 20; brushui.b.hardness = 100; brushui.b.opacity = 100; brushui.b.flow = 100;
  brushui.b.spacing = 25; brushui.b.strength = 50; brushui.b.tolerance = 32; brushui.b.bg = 0xFFFFFF;
  brushui.b.dyn.sym_segments = 6; brushui.b.dyn.buildup_rate = 20; brushui.b.dyn.count = 1; brushui.b.dyn.seed = 0;
  brushui.pick_key = 1;
}
void brushwin_free(void)
{
  presets_free(&brushui.presets);
  cbrush_free(brushui.custom);
  brushui.custom = NULL;
  setwin = dpwin = setsheet = dpsheet = panel = NULL;   /* (the windows went with the application) */
}
void brushwin_classes_free(void)
{
  if (scls) { MUI_DeleteCustomClass(scls); scls = NULL; }
  if (pcls) { MUI_DeleteCustomClass(pcls); pcls = NULL; }
}
void brushwin_apply(Brush *b)
{
  const Brush *s = &brushui.b;
  Doc *d = cur_doc();
  b->angle = s->angle; b->roundness = s->roundness; b->flip_x = s->flip_x; b->flip_y = s->flip_y; b->square = s->square;
  b->tip = s->tip; b->spacing = s->spacing > 0 ? s->spacing : 25;
  b->dyn = s->dyn;
  /* seed 0: every stroke its own jitter (a fixed seed replays the same stroke) */
  if (!s->dyn.seed) { static u32 strokes; b->dyn.seed = brush_hash(0x45534C, ++strokes, (u32)(size_t)b) | 1; }
  b->eraser_mode = s->eraser_mode; b->auto_erase = s->auto_erase; b->sample_all = s->sample_all; b->pattern_aligned = s->pattern_aligned;
  b->sampling = s->sampling; b->limits = s->limits; b->tolerance = s->tolerance; b->protect_fg = s->protect_fg; b->replace_mode = s->replace_mode;
  b->custom_mode = s->custom_mode;
  /* 1.1: the stencil's colours, from the picture as it is now */
  {
    static u32 stc[256];
    b->nstencil = d && brushui.stencil ? stencil_colors(d, brushui.stencil, stc) : 0;
    b->stencil = b->nstencil ? stc : NULL;
    b->stencil_invert = s->stencil_invert;
  }
  b->custom = brushui.use_custom && brushui.custom && (b->tool == PT_BRUSH || b->tool == PT_PENCIL) ? brushui.custom : NULL;
  if (d) b->bg = d->bg;
}
int brushwin_define(Doc *d)
{
  Box b = d->sel ? d->selbox : doc_box(d);
  char name[64];
  int k;
  sprintf(name, "%s %d", tr("Sampled Brush"), brushui.presets.n + 1);
  if (!ask_string("Brush Name", "Name:", name, sizeof name)) return 0;
  k = presets_define(&brushui.presets, d, b, 0, name, &brushui.b);
  if (k < 0) { ask("OK", "%s", tr("Nothing there paints: a brush tip is made from the dark parts of the selection.")); return 0; }
  pick_preset(k);
  return 1;
}
int brushwin_pickup(Doc *d, Box b, int use_sel)
{
  CustomBrush *c = cbrush_pickup(d, b, use_sel, brushui.pick_merged, brushui.pick_key, d->bg);
  if (!c) { ask("OK", "%s", tr("There is nothing there to pick up.")); return 0; }
  set_custom(c);
  changed_all();
  return 1;
}
