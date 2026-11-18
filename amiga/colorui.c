/*
 * colorui.c - Easel 1.2: colour management and pixel aspect in the interface (see colorui.h). The
 * work is done in the core (core/cms.c, core/colorx.c, core/aspect.c); this file asks what is
 * wanted, through requesters and dlgx.h's slider dialog, and keeps the view settings.
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <libraries/mui.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "dlgx.h"
#include "lang.h"
#include "brushwin.h"
#include "colorui.h"
#include "../core/aspect.h"
#include "../core/colorx.h"
#include "../core/type.h"

ViewSpec view_spec = { PRF_CMYK, 0, 0, CI_RELATIVE, 1, 0, NULL };
int view_spec_gen;
static IccProfile *work_cmyk;          /* the working CMYK profile loaded, or NULL */
static char work_cmyk_name[64];
static char prof_path[512];            /* where the last profile was chosen */
static CmsXform *vx;                   /* the canvas's view transform, and what it was made for */
static const Doc *vx_doc; static const u8 *vx_icc; static int vx_gen = -1, vx_none;

const IccProfile *colorui_cmyk(void) { return work_cmyk; }
static void changed(void) { view_spec_gen++; DoMethod(canvas, MUIM_Canvas_Refresh); }

const CmsXform *colorui_view(Doc *doc)
{
  if (!doc) return NULL;
  if (vx_doc == doc && vx_icc == doc->icc && vx_gen == view_spec_gen) return vx_none ? NULL : vx;
  cms_xform_free(vx); vx = NULL;
  {
    IccProfile *p = colorx_doc_profile(doc);
    view_spec.cmyk = work_cmyk;
    vx = cms_view_new(p && icc_space(p) == ICC_RGB ? p : NULL, &view_spec);
    icc_free(p);
  }
  vx_doc = doc; vx_icc = doc->icc; vx_gen = view_spec_gen; vx_none = vx == NULL;
  return vx;
}
void colorui_view_free(void) { cms_xform_free(vx); vx = NULL; vx_doc = NULL; vx_gen = -1; }
void colorui_free(void) { colorui_view_free(); icc_free(work_cmyk); work_cmyk = NULL; }

/* a profile file read (malloc'd bytes, *len); NULL if none was chosen or it isn't a profile */
static u8 *choose_profile(u32 *len, const char *title)
{
  size_t n;
  u8 *b;
  IccProfile *p;
  char err[80];
  if (!ui_file_request(0, title, prof_path, sizeof prof_path, "#?.(icc|icm)")) return NULL;
  if (!(b = file_read(prof_path, &n))) { ask("OK", "%s", tr("The file couldn't be read.")); return NULL; }
  if (!(p = icc_parse(b, n, err, sizeof err))) { ask("OK", "%s", tr("That isn't an ICC profile Easel can use.")); free(b); return NULL; }
  icc_free(p);
  *len = (u32)n;
  return b;
}

/* ------------------------------------------------------------------ the pixel aspect */
static void a_aspect(Doc *d, const LONG *v, int c, int t)
{
  (void)c; (void)t;
  doc_begin(d, "Pixel Aspect Ratio");
  doc_note_pal(d);
  d->aspect_x = (u8)v[0]; d->aspect_y = (u8)v[1];
  doc_end(d);
}
static void set_aspect(Doc *d, int ax, int ay)
{
  if (d->aspect_x == ax && d->aspect_y == ay) return;
  doc_begin(d, "Pixel Aspect Ratio");
  doc_note_pal(d);
  d->aspect_x = (u8)ax; d->aspect_y = (u8)ay;
  doc_end(d);
}

/* ------------------------------------------------------------------ settings */
static const char *intent_list[5];
static void intents(void) { int k; for (k = 0; k < 4; k++) intent_list[k] = tr(cms_intent_names[k]); intent_list[4] = NULL; }
static void a_settings(Doc *d, const LONG *v, int c, int t)
{
  (void)d;
  view_spec.gamma10 = (int)(v[0] < 10 ? 0 : v[0]);
  view_spec.intent = c; view_spec.bpc = t;
  changed();
}
/* Convert to Profile's target, chosen before the dialog */
static int cv_which; static u8 *cv_bytes; static u32 cv_len;
static void a_convert(Doc *d, const LONG *v, int c, int t)
{
  (void)v;
  if (!colorx_convert(d, cv_which, cv_bytes, cv_len, c, t)) ask("OK", "%s", tr("The colours couldn't be converted (not enough memory, or the profile doesn't suit the picture)."));
}

