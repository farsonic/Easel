/*
 * r3ids.h - the menu IDs of round 3's picture work (r3image.c): Combine Shapes, the new filters
 * (Field Blur, Flame, Picture Frame, Tree, HDR Toning, Liquify...), Select Sky and Subject, and the
 * type additions. Kept apart from ids.h, in a range of their own (7700-7899).
 */
#ifndef EASEL_R3IDS_H
#define EASEL_R3IDS_H
#define ID_R3_FIRST     7700
#define ID_COMBINE0     7700           /* + CS_ (core/vecx.h): Unite ... Merge Shape Components */
#define ID_FIELDBLUR    7710
#define ID_FLAME        7711
#define ID_PICFRAME     7712
#define ID_TREE         7713
#define ID_HDRTONING    7714
#define ID_LIQUIFY      7715
#define ID_R3_LAST      7899

#include "../core/doc.h"
#include "../core/path.h"
/* r3image.c: a menu item of these done (0 if id isn't one) */
int  r3_command(Doc *d, ULONG id);
/* Fill Path with its dialog: contents (the foreground colour, the pattern, the history), opacity,
   mode, anti-aliasing; previewed on the picture */
void r3_fill_path(Doc *d, const Path *p);
/* (canvas.c) while set, the picture's presses (phase 0), drags (1) and releases (2) go here in
   image coordinates instead of to the tools; the box it returns is drawn again */
extern Box (*canvas_brush_hook)(int phase, double x, double y);
#endif
