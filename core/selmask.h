/*
 * selmask.h - Select > Focus Area and Select > Select and Mask (the classical parts: edge
 * refinement in a band round the edge, Smooth, Feather, Contrast and Shift Edge).
 */
#ifndef EASEL_SELMASK_H
#define EASEL_SELMASK_H

#include "doc.h"

/* Focus Area: where the active layer (or what shows, all_layers) is sharp. range 0-100 (higher
   takes in softer parts), noise 0-100 (fine texture below this counts as noise, not detail),
   soften: the edge feathered. A soft mask w * h (malloc'd), NULL if out of memory */
u8  *focus_area_mask(Doc *d, int range, int noise, int soften, int all_layers);
/* and made the selection by SEL_ op (one undo step); 0 if out of memory */
int  sel_focus_area(Doc *d, int range, int noise, int soften, int all_layers, int op);

/* Select and Mask's settings: radius 0-250 pixels (the band round the edge whose pixels are
   decided again by their colour: like the colours inside or like those outside), smooth 0-100,
   feather x10 pixels (0-2500), contrast 0-100 %, shift edge -100..100 % */
typedef struct { int radius, smooth, feather10, contrast, shift; } RefineParams;
/* the selection refined, as a new mask (malloc'd; NULL if out of memory). With no selection,
   nothing is selected to refine and NULL is returned */
u8  *refine_mask(Doc *d, const RefineParams *r);
/* and made the selection (one undo step); 0 if nothing was selected or out of memory */
int  sel_refine(Doc *d, const RefineParams *r);
#endif
