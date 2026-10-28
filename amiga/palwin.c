/*
 * palwin.c - Easel 0.4's Palette window (see palwin.h): DPaint's palette requester in Easel's
 * theme. The swatch grid is a custom class drawn with the theme's pens; it also receives the
 * window's commands (each gadget notifies it with MUIM_PalWin_Cmd) and the cycling timer (a MUI
 * timer input handler, MUIIHNF_TIMER, which MUI 3.8 has), so the window needs nothing from
 * main.c's loop.
 *
 * Undo: each palette edit is an undo step of the document (palette.h). A run of slider moves on
 * one entry makes one step: the palette is saved (doc_note_pal) when the run starts, in a step that
 * is closed at once, and the moves after it change the palette without new steps, until another
 * entry is chosen or anything else is done to the document.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <exec/memory.h>
#include <devices/inputevent.h>
#include <libraries/asl.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "theme.h"
#include "lang.h"
#include "palwin.h"
#include "../core/palette.h"

void (*palwin_redraw)(void);
void (*palwin_picked)(int entry);

#define MUIM_PalWin_Cmd  (QTAG + 0x900)  /* struct { MethodID; cmd } from the gadgets */
#define MUIM_PalWin_Tick (QTAG + 0x901)  /* the cycling timer */
struct MUIP_PalWin_Cmd { ULONG MethodID; ULONG cmd; };
#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) (((ULONG)(a) << 24) | ((ULONG)(b) << 16) | ((ULONG)(c) << 8) | (ULONG)(d))
#endif
enum { PC_RGB = 1, PC_HSV, PC_SPREAD, PC_EX, PC_COPY, PC_UNDO, PC_LOCK, PC_LOAD, PC_SAVE, PC_CYCSEL, PC_CYCEDIT,
       PC_CYCADD, PC_CYCDEL, PC_CYCRANGE, PC_PLAY, PC_STOP, PC_EHB, PC_CLOSE };
enum { PEND_NONE, PEND_SPREAD, PEND_EX, PEND_COPY };

/* ------------------------------------------------------------------ the window's state */
static struct MUI_CustomClass *gridcls;
static Object *pw_app, *pw_win, *pw_grid, *pw_sl[6], *pw_info, *pw_lock, *pw_list, *pw_lo, *pw_hi, *pw_rate, *pw_dir, *pw_active,
              *pw_fmt, *pw_play, *pw_stop;
static Doc *pw_doc;
static int pw_cur, pw_pending, pw_curcyc = -1, pw_rlo = -1, pw_rhi = -1, pw_playing, pw_edit_entry = -1;
static void *pw_edit_step;
static u8 pw_saved[768];
static u32 pw_acc[MAXCYCLES];
static char pw_infotext[96];
static char pw_cyctext[MAXCYCLES][48];

static ULONG xget(Object *o, ULONG a) { ULONG v = 0; if (o) get(o, a, &v); return v; }
static int npal(void) { return pw_doc ? (pw_doc->npal < 1 ? 1 : pw_doc->npal > 256 ? 256 : pw_doc->npal) : 0; }
static void changed(void) { if (palwin_redraw) palwin_redraw(); }

