/*
 * maglasso.h - the Magnetic Lasso (Easel 0.7): a live-wire ("intelligent scissors") path that clings
 * to edges between anchor points.
 *
 * The cost of passing through a pixel falls as the picture's gradient (Sobel, on luminance) rises,
 * and the cheapest 8-connected path from the last anchor to the pointer is found with Dijkstra's
 * method inside a band around the straight line between them, so each pointer move costs only the
 * pixels near that line (fast enough for a 68040 on a 640 x 480 picture). The pointer itself is
 * first snapped to the strongest edge within Width pixels.
 *
 * Use: maglasso_new on the first click, maglasso_anchor there (and on each later click),
 * maglasso_track on every pointer move (draw the committed points from maglasso_points plus the
 * live path it returns), maglasso_unanchor for Backspace or Delete, maglasso_close on a double-click
 * or Return (or a click on the first point), then sel_polygon_ex with its points, and maglasso_free.
 * Escape: just maglasso_free.
 */
#ifndef EASEL_MAGLASSO_H
#define EASEL_MAGLASSO_H

#include "doc.h"

typedef struct MagLasso MagLasso;
/* width: how far (1-256 pixels) from the pointer an edge is looked for; contrast (1-100 per cent):
   how strong an edge must be to count fully; frequency: an anchor is laid automatically every that
   many pixels of path (0: never; Photoshop's 0-100 Frequency maps well to 200 / (f + 1) + 8);
   all_layers: edges in what shows rather than in the active layer. NULL if out of memory */
MagLasso *maglasso_new(Doc *d, int width, int contrast, int frequency, int all_layers);
void      maglasso_free(MagLasso *ml);
/* an anchor at x, y (snapped to an edge within width; the first one starts the path). The path so
   far is followed up to it */
void      maglasso_anchor(MagLasso *ml, double x, double y);
/* the live path from the last anchor to the pointer at x, y, snapped to edges: *n points (x, y
   pairs, pixel centres) in a buffer the lasso keeps until the next call. Lays automatic anchors as
   the path grows; returns how many it laid (the UI redraws the committed points when > 0) */
int       maglasso_track(MagLasso *ml, double x, double y, double **pts, int *n);
/* the committed points (every anchor and the paths between them), kept by the lasso */
void      maglasso_points(MagLasso *ml, double **pts, int *n);
int       maglasso_anchors(const MagLasso *ml);              /* how many anchors are laid */
/* Backspace: the last anchor and the path to it taken away; 0 when none is left */
int       maglasso_unanchor(MagLasso *ml);
/* the path closed back to the first anchor along edges: the whole polygon, *n points, malloc'd for
   the caller to free (NULL if there are fewer than 3) */
double   *maglasso_close(MagLasso *ml, int *n);
#endif
