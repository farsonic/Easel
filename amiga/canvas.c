/*
 * canvas.c - the image being worked on, as PhotoCraft shows it: the layers composited over a
 * checkerboard (transparency) on a dark pasteboard, zoomed and scrolled, with the selection's
 * marching outline, and every tool's work with the mouse.
 *
 * How it draws: the composite of all the layers is kept for the whole image (comp, RGBA), brought
 * up to date only where something changed. From it the visible part is rendered into an RGB copy
 * of the canvas area (view), which goes to the window with WritePixelArray on a true-colour screen
 * or dithered to a cube of pens on one with a palette (widgets.c). Rubber bands (marquee, lasso,
 * lines, the crop box) and the brush's outline are drawn over it in COMPLEMENT, so drawing them
 * again takes them away; the selection's outline is drawn in black and white dashes that move.
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <exec/memory.h>
#include <devices/inputevent.h>
#include <graphics/gfxmacros.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <proto/keymap.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "../core/layers.h"
#include "brushwin.h"
#include "../core/guides.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "../core/text.h"
#include "../core/channels.h"
#include "../core/xform.h"
#include "../core/selx.h"
#include "../core/maglasso.h"
#include "../core/crop.h"

char canvas_info[96];
CanvasPoint canvas_pt;
int crop_pending;
int xform_pending;
char xform_info[80];
int ruler_on;
double ruler_xy[4];
char ruler_info[96];
static Doc *ruler_doc;                   /* the picture the Ruler's line was drawn on */

enum { DR_NONE, DR_STROKE, DR_MARQUEE, DR_LASSO, DR_MOVE, DR_PAN, DR_LINE, DR_SHAPE, DR_CROP, DR_CROPMOVE, DR_PICK,
       DR_XFORM, DR_RULER };
#define QSHIFT (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)
#define QALT (IEQUALIFIER_LALT | IEQUALIFIER_RALT)

struct Data {
  Object *obj, *vprop, *hprop;
  struct MUI_EventHandlerNode ehn;
  struct MUI_InputHandlerNode ihn;
  struct MUI_InputHandlerNode sihn; int sihn_on;   /* 0.5: the stroke's timer while it wants one */
  Box sbox;                              /* what the stroke has changed so far */
  int handlers, shown;
  /* the composite of the document's layers (comp, cw x ch RGBA), for doc */
  Doc *doc; u8 *comp; i32 cw, ch;
  /* without the memory for comp: one row composited at a time, as it is drawn */
  u8 *rowbuf; i32 rowcap, rowy; Doc *rowdoc;
  /* the canvas area's pixels (RGB, vw x vh), and each column's image x (-1 off the image) */
  UBYTE *view; LONG vw, vh; LONG *xmap; LONG xmapcap;
  /* the view: zoom, the image's top left within the area (ox, oy, may be negative when scrolled) */
  double z; LONG ox, oy;
  /* the mouse */
  int drag; Stroke *stroke; int changed;
  double ax, ay, bx, by;                 /* where the drag began and where it is (image) */
  LONG pmx, pmy;                         /* (pan: where the pointer was, window) */
  ULONG secs, micros;                    /* the last click, for double clicks */
  double lastx, lasty; int has_last;     /* the last stroke's end (shift-click draws a line from it) */
  double clone_x, clone_y; int clone_set, clone_aligned_set; i32 clone_dx, clone_dy;
  /* the polygonal lasso's points so far (image), and whether it is being made */
  double *poly; int npoly, cappoly, poly_on, lasso_closing;
  /* the crop box (image corners, x0 <= x1); which handle is held */
  double c0x, c0y, c1x, c1y; int crop_on, crop_handle;
  /* the overlay drawn in COMPLEMENT: on or off, and the brush outline's place */
  int ov_on; LONG cur_x, cur_y; int cur_in;
  int space;                             /* the space bar held: the hand for now */
  int modifiers;                         /* the qualifiers at the drag's start */
  /* the selection's outline: image segments, and the dash phase */
  i32 *edges; int nedges, phase, edges_ok;
  /* 0.7: Free Transform: the quad, the quad when the drag began, the handle held, its picture */
  int xf_on, xf_sel, xf_mode, xf_handle; XForm xf, xf0; Doc *xf_doc;
  UWORD qual;                            /* the qualifiers at the last pointer move */
  Box mbox;                              /* the marquee's box as its style makes it */
  /* the Magnetic Lasso, its live path (the lasso's own buffer, good until the next track) and its op */
  MagLasso *ml; double *mlpath; int nmlpath, ml_op;
  int ruler_end;                         /* the Ruler's end being dragged (0 the start, 1 the end) */
  int ov_ruler;                          /* the Ruler's line is in the overlay drawn */
};

static struct MUI_CustomClass *ccls;
static const double zooms[] = { 1.0 / 32, 1.0 / 24, 1.0 / 16, 1.0 / 12, 1.0 / 8, 1.0 / 6, 1.0 / 4, 1.0 / 3, 1.0 / 2, 2.0 / 3, 1, 1.5, 2, 3, 4, 5, 6, 8, 12, 16, 24, 32 };
#define NZOOMS (int)(sizeof zooms / sizeof zooms[0])

static int brushlike(int t) { return t == TL_BRUSH || t == TL_PENCIL || t == TL_ERASER || t == TL_CLONE || (t >= TL_BLUR && t <= TL_SPONGE) || (t >= TL_PATTERN && t <= TL_BGERASE); }
/* 0.3: painting a layer mask: its feathered copy made again so the composite shows it */
static void mask_painted(Doc *doc)
{
  Layer *l = doc && doc->active >= 0 && doc->active < doc->nlay ? doc->lay[doc->active] : NULL;
  if (l && l->edit_mask && l->mask) layer_mask_changed(l);
}
static int pt_of(int t)
{
  switch (t) {
  case TL_PENCIL: return PT_PENCIL; case TL_ERASER: return PT_ERASER; case TL_CLONE: return PT_CLONE;
  case TL_BLUR: return PT_BLUR; case TL_SHARPEN: return PT_SHARPEN; case TL_SMUDGE: return PT_SMUDGE;
  case TL_DODGE: return PT_DODGE; case TL_BURN: return PT_BURN; case TL_SPONGE: return PT_SPONGE;
  }
  return PT_BRUSH;
}

/* ------------------------------------------------------------------ the view's geometry */
/* the zoom and scroll for the area's size: the image centred when it is smaller than the area */
static void place(struct Data *d, Object *obj)
{
  DocView *v = cur_view();
  LONG W = _mwidth(obj), H = _mheight(obj), iw, ih;
  if (!v || !v->doc) return;
  if (!v->placed && W > 20 && H > 20) {
    /* first shown: the largest zoom step up to 100% that fits the image */
    int i;
    double fit = (double)(W - 8) / v->doc->w, fy = (double)(H - 8) / v->doc->h;
    if (fy < fit) fit = fy;
    v->zoom = zooms[0];
    for (i = 0; i < NZOOMS && zooms[i] <= 1.0; i++) if (zooms[i] <= fit) v->zoom = zooms[i];
    v->placed = 1;
  }
  d->z = v->zoom;
  iw = (LONG)ceil(v->doc->w * d->z); ih = (LONG)ceil(v->doc->h * d->z);
  if (iw <= W) { v->sx = 0; d->ox = (W - iw) / 2; }
  else { if (v->sx > iw - W) v->sx = iw - W; if (v->sx < 0) v->sx = 0; d->ox = -v->sx; }
  if (ih <= H) { v->sy = 0; d->oy = (H - ih) / 2; }
  else { if (v->sy > ih - H) v->sy = ih - H; if (v->sy < 0) v->sy = 0; d->oy = -v->sy; }
}
/* window pixel to image (fractional) and back */
static double to_ix(struct Data *d, Object *obj, LONG mx) { return (mx - _mleft(obj) - d->ox) / d->z; }
static double to_iy(struct Data *d, Object *obj, LONG my) { return (my - _mtop(obj) - d->oy) / d->z; }
static LONG to_sx(struct Data *d, Object *obj, double ix) { return _mleft(obj) + d->ox + (LONG)floor(ix * d->z + 0.5); }
static LONG to_sy(struct Data *d, Object *obj, double iy) { return _mtop(obj) + d->oy + (LONG)floor(iy * d->z + 0.5); }
static void update_props(struct Data *d, Object *obj)
{
  DocView *v = cur_view();
  if (!v || !v->doc) return;
  if (d->vprop) SetAttrs(d->vprop, MUIA_NoNotify, TRUE, MUIA_Prop_Entries, (LONG)ceil(v->doc->h * d->z), MUIA_Prop_Visible, _mheight(obj), MUIA_Prop_First, v->sy, TAG_DONE);
  if (d->hprop) SetAttrs(d->hprop, MUIA_NoNotify, TRUE, MUIA_Prop_Entries, (LONG)ceil(v->doc->w * d->z), MUIA_Prop_Visible, _mwidth(obj), MUIA_Prop_First, v->sx, TAG_DONE);
}

/* ------------------------------------------------------------------ the composite and the view */
static void comp_free(struct Data *d) { if (d->comp) FreeVec(d->comp); d->comp = NULL; d->cw = d->ch = 0; d->doc = NULL; d->rowdoc = NULL; }
/* the composite made for the current document if it is another or another size; 0 if no memory */
static int comp_ready(struct Data *d)
{
  Doc *doc = cur_doc();
  if (!doc) { comp_free(d); return 0; }
  if (d->comp && d->doc == doc && d->cw == doc->w && d->ch == doc->h) return 1;
  comp_free(d);
  if (!(d->comp = (u8 *)AllocVec((ULONG)doc->w * doc->h * 4, MEMF_ANY))) return 0;
  d->doc = doc; d->cw = doc->w; d->ch = doc->h;
  doc_composite(doc, doc_box(doc), d->comp, (long)doc->w * 4);
  return 1;
}
static int view_ready(struct Data *d, Object *obj)
{
  LONG W = _mwidth(obj), H = _mheight(obj);
  if (d->view && d->vw == W && d->vh == H) return 1;
  if (d->view) FreeVec(d->view);
  d->view = (UBYTE *)AllocVec((ULONG)(W * H * 3), MEMF_ANY);
  d->vw = d->view ? W : 0; d->vh = d->view ? H : 0;
  if (d->xmapcap < W) {
    if (d->xmap) FreeVec(d->xmap);
    d->xmap = (LONG *)AllocVec((ULONG)(W * sizeof(LONG)), MEMF_ANY);
    d->xmapcap = d->xmap ? W : 0;
  }
  return d->view && d->xmap;
}
/* the pasteboard round the image */
static ULONG board(void) { return theme_is_dark() ? 0x282828 : 0xA8A8A8; }
/* the canvas area's columns x0..x1-1 and rows y0..y1-1 rendered into view */
/* image row iy (columns ix0..ix1-1) composited into rowbuf, when there is no comp: the row's
   pixels, or NULL if even a row's buffer couldn't be had */
