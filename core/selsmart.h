/*
 * selsmart.h - Easel 1.2's Quick Selection and Object Selection tools, without trained models: both
 * grow regions by colour and edges.
 *
 * Quick Selection: what the brush passes over is the object; a few colour clusters are made from it
 * and from a ring round the brush that isn't selected (the background); the selection grows out
 * from the brush to the pixels nearer the object's colours than the background's, stopping at strong
 * edges and within a few brush widths of the stroke. Each drag step adds (or with Alt takes away).
 *
 * Object Selection: a box is drawn round the object; the inside and a band round the outside give the
 * object's and the background's colour clusters, the pixels in the box are classed, the classes
 * smoothed and the clusters made again (three rounds, as GrabCut iterates), and what is joined to the
 * middle of the box is kept, its holes filled. A few seconds on a 68040 for a box of 300 x 300 pixels.
 */
#ifndef EASEL_SELSMART_H
#define EASEL_SELSMART_H
#include "doc.h"

/* one drag step of Quick Selection along points xy[0..2n) with brush radius r; op SEL_ADD or
   SEL_SUBTRACT (the first step of a new selection SEL_REPLACE); all_layers samples the merged picture;
   enhance smooths the edge (Auto-Enhance). One undo step ("Quick Selection") */
void quick_select(Doc *d, const double *xy, int n, int r, int op, int all_layers, int enhance);
/* Object Selection in box b (canvas pixels), by op; hard_edge: no soft edge. 0 if nothing was found */
int  object_select(Doc *d, Box b, int op, int all_layers, int hard_edge);
#endif
