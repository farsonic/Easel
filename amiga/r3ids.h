/*
 * r3ids.h - the menu IDs of round 3's picture work (r3image.c): Combine Shapes, the new filters
 * (Field Blur, Flame, Picture Frame, Tree, HDR Toning, Liquify...), Select Sky and Subject, and the
 * type additions. Kept apart from ids.h, in a range of their own (7700-7899).
 */
#ifndef EASEL_R3IDS_H
#define EASEL_R3IDS_H
#define ID_R3_FIRST     7700
#define ID_COMBINE0     7700           /* + CS_ (core/vecx.h): Unite ... Merge Shape Components */
#define ID_R3_LAST      7899

#include "../core/doc.h"
#include "../core/path.h"
/* r3image.c: a menu item of these done (0 if id isn't one) */
int  r3_command(Doc *d, ULONG id);
/* Fill Path with its dialog: contents (the foreground colour, the pattern, the history), opacity,
   mode, anti-aliasing; previewed on the picture */
void r3_fill_path(Doc *d, const Path *p);
#endif
