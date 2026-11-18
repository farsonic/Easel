/*
 * gallery.h - the Filter Gallery's effects that Easel 1.0 lacked (Artistic, Brush Strokes, Sketch and
 * Texture), and the Filter Gallery itself: a stack of effects applied in order as one undo step.
 *
 * The effects follow PhotoCraft's (crates/algo/src/artistic_fx.rs, MIT): each is a recipe of
 * blurs, edge maps, value noise and lighting on the colour in 0..1. They work tile by tile: a band
 * of the layer's rows at a time is copied (with the rows above and below the effects reach), then
 * worked out in 128-pixel tiles, so a large picture needs only the undo copy and a little more.
 * Everything random is a hash of the canvas position, so the result doesn't depend on the tiles.
 * Like filters10.h's, they work on the active layer, through the selection (feathered edges blend),
 * keep the transparency (and the alpha lock), and call progress() as they go (so Abort works).
 */
#ifndef EASEL_GALLERY_H
#define EASEL_GALLERY_H

#include "doc.h"

enum {
  /* Artistic */
  GX_COLORED_PENCIL, GX_DRY_BRUSH, GX_FRESCO, GX_NEON_GLOW, GX_PAINT_DAUBS, GX_PALETTE_KNIFE, GX_ROUGH_PASTELS,
  GX_SMUDGE_STICK, GX_SPONGE, GX_UNDERPAINTING, GX_WATERCOLOR,
  /* Brush Strokes */
  GX_ACCENTED_EDGES, GX_ANGLED_STROKES, GX_CROSSHATCH, GX_DARK_STROKES, GX_INK_OUTLINES, GX_SPRAYED_STROKES, GX_SUMI_E,
  /* Sketch (foreground colour the ink, background the paper) */
  GX_BAS_RELIEF, GX_CHALK_CHARCOAL, GX_CHARCOAL, GX_CHROME, GX_CONTE_CRAYON, GX_GRAPHIC_PEN, GX_NOTE_PAPER, GX_PLASTER,
  GX_RETICULATION, GX_STAMP, GX_TORN_EDGES, GX_WATER_PAPER,
  /* Texture */
  GX_CRAQUELURE, GX_GRAIN,
  GX_N
};
#define GX_SLIDERS 6
#define GX_STACK 8                     /* effects in one Filter Gallery stack */

/* what each effect's dialog shows: up to six sliders, two lists and a tick */
typedef struct { const char *label; int min, max, def; } GallerySlider;
typedef struct {
  const char *name, *category;        /* "Colored Pencil", "Artistic" */
  int nsl;
  GallerySlider sl[GX_SLIDERS];
  const char *const *list1; const char *list1label; int list1def;
  const char *const *list2; const char *list2label; int list2def;
  const char *tick;                   /* NULL: none */
} GalleryDef;
extern const GalleryDef gallery_defs[GX_N];
extern const char *const gallery_categories[4];

/* one effect with its settings: v the sliders (as gallery_defs), list1, list2, tick; fg and bg the
   ink and paper (Sketch), and Neon Glow's glow colour (0xRRGGBB) */
typedef struct { int fx; int v[GX_SLIDERS]; int list1, list2, tick; u32 fg, bg, glow; } GalleryEffect;

/* an effect with its default settings, black ink on white paper */
void gallery_default(int fx, GalleryEffect *e);
/* how far (in pixels) an effect looks around each pixel */
int  gallery_reach(const GalleryEffect *e);
/* bytes the stack needs over the picture (the undo copy of the area included) */
long gallery_memory(Doc *d, const GalleryEffect *e, int n);
/* the stack (n effects, 1..GX_STACK) applied in order, as one undo step named name (NULL: the
   first effect's name). 1 if anything changed, 0 if not (no layer to change, or aborted), -1 if
   out of memory */
int  flt_gallery(Doc *d, const GalleryEffect *e, int n, const char *name);

/* the effect on a w x h RGBA picture held in memory, whole (the tests' and EaselConvert's way;
   ox, oy where it sits on the canvas). 0 if out of memory */
int  gallery_run_rgba(u8 *px, i32 w, i32 h, i32 ox, i32 oy, const GalleryEffect *e, int n);
#endif
