/*
 * prefs.c - Edit > Preferences (0.8): the settings PhotoCraft keeps on its Preferences pages, as
 * Easel has them. A list of pages down the left (General, Interface, Tools, File Handling, Units &
 * Rulers, Guides & Grid, Scratch Drawer), each page's settings to the right. They are kept in
 * PROGDIR:Easel.prefs with the rest (main.c reads and writes the file; the lines here are this
 * file's) and take effect at once when OK is pressed.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"

Prefs prefs;

static ULONG xget(Object *o, ULONG a) { ULONG v = 0; get(o, a, &v); return v; }
/* the settings Easel starts with */
void prefs_defaults(void)
{
  memset(&prefs, 0, sizeof prefs);
  prefs.tips = 1; prefs.xform_numbers = 1; prefs.caps_precise = 1;
  prefs.save_fmt = SF_ORA; prefs.jpeg_quality = 90; prefs.ask_flatten = 1; prefs.nrecent = 10;
  strcpy(prefs.recovery, "PROGDIR:Recovery");
  prefs.units = UN_PIXELS; prefs.dpi = 72;
  prefs.guide_color = 0x00C0FF; prefs.grid_color = 0x808080; prefs.grid_spacing = 16; prefs.grid_sub = 1;
  strcpy(prefs.scratch, "T:"); prefs.scratch_mb = 256;
  prefs.undo_mb = 40; prefs.undo_levels = 100;
}
/* (see easel.h) one line of Easel.prefs taken if it is one of these settings; 1 if it was */
int prefs_line(const char *l)
{
  static const struct { const char *key; int *v; } ints[] = {
    { "tips=", &prefs.tips }, { "large=", &prefs.large_text }, { "cursor=", &prefs.cursor }, { "capsprecise=", &prefs.caps_precise },
    { "xformnums=", &prefs.xform_numbers }, { "savefmt=", &prefs.save_fmt }, { "jpegq=", &prefs.jpeg_quality },
    { "askflatten=", &prefs.ask_flatten }, { "nrecent=", &prefs.nrecent }, { "units=", &prefs.units }, { "dpi=", &prefs.dpi },
    { "gridsp=", &prefs.grid_spacing }, { "gridsub=", &prefs.grid_sub }, { "scratchon=", &prefs.scratch_on },
    { "scratchmb=", &prefs.scratch_mb }, { "undomb=", &prefs.undo_mb }, { "undolevels=", &prefs.undo_levels },
    { "screenmode=", &prefs.screen_mode }
  };
  int i;
  for (i = 0; i < (int)(sizeof ints / sizeof ints[0]); i++) {
    size_t n = strlen(ints[i].key);
    if (!strncmp(l, ints[i].key, n)) { *ints[i].v = atoi(l + n); return 1; }
  }
  if (!strncmp(l, "guidecol=", 9)) { prefs.guide_color = strtoul(l + 9, NULL, 16) & 0xFFFFFF; return 1; }
  if (!strncmp(l, "gridcol=", 8)) { prefs.grid_color = strtoul(l + 8, NULL, 16) & 0xFFFFFF; return 1; }
  if (!strncmp(l, "screenid=", 9)) { prefs.screen_id = strtoul(l + 9, NULL, 16); return 1; }
  if (!strncmp(l, "recovery=", 9)) { strncpy(prefs.recovery, l + 9, sizeof prefs.recovery - 1); return 1; }
  if (!strncmp(l, "scratch=", 8)) { strncpy(prefs.scratch, l + 8, sizeof prefs.scratch - 1); return 1; }
  return 0;
}
/* these settings written to Easel.prefs */
void prefs_write(FILE *f)
{
  fprintf(f, "tips=%d\nlarge=%d\ncursor=%d\ncapsprecise=%d\nxformnums=%d\n", prefs.tips, prefs.large_text, prefs.cursor, prefs.caps_precise, prefs.xform_numbers);
  fprintf(f, "savefmt=%d\njpegq=%d\naskflatten=%d\nnrecent=%d\nrecovery=%s\n", prefs.save_fmt, prefs.jpeg_quality, prefs.ask_flatten, prefs.nrecent, prefs.recovery);
  fprintf(f, "units=%d\ndpi=%d\nguidecol=%06lX\ngridcol=%06lX\ngridsp=%d\ngridsub=%d\n", prefs.units, prefs.dpi,
          (unsigned long)prefs.guide_color, (unsigned long)prefs.grid_color, prefs.grid_spacing, prefs.grid_sub);
  fprintf(f, "scratchon=%d\nscratch=%s\nscratchmb=%d\nundomb=%d\nundolevels=%d\n", prefs.scratch_on, prefs.scratch, prefs.scratch_mb, prefs.undo_mb, prefs.undo_levels);
  fprintf(f, "screenmode=%d\nscreenid=%08lX\n", prefs.screen_mode, (unsigned long)prefs.screen_id);
}
/* the settings kept in range and handed to the parts that use them (the core's undo, the bubbles) */
void prefs_apply(void)
{
  if (prefs.undo_mb < 1) prefs.undo_mb = 1;
  if (prefs.undo_mb > 1024) prefs.undo_mb = 1024;
  if (prefs.undo_levels < 2) prefs.undo_levels = 2;
  if (prefs.undo_levels > 1000) prefs.undo_levels = 1000;
  if (prefs.jpeg_quality < 1 || prefs.jpeg_quality > 100) prefs.jpeg_quality = 90;
  if (prefs.nrecent < 0) prefs.nrecent = 0;
  if (prefs.nrecent > 10) prefs.nrecent = 10;
  if (prefs.units < 0 || prefs.units >= UN_N) prefs.units = UN_PIXELS;
  if (prefs.dpi < 1 || prefs.dpi > 9999) prefs.dpi = 72;
  if (prefs.grid_spacing < 2) prefs.grid_spacing = 2;
  if (prefs.grid_spacing > 1000) prefs.grid_spacing = 1000;
  if (prefs.grid_sub < 1) prefs.grid_sub = 1;
  if (prefs.grid_sub > 16) prefs.grid_sub = 16;
  if (prefs.scratch_mb < 8) prefs.scratch_mb = 8;
  if (prefs.save_fmt < 0 || prefs.save_fmt >= SF_N) prefs.save_fmt = SF_ORA;
  if (!prefs.recovery[0]) strcpy(prefs.recovery, "PROGDIR:Recovery");
  undo_budget = (long)prefs.undo_mb * 1024 * 1024;
  undo_levels = prefs.undo_levels;
  if (prefs.scratch_on && prefs.scratch[0]) { strncpy(undo_scratch, prefs.scratch, sizeof undo_scratch - 1); undo_scratch[sizeof undo_scratch - 1] = 0; }
  else undo_scratch[0] = 0;
  undo_scratch_max = (long)prefs.scratch_mb * 1024 * 1024;
  theme_tips = prefs.tips;
}

