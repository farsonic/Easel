/*
 * cafill.h - Easel 1.2's content-aware fill: an area filled with patches of the picture round it that
 * fit, found by PatchMatch (Barnes et al. 2009: random search and propagation of a nearest-neighbour
 * field) over a pyramid, coarse to fine, each level refined by a few rounds of searching and voting.
 *
 * It is used by Edit > Content-Aware Fill, the Remove tool, the Spot Healing Brush's Content-Aware
 * type, the Patch tool's Content-Aware mode and the Content-Aware Move tool. The work goes in bands
 * of rows and calls progress() as it goes, so the UI's progress window shows it and Abort stops it.
 * It is slow on a 68040: about a minute for a 100 x 100 pixel area at the default settings; smaller
 * areas take seconds. Only the box round the area (the area and a margin) is worked on.
 */
#ifndef EASEL_CAFILL_H
#define EASEL_CAFILL_H
#include "doc.h"

typedef struct {
  int patch;                          /* the patch's side, odd, 5-15 (0: 7) */
  int rounds;                         /* search-and-vote rounds at each level (0: 3) */
  int margin;                         /* pixels round the area it may take from (0: twice the area's size, at least 32) */
  u32 seed;
} CaOpts;
void cafill_init(void);               /* the Spot Healing Brush's Content-Aware hook (brushx.h) set */
/* img: w x h straight RGBA, changed in place where hole > 127, from pixels where hole is 0 and allow (if
   not NULL) > 127. 0 if out of memory, aborted (progress), or there is nothing to take from */
int  ca_fill_rgba(u8 *img, i32 w, i32 h, const u8 *hole, const u8 *allow, const CaOpts *o);
/* the same on a canvas-size buffer (w x h RGBA) for the area where hole (canvas-size) > 127: only the box
   round it is worked on. The box changed in *changed */
int  ca_fill_canvas(u8 *px, i32 w, i32 h, const u8 *hole, const u8 *allow, const CaOpts *o, Box *changed);

/* on the document (each one undo step; the area changed is returned, empty if nothing was done) */
/* Edit > Content-Aware Fill: the selection on the active layer; sample_all: matched against the merged
   picture's colours (the layer is still what changes) */
Box  doc_ca_fill(Doc *d, const CaOpts *o);
/* the Patch tool, Destination mode: the selection's pixels copied dx, dy away and healed in there (the
   selection goes with them); transparent: only their texture, not their colour */
Box  patch_dest(Doc *d, i32 dx, i32 dy, int transparent);
/* the Patch tool, Content-Aware: the selection filled from the place dx, dy away and round it; structure
   1-7 how strictly the patches keep the shapes there (the patch size), color 0-10 how much their colour is
   blended into the edge */
Box  patch_content(Doc *d, i32 dx, i32 dy, int structure, int color);
/* the Content-Aware Move tool: the selection moved (extend: copied) dx, dy, the hole it leaves filled
   and the edge of what was moved blended in; structure and color as above */
Box  doc_ca_move(Doc *d, i32 dx, i32 dy, int extend, int structure, int color);
#endif
