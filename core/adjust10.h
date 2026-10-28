/*
 * adjust10.h - Easel 1.0's adjustments (Image > Adjustments): the six Hue/Saturation ranges, Black &
 * White with six colour sliders and a tint, Replace Color, Shadows/Highlights and Match Color; the
 * Levels and Curves eyedroppers (black, grey and white point from a colour); per-channel Curves;
 * presets (built in, and saved to and loaded from text files).
 *
 * The ones an adjustment layer can be (Hue/Saturation, Black & White, Curves, Replace Color) keep
 * their settings in AdjParams as described below, and adjust.c's adj_prog_make compiles them through
 * adj10_pixfn and adj10_curves_lut. Shadows/Highlights and Match Color look at the pixels around
 * each one (or at another picture), so, as in Photoshop, they work on pixels only.
 *
 * AdjParams layouts (zero always means what Easel 0.9 did):
 *   AK_HUESAT       v[0..3] master hue, saturation, lightness, colorize; with npts == 15, pts holds
 *                   the six ranges (Reds, Yellows, Greens, Cyans, Blues, Magentas) as five numbers
 *                   each: hue -180..180, saturation and lightness -100..100, width (degrees either
 *                   side of the range's centre at full effect, 0-90) and fuzziness (degrees over
 *                   which it fades, 0-90)
 *   AK_BLACKWHITE   v[9] == 1: v[0..5] Reds, Yellows, Greens, Cyans, Blues, Magentas (-200..300
 *                   per cent), v[6] tint, v[7] tint hue 0-360, v[8] tint saturation 0-100;
 *                   v[9] == 0: Easel 0.9's red, green and blue weights in v[0..2]
 *   AK_CURVES       v[1] == 1: four curves (RGB, Red, Green, Blue): v[2 + c] points each (up to 8),
 *                   at pts[16 c] as (x, y) pairs; v[0] the channel the dialog last showed.
 *                   v[1] == 0: Easel 0.9's one curve in pts (npts points) for channel v[0]
 *   AK_REPLACECOLOR v[0] the colour 0xRRGGBB, v[1] fuzziness 0-200, v[2..4] hue, saturation, lightness
 */
#ifndef EASEL_ADJUST10_H
#define EASEL_ADJUST10_H

#include "doc.h"

#define CURVE10_MAX 8                  /* points in each of the four curves */

/* (adjust.c) a pixel function for the adjustment, and its arguments (a: 48 doubles); exact: apply
   it to each pixel rather than through a colour cube. 0 if the settings aren't one of 1.0's, 2 if
   they are but change nothing */
typedef void (*Adj10Fn)(u8 *p, const double *a);
int  adj10_pixfn(const AdjParams *p, Adj10Fn *fn, double *a, int *exact);
/* the four curves combined into three tables (R, G, B): the channel's curve, then the RGB one */
void adj10_curves_lut(const AdjParams *p, u8 *lut);
/* 1.0's defaults on top of adj_defaults' (ranges for Hue/Saturation, six-slider Black & White) */
void adj10_defaults(int kind, AdjParams *p);
/* Easel 0.9 Curves settings (one curve) as four curves; nothing if they are already */
void curves10_convert(AdjParams *p);

/* destructive forms, each one undo step on the active layer (1 if it changed anything) */
int adj_hue_saturation_ranges(Doc *d, const int *master4, const int *ranges30);
int adj_black_white6(Doc *d, const int *weights6, int tint, int tint_hue, int tint_sat);
int adj_replace_color(Doc *d, u32 color, int fuzziness, int hue, int sat, int light);
/* Shadows/Highlights: amounts 0-100 %, tonal widths 1-100 %, radii 0-2500 pixels; color correction
   -100..100 (how far the colours that change are made more or less saturated), midtone contrast
   -100..100. All amounts 0 and the rest 0 leave the picture as it is */
int adj_shadows_highlights(Doc *d, int sh_amount, int sh_tone, int sh_radius, int hl_amount, int hl_tone, int hl_radius,
                           int color, int midtone);
/* Match Color: the active layer's colours given the mean and spread of src's (w x h RGBA, NULL:
   only neutralize). luminance and intensity 1-200 (100 as matched), fade 0-100, neutralize: the
   colour cast taken out first */
int adj_match_color(Doc *d, const u8 *src, i32 sw, i32 sh, int luminance, int intensity, int fade, int neutralize);

/* eyedroppers: which 0 black point, 1 grey point, 2 white point, set from colour c (0xRRGGBB).
   Levels: per-channel input black or white, or gammas that make c grey; Curves (made four curves):
   a point in each colour channel's curve */
void adj_levels_pick(AdjParams *p, int which, u32 c);
void adj_curves_pick(AdjParams *p, int which, u32 c);

/* Auto (the Levels and Curves dialogs' button): each colour channel stretched, 0.1% of the pixels
   clipped at either end; hist4 is histogram()'s four tables */
void adj_levels_auto(AdjParams *p, const u32 *hist4);
void adj_curves_auto(AdjParams *p, const u32 *hist4);
/* AK_BRIGHTNESS with v[2] set: Photoshop's "Use Legacy" (brightness added, contrast linear) */
void adj10_brightness_lut(const AdjParams *p, u8 *lut);

/* presets: built in for Levels, Curves, Hue/Saturation and Black & White (adj_preset_count is 0 for
   other kinds), and as text ("EaselAdjust 1" files) */
int  adj_preset_count(int kind);
const char *adj_preset_name(int kind, int i);
void adj_preset_get(int kind, int i, AdjParams *p);
int  adj_preset_format(const AdjParams *p, char *out, int cap);     /* the length written (0: too small) */
int  adj_preset_parse(const char *text, AdjParams *p);              /* 1 if it was a preset */
int  adj_preset_save(const AdjParams *p, const char *path);         /* 0 on failure */
int  adj_preset_load(const char *path, AdjParams *p);               /* 0 if unreadable or not one */

/* Photo Filter's named filters (Photoshop's twenty) */
#define PHOTO_FILTERS 20
extern const char *const photo_filter_names[PHOTO_FILTERS];
extern const u32 photo_filter_colors[PHOTO_FILTERS];
#endif
