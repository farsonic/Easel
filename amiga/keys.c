/*
 * keys.c - Edit > Keyboard Shortcuts (0.8): every menu command's Amiga-key shortcut and an extra key
 * of its own (a letter, digit or F-key with Shift, Alt or Ctrl, which the canvas answers), and each
 * tool's letter. Changed in a window listing them all, kept in PROGDIR:Easel.keys.
 *
 * The Amiga keys are the menus' own: the NewMenu table's (read before the window is made, so the
 * menus start with them) and, when changed, each menu item's MUIA_Menuitem_Shortcut.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <devices/inputevent.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"

#define KEYFILE "PROGDIR:Easel.keys"
#define MAXKEYS 400
#define XQ_SHIFT 1
#define XQ_ALT 2
#define XQ_CTRL 4
typedef struct {
  ULONG id;                           /* the command (0: a tool's letter, tool in tool) */
  int tool;
  struct NewMenu *nm;                 /* its menu entry */
  char label[72];                     /* "Edit > Preferences..." */
  char amiga[2];                      /* the Amiga key ("" none) */
  UWORD xkey; UBYTE xqual;            /* the extra key: a character (< 256), 256 + a raw key code; 0 none */
  char line[96], keys[48];            /* as the list shows it: the command, its keys */
} KeyEntry;
static KeyEntry ke[MAXKEYS];
static int nke;
static struct NewMenu *menutab;

