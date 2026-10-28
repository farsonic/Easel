/*
 * preset.h - brush presets (0.5): the Brushes panel's list, a named Brush with its own sampled tip,
 * Easel's built-in presets, Define Brush Preset, a simple text file, and Photoshop .abr files.
 *
 * A preset's Brush has its tip pointer set to the preset's own tip (or NULL); its custom brush,
 * pattern and snapshot pointers are always NULL (they belong to the session, not the preset).
 *
 * Parts ported from PhotoCraft (https://github.com/storytold/photocraft), Copyright (c) 2026
 * ArtCraft Team and the PhotoCraft contributors, used under the MIT License (see Licenses.txt):
 * the built-in presets (crates/paint/src/presets.rs) and the .abr layout (crates/psd/src/abr.rs).
 */
#ifndef EASEL_PRESET_H
#define EASEL_PRESET_H

#include "paint.h"

typedef struct {
  char name[64];                      /* UTF-8 */
  char group[32];                     /* "General", "Dry Media", "Special Effects", an imported file's name */
  Brush b;                            /* b.tip is tip */
  Tip *tip;                           /* owned, or NULL for a round tip */
  int builtin;
} BrushPreset;
typedef struct { BrushPreset *p; int n, cap; } PresetList;

void presets_init(PresetList *l);
void presets_free(PresetList *l);
/* a preset added at the end: b copied, tip copied (NULL: round); 0 if out of memory */
int  presets_add(PresetList *l, const char *name, const char *group, const Brush *b, const Tip *tip);
void presets_remove(PresetList *l, int i);
int  presets_builtin(PresetList *l);               /* how many added */
/* Define Brush Preset: the selection (or box b) as a sampled tip, with base's settings and a size
   that fits it; the new preset's number, -1 if nothing paints there */
int  presets_define(PresetList *l, Doc *d, Box b, int merged, const char *name, const Brush *base);
/* the brush a preset gives when picked over cur (PhotoCraft's picked_over: smoothing, symmetry and
   the tool stay the tool's; the tool's own options too) */
Brush preset_pick(const BrushPreset *p, const Brush *cur);

/* the text file:
     EaselBrushes 1
     preset "Name" "Group"
     size 30            (one line per setting that isn't zero, by its name in the Brush)
     tip W H            (then H lines of W two-digit hex coverages)
     end                                                                       */
char *presets_write(const PresetList *l, size_t *len);
int   presets_read(PresetList *l, const char *text, size_t n);   /* how many added; 0 if not such a file */
int   presets_save(const PresetList *l, const char *path);
int   presets_load(PresetList *l, const char *path);
/* Photoshop brushes: v1 and v2 (computed and sampled tips) and v6-v10 (the sampled tips of the
   samp section, 8 or 16 bits, raw or PackBits). Adds them under group; how many, 0 with err set */
int   presets_read_abr(PresetList *l, const u8 *data, size_t n, const char *group, char *err, size_t errcap);
int   presets_load_abr(PresetList *l, const char *path, char *err, size_t errcap);

#endif
