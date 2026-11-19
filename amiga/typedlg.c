/*
 * typedlg.c - the dialogs of Easel 0.9's type and shape layers: Character (font, size, leading,
 * tracking, baseline shift, colour, underline, strikethrough, faux bold and italic), Paragraph
 * (alignment with justification, indent, space before and after, kerning), Warp Text (the style,
 * its bend and distortions) and Shape Properties (fill, stroke, the shape's own settings and its
 * place). Each changes the settings it is given; the caller makes the undo step.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "../core/type.h"
#include "../core/shape.h"
#include "r3ids.h"

static ULONG xget(Object *o, ULONG a) { ULONG v = 0; get(o, a, &v); return v; }
/* the small controls the dialogs are made of, as dialogs.c makes them */
static Object *slider(LONG min, LONG max, LONG v)
{
  return SliderObject, MUIA_Numeric_Min, min, MUIA_Numeric_Max, max, MUIA_Numeric_Value, v, MUIA_CycleChain, 1, End;
}
/* a number field (decimals allowed) holding v */
static Object *numfield(double v)
{
  char buf[32];
  long t = (long)floor(fabs(v) * 100 + 0.5);
  if (t % 100) sprintf(buf, "%s%ld.%02ld", v < 0 ? "-" : "", t / 100, t % 100);
  else sprintf(buf, "%s%ld", v < 0 && t ? "-" : "", t / 100);
  return StringObject, StringFrame, MUIA_String_Contents, (ULONG)buf, MUIA_String_Accept, (ULONG)"0123456789.-", MUIA_String_MaxLen, 14,
    MUIA_CycleChain, 1, MUIA_FixWidthTxt, (ULONG)"0000000", End;
}
static Object *cycle(const char **labels, int active)
{
  return CycleObject, MUIA_Cycle_Entries, (ULONG)tr_list((const char *const *)labels), MUIA_Cycle_Active, active, MUIA_CycleChain, 1, End;   /* (1.1: in the language chosen) */
}
static Object *check(int on)
{
  return ImageObject, ImageButtonFrame, MUIA_InputMode, MUIV_InputMode_Toggle, MUIA_Image_Spec, MUII_CheckMark,
    MUIA_Background, MUII_ButtonBack, MUIA_ShowSelState, FALSE, MUIA_Selected, on, MUIA_CycleChain, 1, End;
}
/* a colour as a hex field, and a button beside it that opens the colour picker */
static Object *hexfield(ULONG c)
{
  char buf[12];
  sprintf(buf, "%06lX", (unsigned long)(c & 0xFFFFFF));
  return StringObject, StringFrame, MUIA_String_Contents, (ULONG)buf, MUIA_String_MaxLen, 8, MUIA_String_Accept, (ULONG)"0123456789abcdefABCDEF",
    MUIA_CycleChain, 1, MUIA_FixWidthTxt, (ULONG)"0000000", End;
}
static double num_of(Object *s) { return atof((const char *)xget(s, MUIA_String_Contents)); }
static ULONG hex_of(Object *s) { return strtoul((const char *)xget(s, MUIA_String_Contents), NULL, 16) & 0xFFFFFF; }
static int sel_of(Object *o) { return (int)xget(o, MUIA_Selected); }
static int cyc_of(Object *o) { return (int)xget(o, MUIA_Cycle_Active); }
/* translated labels for a cycle (kept in buf, NULL ended) */
static const char **labels_of(const char *const *src, int n, const char **buf)
{
  int i;
  for (i = 0; i < n; i++) buf[i] = tr(src[i]);
  buf[n] = NULL;
  return buf;
}

/* the Pick buttons: the hex field they belong to, and which picker title */
static Object *pick_target[2];
/* (run_modal's each) a Pick button pressed: the colour picker, its choice put in the field */
static void pick_each(ULONG id)
{
  int k = id == ID_DLGRESET ? 0 : id == ID_DLGAUTO ? 1 : -1;
  LONG c;
  char buf[12];
  if (k < 0 || !pick_target[k]) return;
  if ((c = pick_color(k ? "Stroke Color" : "Color", hex_of(pick_target[k]))) < 0) return;
  sprintf(buf, "%06lX", (unsigned long)c);
  set(pick_target[k], MUIA_String_Contents, (ULONG)buf);
}
static Object *pick_button(ULONG id)
{
  Object *b = QButton(tr("Pick..."));
  if (b) DoMethod(b, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, id);
  return b;
}

