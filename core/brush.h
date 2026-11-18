/*
 * brush.h - Easel 0.5's brushes: the pieces a brush stroke is made from (paint.h's Brush and
 * stroke_*), and the files they live in.
 *
 *   Tip          a sampled tip: a greyscale mask (255 paints fully), scaled to the brush size;
 *   CustomBrush  a Deluxe Paint brush: a colour picture with a handle, picked up from the canvas,
 *                transformed (flip, rotate, scale, outline) and kept in ILBM files with GRAB;
 *   BrushDynamics the Brush Settings sections, named as PhotoCraft names them: Shape Dynamics,
 *                Scattering, Color Dynamics, Transfer, airbrush build-up, Smoothing, Symmetry;
 *   BrushPreset  a named brush (settings plus its own tip) in a list with built-in defaults,
 *                saved to and loaded from a simple text file, and read from Photoshop .abr files;
 *   Snapshot     a copy of a layer (or the merged picture) the History Brush paints back from.
 *
 * Every number in a setting is an int, and zero always means "off" or "as before 0.5", so a Brush
 * cleared with memset paints exactly as Easel 0.4 did.
 *
 * Parts ported from PhotoCraft (https://github.com/storytold/photocraft), Copyright (c) 2026
 * ArtCraft Team and the PhotoCraft contributors, used under the MIT License (see Licenses.txt):
 * the dynamics model and its rules (crates/paint/src/brush.rs, dynamics.rs), the counter-based
 * random numbers (rng.rs), symmetry (symmetry.rs), the built-in presets and procedural tips
 * (presets.rs, procedural.rs) and the .abr layout (crates/psd/src/abr.rs).
 */
#ifndef EASEL_BRUSH_H
#define EASEL_BRUSH_H

#include "doc.h"

/* ------------------------------------------------------------------ tips */
typedef struct { i32 w, h; u8 *a; } Tip;          /* w * h coverage, 255 full paint */
Tip  *tip_new(i32 w, i32 h);                       /* all zero; NULL if out of memory or too big */
Tip  *tip_copy(const Tip *t);
void  tip_free(Tip *t);
/* Define Brush Preset: the selection (or box b when nothing is selected) of the active layer, or
   of the merged picture, as a tip: dark and opaque paints, light or transparent doesn't (as
   Photoshop makes one). Trimmed to what paints; NULL if nothing does */
Tip  *tip_from_doc(Doc *d, Box b, int merged);
/* a tip from RGBA or grey pixels (bpp 4 or 1), dark paints (invert: light paints) */
Tip  *tip_from_pixels(const u8 *px, i32 w, i32 h, int bpp, int invert);
/* procedural tips for the built-in presets (PhotoCraft's procedural.rs): n x n, seeded */
enum { PTIP_CHALK, PTIP_SPATTER, PTIP_BRISTLE, PTIP_STAR, PTIP_LEAF, PTIP_GRASS, PTIP_N };
Tip  *tip_procedural(int kind, int n, u32 seed);

/* ------------------------------------------------------------------ DPaint custom brushes */
/* RGBA pixels (straight), w x h, handle hx, hy (the pixel that sits under the pointer; DPaint's
   GRAB). Transparent pixels (alpha 0) are the brush's background */
typedef struct { i32 w, h, hx, hy; u8 *px; } CustomBrush;
enum { CB_MATTE, CB_COLOR, CB_REPLACE, CB_SMEAR, CB_SHADE, CB_BLEND, CB_CYCLE, CB_TINT /* 1.2 */, CB_N };
extern const char *const cb_mode_names[CB_N];
CustomBrush *cbrush_new(i32 w, i32 h);             /* transparent, handle in the middle */
CustomBrush *cbrush_copy(const CustomBrush *b);
void         cbrush_free(CustomBrush *b);
/* picked up from the canvas: box b (or, with use_sel, the selection's pixels within it) of the
   active layer or the merged picture. With key_bg, pixels of colour key (0xRRGGBB, DPaint's
   background colour 0) become transparent. Trimmed to what is left; the handle in the middle.
   NULL if nothing is left */
CustomBrush *cbrush_pickup(Doc *d, Box b, int use_sel, int merged, int key_bg, u32 key);
/* transforms: each returns a new brush (NULL if out of memory) and leaves b as it was */
CustomBrush *cbrush_flip(const CustomBrush *b, int vertical);
CustomBrush *cbrush_rotate90(const CustomBrush *b, int quarter_turns);   /* clockwise */
CustomBrush *cbrush_rotate(const CustomBrush *b, double degrees);        /* any angle, clockwise; nearest pixel (no new colours) */
CustomBrush *cbrush_scale(const CustomBrush *b, i32 w, i32 h);           /* nearest pixel, as DPaint stretches */
CustomBrush *cbrush_half(const CustomBrush *b);
CustomBrush *cbrush_double(const CustomBrush *b);
/* DPaint's Outline: a one-pixel ring of colour (0xRRGGBB) round the brush's shape */
CustomBrush *cbrush_outline(const CustomBrush *b, u32 color);
/* 1.2: DPaint's Shear (each row moved across by kx times its distance from the handle, each column
   down by ky; nearest pixel) and Bend (rows moved across along a parabola, amount pixels at the
   ends; vertical: columns moved down) */
