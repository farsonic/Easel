/*
 * modedlg.c - Easel 0.4's mode and Amiga output dialogs (see modedlg.h), built as dialogs.c builds
 * its own: a ColGroup of labels and gadgets in dialog_window, run by run_modal, the values read
 * back before the window is disposed of. Dialogs whose text follows the choices (Screen Mode,
 * ILBM) loop on ID_DLGCHANGE as dlg_new_values does.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "modedlg.h"
#include "../core/palette.h"
#include "../core/amigamodes.h"
#include "../core/modex.h"                 /* round 3: Matte */

static ULONG xget(Object *o, ULONG a) { ULONG v = 0; if (o) get(o, a, &v); return v; }
static Object *slider(LONG min, LONG max, LONG v)
{
  return SliderObject, MUIA_Numeric_Min, min, MUIA_Numeric_Max, max, MUIA_Numeric_Value, v, MUIA_CycleChain, 1, End;
}
static Object *cycle(const char *const *labels, int active)
{
  return CycleObject, MUIA_Cycle_Entries, (ULONG)tr_list((const char *const *)labels), MUIA_Cycle_Active, active, MUIA_CycleChain, 1, End;   /* (1.1: in the language chosen) */
}
static Object *check(int on)
{
  return ImageObject, ImageButtonFrame, MUIA_InputMode, MUIV_InputMode_Toggle, MUIA_Image_Spec, MUII_CheckMark,
    MUIA_Background, MUII_ButtonBack, MUIA_ShowSelState, FALSE, MUIA_Selected, on, MUIA_CycleChain, 1, End;
}
static Object *text(const char *s, int lines)
{
  return TextObject, MUIA_Text_Contents, (ULONG)s, MUIA_Text_SetMin, FALSE, MUIA_Font, MUIV_Font_Tiny,
    (ULONG)(lines > 1 ? MUIA_FixHeightTxt : TAG_IGNORE), (ULONG)"\n\n\n\n", End;
}
static void notify_change(Object *o, ULONG attr)
{
  if (o) DoMethod(o, MUIM_Notify, attr, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGCHANGE);
}
/* a NULL-ended list of translated labels from a NULL-ended list (kept in buf, n at most) */
static const char **labels(const char *const *in, const char **buf, int n)
{
  int i;
  for (i = 0; i < n - 1 && in[i]; i++) buf[i] = tr(in[i]);
  buf[i] = NULL;
  return buf;
}

