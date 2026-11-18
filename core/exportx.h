/*
 * exportx.h - Easel 1.2: File > Export As's options (PhotoCraft's Export As): the picture scaled
 * by a method of its own, its canvas made larger or smaller about the centre, laid over a matte
 * colour (or kept transparent), with its File Info kept whole, only the copyright, or none, and
 * optionally converted to sRGB. Portable C99.
 */
#ifndef EASEL_EXPORTX_H
#define EASEL_EXPORTX_H
#include "doc.h"

enum { EXM_NONE, EXM_WHITE, EXM_BLACK, EXM_FG, EXM_BG, EXM_N };   /* the matte */
enum { EXD_NONE, EXD_COPYRIGHT, EXD_ALL };                         /* the metadata kept */
typedef struct {
  int pct;                            /* scale, per cent (1-1000) */
  int method;                         /* paint.h's RS_ and resample.h's (Nearest ... Preserve Details) */
  i32 canvas_w, canvas_h;             /* the canvas after scaling (0: as scaled), centred */
  int matte;                          /* EXM_ */
  int metadata;                       /* EXD_ */
  int to_srgb;                        /* the colours converted to sRGB (and tagged so) */
} ExportOpts;
void exportx_defaults(ExportOpts *o);
/* a new one-layer document made from d's composite as the options ask (d unchanged); NULL if
   out of memory */
Doc *exportx_prepare(Doc *d, const ExportOpts *o);
#endif
