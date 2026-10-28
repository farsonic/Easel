/*
 * selmore.h - more of the Select menu (Easel 0.2.1): Reselect, Grow and Similar.
 * Each is one undo step and returns 1 if the selection changed.
 */
#ifndef EASEL_SELMORE_H
#define EASEL_SELMORE_H

#include "doc.h"

/* the selection kept in d->lastsel for Reselect: call before the selection is dropped
   (sel_deselect does both). Nothing happens if there's no selection */
void sel_remember(Doc *d);
/* Select > Deselect, remembering the selection first */
void sel_deselect(Doc *d);
/* Select > Reselect: the last selection back (0 if none is kept, or the canvas has changed size) */
int  sel_reselect(Doc *d);
/* Select > Grow: the pixels joined to the selection whose colour (on the active layer) is within
   tolerance (0-255) of the range of colours already selected (at least half selected) */
int  sel_grow_similar(Doc *d, int tolerance);
/* Select > Similar: the same, but every such pixel in the layer, joined or not */
int  sel_similar(Doc *d, int tolerance);
#endif
