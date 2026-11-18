/*
 * toolopts.h - Easel 1.1's tool options: the pieces of PhotoCraft's tools that Easel 1.0 still
 * lacked (docs/photocraft-gap.md sections 1, 8 and 21):
 *
 *   - Dodge and Burn's Range (Shadows, Midtones, Highlights) and Protect Tones; the Sponge's
 *     Saturate mode and Vibrance (the stroke engine calls these for PT_DODGE, PT_BURN, PT_SPONGE);
 *   - sampling "Current & Below" for the Clone Stamp, the Healing Brush and the Eyedropper, and the
 *     Eyedropper's larger averages (11, 31, 51 and 101 pixels square);
 *   - the Crop tool's overlays (Rule of Thirds, Grid, Diagonal, Triangle, Golden Ratio);
 *   - the Move tool's Auto-Select Group: the outermost group a layer is in.
 *
 * The tone curves are ported from PhotoCraft (https://github.com/storytold/photocraft,
 * crates/algo/src/retouch.rs: tone_curve, dodge_burn, sponge, compress_gamut), Copyright (c) 2026
 * ArtCraft Team and the PhotoCraft contributors, used under the MIT License (see Licenses.txt).
 * Easel works them through 256-entry tables made once a stroke, so a dab costs no pow() per pixel.
 */
#ifndef EASEL_TOOLOPTS_H
#define EASEL_TOOLOPTS_H

#include "doc.h"

/* ------------------------------------------------------------------ Dodge, Burn, Sponge */
/* Brush.tone_range: TR_ALL is Easel 1.0's even lightening or darkening (a cleared Brush) */
enum { TR_ALL, TR_SHADOWS, TR_MIDTONES, TR_HIGHLIGHTS };
/* the transfer table of one tool at exposure 0-100: lut[v] is what v becomes at full coverage */
void tone_lut(u8 *lut, int range, int burn, int exposure);
/* RGB p (straight) dodged or burned through lut at coverage cov 0-255; with protect, the curve is
   applied to the luma and the colour scaled to it (hue and saturation kept, never clipped) */
void tone_apply(u8 *p, const u8 *lut, int cov, int protect);
/* the Sponge: p moved away from (saturate) or towards its grey by amount 0-255; vibrance holds back
   the change where it would clip (saturated colours saturate less, greys desaturate less) */
void tone_sponge(u8 *p, int amount, int saturate, int vibrance);

/* ------------------------------------------------------------------ sampling layers */
/* what the Eyedropper, Clone Stamp and Healing Brush sample (Brush.sample_all takes these too) */
enum { SAMPLE_CURRENT, SAMPLE_ALL, SAMPLE_BELOW };
/* the composite of the layers from the bottom up to and including layer upto (and the groups it is
   in), as doc_composite makes it of all of them */
void doc_composite_below(Doc *d, int upto, Box r, u8 *out, long stride);
/* the Eyedropper's sample sizes, a side in pixels (Point Sample is 1) */
#define NPICK_SIZES 7
extern const int pick_sizes[NPICK_SIZES];
/* the average colour of the size x size square centred on x, y (the pixels with any opacity), from
   the active layer, the layers below and it, or everything (SAMPLE_); 0xFFFFFFFF when all of it is
   transparent. The square is composited once, not pixel by pixel */
u32  doc_pick_ex(Doc *d, i32 x, i32 y, int from, int size);

/* ------------------------------------------------------------------ the Crop tool's overlay */
enum { CO_NONE, CO_THIRDS, CO_GRID, CO_DIAGONAL, CO_TRIANGLE, CO_GOLDEN, CO_N };
extern const char *const crop_overlay_names[CO_N];
/* the overlay's lines inside the box x0, y0 - x1, y1 as segments (x0, y0, x1, y1 each) into seg,
   at most max of them; how many. The grid's lines are grid apart (0: 32) */
int  crop_overlay_lines(int kind, double x0, double y0, double x1, double y1, double grid, double *seg, int max);

/* ------------------------------------------------------------------ the Move tool */
/* the layer Auto-Select picks for layer i: i itself, or with group the outermost group it is in */
int  auto_select_target(const Doc *d, int i, int group);
/* ------------------------------------------------------------------ painting on the grid */
/* DPaint's grid snap for the painting tools: x, y moved to the nearest grid point (the picture's
   grid spacing and offset, 16 pixels when unset; at the pixel's centre, where a dab lands) */
void grid_snap_paint(const Doc *d, double *x, double *y);

/* ------------------------------------------------------------------ DPaint's ranges and stencils */
struct Gradient;
/* a range fill's gradient: palette entries lo..hi (of npal, RGB triples) as evenly spaced colour
   stops (at most GRAD_MAXSTOPS: a longer range is sampled evenly, always with both ends), opaque */
int  gradient_from_palette(struct Gradient *g, const u8 *pal, int npal, int lo, int hi);
/* the palette range a range fill uses: the first active colour-cycling range, else the whole palette */
void doc_fill_range(const Doc *d, int *lo, int *hi);
/* a stencil's colours: STC_BACKGROUND the background colour, STC_LOCKED the palette entries locked
   in the Color Table (indexed pictures); into out (up to 256), how many */
enum { STC_OFF, STC_BACKGROUND, STC_LOCKED };
int  stencil_colors(const Doc *d, int kind, u32 *out);

/* ------------------------------------------------------------------ the History panel */
/* History > Delete: undo step k (0 the oldest) and every step after it taken away, the picture
   going back to the state before k (k -1: the last step done). 0 if there is no such step */
int  doc_delete_state(Doc *d, int k);
#endif
