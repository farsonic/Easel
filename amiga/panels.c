/*
 * panels.c - the panels down the right of Easel's window, as PhotoCraft has them, and the strips
 * round the canvas:
 *   Color    a saturation and brightness square for the foreground's hue, the hue strip beside it,
 *            red, green and blue bars, the colour as hex, and the swatches;
 *   Layers   the active layer's blend mode, opacity and locks, then a row for each layer (top
 *            first) with its eye, a thumbnail and its name, and the buttons along the bottom;
 *   History  the undo steps, the one in effect lit and those undone dimmed;
 * and the panels' title strips, the documents' tabs above the canvas and the status bar.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <exec/memory.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "../core/layers.h"
#include "../core/live.h"
#include "../core/fx.h"                 /* 1.1 */
#include "../core/layerops.h"
#include "theme.h"
#include "ctxmui.h"
#include "lang.h"
#include "ids.h"

static int fh(void) { return theme_font()->tf_YSize; }
/* text cut short to fit maxw (Latin-1), drawn with its top at y */
static void text_fit(struct RastPort *rp, struct TextFont *tf, LONG x, LONG y, const char *s, LONG maxw, LONG pen)
{
  struct TextExtent te;
  ULONG fit;
  if (maxw <= 0) return;
  SetFont(rp, tf);
  SetSoftStyle(rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
  fit = TextFit(rp, (STRPTR)s, (ULONG)strlen(s), &te, NULL, 1, (ULONG)maxw, 100);
  SetAPen(rp, pen); SetDrMd(rp, JAM1);
  Move(rp, x, y + tf->tf_Baseline);
  Text(rp, (STRPTR)s, fit);
}
/* a UTF-8 name as Latin-1 */
static void to_latin1(const char *u, char *out, size_t cap) { utf8_to_latin1(u, (u32)strlen(u), out, cap); }
/* the common parts of a panel's dispatcher: setup with an event handler, show/hide */
#define PANEL_COMMON(T) \
  case MUIM_Setup: \
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE; \
    theme_setup(obj); \
    theme_handler(obj, &((T *)INST_DATA(cl, obj))->ehn); \
    return TRUE; \
  case MUIM_Cleanup: theme_cleanup(obj); break; \
  case MUIM_Show: ((T *)INST_DATA(cl, obj))->shown = 1; break; \
  case MUIM_Hide: ((T *)INST_DATA(cl, obj))->shown = 0; break;

static struct MUI_CustomClass *colcls, *laycls, *histcls, *headcls, *tabcls, *stcls;

/* ================================================================== the Color panel */
struct CData { struct MUI_EventHandlerNode ehn; int shown, drag; double hue; UBYTE *buf; LONG bufcap; int which; };
static const ULONG swatches[] = {
  0x000000, 0x404040, 0x808080, 0xC0C0C0, 0xFFFFFF, 0xFF0000, 0xFF8000, 0xFFFF00, 0x80FF00, 0x00FF00, 0x00FF80, 0x00FFFF,
  0x0080FF, 0x0000FF, 0x8000FF, 0xFF00FF, 0xFF0080, 0x800000, 0x804000, 0x808000, 0x008000, 0x008080, 0x000080, 0x800080,
  0xF4C9A0, 0xE0A070, 0xA06030, 0x603010, 0xFFC0C0, 0xC0FFC0, 0xC0C0FF, 0xFFE0A0
};
#define NSW (int)(sizeof swatches / sizeof swatches[0])
/* the parts' places: the square, the hue strip, the three bars, the hex box, the swatches */
typedef struct { LONG sx0, sy0, sx1, sy1, hx0, hx1, bx0, bx1, by[3], hexy, swy, sws, swcols; } CGeo;
static void cgeo(Object *obj, CGeo *g)
{
  LONG l = _mleft(obj) + 8, r = _mright(obj) - 8, t = _mtop(obj) + 6, i;
  g->sx0 = l; g->sy0 = t; g->hx1 = r; g->hx0 = r - 13; g->sx1 = g->hx0 - 8; g->sy1 = t + 79;
  g->bx0 = l + 14; g->bx1 = r - 34;
  for (i = 0; i < 3; i++) g->by[i] = g->sy1 + 10 + i * 14;
  g->hexy = g->by[2] + 16;
  g->swy = g->hexy + fh() + 10;
  g->swcols = 16; g->sws = (r - l + 2) / 16 - 2;
  if (g->sws > 14) g->sws = 14;
  if (g->swcols < 1) g->swcols = 1;
}
static ULONG cur_fg(void) { Doc *d = cur_doc(); return d ? d->fg : 0; }
static void set_fg(ULONG c) { Doc *d = cur_doc(); if (d) d->fg = c & 0xFFFFFF; }
static void color_draw(struct CData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  struct Screen *scr = _screen(obj);
  CGeo g;
  ULONG c = cur_fg();
  int r = (int)(c >> 16) & 255, gg = (int)(c >> 8) & 255, b = (int)c & 255, i;
  double h, s, v;
  LONG w, ht, x, y;
  char buf[32];
  cgeo(obj, &g);
  theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_PANEL));
  rgb_to_hsv(r, gg, b, &h, &s, &v);
  if (s > 0.001 && v > 0.001) d->hue = h;
  /* the square: saturation across, brightness up, for the hue */
  w = g.sx1 - g.sx0 + 1; ht = g.sy1 - g.sy0 + 1;
  if (w > 0 && ht > 0) {
    LONG need = w * (ht > 256 ? ht : 256) * 3;
    if (need > d->bufcap) {
      if (d->buf) FreeVec(d->buf);
      d->buf = (UBYTE *)AllocVec((ULONG)need, MEMF_ANY);
      d->bufcap = d->buf ? need : 0;
    }
    if (d->buf) {
      int hr, hg, hb;
      hsv_to_rgb(d->hue, 1, 1, &hr, &hg, &hb);
      for (y = 0; y < ht; y++) {
        int vv = (int)(255 - y * 255 / (ht > 1 ? ht - 1 : 1));
        UBYTE *o = d->buf + y * w * 3;
        for (x = 0; x < w; x++, o += 3) {
          int ss = (int)(x * 255 / (w > 1 ? w - 1 : 1));
          /* white mixed toward the hue by saturation, all scaled by brightness */
          o[0] = (UBYTE)(((255 * (255 - ss) + hr * ss) / 255) * vv / 255);
          o[1] = (UBYTE)(((255 * (255 - ss) + hg * ss) / 255) * vv / 255);
          o[2] = (UBYTE)(((255 * (255 - ss) + hb * ss) / 255) * vv / 255);
        }
      }
      draw_rgb(rp, scr, d->buf, w * 3, g.sx0, g.sy0, w, ht);
      /* the hue strip */
      for (y = 0; y < ht; y++) {
        int pr, pg, pb, k;
        hsv_to_rgb(360.0 * y / ht, 1, 1, &pr, &pg, &pb);
        for (k = 0; k < 14; k++) { d->buf[(y * 14 + k) * 3] = (UBYTE)pr; d->buf[(y * 14 + k) * 3 + 1] = (UBYTE)pg; d->buf[(y * 14 + k) * 3 + 2] = (UBYTE)pb; }
      }
      draw_rgb(rp, scr, d->buf, 14 * 3, g.hx0, g.sy0, 14, ht);
      /* the colour's place in the square (a ring) and the hue's on the strip */
      x = g.sx0 + (LONG)(s * (w - 1)); y = g.sy0 + (LONG)((1 - v) * (ht - 1));
      SetAPen(rp, v > 0.6 && s < 0.5 ? tpen(T_WHITE) : tpen(T_TEXT));
      DrawEllipse(rp, x, y, 4, 4);
      y = g.sy0 + (LONG)(d->hue / 360.0 * ht);
      SetAPen(rp, tpen(T_TEXT));
      Move(rp, g.hx0 - 3, y - 3); Draw(rp, g.hx0, y); Draw(rp, g.hx0 - 3, y + 3);
      Move(rp, g.hx1 + 3, y - 3); Draw(rp, g.hx1, y); Draw(rp, g.hx1 + 3, y + 3);
    }
  }
  /* the bars: each channel's gradient with the others held, a marker at its value */
  for (i = 0; i < 3; i++) {
    LONG bw = g.bx1 - g.bx0 + 1, k, by = g.by[i];
    int val = i == 0 ? r : i == 1 ? gg : b;
    static const char *const nm[3] = { "R", "G", "B" };
    theme_text(rp, theme_font(), g.sx0, by - fh() / 2 + 3, nm[i], tpen(T_DIM));
    if (d->buf && bw > 0) {
      for (k = 0; k < bw; k++) {
        int t = (int)(k * 255 / (bw > 1 ? bw - 1 : 1));
        UBYTE *o = d->buf + k * 3;
        o[0] = (UBYTE)(i == 0 ? t : r); o[1] = (UBYTE)(i == 1 ? t : gg); o[2] = (UBYTE)(i == 2 ? t : b);
      }
      for (k = 1; k < 6; k++) memcpy(d->buf + k * bw * 3, d->buf, (size_t)(bw * 3));
      draw_rgb(rp, scr, d->buf, bw * 3, g.bx0, by, bw, 6);
      x = g.bx0 + val * (bw - 1) / 255;
      SetAPen(rp, tpen(T_TEXT));
      Move(rp, x, by + 7); Draw(rp, x - 3, by + 10); Draw(rp, x + 3, by + 10); Draw(rp, x, by + 7);
    }
    sprintf(buf, "%d", val);
    theme_text(rp, theme_font(), g.bx1 + 8, by - fh() / 2 + 3, buf, tpen(T_TEXT));
  }
  /* the hex value and a large swatch of the colour */
  sprintf(buf, "# %02X%02X%02X", r, gg, b);
  theme_fill(rp, g.sx0 + 30, g.hexy, g.sx0 + 110, g.hexy + fh() + 3, tpen(T_WHITE));
  theme_rect(rp, g.sx0 + 30, g.hexy, g.sx0 + 110, g.hexy + fh() + 3, tpen(T_BORDER2));
  theme_text(rp, theme_font(), g.sx0 + 35, g.hexy + 2, buf, tpen(T_TEXT));
  draw_rgb_box(rp, scr, g.sx0, g.hexy, g.sx0 + 22, g.hexy + fh() + 3, c);
  theme_rect(rp, g.sx0, g.hexy, g.sx0 + 22, g.hexy + fh() + 3, tpen(T_BORDER2));
  /* the swatches */
  for (i = 0; i < NSW; i++) {
    LONG sx = g.sx0 + (i % g.swcols) * (g.sws + 2), sy = g.swy + (i / g.swcols) * (g.sws + 2);
    if (sy + g.sws > _mbottom(obj)) break;
    draw_rgb_box(rp, scr, sx, sy, sx + g.sws - 1, sy + g.sws - 1, swatches[i]);
  }
}
/* the mouse in the colour panel at mx, my: the colour changed for where it is */
static int color_at(struct CData *d, Object *obj, LONG mx, LONG my, int part, int alt)
{
  CGeo g;
  ULONG c = cur_fg();
  int r = (int)(c >> 16) & 255, gg = (int)(c >> 8) & 255, b = (int)c & 255;
  double h, s, v;
  cgeo(obj, &g);
  rgb_to_hsv(r, gg, b, &h, &s, &v);
  if (part == 1) {
    LONG w = g.sx1 - g.sx0, ht = g.sy1 - g.sy0;
    double ss = (double)(mx - g.sx0) / (w > 0 ? w : 1), vv = 1 - (double)(my - g.sy0) / (ht > 0 ? ht : 1);
    if (ss < 0) ss = 0;
    if (ss > 1) ss = 1;
    if (vv < 0) vv = 0;
    if (vv > 1) vv = 1;
    hsv_to_rgb(d->hue, ss, vv, &r, &gg, &b);
  } else if (part == 2) {
    double hh = 360.0 * (my - g.sy0) / (g.sy1 - g.sy0 + 1);
    if (hh < 0) hh = 0;
    if (hh > 359.9) hh = 359.9;
    d->hue = hh;
    if (s < 0.01) s = 1;
    if (v < 0.01) v = 1;
    hsv_to_rgb(hh, s, v, &r, &gg, &b);
  } else if (part >= 3 && part <= 5) {
    LONG bw = g.bx1 - g.bx0;
    int t = (int)((mx - g.bx0) * 255 / (bw > 0 ? bw : 1));
    if (t < 0) t = 0;
    if (t > 255) t = 255;
    if (part == 3) r = t; else if (part == 4) gg = t; else b = t;
  } else return 0;
  (void)alt;
  set_fg(((ULONG)r << 16) | ((ULONG)gg << 8) | (ULONG)b);
  return 1;
}
static int color_part(Object *obj, LONG mx, LONG my, int *sw)
{
  CGeo g;
  int i;
  cgeo(obj, &g);
  *sw = -1;
  if (mx >= g.sx0 && mx <= g.sx1 && my >= g.sy0 && my <= g.sy1) return 1;
  if (mx >= g.hx0 && mx <= g.hx1 + 3 && my >= g.sy0 && my <= g.sy1) return 2;
  for (i = 0; i < 3; i++) if (mx >= g.bx0 - 3 && mx <= g.bx1 + 3 && my >= g.by[i] - 2 && my <= g.by[i] + 11) return 3 + i;
  if (my >= g.hexy && my <= g.hexy + fh() + 3 && mx >= g.sx0 && mx <= g.sx0 + 110) return 6;
  for (i = 0; i < NSW; i++) {
    LONG sx = g.sx0 + (i % g.swcols) * (g.sws + 2), sy = g.swy + (i / g.swcols) * (g.sws + 2);
    if (mx >= sx && mx < sx + g.sws && my >= sy && my < sy + g.sws) { *sw = i; return 7; }
  }
  return 0;
}
static ULONG coldispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct CData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
    return (ULONG)obj;
  case OM_DISPOSE:
    d = INST_DATA(cl, obj);
    if (d->buf) FreeVec(d->buf);
    break;
  case OM_GET:
    if (((struct opGet *)msg)->opg_AttrID == MUIA_Color_Changed) { *((struct opGet *)msg)->opg_Storage = TRUE; return TRUE; }
    break;
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    LONG h = 6 + 80 + 10 + 3 * 14 + 16 + fh() + 10 + 2 * 15 + 4;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 200; m->MinMaxInfo->DefWidth += 240; m->MinMaxInfo->MaxWidth += 320;
    m->MinMaxInfo->MinHeight += h; m->MinMaxInfo->DefHeight += h; m->MinMaxInfo->MaxHeight += h;
    return 0;
  }
  PANEL_COMMON(struct CData)
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) color_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Color_Refresh:
    if (((struct CData *)INST_DATA(cl, obj))->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  case MUIM_CreateShortHelp: {
    struct MUIP_CreateShortHelp *m = (struct MUIP_CreateShortHelp *)msg;
    static char name[40];
    int sw, part = color_part(obj, m->mx, m->my, &sw);
    static const char *const n[] = { "", "Saturation and Brightness", "Hue", "Red", "Green", "Blue", "Hex Value" };
    static const char *const b[] = { "", "Click or drag to set how vivid and how bright the foreground colour is.",
      "Click or drag to choose the colour's hue.", "Drag along to set the foreground colour's red.", "Drag along to set the foreground colour's green.",
      "Drag along to set the foreground colour's blue.", "The foreground colour as a hex number. Click to type one." };
    if (part >= 1 && part <= 6) return (ULONG)theme_bubble(tr(n[part]), tr(b[part]));
    if (part == 7) {
      sprintf(name, "%s #%06lX", tr("Swatch"), (unsigned long)swatches[sw]);
      return (ULONG)theme_bubble(name, tr("Click to paint with this colour; Alt-click makes it the background colour."));
    }
    return 0;
  }
  case MUIM_DeleteShortHelp: return 0;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    int sw, part, alt;
    d = INST_DATA(cl, obj);
    if (!im || !d->shown || !cur_doc()) return 0;
    alt = (im->Qualifier & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) != 0;
    if (im->Class == IDCMP_MOUSEMOVE && d->drag) {
      if (color_at(d, obj, im->MouseX, im->MouseY, d->drag, alt)) { MUI_Redraw(obj, MADF_DRAWOBJECT); SetAttrs(obj, MUIA_Color_Changed, TRUE, TAG_DONE); }
      return MUI_EventHandlerRC_Eat;
    }
    if (im->Class != IDCMP_MOUSEBUTTONS) return 0;
    if (im->Code == SELECTUP && d->drag) { d->drag = 0; return MUI_EventHandlerRC_Eat; }
    if (im->Code != SELECTDOWN || im->MouseX < _mleft(obj) || im->MouseX > _mright(obj) || im->MouseY < _mtop(obj) || im->MouseY > _mbottom(obj)) return 0;
    part = color_part(obj, im->MouseX, im->MouseY, &sw);
    if (part >= 1 && part <= 5) { d->drag = part; color_at(d, obj, im->MouseX, im->MouseY, part, alt); }
    else if (part == 6) {
      char buf[16];
      ULONG c = cur_fg();
      sprintf(buf, "%06lX", (unsigned long)c);
      if (!ask_string("Colour", "Hex (RRGGBB):", buf, sizeof buf)) return MUI_EventHandlerRC_Eat;
      set_fg(strtoul(buf[0] == '#' ? buf + 1 : buf, NULL, 16));
    } else if (part == 7) {
      if (alt) cur_doc()->bg = swatches[sw]; else set_fg(swatches[sw]);
    } else return 0;
    MUI_Redraw(obj, MADF_DRAWOBJECT);
    SetAttrs(obj, MUIA_Color_Changed, TRUE, TAG_DONE);
    return MUI_EventHandlerRC_Eat;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *color_class(void)
{
  if (!colcls) colcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct CData), (APTR)coldispatch);
  return colcls;
}

