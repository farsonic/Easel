/*
 * toolbox.c - PhotoCraft's tools down the left of the window and the options bar along the top.
 *
 * The toolbox: one button a group of tools (the group's tool in use shown), a small triangle on
 * those with more than one; clicking the tool already chosen, or its triangle, lists the group.
 * Below, the foreground and background colours as two overlapping squares, with the swap arrow
 * and the small default (black and white) pair.
 *
 * The options bar: the tool's settings as PhotoCraft lays them out - fields that are dragged
 * left and right to change (or clicked to type a number), lists that drop down, ticks, and the
 * four selection modes as icons.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <devices/inputevent.h>
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
#include "../core/crop.h"

static int fh(void) { return theme_font()->tf_YSize; }

/* ------------------------------------------------------------------ the groups */
typedef struct { int n; int tools[4]; } Group;
static const Group groups[] = {
  { 1, { TL_MOVE } }, { 2, { TL_MARQUEE, TL_EMARQUEE } }, { 3, { TL_LASSO, TL_PLASSO, TL_MAGLASSO } }, { 1, { TL_WAND } },
  { 1, { TL_CROP } }, { 3, { TL_EYEDROPPER, TL_SAMPLER, TL_RULER } }, { 4, { TL_SPOTHEAL, TL_HEAL, TL_PATCH, TL_REDEYE } } /* 1.0 */, { 3, { TL_CLONE, TL_PATTERN, TL_HISTORY } }, { 3, { TL_BRUSH, TL_PENCIL, TL_REPLACE } }, { 3, { TL_ERASER, TL_BGERASE, TL_MAGICERASE } },
  { 2, { TL_GRADIENT, TL_BUCKET } }, { 3, { TL_BLUR, TL_SHARPEN, TL_SMUDGE } }, { 3, { TL_DODGE, TL_BURN, TL_SPONGE } },
  { 1, { TL_TEXT } }, { 3, { TL_RECT, TL_ELLIPSE, TL_LINE } }, { 1, { TL_HAND } }, { 1, { TL_ZOOM } }
};
#define NGROUPS (int)(sizeof groups / sizeof groups[0])
#define G_HAND (NGROUPS - 2)             /* the Hand's group, set apart with the Zoom's at the bottom */
static int group_of(int t)
{
  int g, k;
  for (g = 0; g < NGROUPS; g++) for (k = 0; k < groups[g].n; k++) if (groups[g].tools[k] == t) return g;
  return 0;
}
static int shown_tool[NGROUPS];          /* each group's tool last used (the one its button shows) */

#define TB_W 36                          /* the toolbox's width, and each button's height */
#define TB_H 25

struct TData { struct MUI_EventHandlerNode ehn; int shown, hot, popgroup; char labels[4][48]; const char *lp[4]; int pick, color; };
static struct MUI_CustomClass *tcls, *ocls;

