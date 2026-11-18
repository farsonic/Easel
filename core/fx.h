/*
 * fx.h - layer styles (Easel 1.1): Drop Shadow, Inner Shadow, Outer Glow, Inner Glow, Bevel &
 * Emboss, Satin, Color Overlay, Gradient Overlay and Stroke, drawn live from the layer's shape (its
 * alpha, with its mask) as the stack is composited. The settings are in each Layer (fxdef.h). A
 * 68040 can't redraw blurs every time the canvas is drawn, so each layer with effects keeps what
 * they look like (one byte a pixel per effect, over the part of the canvas they reach) until its
 * pixels, mask or style change.
 */
#ifndef EASEL_FX_H
#define EASEL_FX_H
#include "doc.h"

#define FX_MAXSIZE 250                /* the largest size, in pixels */
#define FX_MAXDIST 1000               /* the largest distance */
extern const char *const fx_names[FX_N];         /* "Drop Shadow" ... */

void fx_defaults(FxOne *e, int kind);             /* PhotoCraft's (and Photoshop's) starting settings */
void fx_defaults_all(LayerFx *fx);                /* every effect at its defaults, none on */
int  fx_has(const Layer *l);                      /* some effect is on (drawn or hidden) */
int  fx_any(const Layer *l);                      /* some effect is drawn: on, not hidden, a kind that has a shape */
void fx_light(const Doc *d, int *angle, int *alt);   /* the document's global light */
void fx_free(Layer *l);                           /* the drawn effects forgotten (layer_free calls it) */

/* the compositor's calls (doc.c), for row y from x0, n pixels, onto acc (straight RGBA); keep: acc's
   alpha kept (a layer clipped to the one below). fx_under draws what goes below the layer's own
   pixels (Drop Shadow, Outer Glow), fx_over what goes on them (overlays, inner effects, Stroke,
   Bevel). Nothing is drawn when the effects can't be worked out (out of memory) */
void fx_under(Doc *d, Layer *l, i32 y, i32 x0, i32 n, u8 *acc, int keep);
void fx_over(Doc *d, Layer *l, i32 y, i32 x0, i32 n, u8 *acc, int keep);
/* the effects worked out now if they need to be; 0 if out of memory. *reach (if not NULL) gets the
   box they cover */
int  fx_render(Doc *d, Layer *l, Box *reach);
/* how much of effect k covers canvas pixel x, y (0-255): what fx_under/fx_over draw with (Color and
   Gradient Overlay: the layer's shape) */
int  fx_coverage(Doc *d, Layer *l, int k, i32 x, i32 y);

/* ------------------------------------------------------------------ Layer > Layer Style (each an undo step) */
int  doc_set_fx(Doc *d, int i, const LayerFx *fx);      /* layer i's style replaced */
int  doc_clear_fx(Doc *d, int i);                       /* Clear Layer Style */
int  doc_paste_fx(Doc *d, const LayerFx *fx);           /* Paste Layer Style: onto the picked layers (or the active one) */
int  doc_hide_all_fx(Doc *d, int hide);                 /* Hide All Effects (1) / Show All Effects (0) */
int  doc_scale_fx(Doc *d, int i, int percent);          /* Scale Effects: sizes and distances by 1-1000 % */
int  doc_global_light(Doc *d, int angle, int alt);      /* Global Light (effects that use it change) */
int  doc_fx_to_layers(Doc *d, int i);                   /* Create Layers: each effect a pixel layer of its own */

/* the style as text (OpenRaster's easel:effects attribute; "" when there is none), malloc'd; and back
   (1 if it was understood; what can't be is left at the defaults) */
char *fx_save(const LayerFx *fx);
int   fx_load(LayerFx *fx, const char *s);
#endif