/* ------------------------------------------------------------------ Character */
/* the real bold or italic face of a family when it has one, else the faux style */
static void run_style(TextRun *r, const char *family, int style)
{
  int wb = (style & 1) != 0, wi = (style & 2) != 0;
  Face *f = text_face_lookup && (wb || wi) ? text_face_lookup(family, wb, wi) : NULL;
  int rb = wb && f && f->bold && !Stricmp((STRPTR)f->family, (STRPTR)family);
  int ri = wi && f && f->italic && !Stricmp((STRPTR)f->family, (STRPTR)family);
  strncpy(r->family, family, sizeof r->family - 1); r->family[sizeof r->family - 1] = 0;
  r->bold = (u8)rb; r->faux_bold = (u8)(wb && !rb);
  r->italic = (u8)ri; r->faux_italic = (u8)(wi && !ri);
}
int dlg_character(struct TextParams *tp, int from, int to)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *const styles[] = { "Regular", "Bold", "Italic", "Bold Italic" };
  static const char *fonts[100], *stl[5], *csl[4], *psl[4];
  static const char *const cases[] = { "Normal", "All Caps", "Small Caps" }, *const positions[] = { "Normal", "Superscript", "Subscript" };   /* 1.2 */
  const TextRun *r0 = &tp->run[text_run_at(tp, from < to ? from + 1 : from)];
  TextRun r;
  Object *w, *fc, *sc, *sz, *ld, *tk, *bs, *col, *ul, *st, *fb, *fi, *cc, *pc, *hsc, *vsc;
  int nf = font_count(), i, f0 = font_find(r0->family), s0, ok;
  u32 m = 0;
  if (nf > 99) nf = 99;
  for (i = 0; i < nf; i++) fonts[i] = font_label(i);
  fonts[nf] = NULL;
  if (!nf) { fonts[0] = r0->family; fonts[1] = NULL; f0 = 0; }
  if (f0 < 0) f0 = 0;
  s0 = ((r0->bold || r0->faux_bold) ? 1 : 0) | ((r0->italic || r0->faux_italic) ? 2 : 0);
  w = dialog_window("Character", ColGroup(2),
        Child, QLabel(tr("Font:"), MUIO_Label_Centered), Child, fc = cycle(fonts, f0),
        Child, QLabel(tr("Style:"), MUIO_Label_Centered), Child, sc = cycle(labels_of(styles, 4, stl), s0),
        Child, QLabel(tr("Size (px):"), MUIO_Label_SingleFrame), Child, HGroup, Child, sz = numfield(r0->size), Child, HSpace(0), End,
        Child, QLabel(tr("Leading (px, 0 auto):"), MUIO_Label_SingleFrame), Child, HGroup, Child, ld = numfield(tp->leading), Child, HSpace(0), End,
        Child, QLabel(tr("Tracking (1/1000 em):"), MUIO_Label_SingleFrame), Child, HGroup, Child, tk = numfield(r0->tracking), Child, HSpace(0), End,
        Child, QLabel(tr("Baseline Shift (px):"), MUIO_Label_SingleFrame), Child, HGroup, Child, bs = numfield(r0->baseline), Child, HSpace(0), End,
        Child, QLabel(tr("Horizontal, Vertical Scale (%):"), MUIO_Label_SingleFrame),        /* r3 */
        Child, HGroup, Child, hsc = numfield(text_hscale(r0) * 100), Child, vsc = numfield(text_vscale(r0) * 100), Child, HSpace(0), End,
        Child, QLabel(tr("Case:"), MUIO_Label_Centered), Child, cc = cycle(labels_of(cases, 3, csl), r0->ot & TO_CASE),
        Child, QLabel(tr("Position:"), MUIO_Label_Centered), Child, pc = cycle(labels_of(positions, 3, psl), (r0->ot & TO_POS) >> 2),
        Child, QLabel(tr("Color (hex #):"), MUIO_Label_SingleFrame), Child, HGroup, Child, col = hexfield(r0->color), Child, pick_button(ID_DLGRESET), Child, HSpace(0), End,
        Child, ul = check(r0->underline), Child, QLabel(tr("Underline"), 0),
        Child, st = check(r0->strike), Child, QLabel(tr("Strikethrough"), 0),
        Child, fb = check(r0->faux_bold), Child, QLabel(tr("Faux Bold"), 0),
        Child, fi = check(r0->faux_italic), Child, QLabel(tr("Faux Italic"), 0),
      End, NULL);
  if (!w) return 0;
  pick_target[0] = col; pick_target[1] = NULL;
  ok = run_modal(w, ends, pick_each) == ID_DLGOK;
  if (ok) {
    double v;
    memset(&r, 0, sizeof r);
    if (cyc_of(fc) != f0 || cyc_of(sc) != s0) {
      run_style(&r, nf ? font_name(cyc_of(fc)) : r0->family, cyc_of(sc));
      m |= TS_BOLD | TS_ITALIC | TS_FAUXBOLD | TS_FAUXITALIC;
      if (cyc_of(fc) != f0) m |= TS_FAMILY;
    } else { r.bold = r0->bold; r.italic = r0->italic; r.faux_bold = r0->faux_bold; r.faux_italic = r0->faux_italic; }
    if (sel_of(fb) != r0->faux_bold) { r.faux_bold = (u8)sel_of(fb); m |= TS_FAUXBOLD; }
    if (sel_of(fi) != r0->faux_italic) { r.faux_italic = (u8)sel_of(fi); m |= TS_FAUXITALIC; }
    if ((v = num_of(sz)) >= 1 && v <= 2000 && fabs(v - r0->size) > 0.004) { r.size = v; m |= TS_SIZE; }
    if (fabs((v = num_of(tk)) - r0->tracking) > 0.004) { r.tracking = v; m |= TS_TRACKING; }
    if (fabs((v = num_of(bs)) - r0->baseline) > 0.004) { r.baseline = v; m |= TS_BASELINE; }
    if ((v = num_of(hsc)) >= 1 && v <= 1000 && fabs(v - text_hscale(r0) * 100) > 0.004) { r.hscale = v; m |= TS_HSCALE; }     /* r3 */
    if ((v = num_of(vsc)) >= 1 && v <= 1000 && fabs(v - text_vscale(r0) * 100) > 0.004) { r.vscale = v; m |= TS_VSCALE; }
    if (hex_of(col) != r0->color) { r.color = hex_of(col); m |= TS_COLOR; }
    if (sel_of(ul) != r0->underline) { r.underline = (u8)sel_of(ul); m |= TS_UNDERLINE; }
    if (sel_of(st) != r0->strike) { r.strike = (u8)sel_of(st); m |= TS_STRIKE; }
    if ((v = num_of(ld)) >= 0 && v < 5000) tp->leading = v;
    /* 1.2: the case and position, only where they were changed (the runs keep their own features) */
    if (cyc_of(cc) != (int)(r0->ot & TO_CASE) || (cyc_of(pc) << 2) != (int)(r0->ot & TO_POS)) {
      u16 bits = 0, val = (u16)(cyc_of(cc) | cyc_of(pc) << 2);
      if (cyc_of(cc) != (int)(r0->ot & TO_CASE)) bits |= TO_CASE;
      if ((cyc_of(pc) << 2) != (int)(r0->ot & TO_POS)) bits |= TO_POS;
      text_ot_set(tp, from, to, bits, val);
    }
    if (m) {
      if (from >= to) { from = 0; to = (int)strlen(tp->text); }
      if (to > from) text_style(tp, from, to, &r, m);
      else {
        /* no text yet: the style the first character will have */
        TextRun *a = &tp->run[0];
        if (m & TS_FAMILY) strcpy(a->family, r.family);
        if (m & TS_BOLD) { a->bold = r.bold; a->italic = r.italic; }
        if (m & TS_FAUXBOLD) a->faux_bold = r.faux_bold;
        if (m & TS_FAUXITALIC) a->faux_italic = r.faux_italic;
        if (m & TS_SIZE) a->size = r.size;
        if (m & TS_TRACKING) a->tracking = r.tracking;
        if (m & TS_BASELINE) a->baseline = r.baseline;
        if (m & TS_HSCALE) a->hscale = r.hscale;
        if (m & TS_VSCALE) a->vscale = r.vscale;
        if (m & TS_COLOR) a->color = r.color;
        if (m & TS_UNDERLINE) a->underline = r.underline;
        if (m & TS_STRIKE) a->strike = r.strike;
      }
    }
  }
  pick_target[0] = NULL;
  MUI_DisposeObject(w);
  return ok;
}

