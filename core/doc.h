/*
 * doc.h - Easel's image document: a stack of layers, each the size of the canvas in 8-bit RGBA,
 * a selection (a mask, 0 out to 255 in), the colours in use, and undo.
 * Portable C99 (the native tests and vbcc for the Amiga).
 *
 * Layers are kept bottom first: lay[0] is the background. A layer's pixels are straight (not
 * premultiplied) RGBA, rows top down. The composite is worked out on demand for any rectangle
 * (doc_composite), so the screen never needs a copy of the whole image.
 */
#ifndef EASEL_DOC_H
#define EASEL_DOC_H

#include "util.h"

#define MAXLAYERS 100
#define MAXDIM 16384                  /* the largest width or height */

enum { BM_NORMAL, BM_MULTIPLY, BM_SCREEN, BM_OVERLAY, BM_DARKEN, BM_LIGHTEN, BM_DIFFERENCE, BM_ADD,
       BM_SUBTRACT, BM_DODGE, BM_BURN, BM_SOFTLIGHT, BM_HARDLIGHT, BM_EXCLUSION, BM_HUE, BM_SATURATION,
       BM_COLOR, BM_LUMINOSITY,
       /* 0.2.1: the rest of PhotoCraft's modes */
       BM_DISSOLVE, BM_LINEARBURN, BM_DARKERCOLOR, BM_LIGHTERCOLOR, BM_VIVIDLIGHT,
       BM_LINEARLIGHT, BM_PINLIGHT, BM_HARDMIX, BM_DIVIDE,
       BM_N,
       BM_PASS = 255 };                   /* a group's Pass Through (not a pixel blend) */
extern const char *const blend_names[BM_N];

typedef struct { i32 x0, y0, x1, y1; } Box;      /* x1, y1 exclusive; empty when x1 <= x0 */

/* what a layer is (0.3): pixels; a group, whose members are the layers just below it with a greater
   depth; an adjustment applied to what is below it; or a fill (solid colour or gradient) */
enum { LK_PIXEL, LK_GROUP, LK_ADJUST, LK_FILL };

/* an adjustment layer's settings (0.3): kind is one of AK_ (adjust.h), the numbers its settings in
   the order that adjustment's function takes them; curves and the gradient map keep their points */
typedef struct {
  u8 kind;
  int v[16];
  int npts; int pts[2 * 32];          /* curves: (x, y) pairs; gradient map: stops as (position, 0xRRGGBB);
                                         selective colour: 9 ranges x CMYK; levels: R, G, B rows of 5 */
} AdjParams;

/* a fill layer's settings (0.3): solid colour, or a gradient (paint.h's GR_ kinds) across the canvas */
typedef struct {
  u8 kind;                            /* 0 solid, 1 gradient */
  u32 c0, c1;
  int gkind, angle, scale, reverse;   /* gradient: GR_, degrees, per cent, reversed */
} FillParams;

typedef struct {
  char name[64];                      /* UTF-8 */
  u8 *px;                             /* w * h * 4 (NULL for a group, adjustment or fill layer) */
  u8 opacity;                         /* 0-255 */
  u8 blend;                           /* BM_ (BM_PASS for a group passes its members' modes through) */
  u8 visible, locked;                 /* locked: pixels can't be painted */
  u8 alpha_lock;                      /* painting keeps the layer's transparency */
  /* 0.3: live layers */
  u8 kind;                            /* LK_ */
  u8 depth;                           /* nesting: 0 at the top level, members of a group one more than it */
  u8 collapsed;                       /* a group shown closed in the Layers panel */
  u8 lock_pos, lock_all;              /* can't be moved; can't be changed at all */
  u8 fill_cut;                        /* 255 minus the fill opacity (so a zeroed layer has full fill) */
  u8 clip;                            /* clipped to the layer below (the base of a clipping group) */
  u8 *mask;                           /* layer mask w * h, 255 shows, 0 hides; NULL for none */
  u8 mask_on, mask_linked;            /* the mask is used; moves with the layer */
  u8 mask_fade;                       /* 255 minus the mask's density: 0 hides fully where the mask is black */
  u16 mask_feather;                   /* pixels, applied when compositing */
  u8 blendif[4];                      /* Blend If on grey: this layer's black, 255 - its white; the underlying
                                         black, 255 - its white (all 0: everything blends) */
  AdjParams adj;                      /* LK_ADJUST */
  FillParams fillp;                   /* LK_FILL */
  u8 picked;                          /* chosen in the Layers panel along with the active layer */
  u8 edit_mask;                       /* painting goes into the mask, not the pixels */
  /* worked out when needed, never saved: the feathered mask, and the adjustment's tables */
  u8 *maskfx; u8 maskfx_ok;
  void *adjprog; u8 adjprog_ok;
  /* 0.4: indexed documents */
  u8 *idx;                            /* w * h palette indices when the document is indexed, else NULL */
} Layer;

