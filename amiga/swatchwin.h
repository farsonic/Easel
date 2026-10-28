/*
 * swatchwin.h - Easel 0.5's Swatches panel (MUI, Easel's theme): a grid of colours (click: the
 * foreground colour, Alt-click: the background, Shift-click: select without painting), and along
 * the bottom Add (the foreground colour), Delete (the selected swatch), Load and Save. Files are
 * Easel's .swatches and GIMP .gpl here; .aco, .act, .ase and IFF CMAP go through core/swatch.h's
 * hooks, which core/palette.c fills in when it is merged (see swatch.h's HOOKS note).
 *
 * WIRING (main.c): swatchwin_init() at start-up; Child, swatchwin_panel() where the panel goes;
 * notify MUIA_Swatches_Changed (the foreground or background colour changed) to refresh the
 * toolbox and the Color panel; swatchwin_free() at the end and swatchwin_class_free() after the
 * application has gone.
 */
#ifndef EASEL_SWATCHWIN_H
#define EASEL_SWATCHWIN_H

#include "easel.h"
#include "../core/swatch.h"

#define MUIM_Swatches_Refresh (QTAG + 0x1700)
#define MUIA_Swatches_Changed (QTAG + 0x1701)     /* (notify) a swatch was clicked: the colours changed */

void      swatchwin_init(void);                  /* the default swatches */
void      swatchwin_free(void);
void      swatchwin_class_free(void);
Object   *swatchwin_panel(void);
SwatchSet *swatchwin_set(void);                  /* the swatches shown (change them, then MUIM_Swatches_Refresh) */

#endif
