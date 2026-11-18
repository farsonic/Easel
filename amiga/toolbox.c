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
#include "../core/paint.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "tools2ids.h"                  /* 1.2 */
#include "../core/brushx.h"
#include "../core/crop.h"
#include "../core/toolopts.h"
#include "brushwin.h"

static int fh(void) { return theme_font()->tf_YSize; }

/* ------------------------------------------------------------------ the groups */
typedef struct { int n; int tools[8]; } Group;
static const Group groups[] = {
  /* (1.2: the Artboard, Object and Quick Selection, Perspective Crop, Slice, Frame, Note, Count, Remove,
     Content-Aware Move, Art History, Mixer Brush and Rotate View tools join their groups) */
  { 2, { TL_MOVE, TL_ARTBOARD } }, { 2, { TL_MARQUEE, TL_EMARQUEE } }, { 3, { TL_LASSO, TL_PLASSO, TL_MAGLASSO } }, { 3, { TL_OBJSEL, TL_QUICKSEL, TL_WAND } },
  { 5, { TL_CROP, TL_PCROP, TL_SLICE, TL_SLICESEL, TL_FRAME } }, { 5, { TL_EYEDROPPER, TL_SAMPLER, TL_RULER, TL_NOTE, TL_COUNT } },
  { 6, { TL_SPOTHEAL, TL_HEAL, TL_PATCH, TL_REMOVE, TL_CAMOVE, TL_REDEYE } } /* 1.0 */, { 4, { TL_CLONE, TL_PATTERN, TL_HISTORY, TL_ARTHIST } },
  { 4, { TL_BRUSH, TL_PENCIL, TL_REPLACE, TL_MIXER } }, { 3, { TL_ERASER, TL_BGERASE, TL_MAGICERASE } },
  { 2, { TL_GRADIENT, TL_BUCKET } }, { 3, { TL_BLUR, TL_SHARPEN, TL_SMUDGE } }, { 3, { TL_DODGE, TL_BURN, TL_SPONGE } },
  /* 0.9: the pens, type, path selection and the shapes */
  { 4, { TL_PEN, TL_ADDANCHOR, TL_DELANCHOR, TL_CONVERT } }, { 3, { TL_TEXT, TL_VTEXT, TL_TYPEMASK } }, { 2, { TL_PATHSEL, TL_DIRECTSEL } },
  { 6, { TL_RECT, TL_ROUNDRECT, TL_ELLIPSE, TL_POLYGON, TL_LINE, TL_CUSTOMSHAPE } }, { 2, { TL_HAND, TL_ROTVIEW } }, { 1, { TL_ZOOM } }
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

struct TData { struct MUI_EventHandlerNode ehn; int shown, hot, popgroup; char labels[8][48]; const char *lp[8]; int pick, color; };
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
  "Writes text you can edit later: click and type a line, or drag a box for a paragraph. Click a type layer to edit it; Esc or another tool finishes.",
  "Draws a rectangle as a shape layer (or in pixels) in the foreground colour, rounded with a radius.",
  "Draws an ellipse as a shape layer (or in pixels) in the foreground colour. Shift makes a circle.",
  "Draws a straight line as a shape layer (or in pixels), with arrowheads if you like.",
  "Scrolls the picture as you drag. Hold the space bar for it with any tool.",
  "Zooms in where you click; Alt-click zooms out.",
  "Paints with the chosen pattern.",
  "Paints back the picture as it was when it was opened (or at the last snapshot).",
  "Paints the foreground colour over the colours you brush, keeping their light and shade.",
  "Rubs out the colour under the brush's centre, wherever it reaches, leaving the rest.",
  "Rubs out the area of similar colour where you click, at once.",
  "Selects along edges as you go: click to start, move round the edge (clicking to pin it down), then double-click or press Return. Backspace takes the last point back.",
  "Measures a distance and an angle: drag between two points. Straighten Layer then turns the layer so the line is level.",
  "Marks up to four points whose colours the Info panel shows: click to place one, drag to move it, Alt-click to remove it.",
  /* 1.0 */
  "Paints out spots and blemishes: brush over them, and when you let go they are filled from the picture round them.",
  "Paints with texture from another part of the picture (Alt-click the source first), blended into the colour and light of where you paint when you let go.",
  "Mends an area from another: draw round it, then drag the selection to the part that should replace it.",
  "Takes the red out of eyes in flash photographs: click on the pupil, or drag a box round the eye.",
  /* 0.9 */
  "Makes a selection in the shape of text: click and type, then press Esc.",
  "Writes text in columns, top to bottom: click and type, or drag a box.",
  "Draws paths: click for corners, drag for curves, click the first point to close, Return to finish. Click on the path to add or delete a point.",
  "Adds a point to a path where you click on it.",
  "Deletes the path's point you click.",
  "Turns a smooth point into a corner (click) or a corner into a smooth point (drag); drags one handle alone.",
  "Chooses and moves a whole path; with Free Transform it scales and turns the path.",
  "Chooses and moves points and handles of a path; drag across points to choose several. Backspace deletes them.",
  "Draws a rectangle with round corners as a shape layer (or in pixels).",
  "Draws a polygon or a star as a shape layer (or in pixels).",
  "Draws the active path as a shape, fitted to the box you drag.",
  /* 1.2 */
  "Selects as you paint: brush over what you want and the selection grows out to its edges. Alt takes away.",
  "Selects the object in a box you drag round it, by its colours and edges.",
  "Crops and straightens a picture taken at an angle: drag a box, move its corners onto the corners of what should be square, then press Return.",
  "Divides the picture into slices for exporting as separate files: drag a box for each.",
  "Chooses and moves slices: click one, drag it; the options bar divides or deletes it.",
  "Draws a frame (a rectangle or an ellipse) that a picture can be placed in: drag a box; Layer > Links and Artboards > Place in Frame fills it.",
  "Puts notes on the picture (they are never part of it): click to add one, click it again to change it, Alt-click to delete it.",
  "Counts things: click each one to put a numbered mark on it; Alt-click a mark to take it away.",
  "Paints like real paint: the brush's colour mixes with the colours already there as you paint. Wet, Load and Mix set how much.",
  "Paints quick stylised strokes in the colours of the snapshot (the History Brush's source), for a painted look.",
  "Removes what you brush over, filling it from the rest of the picture (this takes a while on a 68040).",
  "Moves (or copies) the selection somewhere else and fills the place it leaves from the picture round it.",
  "Turns the view (not the picture) as you drag; Shift turns in steps of 15 degrees. Reset View puts it back.",
  "Makes artboards: drag a box for each. A layer inside an artboard shows only within it."
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
    tbcols = _screen(obj)->Height < 700 ? 2 : 1;          /* (0.9: nineteen groups need 700 lines in one column) */
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
enum { F_SCRUB, F_CHOICE, F_CHECK, F_SELOPS, F_BUTTON, F_TEXT, F_SWATCH, F_ICON, F_MENU };
/* (1.1) F_MENU: a button with a list; the entry chosen sends command ids[k] */
/* (0.9) F_SWATCH: a colour, clicked for the command min; F_ICON: icon max as a small button for the
   command min (the label is its name, for the help bubble) */
typedef struct {
  int kind;
  const char *label;
  int *val;
  int min, max;                          /* F_SCRUB: the range; F_BUTTON: the command (min) */
  const char *suffix;                    /* F_SCRUB: "px", "%" */
  const char *const *choices; int nchoices;
  const ULONG *ids;                      /* F_MENU */
} Field;
#define MAXF 16
static const char *const blendlist[BM_N] = { "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten", "Difference", "Add", "Subtract",
  "Color Dodge", "Color Burn", "Soft Light", "Hard Light", "Exclusion", "Hue", "Saturation", "Color", "Luminosity", "Dissolve", "Linear Burn", "Darker Color", "Lighter Color", "Vivid Light", "Linear Light", "Pin Light", "Hard Mix", "Divide",
  "Reflect", "Glow", "Negation", "Xor", "Color Burn (Paint.NET)", "Color Dodge (Paint.NET)" };
/* round 3: the Brush's and Pencil's list, with the painting-only Behind and Clear (core/paint.h) after the layer modes */
static const char *paintlist[BM_N + 3];
static const char *const *paint_modes(void)
{
  int k;
  if (!paintlist[0]) { for (k = 0; k < BM_N; k++) paintlist[k] = blendlist[k]; paintlist[BM_BEHIND] = "Behind"; paintlist[BM_CLEAR] = "Clear"; }
  return paintlist;
}
static const char *const samples[NPICK_SIZES] = { "Point Sample", "3 by 3 Average", "5 by 5 Average", "11 by 11 Average", "31 by 31 Average", "51 by 51 Average", "101 by 101 Average" };
static const char *const layersample[] = { "Current Layer", "Current & Below", "All Layers" };
/* 1.1 */
static const char *const toneranges[] = { "Shadows", "Midtones", "Highlights" };
static const char *const spongemodes[] = { "Desaturate", "Saturate" };
static const char *const rtmodes[RETOUCH_MODES] = { "Normal", "Darken", "Lighten", "Hue", "Saturation", "Color", "Luminosity" };
static const char *const erasemodes[] = { "Brush", "Pencil", "Block" };
static const char *const movetargets[] = { "Layer", "Group" };
static const char *const zoommodes[] = { "Zoom In", "Zoom Out" };
static const char *const alignlist[] = { "Left Edges", "Horizontal Centers", "Right Edges", "Top Edges", "Vertical Centers", "Bottom Edges", "Distribute Horizontally", "Distribute Vertically" };
static const ULONG alignids[] = { ID_AL_LEFT, ID_AL_HCENTER, ID_AL_RIGHT, ID_AL_TOP, ID_AL_VCENTER, ID_AL_BOTTOM, ID_DIST_H, ID_DIST_V };
static const char *const gradkinds[] = { "Linear", "Radial", "Angle", "Reflected", "Diamond" };
static const char *const gradcols[] = { "Foreground to Background", "Foreground to Transparent" };
static const char *const aligns[] = { "Left", "Centre", "Right" };
static const char *const fillsrc[] = { "Foreground", "Pattern" };
static const char *const rplmodes[] = { "Hue", "Saturation", "Color", "Luminosity" };
static const char *const samplings[] = { "Continuous", "Once", "Background Swatch" };
static const char *const limitlist[] = { "Contiguous", "Discontiguous", "Find Edges" };
static const char *fontlist[96];
/* 0.9: type and shapes */
static const char *const tstyles[] = { "Regular", "Bold", "Italic", "Bold Italic" };
static const char *const taa[] = { "None", "Sharp", "Smooth" };
static const char *const taligns[] = { "Left", "Centre", "Right", "Justify", "Just. All" };
static const char *const shmodes[] = { "Shape Layer", "Pixels" };
static const char *const salign[] = { "Centre", "Inside", "Outside" };
static int fg_show;                     /* the foreground colour, for the Type tool's swatch */
static const char *const mqstyles[] = { "Normal", "Fixed Ratio", "Fixed Size", "Single Row", "Single Column" };
static const char *croplist[32];
/* 1.2 */
static const char *const framekinds[] = { "Rectangle", "Ellipse" };
static const char *const cammodes[] = { "Move", "Extend" };
static const char *const spottypes[] = { "Proximity Match", "Create Texture", "Content-Aware" };
static const char *const healsrc[] = { "Sampled", "Pattern" };
static const char *const patchmodes[] = { "Source", "Destination", "Content-Aware" };

static int fields_for(int t, Field *f)
{
  int n = 0, i;
#define SCRUB(l, v, a, b, s) (f[n].kind = F_SCRUB, f[n].label = l, f[n].val = &(v), f[n].min = a, f[n].max = b, f[n].suffix = s, n++)
#define CHOICE(l, v, c, k) (f[n].kind = F_CHOICE, f[n].label = l, f[n].val = &(v), f[n].choices = c, f[n].nchoices = k, n++)
#define CHECK(l, v) (f[n].kind = F_CHECK, f[n].label = l, f[n].val = &(v), n++)
#define BUTTON(l, id) (f[n].kind = F_BUTTON, f[n].label = l, f[n].min = id, n++)
#define TEXT(l) (f[n].kind = F_TEXT, f[n].label = l, n++)
#define SELOPS() (f[n].kind = F_SELOPS, f[n].label = "", f[n].val = &opt.sel_op, n++)
#define SWATCH(l, v, id) (f[n].kind = F_SWATCH, f[n].label = l, f[n].val = &(v), f[n].min = id, n++)
#define ICONBTN(l, ic, id) (f[n].kind = F_ICON, f[n].label = l, f[n].min = id, f[n].max = ic, n++)
#define MENU(l, c, k, i) (f[n].kind = F_MENU, f[n].label = l, f[n].choices = c, f[n].nchoices = k, f[n].ids = i, n++)
  if (xform_pending) {
    /* 0.7: Free Transform's numbers and its buttons, whatever the tool */
    if (prefs.xform_numbers) TEXT(xform_info);      /* (0.8: Preferences > Tools) */
    BUTTON("Apply", ID_COMMIT); BUTTON("Cancel", ID_CANCEL);
    TEXT("Corners scale (Shift: in proportion), outside turns; Ctrl-corner distorts");
    return n;
  }
  switch (t) {
  case TL_MOVE: CHECK("Auto-Select", opt.auto_select); CHOICE("", opt.move_group, movetargets, 2); CHECK("Show Transform Controls", opt.move_bounds);
    MENU("Align", alignlist, 8, alignids); TEXT("Alt-drag moves a copy"); break;
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
    CHOICE("", opt.crop_overlay, crop_overlay_names, CO_N); CHECK("Straighten", opt.crop_straighten);
    BUTTON("Crop", ID_COMMIT); BUTTON("Cancel", ID_CANCEL); TEXT(opt.crop_straighten ? "Drag along what should be level" : "Drag a box, adjust its handles (drag outside it to turn it), then Return"); break;
  case TL_RULER: TEXT(ruler_info[0] ? ruler_info : "Drag to measure"); BUTTON("Straighten Layer", ID_STRAIGHTEN); CHECK("Whole Picture", opt.ruler_image);
    BUTTON("Clear", ID_RULERCLEAR); break;
  case TL_SAMPLER: CHOICE("Sample Size:", opt.sample, samples, NPICK_SIZES); BUTTON("Clear", ID_SAMPCLEAR); TEXT("Click to place up to four; drag one to move it; Alt-click removes it"); break;
  case TL_EYEDROPPER: CHOICE("Sample Size:", opt.sample, samples, NPICK_SIZES); CHOICE("Sample:", opt.pick_from, layersample, 3); TEXT("Alt-click picks the background colour"); break;
  /* 1.0 */
  case TL_SPOTHEAL: SCRUB("Size:", opt.size[t], 1, 2500, "px"); SCRUB("Hardness:", opt.hardness[t], 0, 100, "%"); CHOICE("Type:", opt.spot_type, spottypes, 3); break;
  case TL_HEAL: SCRUB("Size:", opt.size[t], 1, 2500, "px"); CHOICE("Mode:", opt.mode[t], blendlist, BM_N); CHOICE("Source:", opt.heal_source, healsrc, 2);
    if (opt.heal_source && npat_names) CHOICE("", opt.pattern, pat_names, npat_names);
    CHECK("Aligned", opt.clone_aligned); CHOICE("Sample:", opt.clone_sample, layersample, 3);
    SCRUB("Diffusion:", opt.heal_diffusion, 1, 7, ""); SCRUB("Hardness:", opt.hardness[t], 0, 100, "%"); break;
  case TL_PATCH: SELOPS(); CHOICE("", opt.patch_mode, patchmodes, 3);
    if (opt.patch_mode == 1) CHECK("Transparent", opt.patch_transparent);
    if (opt.patch_mode == 2) { SCRUB("Structure:", opt.patch_structure, 1, 7, ""); SCRUB("Color:", opt.patch_color, 0, 10, ""); }
    TEXT(opt.patch_mode == 1 ? "Draw round the patch, then drag it where it goes" : "Draw round the area, then drag it to the patch"); break;
  case TL_REDEYE: SCRUB("Pupil Size:", opt.redeye_pupil, 1, 100, "%"); SCRUB("Darken Amount:", opt.redeye_darken, 1, 100, "%"); break;
  case TL_CLONE: SCRUB("Size:", opt.size[t], 1, 2500, "px"); CHOICE("Mode:", opt.mode[t], blendlist, BM_N); SCRUB("Opacity:", opt.opacity[t], 1, 100, "%");
    SCRUB("Flow:", opt.flow[t], 1, 100, "%"); CHECK("Aligned", opt.clone_aligned); CHOICE("Sample:", opt.clone_sample, layersample, 3);
    SCRUB("Hardness:", opt.hardness[t], 0, 100, "%"); TEXT("Alt-click sets the source"); break;
  case TL_BRUSH: SCRUB("Size:", opt.size[t], 1, 2500, "px"); SCRUB("Hardness:", opt.hardness[t], 0, 100, "%"); CHOICE("Mode:", opt.mode[t], paint_modes(), BM_CLEAR + 1);
    SCRUB("Opacity:", opt.opacity[t], 1, 100, "%"); SCRUB("Flow:", opt.flow[t], 1, 100, "%"); SCRUB("Smoothing:", brushui.b.dyn.smoothing, 0, 100, "%");
    CHECK("Airbrush", brushui.b.dyn.buildup); BUTTON("Brush Settings...", ID_WIN_BRUSHSET); break;
  case TL_PATTERN: SCRUB("Size:", opt.size[t], 1, 2500, "px"); SCRUB("Hardness:", opt.hardness[t], 0, 100, "%"); SCRUB("Opacity:", opt.opacity[t], 1, 100, "%");
    if (npat_names) CHOICE("", opt.pattern, pat_names, npat_names);
    CHECK("Aligned", opt.pattern_aligned); break;
  case TL_HISTORY: SCRUB("Size:", opt.size[t], 1, 2500, "px"); SCRUB("Hardness:", opt.hardness[t], 0, 100, "%"); CHOICE("Mode:", opt.mode[t], blendlist, BM_N);
    SCRUB("Opacity:", opt.opacity[t], 1, 100, "%"); SCRUB("Flow:", opt.flow[t], 1, 100, "%"); BUTTON("Snapshot", ID_SNAPSHOT);
    BUTTON("From History", ID_HISTBRUSHSRC); TEXT("Paints back the snapshot"); break;
  case TL_REPLACE: SCRUB("Size:", opt.size[t], 1, 2500, "px"); CHOICE("Mode:", opt.replace_mode, rplmodes, 4); CHOICE("", opt.sampling, samplings, 3);
    CHOICE("Limits:", opt.limits, limitlist, 3); SCRUB("Tolerance:", opt.tolerance, 0, 255, ""); break;
  case TL_BGERASE: SCRUB("Size:", opt.size[t], 1, 2500, "px"); CHOICE("", opt.sampling, samplings, 3); CHOICE("Limits:", opt.limits, limitlist, 3);
    SCRUB("Tolerance:", opt.tolerance, 0, 255, ""); CHECK("Protect Foreground", opt.protect_fg); break;
  case TL_MAGICERASE: SCRUB("Tolerance:", opt.tolerance, 0, 255, ""); SCRUB("Opacity:", opt.fill_opacity, 1, 100, "%"); CHECK("Anti-alias", opt.antialias);
    CHECK("Contiguous", opt.contiguous); CHECK("All Layers", opt.all_layers); break;
  case TL_PENCIL: SCRUB("Size:", opt.size[t], 1, 2500, "px"); CHOICE("Mode:", opt.mode[t], paint_modes(), BM_CLEAR + 1); SCRUB("Opacity:", opt.opacity[t], 1, 100, "%");
    SCRUB("Smoothing:", brushui.b.dyn.smoothing, 0, 100, "%"); CHECK("Auto Erase", brushui.b.auto_erase); break;
  case TL_ERASER: SCRUB("Size:", opt.size[t], 1, 2500, "px"); CHOICE("Mode:", brushui.b.eraser_mode, erasemodes, 3); SCRUB("Opacity:", opt.opacity[t], 1, 100, "%");
    SCRUB("Flow:", opt.flow[t], 1, 100, "%"); CHECK("Erase to History", opt.erase_history); SCRUB("Hardness:", opt.hardness[t], 0, 100, "%"); break;
  case TL_BUCKET: CHOICE("Fill:", opt.fill_source, fillsrc, 2);
    if (opt.fill_source && npat_names) CHOICE("", opt.pattern, pat_names, npat_names);
    SCRUB("Opacity:", opt.fill_opacity, 1, 100, "%"); SCRUB("Tolerance:", opt.tolerance, 0, 255, ""); CHECK("Anti-alias", opt.antialias);
    CHECK("Contiguous", opt.contiguous); CHECK("All Layers", opt.all_layers); break;
  case TL_GRADIENT: if (ngrad_names) CHOICE("", opt.grad_preset, grad_names, ngrad_names);
    BUTTON("Edit...", ID_GRADEDIT); CHOICE("Style:", opt.grad_kind, gradkinds, 5); CHOICE("Mode:", opt.mode[t], blendlist, BM_N);
    SCRUB("Opacity:", opt.opacity[t], 1, 100, "%"); CHECK("Reverse", opt.grad_reverse); CHECK("Dither", opt.grad_dither);
    if (cur_doc() && cur_doc()->mode == MODE_INDEXED) CHECK("Palette Range", opt.grad_palrange);
    break;
  case TL_BLUR: case TL_SHARPEN: case TL_SMUDGE: SCRUB("Size:", opt.size[t], 1, 2500, "px");
    if (opt.mode[t] >= RETOUCH_MODES) opt.mode[t] = 0;
    CHOICE("Mode:", opt.mode[t], rtmodes, RETOUCH_MODES); SCRUB("Strength:", opt.strength[t], 1, 100, "%");
    if (t == TL_SHARPEN) CHECK("Protect Detail", opt.protect_detail);
    if (t == TL_SMUDGE) CHECK("Finger Painting", opt.finger_paint);
    SCRUB("Hardness:", opt.hardness[t], 0, 100, "%"); break;
  case TL_DODGE: case TL_BURN: SCRUB("Size:", opt.size[t], 1, 2500, "px"); CHOICE("Range:", opt.range, toneranges, 3);
    SCRUB("Exposure:", opt.strength[t], 1, 100, "%"); CHECK("Protect Tones", opt.protect_tones); SCRUB("Hardness:", opt.hardness[t], 0, 100, "%"); break;
  case TL_SPONGE: SCRUB("Size:", opt.size[t], 1, 2500, "px"); CHOICE("Mode:", opt.sponge_mode, spongemodes, 2);
    SCRUB("Flow:", opt.strength[t], 1, 100, "%"); CHECK("Vibrance", opt.vibrance); SCRUB("Hardness:", opt.hardness[t], 0, 100, "%"); break;
  case TL_TEXT: case TL_VTEXT: case TL_TYPEMASK: {
    /* 0.9: the font, its style and size, anti-aliasing, alignment, the colour; Warp, Character, Paragraph */
    Doc *doc = cur_doc();
    for (i = 0; i < font_count() && i < 96; i++) fontlist[i] = font_label(i);
    if (t == TL_TYPEMASK) SELOPS();
    if (i) CHOICE("", opt.text_font, fontlist, i);
    CHOICE("", opt.text_style, tstyles, 4);
    SCRUB("Size:", opt.text_size, 1, 2000, "px"); CHOICE("", opt.text_aa, taa, 3); CHOICE("", opt.text_align, taligns, 5);
    if (t != TL_TYPEMASK) { fg_show = doc ? (int)doc->fg : 0; SWATCH("", fg_show, ID_TEXTCOLOR); ICONBTN("Warp", IC_WARP, ID_WARPTEXT); }
    ICONBTN("Character", IC_FONTDLG, ID_TYPECHAR); ICONBTN("Paragraph", IC_PARADLG, ID_TYPEPARA);
    if (type_editing) BUTTON("Commit", ID_COMMIT);
    else if (t == TL_TYPEMASK) TEXT("Click and type; Esc makes the selection");
    break; }
  case TL_RECT: case TL_ELLIPSE: case TL_LINE: case TL_ROUNDRECT: case TL_POLYGON: case TL_CUSTOMSHAPE:
    /* 0.9: a shape layer or pixels; filled with the foreground colour, stroked; each shape's own */
    CHOICE("", opt.shape_mode, shmodes, 2);
    if (t != TL_LINE) CHECK("Fill", opt.shape_fill);
    if (t != TL_LINE || !opt.shape_mode) CHECK("Stroke", opt.shape_stroke);
    if (opt.shape_stroke && (t != TL_LINE || !opt.shape_mode)) {
      SCRUB("", opt.stroke_width, 1, 500, "px"); SWATCH("", opt.stroke_color, ID_STROKECOLOR);
      if (!opt.shape_mode) CHOICE("", opt.stroke_align, salign, 3);
    }
    if (t == TL_RECT) SCRUB("Radius:", opt.shape_fill_radius, 0, 1000, "px");
    if (t == TL_ROUNDRECT) SCRUB("Radius:", opt.round_radius, 0, 1000, "px");
    if (t == TL_POLYGON) { SCRUB("Sides:", opt.poly_sides, 3, 100, ""); CHECK("Star", opt.poly_star); if (opt.poly_star) SCRUB("Indent:", opt.poly_indent, 1, 99, "%"); }
    if (t == TL_LINE) { SCRUB("Weight:", opt.line_width, 1, 1000, "px"); if (!opt.shape_mode) { CHECK("Start", opt.arrow_start); CHECK("End", opt.arrow_end); } }
    if (t == TL_CUSTOMSHAPE) TEXT("Drag: the active path, fitted to the box");
    else if (t == TL_ELLIPSE || t == TL_RECT) TEXT("Shift: a square or circle");
    break;
  /* 0.9: the pens and path selection */
  case TL_PEN: TEXT("Click: a corner; drag: a curve; click the first point to close; Return finishes"); break;
  case TL_ADDANCHOR: TEXT("Click on the path to add a point (drag to move it)"); break;
  case TL_DELANCHOR: TEXT("Click a point to delete it"); break;
  case TL_CONVERT: TEXT("Click a point: a corner; drag from it: a curve; drag a handle: alone"); break;
  case TL_PATHSEL: case TL_DIRECTSEL:
    BUTTON("Fill Path", ID_PATHFILL); BUTTON("Stroke Path", ID_PATHSTROKE); BUTTON("Make Selection", ID_PATHMKSEL);
    TEXT(t == TL_PATHSEL ? "Drag a path to move it; Ctrl-T transforms it" : "Drag points or handles; drag across points to choose them");
    break;
  /* 1.2 */
  case TL_QUICKSEL: SELOPS(); SCRUB("Size:", opt.size[t], 1, 500, "px"); CHECK("Sample All Layers", opt.all_layers); CHECK("Auto-Enhance", opt.qs_enhance); break;
  case TL_OBJSEL: SELOPS(); CHECK("Sample All Layers", opt.all_layers); CHECK("Hard Edge", opt.obj_hard); TEXT("Drag a box round the object"); break;
  case TL_PCROP: SCRUB("W:", opt.pcrop_w, 0, 16384, "px"); SCRUB("H:", opt.pcrop_h, 0, 16384, "px"); BUTTON("Crop", ID_PCROP_GO); BUTTON("Cancel", ID_CANCEL);
    TEXT("0: the perspective's own size; Return crops"); break;
  case TL_SLICE: BUTTON("Slices From Guides", ID_SLICEGUIDES); BUTTON("Clear Slices", ID_SLICECLEAR); TEXT("Drag a box for each slice"); break;
  case TL_SLICESEL: BUTTON("Divide Slice...", ID_SLICEDIVIDE); BUTTON("Delete Slice", ID_SLICEDEL); BUTTON("Slices From Guides", ID_SLICEGUIDES); break;
  case TL_FRAME: CHOICE("", opt.frame_kind, framekinds, 2); SCRUB("Stroke:", opt.frame_stroke, 0, 100, "px"); TEXT("The stroke in the foreground colour"); break;
  case TL_NOTE: BUTTON("Clear All", ID_NOTECLEAR); TEXT("Click: a note; click one: change it; Alt-click: delete it"); break;
  case TL_COUNT: BUTTON("New Group...", ID_COUNTGROUP); BUTTON("Clear", ID_COUNTCLEAR); BUTTON("Record Measurements", ID_COUNTRECORD); TEXT("Alt-click a mark removes it"); break;
  case TL_MIXER: SCRUB("Size:", opt.size[t], 1, 2500, "px"); BUTTON("Load", ID_MIXLOAD); BUTTON("Clean", ID_MIXCLEAN); CHECK("Auto Load", opt.mix_autoload);
    CHECK("Auto Clean", opt.mix_autoclean); SCRUB("Wet:", opt.mix_wet, 0, 100, "%"); SCRUB("Load:", opt.mix_load, 1, 100, "%"); SCRUB("Mix:", opt.mix_mix, 0, 100, "%");
    SCRUB("Flow:", opt.flow[t], 1, 100, "%"); CHECK("Sample All Layers", opt.mix_all); break;
  case TL_ARTHIST: SCRUB("Size:", opt.size[t], 1, 500, "px"); SCRUB("Opacity:", opt.opacity[t], 1, 100, "%"); CHOICE("Style:", opt.ah_style, ah_names, AH_N);
    SCRUB("Area:", opt.ah_area, 1, 500, "px"); SCRUB("Tolerance:", opt.ah_tolerance, 0, 100, "%"); break;
  case TL_REMOVE: SCRUB("Size:", opt.size[t], 1, 2500, "px"); SCRUB("Hardness:", opt.hardness[t], 0, 100, "%"); TEXT("Brush over what should go; slow on a 68040"); break;
  case TL_CAMOVE: SELOPS(); CHOICE("Mode:", opt.cam_extend, cammodes, 2); SCRUB("Structure:", opt.cam_structure, 1, 7, ""); SCRUB("Color:", opt.cam_color, 0, 10, "");
    TEXT("Draw round it, then drag it"); break;
  case TL_ROTVIEW: BUTTON("Reset View", ID_ROTRESET); BUTTON("90\260", ID_ROT_CW); TEXT("Drag to turn the view; Shift: 15 degree steps"); break;
  case TL_ARTBOARD: TEXT("Drag a box for an artboard; drag one to move it"); break;
  case TL_HAND: case TL_ZOOM:
    if (t == TL_ZOOM) CHOICE("", opt.zoom_out, zoommodes, 2);
    BUTTON("100%", ID_ACTUAL); BUTTON("Fit Screen", ID_FIT); BUTTON("Fill Screen", ID_FILLSCREEN);
    if (t == TL_HAND) { BUTTON("Zoom In", ID_ZOOMIN); BUTTON("Zoom Out", ID_ZOOMOUT); }
    else TEXT("Alt-click: the other way; drag left or right to zoom");
    break;
  }
  return n;
#undef SCRUB
#undef CHOICE
#undef CHECK
#undef BUTTON
#undef TEXT
#undef SELOPS
#undef SWATCH
#undef ICONBTN
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
    { "Sample:", "What it picks up: the layer in use, that layer and the ones below it, or everything you see." },
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
    /* 1.1 */
    { "Auto-Select", "Clicking chooses the topmost layer with something under the pointer (or, with Group, the group it is in)." },
    { "Show Transform Controls", "Shows the layer's box with handles: drag one to scale or turn the layer (Free Transform; Return applies it)." },
    { "Align", "Lines the picked layers up with each other, or with the selection, or spreads them out evenly." },
    { "Range:", "Which tones change most: the shadows, the midtones or the highlights." },
    { "Protect Tones", "Keeps the colours from shifting in hue as they get lighter or darker." },
    { "Vibrance", "Changes dull colours more than vivid ones, so nothing goes past full saturation." },
    { "Aligned", "The source moves with each stroke; off, every stroke starts again from the source you Alt-clicked." },
    { "Protect Detail", "Sharpens gently, without bright fringes or grain." },
    { "Finger Painting", "Each stroke starts with the foreground colour, smeared into the picture." },
    { "Erase to History", "Paints back the snapshot (Edit > Take Snapshot) instead of erasing to transparency." },
    { "Auto Erase", "A stroke that starts on the foreground colour paints the background colour." },
    { "Smoothing:", "Steadies the stroke: the brush follows the pointer more slowly the higher it is." },
    { "Airbrush", "Builds up paint while the button is held and the pointer stays still." },
    { "Straighten", "Drag along something that should be level (or upright): the picture is turned to match and cropped." },
    { "Fill Screen", "Zooms so the picture fills the whole window." },
    { "Palette Range", "Fills through the palette's colours from one end of its first cycling range to the other (or the whole palette), as Deluxe Paint's range fills do." },
    { "Snapshot", "Takes a snapshot of the layer as it is now for the History Brush to paint back (Edit > Take Snapshot)." },
    { "From History", "The History Brush paints back the snapshot clicked last in the History panel instead." },
    /* 0.9 */
    { "AA:", "How the text's edges are smoothed: None (hard), Sharp or Smooth." },
    { "Warp", "Bends the text into an arc, a wave, a flag and other shapes, and can still be edited." },
    { "Character", "The font, size, leading, tracking, baseline shift, colour and styles of the selected text." },
    { "Paragraph", "Alignment, indents and the space before and after paragraphs." },
    { "Commit", "Finishes editing the text (Esc, or Enter on the keypad)." },
    { "Fill", "Fills the shape with the foreground colour." },
    { "Stroke", "Draws an outline round the shape, as wide and in the colour beside it." },
    { "Sides:", "How many sides (or points, for a star) the polygon has." },
    { "Star", "Makes the polygon a star." },
    { "Indent:", "How far the star's inner corners come in." },
    { "Start", "An arrowhead at the line's start." },
    { "End", "An arrowhead at the line's end." },
    { "Fill Path", "Fills the active path with the foreground colour on the layer in use." },
    { "Stroke Path", "Paints along the active path with the Brush tool's settings." },
    { "Make Selection", "Makes the active path the selection." },
  };
  int i;
  if (f->kind == F_SCRUB && f->val == &opt.text_size) return "The text's size in pixels: the selected text, or all of it, takes it.";
  for (i = 0; i < (int)(sizeof h / sizeof h[0]); i++) if (!strcmp(h[i].label, f->label)) return h[i].help;
  if (f->kind == F_CHOICE && f->choices == fontlist) return "The font the text is written in (Amiga bitmap fonts are marked).";
  if (f->kind == F_CHOICE && f->choices == taa) return "How the text's edges are smoothed: None (hard), Sharp or Smooth.";
  if (f->kind == F_CHOICE && f->choices == taligns) return "How the lines line up: left, centre, right, or justified to the paragraph's box.";
  if (f->kind == F_CHOICE && f->choices == tstyles) return "Regular, bold, italic or both: the font's own bold and italic faces when it has them, else drawn slanted or heavier.";
  if (f->kind == F_CHOICE && f->choices == shmodes) return "A shape layer stays sharp and can be changed later (Layer > Shape Properties); Pixels paints the shape on the layer in use.";
  if (f->kind == F_CHOICE && f->choices == salign) return "Where the stroke goes: centred on the shape's edge, inside it or outside it.";
  if (f->kind == F_SWATCH && f->min == ID_TEXTCOLOR) return "The text's colour (the foreground colour). Click to choose another: the selected text, or all of it, takes it.";
  if (f->kind == F_SWATCH && f->min == ID_STROKECOLOR) return "The stroke's colour. Click to choose another.";
  if (f->kind == F_SCRUB && f->val == &opt.text_size) return "The text's size in pixels: the selected text, or all of it, takes it.";
  if (f->kind == F_SCRUB && f->val == &opt.stroke_width) return "The stroke's width in pixels.";
  if (f->kind == F_CHOICE && f->choices == gradcols) return "The gradient's colours: foreground to background, or the foreground fading out.";
  if (f->kind == F_CHOICE && f->choices == crop_overlay_names) return "The lines drawn over the crop box to help with the composition.";
  if (f->kind == F_CHOICE && f->choices == movetargets) return "What Auto-Select picks: the layer under the pointer, or the group it is in.";
  if (f->kind == F_CHOICE && f->choices == spongemodes) return "Takes colour away, or adds it.";
  if (f->kind == F_CHOICE && f->choices == zoommodes) return "What a click does (Alt-click does the other).";
  if (f->kind == F_CHOICE && f->choices == erasemodes) return "The eraser's tip: a soft brush, a hard pencil or a square block.";
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
    LONG lw = f->label[0] ? theme_textlen(rp, theme_font(), tr(f->label)) + 5 : 0, vw = 0;
    d->x0[i] = x;
    switch (f->kind) {
    case F_SCRUB: vw = theme_textlen(rp, theme_font(), "2500px") + 12; break;
    case F_CHOICE: {
      int k;
      LONG ew = 0;
      for (k = 0; k < f->nchoices; k++) {
        LONG w = theme_textlen(rp, theme_font(), tr(f->choices[k])), e = theme_textlen(rp, theme_font(), f->choices[k]);
        if (w > vw) vw = w;
        if (e > ew) ew = e;
      }
      /* (1.1: a translation gets the English's room and a little more; longer words are cut, so the
         bar keeps its width in every language) */
      if (vw > ew + 8) vw = ew + 8;
      if (vw > (f->choices == fontlist ? 110 : 160)) vw = f->choices == fontlist ? 110 : 160;
      vw += 26;
      break; }
    case F_CHECK: lw = 0; vw = 16 + theme_textlen(rp, theme_font(), tr(f->label)); break;
    case F_SELOPS: lw = 0; vw = 4 * 22; break;
    case F_BUTTON: lw = 0; vw = theme_textlen(rp, theme_font(), tr(f->label)) + 20; break;
    case F_MENU: lw = 0; vw = theme_textlen(rp, theme_font(), tr(f->label)) + 30; break;
    case F_TEXT: lw = 0; vw = theme_textlen(rp, theme_font(), tr(f->label)); break;
    case F_SWATCH: lw = 0; vw = 24 + (f->label[0] ? theme_textlen(rp, theme_font(), tr(f->label)) + 5 : 0); break;
    case F_ICON: lw = 0; vw = 22; break;
    }
    d->vx0[i] = x + lw;
    d->x1[i] = x + lw + vw;
    x = d->x1[i] + (f->kind == F_SELOPS ? 18 : f->kind == F_ICON ? 6 : 12);
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
      theme_text(rp, theme_font(), d->x0[i], my - fh() / 2, tr(f->label), tpen(d->hot == i && f->kind == F_SCRUB ? T_TEXT : T_DIM));
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
      theme_text(rp, theme_font(), x0 + 16, my - fh() / 2, tr(f->label), tpen(T_TEXT));
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
    case F_BUTTON: case F_MENU:
      theme_fill(rp, x0, vt, x1, vb, tpen(d->hot == i ? T_HOVER : T_WHITE));
      theme_rect(rp, x0, vt, x1, vb, tpen(T_BORDER2));
      theme_text(rp, theme_font(), x0 + 10, my - fh() / 2, tr(f->label), tpen(T_TEXT));
      if (f->kind == F_MENU) {
        LONG ax = x1 - 10, ay = my - 1;
        SetAPen(rp, tpen(T_DIM));
        Move(rp, ax - 3, ay - 1); Draw(rp, ax + 3, ay - 1); Move(rp, ax - 2, ay); Draw(rp, ax + 2, ay); Move(rp, ax - 1, ay + 1); Draw(rp, ax + 1, ay + 1);
        WritePixel(rp, ax, ay + 2);
      }
      break;
    case F_TEXT:
      theme_text(rp, theme_font(), x0, my - fh() / 2, tr(f->label), tpen(T_GROUPLABEL));
      break;
    case F_ICON:
      if (d->hot == i) theme_fill(rp, x0, vt, x1, vb, tpen(T_HOVER));
      theme_icon(rp, f->max, x0 + 3, my - 8, 16, 0);
      break;
    case F_SWATCH: {
      LONG sx = x0;
      if (f->label[0]) { theme_text(rp, theme_font(), x0, my - fh() / 2, tr(f->label), tpen(T_DIM)); sx += theme_textlen(rp, theme_font(), tr(f->label)) + 5; }
      theme_rect(rp, sx, vt, sx + 22, vb, tpen(d->hot == i ? T_ACCENT : T_BORDER2));
      draw_rgb_box(rp, _screen(obj), sx + 2, vt + 2, sx + 20, vb - 2, (ULONG)*f->val & 0xFFFFFF);
      break; }
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
/* 0.9: a setting changed: main.c tells the canvas (the Type tool applies it to the text) */
static void ob_changed(Object *obj) { DoMethod(_app(obj), MUIM_Application_PushMethod, (ULONG)_app(obj), 2, MUIM_Application_ReturnID, ID_OPTCHG); }
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
      ob_changed(obj);
    } else if (t && d->popfield >= 0 && d->popfield < d->n && d->f[d->popfield].kind == F_MENU && (int)t->ti_Data >= 0 && (int)t->ti_Data < d->f[d->popfield].nchoices)
      DoMethod(_app(obj), MUIM_Application_ReturnID, d->f[d->popfield].ids[t->ti_Data]);    /* (1.1) */
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
      ob_changed(obj);
      return MUI_EventHandlerRC_Eat;
    }
    if (im->Code != SELECTDOWN) return 0;
    h = ob_hit(d, im->MouseX, im->MouseY, obj);
    if (h < 0) return 0;
    {
      Field *f = &d->f[h];
      switch (f->kind) {
      case F_SCRUB: d->drag = h; d->grabx = im->MouseX; d->grabv = *f->val; d->moved = 0; break;
      case F_CHECK: *f->val = !*f->val; ob_redraw(d, obj); ob_changed(obj); break;
      case F_SELOPS: { int k = (int)((im->MouseX - d->vx0[h]) / 22); if (k >= 0 && k < 4) *f->val = k; ob_redraw(d, obj); break; }
      case F_BUTTON: case F_SWATCH: case F_ICON: DoMethod(_app(obj), MUIM_Application_ReturnID, (ULONG)f->min); break;
      case F_CHOICE: case F_MENU: {
        struct Window *w = _window(obj);
        d->popfield = h;
        popup_list(obj, w->LeftEdge + d->vx0[h], w->TopEdge + _mbottom(obj) - 2, d->x1[h] - d->vx0[h], f->choices, f->nchoices, f->kind == F_MENU ? -1 : *f->val, MUIA_Popup_Chosen);
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
