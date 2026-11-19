/*
 * respathtest.c - native tests of core/respath.c: where the shared Fonts, TrueType and Dict drawers
 * are looked for (beside the program, then in Bureau:, then FONTS: for the screen fonts) and the
 * Amiga path joins made with them.
 * cc -std=c99 -I. core/respath.c tests/respathtest.c
 */
#include <stdio.h>
#include <string.h>
#include "../core/respath.h"

static int tests, failed;
static void ok(const char *name, int cond) { tests++; if (!cond) { failed++; printf("FAIL %s\n", name); } }
static int same(const char *a, const char *b) { return a && b ? !strcmp(a, b) : a == b; }

/* the drawers that "exist": a list of names; asked counts the calls */
static int asked;
static int exists(const char *dir, void *ud)
{
  const char *const *have = ud;
  asked++;
  for (; *have; have++) if (!strcmp(*have, dir)) return 1;
  return 0;
}

int main(void)
{
  static const char *const none[] = { NULL };
  static const char *const single[] = { "PROGDIR:Fonts", "PROGDIR:TrueType", "PROGDIR:Dict", "Bureau:Fonts", "Bureau:TrueType", "Bureau:Dict", NULL };
  static const char *const suite[] = { "Bureau:Fonts", "Bureau:TrueType", "Bureau:Dict", NULL };
  static const char *const suite_sys[] = { "Bureau:TrueType", "Bureau:Dict", NULL };
  static const char *const fonts_only[] = { "PROGDIR:Fonts", NULL };
  char out[64];

  /* 1st: a single-app package wins even with Bureau: present */
  ok("fonts beside the program", same(res_pick(RES_FONTS, exists, (void *)single), "PROGDIR:Fonts"));
  ok("truetype beside the program", same(res_pick(RES_TRUETYPE, exists, (void *)single), "PROGDIR:TrueType"));
  ok("dict beside the program", same(res_pick(RES_DICT, exists, (void *)single), "PROGDIR:Dict"));
  /* 2nd: the suite */
  ok("fonts in Bureau:", same(res_pick(RES_FONTS, exists, (void *)suite), "Bureau:Fonts"));
  ok("truetype in Bureau:", same(res_pick(RES_TRUETYPE, exists, (void *)suite), "Bureau:TrueType"));
  ok("dict in Bureau:", same(res_pick(RES_DICT, exists, (void *)suite), "Bureau:Dict"));
  /* 3rd: the suite's screen fonts in FONTS:, which is never asked about */
  asked = 0;
  ok("fonts in FONTS:", same(res_pick(RES_FONTS, exists, (void *)suite_sys), "FONTS:"));
  ok("FONTS: not asked", asked == 2);
  /* no Bureau: and nothing beside the program */
  ok("no fonts: FONTS:", same(res_pick(RES_FONTS, exists, (void *)none), "FONTS:"));
  ok("no truetype", res_pick(RES_TRUETYPE, exists, (void *)none) == NULL);
  ok("no dict", res_pick(RES_DICT, exists, (void *)none) == NULL);
  ok("only fonts: no truetype", res_pick(RES_TRUETYPE, exists, (void *)fonts_only) == NULL);
  ok("unknown kind", res_pick(7, exists, (void *)single) == NULL && res_pick(-1, exists, (void *)single) == NULL);

  /* joins: after ':', after '/', after a plain drawer, an empty drawer */
  ok("join volume", res_join(out, sizeof out, "FONTS:", "QuillSans.font") && !strcmp(out, "FONTS:QuillSans.font"));
  ok("join slash", res_join(out, sizeof out, "Work:Bureau/", "Dict") && !strcmp(out, "Work:Bureau/Dict"));
  ok("join drawer", res_join(out, sizeof out, "Bureau:Dict", "british-english") && !strcmp(out, "Bureau:Dict/british-english"));
  ok("join progdir", res_join(out, sizeof out, "PROGDIR:Fonts", "QuillSans/11") && !strcmp(out, "PROGDIR:Fonts/QuillSans/11"));
  ok("join empty dir", res_join(out, sizeof out, "", "x.font") && !strcmp(out, "x.font"));
  /* truncation: exactly fitting, one byte short (out ""), no room at all */
  ok("join fits exactly", res_join(out, 14, "Bureau:Dict", "a") && !strcmp(out, "Bureau:Dict/a"));
  strcpy(out, "junk");
  ok("join too long", !res_join(out, 14, "Bureau:Dict", "ab") && out[0] == 0);
  strcpy(out, "junk");
  ok("join too long after ':'", !res_join(out, 6, "FONTS:", "") && out[0] == 0);
  ok("join size 0", !res_join(out, 0, "FONTS:", "a"));

  printf("%d tests, %d failed\n", tests, failed);
  return failed != 0;
}
