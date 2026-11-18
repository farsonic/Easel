/* lang.c - see lang.h. Builds natively too (tests/langtest.c, with LANG_NATIVE),
   without lang_system's Locale part. */
#include <string.h>
#include <stdlib.h>
#ifndef LANG_NATIVE
#include <proto/exec.h>
#include <proto/locale.h>
#endif
#include "lang.h"

const char *const ui_languages[UI_NLANG] = { "English", "Deutsch", "Fran\347ais", "Espa\361ol", "Italiano" };
static const char *const codes[UI_NLANG] = { "en", "de", "fr", "es", "it" };
static const char *const names[UI_NLANG] = { "english", "deutsch", "fran\347ais", "espa\361ol", "italiano" };   /* Locale's */

/* the table: English, then each other language in order (NULL: as English); made from
   lang/strings.txt by tools/mklang.py */
typedef struct { const char *s[UI_NLANG]; } Words;
static const Words table[] = {
#include "lang_table.h"
  { { NULL } }
};
#define NWORDS ((int)(sizeof table / sizeof table[0]))
static const Words *byen[NWORDS + 1];   /* sorted by the English, for bsearch */
static int nby, cur;

/* qsort's and bsearch's comparisons, both by the English (bsearch's key is the plain string) */
static int cmp_words(const void *a, const void *b) { return strcmp((*(const Words *const *)a)->s[0], (*(const Words *const *)b)->s[0]); }
static int cmp_key(const void *k, const void *b) { return strcmp((const char *)k, (*(const Words *const *)b)->s[0]); }
/* the interface in language lang (a LANG_; anything else means English). The first call also
   builds byen, the table sorted by its English, so tr() gives the English back until then */
void lang_set(int lang)
{
  int i;
  cur = lang >= 0 && lang < UI_NLANG ? lang : LANG_EN;
  if (!nby) {
    for (i = 0; i < NWORDS; i++) if (table[i].s[0]) byen[nby++] = &table[i];
    qsort(byen, (size_t)nby, sizeof byen[0], cmp_words);
  }
}
/* the LANG_ lang_set chose */
int lang_current(void) { return cur; }
/* the words in the language chosen: a pointer into the built-in table (not to be freed or
   changed), or english itself when the table hasn't got it or has no translation for it */
const char *tr(const char *english)
{
  const Words *const *w;
  if (cur == LANG_EN || !english || !*english || !nby) return english;
  w = (const Words *const *)bsearch(english, byen, (size_t)nby, sizeof byen[0], cmp_key);
  return w && (*w)->s[cur] ? (*w)->s[cur] : english;
}
/* (see lang.h) the table's rows */
int lang_rows(void) { return NWORDS - 1; }
/* (see lang.h) the lists tr_list has made: each the address it was asked for, the English it held
   then (to see when a list made on the stack, or filled again, has changed) and its translation */
#define NLISTS 96
#define LISTMAX 64
static struct { const char *const *src; const char *en[LISTMAX + 1]; const char *out[LISTMAX + 1]; } lists[NLISTS];
static int nlists, nextlist;
const char *const *tr_list(const char *const *english)
{
  int i, k, n;
  if (!english) return english;
  for (n = 0; english[n] && n < LISTMAX; n++) ;
  if (english[n]) return english;                    /* (longer than a list kept here: as it is) */
  for (i = 0; i < nlists; i++) {
    if (lists[i].src != english) continue;
    for (k = 0; k <= n && lists[i].en[k] == english[k]; k++) ;
    if (k > n && !english[n]) { for (k = 0; k < n; k++) lists[i].out[k] = tr(english[k]); return lists[i].out; }
    break;
  }
  if (i == nlists) { if (nlists < NLISTS) i = nlists++; else { i = nextlist; nextlist = (nextlist + 1) % NLISTS; } }
  lists[i].src = english;
  for (k = 0; k < n; k++) { lists[i].en[k] = english[k]; lists[i].out[k] = tr(english[k]); }
  lists[i].en[n] = NULL; lists[i].out[n] = NULL;
  return lists[i].out;
}
/* the two-letter code Easel.prefs keeps (language=de); English's for a number out of range */
const char *lang_code(int lang) { return codes[lang >= 0 && lang < UI_NLANG ? lang : 0]; }
/* the LANG_ for a code from lang_code (as read back from Easel.prefs), -1 if it isn't one */
int lang_by_code(const char *code)
{
  int i;
  for (i = 0; i < UI_NLANG; i++) if (!strcmp(code, codes[i])) return i;
  return -1;
}
/* (see lang.h) the LANG_ for one of Locale prefs' language names ("deutsch", "deutsch.language",
   any case), -1 if Easel hasn't got it: English counts, so English before German in the
   preferred languages keeps Easel in English */
int lang_by_locale_name(const char *p)
{
  int i;
  size_t n;
  for (i = 0; i < UI_NLANG; i++) {
    const char *q = names[i];
    for (n = 0; q[n] && p[n] && (p[n] | 0x20) == (q[n] | 0x20); n++) ;
    if (!q[n] && (!p[n] || p[n] == '.')) return i;
  }
  return -1;
}
#ifndef LANG_NATIVE
/* locale.library, open only while lang_system looks (the proto/ calls go through this global) */
struct Library *LocaleBase;
/* the first of the user's preferred languages in Locale prefs that Easel speaks, else English.
   Locale names them in their own words, lower case ("deutsch"), as names[] has them. The library
   is opened just for this and closed again */
int lang_system(void)
{
  int found = -1, k;
  struct Locale *loc;
  if (!(LocaleBase = OpenLibrary((STRPTR)"locale.library", 38))) return LANG_EN;
  if ((loc = OpenLocale(NULL))) {
    for (k = 0; k < 10 && loc->loc_PrefLanguages[k] && found < 0; k++)
      found = lang_by_locale_name((const char *)loc->loc_PrefLanguages[k]);
    CloseLocale(loc);
  }
  CloseLibrary(LocaleBase); LocaleBase = NULL;
  return found < 0 ? LANG_EN : found;
}
#else
int lang_system(void) { return LANG_EN; }
#endif
