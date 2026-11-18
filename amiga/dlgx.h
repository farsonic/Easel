/*
 * dlgx.h - the slider dialog of dialogs.c for other files, and filtersx.c's commands: the Filter
 * Gallery and its effects, Color Lookup, Equalize's choice, Focus Area and Select and Mask.
 */
#ifndef EASEL_DLGX_H
#define EASEL_DLGX_H

#include "easel.h"

/* a slider: its label, range and starting value */
typedef struct { const char *label; LONG min, max, def; } DlgParam;
/* what the dialog calls with the sliders' values (v[7]: the second list's choice), the first list's
   choice and the tick, to apply (or preview) the settings as one undo step */
typedef void (*DlgApplyFn)(Doc *d, const LONG *v, int choice, int tick);
/* the dialog: title, n sliders (up to 6 with a second list), up to two lists, a tick, starting
   values (NULL: the sliders' own), the call, an object shown above; remember: Filter > Last Filter
   repeats it. 1 if it was applied (OK) */
int  dlg_sliders(const char *title, const DlgParam *p, int n, const char *const *choices, const char *choicelabel, int choiceinit,
                 const char *const *choices2, const char *choice2label, int choice2init, const char *ticklabel, int tickinit,
                 const LONG *init, DlgApplyFn fn, Object *extra, int remember);
/* Filter > Last Filter will call fn (with nothing in the values) */
void dlg_set_last(DlgApplyFn fn);

/* (filtersx.c) */
void dlgx_command(ULONG id);         /* an ID_FX_ or ID_GX0 + k command */
void dlgx_equalize(void);            /* Equalize when part of the layer is selected */
#endif
