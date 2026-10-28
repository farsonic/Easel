/*
 * adjust.h - Image > Adjustments and the Filter menu: each works on the active layer, through the
 * selection (feathered edges blend), as one undo step. Each returns 1 if it changed anything.
 */
#ifndef EASEL_ADJUST_H
#define EASEL_ADJUST_H

#include "doc.h"

/* adjustments */
int adj_brightness_contrast(Doc *d, int brightness, int contrast);        /* -150..150, -50..100 */
int adj_levels(Doc *d, int channel, int in_black, int in_white, double gamma, int out_black, int out_white);   /* channel: 0 RGB, 1 R, 2 G, 3 B */
int adj_curves(Doc *d, int channel, const int *pts, int npts);           /* pts: npts (x, y) pairs, 0-255, x increasing */
void curve_lut(u8 *t, const int *pts, int n);                           /* the curve as a 256-entry table (what adj_curves applies) */
int adj_hue_saturation(Doc *d, int hue, int saturation, int lightness, int colorize);   /* -180..180, -100..100, -100..100 */
int adj_color_balance(Doc *d, const int *shadows, const int *midtones, const int *highlights, int preserve);   /* each 3: cyan-red, magenta-green, yellow-blue, -100..100 */
int adj_exposure(Doc *d, double exposure, double offset, double gamma);
int adj_vibrance(Doc *d, int vibrance, int saturation);                  /* -100..100 */
int adj_black_white(Doc *d, int red, int green, int blue);               /* per cent each channel adds to the grey */
int adj_photo_filter(Doc *d, u32 color, int density, int preserve);
int adj_invert(Doc *d);
int adj_desaturate(Doc *d);
int adj_threshold(Doc *d, int level);
int adj_posterize(Doc *d, int levels);
int adj_auto_tone(Doc *d);                     /* each channel stretched (0.1% clipped) */
int adj_auto_contrast(Doc *d);                 /* all channels stretched together */
int adj_auto_color(Doc *d);                    /* shadows and highlights neutralised as well */
int adj_equalize(Doc *d);
/* a histogram of the active layer (within the selection): 4 x 256 counts (luminosity, R, G, B) */
void histogram(Doc *d, int all_layers, u32 *hist);

/* filters */
int flt_gaussian_blur(Doc *d, double radius);
int flt_box_blur(Doc *d, int radius);
int flt_motion_blur(Doc *d, double angle, int distance);
int flt_radial_blur(Doc *d, int amount, int zoom);                       /* spin, or zoom from the middle */
int flt_blur(Doc *d);
int flt_unsharp_mask(Doc *d, int amount, double radius, int threshold);
int flt_sharpen(Doc *d);
int flt_sharpen_more(Doc *d);
int flt_add_noise(Doc *d, double amount, int gaussian, int mono);
int flt_median(Doc *d, int radius);
int flt_despeckle(Doc *d);
int flt_emboss(Doc *d, double angle, int height, int amount);
int flt_find_edges(Doc *d);
int flt_high_pass(Doc *d, double radius);
int flt_mosaic(Doc *d, int cell);
int flt_twirl(Doc *d, double angle);
int flt_pinch(Doc *d, int amount);                                       /* -100..100 */
int flt_spherize(Doc *d, int amount);
int flt_ripple(Doc *d, int amount, int size);
int flt_wave(Doc *d, int wavelength, int amplitude);
int flt_clouds(Doc *d, u32 c0, u32 c1, unsigned seed);
int flt_minimum(Doc *d, int radius);
int flt_maximum(Doc *d, int radius);
int flt_offset(Doc *d, i32 dx, i32 dy, int wrap);
int flt_solarize(Doc *d);
int flt_oil_paint(Doc *d, int radius, int levels);
int flt_vignette(Doc *d, int amount);
#endif
