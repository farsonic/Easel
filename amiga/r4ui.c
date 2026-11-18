/*
 * r4ui.c - Easel round 4 on the Amiga side: Select > Find Layers, Isolate Layers, Float and Drop;
 * Edit > Check Spelling (Quill's word list from PROGDIR:Dict, the user's words in PROGDIR:Easel.words).
 * The core work is in core/r4.c and core/spell.c.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "r4ids.h"
#include "../core/r4.h"
#include "../core/spell.h"
#include "../core/type.h"
#include "../core/util.h"
#include "../core/adjust.h"
#include "brushwin.h"
#include "swatchwin.h"
#include "gradwin.h"

static ULONG xget(Object *o, ULONG a) { ULONG v = 0; get(o, a, &v); return v; }
static Object *label(const char *s) { return QLabel(tr(s), MUIO_Label_Centered); }
static Object *cycle(const char *const *labels, int active) { return CycleObject, MUIA_Cycle_Entries, (ULONG)tr_list(labels), MUIA_Cycle_Active, active, MUIA_CycleChain, 1, End; }
static Object *textstr(const char *s, int max) { return StringObject, StringFrame, MUIA_String_Contents, (ULONG)s, MUIA_String_MaxLen, max, MUIA_CycleChain, 1, End; }
static Object *button(const char *l, ULONG id)
{
  Object *b = QButton(tr(l));
  if (b) DoMethod(b, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, id);
  return b;
}
static void say(const char *s) { ask("OK", "%s", tr(s)); }
/* Latin-1 (a gadget's) as UTF-8 */
static void l1_utf8(const char *s, char *out, size_t cap)
{
  size_t o = 0;
  for (; *s && o + 3 < cap; s++) {
    unsigned char c = (unsigned char)*s;
    if (c < 0x80) out[o++] = (char)c;
    else { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 0x3F)); }
  }
  out[o] = 0;
}

/* ================================================================== Select > Find Layers */
static void find_layers(Doc *d)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  char name[64];
  Object *w, *ns, *kc;
  utf8_to_latin1(layer_filter.doc == d ? layer_filter.name : "", strlen(layer_filter.doc == d ? layer_filter.name : ""), name, sizeof name);
  w = dialog_window("Find Layers", VGroup,
        Child, ColGroup(2),
          Child, label("Name:"), Child, ns = textstr(name, 60),
          Child, label("Kind:"), Child, kc = cycle(layer_filter_kinds, layer_filter.doc == d ? layer_filter.kind : LFK_ALL),
        End,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("The Layers panel shows only the layers that match.\nAn empty name and All show every layer again."), MUIA_Font, MUIV_Font_Tiny, End,
      End, NULL);
  if (!w) return;
  set(w, MUIA_Window_ActiveObject, (ULONG)ns);
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    char u[200];
    l1_utf8((const char *)xget(ns, MUIA_String_Contents), u, sizeof u);
    layer_filter_find(d, u, (int)xget(kc, MUIA_Cycle_Active));
  }
  MUI_DisposeObject(w);
}

