/*
 * lang.h - the languages Easel's interface speaks: English, German, French, Spanish and Italian.
 * Its words are written in English in the code; tr() gives them in the language chosen, from a table
 * built in (amiga/lang_table.h, made by tools/mklang.py from lang/strings.txt). The language is the
 * first of Locale prefs' preferred languages that Easel has, or the one picked in Edit > Preferences
 * (it takes effect when Easel starts). The custom parts (toolbox, option bar, panels) translate as
 * they draw; menus, cycle gadgets and windows take theirs when made. Latin-1 in, Latin-1 out.
 */
#ifndef EASEL_LANG_H
#define EASEL_LANG_H

enum { LANG_EN, LANG_DE, LANG_FR, LANG_ES, LANG_IT, UI_NLANG };
extern const char *const ui_languages[UI_NLANG];   /* each language's name in itself */

const char *tr(const char *english);                /* the words in the language chosen (or as given) */
void lang_set(int lang);                            /* the interface in language lang (LANG_) */
int  lang_current(void);                            /* the language chosen */
int  lang_system(void);                             /* the language Locale prefs prefer, if Easel has it */
const char *lang_code(int lang);                    /* "en", "de" ... */
int  lang_by_code(const char *code);                /* -1: not known */
/* a NULL-ended list of English words (cycle gadget entries ...) in the language chosen: a list kept
   by lang.c for as long as Easel runs, made again if the list at that address has changed */
const char *const *tr_list(const char *const *english);
int  lang_by_locale_name(const char *name);         /* "deutsch" -> LANG_DE; -1: not known */
int  lang_rows(void);                               /* the table's rows (for the tests) */

#endif
