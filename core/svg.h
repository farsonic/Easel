/*
 * svg.h - SVG (Easel 1.1): a document's paths and shape layers written as an SVG file (File >
 * Export > Paths to SVG), and an SVG drawing read as shape layers that stay editable (opened
 * like any picture, or placed into one). Portable C99.
 *
 * What is read: path, rect (with rounded corners), circle, ellipse, line, polyline and polygon,
 * inside any groups; transform (matrix, translate, scale, rotate, skewX, skewY) on each; fill,
 * stroke, stroke-width, fill-rule, opacity and fill-/stroke-opacity, as attributes or in style,
 * inherited from groups; colours as #rgb, #rrggbb, rgb() or the basic names; the viewBox. Left
 * out: text, images, gradients and patterns (filled with their first colour or black), CSS
 * classes, clipping, filters and markers.
 */
#ifndef EASEL_SVG_H
#define EASEL_SVG_H

#include "doc.h"

/* the saved paths and the work path (unfilled, black), then every shape layer (with its fill and
   stroke), on a canvas the document's size; frame adds the document's bounds as a rectangle. NULL
   if out of memory */
u8  *svg_write_paths(Doc *d, int frame, size_t *len);
/* 1 if the data looks like an SVG drawing */
int  svg_detect(const u8 *d, size_t n);
/* the drawing's shapes added to d as shape layers above the active one, scaled by scale (1: its
   own units), one undo step; how many (0 with a message in err when there are none). Stops at
   MAXLAYERS (*truncated set when not NULL) */
int  svg_import(Doc *d, const u8 *s, size_t n, double scale, int *truncated, char *err, size_t errcap);
/* the drawing as a new document its size (width and height, or its viewBox; 800 x 600 if neither,
   at most MAXDIM), transparent, its shapes as layers. NULL with a message in err */
Doc *svg_load_doc(const u8 *s, size_t n, char *err, size_t errcap);

#endif