/* ------------------------------------------------------------------ Paragraph */
int dlg_paragraph(struct TextParams *tp)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *const aligns[] = { "Left", "Centre", "Right", "Justify (last line left)", "Justify All",
                                        "Justify (last line centred)", "Justify (last line right)" };   /* (1.2: the last two) */
  static const char *al[8];
  Object *w, *ac, *ind, *sb, *sa, *kc, *bw = NULL, *bh = NULL, *grp, *il, *ir, *hy;
  int ok;
  grp = ColGroup(2),
        Child, QLabel(tr("Align:"), MUIO_Label_Centered), Child, ac = cycle(labels_of(aligns, 7, al), tp->align < 7 ? tp->align : 0),
        Child, QLabel(tr("First Line Indent (px):"), MUIO_Label_SingleFrame), Child, HGroup, Child, ind = numfield(tp->indent), Child, HSpace(0), End,
        Child, QLabel(tr("Left Indent (px):"), MUIO_Label_SingleFrame), Child, HGroup, Child, il = numfield(tp->indent_left), Child, HSpace(0), End,
        Child, QLabel(tr("Right Indent (px):"), MUIO_Label_SingleFrame), Child, HGroup, Child, ir = numfield(tp->indent_right), Child, HSpace(0), End,
        Child, QLabel(tr("Space Before (px):"), MUIO_Label_SingleFrame), Child, HGroup, Child, sb = numfield(tp->space_before), Child, HSpace(0), End,
        Child, QLabel(tr("Space After (px):"), MUIO_Label_SingleFrame), Child, HGroup, Child, sa = numfield(tp->space_after), Child, HSpace(0), End,
        Child, kc = check(tp->kern), Child, QLabel(tr("Kerning from the font (Metrics)"), 0),
        Child, hy = check(tp->hyphenate), Child, QLabel(tr("Hyphenate (paragraph text)"), 0),     /* r3 */
      End;
  if (grp && tp->box) {
    /* paragraph text: its box too */
    DoMethod(grp, OM_ADDMEMBER, (ULONG)QLabel(tr("Box Width (px):"), MUIO_Label_SingleFrame));
    DoMethod(grp, OM_ADDMEMBER, (ULONG)(bw = numfield(tp->bw)));
    DoMethod(grp, OM_ADDMEMBER, (ULONG)QLabel(tr("Box Height (px):"), MUIO_Label_SingleFrame));
    DoMethod(grp, OM_ADDMEMBER, (ULONG)(bh = numfield(tp->bh)));
  }
  w = dialog_window("Paragraph", grp, NULL);
  if (!w) return 0;
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  if (ok) {
    tp->align = (u8)cyc_of(ac);
    tp->indent = num_of(ind); tp->space_before = num_of(sb); tp->space_after = num_of(sa);
    { double v = num_of(il); tp->indent_left = v >= 0 ? v : 0; v = num_of(ir); tp->indent_right = v >= 0 ? v : 0; }   /* 1.2 */
    tp->kern = (u8)sel_of(kc);
    tp->hyphenate = (u8)sel_of(hy);
    if (tp->hyphenate && !r3_hyphen_patterns()) ask("OK", "%s", tr("No hyphenation patterns were found: put hyph_en_GB.dic (or your language's) in Easel's Dict drawer or in Bureau:Dict."));
    if (bw && num_of(bw) >= 1) tp->bw = num_of(bw);
    if (bh && num_of(bh) >= 1) tp->bh = num_of(bh);
  }
  MUI_DisposeObject(w);
  return ok;
}

