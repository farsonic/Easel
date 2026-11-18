/*
 * panelsx.c - Easel 1.2's windows: Window > Actions (record menu commands as steps, play them back,
 * sets, stops, steps turned off, saved in PROGDIR:Easel.actions), File > Automate > Batch (an action
 * run on every picture in a drawer, each saved to another), Window > Styles (saved layer styles,
 * PROGDIR:Easel.styles), Window > Layer Comps (and File > Export > Layer Comps to Files) and Edit >
 * Search (every menu command, found by typing part of its name). The work is in core/actions.c.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "mxids.h"
#include "../core/actions.h"
#include "../core/fx.h"
#include "../core/layerops.h"
#include "../core/util.h"

#define ACTIONS_FILE "PROGDIR:Easel.actions"
#define STYLES_FILE "PROGDIR:Easel.styles"
ActionLib mx_actions;
int mx_rec_action = -1;
static int act_cur = -1, act_step = -1;
static StyleLib styles;
static int styles_read;

static ULONG xget(Object *o, ULONG a) { ULONG v = 0; get(o, a, &v); return v; }
static Object *button(const char *l, ULONG id)
{
  Object *b = QButton(tr(l));
  if (b) DoMethod(b, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, id);
  return b;
}
static Object *listview(Object **list, const char *weightw)
{
  (void)weightw;
  return ListviewObject, MUIA_Listview_Input, TRUE, MUIA_CycleChain, 1,
    MUIA_Listview_List, *list = ListObject, InputListFrame, (ULONG)(theme_dialog_font() ? MUIA_Font : TAG_IGNORE), (ULONG)theme_dialog_font(), End, End;
}
static int file_save_text(const char *path, char *t)
{
  int ok = t && file_write(path, t, strlen(t));
  free(t);
  return ok;
}

/* ------------------------------------------------------------------ the libraries kept on disk */
void mx_init(void)
{
  size_t n;
  char *t;
  actlib_init(&mx_actions);
  if ((t = (char *)file_read(ACTIONS_FILE, &n))) {
    char *z = (char *)realloc(t, n + 1);
    if (z) { z[n] = 0; actlib_load(&mx_actions, z, 0); t = z; }
    free(t);
  }
  if (!mx_actions.n) {
    /* a set to start with, made of commands that need no picture-specific choice */
    int a = act_new(&mx_actions, "Default Actions", "Flatten and Auto Tone");
    act_add_step(&mx_actions, a, ID_FLATTEN, "Flatten Image", 0);
    act_add_step(&mx_actions, a, ID_AUTOTONE, "Auto Tone", 0);
    a = act_new(&mx_actions, "Default Actions", "Grayscale and Sharpen");
    act_add_step(&mx_actions, a, ID_MODE_GRAY, "Grayscale", 0);
    act_add_step(&mx_actions, a, ID_F_SHARPEN, "Sharpen", 0);
  }
  stylelib_init(&styles);
}
void mx_free(void)
{
  file_save_text(ACTIONS_FILE, actlib_save(&mx_actions));
  actlib_free(&mx_actions);
  if (styles_read) file_save_text(STYLES_FILE, stylelib_save(&styles));
  stylelib_free(&styles);
  mx_context_free();
}
static void styles_ready(void)
{
  size_t n;
  char *t;
  if (styles_read) return;
  styles_read = 1;
  if ((t = (char *)file_read(STYLES_FILE, &n))) {
    char *z = (char *)realloc(t, n + 1);
    if (z) { z[n] = 0; stylelib_load(&styles, z, 0); t = z; }
    free(t);
  }
  if (!styles.n) stylelib_defaults(&styles);
}

/* ------------------------------------------------------------------ playing */
static int play_step(void *ctx, u32 id, int dialog, const char *name)
{
  static char msg[160];
  (void)ctx; (void)dialog;
  if (id == ACT_STOP) {
    snprintf(msg, sizeof msg, "%s", name[0] ? name : tr("The action stops here."));
    return ask("Continue|Stop", "%s", msg) == 1;
  }
  do_command((ULONG)id);
  return 1;
}
static int play(int a, int from)
{
  int rec = mx_recording, n;
  if (a < 0 || a >= mx_actions.n) return 0;
  mx_recording = 0;                     /* (what the action does isn't recorded again) */
  n = act_play(&mx_actions, a, from, play_step, NULL);
  mx_recording = rec;
  refresh_all();
  return n;
}
int actions_play_current(void)
{
  if (!cur_doc()) return 0;
  if (act_cur < 0 || act_cur >= mx_actions.n) { ask("OK", "%s", tr("Choose an action in Window > Actions first.")); return 0; }
  return play(act_cur, 0);
}

