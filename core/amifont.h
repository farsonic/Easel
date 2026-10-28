/*
 * amifont.h - Amiga bitmap fonts (Easel 0.9) for type layers: a diskfont .font file (its
 * FontContents list of sizes) and the size files it names (hunk executables holding a
 * DiskFontHeader and its TextFont: the glyph strike, locations, spacing and kerning), read by hand
 * so the portable core needs no diskfont.library. Colour fonts give their first plane.
 */
#ifndef EASEL_AMIFONT_H
#define EASEL_AMIFONT_H

#include "util.h"

typedef struct AmiFont {
  char name[32];                      /* dfh_Name */
  int ysize, xsize, baseline;         /* tf_YSize, tf_XSize, tf_Baseline (row of the baseline from the top) */
  int lo, hi;                         /* tf_LoChar..tf_HiChar; the glyph after hi stands for the rest */
  int style, flags, boldsmear;        /* tf_Style (FSF_), tf_Flags (FPF_), tf_BoldSmear */
  int modulo;                         /* bytes a row of the strike */
  u8 *strike;                         /* ysize * modulo bytes, one bit a pixel, high bit first */
  u16 *loc;                           /* 2 per glyph: bit offset in the strike, width */
  i16 *space, *kern;                  /* per glyph (NULL when the font has none: xsize and 0) */
} AmiFont;

/* a font size file's contents (a hunk executable) as a font; NULL if it isn't one */
AmiFont *amifont_read(const u8 *data, size_t len);
/* a .font file: the size nearest ysize among those it lists, read from the file it names (relative
   to the .font's drawer); NULL if none can be read */
AmiFont *amifont_load(const char *fontfile, int ysize);
void     amifont_free(AmiFont *f);
/* character c (Latin-1; others are the font's spare glyph): its advance (kern + space), where its
   image starts from the pen (kern), and its width; when cov is not NULL, the image as coverage
   (ysize rows of *w bytes, 255 set) into cov, which must hold ysize * width bytes */
int      amifont_char(const AmiFont *f, int c, int *adv, int *kern, int *w, u8 *cov);
#endif