/* ------------------------------------------------------------------ Warp Text */
int dlg_warp(struct TextParams *tp)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *const hv[] = { "Horizontal", "Vertical" };
  static const char *ws[TW_N + 1], *hvl[3];
  Object *w, *wc, *hc, *bend, *dh, *dv;
  int ok;
  w = dialog_window("Warp Text", ColGroup(2),
        Child, QLabel(tr("Style:"), MUIO_Label_Centered), Child, wc = cycle(labels_of(text_warp_names, TW_N, ws), tp->warp < TW_N ? tp->warp : 0),
        Child, QLabel(tr("Direction:"), MUIO_Label_Centered), Child, hc = cycle(labels_of(hv, 2, hvl), tp->warp_vertical != 0),
        Child, QLabel(tr("Bend (%):"), MUIO_Label_Centered), Child, bend = slider(-100, 100, (LONG)floor(tp->warp_bend + 0.5)),
        Child, QLabel(tr("Horizontal Distortion (%):"), MUIO_Label_Centered), Child, dh = slider(-100, 100, (LONG)floor(tp->warp_h + 0.5)),
        Child, QLabel(tr("Vertical Distortion (%):"), MUIO_Label_Centered), Child, dv = slider(-100, 100, (LONG)floor(tp->warp_v + 0.5)),
      End, NULL);
  if (!w) return 0;
  if (tp->warp == TW_NONE && !tp->warp_bend) set(bend, MUIA_Numeric_Value, 50);
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  if (ok) {
    tp->warp = (u8)cyc_of(wc);
    tp->warp_vertical = (u8)cyc_of(hc);
    tp->warp_bend = (double)(LONG)xget(bend, MUIA_Numeric_Value);
    tp->warp_h = (double)(LONG)xget(dh, MUIA_Numeric_Value);
    tp->warp_v = (double)(LONG)xget(dv, MUIA_Numeric_Value);
  }
  MUI_DisposeObject(w);
  return ok;
}

