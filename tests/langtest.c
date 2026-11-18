/*
 * langtest.c - native tests of Easel's interface languages (1.1): the table tools/mklang.py makes
 * from lang/strings.txt, tr() and tr_list() in each language, the Locale prefs language names and
 * the codes Easel.prefs keeps. (tools/langcheck.py checks the C source against the table.)
 * cc -std=c99 -DLANG_NATIVE -I. tests/langtest.c
 */
#include <stdio.h>
#include <string.h>
#define LANG_NATIVE 1
#include "../amiga/lang.c"

static int tests, failed;
static void ok(const char *name, int cond) { tests++; if (!cond) { failed++; printf("FAIL %s\n", name); } }
static int is(const char *a, const char *b) { return a && b && !strcmp(a, b); }

int main(void)
{
  int i, k, bad;
  static const char *const cyc[] = { "Normal", "Multiply", "Screen", NULL };
  const char *const *t;
  char stack_list[2][16];
  const char *dyn[3];

  /* before lang_set the English comes back as it is */
  ok("tr before lang_set", is(tr("Layers"), "Layers"));
  lang_set(LANG_EN);
  ok("English is as written", tr("Layers") == tr("Layers") && is(tr("Layers"), "Layers"));
  ok("table has rows", lang_rows() > 2000);

  /* every row: English and all four translations, none empty, all different from NULL */
  bad = 0;
  for (i = 0; i < lang_rows(); i++)
    for (k = 0; k < UI_NLANG; k++) if (!table[i].s[k] || (!table[i].s[k][0] && table[i].s[0][0])) bad++;
  ok("every row translated", bad == 0);
  /* no English twice (tr() would find only one) */
  bad = 0;
  for (i = 1; i < nby; i++) if (!strcmp(byen[i - 1]->s[0], byen[i]->s[0])) bad++;
  ok("no English twice", bad == 0);

  /* PhotoCraft's (Photoshop's) words in each language */
  lang_set(LANG_DE);
  ok("de current", lang_current() == LANG_DE);
  ok("de Layers", is(tr("Layers"), "Ebenen"));
  ok("de Levels...", is(tr("Levels..."), "Tonwertkorrektur..."));
  ok("de Magic Wand", is(tr("Magic Wand Tool"), "Zauberstab-Werkzeug"));
  ok("de Multiply", is(tr("Multiply"), "Multiplizieren"));
  ok("de Cancel", is(tr("Cancel"), "Abbrechen"));
  ok("de unknown as is", is(tr("No such words in the table"), "No such words in the table"));
  ok("de empty and NULL", is(tr(""), "") && tr(NULL) == NULL);
  ok("de Latin-1 umlaut", is(tr("Image Size..."), "Bildgr\366\337e..."));
  lang_set(LANG_FR);
  ok("fr Layers", is(tr("Layers"), "Calques"));
  ok("fr Curves...", is(tr("Curves..."), "Courbes..."));
  ok("fr Screen", is(tr("Screen"), "Superposition"));
  lang_set(LANG_ES);
  ok("es Layers", is(tr("Layers"), "Capas"));
  ok("es Brush Tool", is(tr("Brush Tool"), "Herramienta Pincel"));
  lang_set(LANG_IT);
  ok("it Layers", is(tr("Layers"), "Livelli"));
  ok("it Hue/Saturation...", is(tr("Hue/Saturation..."), "Tonalit\340/saturazione..."));
  lang_set(99);
  ok("out of range is English", lang_current() == LANG_EN && is(tr("Layers"), "Layers"));

  /* tr_list: a cycle gadget's entries, kept while Easel runs and made again when they change */
  lang_set(LANG_DE);
  t = tr_list(cyc);
  ok("tr_list translates", t && is(t[0], "Normal") && is(t[1], "Multiplizieren") && is(t[2], "Negativ multiplizieren") && !t[3]);
  ok("tr_list kept", tr_list(cyc) == t);
  strcpy(stack_list[0], "Red"); strcpy(stack_list[1], "Blue");
  dyn[0] = stack_list[0]; dyn[1] = stack_list[1]; dyn[2] = NULL;
  t = tr_list(dyn);
  ok("tr_list dynamic", is(t[0], "Rot") && is(t[1], "Blau") && !t[2]);
  dyn[1] = "Green";
  t = tr_list(dyn);
  ok("tr_list made again", is(t[0], "Rot") && is(t[1], "Gr\374n") && !t[2]);
  ok("tr_list NULL", tr_list(NULL) == NULL);
  for (i = 0; i < 200; i++) { static const char *more[2]; more[0] = "Red"; more[1] = NULL; (void)tr_list(more); }
  ok("tr_list many lists", is(tr_list(cyc)[1], "Multiplizieren"));

  /* Locale prefs' names (in their own words), and the codes Easel.prefs keeps */
  ok("locale deutsch", lang_by_locale_name("deutsch") == LANG_DE);
  ok("locale Deutsch.language", lang_by_locale_name("Deutsch.language") == LANG_DE);
  ok("locale fran\347ais", lang_by_locale_name("fran\347ais") == LANG_FR);
  ok("locale espa\361ol", lang_by_locale_name("espa\361ol") == LANG_ES);
  ok("locale italiano", lang_by_locale_name("italiano") == LANG_IT);
  ok("locale english", lang_by_locale_name("english") == LANG_EN);
  ok("locale other", lang_by_locale_name("dansk") == -1 && lang_by_locale_name("deutschland") == -1);
  for (k = 0; k < UI_NLANG; k++) ok("code round trip", lang_by_code(lang_code(k)) == k);
  ok("code auto", lang_by_code("auto") == -1);
  ok("language names in themselves", is(ui_languages[LANG_DE], "Deutsch") && is(ui_languages[LANG_FR], "Fran\347ais"));

  printf("%d tests, %d failed\n", tests, failed);
  return failed != 0;
}