static ULONG xget(Object *o, ULONG a) { ULONG v = 0; get(o, a, &v); return v; }
/* an extra key as text: "Ctrl Shift F5", "Alt K", "" */
static void xkey_text(UWORD k, UBYTE q, char *out)
{
  out[0] = 0;
  if (!k) return;
  if (q & XQ_CTRL) strcat(out, "Ctrl ");
  if (q & XQ_ALT) strcat(out, "Alt ");
  if (q & XQ_SHIFT) strcat(out, "Shift ");
  if (k >= 256 + 0x50 && k <= 256 + 0x59) sprintf(out + strlen(out), "F%d", k - 256 - 0x50 + 1);
  else if (k == 256 + 0x5F) strcat(out, "Help");
  else if (k < 256) { size_t n = strlen(out); out[n] = (char)k; out[n + 1] = 0; }
}
/* text typed for an extra key read back; 0 if it isn't one (an empty text clears it: 1, *k 0) */
static int xkey_parse(const char *s, UWORD *k, UBYTE *q)
{
  char w[16];
  *k = 0; *q = 0;
  while (*s) {
    int n = 0;
    while (*s == ' ' || *s == '+' || *s == '-') s++;
    if (!*s) break;
    while (*s && *s != ' ' && *s != '+' && n < 15) w[n++] = *s++;
    w[n] = 0;
    if (!Stricmp((STRPTR)w, (STRPTR)"ctrl")) *q |= XQ_CTRL;
    else if (!Stricmp((STRPTR)w, (STRPTR)"alt")) *q |= XQ_ALT;
    else if (!Stricmp((STRPTR)w, (STRPTR)"shift")) *q |= XQ_SHIFT;
    else if (!Stricmp((STRPTR)w, (STRPTR)"help")) *k = 256 + 0x5F;
    else if ((w[0] == 'F' || w[0] == 'f') && w[1] >= '1' && w[1] <= '9' && (!w[2] || (w[1] == '1' && w[2] == '0' && !w[3]))) *k = (UWORD)(256 + 0x50 + atoi(w + 1) - 1);
    else if (n == 1 && w[0] > ' ') *k = (UWORD)ToUpper((UBYTE)w[0]);
    else return 0;
  }
  return 1;
}
/* an entry's line in the list */
static void ke_line(KeyEntry *e)
{
  char x[40];
  if (!e->id) { sprintf(e->line, "%s: %s", tr("Tool"), tr(tool_names[e->tool])); sprintf(e->keys, "%c", tool_keys[e->tool]); return; }
  xkey_text(e->xkey, e->xqual, x);
  snprintf(e->line, sizeof e->line, "%s", e->label);
  snprintf(e->keys, sizeof e->keys, "%s%s%s%s", e->amiga[0] ? "Amiga " : "", e->amiga, e->amiga[0] && x[0] ? "   " : "", x);
}
/* the list's two columns for an entry (MUI calls this hook with the entry in a1, the columns in a2) */
static ULONG ke_display(__reg("a0") struct Hook *h, __reg("a2") char **cols, __reg("a1") KeyEntry *e)
{
  (void)h;
  if (!e) { cols[0] = (char *)tr("Command"); cols[1] = (char *)tr("Keys"); return 0; }
  cols[0] = e->line; cols[1] = e->keys;
  return 0;
}
static struct Hook ke_hook = { { NULL, NULL }, (HOOKFUNC)ke_display, NULL, NULL };
/* (see easel.h) the commands gathered from the menus (before the window is made), and Easel.keys read */
void keys_init(struct NewMenu *menus)
{
  const char *title = "", *item = "";
  int i;
  FILE *f;
  char l[200];
  menutab = menus;
  nke = 0;
  for (i = 0; menus[i].nm_Type != NM_END && nke < MAXKEYS - TL_N; i++) {
    struct NewMenu *m = &menus[i];
    ULONG id = (ULONG)m->nm_UserData;
    if (m->nm_Type == NM_TITLE) { title = (const char *)m->nm_Label; continue; }
    if (m->nm_Label == NM_BARLABEL) continue;
    if (m->nm_Type == NM_ITEM) item = (const char *)m->nm_Label;
    if (!id || (id >= ID_RECENT0 && id < ID_RECENT0 + 10)) continue;
    ke[nke].id = id; ke[nke].nm = m;
    if (m->nm_Type == NM_SUB) snprintf(ke[nke].label, sizeof ke[nke].label, "%s > %s > %s", tr(title), tr(item), tr((const char *)m->nm_Label));
    else snprintf(ke[nke].label, sizeof ke[nke].label, "%s > %s", tr(title), tr((const char *)m->nm_Label));
    ke[nke].amiga[0] = m->nm_CommKey ? m->nm_CommKey[0] : 0; ke[nke].amiga[1] = 0;
    m->nm_CommKey = ke[nke].amiga[0] ? (STRPTR)ke[nke].amiga : NULL;   /* (the menus use the table's copy) */
    nke++;
  }
  for (i = 0; i < TL_N; i++) { ke[nke].id = 0; ke[nke].tool = i; nke++; }
  if (!(f = fopen(KEYFILE, "r"))) return;
  while (fgets(l, sizeof l, f)) {
    unsigned long id;
    char a[8], x[40];
    int t;
    l[strcspn(l, "\r\n")] = 0;
    if (sscanf(l, "menu %lu %7s %39[^\n]", &id, a, x) >= 2) {
      int k;
      for (k = 0; k < nke; k++) if (ke[k].id == id) {
        ke[k].amiga[0] = a[0] == '-' && !a[1] ? 0 : (char)ToUpper((UBYTE)a[0]);
        ke[k].nm->nm_CommKey = ke[k].amiga[0] ? (STRPTR)ke[k].amiga : NULL;
        if (sscanf(l, "menu %lu %7s %39[^\n]", &id, a, x) == 3 && strcmp(x, "-")) xkey_parse(x, &ke[k].xkey, &ke[k].xqual);
      }
    } else if (sscanf(l, "tool %d %7s", &t, a) == 2 && t >= 0 && t < TL_N && a[0] >= 'A' && a[0] <= 'Z') tool_keys[t] = a[0];
  }
  fclose(f);
}
/* the shortcuts written to Easel.keys (only what differs from nothing: every command with a key) */
static void keys_save(void)
{
  FILE *f = fopen(KEYFILE ".new", "w");
  int i;
  char x[40];
  if (!f) return;
  for (i = 0; i < nke; i++) {
    if (!ke[i].id) { fprintf(f, "tool %d %c\n", ke[i].tool, tool_keys[ke[i].tool]); continue; }
    xkey_text(ke[i].xkey, ke[i].xqual, x);
    fprintf(f, "menu %lu %s %s\n", (unsigned long)ke[i].id, ke[i].amiga[0] ? ke[i].amiga : "-", x[0] ? x : "-");
  }
  if (fclose(f)) return;
  DeleteFile((STRPTR)KEYFILE);
  Rename((STRPTR)KEYFILE ".new", (STRPTR)KEYFILE);
}
/* (see easel.h) the command a key pressed on the canvas has as its extra key, 0 none */
ULONG keys_extra(int c, UWORD code, UWORD qual)
{
  UBYTE q = (UBYTE)(((qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? XQ_SHIFT : 0) | ((qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) ? XQ_ALT : 0) |
                    ((qual & IEQUALIFIER_CONTROL) ? XQ_CTRL : 0));
  int i;
  for (i = 0; i < nke; i++) {
    UWORD k = ke[i].xkey;
    if (!k || !ke[i].id || ke[i].xqual != q) continue;
    if (k >= 256 ? code == k - 256 : (c && (UWORD)ToUpper((ULONG)c) == k)) return ke[i].id;
  }
  return 0;
}
/* every menu item's Amiga key as the entries have it */
static void keys_to_menus(void)
{
  Object *strip = win ? (Object *)xget(win, MUIA_Window_Menustrip) : NULL, *it;
  int i;
  if (!strip) return;
  for (i = 0; i < nke; i++)
    if (ke[i].id && (it = (Object *)DoMethod(strip, MUIM_FindUData, ke[i].id))) set(it, MUIA_Menuitem_Shortcut, (ULONG)(ke[i].amiga[0] ? ke[i].amiga : NULL));
}

/* ------------------------------------------------------------------ the window */
static Object *klist, *kamiga, *kextra, *knote;
static int kcur = -1;
/* the entry picked shown in the strings below the list */
static void show_entry(void)
{
  char x[40];
  LONG a = (LONG)xget(klist, MUIA_List_Active);
  KeyEntry *e;
  kcur = a >= 0 && a < nke ? (int)a : -1;
  if (kcur < 0) return;
  e = &ke[kcur];
  if (!e->id) { x[0] = tool_keys[e->tool]; x[1] = 0; nnset(kamiga, MUIA_String_Contents, (ULONG)x); nnset(kextra, MUIA_String_Contents, (ULONG)""); set(kextra, MUIA_Disabled, TRUE); }
  else { xkey_text(e->xkey, e->xqual, x); nnset(kamiga, MUIA_String_Contents, (ULONG)e->amiga); nnset(kextra, MUIA_String_Contents, (ULONG)x); set(kextra, MUIA_Disabled, FALSE); }
  set(knote, MUIA_Text_Contents, (ULONG)(e->id ? tr("Amiga key: one letter, digit or sign. Extra key: for example Ctrl Shift F5, Alt K or F7 (pressed over the picture).")
                                              : tr("A tool's letter (A to Z). Tools sharing a letter take turns.")));
}
/* the strings' contents put into the entry picked */
static void take_entry(void)
{
  KeyEntry *e;
  const char *a = (const char *)xget(kamiga, MUIA_String_Contents), *x = (const char *)xget(kextra, MUIA_String_Contents);
  UWORD k; UBYTE q;
  if (kcur < 0) return;
  e = &ke[kcur];
  if (!e->id) {
    int c = ToUpper((UBYTE)a[0]);
    if (c >= 'A' && c <= 'Z') tool_keys[e->tool] = (char)c;
  } else {
    e->amiga[0] = a[0] ? (char)ToUpper((UBYTE)a[0]) : 0;
    if (xkey_parse(x, &k, &q)) { e->xkey = k; e->xqual = q; }
    else set(knote, MUIA_Text_Contents, (ULONG)tr("That extra key isn't one Easel knows: write it as Ctrl Alt Shift and a letter, digit, F1-F10 or Help."));
  }
  ke_line(e);
  DoMethod(klist, MUIM_List_Redraw, kcur);
}
static void each(ULONG id)
{
  if (id == ID_DLGCHANGE) show_entry();
  else if (id == ID_DLGAUTO) take_entry();
  else if (id == ID_DLGRESET && kcur >= 0) { nnset(kamiga, MUIA_String_Contents, (ULONG)""); nnset(kextra, MUIA_String_Contents, (ULONG)""); take_entry(); }
}
/* (see easel.h) Edit > Keyboard Shortcuts */
void dlg_keys(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static KeyEntry saved[MAXKEYS];
  static char tsaved[TL_N];
  Object *w, *clr;
  int i, ok;
  memcpy(saved, ke, sizeof ke); memcpy(tsaved, tool_keys, sizeof tsaved);
  for (i = 0; i < nke; i++) ke_line(&ke[i]);
  w = dialog_window("Keyboard Shortcuts", VGroup,
        Child, klist = ListviewObject, MUIA_Listview_Input, TRUE, MUIA_CycleChain, 1,
          MUIA_Listview_List, ListObject, InputListFrame, (ULONG)(theme_dialog_font() ? MUIA_Font : TAG_IGNORE), (ULONG)theme_dialog_font(),
            MUIA_List_DisplayHook, (ULONG)&ke_hook, MUIA_List_Format, (ULONG)"BAR,", MUIA_List_Title, TRUE,
            MUIA_FixHeightTxt, (ULONG)"\n\n\n\n\n\n\n\n\n\n\n\n\n\n", MUIA_FixWidthTxt, (ULONG)"MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM", End, End,
        Child, HGroup,
          Child, QLabel(tr("Amiga key:"), MUIO_Label_SingleFrame),
          Child, kamiga = StringObject, StringFrame, MUIA_String_MaxLen, 2, MUIA_FixWidthTxt, (ULONG)"MM", MUIA_CycleChain, 1, End,
          Child, QLabel(tr("Extra key:"), MUIO_Label_SingleFrame),
          Child, kextra = StringObject, StringFrame, MUIA_String_MaxLen, 30, MUIA_CycleChain, 1, End,
          Child, clr = QButton(tr("Clear")),
        End,
        Child, knote = TextObject, MUIA_Text_Contents, (ULONG)"", MUIA_Text_SetMin, FALSE, End,
      End, "Save");
  if (!w) return;
  for (i = 0; i < nke; i++) DoMethod(klist, MUIM_List_InsertSingle, (ULONG)&ke[i], MUIV_List_Insert_Bottom);
  DoMethod(klist, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGCHANGE);
  DoMethod(kamiga, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGAUTO);
  DoMethod(kextra, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGAUTO);
  DoMethod(clr, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGRESET);
  set(klist, MUIA_List_Active, 0);
  show_entry();
  ok = run_modal(w, ends, each) == ID_DLGOK;
  if (ok) {
    take_entry();
    /* the same Amiga key on two commands: the first pair said */
    for (i = 0; i < nke; i++) {
      int j;
      if (!ke[i].id || !ke[i].amiga[0]) continue;
      for (j = i + 1; j < nke; j++)
        if (ke[j].id && ke[j].amiga[0] == ke[i].amiga[0]) {
          static char msg[300];
          snprintf(msg, sizeof msg, tr("Amiga-%c is the shortcut of both\n%s\nand %s.\nThe menu answers the first."), ke[i].amiga[0], ke[i].label, ke[j].label);
          ask("OK", "%s", msg);
          i = nke; break;
        }
    }
    for (i = 0; i < nke; i++) if (ke[i].id) ke[i].nm->nm_CommKey = ke[i].amiga[0] ? (STRPTR)ke[i].amiga : NULL;
    keys_to_menus();
    keys_save();
    if (toolbox) DoMethod(toolbox, MUIM_Tools_Refresh);
  } else { memcpy(ke, saved, sizeof ke); memcpy(tool_keys, tsaved, sizeof tsaved); }
  MUI_DisposeObject(w);
  klist = kamiga = kextra = knote = NULL; kcur = -1;
}