static const u8 *comp_row(struct Data *d, Doc *doc, LONG iy, LONG ix0, LONG ix1)
{
  if (d->rowcap < doc->w) {
    if (d->rowbuf) FreeVec(d->rowbuf);
    d->rowbuf = (u8 *)AllocVec((ULONG)doc->w * 4, MEMF_ANY);
    d->rowcap = d->rowbuf ? doc->w : 0;
    d->rowdoc = NULL;
  }
  if (!d->rowbuf) return NULL;
  if (d->rowdoc != doc || d->rowy != iy) {
    if (ix0 < 0) ix0 = 0;
    if (ix1 > doc->w) ix1 = doc->w;
    if (ix1 <= ix0) return NULL;
    doc_composite(doc, box_make((i32)ix0, (i32)iy, (i32)ix1, (i32)iy + 1), d->rowbuf + ix0 * 4, (long)doc->w * 4);
    d->rowdoc = doc; d->rowy = (i32)iy;
  }
  return d->rowbuf;
}
/* 0.4: the grid, the guides and the rulers drawn into the view's pixels in x0..x1, y0..y1 */
#define RULER 14
static void vpix(struct Data *d, LONG x, LONG y, ULONG c, int mix)
{
  UBYTE *o = d->view + (y * d->vw + x) * 3;
  if (mix) { o[0] = (UBYTE)((o[0] + (c >> 16 & 255)) / 2); o[1] = (UBYTE)((o[1] + (c >> 8 & 255)) / 2); o[2] = (UBYTE)((o[2] + (c & 255)) / 2); }
  else { o[0] = (UBYTE)(c >> 16); o[1] = (UBYTE)(c >> 8); o[2] = (UBYTE)c; }
}
static void guides_render(struct Data *d, Doc *doc, LONG x0, LONG y0, LONG x1, LONG y1)
{
  GuideSet *g = &doc->guides;
  LONG ix0 = d->ox > x0 ? d->ox : x0, iy0 = d->oy > y0 ? d->oy : y0;                   /* the image's part of the box */
  LONG ix1 = d->ox + (LONG)(doc->w * d->z), iy1 = d->oy + (LONG)(doc->h * d->z), x, y;
  int i, k;
  if (ix1 > x1) ix1 = x1;
  if (iy1 > y1) iy1 = y1;
  if (g->show_grid && ix0 < ix1 && iy0 < iy1) {
    for (k = 0; k < 2; k++) {
      double step, p = grid_first(doc, k == 0, k == 0 ? (ix0 - d->ox) / d->z : (iy0 - d->oy) / d->z, 1, &step);
      if (step * d->z < 4) continue;                     /* (too close together to see) */
      for (; ; p += step) {
        LONG s = (LONG)floor(p * d->z) + (k == 0 ? d->ox : d->oy);
        if (s >= (k == 0 ? ix1 : iy1)) break;
        if (k == 0) { if (s >= ix0) for (y = iy0; y < iy1; y++) vpix(d, s, y, 0x808080, 1); }
        else if (s >= iy0) for (x = ix0; x < ix1; x++) vpix(d, x, s, 0x808080, 1);
      }
    }
  }
  if (g->show_guides) {
    for (i = 0; i < g->nv; i++) { LONG s = (LONG)floor(g->v[i] * d->z) + d->ox; if (s >= x0 && s < x1) for (y = y0; y < y1; y++) vpix(d, s, y, 0x00C0FF, 0); }
    for (i = 0; i < g->nh; i++) { LONG s = (LONG)floor(g->h[i] * d->z) + d->oy; if (s >= y0 && s < y1) for (x = x0; x < x1; x++) vpix(d, x, s, 0x00C0FF, 0); }
  }
  if (g->show_rulers) {
    int minor, major = ruler_step(d->z, 50, &minor);
    double mstep = (double)major / (minor > 0 ? minor : 1);
    for (k = 0; k < 2; k++) {
      LONG a0 = k == 0 ? x0 : y0, a1 = k == 0 ? x1 : y1, b0 = k == 0 ? y0 : x0, b1 = k == 0 ? y1 : x1, off = k == 0 ? d->ox : d->oy, s, b;
      double p;
      if (b0 >= RULER) continue;
      for (s = a0; s < a1; s++) for (b = b0; b < b1 && b < RULER; b++) { if (k == 0) vpix(d, s, b, b == RULER - 1 ? 0x606060 : 0xE8E8E8, 0); else vpix(d, b, s, b == RULER - 1 ? 0x606060 : 0xE8E8E8, 0); }
      for (p = floor((a0 - off) / d->z / mstep) * mstep; ; p += mstep) {
        LONG len;
        s = (LONG)floor(p * d->z) + off;
        if (s >= a1) break;
        if (s < a0) continue;
        len = fmod(fabs(p) + 0.001, major) < 0.01 ? RULER : RULER / 3;
        for (b = RULER - len; b < RULER; b++) if (b >= b0 && b < b1) { if (k == 0) vpix(d, s, b, 0x404040, 0); else vpix(d, b, s, 0x404040, 0); }
      }
    }
  }
}
/* the rulers' numbers, drawn over the view after it is put on the screen */
static void ruler_labels(struct Data *d, Object *obj)
{
  Doc *doc = cur_doc();
  struct RastPort *rp = _rp(obj);
  int minor, major;
  double p;
  char buf[16];
  APTR clip;
  if (!doc || !doc->guides.show_rulers || !d->shown) return;
  major = ruler_step(d->z, 50, &minor);
  clip = (APTR)MUI_AddClipping(muiRenderInfo(obj), _mleft(obj), _mtop(obj), _mwidth(obj), _mheight(obj));
  SetFont(rp, theme_small());
  SetDrMd(rp, JAM1);
  SetAPen(rp, tpen(T_DIM));
  for (p = floor(-d->ox / d->z / major) * major; (LONG)(p * d->z) + d->ox < d->vw; p += major) {
    LONG s = (LONG)floor(p * d->z) + d->ox;
    if (s < RULER) continue;
    sprintf(buf, "%ld", (long)p);
    Move(rp, _mleft(obj) + s + 2, _mtop(obj) + rp->TxBaseline + 1); Text(rp, (STRPTR)buf, strlen(buf));
  }
  for (p = floor(-d->oy / d->z / major) * major; (LONG)(p * d->z) + d->oy < d->vh; p += major) {
    LONG s = (LONG)floor(p * d->z) + d->oy;
    int c, n;
    if (s < RULER) continue;
    sprintf(buf, "%ld", (long)p);
    /* (the digits one under another, as the ruler is narrow) */
    for (c = 0, n = (int)strlen(buf); c < n; c++) { Move(rp, _mleft(obj) + 3, _mtop(obj) + s + 2 + c * rp->TxHeight + rp->TxBaseline); Text(rp, (STRPTR)buf + c, 1); }
  }
  MUI_RemoveClipping(muiRenderInfo(obj), (struct Region *)clip);
}
static void render(struct Data *d, LONG x0, LONG y0, LONG x1, LONG y1)
{
  Doc *doc = d->comp ? d->doc : cur_doc();
  LONG cx0 = -1, cx1 = -1;
  LONG x, y;
  ULONG bc = board();
  UBYTE br = (UBYTE)(bc >> 16), bg = (UBYTE)(bc >> 8), bb = (UBYTE)bc;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > d->vw) x1 = d->vw;
  if (y1 > d->vh) y1 = d->vh;
  if (x1 <= x0 || y1 <= y0) return;
  for (x = x0; x < x1; x++) {
    double ix = floor((x - d->ox) / d->z);
    d->xmap[x] = doc && ix >= 0 && ix < doc->w ? (LONG)ix : -1;
    if (d->xmap[x] >= 0) { if (cx0 < 0 || d->xmap[x] < cx0) cx0 = d->xmap[x]; if (d->xmap[x] + 1 > cx1) cx1 = d->xmap[x] + 1; }
  }
  d->rowdoc = NULL;                    /* (the row cache is for this pass's columns only) */
  for (y = y0; y < y1; y++) {
    UBYTE *o = d->view + (y * d->vw + x0) * 3;
    double iyf = floor((y - d->oy) / d->z);
    LONG iy = doc && iyf >= 0 && iyf < doc->h ? (LONG)iyf : -1;
    const u8 *row = iy < 0 ? NULL : d->comp ? d->comp + (long)iy * doc->w * 4 : comp_row(d, doc, iy, cx0, cx1);
    for (x = x0; x < x1; x++, o += 3) {
      LONG ix = d->xmap[x];
      if (!row || ix < 0) { o[0] = br; o[1] = bg; o[2] = bb; continue; }
      {
        const u8 *p = row + ix * 4;
        int a = p[3];
        if (a == 255) { o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; }
        else {
          /* over the checkerboard: 8 pixel squares of white and light grey, fixed to the image */
          int c = ((((x - d->ox) >> 3) + ((y - d->oy) >> 3)) & 1) ? 0xCC : 0xFF;
          o[0] = (UBYTE)((p[0] * a + c * (255 - a) + 127) / 255);
          o[1] = (UBYTE)((p[1] * a + c * (255 - a) + 127) / 255);
          o[2] = (UBYTE)((p[2] * a + c * (255 - a) + 127) / 255);
        }
        /* 0.7: in Quick Mask mode, what isn't selected under half-strength red */
        if (doc->qmask) {
          int t = (255 - doc->qmask[(long)iy * doc->w + ix]) / 2;
          if (t) { o[0] = (UBYTE)(o[0] + ((255 - o[0]) * t + 127) / 255); o[1] = (UBYTE)((o[1] * (255 - t) + 127) / 255); o[2] = (UBYTE)((o[2] * (255 - t) + 127) / 255); }
        }
      }
    }
  }
  if (doc) guides_render(d, doc, x0, y0, x1, y1);
}
static void blit(struct Data *d, Object *obj, LONG x0, LONG y0, LONG x1, LONG y1)
{
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > d->vw) x1 = d->vw;
  if (y1 > d->vh) y1 = d->vh;
  if (x1 <= x0 || y1 <= y0) return;
  draw_rgb(_rp(obj), _screen(obj), d->view + (y0 * d->vw + x0) * 3, d->vw * 3, _mleft(obj) + x0, _mtop(obj) + y0, x1 - x0, y1 - y0);
}

/* ------------------------------------------------------------------ the selection's outline */
static void edges_make(struct Data *d)
{
  Doc *doc = cur_doc();
  if (d->edges) { free(d->edges); d->edges = NULL; }
  d->nedges = 0;
  d->edges_ok = 1;
  if (!doc || !doc->sel) return;
  d->nedges = sel_edges(doc, doc_box(doc), &d->edges);
  if (d->nedges < 0) d->nedges = 0;
}
/* the outline drawn where it crosses the area's rectangle x0..x1, y0..y1 (window pixels) */
static void ants(struct Data *d, Object *obj, LONG x0, LONG y0, LONG x1, LONG y1)
{
  struct RastPort *rp = _rp(obj);
  int i;
  UWORD pat;
  if (!d->nedges || !d->edges) return;
  pat = (UWORD)((0xF0F0F0F0UL >> (d->phase & 7)) & 0xFFFF);
  SetDrMd(rp, JAM2);
  SetAPen(rp, theme_is_dark() ? tpen(T_WHITE) : tpen(T_TEXT));
  SetBPen(rp, theme_is_dark() ? tpen(T_TEXT) : tpen(T_WHITE));
  /* (T_WHITE is the inputs' colour: near black in the dark theme; T_TEXT near white) */
  SetDrPt(rp, pat);
  for (i = 0; i < d->nedges; i++) {
    const i32 *e = d->edges + i * 4;
    LONG a = to_sx(d, obj, e[0]), b = to_sy(d, obj, e[1]), c = to_sx(d, obj, e[2]), f = to_sy(d, obj, e[3]);
    if (b == f) {
      if (c > a) c--; else if (a > c) a--;
      if (b > _mbottom(obj)) b = f = _mbottom(obj);
      if (b < y0 || b > y1) continue;
      if (a > c) { LONG t = a; a = c; c = t; }
      if (a < x0) a = x0;
      if (c > x1) c = x1;
      if (a > c) continue;
      Move(rp, a, b); Draw(rp, c, b);
    } else {
      if (f > b) f--; else if (b > f) b--;
      if (a > _mright(obj)) a = c = _mright(obj);
      if (a < x0 || a > x1) continue;
      if (b > f) { LONG t = b; b = f; f = t; }
      if (b < y0) b = y0;
      if (f > y1) f = y1;
      if (b > f) continue;
      Move(rp, a, b); Draw(rp, a, f);
    }
  }
  SetDrPt(rp, 0xFFFF);
  SetDrMd(rp, JAM1);
}

