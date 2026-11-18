/*
 * selr3.h - round 3's selection commands, all without trained models: Select > Sky, Select >
 * Subject, and Select and Mask's Decontaminate Colors.
 *
 * Sky follows PhotoCraft's classical method (crates/algo/src/segment/sky.rs, MIT; see
 * Licenses.txt): at a reduced size every pixel gets a sky likelihood (a colour prior - blue and
 * bright, bright and neutral, or a bright warm glow - times smoothness), the sky grows from likely
 * pixels in the top rows through neighbours that stay likely and change colour gradually (or, when
 * the top is covered, from a large certain patch in the upper half), sky seen through gaps joins it
 * by its colour, small specks and holes are cleaned, and the mask is brought to full size with its
 * edge softened by a guided filter on the luminance.
 *
 * Subject finds what stands out: frequency-tuned saliency (Achanta et al. 2009: each pixel's
 * distance in Lab from the picture's mean colour, slightly blurred) weighted towards the middle,
 * thresholded at twice its mean; the largest region's box (a little grown) is then given to Object
 * Selection (selsmart.h), whose colour clusters and smoothing find the object's edge.
 */
#ifndef EASEL_SELR3_H
#define EASEL_SELR3_H

#include "doc.h"

/* the sky's soft mask (w * h, malloc'd), from what shows (merged); threshold 0-100 (higher is
   stricter), softness 0-100. NULL if no sky was found or out of memory */
u8  *sky_mask(Doc *d, int threshold, int softness);
/* Select > Sky by op (SEL_); 0 if no sky was found (or out of memory) */
int  sel_sky(Doc *d, int op, int threshold, int softness);
/* the box Select Subject hands to Object Selection; 0 if nothing stands out */
int  subject_box(Doc *d, Box *b);
/* Select > Subject by op; 0 if nothing was found */
int  sel_subject(Doc *d, int op);

/* Decontaminate Colors: layer i's partly masked pixels (its layer mask between 0 and 255) take the
   colour of the fully shown pixels near them, by amount 0-100 % (more where less is shown). One
   undo step; 0 if the layer has no mask or pixels (or out of memory) */
int  doc_decontaminate(Doc *d, int i, int amount);
#endif
