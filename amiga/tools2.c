/*
 * tools2.c - Easel 1.2's tools, layers and brushes work on the Amiga side: the menu commands
 * (Content-Aware Fill, vector masks, smart objects, links, artboards and frames, pattern fills and
 * Pattern Overlay, Photoshop pattern, gradient and brush files, slices and the programmer's exports,
 * Rotate View and Pixel Aspect Ratio, notes, counts and slices shown, animation frames and onion
 * skins, the spare page, History Options and the History Log), the Mixer Brush's paint, and pen
 * tablets read through an input handler.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <exec/interrupts.h>
#include <exec/io.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "lang.h"
#include "ids.h"
#include "tools2.h"
#include "tools2ids.h"
#include "dlgx.h"
#include "brushwin.h"
#include "gradwin.h"
#include "../core/layers.h"
#include "../core/layerx.h"
#include "../core/cafill.h"
#include "../core/legacy.h"
#include "../core/path.h"
#include "../core/codec.h"
#include "../core/preset.h"
#include "../core/fx.h"

MixerState tool_mixer;
int tablet_curve_pc = 100, tablet_floor_pc = 0, tablet_events;
int onion_on, onion_before = 1, onion_after = 1, onion_opacity = 40, onion_frame;

static ULONG xget(Object *o, ULONG a) { ULONG v = 0; get(o, a, &v); return v; }
static void menu_tick(ULONG id, int on)
{
  Object *strip = win ? (Object *)xget(win, MUIA_Window_Menustrip) : NULL, *it;
  if (strip && (it = (Object *)DoMethod(strip, MUIM_FindUData, id))) set(it, MUIA_Menuitem_Checked, on ? TRUE : FALSE);
}
static int menu_ticked(ULONG id)
{
  Object *strip = win ? (Object *)xget(win, MUIA_Window_Menustrip) : NULL, *it;
  return strip && (it = (Object *)DoMethod(strip, MUIM_FindUData, id)) && xget(it, MUIA_Menuitem_Checked);
}

/* ------------------------------------------------------------------ tablets */
/* the last tablet event (written by the input handler, read by the canvas) */
static volatile LONG tab_press, tab_tx, tab_ty;      /* pressure 0-65535, tilt -32767..32767 */
static volatile ULONG tab_stamp;                      /* the event's ie_TimeStamp seconds, 0 none */
static struct MsgPort *iport;
static struct IOStdReq *ireq;
static struct Interrupt ihandler;
static int iopen;
static struct InputEvent *tablet_handler(__reg("a0") struct InputEvent *ev, __reg("a1") APTR data)
{
  struct InputEvent *e;
  (void)data;
  for (e = ev; e; e = e->ie_NextEvent)
    if (e->ie_Class == IECLASS_NEWPOINTERPOS && e->ie_SubClass == IESUBCLASS_NEWTABLET && e->ie_EventAddress) {
      struct IENewTablet *t = (struct IENewTablet *)e->ie_EventAddress;
      TabletState st;
      if (tablet_parse((const unsigned long *)t->ient_TagList, &st)) {
        tab_press = (LONG)(st.pressure * 65535); tab_tx = (LONG)(st.tilt_x * 32767); tab_ty = (LONG)(st.tilt_y * 32767);
        tab_stamp = e->ie_TimeStamp.tv_secs | 1;
        tablet_events++;
      }
    }
  return ev;
}
int tablet_open(void)
{
  if (iopen) return iopen > 0;
  iopen = -1;
  if (!(iport = CreateMsgPort())) return 0;
  if (!(ireq = (struct IOStdReq *)CreateIORequest(iport, sizeof *ireq))) { DeleteMsgPort(iport); iport = NULL; return 0; }
  if (OpenDevice((STRPTR)"input.device", 0, (struct IORequest *)ireq, 0)) { DeleteIORequest((struct IORequest *)ireq); DeleteMsgPort(iport); ireq = NULL; iport = NULL; return 0; }
  memset(&ihandler, 0, sizeof ihandler);
  ihandler.is_Code = (void (*)(void))tablet_handler;
  ihandler.is_Data = NULL;
  ihandler.is_Node.ln_Pri = 51;                            /* (before Intuition, at 50) */
  ihandler.is_Node.ln_Name = (char *)"Easel tablet";
  ireq->io_Command = IND_ADDHANDLER; ireq->io_Data = (APTR)&ihandler;
  DoIO((struct IORequest *)ireq);
  iopen = 1;
  return 1;
}
void tablet_close(void)
{
  if (iopen > 0) {
    ireq->io_Command = IND_REMHANDLER; ireq->io_Data = (APTR)&ihandler;
    DoIO((struct IORequest *)ireq);
    CloseDevice((struct IORequest *)ireq);
    DeleteIORequest((struct IORequest *)ireq);
    DeleteMsgPort(iport);
  }
  iopen = 0;
}
double tablet_pressure(double *tilt_x, double *tilt_y)
{
  struct DateStamp ds;
  ULONG now;
  if (tilt_x) *tilt_x = 0;
  if (tilt_y) *tilt_y = 0;
  if (iopen <= 0 && !tablet_open()) return 1;
  if (!tab_stamp) return 1;
  DateStamp(&ds);
  /* (ie_TimeStamp counts from the system's start; a tablet event in the last two seconds or so is
     taken as the pen in use: DateStamp's seconds are compared loosely, as only their change matters) */
  now = (ULONG)ds.ds_Days * 86400UL + (ULONG)ds.ds_Minute * 60UL + (ULONG)ds.ds_Tick / 50;
  {
    static ULONG last_stamp, last_now;
    if (tab_stamp != last_stamp) { last_stamp = tab_stamp; last_now = now; }
    else if (now - last_now > 2) return 1;
  }
  if (tilt_x) *tilt_x = tab_tx / 32767.0;
  if (tilt_y) *tilt_y = tab_ty / 32767.0;
  return tablet_curve(tab_press / 65535.0, tablet_curve_pc, tablet_floor_pc);
}

