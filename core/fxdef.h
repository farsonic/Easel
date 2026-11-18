/*
 * fxdef.h - a layer's style (Easel 1.1): the settings of its effects (Layer > Layer Style), kept in
 * each Layer (doc.h) so undo and copying carry them like any other setting. fx.h has what works
 * with them; fx.c draws them.
 */
#ifndef EASEL_FXDEF_H
#define EASEL_FXDEF_H
#include "util.h"

/* the effects, in the order the Layer Style menu lists them */
enum { FX_DROPSHADOW, FX_INNERSHADOW, FX_OUTERGLOW, FX_INNERGLOW, FX_BEVEL, FX_SATIN, FX_COLOR,
       FX_GRADIENT, FX_STROKE, FX_N };
/* Bevel & Emboss styles; Stroke positions; Inner Glow sources */
enum { FXB_INNER, FXB_OUTER, FXB_EMBOSS, FXB_PILLOW, FXB_N };
enum { FXS_OUTSIDE, FXS_INSIDE, FXS_CENTER };
enum { FXG_EDGE, FXG_CENTER };
/* flags */
#define FXF_GLOBAL   1                /* the angle (and altitude) are the document's global light */
#define FXF_INVERT   2                /* Satin: Invert; Gradient Overlay: Reverse; Bevel: direction Down */
#define FXF_KNOCKOUT 4                /* Drop Shadow: the layer knocks out the shadow (where it is see-through) */

typedef struct {
  u8 on;
  u8 blend;                           /* BM_ (doc.h) */
  u8 opacity;                         /* 0-255 */
  u8 style;                           /* Bevel: FXB_; Stroke: FXS_; Inner Glow: FXG_; Gradient Overlay: GR_ (paint.h) */
  u32 color;                          /* 0xRRGGBB; Bevel: the highlight's; Gradient Overlay: its start */
  u32 color2;                         /* Bevel: the shadow's; Gradient Overlay: its end */
  i16 angle;                          /* degrees, the direction the light comes from (0 from the right, 90 from above) */
  u16 distance;                       /* pixels (shadows, satin) */
  u16 size;                           /* pixels (blur, glow, stroke width, bevel size) */
  u8 spread;                          /* per cent of size: shadow and glow spread, inner choke; Bevel: soften (pixels) */
  u8 altitude;                        /* Bevel: degrees above the horizon */
  u16 depth;                          /* Bevel: per cent (1-1000); Gradient Overlay: scale per cent (10-150) */
  u8 flags;                           /* FXF_ */
  u8 blend2, opacity2;                /* Bevel: the shadow's mode and opacity */
  u8 pad;
  i16 dx, dy;                         /* Gradient Overlay: its centre moved by this many pixels */
} FxOne;

typedef struct {
  FxOne e[FX_N];
  u8 hidden;                          /* the effects are kept but not drawn (the eye, Hide All Effects) */
  u8 pad[3];
} LayerFx;
#endif