/* ------------------------------------------------------------------ the swatch grid */
#define GCOLS 16
#define GROWS 16
struct GridData { struct MUI_EventHandlerNode ehn; struct MUI_InputHandlerNode ihn; int shown, timer; };
static void cell_rect(Object *obj, int i, LONG *x0, LONG *y0, LONG *x1, LONG *y1)
{
  LONG w = _mwidth(obj), h = _mheight(obj), c = i % GCOLS, r = i / GCOLS;
  *x0 = _mleft(obj) + c * w / GCOLS; *x1 = _mleft(obj) + (c + 1) * w / GCOLS - 2;
  *y0 = _mtop(obj) + r * h / GROWS; *y1 = _mtop(obj) + (r + 1) * h / GROWS - 2;
}
static void grid_draw(Object *obj)
{
  struct RastPort *rp = _rp(obj);
  struct Screen *scr = _screen(obj);
  int i, n = npal();
  theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_PANEL));
  for (i = 0; i < GCOLS * GROWS; i++) {
    LONG x0, y0, x1, y1;
    cell_rect(obj, i, &x0, &y0, &x1, &y1);
    if (i >= n) { theme_rect(rp, x0 + 2, y0 + 2, x1 - 2, y1 - 2, tpen(T_BORDER)); continue; }
    {
      const u8 *c = pw_doc->pal + i * 3;
      draw_rgb_box(rp, scr, x0, y0, x1, y1, ((ULONG)c[0] << 16) | ((ULONG)c[1] << 8) | c[2]);
      /* a lock: a small square in the corner, in black or white for contrast */
      if (pw_doc->pal_lock[i]) {
        LONG pen = (c[0] * 77 + c[1] * 150 + c[2] * 29) >> 8 > 128 ? tpen(T_TEXT) : tpen(T_WHITE);
        theme_fill(rp, x0 + 1, y0 + 1, x0 + 4, y0 + 4, pen);
      }
      /* the cycle range shown, and a marked range */
      if (pw_curcyc >= 0 && pw_curcyc < pw_doc->ncycle && i >= pw_doc->cycle[pw_curcyc].lo && i <= pw_doc->cycle[pw_curcyc].hi)
        theme_fill(rp, x0, y1 - 1, x1, y1, tpen(T_ORANGE));
      if (pw_rlo >= 0 && i >= pw_rlo && i <= pw_rhi) theme_fill(rp, x0, y0, x1, y0 + 1, tpen(T_GREEN));
    }
    if (i == pw_cur) {
      theme_rect(rp, x0 - 1, y0 - 1, x1 + 1, y1 + 1, tpen(T_ACCENT));
      theme_rect(rp, x0, y0, x1, y1, tpen(T_WHITE));
    }
  }
}
static int grid_hit(Object *obj, LONG mx, LONG my)
{
  LONG c, r;
  if (mx < _mleft(obj) || mx > _mright(obj) || my < _mtop(obj) || my > _mbottom(obj)) return -1;
  c = (mx - _mleft(obj)) * GCOLS / _mwidth(obj); r = (my - _mtop(obj)) * GROWS / _mheight(obj);
  if (c < 0 || c >= GCOLS || r < 0 || r >= GROWS) return -1;
  return (int)(r * GCOLS + c);
}
static void grid_redraw(void)
{
  /* (only while the grid is shown: never during the window's cleanup) */
  if (pw_grid && gridcls && ((struct GridData *)INST_DATA(gridcls->mcc_Class, pw_grid))->shown) MUI_Redraw(pw_grid, MADF_DRAWOBJECT);
}

/* ------------------------------------------------------------------ showing the state */
static void show_entry(void)
{
  int r = 0, g = 0, b = 0, h, s, v, i;
  LONG vals[6];
  if (pw_doc && pw_cur < 256) { r = pw_doc->pal[pw_cur * 3]; g = pw_doc->pal[pw_cur * 3 + 1]; b = pw_doc->pal[pw_cur * 3 + 2]; }
  pal_rgb_to_hsv(r, g, b, &h, &s, &v);
  vals[0] = r; vals[1] = g; vals[2] = b; vals[3] = h; vals[4] = s; vals[5] = v;
  for (i = 0; i < 6; i++) if (pw_sl[i]) nnset(pw_sl[i], MUIA_Numeric_Value, vals[i]);
  if (pw_lock) nnset(pw_lock, MUIA_Selected, pw_doc ? pw_doc->pal_lock[pw_cur] : 0);
  if (!pw_doc) strcpy(pw_infotext, tr("No picture"));
  else if (pw_pending) sprintf(pw_infotext, "%s", tr(pw_pending == PEND_SPREAD ? "Spread: click the last colour" : pw_pending == PEND_EX ?
                                                     "Exchange: click the other colour" : "Copy: click where it goes"));
  else sprintf(pw_infotext, "%s %d  #%02X%02X%02X  (%d %s)%s", tr("Colour"), pw_cur, r, g, b, npal(), tr("colours"),
               pw_doc->mode == MODE_INDEXED ? "" : pw_doc->mode == MODE_BITMAP ? "" : tr("  - not indexed"));
  if (pw_info) set(pw_info, MUIA_Text_Contents, (ULONG)pw_infotext);
}
static void show_cycles(void)
{
  int i;
  if (!pw_list) return;
  DoMethod(pw_list, MUIM_List_Clear);
  for (i = 0; pw_doc && i < pw_doc->ncycle && i < MAXCYCLES; i++) {
    const CycleRange *c = &pw_doc->cycle[i];
    u32 hz = cycle_rate_hz100(c->rate);
    sprintf(pw_cyctext[i], "%d: %d-%d  %lu.%02lu/s  %s%s", i + 1, (int)c->lo, (int)c->hi, (unsigned long)(hz / 100), (unsigned long)(hz % 100),
            c->reverse ? "<<" : ">>", c->active ? "" : " (off)");
    DoMethod(pw_list, MUIM_List_InsertSingle, (ULONG)pw_cyctext[i], MUIV_List_Insert_Bottom);
  }
  if (!pw_doc || pw_curcyc >= pw_doc->ncycle) pw_curcyc = pw_doc && pw_doc->ncycle ? 0 : -1;
  nnset(pw_list, MUIA_List_Active, pw_curcyc >= 0 ? pw_curcyc : MUIV_List_Active_Off);
  if (pw_curcyc >= 0) {
    const CycleRange *c = &pw_doc->cycle[pw_curcyc];
    nnset(pw_lo, MUIA_Numeric_Value, c->lo); nnset(pw_hi, MUIA_Numeric_Value, c->hi);
    nnset(pw_rate, MUIA_Numeric_Value, (LONG)((c->rate * 60UL + 8192) / 16384));
    nnset(pw_dir, MUIA_Cycle_Active, c->reverse ? 1 : 0); nnset(pw_active, MUIA_Selected, c->active);
  }
}
void palwin_refresh(void)
{
  if (pw_doc && pw_cur >= npal()) pw_cur = npal() - 1;
  if (pw_cur < 0) pw_cur = 0;
  show_entry();
  show_cycles();
  grid_redraw();
}