/* ------------------------------------------------------------------ Window > Actions */
#define MAXROWS 200
static char arow[MAXROWS][110], srow[ACT_MAXSTEPS][64];
static Object *alist, *slist;
static void actions_fill(void)
{
  int i;
  set(alist, MUIA_List_Quiet, TRUE);
  DoMethod(alist, MUIM_List_Clear);
  for (i = 0; i < mx_actions.n && i < MAXROWS; i++) {
    const Action *a = &mx_actions.a[i];
    snprintf(arow[i], sizeof arow[i], "%s%s: %s (%d)", mx_recording && i == mx_rec_action ? "\267 " : "", a->set, a->name, a->n);
    DoMethod(alist, MUIM_List_InsertSingle, (ULONG)arow[i], MUIV_List_Insert_Bottom);
  }
  set(alist, MUIA_List_Quiet, FALSE);
  if (act_cur >= mx_actions.n) act_cur = mx_actions.n - 1;
  SetAttrs(alist, MUIA_NoNotify, TRUE, MUIA_List_Active, act_cur >= 0 ? act_cur : MUIV_List_Active_Off, TAG_DONE);
}
static void steps_fill(void)
{
  int k;
  set(slist, MUIA_List_Quiet, TRUE);
  DoMethod(slist, MUIM_List_Clear);
  if (act_cur >= 0 && act_cur < mx_actions.n) {
    const Action *a = &mx_actions.a[act_cur];
    for (k = 0; k < a->n; k++) {
      snprintf(srow[k], sizeof srow[k], "%s %s%s", a->s[k].on ? "[x]" : "[ ]", a->s[k].id == ACT_STOP ? tr("Stop: ") : "", a->s[k].name);
      DoMethod(slist, MUIM_List_InsertSingle, (ULONG)srow[k], MUIV_List_Insert_Bottom);
    }
  }
  set(slist, MUIA_List_Quiet, FALSE);
  if (act_cur >= 0 && act_step >= 0 && act_step < mx_actions.a[act_cur].n) SetAttrs(slist, MUIA_NoNotify, TRUE, MUIA_List_Active, act_step, TAG_DONE);
  else act_step = -1;
}
enum { AB_NEW = ID_MXB0, AB_DUP, AB_DEL, AB_RENAME, AB_TOGGLE, AB_UP, AB_DOWN, AB_DELSTEP, AB_STOP, AB_RECORD, AB_PLAY, AB_PLAYFROM,
       AB_LOAD, AB_SAVE, AB_PICK, AB_STEPPICK, AB_STOPREC, AB_SET };