typedef struct UndoStep UndoStep;

/* document colour modes (0.4) */
enum { MODE_RGB, MODE_INDEXED, MODE_GRAY, MODE_BITMAP };

/* a colour-cycling range (0.4: IFF CRNG/CCRT/DRNG): palette entries lo..hi rotated rate times a
   second (rate in 1/16384ths of 60 Hz steps, as CRNG keeps it), forward or back */
typedef struct { u8 lo, hi, active, reverse; u16 rate; } CycleRange;
#define MAXCYCLES 16

/* 0.4: rulers, guides and grid (guides.h works with them): guide positions in image pixels, the
   grid's spacing (0: 16) and offset, what is shown and what snaps (guides.h's SNAP_ flags) */
#define MAXGUIDES 64
typedef struct {
  i32 h[MAXGUIDES]; int nh;           /* horizontal guides: their y */
  i32 v[MAXGUIDES]; int nv;           /* vertical guides: their x */
  u16 grid_x, grid_y;                 /* grid spacing in pixels (0: 16) */
  i16 grid_ox, grid_oy;               /* where the grid starts */
  u8 grid_sub;                        /* subdivisions per grid square (0 or 1: none) */
  u8 show_grid, show_guides, show_rulers, lock_guides;
  u8 snap;                            /* SNAP_ flags in effect */
  u16 snap_tol;                       /* how close snaps, in 1/16 image pixels (0: 8 px); the UI sets it per zoom */
} GuideSet;

typedef struct {
  i32 w, h;
  Layer *lay[MAXLAYERS]; int nlay;
  int active;                         /* the layer being worked on */
  u8 *sel;                            /* w * h, or NULL for no selection (everything) */
  Box selbox;                         /* the selection's extent */
  u32 fg, bg;                         /* 0xRRGGBB */
  u16 dpi;
  int dirty;
  UndoStep *undo, *redo;
  int undo_depth;
  long undo_bytes;                    /* kept within UNDO_BUDGET by forgetting the oldest steps */
  char last_action[64];               /* for the History panel and the Undo menu item */
  /* 0.4: colour mode, palette and the Amiga picture's display settings */
  u8 mode;                            /* MODE_ */
  int npal;                           /* entries used in pal (indexed), 2-256 */
  u8 pal[256 * 3];
  u8 pal_lock[256];                   /* 1: this entry is kept when the palette is remade */
  CycleRange cycle[MAXCYCLES]; int ncycle;
  u32 modeid;                         /* Amiga display ModeID (CAMG), 0 for none chosen */
  u8 aspect_x, aspect_y;              /* pixel aspect ratio (1:1 square); IFF BMHD's x/yAspect */
  /* 0.2.1: the last selection, for Select > Reselect (selmore.h; 8 bytes of size, then the mask) */
  u8 *lastsel;
  /* 0.4: guides and grid (not part of undo) */
  GuideSet guides;
} Doc;

#define UNDO_BUDGET (40L * 1024 * 1024)       /* the default for undo_budget */
extern long undo_budget;                       /* bytes of undo kept (a preference) */

Doc   *doc_new(i32 w, i32 h, u32 fill, int transparent);   /* one layer, "Background" */
/* a document whose one layer, "Background", is px (w * h * 4, malloc'd), taken over; NULL (px
   left to the caller) if out of memory */