/* ------------------------------------------------------------------ editing */
/* the undo step for a run of edits on one entry (or one cycle range, key -2 - range) */
static void edit_begin(int key, const char *what)
{
  if (pw_doc->undo != pw_edit_step || pw_edit_entry != key) {
    doc_begin(pw_doc, what);
    doc_note_pal(pw_doc);
    doc_end(pw_doc);
    pw_edit_step = pw_doc->undo; pw_edit_entry = key;
  }
}
static void edit_done(void) { pw_edit_step = NULL; pw_edit_entry = -1; }
static void slider_rgb(int hsv)
{
  int r, g, b;
  if (!pw_doc || pw_playing) return;
  if (hsv) pal_hsv_to_rgb((int)xget(pw_sl[3], MUIA_Numeric_Value), (int)xget(pw_sl[4], MUIA_Numeric_Value), (int)xget(pw_sl[5], MUIA_Numeric_Value), &r, &g, &b);
  else { r = (int)xget(pw_sl[0], MUIA_Numeric_Value); g = (int)xget(pw_sl[1], MUIA_Numeric_Value); b = (int)xget(pw_sl[2], MUIA_Numeric_Value); }
  edit_begin(pw_cur, "Palette Colour");
  pal_put_rgb(pw_doc, pw_cur, r, g, b);
  /* the other set of sliders follows */
  if (hsv) { nnset(pw_sl[0], MUIA_Numeric_Value, r); nnset(pw_sl[1], MUIA_Numeric_Value, g); nnset(pw_sl[2], MUIA_Numeric_Value, b); }
  else { int h, s, v; pal_rgb_to_hsv(r, g, b, &h, &s, &v); nnset(pw_sl[3], MUIA_Numeric_Value, h); nnset(pw_sl[4], MUIA_Numeric_Value, s); nnset(pw_sl[5], MUIA_Numeric_Value, v); }
  sprintf(pw_infotext, "%s %d  #%02X%02X%02X", tr("Colour"), pw_cur, r, g, b);
  set(pw_info, MUIA_Text_Contents, (ULONG)pw_infotext);
  grid_redraw();
  changed();
}
static void choose(int i)
{
  if (!pw_doc || i < 0 || i >= npal()) return;
  if (pw_pending) {
    int op = pw_pending;
    pw_pending = PEND_NONE;
    if (pw_playing) palwin_stop();
    edit_done();
    if (op == PEND_SPREAD) pal_spread(pw_doc, pw_cur, i);
    else if (op == PEND_EX) pal_swap(pw_doc, pw_cur, i, 0);
    else if (op == PEND_COPY) pal_copy(pw_doc, pw_cur, i);
    pw_cur = i;
    palwin_refresh();
    changed();
    return;
  }
  if (i != pw_cur) edit_done();
  pw_cur = i;
  pw_rlo = pw_rhi = -1;
  show_entry();
  grid_redraw();
  if (palwin_picked) palwin_picked(i);
}
static void toggle_lock(int i)
{
  if (!pw_doc || i < 0 || i >= 256) return;
  edit_done();
  doc_begin(pw_doc, pw_doc->pal_lock[i] ? "Unlock Colour" : "Lock Colour");
  doc_note_pal(pw_doc);
  pw_doc->pal_lock[i] = (u8)!pw_doc->pal_lock[i];
  doc_end(pw_doc);
  show_entry();
  grid_redraw();
}
static void cycle_edit(void)
{
  CycleRange *c;
  int lo, hi;
  if (!pw_doc || pw_curcyc < 0 || pw_curcyc >= pw_doc->ncycle) return;
  edit_begin(-2 - pw_curcyc, "Cycle Range");
  c = &pw_doc->cycle[pw_curcyc];
  lo = (int)xget(pw_lo, MUIA_Numeric_Value); hi = (int)xget(pw_hi, MUIA_Numeric_Value);
  if (hi < lo) { int t = lo; lo = hi; hi = t; }
  c->lo = (u8)lo; c->hi = (u8)hi;
  c->rate = (u16)((xget(pw_rate, MUIA_Numeric_Value) * 16384UL + 30) / 60);
  c->reverse = (u8)(xget(pw_dir, MUIA_Cycle_Active) != 0);
  c->active = (u8)(xget(pw_active, MUIA_Selected) != 0);
  show_cycles();
  grid_redraw();
}
static void cycle_add(void)
{
  CycleRange *c;
  if (!pw_doc || pw_doc->ncycle >= MAXCYCLES) return;
  edit_done();
  doc_begin(pw_doc, "New Cycle Range");
  doc_note_pal(pw_doc);
  c = &pw_doc->cycle[pw_doc->ncycle];
  memset(c, 0, sizeof *c);
  if (pw_rlo >= 0 && pw_rhi > pw_rlo) { c->lo = (u8)pw_rlo; c->hi = (u8)pw_rhi; }
  else { c->lo = (u8)pw_cur; c->hi = (u8)(pw_cur + 7 < npal() ? pw_cur + 7 : npal() - 1); }
  c->rate = 16384 / 6; c->active = 1;                       /* ten steps a second */
  pw_curcyc = pw_doc->ncycle++;
  doc_end(pw_doc);
  show_cycles();
  grid_redraw();
}
static void cycle_del(void)
{
  if (!pw_doc || pw_curcyc < 0 || pw_curcyc >= pw_doc->ncycle) return;
  if (pw_playing) palwin_stop();
  edit_done();
  doc_begin(pw_doc, "Delete Cycle Range");
  doc_note_pal(pw_doc);
  memmove(&pw_doc->cycle[pw_curcyc], &pw_doc->cycle[pw_curcyc + 1], sizeof(CycleRange) * (size_t)(pw_doc->ncycle - pw_curcyc - 1));
  pw_doc->ncycle--;
  doc_end(pw_doc);
  if (pw_curcyc >= pw_doc->ncycle) pw_curcyc = pw_doc->ncycle - 1;
  show_cycles();
  grid_redraw();
}

