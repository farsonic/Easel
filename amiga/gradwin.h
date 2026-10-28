/*
 * gradwin.h - Easel 0.5's Gradient Editor (MUI, Easel's theme): the presets strip, the gradient
 * bar with its opacity stops above and colour stops below (dragged along, dragged off to delete,
 * clicked by the bar to add), the selected stop's colour (or foreground or background), opacity,
 * location and midpoint, and New, Delete, Load and Save for the presets (Easel's .gradients
 * files and GIMP .ggr).
 *
 * WIRING (main.c): call gradwin_edit() for the Gradient tool's "edit" button or a menu item; it is
 * modal (run_modal). The presets list is the caller's: gradwin_presets() gives a shared one, made
 * with Easel's built-in gradients on first use and freed by gradwin_free() at the end.
 */
#ifndef EASEL_GRADWIN_H
#define EASEL_GRADWIN_H

#include "easel.h"
#include "../core/gradient.h"

#define MUIA_GradEd_Changed (QTAG + 0x1600)   /* (notify) the gradient was edited */

/* g edited (a copy until OK); fg and bg show its foreground and background stops. 1 on OK */
int gradwin_edit(Gradient *g, GradientList *presets, u32 fg, u32 bg);
GradientList *gradwin_presets(void);
void gradwin_free(void);              /* the presets and the class, at the end */

#endif
