/*
 * layerx.h - Easel 1.2's layer extras: vector masks, linked layers, pattern fill layers and the
 * Pattern Overlay effect, smart objects kept on disk, artboards and frames.
 *
 * The settings live in each Layer's x block (doc.h): one flat malloc'd LayerX followed by what it
 * points to by offset (the vector mask's path as SVG path data, the pattern's RGBA pixels), so
 * copying, undo (doc_note_x) and saving treat it as one piece. What is worked out from it (the
 * vector mask's coverage, combined with the layer mask) is the layer's xc cache.
 *
 * Smart objects are memory-conscious: the source picture is kept as a PNG file in a drawer (the
 * undo scratch drawer, or wherever a linked file is) and only read back while the layer is redrawn
 * after a transform; the layer's own pixels are its rendering, so it costs no more memory than a
 * pixel layer. Every transform is worked out from the source, so scaling down and up again loses
 * nothing.
 *
 * Every doc_ function here is one undo step and returns 0 when it did nothing.
 */
#ifndef EASEL_LAYERX_H
#define EASEL_LAYERX_H
#include "doc.h"

struct Path; struct Pattern;

#define LAYERX_MAGIC 0x4C595258UL     /* "LYRX" */
typedef struct {
  u32 magic, len;                     /* len: the whole block's size */
  /* the vector mask: a path (SVG path data at vm_off, vm_len bytes with its NUL) whose inside shows */
  u8 vm_has, vm_on, vm_linked, vm_invert;   /* invert: the outside shows (Hide All is an empty inverted path) */
  u16 vm_feather; u8 vm_density, pad0;      /* feather in pixels; density 0-100 (0 means 100) */
  u32 vm_off, vm_len;
  /* a pattern: a pattern fill layer's (FillParams kind 2) and Pattern Overlay's (RGBA at pat_off) */
  u32 pat_off; i32 pat_w, pat_h;
  u16 pat_scale; i16 pat_ox, pat_oy;  /* scale per cent (0: 100), where the tiles start */
  u8 po_on, po_blend, po_opacity, po_pad;   /* Pattern Overlay: on, BM_, 0-255 */
  /* a smart object: its source (so_w x so_h) on disk at so_path, shown through the map so_m (source
     pixel to canvas: x' = m0 x + m1 y + m2, y' = m3 x + m4 y + m5) */
  u8 so_on, so_linked, so_pad[2];     /* linked: so_path is the user's file, not a copy in the scratch drawer */
  i32 so_w, so_h;
  double so_m[6];
  char so_path[256];
  char so_name[64];
  /* a frame (a group made by the Frame tool): its shape and stroke */
  u8 fr_kind, fr_pad; u16 fr_stroke;  /* FR_; stroke width in pixels (0 none) */
  u32 fr_color;                       /* the stroke's colour 0xRRGGBB */
  Box fr_box;
} LayerX;

void layerx_init(void);               /* the compositor's hooks set (doc.h); called by the tests and main.c */
const LayerX *layerx_get(const Layer *l);   /* the block, NULL for none */

/* ------------------------------------------------------------------ vector masks */
enum { VM_REVEAL_ALL, VM_HIDE_ALL, VM_PATH };
/* Layer > Vector Mask > Reveal All, Hide All, Current Path (p: the path, copied) */
int  doc_vmask_add(Doc *d, int i, int how, const struct Path *p);
int  doc_vmask_delete(Doc *d, int i);
int  doc_vmask_set(Doc *d, int i, int on, int linked, int feather, int density);   /* -1 unchanged */
int  doc_vmask_rasterize(Doc *d, int i);    /* into the layer mask (made if there is none), the vector mask deleted */
struct Path *layer_vmask_path(const Layer *l);   /* a copy of the path, NULL for none */
int  layer_has_vmask(const Layer *l);
/* the vector mask's coverage at x, y (255 shows), without the layer mask; 255 when there is none */
int  layer_vmask_at(Doc *d, Layer *l, i32 x, i32 y);

/* ------------------------------------------------------------------ linked layers */
int  doc_link_layers(Doc *d);         /* the picked layers (and the active one) linked: one new number */
int  doc_unlink_layers(Doc *d);       /* the picked layers (or the active one) unlinked */
int  doc_select_linked(Doc *d);       /* the layers linked to the active one picked; how many */
/* the Move tool: the active layer moved, and every layer linked to it (each its pixels, mask and
   live settings); one step */
Box  doc_move_linked(Doc *d, i32 dx, i32 dy);

