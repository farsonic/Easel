/*
 * r4ids.h - the menu IDs of Easel's round 4 additions (r4ui.c), in a range of their own (8000-8099),
 * and what r4ui.c offers main.c.
 */
#ifndef EASEL_R4IDS_H
#define EASEL_R4IDS_H
#define ID_R4_FIRST     8000
#define ID_FINDLAYERS   8000           /* Select > Find Layers... */
#define ID_ISOLATE      8001           /* Select > Isolate Layers (ticked) */
#define ID_FLOATSEL     8002           /* Select > Float */
#define ID_DROPSEL      8003           /* Select > Drop */
#define ID_CHECKSPELL   8004           /* Edit > Check Spelling... */
#define ID_PRESETMGR    8005           /* Edit > Presets > Preset Manager... */
#define ID_R4_LAST      8099

#ifdef EASEL_AMIGA_H
int  r4_command(Doc *d, ULONG id);     /* 0 if id isn't one of these */
void r4_menus(Doc *d, void (*menu_set)(ULONG id, ULONG attr, ULONG val));
#endif
#endif
