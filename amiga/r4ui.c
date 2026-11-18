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

/* ================================================================== the menus */
int r4_command(Doc *d, ULONG id)
{
  if (id < ID_R4_FIRST || id > ID_R4_LAST || !d) return 0;
  switch (id) {
  case ID_FINDLAYERS: find_layers(d); break;
  case ID_ISOLATE: layer_filter_isolate(d); break;
  case ID_FLOATSEL: if (sel_float(d) < 0) say("Select some pixels on a pixel layer first."); break;
  case ID_DROPSEL: if (!sel_drop(d)) say("Make the floating selection's layer active first."); break;
  case ID_CHECKSPELL: check_spelling(d); break;
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
}