/* ------------------------------------------------------------------ units */
const char *const unit_names[UN_N + 1] = { "Pixels", "Inches", "Centimetres", "Millimetres", "Per cent", NULL };
/* (see easel.h) a length of px pixels in the units chosen: of length (the picture's width or height,
   for per cent) at dpi; with the unit's short name when name is set */
void unit_text(char *out, double px, long length, int dpi, int name)
{
  static const char *const sfx[UN_N] = { " px", " in", " cm", " mm", "%" };
  double v = px;
  long t;
  if (dpi < 1) dpi = prefs.dpi > 0 ? prefs.dpi : 72;
  switch (prefs.units) {
  case UN_INCHES: v = px / dpi; break;
  case UN_CM: v = px * 2.54 / dpi; break;
  case UN_MM: v = px * 25.4 / dpi; break;
  case UN_PERCENT: v = length > 0 ? px * 100.0 / length : 0; break;
  }
  if (prefs.units == UN_PIXELS) { sprintf(out, "%ld%s", (long)(v < 0 ? v - 0.5 : v + 0.5), name ? sfx[0] : ""); return; }
  /* two decimals (one for millimetres and per cent), without the C library's floating point printf */
  if (prefs.units == UN_MM || prefs.units == UN_PERCENT) { t = (long)(v * 10 + (v < 0 ? -0.5 : 0.5)); sprintf(out, "%s%ld.%ld%s", t < 0 ? "-" : "", labs(t) / 10, labs(t) % 10, name ? sfx[prefs.units] : ""); }
  else { t = (long)(v * 100 + (v < 0 ? -0.5 : 0.5)); sprintf(out, "%s%ld.%02ld%s", t < 0 ? "-" : "", labs(t) / 100, labs(t) % 100, name ? sfx[prefs.units] : ""); }
}