/* ------------------------------------------------------------------ the overlay (COMPLEMENT) */
static void xline(struct RastPort *rp, LONG a, LONG b, LONG c, LONG e) { Move(rp, a, b); Draw(rp, c, e); }
/* an outline whose corners are drawn once (so drawing it again takes it all away) */
static void xrect(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1)
{
  LONG t;
  if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
  if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
  xline(rp, x0, y0, x1, y0);
  if (y1 > y0) {
    xline(rp, x0, y1, x1, y1);
    if (y1 > y0 + 1) { xline(rp, x0, y0 + 1, x0, y1 - 1); if (x1 > x0) xline(rp, x1, y0 + 1, x1, y1 - 1); }
  }
}
static void xellipse(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1)
{
  LONG t;
  if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
  if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
  if (x1 - x0 < 2 || y1 - y0 < 2) { xrect(rp, x0, y0, x1, y1); return; }
  DrawEllipse(rp, (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2, (y1 - y0) / 2);
}
static LONG brush_r(struct Data *d) { return (LONG)(opt.size[tool] * d->z / 2); }
/* 0.7: a path of image points (the Magnetic Lasso's) drawn so that each pixel is complemented once:
   every segment from the pixel after its start; *lx, *ly the last pixel drawn (-32000: none yet) */
static void xpath(struct Data *d, Object *obj, struct RastPort *rp, const double *p, int n, LONG *lx, LONG *ly)
{
  int i;
  for (i = 0; i < n; i++) {
    LONG x = to_sx(d, obj, p[2 * i]), y = to_sy(d, obj, p[2 * i + 1]), dx, dy, m;
    if (*lx == -32000) { Move(rp, x, y); Draw(rp, x, y); *lx = x; *ly = y; continue; }
    dx = x - *lx; dy = y - *ly;
    if (!dx && !dy) continue;
    m = dx < 0 ? -dx : dx;
    if ((dy < 0 ? -dy : dy) > m) m = dy < 0 ? -dy : dy;
    Move(rp, *lx + (LONG)floor((double)dx / m + 0.5), *ly + (LONG)floor((double)dy / m + 0.5));
    Draw(rp, x, y);
    *lx = x; *ly = y;
  }
}
/* 0.7: Free Transform's handles in window pixels: 0-3 the corners (XC_), 4-7 the edges' middles
   (XE_), 8 the centre */
static void xf_handles(struct Data *d, Object *obj, LONG *hx, LONG *hy)
{
  const double *q = d->xf.q;
  double h[9], cx = 0, cy = 0;
  int k;
  for (k = 0; k < 4; k++) {
    int n = (k + 1) % 4;
    hx[k] = to_sx(d, obj, q[2 * k]); hy[k] = to_sy(d, obj, q[2 * k + 1]);
    hx[4 + k] = to_sx(d, obj, (q[2 * k] + q[2 * n]) / 2); hy[4 + k] = to_sy(d, obj, (q[2 * k + 1] + q[2 * n + 1]) / 2);
    cx += q[2 * k] / 4; cy += q[2 * k + 1] / 4;
  }
  if (xform_matrix(&d->xf, h)) xform_point(h, (d->xf.src.x0 + d->xf.src.x1) / 2.0, (d->xf.src.y0 + d->xf.src.y1) / 2.0, &cx, &cy);
  hx[8] = to_sx(d, obj, cx); hy[8] = to_sy(d, obj, cy);
}
/* everything in the overlay drawn (or, drawn again, taken away) */
static void ov_draw(struct Data *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  APTR clip;
  int t = d->space ? TL_HAND : tool, i;
  if (!d->shown) return;
  clip = (APTR)MUI_AddClipping(muiRenderInfo(obj), _mleft(obj), _mtop(obj), _mwidth(obj), _mheight(obj));
  SetDrMd(rp, COMPLEMENT);
  SetDrPt(rp, 0xFFFF);
  SetAPen(rp, 1);
  /* the brush's outline (or a cross when it is too small to see) */
  if (d->cur_in && brushlike(t) && d->drag != DR_LINE) {
    LONG r = brush_r(d);
    if (r >= 3) DrawEllipse(rp, d->cur_x, d->cur_y, r, r);
    else { xline(rp, d->cur_x - 4, d->cur_y, d->cur_x + 4, d->cur_y); xline(rp, d->cur_x, d->cur_y - 4, d->cur_x, d->cur_y - 1); xline(rp, d->cur_x, d->cur_y + 1, d->cur_x, d->cur_y + 4); }
  }
  switch (d->drag) {
  case DR_MARQUEE:
    if (tool == TL_EMARQUEE) xellipse(rp, to_sx(d, obj, d->mbox.x0), to_sy(d, obj, d->mbox.y0), to_sx(d, obj, d->mbox.x1), to_sy(d, obj, d->mbox.y1));
    else xrect(rp, to_sx(d, obj, d->mbox.x0), to_sy(d, obj, d->mbox.y0), to_sx(d, obj, d->mbox.x1), to_sy(d, obj, d->mbox.y1));
    break;
  case DR_SHAPE:
    if (tool == TL_ELLIPSE) xellipse(rp, to_sx(d, obj, d->ax), to_sy(d, obj, d->ay), to_sx(d, obj, d->bx), to_sy(d, obj, d->by));
    else xrect(rp, to_sx(d, obj, d->ax), to_sy(d, obj, d->ay), to_sx(d, obj, d->bx), to_sy(d, obj, d->by));
    break;
  case DR_LINE:
    xline(rp, to_sx(d, obj, d->ax), to_sy(d, obj, d->ay), to_sx(d, obj, d->bx), to_sy(d, obj, d->by));
    break;
  case DR_MOVE: {
    /* what is moving: the selection's bounds or the whole layer, where it would go */
    Doc *doc = cur_doc();
    Box b;
    LONG dx = (LONG)floor(d->bx - d->ax + 0.5), dy = (LONG)floor(d->by - d->ay + 0.5);
    if (!doc) break;
    if (!sel_bounds(doc, &b)) b = doc_box(doc);
    xrect(rp, to_sx(d, obj, b.x0 + dx), to_sy(d, obj, b.y0 + dy), to_sx(d, obj, b.x1 + dx) - 1, to_sy(d, obj, b.y1 + dy) - 1);
    break; }
  }
  if (d->drag == DR_LASSO || d->poly_on) {
    for (i = 1; i < d->npoly; i++)
      xline(rp, to_sx(d, obj, d->poly[i * 2 - 2]), to_sy(d, obj, d->poly[i * 2 - 1]), to_sx(d, obj, d->poly[i * 2]), to_sy(d, obj, d->poly[i * 2 + 1]));
    if (d->poly_on && d->npoly && d->cur_in)
      xline(rp, to_sx(d, obj, d->poly[d->npoly * 2 - 2]), to_sy(d, obj, d->poly[d->npoly * 2 - 1]), d->cur_x, d->cur_y);
  }
  if (d->xf_on) {
    /* the quad, its handles and its centre */
    LONG hx[9], hy[9];
    int k;
    xf_handles(d, obj, hx, hy);
    for (k = 0; k < 4; k++) xline(rp, hx[k], hy[k], hx[(k + 1) % 4], hy[(k + 1) % 4]);
    for (k = 0; k < 8; k++) RectFill(rp, hx[k] - 3, hy[k] - 3, hx[k] + 3, hy[k] + 3);
    xline(rp, hx[8] - 5, hy[8], hx[8] + 5, hy[8]); xline(rp, hx[8], hy[8] - 5, hx[8], hy[8] - 1); xline(rp, hx[8], hy[8] + 1, hx[8], hy[8] + 5);
  }
  if (d->ml) {
    /* the Magnetic Lasso: the path so far, then the live part to the pointer */
    double *p;
    int n;
    LONG lx = -32000, ly = 0;
    maglasso_points(d->ml, &p, &n);
    xpath(d, obj, rp, p, n, &lx, &ly);
    if (d->mlpath && d->nmlpath) xpath(d, obj, rp, d->mlpath, d->nmlpath, &lx, &ly);
  }
  if (d->ov_ruler) {
    /* the Ruler's line, with a small cross at each end */
    LONG a = to_sx(d, obj, ruler_xy[0]), b = to_sy(d, obj, ruler_xy[1]), c = to_sx(d, obj, ruler_xy[2]), e = to_sy(d, obj, ruler_xy[3]);
    xline(rp, a, b, c, e);
    xline(rp, a - 4, b - 4, a - 1, b - 1); xline(rp, a + 1, b + 1, a + 4, b + 4);
    xline(rp, c - 4, e + 4, c - 1, e + 1); xline(rp, c + 1, e - 1, c + 4, e - 4);
  }
  if (d->crop_on) {
    LONG x0 = to_sx(d, obj, d->c0x), y0 = to_sy(d, obj, d->c0y), x1 = to_sx(d, obj, d->c1x) - 1, y1 = to_sy(d, obj, d->c1y) - 1, k;
    xrect(rp, x0, y0, x1, y1);
    /* the rule of thirds, and the handles at the corners and sides */
    if (x1 - x0 > 30 && y1 - y0 > 30) {
      SetDrPt(rp, 0xCCCC);
      for (k = 1; k < 3; k++) { xline(rp, x0 + (x1 - x0) * k / 3, y0 + 1, x0 + (x1 - x0) * k / 3, y1 - 1); xline(rp, x0 + 1, y0 + (y1 - y0) * k / 3, x1 - 1, y0 + (y1 - y0) * k / 3); }
      SetDrPt(rp, 0xFFFF);
    }
    for (k = 0; k < 8; k++) {
      LONG hx = k % 3 == 0 ? x0 : k % 3 == 1 ? (x0 + x1) / 2 : x1, hy = k < 3 ? y0 : k < 5 ? (y0 + y1) / 2 : y1;
      if (k == 3) hx = x0; else if (k == 4) hx = x1;
      if (k >= 5) hx = k == 5 ? x0 : k == 6 ? (x0 + x1) / 2 : x1;
      RectFill(rp, hx - 3, hy - 3, hx + 3, hy + 3);
    }
  }
  SetDrMd(rp, JAM1);
  MUI_RemoveClipping(muiRenderInfo(obj), (struct Region *)clip);
}
static void ov_hide(struct Data *d, Object *obj) { if (d->ov_on) { ov_draw(d, obj); d->ov_on = 0; } }
static void ov_show(struct Data *d, Object *obj)
{
  if (d->ov_on) return;
  d->ov_ruler = ruler_on && tool == TL_RULER && ruler_doc == cur_doc();   /* (taken away later as it was put up) */
  ov_draw(d, obj);
  d->ov_on = 1;
}

/* ------------------------------------------------------------------ redrawing */
/* everything again: the view placed, rendered and drawn, the outline and overlay over it */
static void draw_all(struct Data *d, Object *obj)
{
  if (!d->shown) return;
  if (!view_ready(d, obj)) return;
  comp_ready(d);
  place(d, obj);
  if (!d->edges_ok) edges_make(d);
  render(d, 0, 0, d->vw, d->vh);
  d->ov_on = 0;
  blit(d, obj, 0, 0, d->vw, d->vh);
  ruler_labels(d, obj);
  ants(d, obj, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj));
  ov_show(d, obj);
  update_props(d, obj);
}
/* the image's box b changed: the composite remade there and that part of the view drawn again */
static void update_box(struct Data *d, Object *obj, Box b)
{
  Doc *doc = cur_doc();
  LONG x0, y0, x1, y1;
  if (!doc || !d->shown || !d->view) return;
  b = box_clip(b, doc_box(doc));
  if (box_empty(b)) return;
  /* (without comp, render composites the rows itself) */
  if (comp_ready(d)) doc_composite(doc, b, d->comp + ((long)b.y0 * doc->w + b.x0) * 4, (long)doc->w * 4);
  x0 = (LONG)floor(b.x0 * d->z) + d->ox - 1; y0 = (LONG)floor(b.y0 * d->z) + d->oy - 1;
  x1 = (LONG)ceil(b.x1 * d->z) + d->ox + 1; y1 = (LONG)ceil(b.y1 * d->z) + d->oy + 1;
  render(d, x0, y0, x1, y1);
  ov_hide(d, obj);
  blit(d, obj, x0, y0, x1, y1);
  if (x0 < RULER || y0 < RULER) ruler_labels(d, obj);
  ants(d, obj, _mleft(obj) + x0 - 1, _mtop(obj) + y0 - 1, _mleft(obj) + x1 + 1, _mtop(obj) + y1 + 1);
  ov_show(d, obj);
}
/* the selection changed: its outline made again, and the view drawn without the old one */
static void selection_changed(struct Data *d, Object *obj)
{
  edges_make(d);
  if (!d->shown || !d->view) return;
  ov_hide(d, obj);
  blit(d, obj, 0, 0, d->vw, d->vh);
  ants(d, obj, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj));
  ov_show(d, obj);
}
static void zoom_to(struct Data *d, Object *obj, double z, LONG mx, LONG my)
{
  DocView *v = cur_view();
  double ix, iy;
  if (!v) return;
  if (z < zooms[0]) z = zooms[0];
  if (z > zooms[NZOOMS - 1]) z = zooms[NZOOMS - 1];
  if (mx < 0) { mx = _mleft(obj) + _mwidth(obj) / 2; my = _mtop(obj) + _mheight(obj) / 2; }
  ix = to_ix(d, obj, mx); iy = to_iy(d, obj, my);
  v->zoom = z;
  v->sx = (LONG)(ix * z) - (mx - _mleft(obj));
  v->sy = (LONG)(iy * z) - (my - _mtop(obj));
  draw_all(d, obj);
  set(obj, MUIA_Canvas_Zoomed, TRUE);
}
static double zoom_step(double z, int dir)
{
  int i;
  if (dir > 0) { for (i = 0; i < NZOOMS; i++) if (zooms[i] > z * 1.001) return zooms[i]; return zooms[NZOOMS - 1]; }
  for (i = NZOOMS - 1; i >= 0; i--) if (zooms[i] < z * 0.999) return zooms[i];
  return zooms[0];
}

