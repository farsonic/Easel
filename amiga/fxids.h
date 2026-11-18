/*
 * fxids.h - the menu IDs of Easel 1.1's Layer menu additions (Layer Style, Layer Color, Delete Hidden
 * Layers, Reverse, Background from Layer, Copy CSS, Stack Mode), handled by fxdlg.c. Kept apart from
 * ids.h, in a range of their own.
 */
#ifndef EASEL_FXIDS_H
#define EASEL_FXIDS_H
#define ID_LS_FIRST    3300
#define ID_FX0         3300           /* + FX_N - 1: Drop Shadow... Stroke */
#define ID_FXBLENDOPT  3310
#define ID_FXCOPY      3311
#define ID_FXPASTE     3312
#define ID_FXCLEAR     3313
#define ID_FXLIGHT     3314
#define ID_FXLAYERS    3315
#define ID_FXHIDEALL   3316
#define ID_FXSCALE     3317
#define ID_FXRASTER    3318           /* Rasterize > Layer Style */
#define ID_LABEL0      3320           /* + 0..11: None, Red ... Fuchsia */
#define ID_DELHIDDEN   3334
#define ID_REVERSE     3335
#define ID_BGFROMLAYER 3336
#define ID_COPYCSS     3337
#define ID_STACKMODE0  3340           /* + STK_ */
#define ID_LS_LAST     3359
/* (the Layer Style dialog's own buttons: its effect list and its colour buttons) */
#define ID_FXPAGE0     3360           /* + FX_N */
#define ID_FXCOL0      3370           /* + 2 * FX_N */

#include "../core/doc.h"
/* fxdlg.c: a menu item of these done (0 if id isn't one); the items greyed and ticked for the document */
int  layerstyle_command(Doc *d, ULONG id);
void layerstyle_menus(Doc *d, void (*menu_set)(ULONG id, ULONG attr, ULONG val));
#endif
