/*
 * filters2.h - more of the Filter menu (Easel 0.2.1). Like adjust.h's filters, each works on the
 * active layer, through the selection (feathered edges blend), as one undo step, keeps the layer's
 * transparency when its alpha is locked, and returns 1 if it changed anything.
 * Random filters take a seed, so the same seed gives the same picture (a preview matches the result).
 */
#ifndef EASEL_FILTERS2_H
#define EASEL_FILTERS2_H

#include "doc.h"

/* ------------------------------------------------------------------ render */
/* Clouds (as flt_clouds, c0 to c1) blended onto the layer in Difference mode */
int flt_difference_clouds(Doc *d, u32 c0, u32 c1, unsigned seed);
/* fibres in the foreground and background colours: variance 1-64 (how fast the colour changes
   along a fibre), strength 1-64 (how long and straight the fibres are) */
int flt_fibers(Doc *d, int variance, int strength, unsigned seed);
/* a lens flare centred at fx, fy (canvas pixels): brightness 10-300 per cent;
   lens 0 50-300mm zoom, 1 35mm prime, 2 105mm prime, 3 movie prime */
int flt_lens_flare(Doc *d, i32 fx, i32 fy, int brightness, int lens);
/* Lighting Effects, the simplest part: one omni light of colour at lx, ly reaching radius pixels,
   intensity -100..100, and ambient light -100..100 (0 leaves half the light everywhere) */
int flt_lighting_basic(Doc *d, i32 lx, i32 ly, int radius, int intensity, u32 color, int ambient);

/* ------------------------------------------------------------------ distort */
/* Polar Coordinates in the selection's box: to_polar 1 Rectangular to Polar (the top edge to the
   middle, the bottom edge round the outside), 0 Polar to Rectangular */
int flt_polar_coordinates(Doc *d, int to_polar);
/* Shear: pts are npts (position, offset) pairs, 0-255 each with position increasing: position down
   the box, offset 128 none, 0 half the box's width left, 255 half right (a smooth curve through them).
   wrap: what moves out comes back on the other side, or the edge pixels are repeated */
int flt_shear(Doc *d, const int *pts, int npts, int wrap);
/* ZigZag: amount -100..100, ridges 1-20, style 0 around the centre, 1 out from the centre, 2 pond ripples */
int flt_zigzag(Doc *d, int amount, int ridges, int style);
/* Glass (frosted): distortion 0-20 pixels, smoothness 1-15, scale 50-200 per cent */
int flt_glass(Doc *d, int distortion, int smoothness, int scale, unsigned seed);
/* Ocean Ripple: size 1-15, magnitude 0-20 pixels */
int flt_ocean_ripple(Doc *d, int size, int magnitude, unsigned seed);

/* ------------------------------------------------------------------ noise */
/* Dust & Scratches: each channel replaced by the median within radius (1-16) where it differs
   from it by more than threshold (0-255) */
int flt_dust_scratches(Doc *d, int radius, int threshold);
/* Reduce Noise: strength 0-10 (luminance noise), preserve 0-100 (details kept), color 0-100 (colour
   noise taken out) */
int flt_reduce_noise(Doc *d, int strength, int preserve, int color);

/* ------------------------------------------------------------------ blur */
/* Smart Blur: radius 1-20, threshold 0-255 (only neighbours this close in brightness are averaged);
   mode 0 normal, 1 edges only (white on black), 2 edges overlaid in white */
int flt_smart_blur(Doc *d, int radius, int threshold, int mode);
/* Surface Blur: radius 1-32, threshold 1-255 (Photoshop's weighting, 1 - |difference| / 2.5 threshold) */
int flt_surface_blur(Doc *d, int radius, int threshold);
/* Lens Blur, simple: a round iris of radius 1-64; pixels at least threshold (0-255) bright are made
   brightness (0-100) per cent brighter first, so highlights bloom into discs */
int flt_lens_blur(Doc *d, int radius, int brightness, int threshold);
/* Shape Blur: radius 1-64, shape 0 square, 1 diamond, 2 hexagon, 3 triangle, 4 disc */
int flt_shape_blur(Doc *d, int radius, int shape);
/* Spin Blur about cx, cy (canvas pixels) through angle degrees (1-360) */
int flt_spin_blur(Doc *d, i32 cx, i32 cy, int angle);

/* ------------------------------------------------------------------ pixelate */
int flt_crystallize(Doc *d, int cell, unsigned seed);                    /* cell 3-300 */
int flt_pointillize(Doc *d, int cell, unsigned seed);                    /* cell 3-300; gaps in the background colour */
/* Color Halftone: max_radius 4-127 pixels, screen angles in degrees for red, green and blue
   (Photoshop's defaults 108, 162, 90) */
int flt_color_halftone(Doc *d, int max_radius, int angle_r, int angle_g, int angle_b);
int flt_fragment(Doc *d);
/* Mezzotint: type 0 fine dots, 1 medium dots, 2 grainy dots, 3 coarse dots, 4 short lines,
   5 medium lines, 6 long lines, 7 short strokes, 8 medium strokes, 9 long strokes */
int flt_mezzotint(Doc *d, int type, unsigned seed);
int flt_facet(Doc *d);

/* ------------------------------------------------------------------ stylize */
/* Trace Contour: outlines in each channel where it crosses level (0-255); upper 1 outlines what
   is above the level, 0 what is below */
int flt_trace_contour(Doc *d, int level, int upper);
/* Diffuse: mode 0 normal, 1 darken only, 2 lighten only, 3 anisotropic */
int flt_diffuse(Doc *d, int mode, unsigned seed);
/* Tiles: ntiles 1-99 across the shorter side, max_offset 1-99 per cent of a tile; fill the gaps
   with 0 the background colour, 1 the foreground colour, 2 the inverse image, 3 the image */
int flt_tiles(Doc *d, int ntiles, int max_offset, int fill, unsigned seed);
/* Wind: method 0 wind, 1 blast, 2 stagger; from_left 1 blows from the left (streaks run right) */
int flt_wind(Doc *d, int method, int from_left, unsigned seed);

/* ------------------------------------------------------------------ video and other */
/* De-Interlace: the even rows (0, 2, ...) or odd rows replaced, by interpolating between the rows
   either side or duplicating one */
int flt_de_interlace(Doc *d, int eliminate_even, int interpolate);
/* NTSC Colors: chroma reduced so the composite signal stays within broadcast limits (NTSC: -20 to
   120 IRE); pal 1 uses PAL's wider limits (-33 to 133 per cent) */
int flt_ntsc_colors(Doc *d, int pal);
/* Custom: a 5 x 5 kernel (rows top down), the sum divided by scale (0 counts as 1) plus offset */
int flt_custom(Doc *d, const int kernel[25], int scale, int offset);
#endif
