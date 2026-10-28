/*
 * filters10.h - Easel 1.0's filters: the rest of the Filter menu PhotoCraft has that Easel lacked, and
 * the options the older ones were missing. Like adjust.h's and filters2.h's, each works on the active
 * layer, through the selection (feathered edges blend), as one undo step, keeps the transparency when
 * the alpha is locked, and returns 1 if it changed anything. Random ones take a seed.
 */
#ifndef EASEL_FILTERS10_H
#define EASEL_FILTERS10_H

#include "doc.h"

/* ------------------------------------------------------------------ blur */
int flt_average(Doc *d);                                   /* the selection's average colour */
int flt_blur_more(Doc *d);                                 /* about three times Blur */
/* Radial Blur about cx, cy (canvas pixels): amount 1-100, zoom (else spin), quality 0 draft,
   1 good, 2 best (how many samples) */
int flt_radial_blur_ex(Doc *d, int amount, int zoom, i32 cx, i32 cy, int quality);
/* Tilt-Shift: a sharp band through cy (canvas pixels) at angle degrees, half as wide as focus
   pixels, the blur growing to radius pixels over transition pixels either side */
int flt_tilt_shift(Doc *d, int radius, i32 cy, int angle, int focus, int transition);
/* Iris Blur: sharp inside the ellipse at cx, cy with radii rx, ry (canvas pixels), blurring to
   radius over transition per cent of the radii beyond it */
int flt_iris_blur(Doc *d, int radius, i32 cx, i32 cy, i32 rx, i32 ry, int transition);

/* ------------------------------------------------------------------ sharpen */
/* Smart Sharpen: amount 1-500 %, radius x10 (1-640), noise reduction 0-100 %, remove 0 Gaussian
   blur, 1 lens blur, 2 motion blur at angle degrees */
int flt_smart_sharpen(Doc *d, int amount, int radius10, int noise, int remove, int angle);
int flt_sharpen_edges(Doc *d);                             /* sharpens only where there are edges */

/* ------------------------------------------------------------------ distort */
/* Displace by a map (mw x mh RGBA; red moves across, green up and down, grey both): scales
   -999..999 per cent of 128 pixels; stretch the map to the box (else tile it); wrap what moves out
   (else repeat the edge pixels) */
int flt_displace(Doc *d, const u8 *map, i32 mw, i32 mh, int hscale, int vscale, int stretch, int wrap);
/* Spherize: amount -100..100, mode 0 normal, 1 horizontal only, 2 vertical only */
int flt_spherize_ex(Doc *d, int amount, int mode);
/* Wave: generators 1-20, wavelength and amplitude ranges in pixels, scales 0-100 % across and up and
   down, type 0 sine, 1 triangle, 2 square, wrap (else repeat edge pixels) */
int flt_wave_ex(Doc *d, int generators, int wl_min, int wl_max, int amp_min, int amp_max, int hscale, int vscale, int type, int wrap, unsigned seed);
/* Lens Correction: distortion -100..100 (more takes out barrel distortion, less pincushion), the
   red/cyan and blue/yellow fringes -100..100, vignette -100..100 with its midpoint 0-100, scale 50-150 % */
int flt_lens_correction(Doc *d, int distortion, int ca_rc, int ca_by, int vignette, int midpoint, int scale);

/* ------------------------------------------------------------------ stylize */
/* Extrude: type 0 blocks, 1 pyramids; size 2-255, depth 1-255 pixels; random heights (else by
   brightness); solid front faces; incomplete blocks at the box's edge left alone */
int flt_extrude(Doc *d, int type, int size, int depth, int random, int solid, int mask_incomplete, unsigned seed);
/* Glowing Edges: edge width 1-14, brightness 0-20, smoothness 1-15 */
int flt_glowing_edges(Doc *d, int width, int brightness, int smoothness);
/* Plastic Wrap: highlight strength 0-20, detail 1-15, smoothness 1-15 */
int flt_plastic_wrap(Doc *d, int strength, int detail, int smoothness);
/* Find Edges' variants: 0 Sobel (as Find Edges), 1 Prewitt, 2 Laplacian, 3 Roberts; invert gives
   dark edges on white (as Find Edges), else light on black */