/* ------------------------------------------------------------------ the tools */
static void changed(struct Data *d, Object *obj) { d->changed = 1; (void)obj; }
static void tell_changed(struct Data *d, Object *obj)
{
  if (!d->changed) return;
  d->changed = 0;
  set(obj, MUIA_Canvas_Changed, TRUE);
}
static int sel_op_of(int qual)
{
  int shift = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0, alt = (qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) != 0;
  if (shift && alt) return SEL_INTERSECT;
  if (shift) return SEL_ADD;
  if (alt) return SEL_SUBTRACT;
  return opt.sel_op;
}
static void poly_add(struct Data *d, double x, double y)
{
  if (d->npoly >= d->cappoly) {
    int nc = d->cappoly ? d->cappoly * 2 : 64;
    double *np = (double *)realloc(d->poly, (size_t)nc * 2 * sizeof(double));
    if (!np) return;
    d->poly = np; d->cappoly = nc;
  }
  d->poly[d->npoly * 2] = x; d->poly[d->npoly * 2 + 1] = y;
  d->npoly++;
}
static void poly_finish(struct Data *d, Object *obj, int apply)
{
  Doc *doc = cur_doc();
  ov_hide(d, obj);
  if (apply && doc && d->npoly >= 3) { sel_polygon_ex(doc, d->poly, d->npoly, sel_op_of(d->modifiers), opt.feather, opt.sel_antialias); changed(d, obj); }
  d->npoly = 0; d->poly_on = 0;
  if (d->drag == DR_LASSO) d->drag = DR_NONE;
  selection_changed(d, obj);
  tell_changed(d, obj);
}
/* the pick: the colour under x, y as the foreground (or background) */
static void pick(struct Data *d, Object *obj, double x, double y, int bg)
{
  Doc *doc = cur_doc();
  u32 c;
  if (!doc || x < 0 || y < 0 || x >= doc->w || y >= doc->h) return;
  c = doc_pick(doc, (i32)x, (i32)y, opt.all_layers, opt.sample);
  if (c == 0xFFFFFFFFUL) return;
  if (bg) doc->bg = c; else doc->fg = c;
  set(obj, MUIA_Canvas_Picked, TRUE);
  (void)d;
}
static void crop_norm(struct Data *d)
{
  Doc *doc = cur_doc();
  double t;
  if (d->c0x > d->c1x) { t = d->c0x; d->c0x = d->c1x; d->c1x = t; }
  if (d->c0y > d->c1y) { t = d->c0y; d->c0y = d->c1y; d->c1y = t; }
  d->c0x = floor(d->c0x + 0.5); d->c0y = floor(d->c0y + 0.5); d->c1x = floor(d->c1x + 0.5); d->c1y = floor(d->c1y + 0.5);
  if (doc) {
    if (d->c0x < 0) d->c0x = 0;
    if (d->c0y < 0) d->c0y = 0;
    if (d->c1x > doc->w) d->c1x = doc->w;
    if (d->c1y > doc->h) d->c1y = doc->h;
  }
}
static void crop_apply(struct Data *d, Object *obj, int apply)
{
  Doc *doc = cur_doc();
  ov_hide(d, obj);
  d->crop_on = 0; crop_pending = 0;
  if (apply && doc && d->c1x - d->c0x >= 1 && d->c1y - d->c0y >= 1) {
    image_crop_preset(doc, box_make((i32)d->c0x, (i32)d->c0y, (i32)d->c1x, (i32)d->c1y), &crop_presets[opt.crop_preset], opt.crop_swap, RS_BICUBIC);
    changed(d, obj);
    d->edges_ok = 0;
    draw_all(d, obj);
    tell_changed(d, obj);
    return;
  }
  ov_show(d, obj);
}
/* which crop handle is at the window pixel mx, my (0-7: corners and sides, 8 inside, -1 none) */
static int crop_hit(struct Data *d, Object *obj, LONG mx, LONG my)
{
  LONG x0 = to_sx(d, obj, d->c0x), y0 = to_sy(d, obj, d->c0y), x1 = to_sx(d, obj, d->c1x) - 1, y1 = to_sy(d, obj, d->c1y) - 1;
  int hx = mx <= x0 + 5 && mx >= x0 - 5 ? 0 : mx >= x1 - 5 && mx <= x1 + 5 ? 2 : mx > x0 && mx < x1 ? 1 : -1;
  int hy = my <= y0 + 5 && my >= y0 - 5 ? 0 : my >= y1 - 5 && my <= y1 + 5 ? 2 : my > y0 && my < y1 ? 1 : -1;
  if (hx < 0 || hy < 0) return -1;
  if (hx == 1 && hy == 1) return 8;
  return hy * 3 + hx;            /* 0 1 2 / 3 _ 5 / 6 7 8: (4 is inside, reported as 8) */
}
/* ------------------------------------------------------------------ 0.7: Free Transform */
/* v with one decimal, without the C library's floating point printf */
static void fmt1(char *out, double v)
{
  long t = (long)floor(fabs(v) * 10 + 0.5);
  sprintf(out, "%s%ld.%ld", v < 0 && t ? "-" : "", t / 10, t % 10);
}
/* the options bar's numbers: the width and height against the source box's, and the top edge's angle */
static void xf_info(struct Data *d)
{
  const double *q = d->xf.q;
  double sw = d->xf.src.x1 - d->xf.src.x0, sh = d->xf.src.y1 - d->xf.src.y0;
  char w[16], h[16], a[16];
  fmt1(w, sw > 0 ? sqrt((q[2] - q[0]) * (q[2] - q[0]) + (q[3] - q[1]) * (q[3] - q[1])) * 100 / sw : 0);
  fmt1(h, sh > 0 ? sqrt((q[6] - q[0]) * (q[6] - q[0]) + (q[7] - q[1]) * (q[7] - q[1])) * 100 / sh : 0);
  fmt1(a, atan2(q[3] - q[1], q[2] - q[0]) * 180 / M_PI);
  sprintf(xform_info, "W %s%%   H %s%%   %s\260", w, h, a);
}
/* the handles put up round the selection (or the layer's pixels): mode is the handles' XM_, sel
   whether only the selection's outline is to change */
static void xf_start(struct Data *d, Object *obj, int mode, int sel)
{
  Doc *doc = cur_doc();
  Box b;
  if (!doc) return;
  if (!sel) {
    Layer *l = doc->lay[doc->active];
    if (!l->px || l->kind != LK_PIXEL) { ask("OK", "%s", tr("This layer has no pixels to transform: rasterize it first.")); return; }
    if (l->locked || l->lock_all || l->lock_pos) { ask("OK", "%s", tr("The layer is locked.")); return; }
  }
  b = sel ? (sel_bounds(doc, &b) ? b : box_make(0, 0, 0, 0)) : xform_source_box(doc, doc->active);
  if (box_empty(b)) { ask("OK", "%s", tr(sel ? "Nothing is selected." : "There is nothing to transform: the layer is empty.")); return; }
  ov_hide(d, obj);
  if (d->crop_on) { d->crop_on = 0; crop_pending = 0; }
  d->poly_on = 0; d->npoly = 0;
  xform_init(&d->xf, b);
  d->xf_on = 1; d->xf_sel = sel; d->xf_mode = mode; d->xf_doc = doc; d->xf_handle = -1;
  xform_pending = 1;
  xf_info(d);
  ov_show(d, obj);
  DoMethod(optbar, MUIM_Opts_Refresh);
}
/* which handle is at the window pixel mx, my: 0-3 a corner, 4-7 an edge, 8 inside, 9 outside (turns) */
static int xf_hit(struct Data *d, Object *obj, LONG mx, LONG my)
{
  LONG hx[9], hy[9];
  int k, pos = 0, neg = 0;
  xf_handles(d, obj, hx, hy);
  for (k = 0; k < 8; k++) if (mx >= hx[k] - 5 && mx <= hx[k] + 5 && my >= hy[k] - 5 && my <= hy[k] + 5) return k;
  for (k = 0; k < 4; k++) {
    double cr = (double)(hx[(k + 1) % 4] - hx[k]) * (my - hy[k]) - (double)(hy[(k + 1) % 4] - hy[k]) * (mx - hx[k]);
    if (cr > 0) pos++; else if (cr < 0) neg++;
  }
  return !pos || !neg ? 8 : 9;
}
/* scaling by handle h (a corner or an edge), worked out in the source box's own frame so a turned
   or distorted quad scales along its own sides: the handle follows the pointer, the opposite side
   (or with Alt the centre) stays; Shift keeps the proportions */
static void xf_scale(struct Data *d, int h, double dx, double dy, int shift, int alt)
{
  static const signed char cx[8] = { 0, 2, 2, 0, 1, 2, 1, 0 }, cy[8] = { 0, 0, 2, 2, 0, 1, 2, 1 };   /* 0 left/top, 1 middle, 2 right/bottom */
  Box s = d->xf0.src;
  double hm[9], inv[9], hx, hy, fx, fy, tx, ty, px, py, sx = 1, sy = 1;
  int c;
  if (!xform_matrix(&d->xf0, hm) || !hom_invert(hm, inv)) return;
  hx = s.x0 + (s.x1 - s.x0) * cx[h] / 2.0; hy = s.y0 + (s.y1 - s.y0) * cy[h] / 2.0;
  fx = alt ? (s.x0 + s.x1) / 2.0 : s.x0 + (s.x1 - s.x0) * (2 - cx[h]) / 2.0;
  fy = alt ? (s.y0 + s.y1) / 2.0 : s.y0 + (s.y1 - s.y0) * (2 - cy[h]) / 2.0;
  xform_point(hm, hx, hy, &tx, &ty);
  xform_point(inv, tx + dx, ty + dy, &px, &py);       /* where the handle is taken to, in the source frame */
  if (cx[h] != 1 && fabs(hx - fx) > 1e-9) sx = (px - fx) / (hx - fx);
  if (cy[h] != 1 && fabs(hy - fy) > 1e-9) sy = (py - fy) / (hy - fy);
  if (shift && h < 4) { double m = fabs(sx) > fabs(sy) ? fabs(sx) : fabs(sy); sx = sx < 0 ? -m : m; sy = sy < 0 ? -m : m; }
  for (c = 0; c < 4; c++) {
    double ux = c == XC_TL || c == XC_BL ? s.x0 : s.x1, uy = c == XC_TL || c == XC_TR ? s.y0 : s.y1;
    xform_point(hm, fx + (ux - fx) * sx, fy + (uy - fy) * sy, &d->xf.q[2 * c], &d->xf.q[2 * c + 1]);
  }
}
/* turning about the centre by the angle the pointer has gone round it; Shift in 15 degree steps */
static void xf_rotate(struct Data *d, double x, double y, int shift)
{
  double hm[9], cx, cy, a;
  if (!xform_matrix(&d->xf0, hm)) return;
  xform_point(hm, (d->xf0.src.x0 + d->xf0.src.x1) / 2.0, (d->xf0.src.y0 + d->xf0.src.y1) / 2.0, &cx, &cy);
  a = (atan2(y - cy, x - cx) - atan2(d->ay - cy, d->ax - cx)) * 180 / M_PI;
  if (shift) a = floor(a / 15 + 0.5) * 15;
  xform_rotate_about(&d->xf, a, cx, cy);
}
/* the quad as the drag to x, y makes it from where it began: inside moves; corners and edges scale,
   outside turns; Ctrl-corner distorts, Ctrl-Shift-edge skews, Ctrl-Alt-Shift-corner is perspective
   (or the mode Edit > Transform chose) */