/* ------------------------------------------------------------------ cycling */
static void timer_set(int on)
{
  struct GridData *g;
  if (!pw_grid || !gridcls) return;
  g = (struct GridData *)INST_DATA(gridcls->mcc_Class, pw_grid);
  if (g->timer && (!on || g->ihn.ihn_Millis)) { DoMethod(pw_app, MUIM_Application_RemInputHandler, (ULONG)&g->ihn); g->timer = 0; }
  if (on && pw_doc) {
    u32 ms = cycle_timer_ms(pw_doc);
    if (!ms) return;
    /* a step every 20 ms at most (MUI's timer is no finer in practice) */
    g->ihn.ihn_Millis = (UWORD)(ms < 20 ? 20 : ms > 2000 ? 2000 : ms);
    DoMethod(pw_app, MUIM_Application_AddInputHandler, (ULONG)&g->ihn);
    g->timer = 1;
  }
}
static void play(void)
{
  if (!pw_doc || pw_playing || !cycle_timer_ms(pw_doc)) return;
  edit_done();
  memcpy(pw_saved, pw_doc->pal, sizeof pw_saved);
  memset(pw_acc, 0, sizeof pw_acc);
  pw_playing = 1;
  timer_set(1);
  if (pw_play) set(pw_play, MUIA_Disabled, TRUE);
  if (pw_stop) set(pw_stop, MUIA_Disabled, FALSE);
}
void palwin_stop(void)
{
  if (!pw_playing) return;
  pw_playing = 0;
  timer_set(0);
  if (pw_doc) {
    /* the palette as it was before cycling (cycling isn't an edit) */
    memcpy(pw_doc->pal, pw_saved, sizeof pw_saved);
    doc_pal_changed(pw_doc, 0, 255);
  }
  if (pw_play) set(pw_play, MUIA_Disabled, FALSE);
  if (pw_stop) set(pw_stop, MUIA_Disabled, TRUE);
  show_entry();
  grid_redraw();
  changed();
}
int palwin_cycling(void) { return pw_playing; }
static void tick(void)
{
  struct GridData *g;
  if (!pw_playing || !pw_doc) return;
  g = (struct GridData *)INST_DATA(gridcls->mcc_Class, pw_grid);
  if (cycle_advance(pw_doc, pw_acc, g->ihn.ihn_Millis)) { grid_redraw(); changed(); }
}