static void actions_each(ULONG id)
{
  char name[ACT_NAMELEN], path[512];
  ActionLib *l = &mx_actions;
  switch (id) {
  case AB_PICK: {
    /* (a list cleared and filled again says it has no active row on the way: ignored) */
    LONG a = (LONG)xget(alist, MUIA_List_Active);
    if (a >= 0 && a != act_cur) { act_cur = (int)a; act_step = -1; steps_fill(); }
    return; }
  case AB_STEPPICK: { LONG a = (LONG)xget(slist, MUIA_List_Active); if (a >= 0) act_step = (int)a; return; }
  case AB_NEW: {
    char set[ACT_NAMELEN];
    strcpy(set, act_cur >= 0 ? l->a[act_cur].set : "Default Actions");
    sprintf(name, "%s %d", tr("Action"), l->n + 1);
    if (!ask_string("New Action", "Name:", name, sizeof name)) return;
    if ((act_cur = act_new(l, set, name)) < 0) { ask("OK", "%s", tr("There isn't enough memory for that.")); return; }
    break; }
  case AB_SET:
    if (act_cur < 0) return;
    strcpy(name, l->a[act_cur].set);
    if (!ask_string("Action Set", "The set this action is in:", name, sizeof name) || !name[0]) return;
    strcpy(l->a[act_cur].set, name);
    break;
  case AB_DUP: if (act_cur >= 0) act_cur = act_duplicate(l, act_cur); break;
  case AB_DEL:
    if (act_cur < 0 || !ask("Delete|Cancel", tr("Delete the action %s?"), l->a[act_cur].name)) return;
    if (mx_recording && mx_rec_action == act_cur) mx_recording = 0;
    else if (mx_rec_action > act_cur) mx_rec_action--;
    act_delete(l, act_cur);
    break;
  case AB_RENAME:
    if (act_cur < 0) return;
    strcpy(name, l->a[act_cur].name);
    if (!ask_string("Action Name", "Name:", name, sizeof name) || !name[0]) return;
    strcpy(l->a[act_cur].name, name);
    break;
  case AB_TOGGLE: if (act_cur >= 0 && act_step >= 0) l->a[act_cur].s[act_step].on = !l->a[act_cur].s[act_step].on; break;
  case AB_UP: if (act_cur >= 0 && act_step > 0 && act_move_step(l, act_cur, act_step, act_step - 1)) act_step--; break;
  case AB_DOWN: if (act_cur >= 0 && act_step >= 0 && act_move_step(l, act_cur, act_step, act_step + 1)) act_step++; break;
  case AB_DELSTEP: if (act_cur >= 0 && act_step >= 0) act_delete_step(l, act_cur, act_step); break;
  case AB_STOP: {
    char msg[ACT_NAMELEN] = "";
    if (act_cur < 0 || !ask_string("Insert Stop", "Message:", msg, sizeof msg)) return;
    act_insert_step(l, act_cur, act_step >= 0 ? act_step + 1 : -1, ACT_STOP, msg, 0);
    break; }
  case AB_STOPREC: mx_recording = 0; break;
  case AB_LOAD:
    strcpy(path, "PROGDIR:");
    if (mx_file_request(0, "Load Actions", path, sizeof path, 0)) {
      size_t n;
      char *t = (char *)file_read(path, &n), *z;
      if (t && (z = (char *)realloc(t, n + 1))) { t = z; z[n] = 0; if (!actlib_load(l, z, 1)) ask("OK", "%s", tr("That isn't an Easel actions file.")); }
      free(t);
    }
    break;
  case AB_SAVE:
    strcpy(path, "PROGDIR:My.actions");
    if (mx_file_request(1, "Save Actions", path, sizeof path, 0) && !file_save_text(path, actlib_save(l))) ask("OK", "%s", tr("The file couldn't be written."));
    break;
  default: return;
  }
  actions_fill(); steps_fill();
}
void dlg_actions(void)
{
  static const ULONG ends[] = { ID_DLGOK, AB_RECORD, AB_PLAY, AB_PLAYFROM, 0 };
  Object *w;
  ULONG r;
  w = dialog_window("Actions", VGroup,
        Child, HGroup,
          Child, VGroup, MUIA_HorizWeight, 100, Child, QTitleObject(tr("Actions")), Child, listview(&alist, NULL),
            Child, HGroup, Child, button("New...", AB_NEW), Child, button("Duplicate", AB_DUP), Child, button("Rename...", AB_RENAME), End,
            Child, HGroup, Child, button("Set...", AB_SET), Child, button("Delete", AB_DEL), End,
          End,
          Child, VGroup, MUIA_HorizWeight, 100, Child, QTitleObject(tr("Steps")), Child, listview(&slist, NULL),
            Child, HGroup, Child, button("On/Off", AB_TOGGLE), Child, button("Up", AB_UP), Child, button("Down", AB_DOWN), End,
            Child, HGroup, Child, button("Insert Stop...", AB_STOP), Child, button("Delete Step", AB_DELSTEP), End,
          End,
        End,
        Child, HGroup, Child, button("Record", AB_RECORD), Child, button("Stop Recording", AB_STOPREC), Child, button("Play", AB_PLAY),
          Child, button("Play from Step", AB_PLAYFROM), Child, HSpace(8), Child, button("Load...", AB_LOAD), Child, button("Save...", AB_SAVE), End,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Record, then choose menu commands: each becomes a step. A step with\na dialog shows it when the action plays. Window > Stop Recording ends it."), End,
      End, "Close");
  if (!w) return;
  DoMethod(alist, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, AB_PICK);
  DoMethod(slist, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, AB_STEPPICK);
  if (act_cur < 0 && mx_actions.n) act_cur = 0;
  actions_fill(); steps_fill();
  r = run_modal(w, ends, actions_each);
  MUI_DisposeObject(w);
  alist = slist = NULL;
  if (r == AB_RECORD) {
    if (act_cur < 0) {
      char name[ACT_NAMELEN];
      sprintf(name, "%s %d", tr("Action"), mx_actions.n + 1);
      if ((act_cur = act_new(&mx_actions, NULL, name)) < 0) return;
    }
    mx_rec_action = act_cur; mx_recording = 1;
    refresh_all();
  } else if (r == AB_PLAY || r == AB_PLAYFROM) {
    if (!cur_doc()) { ask("OK", "%s", tr("Open a picture to play the action on.")); return; }
    play(act_cur, r == AB_PLAYFROM ? act_step : 0);
  }
  file_save_text(ACTIONS_FILE, actlib_save(&mx_actions));
}