static void xf_drag(struct Data *d, double x, double y, UWORD qual)
{
  int shift = (qual & QSHIFT) != 0, alt = (qual & QALT) != 0, ctrl = (qual & IEQUALIFIER_CONTROL) != 0;
  int h = d->xf_handle, mode = d->xf_mode, e;
  double dx = x - d->ax, dy = y - d->ay;
  const double *q0 = d->xf0.q;
  d->xf = d->xf0;
  if (h < 0) return;
  if (h == 8) { xform_translate(&d->xf, dx, dy); return; }
  if (mode == XM_FREE && ctrl && h < 8) {
    if (h < 4) mode = shift && alt ? XM_PERSPECTIVE : XM_DISTORT;
    else mode = shift ? XM_SKEW : XM_DISTORT;
  }
  switch (mode) {
  case XM_FREE: case XM_SCALE:
    if (h < 8) xf_scale(d, h, dx, dy, shift, alt);
    else if (mode == XM_FREE) xf_rotate(d, x, y, shift);
    break;
  case XM_ROTATE: xf_rotate(d, x, y, shift); break;
  case XM_SKEW:
    if (h >= 4 && h < 8) xform_skew(&d->xf, h - 4, dx, dy);
    else if (h < 4) {
      /* a corner slides the edge along the way it is dragged */
      if (fabs(dx) >= fabs(dy)) e = h == XC_TL || h == XC_TR ? XE_TOP : XE_BOTTOM;
      else e = h == XC_TL || h == XC_BL ? XE_LEFT : XE_RIGHT;
      xform_skew(&d->xf, e, dx, dy);
    }
    break;
  case XM_DISTORT:
    if (h < 4) xform_distort(&d->xf, h, q0[2 * h] + dx, q0[2 * h + 1] + dy);
    else if (h < 8) {
      int a = h - 4, b = (a + 1) % 4;
      xform_distort(&d->xf, a, q0[2 * a] + dx, q0[2 * a + 1] + dy);
      xform_distort(&d->xf, b, q0[2 * b] + dx, q0[2 * b + 1] + dy);
    }
    break;
  case XM_PERSPECTIVE:
    if (h < 4) xform_perspective(&d->xf, h, q0[2 * h] + dx, q0[2 * h + 1] + dy);
    else if (h < 8) xform_skew(&d->xf, h - 4, dx, dy);
    break;
  }
}
/* the transform applied (bicubic) or dropped; an impossible quad (crossed, flat) is kept for mending */
static void xf_finish(struct Data *d, Object *obj, int apply)
{
  Doc *doc = cur_doc();
  int ok;
  if (!d->xf_on) return;
  if (apply && doc && doc == d->xf_doc) {
    if (!xform_valid(&d->xf)) { ask("OK", "%s", tr("The shape can't be applied: its corners cross or it has no area.\nMove the handles back, or press Esc to cancel.")); return; }
    ov_hide(d, obj);
    d->xf_on = 0; xform_pending = 0;
    set(_app(obj), MUIA_Application_Sleep, TRUE);
    ok = d->xf_sel ? xform_selection(doc, &d->xf) : !box_empty(xform_layer(doc, doc->active, &d->xf, RS_BICUBIC));
    set(_app(obj), MUIA_Application_Sleep, FALSE);
    if (ok) changed(d, obj);
    else ask("OK", "%s", tr("The layer can't be changed (it is locked, or has no pixels)."));
    d->edges_ok = 0;
    comp_free(d);                        /* (the composite made again throughout) */
    draw_all(d, obj);
    tell_changed(d, obj);
  } else { ov_hide(d, obj); d->xf_on = 0; xform_pending = 0; ov_show(d, obj); }
  DoMethod(optbar, MUIM_Opts_Refresh);
}

/* ------------------------------------------------------------------ 0.7: the Magnetic Lasso and the Ruler */
/* the lasso closed along edges into a selection (apply), or dropped */
static void ml_finish(struct Data *d, Object *obj, int apply)
{
  Doc *doc = cur_doc();
  ov_hide(d, obj);
  if (apply && doc && d->ml) {
    int n = 0;
    double *pts = maglasso_close(d->ml, &n);
    if (pts) { sel_polygon_ex(doc, pts, n, d->ml_op, opt.feather, opt.sel_antialias); free(pts); changed(d, obj); }
  }
  if (d->ml) maglasso_free(d->ml);
  d->ml = NULL; d->mlpath = NULL; d->nmlpath = 0;
  selection_changed(d, obj);
  tell_changed(d, obj);
}
/* the Ruler's measurements as text (options bar, status bar) */
static void ruler_text(void)
{
  RulerInfo r;
  char l[16], a[16];
  ruler_measure(ruler_xy[0], ruler_xy[1], ruler_xy[2], ruler_xy[3], &r);
  fmt1(l, r.length); fmt1(a, r.angle);
  sprintf(ruler_info, "L %s   A %s\260   W %ld   H %ld", l, a, (long)floor(r.w + 0.5), (long)floor(r.h + 0.5));
}
/* the marquee's box from the anchor to x, y in the options bar's style (Shift at the start: square) */
static Box mq_box(struct Data *d, double x, double y)
{
  int style = opt.mq_style <= MQS_SIZE ? opt.mq_style : MQS_NORMAL;
  return marquee_box(style, opt.mq_w > 0 ? opt.mq_w : 1, opt.mq_h > 0 ? opt.mq_h : 1, d->ax, d->ay, x, y, style == MQS_NORMAL && (d->modifiers & QSHIFT), 0);
}

/* the text tool: the words asked for, then drawn into a new layer at x, y */
static void place_text(struct Data *d, Object *obj, double x, double y)
{
  Doc *doc = cur_doc();
  static char buf[400];
  char u[800];
  Face *f;
  Box b;
  size_t i, o = 0;
  if (!doc || !font_count()) { ask("OK", "%s", tr("There are no TrueType fonts to draw text with.\nKeep the TrueType drawer next to Easel.")); return; }
  if (!ask_string("Text", "Text:", buf, sizeof buf) || !buf[0]) return;
  for (i = 0; buf[i] && o + 4 < sizeof u; i++) {
    UBYTE c = (UBYTE)buf[i];
    if (c == '|') { u[o++] = '\n'; continue; }                     /* (a bar starts another line) */
    if (c < 0x80) u[o++] = (char)c;
    else { u[o++] = (char)(0xC0 | (c >> 6)); u[o++] = (char)(0x80 | (c & 63)); }
  }
  u[o] = 0;
  f = fonts_face(&fonts, font_name(opt.text_font), 0, 0);
  b = text_layer(doc, f, u, opt.text_size, doc->fg, (i32)x, (i32)y, opt.text_align, opt.antialias);
  if (box_empty(b)) return;
  changed(d, obj);
  update_box(d, obj, b);
  tell_changed(d, obj);
}