/* ------------------------------------------------------------------ the History Log */
static void now_text(char *out, size_t cap)
{
  time_t t = time(NULL);
  struct tm *tm = localtime(&t);
  if (!tm || !strftime(out, cap, "%Y-%m-%d %H:%M:%S", tm)) snprintf(out, cap, "%s", "");
}
void t2_log_event(Doc *d, const char *event, const char *name)
{
  char when[32];
  if (!hist_opts.log_on) return;
  now_text(when, sizeof when);
  histlog_event(d, when, tr(event), name);
}
void t2_log_steps(void)
{
  static void *last_undo;
  static Doc *last_doc;
  DocView *v = cur_view();
  Doc *d = v ? v->doc : NULL;
  char when[32];
  if (!d) return;
  if (d != last_doc) { last_doc = d; last_undo = d->undo; return; }
  if ((void *)d->undo == last_undo) return;
  last_undo = d->undo;
  if (!hist_opts.log_on || !d->undo || !doc_can_undo(d)) return;
  now_text(when, sizeof when);
  histlog_step(d, when, v->name, doc_undo_name(d), NULL);
}

/* ------------------------------------------------------------------ small helpers */
static int need_doc(Doc *d) { return d != NULL; }
static int need_layer(Doc *d) { return d && d->active >= 0 && d->active < d->nlay; }
static void oom(void) { ask("OK", "%s", tr("There isn't enough memory for that.")); }
static void scratch_path(char *out, size_t cap, const char *name)
{
  DocView *v = cur_view();
  if (v && v->path[0]) { strncpy(out, v->path, cap - 1); out[cap - 1] = 0; *PathPart((STRPTR)out) = 0; AddPart((STRPTR)out, (STRPTR)name, cap); }
  else snprintf(out, cap, "%s", name);
}
static int save_text(const char *title, const char *defname, const char *text)
{
  char path[512];
  int ok;
  if (!text) { oom(); return 0; }
  scratch_path(path, sizeof path, defname);
  if (!t2_file_request(1, title, path, sizeof path, NULL)) return 0;
  ok = file_write(path, text, strlen(text));
  if (!ok) ask("OK", "%s", tr("The file couldn't be written."));
  return ok;
}
static int save_bytes(const char *title, const char *defname, const u8 *data, size_t n)
{
  char path[512];
  int ok;
  if (!data) { oom(); return 0; }
  scratch_path(path, sizeof path, defname);
  if (!t2_file_request(1, title, path, sizeof path, NULL)) return 0;
  ok = file_write(path, data, n);
  if (!ok) ask("OK", "%s", tr("The file couldn't be written."));
  return ok;
}
/* an indexed picture's indices (the active layer's, else the bottom's) and its depth */
static const u8 *indexed(Doc *d, int *depth)
{
  int i, dp = 1;
  if (d->mode != MODE_INDEXED) { ask("OK", "%s", tr("Make the picture indexed first (Image > Mode > Indexed Color): bitplanes and sprites are made from its colour numbers.")); return NULL; }
  i = d->active >= 0 && d->active < d->nlay && d->lay[d->active]->idx ? d->active : 0;
  if (!d->lay[i]->idx) { ask("OK", "%s", tr("The layer has no colour numbers: flatten the picture first.")); return NULL; }
  while ((1 << dp) < d->npal && dp < 8) dp++;
  *depth = dp;
  return d->lay[i]->idx;
}
static const char *base_of(void)
{
  static char b[40];
  DocView *v = cur_view();
  size_t k = 0;
  const char *s = v ? v->name : "picture";
  for (; *s && k < sizeof b - 1; s++) b[k++] = (char)((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ? *s : '_');
  b[k] = 0;
  if (!k) strcpy(b, "picture");
  if (b[0] >= '0' && b[0] <= '9') b[0] = '_';
  return b;
}

/* ------------------------------------------------------------------ dialogs */
static void ca_apply(Doc *d, const LONG *v, int choice, int tick)
{
  CaOpts o;
  int stopped;
  (void)choice; (void)tick;
  memset(&o, 0, sizeof o);
  o.patch = (int)v[0] | 1; o.rounds = (int)v[1]; o.margin = (int)v[2];
  job_begin(d, "Content-Aware Fill");
  doc_ca_fill(d, &o);
  stopped = job_end();
  (void)stopped;
}
static void po_apply(Doc *d, const LONG *v, int choice, int tick)
{
  static const int modes[] = { BM_NORMAL, BM_MULTIPLY, BM_SCREEN, BM_OVERLAY, BM_SOFTLIGHT };
  (void)tick;
  doc_set_pattern_overlay(d, d->active, 1, tool_pattern(), modes[choice < 0 || choice > 4 ? 0 : choice], (int)(v[0] * 255 / 100), (int)v[1]);
}
static void onion_apply(Doc *d, const LONG *v, int choice, int tick)
{
  (void)d; (void)choice; (void)tick;
  onion_before = (int)v[0]; onion_after = (int)v[1]; onion_opacity = (int)v[2];
}
static void hist_options(void)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *wh[4], *dt[4];
  Object *w, *c1, *c2, *c3, *c4, *cw, *cd, *fs;
  wh[0] = tr("Text File"); wh[1] = tr("Metadata (File Info)"); wh[2] = tr("Both"); wh[3] = NULL;
  dt[0] = tr("Sessions Only"); dt[1] = tr("Concise"); dt[2] = tr("Detailed"); dt[3] = NULL;
  w = dialog_window("History Options", ColGroup(2),
        Child, QLabel(tr("Automatically Create First Snapshot"), MUIO_Label_Centered), Child, c1 = MUI_MakeObject(MUIO_Checkmark, NULL),
        Child, QLabel(tr("Automatically Create New Snapshot When Saving"), MUIO_Label_Centered), Child, c2 = MUI_MakeObject(MUIO_Checkmark, NULL),
        Child, QLabel(tr("Show New Snapshot Dialog by Default"), MUIO_Label_Centered), Child, c3 = MUI_MakeObject(MUIO_Checkmark, NULL),
        Child, QLabel(tr("History Log"), MUIO_Label_Centered), Child, c4 = MUI_MakeObject(MUIO_Checkmark, NULL),
        Child, QLabel(tr("Save Log Items To:"), MUIO_Label_Centered), Child, cw = CycleObject, MUIA_Cycle_Entries, (ULONG)wh, MUIA_Cycle_Active, hist_opts.log_where, End,
        Child, QLabel(tr("Edit Log Items:"), MUIO_Label_Centered), Child, cd = CycleObject, MUIA_Cycle_Entries, (ULONG)dt, MUIA_Cycle_Active, hist_opts.log_detail, End,
        Child, QLabel(tr("Log File:"), MUIO_Label_SingleFrame), Child, fs = StringObject, StringFrame, MUIA_String_Contents, (ULONG)hist_opts.log_file, MUIA_String_MaxLen, 250, End,
      End, NULL);
  if (!w) return;
  set(c1, MUIA_Selected, hist_opts.auto_snapshot); set(c2, MUIA_Selected, hist_opts.snapshot_on_save);
  set(c3, MUIA_Selected, hist_opts.ask_snapshot_name); set(c4, MUIA_Selected, hist_opts.log_on);
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    hist_opts.auto_snapshot = xget(c1, MUIA_Selected) != 0; hist_opts.snapshot_on_save = xget(c2, MUIA_Selected) != 0;
    hist_opts.ask_snapshot_name = xget(c3, MUIA_Selected) != 0; hist_opts.log_on = xget(c4, MUIA_Selected) != 0;
    hist_opts.log_where = (int)xget(cw, MUIA_Cycle_Active); hist_opts.log_detail = (int)xget(cd, MUIA_Cycle_Active);
    strncpy(hist_opts.log_file, (const char *)xget(fs, MUIA_String_Contents), sizeof hist_opts.log_file - 1);
  }
  MUI_DisposeObject(w);
}