/* ------------------------------------------------------------------ Shape Properties */
int dlg_shape(struct ShapeParams *sp)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *const aligns[] = { "Centre", "Inside", "Outside" };
  static const char *const caps[] = { "Butt", "Round", "Square" };
  static const char *const joins[] = { "Miter", "Round", "Bevel" };
  static const char *al[4], *cl[4], *jl[4];
  static const char *const kinds[SH_N] = { "Rectangle", "Rounded Rectangle", "Ellipse", "Triangle", "Polygon", "Line", "Custom Shape" };
  Object *w, *grp, *fon, *fcol, *fop, *son, *sw, *scol, *sop, *sal, *scap, *sjn, *dash, *px, *py, *pw, *ph;
  static const char *const fkinds[] = { "Color", "Gradient", "Pattern", NULL };          /* r3 */
  static const char *const gstyles[] = { "Linear", "Radial", "Angle", "Reflected", "Diamond", NULL };
  Object *fkind, *gsty, *gang, *gsc, *grev, *rad2 = NULL, *rad3 = NULL, *rad4 = NULL;
  Object *rad = NULL, *sides = NULL, *star = NULL, *ind = NULL, *ang = NULL, *lw = NULL, *as = NULL, *ae = NULL;
  char dbuf[64];
  int ok, i;
  double x0 = sp->x0 < sp->x1 ? sp->x0 : sp->x1, y0 = sp->y0 < sp->y1 ? sp->y0 : sp->y1;
  dbuf[0] = 0;
  for (i = 0; i < sp->ndash && i < 8; i++) sprintf(dbuf + strlen(dbuf), "%s%ld", i ? " " : "", (long)floor(sp->dash[i] + 0.5));
  grp = ColGroup(2),
        Child, QLabel(tr("Shape:"), MUIO_Label_Centered), Child, TextObject, MUIA_Text_Contents, (ULONG)tr(kinds[sp->kind < SH_N ? sp->kind : 0]), End,
        Child, QLabel(tr("Position X, Y (px):"), MUIO_Label_SingleFrame),
        Child, HGroup, Child, px = numfield(sp->kind == SH_LINE ? sp->x0 : x0), Child, py = numfield(sp->kind == SH_LINE ? sp->y0 : y0), Child, HSpace(0), End,
        Child, QLabel(tr(sp->kind == SH_LINE ? "To X, Y (px):" : "Width, Height (px):"), MUIO_Label_SingleFrame),
        Child, HGroup, Child, pw = numfield(sp->kind == SH_LINE ? sp->x1 : fabs(sp->x1 - sp->x0)), Child, ph = numfield(sp->kind == SH_LINE ? sp->y1 : fabs(sp->y1 - sp->y0)), Child, HSpace(0), End,
        Child, fon = check(sp->fill_on),
        Child, HGroup, Child, QLabel(tr("Fill"), 0), Child, fkind = cycle((const char **)fkinds, sp->fill_kind < SFILL_N ? sp->fill_kind : 0), Child, HSpace(0), End,
        Child, QLabel(tr("Fill Color (hex #):"), MUIO_Label_SingleFrame), Child, HGroup, Child, fcol = hexfield(sp->fill_color), Child, pick_button(ID_DLGRESET), Child, HSpace(0), End,
        Child, QLabel(tr("Fill Opacity (%):"), MUIO_Label_Centered), Child, fop = slider(0, 100, ((255 - sp->fill_alpha) * 100 + 127) / 255),
        /* r3: the gradient's style, angle, scale (the pattern's scale too) and Reverse */
        Child, QLabel(tr("Gradient:"), MUIO_Label_SingleFrame),
        Child, HGroup, Child, gsty = cycle((const char **)gstyles, sp->grad_style <= GR_DIAMOND ? sp->grad_style : 0),
          Child, gang = numfield(sp->grad_angle), Child, gsc = numfield(sp->grad_scale > 0 ? sp->grad_scale : 100),
          Child, grev = check(sp->grad_reverse), Child, QLabel(tr("Reverse"), 0), End,
        Child, son = check(sp->stroke_on), Child, QLabel(tr("Stroke"), 0),
        Child, QLabel(tr("Stroke Width (px):"), MUIO_Label_SingleFrame), Child, HGroup, Child, sw = numfield(sp->stroke_width), Child, HSpace(0), End,
        Child, QLabel(tr("Stroke Color (hex #):"), MUIO_Label_SingleFrame), Child, HGroup, Child, scol = hexfield(sp->stroke_color), Child, pick_button(ID_DLGAUTO), Child, HSpace(0), End,
        Child, QLabel(tr("Stroke Opacity (%):"), MUIO_Label_Centered), Child, sop = slider(0, 100, ((255 - sp->stroke_alpha) * 100 + 127) / 255),
        Child, QLabel(tr("Stroke Position:"), MUIO_Label_Centered), Child, sal = cycle(labels_of(aligns, 3, al), sp->align < 3 ? sp->align : 0),
        Child, QLabel(tr("Ends:"), MUIO_Label_Centered), Child, scap = cycle(labels_of(caps, 3, cl), sp->cap < 3 ? sp->cap : 0),
        Child, QLabel(tr("Corners:"), MUIO_Label_Centered), Child, sjn = cycle(labels_of(joins, 3, jl), sp->join < 3 ? sp->join : 0),
        Child, QLabel(tr("Dashes (on off ... px):"), MUIO_Label_SingleFrame),
        Child, dash = StringObject, StringFrame, MUIA_String_Contents, (ULONG)dbuf, MUIA_String_MaxLen, 60, MUIA_String_Accept, (ULONG)"0123456789. ", MUIA_CycleChain, 1, End,
      End;
  if (!grp) return 0;
  /* the shape's own settings */
