/*
 * prefs.c - Edit > Preferences (0.8): the settings PhotoCraft keeps on its Preferences pages, as
 * Easel has them. A list of pages down the left (General, Interface, Tools, File Handling, Units &
 * Rulers, Guides & Grid, Scratch Drawer; 1.2: Transparency), each page's settings to the right. They are kept in
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
#include "../core/menu3.h"
#include "tools2.h"                     /* 1.2 */
#include "../core/layerx.h"
#include "theme.h"
#include "colorui.h"
#include "lang.h"
#include "ids.h"
#include "../core/viewx.h"                 /* 1.2 */
#include "../core/cms.h"                   /* round 4: the gamut warning's colour */
#include "mxids.h"                         /* (vx_par_correct, kept in Easel.prefs) */

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
  prefs.language = -1;
  prefs.checker_size = 2; prefs.checker_scheme = CHK_LIGHT; prefs.checker_c1 = 0xFFFFFF; prefs.checker_c2 = 0xCCCCCC;   /* 1.2 */
  prefs.screen_dpi = 72; prefs.column_w = 180; prefs.gutter_w = 12;
  prefs.qx_fmt = QX_PNG; prefs.qx_where = QW_BESIDE; prefs.qx_meta = 2; prefs.webp_quality = 80;   /* round 3 (2: EXD_ALL) */
  prefs.gamut_color = GAMUT_GREY; prefs.gamut_opacity = 100;   /* round 4 */
}
/* (see easel.h) one line of Easel.prefs taken if it is one of these settings; 1 if it was */
extern signed char key_tool[26];                           /* (main.c) */
int prefs_line(const char *l)
{
  if (histopts_line(l)) return 1;                          /* 1.2: History Options and Log */
  if (!strncmp(l, "toolkeys=", 9)) return toolkeys_parse(l + 9, key_tool, TL_N), 1;   /* 1.3: each letter's last tool */
  static const struct { const char *key; int *v; } ints[] = {
    { "tips=", &prefs.tips }, { "large=", &prefs.large_text }, { "cursor=", &prefs.cursor }, { "capsprecise=", &prefs.caps_precise },
    { "xformnums=", &prefs.xform_numbers }, { "savefmt=", &prefs.save_fmt }, { "jpegq=", &prefs.jpeg_quality },
    { "askflatten=", &prefs.ask_flatten }, { "nrecent=", &prefs.nrecent }, { "units=", &prefs.units }, { "dpi=", &prefs.dpi },
    { "gridsp=", &prefs.grid_spacing }, { "gridsub=", &prefs.grid_sub }, { "scratchon=", &prefs.scratch_on },
    { "scratchmb=", &prefs.scratch_mb }, { "undomb=", &prefs.undo_mb }, { "undolevels=", &prefs.undo_levels },
    { "screenmode=", &prefs.screen_mode }, { "skipplacexf=", &prefs.skip_place_xf },
    { "tabletcurve=", &tablet_curve_pc }, { "tabletfloor=", &tablet_floor_pc }, { "dither=", &display_dither },   /* 1.2 */
    { "parcorrect=", &vx_par_correct },                    /* 1.2: View > Pixel Aspect Ratio Correction */
    { "checker=", &prefs.checker_size }, { "checkerscheme=", &prefs.checker_scheme }, { "gridstyle=", &prefs.grid_style },
    { "guidestyle=", &prefs.guide_style }, { "screendpi=", &prefs.screen_dpi }, { "columnw=", &prefs.column_w },
    { "gutterw=", &prefs.gutter_w }, { "pointkind=", &prefs.point_kind },
    { "qxfmt=", &prefs.qx_fmt }, { "qxwhere=", &prefs.qx_where }, { "qxmeta=", &prefs.qx_meta }, { "qxsrgb=", &prefs.qx_srgb },   /* round 3 */
    { "webpq=", &prefs.webp_quality }, { "placesmart=", &prefs.place_smart }, { "beepdone=", &prefs.beep_done },
    { "smartquotes=", &prefs.smart_quotes }, { "canvascolor=", &prefs.canvas_color }, { "gamutopacity=", &prefs.gamut_opacity }, { "wheelzoom=", &prefs.wheel_zoom }   /* round 4 */
  };
  int i;
  for (i = 0; i < (int)(sizeof ints / sizeof ints[0]); i++) {
    size_t n = strlen(ints[i].key);
    if (!strncmp(l, ints[i].key, n)) { *ints[i].v = atoi(l + n); return 1; }
  }
  if (!strncmp(l, "guidecol=", 9)) { prefs.guide_color = strtoul(l + 9, NULL, 16) & 0xFFFFFF; return 1; }
  if (!strncmp(l, "gridcol=", 8)) { prefs.grid_color = strtoul(l + 8, NULL, 16) & 0xFFFFFF; return 1; }
  if (!strncmp(l, "checkerc1=", 10)) { prefs.checker_c1 = strtoul(l + 10, NULL, 16) & 0xFFFFFF; return 1; }
  if (!strncmp(l, "gamutcol=", 9)) { prefs.gamut_color = strtoul(l + 9, NULL, 16) & 0xFFFFFF; return 1; }   /* round 4 */
  if (!strncmp(l, "checkerc2=", 10)) { prefs.checker_c2 = strtoul(l + 10, NULL, 16) & 0xFFFFFF; return 1; }
  if (!strncmp(l, "screenid=", 9)) { prefs.screen_id = strtoul(l + 9, NULL, 16); return 1; }
  if (!strncmp(l, "recovery=", 9)) { strncpy(prefs.recovery, l + 9, sizeof prefs.recovery - 1); return 1; }
  if (!strncmp(l, "monitoricc=", 11)) { strncpy(prefs.monitor_icc, l + 11, sizeof prefs.monitor_icc - 1); return 1; }
  if (!strncmp(l, "qxdir=", 6)) { strncpy(prefs.qx_dir, l + 6, sizeof prefs.qx_dir - 1); return 1; }   /* round 3 */
  if (!strncmp(l, "scratch=", 8)) { strncpy(prefs.scratch, l + 8, sizeof prefs.scratch - 1); return 1; }
  if (!strncmp(l, "language=", 9)) { prefs.language = lang_by_code(l + 9); return 1; }    /* (1.1: "auto" or a code) */
  return 0;
}
/* these settings written to Easel.prefs */
void prefs_write(FILE *f)
{
  char tk[200];
  histopts_write(f);                                       /* 1.2 */
  toolkeys_format(key_tool, tk, sizeof tk);                /* 1.3 */
  fprintf(f, "toolkeys=%s\n", tk);
  fprintf(f, "tabletcurve=%d\ntabletfloor=%d\ndither=%d\nparcorrect=%d\n", tablet_curve_pc, tablet_floor_pc, display_dither, vx_par_correct);
  fprintf(f, "tips=%d\nlarge=%d\ncursor=%d\ncapsprecise=%d\nxformnums=%d\n", prefs.tips, prefs.large_text, prefs.cursor, prefs.caps_precise, prefs.xform_numbers);
  fprintf(f, "savefmt=%d\njpegq=%d\naskflatten=%d\nnrecent=%d\nrecovery=%s\n", prefs.save_fmt, prefs.jpeg_quality, prefs.ask_flatten, prefs.nrecent, prefs.recovery);
  fprintf(f, "units=%d\ndpi=%d\nguidecol=%06lX\ngridcol=%06lX\ngridsp=%d\ngridsub=%d\n", prefs.units, prefs.dpi,
          (unsigned long)prefs.guide_color, (unsigned long)prefs.grid_color, prefs.grid_spacing, prefs.grid_sub);
  fprintf(f, "scratchon=%d\nscratch=%s\nscratchmb=%d\nundomb=%d\nundolevels=%d\n", prefs.scratch_on, prefs.scratch, prefs.scratch_mb, prefs.undo_mb, prefs.undo_levels);
  fprintf(f, "screenmode=%d\nscreenid=%08lX\n", prefs.screen_mode, (unsigned long)prefs.screen_id);
  fprintf(f, "skipplacexf=%d\n", prefs.skip_place_xf);
  fprintf(f, "language=%s\n", prefs.language >= 0 ? lang_code(prefs.language) : "auto");
  fprintf(f, "checker=%d\ncheckerscheme=%d\ncheckerc1=%06lX\ncheckerc2=%06lX\ngridstyle=%d\nguidestyle=%d\n", prefs.checker_size, prefs.checker_scheme,
          (unsigned long)prefs.checker_c1, (unsigned long)prefs.checker_c2, prefs.grid_style, prefs.guide_style);
  fprintf(f, "screendpi=%d\ncolumnw=%d\ngutterw=%d\npointkind=%d\n", prefs.screen_dpi, prefs.column_w, prefs.gutter_w,
          prefs.point_kind);
  fprintf(f, "qxfmt=%d\nqxwhere=%d\nqxmeta=%d\nqxsrgb=%d\nqxdir=%s\nwebpq=%d\nplacesmart=%d\nbeepdone=%d\n", prefs.qx_fmt, prefs.qx_where,
          prefs.qx_meta, prefs.qx_srgb, prefs.qx_dir, prefs.webp_quality, prefs.place_smart, prefs.beep_done);   /* round 3 */
  fprintf(f, "monitoricc=%s\n", prefs.monitor_icc);
  fprintf(f, "smartquotes=%d\ncanvascolor=%d\ngamutcol=%06lX\ngamutopacity=%d\nwheelzoom=%d\n", prefs.smart_quotes, prefs.canvas_color,
          (unsigned long)prefs.gamut_color, prefs.gamut_opacity, prefs.wheel_zoom);                    /* round 4 */
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
  if (prefs.checker_size < 0 || prefs.checker_size > 3) prefs.checker_size = 2;          /* 1.2 */
  if (prefs.checker_scheme < 0 || prefs.checker_scheme >= CHK_N) prefs.checker_scheme = CHK_LIGHT;
  if (prefs.grid_style < 0 || prefs.grid_style >= LS_N) prefs.grid_style = LS_LINES;
  if (prefs.guide_style < 0 || prefs.guide_style >= LS_N) prefs.guide_style = LS_LINES;
  if (prefs.screen_dpi < 20 || prefs.screen_dpi > 600) prefs.screen_dpi = 72;
  if (prefs.column_w < 1 || prefs.column_w > 9999) prefs.column_w = 180;
  if (prefs.gutter_w < 0 || prefs.gutter_w > 999) prefs.gutter_w = 12;
  if (prefs.point_kind != 1) prefs.point_kind = 0;
  if (prefs.qx_fmt < 0 || prefs.qx_fmt >= QX_N) prefs.qx_fmt = QX_PNG;                  /* round 3 */
  if (prefs.qx_where < QW_BESIDE || prefs.qx_where > QW_ASK) prefs.qx_where = QW_BESIDE;
  if (prefs.qx_meta < 0 || prefs.qx_meta > 2) prefs.qx_meta = 2;
  if (prefs.webp_quality < 1 || prefs.webp_quality > 100) prefs.webp_quality = 80;
  if (prefs.canvas_color < 0 || prefs.canvas_color > 5) prefs.canvas_color = 0;          /* round 4 */
  if (prefs.gamut_opacity < 1 || prefs.gamut_opacity > 100) prefs.gamut_opacity = 100;
  gamut_warn_color = prefs.gamut_color; gamut_warn_opacity = prefs.gamut_opacity;
  undo_budget = (long)prefs.undo_mb * 1024 * 1024;
  undo_levels = prefs.undo_levels;
  if (prefs.scratch_on && prefs.scratch[0]) { strncpy(undo_scratch, prefs.scratch, sizeof undo_scratch - 1); undo_scratch[sizeof undo_scratch - 1] = 0; }
  else undo_scratch[0] = 0;
  undo_scratch_max = (long)prefs.scratch_mb * 1024 * 1024;
  theme_tips = prefs.tips;
  colorui_set_monitor(prefs.monitor_icc);                  /* round 3 */
  /* 1.2: smart objects keep their sources in the scratch drawer (T: when none is set) */
  strncpy(smart_drawer, prefs.scratch[0] ? prefs.scratch : "T:", sizeof smart_drawer - 1); smart_drawer[sizeof smart_drawer - 1] = 0;
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
static const char *const pages[] = { "General", "Interface", "Tools", "File Handling", "Export", "Units & Rulers", "Guides & Grid", "Scratch Drawer", "Transparency & Gamut", "Type", NULL };
static const char *const canvas_colors[] = { "Default", "Black", "Dark Grey", "Medium Grey", "Light Grey", "White", NULL };   /* round 4 */
/* round 3: the Export page's choices */
static const char *const qx_formats[] = { "PNG", "JPEG", "GIF", "WebP", "TIFF", NULL };
static const char *const qx_wheres[] = { "Beside the picture's file", "In the last drawer exported to", "In this drawer:", "Ask each time", NULL };
static const char *const qx_metas[] = { "None", "Copyright only", "All", NULL };
static const char *const point_kinds[] = { "PostScript (72 points/inch)", "Traditional (72.27 points/inch)", NULL };
static const char *cyc_labels[3][CHK_N + 1];       /* (1.2: the cycles' labels, translated) */
static const char *const cursors[] = { "Brush outline", "Crosshair", "Outline and crosshair", NULL };
static const char *const formats[] = { "OpenRaster (.ora)", "PNG", "JPEG", "TIFF", "Photoshop PSD", "IFF ILBM", NULL };
static const char *pagelist[12];
static ULONG pick_guide, pick_grid, pick_c1, pick_c2, pick_gam;   /* the colours as they are being chosen */
static Object *guide_txt, *grid_txt, *c1_txt, *c2_txt, *gam_txt;
static const char *langlist[UI_NLANG + 2];  /* (1.1) Locale prefs' choice, then each language in its own words */
static Object *slider(LONG min, LONG max, LONG v) { return SliderObject, MUIA_Numeric_Min, min, MUIA_Numeric_Max, max, MUIA_Numeric_Value, v, MUIA_CycleChain, 1, End; }
static Object *check(int on)
{
  return ImageObject, ImageButtonFrame, MUIA_InputMode, MUIV_InputMode_Toggle, MUIA_Image_Spec, MUII_CheckMark,
    MUIA_Background, MUII_ButtonBack, MUIA_ShowSelState, FALSE, MUIA_Selected, on, MUIA_CycleChain, 1, End;
}
static Object *cycle(const char *const *labels, int active) { return CycleObject, MUIA_Cycle_Entries, (ULONG)tr_list(labels), MUIA_Cycle_Active, active, MUIA_CycleChain, 1, End; }
static Object *string(const char *s, int max) { return StringObject, StringFrame, MUIA_String_Contents, (ULONG)s, MUIA_String_MaxLen, max, MUIA_CycleChain, 1, End; }
static Object *text(const char *s) { return TextObject, MUIA_Text_Contents, (ULONG)s, End; }
static Object *label(const char *s) { return QLabel(tr(s), MUIO_Label_Centered); }
/* a colour's hex shown in its text */
static void show_color(Object *t, ULONG c) { static char b[5][16]; char *p = b[t == grid_txt ? 1 : t == c1_txt ? 2 : t == c2_txt ? 3 : t == gam_txt ? 4 : 0]; sprintf(p, "#%06lX", (unsigned long)c); set(t, MUIA_Text_Contents, (ULONG)p); }
/* the colour buttons' presses (the dialog's loop calls this) */
static void each(ULONG id)
{
  LONG c;
  if (id == ID_DLGRESET && (c = pick_color("Guide Colour", pick_guide)) >= 0) { pick_guide = (ULONG)c; show_color(guide_txt, pick_guide); }
  if (id == ID_DLGAUTO && (c = pick_color("Grid Colour", pick_grid)) >= 0) { pick_grid = (ULONG)c; show_color(grid_txt, pick_grid); }
  if (id == ID_DLGBTN0 && (c = pick_color("Checkerboard Colour", pick_c1)) >= 0) { pick_c1 = (ULONG)c; show_color(c1_txt, pick_c1); }
  if (id == ID_DLGBTN0 + 1 && (c = pick_color("Checkerboard Colour", pick_c2)) >= 0) { pick_c2 = (ULONG)c; show_color(c2_txt, pick_c2); }
  if (id == ID_DLGBTN0 + 2 && (c = pick_color("Gamut Warning Colour", pick_gam)) >= 0) { pick_gam = (ULONG)c; show_color(gam_txt, pick_gam); }   /* round 4 */
}
/* (see easel.h) Edit > Preferences: 1 if OK was pressed (the settings are then in prefs and applied;
   *reopen set when the window must open again for the theme or text size) */
int dlg_preferences(int *reopen)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *list, *pg, *gpick, *gridpick;
  Object *ck, *cks, *c1p, *c2p, *grs, *gus, *sdp, *cw, *gw, *pk, *hl, *hp;
  char sdb[8], cwb[8], gwb[8];
  Object *qf, *qw, *qd, *qm, *qs, *wq, *psm, *bd;   /* round 3 */
  Object *smq, *cvc, *gmp, *gmo, *wz;                /* round 4 */
  Object *spx, *lgc, *as, *ic, *lv, *mb, *tp, *dk, *lg, *cu, *cp, *xn, *sf, *jq, *af, *nr, *rc, *un, *dp, *gs, *gsub, *so, *sp, *smb;
  char asb[8], dpb[8], usage[120];
  int i, ok;
  for (i = 0; pages[i]; i++) pagelist[i] = tr(pages[i]);
  for (i = 0; i < CHK_N; i++) cyc_labels[0][i] = tr(checker_names[i]);
  for (i = 0; i < 4; i++) cyc_labels[1][i] = tr(checker_size_names[i]);
  for (i = 0; i < LS_N; i++) cyc_labels[2][i] = tr(line_style_names[i]);
  langlist[0] = tr("As in Locale preferences");
  for (i = 0; i < UI_NLANG; i++) langlist[i + 1] = ui_languages[i];
  langlist[UI_NLANG + 1] = NULL;
  pick_guide = prefs.guide_color; pick_grid = prefs.grid_color; pick_c1 = prefs.checker_c1; pick_c2 = prefs.checker_c2; pick_gam = prefs.gamut_color;
  sprintf(sdb, "%d", prefs.screen_dpi); sprintf(cwb, "%d", prefs.column_w); sprintf(gwb, "%d", prefs.gutter_w);
  sprintf(asb, "%d", pref_autosave); sprintf(dpb, "%d", prefs.dpi);
  sprintf(usage, tr("In the drawer now: %ld KB of undo steps."), undo_scratch_bytes / 1024);
  w = dialog_window("Preferences", HGroup,
        Child, list = ListviewObject, MUIA_Listview_Input, TRUE, MUIA_CycleChain, 1, MUIA_HorizWeight, 30,
          MUIA_Listview_List, ListObject, InputListFrame, (ULONG)(theme_dialog_font() ? MUIA_Font : TAG_IGNORE), (ULONG)theme_dialog_font(), MUIA_List_SourceArray, (ULONG)pagelist, MUIA_List_Active, 0, MUIA_List_AdjustWidth, TRUE, End, End,
        Child, pg = VGroup, MUIA_Group_PageMode, TRUE, MUIA_HorizWeight, 100,
          /* General */
          Child, VGroup, Child, QTitleObject(tr("General")), Child, ColGroup(2),
            Child, label("Autosave every (minutes, 0 none):"), Child, as = StringObject, StringFrame, MUIA_String_Contents, (ULONG)asb, MUIA_String_Accept, (ULONG)"0123456789", MUIA_String_MaxLen, 4, MUIA_CycleChain, 1, End,
            Child, label("Save icons with pictures:"), Child, HGroup, Child, ic = check(pref_icons), Child, HSpace(0), End,
            Child, label("History states:"), Child, lv = slider(2, 1000, prefs.undo_levels),
            Child, label("Undo memory (MB):"), Child, mb = slider(1, 512, prefs.undo_mb),
            Child, label("Help bubbles:"), Child, HGroup, Child, tp = check(prefs.tips), Child, HSpace(0), End,
            Child, label("Skip Transform when Placing:"), Child, HGroup, Child, spx = check(prefs.skip_place_xf), Child, HSpace(0), End,
            Child, label("Always create Smart Objects when placing:"), Child, HGroup, Child, psm = check(prefs.place_smart), Child, HSpace(0), End,
            Child, label("Beep when long jobs are done:"), Child, HGroup, Child, bd = check(prefs.beep_done), Child, HSpace(0), End,
            Child, label("Zoom with Scroll Wheel:"), Child, HGroup, Child, wz = check(prefs.wheel_zoom), Child, HSpace(0), End,   /* round 4 */
            Child, label("History log (each step to a file):"), Child, HGroup, Child, hl = check(hist_opts.log_on), Child, HSpace(0), End,
            Child, label("Log file:"), Child, hp = string(hist_opts.log_file, 250),
            End, Child, HVSpace, End,
          /* Interface */
          Child, VGroup, Child, QTitleObject(tr("Interface")), Child, ColGroup(2),
            Child, label("Dark interface:"), Child, HGroup, Child, dk = check(pref_dark), Child, HSpace(0), End,
            Child, label("Large interface text:"), Child, HGroup, Child, lg = check(prefs.large_text), Child, HSpace(0), End,
            Child, label("Language:"), Child, lgc = CycleObject, MUIA_Cycle_Entries, (ULONG)langlist, MUIA_Cycle_Active, prefs.language + 1, MUIA_CycleChain, 1, End,
            Child, label("Canvas colour:"), Child, cvc = cycle(canvas_colors, prefs.canvas_color),
            End, Child, text(tr("(A new language shows when Easel starts again.)")), Child, HVSpace, End,
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
          /* round 3: Export */
          Child, VGroup, Child, QTitleObject(tr("Export")), Child, ColGroup(2),
            Child, label("Quick Export format:"), Child, qf = cycle(qx_formats, prefs.qx_fmt),
            Child, label("Quick Export writes:"), Child, qw = cycle(qx_wheres, prefs.qx_where),
            Child, label("Drawer:"), Child, qd = string(prefs.qx_dir, 250),
            Child, label("Metadata kept:"), Child, qm = cycle(qx_metas, prefs.qx_meta),
            Child, label("Convert to sRGB:"), Child, HGroup, Child, qs = check(prefs.qx_srgb), Child, HSpace(0), End,
            Child, label("Lossy WebP quality:"), Child, wq = slider(1, 100, prefs.webp_quality),
            End, Child, text(tr("(Quick Export's choices; Export As asks its own.\nJPEG's quality is on File Handling.)")), Child, HVSpace, End,
          /* Units & Rulers */
          Child, VGroup, Child, QTitleObject(tr("Units & Rulers")), Child, ColGroup(2),
            Child, label("Rulers and Info in:"), Child, un = cycle(unit_names, prefs.units),
            Child, label("New picture resolution (dpi):"), Child, dp = StringObject, StringFrame, MUIA_String_Contents, (ULONG)dpb, MUIA_String_Accept, (ULONG)"0123456789", MUIA_String_MaxLen, 5, MUIA_CycleChain, 1, End,
            Child, label("Screen resolution (dpi, Print Size):"), Child, sdp = StringObject, StringFrame, MUIA_String_Contents, (ULONG)sdb, MUIA_String_Accept, (ULONG)"0123456789", MUIA_String_MaxLen, 4, MUIA_CycleChain, 1, End,
            Child, label("Column width (pixels):"), Child, cw = StringObject, StringFrame, MUIA_String_Contents, (ULONG)cwb, MUIA_String_Accept, (ULONG)"0123456789", MUIA_String_MaxLen, 5, MUIA_CycleChain, 1, End,
            Child, label("Gutter (pixels):"), Child, gw = StringObject, StringFrame, MUIA_String_Contents, (ULONG)gwb, MUIA_String_Accept, (ULONG)"0123456789", MUIA_String_MaxLen, 4, MUIA_CycleChain, 1, End,
            Child, label("Point size:"), Child, pk = cycle(point_kinds, prefs.point_kind),
            End, Child, HVSpace, End,
          /* Guides & Grid */
          Child, VGroup, Child, QTitleObject(tr("Guides & Grid")), Child, ColGroup(2),
            Child, label("Guide colour:"), Child, HGroup, Child, guide_txt = text(""), Child, gpick = QButton(tr("Choose...")), End,
            Child, label("Grid colour:"), Child, HGroup, Child, grid_txt = text(""), Child, gridpick = QButton(tr("Choose...")), End,
            Child, label("Gridline every (pixels):"), Child, gs = slider(2, 512, prefs.grid_spacing),
            Child, label("Subdivisions:"), Child, gsub = slider(1, 16, prefs.grid_sub),
            Child, label("Grid style:"), Child, grs = cycle(cyc_labels[2], prefs.grid_style),
            Child, label("Guide style:"), Child, gus = cycle(cyc_labels[2], prefs.guide_style),
            End, Child, text(tr("(The grid's spacing is for pictures opened or made from now on;\nView > Grid Settings changes the picture's own.)")), Child, HVSpace, End,
          /* Scratch */
          Child, VGroup, Child, QTitleObject(tr("Scratch Drawer")), Child, ColGroup(2),
            Child, label("Keep old undo steps on disk:"), Child, HGroup, Child, so = check(prefs.scratch_on), Child, HSpace(0), End,
            Child, label("Scratch drawer:"), Child, sp = string(prefs.scratch, 250),
            Child, label("At most (MB):"), Child, smb = slider(8, 2048, prefs.scratch_mb),
            End,
            Child, text(tr("Past the undo memory, the oldest steps are written to this drawer\ninstead of being forgotten, and read back when you undo that far.")),
            Child, text(usage), Child, HVSpace, End,
          /* 1.2: Transparency */
          Child, VGroup, Child, QTitleObject(tr("Transparency & Gamut")), Child, ColGroup(2),
            Child, label("Grid size:"), Child, cks = cycle(cyc_labels[1], prefs.checker_size),
            Child, label("Grid colours:"), Child, ck = cycle(cyc_labels[0], prefs.checker_scheme),
            Child, label("Custom colour 1:"), Child, HGroup, Child, c1_txt = text(""), Child, c1p = QButton(tr("Choose...")), End,
            Child, label("Custom colour 2:"), Child, HGroup, Child, c2_txt = text(""), Child, c2p = QButton(tr("Choose...")), End,
            Child, label("Gamut warning colour:"), Child, HGroup, Child, gam_txt = text(""), Child, gmp = QButton(tr("Choose...")), End,
            Child, label("Gamut warning opacity (%):"), Child, gmo = slider(1, 100, prefs.gamut_opacity),
            End, Child, HVSpace, End,
          /* round 4: Type */
          Child, VGroup, Child, QTitleObject(tr("Type")), Child, ColGroup(2),
            Child, label("Use Smart Quotes:"), Child, HGroup, Child, smq = check(prefs.smart_quotes), Child, HSpace(0), End,
            End, Child, text(tr("(Quotes typed with the Type tool become typographer's quotes.)")), Child, HVSpace, End,
        End,
      End, "OK");
  if (!w) return 0;
  show_color(guide_txt, pick_guide); show_color(grid_txt, pick_grid); show_color(c1_txt, pick_c1); show_color(c2_txt, pick_c2); show_color(gam_txt, pick_gam);
  DoMethod(gmp, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGBTN0 + 2);
  DoMethod(c1p, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGBTN0);
  DoMethod(c2p, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGBTN0 + 1);
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
    prefs.skip_place_xf = xget(spx, MUIA_Selected) != 0;
    prefs.place_smart = xget(psm, MUIA_Selected) != 0; prefs.beep_done = xget(bd, MUIA_Selected) != 0;   /* round 3 */
    prefs.qx_fmt = (int)xget(qf, MUIA_Cycle_Active); prefs.qx_where = (int)xget(qw, MUIA_Cycle_Active);
    prefs.qx_meta = (int)xget(qm, MUIA_Cycle_Active); prefs.qx_srgb = xget(qs, MUIA_Selected) != 0;
    prefs.webp_quality = (int)xget(wq, MUIA_Numeric_Value);
    strncpy(prefs.qx_dir, (const char *)xget(qd, MUIA_String_Contents), sizeof prefs.qx_dir - 1);
    prefs.language = (int)xget(lgc, MUIA_Cycle_Active) - 1;
    prefs.cursor = (int)xget(cu, MUIA_Cycle_Active); prefs.caps_precise = xget(cp, MUIA_Selected) != 0; prefs.xform_numbers = xget(xn, MUIA_Selected) != 0;
    prefs.save_fmt = (int)xget(sf, MUIA_Cycle_Active); prefs.jpeg_quality = (int)xget(jq, MUIA_Numeric_Value);
    prefs.ask_flatten = xget(af, MUIA_Selected) != 0; prefs.nrecent = (int)xget(nr, MUIA_Numeric_Value);
    strncpy(prefs.recovery, (const char *)xget(rc, MUIA_String_Contents), sizeof prefs.recovery - 1);
    prefs.units = (int)xget(un, MUIA_Cycle_Active); prefs.dpi = atoi((const char *)xget(dp, MUIA_String_Contents));
    prefs.guide_color = pick_guide; prefs.grid_color = pick_grid;
    prefs.grid_spacing = (int)xget(gs, MUIA_Numeric_Value); prefs.grid_sub = (int)xget(gsub, MUIA_Numeric_Value);
    prefs.scratch_on = xget(so, MUIA_Selected) != 0; prefs.scratch_mb = (int)xget(smb, MUIA_Numeric_Value);
    strncpy(prefs.scratch, (const char *)xget(sp, MUIA_String_Contents), sizeof prefs.scratch - 1);
    /* 1.2 */
    prefs.checker_size = (int)xget(cks, MUIA_Cycle_Active); prefs.checker_scheme = (int)xget(ck, MUIA_Cycle_Active);
    prefs.checker_c1 = pick_c1; prefs.checker_c2 = pick_c2;
    prefs.smart_quotes = xget(smq, MUIA_Selected) != 0; prefs.canvas_color = (int)xget(cvc, MUIA_Cycle_Active);   /* round 4 */
    prefs.wheel_zoom = xget(wz, MUIA_Selected) != 0;
    prefs.gamut_color = pick_gam; prefs.gamut_opacity = (int)xget(gmo, MUIA_Numeric_Value);
    prefs.grid_style = (int)xget(grs, MUIA_Cycle_Active); prefs.guide_style = (int)xget(gus, MUIA_Cycle_Active);
    prefs.screen_dpi = atoi((const char *)xget(sdp, MUIA_String_Contents)); prefs.column_w = atoi((const char *)xget(cw, MUIA_String_Contents));
    prefs.gutter_w = atoi((const char *)xget(gw, MUIA_String_Contents)); prefs.point_kind = (int)xget(pk, MUIA_Cycle_Active);
    /* (the same History Log as Edit > History Options: one setting, kept by histopts_write) */
    hist_opts.log_on = xget(hl, MUIA_Selected) != 0;
    strncpy(hist_opts.log_file, (const char *)xget(hp, MUIA_String_Contents), sizeof hist_opts.log_file - 1);
    prefs_apply();
  }
  MUI_DisposeObject(w);
  guide_txt = grid_txt = c1_txt = c2_txt = gam_txt = NULL;
  return ok;
}
