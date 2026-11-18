/*
 * brushx.h - Easel 1.2's brush extras: Brush Settings' Texture, Dual Brush, Noise, Wet Edges and
 * Brush Pose sections, pen tilt, the Mixer Brush's paint, the Art History Brush, healing from a
 * pattern with Diffusion and a mode, tablet pressure from Intuition's tablet events, and brush,
 * gradient and pattern files from Photoshop (.abr written, .grd and .pat read).
 *
 * The stroke engine (stroke.c) calls brushx_cov for each pixel of a dab when Texture, Dual Brush
 * or Noise is on, and brushx_wet for Wet Edges, so a Brush with these off paints as before.
 *
 * The Mixer Brush's model follows PhotoCraft's (crates/paint/src/mixer.rs, MIT, see Licenses.txt):
 * a reservoir of loaded paint plus a pickup well that takes in the canvas's colour; each dab mixes
 * the two by Wet and Mix and lays them at Flow; the reservoir drains unless Load is 100 %.
 */
#ifndef EASEL_BRUSHX_H
#define EASEL_BRUSHX_H
#include "paint.h"

#include "pattern.h"
struct Gradient;

/* ------------------------------------------------------------------ Texture, Dual Brush, Noise, Wet Edges */
enum { TXM_MULTIPLY, TXM_SUBTRACT, TXM_DARKEN, TXM_HEIGHT, TXM_LINEARHEIGHT, TXM_N };
enum { DBM_MULTIPLY, DBM_DARKEN, DBM_SUBTRACT, DBM_N };
extern const char *const txm_names[TXM_N];
extern const char *const dbm_names[DBM_N];
/* the texture's height at canvas pixel x, y (0-255, brightness, contrast and invert applied) */
int  brushx_texture(const Brush *b, i32 x, i32 y);
/* a dab's coverage a (0-255) at pixel x, y once Texture, Dual Brush and Noise have had their say.
   cx, cy, diam: the dab; idx its number in the stroke (the dual tips' scatter) */
int  brushx_cov(const Brush *b, int a, i32 x, i32 y, double cx, double cy, double diam, u32 idx);
int  brushx_active(const Brush *b);      /* any of those on */
/* Wet Edges: the stroke's coverage m (0-255) to what is laid, lighter in the middle than at the edges */
int  brushx_wet(const Brush *b, int m);
/* Brush Pose */
#define POSE_TILT 1
#define POSE_ROTATION 2
#define POSE_PRESSURE 4
double brushx_pose_pressure(const Brush *b, double pressure);
/* the pen's tilt (each -1..1) for Shape Dynamics' Pen Tilt control: with Brush Pose's tilt override
   its tilt, else what the stroke was told (stroke_tilt) */
void stroke_tilt(Stroke *s, double tx, double ty);
/* Protect Texture: a picked preset keeps the brush's texture (pattern and its settings) */
void brushx_protect_texture(Brush *picked, const Brush *cur);

/* ------------------------------------------------------------------ the Mixer Brush */
typedef struct MixerState {
  int loaded;                         /* the reservoir holds paint */
  double res[4];                      /* its colour (straight RGBA 0-255) */
  double amount;                      /* how much is left, 0-1 */
  int has_pickup; double pick[4];     /* the colour picked up from the canvas */
  u32 color; int has_color;           /* the colour it was last loaded with (another loads it afresh) */
  int auto_load, auto_clean;          /* after each stroke: cleaned, then loaded again */
  int sample_all;                     /* picks up from everything visible, not just the layer */
} MixerState;
void mixer_init(MixerState *m);       /* clean, Auto Load and Auto Clean on */
void mixer_load(MixerState *m, u32 color);
void mixer_clean(MixerState *m);
/* a dab: the canvas colour under it (has: anything there), what it lays (out RGB) and the flow factor
   (0-1) it lays it at; wet, load, mix 0-100 */