/* ================================================================== the Layers panel */
/* (0.3) Groups indent their members and can be closed with the triangle; a mask shows as a second
   thumbnail, the one painting goes into framed; a clipped layer has an arrow down to its base.
   Shift-click picks a run of layers, Ctrl-click one more; dragging moves a layer (or a whole
   group), into a group when dropped on its row */
/* the thumbnails' size: smaller when the panel has room for fewer than four rows (a 480-line screen) */
static int thumb_sz = 28;
#define THUMB thumb_sz
static const char *const blendnames[BM_N + 1] = { "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten", "Difference", "Add", "Subtract",
  "Color Dodge", "Color Burn", "Soft Light", "Hard Light", "Exclusion", "Hue", "Saturation", "Color", "Luminosity", "Dissolve", "Linear Burn",
  "Darker Color", "Lighter Color", "Vivid Light", "Linear Light", "Pin Light", "Hard Mix", "Divide",
  "Reflect", "Glow", "Negation", "Xor", "Color Burn (Paint.NET)", "Color Dodge (Paint.NET)", "Pass Through" };
static const char *blend_label(const Layer *l) { return tr(blendnames[l->blend == BM_PASS ? BM_N : l->blend < BM_N ? l->blend : 0]); }
struct LData {
  struct MUI_EventHandlerNode ehn;
  int shown, first, drag, dragfrom, dragto, scrub, moved; LONG grabx; int grabv, grabv2;
  ULONG secs, micros; int lastrow;
  UBYTE *buf; LONG bufcap;
  ULONG cmd;
  int list[MAXLAYERS], nlist;          /* the rows: layer indices, top first, closed groups' members left out */
};
static LONG rowh(void) { return THUMB + 6; }
/* the parts: the mode list and opacity on top, the locks and fill, the rows, the buttons at the bottom */
static LONG lay_top(Object *obj) { return _mtop(obj) + 2 * (fh() + 10) + 2; }
static LONG lay_bottom(Object *obj) { return _mbottom(obj) - 26; }
static int lay_rows(Object *obj) { LONG h = lay_bottom(obj) - lay_top(obj); return h > 0 ? (int)(h / rowh()) : 0; }
static void lay_fit(Object *obj) { thumb_sz = lay_bottom(obj) - lay_top(obj) < 4 * (28 + 6) ? 18 : 28; }
static const int lay_buttons[] = { IC_ADDMASK, IC_ADJLAYER, IC_FOLDER, IC_NEWLAYER, IC_DUPLAYER, IC_TRASH };
static const ULONG lay_ids[] = { ID_MASK_REVEALALL, ID_NEWADJ, ID_NEWGROUP, ID_NEWLAYER, ID_DUPLAYER, ID_DELLAYER };
#define NLB 6
#define NLOCK 4
static const int lock_icons[NLOCK] = { IC_ALPHALOCK, IC_LOCKL, IC_LOCKPOS, IC_LOCKALL };
static void lay_list(struct LData *d, Doc *doc)
{
  int i;
  d->nlist = 0;
  for (i = doc->nlay - 1; i >= 0; i--) if (!layer_hidden_by_collapse(doc, i)) d->list[d->nlist++] = i;
}
static int lock_on(const Layer *a, int k) { return k == 0 ? a->alpha_lock : k == 1 ? a->locked : k == 2 ? a->lock_pos : a->lock_all; }
/* where a row's parts are: the indent, the triangle (groups), the thumbnail and the mask's */
static LONG row_indent(const Layer *ly) { return (LONG)(ly->depth > 6 ? 6 : ly->depth) * 10; }
static LONG thumb_x(Object *obj, const Layer *ly) { return _mleft(obj) + 30 + row_indent(ly) + (ly->clip ? 8 : 0) + (ly->kind == LK_GROUP ? 10 : 0); }
static LONG mask_x(Object *obj, const Layer *ly) { return thumb_x(obj, ly) + THUMB + 4; }
static LONG name_x(Object *obj, const Layer *ly) { return (ly->mask ? mask_x(obj, ly) : thumb_x(obj, ly)) + THUMB + 8; }
/* a picture (pixels, or a mask as grey) fitted in a THUMB square over the checkerboard */
static void thumb_draw(struct LData *d, struct RastPort *rp, struct Screen *scr, Doc *doc, const u8 *px, const u8 *mask, LONG x, LONG y, int lit)
{
  double sc;
  LONG tw, th, xx, yy;
  if (!d->buf && !(d->buf = (UBYTE *)AllocVec(THUMB * THUMB * 3, MEMF_ANY))) return;
  sc = (double)(doc->w > doc->h ? doc->w : doc->h) / THUMB;
  tw = (LONG)(doc->w / sc); th = (LONG)(doc->h / sc);
  if (tw < 1) tw = 1;
  if (th < 1) th = 1;
  for (yy = 0; yy < th; yy++) for (xx = 0; xx < tw; xx++) {
    LONG ix = (LONG)((xx + 0.5) * sc), iy = (LONG)((yy + 0.5) * sc);
    UBYTE *o = d->buf + (yy * tw + xx) * 3;
    int c = (((xx >> 2) + (yy >> 2)) & 1) ? 0xCC : 0xFF, a;
    if (ix >= doc->w) ix = doc->w - 1;
    if (iy >= doc->h) iy = doc->h - 1;
    if (mask) { o[0] = o[1] = o[2] = mask[(long)iy * doc->w + ix]; continue; }
    {
      const u8 *p = px + ((long)iy * doc->w + ix) * 4;
      a = p[3];
      o[0] = (UBYTE)((p[0] * a + c * (255 - a)) / 255); o[1] = (UBYTE)((p[1] * a + c * (255 - a)) / 255); o[2] = (UBYTE)((p[2] * a + c * (255 - a)) / 255);
    }
  }
  x += (THUMB - tw) / 2; y += (THUMB - th) / 2;
  draw_rgb(rp, scr, d->buf, tw * 3, x, y, tw, th);
  theme_rect(rp, x - 1, y - 1, x + tw, y + th, tpen(lit ? T_ACCENT : T_BORDER2));
  if (lit) theme_rect(rp, x - 2, y - 2, x + tw + 1, y + th + 1, tpen(T_ACCENT));
}
/* a fill layer's thumbnail: its colour, or its two colours side by side as a ramp */
static void fill_thumb(struct LData *d, struct RastPort *rp, struct Screen *scr, const Layer *ly, LONG x, LONG y)
{
  LONG xx, yy;
  if (!d->buf && !(d->buf = (UBYTE *)AllocVec(THUMB * THUMB * 3, MEMF_ANY))) return;
  for (yy = 0; yy < THUMB; yy++) for (xx = 0; xx < THUMB; xx++) {
    UBYTE *o = d->buf + (yy * THUMB + xx) * 3;
    int t = ly->fillp.kind ? (int)(xx * 255 / (THUMB - 1)) : 0, k;
    for (k = 0; k < 3; k++) {
      int a = (int)((ly->fillp.c0 >> (16 - 8 * k)) & 255), b = (int)((ly->fillp.c1 >> (16 - 8 * k)) & 255);
      o[k] = (UBYTE)(a + (b - a) * t / 255);
    }
  }
  draw_rgb(rp, scr, d->buf, THUMB * 3, x, y, THUMB, THUMB);
  theme_rect(rp, x - 1, y - 1, x + THUMB, y + THUMB, tpen(T_BORDER2));
}
/* 0.9: a type layer's thumbnail (a T on white, as PhotoCraft shows it), or a shape layer's: its
   rendering over the checkerboard, fitted as the picture is */
