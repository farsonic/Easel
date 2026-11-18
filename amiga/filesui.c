/*
 * filesui.c - round 3's file commands in the interface: File > Open As's choice of reader (the work is
 * core/filesx.c's codec_load_as; main.c reads the file and shows the picture).
 */
#include <string.h>
#include <stdio.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <libraries/mui.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "lang.h"
#include "ids.h"
#include "theme.h"
#include "filesui.h"
#include "../core/filesx.h"

static ULONG xget(Object *o, ULONG a) { ULONG v = 0; get(o, a, &v); return v; }

/* (see filesui.h) */
int filesui_open_as_format(const char *file, int current)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *names[OA_N + 1];
  static char head[200];
  Object *w, *list;
  int i, k = -1;
  for (i = 0; i < OA_N; i++) names[i] = tr(open_as_names[i]);
  names[OA_N] = NULL;
  snprintf(head, sizeof head, tr("Open %s as:"), file);
  w = dialog_window("Open As", VGroup,
        Child, TextObject, MUIA_Text_Contents, (ULONG)head, End,
        Child, list = ListviewObject, MUIA_Listview_Input, TRUE, MUIA_CycleChain, 1, MUIA_Listview_DoubleClick, TRUE,
          MUIA_Listview_List, ListObject, InputListFrame, (ULONG)(theme_dialog_font() ? MUIA_Font : TAG_IGNORE), (ULONG)theme_dialog_font(),
            MUIA_List_SourceArray, (ULONG)names, MUIA_List_Active, current >= 0 && current < OA_N ? current : 0, End, End,
      End, "Open");
  if (!w) return -1;
  DoMethod(list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
  if (run_modal(w, ends, NULL) == ID_DLGOK) k = (int)(LONG)xget(list, MUIA_List_Active);
  MUI_DisposeObject(w);
  return k >= 0 && k < OA_N ? k : -1;
}

/* ------------------------------------------------------------------ round 3's list windows */
/* a window of a list and a row (or two) of buttons: Timeline, Notes, Measurement Log */
#include "filesids.h"
#include "tools2ids.h"
#include "brushwin.h"
#include "../core/legacy.h"
#include "../core/timeline.h"
#include "../core/toolsx.h"
#include "../core/measure.h"

#define FU_ID(a, b, c, d) (((ULONG)(a) << 24) | ((ULONG)(b) << 16) | ((ULONG)(c) << 8) | (ULONG)(d))
typedef struct { Object *win, *list, *info; } LWin;
static LWin tlw, ntw, mlw;
static MeasureLog mlog;
extern int ruler_on;
extern double ruler_xy[4];

