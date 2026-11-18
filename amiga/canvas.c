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
#include "../core/menu3.h"
#include "m3ids.h"
#include "../core/layers.h"
#include "brushwin.h"
#include "../core/guides.h"
#include "theme.h"
#include "screenpick.h"
#include "ctxmui.h"
#include "lang.h"
#include "../core/retouch.h"
#include "ids.h"
#include "../core/text.h"
#include "../core/channels.h"
#include "../core/xform.h"
#include "../core/selx.h"
#include "../core/maglasso.h"
#include "../core/crop.h"
#include "../core/type.h"
#include "../core/path.h"
#include "tools2.h"                     /* 1.2 */
#include "tools2ids.h"
#include "../core/selsmart.h"
#include "../core/cafill.h"
#include "../core/layerx.h"
#include "../core/pathops.h"
#include "../core/shape.h"
#include "../core/live.h"
#include "../core/toolopts.h"
#include "colorui.h"                  /* 1.2: proof colours, the gamut warning, the display profile */
#include "../core/viewx.h"                 /* 1.2 */
#include "../core/guides.h"
#include "mxids.h"

char canvas_info[96];
CanvasPoint canvas_pt;
int crop_pending;
int pcrop_pending;                       /* 1.2: Perspective Crop's corners are out (the right button's Crop and Cancel) */
int xform_pending;
char xform_info[80];
int ruler_on;
double ruler_xy[4];
char ruler_info[96];
int extras_hidden;                       /* (1.1, easel.h) */
static Doc *ruler_doc;                   /* the picture the Ruler's line was drawn on */

enum { DR_NONE, DR_STROKE, DR_MARQUEE, DR_LASSO, DR_MOVE, DR_PAN, DR_LINE, DR_SHAPE, DR_CROP, DR_CROPMOVE, DR_PICK,
       DR_XFORM, DR_RULER, DR_SAMPLE,
       DR_TYPEBOX, DR_TYPESEL, DR_PATH,
       DR_STRAIGHTEN, DR_ZOOMSCRUB,
       DR_QSEL, DR_ARTHIST, DR_PCROP, DR_ANMOVE, DR_ROTVIEW, DR_CROPROT };     /* 1.2 */                   /* 1.1: the Crop tool's straightening line; scrubby zoom */              /* 0.9: a paragraph box dragged out, text selected, a path edited */
/* 0.9: what a path tool's drag does */
enum { PE_NONE, PE_HANDLES, PE_ANCHOR, PE_SELMOVE, PE_HANDLE, PE_SUBMOVE, PE_BOX };
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
  double zy;                             /* 1.2: the vertical zoom (z times the pixel aspect when it is corrected) */
  int ov_ledges; Box ov_lbox;            /* 1.2: Show > Layer Edges: the active layer's box in the overlay drawn */
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
  int xf_kind;                           /* 0.9: 0 pixels (or the selection), 1 a type or shape layer's settings, 2 the active path */
  UWORD qual;                            /* the qualifiers at the last pointer move */
  Box mbox;                              /* the marquee's box as its style makes it */
  /* the Magnetic Lasso, its live path (the lasso's own buffer, good until the next track) and its op */
  MagLasso *ml; double *mlpath; int nmlpath, ml_op;
  int ruler_end;                         /* the Ruler's end being dragged (0 the start, 1 the end) */
  int ov_ruler;                          /* the Ruler's line is in the overlay drawn */
  int ov_tool;                           /* the tool whose brush outline the overlay drawn has */
  /* 0.8: the colour samplers in the overlay drawn (image pixels), and the one being dragged */
  LONG ov_sx[MAXSAMPLERS], ov_sy[MAXSAMPLERS]; int ov_nsamp, samp_drag;
  int ov_cursor;                         /* the painting cursor drawn: 0 outline, 1 crosshair, 2 both */
  /* 0.8: a spring-loaded tool key: the key held, the tool before it, when it went down, used meanwhile */
  UWORD spring_code; int spring_prev, spring_used; ULONG spring_secs, spring_micros;
  /* 0.9: a shape's outline while it is dragged out (image points), and whether it is in the overlay */
  Poly prev; int prev_on, ov_prev;
  /* 0.9: the active path as drawn in the overlay: its outline and its anchors (a copy, made again
     when path_dirty is set), with or without anchors and handles */
  Poly pp; PathAnchor *pa; int npa, capa, path_dirty, ov_path, ov_anchors, ov_handles;
  /* the Pen tool's open subpath being drawn; a path tool's drag: what, which anchor or handle, the
     last point; an undo step open for the drag */
  int pen_on, pen_sub; Doc *pen_doc;
  int pe_what, pe_s, pe_k, pe_which, pe_alone, pe_moved; double pe_lx, pe_ly; Doc *pe_doc;
  int ov_type;                           /* the Type tool's caret and selection are in the overlay drawn */
  /* 1.1: the crop overlay drawn; the Move tool's transform box drawn (and the layer it was worked
     out for); scrubby zoom's start */
  int ov_crop, ov_bounds; Box bbox; Doc *bb_doc; void *bb_undo; int bb_active, bb_nlay; u8 *bb_px; Box ov_bbox;
  double zs_z; LONG zs_mx, zs_my; int zs_moved;
  int tilde;                             /* 1.1: the ` key held: the Brush erases */
  int shape_centre;                      /* 1.1: Alt held while a shape is dragged out: from its centre */
  /* 1.2: the view's turn (Rotate View; zy, the vertical zoom, is above); the new tools' state (tools2.h) */
  double rot;
  int flip;                             /* 1.3: View > Flip Horizontal (the view mirrored) */
  double pq[8]; int pq_on, pq_handle;    /* Perspective Crop's corners (image), clockwise from the top left */
  int qs_open, ah_open; u32 ah_seed; double ah_x, ah_y;   /* Quick Selection's and Art History's steps in one */
  int an_pick;                           /* the note, count marker or slice being dragged */
  double rot0, rotstart;                 /* Rotate View's drag: the angle when it began, and the pointer's */
  int ab_pick;                           /* the artboard being moved */
};

static struct MUI_CustomClass *ccls;
static const double zooms[] = { 1.0 / 32, 1.0 / 24, 1.0 / 16, 1.0 / 12, 1.0 / 8, 1.0 / 6, 1.0 / 4, 1.0 / 3, 1.0 / 2, 2.0 / 3, 1, 1.5, 2, 3, 4, 5, 6, 8, 12, 16, 24, 32 };
#define NZOOMS (int)(sizeof zooms / sizeof zooms[0])

