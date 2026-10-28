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
       BM_COLOR, BM_LUMINOSITY, BM_N };
extern const char *const blend_names[BM_N];

typedef struct { i32 x0, y0, x1, y1; } Box;      /* x1, y1 exclusive; empty when x1 <= x0 */

typedef struct {
  char name[64];                      /* UTF-8 */
  u8 *px;                             /* w * h * 4 */
  u8 opacity;                         /* 0-255 */
  u8 blend;                           /* BM_ */
  u8 visible, locked;                 /* locked: pixels can't be painted */
  u8 alpha_lock;                      /* painting keeps the layer's transparency */
} Layer;

typedef struct UndoStep UndoStep;

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
} Doc;

#define UNDO_BUDGET (40L * 1024 * 1024)

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
/* one blend: src over dst (both straight RGBA), with opacity 0-255 and mode */
void   blend_pixel(u8 *dst, const u8 *src, int opacity, int mode);

/* undo: a step begun, the regions of layers about to change noted, the step ended.
   doc_note_layer saves layer i's pixels in box r; doc_note_all saves the whole document
   (layers added, deleted, moved, resized). A step with nothing noted is dropped */
void   doc_begin(Doc *d, const char *action);
void   doc_note_layer(Doc *d, int i, Box r);
void   doc_note_sel(Doc *d);
void   doc_note_all(Doc *d);
void   doc_end(Doc *d);
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