static void live_thumb(struct LData *d, struct RastPort *rp, struct Screen *scr, Doc *doc, Layer *ly, LONG x, LONG y, int lit)
{
  double sc;
  LONG tw, th, xx, yy;
  Box b;
  if (ly->kind == LK_TEXT) {
    theme_fill(rp, x, y, x + THUMB - 1, y + THUMB - 1, tpen(T_WHITE));
    theme_rect(rp, x - 1, y - 1, x + THUMB, y + THUMB, tpen(lit ? T_ACCENT : T_BORDER2));
    theme_icon(rp, IC_T_TEXT, x + (THUMB - 20) / 2, y + (THUMB - 20) / 2, 20, 0);
    return;
  }
  if (!d->buf && !(d->buf = (UBYTE *)AllocVec(THUMB * THUMB * 3, MEMF_ANY))) return;
  if (!ly->live_ok) live_render(doc, ly);
  b = ly->livebox;
  sc = (double)(doc->w > doc->h ? doc->w : doc->h) / THUMB;
  tw = (LONG)(doc->w / sc); th = (LONG)(doc->h / sc);
  if (tw < 1) tw = 1;
  if (th < 1) th = 1;
  for (yy = 0; yy < th; yy++) for (xx = 0; xx < tw; xx++) {
    LONG ix = (LONG)((xx + 0.5) * sc), iy = (LONG)((yy + 0.5) * sc);
    UBYTE *o = d->buf + (yy * tw + xx) * 3;
    int c = (((xx >> 2) + (yy >> 2)) & 1) ? 0xCC : 0xFF, a = 0;
    const u8 *p = NULL;
    if (ly->live_ok && ly->livepx && ix >= b.x0 && iy >= b.y0 && ix < b.x1 && iy < b.y1) { p = ly->livepx + ((long)(iy - b.y0) * (b.x1 - b.x0) + (ix - b.x0)) * 4; a = p[3]; }
    if (!p) { o[0] = o[1] = o[2] = (UBYTE)c; continue; }
    o[0] = (UBYTE)((p[0] * a + c * (255 - a)) / 255); o[1] = (UBYTE)((p[1] * a + c * (255 - a)) / 255); o[2] = (UBYTE)((p[2] * a + c * (255 - a)) / 255);
  }
  x += (THUMB - tw) / 2; y += (THUMB - th) / 2;
  draw_rgb(rp, scr, d->buf, tw * 3, x, y, tw, th);
  theme_rect(rp, x - 1, y - 1, x + tw, y + th, tpen(lit ? T_ACCENT : T_BORDER2));
}
static void lay_draw(struct LData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  struct Screen *scr = _screen(obj);
  Doc *doc = cur_doc();
  LONG l = _mleft(obj), r = _mright(obj), y = _mtop(obj) + 5, my;
  int i, k, rows;
  char buf[80];
  theme_fill(rp, l, _mtop(obj), r, _mbottom(obj), tpen(T_PANEL));
  if (!doc) return;
  lay_list(d, doc);
  {
    Layer *a = doc->lay[doc->active];
    /* the blend mode (a list) and the opacity (dragged or typed) */
    LONG bx1 = r - 54 - theme_textlen(rp, theme_font(), tr("Opacity:")) - 8;
    theme_fill(rp, l + 6, y, bx1, y + fh() + 4, tpen(T_WHITE));
    theme_rect(rp, l + 6, y, bx1, y + fh() + 4, tpen(T_BORDER2));
    text_fit(rp, theme_font(), l + 10, y + 2, blend_label(a), bx1 - l - 28, tpen(T_TEXT));
    SetAPen(rp, tpen(T_DIM));
    my = y + (fh() + 4) / 2;
    Move(rp, bx1 - 10, my - 1); Draw(rp, bx1 - 4, my - 1); Move(rp, bx1 - 9, my); Draw(rp, bx1 - 5, my); Move(rp, bx1 - 8, my + 1); Draw(rp, bx1 - 6, my + 1);
    theme_text(rp, theme_font(), r - 54 - theme_textlen(rp, theme_font(), tr("Opacity:")), y + 2, tr("Opacity:"), tpen(T_DIM));
    sprintf(buf, "%d%%", (a->opacity * 100 + 127) / 255);
    theme_fill(rp, r - 48, y, r - 6, y + fh() + 4, tpen(T_WHITE));
    theme_rect(rp, r - 48, y, r - 6, y + fh() + 4, tpen(T_BORDER2));
    theme_text(rp, theme_font(), r - 44, y + 2, buf, tpen(T_TEXT));
    /* the locks (transparency, pixels, position, all) and the fill opacity */
    y += fh() + 10;
    theme_text(rp, theme_font(), l + 6, y + 2, tr("Lock:"), tpen(T_DIM));
    for (k = 0; k < NLOCK; k++) {
      LONG bx = l + 46 + k * 22;
      if (lock_on(a, k)) theme_fill(rp, bx - 2, y - 1, bx + 17, y + fh() + 4, tpen(T_PRESSED));
      theme_icon(rp, lock_icons[k], bx + 1, y + (fh() + 4 - 14) / 2, 14, 0);
    }
    theme_text(rp, theme_font(), r - 54 - theme_textlen(rp, theme_font(), "Fill:"), y + 2, tr("Fill:"), tpen(T_DIM));
    sprintf(buf, "%d%%", ((255 - a->fill_cut) * 100 + 127) / 255);
    theme_fill(rp, r - 48, y, r - 6, y + fh() + 4, tpen(T_WHITE));
    theme_rect(rp, r - 48, y, r - 6, y + fh() + 4, tpen(T_BORDER2));
    theme_text(rp, theme_font(), r - 44, y + 2, buf, tpen(T_TEXT));
  }
  SetAPen(rp, tpen(T_BORDER)); Move(rp, l, lay_top(obj) - 2); Draw(rp, r, lay_top(obj) - 2);
  /* the rows, the top layer first */
  rows = lay_rows(obj);
  if (d->first > d->nlist - rows) d->first = d->nlist - rows;
  if (d->first < 0) d->first = 0;
  for (k = 0; k < rows && d->first + k < d->nlist; k++) {
    int li = d->list[d->first + k];
    Layer *ly = doc->lay[li];
    LONG ry = lay_top(obj) + k * rowh(), tx = thumb_x(obj, ly), ty = ry + 3, nx = name_x(obj, ly);
    if (li == doc->active) theme_fill(rp, l, ry, r, ry + rowh() - 2, tpen(T_SELECTION));
    else if (ly->picked) theme_fill(rp, l, ry, r, ry + rowh() - 2, tpen(T_PRESSED));
    if (d->drag && d->moved && d->dragto == li) {
      if (doc->lay[li]->kind == LK_GROUP) theme_rect(rp, l + 1, ry, r - 1, ry + rowh() - 2, tpen(T_ACCENT));
      else theme_fill(rp, l, d->dragto < d->dragfrom ? ry + rowh() - 3 : ry, r, d->dragto < d->dragfrom ? ry + rowh() - 2 : ry + 1, tpen(T_ACCENT));
    }
    /* the eye */
    if (ly->visible) theme_icon(rp, IC_UNHIDE, l + 6, ry + (rowh() - 16) / 2, 16, !layer_shown(doc, li));
    SetAPen(rp, tpen(T_BORDER)); Move(rp, l + 26, ry); Draw(rp, l + 26, ry + rowh() - 2);
    /* a clipped layer's arrow down to its base */
    if (ly->clip) {
      LONG ax = tx - 7, my2 = ry + rowh() / 2;
      SetAPen(rp, tpen(T_DIM));
      Move(rp, ax, my2 - 6); Draw(rp, ax, my2 + 6); Move(rp, ax - 2, my2 + 4); Draw(rp, ax, my2 + 6); Draw(rp, ax + 2, my2 + 4);
    }
    /* a group's triangle: pointing right when closed, down when open */
    if (ly->kind == LK_GROUP) {
      LONG gx = tx - 9, gy = ry + rowh() / 2, t;
      SetAPen(rp, tpen(T_TEXT));
      for (t = 0; t < 4; t++) {
        if (ly->collapsed) { Move(rp, gx + t, gy - 3 + t); Draw(rp, gx + t, gy + 3 - t); }
        else { Move(rp, gx - 3 + t, gy - 1 + t); Draw(rp, gx + 3 - t, gy - 1 + t); }
      }
    }
    /* the thumbnail (and the mask's beside it) */
    if (ly->kind == LK_GROUP) theme_icon(rp, IC_FOLDER, tx + (THUMB - 20) / 2, ty + (THUMB - 20) / 2, 20, 0);
    else if (ly->kind == LK_ADJUST) theme_icon(rp, IC_ADJLAYER, tx + (THUMB - 20) / 2, ty + (THUMB - 20) / 2, 20, 0);
    else if (ly->kind == LK_FILL) fill_thumb(d, rp, scr, ly, tx, ty);
    else if (ly->kind == LK_TEXT || ly->kind == LK_SHAPE) live_thumb(d, rp, scr, doc, ly, tx, ty, li == doc->active && ly->mask && !ly->edit_mask);
    else if (ly->px) thumb_draw(d, rp, scr, doc, ly->px, NULL, tx, ty, li == doc->active && ly->mask && !ly->edit_mask);
    if (ly->mask) {
      thumb_draw(d, rp, scr, doc, NULL, ly->mask, mask_x(obj, ly), ty, li == doc->active && ly->edit_mask);
      if (!ly->mask_on) {                                  /* turned off: a red cross over it */
        LONG mx0 = mask_x(obj, ly);
        SetAPen(rp, tpen(T_RED));
        Move(rp, mx0, ty); Draw(rp, mx0 + THUMB - 1, ty + THUMB - 1); Move(rp, mx0 + THUMB - 1, ty); Draw(rp, mx0, ty + THUMB - 1);
      }
    }
    to_latin1(ly->name, buf, sizeof buf);
    text_fit(rp, theme_font(), nx, ry + (rowh() - fh()) / 2 - 1, buf, r - nx - (fx_has(ly) ? 38 : 22), tpen(T_TEXT));
    /* 1.1: the colour label down the eye's column, and "fx" for a layer with a style */
    if (ly->label && ly->label < NLABELS) draw_rgb_box(rp, scr, l + 1, ry + 1, l + 3, ry + rowh() - 3, label_colors[ly->label]);
    if (fx_has(ly)) theme_text(rp, theme_small(), r - 34, ry + (rowh() - fh()) / 2, "fx", tpen(ly->fx.hidden ? T_BORDER2 : T_DIM));
    if (ly->lock_all || ly->locked) theme_icon(rp, IC_LOCKL, r - 18, ry + (rowh() - 12) / 2, 12, 0);
    else if (ly->lock_pos) theme_icon(rp, IC_LOCKPOS, r - 18, ry + (rowh() - 12) / 2, 12, 0);
    else if (ly->alpha_lock) theme_icon(rp, IC_ALPHALOCK, r - 18, ry + (rowh() - 12) / 2, 12, 0);
    if (THUMB >= 28 && (ly->opacity < 255 || (ly->blend && ly->blend != BM_PASS) || ly->fill_cut)) {
      sprintf(buf, "%s %d%%", ly->blend && ly->blend != BM_PASS ? blend_label(ly) : "", (ly->opacity * 100 + 127) / 255);
      text_fit(rp, theme_small(), nx, ry + rowh() / 2 + 3, buf, r - nx - 22, tpen(T_DIM));
    }
    SetAPen(rp, tpen(T_BORDER)); Move(rp, l, ry + rowh() - 1); Draw(rp, r, ry + rowh() - 1);
  }
  /* more layers than fit: a thin bar at the right */
  if (d->nlist > rows && rows > 0) {
    LONG t = lay_top(obj), b = t + rows * rowh(), a0 = t + (b - t) * d->first / d->nlist, a1 = t + (b - t) * (d->first + rows) / d->nlist;
    theme_fill(rp, r - 7, t, r, b, tpen(T_CANVAS));
    theme_fill(rp, r - 6, a0 + 1, r - 1, a1 - 1, tpen(T_BORDER2));
  }
  /* the buttons */
  SetAPen(rp, tpen(T_BORDER)); Move(rp, l, lay_bottom(obj)); Draw(rp, r, lay_bottom(obj));
  for (i = 0; i < NLB; i++) {
    LONG bx = r - (NLB - i) * 24 - 4;
    theme_icon(rp, lay_buttons[i], bx + 4, lay_bottom(obj) + 5, 16, i == 0 && doc->lay[doc->active]->mask != NULL);
  }
}
static int lay_row_at(Object *obj, struct LData *d, LONG my)
{
  Doc *doc = cur_doc();
  LONG k;
  if (!doc || my < lay_top(obj)) return -1;
  lay_list(d, doc);
  k = (my - lay_top(obj)) / rowh();
  if (k >= lay_rows(obj) || d->first + k >= d->nlist) return -1;
  return d->list[d->first + (int)k];
}
static void lay_changed(Object *obj) { SetAttrs(obj, MUIA_Layers_Changed, TRUE, TAG_DONE); }
/* 1.1: the right button on a layer's row, its thumbnail or its mask's thumbnail: that layer's menu
   (ctxmenu.c). A layer outside the picked ones becomes the active one first, as a click would */