static int brushlike(int t) { return t == TL_BRUSH || t == TL_PENCIL || t == TL_ERASER || t == TL_CLONE || (t >= TL_BLUR && t <= TL_SPONGE) || (t >= TL_PATTERN && t <= TL_BGERASE) ||
                                     t == TL_HEAL || t == TL_SPOTHEAL || t == TL_MIXER || t == TL_REMOVE; }   /* (1.2: the Mixer Brush, Remove) */
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
  case TL_HEAL: return PT_HEAL; case TL_SPOTHEAL: return PT_SPOTHEAL;     /* 1.0 */
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
  d->zy = d->z * par_view_ratio(v->doc, vx_par_correct);
  d->rot = v->vrot;
  d->flip = m3_flip;
  iw = (LONG)ceil(v->doc->w * d->z); ih = (LONG)ceil(v->doc->h * d->zy);
  if (iw <= W) { v->sx = 0; d->ox = (W - iw) / 2; }
  else { if (v->sx > iw - W) v->sx = iw - W; if (v->sx < 0) v->sx = 0; d->ox = -v->sx; }
  if (ih <= H) { v->sy = 0; d->oy = (H - ih) / 2; }
  else { if (v->sy > ih - H) v->sy = ih - H; if (v->sy < 0) v->sy = 0; d->oy = -v->sy; }
}
/* window pixel to image (fractional) and back */
static double to_ix(struct Data *d, Object *obj, LONG mx) { return (mx - _mleft(obj) - d->ox) / d->z; }
static double to_iy(struct Data *d, Object *obj, LONG my) { return (my - _mtop(obj) - d->oy) / d->zy; }
static LONG to_sx(struct Data *d, Object *obj, double ix) { return _mleft(obj) + d->ox + (LONG)floor(ix * d->z + 0.5); }
static LONG to_sy(struct Data *d, Object *obj, double iy) { return _mtop(obj) + d->oy + (LONG)floor(iy * d->zy + 0.5); }
/* 1.2: a window point to the image with the view turned (Rotate View): about the area's middle */
#define TURNED(d) ((d)->rot || (d)->flip)
static void to_img(struct Data *d, Object *obj, LONG mx, LONG my, double *ix, double *iy)
{
  if (d->flip) mx = 2 * _mleft(obj) + _mwidth(obj) - 1 - mx;     /* (1.3: Flip Horizontal, then the turn) */
  if (d->rot) {
    double cx = _mleft(obj) + _mwidth(obj) / 2.0, cy = _mtop(obj) + _mheight(obj) / 2.0, sx, sy;
    view_rotate(d->rot, mx - cx, my - cy, &sx, &sy);
    mx = (LONG)floor(cx + sx + 0.5); my = (LONG)floor(cy + sy + 0.5);
  }
  *ix = to_ix(d, obj, mx); *iy = to_iy(d, obj, my);
}
static void update_props(struct Data *d, Object *obj)
{
  DocView *v = cur_view();
  if (!v || !v->doc) return;
  if (d->vprop) SetAttrs(d->vprop, MUIA_NoNotify, TRUE, MUIA_Prop_Entries, (LONG)ceil(v->doc->h * d->zy), MUIA_Prop_Visible, _mheight(obj), MUIA_Prop_First, v->sy, TAG_DONE);
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
  view_composite(doc, doc_box(doc), d->comp, (long)doc->w * 4);   /* (1.2: with onion skins when they are on) */
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
    view_composite(doc, box_make((i32)ix0, (i32)iy, (i32)ix1, (i32)iy + 1), d->rowbuf + ix0 * 4, (long)doc->w * 4);
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
  LONG ix1 = d->ox + (LONG)(doc->w * d->z), iy1 = d->oy + (LONG)(doc->h * d->zy), x, y;
  int i, k;
  if (extras_hidden) return;                             /* (1.1: View > Extras off) */
  if (ix1 > x1) ix1 = x1;
  if (iy1 > y1) iy1 = y1;
  if (g->show_grid && ix0 < ix1 && iy0 < iy1) {
    for (k = 0; k < 2; k++) {
      double zk = k == 0 ? d->z : d->zy;
      double step, p = grid_first(doc, k == 0, k == 0 ? (ix0 - d->ox) / d->z : (iy0 - d->oy) / d->zy, 1, &step);
      if (step * zk < 4) continue;                     /* (too close together to see) */
      for (; ; p += step) {
        LONG s = (LONG)floor(p * zk) + (k == 0 ? d->ox : d->oy);
        if (s >= (k == 0 ? ix1 : iy1)) break;
        /* (1.2: in the preferences' line style) */
        if (k == 0) { if (s >= ix0) for (y = iy0; y < iy1; y++) { if (line_style_on(prefs.grid_style, y - d->oy)) vpix(d, s, y, prefs.grid_color, 1); } }
        else if (s >= iy0) for (x = ix0; x < ix1; x++) if (line_style_on(prefs.grid_style, x - d->ox)) vpix(d, x, s, prefs.grid_color, 1);
      }
    }
  }
  if (g->show_guides) {
    for (i = 0; i < g->nv; i++) { LONG s = (LONG)floor(g->v[i] * d->z) + d->ox; if (s >= x0 && s < x1) for (y = y0; y < y1; y++) if (line_style_on(prefs.guide_style, y)) vpix(d, s, y, prefs.guide_color, 0); }
    for (i = 0; i < g->nh; i++) { LONG s = (LONG)floor(g->h[i] * d->zy) + d->oy; if (s >= y0 && s < y1) for (x = x0; x < x1; x++) if (line_style_on(prefs.guide_style, x)) vpix(d, x, s, prefs.guide_color, 0); }
  }
  if (g->show_rulers) {
    for (k = 0; k < 2; k++) {
      LONG a0 = k == 0 ? x0 : y0, a1 = k == 0 ? x1 : y1, b0 = k == 0 ? y0 : x0, b1 = k == 0 ? y1 : x1, off = k == 0 ? d->ox : d->oy, s, b;
      double p, zk = k == 0 ? d->z : d->zy;
      int minor, major = ruler_step(zk, 50, &minor);
      double mstep = (double)major / (minor > 0 ? minor : 1);
      if (b0 >= RULER) continue;
      for (s = a0; s < a1; s++) for (b = b0; b < b1 && b < RULER; b++) { if (k == 0) vpix(d, s, b, b == RULER - 1 ? 0x606060 : 0xE8E8E8, 0); else vpix(d, b, s, b == RULER - 1 ? 0x606060 : 0xE8E8E8, 0); }
      for (p = floor((a0 - off) / zk / mstep) * mstep; ; p += mstep) {
        LONG len;
        s = (LONG)floor(p * zk) + off;
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
    unit_text(buf, p, doc->w, doc->dpi, 0);             /* (0.8: in the preferences' units) */
    Move(rp, _mleft(obj) + s + 2, _mtop(obj) + rp->TxBaseline + 1); Text(rp, (STRPTR)buf, strlen(buf));
  }
  major = ruler_step(d->zy, 50, &minor);
  for (p = floor(-d->oy / d->zy / major) * major; (LONG)(p * d->zy) + d->oy < d->vh; p += major) {
    LONG s = (LONG)floor(p * d->zy) + d->oy;
    int c, n;
    if (s < RULER) continue;
    unit_text(buf, p, doc->h, doc->dpi, 0);
    /* (the digits one under another, as the ruler is narrow) */
    for (c = 0, n = (int)strlen(buf); c < n; c++) { Move(rp, _mleft(obj) + 3, _mtop(obj) + s + 2 + c * rp->TxHeight + rp->TxBaseline); Text(rp, (STRPTR)buf + c, 1); }
  }
  MUI_RemoveClipping(muiRenderInfo(obj), (struct Region *)clip);
}
/* 1.2: the view turned (Rotate View): each of the area's pixels from the image point under it once
   turned back about the area's middle (needs the whole composite) */
static void render_turned(struct Data *d, Doc *doc, const CmsXform *vxf, LONG x0, LONG y0, LONG x1, LONG y1)
{
  ULONG bc = board();
  double a = -d->rot * 3.14159265358979 / 180, ca = cos(a), sa = sin(a), cx = d->vw / 2.0, cy = d->vh / 2.0;
  LONG x, y, rs;
  for (y = y0; y < y1; y++) {
    UBYTE *o = d->view + (y * d->vw + x0) * 3;
    rs = -1;                             /* (where the run of the picture's pixels began, for the view transform) */
    for (x = x0; x < x1; x++, o += 3) {
      LONG ux = d->flip ? d->vw - 1 - x : x;                       /* (1.3: Flip Horizontal) */
      double sx = cx + (ux - cx) * ca - (y - cy) * sa, sy = cy + (ux - cx) * sa + (y - cy) * ca;
      double fx = floor((sx - d->ox) / d->z), fy = floor((sy - d->oy) / d->zy);
      const u8 *p;
      int al;
      u32 c;
      if (vx_tile) { fx = (double)wrap_coord((long)fx, doc->w); fy = (double)wrap_coord((long)fy, doc->h); }   /* (Pattern Preview) */
      if (fx < 0 || fy < 0 || fx >= doc->w || fy >= doc->h) {
        o[0] = (UBYTE)(bc >> 16); o[1] = (UBYTE)(bc >> 8); o[2] = (UBYTE)bc;
        if (vxf && rs >= 0) { cms_xform_apply_rgb(vxf, d->view + (y * d->vw + rs) * 3, x - rs); rs = -1; }
        continue;
      }
      if (rs < 0) rs = x;
      p = d->comp + ((long)fy * doc->w + (long)fx) * 4;
      al = p[3];
      if (al == 255) { o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; continue; }
      /* (the preferences' checkerboard, fixed to the picture as on a straight view) */
      c = checker_at((long)floor(sx - d->ox), (long)floor(sy - d->oy), prefs.checker_size, prefs.checker_scheme, prefs.checker_c1, prefs.checker_c2);
      o[0] = (UBYTE)((p[0] * al + (int)(c >> 16) * (255 - al) + 127) / 255);
      o[1] = (UBYTE)((p[1] * al + (int)((c >> 8) & 255) * (255 - al) + 127) / 255);
      o[2] = (UBYTE)((p[2] * al + (int)(c & 255) * (255 - al) + 127) / 255);
    }
    /* (1.2: through the view transform, as a straight view's rows are) */
    if (vxf && rs >= 0) cms_xform_apply_rgb(vxf, d->view + (y * d->vw + rs) * 3, x1 - rs);
  }
}
/* 1.2: the notes (small sheets in their colour), count marks (crosses) and slices (outlines) drawn into
   the view's pixels; their numbers are drawn after (annot_labels) */
static void view_box(struct Data *d, LONG x0, LONG y0, LONG x1, LONG y1, LONG cx0, LONG cy0, LONG cx1, LONG cy1, ULONG c, int fill)
{
  LONG x, y;
  for (y = y0 < cy0 ? cy0 : y0; y <= y1 && y < cy1; y++)
    for (x = x0 < cx0 ? cx0 : x0; x <= x1 && x < cx1; x++)
      if (fill || x == x0 || x == x1 || y == y0 || y == y1) vpix(d, x, y, c, 0);
}
static void annots_render(struct Data *d, Doc *doc, LONG x0, LONG y0, LONG x1, LONG y1)
{
  Annots *a = doc->annot;
  int i;
  if (extras_hidden || TURNED(d)) return;
  if (a->show_slices)
    for (i = 0; i < a->nslice; i++) {
      Box b = a->slice[i].b;
      view_box(d, d->ox + (LONG)floor(b.x0 * d->z), d->oy + (LONG)floor(b.y0 * d->zy), d->ox + (LONG)floor(b.x1 * d->z) - 1, d->oy + (LONG)floor(b.y1 * d->zy) - 1,
               x0, y0, x1, y1, i == slice_pick ? 0xFF8000 : 0x2060FF, 0);
    }
  if (a->show_count)
    for (i = 0; i < a->nmark; i++) {
      const CountGroup *g = &a->group[a->mark[i].group];
      LONG sx = d->ox + (LONG)floor((a->mark[i].x + 0.5) * d->z), sy = d->oy + (LONG)floor((a->mark[i].y + 0.5) * d->zy), r = g->size + 2, k;
      if (!g->visible) continue;
      for (k = -r; k <= r; k++) {
        if (sx + k >= x0 && sx + k < x1 && sy >= y0 && sy < y1) vpix(d, sx + k, sy, g->color, 0);
        if (sy + k >= y0 && sy + k < y1 && sx >= x0 && sx < x1) vpix(d, sx, sy + k, g->color, 0);
      }
    }
  if (a->show_notes)
    for (i = 0; i < a->nnote; i++) {
      LONG sx = d->ox + (LONG)floor(a->note[i].x * d->z), sy = d->oy + (LONG)floor(a->note[i].y * d->zy);
      view_box(d, sx, sy, sx + 11, sy + 11, x0, y0, x1, y1, a->note[i].color, 1);
      view_box(d, sx, sy, sx + 11, sy + 11, x0, y0, x1, y1, 0x404040, 0);
      view_box(d, sx + 3, sy + 4, sx + 8, sy + 4, x0, y0, x1, y1, 0x404040, 1);
      view_box(d, sx + 3, sy + 7, sx + 8, sy + 7, x0, y0, x1, y1, 0x404040, 1);
    }
}
/* the count marks' and slices' numbers, over the view once it is on the screen */
static void annot_labels(struct Data *d, Object *obj)
{
  Doc *doc = cur_doc();
  Annots *a = doc ? doc->annot : NULL;
  struct RastPort *rp = _rp(obj);
  APTR clip;
  char buf[12];
  int i;
  if (!a || extras_hidden || TURNED(d) || !d->shown || (!(a->show_count && a->nmark) && !(a->show_slices && a->nslice))) return;
  clip = (APTR)MUI_AddClipping(muiRenderInfo(obj), _mleft(obj), _mtop(obj), _mwidth(obj), _mheight(obj));
  SetFont(rp, theme_small());
  SetDrMd(rp, JAM1);
  if (a->show_count)
    for (i = 0; i < a->nmark; i++) {
      const CountGroup *g = &a->group[a->mark[i].group];
      if (!g->visible) continue;
      sprintf(buf, "%d", count_number(doc, i));
      SetAPen(rp, tpen(T_RED));
      Move(rp, to_sx(d, obj, a->mark[i].x + 0.5) + g->size + 4, to_sy(d, obj, a->mark[i].y + 0.5) - 2); Text(rp, (STRPTR)buf, strlen(buf));
    }
  if (a->show_slices) {
    SetAPen(rp, tpen(T_ACCENT));
    for (i = 0; i < a->nslice; i++) {
      sprintf(buf, "%02d", i + 1);
      Move(rp, to_sx(d, obj, a->slice[i].b.x0) + 3, to_sy(d, obj, a->slice[i].b.y0) + rp->TxBaseline + 2); Text(rp, (STRPTR)buf, strlen(buf));
    }
  }
  MUI_RemoveClipping(muiRenderInfo(obj), (struct Region *)clip);
}
static void render(struct Data *d, LONG x0, LONG y0, LONG x1, LONG y1)
{
  Doc *doc = d->comp ? d->doc : cur_doc();
  LONG cx0 = -1, cx1 = -1;
  LONG x, y, sx0, sx1;
  const CmsXform *vxf;
  ULONG bc = board();
  UBYTE br = (UBYTE)(bc >> 16), bg = (UBYTE)(bc >> 8), bb = (UBYTE)bc;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > d->vw) x1 = d->vw;
  if (y1 > d->vh) y1 = d->vh;
  if (x1 <= x0 || y1 <= y0) return;
  vxf = doc ? colorui_view(doc) : NULL;          /* (1.2) */
  if (TURNED(d) && d->comp && doc) { render_turned(d, doc, vxf, x0, y0, x1, y1); return; }   /* 1.2: Rotate View */
  for (x = x0; x < x1; x++) {
    double ix = floor((x - d->ox) / d->z);
    if (vx_tile && doc) ix = (double)wrap_coord((long)ix, doc->w);     /* (1.2: Pattern Preview tiles the picture) */
    d->xmap[x] = doc && ix >= 0 && ix < doc->w ? (LONG)ix : -1;
    if (d->xmap[x] >= 0) { if (cx0 < 0 || d->xmap[x] < cx0) cx0 = d->xmap[x]; if (d->xmap[x] + 1 > cx1) cx1 = d->xmap[x] + 1; }
  }
  d->rowdoc = NULL;                    /* (the row cache is for this pass's columns only) */
  for (y = y0; y < y1; y++) {
    UBYTE *o = d->view + (y * d->vw + x0) * 3;
    double iyf = floor((y - d->oy) / d->zy);
    LONG iy;
    if (vx_tile && doc) iyf = (double)wrap_coord((long)iyf, doc->h);
    iy = doc && iyf >= 0 && iyf < doc->h ? (LONG)iyf : -1;
    const u8 *row = iy < 0 ? NULL : d->comp ? d->comp + (long)iy * doc->w * 4 : comp_row(d, doc, iy, cx0, cx1);
    for (x = x0; x < x1; x++, o += 3) {
      LONG ix = d->xmap[x];
      if (!row || ix < 0) { o[0] = br; o[1] = bg; o[2] = bb; continue; }
      {
        const u8 *p = row + ix * 4;
        int a = p[3];
        if (a == 255) { o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; }
        else {
          /* over the checkerboard, fixed to the image (1.2: its size and colours from the preferences) */
          u32 c = checker_at(x - d->ox, y - d->oy, prefs.checker_size, prefs.checker_scheme, prefs.checker_c1, prefs.checker_c2);
          o[0] = (UBYTE)((p[0] * a + (int)(c >> 16) * (255 - a) + 127) / 255);
          o[1] = (UBYTE)((p[1] * a + (int)((c >> 8) & 255) * (255 - a) + 127) / 255);
          o[2] = (UBYTE)((p[2] * a + (int)(c & 255) * (255 - a) + 127) / 255);
        }
        /* 0.7: in Quick Mask mode, what isn't selected under half-strength red */
        if (doc->qmask) {
          int t = (255 - doc->qmask[(long)iy * doc->w + ix]) / 2;
          if (t) { o[0] = (UBYTE)(o[0] + ((255 - o[0]) * t + 127) / 255); o[1] = (UBYTE)((o[1] * (255 - t) + 127) / 255); o[2] = (UBYTE)((o[2] * (255 - t) + 127) / 255); }
        }
      }
    }
    /* 1.2: the picture's part of the row through the view transform (proofing, gamut warning, its profile) */
    if (vxf && row) {
      for (sx0 = x0; sx0 < x1 && d->xmap[sx0] < 0; sx0++) ;
      for (sx1 = x1; sx1 > sx0 && d->xmap[sx1 - 1] < 0; sx1--) ;
      if (sx1 > sx0) cms_xform_apply_rgb(vxf, d->view + (y * d->vw + sx0) * 3, sx1 - sx0);
    }
  }
  /* 1.2: the pixel grid from 800% up, a thin line where each image pixel starts */
  if (doc && (vx_pixgrid || m3_pixelart) && !extras_hidden && d->z >= PIXGRID_MIN_ZOOM) {   /* (1.3: and with Pixel Art Preview) */
    ULONG pg = theme_is_dark() ? 0x505050 : 0xB0B0B0;
    for (y = y0; y < y1; y++) {
      int rowline = pixgrid_line(y, d->oy, d->zy);
      for (x = x0; x < x1; x++) if (d->xmap[x] >= 0 && (rowline || pixgrid_line(x, d->ox, d->z))) vpix(d, x, y, pg, 1);
    }
  }
  if (doc) guides_render(d, doc, x0, y0, x1, y1);
  if (doc && doc->annot) annots_render(d, doc, x0, y0, x1, y1);   /* 1.2: notes, counts, slices */
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
  if (!d->nedges || !d->edges || extras_hidden || !vx_seledges || TURNED(d)) return;   /* (1.2: View > Show > Selection Edges; not on a turned view) */
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
/* ------------------------------------------------------------------ 0.9: what the overlay shows of paths and type */
/* the Type tool's editing: the layer (found again by its pointer, as the Layers panel may move it),
   whether this editing made it, the undo step that did, the settings being edited and as they
   were, their layout, the caret and the other end of the selection */
int type_editing;
static struct {
  Doc *doc; Layer *lay;
  int isnew, mask;                       /* made by this editing; the Type Mask tool's (a selection at the end) */
  void *mark;                            /* d->undo after the New Type Layer step */
  TextParams *tp, *orig;
  TextLayout *tl;
  int caret, anchor, changed;
  int o_font, o_style, o_size, o_aa, o_align;   /* the options bar as last applied */
} ty;
static void ty_end(struct Data *d, Object *obj, int how);
/* the path the Paths panel has chosen (make: the work path, made if needed, when none is) */
static Path *cur_path(Doc *doc, int make)
{
  if (!doc) return NULL;
  if (!doc->path_active && make) doc->path_active = -1;
  if (doc->path_active > doc->npaths) doc->path_active = 0;
  if (!doc->path_active) return NULL;
  return doc_path(doc, doc->path_active < 0 ? -1 : doc->path_active - 1, make);
}
/* the active path copied for the overlay when it has changed: its outline flattened for this zoom
   and its anchors; what is shown depends on the tool (anchors with the path tools, handles of the
   chosen anchors except with Path Selection) */
static void path_snapshot(struct Data *d)
{
  Doc *doc = cur_doc();
  Path *p = cur_path(doc, 0);
  int t = d->ov_tool;
  d->ov_path = p && p->na > 0 && ((vx_path && !extras_hidden) || PATH_TOOL(t));   /* (1.2: Show > Target Path) */
  d->ov_anchors = PATH_TOOL(t);
  d->ov_handles = PATH_TOOL(t) && t != TL_PATHSEL;
  if (!d->ov_path || !d->path_dirty) return;
  d->path_dirty = 0;
  poly_free(&d->pp); poly_init(&d->pp);
  if (!path_flatten(p, 0.7 / d->z, &d->pp)) { poly_free(&d->pp); poly_init(&d->pp); }
  if (d->capa < p->na) {
    PathAnchor *n = (PathAnchor *)realloc(d->pa, (size_t)p->na * sizeof *n);
    if (!n) { d->npa = 0; return; }
    d->pa = n; d->capa = p->na;
  }
  memcpy(d->pa, p->a, (size_t)p->na * sizeof *d->pa);
  d->npa = p->na;
}
/* the path copy drawn in COMPLEMENT: the outline, the anchors as small squares (filled when chosen)
   and the chosen anchors' handles */
static void path_overlay(struct Data *d, Object *obj, struct RastPort *rp)
{
  int c, st = 0, i;
  for (c = 0; c < d->pp.nc; c++) {
    LONG lx = -32000, ly = 0;
    int en = d->pp.end[c];
    xpath(d, obj, rp, d->pp.xy + 2 * st, en - st, &lx, &ly);
    if (d->pp.closed[c] && en - st > 2) xpath(d, obj, rp, d->pp.xy + 2 * st, 1, &lx, &ly);
    st = en;
  }
  if (!d->ov_anchors) return;
  for (i = 0; i < d->npa; i++) {
    const PathAnchor *a = &d->pa[i];
    LONG x = to_sx(d, obj, a->x), y = to_sy(d, obj, a->y);
    /* (Path Selection shows only the chosen subpath's anchors, as does a path of very many, such as text's outlines) */
    if (!a->sel && (!d->ov_handles || d->npa > 300)) continue;
    if (a->sel) RectFill(rp, x - 2, y - 2, x + 2, y + 2); else xrect(rp, x - 2, y - 2, x + 2, y + 2);
    if (a->sel && d->ov_handles) {
      int h;
      for (h = 0; h < 2; h++) {
        double hx = h ? a->ox : a->ix, hy = h ? a->oy : a->iy;
        LONG sx = to_sx(d, obj, hx), sy = to_sy(d, obj, hy);
        if (labs(sx - x) + labs(sy - y) < 4) continue;
        xline(rp, x, y, sx, sy);
        RectFill(rp, sx - 1, sy - 1, sx + 1, sy + 1);
      }
    }
  }
}
/* the Type tool's caret (or its selection, lit) and a paragraph's box, in COMPLEMENT */
static void type_overlay(struct Data *d, Object *obj, struct RastPort *rp)
{
  double q[8 * 64], x0, y0, x1, y1;
  int a = ty.anchor < ty.caret ? ty.anchor : ty.caret, b = ty.anchor < ty.caret ? ty.caret : ty.anchor, n, i, k;
  if (ty.tp->box) {
    /* the box (through the layer's transform) */
    double bx[4], by[4];
    const double *m = ty.tp->xf;
    for (k = 0; k < 4; k++) {
      double px = ty.tp->x + (k == 1 || k == 2 ? ty.tp->bw : 0), py = ty.tp->y + (k >= 2 ? ty.tp->bh : 0);
      bx[k] = m[0] * px + m[1] * py + m[2]; by[k] = m[3] * px + m[4] * py + m[5];
    }
    SetDrPt(rp, 0xCCCC);
    for (k = 0; k < 4; k++) xline(rp, to_sx(d, obj, bx[k]), to_sy(d, obj, by[k]), to_sx(d, obj, bx[(k + 1) % 4]), to_sy(d, obj, by[(k + 1) % 4]));
    SetDrPt(rp, 0xFFFF);
  }
  if (a < b) {
    n = text_sel_quads(ty.tl, a, b, q, 64);
    for (i = 0; i < n; i++) {
      const double *c = q + 8 * i;
      LONG sx[4], sy[4];
      for (k = 0; k < 4; k++) { sx[k] = to_sx(d, obj, c[2 * k]); sy[k] = to_sy(d, obj, c[2 * k + 1]); }
      if ((sy[0] == sy[1] && sx[1] == sx[2]) || (sx[0] == sx[1] && sy[1] == sy[2])) {
        /* square on the screen: filled */
        LONG l = sx[0], r = sx[0], t = sy[0], bo = sy[0];
        for (k = 1; k < 4; k++) { if (sx[k] < l) l = sx[k]; if (sx[k] > r) r = sx[k]; if (sy[k] < t) t = sy[k]; if (sy[k] > bo) bo = sy[k]; }
        if (r > l && bo > t) RectFill(rp, l, t, r - 1, bo - 1);
      } else for (k = 0; k < 4; k++) xline(rp, sx[k], sy[k], sx[(k + 1) % 4], sy[(k + 1) % 4]);
    }
  } else if (text_caret_pos(ty.tl, ty.caret, &x0, &y0, &x1, &y1)) {
    LONG a0 = to_sx(d, obj, x0), b0 = to_sy(d, obj, y0), a1 = to_sx(d, obj, x1), b1 = to_sy(d, obj, y1);
    xline(rp, a0, b0, a1, b1);
    if (a0 == a1) xline(rp, a0 + 1, b0, a1 + 1, b1);        /* (two pixels wide, to be seen) */
  }
}
/* 1.1: the Move tool's transform box: the active layer's content (xform_source_box), worked out
   again only when the picture or the layer changes (a scan of the layer is slow on a 68040) */
static int move_bounds(struct Data *d, Box *b)
{
  Doc *doc = cur_doc();
  Layer *l;
  if (!doc || doc->active < 0 || doc->active >= doc->nlay) return 0;
  l = doc->lay[doc->active];
  if (l->kind != LK_PIXEL || !l->px) return 0;
  if (d->bb_doc != doc || d->bb_undo != (void *)doc->undo || d->bb_active != doc->active || d->bb_nlay != doc->nlay || d->bb_px != l->px) {
    d->bbox = xform_source_box(doc, doc->active);
    d->bb_doc = doc; d->bb_undo = (void *)doc->undo; d->bb_active = doc->active; d->bb_nlay = doc->nlay; d->bb_px = l->px;
  }
  *b = d->bbox;
  return !box_empty(d->bbox);
}
/* its eight handles on the screen: the corners, then the sides' middles */
static void bounds_handles(LONG x0, LONG y0, LONG x1, LONG y1, LONG *hx, LONG *hy)
{
  LONG mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
  hx[0] = x0; hy[0] = y0; hx[1] = x1; hy[1] = y0; hx[2] = x1; hy[2] = y1; hx[3] = x0; hy[3] = y1;
  hx[4] = mx; hy[4] = y0; hx[5] = x1; hy[5] = my; hx[6] = mx; hy[6] = y1; hx[7] = x0; hy[7] = my;
}
static void drag_box(struct Data *d, double *x0, double *y0, double *x1, double *y1);
/* 1.2: the box of what the Note, Count, Slice Select or Artboard tool is dragging */
static Box an_box(struct Data *d)
{
  Doc *doc = cur_doc();
  Annots *a = doc ? doc->annot : NULL;
  int k = d->an_pick;
  if (!doc) return box_make(0, 0, 0, 0);
  if (tool == TL_ARTBOARD) return d->ab_pick >= 0 && d->ab_pick < doc->nlay ? doc->lay[d->ab_pick]->abox : box_make(0, 0, 0, 0);
  if (!a || k < 0) return box_make(0, 0, 0, 0);
  if (tool == TL_SLICESEL && k < a->nslice) return a->slice[k].b;
  if (tool == TL_NOTE && k < a->nnote) { i32 sz = (i32)(12 / d->z) + 1; return box_make(a->note[k].x, a->note[k].y, a->note[k].x + sz, a->note[k].y + sz); }
  if (tool == TL_COUNT && k < a->nmark) { i32 sz = (i32)(4 / d->z) + 1; return box_make(a->mark[k].x - sz, a->mark[k].y - sz, a->mark[k].x + sz + 1, a->mark[k].y + sz + 1); }
  return box_make(0, 0, 0, 0);
}
/* everything in the overlay drawn (or, drawn again, taken away) */
static void ov_draw(struct Data *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  APTR clip;
  int t = d->ov_tool, i;
  if (!d->shown) return;
  clip = (APTR)MUI_AddClipping(muiRenderInfo(obj), _mleft(obj), _mtop(obj), _mwidth(obj), _mheight(obj));
  SetDrMd(rp, COMPLEMENT);
  SetDrPt(rp, 0xFFFF);
  SetAPen(rp, 1);
  /* the brush's outline (or a cross when it is too small to see); 0.8: or a crosshair, as the
     preferences ask (Caps Lock for it too) */
  if (d->cur_in && brushlike(t) && d->drag != DR_LINE) {
    LONG r = brush_r(d);
    int cross = d->ov_cursor != 0;
    if (r >= 3 && d->ov_cursor != 1) DrawEllipse(rp, d->cur_x, d->cur_y, r, (LONG)(r * d->zy / d->z + 0.5));   /* (1.2: as tall as the aspect draws it) */
    if (cross && (r >= 3 || d->ov_cursor == 1)) { xline(rp, d->cur_x - 6, d->cur_y, d->cur_x + 6, d->cur_y); xline(rp, d->cur_x, d->cur_y - 6, d->cur_x, d->cur_y - 1); xline(rp, d->cur_x, d->cur_y + 1, d->cur_x, d->cur_y + 6); }
    else if (r < 3) { xline(rp, d->cur_x - 4, d->cur_y, d->cur_x + 4, d->cur_y); xline(rp, d->cur_x, d->cur_y - 4, d->cur_x, d->cur_y - 1); xline(rp, d->cur_x, d->cur_y + 1, d->cur_x, d->cur_y + 4); }
  }
  switch (d->drag) {
  case DR_MARQUEE:
    if (tool == TL_EMARQUEE) xellipse(rp, to_sx(d, obj, d->mbox.x0), to_sy(d, obj, d->mbox.y0), to_sx(d, obj, d->mbox.x1), to_sy(d, obj, d->mbox.y1));
    else xrect(rp, to_sx(d, obj, d->mbox.x0), to_sy(d, obj, d->mbox.y0), to_sx(d, obj, d->mbox.x1), to_sy(d, obj, d->mbox.y1));
    break;
  case DR_SHAPE:
    if (d->ov_prev) break;
    {
      double x0, y0, x1, y1;
      drag_box(d, &x0, &y0, &x1, &y1);
      if (tool == TL_ELLIPSE) xellipse(rp, to_sx(d, obj, x0), to_sy(d, obj, y0), to_sx(d, obj, x1), to_sy(d, obj, y1));
      else xrect(rp, to_sx(d, obj, x0), to_sy(d, obj, y0), to_sx(d, obj, x1), to_sy(d, obj, y1));
    }
    break;
  case DR_LINE:
    if (d->ov_prev) break;
    xline(rp, to_sx(d, obj, d->ax), to_sy(d, obj, d->ay), to_sx(d, obj, d->bx), to_sy(d, obj, d->by));
    break;
  case DR_TYPEBOX:
    xrect(rp, to_sx(d, obj, d->ax), to_sy(d, obj, d->ay), to_sx(d, obj, d->bx), to_sy(d, obj, d->by));
    break;
  case DR_STRAIGHTEN:
    xline(rp, to_sx(d, obj, d->ax), to_sy(d, obj, d->ay), to_sx(d, obj, d->bx), to_sy(d, obj, d->by));
    break;
  case DR_PATH:
    if (d->pe_what == PE_BOX) xrect(rp, to_sx(d, obj, d->ax), to_sy(d, obj, d->ay), to_sx(d, obj, d->bx), to_sy(d, obj, d->by));
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
  if (d->ov_prev) {
    /* 0.9: a shape's outline as it is dragged out */
    int c, st = 0;
    for (c = 0; c < d->prev.nc; c++) {
      LONG lx = -32000, ly = 0;
      int en = d->prev.end[c];
      xpath(d, obj, rp, d->prev.xy + 2 * st, en - st, &lx, &ly);
      if (d->prev.closed[c] && en - st > 1) xpath(d, obj, rp, d->prev.xy + 2 * st, 1, &lx, &ly);
      st = en;
    }
  }
  if (d->ov_bounds) {
    /* 1.1: the Move tool's Show Transform Controls: the layer's box and its handles */
    LONG x0 = to_sx(d, obj, d->ov_bbox.x0), y0 = to_sy(d, obj, d->ov_bbox.y0), x1 = to_sx(d, obj, d->ov_bbox.x1) - 1, y1 = to_sy(d, obj, d->ov_bbox.y1) - 1, hx[8], hy[8];
    int k;
    xrect(rp, x0, y0, x1, y1);
    bounds_handles(x0, y0, x1, y1, hx, hy);
    for (k = 0; k < 8; k++) xrect(rp, hx[k] - 3, hy[k] - 3, hx[k] + 3, hy[k] + 3);
  }
  if (d->ov_ledges) {
    /* 1.2: Show > Layer Edges, dotted */
    SetDrPt(rp, 0xAAAA);
    xrect(rp, to_sx(d, obj, d->ov_lbox.x0), to_sy(d, obj, d->ov_lbox.y0), to_sx(d, obj, d->ov_lbox.x1) - 1, to_sy(d, obj, d->ov_lbox.y1) - 1);
    SetDrPt(rp, 0xFFFF);
  }
  if (d->ov_path) path_overlay(d, obj, rp);
  if (d->ov_type) type_overlay(d, obj, rp);
  for (i = 0; i < d->ov_nsamp; i++) {
    /* 0.8: a colour sampler: a ring round the point with ticks outside it, and its number */
    LONG sx = to_sx(d, obj, d->ov_sx[i] + 0.5), sy = to_sy(d, obj, d->ov_sy[i] + 0.5);
    char num[2];
    DrawEllipse(rp, sx, sy, 4, 4);
    xline(rp, sx - 9, sy, sx - 6, sy); xline(rp, sx + 6, sy, sx + 9, sy); xline(rp, sx, sy - 9, sx, sy - 6); xline(rp, sx, sy + 6, sx, sy + 9);
    num[0] = (char)('1' + i); num[1] = 0;
    SetFont(rp, theme_small());
    Move(rp, sx + 7, sy + 7 + rp->TxBaseline); Text(rp, (STRPTR)num, 1);
  }
  if (d->ov_ruler) {
    /* the Ruler's line, with a small cross at each end */
    LONG a = to_sx(d, obj, ruler_xy[0]), b = to_sy(d, obj, ruler_xy[1]), c = to_sx(d, obj, ruler_xy[2]), e = to_sy(d, obj, ruler_xy[3]);
    xline(rp, a, b, c, e);
    xline(rp, a - 4, b - 4, a - 1, b - 1); xline(rp, a + 1, b + 1, a + 4, b + 4);
    xline(rp, c - 4, e + 4, c - 1, e + 1); xline(rp, c + 1, e - 1, c + 4, e - 4);
  }
  if (d->crop_on && opt.crop_angle) {
    /* 1.2: the turned crop box */
    double c[8];
    int k;
    crop_corners((d->c0x + d->c1x) / 2, (d->c0y + d->c1y) / 2, d->c1x - d->c0x, d->c1y - d->c0y, opt.crop_angle, c);
    for (k = 0; k < 4; k++) xline(rp, to_sx(d, obj, c[k * 2]), to_sy(d, obj, c[k * 2 + 1]), to_sx(d, obj, c[(k + 1) % 4 * 2]), to_sy(d, obj, c[(k + 1) % 4 * 2 + 1]));
  } else if (d->crop_on) {
    LONG x0 = to_sx(d, obj, d->c0x), y0 = to_sy(d, obj, d->c0y), x1 = to_sx(d, obj, d->c1x) - 1, y1 = to_sy(d, obj, d->c1y) - 1, k;
    xrect(rp, x0, y0, x1, y1);
    /* the overlay (1.1: the one chosen; the rule of thirds at first), and the handles at the corners and sides */
    if (x1 - x0 > 30 && y1 - y0 > 30 && d->ov_crop) {
      double sg[4 * 64];
      int ns = crop_overlay_lines(d->ov_crop, x0, y0, x1, y1, 32, sg, 64);
      SetDrPt(rp, 0xCCCC);
      for (k = 0; k < ns; k++) xline(rp, (LONG)sg[k * 4], (LONG)sg[k * 4 + 1], (LONG)sg[k * 4 + 2], (LONG)sg[k * 4 + 3]);
      SetDrPt(rp, 0xFFFF);
    }
    for (k = 0; k < 8; k++) {
      LONG hx = k % 3 == 0 ? x0 : k % 3 == 1 ? (x0 + x1) / 2 : x1, hy = k < 3 ? y0 : k < 5 ? (y0 + y1) / 2 : y1;
      if (k == 3) hx = x0; else if (k == 4) hx = x1;
      if (k >= 5) hx = k == 5 ? x0 : k == 6 ? (x0 + x1) / 2 : x1;
      RectFill(rp, hx - 3, hy - 3, hx + 3, hy + 3);
    }
  }
  if (d->pq_on) {
    /* 1.2: Perspective Crop's quad and its corner handles */
    int k;
    for (k = 0; k < 4; k++) {
      LONG ax = to_sx(d, obj, d->pq[k * 2]), ay = to_sy(d, obj, d->pq[k * 2 + 1]);
      xline(rp, ax, ay, to_sx(d, obj, d->pq[(k + 1) % 4 * 2]), to_sy(d, obj, d->pq[(k + 1) % 4 * 2 + 1]));
      RectFill(rp, ax - 3, ay - 3, ax + 3, ay + 3);
    }
  }
  if (d->drag == DR_ANMOVE) {
    /* 1.2: a note, count mark, slice or artboard being dragged: where it would go */
    Box b = an_box(d);
    double dx = d->bx - d->ax, dy = d->by - d->ay;
    if (!box_empty(b)) xrect(rp, to_sx(d, obj, b.x0 + dx), to_sy(d, obj, b.y0 + dy), to_sx(d, obj, b.x1 + dx) - 1, to_sy(d, obj, b.y1 + dy) - 1);
  }
  SetDrMd(rp, JAM1);
  MUI_RemoveClipping(muiRenderInfo(obj), (struct Region *)clip);
}
static void ov_hide(struct Data *d, Object *obj) { if (d->ov_on) { ov_draw(d, obj); d->ov_on = 0; } }
static void ov_show(struct Data *d, Object *obj)
{
  if (d->ov_on) return;
  /* (what depends on the tool is remembered, so it is taken away later as it was put up) */
  d->ov_ruler = ruler_on && tool == TL_RULER && ruler_doc == cur_doc();
  d->ov_tool = d->space ? TL_HAND : tool;
  d->ov_cursor = prefs.caps_precise && (d->qual & IEQUALIFIER_CAPSLOCK) ? 1 : prefs.cursor;
  d->ov_prev = d->prev_on && (d->drag == DR_SHAPE || d->drag == DR_LINE);
  path_snapshot(d);
  d->ov_type = type_editing && ty.doc == cur_doc() && ty.tl;
  d->ov_crop = opt.crop_overlay;
  d->ov_bounds = tool == TL_MOVE && opt.move_bounds && !d->xf_on && d->drag != DR_MOVE && move_bounds(d, &d->ov_bbox);
  d->ov_ledges = vx_layeredges && !extras_hidden && !d->ov_bounds && !d->xf_on && d->drag != DR_MOVE && move_bounds(d, &d->ov_lbox);
  {
    DocView *v = cur_view();
    int i;
    d->ov_nsamp = v ? v->nsamp : 0;
    for (i = 0; i < d->ov_nsamp; i++) { d->ov_sx[i] = v->samp_x[i]; d->ov_sy[i] = v->samp_y[i]; }
  }
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
  d->path_dirty = 1;                     /* (0.9: the path's outline flattened again for this zoom) */
  render(d, 0, 0, d->vw, d->vh);
  d->ov_on = 0;
  blit(d, obj, 0, 0, d->vw, d->vh);
  ruler_labels(d, obj);
  annot_labels(d, obj);                  /* 1.2 */
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
  if (comp_ready(d)) view_composite(doc, b, d->comp + ((long)b.y0 * doc->w + b.x0) * 4, (long)doc->w * 4);
  x0 = (LONG)floor(b.x0 * d->z) + d->ox - 1; y0 = (LONG)floor(b.y0 * d->zy) + d->oy - 1;
  x1 = (LONG)ceil(b.x1 * d->z) + d->ox + 1; y1 = (LONG)ceil(b.y1 * d->zy) + d->oy + 1;
  if (TURNED(d)) { x0 = y0 = 0; x1 = d->vw; y1 = d->vh; }   /* (1.2: a turned view is drawn again whole) */
  if (vx_tile) { x0 = y0 = 0; x1 = d->vw; y1 = d->vh; }      /* (1.2: Pattern Preview shows the change in every tile) */
  render(d, x0, y0, x1, y1);
  ov_hide(d, obj);
  blit(d, obj, x0, y0, x1, y1);
  if (x0 < RULER || y0 < RULER) ruler_labels(d, obj);
  if (doc->annot) annot_labels(d, obj);  /* 1.2 */
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
  if (m3_pixelart) z = pixelart_zoom((long)(z * 1000 + 0.5), 0) / 1000.0;      /* 1.3: Pixel Art Preview's whole steps */
  if (mx < 0) { mx = _mleft(obj) + _mwidth(obj) / 2; my = _mtop(obj) + _mheight(obj) / 2; }
  ix = to_ix(d, obj, mx); iy = to_iy(d, obj, my);
  v->zoom = z;
  v->sx = (LONG)(ix * z) - (mx - _mleft(obj));
  v->sy = (LONG)(iy * z * par_view_ratio(v->doc, vx_par_correct)) - (my - _mtop(obj));
  draw_all(d, obj);
  set(obj, MUIA_Canvas_Zoomed, TRUE);
}
static double zoom_step(double z, int dir)
{
  int i;
  if (m3_pixelart) return pixelart_zoom((long)(z * 1000 + 0.5), dir) / 1000.0;
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
static void lasso_add(struct Data *d, double x, double y)
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
  c = doc_pick_ex(doc, (i32)x, (i32)y, sample_layers[opt.pick_from % 3], pick_sizes[opt.sample % NPICK_SIZES]);   /* (1.1) */
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
  if (apply && doc && opt.crop_angle && d->c1x - d->c0x >= 1 && d->c1y - d->c0y >= 1) {
    /* 1.2: the turned box: what is in it becomes the upright canvas */
    image_crop_rotated(doc, (d->c0x + d->c1x) / 2, (d->c0y + d->c1y) / 2, d->c1x - d->c0x, d->c1y - d->c0y, opt.crop_angle, RS_BICUBIC);
    opt.crop_angle = 0;
    changed(d, obj);
    d->edges_ok = 0;
    draw_all(d, obj);
    tell_changed(d, obj);
    return;
  }
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
/* 0.9: a path's extent as whole pixels (empty for none) */
static Box path_box(const Path *p)
{
  double x0, y0, x1, y1;
  if (!p || !path_bounds(p, &x0, &y0, &x1, &y1)) return box_make(0, 0, 0, 0);
  if (x1 - x0 < 1) x1 = x0 + 1;
  if (y1 - y0 < 1) y1 = y0 + 1;
  return box_make((i32)floor(x0), (i32)floor(y0), (i32)ceil(x1), (i32)ceil(y1));
}
/* 0.9: what Free Transform's box is for a type layer (its text) or a shape layer (its path, and
   half the stroke round it) */
static Box live_src_box(Doc *doc, int i)
{
  const TextParams *tp = layer_text(doc->lay[i]);
  const ShapeParams *sp = layer_shape(doc->lay[i]);
  if (tp) return text_bounds(tp);
  if (sp) {
    Path v = shape_path(sp);
    Box b = path_box(&v);
    i32 m = sp->stroke_on ? (i32)(sp->stroke_width / 2 + 1) : 0;
    if (!box_empty(b)) return box_make(b.x0 - m, b.y0 - m, b.x1 + m, b.y1 + m);
  }
  return box_make(0, 0, 0, 0);
}
/* the handles put up round the selection (or the layer's pixels): mode is the handles' XM_, sel
   whether only the selection's outline is to change */
static void xf_start(struct Data *d, Object *obj, int mode, int sel)
{
  Doc *doc = cur_doc();
  Box b;
  int kind = 0;
  Path *ap;
  if (!doc) return;
  if (type_editing) ty_end(d, obj, 1);
  if (!sel) {
    Layer *l = doc->lay[doc->active];
    /* 0.9: with a path tool, the active path; a type or shape layer's settings; else pixels */
    if (PATH_TOOL(tool) && (ap = cur_path(doc, 0)) && ap->na) kind = 2;
    else if (l->live && (l->kind == LK_TEXT || l->kind == LK_SHAPE)) {
      kind = 1;
      if (l->lock_all || l->lock_pos) { ask("OK", "%s", tr("The layer is locked.")); return; }
    } else {
      if (!l->px || l->kind != LK_PIXEL) { ask("OK", "%s", tr("This layer has no pixels to transform: rasterize it first.")); return; }
      if (l->locked || l->lock_all || l->lock_pos) { ask("OK", "%s", tr("The layer is locked.")); return; }
    }
  }
  b = sel ? (sel_bounds(doc, &b) ? b : box_make(0, 0, 0, 0)) : kind == 2 ? path_box(cur_path(doc, 0)) : kind == 1 ? live_src_box(doc, doc->active) : xform_source_box(doc, doc->active);
  if (box_empty(b)) { ask("OK", "%s", tr(sel ? "Nothing is selected." : "There is nothing to transform: the layer is empty.")); return; }
  ov_hide(d, obj);
  if (d->crop_on) { d->crop_on = 0; crop_pending = 0; }
  d->poly_on = 0; d->npoly = 0;
  xform_init(&d->xf, b);
  d->xf_on = 1; d->xf_sel = sel; d->xf_mode = mode; d->xf_doc = doc; d->xf_handle = -1; d->xf_kind = kind;
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
    if (d->xf_kind) {
      /* 0.9: a type or shape layer, or the active path: through the affine map taking the box's
         top left, top right and bottom left corners to the quad's */
      double m[6], w = d->xf.src.x1 - d->xf.src.x0, h = d->xf.src.y1 - d->xf.src.y0, x0 = d->xf.src.x0, y0 = d->xf.src.y0;
      const double *q = d->xf.q;
      m[0] = (q[2] - q[0]) / w; m[1] = (q[6] - q[0]) / h; m[2] = q[0] - m[0] * x0 - m[1] * y0;
      m[3] = (q[3] - q[1]) / w; m[4] = (q[7] - q[1]) / h; m[5] = q[1] - m[3] * x0 - m[4] * y0;
      if (d->xf_kind == 1) ok = doc_live_transform(doc, doc->active, m);
      else {
        doc_begin(doc, "Transform Path");
        doc_note_paths(doc);
        if ((ok = cur_path(doc, 0) != NULL)) path_transform(cur_path(doc, 0), m);
        doc_end(doc);
        d->path_dirty = 1;
      }
    } else if (!d->xf_sel && !doc->sel && doc->active >= 0 && layer_is_smart(doc->lay[doc->active])) {
      /* 1.2: a smart object: the affine part of the quad applied to its source, so nothing is lost */
      double m[6], w = d->xf.src.x1 - d->xf.src.x0, h = d->xf.src.y1 - d->xf.src.y0, x0 = d->xf.src.x0, y0 = d->xf.src.y0;
      const double *q = d->xf.q;
      m[0] = (q[2] - q[0]) / w; m[1] = (q[6] - q[0]) / h; m[2] = q[0] - m[0] * x0 - m[1] * y0;
      m[3] = (q[3] - q[1]) / w; m[4] = (q[7] - q[1]) / h; m[5] = q[1] - m[3] * x0 - m[4] * y0;
      set(_app(obj), MUIA_Application_Sleep, TRUE);
      job_begin(doc, "Free Transform");
      ok = doc_smart_transform(doc, doc->active, m);
      if (job_end()) ok = -1;
      set(_app(obj), MUIA_Application_Sleep, FALSE);
    } else {
    set(_app(obj), MUIA_Application_Sleep, TRUE);
    job_begin(doc, d->xf_sel ? "Transform Selection" : "Free Transform");     /* (0.8: progress and Abort) */
    ok = d->xf_sel ? xform_selection(doc, &d->xf) : !box_empty(xform_layer(doc, doc->active, &d->xf, RS_BICUBIC));
    if (job_end()) ok = -1;
    set(_app(obj), MUIA_Application_Sleep, FALSE);
    }
    if (ok > 0) changed(d, obj);
    else if (!ok) ask("OK", "%s", tr("The layer can't be changed (it is locked, or has no pixels)."));
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

/* ------------------------------------------------------------------ 0.9: the Type tools */
/* the layer being edited, found again by its pointer; -1 if it has gone */
static int ty_index(void)
{
  int i;
  if (!type_editing || !ty.doc) return -1;
  for (i = 0; i < ty.doc->nlay; i++) if (ty.doc->lay[i] == ty.lay) return i;
  return -1;
}
static void ty_free(void)
{
  text_layout_free(ty.tl);
  free(ty.tp); free(ty.orig);
  memset(&ty, 0, sizeof ty);
  type_editing = 0;
}
/* where text drawn with these settings may reach (a margin for the caret and faux styles) */
static Box ty_box(const TextParams *tp)
{
  Box b = text_bounds(tp);
  i32 m = (i32)(tp->run[0].size / 3) + 4;
  if (box_empty(b)) b = box_make((i32)tp->x, (i32)tp->y, (i32)tp->x + 1, (i32)tp->y + 1);
  return box_make(b.x0 - m, b.y0 - m, b.x1 + m, b.y1 + m);
}
/* a run set to the options bar's font and style: the real bold or italic face when the family has
   one, else the faux style */
static void ty_run_style(TextRun *r, const char *family, int style)
{
  int wb = (style & 1) != 0, wi = (style & 2) != 0;
  Face *f = text_face_lookup && (wb || wi) ? text_face_lookup(family, wb, wi) : NULL;
  int rb = wb && f && f->bold && !Stricmp((STRPTR)f->family, (STRPTR)family);
  int ri = wi && f && f->italic && !Stricmp((STRPTR)f->family, (STRPTR)family);
  strncpy(r->family, family, sizeof r->family - 1); r->family[sizeof r->family - 1] = 0;
  r->bold = (u8)rb; r->faux_bold = (u8)(wb && !rb);
  r->italic = (u8)ri; r->faux_italic = (u8)(wi && !ri);
}
/* the options bar shown as the text at the caret is (and remembered as applied) */
static void ty_sync_opts(const TextParams *tp, int at)
{
  const TextRun *r = &tp->run[text_run_at(tp, at)];
  int f = font_find(r->family);
  if (f >= 0) opt.text_font = f;
  opt.text_size = (int)(r->size + 0.5);
  opt.text_style = ((r->bold || r->faux_bold) ? 1 : 0) | ((r->italic || r->faux_italic) ? 2 : 0);
  opt.text_aa = tp->aa; opt.text_align = tp->align;
  ty.o_font = opt.text_font; ty.o_style = opt.text_style; ty.o_size = opt.text_size; ty.o_aa = opt.text_aa; ty.o_align = opt.text_align;
  if (optbar) DoMethod(optbar, MUIM_Opts_Refresh);
}
/* the working copy put into the layer (no undo step yet: that is made when the editing ends), laid
   out again and the canvas brought up to date (call with the overlay hidden) */
static void ty_apply(struct Data *d, Object *obj)
{
  int i = ty_index();
  Layer *l;
  Box old;
  if (i < 0) return;
  l = ty.doc->lay[i];
  old = ty_box((const TextParams *)l->live);
  memcpy(l->live, ty.tp, sizeof(TextParams));
  l->live_ok = 0;
  text_layout_free(ty.tl);
  ty.tl = text_layout(ty.tp);
  ty.changed = 1;
  if (ty.caret > (int)strlen(ty.tp->text)) ty.caret = (int)strlen(ty.tp->text);
  if (ty.anchor > (int)strlen(ty.tp->text)) ty.anchor = ty.caret;
  update_box(d, obj, box_union(old, ty_box(ty.tp)));
}
/* editing begun on type layer i of doc (isnew: just made for it; mask: the Type Mask tool's) */
static int ty_begin(Doc *doc, int i, int isnew, int mask)
{
  const TextParams *lt = layer_text(doc->lay[i]);
  if (!lt) return 0;
  memset(&ty, 0, sizeof ty);
  ty.tp = (TextParams *)malloc(sizeof(TextParams));
  ty.orig = (TextParams *)malloc(sizeof(TextParams));
  if (!ty.tp || !ty.orig) { free(ty.tp); free(ty.orig); memset(&ty, 0, sizeof ty); return 0; }
  *ty.tp = *lt; *ty.orig = *lt;
  ty.doc = doc; ty.lay = doc->lay[i]; ty.isnew = isnew; ty.mask = mask;
  ty.mark = isnew ? (void *)doc->undo : NULL;
  ty.tl = text_layout(ty.tp);
  ty.caret = ty.anchor = (int)strlen(ty.tp->text);
  doc->active = i;
  type_editing = 1;
  return 1;
}
/* a new type layer (empty) at x, y: point text, or a paragraph in the box w x h; editing begun */
static int ty_new(Doc *doc, double x, double y, int box, double w, double h)
{
  TextParams *tp = (TextParams *)malloc(sizeof(TextParams));
  int i, mask = tool == TL_TYPEMASK;
  if (!tp) return 0;
  if (!font_count()) { free(tp); ask("OK", "%s", tr("There are no fonts to write with.\nKeep the TrueType drawer next to Easel.")); return 0; }
  text_params_init(tp, font_name(opt.text_font), opt.text_size, mask ? 0xFF0000 : doc->fg, x, y);
  ty_run_style(&tp->run[0], font_name(opt.text_font), opt.text_style);
  tp->aa = (u8)opt.text_aa; tp->align = (u8)opt.text_align;
  tp->vertical = tool == TL_VTEXT;
  if (box) { tp->box = 1; tp->bw = w; tp->bh = h; }
  i = doc_new_text(doc, tp, mask ? "Type Mask" : "Text");
  free(tp);
  if (i < 0) { ask("OK", "%s", tr("There isn't enough memory for that.")); return 0; }
  if (mask) doc->lay[i]->opacity = 128;                     /* (red, half seen, as a quick mask) */
  if (!ty_begin(doc, i, 1, mask)) return 0;
  ty_sync_opts(ty.tp, 0);
  return 1;
}
/* the editing ended: how 1 keeps it (one undo step: the new layer named after its text, or Edit
   Type; the Type Mask's selection), 0 puts the layer back as it was, -1 just forgets (the picture
   was changed under it, as by the History panel) */
static void ty_end(struct Data *d, Object *obj, int how)
{
  Doc *doc = ty.doc;
  int i = ty_index();
  if (!type_editing) return;
  ov_hide(d, obj);
  if (i >= 0 && how >= 0) {
    Layer *l = doc->lay[i];
    int empty = !ty.tp->text[0];
    if (ty.isnew && (void *)doc->undo == ty.mark) {
      doc_discard_last(doc);                                  /* (the empty layer made for it) */
      if (how > 0 && !empty) {
        if (ty.mask) sel_from_text(doc, ty.tp, opt.sel_op);
        else doc_new_text(doc, ty.tp, NULL);
      }
    } else if (ty.changed || ty.isnew) {
      memcpy(l->live, ty.orig, sizeof(TextParams));
      l->live_ok = 0;
      if (how > 0) {
        if (empty || ty.mask) { if (empty || ty.isnew) doc_delete_layer(doc, i); }
        else doc_set_text(doc, i, ty.tp);
        if (ty.mask && !empty) sel_from_text(doc, ty.tp, opt.sel_op);
      }
    }
  }
  ty_free();
  if (doc == cur_doc()) {
    changed(d, obj);
    d->edges_ok = 0;
    comp_free(d);
    draw_all(d, obj);
    tell_changed(d, obj);
  }
}
/* text put in at the caret, over the selection */
static void ty_insert(struct Data *d, Object *obj, const char *u, int n)
{
  int a = ty.anchor < ty.caret ? ty.anchor : ty.caret, b = ty.anchor < ty.caret ? ty.caret : ty.anchor;
  ov_hide(d, obj);
  if (a < b) { text_delete(ty.tp, a, b); ty.caret = a; }
  if (n && text_insert(ty.tp, ty.caret, u, n)) ty.caret += n;
  ty.anchor = ty.caret;
  ty_apply(d, obj);
  ov_show(d, obj);
}
/* a key while typing: 1 if it was the Type tool's */
static int ty_key(struct Data *d, Object *obj, struct IntuiMessage *im)
{
  UWORD code = im->Code, q = im->Qualifier;
  int shift = (q & QSHIFT) != 0, ctrl = (q & IEQUALIFIER_CONTROL) != 0, len = (int)strlen(ty.tp->text);
  int a = ty.anchor < ty.caret ? ty.anchor : ty.caret, b = ty.anchor < ty.caret ? ty.caret : ty.anchor, how = -1;
  if (code & IECODE_UP_PREFIX) return code == (0x40 | IECODE_UP_PREFIX);
  if (q & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND)) {
    if (code == 0x44 || code == 0x43) { ty_end(d, obj, 1); return 1; }     /* Amiga-Return commits */
    return 0;
  }
  switch (code) {
  case 0x45: case 0x43: ty_end(d, obj, 1); return 1;                     /* Esc, Enter: done */
  case 0x44: ty_insert(d, obj, "\n", 1); return 1;                        /* Return: a new paragraph */
  case 0x41:                                                              /* Backspace */
    if (a < b) ty_insert(d, obj, "", 0);
    else if (ty.caret > 0) { ov_hide(d, obj); ty.anchor = text_caret_move(ty.tl, ty.caret, TCM_LEFT); ty_insert(d, obj, "", 0); }
    return 1;
  case 0x46:                                                              /* Del */
    if (a < b) ty_insert(d, obj, "", 0);
    else if (ty.caret < len) { ov_hide(d, obj); ty.anchor = text_caret_move(ty.tl, ty.caret, TCM_RIGHT); ty_insert(d, obj, "", 0); }
    return 1;
  case 0x4C: how = ctrl ? TCM_START : TCM_UP; break;
  case 0x4D: how = ctrl ? TCM_END_ALL : TCM_DOWN; break;
  case 0x4E: how = ctrl ? TCM_WORDRIGHT : TCM_RIGHT; break;
  case 0x4F: how = ctrl ? TCM_WORDLEFT : TCM_LEFT; break;
  case 0x70: how = ctrl ? TCM_START : TCM_HOME; break;                    /* Home, End (on keyboards that have them) */
  case 0x71: how = ctrl ? TCM_END_ALL : TCM_END; break;
  }
  if (how >= 0) {
    ov_hide(d, obj);
    if (!shift && a < b && (how == TCM_LEFT || how == TCM_RIGHT)) ty.caret = how == TCM_LEFT ? a : b;   /* (a selection collapses) */
    else ty.caret = text_caret_move(ty.tl, ty.caret, how);
    if (!shift) ty.anchor = ty.caret;
    ov_show(d, obj);
    return 1;
  }
  {
    /* a character (Latin-1 from the keymap) put in as UTF-8 */
    struct InputEvent ie;
    UBYTE buf[4];
    char u[3];
    memset(&ie, 0, sizeof ie);
    ie.ie_Class = IECLASS_RAWKEY; ie.ie_Code = code; ie.ie_Qualifier = q & ~IEQUALIFIER_CONTROL;
    ie.ie_EventAddress = (APTR *)*((ULONG *)im->IAddress);
    if (MapRawKey(&ie, (STRPTR)buf, sizeof buf, NULL) != 1) return 0;
    if (ctrl) {
      if (buf[0] == 'a' || buf[0] == 'A') { ov_hide(d, obj); ty.anchor = 0; ty.caret = len; ov_show(d, obj); return 1; }
      return 0;                                                           /* (the other Ctrl keys: commands) */
    }
    if (buf[0] < 32 && buf[0] != 9) return buf[0] != 0;
    if (buf[0] >= 127 && buf[0] < 160) return 1;
    if (buf[0] < 0x80) { u[0] = (char)buf[0]; ty_insert(d, obj, u, 1); }
    else { u[0] = (char)(0xC0 | (buf[0] >> 6)); u[1] = (char)(0x80 | (buf[0] & 63)); ty_insert(d, obj, u, 2); }
    return 1;
  }
}
/* the type layer under image point x, y (the topmost visible one whose text reaches there), -1 none */
static int ty_layer_at(Doc *doc, double x, double y, double margin)
{
  int i;
  for (i = doc->nlay - 1; i >= 0; i--) {
    const TextParams *tp = layer_text(doc->lay[i]);
    Box b;
    if (!tp || !layer_shown(doc, i)) continue;
    b = text_bounds(tp);
    if (tp->box && !tp->xf_on) b = box_union(b, box_make((i32)tp->x, (i32)tp->y, (i32)(tp->x + tp->bw), (i32)(tp->y + tp->bh)));
    if (x >= b.x0 - margin && x < b.x1 + margin && y >= b.y0 - margin && y < b.y1 + margin) return i;
  }
  return -1;
}
/* the Type tool's click: in the text being edited, the caret (Shift: the selection) put there and a
   double-click takes the word; on another type layer, editing it; elsewhere a drag for a paragraph's box */
static void ty_press(struct Data *d, Object *obj, Doc *doc, double x, double y, int shift, int dbl)
{
  int i;
  if (type_editing && ty.doc == doc && ty_index() >= 0) {
    if (ty_layer_at(doc, x, y, 8 / d->z) == ty_index() || (ty.tp->box && x >= ty.tp->x && y >= ty.tp->y && x < ty.tp->x + ty.tp->bw && y < ty.tp->y + ty.tp->bh)) {
      ty.caret = text_hit(ty.tl, x, y);
      if (dbl) { int e = text_caret_move(ty.tl, ty.caret, TCM_WORDRIGHT); ty.anchor = text_caret_move(ty.tl, e, TCM_WORDLEFT); ty.caret = e; }
      else if (!shift) ty.anchor = ty.caret;
      ty_sync_opts(ty.tp, ty.caret);
      d->drag = DR_TYPESEL;
      return;
    }
    ty_end(d, obj, 1);
    ov_hide(d, obj);
  }
  if (tool != TL_TYPEMASK && (i = ty_layer_at(doc, x, y, 4 / d->z)) >= 0) {
    Layer *l = doc->lay[i];
    if (l->lock_all) { ask("OK", "%s", tr("The layer is locked.")); return; }
    if (ty_begin(doc, i, 0, 0)) {
      ty.caret = ty.anchor = text_hit(ty.tl, x, y);
      ty_sync_opts(ty.tp, ty.caret);
      changed(d, obj);
      d->drag = DR_TYPESEL;
    }
    return;
  }
  d->drag = DR_TYPEBOX;
}
/* the options bar's type settings applied where they differ from those last applied: to the text
   selected, or all of it */
static void ty_from_opts(TextParams *tp, int from, int to)
{
  TextRun r;
  u32 m = 0;
  memset(&r, 0, sizeof r);
  if (opt.text_font != ty.o_font || opt.text_style != ty.o_style) { ty_run_style(&r, font_name(opt.text_font), opt.text_style); m |= TS_BOLD | TS_ITALIC | TS_FAUXBOLD | TS_FAUXITALIC; }
  if (opt.text_font != ty.o_font) m |= TS_FAMILY;
  if (opt.text_size != ty.o_size) { r.size = opt.text_size; m |= TS_SIZE; }
  if (m) {
    if (from >= to) { from = 0; to = (int)strlen(tp->text); }
    if (to > from) text_style(tp, from, to, &r, m);
    else {
      /* no text yet: the first run's style */
      if (m & TS_FAMILY) strcpy(tp->run[0].family, r.family);
      if (m & TS_BOLD) { tp->run[0].bold = r.bold; tp->run[0].faux_bold = r.faux_bold; tp->run[0].italic = r.italic; tp->run[0].faux_italic = r.faux_italic; }
      if (m & TS_SIZE) tp->run[0].size = r.size;
    }
  }
  if (opt.text_aa != ty.o_aa) tp->aa = (u8)opt.text_aa;
  if (opt.text_align != ty.o_align) tp->align = (u8)opt.text_align;
  ty.o_font = opt.text_font; ty.o_style = opt.text_style; ty.o_size = opt.text_size; ty.o_aa = opt.text_aa; ty.o_align = opt.text_align;
}
/* MUIM_Canvas_TypeCmd: the options bar, the Character, Paragraph and Warp windows, orientation and
   the colour, applied to the text being edited (its selection, or all of it) or else to the whole
   active type layer as an undo step; TC_EDIT starts editing the active type layer, TC_COMMIT ends it */
static void ty_cmd(struct Data *d, Object *obj, int code, int arg)
{
  Doc *doc = cur_doc();
  TextParams *tp;
  int editing = type_editing && ty.doc == doc && ty_index() >= 0, from = 0, to = 0, ok = 1, li;
  if (!doc) return;
  if (code == TC_COMMIT) { if (type_editing) ty_end(d, obj, 1); return; }
  if (code == TC_EDIT) {
    if (type_editing) ty_end(d, obj, 1);
    if (!layer_text(doc->lay[doc->active])) return;
    ov_hide(d, obj);
    if (ty_begin(doc, doc->active, 0, 0)) { ty.anchor = 0; ty_sync_opts(ty.tp, 0); }
    ov_show(d, obj);
    return;
  }
  if (editing) {
    tp = ty.tp;
    from = ty.anchor < ty.caret ? ty.anchor : ty.caret; to = ty.anchor < ty.caret ? ty.caret : ty.anchor;
  } else {
    const TextParams *lt = layer_text(doc->lay[doc->active]);
    if (!lt) {
      if (code != TC_OPTS) ask("OK", "%s", tr("Choose a type layer first (or click in the picture with the Type tool)."));
      ty.o_font = opt.text_font; ty.o_style = opt.text_style; ty.o_size = opt.text_size; ty.o_aa = opt.text_aa; ty.o_align = opt.text_align;
      return;
    }
    if (code == TC_OPTS && !TYPE_TOOL(tool)) return;
    if (!(tp = (TextParams *)malloc(sizeof(TextParams)))) return;
    *tp = *lt;
  }
  switch (code) {
  case TC_OPTS: ty_from_opts(tp, from, to); break;
  case TC_CHARACTER: ok = dlg_character(tp, from, to); break;
  case TC_PARAGRAPH: ok = dlg_paragraph(tp); break;
  case TC_WARP: ok = dlg_warp(tp); break;
  case TC_ORIENT: tp->vertical = (u8)(arg != 0); break;
  case TC_COLOR: {
    TextRun r;
    memset(&r, 0, sizeof r);
    r.color = doc->fg;
    if (from >= to) { from = 0; to = (int)strlen(tp->text); }
    if (to > from) text_style(tp, from, to, &r, TS_COLOR); else tp->run[0].color = doc->fg;
    break; }
  }
  if (editing) {
    if (ok) { ov_hide(d, obj); ty_apply(d, obj); ov_show(d, obj); }
    return;
  }
  li = doc->active;
  if (ok && memcmp(tp, layer_text(doc->lay[li]), sizeof *tp)) {
    Box old = ty_box(layer_text(doc->lay[li]));
    doc_set_text(doc, li, tp);
    changed(d, obj);
    update_box(d, obj, box_union(old, ty_box(tp)));
    tell_changed(d, obj);
  }
  free(tp);
}

/* ------------------------------------------------------------------ 0.9: the pen and path tools */
static void path_clear_sel(Path *p) { int i; for (i = 0; p && i < p->na; i++) p->a[i].sel = 0; }
static PathAnchor *anchor_of(Path *p, int s, int k) { return &p->a[p->s[s].first + k]; }
/* an undo step for the paths begun (ended when the button is let go) */
static void pe_begin(struct Data *d, Doc *doc, const char *name)
{
  if (d->pe_doc) doc_end(d->pe_doc);
  doc_begin(doc, name);
  doc_note_paths(doc);
  d->pe_doc = doc;
}
static void pe_end(struct Data *d)
{
  int i;
  if (!d->pe_doc) return;
  for (i = 0; i < ndocs; i++) if (docs[i].doc == d->pe_doc) doc_end(d->pe_doc);
  d->pe_doc = NULL;
}
/* a path tool's click: the Pen lays anchors (a click a corner, a drag a smooth one; on the first
   anchor it closes the subpath; on the path it adds or deletes an anchor), Add and Delete Anchor
   Point, Convert Point, Direct Selection (anchors and handles; a drag on nothing chooses anchors in
   a box) and Path Selection (a whole subpath) */
static void path_press(struct Data *d, Object *obj, Doc *doc, LONG mx, LONG my, double x, double y, int shift, int alt)
{
  double tol = 5 / d->z;
  PathHit h;
  Path *p;
  int hit, k;
  d->pe_what = PE_NONE; d->pe_moved = 0; d->pe_lx = x; d->pe_ly = y; d->pe_alone = alt;
  d->drag = DR_PATH;
  d->path_dirty = 1;
  if (tool == TL_PEN) {
    pe_begin(d, doc, "Pen");
    if (!(p = cur_path(doc, 1))) return;
    if (d->pen_on && (d->pen_doc != doc || d->pen_sub != p->ns - 1 || p->s[d->pen_sub].closed || !p->s[d->pen_sub].n)) d->pen_on = 0;
    if (d->pen_on) {
      SubPath *sp = &p->s[d->pen_sub];
      PathAnchor *a0 = &p->a[sp->first];
      if (sp->n >= 2 && labs(to_sx(d, obj, a0->x) - mx) <= 5 && labs(to_sy(d, obj, a0->y) - my) <= 5) {
        path_close(p); d->pen_on = 0;
        d->pe_s = d->pen_sub; d->pe_k = 0; d->pe_what = PE_HANDLES;
      } else if (path_line_to(p, x, y)) { d->pe_s = d->pen_sub; d->pe_k = p->s[d->pen_sub].n - 1; d->pe_what = PE_HANDLES; }
    } else {
      hit = path_hit(p, x, y, tol, 0, &h);
      if (hit == PH_SEGMENT && (k = path_split_segment(p, h.s, h.k, h.t)) > 0) { d->pe_s = h.s; d->pe_k = k; d->pe_what = PE_ANCHOR; }
      else if (hit == PH_ANCHOR) {
        SubPath *sp = &p->s[h.s];
        if (!sp->closed && h.s == p->ns - 1 && (h.k == sp->n - 1 || h.k == 0) && sp->n >= 1) {
          /* an open end: the subpath carried on from it */
          if (h.k == 0 && sp->n > 1) path_reverse(p, h.s);
          d->pen_on = 1; d->pen_sub = h.s; d->pen_doc = doc;
          d->pe_s = h.s; d->pe_k = p->s[h.s].n - 1;
          d->pe_what = PE_NONE;
          path_clear_sel(p); anchor_of(p, d->pe_s, d->pe_k)->sel = 1;
          return;
        }
        path_delete_anchor(p, h.s, h.k);
      } else if ((k = path_move_to(p, x, y)) > 0) { d->pen_on = 1; d->pen_sub = k - 1; d->pen_doc = doc; d->pe_s = k - 1; d->pe_k = 0; d->pe_what = PE_HANDLES; }
    }
    path_clear_sel(p);
    if (d->pe_what != PE_NONE) anchor_of(p, d->pe_s, d->pe_k)->sel = 1;
    return;
  }
  if (!(p = cur_path(doc, 0)) && doc->workpath) { doc->path_active = -1; p = cur_path(doc, 0); }
  if (!p) { if (tool != TL_PATHSEL && tool != TL_DIRECTSEL) ask("OK", "%s", tr("There is no path: draw one with the Pen tool, or choose one in the Paths panel.")); d->drag = DR_NONE; return; }
  hit = path_hit(p, x, y, tol, tool == TL_CONVERT, &h);
  switch (tool) {
  case TL_ADDANCHOR:
    if (hit == PH_SEGMENT) {
      pe_begin(d, doc, "Add Anchor Point");
      if ((k = path_split_segment(p, h.s, h.k, h.t)) > 0) { path_clear_sel(p); anchor_of(p, h.s, k)->sel = 1; d->pe_s = h.s; d->pe_k = k; d->pe_what = PE_ANCHOR; }
    }
    break;
  case TL_DELANCHOR:
    if (hit == PH_ANCHOR) { pe_begin(d, doc, "Delete Anchor Point"); path_delete_anchor(p, h.s, h.k); }
    break;
  case TL_CONVERT:
    if (hit == PH_ANCHOR) {
      PathAnchor *a = anchor_of(p, h.s, h.k);
      pe_begin(d, doc, "Convert Point");
      if (a->ix != a->x || a->iy != a->y || a->ox != a->x || a->oy != a->y) path_convert_anchor(p, h.s, h.k, PA_CORNER);
      path_clear_sel(p); a->sel = 1;
      d->pe_s = h.s; d->pe_k = h.k; d->pe_what = PE_HANDLES;
    } else if (hit == PH_IN || hit == PH_OUT) {
      pe_begin(d, doc, "Convert Point");
      d->pe_s = h.s; d->pe_k = h.k; d->pe_which = hit == PH_OUT; d->pe_alone = 1; d->pe_what = PE_HANDLE;
    }
    break;
  case TL_DIRECTSEL:
    if (hit == PH_ANCHOR) {
      PathAnchor *a = anchor_of(p, h.s, h.k);
      if (shift) a->sel = !a->sel;
      else if (!a->sel) { path_clear_sel(p); a->sel = 1; }
      pe_begin(d, doc, "Move Anchor"); d->pe_what = PE_SELMOVE;
    } else if (hit == PH_IN || hit == PH_OUT) {
      pe_begin(d, doc, "Move Handle");
      d->pe_s = h.s; d->pe_k = h.k; d->pe_which = hit == PH_OUT; d->pe_what = PE_HANDLE;
    } else if (hit == PH_SEGMENT) {
      int n = p->s[h.s].n;
      if (!shift) path_clear_sel(p);
      anchor_of(p, h.s, h.k)->sel = 1; anchor_of(p, h.s, (h.k + 1) % n)->sel = 1;
      pe_begin(d, doc, "Move Segment"); d->pe_what = PE_SELMOVE;
    } else { if (!shift) path_clear_sel(p); d->pe_what = PE_BOX; }
    break;
  case TL_PATHSEL:
    path_clear_sel(p);
    if (hit != PH_NONE) {
      for (k = 0; k < p->s[h.s].n; k++) anchor_of(p, h.s, k)->sel = 1;
      pe_begin(d, doc, "Move Path"); d->pe_s = h.s; d->pe_what = PE_SUBMOVE;
    }
    break;
  }
}
/* a path tool's drag to x, y (call with the overlay hidden) */
static void path_motion(struct Data *d, Object *obj, double x, double y)
{
  Doc *doc = cur_doc();
  Path *p = cur_path(doc, 0);
  double dx = x - d->pe_lx, dy = y - d->pe_ly;
  int s, k;
  d->bx = x; d->by = y;
  if (!p || d->pe_what == PE_NONE) return;
  if (!d->pe_moved && fabs((x - d->ax) * d->z) + fabs((y - d->ay) * d->z) < 3) return;
  d->pe_moved = 1;
  if (d->pe_what != PE_BOX && (d->pe_s >= p->ns || d->pe_k >= p->s[d->pe_s].n)) return;
  switch (d->pe_what) {
  case PE_HANDLES: path_drag_handles(p, d->pe_s, d->pe_k, x, y); break;
  case PE_ANCHOR: path_move_anchor(p, d->pe_s, d->pe_k, dx, dy); break;
  case PE_HANDLE: path_move_handle(p, d->pe_s, d->pe_k, d->pe_which, x, y, d->pe_alone); break;
  case PE_SELMOVE:
    for (s = 0; s < p->ns; s++) for (k = 0; k < p->s[s].n; k++) if (anchor_of(p, s, k)->sel) path_move_anchor(p, s, k, dx, dy);
    break;
  case PE_SUBMOVE:
    for (k = 0; k < p->s[d->pe_s].n; k++) path_move_anchor(p, d->pe_s, k, dx, dy);
    break;
  }
  d->pe_lx = x; d->pe_ly = y;
  d->path_dirty = 1;
}
/* the button let go: Direct Selection's box chooses the anchors in it; the step ended */
static void path_release(struct Data *d, Object *obj)
{
  Doc *doc = cur_doc();
  Path *p = cur_path(doc, 0);
  if (p && d->pe_what == PE_BOX) {
    double x0 = d->ax < d->bx ? d->ax : d->bx, x1 = d->ax < d->bx ? d->bx : d->ax, y0 = d->ay < d->by ? d->ay : d->by, y1 = d->ay < d->by ? d->by : d->ay;
    int i;
    for (i = 0; i < p->na; i++) if (p->a[i].x >= x0 && p->a[i].x <= x1 && p->a[i].y >= y0 && p->a[i].y <= y1) p->a[i].sel = 1;
  }
  pe_end(d);
  d->pe_what = PE_NONE;
  d->path_dirty = 1;
  changed(d, obj);
  tell_changed(d, obj);
}
/* the chosen anchors of the active path deleted (Direct Selection's Backspace); 1 if there were any */
static int path_delete_sel(struct Data *d, Object *obj)
{
  Doc *doc = cur_doc();
  Path *p = cur_path(doc, 0);
  int s, k, any = 0;
  if (!p) return 0;
  for (s = 0; s < p->ns; s++) for (k = 0; k < p->s[s].n; k++) if (anchor_of(p, s, k)->sel) any = 1;
  if (!any) return 0;
  ov_hide(d, obj);
  doc_begin(doc, "Delete Anchor Point");
  doc_note_paths(doc);
  p = cur_path(doc, 0);
  for (s = p->ns - 1; s >= 0; s--) for (k = p->s[s].n - 1; k >= 0; k--) if (s < p->ns && k < p->s[s].n && anchor_of(p, s, k)->sel) path_delete_anchor(p, s, k);
  doc_end(doc);
  d->path_dirty = 1;
  ov_show(d, obj);
  changed(d, obj);
  tell_changed(d, obj);
  return 1;
}

/* ------------------------------------------------------------------ 0.9: the shape tools */
/* the settings the shape tool and its options bar make for the box (a line: from x0, y0 to x1, y1) */
static void shape_params_for(ShapeParams *sp, int t, double x0, double y0, double x1, double y1, u32 fg)
{
  int kind = t == TL_ELLIPSE ? SH_ELLIPSE : t == TL_LINE ? SH_LINE : t == TL_ROUNDRECT ? SH_ROUNDRECT : t == TL_POLYGON ? SH_POLYGON :
             t == TL_CUSTOMSHAPE ? SH_CUSTOM : SH_RECT;
  shape_defaults(sp, kind, x0, y0, x1, y1, fg);
  sp->fill_on = (u8)(opt.shape_fill != 0);
  sp->stroke_on = (u8)(opt.shape_stroke != 0);
  sp->stroke_width = opt.stroke_width > 0 ? opt.stroke_width : 1;
  sp->stroke_color = (u32)opt.stroke_color & 0xFFFFFF;
  sp->align = (u8)opt.stroke_align;
  if (kind == SH_RECT) sp->radius = opt.shape_fill_radius;
  if (kind == SH_ROUNDRECT) sp->radius = opt.round_radius;
  if (kind == SH_POLYGON) { sp->sides = opt.poly_sides < 3 ? 3 : opt.poly_sides; sp->star = (u8)(opt.poly_star != 0); sp->indent = opt.poly_indent; }
  if (kind == SH_LINE) { sp->line_width = opt.line_width > 0 ? opt.line_width : 1; sp->arrow_start = (u8)(opt.arrow_start != 0); sp->arrow_end = (u8)(opt.arrow_end != 0); sp->fill_on = 1; }
  if (kind == SH_LINE || kind == SH_CUSTOM || kind == SH_POLYGON) sp->join = JOIN_ROUND;
}
/* the active path scaled into the box (the Custom Shape tool's shape), or NULL */
static Path *custom_path(Doc *doc, double x0, double y0, double x1, double y1)
{
  Path *src = cur_path(doc, 0), *p;
  double bx0, by0, bx1, by1, m[6];
  if (!src || !src->na || !path_bounds(src, &bx0, &by0, &bx1, &by1) || bx1 - bx0 < 1e-6 || by1 - by0 < 1e-6) return NULL;
  if (!(p = path_copy(src))) return NULL;
  if (x0 > x1) { double t = x0; x0 = x1; x1 = t; }
  if (y0 > y1) { double t = y0; y0 = y1; y1 = t; }
  m[0] = (x1 - x0) / (bx1 - bx0); m[1] = 0; m[2] = x0 - bx0 * m[0];
  m[3] = 0; m[4] = (y1 - y0) / (by1 - by0); m[5] = y0 - by0 * m[4];
  path_transform(p, m);
  path_clear_sel(p);
  return p;
}
/* the drag's box in order (a line keeps its ends) */
static void drag_box(struct Data *d, double *x0, double *y0, double *x1, double *y1)
{
  if (tool == TL_LINE) { *x0 = d->ax; *y0 = d->ay; *x1 = d->bx; *y1 = d->by; return; }
  if (d->shape_centre && d->drag == DR_SHAPE && tool != TL_REDEYE) {
    double w = fabs(d->bx - d->ax), h = fabs(d->by - d->ay);
    *x0 = d->ax - w; *x1 = d->ax + w; *y0 = d->ay - h; *y1 = d->ay + h;
    return;
  }
  *x0 = d->ax < d->bx ? d->ax : d->bx; *x1 = d->ax < d->bx ? d->bx : d->ax;
  *y0 = d->ay < d->by ? d->ay : d->by; *y1 = d->ay < d->by ? d->by : d->ay;
}
/* the outline of the shape being dragged out, for the overlay (call with the overlay hidden) */
static void prev_make(struct Data *d)
{
  Doc *doc = cur_doc();
  ShapeParams sp;
  Path *p;
  double x0, y0, x1, y1;
  poly_free(&d->prev); poly_init(&d->prev);
  if (!doc || !d->prev_on) return;
  drag_box(d, &x0, &y0, &x1, &y1);
  if (tool != TL_LINE && (x1 - x0 < 1 || y1 - y0 < 1)) return;
  shape_params_for(&sp, tool, x0, y0, x1, y1, doc->fg);
  if (tool == TL_CUSTOMSHAPE) p = custom_path(doc, x0, y0, x1, y1);
  else if ((p = path_new()) && !shape_make_path(&sp, p)) { path_free(p); p = NULL; }
  if (p) { path_flatten(p, 0.7 / d->z, &d->prev); path_free(p); }
}
/* the shape dragged out made: a shape layer, or (Pixels) filled and stroked on the active layer */
static void shape_make(struct Data *d, Object *obj, Doc *doc)
{
  ShapeParams sp;
  Path *cust = NULL, *p;
  double x0, y0, x1, y1;
  drag_box(d, &x0, &y0, &x1, &y1);
  shape_params_for(&sp, tool, x0, y0, x1, y1, doc->fg);
  if (tool == TL_CUSTOMSHAPE && !(cust = custom_path(doc, x0, y0, x1, y1))) return;
  if (!opt.shape_mode) {
    if (doc_new_shape(doc, &sp, cust, NULL) < 0) ask("OK", "%s", tr("There isn't enough memory for that."));
  } else if ((p = cust ? cust : path_new())) {
    if (cust || shape_make_path(&sp, p)) {
      doc_begin(doc, tool_names[tool]);
      if (sp.fill_on) path_fill(doc, p, sp.fill_color, 100, BM_NORMAL, 1);
      if (sp.stroke_on && sp.kind != SH_LINE) path_stroke_simple(doc, p, sp.stroke_width, sp.stroke_color, 100, BM_NORMAL);
      doc_end(doc);
    }
    if (p != cust) path_free(p);
  }
  path_free(cust);
  changed(d, obj);
  comp_free(d);
  draw_all(d, obj);
}

/* ------------------------------------------------------------------ 0.9: live layers moved */
/* layer i's rendering's box (worked out now if need be) */
static Box live_box(Doc *doc, int i)
{
  Layer *l = doc->lay[i];
  if (!l->live) return box_make(0, 0, 0, 0);
  if (!l->live_ok) live_render(doc, l);
  return l->live_ok && l->livepx ? l->livebox : box_make(0, 0, 0, 0);
}
/* live layer i moved by the forward map m (an undo step) and the canvas brought up to date */
static int live_move(struct Data *d, Object *obj, Doc *doc, int i, const double *m)
{
  Box old = live_box(doc, i);
  if (!doc_live_transform(doc, i, m)) { ask("OK", "%s", tr("The layer is locked.")); return 0; }
  changed(d, obj);
  update_box(d, obj, box_union(old, live_box(doc, i)));
  return 1;
}
/* is there something (alpha > 16) at image pixel x, y of layer l? */
static int layer_has(Doc *doc, Layer *l, i32 x, i32 y)
{
  if (l->px) return l->px[((long)y * doc->w + x) * 4 + 3] > 16;
  if (l->live) {
    Box b;
    if (!l->live_ok) live_render(doc, l);
    b = l->livebox;
    if (!l->livepx || x < b.x0 || y < b.y0 || x >= b.x1 || y >= b.y1) return 0;
    return l->livepx[((long)(y - b.y0) * (b.x1 - b.x0) + (x - b.x0)) * 4 + 3] > 16;
  }
  return 0;
}

/* the mouse button down at window mx, my (inside the area) */
/* ------------------------------------------------------------------ 1.2: the new tools */
int slice_pick = -1;                     /* (tools2.h) */
/* the tools that work on a turned view (the others ask for it to be put straight first) */
static int rot_ok(int t) { return brushlike(t) || t == TL_HAND || t == TL_ZOOM || t == TL_ROTVIEW || t == TL_EYEDROPPER || t == TL_ARTHIST || t == TL_QUICKSEL; }
/* the box dragged out, in whole pixels */
static Box drag_ibox(struct Data *d)
{
  double x0, y0, x1, y1;
  drag_box(d, &x0, &y0, &x1, &y1);
  return box_make((i32)floor(x0 < x1 ? x0 : x1), (i32)floor(y0 < y1 ? y0 : y1), (i32)ceil(x0 < x1 ? x1 : x0), (i32)ceil(y0 < y1 ? y1 : y0));
}
/* everything drawn again (after a change throughout, or to the annotations) */
static void all_changed(struct Data *d, Object *obj) { changed(d, obj); comp_free(d); d->edges_ok = 0; draw_all(d, obj); tell_changed(d, obj); }
/* the Art History Brush at x, y */
static void art_step(struct Data *d, Object *obj, Doc *doc, double x, double y)
{
  DocView *v = cur_view();
  Brush b = tool_brush(TL_ARTHIST);
  Box ch;
  if (!v || !v->snap) return;
  b.tool = PT_BRUSH;
  ch = art_history(doc, &b, v->snap->px, x, y, opt.ah_style, opt.ah_area, opt.ah_tolerance, ++d->ah_seed);
  d->ah_x = x; d->ah_y = y;
  if (!box_empty(ch)) { changed(d, obj); update_box(d, obj, ch); }
}
static void pcrop_apply(struct Data *d, Object *obj, int apply)
{
  Doc *doc = cur_doc();
  ov_hide(d, obj);
  d->pq_on = 0; pcrop_pending = 0;
  if (apply && doc) {
    int ok;
    job_begin(doc, "Perspective Crop Tool");
    ok = image_perspective_crop(doc, d->pq, opt.pcrop_w, opt.pcrop_h, RS_BICUBIC);
    if (job_end()) ok = 0;
    if (ok) { all_changed(d, obj); return; }
    if (!progress_aborted) ask("OK", "%s", tr("The corners don't make a shape that can be cropped (or there isn't the memory for it)."));
  }
  ov_show(d, obj);
}
/* a press with one of 1.2's tools: 1 if it was one (what it starts is in d->drag) */
static int t2_press(struct Data *d, Object *obj, Doc *doc, int t, LONG mx, LONG my, double x, double y, UWORD qual, int dbl)
{
  int alt = (qual & QALT) != 0, shift = (qual & QSHIFT) != 0, k;
  Annots *a;
  switch (t) {
  case TL_QUICKSEL: {
    double pt[2];
    int op = sel_op_of(qual);
    if (op == SEL_REPLACE && doc->sel) op = SEL_ADD;          /* (after the first stroke it adds, as Photoshop's does) */
    if (opt.sel_op == SEL_SUBTRACT) op = SEL_SUBTRACT;
    d->modifiers = (UWORD)(op == SEL_SUBTRACT ? QALT : op == SEL_ADD ? QSHIFT : 0);
    doc_begin(doc, "Quick Selection"); d->qs_open = 1;
    pt[0] = x; pt[1] = y;
    quick_select(doc, pt, 1, opt.size[TL_QUICKSEL] / 2 + 1, op, opt.all_layers, opt.qs_enhance);
    d->bx = x; d->by = y;
    changed(d, obj); selection_changed(d, obj);
    d->drag = DR_QSEL;
    return 1; }
  case TL_OBJSEL: case TL_SLICE: case TL_FRAME:
    if (t == TL_SLICE && m3_slices_locked) { ask("OK", "%s", tr("The slices are locked (View > Lock Slices).")); return 1; }   /* 1.3 */
    d->drag = DR_SHAPE; d->prev_on = 0; return 1;
  case TL_ARTBOARD:
    if ((d->ab_pick = doc_artboard_at(doc, (i32)x, (i32)y)) >= 0) { doc->active = d->ab_pick; d->drag = DR_ANMOVE; tell_changed(d, obj); }
    else { d->drag = DR_SHAPE; d->prev_on = 0; }
    return 1;
  case TL_PCROP:
    if (d->pq_on) {
      for (k = 0; k < 4; k++) if (labs(to_sx(d, obj, d->pq[k * 2]) - mx) <= 6 && labs(to_sy(d, obj, d->pq[k * 2 + 1]) - my) <= 6) break;
      if (k < 4) { d->pq_handle = k; d->drag = DR_PCROP; return 1; }
      if (dbl) { pcrop_apply(d, obj, 1); return 1; }
    }
    d->pq_on = 1; d->pq_handle = -1; pcrop_pending = 1;
    for (k = 0; k < 4; k++) { d->pq[k * 2] = x; d->pq[k * 2 + 1] = y; }
    d->drag = DR_PCROP;
    return 1;
  case TL_SLICESEL:
    slice_pick = slice_at(doc, (i32)x, (i32)y);
    d->an_pick = slice_pick;
    if (slice_pick >= 0 && !m3_slices_locked) d->drag = DR_ANMOVE;        /* (1.3: View > Lock Slices) */
    draw_all(d, obj);
    return 1;
  case TL_NOTE:
    k = note_at(doc, (i32)x, (i32)y, (i32)(2 / d->z) + 1);
    if (k >= 0 && alt) { note_delete(doc, k); all_changed(d, obj); return 1; }
    if (k >= 0 && dbl) {
      char text[NOTE_LEN];
      strcpy(text, doc->annot->note[k].text);
      if (ask_string("Note", "Text:", text, sizeof text)) note_set(doc, k, text);
      all_changed(d, obj);
      return 1;
    }
    if (k >= 0) { d->an_pick = k; d->drag = DR_ANMOVE; return 1; }
    if (x < 0 || y < 0 || x >= doc->w || y >= doc->h) return 1;
    {
      char text[NOTE_LEN];
      text[0] = 0;
      if (ask_string("New Note", "Text:", text, sizeof text) && note_add(doc, (i32)x, (i32)y, "", text, 0xFFFF80) < 0) ask("OK", "%s", tr("There can be 64 notes."));
    }
    all_changed(d, obj);
    return 1;
  case TL_COUNT:
    k = count_at(doc, (i32)x, (i32)y, (i32)(4 / d->z) + 1);
    if (k >= 0 && alt) { count_delete(doc, k); all_changed(d, obj); return 1; }
    if (k >= 0) { d->an_pick = k; d->drag = DR_ANMOVE; return 1; }
    if (x >= 0 && y >= 0 && x < doc->w && y < doc->h && count_add(doc, (i32)x, (i32)y) < 0) ask("OK", "%s", tr("There can be 999 count marks."));
    all_changed(d, obj);
    return 1;
  case TL_ARTHIST: {
    DocView *v = cur_view();
    if (!v || !v->snap) { ask("OK", "%s", tr("There is no snapshot to paint from (Edit > Take Snapshot).")); return 1; }
    if (v->snap->w != doc->w || v->snap->h != doc->h) { ask("OK", "%s", tr("The snapshot is another size from the picture now: take a new one (Edit > Take Snapshot).")); return 1; }
    doc_begin(doc, "Art History Brush"); d->ah_open = 1;
    art_step(d, obj, doc, x, y);
    d->drag = DR_ARTHIST;
    return 1; }
  case TL_CAMOVE:
    if (doc->sel && x >= 0 && y >= 0 && x < doc->w && y < doc->h && sel_at(doc, (i32)x, (i32)y) > 0) d->drag = DR_MOVE;
    else { d->drag = DR_LASSO; d->npoly = 0; lasso_add(d, x, y); }
    return 1;
  case TL_ROTVIEW: {
    DocView *v = cur_view();
    double cx = _mleft(obj) + _mwidth(obj) / 2.0, cy = _mtop(obj) + _mheight(obj) / 2.0;
    if (!v) return 1;
    if (dbl) { v->vrot = 0; draw_all(d, obj); return 1; }
    if (!d->comp && !comp_ready(d)) { ask("OK", "%s", tr("There isn't the memory to turn the view (it needs a copy of the whole picture).")); return 1; }
    d->rot0 = v->vrot; d->rotstart = atan2(my - cy, mx - cx) * 180 / 3.14159265358979;
    (void)shift;
    d->drag = DR_ROTVIEW;
    return 1; }
  }
  (void)a;
  return 0;
}
static int t2_motion(struct Data *d, Object *obj, Doc *doc, LONG mx, LONG my, double x, double y)
{
  switch (d->drag) {
  case DR_QSEL: {
    double pt[4];
    int op = d->modifiers & QALT ? SEL_SUBTRACT : SEL_ADD;
    if (fabs(x - d->bx) + fabs(y - d->by) < opt.size[TL_QUICKSEL] / 4.0 + 1) return 1;
    pt[0] = d->bx; pt[1] = d->by; pt[2] = x; pt[3] = y;
    quick_select(doc, pt, 2, opt.size[TL_QUICKSEL] / 2 + 1, op, opt.all_layers, opt.qs_enhance);
    d->bx = x; d->by = y;
    selection_changed(d, obj);
    return 1; }
  case DR_ARTHIST:
    if (fabs(x - d->ah_x) + fabs(y - d->ah_y) >= opt.ah_area / 3.0 + 1) art_step(d, obj, doc, x, y);
    return 1;
  case DR_PCROP:
    ov_hide(d, obj);
    if (d->pq_handle >= 0) { d->pq[d->pq_handle * 2] = x; d->pq[d->pq_handle * 2 + 1] = y; }
    else {
      double x0 = d->ax < x ? d->ax : x, y0 = d->ay < y ? d->ay : y, x1 = d->ax < x ? x : d->ax, y1 = d->ay < y ? y : d->ay;
      d->pq[0] = x0; d->pq[1] = y0; d->pq[2] = x1; d->pq[3] = y0; d->pq[4] = x1; d->pq[5] = y1; d->pq[6] = x0; d->pq[7] = y1;
    }
    ov_show(d, obj);
    return 1;
  case DR_ANMOVE: ov_hide(d, obj); d->bx = x; d->by = y; ov_show(d, obj); return 1;
  case DR_ROTVIEW: {
    DocView *v = cur_view();
    double cx = _mleft(obj) + _mwidth(obj) / 2.0, cy = _mtop(obj) + _mheight(obj) / 2.0, an = atan2(my - cy, mx - cx) * 180 / 3.14159265358979;
    if (!v) return 1;
    v->vrot = view_angle_snap(d->rot0 + an - d->rotstart, (d->qual & QSHIFT) != 0);
    draw_all(d, obj);
    return 1; }
  case DR_CROPROT: {
    double cx = (d->c0x + d->c1x) / 2, cy = (d->c0y + d->c1y) / 2;
    ov_hide(d, obj);
    opt.crop_angle = (int)floor(view_angle_snap(d->rot0 + crop_drag_angle(cx, cy, d->ax, d->ay, x, y), (d->qual & QSHIFT) != 0) + 0.5);
    ov_show(d, obj);
    return 1; }
  }
  return 0;
}
/* a release with one of 1.2's drags or tools: 1 if it was one */
static int t2_release(struct Data *d, Object *obj, Doc *doc, int drag)
{
  Box b, ch;
  int stopped;
  switch (drag) {
  case DR_QSEL: if (d->qs_open) { doc_end(doc); d->qs_open = 0; } selection_changed(d, obj); tell_changed(d, obj); return 1;
  case DR_ARTHIST: if (d->ah_open) { doc_end(doc); d->ah_open = 0; } tell_changed(d, obj); return 1;
  case DR_PCROP:
    if (d->pq_handle < 0 && (fabs(d->pq[2] - d->pq[0]) < 2 || fabs(d->pq[5] - d->pq[1]) < 2)) { ov_hide(d, obj); d->pq_on = 0; pcrop_pending = 0; }
    return 1;
  case DR_ROTVIEW: return 1;
  case DR_CROPROT: return 1;
  case DR_ANMOVE: {
    i32 dx = (i32)floor(d->bx - d->ax + 0.5), dy = (i32)floor(d->by - d->ay + 0.5);
    Annots *a = doc->annot;
    d->drag = DR_ANMOVE; ov_hide(d, obj); d->drag = DR_NONE;
    if (!dx && !dy) return 1;
    if (tool == TL_ARTBOARD && d->ab_pick >= 0 && d->ab_pick < doc->nlay) {
      Box ab = doc->lay[d->ab_pick]->abox;
      doc_begin(doc, "Move Artboard");
      doc_move_group(doc, d->ab_pick, dx, dy);
      doc_artboard_set(doc, d->ab_pick, box_make(ab.x0 + dx, ab.y0 + dy, ab.x1 + dx, ab.y1 + dy), -2);
      doc_end(doc);
    } else if (a && tool == TL_SLICESEL && d->an_pick >= 0 && d->an_pick < a->nslice) {
      Box sb = a->slice[d->an_pick].b;
      slice_set(doc, d->an_pick, box_make(sb.x0 + dx, sb.y0 + dy, sb.x1 + dx, sb.y1 + dy));
    } else if (a && tool == TL_NOTE && d->an_pick >= 0) note_move(doc, d->an_pick, a->note[d->an_pick].x + dx, a->note[d->an_pick].y + dy);
    else if (a && tool == TL_COUNT && d->an_pick >= 0) count_move(doc, d->an_pick, a->mark[d->an_pick].x + dx, a->mark[d->an_pick].y + dy);
    all_changed(d, obj);
    return 1; }
  case DR_SHAPE:
    if (tool != TL_OBJSEL && tool != TL_SLICE && tool != TL_FRAME && tool != TL_ARTBOARD) return 0;
    d->drag = DR_SHAPE; ov_hide(d, obj); d->drag = DR_NONE;
    b = drag_ibox(d);
    if (b.x1 - b.x0 < 3 || b.y1 - b.y0 < 3) return 1;
    if (tool == TL_OBJSEL) {
      set(_app(obj), MUIA_Application_Sleep, TRUE);
      job_begin(doc, "Object Selection Tool");
      stopped = !object_select(doc, b, sel_op_of(d->modifiers), opt.all_layers, opt.obj_hard);
      if (job_end()) stopped = 0;
      else if (stopped) ask("OK", "%s", tr("No object was found in the box."));
      set(_app(obj), MUIA_Application_Sleep, FALSE);
      changed(d, obj); selection_changed(d, obj); tell_changed(d, obj);
      return 1;
    }
    if (tool == TL_SLICE) { if (slice_add(doc, b) < 0) ask("OK", "%s", tr("There can be 64 slices.")); all_changed(d, obj); return 1; }
    if (tool == TL_FRAME) { if (doc_new_frame(doc, b, opt.frame_kind ? FR_ELLIPSE : FR_RECT, opt.frame_stroke, doc->fg) < 0) ask("OK", "%s", tr("There isn't room for another layer.")); all_changed(d, obj); return 1; }
    if (doc_new_artboard(doc, b, 0xFFFFFF, NULL) < 0) ask("OK", "%s", tr("There isn't room for another layer."));
    all_changed(d, obj);
    return 1;
  case DR_MOVE: {
    LONG dx = (LONG)floor(d->bx - d->ax + 0.5), dy = (LONG)floor(d->by - d->ay + 0.5);
    if (tool != TL_CAMOVE && !(tool == TL_PATCH && opt.patch_mode)) return 0;
    d->drag = DR_MOVE; ov_hide(d, obj); d->drag = DR_NONE;
    if (!dx && !dy) return 1;
    set(_app(obj), MUIA_Application_Sleep, TRUE);
    job_begin(doc, tool == TL_CAMOVE ? "Content-Aware Move Tool" : "Patch Tool");
    if (tool == TL_CAMOVE) ch = doc_ca_move(doc, dx, dy, opt.cam_extend, opt.cam_structure, opt.cam_color);
    else if (opt.patch_mode == 1) ch = patch_dest(doc, dx, dy, opt.patch_transparent);
    else ch = patch_content(doc, dx, dy, opt.patch_structure, opt.patch_color);
    stopped = job_end();
    set(_app(obj), MUIA_Application_Sleep, FALSE);
    if (!box_empty(ch) && !stopped) { changed(d, obj); edges_make(d); update_box(d, obj, ch); selection_changed(d, obj); }
    else if (!stopped) ask("OK", "%s", tr("That didn't work: the layer must be a visible pixel layer that isn't locked."));
    tell_changed(d, obj);
    return 1; }
  }
  return 0;
}
static void press(struct Data *d, Object *obj, LONG mx, LONG my, UWORD qual, ULONG secs, ULONG micros)
{
  Doc *doc = cur_doc();
  double x = to_ix(d, obj, mx), y = to_iy(d, obj, my);
  int t = d->space ? TL_HAND : tool, dbl = DoubleClick(d->secs, d->micros, secs, micros);
  /* 1.1: Ctrl-Space and Alt-Space zoom in and out while held; Ctrl with a painting, filling or
     sampling tool is the Move tool for the drag */
  if (d->space && (qual & (IEQUALIFIER_CONTROL | QALT))) t = TL_ZOOM;
  else if ((qual & IEQUALIFIER_CONTROL) && !d->xf_on && (brushlike(t) || t == TL_GRADIENT || t == TL_BUCKET || t == TL_EYEDROPPER || t == TL_MAGICERASE)) t = TL_MOVE;
  int shift = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0, alt = (qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) != 0;
  Box ch;
  if (TURNED(d)) {
    /* 1.2: a turned view: the point under the pointer turned back; the tools that can't follow ask */
    to_img(d, obj, mx, my, &x, &y);
    if (doc && !rot_ok(t)) { ask("OK", "%s", tr("This tool needs the view put straight first (View > Rotate View > Reset View).")); return; }
  }
  d->secs = secs; d->micros = micros;
  d->modifiers = qual;
  if (d->spring_code) d->spring_used = 1;               /* (0.8: the spring-loaded tool used) */
  /* 0.4: snapping (guides, grid, edges) for the tools that drag out shapes */
  if (doc && doc->guides.snap && (t == TL_MARQUEE || t == TL_EMARQUEE || SHAPE_TOOL(t) || t == TL_GRADIENT || t == TL_CROP || t == TL_PEN)) {
    doc->guides.snap_tol = (u16)(16 * 6 / d->z + 1);
    snap_point(doc, &x, &y, SNAP_DOC);
  }
  /* 1.1: DPaint's grid snap: with the grid shown and snapping to it, painting goes from grid point to grid point */
  if (doc && brushlike(t) && doc->guides.show_grid && (doc->guides.snap & SNAP_GRID)) grid_snap_paint(doc, &x, &y);
  d->ax = d->bx = x; d->ay = d->by = y;
  if (!doc) return;
  d->shape_centre = 0;
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
  if (brushlike(t) && alt && t != TL_CLONE && t != TL_HEAL && t != TL_ERASER) t = TL_EYEDROPPER;    /* (Alt: the eyedropper for a moment) */
  if (t2_press(d, obj, doc, t, mx, my, x, y, qual, dbl)) { ov_show(d, obj); return; }   /* 1.2 */
  switch (t) {
  case TL_HAND: d->drag = DR_PAN; d->pmx = mx; d->pmy = my; break;
  case TL_ZOOM:
    /* 1.1: a click zooms in (out with Zoom Out chosen, or Alt); a drag left or right zooms smoothly */
    d->drag = DR_ZOOMSCRUB; d->zs_z = d->z; d->zs_mx = mx; d->zs_my = my; d->zs_moved = 0;
    break;
  case TL_EYEDROPPER: pick(d, obj, x, y, alt && tool == TL_EYEDROPPER); d->drag = DR_PICK; break;
  case TL_MOVE:
    if (opt.move_bounds && !doc->sel) {
      /* 1.1: a handle of the transform box held: Free Transform from there */
      Box bb;
      if (move_bounds(d, &bb)) {
        LONG hx[8], hy[8];
        int k;
        bounds_handles(to_sx(d, obj, bb.x0), to_sy(d, obj, bb.y0), to_sx(d, obj, bb.x1) - 1, to_sy(d, obj, bb.y1) - 1, hx, hy);
        for (k = 0; k < 8; k++) if (labs(mx - hx[k]) <= 5 && labs(my - hy[k]) <= 5) break;
        if (k < 8) {
          xf_start(d, obj, XM_FREE, 0);
          if (d->xf_on) {
            ov_hide(d, obj);
            d->xf_handle = xf_hit(d, obj, mx, my); d->xf0 = d->xf; d->qual = qual;
            d->drag = DR_XFORM;
            ov_show(d, obj);
          }
          ov_show(d, obj);
          return;
        }
      }
    }
    if (opt.auto_select && x >= 0 && y >= 0 && x < doc->w && y < doc->h) {
      /* the topmost visible layer with something at the pointer (1.1: or the group it is in) */
      int i;
      for (i = doc->nlay - 1; i >= 0; i--)
        if (doc->lay[i]->visible && layer_has(doc, doc->lay[i], (i32)x, (i32)y)) break;
      i = auto_select_target(doc, i, opt.move_group);
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
      int stopped;
      set(_app(obj), MUIA_Application_Sleep, TRUE);
      job_begin(doc, "Magnetic Lasso Tool");                     /* (0.8: the edge map, with progress and Abort) */
      d->ml = maglasso_new(doc, opt.ml_width, opt.ml_contrast, 200 / (opt.ml_freq + 1) + 8, 0);
      stopped = job_end();
      set(_app(obj), MUIA_Application_Sleep, FALSE);
      if (!d->ml) { if (!stopped) ask("OK", "%s", tr("There isn't enough memory for that.")); break; }
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
  case TL_SAMPLER: {
    /* 0.8: a sampler under the pointer dragged (Alt: removed), else a new one placed (up to four) */
    DocView *v = cur_view();
    int i, hit = -1;
    for (i = 0; i < v->nsamp; i++)
      if (labs(to_sx(d, obj, v->samp_x[i] + 0.5) - mx) <= 6 && labs(to_sy(d, obj, v->samp_y[i] + 0.5) - my) <= 6) hit = i;
    if (hit >= 0 && alt) {
      for (i = hit; i < v->nsamp - 1; i++) { v->samp_x[i] = v->samp_x[i + 1]; v->samp_y[i] = v->samp_y[i + 1]; }
      v->nsamp--;
      set(obj, MUIA_Canvas_Pointer, TRUE);
      break;
    }
    if (hit < 0) {
      if (x < 0 || y < 0 || x >= doc->w || y >= doc->h) break;
      if (v->nsamp >= MAXSAMPLERS) { ov_show(d, obj); ask("OK", "%s", tr("There can be ten colour samplers: drag one somewhere else, or Alt-click one to remove it.")); return; }
      hit = v->nsamp++;
      v->samp_x[hit] = (LONG)x; v->samp_y[hit] = (LONG)y;
    }
    d->samp_drag = hit;
    d->drag = DR_SAMPLE;
    set(obj, MUIA_Canvas_Pointer, TRUE);
    break; }
  case TL_LASSO: d->drag = DR_LASSO; d->npoly = 0; lasso_add(d, x, y); break;
  case TL_PLASSO:
    if (!d->poly_on) { d->poly_on = 1; d->npoly = 0; lasso_add(d, x, y); }
    else if (dbl || (d->npoly >= 3 && fabs(to_sx(d, obj, d->poly[0]) - mx) < 6 && fabs(to_sy(d, obj, d->poly[1]) - my) < 6)) { poly_finish(d, obj, 1); return; }
    else lasso_add(d, x, y);
    break;
  case TL_WAND:
    if (x >= 0 && y >= 0 && x < doc->w && y < doc->h) { similar_sample = pick_sizes[opt.wand_sample % NPICK_SIZES]; sel_wand_ex(doc, (i32)x, (i32)y, opt.tolerance, opt.contiguous, opt.all_layers, sel_op_of(qual), opt.sel_antialias); similar_sample = 1; changed(d, obj); }
    selection_changed(d, obj);
    tell_changed(d, obj);
    break;
  case TL_BUCKET:
    if (x >= 0 && y >= 0 && x < doc->w && y < doc->h) {
      similar_sample = pick_sizes[opt.wand_sample % NPICK_SIZES];          /* (1.3: Sample Size and Mode) */
      if (opt.fill_source && tool_pattern()) ch = fill_bucket_pattern(doc, (i32)x, (i32)y, tool_pattern(), opt.tolerance, opt.contiguous, opt.all_layers, opt.fill_opacity, opt.antialias);
      else ch = fill_bucket_mode(doc, (i32)x, (i32)y, doc->fg, opt.tolerance, opt.contiguous, opt.all_layers, opt.fill_opacity, opt.antialias, opt.mode[TL_BUCKET]);
      similar_sample = 1;
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
  case TL_GRADIENT: d->drag = DR_LINE; d->prev_on = 0; break;
  /* 0.9: the shape tools (a shape layer, or pixels); the Custom Shape is the active path's */
  case TL_LINE: d->drag = DR_LINE; d->prev_on = !opt.shape_mode; poly_free(&d->prev); poly_init(&d->prev); break;
  case TL_RECT: case TL_ELLIPSE: case TL_ROUNDRECT: case TL_POLYGON: case TL_CUSTOMSHAPE:
    if (t == TL_CUSTOMSHAPE && (!cur_path(doc, 0) || !cur_path(doc, 0)->na)) { ov_show(d, obj); ask("OK", "%s", tr("The Custom Shape tool draws the active path as a shape: draw a path with the Pen tool, or choose one in the Paths panel, first.")); return; }
    d->drag = DR_SHAPE; d->prev_on = t == TL_ROUNDRECT || t == TL_POLYGON || t == TL_CUSTOMSHAPE;
    poly_free(&d->prev); poly_init(&d->prev);
    break;
  case TL_TEXT: case TL_VTEXT: case TL_TYPEMASK: ty_press(d, obj, doc, x, y, shift, dbl); break;
  case TL_PEN: case TL_ADDANCHOR: case TL_DELANCHOR: case TL_CONVERT: case TL_PATHSEL: case TL_DIRECTSEL:
    path_press(d, obj, doc, mx, my, x, y, shift, alt);
    break;
  /* 1.0: the Red Eye tool drags a box (or clicks); the Patch tool draws round the area like the
     Lasso, then drags it to where the patch comes from */
  case TL_REDEYE: d->drag = DR_SHAPE; d->prev_on = 0; break;
  case TL_PATCH:
    if (doc->sel && x >= 0 && y >= 0 && x < doc->w && y < doc->h && sel_at(doc, (i32)x, (i32)y) > 0) d->drag = DR_MOVE;
    else { d->drag = DR_LASSO; d->npoly = 0; lasso_add(d, x, y); }
    break;
  case TL_CROP:
    if (opt.crop_straighten) { d->drag = DR_STRAIGHTEN; break; }      /* 1.1: a line to straighten by */
    if (d->crop_on) {
      int h = crop_hit(d, obj, mx, my);
      if (opt.crop_angle) h = crop_inside((d->c0x + d->c1x) / 2, (d->c0y + d->c1y) / 2, d->c1x - d->c0x, d->c1y - d->c0y, opt.crop_angle, x, y) ? 8 : -1;
      if (dbl && h == 8) { ov_show(d, obj); crop_apply(d, obj, 1); return; }
      if (h >= 0) { d->drag = DR_CROPMOVE; d->crop_handle = h; break; }
      /* 1.2: a drag outside the box turns it */
      d->rot0 = opt.crop_angle; d->drag = DR_CROPROT;
      break;
    }
    d->crop_on = 1; crop_pending = 1;
    d->c0x = d->c1x = x; d->c0y = d->c1y = y;
    d->drag = DR_CROP;
    break;
  default:
    if (brushlike(t)) {
      Brush b = tool_brush(t);
      u32 color = doc->fg;
      if (t == TL_BRUSH && d->tilde) { b.tool = PT_ERASER; b.custom = NULL; }     /* (1.1: ` held: the Brush erases) */
      if (t == TL_CLONE || t == TL_HEAL) {
        if (alt) { d->clone_x = x; d->clone_y = y; d->clone_set = 1; d->clone_aligned_set = 0; break; }
        if (!d->clone_set) { ask("OK", "%s", tr(t == TL_HEAL ? "Alt-click where the Healing Brush should take its texture from first." : "Alt-click where the Clone Stamp should copy from first.")); break; }
        if (!opt.clone_aligned) d->clone_aligned_set = 0;          /* (1.1: not Aligned: each stroke starts from the source) */
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
      } else {
        /* 1.2: a pen tablet's pressure and tilt */
        double tx, ty, pr = tablet_pressure(&tx, &ty);
        d->stroke = stroke_begin_p(doc, &b, color, x, y, pr, &ch);
        if (d->stroke) stroke_tilt(d->stroke, tx, ty);
      }
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
  if (TURNED(d)) to_img(d, obj, mx, my, &x, &y);              /* 1.2: a turned view */
  if (t2_motion(d, obj, doc, mx, my, x, y)) return;
  switch (d->drag) {
  case DR_PAN:
    v->sx -= mx - d->pmx; v->sy -= my - d->pmy;
    d->pmx = mx; d->pmy = my;
    draw_all(d, obj);
    break;
  case DR_STRAIGHTEN: ov_hide(d, obj); d->bx = x; d->by = y; ov_show(d, obj); break;
  case DR_ZOOMSCRUB:
    /* 1.1: scrubby zoom: right zooms in, left out, about where the drag began */
    if (labs(mx - d->zs_mx) > 3) d->zs_moved = 1;
    if (d->zs_moved) { ov_hide(d, obj); zoom_to(d, obj, d->zs_z * pow(2.0, (mx - d->zs_mx) / 100.0), d->zs_mx, d->zs_my); }
    break;
  case DR_PICK: {
    int bg = (d->modifiers & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) && tool == TL_EYEDROPPER;
    if (doc && x >= 0 && y >= 0 && x < doc->w && y < doc->h) pick(d, obj, x, y, bg);
    else if (doc && tool == TL_EYEDROPPER) {
      /* 1.1: dragged off the picture, the colour on the screen there (panels, other windows) */
      LONG c = screen_pixel(_window(obj), mx, my);
      if (c >= 0) { if (bg) doc->bg = (u32)c; else doc->fg = (u32)c; set(obj, MUIA_Canvas_Picked, TRUE); }
    }
    break; }
  case DR_STROKE:
    if (doc->guides.show_grid && (doc->guides.snap & SNAP_GRID)) grid_snap_paint(doc, &x, &y);   /* (1.1) */
    if (d->stroke) {
      double tx, ty, pr = tablet_pressure(&tx, &ty);       /* (1.2: the pen's pressure and tilt) */
      stroke_tilt(d->stroke, tx, ty);
      stroke_to_p(d->stroke, x, y, pr, &ch); d->sbox = box_union(d->sbox, ch); mask_painted(doc); update_box(d, obj, ch);
    }
    d->bx = x; d->by = y;
    break;
  case DR_LASSO:
    ov_hide(d, obj);
    if (fabs(to_sx(d, obj, d->poly[d->npoly * 2 - 2]) - mx) + fabs(to_sy(d, obj, d->poly[d->npoly * 2 - 1]) - my) >= 2) lasso_add(d, x, y);
    ov_show(d, obj);
    break;
  case DR_XFORM:
    ov_hide(d, obj);
    xf_drag(d, x, y, d->qual);
    xf_info(d);
    ov_show(d, obj);
    DoMethod(optbar, MUIM_Opts_Refresh);
    break;
  case DR_SAMPLE:
    /* 0.8: the sampler follows the pointer, within the picture */
    ov_hide(d, obj);
    if (d->samp_drag >= 0 && d->samp_drag < v->nsamp) {
      v->samp_x[d->samp_drag] = x < 0 ? 0 : x >= doc->w ? doc->w - 1 : (LONG)x;
      v->samp_y[d->samp_drag] = y < 0 ? 0 : y >= doc->h ? doc->h - 1 : (LONG)y;
    }
    ov_show(d, obj);
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
  case DR_TYPEBOX:
    ov_hide(d, obj); d->bx = x; d->by = y; ov_show(d, obj);
    break;
  case DR_TYPESEL:
    if (type_editing && ty.tl) { ov_hide(d, obj); ty.caret = text_hit(ty.tl, x, y); ov_show(d, obj); }
    break;
  case DR_PATH:
    ov_hide(d, obj);
    if (doc->guides.snap && tool == TL_PEN) snap_point(doc, &x, &y, SNAP_DOC);
    path_motion(d, obj, x, y);
    ov_show(d, obj);
    break;
  case DR_SHAPE: case DR_LINE: case DR_MOVE:
    ov_hide(d, obj);
    if (doc->guides.snap && d->drag != DR_MOVE) snap_point(doc, &x, &y, SNAP_DOC);
    d->bx = x; d->by = y;
    d->shape_centre = d->drag == DR_SHAPE && (d->qual & QALT) != 0;        /* (1.1: Alt: from the centre) */
    if ((d->modifiers | (d->drag == DR_SHAPE ? d->qual : 0)) & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) {
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
    if (d->prev_on) prev_make(d);
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
  if (t2_release(d, obj, doc, drag)) { ov_show(d, obj); return; }   /* 1.2 */
  switch (drag) {
  case DR_STROKE:
    stroke_timer_off(d, obj);
    if (d->stroke && (tool == TL_REMOVE || (tool == TL_SPOTHEAL && opt.spot_type == SPOT_CONTENT))) {
      /* 1.2: the content-aware fill at the stroke's end, with progress and Abort */
      set(_app(obj), MUIA_Application_Sleep, TRUE);
      job_begin(doc, tool_names[tool]);
      stroke_end(d->stroke); d->stroke = NULL;
      job_end();
      set(_app(obj), MUIA_Application_Sleep, FALSE);
      update_box(d, obj, d->sbox);
    }
    if (d->stroke) { stroke_end(d->stroke); d->stroke = NULL; mask_painted(doc);
      /* (smoothing finishes the stroke at the pointer; an indexed or grey picture was brought back to its colours) */
      if (brushui.b.dyn.smoothing || brushui.b.dyn.pulled_string) DoMethod(obj, MUIM_Canvas_Update, 0, 0, doc->w, doc->h);
      else if (doc->mode != MODE_RGB || tool == TL_HEAL || tool == TL_SPOTHEAL) update_box(d, obj, d->sbox);   /* (1.0: healed as it ends) */
    }
    d->lastx = d->bx; d->lasty = d->by; d->has_last = 1;
    tell_changed(d, obj);
    break;
  case DR_PICK: break;
  case DR_ZOOMSCRUB:
    if (!d->zs_moved) { int out = d->space ? (d->modifiers & QALT) != 0 : opt.zoom_out ^ ((d->modifiers & QALT) != 0); ov_hide(d, obj); zoom_to(d, obj, zoom_step(d->z, out ? -1 : 1), d->zs_mx, d->zs_my); }
    break;
  case DR_STRAIGHTEN:
    /* 1.1: the Crop tool's Straighten: the picture turned so the line is level, and cropped */
    d->drag = DR_STRAIGHTEN; ov_hide(d, obj); d->drag = DR_NONE;
    if (fabs(d->bx - d->ax) + fabs(d->by - d->ay) >= 4 && image_straighten(doc, d->ax, d->ay, d->bx, d->by)) {
      opt.crop_straighten = 0;
      d->crop_on = 0; crop_pending = 0;
      changed(d, obj);
      d->edges_ok = 0;
      draw_all(d, obj);
      DoMethod(optbar, MUIM_Opts_Refresh);
      tell_changed(d, obj);
      return;
    }
    break;
  case DR_SAMPLE: set(obj, MUIA_Canvas_Pointer, TRUE); break;
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
    if (tool == TL_PATCH) {
      /* 1.0: the selection healed from where it was dragged to */
      if (dx || dy) {
        ch = patch_heal(doc, dx, dy);
        if (!box_empty(ch)) { changed(d, obj); update_box(d, obj, ch); }
        else if (doc->lay[doc->active]->locked || doc->lay[doc->active]->kind != LK_PIXEL) ask("OK", "%s", tr("This layer can't be patched (it is locked or has no pixels)."));
      }
      tell_changed(d, obj);
      break;
    }
    if ((dx || dy) && doc->lay[doc->active]->live && !(d->modifiers & QALT)) {
      /* 0.9: a type or shape layer: its settings moved (it stays sharp) */
      double m[6];
      m[0] = 1; m[1] = 0; m[2] = d->bx - d->ax; m[3] = 0; m[4] = 1; m[5] = d->by - d->ay;
      live_move(d, obj, doc, doc->active, m);
    } else if (dx || dy) {
      /* 0.7: Alt-drag moves a copy (of the selected pixels, or of the whole layer) */
      if ((d->modifiers & QALT) && doc->lay[doc->active]->kind != LK_GROUP) ch = move_selection_copy(doc, dx, dy);
      else ch = doc->lay[doc->active]->kind == LK_GROUP ? doc_move_group(doc, doc->active, dx, dy) :
                doc->lay[doc->active]->link && !doc->sel ? doc_move_linked(doc, dx, dy) : move_layer(doc, dx, dy);   /* (1.2: linked layers go together) */
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
      else { shape_make(d, obj, doc); ch = box_make(0, 0, 0, 0); }
      if (!box_empty(ch)) { changed(d, obj); update_box(d, obj, ch); }
    }
    d->prev_on = 0;
    tell_changed(d, obj);
    break;
  case DR_SHAPE:
    d->drag = DR_SHAPE; ov_hide(d, obj); d->drag = DR_NONE;
    if (tool == TL_REDEYE) {
      /* 1.0: in the box dragged, or round the click */
      Box b;
      if (fabs(d->bx - d->ax) >= 3 && fabs(d->by - d->ay) >= 3) b = box_make((i32)floor(d->ax < d->bx ? d->ax : d->bx), (i32)floor(d->ay < d->by ? d->ay : d->by),
                                                                         (i32)ceil(d->ax < d->bx ? d->bx : d->ax), (i32)ceil(d->ay < d->by ? d->by : d->ay));
      else { i32 r = 40; b = box_make((i32)d->ax - r, (i32)d->ay - r, (i32)d->ax + r + 1, (i32)d->ay + r + 1); }
      ch = redeye_fix(doc, b, opt.redeye_pupil, opt.redeye_darken);
      if (!box_empty(ch)) { changed(d, obj); update_box(d, obj, ch); }
      else if (doc->lay[doc->active]->kind == LK_PIXEL && !doc->lay[doc->active]->locked) ask("OK", "%s", tr("No red pupil was found there: click on it, or drag a box round the eye."));
      tell_changed(d, obj);
      break;
    }
    if (fabs(d->bx - d->ax) >= 1 && fabs(d->by - d->ay) >= 1) shape_make(d, obj, doc);
    d->prev_on = 0;
    tell_changed(d, obj);
    break;
  case DR_TYPEBOX:
    /* a click: point text there; a drag: a paragraph in the box */
    d->drag = DR_TYPEBOX; ov_hide(d, obj); d->drag = DR_NONE;
    if (fabs(d->bx - d->ax) * d->z >= 6 && fabs(d->by - d->ay) * d->z >= 6)
      ty_new(doc, d->ax < d->bx ? d->ax : d->bx, d->ay < d->by ? d->ay : d->by, 1, fabs(d->bx - d->ax), fabs(d->by - d->ay));
    else ty_new(doc, d->ax, d->ay, 0, 0, 0);
    changed(d, obj);
    comp_free(d);
    draw_all(d, obj);
    tell_changed(d, obj);
    break;
  case DR_TYPESEL: tell_changed(d, obj); break;          /* (the Layers panel shows the layer being edited) */
  case DR_PATH:
    d->drag = DR_PATH; ov_hide(d, obj); d->drag = DR_NONE;
    path_release(d, obj);
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
  if (type_editing && doc && ty.doc == doc && ty_index() >= 0 && ty_key(d, obj, im)) return 1;    /* 0.9: typing */
  if (q & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND)) return 0;      /* the menus' shortcuts */
  if (code == 0x40 || code == (0x40 | IECODE_UP_PREFIX)) {
    /* space: the hand while it is held */
    int down = !(code & IECODE_UP_PREFIX);
    if (down != d->space) { ov_hide(d, obj); d->space = down; ov_show(d, obj); }
    return 1;
  }
  if (d->spring_code && (code & ~IECODE_UP_PREFIX) == d->spring_code) {
    /* 0.8: a tool key held: repeats change nothing; let go after half a second (or after using the
       tool) and the tool before comes back */
    if (!(code & IECODE_UP_PREFIX)) return 1;
    {
      ULONG ds = im->Seconds - d->spring_secs;
      LONG dm = (LONG)im->Micros - (LONG)d->spring_micros;
      long held = (long)ds * 1000 + dm / 1000;
      int prev = d->spring_prev;
      d->spring_code = 0;
      if ((held >= 500 || d->spring_used) && prev != tool) set_tool(prev);
    }
    return 1;
  }
  if ((code & ~IECODE_UP_PREFIX) == 0x00) { d->tilde = !(code & IECODE_UP_PREFIX); return 1; }   /* 1.1: ` (the key left of 1) */
  if (code & IECODE_UP_PREFIX) return 0;
  if (!(q & IEQUALIFIER_REPEAT)) {
    /* 0.8: a command's extra key (Edit > Keyboard Shortcuts) */
    ULONG id = keys_extra(key_char(im), code, q);
    if (id) { DoMethod(_app(obj), MUIM_Application_ReturnID, id); return 1; }
  }
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
      else if (tool == TL_BLUR || tool == TL_SHARPEN || tool == TL_SMUDGE) {
        /* (1.1: their Mode list has seven of the modes) */
        int k;
        for (k = 0; k < RETOUCH_MODES; k++) if (retouch_bm[k] == mode[i]) { opt.mode[tool] = k; DoMethod(optbar, MUIM_Opts_Refresh); }
      } else if (brushlike(tool) || tool == TL_GRADIENT) { opt.mode[tool] = mode[i]; DoMethod(optbar, MUIM_Opts_Refresh); }
      return 1;
    }
  }
  switch (code) {
  case 0x44: case 0x43:                         /* Return, Enter */
    if (d->xf_on) { xf_finish(d, obj, 1); return 1; }
    if (d->ml) { ml_finish(d, obj, 1); return 1; }
    if (d->poly_on) { poly_finish(d, obj, 1); return 1; }
    if (d->crop_on) { crop_apply(d, obj, 1); return 1; }
    if (d->pen_on) { ov_hide(d, obj); d->pen_on = 0; path_clear_sel(cur_path(doc, 0)); d->path_dirty = 1; ov_show(d, obj); return 1; }   /* 0.9: the Pen's path finished */
    return 0;
  case 0x45:                                    /* Esc */
    if (d->xf_on) { xf_finish(d, obj, 0); return 1; }
    if (d->ml) { ml_finish(d, obj, 0); return 1; }
    if (d->poly_on) { poly_finish(d, obj, 0); return 1; }
    if (d->crop_on) { ov_hide(d, obj); d->crop_on = 0; crop_pending = 0; ov_show(d, obj); return 1; }
    if (d->pen_on) { ov_hide(d, obj); d->pen_on = 0; path_clear_sel(cur_path(doc, 0)); d->path_dirty = 1; ov_show(d, obj); return 1; }
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
    if (d->poly_on && d->npoly > 1) { ov_hide(d, obj); d->npoly--; ov_show(d, obj); return 1; }   /* 1.1: the Polygonal Lasso's last corner taken back */
    if (PATH_TOOL(tool) && path_delete_sel(d, obj)) return 1;          /* 0.9: the chosen anchors deleted */
    DoMethod(_app(obj), MUIM_Application_ReturnID, (q & IEQUALIFIER_CONTROL) ? ID_FILLBG : (q & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) ? ID_FILLFG : ID_CLEAR);
    return 1;
  case 0x48: case 0x49: case 0x70: case 0x71:   /* 1.1: Page Up, Page Down (Ctrl: left, right), Home, End */
    if (code == 0x70) { v->sx = -1000000; v->sy = -1000000; }
    else if (code == 0x71) { v->sx = 1000000; v->sy = 1000000; }
    else if (q & IEQUALIFIER_CONTROL) v->sx += (code == 0x49 ? 1 : -1) * (_mwidth(obj) * 9 / 10);
    else v->sy += (code == 0x49 ? 1 : -1) * (_mheight(obj) * 9 / 10);
    draw_all(d, obj);
    return 1;
  case 0x54: case 0x55: case 0x6F:               /* 1.1: Shift-F5 Fill, Shift-F6 Feather, F12 Revert */
    if (code != 0x6F && !shift) return 0;
    DoMethod(_app(obj), MUIM_Application_ReturnID, code == 0x54 ? ID_FILL : code == 0x55 ? ID_FEATHER : ID_REVERT);
    return 1;
  case 0x4C: case 0x4D: case 0x4E: case 0x4F: {  /* the arrows: nudge (move tool) or scroll */
    LONG dx = code == 0x4E ? 1 : code == 0x4F ? -1 : 0, dy = code == 0x4D ? 1 : code == 0x4C ? -1 : 0;
    if (d->xf_on) { ov_hide(d, obj); xform_translate(&d->xf, dx * (shift ? 10 : 1), dy * (shift ? 10 : 1)); xf_info(d); ov_show(d, obj); DoMethod(optbar, MUIM_Opts_Refresh); }
    else if (tool == TL_MOVE && doc->lay[doc->active]->live) {
      double m[6];
      m[0] = 1; m[1] = 0; m[2] = dx * (shift ? 10 : 1); m[3] = 0; m[4] = 1; m[5] = dy * (shift ? 10 : 1);
      if (live_move(d, obj, doc, doc->active, m)) tell_changed(d, obj);
    } else if (tool == TL_MOVE) {
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
    /* 1.1: more of PhotoCraft's (CAT): Reselect, Fade, Last Filter, Clipping Mask, Auto Color, Black
       & White, Auto Contrast, Rulers, Grid, Guides, Snap, Extras, Save a Copy, Close All */
    switch (ToLower((ULONG)c)) {
    case 'd': if (shift) id = ID_RESELECT; break;
    case 'f': id = shift ? ID_FADE : alt ? ID_F_AGAIN : 0; break;
    case 'g': if (alt) id = ID_CLIP; break;
    case 'b': if (shift) id = alt ? ID_ADJ_BW : ID_AUTOCOLOR; break;
    case 'l': if (shift && alt) id = ID_AUTOCONTRAST; break;
    case 'r': id = ID_RULERS; break;
    case '\'': id = ID_SHOWGRID; break;
    case ';': id = ID_SHOWGUIDES; break;
    case ':': id = ID_SNAP; break;
    case 'h': id = ID_SHOWEDGES; break;
    case 's': if (alt) id = ID_SAVECOPY; break;
    case 'w': if (alt) id = ID_CLOSEALL; break;
    }
    if (id) { DoMethod(_app(obj), MUIM_Application_ReturnID, id); return 1; }
    switch (ToLower((ULONG)c)) {
    case 'z': id = shift ? ID_REDO : alt ? ID_STEPBACK : ID_UNDO; break;
    case 'y': id = ID_REDO; break;
    case 'x': id = ID_CUT; break;
    case 'c': id = alt ? ID_CANVASSIZE : shift ? ID_COPYMERGED : ID_COPY; break;
    case 'v': id = shift ? ID_PASTEINPLACE : ID_PASTE; break;
    case 'a': id = ID_SELALL; break;
    case 'd': id = ID_DESELECT; break;
    case 'i': id = alt ? ID_IMAGESIZE : shift ? ID_INVERSE : ID_ADJ_INVERT; break;
    case 'k': id = ID_PREFS; break;                                      /* 0.8: Preferences */
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
  if ((c == '{' || c == '}') && tool == TL_MAGLASSO) {
    /* r3: Shift-[ and Shift-] take the Magnetic Lasso's width to its limits (PhotoCraft #11701) */
    opt.ml_width = c == '}' ? 256 : 1;
    DoMethod(optbar, MUIM_Opts_Refresh);
    return 1;
  }
  if ((c == '[' || c == ']') && tool == TL_MAGLASSO) {
    opt.ml_width += c == ']' ? 1 : -1;
    if (opt.ml_width < 1) opt.ml_width = 1;
    if (opt.ml_width > 256) opt.ml_width = 256;
    DoMethod(optbar, MUIM_Opts_Refresh);
    return 1;
  }
  if (shift && (c == '+' || c == '_' || (c == '-' && shift)) && (brushlike(tool) || tool == TL_MOVE || tool == TL_BUCKET || tool == TL_GRADIENT)) {
    /* r3: Shift + and Shift - step through the blend modes: the tool's, or (Move tool) the layer's */
    int dir = c == '+' ? 1 : -1;
    Doc *doc = cur_doc();
    if (tool == TL_MOVE) { if (doc && doc->lay[doc->active]->kind != LK_GROUP) { doc_set_layer_props(doc, doc->active, NULL, -1, blend_step(doc->lay[doc->active]->blend, dir), -1); DoMethod(_app(obj), MUIM_Application_ReturnID, ID_CANVASCHG); } }
    else opt.mode[tool] = blend_step(opt.mode[tool], dir);
    DoMethod(optbar, MUIM_Opts_Refresh);
    return 1;
  }
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
  if (shift && code >= 0x01 && code <= 0x0A && brushlike(tool)) c = code == 0x0A ? '0' : '0' + code;   /* (1.1: Shift and a digit key) */
  if (c >= '0' && c <= '9' && (brushlike(tool) || tool == TL_GRADIENT || tool == TL_BUCKET)) {
    static OpacityKeys ok;
    /* r3: two digits typed quickly set the opacity exactly ("4", "5": 45 %; "00": 0 %) */
    int o = (q & QALT) || shift ? (c == '0' ? 100 : (c - '0') * 10) : opacity_key(&ok, c - '0', (unsigned long)im->Seconds * 1000UL + im->Micros / 1000);
    if (o < 1 && tool != TL_GRADIENT) o = 1;
    /* 1.1: Alt and a digit sets the Smoothing, Shift and a digit the Flow (PhotoCraft's keys) */
    if ((q & QALT) && brushlike(tool)) brushui.b.dyn.smoothing = c == '0' ? 100 : (c - '0') * 10;
    else if (shift && brushlike(tool)) opt.flow[tool] = o;
    else if (tool == TL_BUCKET) opt.fill_opacity = o; else opt.opacity[tool] = o;
    DoMethod(optbar, MUIM_Opts_Refresh);
    return 1;
  }
  if (c == '+' || c == '=') { zoom_to(d, obj, zoom_step(d->z, 1), -1, -1); return 1; }
  if (c == '-') { zoom_to(d, obj, zoom_step(d->z, -1), -1, -1); return 1; }
  if (c == 9) { DoMethod(_app(obj), MUIM_Application_ReturnID, shift ? ID_TOGGLERIGHT : ID_TOGGLEPANELS); return 1; }   /* Tab, Shift-Tab */
  if (c == 'x' || c == 'X') { DoMethod(_app(obj), MUIM_Application_ReturnID, ID_SWAPCOLORS); return 1; }
  if (c == 'd' || c == 'D') { DoMethod(_app(obj), MUIM_Application_ReturnID, ID_DEFCOLORS); return 1; }
  if (c == 'q' || c == 'Q') { DoMethod(_app(obj), MUIM_Application_ReturnID, ID_QUICKMASK); return 1; }   /* 0.7: Quick Mask */
  if ((c == 'f' || c == 'F') && tool_for_key('F') < 0) {
    /* 1.1: F steps through the screen modes: a window, Easel's own screen, full screen */
    static const ULONG sm[3] = { ID_SCR_OWN, ID_SCR_FULL, ID_SCR_WINDOW };
    DoMethod(_app(obj), MUIM_Application_ReturnID, sm[prefs.screen_mode % 3]);
    return 1;
  }
  /* a tool's letter: that tool, or (pressed again, or with Shift) the next tool sharing the letter */
  c = ToUpper((ULONG)c);
  if ((i = tool_for_key(c)) < 0) return 0;
  if (q & IEQUALIFIER_REPEAT) return 1;                 /* (a letter held: not round and round its tools) */
  if (tool_keys[tool] == c && (shift || i == tool)) {
    int k;
    for (k = 1; k < TL_N; k++) if (tool_keys[(tool + k) % TL_N] == c) { i = (tool + k) % TL_N; break; }
  }
  /* 0.8: spring-loaded: held, the tool is only for as long as the key is down */
  d->spring_code = code; d->spring_prev = tool; d->spring_used = 0; d->spring_secs = im->Seconds; d->spring_micros = im->Micros;
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
/* 1.1: the right mouse button over the picture (MUIM_ContextMenuBuild): the tool's menu
   (ctxmenu.c). The layers with something under the pointer are found for the Move tool's list (and
   Ctrl's, with any tool); with the Type tool a type layer there becomes the active one first */
static Object *canvas_context(struct Data *d, Object *obj, LONG mx, LONG my)
{
  Doc *doc = cur_doc();
  CtxState s;
  int under[CTX_MAXUNDER], n = 0, i;
  if (d->drag || type_editing) return NULL;
  ctx_state(&s);
  s.ctrl = (d->qual & IEQUALIFIER_CONTROL) != 0;
  if (doc) {
    double fx = to_ix(d, obj, mx), fy = to_iy(d, obj, my);
    if (fx >= 0 && fy >= 0 && fx < doc->w && fy < doc->h) {
      i32 x = (i32)fx, y = (i32)fy;
      for (i = doc->nlay - 1; i >= 0 && n < CTX_MAXUNDER; i--)
        if (doc->lay[i]->visible && layer_has(doc, doc->lay[i], x, y)) under[n++] = i;
    }
    if (s.tool == CK_TYPE && n && doc->lay[under[0]]->kind == LK_TEXT && under[0] != doc->active) {
      doc->active = under[0]; changed(d, obj); tell_changed(d, obj); ctx_state(&s);
    }
  }
  ctx_set_under(&s, under, n);
  {
    /* a tool without a menu of its own: the general one (menux.c, 1.2) */
    Object *m = ctx_show(CTX_CANVAS, &s);
    return m ? m : doc ? mx_context_menu() : NULL;
  }
}
static ULONG dispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct Data *d;
  switch (msg->MethodID) {
  case MUIM_ContextMenuBuild: return (ULONG)canvas_context(INST_DATA(cl, obj), obj, ((struct MUIP_ContextMenuBuild *)msg)->mx, ((struct MUIP_ContextMenuBuild *)msg)->my);
  case MUIM_ContextMenuChoice: return ctx_choice(obj, ((struct MUIP_ContextMenuChoice *)msg)->item);   /* (either menu's item: its command, as if from the menus) */
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->obj = obj; d->z = 1; d->zy = 1;
    poly_init(&d->prev); poly_init(&d->pp); d->path_dirty = 1;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = EV;
    d->ihn.ihn_Object = obj; d->ihn.ihn_Flags = MUIIHNF_TIMER; d->ihn.ihn_Method = MUIM_Canvas_Tick;
    d->ihn.ihn_stuff.ihn_timer.ihn_millis = 250;
    mset(d, obj, (struct opSet *)msg);
    ctx_attach(obj, CTX_CANVAS);                 /* (1.1: the right button's menu) */
    return (ULONG)obj;
  case OM_DISPOSE:
    d = INST_DATA(cl, obj);
    ctx_detach(obj, CTX_CANVAS);
    comp_free(d);
    if (d->rowbuf) FreeVec(d->rowbuf);
    if (d->view) FreeVec(d->view);
    if (d->xmap) FreeVec(d->xmap);
    free(d->edges); free(d->poly);
    if (d->ml) maglasso_free(d->ml);
    poly_free(&d->prev); poly_free(&d->pp); free(d->pa);
    if (type_editing) ty_free();
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
    v->sy = (LONG)(m->y * v->zoom * par_view_ratio(v->doc, vx_par_correct)) - _mheight(obj) / 2;
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
    pe_end(d);
    d->drag = DR_NONE;
    d->path_dirty = 1; d->prev_on = 0;
    if (type_editing && ty_index() < 0) ty_free();        /* 0.9: the layer being typed in has gone */
    if (d->pen_on && d->pen_doc != cur_doc()) d->pen_on = 0;
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
    if (type_editing) ty_end(d, obj, 1);
    else if (d->xf_on) xf_finish(d, obj, 1);
    else if (d->ml) ml_finish(d, obj, 1);
    else if (d->poly_on) poly_finish(d, obj, 1);
    else if (d->crop_on) crop_apply(d, obj, 1);
    else if (d->pq_on) pcrop_apply(d, obj, 1);             /* 1.2 */
    return 0;
  case MUIM_Canvas_TypeCmd: {
    struct MUIP_Canvas_TypeCmd *m = (struct MUIP_Canvas_TypeCmd *)msg;
    d = INST_DATA(cl, obj);
    if (m->code == TC_COMMIT && m->arg < 0) { if (type_editing) { ov_hide(d, obj); ty_end(d, obj, -1); ov_show(d, obj); } return 0; }
    ty_cmd(d, obj, (int)m->code, (int)m->arg);
    return 0; }
  case MUIM_Canvas_Paths:
    d = INST_DATA(cl, obj);
    ov_hide(d, obj);
    d->path_dirty = 1;
    if (d->pen_on && (!cur_path(cur_doc(), 0) || d->pen_doc != cur_doc())) d->pen_on = 0;
    ov_show(d, obj);
    return 0;
  case MUIM_Canvas_Cancel: case MUIM_Canvas_Tool:
    d = INST_DATA(cl, obj);
    if (type_editing) ty_end(d, obj, 1);                  /* 0.9: Esc or another tool: the typing kept */
    ov_hide(d, obj);
    if (!PATH_TOOL(tool) || msg->MethodID == MUIM_Canvas_Cancel) d->pen_on = 0;
    if (d->poly_on) { d->poly_on = 0; d->npoly = 0; }
    if (d->crop_on && (msg->MethodID == MUIM_Canvas_Cancel || tool != TL_CROP)) { d->crop_on = 0; crop_pending = 0; opt.crop_angle = 0; }
    if (d->pq_on && (msg->MethodID == MUIM_Canvas_Cancel || tool != TL_PCROP)) { d->pq_on = 0; pcrop_pending = 0; }   /* 1.2 */
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