/* ------------------------------------------------------------------ File > Automate > Batch */
static const char *const out_exts[] = { ".png", ".jpg", ".iff", ".ora", ".tif", ".psd", ".bmp", ".gif" };
static const char *const out_names[] = { "PNG", "JPEG", "IFF ILBM", "OpenRaster", "TIFF", "Photoshop PSD", "BMP", "GIF", NULL };
void dlg_batch(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static char src[256] = "RAM:", dst[256] = "RAM:";
  static int fmt;
  const char *names[MAXROWS + 1];
  Object *w, *ac, *s, *t, *f;
  int i, done = 0, failed = 0;
  struct FileInfoBlock *fib;
  BPTR lock;
  static char msg[200];
  if (!mx_actions.n) { ask("OK", "%s", tr("Record an action first (Window > Actions).")); return; }
  for (i = 0; i < mx_actions.n && i < MAXROWS; i++) names[i] = mx_actions.a[i].name;
  names[i] = NULL;
  w = dialog_window("Batch", ColGroup(2),
        Child, QLabel(tr("Action:"), MUIO_Label_Centered), Child, ac = CycleObject, MUIA_Cycle_Entries, (ULONG)names, MUIA_Cycle_Active, act_cur > 0 ? act_cur : 0, MUIA_CycleChain, 1, End,
        Child, QLabel(tr("Source drawer:"), MUIO_Label_Centered), Child, s = StringObject, StringFrame, MUIA_String_Contents, (ULONG)src, MUIA_String_MaxLen, 250, MUIA_CycleChain, 1, End,
        Child, QLabel(tr("Destination drawer:"), MUIO_Label_Centered), Child, t = StringObject, StringFrame, MUIA_String_Contents, (ULONG)dst, MUIA_String_MaxLen, 250, MUIA_CycleChain, 1, End,
        Child, QLabel(tr("Save as:"), MUIO_Label_Centered), Child, f = CycleObject, MUIA_Cycle_Entries, (ULONG)out_names, MUIA_Cycle_Active, fmt, MUIA_CycleChain, 1, End,
      End, "Run");
  if (!w) return;
  if (run_modal(w, ends, NULL) != ID_DLGOK) { MUI_DisposeObject(w); return; }
  act_cur = (int)xget(ac, MUIA_Cycle_Active); fmt = (int)xget(f, MUIA_Cycle_Active);
  strncpy(src, (const char *)xget(s, MUIA_String_Contents), sizeof src - 1);
  strncpy(dst, (const char *)xget(t, MUIA_String_Contents), sizeof dst - 1);
  MUI_DisposeObject(w);
  if (!Stricmp((STRPTR)src, (STRPTR)dst)) { ask("OK", "%s", tr("Choose a destination drawer other than the source.")); return; }
  if (!(lock = Lock((STRPTR)src, ACCESS_READ))) { ask("OK", "%s", tr("The source drawer couldn't be read.")); return; }
  if (!(fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL))) { UnLock(lock); return; }
  if (Examine(lock, fib))
    while (ExNext(lock, fib)) {
      char in[512], out[512], base[108], *dot;
      size_t n = strlen(fib->fib_FileName);
      if (fib->fib_DirEntryType >= 0 || (n > 5 && !Stricmp((STRPTR)fib->fib_FileName + n - 5, (STRPTR)".info"))) continue;
      if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) break;
      strncpy(in, src, sizeof in - 1); in[sizeof in - 1] = 0;
      AddPart((STRPTR)in, (STRPTR)fib->fib_FileName, sizeof in);
      if (!mx_open(in)) { failed++; continue; }
      play(act_cur, 0);
      strncpy(base, fib->fib_FileName, sizeof base - 6); base[sizeof base - 6] = 0;
      if ((dot = strrchr(base, '.'))) *dot = 0;
      strcat(base, out_exts[fmt]);
      strncpy(out, dst, sizeof out - 1); out[sizeof out - 1] = 0;
      AddPart((STRPTR)out, (STRPTR)base, sizeof out);
      if (cur_doc() && mx_write(cur_doc(), out)) done++; else failed++;
      mx_close_current();
    }
  FreeDosObject(DOS_FIB, fib);
  UnLock(lock);
  refresh_all();
  sprintf(msg, tr("%d pictures done, %d couldn't be opened or saved."), done, failed);
  ask("OK", "%s", msg);
}

