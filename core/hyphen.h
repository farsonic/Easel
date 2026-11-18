/*
 * hyphen.h - round 3: automatic hyphenation for paragraph type, with Liang's patterns (TeX's
 * method) read from a Hyphen .dic file (as LibreOffice and Quill ship them: the character set on
 * the first line, then one pattern a line, digits between the letters; the higher digit wins and odd
 * ones allow a break). The method and this reader are Quill's (core/hyph.c, GPL-2.0-only like
 * Easel), kept to one set of patterns. The patterns are files of their own (Quill's Dict drawer:
 * hyph_en_GB.dic and the others, each with its licence); none is built in.
 */
#ifndef EASEL_HYPHEN_H
#define EASEL_HYPHEN_H

#include "util.h"

int  hyphen_load(const char *path);                /* the patterns from a .dic file; 0 if unreadable */
int  hyphen_load_mem(const char *data, size_t n);  /* from a copy of data; 0 only if out of memory */
void hyphen_free(void);
int  hyphen_ready(void);                           /* are patterns loaded? */
/* where a word may break: brk[i] = 1 allows a hyphen before byte i (n + 1 entries). The word is
   UTF-8 letters (and apostrophes); the shortest pieces are the file's LEFTHYPHENMIN and
   RIGHTHYPHENMIN letters (2 and 3 when it doesn't say). How many breaks */
int  hyphen_word(const char *w, u32 n, u8 *brk);
#endif