static Object *lwin_make(LWin *lw, const char *title, ULONG wid, const char *const *buttons, int nb, int perrow, ULONG id0, ULONG selid)
{
  Object *rows[2] = { NULL, NULL }, *b;
  int i;
  if (lw->win) return lw->win;
  for (i = 0; i < 2; i++) rows[i] = HGroup, End;
  lw->list = ListviewObject, MUIA_Listview_Input, TRUE, MUIA_CycleChain, 1,
    MUIA_Listview_List, ListObject, InputListFrame, (ULONG)(theme_dialog_font() ? MUIA_Font : TAG_IGNORE), (ULONG)theme_dialog_font(),
      MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, End;
  lw->info = TextObject, MUIA_Text_Contents, (ULONG)"", End;
  lw->win = QWindowObject, MUIA_Window_Title, (ULONG)tr(title), MUIA_Window_ID, wid, MUIA_Window_Width, MUIV_Window_Width_Visible(45),
    MUIA_Window_Height, MUIV_Window_Height_Visible(40),
    WindowContents, VGroup, Child, lw->list, Child, lw->info, Child, rows[0], Child, rows[1], End,
  End;
  if (!lw->win) return NULL;
  for (i = 0; i < nb; i++) {
    b = SimpleButton(tr(buttons[i]));
    DoMethod(rows[i < perrow ? 0 : 1], OM_ADDMEMBER, (ULONG)b);
    DoMethod(b, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, id0 + i);
  }
  DoMethod(app, OM_ADDMEMBER, (ULONG)lw->win);
  DoMethod(lw->win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)lw->win, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  DoMethod(lw->list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, selid);
  return lw->win;
}
static int lwin_open(const LWin *lw) { return lw->win && xget(lw->win, MUIA_Window_Open); }
static int lwin_sel(const LWin *lw) { LONG a = (LONG)xget(lw->list, MUIA_List_Active); return a < 0 ? -1 : (int)a; }
static void lwin_fill_begin(LWin *lw) { set(lw->list, MUIA_List_Quiet, TRUE); DoMethod(lw->list, MUIM_List_Clear); }
static void lwin_add(LWin *lw, const char *s) { DoMethod(lw->list, MUIM_List_InsertSingle, (ULONG)s, MUIV_List_Insert_Bottom); }
static void lwin_fill_end(LWin *lw, int active)
{
  set(lw->list, MUIA_List_Quiet, FALSE);
  nnset(lw->list, MUIA_List_Active, active >= 0 ? active : MUIV_List_Active_Off);
}
/* hundredths of a second as text ("0.25 s") */
static void secs(char *o, size_t cap, long cs) { snprintf(o, cap, "%ld.%02ld s", cs / 100, cs % 100); }

/* the frame shown: the visible one of the frames, or the active layer's */
static int tl_current(Doc *d)
{
  int f[MAXLAYERS], n = frames_list(d, f, MAXLAYERS), k, vis = -1, nvis = 0;
  for (k = 0; k < n; k++) if (d->lay[f[k]]->visible) { vis = k; nvis++; }
  if (nvis == 1) return vis;
  for (k = 0; k < n; k++) if (f[k] >= d->active) return k;
  return n - 1;
}
static void tl_fill(Doc *d)
{
  int f[MAXLAYERS], n = d ? frames_list(d, f, MAXLAYERS) : 0, k;
  static char info[100];
  char line[120], t[24];
  lwin_fill_begin(&tlw);
  for (k = 0; k < n; k++) {
    int cs = frame_time(d->lay[f[k]]->name);
    secs(t, sizeof t, cs);
    snprintf(line, sizeof line, "%3d  %s  %s", k + 1, cs ? t : tr("default time"), d->lay[f[k]]->name);
    lwin_add(&tlw, line);
  }
  secs(t, sizeof t, d ? tl_length(d, 10) : 0);
  snprintf(info, sizeof info, tr("%d frames, %s"), n, t);
  set(tlw.info, MUIA_Text_Contents, (ULONG)info);
  lwin_fill_end(&tlw, d && n ? tl_current(d) : -1);
}
static void nt_fill(Doc *d)
{
  Annots *a = d ? doc_annots(d, 0) : NULL;
  int k;
  char line[120];
  static char info[80];
  lwin_fill_begin(&ntw);
  for (k = 0; a && k < a->nnote; k++) {
    char t[60];
    size_t l = utf8_to_latin1(a->note[k].text, strlen(a->note[k].text), t, sizeof t);
    char *nl;
    t[l < sizeof t ? l : sizeof t - 1] = 0;
    if ((nl = strchr(t, '\n')) != NULL) *nl = 0;
    snprintf(line, sizeof line, "%2d  %s%s%s", k + 1, a->note[k].author, a->note[k].author[0] ? ": " : "", t);
    lwin_add(&ntw, line);
  }
  snprintf(info, sizeof info, tr("%d notes"), a ? a->nnote : 0);
  set(ntw.info, MUIA_Text_Contents, (ULONG)info);
  lwin_fill_end(&ntw, -1);
}
static void ml_fill(void)
{
  int k;
  char line[160];
  lwin_fill_begin(&mlw);
  for (k = 0; k < mlog.n; k++) { mlog_line(&mlog.r[k], k, tr(measure_sources[mlog.r[k].source]), line, sizeof line); lwin_add(&mlw, line); }
  lwin_fill_end(&mlw, mlog.n ? mlog.n - 1 : -1);
}
/* (see filesui.h) */
void filesui_refresh(void)
{
  Doc *d = cur_doc();
  if (lwin_open(&tlw)) tl_fill(d);
  if (lwin_open(&ntw)) nt_fill(d);
  if (lwin_open(&mlw)) ml_fill();
}
static void show(LWin *lw) { if (lw->win) { set(lw->win, MUIA_Window_Open, TRUE); filesui_refresh(); } }

