/*
 * r4.h - Easel round 4: what the Select, Layer and adjustment additions of this round need from the
 * core.
 *
 *   - Select > Find Layers and Isolate Layers: a filter on the Layers panel's rows (by name, by kind,
 *     or only the layers that were picked when Isolate was chosen);
 *   - Select > Float and Drop: the selected pixels lifted onto a floating layer, and put back down;
 *   - Levels' Show Clipping: the picture as black and white points clip it (Photoshop's Alt-drag);
 *   - Alt+2 ... Alt+5 in Levels and Curves: the channel by its key;
 *   - Color Range's preview modes (selection, image, grey, black and white matte, quick mask);
 *   - typographer's quotes for the Type tool (Preferences > Type > Use Smart Quotes).
 */
#ifndef EASEL_R4_H
#define EASEL_R4_H
#include "doc.h"

/* ------------------------------------------------------------------ Find Layers, Isolate Layers */
enum { LFK_ALL, LFK_PIXEL, LFK_ADJUST, LFK_TYPE, LFK_SHAPE, LFK_SMART, LFK_GROUP, LFK_FILL, LFK_N };
extern const char *const layer_filter_kinds[LFK_N + 1];   /* "All", "Pixel", "Adjustment" ... */
typedef struct {
  const Doc *doc;                     /* the picture it is for (another picture shows every layer) */
  char name[64];                      /* a part of the name (any case; UTF-8), "" any */
  int kind;                           /* LFK_ */
  int isolate;                        /* only the layers in iso */
  const Layer *iso[MAXLAYERS]; int niso;
} LayerFilter;
extern LayerFilter layer_filter;      /* the Layers panel's (one for the session) */
/* is the filter on for d (anything set)? */
int  layer_filter_on(const Doc *d);
/* does layer i pass (shown in the panel)? 1 for every layer when the filter is off */
int  layer_filter_pass(const Doc *d, int i);
/* Find Layers: the name and kind to show (both empty/All turns the filter off) */
void layer_filter_find(const Doc *d, const char *name, int kind);
/* Isolate Layers: on, only the active and picked layers are shown (groups with their members); off
   again, everything. Returns 1 if now isolating */
int  layer_filter_isolate(const Doc *d);
void layer_filter_clear(void);
/* how many layers pass (the panel's rows) */
int  layer_filter_count(const Doc *d);

/* ------------------------------------------------------------------ Float, Drop */
#define FLOAT_NAME "Floating Selection"
/* the selection's pixels of the active pixel layer cut onto a new layer above it named
   "Floating Selection" (one undo step, the selection kept); its index, or -1 (nothing selected,
   not a pixel layer, out of memory) */
int  sel_float(Doc *d);
/* the active floating layer merged into the layer below it (one undo step); 0 if the active layer
   isn't a floating selection */
int  sel_drop(Doc *d);
int  layer_is_floating(const Layer *l);

/* ------------------------------------------------------------------ Levels: Show Clipping */
/* n RGBA pixels shown as clipping is: white (0) - each channel black where it is at or under lo[c]
   (so a pixel black in every channel shows black, one clipped in red only shows cyan), white
   elsewhere; white (1) - each channel lit where it is at or over hi[c], black elsewhere */
void clip_view_rgba(u8 *px, long n, int white, const int *lo, const int *hi);
/* the picture as it shows now, as clipping, on a new top layer named "Clipping" (one undo step, for
   a dialog's preview to take back); the layer's index or -1 */
int  doc_clip_view_layer(Doc *d, int white, const int *lo, const int *hi);
/* which end a Levels slider change is about: the five sliders' values before and after (input
   black, gamma, input white, output black, output white); 1 white, 0 black, -1 unchanged */
int  levels_clip_end(const long *before, const long *after);

/* ------------------------------------------------------------------ Alt+digit picks the channel */
/* the channel a digit key picks in a dialog with nchan channels in its list (Alt+2 the composite,
   Alt+3 red, Alt+4 green, Alt+5 blue, as Photoshop's Levels and Curves): its index, or -1 */
int  channel_for_key(int digit, int nchan);
/* the digit of an Amiga raw key code for the main keyboard's top row (0x01 '1' ... 0x0A '0'), -1 */
int  rawkey_digit(int code);

/* ------------------------------------------------------------------ Color Range's preview */
enum { CRV_SELECTION, CRV_IMAGE, CRV_GRAY, CRV_BLACKMATTE, CRV_WHITEMATTE, CRV_QUICKMASK, CRV_N };
extern const char *const cr_view_names[CRV_N + 1];
/* one preview pixel (RGB out) from the mask value m and the picture's colour p (RGBA) */
void cr_view_pixel(int mode, int m, const u8 *p, u8 *out);

/* ------------------------------------------------------------------ smart quotes */
/* the quotes in UTF-8 text made typographer's: " and ' after a space, an opening bracket, a dash or at
   the start become opening ones, others closing ones (an apostrophe is a closing single quote).
   Writes into out (cap bytes, always ended); returns its length */
int  smart_quotes(const char *in, char *out, int cap);
/* the quote to type at a caret: c is '"' or '\'', prev the byte before the caret (0 at the start);
   the UTF-8 of the typographer's quote into out (4 bytes), its length */
int  smart_quote_for(int c, int prev, char *out);

#endif