int colorui_command(Doc *d, ULONG id)
{
  if (id < ID_CU_FIRST || id > ID_CU_LAST) return 0;
  if (id >= ID_PAR0 && id < ID_PAR0 + (ULONG)aspect_count()) {
    const AspectPreset *a = aspect_get((int)(id - ID_PAR0));
    if (d && a) set_aspect(d, a->ax, a->ay);
    refresh_all();
    return 1;
  }
  switch (id) {
  case ID_PARCUSTOM: {
    static const DlgParam p[2] = { { "Pixel width:", 1, 255, 1 }, { "Pixel height:", 1, 255, 1 } };
    LONG init[2];
    if (!d) return 1;
    init[0] = d->aspect_x ? d->aspect_x : 1; init[1] = d->aspect_y ? d->aspect_y : 1;
    dlg_sliders("Custom Pixel Aspect Ratio", p, 2, NULL, NULL, 0, NULL, NULL, 0, NULL, 0, init, a_aspect, NULL, 0);
    break; }
  case ID_PARFIX: aspect_correct = !aspect_correct; DoMethod(canvas, MUIM_Canvas_Refresh); break;
  case ID_PROOF: view_spec.on = !view_spec.on; changed(); break;
  case ID_GAMUT: view_spec.gamut = !view_spec.gamut; changed(); break;
  case ID_LOADCMYK: {
    u32 len;
    u8 *b = choose_profile(&len, "Load a CMYK profile");
    IccProfile *p = b ? icc_parse(b, len, NULL, 0) : NULL;
    free(b);
    if (p && icc_space(p) != ICC_CMYK) { ask("OK", "%s", tr("That profile isn't a CMYK one.")); icc_free(p); p = NULL; }
    if (p) { icc_free(work_cmyk); work_cmyk = p; strncpy(work_cmyk_name, icc_desc(p), sizeof work_cmyk_name - 1); changed(); }
    break; }
  case ID_PRESSCMYK: icc_free(work_cmyk); work_cmyk = NULL; work_cmyk_name[0] = 0; changed(); break;
  case ID_COLORSET: {
    static const DlgParam p[1] = { { "Display gamma (tenths; under 1.0: sRGB):", 0, 30, 0 } };
    LONG init[1];
    init[0] = view_spec.gamma10;
    intents();
    dlg_sliders("Color Settings", p, 1, intent_list, "Proofing intent:", view_spec.intent, NULL, NULL, 0, "Black Point Compensation", view_spec.bpc, init, a_settings, NULL, 0);
    break; }
  case ID_ASSIGNPROF: {
    char msg[200];
    int r;
    IccProfile *cur;
    if (!d) return 1;
    cur = colorx_doc_profile(d);
    snprintf(msg, sizeof msg, tr("Assign a profile (the colour numbers are kept).\nNow: %s"), d->icc ? icc_desc(cur) : tr("none (sRGB is assumed)"));
    icc_free(cur);
    r = ask("Don't Color Manage|sRGB|Adobe RGB (1998)|Profile File...|Cancel", "%s", msg);
    if (r == 1) colorx_assign(d, CX_NONE, NULL, 0);
    else if (r == 2) colorx_assign(d, CX_SRGB, NULL, 0);
    else if (r == 3) colorx_assign(d, CX_ADOBERGB, NULL, 0);
    else if (r == 4) {
      u32 len;
      u8 *b = choose_profile(&len, "Assign a profile");
      if (b && !colorx_assign(d, CX_BYTES, b, len)) ask("OK", "%s", tr("That profile doesn't suit the picture's mode."));
      free(b);
    }
    break; }
  case ID_CONVERTPROF: {
    int r;
    if (!d) return 1;
    if (d->mode == MODE_GRAY) { ask("OK", "%s", tr("Grayscale pictures can only be assigned a profile.")); return 1; }
    r = ask("sRGB|Adobe RGB (1998)|Profile File...|Cancel", "%s", tr("Convert the picture's colours into which profile?"));
    cv_bytes = NULL; cv_len = 0;
    if (r == 1) cv_which = CX_SRGB;
    else if (r == 2) cv_which = CX_ADOBERGB;
    else if (r == 3) { cv_which = CX_BYTES; if (!(cv_bytes = choose_profile(&cv_len, "Convert to a profile"))) return 1; }
    else return 1;
    intents();
    dlg_sliders("Convert to Profile", NULL, 0, intent_list, "Intent:", CI_RELATIVE, NULL, NULL, 0, "Black Point Compensation", 1, NULL, a_convert, NULL, 0);
    free(cv_bytes); cv_bytes = NULL;
    break; }
  case ID_MODE_CMYK:
    if (!d || (d->mode == MODE_RGB && d->cmode == CM_CMYK)) return 1;
    job_begin(d, "CMYK Color");
    if (!colorx_convert_model(d, CM_CMYK, work_cmyk)) ask("OK", "%s", tr("Not enough memory to convert the picture."));
    job_end();
    break;
  case ID_MODE_LAB:
    if (!d || (d->mode == MODE_RGB && d->cmode == CM_LAB)) return 1;
    colorx_convert_model(d, CM_LAB, NULL);
    break;
  default:
    if (id >= ID_OT0 && id < ID_OT0 + TO_NFEAT) { if (d) DoMethod(canvas, MUIM_Canvas_TypeCmd, TC_OT, (ULONG)(TO_LIGA << (id - ID_OT0))); refresh_panels(); return 1; }
    if (id >= ID_PROOFSET0 + PRF_CMYK && id <= ID_PROOFSET0 + PRF_DEUTAN) { view_spec.proof = (int)(id - ID_PROOFSET0); view_spec.on = 1; changed(); }
    break;
  }
  refresh_all();
  return 1;
}