double mixer_dab(MixerState *m, const double *canvas, int has, int wet, int load, int mix, u8 *out);
void mixer_begin(MixerState *m, u32 color);
void mixer_end(MixerState *m, u32 color);

/* ------------------------------------------------------------------ the Art History Brush */
enum { AH_TIGHT_SHORT, AH_TIGHT_MEDIUM, AH_TIGHT_LONG, AH_LOOSE_MEDIUM, AH_LOOSE_LONG, AH_DAB, AH_TIGHT_CURL,
       AH_TIGHT_CURL_LONG, AH_LOOSE_CURL, AH_LOOSE_CURL_LONG, AH_N };
extern const char *const ah_names[AH_N];
/* one press (or a step of a drag) at x, y: strokes in the snapshot's colours (src: canvas-size RGBA)
   laid with brush b round the area (pixels across, 0: 50) in style AH_; only where the picture
   differs from the source by more than tolerance (0-100). seed: the press's number. One undo step
   ("Art History Brush"); the area changed */
Box art_history(Doc *d, const Brush *b, const u8 *src, double x, double y, int style, int area, int tolerance, u32 seed);

/* ------------------------------------------------------------------ healing */
enum { SPOT_PROXIMITY, SPOT_TEXTURE, SPOT_CONTENT, SPOT_N };
/* the Healing Brush with Diffusion (1-7: how much of the source's fine detail is kept, 1 all of it) */
int  heal_blend_ex(u8 *out, const u8 *base, const u8 *src, const u8 *mask, i32 bw, i32 bh, int diffusion);
/* Create Texture: the mask's area filled with texture made from the pixels round it, then healed into
   its edge. Same arguments as spot_heal_fill (retouch.h) */
int  spot_texture_fill(const u8 *px, i32 w, i32 h, Box b, const u8 *mask, u8 *out, u32 seed);
/* the Content-Aware type, set by cafill_init (cafill.h): the same as spot_heal_fill */
extern int (*spot_content_hook)(const u8 *px, i32 w, i32 h, Box b, const u8 *mask, u8 *out);

/* ------------------------------------------------------------------ tablets */
/* Intuition's tablet events (IESUBCLASS_NEWTABLET, TABLETA_ tags, V39): the tag list given as
   (tag, value) pairs ending with tag 0, read into pressure 0-1 (TABLETA_Pressure is a signed long
   over its whole range, the tablet's driver scales it), tilt -1..1 each (TABLETA_AngleX/Y) and
   whether the stylus is in proximity. Returns 1 if the event carried pressure */
#define TABLET_TAG_BASE 0x8003A000UL  /* TABLETA_Dummy (TAG_USER + 0x3A000) */
typedef struct { double pressure, tilt_x, tilt_y; int in_proximity, has_pressure, has_tilt; } TabletState;
int  tablet_parse(const unsigned long *tags, TabletState *st);
/* the pressure curve (Preferences): soft (gamma) 25-400 %, 100 straight; a floor below which a touch
   is no paint */
double tablet_curve(double p, int curve, int floor_pc);

/* ------------------------------------------------------------------ Photoshop files */
/* .abr version 2 (computed round tips and sampled tips with their names), from brushes bs[0..n) */
u8  *abr_write(const Brush *bs, const char *const *names, int n, size_t *len);
/* .grd (version 5 descriptors, and version 3): gradients added to out (up to max); how many */
int  grd_read(const u8 *data, size_t n, struct Gradient *out, int max, char *err, size_t errcap);
/* round 3: and written (version 5, grdw.c), n gradients; malloc'd, NULL if out of memory */
u8  *grd_write(const struct Gradient *gs, int n, size_t *len);
/* .pat (8BPT version 1: grey, indexed and RGB, raw or PackBits, 8 bits): patterns added; how many */
int  pat_read(PatternList *l, const u8 *data, size_t n, char *err, size_t errcap);
/* and writing one (RGB, raw) so pictures can be taken to other programs */
u8  *pat_write(const struct Pattern *const *ps, int n, size_t *len);
#endif