static Object *lay_context(Object *obj, struct LData *d, LONG mx, LONG my)
{
  Doc *doc = cur_doc();
  CtxState s;
  int li = lay_row_at(obj, d, my), target = CTX_LAYERROW;
  Layer *ly;
  if (!doc || li < 0 || li >= doc->nlay || d->drag || d->scrub >= 0) return NULL;
  ly = doc->lay[li];
  if (li != doc->active && !ly->picked) { doc_pick_layer(doc, li, 0); MUI_Redraw(obj, MADF_DRAWOBJECT); lay_changed(obj); }
  if (ly->mask && mx >= mask_x(obj, ly) && mx < mask_x(obj, ly) + THUMB) target = CTX_MASKTHUMB;
  else if (mx >= thumb_x(obj, ly) && mx < thumb_x(obj, ly) + THUMB) target = CTX_LAYERTHUMB;
  ctx_state(&s);
  return ctx_show(target, &s);
}
static ULONG laydispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct LData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->lastrow = -1; d->scrub = -1;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY;
    ctx_attach(obj, CTX_LAYERROW);               /* (1.1: the right button's menus: row, thumbnail, mask) */
    ctx_attach(obj, CTX_LAYERTHUMB); ctx_attach(obj, CTX_MASKTHUMB);
    return (ULONG)obj;
  case OM_DISPOSE:
    d = INST_DATA(cl, obj);
    ctx_detach(obj, CTX_LAYERROW); ctx_detach(obj, CTX_LAYERTHUMB); ctx_detach(obj, CTX_MASKTHUMB);
    if (d->buf) FreeVec(d->buf);
    break;
  case MUIM_ContextMenuBuild: return (ULONG)lay_context(obj, INST_DATA(cl, obj), ((struct MUIP_ContextMenuBuild *)msg)->mx, ((struct MUIP_ContextMenuBuild *)msg)->my);
  case MUIM_ContextMenuChoice: return ctx_choice(obj, ((struct MUIP_ContextMenuChoice *)msg)->item);
  case OM_SET: {
    struct TagItem *t = FindTagItem(MUIA_Popup_Chosen, ((struct opSet *)msg)->ops_AttrList);
    struct TagItem *ta = FindTagItem(MUIA_Layers_AdjPick, ((struct opSet *)msg)->ops_AttrList);
    Doc *doc = cur_doc();
    if (t && doc && (int)t->ti_Data <= BM_N) {
      int b = (int)t->ti_Data;
      if (b == BM_N) b = doc->lay[doc->active]->kind == LK_GROUP ? BM_PASS : BM_NORMAL;
      doc_set_layer_props(doc, doc->active, NULL, -1, b, -1);
      lay_changed(obj);
    }
    if (ta && doc && (int)ta->ti_Data < AK_N) DoMethod(_app(obj), MUIM_Application_ReturnID, ID_NEWADJ0 + ta->ti_Data);
    break;
  }
  case OM_GET: {
    struct opGet *g = (struct opGet *)msg;
    if (g->opg_AttrID == MUIA_Layers_Changed) { *g->opg_Storage = TRUE; return TRUE; }
    if (g->opg_AttrID == MUIA_Layers_Command) { *g->opg_Storage = ((struct LData *)INST_DATA(cl, obj))->cmd; return TRUE; }
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 200; m->MinMaxInfo->DefWidth += 240; m->MinMaxInfo->MaxWidth += 320;
    m->MinMaxInfo->MinHeight += 2 * (fh() + 10) + 2 * (THUMB + 6) + 30; m->MinMaxInfo->DefHeight += 2 * (fh() + 10) + 6 * (THUMB + 6) + 30;
    m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  PANEL_COMMON(struct LData)
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) { lay_fit(obj); lay_draw(INST_DATA(cl, obj), obj); }
    return 0;
  case MUIM_Layers_Refresh:
    if (((struct LData *)INST_DATA(cl, obj))->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  case MUIM_CreateShortHelp: {
    struct MUIP_CreateShortHelp *m = (struct MUIP_CreateShortHelp *)msg;
    Doc *doc = cur_doc();
    LONG mx = m->mx, my = m->my, l = _mleft(obj), r = _mright(obj), y0 = _mtop(obj) + 5;
    int i, li;
    static const char *const bn[] = { "Add Layer Mask", "New Adjustment Layer", "New Group", "New Layer", "Duplicate Layer", "Delete Layer" };
    static const char *const bb[] = { "Adds a mask to the layer in use: it shows the layer where the mask is white and hides it where it is black (the selection, if there is one).",
      "Adds a layer that adjusts the layers below it (Levels, Curves, Hue/Saturation and the rest) without changing their pixels. Click for the list.",
      "Adds an empty group above the layer in use. Drag layers onto its row to put them in it.",
      "Adds an empty layer above the one in use.", "Adds a copy of the layer in use (or the whole group) above it (Amiga-J).",
      "Deletes the layer in use (a group with everything in it)." };
    static const char *const ln[] = { "Lock Transparency", "Lock Pixels", "Lock Position", "Lock All" };
    static const char *const lb[] = { "Painting changes only the layer's existing pixels; its clear parts stay clear.",
      "The layer's pixels can't be painted on or changed.", "The layer can't be moved.", "Nothing can change the layer in use." };
    d = INST_DATA(cl, obj);
    if (!doc) return 0;
    if (my >= y0 && my <= y0 + fh() + 4) {
      if (mx <= r - 54 - theme_textlen(_rp(obj), theme_font(), tr("Opacity:")) - 8) return (ULONG)theme_bubble(tr("Blend Mode"), tr("How the layer in use mixes with the layers below it. Click for the list. A group's Pass Through lets its layers mix with everything below."));
      return (ULONG)theme_bubble(tr("Opacity"), tr("How solid the layer in use is. Drag left or right to change it, or click to type it."));
    }
    if (my >= y0 + fh() + 9 && my < lay_top(obj) - 2) {
      int k = (int)((mx - l - 44) / 22);
      if (mx >= l + 44 && k >= 0 && k < NLOCK) return (ULONG)theme_bubble(tr(ln[k]), tr(lb[k]));
      if (mx >= r - 48) return (ULONG)theme_bubble(tr("Fill Opacity"), tr("Like opacity, but for the layer's own pixels only. Drag left or right, or click to type it."));
      return 0;
    }
    if (my >= lay_bottom(obj)) {
      for (i = 0; i < NLB; i++) {
        LONG bx = r - (NLB - i) * 24 - 4;
        if (mx >= bx && mx < bx + 22) return (ULONG)theme_bubble(tr(bn[i]), tr(bb[i]));
      }
      return 0;
    }
    if ((li = lay_row_at(obj, d, my)) < 0) return 0;
    if (mx < l + 26) return (ULONG)theme_bubble(tr("Show or Hide"), tr("Click the eye to hide the layer, and again to show it."));
    if (doc->lay[li]->mask && mx >= mask_x(obj, doc->lay[li]) && mx < mask_x(obj, doc->lay[li]) + THUMB)
      return (ULONG)theme_bubble(tr("Layer Mask"), tr("Click to paint the mask instead of the layer: black hides, white shows. Shift-click turns the mask off and on."));
    if (doc->lay[li]->kind == LK_GROUP) return (ULONG)theme_bubble(tr("Group"), tr("Click the triangle to open or close the group. Drag layers onto it to put them in; double-click for its settings."));
    if (doc->lay[li]->kind == LK_ADJUST) return (ULONG)theme_bubble(tr("Adjustment Layer"), tr("Adjusts what is below it. Double-click to change its settings."));
    if (doc->lay[li]->kind == LK_FILL) return (ULONG)theme_bubble(tr("Fill Layer"), tr("A colour or gradient filling the canvas. Double-click to change it."));
    if (doc->lay[li]->kind == LK_TEXT) return (ULONG)theme_bubble(tr("Type Layer"), tr("Text you can edit: double-click its thumbnail to edit the text, elsewhere for the layer's settings. Layer > Type has more."));
    if (doc->lay[li]->kind == LK_SHAPE) return (ULONG)theme_bubble(tr("Shape Layer"), tr("A shape that stays sharp: double-click its thumbnail to change its fill, stroke and size (Layer > Shape Properties)."));
    return (ULONG)theme_bubble(tr("Layer"), tr("Click to work on this layer (Shift-click for several in a row, Ctrl-click to add one), drag it to change the order (onto a group to put it in), or double-click for its settings."));
  }
  case MUIM_DeleteShortHelp: return 0;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    Doc *doc = cur_doc();
    LONG mx, my, l, r, y0;
    int li, i;
    d = INST_DATA(cl, obj);
    if (!im || !d->shown || !doc) return 0;
    mx = im->MouseX; my = im->MouseY; l = _mleft(obj); r = _mright(obj); y0 = _mtop(obj) + 5;
    lay_fit(obj);
    if (im->Class == IDCMP_MOUSEMOVE) {
      if (d->scrub >= 0) {
        int v = d->grabv + (int)(mx - d->grabx);
        Layer *a = doc->lay[doc->active];
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        if (mx != d->grabx) d->moved = 1;
        if (d->moved) {
          /* (the change shown as it is dragged; one undo step once it is let go) */
          u8 nv = (u8)((v * 255 + 50) / 100);
          if (d->scrub == 1 && nv != a->opacity) a->opacity = nv;
          else if (d->scrub == 2 && 255 - nv != a->fill_cut) a->fill_cut = (u8)(255 - nv);
          else return MUI_EventHandlerRC_Eat;
          MUI_Redraw(obj, MADF_DRAWOBJECT);
          DoMethod(canvas, MUIM_Canvas_Update, 0, 0, doc->w, doc->h);
        }
        return MUI_EventHandlerRC_Eat;
      }
      if (d->drag) {
        int to = lay_row_at(obj, d, my);
        if (to >= 0 && to != d->dragto) { d->dragto = to; d->moved = to != d->dragfrom; MUI_Redraw(obj, MADF_DRAWOBJECT); }
        return MUI_EventHandlerRC_Eat;
      }
      return 0;
    }
    if (im->Class == IDCMP_RAWKEY && (im->Code == 0x7a || im->Code == 0x7b)) {
      /* the mouse wheel over the rows */
      if (mx < l || mx > r || my < lay_top(obj) || my > lay_bottom(obj)) return 0;
      d->first += im->Code == 0x7a ? -1 : 1;
      MUI_Redraw(obj, MADF_DRAWOBJECT);
      return MUI_EventHandlerRC_Eat;
    }
    if (im->Class != IDCMP_MOUSEBUTTONS) return 0;
    if (im->Code == SELECTUP) {
      if (d->scrub >= 0) {
        Layer *a = doc->lay[doc->active];
        int fill = d->scrub == 2, v = fill ? ((255 - a->fill_cut) * 100 + 127) / 255 : (a->opacity * 100 + 127) / 255;
        d->scrub = -1;
        if (fill) a->fill_cut = (u8)d->grabv2; else a->opacity = (u8)d->grabv2;
        if (!d->moved) {
          LONG n = v;
          if (!ask_number(fill ? "Fill Opacity" : "Layer Opacity", fill ? "Fill (%):" : "Opacity (%):", &n, 0, 100)) return MUI_EventHandlerRC_Eat;
          v = (int)n;
        }
        if (fill) { doc_begin(doc, "Fill Opacity"); a->fill_cut = (u8)(255 - (v * 255 + 50) / 100); doc_end(doc); }
        else doc_set_layer_props(doc, doc->active, NULL, (v * 255 + 50) / 100, -1, -1);
        lay_changed(obj);
        return MUI_EventHandlerRC_Eat;
      }
      if (d->drag) {
        d->drag = 0;
        if (d->moved && d->dragto >= 0 && d->dragto != d->dragfrom) {
          int to = d->dragto, from = d->dragfrom, s = block_start(doc, from);
          Layer *t = doc->lay[to];
          if (to >= s && to <= from) { /* onto itself or its own members: nothing */ }
          else if (t->kind == LK_GROUP && !t->collapsed) doc_move_block(doc, from, to - 1, t->depth + 1);    /* into the group, at its top */
          else if (t->kind == LK_GROUP && t->collapsed && to > from) doc_move_block(doc, from, to, t->depth);
          else if (to > from) doc_move_block(doc, from, to, t->depth);                            /* dragged up: above it */
          else doc_move_block(doc, from, block_start(doc, to) - 1, t->depth);                     /* dragged down: below it */
          lay_changed(obj);
        } else MUI_Redraw(obj, MADF_DRAWOBJECT);
        d->moved = 0;
        return MUI_EventHandlerRC_Eat;
      }
      return 0;
    }
    if (im->Code != SELECTDOWN || mx < l || mx > r || my < _mtop(obj) || my > _mbottom(obj)) return 0;
    if (my >= y0 && my <= y0 + fh() + 4) {
      if (mx <= r - 54 - theme_textlen(_rp(obj), theme_font(), tr("Opacity:")) - 8) {
        struct Window *w = _window(obj);
        Layer *a = doc->lay[doc->active];
        popup_list(obj, w->LeftEdge + l + 6, w->TopEdge + y0 + fh() + 5, 115, blendnames, a->kind == LK_GROUP ? BM_N + 1 : BM_N,
                   a->blend == BM_PASS ? BM_N : a->blend, MUIA_Popup_Chosen);
      } else if (mx >= r - 48) {
        d->scrub = 1; d->grabx = mx; d->moved = 0;
        d->grabv2 = doc->lay[doc->active]->opacity;
        d->grabv = (d->grabv2 * 100 + 127) / 255;
      }
      return MUI_EventHandlerRC_Eat;
    }
    if (my >= y0 + fh() + 9 && my < lay_top(obj) - 2) {
      int k = (int)((mx - l - 44) / 22);
      Layer *a = doc->lay[doc->active];
      if (mx >= l + 44 && k >= 0 && k < NLOCK) {
        /* (the locks aren't undo steps: PhotoCraft's aren't either) */
        if (k == 0) a->alpha_lock = !a->alpha_lock;
        else if (k == 1) a->locked = !a->locked;
        else if (k == 2) a->lock_pos = !a->lock_pos;
        else a->lock_all = !a->lock_all;
        lay_changed(obj);
      } else if (mx >= r - 48) {
        d->scrub = 2; d->grabx = mx; d->moved = 0;
        d->grabv2 = a->fill_cut;
        d->grabv = ((255 - a->fill_cut) * 100 + 127) / 255;
      }
      return MUI_EventHandlerRC_Eat;
    }
    if (my >= lay_bottom(obj)) {
      for (i = 0; i < NLB; i++) {
        LONG bx = r - (NLB - i) * 24 - 4;
        if (mx >= bx && mx < bx + 22) {
          if (lay_ids[i] == ID_NEWADJ) {
            struct Window *w = _window(obj);
            popup_list(obj, w->LeftEdge + bx - 100, w->TopEdge + lay_bottom(obj) - AK_N * (fh() + 4) - 8, 130, adj_names, AK_N, -1, MUIA_Layers_AdjPick);
            break;
          }
          d->cmd = lay_ids[i] == ID_MASK_REVEALALL && doc->sel ? ID_MASK_REVEALSEL : lay_ids[i];
          DoMethod(_app(obj), MUIM_Application_ReturnID, d->cmd);
          break;
        }
      }
      return MUI_EventHandlerRC_Eat;
    }
    lay_list(d, doc);
    if (d->nlist > lay_rows(obj) && mx >= r - 8) {
      /* the bar: a page up or down */
      int rows = lay_rows(obj);
      LONG t = lay_top(obj), a0 = t + rows * rowh() * d->first / d->nlist;
      d->first += my < a0 ? -rows : rows;
      MUI_Redraw(obj, MADF_DRAWOBJECT);
      return MUI_EventHandlerRC_Eat;
    }
    li = lay_row_at(obj, d, my);
    if (li < 0) return MUI_EventHandlerRC_Eat;
    {
      Layer *ly = doc->lay[li];
      LONG tx = thumb_x(obj, ly);
      int shift = (im->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0, ctrl = (im->Qualifier & IEQUALIFIER_CONTROL) != 0;
      if (mx < l + 26) {
        doc_set_layer_props(doc, li, NULL, -1, -1, !ly->visible);
        lay_changed(obj);
        return MUI_EventHandlerRC_Eat;
      }
      if (ly->kind == LK_GROUP && mx >= tx - 13 && mx < tx) {
        ly->collapsed = !ly->collapsed;                  /* (open or closed isn't an undo step) */
        MUI_Redraw(obj, MADF_DRAWOBJECT);
        return MUI_EventHandlerRC_Eat;
      }
      if (ly->mask && mx >= mask_x(obj, ly) && mx < mask_x(obj, ly) + THUMB) {
        if (shift) doc_mask_set(doc, li, !ly->mask_on, -1, -1, -1);
        else { doc_pick_layer(doc, li, 0); ly->edit_mask = 1; }
        lay_changed(obj);
        return MUI_EventHandlerRC_Eat;
      }
      if (mx >= tx && mx < tx + THUMB && ly->edit_mask) ly->edit_mask = 0;
      if (li == d->lastrow && !shift && !ctrl && DoubleClick(d->secs, d->micros, im->Seconds, im->Micros)) {
        d->lastrow = -1;
        /* 0.9: a type layer's thumbnail edits its text, a shape layer's opens its properties */
        DoMethod(_app(obj), MUIM_Application_ReturnID, ly->kind == LK_TEXT && mx >= tx && mx < tx + THUMB ? ID_EDITTYPE :
                 ly->kind == LK_SHAPE && mx >= tx && mx < tx + THUMB ? ID_SHAPEPROPS : ID_LAYERPROPS);
        return MUI_EventHandlerRC_Eat;
      }
      d->lastrow = li; d->secs = im->Seconds; d->micros = im->Micros;
      doc_pick_layer(doc, li, shift ? 2 : ctrl ? 1 : 0);
      lay_changed(obj);
      if (!shift && !ctrl) { d->drag = 1; d->dragfrom = d->dragto = li; d->moved = 0; }
    }
    return MUI_EventHandlerRC_Eat;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *layers_class(void)
{
  if (!laycls) laycls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct LData), (APTR)laydispatch);
  return laycls;
}