/* ================================================================== Edit > Check Spelling */
static Spell spell;
static int spell_tried;
/* the word list (once), and the user's words */
static int spell_ready(void)
{
  if (spell.n || spell_tried) return spell.n != 0;
  spell_tried = 1;
  if (!spell_load(&spell, "PROGDIR:Dict/british-english")) return 0;
  {
    size_t n;
    u8 *w = file_read("PROGDIR:Easel.words", &n);
    if (w) {
      size_t i = 0;
      while (i < n) {
        size_t e = i;
        while (e < n && w[e] != '\n' && w[e] != '\r') e++;
        if (e > i) spell_add(&spell, (const char *)w + i, (u32)(e - i));
        i = e + 1;
      }
      free(w);
    }
  }
  return 1;
}
static void words_append(const char *u)
{
  BPTR f = Open((STRPTR)"PROGDIR:Easel.words", MODE_READWRITE);
  if (!f) return;
  Seek(f, 0, OFFSET_END);
  Write(f, (APTR)u, (LONG)strlen(u));
  Write(f, (APTR)"\n", 1);
  Close(f);
}
#define SP_IGNORE    (ID_DLGBTN0 + 0)
#define SP_IGNOREALL (ID_DLGBTN0 + 1)
#define SP_CHANGE    (ID_DLGBTN0 + 2)
#define SP_CHANGEALL (ID_DLGBTN0 + 3)
#define SP_ADD       (ID_DLGBTN0 + 4)
#define SP_PICK      (ID_DLGBTN0 + 5)
static struct { Doc *d; Object *word, *to, *list; int li, ws, we, done; char cur[128]; } sp;
/* the next misspelt word from where the check is, shown with its suggestions; 0 when there is none */
static int sp_next(int layer, int off)
{
  char *sug[8], l1[200];
  int n, k;
  DoMethod(sp.list, MUIM_List_Clear);
  if (!spell_next_type(&spell, sp.d, layer, off, &sp.li, &sp.ws, &sp.we)) { sp.done = 1; return 0; }
  {
    const TextParams *tp = layer_text(sp.d->lay[sp.li]);
    int len = sp.we - sp.ws < (int)sizeof sp.cur - 1 ? sp.we - sp.ws : (int)sizeof sp.cur - 1;
    memcpy(sp.cur, tp->text + sp.ws, (size_t)len); sp.cur[len] = 0;
  }
  if (sp.d->active != sp.li) { sp.d->active = sp.li; refresh_all(); }
  utf8_to_latin1(sp.cur, strlen(sp.cur), l1, sizeof l1);
  set(sp.word, MUIA_Text_Contents, (ULONG)l1);
  n = spell_suggest(&spell, sp.cur, (u32)strlen(sp.cur), sug, 8);
  for (k = 0; k < n; k++) {
    utf8_to_latin1(sug[k], strlen(sug[k]), l1, sizeof l1);
    DoMethod(sp.list, MUIM_List_InsertSingle, (ULONG)l1, MUIV_List_Insert_Bottom);
    free(sug[k]);
  }
  if (n) { char *first = NULL; DoMethod(sp.list, MUIM_List_GetEntry, 0, (ULONG)&first); set(sp.to, MUIA_String_Contents, (ULONG)(first ? first : "")); set(sp.list, MUIA_List_Active, 0); }
  else { utf8_to_latin1(sp.cur, strlen(sp.cur), l1, sizeof l1); set(sp.to, MUIA_String_Contents, (ULONG)l1); }
  return 1;
}
static void sp_each(ULONG id)
{
  char u[300];
  if (sp.done) return;
  switch (id) {
  case SP_PICK: { char *e = NULL; DoMethod(sp.list, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, (ULONG)&e); if (e) set(sp.to, MUIA_String_Contents, (ULONG)e); return; }
  case SP_IGNORE: break;
  case SP_IGNOREALL: spell_add(&spell, sp.cur, (u32)strlen(sp.cur)); break;
  case SP_ADD: spell_add(&spell, sp.cur, (u32)strlen(sp.cur)); words_append(sp.cur); break;
  case SP_CHANGE:
    l1_utf8((const char *)xget(sp.to, MUIA_String_Contents), u, sizeof u);
    if (spell_change(sp.d, sp.li, sp.ws, sp.we, u)) { sp.we = sp.ws + (int)strlen(u); refresh_all(); }
    break;
  case SP_CHANGEALL:
    l1_utf8((const char *)xget(sp.to, MUIA_String_Contents), u, sizeof u);
    if (spell_change_all(sp.d, sp.cur, u)) { sp.we = sp.ws + (int)strlen(u); refresh_all(); }
    break;
  default: return;
  }
  if (!sp_next(sp.li, sp.we)) {
    say("The spelling check is complete.");
    DoMethod(app, MUIM_Application_ReturnID, ID_DLGOK);
  }
}
static void check_spelling(Doc *d)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w;
  int i, any = 0;
  for (i = 0; i < d->nlay; i++) if (layer_text(d->lay[i])) any = 1;
  if (!any) { say("There is no type layer to check."); return; }
  if (!spell_ready()) { say("The word list wasn't found: Easel looks for Dict/british-english\nin its drawer."); return; }
  memset(&sp, 0, sizeof sp);
  sp.d = d;
  w = dialog_window("Check Spelling", VGroup,
        Child, ColGroup(2),
          Child, label("Not in Dictionary:"), Child, sp.word = TextObject, TextFrame, MUIA_Background, MUII_TextBack, MUIA_Text_Contents, (ULONG)"", End,
          Child, label("Change To:"), Child, sp.to = textstr("", 120),
          Child, label("Suggestions:"), Child, ListviewObject, MUIA_Listview_List, sp.list = ListObject, InputListFrame,
            MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String, End,
            MUIA_FixHeightTxt, (ULONG)"\n\n\n\n\n", MUIA_CycleChain, 1, End,
        End,
        Child, HGroup, Child, button("Ignore", SP_IGNORE), Child, button("Ignore All", SP_IGNOREALL), Child, button("Add", SP_ADD), End,
        Child, HGroup, Child, button("Change", SP_CHANGE), Child, button("Change All", SP_CHANGEALL), Child, HSpace(0), End,
      End, "Done");
  if (!w) return;
  DoMethod(sp.list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, SP_PICK);
  /* from the active layer down (Photoshop checks from where it is) */
  if (!sp_next(d->active, 0)) { MUI_DisposeObject(w); say("The spelling check is complete."); return; }
  run_modal(w, ends, sp_each);
  MUI_DisposeObject(w);
}