CustomBrush *cbrush_shear(const CustomBrush *b, double kx, double ky);
CustomBrush *cbrush_bend(const CustomBrush *b, double amount, int vertical);
/* the brush's shape as a tip (alpha), for painting it in a single colour */
Tip         *cbrush_to_tip(const CustomBrush *b);
/* ILBM brush files: FORM ILBM with BMHD, CMAP, GRAB and BODY; a mask plane (masking 1), or a
   transparent colour (masking 2) when there is a colour free for it. Up to 256 colours are kept
   exactly as an 8-plane (or fewer) picture; more are written as a 24-bit deep ILBM with a mask
   plane. Read: 1-8 planes (EHB too), 24 and 32 planes, masking 0-3, GRAB (the middle without one).
   cbrush_write returns the file malloc'd (*len its size), NULL if out of memory */
u8          *cbrush_write_ilbm(const CustomBrush *b, size_t *len);
CustomBrush *cbrush_read_ilbm(const u8 *data, size_t n, char *err, size_t errcap);
int          cbrush_save(const CustomBrush *b, const char *path);       /* 0 on failure */
CustomBrush *cbrush_load(const char *path, char *err, size_t errcap);
/* the same reader and writer for any RGBA picture (patterns use them): handle in *hx, *hy if not NULL */
u8          *ilbm_brush_encode(const u8 *px, i32 w, i32 h, i32 hx, i32 hy, const char *name, size_t *len);
u8          *ilbm_brush_decode(const u8 *data, size_t n, i32 *w, i32 *h, i32 *hx, i32 *hy, char *name, size_t namecap, char *err, size_t errcap);

/* ------------------------------------------------------------------ dynamics */
/* what drives a dynamic setting (PhotoCraft's Control) */
enum { CTL_OFF, CTL_FADE, CTL_PRESSURE, CTL_DIRECTION, CTL_INITIAL, CTL_TILT /* 1.2 */, CTL_N };
extern const char *const ctl_names[CTL_N];
/* one dynamic setting: random jitter 0-100 % (scatter up to 1000 %), a control, the Fade steps
   (0: 25) and the floor 0-100 % the control and jitter may take it down to */
typedef struct { int jitter, control, fade_steps, minimum; } Dynamic;
/* painting symmetry */
enum { SYM_OFF, SYM_VERTICAL, SYM_HORIZONTAL, SYM_BOTH, SYM_RADIAL, SYM_MANDALA,
       SYM_DIAGONAL, SYM_TILE,         /* 1.2: both diagonals; tiled (sym_segments across and down, DPaint's tile) */
       SYM_N };
extern const char *const sym_names[SYM_N];
#define SYM_MAXCOPIES 64
typedef struct {
  /* Shape Dynamics */
  int shape_on; Dynamic size, angle, roundness; int flip_x_jitter, flip_y_jitter;
  /* Scattering: scatter.jitter is the amount, 0-1000 % of the radius */
  int scatter_on; Dynamic scatter; int both_axes; int count; Dynamic count_jitter;   /* count 1-16 (0: 1) */
  /* Color Dynamics: jitter amounts 0-100 %, purity -100..100 */
  int color_on; int per_stroke; Dynamic fgbg; int hue_jitter, sat_jitter, bri_jitter, purity;
  /* Transfer */
  int transfer_on; Dynamic opacity, flow;
  /* the options bar's pressure buttons (PhotoCraft's pressure_size, pressure_opacity) */
  int pressure_size, pressure_opacity;
  /* Airbrush: build-up while the pointer holds still, dabs a second (0: 20) */
  int buildup, buildup_rate;
  /* Smoothing 0-100 %; pulled string (lazy mouse): the string is amount pixels long */
  int smoothing, pulled_string;
  /* Symmetry: mode, segments (radial and mandala, 2-32), centre (sym_center_set, else the
     canvas's middle) and the axis turned clockwise by sym_angle degrees */
  int sym_mode, sym_segments, sym_center_set, sym_angle; double sym_cx, sym_cy;
  u32 seed;                           /* the jitters' seed: the same seed, the same stroke */
} BrushDynamics;

/* counter-based random numbers (PhotoCraft's rng.rs, in 32 bits): a pure function of the seed,
   the dab's number and the quantity, so a stroke replays exactly */
u32    brush_hash(u32 seed, u32 index, u32 stream);
double brush_rand01(u32 seed, u32 index, u32 stream);       /* [0, 1) */

/* symmetry: the copies of point x, y (the first is the point itself); rot gets each copy's turn in
   degrees and mir 1 where it is mirrored. Returns how many (1 when off). w, h: the canvas */
int    brush_symmetry(const BrushDynamics *dy, i32 w, i32 h, double x, double y, double *ox, double *oy, double *rot, int *mir);

/* ------------------------------------------------------------------ the History Brush */
typedef struct Snapshot { i32 w, h; u8 *px; char name[64]; } Snapshot;   /* RGBA, the canvas's size */
Snapshot *snapshot_take(Doc *d);                    /* a copy of the active layer (a layer without pixels: merged) */
Snapshot *snapshot_take_merged(Doc *d);             /* everything visible, merged */
void      snapshot_free(Snapshot *s);

/* ------------------------------------------------------------------ the Magic Eraser */
/* the area like the pixel at x, y (tolerance 0-255; contiguous or every such pixel; matched on the
   active layer or the merged picture) erased to transparency at opacity 0-100, softened at its
   edge with antialias. One undo step; the area changed is returned */
Box magic_erase(Doc *d, i32 x, i32 y, int tolerance, int contiguous, int all_layers, int opacity, int antialias);

#endif