/* ================================================================== Indexed Color */
void modedlg_indexed_defaults(IndexedOpts *o)
{
  memset(o, 0, sizeof *o);
  o->colors = 256; o->source = IP_ADAPTIVE; o->method = QM_MEDIAN; o->preset = PP_WB20; o->dither = DI_FS; o->forced = IF_NONE; o->keep_locked = 1;
  o->matte = MT_NONE; o->transparency = 1;                   /* (round 3) */
}
int modedlg_indexed(Doc *d, IndexedOpts *o)
{
  /* the palette list: adaptive, adaptive in OCS 12 bits, the document's own, then the presets */
  static const char *srcs[4 + PP_N + 1];
  static const char *meths[QM_N + 1], *dits[DI_N + 1], *forced[6], *mattes[MT_N + 1];
  static const char *const forced_en[] = { "None", "Black and White", "Primaries", "Workbench (4)", "Foreground and Background", NULL };
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *cs, *ps, *ms, *ds, *fs, *kl, *mt, *tr_;
  int i, ok, src;
  if (!d) return 0;
  srcs[0] = tr("Adaptive (AGA, 24-bit)"); srcs[1] = tr("Adaptive (OCS/ECS, 12-bit)"); srcs[2] = tr("Current Palette");
  for (i = 0; i < PP_N; i++) srcs[3 + i] = preset_names[i];
  srcs[3 + PP_N] = NULL;
  labels(quant_names, meths, QM_N);                          /* (without "Fixed Palette": that is the list above) */
  labels(dither_names, dits, DI_N + 1);
  labels(forced_en, forced, 6);
  labels(matte_names, mattes, MT_N + 1);
  src = o->source == IP_PRESET ? 3 + o->preset : o->source == IP_CURRENT ? 2 : o->source == IP_ADAPTIVE_OCS ? 1 : 0;
  w = dialog_window("Indexed Color", ColGroup(2),
        Child, QLabel(tr("Palette:"), MUIO_Label_Centered), Child, ps = cycle(srcs, src),
        Child, QLabel(tr("Colours:"), MUIO_Label_Centered), Child, cs = slider(2, 256, o->colors),
        Child, QLabel(tr("Method:"), MUIO_Label_Centered), Child, ms = cycle(meths, o->method < QM_FIXED ? o->method : 0),
        Child, QLabel(tr("Forced:"), MUIO_Label_Centered), Child, fs = cycle(forced, o->forced),
        Child, QLabel(tr("Dither:"), MUIO_Label_Centered), Child, ds = cycle(dits, o->dither),
        Child, kl = check(o->keep_locked), Child, QLabel(tr("Keep locked colours in their places"), 0),
        Child, tr_ = check(o->transparency), Child, QLabel(tr("Transparency"), 0),                     /* round 3 */
        Child, QLabel(tr("Matte:"), MUIO_Label_Centered), Child, mt = cycle(mattes, o->matte),
        Child, HSpace(0), Child, text(tr("Layers keep their pixels; each also gets palette\nindices, which painting and palette edits then follow."), 2),
      End, NULL);
  if (!w) return 0;
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  if (ok) {
    src = (int)xget(ps, MUIA_Cycle_Active);
    o->source = src >= 3 ? IP_PRESET : src == 2 ? IP_CURRENT : src == 1 ? IP_ADAPTIVE_OCS : IP_ADAPTIVE;
    o->preset = src >= 3 ? src - 3 : o->preset;
    o->colors = (int)xget(cs, MUIA_Numeric_Value);
    o->method = (int)xget(ms, MUIA_Cycle_Active);
    o->forced = (int)xget(fs, MUIA_Cycle_Active);
    o->dither = (int)xget(ds, MUIA_Cycle_Active);
    o->keep_locked = (int)xget(kl, MUIA_Selected);
    o->matte = (int)xget(mt, MUIA_Cycle_Active); o->transparency = xget(tr_, MUIA_Selected) != 0;
  }
  MUI_DisposeObject(w);
  return ok;
}
int modedlg_apply_indexed(Doc *d, const IndexedOpts *o)
{
  u8 *fixed, locks[256];
  int nf = 0, ok;
  if (!d || !(fixed = (u8 *)malloc(768))) return 0;
  memcpy(locks, d->pal_lock, sizeof locks);
  /* one undo step: the locks dropped (if asked) and the conversion together */
  doc_begin(d, "Indexed Color");
  doc_note_pal(d);
  if (!o->keep_locked) memset(d->pal_lock, 0, sizeof d->pal_lock);
  set(app, MUIA_Application_Sleep, TRUE);
  /* round 3: the see-through pixels laid over the matte first (white when transparency isn't kept and no
     matte is chosen, as Photoshop does) */
  if (o->matte != MT_NONE || !o->transparency) {
    doc_note_all(d);
    layers_matte(d, o->matte != MT_NONE ? matte_color(d, o->matte) : 0xFFFFFF, o->transparency);
  }
  if (o->source == IP_PRESET || o->source == IP_CURRENT) {
    if (o->source == IP_PRESET) nf = pal_preset(o->preset, fixed);
    else { nf = d->npal < 2 ? 2 : d->npal; memcpy(fixed, d->pal, (size_t)nf * 3); }
    ok = doc_to_indexed(d, nf, QM_FIXED, o->dither, fixed, nf);
  } else {
    switch (o->forced) {
    case IF_BW: { static const u8 c[6] = { 0, 0, 0, 255, 255, 255 }; memcpy(fixed, c, 6); nf = 2; break; }
    case IF_PRIMARIES: {
      static const u8 c[24] = { 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0, 0, 255, 255, 255, 0, 255 };
      memcpy(fixed, c, 24); nf = 8; break;
    }
    case IF_WORKBENCH: nf = pal_preset(PP_WB20, fixed); break;
    case IF_FGBG:
      fixed[0] = (u8)(d->fg >> 16); fixed[1] = (u8)(d->fg >> 8); fixed[2] = (u8)d->fg;
      fixed[3] = (u8)(d->bg >> 16); fixed[4] = (u8)(d->bg >> 8); fixed[5] = (u8)d->bg; nf = 2; break;
    }
    ok = doc_to_indexed(d, o->colors, (o->method < QM_FIXED ? o->method : QM_MEDIAN) | (o->source == IP_ADAPTIVE_OCS ? QM_OCS12 : 0),
                        o->dither, nf ? fixed : NULL, nf);
  }
  set(app, MUIA_Application_Sleep, FALSE);
  if (!ok) memcpy(d->pal_lock, locks, sizeof locks);
  doc_end(d);
  if (!ok) doc_discard_last(d);                              /* (out of memory: nothing changed) */
  free(fixed);
  return ok;
}