/* ================================================================== the History panel */
/* 0.8: the snapshots first (each a whole copy of the picture), then the steps; along the bottom the
   buttons: a new snapshot, a new picture from the state shown, and delete the snapshot picked */
struct HData { struct MUI_EventHandlerNode ehn; int shown, first; };
#define MAXHIST 120
int hist_pick = -1;
static const int hist_buttons[] = { IC_NEW, IC_CAMERA, IC_TRASH };
static const ULONG hist_ids[] = { ID_HISTNEWDOC, ID_HISTSNAP, ID_HISTSNAPDEL };
#define NHB 3
static LONG hrow(void) { return fh() + 6; }
static LONG hist_bottom(Object *obj) { return _mbottom(obj) - 26; }
/* the rows: the snapshots, the document's start, the steps done (oldest first), then those undone;
   *nsnap the snapshots' rows, *ndone the rows up to the state in effect (after the snapshots) */
static int hist_list(Doc *doc, const char **names, int *ndone, int *nsnap)
{
  DocView *v = cur_view();
  int s = 0, n, m, i;
  for (i = 0; v && i < v->nhsnap && s < MAXHIST / 2; i++) names[s++] = v->hsnap_name[i];
  *nsnap = s;
  n = doc_history(doc, names + s + 1, MAXHIST - s - 1);
  names[s] = v && v->path[0] ? "Open" : "New";
  *ndone = n + 1;
  m = doc_redo_history(doc, names + s + 1 + n, MAXHIST - s - 1 - n);
  return s + n + 1 + m;
}
static void hist_draw(struct HData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  Doc *doc = cur_doc();
  const char *names[MAXHIST];
  int n, done, ns, i, rows, cur;
  LONG r = _mright(obj);
  theme_fill(rp, _mleft(obj), _mtop(obj), r, _mbottom(obj), tpen(T_PANEL));
  if (!doc) return;
  n = hist_list(doc, names, &done, &ns);
  if (hist_pick >= ns) hist_pick = ns - 1;
  rows = (int)((hist_bottom(obj) - _mtop(obj)) / hrow());
  cur = ns + done - 1;
  /* the current step kept in view */
  if (cur < d->first) d->first = cur;
  if (cur >= d->first + rows) d->first = cur - rows + 1;
  if (d->first > n - rows) d->first = n - rows;
  if (d->first < 0) d->first = 0;
  for (i = 0; i < rows && d->first + i < n; i++) {
    int k = d->first + i;
    LONG y = _mtop(obj) + i * hrow();
    if (k < ns) {
      /* a snapshot (the one picked lit faintly), with a line under the last */
      if (k == hist_pick) theme_fill(rp, _mleft(obj), y, r, y + hrow() - 1, tpen(T_PRESSED));
      theme_icon(rp, IC_CAMERA, _mleft(obj) + 6, y + (hrow() - 12) / 2, 12, 0);
      text_fit(rp, theme_font(), _mleft(obj) + 24, y + 3, names[k], _mwidth(obj) - 28, tpen(T_TEXT));
      if (k == ns - 1) { SetAPen(rp, tpen(T_BORDER)); Move(rp, _mleft(obj), y + hrow() - 1); Draw(rp, r, y + hrow() - 1); }
      continue;
    }
    if (k == cur) theme_fill(rp, _mleft(obj), y, r, y + hrow() - 1, tpen(T_SELECTION));
    theme_icon(rp, k == ns ? IC_NEW : IC_HISTSTEP, _mleft(obj) + 6, y + (hrow() - 12) / 2, 12, k > cur);
    text_fit(rp, theme_font(), _mleft(obj) + 24, y + 3, tr(names[k]), _mwidth(obj) - 28, tpen(k > cur ? T_DISABLED : T_TEXT));
  }
  /* the buttons */
  SetAPen(rp, tpen(T_BORDER)); Move(rp, _mleft(obj), hist_bottom(obj)); Draw(rp, r, hist_bottom(obj));
  for (i = 0; i < NHB; i++) theme_icon(rp, hist_buttons[i], r - (NHB - i) * 24, hist_bottom(obj) + 5, 16, i == 2 && hist_pick < 0);
}
/* the button at window x, y along the bottom (-1 none) */
static int hist_button_at(Object *obj, LONG mx, LONG my)
{
  int i;
  if (my < hist_bottom(obj) || my > _mbottom(obj)) return -1;
  for (i = 0; i < NHB; i++) { LONG bx = _mright(obj) - (NHB - i) * 24; if (mx >= bx - 4 && mx < bx + 20) return i; }
  return -1;
}
static ULONG histdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct HData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS;
    return (ULONG)obj;
  case OM_GET:
    if (((struct opGet *)msg)->opg_AttrID == MUIA_History_Changed) { *((struct opGet *)msg)->opg_Storage = TRUE; return TRUE; }
    break;
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 200; m->MinMaxInfo->DefWidth += 240; m->MinMaxInfo->MaxWidth += 320;
    m->MinMaxInfo->MinHeight += 2 * hrow() + 26; m->MinMaxInfo->DefHeight += 6 * hrow() + 26; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  PANEL_COMMON(struct HData)
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) hist_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_History_Refresh:
    if (((struct HData *)INST_DATA(cl, obj))->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  case MUIM_CreateShortHelp: {
    static const char *const bn[NHB] = { "New Document from State", "New Snapshot", "Delete Snapshot" };
    static const char *const bb[NHB] = { "A new picture, a copy of this one as it is now.", "Keeps a copy of the whole picture as it is now, at the top of the list. Click it later to go back to it (as a step you can undo).",
      "Deletes the snapshot you clicked last; or else the step in effect and the ones after it." };
    struct MUIP_CreateShortHelp *m = (struct MUIP_CreateShortHelp *)msg;
    int b = hist_button_at(obj, m->mx, m->my);
    if (!cur_doc()) return 0;
    if (b >= 0) return (ULONG)theme_bubble(tr(bn[b]), tr(bb[b]));
    return (ULONG)theme_bubble(tr("History"), tr("Snapshots at the top, then each change you have made, oldest first. Click a step to go back to it; click a later (dimmed) one to go forward again; click a snapshot to go back to it."));
  }
  case MUIM_DeleteShortHelp: return 0;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    Doc *doc = cur_doc();
    const char *names[MAXHIST];
    int n, done, ns, k, b;
    d = INST_DATA(cl, obj);
    if (!im || !d->shown || !doc || im->Class != IDCMP_MOUSEBUTTONS || im->Code != SELECTDOWN) return 0;
    if (im->MouseX < _mleft(obj) || im->MouseX > _mright(obj) || im->MouseY < _mtop(obj) || im->MouseY > _mbottom(obj)) return 0;
    if ((b = hist_button_at(obj, im->MouseX, im->MouseY)) >= 0) {
      DoMethod(_app(obj), MUIM_Application_ReturnID, hist_ids[b]);     /* (1.1: the bin deletes a step too) */
      return MUI_EventHandlerRC_Eat;
    }
    if (im->MouseY >= hist_bottom(obj)) return MUI_EventHandlerRC_Eat;
    n = hist_list(doc, names, &done, &ns);
    k = d->first + (int)((im->MouseY - _mtop(obj)) / hrow());
    if (k >= n) return MUI_EventHandlerRC_Eat;
    if (k < ns) {
      /* a snapshot: picked, and the picture taken back to it (in main.c, as an undoable step) */
      hist_pick = k;
      DoMethod(_app(obj), MUIM_Application_ReturnID, ID_HISTSNAPGO);
      return MUI_EventHandlerRC_Eat;
    }
    k -= ns;
    hist_pick = -1;                     /* (1.1: the bin now means this step) */
    /* back (or forward) to that step */
    while (done - 1 > k && doc_undo(doc)) done--;
    while (done - 1 < k && doc_redo(doc)) done++;
    SetAttrs(obj, MUIA_History_Changed, TRUE, TAG_DONE);
    return MUI_EventHandlerRC_Eat;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *history_class(void)
{
  if (!histcls) histcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct HData), (APTR)histdispatch);
  return histcls;
}