/* ================================================================== Edit > Presets > Preset Manager */
extern void refresh_preset_panels(void);   /* (main.c) */
#define PM_KIND   (ID_DLGBTN0 + 0)
#define PM_RENAME (ID_DLGBTN0 + 1)
#define PM_DEL    (ID_DLGBTN0 + 2)
#define PM_UP     (ID_DLGBTN0 + 3)
#define PM_DOWN   (ID_DLGBTN0 + 4)
#define PM_LOAD   (ID_DLGBTN0 + 5)
#define PM_SAVE   (ID_DLGBTN0 + 6)
static struct { Object *kind, *list; int k; char path[512]; } pm;
static void *pm_list(int k)
{
  switch (k) {
  case PM_BRUSHES: return &brushui.presets;
  case PM_SWATCHES: return swatchwin_set();
  case PM_GRADIENTS: return gradwin_presets();
  case PM_PATTERNS: return &patterns;
  }
  return NULL;
}
static void pm_fill(int sel)
{
  void *l = pm_list(pm.k);
  int i, n = pm_count(pm.k, l);
  char b[100];
  set(pm.list, MUIA_List_Quiet, TRUE);
  DoMethod(pm.list, MUIM_List_Clear);
  for (i = 0; i < n; i++) {
    const char *nm = pm_name(pm.k, l, i);
    if (nm[0]) utf8_to_latin1(nm, strlen(nm), b, sizeof b); else sprintf(b, "%s %d", tr(pm_kind_names[pm.k]), i + 1);
    DoMethod(pm.list, MUIM_List_InsertSingle, (ULONG)b, MUIV_List_Insert_Bottom);
  }
  set(pm.list, MUIA_List_Quiet, FALSE);
  if (n) set(pm.list, MUIA_List_Active, sel < 0 ? 0 : sel >= n ? n - 1 : sel);
}
static void pm_each(ULONG id)
{
  void *l = pm_list(pm.k);
  int i = (int)xget(pm.list, MUIA_List_Active), j, changed = 0;
  if (id == PM_KIND) { pm.k = (int)xget(pm.kind, MUIA_Cycle_Active); pm_fill(0); return; }
  if (i < 0 && id != PM_LOAD && id != PM_SAVE) return;
  switch (id) {
  case PM_RENAME: {
    char nm[80], u[200];
    utf8_to_latin1(pm_name(pm.k, l, i), strlen(pm_name(pm.k, l, i)), nm, sizeof nm);
    if (ask_string("Rename Preset", "Name:", nm, sizeof nm) && nm[0]) { l1_utf8(nm, u, sizeof u); changed = pm_rename(pm.k, l, i, u); }
    break; }
  case PM_DEL: changed = pm_delete(pm.k, l, i); break;
  case PM_UP: case PM_DOWN: if ((j = pm_move(pm.k, l, i, id == PM_UP ? -1 : 1)) >= 0) { i = j; changed = 1; } break;
  case PM_LOAD:
    if (ui_file_request(0, "Load Presets", pm.path, sizeof pm.path, pm_pattern(pm.k))) {
      if (pm_load(pm.k, l, pm.path) > 0) { changed = 1; i = pm_count(pm.k, l) - 1; }
      else say("No presets of this kind were found in that file.");
    }
    break;
  case PM_SAVE:
    if (ui_file_request(1, "Save Presets", pm.path, sizeof pm.path, pm_pattern(pm.k)) && !pm_save(pm.k, l, pm.path)) say("The presets couldn't be saved.");
    break;
  default: return;
  }
  if (changed) { pm_fill(i); refresh_preset_panels(); }
}
static void preset_manager(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w;
  w = dialog_window("Preset Manager", VGroup,
        Child, HGroup, Child, label("Preset Type:"), Child, pm.kind = cycle(pm_kind_names, pm.k), End,
        Child, HGroup,
          Child, ListviewObject, MUIA_Listview_Input, TRUE, MUIA_CycleChain, 1, MUIA_Listview_List, pm.list = ListObject, InputListFrame,
            MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String, End,
            MUIA_FixHeightTxt, (ULONG)"\n\n\n\n\n\n\n\n\n\n", MUIA_FixWidthTxt, (ULONG)"MMMMMMMMMMMMMMMMMMMMMM", End,
          Child, VGroup, Child, button("Rename...", PM_RENAME), Child, button("Delete", PM_DEL), Child, button("Move Up", PM_UP), Child, button("Move Down", PM_DOWN),
            Child, HVSpace, Child, button("Load...", PM_LOAD), Child, button("Save Set...", PM_SAVE), End,
        End,
      End, "Done");
  if (!w) return;
  DoMethod(pm.kind, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, PM_KIND);
  pm_fill(0);
  run_modal(w, ends, pm_each);
  MUI_DisposeObject(w);
}