/* ------------------------------------------------------------------ palette files */
static char pw_path[400];
static int ask_file(int save, const char *title)
{
  struct FileRequester *fr;
  char dir[400], file[108];
  int ok = 0;
  strncpy(dir, pw_path, sizeof dir - 1); dir[sizeof dir - 1] = 0;
  *PathPart((STRPTR)dir) = 0;
  strncpy(file, pw_path[0] ? (const char *)FilePart((STRPTR)pw_path) : "", sizeof file - 1); file[sizeof file - 1] = 0;
  if (!(fr = (struct FileRequester *)MUI_AllocAslRequestTags(ASL_FileRequest, ASLFR_TitleText, (ULONG)tr(title), ASLFR_DoSaveMode, save,
        ASLFR_InitialDrawer, (ULONG)dir, ASLFR_InitialFile, (ULONG)file, TAG_DONE))) return 0;
  if (MUI_AslRequestTags(fr, ASLFR_Window, (ULONG)xget(pw_win, MUIA_Window_Window), TAG_DONE)) {
    strncpy(pw_path, (const char *)fr->fr_Drawer, sizeof pw_path - 1); pw_path[sizeof pw_path - 1] = 0;
    ok = AddPart((STRPTR)pw_path, fr->fr_File, sizeof pw_path) && fr->fr_File[0];
  }
  MUI_FreeAslRequest(fr);
  return ok;
}
static void message(const char *text)
{
  MUI_Request(pw_app, pw_win, 0, (STRPTR)"Easel", (STRPTR)tr("OK"), (STRPTR)"%s", (ULONG)text);
}
static void load_palette(void)
{
  PalFile *p;
  u8 *data;
  size_t len;
  char err[128];
  if (!pw_doc || !ask_file(0, "Load Palette")) return;
  if (!(data = file_read(pw_path, &len))) { message(tr("The file can't be read.")); return; }
  if (!(p = (PalFile *)malloc(sizeof *p))) { free(data); message(tr("There isn't enough memory for that.")); return; }
  if (!palfile_read(data, len, pw_path, p, err, sizeof err)) message(tr(err));
  else {
    if (pw_playing) palwin_stop();
    edit_done();
    doc_begin(pw_doc, "Load Palette");
    pal_replace(pw_doc, p->rgb, p->n);
    if (p->ncycle) {                                       /* an IFF palette's ranges come with it */
      doc_note_pal(pw_doc);
      memcpy(pw_doc->cycle, p->cycle, sizeof(CycleRange) * (size_t)p->ncycle);
      pw_doc->ncycle = p->ncycle;
    }
    doc_end(pw_doc);
    palwin_refresh();
    changed();
  }
  free(p); free(data);
}
static void save_palette(void)
{
  PalFile *p;
  u8 *data;
  size_t len;
  int fmt = (int)xget(pw_fmt, MUIA_Cycle_Active);
  char *dot;
  if (!pw_doc) return;
  if (fmt < 0 || fmt >= PF_N) fmt = PF_ACT;
  /* the file name gets the format's extension */
  if ((dot = strrchr((char *)FilePart((STRPTR)pw_path), '.')) != NULL) *dot = 0;
  if (pw_path[0] && strlen(pw_path) + 6 < sizeof pw_path) { strcat(pw_path, "."); strcat(pw_path, palfile_ext[fmt]); }
  if (!ask_file(1, "Save Palette")) return;
  if (!(p = (PalFile *)malloc(sizeof *p))) { message(tr("There isn't enough memory for that.")); return; }
  palfile_from_doc(pw_doc, p);
  if (!(data = palfile_write(fmt, p, &len))) message(tr("There isn't enough memory for that."));
  else if (!file_write(pw_path, data, len)) message(tr("The file can't be written."));
  free(data); free(p);
}

