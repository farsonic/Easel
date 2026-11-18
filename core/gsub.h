/*
 * gsub.h - Easel 1.2: OpenType glyph substitution (gsub.c) for type layers.
 */
#ifndef EASEL_GSUB_H
#define EASEL_GSUB_H
#include "util.h"
/* 1 if the GSUB table t (len bytes) has the feature tag ("liga", "smcp" ...) for Latin text */
int  gsub_has(const u8 *t, u32 len, const char *tag);
/* the features named (ntags tags of 4 characters) applied to n glyphs in place; a ligature takes
   the place of its first glyph and marks the others dead (dead[i] 1: no longer drawn). Glyphs
   already dead are passed over */
void gsub_apply(const u8 *t, u32 len, const char *const *tags, int ntags, u16 *g, u8 *dead, int n);
#endif