/* ================================================================== a panel's title strip */
struct PHData { struct MUI_EventHandlerNode ehn; int shown, active, n; const char *const *labels; };
/* 0.9: tab i's label: "Long|Short" gives the short one when the long ones don't all fit (into buf) */
static const char *head_label(struct PHData *d, int i, int brief, char *buf)
{
  const char *l = tr(d->labels[i]), *bar = strchr(l, '|');
  size_t n;
  if (!bar) return l;
  if (brief) return bar + 1;
  n = (size_t)(bar - l) < 39 ? (size_t)(bar - l) : 39;
  memcpy(buf, l, n); buf[n] = 0;
  return buf;
}
/* the tabs' widths: the long labels with room round them if they fit, else the short ones, closer
   together if need be (w[i] each tab's width; *brief set when the short labels are used) */
static void head_widths(struct PHData *d, Object *obj, LONG *w, int *brief)
{
  struct RastPort *rp = _rp(obj);
  char buf[40];
  LONG tot, pad;
  int i, b;
  for (b = 0; b < 2; b++) {
    for (tot = 0, i = 0; i < d->n; i++) tot += theme_textlen(rp, i == d->active ? theme_bold() : theme_font(), head_label(d, i, b, buf));
    if (tot + 20 * d->n <= _mwidth(obj) || b == 1) break;
  }
  pad = (_mwidth(obj) - tot) / (d->n ? d->n : 1);
  if (pad > 20) pad = 20;
  if (pad < 6) pad = 6;
  for (i = 0; i < d->n; i++) w[i] = theme_textlen(rp, i == d->active ? theme_bold() : theme_font(), head_label(d, i, b, buf)) + pad;
  *brief = b;
}
static ULONG headdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct PHData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->labels = (const char *const *)GetTagData(MUIA_Head_Labels, 0, ((struct opSet *)msg)->ops_AttrList);
    while (d->labels && d->labels[d->n]) d->n++;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS;
    return (ULONG)obj;
  case OM_SET: {
    struct TagItem *t = FindTagItem(MUIA_Head_Active, ((struct opSet *)msg)->ops_AttrList);
    d = INST_DATA(cl, obj);
    if (t && d->active != (int)t->ti_Data) { d->active = (int)t->ti_Data; if (d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT); }
    break;
  }
  case OM_GET:
    if (((struct opGet *)msg)->opg_AttrID == MUIA_Head_Active) { *((struct opGet *)msg)->opg_Storage = (ULONG)((struct PHData *)INST_DATA(cl, obj))->active; return TRUE; }
    break;
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 60; m->MinMaxInfo->DefWidth += 240; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += fh() + 8; m->MinMaxInfo->DefHeight += fh() + 8; m->MinMaxInfo->MaxHeight += fh() + 8;
    return 0;
  }
  PANEL_COMMON(struct PHData)
  case MUIM_Draw: {
    struct RastPort *rp = _rp(obj);
    LONG x;
    int i;
    DoSuperMethodA(cl, obj, msg);
    d = INST_DATA(cl, obj);
    if (!(((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))) return 0;
    theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_TABSTRIP));
    x = _mleft(obj);
    {
      LONG ws[8];
      int brief;
      char buf[40];
      head_widths(d, obj, ws, &brief);
      for (i = 0; i < d->n && i < 8; i++) {
        LONG w = ws[i];
        if (i == d->active) theme_fill(rp, x, _mtop(obj) + 1, x + w - 1, _mbottom(obj), tpen(T_PANEL));
        const char *lab = head_label(d, i, brief, buf);
        struct TextFont *tf = i == d->active ? theme_bold() : theme_font();
        theme_text(rp, tf, x + (w - theme_textlen(rp, tf, lab)) / 2, _mtop(obj) + 4, lab, tpen(i == d->active ? T_TEXT : T_DIM));
        x += w;
      }
    }
    SetAPen(rp, tpen(T_BORDER)); Move(rp, _mleft(obj), _mtop(obj)); Draw(rp, _mright(obj), _mtop(obj));
    return 0;
  }
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    LONG x;
    int i;
    d = INST_DATA(cl, obj);
    if (!im || !d->shown || im->Code != SELECTDOWN || im->MouseY < _mtop(obj) || im->MouseY > _mbottom(obj)) return 0;
    x = _mleft(obj);
    {
      LONG ws[8];
      int brief;
      head_widths(d, obj, ws, &brief);
      for (i = 0; i < d->n && i < 8; i++) {
        LONG w = ws[i];
        if (im->MouseX >= x && im->MouseX < x + w) { if (i != d->active) SetAttrs(obj, MUIA_Head_Active, i, TAG_DONE); return MUI_EventHandlerRC_Eat; }
        x += w;
      }
    }
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *head_class(void)
{
  if (!headcls) headcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct PHData), (APTR)headdispatch);
  return headcls;
}