/* Image > Analysis > Record Measurements: the Ruler's line when there is one, else the selection
   (or the Count tool's markers when there are some and nothing is selected) */
static void record(Doc *d, const char *name)
{
  MeasureRec m;
  Annots *a = doc_annots(d, 0);
  if (ruler_on) measure_ruler(ruler_xy[0], ruler_xy[1], ruler_xy[2], ruler_xy[3], &m);
  else if (!d->sel && a && a->nmark) measure_count(d, &m);
  else if (!measure_selection(d, &m)) { ask("OK", "%s", tr("Nothing is selected to measure.")); return; }
  if (mlog_add(&mlog, &m, name) < 0) { ask("OK", "%s", tr("The Measurement Log is full: delete some rows.")); return; }
  if (!mlw.win) filesui_command(ID_WIN_MLOG);
  else show(&mlw);
}

int filesui_command(ULONG id)
{
  static const char *const tlb[] = { "|<", "<", "Play", ">", ">|", "New Frame", "Delete Frame", "Tween...", "Frame Time...", "Reverse Frames" };
  static const char *const ntb[] = { "Edit Note...", "Delete Note", "Delete All", "Export Notes..." };
  static const char *const mlb[] = { "Record Measurements", "Delete", "Clear All", "Export..." };
  Doc *d = cur_doc();
  DocView *v = cur_view();
  int f[MAXLAYERS], n = d ? frames_list(d, f, MAXLAYERS) : 0, k;
  switch (id) {
  case ID_WIN_TIMELINE: if (lwin_make(&tlw, "Timeline", FU_ID('E', 'T', 'L', 'N'), tlb, 10, 5, ID_TL_BTN0, ID_TL_SEL)) show(&tlw); return 0;
  case ID_WIN_NOTES: if (lwin_make(&ntw, "Notes", FU_ID('E', 'N', 'T', 'S'), ntb, 4, 4, ID_NT_BTN0, ID_NT_SEL)) show(&ntw); return 0;
  case ID_WIN_MLOG: if (lwin_make(&mlw, "Measurement Log", FU_ID('E', 'M', 'L', 'G'), mlb, 4, 4, ID_ML_BTN0, ID_ML_SEL)) show(&mlw); return 0;
  case ID_RECORDMEAS: case ID_ML_BTN0: if (d) record(d, v ? v->name : ""); return 0;
  case ID_ML_BTN0 + 1: mlog_delete(&mlog, lwin_sel(&mlw)); ml_fill(); return 0;
  case ID_ML_BTN0 + 2: mlog.n = 0; ml_fill(); return 0;
  case ID_ML_BTN0 + 3: case ID_NT_BTN0 + 3: {
    static char path[512] = "RAM:Measurements.txt";
    char *t = NULL;
    Buf b;
    if (id == ID_NT_BTN0 + 3) {
      Annots *a = d ? doc_annots(d, 0) : NULL;
      if (!a || !a->nnote) return 0;
      buf_init(&b);
      for (k = 0; k < a->nnote; k++) buf_printf(&b, "Note %d (%ld, %ld) %s\n%s\n\n", k + 1, (long)a->note[k].x, (long)a->note[k].y, a->note[k].author, a->note[k].text);
      t = b.oom ? NULL : buf_take(&b);
      buf_free(&b);
      if (!strstr(path, "Notes")) strcpy(path, "RAM:Notes.txt");
    } else { if (!mlog.n) return 0; t = mlog_text(&mlog); }
    if (!t) { ask("OK", "%s", tr("There isn't enough memory for that.")); return 0; }
    if (ui_file_request(1, id == ID_NT_BTN0 + 3 ? "Export the notes" : "Export the measurements", path, sizeof path, "#?.txt") && !file_write(path, t, strlen(t)))
      ask("OK", "%s", tr("The file couldn't be written."));
    free(t);
    return 0; }
  case ID_TL_SEL:
    if (!d || (k = lwin_sel(&tlw)) < 0 || k >= n) return 0;
    frames_show(d, k); d->active = f[k];
    return 1;
  case ID_TL_BTN0: case ID_TL_BTN0 + 4:
    if (n) { k = id == ID_TL_BTN0 ? 0 : n - 1; frames_show(d, k); d->active = f[k]; }
    return 1;
  case ID_TL_BTN0 + 1: DoMethod(app, MUIM_Application_ReturnID, ID_FRAMEPREV); return 0;
  case ID_TL_BTN0 + 2: DoMethod(app, MUIM_Application_ReturnID, ID_FRAMEPLAY); return 0;
  case ID_TL_BTN0 + 3: DoMethod(app, MUIM_Application_ReturnID, ID_FRAMENEXT); return 0;
  case ID_TL_BTN0 + 5:
    if (d && !tl_new_frame(d, n ? tl_current(d) : 0)) ask("OK", "%s", tr("A group frame can't be copied:\nchoose a frame that is one layer."));
    else if (d) frames_show(d, tl_current(d));
    return 1;
  case ID_TL_BTN0 + 6:
    if (d && !tl_delete_frame(d, tl_current(d))) ask("OK", "%s", tr("The last frame can't be deleted."));
    return 1;
  case ID_TL_BTN0 + 7: {
    static LONG nt = 3;
    if (!d || !ask_number("Tween", "Frames to add after this one:", &nt, 1, 50)) return 0;
    if (!tl_tween(d, tl_current(d), (int)nt)) ask("OK", "%s", tr("Tween needs this frame and the next to be pixel layers (and room for more layers)."));
    return 1; }
  case ID_TL_BTN0 + 8: {
    LONG cs = 10;
    int r;
    if (!d || !n) return 0;
    r = ask("This Frame...|All Frames...|Cancel", "%s", tr("Set the time of this frame, or of every frame?"));
    if (r == 1) { DoMethod(app, MUIM_Application_ReturnID, ID_FRAMETIME); return 0; }
    if (r != 2) return 0;
    cs = frame_time(d->lay[f[tl_current(d)]]->name);
    if (!ask_number("Frame Time", "Hundredths of a second (0: the default):", &cs, 0, 6000)) return 0;
    tl_set_all_times(d, (int)cs);
    return 1; }
  case ID_TL_BTN0 + 9:
    if (d && !tl_reverse(d)) ask("OK", "%s", tr("Reverse Frames needs every frame to be one layer."));
    return 1;
  case ID_NT_SEL: return 0;
  case ID_NT_BTN0: {
    Annots *a = d ? doc_annots(d, 0) : NULL;
    char buf[NOTE_LEN];
    if (!a || (k = lwin_sel(&ntw)) < 0 || k >= a->nnote) return 0;
    strncpy(buf, a->note[k].text, sizeof buf - 1); buf[sizeof buf - 1] = 0;
    if (ask_string("Edit Note", "Note:", buf, sizeof buf)) note_set(d, k, buf);
    return 1; }
  case ID_NT_BTN0 + 1: if (d && (k = lwin_sel(&ntw)) >= 0) note_delete(d, k); return 1;
  case ID_NT_BTN0 + 2: if (d && ask("Delete All|Cancel", "%s", tr("Delete every note in the picture?")) == 1) note_clear(d); return 1;
  }
  return 0;
}