/* ------------------------------------------------------------------ the commands */
static void command(ULONG cmd)
{
  if (!pw_doc && cmd != PC_CLOSE) return;
  switch (cmd) {
  case PC_RGB: slider_rgb(0); break;
  case PC_HSV: slider_rgb(1); break;
  case PC_SPREAD: pw_pending = PEND_SPREAD; show_entry(); break;
  case PC_EX: pw_pending = PEND_EX; show_entry(); break;
  case PC_COPY: pw_pending = PEND_COPY; show_entry(); break;
  case PC_UNDO:
    if (pw_playing) palwin_stop();
    edit_done();
    if (doc_undo(pw_doc)) { palwin_refresh(); changed(); }
    break;
  case PC_LOCK:
    if ((int)xget(pw_lock, MUIA_Selected) != pw_doc->pal_lock[pw_cur]) toggle_lock(pw_cur);
    break;
  case PC_EHB:
    if (pw_playing) palwin_stop();
    edit_done();
    pal_make_ehb(pw_doc);
    palwin_refresh(); changed();
    break;
  case PC_LOAD: load_palette(); break;
  case PC_SAVE: save_palette(); break;
  case PC_CYCSEL: {
    LONG a = (LONG)xget(pw_list, MUIA_List_Active);
    if (a >= 0 && a < pw_doc->ncycle) { pw_curcyc = (int)a; show_cycles(); grid_redraw(); }
    break;
  }
  case PC_CYCEDIT: cycle_edit(); if (pw_playing) timer_set(1); break;
  case PC_CYCADD: cycle_add(); break;
  case PC_CYCDEL: cycle_del(); break;
  case PC_CYCRANGE:
    if (pw_curcyc >= 0 && pw_rlo >= 0 && pw_rhi > pw_rlo) {
      nnset(pw_lo, MUIA_Numeric_Value, pw_rlo); nnset(pw_hi, MUIA_Numeric_Value, pw_rhi);
      cycle_edit();
    }
    break;
  case PC_PLAY: play(); break;
  case PC_STOP: palwin_stop(); break;
  case PC_CLOSE: palwin_open(0); break;
  }
}

/* ------------------------------------------------------------------ the grid's class */
static ULONG grid_dispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct GridData *g;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    g = INST_DATA(cl, obj);
    memset(g, 0, sizeof *g);
    g->ehn.ehn_Object = obj; g->ehn.ehn_Class = cl; g->ehn.ehn_Events = IDCMP_MOUSEBUTTONS;
    g->ihn.ihn_Object = obj; g->ihn.ihn_Flags = MUIIHNF_TIMER; g->ihn.ihn_Method = MUIM_PalWin_Tick; g->ihn.ihn_Millis = 100;
    return (ULONG)obj;
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += GCOLS * 9; m->MinMaxInfo->DefWidth += GCOLS * 15; m->MinMaxInfo->MaxWidth += GCOLS * 40;
    m->MinMaxInfo->MinHeight += GROWS * 7; m->MinMaxInfo->DefHeight += GROWS * 12; m->MinMaxInfo->MaxHeight += GROWS * 40;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct GridData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup:
    g = INST_DATA(cl, obj);
    if (pw_playing) palwin_stop();                         /* (the window closing stops cycling) */
    if (g->timer) { DoMethod(_app(obj), MUIM_Application_RemInputHandler, (ULONG)&g->ihn); g->timer = 0; }
    theme_cleanup(obj);
    break;
  case MUIM_Show: ((struct GridData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct GridData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) grid_draw(obj);
    return 0;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    int i;
    g = INST_DATA(cl, obj);
    if (!im || !g->shown || im->Class != IDCMP_MOUSEBUTTONS || im->Code != SELECTDOWN) return 0;
    if ((i = grid_hit(obj, im->MouseX, im->MouseY)) < 0) return 0;
    if (im->Qualifier & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) toggle_lock(i);
    else if ((im->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) && pw_doc && i < npal() && !pw_pending) {
      pw_rlo = pw_cur < i ? pw_cur : i; pw_rhi = pw_cur < i ? i : pw_cur;
      sprintf(pw_infotext, "%s %d-%d", tr("Range"), pw_rlo, pw_rhi);
      set(pw_info, MUIA_Text_Contents, (ULONG)pw_infotext);
      grid_redraw();
    } else choose(i);
    return MUI_EventHandlerRC_Eat;
  }
  case MUIM_PalWin_Cmd: command(((struct MUIP_PalWin_Cmd *)msg)->cmd); return 0;
  case MUIM_PalWin_Tick: tick(); return 0;
  }
  return DoSuperMethodA(cl, obj, msg);
}

