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
#include "fxdef.h"                   /* 1.1: layer styles */

#define MAXLAYERS 100
#define MAXDIM 16384                  /* the largest width or height */
#define MAXCHANNELS 24                /* 0.7: alpha channels (saved selections) a document can keep */
#define MAXPATHS 64                   /* 0.9: saved paths a document can keep */

enum { BM_NORMAL, BM_MULTIPLY, BM_SCREEN, BM_OVERLAY, BM_DARKEN, BM_LIGHTEN, BM_DIFFERENCE, BM_ADD,
       BM_SUBTRACT, BM_DODGE, BM_BURN, BM_SOFTLIGHT, BM_HARDLIGHT, BM_EXCLUSION, BM_HUE, BM_SATURATION,
       BM_COLOR, BM_LUMINOSITY,
       /* 0.2.1: the rest of PhotoCraft's modes */
       BM_DISSOLVE, BM_LINEARBURN, BM_DARKERCOLOR, BM_LIGHTERCOLOR, BM_VIVIDLIGHT,
       BM_LINEARLIGHT, BM_PINLIGHT, BM_HARDMIX, BM_DIVIDE,
       /* 1.1: Paint.NET's own modes */
       BM_REFLECT, BM_GLOW, BM_NEGATION, BM_XOR, BM_PDNBURN, BM_PDNDODGE,
       BM_N,
       BM_PASS = 255 };                   /* a group's Pass Through (not a pixel blend) */
extern const char *const blend_names[BM_N];

typedef struct { i32 x0, y0, x1, y1; } Box;      /* x1, y1 exclusive; empty when x1 <= x0 */

/* what a layer is (0.3): pixels; a group, whose members are the layers just below it with a greater
   depth; an adjustment applied to what is below it; or a fill (solid colour or gradient) */
enum { LK_PIXEL, LK_GROUP, LK_ADJUST, LK_FILL,
       LK_TEXT, LK_SHAPE };           /* 0.9: live type and shape layers (live.h, type.h, shape.h) */

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
  /* ---- 0.9: type and shape layers (px is NULL for them) ---- */
  void *live; u32 livelen;            /* LK_TEXT: a TextParams (type.h); LK_SHAPE: a ShapeParams with its path
                                         after it (shape.h). One flat malloc'd block, copied and freed whole */
  u8 *livepx; Box livebox;            /* the rendering (RGBA, livebox's size, in canvas pixels), worked out */
  u8 live_ok;                         /*   when needed: live_ok is cleared when the settings change */
  /* ---- end of 0.9 ---- */
  /* ---- 1.1: the layer's style (fx.h) and colour label ---- */
  LayerFx fx;
  u8 label;                           /* 0 none, 1-11 red, orange, yellow, green, blue, violet, grey, seafoam, indigo, magenta, fuchsia */
  u8 fx_ok;                           /* fxc matches the layer (cleared when its pixels or mask change) */
  void *fxc;                          /* the effects drawn (fx.c), worked out when needed, never saved */
  /* ---- end of 1.1 ---- */
  /* ---- 1.2 (layerx.h): vector mask, pattern fill and Pattern Overlay, smart object, frame settings in
     one flat malloc'd block (x, xlen bytes; NULL for none), copied and freed whole; doc_note_x saves it
     for undo. xc is what is worked out from it (the vector mask's coverage, the pattern scaled), never
     saved; xc_ok is cleared when the block changes ---- */
  void *x; u32 xlen;
  void *xc; u8 xc_ok;
  u16 link;                           /* linked layers: layers with the same nonzero number move together */
  u8 artboard;                        /* a group that is an artboard: its members are clipped to abox */
  Box abox;
  /* ---- end of 1.2 ---- */
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

/* 0.6: File Info, the picture's descriptive metadata (UTF-8, NUL-terminated; an empty string is
   unset). Written to and read from PNG text chunks, JPEG (COM and XMP), TIFF tags and XMP, the ORA
   stack and PSD's XMP; keywords are one string, separated by commas */