static int tbcols = 1;                   /* two columns on a short screen (set up in MUIM_Setup) */
/* in two columns the Hand and Zoom start a row of their own */
static int tb_slot(int g) { return tbcols == 2 && g >= G_HAND && (G_HAND & 1) ? g + 1 : g; }
static LONG tb_y(Object *obj, int g)
{
  if (tbcols == 2) return _mtop(obj) + 6 + (tb_slot(g) / 2) * TB_H + (g >= G_HAND ? 4 : 0);
  return _mtop(obj) + 6 + g * TB_H + (g > G_HAND ? 6 : 0);
}
static LONG tb_x(Object *obj, int g)
{
  if (tbcols == 2) return _mleft(obj) + 4 + (tb_slot(g) & 1) * 29;
  return _mleft(obj) + (_mwidth(obj) - 24) / 2;
}
static LONG swatch_y(Object *obj) { return tb_y(obj, tbcols == 2 ? NGROUPS - 1 : NGROUPS) + (tbcols == 2 ? TB_H + 8 : 10); }
static void tb_draw(struct TData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  struct Screen *scr = _screen(obj);
  Doc *doc = cur_doc();
  LONG l = _mleft(obj), x = l + (_mwidth(obj) - 24) / 2, y, s;
  int g;
  ULONG fg = doc ? doc->fg : 0, bg = doc ? doc->bg : 0xFFFFFF;
  theme_fill(rp, l, _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_PANEL));
  SetAPen(rp, tpen(T_BORDER)); Move(rp, _mright(obj), _mtop(obj)); Draw(rp, _mright(obj), _mbottom(obj));
  for (g = 0; g < NGROUPS; g++) {
    int t = shown_tool[g], on = group_of(tool) == g;
    y = tb_y(obj, g); x = tb_x(obj, g);
    if (on) theme_fill(rp, x - 2, y, x + 25, y + TB_H - 3, tpen(T_PRESSED));
    else if (d->hot == g) theme_fill(rp, x - 2, y, x + 25, y + TB_H - 3, tpen(T_HOVER));
    theme_icon(rp, TOOL_ICON(t), x + 4, y + (TB_H - 3 - 16) / 2, 16, 0);
    if (groups[g].n > 1) {
      /* the small triangle: there are more in the group */
      LONG tx = x + 25, ty = y + TB_H - 4;
      SetAPen(rp, tpen(T_DIM));
      Move(rp, tx - 4, ty); Draw(rp, tx, ty); Draw(rp, tx, ty - 4); Draw(rp, tx - 4, ty);
      Move(rp, tx - 2, ty - 1); Draw(rp, tx - 1, ty - 1); Draw(rp, tx - 1, ty - 2);
    }
    if (g == G_HAND && tbcols == 1) { SetAPen(rp, tpen(T_BORDER)); Move(rp, x, y - 3); Draw(rp, x + 23, y - 3); }
  }
  x = l + (_mwidth(obj) - 24) / 2;
  /* the colours: background behind and below right, foreground in front */
  y = swatch_y(obj); s = 17;
  theme_rect(rp, x + 7, y + 7, x + 8 + s, y + 8 + s, tpen(T_DIM));
  draw_rgb_box(rp, scr, x + 8, y + 8, x + 7 + s, y + 7 + s, bg);
  theme_rect(rp, x - 1, y - 1, x + s, y + s, tpen(T_TEXT));
  draw_rgb_box(rp, scr, x, y, x + s - 1, y + s - 1, fg);
  /* the swap arrow (top right) and the default pair (bottom left) */
  theme_icon(rp, IC_SWAPCOL, x + 18, y - 4, 10, 0);
  draw_rgb_box(rp, scr, x - 1 + 3, y + s + 4, x + 4 + 3, y + s + 9, 0xFFFFFF);
  draw_rgb_box(rp, scr, x - 1, y + s + 2, x + 4, y + s + 7, 0x000000);
  theme_rect(rp, x - 1, y + s + 2, x + 4, y + s + 7, tpen(T_DIM));
}
/* what each tool does, for its help bubble (in TL_ order) */
static const char *const tool_help[TL_N] = {
  "Moves the layer, or what is selected, as you drag. The arrow keys nudge it a pixel at a time.",
  "Selects a rectangle: drag across what you want. Shift adds to the selection, Alt takes away.",
  "Selects an ellipse: drag across what you want. Shift adds to the selection, Alt takes away.",
  "Selects a freehand shape: drag round what you want, and let go to close it.",
  "Selects a shape with straight sides: click each corner, then double-click or press Return.",
  "Selects the area of similar colour where you click. Tolerance sets how similar.",
  "Crops the picture: drag a box, adjust its handles, then press Return.",
  "Picks up the colour you click as the foreground colour (Alt-click: the background).",
  "Paints with a copy of another part of the picture: Alt-click the source first.",
  "Paints soft-edged strokes in the foreground colour.",
  "Draws hard-edged lines in the foreground colour, a pixel exactly.",
  "Rubs out to transparency (or to the background colour on the Background layer).",
  "Fills the area of similar colour where you click with the foreground colour.",
  "Fills with a blend of colours: drag from where it starts to where it ends.",
  "Softens what you paint over.",
  "Brings out the detail in what you paint over.",
  "Smears the colours along as you drag, like a finger in wet paint.",
  "Lightens what you paint over.",
  "Darkens what you paint over.",
  "Takes the colour out of (or puts more into) what you paint over.",
  "Writes text in the foreground colour: click where it goes and type.",
  "Draws a filled rectangle in the foreground colour (rounded with a radius).",
  "Draws a filled ellipse in the foreground colour.",
  "Draws a straight line in the foreground colour.",
  "Scrolls the picture as you drag. Hold the space bar for it with any tool.",
  "Zooms in where you click; Alt-click zooms out.",
  "Paints with the chosen pattern.",
  "Paints back the picture as it was when it was opened (or at the last snapshot).",
  "Paints the foreground colour over the colours you brush, keeping their light and shade.",
  "Rubs out the colour under the brush's centre, wherever it reaches, leaving the rest.",
  "Rubs out the area of similar colour where you click, at once.",
  "Selects along edges as you go: click to start, move round the edge (clicking to pin it down), then double-click or press Return. Backspace takes the last point back.",
  "Measures a distance and an angle: drag between two points. Straighten Layer then turns the layer so the line is level.",
  /* 1.0 */
  "Paints out spots and blemishes: brush over them, and when you let go they are filled from the picture round them.",
  "Paints with texture from another part of the picture (Alt-click the source first), blended into the colour and light of where you paint when you let go.",
  "Mends an area from another: draw round it, then drag the selection to the part that should replace it.",
  "Takes the red out of eyes in flash photographs: click on the pupil, or drag a box round the eye."
};