Doc   *doc_adopt(i32 w, i32 h, u8 *px);
void   doc_free(Doc *d);
Layer *layer_new(Doc *d, const char *name);                 /* transparent, not added */
void   layer_free(Layer *l);
Layer *layer_copy(const Layer *l, i32 w, i32 h);
/* layers in and out of the stack (each an undo step) */
int    doc_add_layer(Doc *d, Layer *l, int at);             /* at: index (above that many); takes l */
int    doc_delete_layer(Doc *d, int i);
int    doc_move_layer(Doc *d, int from, int to);
int    doc_duplicate_layer(Doc *d, int i);
int    doc_merge_down(Doc *d, int i);
int    doc_flatten(Doc *d);
int    doc_merge_visible(Doc *d);
void   doc_set_layer_props(Doc *d, int i, const char *name, int opacity, int blend, int visible);  /* -1: unchanged */

/* the composite of every visible layer over transparency, in rect r (clipped to the canvas), as RGBA
   into out (stride bytes a row). Transparent pixels come out with alpha 0 */
void   doc_composite(Doc *d, Box r, u8 *out, long stride);
/* layers lo..hi-1 at nesting depth composited in r onto out (cleared first unless onto): what
   merging part of the stack needs (0.3) */
void   doc_composite_range(Doc *d, int lo, int hi, int depth, Box r, u8 *out, long stride, int onto);
/* the mask a layer's compositing uses (feathered when it asks), or NULL for none or turned off */
const u8 *layer_mask_used(Doc *d, Layer *l);
/* adjustment layers' tables: hooks set by adjlayer_init (adjust.c) */
extern void *(*adjlayer_make)(const AdjParams *p);
extern void (*adjlayer_free)(void *prog);
extern void (*adjlayer_row)(const void *prog, u8 *px, int n);
/* one blend: src over dst (both straight RGBA), with opacity 0-255 and mode */
void   blend_pixel(u8 *dst, const u8 *src, int opacity, int mode);
/* the same at canvas pixel x, y: Dissolve needs the position (blend_pixel treats it as Normal) */
void   blend_pixel_at(u8 *dst, const u8 *src, int opacity, int mode, i32 x, i32 y);

/* undo: a step begun, the regions of layers about to change noted, the step ended.
   doc_note_layer saves layer i's pixels in box r; doc_note_all saves the whole document
   (layers added, deleted, moved, resized). A step with nothing noted is dropped */
void   doc_begin(Doc *d, const char *action);
void   doc_note_layer(Doc *d, int i, Box r);
void   doc_note_mask(Doc *d, int i, Box r);                /* layer i's mask in box r (0.3) */
void   doc_note_sel(Doc *d);
void   doc_note_all(Doc *d);
/* 0.4: the palette, mode, cycle ranges, ModeID and aspect saved in the step (doc_note_all saves them
   too). Undoing a step that saved only these re-derives indexed layers' RGBA from their indices */
void   doc_note_pal(Doc *d);
void   doc_end(Doc *d);
int    doc_fade(Doc *d, int opacity, int blend);   /* Edit > Fade (0.6): the last change mixed back */
int    doc_undo(Doc *d);
int    doc_redo(Doc *d);
int    doc_discard_last(Doc *d);                           /* the last step undone and forgotten (a preview cancelled) */
int    doc_can_undo(Doc *d);
int    doc_can_redo(Doc *d);
const char *doc_undo_name(Doc *d);                          /* "" if nothing */
const char *doc_redo_name(Doc *d);
void   doc_forget_undo(Doc *d);
/* the history: the names of the undo steps, the oldest first (n of them) */
int    doc_history(Doc *d, const char **names, int max);
/* the steps that can be redone, the next first */
int    doc_redo_history(Doc *d, const char **names, int max);

/* boxes */
Box    box_make(i32 x0, i32 y0, i32 x1, i32 y1);
Box    box_clip(Box a, Box b);
Box    box_union(Box a, Box b);
int    box_empty(Box b);
Box    doc_box(Doc *d);                                     /* the whole canvas */

/* the selection: how much pixel x, y is selected (255 when there is no selection) */
static inline int sel_at(const Doc *d, i32 x, i32 y) { return d->sel ? d->sel[(long)y * d->w + x] : 255; }
#endif