/* ------------------------------------------------------------------ Window > Styles */
static char strow[MAXROWS][ACT_NAMELEN + 8];
static Object *stlist;
static int st_cur = -1;
static void styles_fill(void)
{
  int i;
  set(stlist, MUIA_List_Quiet, TRUE);
  DoMethod(stlist, MUIM_List_Clear);
  for (i = 0; i < styles.n && i < MAXROWS; i++) {
    int k, nfx = 0;
    for (k = 0; k < FX_N; k++) nfx += styles.s[i].fx.e[k].on != 0;
    snprintf(strow[i], sizeof strow[i], "%s (%d)", styles.s[i].name, nfx);
    DoMethod(stlist, MUIM_List_InsertSingle, (ULONG)strow[i], MUIV_List_Insert_Bottom);
  }
  set(stlist, MUIA_List_Quiet, FALSE);
  if (st_cur >= styles.n) st_cur = styles.n - 1;
  SetAttrs(stlist, MUIA_NoNotify, TRUE, MUIA_List_Active, st_cur >= 0 ? st_cur : MUIV_List_Active_Off, TAG_DONE);
}
enum { SB_APPLY = ID_MXB0, SB_NEW, SB_RENAME, SB_DEL, SB_LOAD, SB_SAVE, SB_RESET, SB_PICK, SB_DBL };
static void styles_each(ULONG id)
{
  Doc *d = cur_doc();
  char name[ACT_NAMELEN], path[512];
  switch (id) {
  case SB_PICK: { LONG a = (LONG)xget(stlist, MUIA_List_Active); if (a >= 0) st_cur = (int)a; return; }
  case SB_APPLY: case SB_DBL:
    if (!d || st_cur < 0) return;
    doc_paste_fx(d, &styles.s[st_cur].fx);
    refresh_all();
    return;
  case SB_NEW:
    if (!d || !fx_has(d->lay[d->active])) { ask("OK", "%s", tr("The active layer has no layer style to save.")); return; }
    sprintf(name, "%s %d", tr("Style"), styles.n + 1);
    if (!ask_string("New Style", "Name:", name, sizeof name)) return;
    st_cur = stylelib_add(&styles, name, &d->lay[d->active]->fx);
    break;
  case SB_RENAME:
    if (st_cur < 0) return;
    strcpy(name, styles.s[st_cur].name);
    if (!ask_string("Style Name", "Name:", name, sizeof name)) return;
    stylelib_rename(&styles, st_cur, name);
    break;
  case SB_DEL: stylelib_delete(&styles, st_cur); break;
  case SB_RESET: stylelib_free(&styles); stylelib_defaults(&styles); st_cur = 0; break;
  case SB_LOAD:
    strcpy(path, "PROGDIR:");
    if (mx_file_request(0, "Load Styles", path, sizeof path, 0)) {
      size_t n;
      char *t = (char *)file_read(path, &n), *z;
      if (t && (z = (char *)realloc(t, n + 1))) { t = z; z[n] = 0; if (!stylelib_load(&styles, z, 1)) ask("OK", "%s", tr("That isn't an Easel styles file.")); }
      free(t);
    }
    break;
  case SB_SAVE:
    strcpy(path, "PROGDIR:My.styles");
    if (mx_file_request(1, "Save Styles", path, sizeof path, 0) && !file_save_text(path, stylelib_save(&styles))) ask("OK", "%s", tr("The file couldn't be written."));
    break;
  default: return;
  }
  styles_fill();
}
void dlg_styles(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w;
  styles_ready();
  w = dialog_window("Styles", VGroup,
        Child, listview(&stlist, NULL),
        Child, HGroup, Child, button("Apply", SB_APPLY), Child, button("New Style...", SB_NEW), Child, button("Rename...", SB_RENAME), Child, button("Delete", SB_DEL), End,
        Child, HGroup, Child, button("Load...", SB_LOAD), Child, button("Save...", SB_SAVE), Child, button("Reset Styles", SB_RESET), End,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Apply puts the style on the picked layers (double-click does it too).\nNew Style saves the active layer's style."), End,
      End, "Close");
  if (!w) return;
  DoMethod(stlist, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, SB_PICK);
  DoMethod(stlist, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, SB_DBL);
  styles_fill();
  run_modal(w, ends, styles_each);
  MUI_DisposeObject(w);
  stlist = NULL;
  file_save_text(STYLES_FILE, stylelib_save(&styles));
}

