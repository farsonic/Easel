/*
 * pathops.h - what paths do to the picture (Easel 0.9): Make Selection, Make Work Path from the
 * selection, Fill Path, Stroke Path (with the brush, through the stroke engine, or a plain width),
 * and the Type Mask (a selection from text). Each is one undo step.
 */
#ifndef EASEL_PATHOPS_H
#define EASEL_PATHOPS_H

#include "doc.h"
#include "path.h"
#include "paint.h"
#include "type.h"

/* the path's inside (its fill rule) made a selection by op (SEL_), feathered (pixels) and
   anti-aliased or hard */
void  path_to_selection(Doc *d, const Path *p, int op, double feather, int antialias);
/* the selection's outline (pixels at least half selected) traced and fitted with curves no further
   than tolerance pixels (0.5-10; 0: 2) from it; a new path (the caller decides where it goes:
   doc_path_set(d, -1, p, "Make Work Path") makes it the work path). NULL if nothing is selected */
Path *path_from_selection(Doc *d, double tolerance);
/* the path's inside filled on the active layer (inside the selection) with color, opacity 0-100
   and blend mode BM_; the area changed (empty if the layer can't be painted) */
Box   path_fill(Doc *d, const Path *p, u32 color, int opacity, int blend, int antialias);
/* r3: Fill Path's other contents: PF_PATTERN (pat's tiles from the canvas's top left) or PF_HISTORY
   (the snapshot snap, canvas sized: the History panel's source); PF_COLOR is path_fill */
enum { PF_COLOR, PF_PATTERN, PF_HISTORY };
struct Pattern; struct Snapshot;
Box   path_fill_source(Doc *d, const Path *p, int source, u32 color, const struct Pattern *pat, const struct Snapshot *snap,
                       int opacity, int blend, int antialias);
/* r3: the outline of mask's pixels at least half on (w x h, looked at inside b) traced and fitted
   no further than tol pixels from it; NULL if nothing is on (or out of memory) */
Path *path_from_mask(const u8 *mask, i32 w, i32 h, Box b, double tol);
/* each subpath stroked with the brush b (the stroke engine: the active tool's dabs, spacing and
   dynamics), pressure tapering in and out when simulate_pressure is set */
Box   path_stroke(Doc *d, const Path *p, const Brush *b, u32 color, int simulate_pressure);
/* each subpath stroked width pixels wide (round joins and ends), anti-aliased */
Box   path_stroke_simple(Doc *d, const Path *p, double width, u32 color, int opacity, int blend);
/* the Type Mask tools: the text's shape (anti-aliased) made a selection by op */
void  sel_from_text(Doc *d, const TextParams *tp, int op);
#endif
