/*
 * live.h - what type and shape layers (Easel 0.9) share: drawing them for the compositor, saving
 * their settings as text (OpenRaster's easel: attributes), and moving them with the picture when
 * the canvas is cropped, resized, rotated or flipped (their settings are transformed, not their
 * pixels, so they stay sharp).
 */
#ifndef EASEL_LIVE_H
#define EASEL_LIVE_H

#include "doc.h"

/* the compositor's call: layer l's rendering (l->livepx, l->livebox) worked out from its settings
   and l->live_ok set. 0 if it couldn't be (out of memory; the old rendering is kept) */
int   live_render(Doc *d, Layer *l);
/* the settings as one string of key=value pairs joined by '&' (values %-escaped), malloc'd */
char *live_save(const Layer *l);
/* and back: l's live settings (kind LK_TEXT or LK_SHAPE) from such a string; 0 if unusable */
int   live_load(Layer *l, int kind, const char *s);
/* layer l's settings moved by the map m (x' = m0 x + m1 y + m2, y' = m3 x + m4 y + m5): a uniform
   scale changes sizes, anything else is kept as the text's transform or a custom shape's path.
   Its rendering is dropped, to be worked out again */
void  live_transform(Layer *l, const double *m);
/* every live layer and path of the document moved by m (the canvas operations call this, inside
   their undo step, after doc_note_all) */
void  live_remap(Doc *d, const double *m);
/* live layer i moved by m as an undo step of its own (the Move tool: a translation; Free Transform) */
int   doc_live_transform(Doc *d, int i, const double *m);
/* a live layer turned into pixels (Rasterize Type / Rasterize Shape): doc_rasterize does it */
#endif
