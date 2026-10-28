/*
 * palwin.h - Easel 0.4's Palette window (MUI), in the style of Deluxe Paint's palette requester:
 * the palette as a grid of swatches, R/G/B and H/S/V sliders for the chosen entry, Spread, Ex(change),
 * Copy and Undo, a lock per entry, the colour-cycling ranges with play and stop, and loading and
 * saving palette files.
 *
 * Use: palwin_create(app) once (the window is added to the application, closed); palwin_set_doc()
 * whenever the current document changes (NULL when none); palwin_refresh() after anything else
 * changed the document's palette (undo, a mode change...); palwin_open(1) from a menu item. Set
 * palwin_redraw to a function that redraws the canvas (and panels): the window calls it after
 * every palette change and every cycling step. palwin_dispose() at the end (before the
 * application is disposed of, or let MUI dispose of the window with the application).
 *
 * Mouse: click a swatch to choose it; Alt-click toggles its lock; Shift-click marks a range from
 * the chosen entry (for a cycle range). Spread, Ex and Copy ask for the second swatch next.
 */
#ifndef EASEL_PALWIN_H
#define EASEL_PALWIN_H

#include <exec/types.h>
#include <intuition/classusr.h>
#include "../core/doc.h"

Object *palwin_create(Object *app);   /* the window (closed), already added to app; NULL on failure */
void palwin_set_doc(Doc *d);           /* the document shown (stops cycling on the old one) */
void palwin_refresh(void);             /* the palette changed elsewhere: shown again */
void palwin_open(int open);            /* the window opened (1) or closed (0) */
int  palwin_is_open(void);
void palwin_stop(void);                /* colour cycling stopped (the palette put back as it was) */
int  palwin_cycling(void);             /* 1 while cycling plays */
void palwin_dispose(void);             /* the window taken out of the application and freed */
/* the hook Easel provides: redraw the canvas after a palette change (may be NULL) */
extern void (*palwin_redraw)(void);
/* the entry chosen in the grid (for a "pen" colour), and a hook told when it changes (may be NULL) */
int  palwin_current(void);
extern void (*palwin_picked)(int entry);

#endif