void colorui_menus(Doc *d, void (*menu_set)(ULONG id, ULONG attr, ULONG val))
{
  int i, k = d ? aspect_find(d->aspect_x, d->aspect_y) : -1;
  for (i = 0; i < aspect_count(); i++) { menu_set(ID_PAR0 + i, MUIA_Menuitem_Checked, k == i); menu_set(ID_PAR0 + i, MUIA_Menuitem_Enabled, d != NULL); }
  menu_set(ID_PARCUSTOM, MUIA_Menuitem_Checked, d && k < 0);
  menu_set(ID_PARFIX, MUIA_Menuitem_Checked, aspect_correct);
  menu_set(ID_PROOF, MUIA_Menuitem_Checked, view_spec.on);
  menu_set(ID_GAMUT, MUIA_Menuitem_Checked, view_spec.gamut);
  for (i = PRF_CMYK; i <= PRF_DEUTAN; i++) menu_set(ID_PROOFSET0 + i, MUIA_Menuitem_Checked, view_spec.proof == i);
  menu_set(ID_PRESSCMYK, MUIA_Menuitem_Checked, work_cmyk == NULL);
  {
    /* the active type layer's features (its first run's) */
    Layer *al = d && d->active >= 0 && d->active < d->nlay ? d->lay[d->active] : NULL;
    const TextParams *tp = al && al->kind == LK_TEXT ? (const TextParams *)al->live : NULL;
    for (i = 0; i < TO_NFEAT; i++) {
      menu_set(ID_OT0 + i, MUIA_Menuitem_Checked, tp && (tp->run[0].ot & (TO_LIGA << i)));
      menu_set(ID_OT0 + i, MUIA_Menuitem_Enabled, tp != NULL || type_editing);
    }
  }
  menu_set(ID_ASSIGNPROF, MUIA_Menuitem_Enabled, d != NULL);
  menu_set(ID_CONVERTPROF, MUIA_Menuitem_Enabled, d != NULL);
  menu_set(ID_MODE_CMYK, MUIA_Menuitem_Enabled, d != NULL);
  menu_set(ID_MODE_LAB, MUIA_Menuitem_Enabled, d != NULL);
}
