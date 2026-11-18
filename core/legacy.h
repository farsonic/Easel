/*
 * legacy.h - Easel 1.2's Amiga-era extras (the gap list's section 21): DPaint's brush Shear and
 * Bend, draw modes (Tint, Shade, Blend, Smear, Cycle) for any brush, animated brushes, a coder's
 * export of an indexed picture (raw bitplanes, interleaved or not, the palette as LoadRGB4 or
 * LoadRGB32 data, C or assembler source, hardware sprites, copper-list gradients), the overscan
 * sizes and TV safe areas, and frames (layers as an animation's frames: their times, onion skins).
 */
#ifndef EASEL_LEGACY_H
#define EASEL_LEGACY_H
#include "doc.h"
#include "paint.h"

/* ------------------------------------------------------------------ draw modes for any brush */
/* the Brush and Pencil tools' draw mode (Brush.custom_mode with no custom brush): CB_MATTE is
   ordinary painting; CB_SHADE darkens (or with Lighten lightens) step by step, CB_BLEND mixes in
   half the colour, CB_SMEAR drags the colours along, CB_CYCLE steps through the palette's first
   cycling range (or round the hue circle), CB_TINT gives the pixels the colour's hue and saturation,
   keeping their brightness */

/* ------------------------------------------------------------------ animated brushes */
/* a brush of frames that steps on each dab (DPaint's AnimBrush): the frames are kept by the caller */
typedef struct { const CustomBrush *const *frame; int n; int step; } AnimBrush;   /* step: dabs a frame (0: 1) */
/* the frame a dab takes */
const CustomBrush *animbrush_frame(const AnimBrush *a, long dab);
/* frames picked up from the picture: box b of each visible top-level pixel layer (the frames as
   layers, bottom first), the background colour key made transparent; how many (into out, up to max) */
int  animbrush_pickup(Doc *d, Box b, int key_bg, u32 key, CustomBrush **out, int max);
/* a stroke with an animated brush: one dab at each point (x, y pairs), each the next frame, laid with
   brush b (b.custom is set to each frame in turn). One undo step; the area changed */
Box  animbrush_stroke(Doc *d, const Brush *b, const AnimBrush *a, const double *xy, int n);

/* ------------------------------------------------------------------ the coder's export */
enum { BP_INTERLEAVED = 1, BP_PALETTE4 = 2, BP_PALETTE32 = 4 };
/* an indexed picture (idx w x h, depth 1-8 planes) as raw bitplanes: each row padded to 16 bits; plane
   after plane (or interleaved: each row's planes together); the palette after it if asked, as LoadRGB4
   words (12 bits) or a LoadRGB32 table. Malloc'd, *len its size */
u8  *bitplanes_raw(const u8 *idx, i32 w, i32 h, int depth, const u8 *pal, int npal, int flags, size_t *len);
/* the same as source: C (UWORD arrays, chip memory) or assembler (dc.w lines, SECTION DATA_C), name
   the label */
char *bitplanes_source(const u8 *idx, i32 w, i32 h, int depth, const u8 *pal, int npal, int flags, int asm_, const char *name);
/* hardware sprites: the picture (indices 0-3, colour 0 transparent) cut into 16-pixel wide strips,
   each a sprite with its control words (position vstart at y0, hstart at x0) and two planes; and the
   four colours. C or assembler source */
char *sprites_source(const u8 *idx, i32 w, i32 h, int asm_, const char *name, i32 x0, i32 y0);
/* a copper list gradient: colour register reg (0: the background) set at each line from y0 for
   ncolors lines to the colours given (0xRRGGBB, 12-bit on OCS/ECS: aga off), as a copper list
   (WAIT/MOVE pairs ending with the end marker), as C or assembler source. Lines past 255 get the
   PAL wait for line 255 first */
char *copper_gradient_source(const u32 *colors, int ncolors, int y0, int reg, int aga, int asm_, const char *name);
/* the copper list as data (big-endian words), *len bytes */
u8  *copper_gradient(const u32 *colors, int ncolors, int y0, int reg, int aga, size_t *len);

/* ------------------------------------------------------------------ overscan and safe areas */
typedef struct { const char *name; i32 w, h; u8 ax, ay; } ScreenSize;   /* with its pixel aspect */
extern const ScreenSize overscan_sizes[];
extern const int noverscan_sizes;
/* the action-safe (90 %) and title-safe (80 %) rectangles of a w x h picture */
void safe_areas(i32 w, i32 h, Box *action, Box *title);
/* guides at both safe areas' edges added (View > New Guides from TV Safe Areas); how many */
int  guides_safe_areas(Doc *d);

/* ------------------------------------------------------------------ frames (layers as an animation's frames) */
/* the frames: the visible-or-not top-level layers, bottom first (indices into out, up to max) */
int  frames_list(const Doc *d, int *out, int max);
/* a frame's time from its layer name ("Frame 3 (0.25 s)", or "(250ms)" as other programs write it),
   in hundredths of a second; 0 if none */
int  frame_time(const char *name);
/* the name with the time set (cs hundredths), into out */
void frame_set_time(const char *name, int cs, char *out, size_t cap);
/* frame k of the frames composited in r, with onion skins: the before frames below and after
   frames above at falling opacity (opacity 1-100 for the nearest), into out (RGBA, stride) */
void frames_onion(Doc *d, int k, int before, int after, int opacity, Box r, u8 *out, long stride);
/* frame k shown alone (the others hidden), for playing; the visibility as it was is kept in vis[] */
void frames_show(Doc *d, int k);
#endif