/* ------------------------------------------------------------------ frames */
static int frame_count(Doc *d) { int f[MAXLAYERS]; return frames_list(d, f, MAXLAYERS); }
static void frame_go(Doc *d, int k)
{
  int n = frame_count(d);
  if (n < 1) return;
  onion_frame = ((k % n) + n) % n;
  frames_show(d, onion_frame);
  {
    int f[MAXLAYERS];
    frames_list(d, f, MAXLAYERS);
    d->active = f[onion_frame];
  }
}
static int current_frame(Doc *d)
{
  int f[MAXLAYERS], n = frames_list(d, f, MAXLAYERS), k;
  for (k = 0; k < n; k++) if (d->lay[f[k]]->visible && (f[k] == d->active || k == onion_frame)) return k;
  for (k = 0; k < n; k++) if (f[k] >= d->active) return k;
  return 0;
}
static void frames_play(Doc *d)
{
  int f[MAXLAYERS], n = frames_list(d, f, MAXLAYERS), k, round;
  u8 vis[MAXLAYERS];
  if (n < 2) { ask("OK", "%s", tr("An animation's frames are the picture's layers: there must be two or more.")); return; }
  for (k = 0; k < d->nlay; k++) vis[k] = d->lay[k]->visible;
  job_begin(NULL, "Play Frames");
  for (round = 0; round < 50; round++)
    for (k = 0; k < n; k++) {
      int cs = frame_time(d->lay[f[k]]->name);
      frames_show(d, k);
      DoMethod(canvas, MUIM_Canvas_Refresh);
      Delay(cs > 0 ? cs / 2 : 5);                          /* (ticks of a 50th of a second) */
      if (progress((long)round * n + k, 50L * n)) { round = 50; break; }
    }
  job_end();
  for (k = 0; k < d->nlay; k++) d->lay[k]->visible = vis[k];
}