/* ------------------------------------------------------------------ building the window */
static Object *slider(LONG min, LONG max)
{
  return SliderObject, MUIA_Numeric_Min, min, MUIA_Numeric_Max, max, MUIA_Numeric_Value, 0, MUIA_CycleChain, 1, End;
}
/* a check box and its label; *box gets the check box */
static Object *toggle(const char *label, Object **box)
{
  return HGroup,
    Child, *box = ImageObject, ImageButtonFrame, MUIA_InputMode, MUIV_InputMode_Toggle, MUIA_Image_Spec, MUII_CheckMark,
      MUIA_Background, MUII_ButtonBack, MUIA_ShowSelState, FALSE, MUIA_CycleChain, 1, End,
    Child, QLabel(tr(label), 0), End;
}
static void notify_cmd(Object *o, ULONG attr, ULONG val, ULONG cmd)
{
  if (o) DoMethod(o, MUIM_Notify, attr, val, (ULONG)pw_grid, 2, MUIM_PalWin_Cmd, cmd);
}
Object *palwin_create(Object *app)
{
  static const char *dirs[3];
  static const char *fmts[PF_N + 1];
  Object *bspread, *bex, *bcopy, *bundo, *bload, *bsave, *bnew, *bdel, *brange, *behb;
  int i;
  if (pw_win) return pw_win;
  pw_app = app;
  dirs[0] = tr("Forward"); dirs[1] = tr("Reverse"); dirs[2] = NULL;
  for (i = 0; i < PF_N; i++) fmts[i] = palfile_names[i];
  fmts[PF_N] = NULL;
  if (!gridcls && !(gridcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct GridData), (APTR)grid_dispatch))) return NULL;
  pw_grid = NewObject(gridcls->mcc_Class, NULL, MUIA_FillArea, FALSE, TAG_DONE);
  if (!pw_grid) return NULL;
  pw_win = QWindowObject, MUIA_Window_Title, (ULONG)tr("Palette"), MUIA_Window_ID, MAKE_ID('P', 'A', 'L', 'W'),
    WindowContents, VGroup, QDLG,
      Child, HGroup,
        Child, pw_grid,
        Child, VGroup, MUIA_Weight, 60,
          Child, pw_info = TextObject, MUIA_Text_Contents, (ULONG)"", MUIA_Text_SetMin, FALSE, End,
          Child, ColGroup(2),
            Child, QLabel("R", MUIO_Label_Centered), Child, pw_sl[0] = slider(0, 255),
            Child, QLabel("G", MUIO_Label_Centered), Child, pw_sl[1] = slider(0, 255),
            Child, QLabel("B", MUIO_Label_Centered), Child, pw_sl[2] = slider(0, 255),
            Child, QLabel("H", MUIO_Label_Centered), Child, pw_sl[3] = slider(0, 359),
            Child, QLabel("S", MUIO_Label_Centered), Child, pw_sl[4] = slider(0, 255),
            Child, QLabel("V", MUIO_Label_Centered), Child, pw_sl[5] = slider(0, 255),
          End,
          Child, ColGroup(3),
            Child, bspread = QButton(tr("_Spread")), Child, bex = QButton(tr("_Ex")), Child, bcopy = QButton(tr("_Copy")),
            Child, bundo = QButton(tr("_Undo")), Child, behb = QButton(tr("Half-_Brite")), Child, toggle("Lock", &pw_lock),
          End,
          Child, HGroup,
            Child, bload = QButton(tr("_Load...")), Child, bsave = QButton(tr("Sa_ve...")),
          End,
          Child, pw_fmt = CycleObject, MUIA_Cycle_Entries, (ULONG)fmts, MUIA_Cycle_Active, PF_ACT, MUIA_CycleChain, 1, End,
        End,
      End,
      Child, QTitleObject(tr("Colour Cycling")),
      Child, HGroup,
        Child, ListviewObject, MUIA_Weight, 80, MUIA_CycleChain, 1,
          MUIA_Listview_List, pw_list = ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
            MUIA_List_DestructHook, MUIV_List_DestructHook_String, MUIA_List_AdjustHeight, FALSE, End,
        End,
        Child, VGroup,
          Child, ColGroup(2),
            Child, QLabel(tr("Low:"), MUIO_Label_Centered), Child, pw_lo = slider(0, 255),
            Child, QLabel(tr("High:"), MUIO_Label_Centered), Child, pw_hi = slider(0, 255),
            Child, QLabel(tr("Steps/s:"), MUIO_Label_Centered), Child, pw_rate = slider(0, 60),
            Child, QLabel(tr("Direction:"), MUIO_Label_Centered), Child, pw_dir = CycleObject, MUIA_Cycle_Entries, (ULONG)dirs, MUIA_CycleChain, 1, End,
          End,
          Child, HGroup, Child, toggle("Active", &pw_active), Child, HSpace(0), Child, brange = QButton(tr("_Marked Range")), End,
          Child, HGroup,
            Child, bnew = QButton(tr("_New")), Child, bdel = QButton(tr("_Delete")),
            Child, pw_play = QButton(tr("_Play")), Child, pw_stop = QButton(tr("S_top")),
          End,
        End,
      End,
    End,
  End;
  if (!pw_win) { pw_grid = NULL; return NULL; }
  set(pw_stop, MUIA_Disabled, TRUE);
  for (i = 0; i < 6; i++) notify_cmd(pw_sl[i], MUIA_Numeric_Value, MUIV_EveryTime, i < 3 ? PC_RGB : PC_HSV);
  notify_cmd(bspread, MUIA_Pressed, FALSE, PC_SPREAD);
  notify_cmd(bex, MUIA_Pressed, FALSE, PC_EX);
  notify_cmd(bcopy, MUIA_Pressed, FALSE, PC_COPY);
  notify_cmd(bundo, MUIA_Pressed, FALSE, PC_UNDO);
  notify_cmd(behb, MUIA_Pressed, FALSE, PC_EHB);
  notify_cmd(pw_lock, MUIA_Selected, MUIV_EveryTime, PC_LOCK);
  notify_cmd(bload, MUIA_Pressed, FALSE, PC_LOAD);
  notify_cmd(bsave, MUIA_Pressed, FALSE, PC_SAVE);
  notify_cmd(pw_list, MUIA_List_Active, MUIV_EveryTime, PC_CYCSEL);
  notify_cmd(pw_lo, MUIA_Numeric_Value, MUIV_EveryTime, PC_CYCEDIT);
  notify_cmd(pw_hi, MUIA_Numeric_Value, MUIV_EveryTime, PC_CYCEDIT);
  notify_cmd(pw_rate, MUIA_Numeric_Value, MUIV_EveryTime, PC_CYCEDIT);
  notify_cmd(pw_dir, MUIA_Cycle_Active, MUIV_EveryTime, PC_CYCEDIT);
  notify_cmd(pw_active, MUIA_Selected, MUIV_EveryTime, PC_CYCEDIT);
  notify_cmd(brange, MUIA_Pressed, FALSE, PC_CYCRANGE);
  notify_cmd(bnew, MUIA_Pressed, FALSE, PC_CYCADD);
  notify_cmd(bdel, MUIA_Pressed, FALSE, PC_CYCDEL);
  notify_cmd(pw_play, MUIA_Pressed, FALSE, PC_PLAY);
  notify_cmd(pw_stop, MUIA_Pressed, FALSE, PC_STOP);
  notify_cmd(pw_win, MUIA_Window_CloseRequest, TRUE, PC_CLOSE);
  DoMethod(app, OM_ADDMEMBER, (ULONG)pw_win);
  palwin_refresh();
  return pw_win;
}
void palwin_set_doc(Doc *d)
{
  if (d == pw_doc) { palwin_refresh(); return; }
  palwin_stop();
  edit_done();
  pw_doc = d;
  pw_pending = PEND_NONE; pw_rlo = pw_rhi = -1; pw_curcyc = d && d->ncycle ? 0 : -1;
  if (d && pw_cur >= npal()) pw_cur = 0;
  palwin_refresh();
}
void palwin_open(int open)
{
  if (!pw_win) return;
  if (!open) palwin_stop();
  set(pw_win, MUIA_Window_Open, open ? TRUE : FALSE);
  if (open) palwin_refresh();
}
int palwin_is_open(void) { return pw_win && xget(pw_win, MUIA_Window_Open); }
int palwin_current(void) { return pw_cur; }
void palwin_dispose(void)
{
  if (!pw_win) return;
  palwin_stop();
  set(pw_win, MUIA_Window_Open, FALSE);
  DoMethod(pw_app, OM_REMMEMBER, (ULONG)pw_win);
  MUI_DisposeObject(pw_win);
  pw_win = pw_grid = NULL;
  if (gridcls) { MUI_DeleteCustomClass(gridcls); gridcls = NULL; }
}