/* the mouse button down at window mx, my (inside the area) */
static void press(struct Data *d, Object *obj, LONG mx, LONG my, UWORD qual, ULONG secs, ULONG micros)
{
  Doc *doc = cur_doc();
  double x = to_ix(d, obj, mx), y = to_iy(d, obj, my);
  int t = d->space ? TL_HAND : tool, dbl = DoubleClick(d->secs, d->micros, secs, micros);
  int shift = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0, alt = (qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) != 0;
  Box ch;
  d->secs = secs; d->micros = micros;
  d->modifiers = qual;
  /* 0.4: snapping (guides, grid, edges) for the tools that drag out shapes */
  if (doc && doc->guides.snap && (t == TL_MARQUEE || t == TL_EMARQUEE || t == TL_RECT || t == TL_ELLIPSE || t == TL_LINE || t == TL_GRADIENT || t == TL_CROP)) {
    doc->guides.snap_tol = (u16)(16 * 6 / d->z + 1);
    snap_point(doc, &x, &y, SNAP_DOC);
  }
  d->ax = d->bx = x; d->ay = d->by = y;
  if (!doc) return;
  ov_hide(d, obj);
  if (d->xf_on && t != TL_HAND) {
    /* 0.7: Free Transform's handles take the mouse, whatever the tool */
    int h = xf_hit(d, obj, mx, my);
    if (dbl && h == 8) { ov_show(d, obj); xf_finish(d, obj, 1); return; }
    d->xf_handle = h; d->xf0 = d->xf; d->qual = qual;
    d->drag = DR_XFORM;
    ov_show(d, obj);
    return;
  }
  if (brushlike(t) && alt && t != TL_CLONE && t != TL_ERASER) t = TL_EYEDROPPER;    /* (Alt: the eyedropper for a moment) */
  switch (t) {
  case TL_HAND: d->drag = DR_PAN; d->pmx = mx; d->pmy = my; break;
  case TL_ZOOM:
    ov_show(d, obj);
    zoom_to(d, obj, zoom_step(d->z, alt ? -1 : 1), mx, my);
    return;
  case TL_EYEDROPPER: pick(d, obj, x, y, alt && tool == TL_EYEDROPPER); d->drag = DR_PICK; break;
  case TL_MOVE:
    if (opt.auto_select && x >= 0 && y >= 0 && x < doc->w && y < doc->h) {
      /* the topmost visible layer with something at the pointer */
      int i;
      for (i = doc->nlay - 1; i >= 0; i--)
        if (doc->lay[i]->visible && doc->lay[i]->px && doc->lay[i]->px[((long)(i32)y * doc->w + (i32)x) * 4 + 3] > 16) break;
      if (i >= 0 && i != doc->active) { doc->active = i; changed(d, obj); tell_changed(d, obj); }
    }
    d->drag = DR_MOVE;
    break;
  case TL_MARQUEE: case TL_EMARQUEE:
    if (t == TL_MARQUEE && (opt.mq_style == MQS_ROW || opt.mq_style == MQS_COL)) {
      /* 0.7: the Single Row and Single Column marquees select at the click */
      if (x >= 0 && y >= 0 && x < doc->w && y < doc->h) {
        if (opt.mq_style == MQS_ROW) sel_single_row(doc, (i32)y, sel_op_of(qual)); else sel_single_col(doc, (i32)x, sel_op_of(qual));
        changed(d, obj);
      }
      selection_changed(d, obj);
      tell_changed(d, obj);
      break;
    }
    d->drag = DR_MARQUEE; d->mbox = mq_box(d, x, y);
    break;
  case TL_MAGLASSO: {
    /* 0.7: the first click starts the lasso, each later one lays an anchor; a double-click, or a
       click on the first point, closes it */
    double *p;
    int n;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > doc->w - 1) x = doc->w - 1;
    if (y > doc->h - 1) y = doc->h - 1;
    if (!d->ml) {
      if (!(d->ml = maglasso_new(doc, opt.ml_width, opt.ml_contrast, 200 / (opt.ml_freq + 1) + 8, 0))) { ask("OK", "%s", tr("There isn't enough memory for that.")); break; }
      d->ml_op = sel_op_of(qual);
      maglasso_anchor(d->ml, x, y);
      d->mlpath = NULL; d->nmlpath = 0;
      break;
    }
    maglasso_points(d->ml, &p, &n);
    if (dbl || (maglasso_anchors(d->ml) >= 2 && n > 0 && labs(to_sx(d, obj, p[0]) - mx) < 6 && labs(to_sy(d, obj, p[1]) - my) < 6)) { ml_finish(d, obj, 1); return; }
    maglasso_anchor(d->ml, x, y);
    d->mlpath = NULL; d->nmlpath = 0;
    break; }
  case TL_RULER:
    /* 0.7: a new line, or an end of the one there dragged */
    d->ruler_end = 1;
    if (ruler_on && ruler_doc == doc) {
      if (labs(to_sx(d, obj, ruler_xy[0]) - mx) < 6 && labs(to_sy(d, obj, ruler_xy[1]) - my) < 6) { d->ruler_end = 0; d->drag = DR_RULER; break; }
      if (labs(to_sx(d, obj, ruler_xy[2]) - mx) < 6 && labs(to_sy(d, obj, ruler_xy[3]) - my) < 6) { d->drag = DR_RULER; break; }
    }
    ruler_xy[0] = ruler_xy[2] = x; ruler_xy[1] = ruler_xy[3] = y;
    ruler_on = 1; ruler_doc = doc;
    ruler_text();
    d->drag = DR_RULER;
    break;
  case TL_LASSO: d->drag = DR_LASSO; d->npoly = 0; poly_add(d, x, y); break;
  case TL_PLASSO:
    if (!d->poly_on) { d->poly_on = 1; d->npoly = 0; poly_add(d, x, y); }
    else if (dbl || (d->npoly >= 3 && fabs(to_sx(d, obj, d->poly[0]) - mx) < 6 && fabs(to_sy(d, obj, d->poly[1]) - my) < 6)) { poly_finish(d, obj, 1); return; }
    else poly_add(d, x, y);
    break;
  case TL_WAND:
    if (x >= 0 && y >= 0 && x < doc->w && y < doc->h) { sel_wand_ex(doc, (i32)x, (i32)y, opt.tolerance, opt.contiguous, opt.all_layers, sel_op_of(qual), opt.sel_antialias); changed(d, obj); }
    selection_changed(d, obj);
    tell_changed(d, obj);
    break;
  case TL_BUCKET:
    if (x >= 0 && y >= 0 && x < doc->w && y < doc->h) {
      if (opt.fill_source && tool_pattern()) ch = fill_bucket_pattern(doc, (i32)x, (i32)y, tool_pattern(), opt.tolerance, opt.contiguous, opt.all_layers, opt.fill_opacity, opt.antialias);
      else ch = fill_bucket(doc, (i32)x, (i32)y, doc->fg, opt.tolerance, opt.contiguous, opt.all_layers, opt.fill_opacity, opt.antialias);
      if (!box_empty(ch)) { changed(d, obj); update_box(d, obj, ch); }
      tell_changed(d, obj);
    }
    break;
  case TL_MAGICERASE:
    if (x >= 0 && y >= 0 && x < doc->w && y < doc->h) {
      ch = magic_erase(doc, (i32)x, (i32)y, opt.tolerance, opt.contiguous, opt.all_layers, opt.fill_opacity, opt.antialias);
      if (!box_empty(ch)) { changed(d, obj); update_box(d, obj, ch); }
      tell_changed(d, obj);
    }
    break;
  case TL_GRADIENT: case TL_LINE: d->drag = DR_LINE; break;
  case TL_RECT: case TL_ELLIPSE: d->drag = DR_SHAPE; break;
  case TL_TEXT: ov_show(d, obj); place_text(d, obj, x, y); return;
  case TL_CROP:
    if (d->crop_on) {
      int h = crop_hit(d, obj, mx, my);
      if (dbl && h == 8) { ov_show(d, obj); crop_apply(d, obj, 1); return; }
      if (h >= 0) { d->drag = DR_CROPMOVE; d->crop_handle = h; break; }
    }
    d->crop_on = 1; crop_pending = 1;
    d->c0x = d->c1x = x; d->c0y = d->c1y = y;
    d->drag = DR_CROP;
    break;
  default:
    if (brushlike(t)) {
      Brush b = tool_brush(t);
      u32 color = doc->fg;
      if (t == TL_CLONE) {
        if (alt) { d->clone_x = x; d->clone_y = y; d->clone_set = 1; d->clone_aligned_set = 0; break; }
        if (!d->clone_set) { ask("OK", "%s", tr("Alt-click where the Clone Stamp should copy from first.")); break; }
        if (!d->clone_aligned_set) { d->clone_dx = (i32)floor(d->clone_x - x + 0.5); d->clone_dy = (i32)floor(d->clone_y - y + 0.5); d->clone_aligned_set = 1; }
        b.clone_dx = d->clone_dx; b.clone_dy = d->clone_dy;
      }
      if (quickmask_active(doc)) {
        /* 0.7: in Quick Mask mode the brush, pencil and eraser paint the mask (black hides, white shows) */
        PaintTarget pt;
        if (t != TL_BRUSH && t != TL_PENCIL && t != TL_ERASER) { ask("OK", "%s", tr("Paint the Quick Mask with the Brush, Pencil or Eraser (Q leaves Quick Mask mode).")); break; }
        if (!quickmask_target(doc, &pt)) break;
        if (shift && d->has_last) {
          d->stroke = stroke_begin_target(doc, &pt, &b, color, d->lastx, d->lasty, 1.0, &ch);
          if (d->stroke) { update_box(d, obj, ch); stroke_to(d->stroke, x, y, &ch); }
        } else d->stroke = stroke_begin_target(doc, &pt, &b, color, x, y, 1.0, &ch);
        if (!d->stroke) break;
      } else if (shift && d->has_last) {
        /* a straight line from the last stroke's end */
        d->stroke = stroke_begin(doc, &b, color, d->lastx, d->lasty, &ch);
        if (d->stroke) { update_box(d, obj, ch); stroke_to(d->stroke, x, y, &ch); }
      } else d->stroke = stroke_begin(doc, &b, color, x, y, &ch);
      if (!d->stroke) {
        Layer *al = doc->lay[doc->active];
        if (al->locked || al->lock_all) ask("OK", "%s", tr("The layer is locked."));
        else if (al->kind != LK_PIXEL && !al->edit_mask) ask("OK", "%s", tr("This layer has no pixels to paint on: paint on its mask, or rasterize it first."));
        else if (t == TL_HISTORY) ask("OK", "%s", tr("There is no snapshot to paint from (Edit > Take Snapshot)."));
        else if (t == TL_PATTERN) ask("OK", "%s", tr("Choose a pattern first."));
        else if (!doc->lay[doc->active]->visible) ask("OK", "%s", tr("The layer is hidden: it can't be painted on."));
        break;
      }
      d->drag = DR_STROKE;
      d->sbox = ch;
      changed(d, obj);
      mask_painted(doc);
      update_box(d, obj, ch);
      if (stroke_wants_tick(&b) && !d->sihn_on) {
        int ms = stroke_tick_ms(&b);
        d->sihn.ihn_Object = obj; d->sihn.ihn_Flags = MUIIHNF_TIMER; d->sihn.ihn_Method = MUIM_Canvas_StrokeTick;
        d->sihn.ihn_stuff.ihn_timer.ihn_millis = ms < 40 ? 40 : ms;
        DoMethod(_app(obj), MUIM_Application_AddInputHandler, (ULONG)&d->sihn);
        d->sihn_on = 1;
      }
    }
    break;
  }
  ov_show(d, obj);
}
static void motion(struct Data *d, Object *obj, LONG mx, LONG my)
{
  Doc *doc = cur_doc();
  DocView *v = cur_view();
  double x = to_ix(d, obj, mx), y = to_iy(d, obj, my);
  Box ch;
  if (!doc || !v) return;
  switch (d->drag) {
  case DR_PAN:
    v->sx -= mx - d->pmx; v->sy -= my - d->pmy;
    d->pmx = mx; d->pmy = my;
    draw_all(d, obj);
    break;
  case DR_PICK: pick(d, obj, x, y, (d->modifiers & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) && tool == TL_EYEDROPPER); break;
  case DR_STROKE:
    if (d->stroke) { stroke_to(d->stroke, x, y, &ch); d->sbox = box_union(d->sbox, ch); mask_painted(doc); update_box(d, obj, ch); }
    d->bx = x; d->by = y;
    break;
  case DR_LASSO:
    ov_hide(d, obj);
    if (fabs(to_sx(d, obj, d->poly[d->npoly * 2 - 2]) - mx) + fabs(to_sy(d, obj, d->poly[d->npoly * 2 - 1]) - my) >= 2) poly_add(d, x, y);
    ov_show(d, obj);
    break;
  case DR_XFORM:
    ov_hide(d, obj);
    xf_drag(d, x, y, d->qual);
    xf_info(d);
    ov_show(d, obj);
    DoMethod(optbar, MUIM_Opts_Refresh);
    break;
  case DR_RULER:
    ov_hide(d, obj);
    ruler_xy[2 * d->ruler_end] = x; ruler_xy[2 * d->ruler_end + 1] = y;
    ruler_text();
    ov_show(d, obj);
    DoMethod(optbar, MUIM_Opts_Refresh);
    break;
  case DR_MARQUEE:
    ov_hide(d, obj);
    if (doc->guides.snap) snap_point(doc, &x, &y, SNAP_DOC);
    d->bx = x; d->by = y;
    d->mbox = mq_box(d, x, y);
    ov_show(d, obj);
    break;
  case DR_SHAPE: case DR_LINE: case DR_MOVE:
    ov_hide(d, obj);
    if (doc->guides.snap && d->drag != DR_MOVE) snap_point(doc, &x, &y, SNAP_DOC);
    d->bx = x; d->by = y;
    if (d->modifiers & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) {
      if (d->drag == DR_MARQUEE || d->drag == DR_SHAPE) {
        /* Shift: a square or a circle */
        double w = fabs(d->bx - d->ax), h = fabs(d->by - d->ay), s = w > h ? w : h;
        d->bx = d->ax + (d->bx < d->ax ? -s : s); d->by = d->ay + (d->by < d->ay ? -s : s);
      } else if (d->drag == DR_LINE) {
        /* Shift: across, down or at 45 degrees */
        double dx = d->bx - d->ax, dy = d->by - d->ay;
        if (fabs(dx) > 2 * fabs(dy)) d->by = d->ay;
        else if (fabs(dy) > 2 * fabs(dx)) d->bx = d->ax;
        else { double s = (fabs(dx) + fabs(dy)) / 2; d->bx = d->ax + (dx < 0 ? -s : s); d->by = d->ay + (dy < 0 ? -s : s); }
      }
    }
    ov_show(d, obj);
    break;
  case DR_CROP: {
    Box b;
    ov_hide(d, obj);
    if (doc->guides.snap) snap_point(doc, &x, &y, SNAP_DOC);
    /* 0.7: in the preset's proportions */
    b = crop_preset_box(doc, &crop_presets[opt.crop_preset], opt.crop_swap, d->ax, d->ay, x, y, 0);
    d->c0x = b.x0; d->c0y = b.y0; d->c1x = b.x1; d->c1y = b.y1;
    ov_show(d, obj);
    break; }
  case DR_CROPMOVE: {
    double dx = x - d->bx, dy = y - d->by;
    int h = d->crop_handle;
    ov_hide(d, obj);
    if (h == 8) { d->c0x += dx; d->c1x += dx; d->c0y += dy; d->c1y += dy; }
    else {
      if (h % 3 == 0) d->c0x += dx;
      if (h % 3 == 2) d->c1x += dx;
      if (h < 3) d->c0y += dy;
      if (h >= 6) d->c1y += dy;
    }
    d->bx = x; d->by = y;
    ov_show(d, obj);
    break; }
  }
}
static void stroke_timer_off(struct Data *d, Object *obj)
{
  if (d->sihn_on) { DoMethod(_app(obj), MUIM_Application_RemInputHandler, (ULONG)&d->sihn); d->sihn_on = 0; }
}
static void release(struct Data *d, Object *obj)
{
  Doc *doc = cur_doc();
  Box ch;
  int drag = d->drag;
  if (!doc) { d->drag = DR_NONE; return; }
  d->drag = DR_NONE;
  switch (drag) {
  case DR_STROKE:
    stroke_timer_off(d, obj);
    if (d->stroke) { stroke_end(d->stroke); d->stroke = NULL; mask_painted(doc);
      /* (smoothing finishes the stroke at the pointer; an indexed or grey picture was brought back to its colours) */
      if (brushui.b.dyn.smoothing || brushui.b.dyn.pulled_string) DoMethod(obj, MUIM_Canvas_Update, 0, 0, doc->w, doc->h);
      else if (doc->mode != MODE_RGB) update_box(d, obj, d->sbox);
    }
    d->lastx = d->bx; d->lasty = d->by; d->has_last = 1;
    tell_changed(d, obj);
    break;
  case DR_PICK: break;
  case DR_LASSO: d->drag = DR_LASSO; poly_finish(d, obj, 1); break;
  case DR_MARQUEE:
    d->drag = DR_MARQUEE; ov_hide(d, obj); d->drag = DR_NONE;
    if (opt.mq_style != MQS_SIZE && fabs(to_sx(d, obj, d->bx) - to_sx(d, obj, d->ax)) < 2 && fabs(to_sy(d, obj, d->by) - to_sy(d, obj, d->ay)) < 2) {
      if (sel_op_of(d->modifiers) == SEL_REPLACE && doc->sel) { sel_none(doc); changed(d, obj); }
    } else if (!box_empty(d->mbox)) {
      Box b = d->mbox;
      if (tool == TL_EMARQUEE) sel_ellipse_ex(doc, b.x0, b.y0, b.x1, b.y1, sel_op_of(d->modifiers), opt.feather, opt.sel_antialias);
      else sel_rect(doc, b.x0, b.y0, b.x1, b.y1, sel_op_of(d->modifiers), opt.feather);
      changed(d, obj);
    }
    selection_changed(d, obj);
    tell_changed(d, obj);
    break;
  case DR_MOVE: {
    LONG dx = (LONG)floor(d->bx - d->ax + 0.5), dy = (LONG)floor(d->by - d->ay + 0.5);
    d->drag = DR_MOVE; ov_hide(d, obj); d->drag = DR_NONE;
    if (dx || dy) {
      /* 0.7: Alt-drag moves a copy (of the selected pixels, or of the whole layer) */
      if ((d->modifiers & QALT) && doc->lay[doc->active]->kind != LK_GROUP) ch = move_selection_copy(doc, dx, dy);
      else ch = doc->lay[doc->active]->kind == LK_GROUP ? doc_move_group(doc, doc->active, dx, dy) : move_layer(doc, dx, dy);
      if (!box_empty(ch)) {
        changed(d, obj);
        if (doc->sel) edges_make(d);
        update_box(d, obj, ch);
        if (doc->sel) selection_changed(d, obj);
      } else if (doc->lay[doc->active]->locked) ask("OK", "%s", tr("The layer is locked."));
    }
    tell_changed(d, obj);
    break; }
  case DR_LINE:
    d->drag = DR_LINE; ov_hide(d, obj); d->drag = DR_NONE;
    if (fabs(d->bx - d->ax) + fabs(d->by - d->ay) > 0.5 / d->z) {
      if (tool == TL_GRADIENT)
      {
        Gradient g = *tool_gradient();
        g.fg = doc->fg; g.bg = doc->bg;
        ch = fill_gradient_ex(doc, d->ax, d->ay, d->bx, d->by, opt.grad_kind, &g, opt.grad_reverse, opt.grad_dither, opt.opacity[TL_GRADIENT], opt.mode[TL_GRADIENT]);
      }
      else ch = shape_line(doc, d->ax, d->ay, d->bx, d->by, opt.line_width, doc->fg, opt.opacity[TL_LINE]);
      if (!box_empty(ch)) { changed(d, obj); update_box(d, obj, ch); }
    }
    tell_changed(d, obj);
    break;
  case DR_SHAPE:
    d->drag = DR_SHAPE; ov_hide(d, obj); d->drag = DR_NONE;
    if (fabs(d->bx - d->ax) >= 1 && fabs(d->by - d->ay) >= 1) {
      double x0 = d->ax < d->bx ? d->ax : d->bx, x1 = d->ax < d->bx ? d->bx : d->ax, y0 = d->ay < d->by ? d->ay : d->by, y1 = d->ay < d->by ? d->by : d->ay;
      if (tool == TL_ELLIPSE) ch = shape_ellipse(doc, x0, y0, x1, y1, doc->fg, opt.opacity[TL_ELLIPSE]);
      else ch = shape_rect(doc, x0, y0, x1, y1, opt.shape_fill_radius, doc->fg, opt.opacity[TL_RECT]);
      if (!box_empty(ch)) { changed(d, obj); update_box(d, obj, ch); }
    }
    tell_changed(d, obj);
    break;
  case DR_XFORM: xf_info(d); DoMethod(optbar, MUIM_Opts_Refresh); break;
  case DR_RULER:
    ov_hide(d, obj);
    if (fabs(ruler_xy[2] - ruler_xy[0]) + fabs(ruler_xy[3] - ruler_xy[1]) < 0.5) { ruler_on = 0; ruler_info[0] = 0; }
    DoMethod(optbar, MUIM_Opts_Refresh);
    break;
  case DR_CROP: case DR_CROPMOVE:
    ov_hide(d, obj);
    crop_norm(d);
    if (d->c1x - d->c0x < 1 || d->c1y - d->c0y < 1) { d->crop_on = 0; crop_pending = 0; }
    break;
  }
  ov_show(d, obj);
}