/* ------------------------------------------------------------------ Window > Layer Comps */
#define MAXCOMPS 24
static struct { Doc *doc; LayerComp c[MAXCOMPS]; int n; } comps[MAXDOCS];
static Object *clist;
static char crow[MAXCOMPS][ACT_NAMELEN + 40];
static int comp_cur = -1, comp_slot = -1;
static int comp_slot_for(Doc *d, int make)
{
  int i;
  for (i = 0; i < MAXDOCS; i++) if (comps[i].doc == d) return i;
  if (!make) return -1;
  for (i = 0; i < MAXDOCS; i++) if (!comps[i].doc) { comps[i].doc = d; comps[i].n = 0; return i; }
  return -1;
}
void comps_forget(Doc *d)
{
  int i, k = comp_slot_for(d, 0);
  if (k < 0) return;
  for (i = 0; i < comps[k].n; i++) comp_free(&comps[k].c[i]);
  comps[k].doc = NULL; comps[k].n = 0;
}
static void comps_fill(void)
{
  int i, n = comp_slot >= 0 ? comps[comp_slot].n : 0;
  set(clist, MUIA_List_Quiet, TRUE);
  DoMethod(clist, MUIM_List_Clear);
  for (i = 0; i < n; i++) {
    const LayerComp *c = &comps[comp_slot].c[i];
    snprintf(crow[i], sizeof crow[i], "%s  (%s%s%s)", c->name, c->what & COMP_VIS ? tr("visibility") : "", c->what == (COMP_VIS | COMP_LOOK) ? ", " : "", c->what & COMP_LOOK ? tr("appearance") : "");
    DoMethod(clist, MUIM_List_InsertSingle, (ULONG)crow[i], MUIV_List_Insert_Bottom);
  }
  set(clist, MUIA_List_Quiet, FALSE);
  if (comp_cur >= n) comp_cur = n - 1;
  SetAttrs(clist, MUIA_NoNotify, TRUE, MUIA_List_Active, comp_cur >= 0 ? comp_cur : MUIV_List_Active_Off, TAG_DONE);
}
enum { CB_NEW = ID_MXB0, CB_APPLY, CB_UPDATE, CB_DEL, CB_PREV, CB_NEXT, CB_EXPORT, CB_PICK, CB_DBL };
static void comps_export(Doc *d)
{
  DocView *v = cur_view();
  char dir[512] = "RAM:", path[600], base[108], *dot;
  int i, ok = 0;
  if (!comps[comp_slot].n) return;
  if (v->path[0]) { strncpy(dir, v->path, sizeof dir - 1); dir[sizeof dir - 1] = 0; *PathPart((STRPTR)dir) = 0; }
  if (!ask_string("Layer Comps to Files", "Drawer to write them to:", dir, sizeof dir)) return;
  strncpy(base, v->name, sizeof base - 1); base[sizeof base - 1] = 0;
  if ((dot = strrchr(base, '.'))) *dot = 0;
  for (i = 0; i < comps[comp_slot].n; i++) {
    char file[200];
    size_t k;
    snprintf(file, sizeof file, "%s-%s.png", base, comps[comp_slot].c[i].name);
    for (k = 0; file[k]; k++) if (strchr("/:*?\"#", file[k])) file[k] = '_';
    strcpy(path, dir);
    AddPart((STRPTR)path, (STRPTR)file, sizeof path);
    comp_apply(d, &comps[comp_slot].c[i]);
    ok += mx_write(d, path);
  }
  refresh_all();
  if (ok < comps[comp_slot].n) ask("OK", "%s", tr("Some of the files couldn't be written."));
}
static void comps_each(ULONG id)
{
  Doc *d = cur_doc();
  LayerComp *cs;
  char name[ACT_NAMELEN];
  if (!d || comp_slot < 0) return;
  cs = comps[comp_slot].c;
  switch (id) {
  case CB_PICK: { LONG a = (LONG)xget(clist, MUIA_List_Active); if (a >= 0) comp_cur = (int)a; return; }
  case CB_NEW: {
    LONG what = 3;
    if (comps[comp_slot].n >= MAXCOMPS) { ask("OK", "%s", tr("A picture keeps at most 24 layer comps.")); return; }
    sprintf(name, "%s %d", tr("Layer Comp"), comps[comp_slot].n + 1);
    if (!ask_string("New Layer Comp", "Name:", name, sizeof name)) return;
    if (!ask_number("New Layer Comp", "Keep 1 visibility, 2 appearance, 3 both:", &what, 1, 3)) return;
    if (!comp_capture(&cs[comps[comp_slot].n], d, name, (int)what)) { ask("OK", "%s", tr("There isn't enough memory for that.")); return; }
    comp_cur = comps[comp_slot].n++;
    break; }
  case CB_APPLY: case CB_DBL: if (comp_cur >= 0) { comp_apply(d, &cs[comp_cur]); refresh_all(); } return;
  case CB_UPDATE:
    if (comp_cur >= 0) {
      LayerComp n;
      if (comp_capture(&n, d, cs[comp_cur].name, cs[comp_cur].what)) { comp_free(&cs[comp_cur]); cs[comp_cur] = n; }
    }
    break;
  case CB_DEL:
    if (comp_cur < 0) return;
    comp_free(&cs[comp_cur]);
    memmove(cs + comp_cur, cs + comp_cur + 1, sizeof(LayerComp) * (size_t)(comps[comp_slot].n - comp_cur - 1));
    comps[comp_slot].n--;
    break;
  case CB_PREV: case CB_NEXT:
    if (!comps[comp_slot].n) return;
    comp_cur = comp_cur < 0 ? 0 : (comp_cur + (id == CB_NEXT ? 1 : comps[comp_slot].n - 1)) % comps[comp_slot].n;
    comp_apply(d, &cs[comp_cur]);
    refresh_all();
    break;
  case CB_EXPORT: comps_export(d); break;
  default: return;
  }
  comps_fill();
}
void dlg_comps(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w;
  Doc *d = cur_doc();
  if (!d || (comp_slot = comp_slot_for(d, 1)) < 0) return;
  comp_cur = comps[comp_slot].n ? 0 : -1;
  w = dialog_window("Layer Comps", VGroup,
        Child, listview(&clist, NULL),
        Child, HGroup, Child, button("New...", CB_NEW), Child, button("Apply", CB_APPLY), Child, button("Update", CB_UPDATE), Child, button("Delete", CB_DEL), End,
        Child, HGroup, Child, button("Previous", CB_PREV), Child, button("Next", CB_NEXT), Child, button("Export to Files...", CB_EXPORT), End,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("A comp keeps the layers' visibility and appearance (opacity, fill, mode\nand style). Each Apply is a step in the History."), End,
      End, "Close");
  if (!w) return;
  DoMethod(clist, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, CB_PICK);
  DoMethod(clist, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, CB_DBL);
  comps_fill();
  run_modal(w, ends, comps_each);
  MUI_DisposeObject(w);
  clist = NULL;
}