/* ------------------------------------------------------------------ the window */
static const char *const pages[] = { "General", "Interface", "Tools", "File Handling", "Units & Rulers", "Guides & Grid", "Scratch Drawer", NULL };
static const char *const cursors[] = { "Brush outline", "Crosshair", "Outline and crosshair", NULL };
static const char *const formats[] = { "OpenRaster (.ora)", "PNG", "JPEG", "TIFF", "Photoshop PSD", "IFF ILBM", NULL };
static const char *pagelist[8];
static ULONG pick_guide, pick_grid;          /* the colours as they are being chosen */
static Object *guide_txt, *grid_txt;
static Object *slider(LONG min, LONG max, LONG v) { return SliderObject, MUIA_Numeric_Min, min, MUIA_Numeric_Max, max, MUIA_Numeric_Value, v, MUIA_CycleChain, 1, End; }
static Object *check(int on)
{
  return ImageObject, ImageButtonFrame, MUIA_InputMode, MUIV_InputMode_Toggle, MUIA_Image_Spec, MUII_CheckMark,
    MUIA_Background, MUII_ButtonBack, MUIA_ShowSelState, FALSE, MUIA_Selected, on, MUIA_CycleChain, 1, End;
}
static Object *cycle(const char *const *labels, int active) { return CycleObject, MUIA_Cycle_Entries, (ULONG)labels, MUIA_Cycle_Active, active, MUIA_CycleChain, 1, End; }
static Object *string(const char *s, int max) { return StringObject, StringFrame, MUIA_String_Contents, (ULONG)s, MUIA_String_MaxLen, max, MUIA_CycleChain, 1, End; }
static Object *text(const char *s) { return TextObject, MUIA_Text_Contents, (ULONG)s, End; }
static Object *label(const char *s) { return QLabel(tr(s), MUIO_Label_Centered); }
/* a colour's hex shown in its text */
static void show_color(Object *t, ULONG c) { static char b[2][16]; char *p = b[t == grid_txt]; sprintf(p, "#%06lX", (unsigned long)c); set(t, MUIA_Text_Contents, (ULONG)p); }
/* the colour buttons' presses (the dialog's loop calls this) */
static void each(ULONG id)
{
  LONG c;
  if (id == ID_DLGRESET && (c = pick_color("Guide Colour", pick_guide)) >= 0) { pick_guide = (ULONG)c; show_color(guide_txt, pick_guide); }
  if (id == ID_DLGAUTO && (c = pick_color("Grid Colour", pick_grid)) >= 0) { pick_grid = (ULONG)c; show_color(grid_txt, pick_grid); }
}
/* (see easel.h) Edit > Preferences: 1 if OK was pressed (the settings are then in prefs and applied;
   *reopen set when the window must open again for the theme or text size) */
