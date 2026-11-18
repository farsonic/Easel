/*
 * spell.h - Easel round 4: spelling for Edit > Check Spelling. A word list (one word a line, UTF-8:
 * Quill's British English list ships in Easel's Dict drawer), the user's own words and ignored ones,
 * and suggestions for a word not in the list (the words one edit away: a letter left out, added,
 * changed, or two swapped). The code is Quill's spelling (same authors), without Hunspell; Easel
 * checks the text of its type layers.
 */
#ifndef EASEL_SPELL_H
#define EASEL_SPELL_H
#include "doc.h"

typedef struct {
  char *text; u32 *word; u32 n;          /* the list, its words sorted (offsets into text) */
  char **extra; u32 nextra, extracap;    /* the user's words and ignored ones (added at run time) */
} Spell;

int  spell_load(Spell *s, const char *path);       /* 0 if the file can't be read (s stays empty) */
int  spell_load_mem(Spell *s, const char *text, size_t n);  /* from a copy of text; 0 if out of memory */
void spell_free(Spell *s);                         /* frees everything; s is left empty */
void spell_add(Spell *s, const char *word, u32 n); /* known from now on */
/* is this word (UTF-8, n bytes) spelt right? Words with digits, and capitals of a known word, are */
int  spell_ok(const Spell *s, const char *word, u32 n);
/* up to max suggestions for a word, best first, each malloc'd into out; returns how many */
int  spell_suggest(const Spell *s, const char *word, u32 n, char **out, int max);
/* the extent of the word (letters, inner apostrophes) in t at off..: start and end; 0 if none */
int  spell_word_at(const char *t, u32 len, u32 off, u32 *ws, u32 *we);
/* the next misspelt word in d's type layers from layer `layer`, byte off (top layer first, wrapping
   once): 1 with its layer and byte range */
int  spell_next_type(const Spell *s, const Doc *d, int layer, int off, int *li, int *ws, int *we);
/* the word at ws..we of type layer li changed to repl (UTF-8; one undo step); 0 if it couldn't be */
int  spell_change(Doc *d, int li, int ws, int we, const char *repl);
/* every whole word equal to word in every type layer changed (one undo step); how many */
int  spell_change_all(Doc *d, const char *word, const char *repl);
#endif