/* ================================================================== Bitmap */
int modedlg_bitmap(int *method)
{
  static const char *meths[BMM_N + 1];
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *mc;
  int ok;
  labels(bitmap_names, meths, BMM_N + 1);
  w = dialog_window("Bitmap", ColGroup(2),
        Child, QLabel(tr("Method:"), MUIO_Label_Centered), Child, mc = cycle(meths, *method >= 0 && *method < BMM_N ? *method : 0),
        Child, HSpace(0), Child, text(tr("Black and white only: from each pixel's brightness."), 1),
      End, NULL);
  if (!w) return 0;
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  if (ok) *method = (int)xget(mc, MUIA_Cycle_Active);
  MUI_DisposeObject(w);
  return ok;
}

/* ================================================================== Amiga screen mode */
#define SM_ROWS 40
static char sm_rows[SM_ROWS][72];
static char sm_info[512], sm_warn[1024];
static void sm_describe(Doc *d, int mi, int chip, int ovs, int kind)
{
  const AmigaMode *m = amode_get(mi);
  static const char *const chips[3] = { "OCS", "ECS", "AGA" };
  int dd = 0, h6 = 0, h8 = 0, e = 0, w, h;
  long cols = 0;
  sm_info[0] = sm_warn[0] = 0;
  if (!m) return;
  amode_size(m, ovs, &w, &h);
  if (amode_limits(m, chip, &dd, &cols, &h6, &h8, &e))
    sprintf(sm_info, "%s: 0x%08lX\n%s: %d x %d   %s: %d:%d%s\n%s %s: %ld %s (%d %s)%s%s%s", tr("ModeID"), (unsigned long)m->modeid,
            tr("Size"), w, h, tr("Pixel aspect"), (int)m->ax, (int)m->ay, (m->flags & AM_EST_ASPECT) ? tr(" (estimated)") : "",
            tr("On"), chips[chip], cols, tr("colours"), dd, tr("planes"), e ? ", EHB" : "", h6 ? ", HAM6" : "", h8 ? ", HAM8" : "");
  else sprintf(sm_info, "%s: 0x%08lX\n%s %s.", tr("ModeID"), (unsigned long)m->modeid, tr("Not available on"), chips[chip]);
  if ((m->flags & AM_EST_OVERSCAN) && ovs) strcat(sm_info, tr("\n(overscan sizes are the usual defaults; Overscan prefs may differ)"));
  if (!amode_check(d, m, chip, kind, 0, sm_warn, sizeof sm_warn)) strcpy(sm_warn, tr("The picture fits this mode."));
}
int modedlg_screenmode(Doc *d, ScreenModeChoice *c)
{
  static const char *chips[4], *ovss[4];
  static const ULONG ends[] = { ID_DLGOK, ID_DLGCHANGE, 0 };
  Object *w, *list, *cc, *oc, *info, *warn, *sa;
  int i, n = amode_count(), ok = 0, mi, kind;
  ULONG id;
  if (!d) return 0;
  chips[0] = "OCS"; chips[1] = "ECS"; chips[2] = "AGA"; chips[3] = NULL;
  ovss[0] = tr("Nominal"); ovss[1] = tr("Standard Overscan"); ovss[2] = tr("Largest Overscan"); ovss[3] = NULL;
  mi = c->modeid ? amode_find(c->modeid) : -1;
  if (mi < 0) mi = amode_suggest(d, c->chipset, 1);
  if (mi < 0) mi = 0;
  kind = IK_NORMAL;
  w = dialog_window("Amiga Screen Mode", VGroup,
        Child, ListviewObject, MUIA_CycleChain, 1,
          MUIA_Listview_List, list = ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
            MUIA_List_DestructHook, MUIV_List_DestructHook_String, MUIA_FixHeightTxt, (ULONG)"\n\n\n\n\n\n\n\n\n\n", End,
        End,
        Child, ColGroup(2),
          Child, QLabel(tr("Chipset:"), MUIO_Label_Centered), Child, cc = cycle(chips, c->chipset),
          Child, QLabel(tr("Overscan:"), MUIO_Label_Centered), Child, oc = cycle(ovss, c->overscan),
          Child, sa = check(c->set_aspect), Child, QLabel(tr("Give the picture the mode's pixel aspect"), 0),
        End,
        Child, info = TextObject, TextFrame, MUIA_Text_Contents, (ULONG)"", MUIA_FixHeightTxt, (ULONG)"\n\n\n\n", End,
        Child, QTitleObject(tr("Warnings")),
        Child, warn = TextObject, MUIA_Text_Contents, (ULONG)"", MUIA_FixHeightTxt, (ULONG)"\n\n\n\n\n", End,
      End, NULL);
  if (!w) return 0;
  for (i = 0; i < n && i < SM_ROWS; i++) {
    const AmigaMode *m = amode_get(i);
    sprintf(sm_rows[i], "%-32s %4d x %-4d  %s", m->name, (int)m->w, (int)m->h, m->chipset == AC_AGA ? "AGA" : m->chipset == AC_ECS ? "ECS" : "OCS");
    DoMethod(list, MUIM_List_InsertSingle, (ULONG)sm_rows[i], MUIV_List_Insert_Bottom);
  }
  set(list, MUIA_List_Active, mi);
  notify_change(list, MUIA_List_Active);
  notify_change(cc, MUIA_Cycle_Active);
  notify_change(oc, MUIA_Cycle_Active);
  sm_describe(d, mi, c->chipset, c->overscan, kind);
  set(info, MUIA_Text_Contents, (ULONG)sm_info); set(warn, MUIA_Text_Contents, (ULONG)sm_warn);
  while ((id = run_modal(w, ends, NULL)) == ID_DLGCHANGE) {
    LONG a = (LONG)xget(list, MUIA_List_Active);
    if (a >= 0) mi = (int)a;
    sm_describe(d, mi, (int)xget(cc, MUIA_Cycle_Active), (int)xget(oc, MUIA_Cycle_Active), kind);
    set(info, MUIA_Text_Contents, (ULONG)sm_info); set(warn, MUIA_Text_Contents, (ULONG)sm_warn);
  }
  if (id == ID_DLGOK) {
    const AmigaMode *m = amode_get(mi);
    if (m) {
      c->mode = mi; c->modeid = m->modeid;
      c->chipset = (int)xget(cc, MUIA_Cycle_Active); c->overscan = (int)xget(oc, MUIA_Cycle_Active);
      amode_size(m, c->overscan, &c->w, &c->h);
      c->aspect_x = m->ax; c->aspect_y = m->ay;
      c->set_aspect = (int)xget(sa, MUIA_Selected);
      ok = 1;
    }
  }
  MUI_DisposeObject(w);
  return ok;
}
int modedlg_apply_screenmode(Doc *d, const ScreenModeChoice *c)
{
  if (!d) return 0;
  doc_begin(d, "Screen Mode");
  doc_note_pal(d);                                           /* (ModeID and aspect are kept with the palette) */
  d->modeid = c->modeid;
  if (c->set_aspect) { d->aspect_x = c->aspect_x; d->aspect_y = c->aspect_y; }
  doc_end(d);
  return 1;
}

