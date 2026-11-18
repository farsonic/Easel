/*
 * guides.h - Easel 0.4: rulers, guides and the grid, the core side: the guides kept on the
 * document (doc.h's GuideSet, d->guides), the grid's settings, snapping, and the rulers' tick
 * spacing. Portable C99. Guides are not part of undo.
 */
#ifndef EASEL_GUIDES_H
#define EASEL_GUIDES_H

#include "doc.h"

/* what snaps (GuideSet.snap, or the flags given to snap_point) */
#define SNAP_GUIDES 1                 /* to the guides */
#define SNAP_GRID   2                 /* to the grid lines */
#define SNAP_EDGES  4                 /* to the canvas's edges */
#define SNAP_CENTER 8                 /* to the canvas's centre lines */
#define SNAP_PIXEL  16                /* to whole pixels when nothing else is near */
#define SNAP_SUB    32                /* to the grid's subdivisions too */
#define SNAP_DOC    0x8000            /* (snap_point) use d->guides.snap instead of the flags */

/* guides: added (1, or 0 when there are MAXGUIDES or one is there already), moved, removed, found */
int  guide_add(Doc *d, int vertical, i32 pos);
int  guide_move(Doc *d, int vertical, int i, i32 pos);       /* off the canvas removes it */
int  guide_remove(Doc *d, int vertical, int i);
void guide_clear(Doc *d);
/* the guide within tol pixels of pos (-1 none) */
int  guide_find(const Doc *d, int vertical, double pos, double tol);
/* the grid: spacing (pixels, 1 or more), offset and subdivisions */
void grid_set(Doc *d, int sx, int sy, int ox, int oy, int sub);
int  grid_spacing_x(const Doc *d);
int  grid_spacing_y(const Doc *d);
/* the first grid line at or after p, and the line spacing (with subdivisions if sub), for drawing */
double grid_first(const Doc *d, int vertical, double p, int sub, double *step);
/* the snap distance in image pixels (from GuideSet.snap_tol, default 8) */
double snap_tolerance(const Doc *d);
/* a point snapped: each axis moves to the nearest thing it may snap to within the tolerance.
   Returns which axes snapped (1 x, 2 y) */
int  snap_point(Doc *d, double *x, double *y, int flags);
/* a single coordinate snapped along one axis (vertical 1 for x, against vertical guides) */
int  snap_coord(Doc *d, double *v, int vertical, int flags);
/* r3: more places to snap to with SNAP_LAYERS (64) and SNAP_SLICES (128): menu3.c's, set by menu3_init */
extern int (*snap_extra)(Doc *d, int vertical, int flags, double *out, int max);
/* the rulers: at zoom (screen pixels per image pixel), the image-pixel spacing of labelled ticks
   so labels are at least minpx screen pixels apart (1, 2, 5, 10, 20, 50 ...), and how many minor
   ticks divide it */
int  ruler_step(double zoom, int minpx, int *minor);
/* pixel aspect correction for a ruler or a view: the height an image pixel is shown at relative to
   its width (ay/ax; 1 for square) */
double aspect_ratio(const Doc *d);

#endif
