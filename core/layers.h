/*
 * layers.h - live layers (0.3): groups, layer masks, clipping, adjustment and fill layers, and the
 * operations on the stack that respect them (moving, duplicating, deleting and merging a group
 * with its members). Every operation is one undo step. See doc.h for how the stack is laid out.
 */
#ifndef EASEL_LAYERS_H
#define EASEL_LAYERS_H
#include "doc.h"

/* ------------------------------------------------------------------ the stack's shape */
int  layer_paintable(const Doc *d, int i);           /* a pixel layer that may be painted on (not locked, hidden) */
int  block_start(const Doc *d, int i);               /* the lowest index of i's block (a group's first member, else i) */
int  layer_parent(const Doc *d, int i);              /* the group i is in, or -1 */
int  layer_shown(const Doc *d, int i);               /* visible, and every group it is in visible */
int  layer_hidden_by_collapse(const Doc *d, int i);  /* inside a collapsed group (the Layers panel hides it) */

/* ------------------------------------------------------------------ groups */
int  doc_new_group(Doc *d, const char *name);        /* an empty group above the active layer; its index */
int  doc_group_layers(Doc *d);                       /* the picked layers (or the active one) into a new group */
int  doc_ungroup(Doc *d, int g);
/* block i moved so it sits just above layer below (an index before the move; -1 for the bottom),
   at depth (its members keep their nesting under it) */
int  doc_move_block(Doc *d, int i, int below, int depth);
/* Layer > Arrange: 0 to the front, 1 forward, 2 backward, 3 to the back (within its group) */
int  doc_arrange(Doc *d, int i, int how);

/* ------------------------------------------------------------------ masks */
enum { MK_REVEAL_ALL, MK_HIDE_ALL, MK_REVEAL_SEL, MK_HIDE_SEL, MK_FROM_ALPHA };
int  doc_mask_add(Doc *d, int i, int how);
int  doc_mask_delete(Doc *d, int i, int apply);      /* apply: the mask goes into the layer's alpha first */
int  doc_mask_set(Doc *d, int i, int on, int linked, int fade, int feather);   /* -1: unchanged */
int  doc_mask_invert(Doc *d, int i);
void doc_sel_from_mask(Doc *d, int i, int op);       /* SEL_ (paint.h) */
void layer_mask_changed(Layer *l);                   /* after the mask's pixels change (its feathered copy is redone) */

/* ------------------------------------------------------------------ clipping */
int  doc_set_clip(Doc *d, int i, int on);            /* Create/Release Clipping Mask */

/* ------------------------------------------------------------------ adjustment and fill layers */
int  doc_new_adjust(Doc *d, const AdjParams *p, const char *name);   /* above the active layer; its index */
int  doc_set_adjust(Doc *d, int i, const AdjParams *p);
int  doc_new_fill(Doc *d, const FillParams *f, const char *name);
int  doc_set_fill(Doc *d, int i, const FillParams *f);
int  doc_rasterize(Doc *d, int i);                    /* a fill layer (or a group, merged) into pixels */

/* ------------------------------------------------------------------ merging */
int  doc_merge_group(Doc *d, int g);                 /* a group and its members into one pixel layer */
int  doc_merge_picked(Doc *d);                       /* the picked layers into one (Merge Layers) */

/* ------------------------------------------------------------------ the picked layers (Layers panel multi-selection) */
void doc_pick_layer(Doc *d, int i, int how);               /* 0 only this, 1 toggle, 2 range from the active one */
int  doc_npicked(const Doc *d);

/* ------------------------------------------------------------------ aligning */
enum { AL_LEFT, AL_HCENTER, AL_RIGHT, AL_TOP, AL_VCENTER, AL_BOTTOM, AL_DIST_H, AL_DIST_V };
int  doc_align(Doc *d, int how);                      /* the picked pixel layers' content, to the selection or canvas */
int  layer_content_box(const Doc *d, int i, Box *b);  /* the non-transparent part of a pixel layer */
Box  doc_move_group(Doc *d, int g, i32 dx, i32 dy);   /* the Move tool on a group: all its layers */
#endif