int flt_edge_detect(Doc *d, int method, int invert);

/* ------------------------------------------------------------------ other */
/* Maximum and Minimum with a shape: radius 1-100, round (else square) */
int flt_minmax(Doc *d, int radius, int maximum, int round);
/* Offset: what is left undefined 0 transparent, 1 the background colour bg, 2 the edge pixels
   repeated, 3 what moved out wrapped round */
int flt_offset_ex(Doc *d, i32 dx, i32 dy, int mode, u32 bg);
/* Color to Alpha: color taken out, leaving what differs from it as it would look over it */
int flt_color_to_alpha(Doc *d, u32 color);
/* HSB/HSL: 0 RGB to HSL, 1 HSL to RGB, 2 RGB to HSB, 3 HSB to RGB (hue, saturation and
   lightness or brightness in the red, green and blue channels) */
int flt_hsb_hsl(Doc *d, int mode);

/* ------------------------------------------------------------------ render */
/* Lighting Effects with a light type: 0 point, 1 spot (pointing angle degrees, cone 1-90 degrees),
   2 infinite (from angle degrees); at lx, ly, reaching radius pixels; intensity -100..100,
   colour; ambient -100..100; texture 0-100 (bumps from the brightness), gloss 0-100 */
int flt_lighting_ex(Doc *d, int type, i32 lx, i32 ly, int radius, int angle, int cone, int intensity, u32 color, int ambient,
                    int texture, int gloss);

/* ------------------------------------------------------------------ gallery effects */
/* Film Grain: grain 0-20, highlight area 0-20, intensity 0-10 */
int flt_film_grain(Doc *d, int grain, int highlight, int intensity, unsigned seed);
/* Cutout: levels 2-8, edge simplicity 0-10, edge fidelity 1-3 */
int flt_cutout(Doc *d, int levels, int simplicity, int fidelity);
/* Poster Edges: edge thickness 0-10, edge intensity 0-10, posterization 0-6 */
int flt_poster_edges(Doc *d, int thickness, int intensity, int posterize);
/* Diffuse Glow: graininess 0-10, glow amount 0-20, clear amount 0-20, in the background colour bg */
int flt_diffuse_glow(Doc *d, int graininess, int glow, int clear, u32 bg, unsigned seed);
/* Stained Glass: cell size 2-50, border thickness 1-20 (in fg), light intensity 0-10 */
int flt_stained_glass(Doc *d, int cell, int border, int light, u32 fg, unsigned seed);
/* Halftone Pattern: size 1-12, contrast 0-50, type 0 dot, 1 circle, 2 line; in fg on bg */
int flt_halftone_pattern(Doc *d, int size, int contrast, int type, u32 fg, u32 bg);
/* Photocopy: detail 1-24, darkness 1-50; in fg on bg */
int flt_photocopy(Doc *d, int detail, int darkness, u32 fg, u32 bg);
/* Texturizer: type 0 brick, 1 burlap, 2 canvas, 3 sandstone; scaling 50-200 %, relief 0-50, light angle */
int flt_texturizer(Doc *d, int type, int scaling, int relief, int angle, unsigned seed);
/* Mosaic Tiles: tile size 2-100, grout width 1-15, lighten grout 0-10 */
int flt_mosaic_tiles(Doc *d, int tile, int grout, int lighten);
/* Patchwork: square size 2-50, relief 0-25 */
int flt_patchwork(Doc *d, int square, int relief);
/* Spatter: spray radius 0-25, smoothness 1-15 */
int flt_spatter(Doc *d, int radius, int smoothness, unsigned seed);
#endif
