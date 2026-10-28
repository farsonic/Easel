/*
 * matte.h - Layer > Matting (Easel 0.7): Defringe, Remove Black Matte and Remove White Matte, on the
 * active layer (within the selection, if any). Each is one undo step and returns the area changed
 * (empty if the layer can't be changed).
 */
#ifndef EASEL_MATTE_H
#define EASEL_MATTE_H

#include "doc.h"

/* the colour of the pixels within width (1-200) of the layer's transparent edge replaced by the
   colour of the nearest pixels further in, so a halo picked up from an old background goes; their
   transparency is kept */
Box layer_defringe(Doc *d, int width);
/* edge colours that were blended with black (or white) unblended: c = (c - matte (1 - a)) / a */
Box layer_remove_black_matte(Doc *d);
Box layer_remove_white_matte(Doc *d);
#endif