/* ------------------------------------------------------------------ Edit > Search */
#define MAXCMDS 700
static struct { ULONG id; char label[80]; } cmds[MAXCMDS];
static int ncmds, shown[MAXCMDS], nshown;
static Object *qstr, *qlist;
static void cmds_gather(void)
{
  const struct NewMenu *m = menu_table();
  char title[32] = "", item[40] = "";
  int i;
  if (ncmds) return;
  for (i = 0; m[i].nm_Type != NM_END && ncmds < MAXCMDS; i++) {
    const char *l = (const char *)m[i].nm_Label;
    if (m[i].nm_Label == NM_BARLABEL) continue;
    if (m[i].nm_Type == NM_TITLE) { snprintf(title, sizeof title, "%s", tr(l)); continue; }
    if (m[i].nm_Type == NM_ITEM) snprintf(item, sizeof item, "%s", tr(l));
    if (!m[i].nm_UserData) continue;
    cmds[ncmds].id = (ULONG)m[i].nm_UserData;
    if (m[i].nm_Type == NM_SUB) snprintf(cmds[ncmds].label, sizeof cmds[0].label, "%s > %s > %s", title, item, tr(l));
    else snprintf(cmds[ncmds].label, sizeof cmds[0].label, "%s > %s", title, tr(l));
    ncmds++;
  }
}
/* every word of what is typed somewhere in the label, case aside */
static int matches(const char *label, const char *q)
{
  char w[40];
  while (*q) {
    int n = 0, k, L = (int)strlen(label), found = 0;
    while (*q == ' ') q++;
    while (*q && *q != ' ' && n < (int)sizeof w - 1) w[n++] = *q++;
    w[n] = 0;
    if (!n) break;
    for (k = 0; k + n <= L && !found; k++) if (!str_ncasecmp(label + k, w, (size_t)n)) found = 1;
    if (!found) return 0;
  }
  return 1;
}
static void search_fill(void)
{
  const char *q = qstr ? (const char *)xget(qstr, MUIA_String_Contents) : "";
  int i;
  set(qlist, MUIA_List_Quiet, TRUE);
  DoMethod(qlist, MUIM_List_Clear);
  for (nshown = 0, i = 0; i < ncmds; i++)
    if (matches(cmds[i].label, q ? q : "")) { shown[nshown++] = i; DoMethod(qlist, MUIM_List_InsertSingle, (ULONG)cmds[i].label, MUIV_List_Insert_Bottom); }
  set(qlist, MUIA_List_Quiet, FALSE);
  set(qlist, MUIA_List_Active, nshown ? 0 : MUIV_List_Active_Off);
}
enum { QB_TYPED = ID_MXB0, QB_RUN };
static void search_each(ULONG id) { if (id == QB_TYPED) search_fill(); }
void dlg_search(void)
{
  static const ULONG ends[] = { ID_DLGOK, QB_RUN, 0 };
  Object *w;
  ULONG r;
  LONG a;
  cmds_gather();
  w = dialog_window("Search", VGroup,
        Child, HGroup, Child, QLabel(tr("Find:"), MUIO_Label_Centered),
          Child, qstr = StringObject, StringFrame, MUIA_String_MaxLen, 38, MUIA_CycleChain, 1, End, End,
        Child, ListviewObject, MUIA_Listview_Input, TRUE, MUIA_CycleChain, 1,
          MUIA_Listview_List, qlist = ListObject, InputListFrame, (ULONG)(theme_dialog_font() ? MUIA_Font : TAG_IGNORE), (ULONG)theme_dialog_font(),
            MUIA_FixHeightTxt, (ULONG)"\n\n\n\n\n\n\n\n\n\n", MUIA_FixWidthTxt, (ULONG)"MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM", End, End,
        Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Type part of a command's name; Return or a double-click runs it."), End,
      End, "Run");
  if (!w) return;
  /* each change to the text filters the list; Return runs the first found */
  DoMethod(qstr, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, QB_TYPED);
  DoMethod(qstr, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, QB_RUN);
  DoMethod(qlist, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, QB_RUN);
  set(w, MUIA_Window_ActiveObject, (ULONG)qstr);
  search_fill();
  r = run_modal(w, ends, search_each);
  a = (LONG)xget(qlist, MUIA_List_Active);
  MUI_DisposeObject(w);
  qstr = qlist = NULL;
  if ((r == ID_DLGOK || r == QB_RUN) && a >= 0 && a < nshown) do_command(cmds[shown[a]].id);
}
