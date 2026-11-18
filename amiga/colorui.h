/*
 * colorui.h - Easel 1.2's colour management and pixel aspect in the interface (colorui.c): the
 * menu commands of colorids.h, their ticks, the view settings the canvas draws through, and what a
 * save needs (the working CMYK profile).
 */
#ifndef EASEL_COLORUI_H
#define EASEL_COLORUI_H
#include "../core/doc.h"
#include "../core/cms.h"
#include "colorids.h"

extern ViewSpec view_spec;            /* proofing, gamut warning, intent, display gamma, working CMYK */
extern int view_spec_gen;             /* counted up whenever view_spec changes */
/* a menu item of colorids.h done (0 if id isn't one) */
int  colorui_command(Doc *d, ULONG id);
/* the items ticked and greyed for the document */
void colorui_menus(Doc *d, void (*menu_set)(ULONG id, ULONG attr, ULONG val));
/* the transform the canvas draws doc through (NULL: none), made again when the document's profile
   or the view settings change; colorui_view_free when the canvas goes */
const CmsXform *colorui_view(Doc *doc);
void colorui_view_free(void);
/* the working CMYK profile (NULL: Easel's press) */
const IccProfile *colorui_cmyk(void);
void colorui_free(void);
#endif
