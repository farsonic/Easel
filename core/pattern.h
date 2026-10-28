/*
 * pattern.h - Easel's patterns (0.5): RGBA tiles for Edit > Fill > Pattern, the Paint Bucket and
 * the Pattern Stamp; Define Pattern from the selection, a list with built-in patterns drawn in
 * code, and files: one ILBM a pattern, or a set as an IFF CAT of ILBMs with NAME chunks.
 */
#ifndef EASEL_PATTERN_H
#define EASEL_PATTERN_H

#include "doc.h"

typedef struct Pattern { char name[64]; i32 w, h; u8 *px; } Pattern;   /* straight RGBA, w * h * 4 */
Pattern *pattern_new(const char *name, i32 w, i32 h);    /* transparent; NULL if out of memory or too big */
Pattern *pattern_copy(const Pattern *p);
void     pattern_free(Pattern *p);
/* Define Pattern: the selection's box (or box b with nothing selected) of the active layer or the
   merged picture; unselected pixels transparent. NULL if empty */
Pattern *pattern_from_doc(Doc *d, Box b, int merged);
/* the pattern's pixel for canvas pixel x, y with the tiles starting at ox, oy */
static inline const u8 *pattern_at(const Pattern *p, i32 x, i32 y, i32 ox, i32 oy)
{
  i32 u = (x - ox) % p->w, v = (y - oy) % p->h;
  if (u < 0) u += p->w;
  if (v < 0) v += p->h;
  return p->px + ((long)v * p->w + u) * 4;
}
/* built-in patterns drawn in code (Easel's own): n x n */
enum { PAT_CHECKER, PAT_DOTS, PAT_BRICKS, PAT_WEAVE, PAT_NOISE, PAT_STRIPES, PAT_DIAMONDS, PAT_N };
Pattern *pattern_builtin(int kind, int n);

typedef struct { Pattern **p; int n, cap; } PatternList;
void     patlist_init(PatternList *l);
void     patlist_free(PatternList *l);               /* frees the patterns too */
int      patlist_add(PatternList *l, Pattern *p);    /* takes p; 0 if out of memory (p freed) */
void     patlist_remove(PatternList *l, int i);
int      patlist_builtin(PatternList *l);
/* files: a set (IFF CAT of FORM ILBM, each with its NAME), or one ILBM a pattern. load accepts
   either and adds to l, returning how many (0 if it isn't one) */
u8      *patlist_write(const PatternList *l, size_t *len);
int      patlist_read(PatternList *l, const u8 *data, size_t n);
int      patlist_save(const PatternList *l, const char *path);
int      patlist_load(PatternList *l, const char *path);
int      pattern_save_ilbm(const Pattern *p, const char *path);
/* a pattern from any RGBA picture (e.g. a PNG decoded with codec.h's codec_decode) */
Pattern *pattern_from_rgba(const char *name, const u8 *px, i32 w, i32 h);

#endif
