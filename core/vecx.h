/*
 * vecx.h - round 3's vector extras: Layer > Combine Shapes (Unite Shapes, Subtract Front Shape,
 * Intersect Shape Areas, Exclude Overlapping Shapes, Merge Shape Components) and the built-in
 * library of custom shapes (PhotoCraft's Shapes panel holds its own drawings; these are Easel's).
 *
 * The boolean operations work on the shapes' fills drawn into a mask finer than the canvas (up to
 * four times, within a budget of four million mask pixels), combined there and traced back into a
 * path fitted with curves (pathops.h's path_from_mask), so they follow the outlines to a quarter of
 * a pixel or better whatever the shapes are made of.
 */
#ifndef EASEL_VECX_H
#define EASEL_VECX_H

#include "doc.h"
#include "path.h"

enum { CS_UNITE, CS_SUBTRACT, CS_INTERSECT, CS_EXCLUDE, CS_MERGE, CS_N };

/* the active shape layer and the shape layers picked with it combined by op into the lowest of them
   (a custom shape with its fill and stroke; the others deleted): Subtract takes every upper shape
   from the lowest, Exclude keeps what an odd number of them cover. CS_MERGE: each of the layers'
   own subpaths resolved into one outline. One undo step. The combined layer's index, or -1 (fewer
   than two shape layers for the first four; out of memory; nothing left) */
int  doc_combine_shapes(Doc *d, int op);
/* the boolean operation on two paths (each filled by its own rule), as a new path; NULL if nothing
   is left or out of memory */
Path *path_boolean(const Path *a, const Path *b, int op);

/* ------------------------------------------------------------------ the shape library */
int         shape_library_count(void);
const char *shape_library_name(int k);          /* English, for the interface to translate */
/* shape k fitted to the box x0, y0 - x1, y1 (its own proportions stretched), appended to p; 0 if
   out of memory or no such shape */
int         shape_library_path(int k, Path *p, double x0, double y0, double x1, double y1);
#endif