/* ------------------------------------------------------------------ keys */
static int key_char(struct IntuiMessage *im)
{
  struct InputEvent ie;
  char buf[4];
  memset(&ie, 0, sizeof ie);
  ie.ie_Class = IECLASS_RAWKEY; ie.ie_Code = im->Code; ie.ie_Qualifier = im->Qualifier & ~(IEQUALIFIER_CONTROL | IEQUALIFIER_LALT | IEQUALIFIER_RALT);
  ie.ie_EventAddress = (APTR *)*((ULONG *)im->IAddress);
  if (MapRawKey(&ie, (STRPTR)buf, sizeof buf, NULL) != 1) return 0;
  return (UBYTE)buf[0];
}
static void brush_size(int up)
{
  int s = opt.size[tool];
  int step = s < 10 ? 1 : s < 50 ? 5 : s < 100 ? 10 : s < 300 ? 25 : 50;
  s += up ? step : -step;
  if (s < 1) s = 1;
  if (s > 2500) s = 2500;
  opt.size[tool] = s;
}
static int keys(struct Data *d, Object *obj, struct IntuiMessage *im)
{
  UWORD code = im->Code, q = im->Qualifier;
  Doc *doc = cur_doc();
  DocView *v = cur_view();
  int c, i, shift = (q & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
  if (q & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND)) return 0;      /* the menus' shortcuts */
  if (code == 0x40 || code == (0x40 | IECODE_UP_PREFIX)) {
    /* space: the hand while it is held */
    int down = !(code & IECODE_UP_PREFIX);
    if (down != d->space) { ov_hide(d, obj); d->space = down; ov_show(d, obj); }
    return 1;
  }
  if (code & IECODE_UP_PREFIX) return 0;
  if (!doc || !v) return 0;
  if (shift && (q & (IEQUALIFIER_LALT | IEQUALIFIER_RALT))) {
    /* Shift-Alt and a letter: a blend mode, as Photoshop's keys (the painting tool's, or with the
       Move tool the active layer's) */
    static const UBYTE raw[] = { 0x36, 0x17, 0x27, 0x37, 0x35, 0x20, 0x24, 0x21, 0x22, 0x11, 0x18, 0x23, 0x25, 0x34, 0x26, 0x31, 0x28, 0x12, 0x32, 0x16, 0x14, 0x33, 0x15 };
    static const UBYTE mode[] = { BM_NORMAL, BM_DISSOLVE, BM_DARKEN, BM_MULTIPLY, BM_BURN, BM_LINEARBURN, BM_LIGHTEN, BM_SCREEN, BM_DODGE, BM_ADD,
      BM_OVERLAY, BM_SOFTLIGHT, BM_HARDLIGHT, BM_VIVIDLIGHT, BM_LINEARLIGHT, BM_PINLIGHT, BM_HARDMIX, BM_DIFFERENCE, BM_EXCLUSION, BM_HUE,
      BM_SATURATION, BM_COLOR, BM_LUMINOSITY };
    for (i = 0; i < (int)sizeof raw; i++) if (raw[i] == code) {
      if (tool == TL_MOVE) { if (doc->lay[doc->active]->kind != LK_GROUP || mode[i]) doc_set_layer_props(doc, doc->active, NULL, -1, mode[i], -1); changed(d, obj); tell_changed(d, obj); draw_all(d, obj); }
      else if (brushlike(tool) || tool == TL_GRADIENT) { opt.mode[tool] = mode[i]; DoMethod(optbar, MUIM_Opts_Refresh); }
      return 1;
    }
  }
  switch (code) {
  case 0x44: case 0x43:                         /* Return, Enter */
    if (d->xf_on) { xf_finish(d, obj, 1); return 1; }
    if (d->ml) { ml_finish(d, obj, 1); return 1; }
    if (d->poly_on) { poly_finish(d, obj, 1); return 1; }
    if (d->crop_on) { crop_apply(d, obj, 1); return 1; }
    return 0;
  case 0x45:                                    /* Esc */
    if (d->xf_on) { xf_finish(d, obj, 0); return 1; }
    if (d->ml) { ml_finish(d, obj, 0); return 1; }
    if (d->poly_on) { poly_finish(d, obj, 0); return 1; }
    if (d->crop_on) { ov_hide(d, obj); d->crop_on = 0; crop_pending = 0; ov_show(d, obj); return 1; }
    return 0;
  case 0x46: case 0x41:                         /* Del, Backspace (Alt: fill with the foreground, Ctrl: the background) */
    if (d->ml) {
      /* the Magnetic Lasso's last anchor taken back (and the lasso dropped with its first) */
      ov_hide(d, obj);
      d->mlpath = NULL; d->nmlpath = 0;
      if (!maglasso_unanchor(d->ml)) { maglasso_free(d->ml); d->ml = NULL; }
      ov_show(d, obj);
      return 1;
    }
    if (d->xf_on) return 1;
    DoMethod(_app(obj), MUIM_Application_ReturnID, (q & IEQUALIFIER_CONTROL) ? ID_FILLBG : (q & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) ? ID_FILLFG : ID_CLEAR);
    return 1;
  case 0x4C: case 0x4D: case 0x4E: case 0x4F: {  /* the arrows: nudge (move tool) or scroll */
    LONG dx = code == 0x4E ? 1 : code == 0x4F ? -1 : 0, dy = code == 0x4D ? 1 : code == 0x4C ? -1 : 0;
    if (d->xf_on) { ov_hide(d, obj); xform_translate(&d->xf, dx * (shift ? 10 : 1), dy * (shift ? 10 : 1)); xf_info(d); ov_show(d, obj); DoMethod(optbar, MUIM_Opts_Refresh); }
    else if (tool == TL_MOVE) {
      Box ch = doc->lay[doc->active]->kind == LK_GROUP ? doc_move_group(doc, doc->active, dx * (shift ? 10 : 1), dy * (shift ? 10 : 1))
                                                      : move_layer(doc, dx * (shift ? 10 : 1), dy * (shift ? 10 : 1));
      if (!box_empty(ch)) { changed(d, obj); if (doc->sel) edges_make(d); update_box(d, obj, ch); if (doc->sel) selection_changed(d, obj); tell_changed(d, obj); }
    } else { v->sx += dx * (shift ? 200 : 40); v->sy += dy * (shift ? 200 : 40); draw_all(d, obj); }
    return 1; }
  }
  c = key_char(im);
  if (!c) return 0;
  if (q & IEQUALIFIER_CONTROL) {
    /* PhotoCraft's Ctrl shortcuts (the Amiga key ones are the menus') */
    int alt = (q & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) != 0;
    ULONG id = 0;
    switch (ToLower((ULONG)c)) {
    case 'z': id = shift ? ID_REDO : alt ? ID_STEPBACK : ID_UNDO; break;
    case 'y': id = ID_REDO; break;
    case 'x': id = ID_CUT; break;
    case 'c': id = alt ? ID_CANVASSIZE : shift ? ID_COPYMERGED : ID_COPY; break;
    case 'v': id = shift ? ID_PASTEINPLACE : ID_PASTE; break;
    case 'a': id = ID_SELALL; break;
    case 'd': id = ID_DESELECT; break;
    case 'i': id = alt ? ID_IMAGESIZE : shift ? ID_INVERSE : ID_ADJ_INVERT; break;
    case 'l': id = shift ? ID_AUTOTONE : ID_ADJ_LEVELS; break;
    case 'm': id = ID_ADJ_CURVES; break;
    case 'u': id = shift ? ID_ADJ_DESAT : ID_ADJ_HUESAT; break;
    case 'b': id = ID_ADJ_BALANCE; break;
    case 'j': id = shift ? ID_LAYERCUTSEL : ID_LAYERFROMSEL; break;
    case 'e': id = shift ? ID_MERGEVISIBLE : ID_MERGEDOWN; break;
    case 't': id = shift ? ID_XF_AGAIN : ID_FREETRANSFORM; break;
    case 'n': id = shift ? ID_NEWLAYER : ID_NEW; break;
    case 'o': id = ID_OPEN; break;
    case 's': id = shift ? ID_SAVEAS : ID_SAVE; break;
    case 'w': id = ID_CLOSE; break;
    case 'q': id = ID_QUIT; break;
    case '0': id = ID_FIT; break;
    case '1': id = ID_ACTUAL; break;
    case '+': case '=': id = ID_ZOOMIN; break;
    case '-': id = ID_ZOOMOUT; break;
    case '\t': id = ID_NEXTDOC; break;
    case 8: id = ID_FILLBG; break;
    case ']': id = shift ? ID_TOFRONT : ID_RAISE; break;
    case '[': id = shift ? ID_TOBACK : ID_LOWER; break;
    }
    if (!id) return 0;
    DoMethod(_app(obj), MUIM_Application_ReturnID, id);
    return 1;
  }
  if ((q & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) && c == 8) { DoMethod(_app(obj), MUIM_Application_ReturnID, ID_FILLFG); return 1; }
  if (c == '[' || c == ']' || c == '{' || c == '}') {
    if (!brushlike(tool)) return 1;
    ov_hide(d, obj);
    if (c == '{' || c == '}' || shift) {
      int h = opt.hardness[tool] + ((c == '}' || c == ']') ? 25 : -25);
      opt.hardness[tool] = h < 0 ? 0 : h > 100 ? 100 : h;
    } else brush_size(c == ']');
    ov_show(d, obj);
    DoMethod(optbar, MUIM_Opts_Refresh);
    return 1;
  }
  if (c >= '0' && c <= '9' && (brushlike(tool) || tool == TL_GRADIENT || tool == TL_BUCKET)) {
    int o = c == '0' ? 100 : (c - '0') * 10;
    if (tool == TL_BUCKET) opt.fill_opacity = o; else opt.opacity[tool] = o;
    DoMethod(optbar, MUIM_Opts_Refresh);
    return 1;
  }
  if (c == '+' || c == '=') { zoom_to(d, obj, zoom_step(d->z, 1), -1, -1); return 1; }
  if (c == '-') { zoom_to(d, obj, zoom_step(d->z, -1), -1, -1); return 1; }
  if (c == 9) { DoMethod(_app(obj), MUIM_Application_ReturnID, shift ? ID_TOGGLERIGHT : ID_TOGGLEPANELS); return 1; }   /* Tab, Shift-Tab */
  if (c == 'x' || c == 'X') { DoMethod(_app(obj), MUIM_Application_ReturnID, ID_SWAPCOLORS); return 1; }
  if (c == 'd' || c == 'D') { DoMethod(_app(obj), MUIM_Application_ReturnID, ID_DEFCOLORS); return 1; }
  if (c == 'q' || c == 'Q') { DoMethod(_app(obj), MUIM_Application_ReturnID, ID_QUICKMASK); return 1; }   /* 0.7: Quick Mask */
  /* a tool's letter: that tool, or (pressed again, or with Shift) the next tool sharing the letter */
  c = ToUpper((ULONG)c);
  if ((i = tool_for_key(c)) < 0) return 0;
  if (tool_keys[tool] == c && (shift || i == tool)) {
    int k;
    for (k = 1; k < TL_N; k++) if (tool_keys[(tool + k) % TL_N] == c) { i = (tool + k) % TL_N; break; }
  }
  set_tool(i);
  return 1;
}

