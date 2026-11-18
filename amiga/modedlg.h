/*
 * modedlg.h - Easel 0.4's dialogs for colour modes and Amiga output (MUI, built as dialogs.c builds
 * its own: dialog_window and run_modal from main.c):
 *   Image > Mode > Indexed Color: colours, palette (adaptive by method, OCS 12-bit, a preset, or the
 *     document's own), dither, forced colours, keep locked entries;
 *   Image > Mode > Bitmap: the method;
 *   Amiga Screen Mode: the modes table with ModeID, overscan, aspect, limits and the document's
 *     warnings for the chosen mode and chipset;
 *   ILBM save options: Normal (planes) / EHB / HAM6 / HAM8 / SHAM / 24-bit, mask or transparent
 *     colour, colour cycling, dither, compression.
 * Each dialog fills a struct and returns 1 for OK; the apply functions do the work as undo steps.
 */
#ifndef EASEL_MODEDLG_H
#define EASEL_MODEDLG_H

#include "../core/doc.h"
#include "../core/codec.h"

/* ------------------------------------------------------------------ Indexed Color */
enum { IP_ADAPTIVE, IP_ADAPTIVE_OCS, IP_CURRENT, IP_PRESET };   /* where the palette comes from */
enum { IF_NONE, IF_BW, IF_PRIMARIES, IF_WORKBENCH, IF_FGBG };   /* forced colours */
typedef struct {
  int colors;                         /* 2-256 */
  int source;                         /* IP_ */
  int method;                         /* QM_MEDIAN, QM_OCTREE, QM_WU (adaptive) */
  int preset;                         /* PP_ (IP_PRESET) */
  int dither;                         /* DI_ */
  int forced;                         /* IF_ */
  int keep_locked;                    /* locked entries keep their places */
  int matte, transparency;            /* round 3: core/modex.h's MT_; see-through pixels kept see-through */
} IndexedOpts;
void modedlg_indexed_defaults(IndexedOpts *o);
int  modedlg_indexed(Doc *d, IndexedOpts *o);
int  modedlg_apply_indexed(Doc *d, const IndexedOpts *o);   /* doc_to_indexed with them; 0 out of memory */

/* ------------------------------------------------------------------ Bitmap */
int  modedlg_bitmap(int *method);                           /* BMM_ */

/* ------------------------------------------------------------------ Amiga screen mode */
typedef struct {
  u32 modeid;                         /* the table's ModeID (without HAM/EHB) */
  int mode;                           /* its index in amigamodes.h's table */
  int chipset;                        /* AC_ */
  int overscan;                       /* 0 nominal, 1 standard, 2 the largest */
  int w, h;                           /* the size for that overscan */
  u8 aspect_x, aspect_y;              /* the mode's pixel aspect */
  int set_aspect;                     /* give the document the mode's aspect too */
} ScreenModeChoice;
int  modedlg_screenmode(Doc *d, ScreenModeChoice *c);       /* c's values are the starting ones */
int  modedlg_apply_screenmode(Doc *d, const ScreenModeChoice *c);   /* ModeID (and aspect) set, one undo step */

/* ------------------------------------------------------------------ ILBM save options */
int  modedlg_ilbm(Doc *d, IlbmOpts *o);                     /* o's values are the starting ones */

#endif
