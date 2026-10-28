/*
 * channels.h - alpha channels (saved selections) and Quick Mask (Easel 0.7).
 *
 * A document keeps up to MAXCHANNELS alpha channels (Doc's chan, chan_name, nchan): 8-bit planes the
 * size of the canvas, 255 selected (white) and 0 not (black), like the selection. They go with the
 * picture through crops, resizes and rotations, are saved in OpenRaster files, and every change to
 * them is one undo step. Quick Mask turns the selection into a plane (Doc's qmask) that the brushes
 * paint in grey, and back.
 */
#ifndef EASEL_CHANNELS_H
#define EASEL_CHANNELS_H

#include "doc.h"
#include "paint.h"

/* mask m (w * h, taken; NULL means nothing) combined with the selection by op (SEL_ of paint.h), as
   one undo step called name. Every 0.7 selection maker goes through this */
void sel_apply_mask(Doc *d, u8 *m, int op, const char *name);

/* Select > Save Selection: into a new channel (into = -1, called name, or "Alpha N" when name is
   NULL or empty), or into channel into combined by op (SEL_REPLACE, SEL_ADD, SEL_SUBTRACT,
   SEL_INTERSECT). With no selection, everything is saved (white). The channel's index, -1 if there
   is no room or memory */
int  chan_save_selection(Doc *d, int into, int op, const char *name);
/* Select > Load Selection: channel ch (inverted if invert) made the selection by op. 0 if no such channel */
int  chan_load_selection(Doc *d, int ch, int op, int invert);
/* the Channels panel's commands; each returns 0 if nothing was done */
int  chan_delete(Doc *d, int ch);
int  chan_duplicate(Doc *d, int ch, const char *name);     /* the copy goes just above ch; its index */
int  chan_rename(Doc *d, int ch, const char *name);
int  chan_move(Doc *d, int from, int to);
int  chan_invert(Doc *d, int ch);                          /* Image > Adjustments > Invert on a channel */
/* a new channel from layer's red, green or blue, its luminosity or its alpha (Calculations, or
   Ctrl-clicking a colour channel in Photoshop's Channels panel); its index, -1 on failure */
enum { CH_RED, CH_GREEN, CH_BLUE, CH_LUMINOSITY, CH_ALPHA };
int  chan_from_layer(Doc *d, int layer, int which, const char *name);
/* a channel as a brush target (stroke_begin_target in paint.h): the brushes paint it in grey, and
   each part is noted for undo before it changes. 0 if no such channel */
int  chan_target(Doc *d, int ch, PaintTarget *t);

/* ------------------------------------------------------------------ Quick Mask */
/* enter: the selection (or everything, when there is none) becomes the Quick Mask plane d->qmask,
   and the selection is dropped while the mask is edited, so strokes aren't clipped by it. leave:
   the plane becomes the selection again (all black: nothing selected). Each is one undo step;
   0 if already in (or not in) Quick Mask mode or out of memory */
int  quickmask_enter(Doc *d);
int  quickmask_leave(Doc *d);
int  quickmask_active(const Doc *d);
/* the plane as a brush target: black takes away from the selection, white adds. 0 if not in Quick Mask mode */
int  quickmask_target(Doc *d, PaintTarget *t);
#endif