/* ------------------------------------------------------------------ patterns */
/* a pattern fill layer above the active one (Layer > New Fill Layer > Pattern); its index or -1 */
int  doc_new_pattern_fill(Doc *d, const struct Pattern *p, int scale, const char *name);
int  doc_set_pattern_fill(Doc *d, int i, const struct Pattern *p, int scale);    /* p NULL: kept */
/* Layer Style > Pattern Overlay: on/off, the pattern (NULL: kept), mode, opacity 0-255, scale % */
int  doc_set_pattern_overlay(Doc *d, int i, int on, const struct Pattern *p, int blend, int opacity, int scale);
/* the pattern pixel a layer's pattern shows at canvas x, y (RGBA), NULL for no pattern */
const u8 *layerx_pattern_at(const Layer *l, i32 x, i32 y);

/* ------------------------------------------------------------------ smart objects */
/* the drawer smart objects' sources are written to ("" : the current directory); set from undo_scratch */
extern char smart_drawer[256];
/* Convert to Smart Object: layer i's pixels (its content box) written to the drawer as a PNG; the
   layer keeps its pixels as the rendering. 0 if it can't be written */
int  doc_smart_convert(Doc *d, int i);
/* New Smart Object via Copy: a copy of layer i with a copy of its source; its index or -1 */
int  doc_smart_copy(Doc *d, int i);
/* the smart object transformed by m (canvas to canvas: x' = m0 x + m1 y + m2 ...), redrawn from its source */
int  doc_smart_transform(Doc *d, int i, const double *m);
int  doc_smart_reset(Doc *d, int i);          /* Reset Transform: as it was converted, at its first place */
int  doc_smart_replace(Doc *d, int i, const char *file);   /* Replace Contents (or Relink to File: linked) */
int  doc_smart_relink(Doc *d, int i, const char *file);
int  doc_smart_update(Doc *d, int i);         /* Update Modified Content: the source read again */
int  doc_smart_update_all(Doc *d);
int  doc_smart_export(Doc *d, int i, const char *file);    /* Export Contents: the source as PNG */
int  doc_smart_embed(Doc *d, int i);          /* Convert to Embedded: a copy in the drawer */
int  doc_smart_link(Doc *d, int i, const char *file);      /* Convert to Linked: the source written to file and linked */
int  doc_smart_rasterize(Doc *d, int i);      /* an ordinary pixel layer again (its file deleted when embedded) */
int  doc_smart_to_layers(Doc *d, int i);      /* Convert to Layers: the source as a pixel layer at its place, untransformed */
int  layer_is_smart(const Layer *l);
const char *layer_smart_path(const Layer *l); /* where its source is ("" none): Show in Drawer, Edit Contents */
/* a new file name for a source in smart_drawer (0 if none could be found); and a layer's source moved
   there (OpenRaster reading an embedded one) */
int  smart_new_file(char *out, size_t cap);
int  layerx_set_smart_path(Layer *l, const char *path);
/* Open as Smart Object: a new document of the picture at file, its one layer a smart object */
Doc *smart_open(const char *file, char *err, int errcap);

/* ------------------------------------------------------------------ artboards and frames */
/* a new artboard (a group clipped to box b, filled with bg unless bg is -1 for transparent) above
   the active layer; its index or -1 */
int  doc_new_artboard(Doc *d, Box b, long bg, const char *name);
int  doc_artboard_from_group(Doc *d, int g, Box b, long bg);
int  doc_artboard_set(Doc *d, int g, Box b, long bg);   /* moves the box (bg -2: kept) */
/* the artboard group a point is in (the topmost), -1 for none */
int  doc_artboard_at(const Doc *d, i32 x, i32 y);
enum { FR_NONE, FR_RECT, FR_ELLIPSE };
/* the Frame tool: a frame group of shape kind (FR_) over box b, with a grey placeholder inside and
   a stroke (width 0: none); its index or -1 */
int  doc_new_frame(Doc *d, Box b, int kind, int stroke, u32 color);
/* layer i placed in frame f, scaled to fill it (fit: to fit inside it) and centred; replaces what
   was in it. The frame's index after the move, -1 if it couldn't be */
int  doc_frame_place(Doc *d, int f, int i, int fit);
int  layer_is_frame(const Layer *l);

/* ------------------------------------------------------------------ saving (OpenRaster's easel: attributes) */
/* the block as text: nums (a version and the header's numbers), the vector mask's SVG path data and the
   smart object's source file (each malloc'd, "" when unused); the pattern's pixels point into the block.
   0 when the layer has no block */
typedef struct { char *nums, *vpath, *spath; const u8 *pat; i32 pw, ph; } LayerXText;
int  layerx_to_text(const Layer *l, LayerXText *t);
void layerx_text_free(LayerXText *t);
/* and back onto a layer being read (no undo): 0 if unusable */
int  layerx_from_text(Layer *l, const char *nums, const char *vpath, const char *spath, const u8 *pat, i32 pw, i32 ph);
#endif