/* ================================================================== the documents' tabs */
struct DTData { struct MUI_EventHandlerNode ehn; int shown, pick; LONG x0[MAXDOCS], x1[MAXDOCS]; };
static void tab_label(int i, char *buf, size_t cap)
{
  DocView *v = &docs[i];
  Doc *doc = v->doc;
  char lay[48];
  to_latin1(doc->lay[doc->active]->name, lay, sizeof lay);
  snprintf(buf, cap, "%s @ %ld%% (%s, %s)%s", v->name, (long)(v->zoom * 100 + 0.5), lay, doc->mode == MODE_INDEXED ? tr("Indexed") : doc->mode == MODE_GRAY ? tr("Gray/8") : doc->mode == MODE_BITMAP ? tr("Bitmap") : "RGB/8", doc->dirty ? "*" : "");
}
static ULONG tabdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct DTData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS;
    ctx_attach(obj, CTX_DOCTAB);                 /* (1.1: Close, Close Others, Close All ...) */
    return (ULONG)obj;
  case OM_DISPOSE: ctx_detach(obj, CTX_DOCTAB); break;
  case MUIM_ContextMenuBuild: {
    /* the tab right-clicked: its menu's commands work on that picture (ctx_command switches to it) */
    struct MUIP_ContextMenuBuild *m = (struct MUIP_ContextMenuBuild *)msg;
    CtxState s;
    int i;
    d = INST_DATA(cl, obj);
    for (i = 0; i < ndocs; i++)
      if (d->x0[i] >= 0 && m->mx >= d->x0[i] && m->mx <= d->x1[i]) {
        ctx_state(&s);
        ctx_tab = i;
        return (ULONG)ctx_show(CTX_DOCTAB, &s);
      }
    return 0;
  }
  case MUIM_ContextMenuChoice: return ctx_choice(obj, ((struct MUIP_ContextMenuChoice *)msg)->item);
  case OM_GET: {
    struct opGet *g = (struct opGet *)msg;
    if (g->opg_AttrID == MUIA_DocTabs_Pick || g->opg_AttrID == MUIA_DocTabs_Close) { *g->opg_Storage = (ULONG)((struct DTData *)INST_DATA(cl, obj))->pick; return TRUE; }
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 100; m->MinMaxInfo->DefWidth += 640; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += fh() + 9; m->MinMaxInfo->DefHeight += fh() + 9; m->MinMaxInfo->MaxHeight += fh() + 9;
    return 0;
  }
  PANEL_COMMON(struct DTData)
  case MUIM_DocTabs_Refresh:
    if (((struct DTData *)INST_DATA(cl, obj))->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  case MUIM_CreateShortHelp: {
    struct MUIP_CreateShortHelp *m = (struct MUIP_CreateShortHelp *)msg;
    static char name[140];
    int i;
    d = INST_DATA(cl, obj);
    for (i = 0; i < ndocs; i++)
      if (d->x0[i] >= 0 && m->mx >= d->x0[i] && m->mx <= d->x1[i]) {
        if (m->mx >= d->x1[i] - 20) return (ULONG)theme_bubble(tr("Close"), tr("Closes this picture (asking first if it has changes)."));
        snprintf(name, sizeof name, "%s", docs[i].path[0] ? docs[i].path : docs[i].name);
        sprintf(name + strlen(name), "  %ld x %ld", (long)docs[i].doc->w, (long)docs[i].doc->h);
        return (ULONG)theme_bubble(name, tr("Click to work on this picture."));
      }
    return 0;
  }
  case MUIM_DeleteShortHelp: return 0;
  case MUIM_Draw: {
    struct RastPort *rp = _rp(obj);
    LONG x;
    int i;
    char buf[200];
    DoSuperMethodA(cl, obj, msg);
    d = INST_DATA(cl, obj);
    if (!(((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))) return 0;
    theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_TABSTRIP));
    x = _mleft(obj);
    for (i = 0; i < ndocs; i++) {
      LONG w;
      tab_label(i, buf, sizeof buf);
      w = theme_textlen(rp, theme_font(), buf) + 34;
      if (w > 320) w = 320;
      if (x + 40 > _mright(obj)) { d->x0[i] = d->x1[i] = -1; continue; }
      if (x + w > _mright(obj)) w = _mright(obj) - x;
      d->x0[i] = x; d->x1[i] = x + w - 1;
      if (i == curdoc) theme_fill(rp, x, _mtop(obj) + 2, x + w - 1, _mbottom(obj), tpen(T_PANEL));
      SetAPen(rp, tpen(T_BORDER)); Move(rp, x + w - 1, _mtop(obj) + 3); Draw(rp, x + w - 1, _mbottom(obj) - 2);
      text_fit(rp, theme_font(), x + 10, _mtop(obj) + 5, buf, w - 34, tpen(i == curdoc ? T_TEXT : T_DIM));
      /* the close cross */
      {
        LONG cx = x + w - 14, cy = (_mtop(obj) + _mbottom(obj)) / 2 + 1;
        SetAPen(rp, tpen(T_DIM));
        Move(rp, cx - 3, cy - 3); Draw(rp, cx + 3, cy + 3); Move(rp, cx + 3, cy - 3); Draw(rp, cx - 3, cy + 3);
      }
      x += w;
    }
    return 0;
  }
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    int i;
    d = INST_DATA(cl, obj);
    if (!im || !d->shown || im->Code != SELECTDOWN || im->MouseY < _mtop(obj) || im->MouseY > _mbottom(obj)) return 0;
    for (i = 0; i < ndocs; i++)
      if (d->x0[i] >= 0 && im->MouseX >= d->x0[i] && im->MouseX <= d->x1[i]) {
        d->pick = i;
        if (im->MouseX >= d->x1[i] - 20) SetAttrs(obj, MUIA_DocTabs_Close, i, TAG_DONE);
        else SetAttrs(obj, MUIA_DocTabs_Pick, i, TAG_DONE);
        return MUI_EventHandlerRC_Eat;
      }
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *doctabs_class(void)
{
  if (!tabcls) tabcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct DTData), (APTR)tabdispatch);
  return tabcls;
}