/* ================================================================== Window > Character Styles, Paragraph Styles */
#define TS_FILE "PROGDIR:Easel.typestyles"
#define TS_NEW   (ID_DLGBTN0 + 0)
#define TS_APPLY (ID_DLGBTN0 + 1)
#define TS_REN   (ID_DLGBTN0 + 2)
#define TS_DEL   (ID_DLGBTN0 + 3)
static TypeStyles tstyles;
static int ts_read;
static struct { Doc *d; int para; Object *list; } tsw;
static void ts_load(void)
{
  size_t n;
  u8 *t;
  if (ts_read) return;
  ts_read = 1;
  if ((t = file_read(TS_FILE, &n)) != NULL) {
    char *z = (char *)realloc(t, n + 1);
    if (z) { z[n] = 0; tstyles_load(&tstyles, z); t = (u8 *)z; }
    free(t);
  }
}
static void ts_save(void)
{
  char *t = tstyles_save(&tstyles);
  FILE *f;
  if (!t) return;
  if ((f = fopen(TS_FILE, "w")) != NULL) { fputs(t, f); fclose(f); }
  free(t);
}
static void ts_fill(int sel)
{
  int i, n = tsw.para ? tstyles.np : tstyles.nc;
  char b[100];
  DoMethod(tsw.list, MUIM_List_Clear);
  for (i = 0; i < n; i++) {
    const char *nm = tsw.para ? tstyles.p[i].name : tstyles.c[i].name;
    utf8_to_latin1(nm, strlen(nm), b, sizeof b);
    DoMethod(tsw.list, MUIM_List_InsertSingle, (ULONG)b, MUIV_List_Insert_Bottom);
  }
  if (n) set(tsw.list, MUIA_List_Active, sel < 0 ? 0 : sel >= n ? n - 1 : sel);
}
static void ts_each(ULONG id)
{
  Doc *d = tsw.d;
  int i = (int)xget(tsw.list, MUIA_List_Active), n = tsw.para ? tstyles.np : tstyles.nc, k, done = 0;
  const TextParams *tp = d->active >= 0 && d->active < d->nlay ? layer_text(d->lay[d->active]) : NULL;
  switch (id) {
  case TS_NEW: {
    char nm[48], u[100];
    if (!tp) { say("Choose a type layer first."); return; }
    sprintf(nm, "%s %d", tr(tsw.para ? "Paragraph Style" : "Character Style"), n + 1);
    if (!ask_string(tsw.para ? "New Paragraph Style" : "New Character Style", "Name:", nm, sizeof nm)) return;
    l1_utf8(nm, u, sizeof u);
    k = tsw.para ? tstyle_para_new(&tstyles, tp, u) : tstyle_char_new(&tstyles, tp, u);
    if (k >= 0) { ts_save(); ts_fill(k); }
    return; }
  case TS_APPLY:
    if (i < 0 || i >= n) return;
    /* the active type layer and every picked one */
    for (k = 0; k < d->nlay; k++) if ((k == d->active || d->lay[k]->picked) && layer_text(d->lay[k]))
      done += tsw.para ? doc_para_style(d, k, &tstyles.p[i]) : doc_char_style(d, k, &tstyles.c[i]);
    if (!done) say("Choose a type layer first.");
    refresh_all();
    return;
  case TS_REN: {
    char nm[48], u[100];
    char *cur = tsw.para ? (i >= 0 && i < n ? tstyles.p[i].name : NULL) : (i >= 0 && i < n ? tstyles.c[i].name : NULL);
    if (!cur) return;
    utf8_to_latin1(cur, strlen(cur), nm, sizeof nm);
    if (ask_string("Rename Style", "Name:", nm, sizeof nm) && nm[0]) { l1_utf8(nm, u, sizeof u); snprintf(cur, 48, "%s", u); ts_save(); ts_fill(i); }
    return; }
  case TS_DEL:
    if (i < 0 || i >= n) return;
    tstyle_delete(&tstyles, tsw.para, i); ts_save(); ts_fill(i);
    return;
  }
}
static void type_styles(Doc *d, int para)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *lv;
  ts_load();
  tsw.d = d; tsw.para = para;
  w = dialog_window(para ? "Paragraph Styles" : "Character Styles", VGroup,
        Child, HGroup,
          Child, lv = ListviewObject, MUIA_Listview_Input, TRUE, MUIA_Listview_DoubleClick, TRUE, MUIA_CycleChain, 1, MUIA_Listview_List, tsw.list = ListObject, InputListFrame,
            MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String, End,
            MUIA_FixHeightTxt, (ULONG)"\n\n\n\n\n\n\n\n", MUIA_FixWidthTxt, (ULONG)"MMMMMMMMMMMMMMMMMMMM", End,
          Child, VGroup, Child, button("New...", TS_NEW), Child, button("Apply", TS_APPLY), Child, button("Rename...", TS_REN), Child, button("Delete", TS_DEL), Child, HVSpace, End,
        End,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr(para ? "New takes the active type layer's paragraph settings;\nApply puts the style on the active and picked type layers."
                                                              : "New takes the active type layer's first character style;\nApply puts the style on the active and picked type layers."), MUIA_Font, MUIV_Font_Tiny, End,
      End, "Done");
  if (!w) return;
  DoMethod(lv, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, TS_APPLY);
  ts_fill(0);
  run_modal(w, ends, ts_each);
  MUI_DisposeObject(w);
}