typedef struct {
  char title[128];
  char author[128];
  char description[512];
  char keywords[256];
  char copyright[256];
  char url[256];
} DocInfo;

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
  /* 0.6: File Info (not part of undo) */
  DocInfo info;
  /* ---- 0.7: alpha channels (saved selections, channels.h), Quick Mask and Transform Again ---- */
  u8 *chan[MAXCHANNELS];              /* w * h each, 255 selected; chan[0..nchan) */
  char chan_name[MAXCHANNELS][32];    /* UTF-8 */
  int nchan;
  u8 *qmask;                          /* the Quick Mask plane (w * h) while in Quick Mask mode, else NULL */
  double xf_again[9];                 /* the last transform about its box's centre (xform.h), for Transform Again */
  u8 xf_again_ok;
  /* ---- end of 0.7 ---- */
  /* ---- 0.9: vector paths (path.h): the saved paths and the work path ---- */
  struct Path *path[MAXPATHS];        /* path[0..npaths) */
  char path_name[MAXPATHS][32];       /* UTF-8 */
  int npaths;
  struct Path *workpath;              /* NULL when there is none */
  int path_active;                    /* the path the Paths panel has chosen: k + 1 for path[k], -1 the work path, 0 none */
  /* ---- end of 0.9 ---- */
  /* 1.1: Layer Style > Global Light (degrees; light_set 0 means the default 120, 30) */
  i16 light_angle; u8 light_alt, light_set;
  /* 1.2: notes, counts and slices (toolsx.h; one flat malloc'd block, NULL for none; not part of undo) */
  struct Annots *annot;
  /* ---- 1.2: colour management (cms.h): the document's ICC profile (NULL: untagged, taken as sRGB;
     malloc'd, kept by undo with the palette) and, in MODE_RGB, its colour model (cms.h's CM_: a CMYK or
     Lab document is edited as RGB and saved in its model) ---- */
  u8 *icc; u32 icclen;
  u8 cmode;
  /* round 3: the EXIF block an opened file had (a TIFF-structured block as JPEG's APP1 holds it after
     "Exif\0\0"; malloc'd, NULL for none; not part of undo), written back by filesx.h's exif_embed */
  u8 *exif; u32 exiflen;
} Doc;

#define UNDO_BUDGET (40L * 1024 * 1024)       /* the default for undo_budget */
extern long undo_budget;                       /* bytes of undo kept (a preference) */
/* 0.8: at most undo_levels steps are kept (a preference, 100 at first). With undo_scratch naming a
   drawer ("" for none), the oldest steps past undo_budget have their buffers written there (as
   EaselUndo-... files, up to undo_scratch_max bytes in all) instead of being forgotten, and read back
   when they are undone; undo_scratch_bytes is how much is there now */
extern int undo_levels;
extern char undo_scratch[256];
extern long undo_scratch_max, undo_scratch_bytes;
int    doc_spilled_steps(Doc *d);                  /* how many of d's steps are in the scratch drawer */

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
/* 0.7: the alpha channels and the Quick Mask plane: the whole set copied (channels added, deleted,
   renamed, Quick Mask entered or left; doc_note_all does it too), or a box of one plane before it is
   painted (channel k, or the Quick Mask plane) */
void   doc_note_channels(Doc *d);
void   doc_note_chan(Doc *d, int k, Box r);
void   doc_note_qmask(Doc *d, Box r);
/* 0.9: a live (type or shape) layer's settings as they are, before they change; and every path
   (the saved ones, their names and the work path). doc_note_all does both */
void   doc_note_live(Doc *d, int i);
void   doc_note_paths(Doc *d);
/* 1.2: layer i's x block (layerx.h) as it is, before it changes (also when it has none yet) */
void   doc_note_x(Doc *d, int i);
/* 1.2: hooks layerx.c sets (layerx_init): a layer's vector mask coverage (w * h, or NULL for none), and
   Pattern Overlay drawn on a row of a layer's content (row y from x0, n pixels; shape: its coverage) */
extern const u8 *(*layerx_vmask)(Doc *d, Layer *l);
extern void (*layerx_overlay_row)(Doc *d, Layer *l, i32 y, i32 x0, i32 n, u8 *acc, const u8 *shape, int keep);
extern void (*layerx_fill_row)(Doc *d, Layer *l, i32 y, i32 x0, i32 n, u8 *out);
extern void (*layerx_free_hook)(Layer *l);
extern void (*layerx_move_hook)(Doc *d, int i, i32 dx, i32 dy);
extern void (*layerx_remap_hook)(Doc *d, const double *m);       /* the canvas remapped by m (live_remap) */   /* layer i moved by dx, dy (inside its step) */
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
