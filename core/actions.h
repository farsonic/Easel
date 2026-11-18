/*
 * actions.h - Easel 1.2: the Actions panel's recorder, the Styles panel's saved layer styles, the
 * Layer Comps panel's saved states and the Histogram panel's statistics. Portable C99.
 *
 * An action is a plain list of steps, each a menu command (its ID and the name it was recorded
 * under), played back in order. There is no scripting language and nothing is controlled from
 * outside Easel (that would be ARexx's role, which Easel leaves out): a step whose command has a
 * dialog shows it when the step's dialog toggle is on, else it is done with the settings last used.
 */
#ifndef EASEL_ACTIONS_H
#define EASEL_ACTIONS_H
#include "doc.h"

/* ------------------------------------------------------------------ actions */
#define ACT_STOP 0xFFFFFFFFUL          /* a step's id: Insert Stop (a message, then the rest or not) */
#define ACT_NAMELEN 48
typedef struct { u32 id; u8 on, dialog; char name[ACT_NAMELEN]; } ActStep;
typedef struct {
  char set[ACT_NAMELEN];               /* the set it belongs to */
  char name[ACT_NAMELEN];
  u8 color;                            /* a colour label as the layers have (0 none) */
  ActStep *s; int n, cap;
} Action;
typedef struct { Action *a; int n, cap; } ActionLib;

void actlib_init(ActionLib *l);
void actlib_free(ActionLib *l);
/* a new empty action at the end (its index, -1 out of memory) */
int  act_new(ActionLib *l, const char *set, const char *name);
int  act_delete(ActionLib *l, int i);
int  act_duplicate(ActionLib *l, int i);                    /* "<name> copy" after it */
int  act_move(ActionLib *l, int from, int to);
/* a step added at the end (recording) or inserted at k; 0 if out of memory or full */
#define ACT_MAXSTEPS 500
int  act_add_step(ActionLib *l, int i, u32 id, const char *name, int dialog);
int  act_insert_step(ActionLib *l, int i, int k, u32 id, const char *name, int dialog);
int  act_delete_step(ActionLib *l, int i, int k);
int  act_move_step(ActionLib *l, int i, int from, int to);
/* the sets, in the order their first action comes: names[0..n) point into the library */
int  act_sets(const ActionLib *l, const char **names, int max);
/* played: run(ctx, id, dialog, name) for each step turned on, from step `from`; stops when run
   returns 0. How many steps ran */
int  act_play(const ActionLib *l, int i, int from, int (*run)(void *ctx, u32 id, int dialog, const char *name), void *ctx);
/* saved as text (Easel's own .actions file), and read back (added to what there is: merge, else
   replacing it); 0 on failure. The text form is UTF-8 lines; see actions.c */
char *actlib_save(const ActionLib *l);
int   actlib_load(ActionLib *l, const char *text, int merge);

/* ------------------------------------------------------------------ saved layer styles (Styles panel) */
typedef struct { char name[ACT_NAMELEN]; LayerFx fx; } SavedStyle;
typedef struct { SavedStyle *s; int n, cap; } StyleLib;
void  stylelib_init(StyleLib *l);
void  stylelib_free(StyleLib *l);
int   stylelib_add(StyleLib *l, const char *name, const LayerFx *fx);   /* its index, -1 */
int   stylelib_delete(StyleLib *l, int i);
int   stylelib_rename(StyleLib *l, int i, const char *name);
/* the library's built-in styles added (a few to start with) */
void  stylelib_defaults(StyleLib *l);
char *stylelib_save(const StyleLib *l);                    /* one "name<TAB>fx_save text" line each */
int   stylelib_load(StyleLib *l, const char *text, int merge);

/* ------------------------------------------------------------------ layer comps */
/* what a comp keeps of each layer: visibility, appearance (opacity, fill, mode, style). Position
   isn't kept: Easel's layers are canvas-sized, so a layer's position is its pixels */
#define COMP_VIS 1
#define COMP_LOOK 2
typedef struct { char name[64]; u8 visible, opacity, blend, fill_cut; LayerFx fx; } CompLayer;
typedef struct {
  char name[ACT_NAMELEN];
  char comment[128];
  u8 what;                             /* COMP_ */
  CompLayer *lay; int nlay;
} LayerComp;
/* the document's layers as they are now; 0 if out of memory */
int  comp_capture(LayerComp *c, const Doc *d, const char *name, int what);
/* applied (one undo step): each layer found by name (the first not yet used), else by its place;
   how many layers it changed */
int  comp_apply(Doc *d, const LayerComp *c);
void comp_free(LayerComp *c);

/* ------------------------------------------------------------------ histogram */
enum { HC_LUM, HC_RED, HC_GREEN, HC_BLUE, HC_ALPHA, HC_N };
extern const char *const histo_channel_names[HC_N];
typedef struct { double mean, stddev; int median; long pixels; int min, max; } HistoStats;
/* the composite's histogram in box b (the whole picture if empty), counting only selected pixels
   (each at its selection's strength, rounded) and opaque ones; 0 if out of memory */
int  doc_histogram(Doc *d, Box b, int channel, u32 hist[256]);
void histo_stats(const u32 hist[256], HistoStats *s);
#endif