int dlg_preferences(int *reopen)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *list, *pg, *gpick, *gridpick;
  Object *as, *ic, *lv, *mb, *tp, *dk, *lg, *cu, *cp, *xn, *sf, *jq, *af, *nr, *rc, *un, *dp, *gs, *gsub, *so, *sp, *smb;
  char asb[8], dpb[8], usage[120];
  int i, ok;
  for (i = 0; pages[i]; i++) pagelist[i] = tr(pages[i]);
  pick_guide = prefs.guide_color; pick_grid = prefs.grid_color;
  sprintf(asb, "%d", pref_autosave); sprintf(dpb, "%d", prefs.dpi);
  sprintf(usage, tr("In the drawer now: %ld KB of undo steps."), undo_scratch_bytes / 1024);
  w = dialog_window("Preferences", HGroup,
        Child, list = ListviewObject, MUIA_Listview_Input, TRUE, MUIA_CycleChain, 1, MUIA_HorizWeight, 30,
          MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_SourceArray, (ULONG)pagelist, MUIA_List_Active, 0, MUIA_List_AdjustWidth, TRUE, End, End,
        Child, pg = VGroup, MUIA_Group_PageMode, TRUE, MUIA_HorizWeight, 100,
          /* General */
          Child, VGroup, Child, QTitleObject(tr("General")), Child, ColGroup(2),
            Child, label("Autosave every (minutes, 0 none):"), Child, as = StringObject, StringFrame, MUIA_String_Contents, (ULONG)asb, MUIA_String_Accept, (ULONG)"0123456789", MUIA_String_MaxLen, 4, MUIA_CycleChain, 1, End,
            Child, label("Save icons with pictures:"), Child, HGroup, Child, ic = check(pref_icons), Child, HSpace(0), End,
            Child, label("History states:"), Child, lv = slider(2, 1000, prefs.undo_levels),
            Child, label("Undo memory (MB):"), Child, mb = slider(1, 512, prefs.undo_mb),
            Child, label("Help bubbles:"), Child, HGroup, Child, tp = check(prefs.tips), Child, HSpace(0), End,
            End, Child, HVSpace, End,
          /* Interface */
          Child, VGroup, Child, QTitleObject(tr("Interface")), Child, ColGroup(2),
            Child, label("Dark interface:"), Child, HGroup, Child, dk = check(pref_dark), Child, HSpace(0), End,
            Child, label("Large interface text:"), Child, HGroup, Child, lg = check(prefs.large_text), Child, HSpace(0), End,
            Child, label("Language:"), Child, text(tr("English (Easel's own words are in English only)")),
            End, Child, HVSpace, End,
          /* Tools */
          Child, VGroup, Child, QTitleObject(tr("Tools")), Child, ColGroup(2),
            Child, label("Painting cursor:"), Child, cu = cycle(cursors, prefs.cursor),
            Child, label("Caps Lock shows a precise cross:"), Child, HGroup, Child, cp = check(prefs.caps_precise), Child, HSpace(0), End,
            Child, label("Show transform values:"), Child, HGroup, Child, xn = check(prefs.xform_numbers), Child, HSpace(0), End,
            End, Child, HVSpace, End,
          /* File Handling */
          Child, VGroup, Child, QTitleObject(tr("File Handling")), Child, ColGroup(2),
            Child, label("New pictures save as:"), Child, sf = cycle(formats, prefs.save_fmt),
            Child, label("JPEG quality:"), Child, jq = slider(1, 100, prefs.jpeg_quality),
            Child, label("Ask before flattening layers:"), Child, HGroup, Child, af = check(prefs.ask_flatten), Child, HSpace(0), End,
            Child, label("Recent files listed:"), Child, nr = slider(0, 10, prefs.nrecent),
            Child, label("Recovery drawer:"), Child, rc = string(prefs.recovery, 250),
            End, Child, HVSpace, End,
          /* Units & Rulers */
          Child, VGroup, Child, QTitleObject(tr("Units & Rulers")), Child, ColGroup(2),
            Child, label("Rulers and Info in:"), Child, un = cycle(unit_names, prefs.units),
            Child, label("New picture resolution (dpi):"), Child, dp = StringObject, StringFrame, MUIA_String_Contents, (ULONG)dpb, MUIA_String_Accept, (ULONG)"0123456789", MUIA_String_MaxLen, 5, MUIA_CycleChain, 1, End,
            End, Child, HVSpace, End,
          /* Guides & Grid */
          Child, VGroup, Child, QTitleObject(tr("Guides & Grid")), Child, ColGroup(2),
            Child, label("Guide colour:"), Child, HGroup, Child, guide_txt = text(""), Child, gpick = QButton(tr("Choose...")), End,
            Child, label("Grid colour:"), Child, HGroup, Child, grid_txt = text(""), Child, gridpick = QButton(tr("Choose...")), End,
            Child, label("Gridline every (pixels):"), Child, gs = slider(2, 512, prefs.grid_spacing),
            Child, label("Subdivisions:"), Child, gsub = slider(1, 16, prefs.grid_sub),
            End, Child, text(tr("(The grid's spacing is for pictures opened or made from now on;\nView > Grid Settings changes the picture's own.)")), Child, HVSpace, End,
          /* Scratch */
          Child, VGroup, Child, QTitleObject(tr("Scratch Drawer")), Child, ColGroup(2),
            Child, label("Keep old undo steps on disk:"), Child, HGroup, Child, so = check(prefs.scratch_on), Child, HSpace(0), End,
            Child, label("Scratch drawer:"), Child, sp = string(prefs.scratch, 250),
            Child, label("At most (MB):"), Child, smb = slider(8, 2048, prefs.scratch_mb),
            End,
            Child, text(tr("Past the undo memory, the oldest steps are written to this drawer\ninstead of being forgotten, and read back when you undo that far.")),
            Child, text(usage), Child, HVSpace, End,
        End,
      End, "OK");
  if (!w) return 0;
  show_color(guide_txt, pick_guide); show_color(grid_txt, pick_grid);
  DoMethod(list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)pg, 3, MUIM_Set, MUIA_Group_ActivePage, MUIV_TriggerValue);
  DoMethod(gpick, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGRESET);
  DoMethod(gridpick, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGAUTO);
  ok = run_modal(w, ends, each) == ID_DLGOK;
  if (ok) {
    int dark = (int)xget(dk, MUIA_Selected) != 0, large = (int)xget(lg, MUIA_Selected) != 0;
    *reopen = dark != pref_dark || large != prefs.large_text;
    pref_dark = dark; prefs.large_text = large;
    pref_autosave = atoi((const char *)xget(as, MUIA_String_Contents));
    if (pref_autosave < 0) pref_autosave = 0;
    if (pref_autosave > 120) pref_autosave = 120;
    pref_icons = xget(ic, MUIA_Selected) != 0;
    prefs.undo_levels = (int)xget(lv, MUIA_Numeric_Value); prefs.undo_mb = (int)xget(mb, MUIA_Numeric_Value);
    prefs.tips = xget(tp, MUIA_Selected) != 0;
    prefs.cursor = (int)xget(cu, MUIA_Cycle_Active); prefs.caps_precise = xget(cp, MUIA_Selected) != 0; prefs.xform_numbers = xget(xn, MUIA_Selected) != 0;
    prefs.save_fmt = (int)xget(sf, MUIA_Cycle_Active); prefs.jpeg_quality = (int)xget(jq, MUIA_Numeric_Value);
    prefs.ask_flatten = xget(af, MUIA_Selected) != 0; prefs.nrecent = (int)xget(nr, MUIA_Numeric_Value);
    strncpy(prefs.recovery, (const char *)xget(rc, MUIA_String_Contents), sizeof prefs.recovery - 1);
    prefs.units = (int)xget(un, MUIA_Cycle_Active); prefs.dpi = atoi((const char *)xget(dp, MUIA_String_Contents));
    prefs.guide_color = pick_guide; prefs.grid_color = pick_grid;
    prefs.grid_spacing = (int)xget(gs, MUIA_Numeric_Value); prefs.grid_sub = (int)xget(gsub, MUIA_Numeric_Value);
    prefs.scratch_on = xget(so, MUIA_Selected) != 0; prefs.scratch_mb = (int)xget(smb, MUIA_Numeric_Value);
    strncpy(prefs.scratch, (const char *)xget(sp, MUIA_String_Contents), sizeof prefs.scratch - 1);
    prefs_apply();
  }
  MUI_DisposeObject(w);
  guide_txt = grid_txt = NULL;
  return ok;
}
