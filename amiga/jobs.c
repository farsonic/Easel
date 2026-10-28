/*
 * jobs.c - long operations with a progress window and an Abort button (0.8).
 *
 * The core's slow loops (filters, adjustments, Image Size, transforms, Color Range, the Magnetic
 * Lasso's edge map, AVIF tiles) call progress() as they go (core/util.h). While a job is running its
 * hook is set: once an operation has taken half a second a small window opens with a gauge and an
 * Abort button, and each call afterwards moves the gauge and lets MUI deal with the window's input
 * (MUIM_Application_InputBuffered), so Abort (or Esc, or the window's close gadget) is seen. The
 * core then stops, and job_end takes the operation's unfinished undo step back, leaving the picture
 * as it was. Jobs nest: each takes back only its own step.
 */
#include <string.h>
#include <stdio.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "lang.h"
#include "ids.h"
#include "../core/util.h"

#define MAXJOBS 8
static struct { Doc *doc; void *top; int (*hook)(long, long); } jobs[MAXJOBS];
static int njobs;
static Object *pwin, *pgauge, *ptext;
static LONG abort_flag;
static LONG started, lastcall;           /* when the operation now running began, and last told its progress */
static char what[80];                    /* its name, for the window's text */
int jobs_shown;                          /* (for testing) how many times the window has opened */

static ULONG xget(Object *o, ULONG a) { ULONG v = 0; get(o, a, &v); return v; }
/* the time in fiftieths of a second */
static LONG now_ticks(void)
{
  struct DateStamp now;
  DateStamp(&now);
  return now.ds_Days * (24L * 60 * 60 * 50) + now.ds_Minute * (60L * 50) + now.ds_Tick;
}
/* the progress window made and opened over the main window (awake, though the rest sleeps) */
static void pwin_open(void)
{
  Object *abort;
  int i;
  pwin = QWindowObject, MUIA_Window_Title, (ULONG)tr("Working"), MUIA_Window_RefWindow, (ULONG)win,
    MUIA_Window_LeftEdge, MUIV_Window_LeftEdge_Centered, MUIA_Window_TopEdge, MUIV_Window_TopEdge_Centered,
    MUIA_Window_CloseGadget, TRUE, MUIA_Window_SizeGadget, FALSE,
    WindowContents, VGroup, QDLG,
      Child, ptext = TextObject, MUIA_Text_Contents, (ULONG)what, End,
      Child, pgauge = GaugeObject, GaugeFrame, MUIA_Gauge_Horiz, TRUE, MUIA_Gauge_Max, 1000, MUIA_FixHeightTxt, (ULONG)"M", MUIA_FixWidthTxt, (ULONG)"MMMMMMMMMMMMMMMMMMMMMMMM", End,
      Child, HGroup, Child, HSpace(0), Child, abort = QButton(tr("_Abort (Esc)")), Child, HSpace(0), End,
    End,
  End;
  if (!pwin) return;
  DoMethod(app, OM_ADDMEMBER, (ULONG)pwin);
  DoMethod(abort, MUIM_Notify, MUIA_Pressed, FALSE, MUIV_Notify_Application, 3, MUIM_WriteLong, 1, (ULONG)&abort_flag);
  DoMethod(pwin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, MUIV_Notify_Application, 3, MUIM_WriteLong, 1, (ULONG)&abort_flag);
  set(pwin, MUIA_Window_Open, TRUE);
  /* the application is asleep while a command runs: this window alone woken */
  for (i = 0; i < 8 && xget(pwin, MUIA_Window_Sleep); i++) set(pwin, MUIA_Window_Sleep, FALSE);
  jobs_shown++;
  LOG("progress window opened: %s", what);
}
static void pwin_close(void)
{
  if (!pwin) return;
  set(pwin, MUIA_Window_Open, FALSE);
  DoMethod(app, OM_REMMEMBER, (ULONG)pwin);
  MUI_DisposeObject(pwin);
  pwin = pgauge = ptext = NULL;
  redraw_window();
}
/* the core's progress hook: the window opened once the operation has run half a second, the
   gauge moved, the window's input dealt with; nonzero when Abort has been pressed */
static int hook(long done, long total)
{
  static LONG shown_at;
  static int shown_pc = -1;
  LONG t;
  char buf[100];
  int pc;
  if (!app) return 0;
  t = now_ticks();
  /* (a pause since the last call, such as a dialog, and this is another operation: timed afresh) */
  if (!pwin && t - lastcall > 10) started = t;
  lastcall = t;
  t -= started;
  if (!pwin) {
    if (t < 25) return abort_flag != 0;
    pwin_open();
    shown_at = -100; shown_pc = -1;
  }
  /* the window brought up to date at most ten times a second */
  if (!pwin || t - shown_at < 5) return abort_flag != 0;
  shown_at = t;
  pc = total > 0 ? (int)((double)done * 100.0 / total) : 0;
  set(pgauge, MUIA_Gauge_Current, (ULONG)(total > 0 ? (double)done * 1000.0 / total : 0));
  if (pc != shown_pc) {
    shown_pc = pc;
    snprintf(buf, sizeof buf, "%s   %d%%", what, pc);
    set(ptext, MUIA_Text_Contents, (ULONG)buf);
  }
  DoMethod(app, MUIM_Application_InputBuffered);
  return abort_flag != 0;
}
/* (see easel.h) a job begun: d's undo step on top noted, the hook set */
void job_begin(Doc *d, const char *name)
{
  if (njobs >= MAXJOBS) { njobs++; return; }
  jobs[njobs].doc = d; jobs[njobs].top = d ? (void *)d->undo : NULL; jobs[njobs].hook = progress_hook;
  njobs++;
  if (njobs == 1) { abort_flag = 0; progress_reset(); }
  if (name && name[0]) {
    char *e;
    snprintf(what, sizeof what, "%s", tr(name));
    if ((e = strstr(what, "...")) != NULL) *e = 0;          /* ("Image Size...": the dialog is over) */
  }
  else if (njobs == 1) strcpy(what, tr("Working..."));
  if (ptext) set(ptext, MUIA_Text_Contents, (ULONG)what);
  started = lastcall = now_ticks();
  progress_hook = hook;
}
/* (see easel.h) the job ended: if Abort was pressed, its undo step taken back; 1 if it was aborted */
int job_end(void)
{
  int aborted = progress_aborted || abort_flag;
  if (njobs <= 0) return 0;
  njobs--;
  if (njobs >= MAXJOBS) return 0;
  if (aborted) {
    Doc *d = jobs[njobs].doc;
    LOG("job aborted");
    int i, open = 0;
    for (i = 0; i < ndocs; i++) if (docs[i].doc == d) open = 1;
    /* only a step this job made (the document still open, with something new on top) */
    if (d && open && (void *)d->undo != jobs[njobs].top && doc_can_undo(d)) doc_discard_last(d);
    abort_flag = 0;
    progress_reset();
  }
  progress_hook = jobs[njobs].hook;      /* (as it was: a dialog's previews run without it) */
  if (!njobs) pwin_close();
  return aborted;
}
int job_running(void) { return njobs > 0; }