#define ADD2(lab, o) (DoMethod(grp, OM_ADDMEMBER, (ULONG)QLabel(tr(lab), MUIO_Label_SingleFrame)), DoMethod(grp, OM_ADDMEMBER, (ULONG)(o)))
  if (sp->kind == SH_RECT || sp->kind == SH_ROUNDRECT) {
    /* r3: a radius per corner (top left, top right, bottom right, bottom left) */
    const double *r4 = sp->radii;
    double one[4];
    if (!sp->radii_on) { one[0] = one[1] = one[2] = one[3] = sp->radius; r4 = one; }
    Object *rg = HGroup, Child, rad = numfield(r4[0]), Child, rad2 = numfield(r4[1]), Child, rad3 = numfield(r4[2]), Child, rad4 = numfield(r4[3]), End;
    ADD2("Corner Radius (px):", rg);
  }
  if (sp->kind == SH_POLYGON) {
    ADD2("Sides:", sides = numfield(sp->sides));
    DoMethod(grp, OM_ADDMEMBER, (ULONG)(star = check(sp->star))); DoMethod(grp, OM_ADDMEMBER, (ULONG)QLabel(tr("Star"), 0));
    ADD2("Star Indent (%):", ind = numfield(sp->indent));
    ADD2("Angle (degrees):", ang = numfield(sp->angle));
  }
  if (sp->kind == SH_LINE) {
    ADD2("Line Weight (px):", lw = numfield(sp->line_width));
    DoMethod(grp, OM_ADDMEMBER, (ULONG)(as = check(sp->arrow_start))); DoMethod(grp, OM_ADDMEMBER, (ULONG)QLabel(tr("Arrowhead at the start"), 0));
    DoMethod(grp, OM_ADDMEMBER, (ULONG)(ae = check(sp->arrow_end))); DoMethod(grp, OM_ADDMEMBER, (ULONG)QLabel(tr("Arrowhead at the end"), 0));
  }