/* ================================================================== the status bar */
struct SData { int shown; };
static ULONG stdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  switch (msg->MethodID) {
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 100; m->MinMaxInfo->DefWidth += 640; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += fh() + 6; m->MinMaxInfo->DefHeight += fh() + 6; m->MinMaxInfo->MaxHeight += fh() + 6;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    return TRUE;
  case MUIM_Cleanup: theme_cleanup(obj); break;
  case MUIM_Show: ((struct SData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct SData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_Status_Refresh:
    if (((struct SData *)INST_DATA(cl, obj))->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  case MUIM_Draw: {
    struct RastPort *rp = _rp(obj);
    DocView *v = cur_view();
    char buf[160];
    LONG x = _mleft(obj) + 8, y = _mtop(obj) + 3;
    DoSuperMethodA(cl, obj, msg);
    if (!(((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))) return 0;
    theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_STATUS));
    SetAPen(rp, tpen(T_BORDER)); Move(rp, _mleft(obj), _mtop(obj)); Draw(rp, _mright(obj), _mtop(obj));
    if (!v || !v->doc) { theme_text(rp, theme_font(), x, y, "Easel", tpen(T_DIM)); return 0; }
    sprintf(buf, "%ld%%", (long)(v->zoom * 100 + 0.5));
    theme_text(rp, theme_font(), x, y, buf, tpen(T_TEXT));
    x += 60;
    SetAPen(rp, tpen(T_BORDER)); Move(rp, x - 10, y); Draw(rp, x - 10, y + fh());
    sprintf(buf, "%ld x %ld px (%d dpi)   %d %s", (long)v->doc->w, (long)v->doc->h, v->doc->dpi ? v->doc->dpi : 72, v->doc->nlay, tr(v->doc->nlay == 1 ? "layer" : "layers"));
    theme_text(rp, theme_font(), x, y, buf, tpen(T_TEXT));
    x += theme_textlen(rp, theme_font(), buf) + 20;
    SetAPen(rp, tpen(T_BORDER)); Move(rp, x - 10, y); Draw(rp, x - 10, y + fh());
    text_fit(rp, theme_font(), x, y, canvas_info, _mright(obj) - x - 4, tpen(T_DIM));
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *status_class(void)
{
  if (!stcls) stcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct SData), (APTR)stdispatch);
  return stcls;
}

void panels_free(void)
{
  struct MUI_CustomClass **c[] = { &colcls, &laycls, &histcls, &headcls, &tabcls, &stcls };
  int i;
  for (i = 0; i < 6; i++) if (*c[i]) { MUI_DeleteCustomClass(*c[i]); *c[i] = NULL; }
}