static int tb_hit(Object *obj, LONG mx, LONG my, int *corner)
{
  LONG x = _mleft(obj) + (_mwidth(obj) - 24) / 2, y;
  int g;
  *corner = 0;
  if (mx < _mleft(obj) || mx > _mright(obj)) return -1;
  for (g = 0; g < NGROUPS; g++) {
    LONG gx = tb_x(obj, g);
    y = tb_y(obj, g);
    if (my >= y && my < y + TB_H - 2 && mx >= gx - 2 && mx <= gx + 26) { *corner = mx >= gx + 18 && my >= y + TB_H - 12; return g; }
  }
  y = swatch_y(obj);
  if (mx >= x + 17 && my >= y - 5 && my < y + 7 && mx <= x + 30) return 100;            /* swap */
  if (mx <= x + 7 && my >= y + 18 && my <= y + 27) return 101;                           /* default */
  if (mx >= x && mx < x + 17 && my >= y && my < y + 17) return 102;                       /* foreground */
  if (mx >= x + 8 && mx < x + 26 && my >= y + 8 && my < y + 26) return 103;              /* background */
  return -1;
}
static ULONG tdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct TData *d;
  switch (msg->MethodID) {
  case OM_NEW: {
    int g;
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->hot = -1;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
    for (g = 0; g < NGROUPS; g++) shown_tool[g] = groups[g].tools[0];
    return (ULONG)obj;
  }
  case OM_SET: {
    struct TagItem *t = FindTagItem(MUIA_Popup_Chosen, ((struct opSet *)msg)->ops_AttrList);
    d = INST_DATA(cl, obj);
    if (t && d->popgroup >= 0 && (int)t->ti_Data < groups[d->popgroup].n) {
      d->pick = groups[d->popgroup].tools[t->ti_Data];
      SetAttrs(obj, MUIA_Tools_Pick, d->pick, TAG_DONE);
    }
    break;
  }
  case OM_GET: {
    struct opGet *g = (struct opGet *)msg;
    d = INST_DATA(cl, obj);
    if (g->opg_AttrID == MUIA_Tools_Pick) { *g->opg_Storage = (ULONG)d->pick; return TRUE; }
    if (g->opg_AttrID == MUIA_Tools_Color) { *g->opg_Storage = (ULONG)d->color; return TRUE; }
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    LONG h = tbcols == 2 ? 6 + (tb_slot(NGROUPS - 1) / 2 + 1) * TB_H + 4 + 8 + 36 : 6 + NGROUPS * TB_H + 6 + 10 + 36, w = tbcols == 2 ? 64 : TB_W;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += w; m->MinMaxInfo->DefWidth += w; m->MinMaxInfo->MaxWidth += w;
    m->MinMaxInfo->MinHeight += h; m->MinMaxInfo->DefHeight += h; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    tbcols = _screen(obj)->Height < 600 ? 2 : 1;
    theme_setup(obj);
    theme_handler(obj, &((struct TData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup: theme_cleanup(obj); break;
  case MUIM_Show: ((struct TData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct TData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) tb_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Tools_Refresh:
    d = INST_DATA(cl, obj);
    shown_tool[group_of(tool)] = tool;
    if (d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  case MUIM_CreateShortHelp: {
    /* the tool under the pointer: its name and key, what it does, and the others in its group */
    struct MUIP_CreateShortHelp *m = (struct MUIP_CreateShortHelp *)msg;
    static char name[80], body[400];
    int corner, h = tb_hit(obj, m->mx, m->my, &corner);
    static const char *const sw[] = { "Switch Colors", "Default Colors", "Foreground Color", "Background Color" };
    static const char *const swb[] = { "Swaps the foreground and background colours (X).", "Black foreground, white background (D).",
      "The colour you paint with. Click to choose another.", "The colour the eraser leaves, and the gradient's other end. Click to choose another." };
    if (h >= 100 && h <= 103) return (ULONG)theme_bubble(tr(sw[h - 100]), tr(swb[h - 100]));
    if (h < 0 || h >= NGROUPS) return 0;
    {
      int t = shown_tool[h], k;
      snprintf(name, sizeof name, "%s (%c)", tr(tool_names[t]), tool_keys[t]);
      snprintf(body, sizeof body, "%s", tr(tool_help[t]));
      if (groups[h].n > 1) {
        strncat(body, " ", sizeof body - strlen(body) - 1);
        strncat(body, tr("Also here (click its triangle):"), sizeof body - strlen(body) - 1);
        {
          int first = 1;
          for (k = 0; k < groups[h].n; k++) if (groups[h].tools[k] != t) {
            strncat(body, first ? " " : ", ", sizeof body - strlen(body) - 1);
            strncat(body, tr(tool_names[groups[h].tools[k]]), sizeof body - strlen(body) - 1);
            first = 0;
          }
          strncat(body, ".", sizeof body - strlen(body) - 1);
        }
      }
      return (ULONG)theme_bubble(name, body);
    }
  }
  case MUIM_DeleteShortHelp: return 0;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    int corner, h;
    d = INST_DATA(cl, obj);
    if (!im || !d->shown) return 0;
    h = tb_hit(obj, im->MouseX, im->MouseY, &corner);
    if (im->Class == IDCMP_MOUSEMOVE) {
      int hot = h >= 0 && h < NGROUPS ? h : -1;
      if (hot != d->hot) { d->hot = hot; MUI_Redraw(obj, MADF_DRAWOBJECT); }
      return 0;
    }
    if (im->Class != IDCMP_MOUSEBUTTONS || im->Code != SELECTDOWN || h < 0) return 0;
    if (h >= 100) {
      if (h == 100) DoMethod(_app(obj), MUIM_Application_ReturnID, ID_SWAPCOLORS);
      else if (h == 101) DoMethod(_app(obj), MUIM_Application_ReturnID, ID_DEFCOLORS);
      else { d->color = h == 102 ? 1 : 2; SetAttrs(obj, MUIA_Tools_Color, d->color, TAG_DONE); }
      return MUI_EventHandlerRC_Eat;
    }
    if (groups[h].n > 1 && (corner || group_of(tool) == h)) {
      /* the group's tools listed beside the button */
      struct Window *w = _window(obj);
      int k;
      for (k = 0; k < groups[h].n; k++) {
        snprintf(d->labels[k], sizeof d->labels[k], "%s   %c", tr(tool_names[groups[h].tools[k]]), tool_keys[groups[h].tools[k]]);
        d->lp[k] = d->labels[k];
      }
      d->popgroup = h;
      popup_list(obj, w->LeftEdge + _mright(obj) + 2, w->TopEdge + tb_y(obj, h), 120, d->lp, groups[h].n, -1, MUIA_Popup_Chosen);
      return MUI_EventHandlerRC_Eat;
    }
    d->pick = shown_tool[h];
    SetAttrs(obj, MUIA_Tools_Pick, d->pick, TAG_DONE);
    return MUI_EventHandlerRC_Eat;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *toolbox_class(void)
{
  if (!tcls) tcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct TData), (APTR)tdispatch);
  return tcls;
}

/* ------------------------------------------------------------------ the options bar */
enum { F_SCRUB, F_CHOICE, F_CHECK, F_SELOPS, F_BUTTON, F_TEXT };
typedef struct {
  int kind;
  const char *label;
  int *val;
  int min, max;                          /* F_SCRUB: the range; F_BUTTON: the command (min) */
  const char *suffix;                    /* F_SCRUB: "px", "%" */
  const char *const *choices; int nchoices;
} Field;
#define MAXF 10
static const char *const blendlist[BM_N] = { "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten", "Difference", "Add", "Subtract",
  "Color Dodge", "Color Burn", "Soft Light", "Hard Light", "Exclusion", "Hue", "Saturation", "Color", "Luminosity", "Dissolve", "Linear Burn", "Darker Color", "Lighter Color", "Vivid Light", "Linear Light", "Pin Light", "Hard Mix", "Divide" };
static const char *const samples[] = { "Point Sample", "3 by 3 Average", "5 by 5 Average" };
static const char *const layersample[] = { "Current Layer", "All Layers" };
static const char *const gradkinds[] = { "Linear", "Radial", "Angle", "Reflected", "Diamond" };
static const char *const gradcols[] = { "Foreground to Background", "Foreground to Transparent" };
static const char *const aligns[] = { "Left", "Centre", "Right" };
static const char *const fillsrc[] = { "Foreground", "Pattern" };
static const char *const rplmodes[] = { "Hue", "Saturation", "Color", "Luminosity" };
static const char *const samplings[] = { "Continuous", "Once", "Background Swatch" };
static const char *const limitlist[] = { "Contiguous", "Discontiguous", "Find Edges" };
static const char *fontlist[64];
static const char *const mqstyles[] = { "Normal", "Fixed Ratio", "Fixed Size", "Single Row", "Single Column" };
static const char *croplist[32];

static int fields_for(int t, Field *f)
{
  int n = 0, i;
#define SCRUB(l, v, a, b, s) (f[n].kind = F_SCRUB, f[n].label = l, f[n].val = &(v), f[n].min = a, f[n].max = b, f[n].suffix = s, n++)
#define CHOICE(l, v, c, k) (f[n].kind = F_CHOICE, f[n].label = l, f[n].val = &(v), f[n].choices = c, f[n].nchoices = k, n++)
#define CHECK(l, v) (f[n].kind = F_CHECK, f[n].label = l, f[n].val = &(v), n++)
#define BUTTON(l, id) (f[n].kind = F_BUTTON, f[n].label = l, f[n].min = id, n++)
#define TEXT(l) (f[n].kind = F_TEXT, f[n].label = l, n++)
#define SELOPS() (f[n].kind = F_SELOPS, f[n].label = "", f[n].val = &opt.sel_op, n++)
  if (xform_pending) {
    /* 0.7: Free Transform's numbers and its buttons, whatever the tool */
    if (prefs.xform_numbers) TEXT(xform_info);      /* (0.8: Preferences > Tools) */
    BUTTON("Apply", ID_COMMIT); BUTTON("Cancel", ID_CANCEL);
    TEXT("Corners scale (Shift: in proportion), outside turns; Ctrl-corner distorts");
    return n;
  }
  switch (t) {
  case TL_MOVE: CHECK("Auto-Select Layer", opt.auto_select); TEXT("Drag to move the layer or the selection; Alt-drag moves a copy"); break;
  case TL_MARQUEE: case TL_EMARQUEE: SELOPS(); SCRUB("Feather:", opt.feather, 0, 250, "px");
    if (t == TL_EMARQUEE) { if (opt.mq_style > MQS_SIZE) opt.mq_style = MQS_NORMAL; CHECK("Anti-alias", opt.sel_antialias); }
    CHOICE("Style:", opt.mq_style, mqstyles, t == TL_EMARQUEE ? 3 : 5);
    if (opt.mq_style == MQS_RATIO || opt.mq_style == MQS_SIZE) { SCRUB("Width:", opt.mq_w, 1, 10000, opt.mq_style == MQS_SIZE ? "px" : ""); SCRUB("Height:", opt.mq_h, 1, 10000, opt.mq_style == MQS_SIZE ? "px" : ""); }
    else TEXT("Shift adds, Alt subtracts");
    break;
  case TL_LASSO: case TL_PLASSO: SELOPS(); SCRUB("Feather:", opt.feather, 0, 250, "px"); CHECK("Anti-alias", opt.sel_antialias);
    if (t == TL_PLASSO) TEXT("Click the corners; double-click or Return closes it");
    break;
  case TL_MAGLASSO: SELOPS(); SCRUB("Width:", opt.ml_width, 1, 256, "px"); SCRUB("Contrast:", opt.ml_contrast, 1, 100, "%"); SCRUB("Frequency:", opt.ml_freq, 0, 100, "");
    CHECK("Anti-alias", opt.sel_antialias); SCRUB("Feather:", opt.feather, 0, 250, "px");
    break;
  case TL_WAND: SELOPS(); SCRUB("Tolerance:", opt.tolerance, 0, 255, ""); CHECK("Anti-alias", opt.sel_antialias); CHECK("Contiguous", opt.contiguous); CHECK("Sample All Layers", opt.all_layers); break;
  case TL_CROP:
    for (i = 0; i < ncrop_presets && i < 32; i++) croplist[i] = crop_presets[i].name;
    if (opt.crop_preset >= i) opt.crop_preset = 0;
    CHOICE("", opt.crop_preset, croplist, i); BUTTON(opt.crop_swap ? "Swap (H:W)" : "Swap (W:H)", ID_CROPSWAP);
    BUTTON("Crop", ID_COMMIT); BUTTON("Cancel", ID_CANCEL); TEXT("Drag a box, adjust its handles, then Return"); break;
  case TL_RULER: TEXT(ruler_info[0] ? ruler_info : "Drag to measure"); BUTTON("Straighten Layer", ID_STRAIGHTEN); CHECK("Whole Picture", opt.ruler_image);
    BUTTON("Clear", ID_RULERCLEAR); break;
  case TL_SAMPLER: CHOICE("Sample Size:", opt.sample, samples, 3); BUTTON("Clear", ID_SAMPCLEAR); TEXT("Click to place up to four; drag one to move it; Alt-click removes it"); break;
  case TL_EYEDROPPER: CHOICE("Sample Size:", opt.sample, samples, 3); CHOICE("Sample:", opt.all_layers, layersample, 2); TEXT("Alt-click picks the background colour"); break;
  /* 1.0 */
  case TL_SPOTHEAL: SCRUB("Size:", opt.size[t], 1, 2500, "px"); SCRUB("Hardness:", opt.hardness[t], 0, 100, "%"); TEXT("Type: Proximity Match"); break;
  case TL_HEAL: SCRUB("Size:", opt.size[t], 1, 2500, "px"); SCRUB("Hardness:", opt.hardness[t], 0, 100, "%"); TEXT("Alt-click sets the source"); break;
  case TL_PATCH: SELOPS(); TEXT("Draw round the area, then drag it to the patch"); break;
  case TL_REDEYE: SCRUB("Pupil Size:", opt.redeye_pupil, 1, 100, "%"); SCRUB("Darken Amount:", opt.redeye_darken, 1, 100, "%"); break;
  case TL_CLONE: SCRUB("Size:", opt.size[t], 1, 2500, "px"); SCRUB("Hardness:", opt.hardness[t], 0, 100, "%"); SCRUB("Opacity:", opt.opacity[t], 1, 100, "%");
    SCRUB("Flow:", opt.flow[t], 1, 100, "%"); TEXT("Alt-click sets the source"); break;
  case TL_BRUSH: SCRUB("Size:", opt.size[t], 1, 2500, "px"); SCRUB("Hardness:", opt.hardness[t], 0, 100, "%"); CHOICE("Mode:", opt.mode[t], blendlist, BM_N);
    SCRUB("Opacity:", opt.opacity[t], 1, 100, "%"); SCRUB("Flow:", opt.flow[t], 1, 100, "%"); BUTTON("Brush Settings...", ID_WIN_BRUSHSET); break;
  case TL_PATTERN: SCRUB("Size:", opt.size[t], 1, 2500, "px"); SCRUB("Hardness:", opt.hardness[t], 0, 100, "%"); SCRUB("Opacity:", opt.opacity[t], 1, 100, "%");
    if (npat_names) CHOICE("", opt.pattern, pat_names, npat_names);
    CHECK("Aligned", opt.pattern_aligned); break;
  case TL_HISTORY: SCRUB("Size:", opt.size[t], 1, 2500, "px"); SCRUB("Hardness:", opt.hardness[t], 0, 100, "%"); CHOICE("Mode:", opt.mode[t], blendlist, BM_N);
    SCRUB("Opacity:", opt.opacity[t], 1, 100, "%"); BUTTON("Snapshot", ID_SNAPSHOT); TEXT("Paints back the snapshot"); break;
  case TL_REPLACE: SCRUB("Size:", opt.size[t], 1, 2500, "px"); CHOICE("Mode:", opt.replace_mode, rplmodes, 4); CHOICE("", opt.sampling, samplings, 3);
    CHOICE("Limits:", opt.limits, limitlist, 3); SCRUB("Tolerance:", opt.tolerance, 0, 255, ""); break;
  case TL_BGERASE: SCRUB("Size:", opt.size[t], 1, 2500, "px"); CHOICE("", opt.sampling, samplings, 3); CHOICE("Limits:", opt.limits, limitlist, 3);
    SCRUB("Tolerance:", opt.tolerance, 0, 255, ""); CHECK("Protect Foreground", opt.protect_fg); break;
  case TL_MAGICERASE: SCRUB("Tolerance:", opt.tolerance, 0, 255, ""); SCRUB("Opacity:", opt.fill_opacity, 1, 100, "%"); CHECK("Anti-alias", opt.antialias);
    CHECK("Contiguous", opt.contiguous); CHECK("All Layers", opt.all_layers); break;
  case TL_PENCIL: SCRUB("Size:", opt.size[t], 1, 2500, "px"); CHOICE("Mode:", opt.mode[t], blendlist, BM_N); SCRUB("Opacity:", opt.opacity[t], 1, 100, "%"); break;
  case TL_ERASER: SCRUB("Size:", opt.size[t], 1, 2500, "px"); SCRUB("Hardness:", opt.hardness[t], 0, 100, "%"); SCRUB("Opacity:", opt.opacity[t], 1, 100, "%");
    SCRUB("Flow:", opt.flow[t], 1, 100, "%"); break;
  case TL_BUCKET: CHOICE("Fill:", opt.fill_source, fillsrc, 2);
    if (opt.fill_source && npat_names) CHOICE("", opt.pattern, pat_names, npat_names);
    SCRUB("Opacity:", opt.fill_opacity, 1, 100, "%"); SCRUB("Tolerance:", opt.tolerance, 0, 255, ""); CHECK("Anti-alias", opt.antialias);
    CHECK("Contiguous", opt.contiguous); CHECK("All Layers", opt.all_layers); break;
  case TL_GRADIENT: if (ngrad_names) CHOICE("", opt.grad_preset, grad_names, ngrad_names);
    BUTTON("Edit...", ID_GRADEDIT); CHOICE("Style:", opt.grad_kind, gradkinds, 5); CHOICE("Mode:", opt.mode[t], blendlist, BM_N);
    SCRUB("Opacity:", opt.opacity[t], 1, 100, "%"); CHECK("Reverse", opt.grad_reverse); CHECK("Dither", opt.grad_dither); break;
  case TL_BLUR: case TL_SHARPEN: case TL_SMUDGE: SCRUB("Size:", opt.size[t], 1, 2500, "px"); SCRUB("Hardness:", opt.hardness[t], 0, 100, "%");
    SCRUB("Strength:", opt.strength[t], 1, 100, "%"); break;
  case TL_DODGE: case TL_BURN: case TL_SPONGE: SCRUB("Size:", opt.size[t], 1, 2500, "px"); SCRUB("Hardness:", opt.hardness[t], 0, 100, "%");
    SCRUB(t == TL_SPONGE ? "Flow:" : "Exposure:", opt.strength[t], 1, 100, "%"); break;
  case TL_TEXT:
    for (i = 0; i < font_count() && i < 64; i++) fontlist[i] = font_name(i);
    if (i) CHOICE("", opt.text_font, fontlist, i);
    SCRUB("Size:", opt.text_size, 4, 1000, "px"); CHOICE("Align:", opt.text_align, aligns, 3); CHECK("Anti-alias", opt.antialias);
    TEXT("Click where the text goes"); break;
  case TL_RECT: SCRUB("Radius:", opt.shape_fill_radius, 0, 1000, "px"); SCRUB("Opacity:", opt.opacity[t], 1, 100, "%"); TEXT("Drag; Shift makes a square"); break;
  case TL_ELLIPSE: SCRUB("Opacity:", opt.opacity[t], 1, 100, "%"); TEXT("Drag; Shift makes a circle"); break;
  case TL_LINE: SCRUB("Weight:", opt.line_width, 1, 1000, "px"); SCRUB("Opacity:", opt.opacity[t], 1, 100, "%"); break;
  case TL_HAND: case TL_ZOOM: BUTTON("100%", ID_ACTUAL); BUTTON("Fit Screen", ID_FIT); BUTTON("Zoom In", ID_ZOOMIN); BUTTON("Zoom Out", ID_ZOOMOUT);
    if (t == TL_ZOOM) TEXT("Alt-click zooms out");
    break;
  }
  return n;
#undef SCRUB
#undef CHOICE
#undef CHECK
#undef BUTTON
#undef TEXT
#undef SELOPS
}

struct OData {
  struct MUI_EventHandlerNode ehn;
  int shown, n, drag, hot, popfield;
  Field f[MAXF];
  LONG x0[MAXF], x1[MAXF], vx0[MAXF];   /* each field's extent, and where its value box starts */
  LONG grabx; int grabv, moved;
};
static LONG ob_h(void) { return fh() + 14; }
static void field_value(Field *f, char *buf, size_t cap)
{
  if (f->kind == F_SCRUB) snprintf(buf, cap, "%d%s", *f->val, f->suffix);
  else if (f->kind == F_CHOICE) snprintf(buf, cap, "%s", *f->val >= 0 && *f->val < f->nchoices ? tr(f->choices[*f->val]) : "");
  else buf[0] = 0;
}
/* the fields laid out across the bar (after the tool's icon) */
/* a setting's explanation, for its help bubble, by its label */
static const char *field_help(const Field *f)
{
  static const struct { const char *label, *help; } h[] = {
    { "Size:", "The brush's width in pixels. Drag left or right to change it, or click to type it. [ and ] change it too." },
    { "Hardness:", "How sharp the brush's edge is: 100% is crisp, lower fades it out." },
    { "Opacity:", "How strongly it paints: lower lets what is underneath show through." },
    { "Flow:", "How much paint each dab lays down; strokes build up where they cross." },
    { "Mode:", "How the paint mixes with the colours under it." },
    { "Feather:", "Softens the selection's edge by this many pixels." },
    { "Tolerance:", "How different a colour may be and still count as the same (0-255)." },
    { "Strength:", "How strongly the tool works." },
    { "Exposure:", "How much lighter or darker each stroke makes it." },
    { "Radius:", "Rounds the rectangle's corners by this many pixels." },
    { "Weight:", "The line's width in pixels." },
    { "Style:", "The shape of the gradient: in a line, round from the middle, and so on." },
    { "Sample Size:", "Picks up one pixel, or the average of a small square round it." },
    { "Sample:", "Picks up the colour of the layer in use only, or of everything you see." },
    { "Align:", "Lines the text up on the point you clicked: left, centre or right." },
    { "Contiguous", "Only the area touching the pixel you click, not every pixel of that colour." },
    { "Sample All Layers", "Looks at everything you see, not just the layer in use." },
    { "All Layers", "Looks at everything you see, not just the layer in use." },
    { "Anti-alias", "Smooths the edges." },
    { "Auto-Select Layer", "Clicking chooses the topmost layer with something under the pointer." },
    { "Reverse", "Runs the gradient the other way." },
    { "Crop", "Crops to the box (Return)." },
    { "Apply", "Applies the transform (Return, or double-click inside it)." },
    { "Style:", "Normal follows the pointer; Fixed Ratio keeps the width and height in proportion; Fixed Size makes a box exactly that big; Single Row and Single Column select one pixel's line where you click." },
    { "Width:", "The width: of the box (Fixed Size), its share of the proportion (Fixed Ratio), or how far from the pointer the Magnetic Lasso looks for an edge." },
    { "Height:", "The height of the box (Fixed Size), or its share of the proportion (Fixed Ratio)." },
    { "Contrast:", "How strong an edge must be for the Magnetic Lasso to cling to it." },
    { "Frequency:", "How often the Magnetic Lasso pins its path down by itself: higher pins it more often." },
    { "Straighten Layer", "Turns the layer so the Ruler's line is level (or upright). With Whole Picture, every layer is turned and the canvas cropped." },
    { "Whole Picture", "Straighten turns every layer and crops away the empty corners." },
    { "Clear", "Takes the Ruler's line away." },
    { "Cancel", "Forgets the box (Esc)." },
    { "100%", "Shows the picture at its own size, a pixel a pixel." },
    { "Fit Screen", "Zooms so the whole picture fits the window." },
    { "Zoom In", "Zooms in a step." },
    { "Zoom Out", "Zooms out a step." },
  };
  int i;
  for (i = 0; i < (int)(sizeof h / sizeof h[0]); i++) if (!strcmp(h[i].label, f->label)) return h[i].help;
  if (f->kind == F_CHOICE && f->choices == fontlist) return "The font the text is written in.";
  if (f->kind == F_CHOICE && f->choices == gradcols) return "The gradient's colours: foreground to background, or the foreground fading out.";
  if (f->kind == F_CHOICE && f->choices == croplist) return "The crop's proportions or size: free, the picture's own, a ratio, or a size it is resampled to.";
  if (f->kind == F_BUTTON && f->min == ID_CROPSWAP) return "Swaps the preset's width and height (portrait or landscape).";
  return NULL;
}
static void ob_layout(struct OData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG x = _mleft(obj) + 44;
  int i;
  d->n = fields_for(tool, d->f);
  for (i = 0; i < d->n; i++) {
    Field *f = &d->f[i];
    LONG lw = f->label[0] ? theme_textlen(rp, theme_font(), f->label) + 5 : 0, vw = 0;
    d->x0[i] = x;
    switch (f->kind) {
    case F_SCRUB: vw = theme_textlen(rp, theme_font(), "2500px") + 12; break;
    case F_CHOICE: {
      int k;
      for (k = 0; k < f->nchoices; k++) { LONG w = theme_textlen(rp, theme_font(), f->choices[k]); if (w > vw) vw = w; }
      if (vw > 160) vw = 160;
      vw += 26;
      break; }
    case F_CHECK: lw = 0; vw = 16 + theme_textlen(rp, theme_font(), f->label); break;
    case F_SELOPS: lw = 0; vw = 4 * 22; break;
    case F_BUTTON: lw = 0; vw = theme_textlen(rp, theme_font(), f->label) + 20; break;
    case F_TEXT: lw = 0; vw = theme_textlen(rp, theme_font(), f->label); break;
    }
    d->vx0[i] = x + lw;
    d->x1[i] = x + lw + vw;
    x = d->x1[i] + (f->kind == F_SELOPS ? 18 : 14);
  }
}
static void ob_draw(struct OData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG t = _mtop(obj), b = _mbottom(obj), my = (t + b) / 2, vt = my - fh() / 2 - 3, vb = my + fh() / 2 + 3;
  int i;
  char buf[80];
  theme_fill(rp, _mleft(obj), t, _mright(obj), b, tpen(T_PANEL));
  SetAPen(rp, tpen(T_BORDER)); Move(rp, _mleft(obj), b); Draw(rp, _mright(obj), b);
  /* the tool's icon, and a divider */
  theme_icon(rp, TOOL_ICON(tool), _mleft(obj) + 10, my - 9, 18, 0);
  SetAPen(rp, tpen(T_BORDER)); Move(rp, _mleft(obj) + 36, t + 4); Draw(rp, _mleft(obj) + 36, b - 4);
  ob_layout(d, obj);
  for (i = 0; i < d->n; i++) {
    Field *f = &d->f[i];
    LONG x0 = d->vx0[i], x1 = d->x1[i];
    if (x1 > _mright(obj) - 2) break;
    if (f->label[0] && (f->kind == F_SCRUB || f->kind == F_CHOICE))
      theme_text(rp, theme_font(), d->x0[i], my - fh() / 2, f->label, tpen(d->hot == i && f->kind == F_SCRUB ? T_TEXT : T_DIM));
    switch (f->kind) {
    case F_SCRUB: case F_CHOICE:
      theme_fill(rp, x0, vt, x1, vb, tpen(T_WHITE));
      theme_rect(rp, x0, vt, x1, vb, tpen(d->hot == i ? T_ACCENT : T_BORDER2));
      field_value(f, buf, sizeof buf);
      {
        struct TextExtent te;
        ULONG fit;
        SetFont(rp, theme_font());
        fit = TextFit(rp, (STRPTR)buf, (ULONG)strlen(buf), &te, NULL, 1, (ULONG)(x1 - x0 - (f->kind == F_CHOICE ? 20 : 8)), 100);
        SetAPen(rp, tpen(T_TEXT)); SetDrMd(rp, JAM1);
        Move(rp, x0 + 5, my - fh() / 2 + theme_font()->tf_Baseline);
        Text(rp, (STRPTR)buf, fit);
      }
      if (f->kind == F_CHOICE) {
        LONG ax = x1 - 10, ay = my - 1;
        SetAPen(rp, tpen(T_DIM));
        Move(rp, ax - 3, ay - 1); Draw(rp, ax + 3, ay - 1); Move(rp, ax - 2, ay); Draw(rp, ax + 2, ay); Move(rp, ax - 1, ay + 1); Draw(rp, ax + 1, ay + 1);
        WritePixel(rp, ax, ay + 2);
      }
      break;
    case F_CHECK:
      theme_fill(rp, x0, my - 5, x0 + 10, my + 5, tpen(T_WHITE));
      theme_rect(rp, x0, my - 5, x0 + 10, my + 5, tpen(d->hot == i ? T_ACCENT : T_BORDER2));
      if (*f->val) {
        SetAPen(rp, tpen(T_TEXT));
        Move(rp, x0 + 2, my); Draw(rp, x0 + 4, my + 2); Draw(rp, x0 + 8, my - 3);
        Move(rp, x0 + 2, my + 1); Draw(rp, x0 + 4, my + 3); Draw(rp, x0 + 8, my - 2);
      }
      theme_text(rp, theme_font(), x0 + 16, my - fh() / 2, f->label, tpen(T_TEXT));
      break;
    case F_SELOPS: {
      int k;
      for (k = 0; k < 4; k++) {
        LONG bx = x0 + k * 22;
        if (*f->val == k) theme_fill(rp, bx, vt, bx + 20, vb, tpen(T_PRESSED));
        else if (d->hot == i) theme_rect(rp, bx, vt, bx + 20, vb, tpen(T_BORDER));
        theme_icon(rp, IC_SELNEW + k, bx + 2, my - 8, 16, 0);
      }
      break; }
    case F_BUTTON:
      theme_fill(rp, x0, vt, x1, vb, tpen(d->hot == i ? T_HOVER : T_WHITE));
      theme_rect(rp, x0, vt, x1, vb, tpen(T_BORDER2));
      theme_text(rp, theme_font(), x0 + 10, my - fh() / 2, f->label, tpen(T_TEXT));
      break;
    case F_TEXT:
      theme_text(rp, theme_font(), x0, my - fh() / 2, f->label, tpen(T_GROUPLABEL));
      break;
    }
  }
}
static int ob_hit(struct OData *d, LONG mx, LONG my, Object *obj)
{
  int i;
  if (my < _mtop(obj) || my > _mbottom(obj)) return -1;
  for (i = 0; i < d->n; i++) if (mx >= d->x0[i] && mx <= d->x1[i] && d->f[i].kind != F_TEXT) return i;
  return -1;
}
static void ob_redraw(struct OData *d, Object *obj) { if (d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT); }
static ULONG odispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct OData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->hot = -1; d->drag = -1;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
    return (ULONG)obj;
  case OM_SET: {
    struct TagItem *t = FindTagItem(MUIA_Popup_Chosen, ((struct opSet *)msg)->ops_AttrList);
    d = INST_DATA(cl, obj);
    if (t && d->popfield >= 0 && d->popfield < d->n && d->f[d->popfield].kind == F_CHOICE) {
      *d->f[d->popfield].val = (int)t->ti_Data;
      ob_redraw(d, obj);
    }
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 200; m->MinMaxInfo->DefWidth += 800; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += ob_h(); m->MinMaxInfo->DefHeight += ob_h(); m->MinMaxInfo->MaxHeight += ob_h();
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct OData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup: theme_cleanup(obj); break;
  case MUIM_Show: ((struct OData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct OData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) ob_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Opts_Refresh:
    ob_redraw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_CreateShortHelp: {
    struct MUIP_CreateShortHelp *m = (struct MUIP_CreateShortHelp *)msg;
    static char name[80];
    int h;
    const Field *f;
    d = INST_DATA(cl, obj);
    if (m->mx < _mleft(obj) + 40) {
      snprintf(name, sizeof name, "%s (%c)", tr(tool_names[tool]), tool_keys[tool]);
      return (ULONG)theme_bubble(name, tr("The tool in use. Its settings are along this bar."));
    }
    ob_layout(d, obj);
    if ((h = ob_hit(d, m->mx, m->my, obj)) < 0) return 0;
    f = &d->f[h];
    if (f->kind == F_SELOPS) {
      static const char *const n[] = { "New Selection", "Add to Selection", "Subtract from Selection", "Intersect with Selection" };
      static const char *const b[] = { "Each selection you make replaces the one before.", "What you select is added to the selection (or hold Shift).",
        "What you select is taken out of the selection (or hold Alt).", "Only what is in both the old and the new selection stays selected (or Shift and Alt)." };
      int k = (int)((m->mx - d->vx0[h]) / 22);
      if (k < 0 || k > 3) return 0;
      return (ULONG)theme_bubble(tr(n[k]), tr(b[k]));
    }
    {
      const char *hp = field_help(f);
      char lab[60];
      size_t n;
      strncpy(lab, tr(f->label[0] ? f->label : "Setting"), sizeof lab - 1); lab[sizeof lab - 1] = 0;
      n = strlen(lab);
      if (n && lab[n - 1] == ':') lab[n - 1] = 0;
      return (ULONG)theme_bubble(lab, hp ? tr(hp) : "");
    }
  }
  case MUIM_DeleteShortHelp: return 0;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    int h;
    d = INST_DATA(cl, obj);
    if (!im || !d->shown) return 0;
    if (im->Class == IDCMP_MOUSEMOVE) {
      if (d->drag >= 0) {
        Field *f = &d->f[d->drag];
        LONG dx = im->MouseX - d->grabx;
        int range = f->max - f->min, v;
        /* a pixel a step for small ranges, faster for big ones */
        v = d->grabv + (int)(range > 300 ? dx * (range / 300 + 1) : range > 120 ? dx : dx / 2);
        if (v < f->min) v = f->min;
        if (v > f->max) v = f->max;
        if (dx > 2 || dx < -2) d->moved = 1;
        if (v != *f->val && d->moved) { *f->val = v; ob_redraw(d, obj); }
        return MUI_EventHandlerRC_Eat;
      }
      h = ob_hit(d, im->MouseX, im->MouseY, obj);
      if (h != d->hot) { d->hot = h; ob_redraw(d, obj); }
      return 0;
    }
    if (im->Class != IDCMP_MOUSEBUTTONS) return 0;
    if (im->Code == SELECTUP && d->drag >= 0) {
      Field *f = &d->f[d->drag];
      d->drag = -1;
      if (!d->moved) {
        /* clicked, not dragged: the number typed */
        LONG v = *f->val;
        char t[80];
        snprintf(t, sizeof t, "%s", tr(f->label));
        if (ask_number("Tool Options", t, &v, f->min, f->max)) *f->val = (int)v;
      }
      ob_redraw(d, obj);
      return MUI_EventHandlerRC_Eat;
    }
    if (im->Code != SELECTDOWN) return 0;
    h = ob_hit(d, im->MouseX, im->MouseY, obj);
    if (h < 0) return 0;
    {
      Field *f = &d->f[h];
      switch (f->kind) {
      case F_SCRUB: d->drag = h; d->grabx = im->MouseX; d->grabv = *f->val; d->moved = 0; break;
      case F_CHECK: *f->val = !*f->val; ob_redraw(d, obj); break;
      case F_SELOPS: { int k = (int)((im->MouseX - d->vx0[h]) / 22); if (k >= 0 && k < 4) *f->val = k; ob_redraw(d, obj); break; }
      case F_BUTTON: DoMethod(_app(obj), MUIM_Application_ReturnID, (ULONG)f->min); break;
      case F_CHOICE: {
        struct Window *w = _window(obj);
        d->popfield = h;
        popup_list(obj, w->LeftEdge + d->vx0[h], w->TopEdge + _mbottom(obj) - 2, d->x1[h] - d->vx0[h], f->choices, f->nchoices, *f->val, MUIA_Popup_Chosen);
        break; }
      }
    }
    return MUI_EventHandlerRC_Eat;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *optbar_class(void)
{
  if (!ocls) ocls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct OData), (APTR)odispatch);
  return ocls;
}
void toolbox_free(void)
{
  if (tcls) { MUI_DeleteCustomClass(tcls); tcls = NULL; }
  if (ocls) { MUI_DeleteCustomClass(ocls); ocls = NULL; }
}
