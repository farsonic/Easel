/*
 * retouch.h - Easel 1.0's retouching: the Healing Brush, the Spot Healing Brush (Proximity Match),
 * the Patch tool and the Red Eye tool.
 *
 * Healing is a Poisson blend: the texture (the detail) comes from the source, the colour and
 * brightness from round the edge of the area healed. In the area, the result is the source plus
 * a smooth "membrane" that takes the difference between destination and source at the edge and
 * spreads it inwards (a harmonic interpolation: Laplace's equation solved by push-pull and then
 * relaxation, quick enough for brush-sized areas on a 68040).
 *
 * The brushes themselves are stroke.c's (PT_HEAL paints like the Clone Stamp while the button is
 * down, PT_SPOTHEAL shows where it has been); when the stroke ends, the area it covered is healed.
 */
#ifndef EASEL_RETOUCH_H
#define EASEL_RETOUCH_H

#include "doc.h"

/* the blend on bw x bh RGBA buffers: base (the destination before), src (the source pixels as they
   would be laid on it), mask 0-255 (how much each pixel is healed; 0 on the border keeps it the
   edge). out gets base with the healed area (out may be base). 0 if out of memory */
int heal_blend(u8 *out, const u8 *base, const u8 *src, const u8 *mask, i32 bw, i32 bh);
/* Proximity Match: the mask's area of px (canvas w x h RGBA) filled from the pixels around it. The
   area box is b (canvas pixels, the mask's bw x bh, with a margin round what it covers); a patch
   of the same shape is looked for nearby whose surroundings match best, and healed in. out: bw x bh
   RGBA (the box with the area filled). 0 if out of memory */
int spot_heal_fill(const u8 *px, i32 w, i32 h, Box b, const u8 *mask, u8 *out);

/* on a document's active layer, each one undo step; the area changed is returned (empty if none) */
/* the selection healed from the part dx, dy away (the Patch tool, Source mode) */
Box patch_heal(Doc *d, i32 dx, i32 dy);
/* Red Eye: the red pupil in box b found and darkened: pupil 1-100 (how much of the red around the
   reddest spot is taken as the pupil), darken 1-100 */
Box redeye_fix(Doc *d, Box b, int pupil, int darken);
/* the same on its own: px (w x h RGBA) changed in box b, through sel (w x h, or NULL); 1 if a pupil
   was found */
int redeye_pixels(u8 *px, i32 w, i32 h, Box b, int pupil, int darken, const u8 *sel);
#endif