#undef ADD2
  w = dialog_window("Shape Properties", grp, NULL);
  if (!w) return 0;
  pick_target[0] = fcol; pick_target[1] = scol;
  ok = run_modal(w, ends, pick_each) == ID_DLGOK;
  if (ok) {
    double a = num_of(px), b = num_of(py), c = num_of(pw), e = num_of(ph);
    const char *ds = (const char *)xget(dash, MUIA_String_Contents);
    if (sp->kind == SH_LINE) { sp->x0 = a; sp->y0 = b; sp->x1 = c; sp->y1 = e; }
    else if (c >= 1 && e >= 1 && sp->kind != SH_CUSTOM) { sp->x0 = a; sp->y0 = b; sp->x1 = a + c; sp->y1 = b + e; }
    sp->fill_on = (u8)sel_of(fon); sp->fill_color = hex_of(fcol);
    sp->fill_alpha = (u8)(255 - (xget(fop, MUIA_Numeric_Value) * 255 + 50) / 100);
    sp->stroke_on = (u8)sel_of(son); sp->stroke_color = hex_of(scol);
    if (num_of(sw) > 0) sp->stroke_width = num_of(sw);
    sp->stroke_alpha = (u8)(255 - (xget(sop, MUIA_Numeric_Value) * 255 + 50) / 100);
    sp->align = (u8)cyc_of(sal); sp->cap = (u8)cyc_of(scap); sp->join = (u8)cyc_of(sjn);
    for (sp->ndash = 0; *ds && sp->ndash < 8; ) {
      char *end;
      double v = strtod(ds, &end);
      if (end == ds) break;
      if (v > 0) sp->dash[sp->ndash++] = v;
      ds = end;
      while (*ds == ' ') ds++;
    }
    if (sp->ndash == 1) sp->dash[sp->ndash++] = sp->dash[0];      /* (one number: as long on as off) */
    if (rad && rad2 && rad3 && rad4) {
      double r[4];
      int k;
      r[0] = num_of(rad); r[1] = num_of(rad2); r[2] = num_of(rad3); r[3] = num_of(rad4);
      for (k = 0; k < 4; k++) if (r[k] < 0) r[k] = 0;
      if (r[0] == r[1] && r[1] == r[2] && r[2] == r[3]) { sp->radius = r[0]; sp->radii_on = 0; }
      else { memcpy(sp->radii, r, sizeof r); sp->radii_on = 1; }
    }
    {
      /* r3: the fill: a gradient starts from the Gradient tool's (main.c takes the pattern) */
      int fk = cyc_of(fkind);
      double sc = num_of(gsc);
      if (fk == SFILL_GRADIENT && (sp->fill_kind != SFILL_GRADIENT || !sp->grad_nc)) shape_set_gradient(sp, tool_gradient(), cyc_of(gsty), num_of(gang), sc, sel_of(grev));
      sp->fill_kind = (u8)fk;
      sp->grad_style = (u8)cyc_of(gsty); sp->grad_angle = num_of(gang); sp->grad_scale = sc >= 1 && sc <= 1000 ? sc : 100; sp->grad_reverse = (u8)sel_of(grev);
    }
    if (sides) { int n = (int)num_of(sides); sp->sides = n < 3 ? 3 : n > 100 ? 100 : n; }
    if (star) sp->star = (u8)sel_of(star);
    if (ind) { double v = num_of(ind); sp->indent = v < 0 ? 0 : v > 99 ? 99 : v; }
    if (ang) sp->angle = num_of(ang);
    if (lw && num_of(lw) > 0) sp->line_width = num_of(lw);
    if (as) sp->arrow_start = (u8)sel_of(as);
    if (ae) sp->arrow_end = (u8)sel_of(ae);
  }
  pick_target[0] = pick_target[1] = NULL;
  MUI_DisposeObject(w);
  return ok;
}