/* ------------------------------------------------------------------ events */
#define EV (IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE)
static ULONG handle(struct IClass *cl, Object *obj, struct MUIP_HandleEvent *m)
{
  struct Data *d = INST_DATA(cl, obj);
  struct IntuiMessage *im = m->imsg;
  LONG mx, my;
  int in;
  if (!im || !d->shown) return 0;
  if (im->Class == IDCMP_RAWKEY) return keys(d, obj, im) ? MUI_EventHandlerRC_Eat : 0;
  mx = im->MouseX; my = im->MouseY;
  in = mx >= _mleft(obj) && mx <= _mright(obj) && my >= _mtop(obj) && my <= _mbottom(obj);
  if (im->Class == IDCMP_MOUSEMOVE) {
    Doc *doc = cur_doc();
    if (!in && !d->drag && !d->cur_in) return 0;
    ov_hide(d, obj);
    d->cur_x = mx; d->cur_y = my; d->cur_in = in;
    ov_show(d, obj);
    d->qual = im->Qualifier;
    if (d->drag) motion(d, obj, mx, my);
    else if (d->ml && in) {
      /* 0.7: the Magnetic Lasso's live path follows the pointer */
      ov_hide(d, obj);
      maglasso_track(d->ml, to_ix(d, obj, mx), to_iy(d, obj, my), &d->mlpath, &d->nmlpath);
      ov_show(d, obj);
    }
    if (doc && in) {
      double x = to_ix(d, obj, mx), y = to_iy(d, obj, my);
      if (x >= 0 && y >= 0 && x < doc->w && y < doc->h) {
        const u8 *p = d->comp && d->doc == doc ? d->comp + ((long)(i32)y * doc->w + (i32)x) * 4 : NULL;
        if (p) sprintf(canvas_info, "X %ld  Y %ld   R %d G %d B %d A %d", (long)x, (long)y, (int)p[0], (int)p[1], (int)p[2], (int)p[3]);
        else sprintf(canvas_info, "X %ld  Y %ld", (long)x, (long)y);
        canvas_pt.x = (LONG)x; canvas_pt.y = (LONG)y; canvas_pt.in = 1; canvas_pt.has_rgb = p != NULL;
        if (p) { canvas_pt.rgba[0] = p[0]; canvas_pt.rgba[1] = p[1]; canvas_pt.rgba[2] = p[2]; canvas_pt.rgba[3] = p[3]; }
      } else { canvas_info[0] = 0; canvas_pt.in = 0; }
      if (d->drag == DR_MARQUEE) sprintf(canvas_info + strlen(canvas_info), "   %ld x %ld", (long)(d->mbox.x1 - d->mbox.x0), (long)(d->mbox.y1 - d->mbox.y0));
      else if (d->drag == DR_CROP) sprintf(canvas_info + strlen(canvas_info), "   %ld x %ld", (long)(d->c1x - d->c0x), (long)(d->c1y - d->c0y));
      else if (d->drag == DR_SHAPE)
        sprintf(canvas_info + strlen(canvas_info), "   %ld x %ld", (long)fabs(floor(d->bx + 0.5) - floor(d->ax + 0.5)), (long)fabs(floor(d->by + 0.5) - floor(d->ay + 0.5)));
      if (tool == TL_RULER && ruler_on && ruler_doc == doc) snprintf(canvas_info + strlen(canvas_info), sizeof canvas_info - strlen(canvas_info), "   %s", ruler_info);
      set(obj, MUIA_Canvas_Pointer, TRUE);
    }
    return d->drag ? MUI_EventHandlerRC_Eat : 0;
  }
  if (im->Class == IDCMP_MOUSEBUTTONS) {
    if (im->Code == SELECTDOWN && in) {
      press(d, obj, mx, my, im->Qualifier, im->Seconds, im->Micros);
      return MUI_EventHandlerRC_Eat;
    }
    if (im->Code == SELECTUP && d->drag) { release(d, obj); return MUI_EventHandlerRC_Eat; }
  }
  return 0;
}

/* ------------------------------------------------------------------ the dispatcher */
static void mset(struct Data *d, Object *obj, struct opSet *m)
{
  struct TagItem *tags = m->ops_AttrList, *t;
  DocView *v = cur_view();
  while ((t = NextTagItem(&tags))) {
    switch (t->ti_Tag) {
    case MUIA_Canvas_VProp: d->vprop = (Object *)t->ti_Data; break;
    case MUIA_Canvas_HProp: d->hprop = (Object *)t->ti_Data; break;
    case MUIA_Canvas_Top: if (v && v->sy != (LONG)t->ti_Data) { v->sy = (LONG)t->ti_Data; draw_all(d, obj); } break;
    case MUIA_Canvas_Left: if (v && v->sx != (LONG)t->ti_Data) { v->sx = (LONG)t->ti_Data; draw_all(d, obj); } break;
    }
  }
}
static ULONG dispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct Data *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->obj = obj; d->z = 1;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = EV;
    d->ihn.ihn_Object = obj; d->ihn.ihn_Flags = MUIIHNF_TIMER; d->ihn.ihn_Method = MUIM_Canvas_Tick;
    d->ihn.ihn_stuff.ihn_timer.ihn_millis = 250;
    mset(d, obj, (struct opSet *)msg);
    return (ULONG)obj;
  case OM_DISPOSE:
    d = INST_DATA(cl, obj);
    comp_free(d);
    if (d->rowbuf) FreeVec(d->rowbuf);
    if (d->view) FreeVec(d->view);
    if (d->xmap) FreeVec(d->xmap);
    free(d->edges); free(d->poly);
    if (d->ml) maglasso_free(d->ml);
    break;
  case OM_SET:
    mset(INST_DATA(cl, obj), obj, (struct opSet *)msg);
    break;
  case OM_GET: {
    struct opGet *g = (struct opGet *)msg;
    if (g->opg_AttrID == MUIA_Canvas_Changed || g->opg_AttrID == MUIA_Canvas_Pointer || g->opg_AttrID == MUIA_Canvas_Picked || g->opg_AttrID == MUIA_Canvas_Zoomed) { *g->opg_Storage = TRUE; return TRUE; }
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 120; m->MinMaxInfo->DefWidth += 640; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += 80; m->MinMaxInfo->DefHeight += 480; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    d = INST_DATA(cl, obj);
    if (d->handlers) return TRUE;
    theme_setup(obj);
    theme_handler(obj, &d->ehn);
    DoMethod(_app(obj), MUIM_Application_AddInputHandler, (ULONG)&d->ihn);
    d->handlers = 1;
    return TRUE;
  case MUIM_Cleanup:
    d = INST_DATA(cl, obj);
    if (d->handlers) {
      stroke_timer_off(d, obj);
      DoMethod(_app(obj), MUIM_Application_RemInputHandler, (ULONG)&d->ihn);
      theme_cleanup(obj);
      pens_free();
      d->handlers = 0;
    }
    break;
  case MUIM_Show: ((struct Data *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: d = INST_DATA(cl, obj); d->shown = 0; d->ov_on = 0; break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) {
      d = INST_DATA(cl, obj);
      if (!cur_doc()) { theme_fill(_rp(obj), _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_CANVAS)); return 0; }
      draw_all(d, obj);
    }
    return 0;
  case MUIM_HandleEvent:
    return handle(cl, obj, (struct MUIP_HandleEvent *)msg);
  case MUIM_Canvas_Tick: {
    static ULONG ticks;
    /* (once a minute: time for main.c to see whether an autosave is due) */
    if (++ticks % 240 == 0) DoMethod(_app(obj), MUIM_Application_PushMethod, (ULONG)_app(obj), 2, MUIM_Application_ReturnID, ID_AUTOSAVE);
    }
    d = INST_DATA(cl, obj);
    if (d->shown && d->nedges && d->nedges < 6000 && !d->drag && d->view) {
      d->phase++;
      ov_hide(d, obj);
      ants(d, obj, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj));
      ov_show(d, obj);
    }
    return 0;
  case MUIM_Canvas_ScrollTo: {
    struct MUIP_Canvas_ScrollTo *m = (struct MUIP_Canvas_ScrollTo *)msg;
    DocView *v = cur_view();
    d = INST_DATA(cl, obj);
    if (!v || !d->shown) return 0;
    v->sx = (LONG)(m->x * v->zoom) - _mwidth(obj) / 2;
    v->sy = (LONG)(m->y * v->zoom) - _mheight(obj) / 2;
    draw_all(d, obj);
    return 0; }
  case MUIM_Canvas_StrokeTick: {
    Box ch;
    d = INST_DATA(cl, obj);
    if (d->stroke && d->drag == DR_STROKE) { stroke_tick(d->stroke, &ch); d->sbox = box_union(d->sbox, ch); mask_painted(cur_doc()); update_box(d, obj, ch); }
    return 0; }
  case MUIM_Canvas_Refresh:
    d = INST_DATA(cl, obj);
    stroke_timer_off(d, obj);
    if (d->stroke) { stroke_end(d->stroke); d->stroke = NULL; }
    d->drag = DR_NONE;
    comp_free(d);
    d->edges_ok = 0;
    if (!cur_doc()) { d->crop_on = 0; d->poly_on = 0; crop_pending = 0; }
    /* 0.7: a transform or lasso begun on another picture (or one changed under it) dropped */
    if (d->xf_on && d->xf_doc != cur_doc()) { d->xf_on = 0; xform_pending = 0; DoMethod(optbar, MUIM_Opts_Refresh); }
    if (d->ml) { maglasso_free(d->ml); d->ml = NULL; d->mlpath = NULL; d->nmlpath = 0; }
    if (d->shown) {
      if (cur_doc()) draw_all(d, obj);
      else MUI_Redraw(obj, MADF_DRAWOBJECT);
    }
    return 0;
  case MUIM_Canvas_Update: {
    struct MUIP_Canvas_Update *m = (struct MUIP_Canvas_Update *)msg;
    update_box(INST_DATA(cl, obj), obj, box_make(m->x0, m->y0, m->x1, m->y1));
    return 0;
  }
  case MUIM_Canvas_Zoom: {
    struct MUIP_Canvas_Zoom *m = (struct MUIP_Canvas_Zoom *)msg;
    Doc *doc = cur_doc();
    d = INST_DATA(cl, obj);
    if (!doc || !d->shown) return 0;
    if (m->how == 1 || m->how == -1) zoom_to(d, obj, zoom_step(d->z, (int)m->how), -1, -1);
    else if (m->how == 2 || m->how == 3) {
      double fx = (double)(_mwidth(obj) - 8) / doc->w, fy = (double)(_mheight(obj) - 8) / doc->h;
      zoom_to(d, obj, m->how == 2 ? (fx < fy ? fx : fy) : (fx > fy ? fx : fy), -1, -1);
    } else zoom_to(d, obj, m->permille / 1000.0, -1, -1);
    return 0;
  }
  case MUIM_Canvas_Transform: {
    struct MUIP_Canvas_Transform *m = (struct MUIP_Canvas_Transform *)msg;
    d = INST_DATA(cl, obj);
    if (d->xf_on) xf_finish(d, obj, 0);
    if (d->ml) ml_finish(d, obj, 0);
    xf_start(d, obj, (int)m->mode, (int)m->selection);
    return 0; }
  case MUIM_Canvas_Commit:
    d = INST_DATA(cl, obj);
    if (d->xf_on) xf_finish(d, obj, 1);
    else if (d->ml) ml_finish(d, obj, 1);
    else if (d->poly_on) poly_finish(d, obj, 1);
    else if (d->crop_on) crop_apply(d, obj, 1);
    return 0;
  case MUIM_Canvas_Cancel: case MUIM_Canvas_Tool:
    d = INST_DATA(cl, obj);
    ov_hide(d, obj);
    if (d->poly_on) { d->poly_on = 0; d->npoly = 0; }
    if (d->crop_on && (msg->MethodID == MUIM_Canvas_Cancel || tool != TL_CROP)) { d->crop_on = 0; crop_pending = 0; }
    if (d->xf_on) { d->xf_on = 0; xform_pending = 0; DoMethod(optbar, MUIM_Opts_Refresh); }
    if (d->ml && (msg->MethodID == MUIM_Canvas_Cancel || tool != TL_MAGLASSO)) { maglasso_free(d->ml); d->ml = NULL; d->mlpath = NULL; d->nmlpath = 0; }
    d->has_last = 0;
    ov_show(d, obj);
    return 0;
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *canvas_class(void)
{
  if (!ccls) ccls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct Data), (APTR)dispatch);
  return ccls;
}
void canvas_class_free(void)
{
  if (ccls) { MUI_DeleteCustomClass(ccls); ccls = NULL; }
}
