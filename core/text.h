/*
 * text.h - the Text tool: a line (or lines) of text drawn with a TrueType face into a new layer,
 * through the same rasterizer Quill and Ledger use (ttraster.c).
 */
#ifndef EASEL_TEXT_H
#define EASEL_TEXT_H

#include "doc.h"
#include "fonts.h"

enum { TA_LEFT, TA_CENTER, TA_RIGHT };
/* the text's extent at px pixels to the em: width of the widest line and the height of all of
   them (lines split at '\n'); 0 if the face can't be read */
int text_measure(Face *f, const char *utf8, int px, i32 *w, i32 *h);
/* utf8 drawn into a new layer above the active one, its first line's top-left (or centre, or right,
   by align) at x, y, in color; antialiased or hard edged. The layer is named after the text and
   becomes the active one; one undo step. Returns the area drawn (empty if nothing could be) */
Box text_layer(Doc *d, Face *f, const char *utf8, int px, u32 color, i32 x, i32 y, int align, int antialias);
#endif
