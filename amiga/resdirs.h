/*
 * resdirs.h - the drawers Easel takes its shared files from, chosen once at start (main.c,
 * core/respath.c): beside Easel (PROGDIR:), else in Bureau: (the suite), else FONTS: for the screen
 * fonts. Never NULL: a TrueType or Dict drawer found nowhere stays PROGDIR:'s (and fails as before).
 * Paths in them are made with res_join. Files Easel writes (prefs, caches, user words) stay in PROGDIR:.
 */
#ifndef RESDIRS_H
#define RESDIRS_H
#include "../core/respath.h"
extern const char *res_fonts_dir, *res_truetype_dir, *res_dict_dir;
#endif