/* ------------------------------------------------------------------ the commands */
int tools2_command(ULONG id)
{
  DocView *v = cur_view();
  Doc *d = cur_doc();
  int i = d ? d->active : -1, k;
  char path[512], err[120];
  switch (id) {
  /* ---- Edit */
  case ID_CAFILL: {
    static const DlgParam ps[3] = { { "Patch Size (px)", 5, 15, 7 }, { "Rounds at Each Level", 1, 10, 3 }, { "Sampling Margin (px, 0: automatic)", 0, 2000, 0 } };
    if (!need_layer(d)) return 0;
    if (!d->sel) { ask("OK", "%s", tr("Select what should be filled first: Content-Aware Fill fills the selection from the picture round it.")); return 0; }
    dlg_sliders("Content-Aware Fill", ps, 3, NULL, NULL, 0, NULL, NULL, 0, NULL, 0, NULL, ca_apply, NULL, 0);
    return 1; }
  case ID_LOADPAT: case ID_LOADGRD: {
    size_t n;
    u8 *data;
    path[0] = 0;
    if (!t2_file_request(0, id == ID_LOADPAT ? "Load Photoshop Patterns" : "Load Photoshop Gradients", path, sizeof path, id == ID_LOADPAT ? "#?.pat" : "#?.grd")) return 0;
    if (!(data = file_read(path, &n))) { ask("OK", "%s", tr("The file couldn't be read.")); return 0; }
    if (id == ID_LOADPAT) k = pat_read(&patterns, data, n, err, sizeof err);
    else {
      Gradient *g = (Gradient *)malloc(64 * sizeof(Gradient));
      GradientList *gl = gradwin_presets();
      int j;
      k = g ? grd_read(data, n, g, 64, err, sizeof err) : 0;
      for (j = 0; j < k && gl; j++) gradlist_add(gl, &g[j]);
      free(g);
    }
    free(data);
    if (!k) ask("OK", "%s", tr(err[0] ? err : "Nothing could be read."));
    names_refresh();
    DoMethod(optbar, MUIM_Opts_Refresh);
    return 1; }
  case ID_SAVEABR: {
    Brush *bs;
    const char **names;
    size_t n;
    u8 *abr;
    if (!brushui.presets.n) return 0;
    bs = (Brush *)malloc((size_t)brushui.presets.n * sizeof(Brush)); names = (const char **)malloc((size_t)brushui.presets.n * sizeof(char *));
    if (!bs || !names) { free(bs); free(names); oom(); return 0; }
    for (k = 0; k < brushui.presets.n; k++) { bs[k] = brushui.presets.p[k].b; bs[k].tip = brushui.presets.p[k].tip; names[k] = brushui.presets.p[k].name; }
    abr = abr_write(bs, names, brushui.presets.n, &n);
    free(bs); free(names);
    save_bytes("Save Brushes as .abr", "Easel.abr", abr, n);
    free(abr);
    return 0; }
  case ID_SAVEPAT: {
    size_t n;
    u8 *pat;
    if (!patterns.n) return 0;
    pat = pat_write((const Pattern *const *)patterns.p, patterns.n, &n);
    save_bytes("Save Patterns as .pat", "Easel.pat", pat, n);
    free(pat);
    return 0; }
  case ID_HISTOPTS: hist_options(); return 0;
  case ID_SNAPRENAME:
    if (!v || hist_pick < 0 || hist_pick >= v->nhsnap) { ask("OK", "%s", tr("Click a snapshot in the History panel first.")); return 0; }
    ask_string("Rename Snapshot", "Name:", v->hsnap_name[hist_pick], sizeof v->hsnap_name[0]);
    return 1;
  /* ---- File */
  case ID_OPENSMART: {
    Doc *n;
    path[0] = 0;
    if (!t2_file_request(0, "Open as Smart Object", path, sizeof path, NULL)) return 0;
    if (!(n = smart_open(path, err, sizeof err))) { ask("OK", "%s", tr(err[0] ? err : "The picture could not be read.")); return 0; }
    t2_doc_show(n, NULL);
    return 1; }
  case ID_EXPSLICES: case ID_EXPARTBOARDS: {
    char drawer[512];
    if (!need_doc(d)) return 0;
    if (id == ID_EXPSLICES && (!d->annot || !d->annot->nslice)) { ask("OK", "%s", tr("There are no slices: draw them with the Slice tool first.")); return 0; }
    scratch_path(path, sizeof path, base_of());
    if (!t2_file_request(1, id == ID_EXPSLICES ? "Slices as PNG: the first file's name" : "Artboards as PNG: the drawer", path, sizeof path, NULL)) return 0;
    strncpy(drawer, path, sizeof drawer - 1); drawer[sizeof drawer - 1] = 0;
    *PathPart((STRPTR)drawer) = 0;
    if (id == ID_EXPSLICES) k = slices_export(d, drawer, (const char *)FilePart((STRPTR)path));
    else {
      k = 0;
      for (i = 0; i < d->nlay; i++) if (d->lay[i]->artboard) {
        Box b = d->lay[i]->abox;
        Rgba im;
        size_t n;
        u8 *png;
        char f[512];
        if (!rgba_alloc(&im, b.x1 - b.x0, b.y1 - b.y0)) continue;
        doc_composite(d, b, im.px, (long)im.w * 4);
        png = png_encode(&im, d->dpi, &n);
        rgba_free(&im);
        strncpy(f, drawer, sizeof f - 1); f[sizeof f - 1] = 0;
        { char nm[80]; snprintf(nm, sizeof nm, "%s.png", d->lay[i]->name); AddPart((STRPTR)f, (STRPTR)nm, sizeof f); }
        if (png && file_write(f, png, n)) k++;
        free(png);
      }
    }
    if (!k) ask("OK", "%s", tr("Nothing was written."));
    return 0; }
  case ID_EXPRAW: case ID_EXPCSRC: case ID_EXPASM: case ID_EXPSPRC: case ID_EXPSPRASM: {
    int depth, asm_ = id == ID_EXPASM || id == ID_EXPSPRASM, inter = 0;
    const u8 *idx;
    char name[64];
    if (!need_doc(d) || !(idx = indexed(d, &depth))) return 0;
    if (id == ID_EXPSPRC || id == ID_EXPSPRASM) {
      if (d->npal > 4) { ask("OK", "%s", tr("A sprite has four colours (colour 0 see-through): reduce the picture to four colours first.")); return 0; }
      if (d->h > 255) { ask("OK", "%s", tr("A sprite is at most 255 lines tall.")); return 0; }
      snprintf(name, sizeof name, "%s_sprite", base_of());
      save_text(asm_ ? "Sprites as Assembler" : "Sprites as C", asm_ ? "sprite.s" : "sprite.c", sprites_source(idx, d->w, d->h, asm_, name, 0, 0));
      return 0;
    }
    inter = ask("Interleaved|One Plane After Another|Cancel", "%s", tr("How should the bitplanes be laid out?"));
    if (!inter) return 0;
    if (id == ID_EXPRAW) {
      size_t n;
      int pal = ask("LoadRGB4 (12-bit)|LoadRGB32 (24-bit)|None", "%s", tr("The palette after the bitplanes:"));
      u8 *raw = bitplanes_raw(idx, d->w, d->h, depth, d->pal, d->npal, (inter == 1 ? BP_INTERLEAVED : 0) | (pal == 1 ? BP_PALETTE4 : pal == 2 ? BP_PALETTE32 : 0), &n);
      save_bytes("Raw Bitplanes and Palette", "picture.raw", raw, n);
      free(raw);
      return 0;
    }
    snprintf(name, sizeof name, "%s", base_of());
    save_text(asm_ ? "Bitplanes as Assembler" : "Bitplanes as C", asm_ ? "picture.s" : "picture.c",
              bitplanes_source(idx, d->w, d->h, depth, d->pal, d->npal, inter == 1 ? BP_INTERLEAVED : 0, asm_, name));
    return 0; }
  case ID_EXPCOPC: case ID_EXPCOPASM: {
    /* the Gradient tool's gradient down the picture's height, a colour a line */
    LONG lines = d ? d->h : 256, y0 = 44;
    u32 *cols;
    int aga, j;
    Gradient g;
    if (!need_doc(d)) return 0;
    if (!ask_number("Copper Gradient", "Lines:", &lines, 1, 512) || !ask_number("Copper Gradient", "First line (44 is the top of a normal display):", &y0, 0, 300)) return 0;
    aga = ask("OCS/ECS (12-bit)|AGA (24-bit)|Cancel", "%s", tr("For which chips?"));
    if (!aga) return 0;
    if (!(cols = (u32 *)malloc((size_t)lines * sizeof(u32)))) { oom(); return 0; }
    g = *tool_gradient(); g.fg = d->fg; g.bg = d->bg;
    for (j = 0; j < lines; j++) { u8 c[4]; gradient_sample(&g, lines > 1 ? (double)j / (lines - 1) : 0, c); cols[j] = (u32)c[0] << 16 | (u32)c[1] << 8 | c[2]; }
    save_text(id == ID_EXPCOPASM ? "Copper Gradient as Assembler" : "Copper Gradient as C", id == ID_EXPCOPASM ? "copper.s" : "copper.c",
              copper_gradient_source(cols, (int)lines, (int)y0, 0, aga == 2, id == ID_EXPCOPASM, "copper"));
    free(cols);
    return 0; }
  /* ---- Layer: vector masks */
  case ID_VM_REVEAL: case ID_VM_HIDE: case ID_VM_PATH: {
    struct Path *p = id == ID_VM_PATH && d ? active_path(d) : NULL;
    if (!need_layer(d)) return 0;
    if (id == ID_VM_PATH && (!p || !p->na)) { ask("OK", "%s", tr("Choose a path in the Paths panel (or draw one with the Pen tool) first.")); return 0; }
    if (!doc_vmask_add(d, i, id == ID_VM_REVEAL ? VM_REVEAL_ALL : id == ID_VM_HIDE ? VM_HIDE_ALL : VM_PATH, p)) oom();
    return 1; }
  case ID_VM_DELETE: if (d && !doc_vmask_delete(d, i)) ask("OK", "%s", tr("The layer has no vector mask.")); return 1;
  case ID_VM_ENABLE: case ID_VM_LINK: {
    const LayerX *x = d && need_layer(d) ? layerx_get(d->lay[i]) : NULL;
    if (!x || !x->vm_has) { ask("OK", "%s", tr("The layer has no vector mask.")); return 0; }
    doc_vmask_set(d, i, id == ID_VM_ENABLE ? !x->vm_on : -1, id == ID_VM_LINK ? !x->vm_linked : -1, -1, -1);
    return 1; }
  case ID_VM_RASTER: if (d && !doc_vmask_rasterize(d, i)) ask("OK", "%s", tr("The layer has no vector mask.")); return 1;
  /* ---- Layer: smart objects */
  case ID_SO_CONVERT:
    if (!need_layer(d)) return 0;
    if (layer_is_smart(d->lay[i])) return 0;
    if (d->lay[i]->kind != LK_PIXEL) { ask("OK", "%s", tr("Only a pixel layer becomes a smart object: rasterize it first.")); return 0; }
    job_begin(d, "Convert to Smart Object");
    k = doc_smart_convert(d, i);
    job_end();
    if (!k) ask("OK", "%s", tr("The smart object's source couldn't be written to the scratch drawer (Preferences > Performance)."));
    return 1;
  default: break;
  }
  if (id >= ID_SO_COPY && id <= ID_SO_RASTER) {
    if (!need_layer(d) || !layer_is_smart(d->lay[i])) { ask("OK", "%s", tr("The layer isn't a smart object (Layer > Smart Objects > Convert to Smart Object).")); return 0; }
    k = 1;
    job_begin(d, "Smart Object");
    switch (id) {
    case ID_SO_COPY: k = doc_smart_copy(d, i) >= 0; break;
    case ID_SO_EDIT:
      /* the source opened as a picture of its own; saved, Update Modified Content brings it back */
      job_end();
      strncpy(path, layer_smart_path(d->lay[i]), sizeof path - 1); path[sizeof path - 1] = 0;
      t2_open_path(path);
      return 1;
    case ID_SO_SHOW: job_end(); ask("OK", "The smart object's source is\n%s", layer_smart_path(d->lay[i])); return 0;
    case ID_SO_UPDATE: k = doc_smart_update(d, i); break;
    case ID_SO_UPDATEALL: k = doc_smart_update_all(d) >= 0; break;
    case ID_SO_REPLACE: case ID_SO_RELINK:
      job_end();
      path[0] = 0;
      if (!t2_file_request(0, id == ID_SO_REPLACE ? "Replace Contents" : "Relink to File", path, sizeof path, NULL)) return 0;
      job_begin(d, "Smart Object");
      k = id == ID_SO_REPLACE ? doc_smart_replace(d, i, path) : doc_smart_relink(d, i, path);
      break;
    case ID_SO_EXPORT: case ID_SO_TOLINKED:
      job_end();
      scratch_path(path, sizeof path, "Contents.png");
      if (!t2_file_request(1, id == ID_SO_EXPORT ? "Export Contents" : "Convert to Linked", path, sizeof path, NULL)) return 0;
      job_begin(d, "Smart Object");
      k = id == ID_SO_EXPORT ? doc_smart_export(d, i, path) : doc_smart_link(d, i, path);
      break;
    case ID_SO_TOEMBEDDED: k = doc_smart_embed(d, i); break;
    case ID_SO_TOLAYERS: k = doc_smart_to_layers(d, i); break;
    case ID_SO_RESET: k = doc_smart_reset(d, i); break;
    case ID_SO_RASTER: k = doc_smart_rasterize(d, i); break;
    }
    if (job_end()) return 1;
    if (!k) ask("OK", "%s", tr("That didn't work: the source file couldn't be read or written."));
    return 1;
  }
  switch (id) {
  /* ---- Layer: links, artboards, frames, patterns */
  case ID_LINK: if (d && !doc_link_layers(d)) ask("OK", "%s", tr("Pick two or more layers in the Layers panel (Shift-click) to link them.")); return 1;
  case ID_UNLINK: if (d) doc_unlink_layers(d); return 1;
  case ID_SELLINKED: if (d && !doc_select_linked(d)) ask("OK", "%s", tr("The layer isn't linked to others.")); return 1;
  case ID_NEWARTBOARD: case ID_ARTBOARDGROUP: {
    Box b;
    if (!need_doc(d)) return 0;
    if (!d->sel || !sel_bounds(d, &b)) b = doc_box(d);
    if (id == ID_NEWARTBOARD) { if (doc_new_artboard(d, b, 0xFFFFFF, NULL) < 0) oom(); }
    else if (i < 0 || d->lay[i]->kind != LK_GROUP) ask("OK", "%s", tr("Choose a group in the Layers panel first."));
    else doc_artboard_from_group(d, i, b, 0xFFFFFF);
    return 1; }
  case ID_FRAMEPLACE: {
    int f = -1;
    if (!need_layer(d)) return 0;
    for (k = d->nlay - 1; k >= 0; k--) if (k != i && layer_is_frame(d->lay[k]) && d->lay[k]->picked) { f = k; break; }
    if (f < 0) for (k = d->nlay - 1; k >= 0; k--) if (k != i && layer_is_frame(d->lay[k])) { f = k; break; }
    if (f < 0) { ask("OK", "%s", tr("There is no frame: draw one with the Frame tool first.")); return 0; }
    if (doc_frame_place(d, f, i, 0) < 0) ask("OK", "%s", tr("Place in Frame puts a pixel layer with something on it into the frame."));
    return 1; }
  case ID_NEWFILLPAT: {
    LONG sc = 100;
    if (!need_doc(d)) return 0;
    if (!tool_pattern()) { ask("OK", "%s", tr("Choose a pattern first.")); return 0; }
    if (!ask_number("Pattern Fill", "Scale (%):", &sc, 1, 1000)) return 0;
    if (doc_new_pattern_fill(d, tool_pattern(), (int)sc, NULL) < 0) oom();
    return 1; }
  case ID_FXPATTERN: {
    static const DlgParam ps[2] = { { "Opacity (%)", 0, 100, 100 }, { "Scale (%)", 1, 1000, 100 } };
    static const char *const modes[] = { "Normal", "Multiply", "Screen", "Overlay", "Soft Light", NULL };
    const LayerX *x;
    if (!need_layer(d)) return 0;
    if (d->lay[i]->kind == LK_GROUP || d->lay[i]->kind == LK_ADJUST) { ask("OK", "%s", tr("Pattern Overlay goes on pixel, type, shape and fill layers.")); return 0; }
    x = layerx_get(d->lay[i]);
    if (x && x->po_on && ask("Change|Remove", "%s", tr("The layer has a Pattern Overlay.")) == 0) { doc_set_pattern_overlay(d, i, 0, NULL, -1, -1, -1); return 1; }
    if (!tool_pattern()) { ask("OK", "%s", tr("Choose a pattern first.")); return 0; }
    dlg_sliders("Pattern Overlay", ps, 2, modes, "Mode:", 0, NULL, NULL, 0, NULL, 0, NULL, po_apply, NULL, 0);
    return 1; }
  /* ---- View */
  case ID_PIXASPECT: view_aspect = menu_ticked(ID_PIXASPECT); return 1;
  case ID_DITHER0: case ID_DITHER1: case ID_DITHER2: display_dither = (int)(id - ID_DITHER0); redraw_window(); return 1;
  case ID_ROT_CW: case ID_ROT_CCW: case ID_ROT_180: case ID_ROT_RESET: case ID_ROTRESET:
    if (!v) return 0;
    if (id == ID_ROT_RESET || id == ID_ROTRESET) v->vrot = 0;
    else v->vrot = view_angle_snap(v->vrot + (id == ID_ROT_CW ? 90 : id == ID_ROT_CCW ? -90 : 180), 0);
    return 1;
  case ID_SAFEGUIDES: if (d) guides_safe_areas(d); return 1;
  case ID_SHOWNOTES: case ID_SHOWCOUNT: case ID_SHOWSLICES: {
    Annots *a = d ? doc_annots(d, 1) : NULL;
    if (!a) return 0;
    if (id == ID_SHOWNOTES) a->show_notes = (u8)menu_ticked(id);
    else if (id == ID_SHOWCOUNT) a->show_count = (u8)menu_ticked(id);
    else a->show_slices = (u8)menu_ticked(id);
    return 1; }
  /* ---- Window */
  case ID_FRAMEPREV: case ID_FRAMENEXT:
    if (!need_doc(d)) return 0;
    frame_go(d, current_frame(d) + (id == ID_FRAMENEXT ? 1 : -1));
    return 1;
  case ID_FRAMEPLAY: if (d) frames_play(d); return 1;
  case ID_ONION:
    onion_on = menu_ticked(ID_ONION);
    if (d) onion_frame = current_frame(d);
    return 1;
  case ID_ONIONSET: {
    static const DlgParam ps[3] = { { "Frames Before", 0, 8, 1 }, { "Frames After", 0, 8, 1 }, { "Opacity (%)", 1, 100, 40 } };
    LONG init[3];
    init[0] = onion_before; init[1] = onion_after; init[2] = onion_opacity;
    dlg_sliders("Onion Skin Settings", ps, 3, NULL, NULL, 0, NULL, NULL, 0, NULL, 0, init, onion_apply, NULL, 0);
    return 1; }
  case ID_FRAMETIME: {
    LONG cs;
    char nm[64];
    if (!need_layer(d)) return 0;
    cs = frame_time(d->lay[i]->name);
    if (!ask_number("Frame Time", "Hundredths of a second:", &cs, 0, 100000)) return 0;
    frame_set_time(d->lay[i]->name, (int)cs, nm, sizeof nm);
    doc_set_layer_props(d, i, nm, -1, -1, -1);
    return 1; }
  case ID_SPARE:
    if (t2_spare >= 0 && t2_spare < ndocs && t2_spare != curdoc) t2_doc_switch(t2_spare);
    else ask("OK", "%s", tr("There is no spare page: open or make a second picture (or Copy to Spare)."));
    return 1;
  case ID_COPYSPARE: {
    Doc *c;
    if (!need_doc(d)) return 0;
    if (!(c = t2_state_copy(d))) { oom(); return 0; }
    if (t2_spare >= 0 && t2_spare < ndocs && t2_spare != curdoc) {
      /* the spare's picture replaced by a copy of this one (one step that can be undone there) */
      Doc *sp = docs[t2_spare].doc;
      int j;
      doc_begin(sp, "Copy to Spare");
      doc_note_all(sp);
      for (j = 0; j < sp->nlay; j++) layer_free(sp->lay[j]);
      memcpy(sp->lay, c->lay, sizeof sp->lay); sp->nlay = c->nlay; sp->active = c->active; sp->w = c->w; sp->h = c->h;
      free(sp->sel); sp->sel = c->sel; sp->selbox = c->selbox; c->sel = NULL;
      doc_end(sp);
      c->nlay = 0;
      doc_free(c);
    } else t2_doc_show(c, NULL);
    return 1; }
  /* ---- the options bar */
  case ID_MIXLOAD: if (d) mixer_load(&tool_mixer, d->fg); tool_mixer.color = d ? d->fg : 0; tool_mixer.has_color = d != NULL; return 0;
  case ID_MIXCLEAN: mixer_clean(&tool_mixer); return 0;
  case ID_NOTECLEAR:
    if (d && d->annot && d->annot->nnote && ask("Delete|Cancel", "%s", tr("Delete all the notes?")) == 1) note_clear(d);
    return 1;
  case ID_COUNTCLEAR: if (d && d->annot) count_clear(d, -1); return 1;
  case ID_COUNTGROUP: {
    char nm[32];
    Annots *a = d ? doc_annots(d, 1) : NULL;
    if (!a) return 0;
    snprintf(nm, sizeof nm, "%s %d", tr("Count Group"), a->ngroup + 1);
    if (!ask_string("New Count Group", "Name:", nm, sizeof nm)) return 0;
    if (count_group_add(d, nm, a->ngroup % 2 ? 0x0080FF : 0xFF00FF) < 0) ask("OK", "%s", tr("There can be sixteen count groups."));
    return 1; }
  case ID_COUNTRECORD: if (d) { char *r = count_record(d); if (r) { ask("OK", "%s", r); free(r); } } return 0;
  case ID_SLICEDEL: case ID_SLICEDIVIDE: {
    if (!d || slice_pick < 0 || !d->annot || slice_pick >= d->annot->nslice) { ask("OK", "%s", tr("Click a slice with the Slice Select tool first.")); return 0; }
    if (id == ID_SLICEDEL) { slice_delete(d, slice_pick); slice_pick = -1; }
    else {
      LONG ac = 2, dn = 1;
      if (!ask_number("Divide Slice", "Across:", &ac, 1, 16) || !ask_number("Divide Slice", "Down:", &dn, 1, 16)) return 0;
      if (!slice_divide(d, slice_pick, (int)ac, (int)dn)) ask("OK", "%s", tr("The slice can't be divided like that."));
      slice_pick = -1;
    }
    return 1; }
  case ID_SLICEGUIDES: if (d && !slices_from_guides(d)) ask("OK", "%s", tr("There are no guides to make slices from.")); return 1;
  case ID_SLICECLEAR: if (d && d->annot) d->annot->nslice = 0; return 1;
  case ID_PCROP_GO: DoMethod(canvas, MUIM_Canvas_Commit); return 0;
  }
  return 0;
}
void tools2_menus(void)
{
  Doc *d = cur_doc();
  Annots *a = d ? d->annot : NULL;
  menu_tick(ID_PIXASPECT, view_aspect);
  menu_tick(ID_DITHER0, display_dither == 0); menu_tick(ID_DITHER1, display_dither == 1); menu_tick(ID_DITHER2, display_dither == 2);
  menu_tick(ID_ONION, onion_on);
  menu_tick(ID_SHOWNOTES, !a || a->show_notes);
  menu_tick(ID_SHOWCOUNT, !a || a->show_count);
  menu_tick(ID_SHOWSLICES, !a || a->show_slices);
}
void tools2_free(void)
{
  int i, k;
  char p[300];
  tablet_close();
  /* the smart objects' sources in the scratch drawer: what no open picture uses goes (an embedded
     source is kept inside OpenRaster files when they are saved) */
  for (k = 1; k < 10000; k++) {
    int used = 0;
    BPTR lk;
    char nm[32];
    sprintf(nm, "EaselSmart%04d.png", k);
    strncpy(p, smart_drawer, sizeof p - 1); p[sizeof p - 1] = 0;
    AddPart((STRPTR)p, (STRPTR)nm, sizeof p);
    if (!(lk = Lock((STRPTR)p, ACCESS_READ))) { if (k > 64) break; continue; }
    UnLock(lk);
    for (i = 0; i < ndocs && !used; i++) {
      int j;
      for (j = 0; j < docs[i].doc->nlay; j++) if (layer_is_smart(docs[i].doc->lay[j]) && !strcmp(layer_smart_path(docs[i].doc->lay[j]), p)) used = 1;
    }
    if (!used) DeleteFile((STRPTR)p);
  }
}
/* the canvas's composite: with onion skins, the frames round the one shown (tools2.h) */
void view_composite(Doc *d, Box r, u8 *out, long stride)
{
  if (onion_on && frame_count(d) > 1) frames_onion(d, onion_frame, onion_before, onion_after, onion_opacity, r, out, stride);
  else doc_composite(d, r, out, stride);
}