/* ================================================================== ILBM save options */
static char il_warn[1024];
static void il_describe(Doc *d, int kind, int planes, Object *t)
{
  int mi = d->modeid ? amode_find(d->modeid) : -1;
  il_warn[0] = 0;
  if (mi >= 0) {
    const AmigaMode *m = amode_get(mi);
    int n = amode_check(d, m, (kind == IK_HAM8 || planes > 6) ? AC_AGA : AC_ECS, kind, planes, il_warn, sizeof il_warn);
    if (!n) sprintf(il_warn, "%s %s.", tr("Fits"), m->name);
  } else if (kind == IK_NORMAL && (d->mode == MODE_INDEXED || d->mode == MODE_BITMAP))
    sprintf(il_warn, "%s %d %s.", tr("The palette and its indices are written exactly:"), d->npal, tr("colours"));
  else if (kind == IK_NORMAL) strcpy(il_warn, tr("The picture will be reduced to the colours the planes hold."));
  else strcpy(il_warn, tr("No screen mode chosen: CAMG follows the picture's size."));
  set(t, MUIA_Text_Contents, (ULONG)il_warn);
}
int modedlg_ilbm(Doc *d, IlbmOpts *o)
{
  /* in IK_ order */
  static const char *kinds[IK_N + 1], *masks[4], *dits[DI_N + 1];
  static const ULONG ends[] = { ID_DLGOK, ID_DLGCHANGE, 0 };
  Object *w, *kc, *pl, *au, *mc, *tr_, *cy, *dc, *cp, *warn;
  int ok = 0, tp;
  ULONG id;
  if (!d) return 0;
  kinds[IK_NORMAL] = tr("Normal (bitplanes)"); kinds[IK_EHB] = tr("Extra Half-Brite (64)"); kinds[IK_HAM6] = "HAM6"; kinds[IK_HAM8] = tr("HAM8 (AGA)");
  kinds[IK_DEEP24] = tr("24-bit"); kinds[IK_SHAM] = tr("SHAM (HAM6, a palette per line)"); kinds[IK_N] = NULL;
  masks[0] = tr("None"); masks[1] = tr("Mask Plane"); masks[2] = tr("Transparent Colour"); masks[3] = NULL;
  labels(dither_names, dits, DI_N + 1);
  tp = o->transparent;
  w = dialog_window("ILBM Options", VGroup,
        Child, ColGroup(2),
          Child, QLabel(tr("Picture:"), MUIO_Label_Centered), Child, kc = cycle(kinds, o->kind >= 0 && o->kind < IK_N ? o->kind : IK_NORMAL),
          Child, QLabel(tr("Planes:"), MUIO_Label_Centered), Child, HGroup, Child, pl = slider(1, 8, o->planes > 0 ? o->planes : 8),
            Child, au = check(o->planes <= 0), Child, QLabel(tr("As the palette needs"), 0), End,
          Child, QLabel(tr("Mask:"), MUIO_Label_Centered), Child, mc = cycle(masks, o->mask),
          Child, QLabel(tr("Transparent entry:"), MUIO_Label_Centered), Child, tr_ = slider(-1, 255, tp),
          Child, QLabel(tr("Dither:"), MUIO_Label_Centered), Child, dc = cycle(dits, o->dither),
          Child, cy = check(o->cycles), Child, QLabel(tr("Include colour cycling (CRNG)"), 0),
          Child, cp = check(o->compress), Child, QLabel(tr("Compress (ByteRun1)"), 0),
        End,
        Child, warn = TextObject, TextFrame, MUIA_Text_Contents, (ULONG)"", MUIA_FixHeightTxt, (ULONG)"\n\n\n\n", MUIA_Font, MUIV_Font_Tiny, End,
      End, "Save");
  if (!w) return 0;
  notify_change(kc, MUIA_Cycle_Active);
  notify_change(pl, MUIA_Numeric_Value);
  notify_change(au, MUIA_Selected);
  il_describe(d, o->kind, o->planes, warn);
  while ((id = run_modal(w, ends, NULL)) == ID_DLGCHANGE)
    il_describe(d, (int)xget(kc, MUIA_Cycle_Active), xget(au, MUIA_Selected) ? 0 : (int)xget(pl, MUIA_Numeric_Value), warn);
  if (id == ID_DLGOK) {
    o->kind = (int)xget(kc, MUIA_Cycle_Active);
    o->planes = xget(au, MUIA_Selected) ? 0 : (int)xget(pl, MUIA_Numeric_Value);
    o->mask = (int)xget(mc, MUIA_Cycle_Active);
    o->transparent = (int)(LONG)xget(tr_, MUIA_Numeric_Value);
    o->dither = (int)xget(dc, MUIA_Cycle_Active);
    o->cycles = (int)xget(cy, MUIA_Selected);
    o->compress = (int)xget(cp, MUIA_Selected);
    if (!o->modeid) o->modeid = d->modeid;
    ok = 1;
  }
  MUI_DisposeObject(w);
  return ok;
}