/* ================================================================== Window > Adjustments */
/* PhotoCraft's Adjustments panel as a window: each adjustment layer one double-click (or Create) away;
   the layer's dialog then opens, with its presets */
static void adjustments_window(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *const names[AK_N] = { "Brightness/Contrast...", "Levels...", "Curves...", "Exposure...", "Vibrance...", "Hue/Saturation...",
    "Color Balance...", "Black & White...", "Photo Filter...", "Channel Mixer...", "Invert", "Posterize...", "Threshold...", "Gradient Map...",
    "Selective Color...", "Replace Color..." };
  static int last;
  Object *w, *lv, *list;
  int k;
  w = dialog_window("Adjustments", VGroup,
        Child, lv = ListviewObject, MUIA_Listview_Input, TRUE, MUIA_Listview_DoubleClick, TRUE, MUIA_CycleChain, 1, MUIA_Listview_List, list = ListObject, InputListFrame,
          MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String, End,
          MUIA_FixHeightTxt, (ULONG)"\n\n\n\n\n\n\n\n\n\n\n\n", End,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Double-click an adjustment (or Create) to put it\nabove the active layer as an adjustment layer."), MUIA_Font, MUIV_Font_Tiny, End,
      End, "Create");
  if (!w) return;
  for (k = 0; k < AK_N; k++) DoMethod(list, MUIM_List_InsertSingle, (ULONG)tr(names[k]), MUIV_List_Insert_Bottom);
  set(list, MUIA_List_Active, last);
  DoMethod(lv, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
  k = run_modal(w, ends, NULL) == ID_DLGOK ? (int)xget(list, MUIA_List_Active) : -1;
  MUI_DisposeObject(w);
  if (k >= 0 && k < AK_N) { last = k; DoMethod(app, MUIM_Application_PushMethod, (ULONG)app, 2, MUIM_Application_ReturnID, (ULONG)(ID_NEWADJ0 + k)); }
}

/* ================================================================== the menus */
int r4_command(Doc *d, ULONG id)
{
  if (id == ID_PRESETMGR) { preset_manager(); return 1; }        /* (no picture needed) */
  if (id == ID_ADJPANEL) { if (d) adjustments_window(); return 1; }
  if (id < ID_R4_FIRST || id > ID_R4_LAST || !d) return 0;
  switch (id) {
  case ID_FINDLAYERS: find_layers(d); break;
  case ID_ISOLATE: layer_filter_isolate(d); break;
  case ID_FLOATSEL: if (sel_float(d) < 0) say("Select some pixels on a pixel layer first."); break;
  case ID_DROPSEL: if (!sel_drop(d)) say("Make the floating selection's layer active first."); break;
  case ID_CHECKSPELL: check_spelling(d); break;
  case ID_CHARSTYLES: type_styles(d, 0); break;
  case ID_PARASTYLES: type_styles(d, 1); break;
  default: return 0;
  }
  return 1;
}
void r4_menus(Doc *d, void (*menu_set)(ULONG id, ULONG attr, ULONG val))
{
  Layer *al = d && d->active >= 0 && d->active < d->nlay ? d->lay[d->active] : NULL;
  int i, anytype = 0;
  if (d) for (i = 0; i < d->nlay; i++) if (layer_text(d->lay[i])) anytype = 1;
  menu_set(ID_FINDLAYERS, MUIA_Menuitem_Enabled, d != NULL);
  menu_set(ID_ISOLATE, MUIA_Menuitem_Enabled, d != NULL);
  menu_set(ID_ISOLATE, MUIA_Menuitem_Checked, d && layer_filter.doc == d && layer_filter.isolate);
  menu_set(ID_FLOATSEL, MUIA_Menuitem_Enabled, d && d->sel && al && al->kind == LK_PIXEL && al->px);
  menu_set(ID_DROPSEL, MUIA_Menuitem_Enabled, al && layer_is_floating(al));
  menu_set(ID_CHECKSPELL, MUIA_Menuitem_Enabled, anytype);
  menu_set(ID_PRESETMGR, MUIA_Menuitem_Enabled, TRUE);
}
