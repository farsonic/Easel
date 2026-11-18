/*
 * typex.h - Easel 1.2: Layer > Type > Convert to Shape and Update All Text Layers (typex.c).
 */
#ifndef EASEL_TYPEX_H
#define EASEL_TYPEX_H
#include "type.h"
#include "fx.h"
/* type layer i made a shape layer of its outlines, filled with its first run's colour; one undo
   step. 0 if it isn't a type layer, or for want of memory */
int doc_text_to_shape(Doc *d, int i);
/* every type layer's drawing thrown away (made again when next shown) and the font files closed;
   how many type layers there are */
int doc_update_text(Doc *d);
#endif
