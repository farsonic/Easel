/*
 * menux3.c - Easel 1.3's menu, tool and automation additions on the Amiga side (round 3 of the
 * PhotoCraft gap work): File > Open As, Export > Save for Web (Legacy) (with its 2-up preview, the
 * "Save for Screen" of ADPro's tradition), Export Picked Layers to Files, Color Lookup Tables,
 * Export Preferences, Generate > Image Assets, Scripts > Image Processor and Statistics, Automate >
 * Contact Sheet II, Import > Grab Screen; Edit > Find and Replace Text, Reset Tool and Reset All
 * Tools; Image > Apply Image, Calculations and Analysis; Layer > New Layer Based Slice, Artboard and
 * Frame from Layers; View > Pixel Art Preview, Flip Horizontal, Fit Artboard on Screen, Snap To >
 * Layers and Slices, Lock and Clear Slices; and File > New's presets, units, modes and pixel aspect.
 * The work is in core/menu3.c; main.c hands the IDs in m3ids.h here.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <graphics/gfx.h>
#include <graphics/view.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <math.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "m3ids.h"
#include "mxids.h"
#include "tools2ids.h"
#include "clipboard.h"
#include "../core/menu3.h"
#include "../core/viewx.h"
#include "../core/layers.h"
#include "../core/layerx.h"
#include "../core/layerops.h"
#include "../core/toolsx.h"
#include "../core/palette.h"
#include "../core/exportx.h"
#include "../core/batch.h"
#include "../core/guides.h"

int m3_pixelart, m3_flip, m3_slices_locked;
extern struct IntuitionBase *IntuitionBase;
/* cybergraphics.library's ReadPixelArray, as a vbcc inline call (the library is optional) */
ULONG __cgx_ReadPixelArray(__reg("a6") void *, __reg("a0") APTR, __reg("d0") UWORD, __reg("d1") UWORD, __reg("d2") UWORD,
                           __reg("a1") struct RastPort *, __reg("d3") UWORD, __reg("d4") UWORD, __reg("d5") UWORD,
                           __reg("d6") UWORD, __reg("d7") UBYTE) = "\tjsr\t-120(a6)";
#define ReadPixelArray(dst, dx, dy, mod, rp, sx, sy, w, h, fmt) __cgx_ReadPixelArray(CyberGfxBase, (dst), (dx), (dy), (mod), (rp), (sx), (sy), (w), (h), (fmt))
#define RECTFMT_RGB 0

static ULONG xget(Object *o, ULONG a) { ULONG v = 0; get(o, a, &v); return v; }
static Object *slider(LONG min, LONG max, LONG v) { return SliderObject, MUIA_Numeric_Min, min, MUIA_Numeric_Max, max, MUIA_Numeric_Value, v, MUIA_CycleChain, 1, End; }
static Object *check(int on)
{
  return ImageObject, ImageButtonFrame, MUIA_InputMode, MUIV_InputMode_Toggle, MUIA_Image_Spec, MUII_CheckMark,
    MUIA_Background, MUII_ButtonBack, MUIA_ShowSelState, FALSE, MUIA_Selected, on, MUIA_CycleChain, 1, End;
}
static Object *cycle(const char *const *labels, int active) { return CycleObject, MUIA_Cycle_Entries, (ULONG)tr_list(labels), MUIA_Cycle_Active, active, MUIA_CycleChain, 1, End; }
static Object *cycle_raw(const char **labels, int active) { return CycleObject, MUIA_Cycle_Entries, (ULONG)labels, MUIA_Cycle_Active, active, MUIA_CycleChain, 1, End; }
static Object *label(const char *s) { return QLabel(tr(s), MUIO_Label_Centered); }
static Object *numstr(LONG v)
{
  char b[16];
  sprintf(b, "%ld", (long)v);
  return StringObject, StringFrame, MUIA_String_Contents, (ULONG)b, MUIA_String_Accept, (ULONG)"0123456789", MUIA_String_MaxLen, 7, MUIA_FixWidthTxt, (ULONG)"000000", MUIA_CycleChain, 1, End;
}
static Object *decstr(double v)
{
  char b[24];
  long t = (long)(v * 100 + 0.5);
  if (t % 100) sprintf(b, "%ld.%02ld", t / 100, t % 100); else sprintf(b, "%ld", t / 100);
  return StringObject, StringFrame, MUIA_String_Contents, (ULONG)b, MUIA_String_Accept, (ULONG)"0123456789.", MUIA_String_MaxLen, 10, MUIA_FixWidthTxt, (ULONG)"0000000", MUIA_CycleChain, 1, End;
}
static Object *textstr(const char *s, int max) { return StringObject, StringFrame, MUIA_String_Contents, (ULONG)s, MUIA_String_MaxLen, max, MUIA_CycleChain, 1, End; }
static LONG numof(Object *o) { return atol((const char *)xget(o, MUIA_String_Contents)); }
/* a decimal number typed (no strtod: vbcc's is slow and locale-free anyway) */
static double decof(Object *o)
{
  const char *s = (const char *)xget(o, MUIA_String_Contents);
  double v = 0, f = 0.1;
  for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0');
  if (*s == '.') for (s++; *s >= '0' && *s <= '9'; s++, f /= 10) v += (*s - '0') * f;
  return v;
}
static Object *button(const char *l, ULONG id)
{
  Object *b = QButton(tr(l));
  if (b) DoMethod(b, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, id);
  return b;
}
static void notify_change(Object *o, ULONG attr) { DoMethod(o, MUIM_Notify, attr, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGCHANGE); }
static void say(const char *s) { ask("OK", "%s", tr(s)); }
static void sayf(const char *fmt, const char *arg) { static char m[400]; snprintf(m, sizeof m, tr(fmt), arg); ask("OK", "%s", m); }
static void sayn(const char *fmt, long n) { static char m[300]; snprintf(m, sizeof m, tr(fmt), n); ask("OK", "%s", m); }
static int make_drawer(const char *path)
{
  BPTR l = Lock((STRPTR)path, ACCESS_READ);
  if (l) { UnLock(l); return 1; }
  if (!(l = CreateDir((STRPTR)path))) return 0;
  UnLock(l);
  return 1;
}
/* the file name part of a path */
static const char *file_part(const char *p) { return (const char *)FilePart((STRPTR)p); }
static char m3dir[512];                /* the last drawer used here */

/* ================================================================== a picture in a dialog */
#define MUIA_M3Pic_Image (QTAG + 0x7300)   /* (set) a const Rgba * to show, NULL for none */
struct PicData { const Rgba *im; };
static struct MUI_CustomClass *piccls;
static ULONG picdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct PicData *p;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    ((struct PicData *)INST_DATA(cl, obj))->im = (const Rgba *)GetTagData(MUIA_M3Pic_Image, 0, ((struct opSet *)msg)->ops_AttrList);
    return (ULONG)obj;
  case OM_SET: {
    struct TagItem *t = FindTagItem(MUIA_M3Pic_Image, ((struct opSet *)msg)->ops_AttrList);
    if (t) { ((struct PicData *)INST_DATA(cl, obj))->im = (const Rgba *)t->ti_Data; MUI_Redraw(obj, MADF_DRAWOBJECT); }
    break; }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 120; m->MinMaxInfo->DefWidth += 200; m->MinMaxInfo->MaxWidth += 320;
    m->MinMaxInfo->MinHeight += 90; m->MinMaxInfo->DefHeight += 150; m->MinMaxInfo->MaxHeight += 240;
    return 0; }
  case MUIM_Draw: {
    struct RastPort *rp = _rp(obj);
    LONG W = _mwidth(obj), H = _mheight(obj), w, h, x, y;
    UBYTE *row;
    DoSuperMethodA(cl, obj, msg);
    p = INST_DATA(cl, obj);
    SetAPen(rp, tpen(T_CANVAS)); RectFill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj));
    if (!p->im || !p->im->px || p->im->w < 1 || p->im->h < 1) return 0;
    /* fitted (never enlarged), nearest pixels, transparency over grey */
    w = p->im->w; h = p->im->h;
    if (w > W) { h = h * W / w; w = W; }
    if (h > H) { w = w * H / h; h = H; }
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (!(row = (UBYTE *)AllocVec((ULONG)w * 3, MEMF_ANY))) return 0;
    for (y = 0; y < h; y++) {
      const u8 *src = p->im->px + (long)(y * p->im->h / h) * p->im->w * 4;
      for (x = 0; x < w; x++) {
        const u8 *q = src + (long)(x * p->im->w / w) * 4;
        int a = q[3], g = ((x >> 3) + (y >> 3)) & 1 ? 204 : 255, k;
        for (k = 0; k < 3; k++) row[x * 3 + k] = (UBYTE)((q[k] * a + g * (255 - a) + 127) / 255);
      }
      draw_rgb(rp, _screen(obj), row, w * 3, _mleft(obj) + (W - w) / 2, _mtop(obj) + (H - h) / 2 + y, w, 1);
    }
    FreeVec(row);
    return 0; }
  }
  return DoSuperMethodA(cl, obj, msg);
}
static Object *picture(const Rgba *im)
{
  if (!piccls && !(piccls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct PicData), (APTR)picdispatch))) return NULL;
  return NewObject(piccls->mcc_Class, NULL, TextFrame, MUIA_M3Pic_Image, (ULONG)im, MUIA_FillArea, FALSE, TAG_DONE);
}

/* ================================================================== File > Open As */
static void open_as_cmd(void)
{
  static char path[512];
  static RawOpts raw;
  static const ULONG ends[] = { ID_DLGOK, 0 };
  u8 *data;
  size_t len = 0;
  int f;
  Doc *d;
  char err[120], name[108];
  if (!path[0]) strcpy(path, m3dir);
  if (!m3_file_request(0, "Open a picture as a format of your choice", path, sizeof path, NULL)) return;
  if (!(f = ask("PNG|JPEG|BMP|IFF ILBM|GIF|Photoshop Raw...|Cancel", "%s", tr("Open the file as which format?")))) return;
  f--;
  if (!(data = file_read(path, &len))) { sayf("%s couldn't be read.", file_part(path)); return; }
  if (f == OA_RAW) {
    Object *w, *ws, *hs, *cs, *hd, *il, *b16;
    int ok;
    if (!raw.w) raw_guess(len, &raw);
    w = dialog_window("Photoshop Raw Options", ColGroup(2),
          Child, label("Width (px):"), Child, ws = numstr(raw.w),
          Child, label("Height (px):"), Child, hs = numstr(raw.h),
          Child, label("Channels:"), Child, cs = numstr(raw.channels),
          Child, label("Header (bytes):"), Child, hd = numstr(raw.header),
          Child, il = check(raw.interleaved), Child, QLabel(tr("Interleaved"), 0),
          Child, b16 = check(raw.bits16), Child, QLabel(tr("16 bits a channel (Mac byte order)"), 0),
        End, "OK");
    if (!w) { free(data); return; }
    ok = run_modal(w, ends, NULL) == ID_DLGOK;
    raw.w = (i32)numof(ws); raw.h = (i32)numof(hs); raw.channels = (int)numof(cs); raw.header = numof(hd);
    raw.interleaved = (int)xget(il, MUIA_Selected); raw.bits16 = (int)xget(b16, MUIA_Selected); raw.bigendian = 1;
    MUI_DisposeObject(w);
    if (!ok) { free(data); return; }
  }
  err[0] = 0;
  d = open_as(data, len, f, &raw, err, sizeof err);
  free(data);
  if (!d) { static char m[300]; snprintf(m, sizeof m, tr("%s couldn't be opened:\n%s"), file_part(path), err[0] ? err : tr("not enough memory")); ask("OK", "%s", m); return; }
  strncpy(name, file_part(path), sizeof name - 1); name[sizeof name - 1] = 0;
  m3_show(d, "");
}

/* ================================================================== Save for Web (Legacy) */
static void sfw_cmd(Doc *d)
{
  static SfwOpts o;
  static int set_up;
  static const char *const methods[] = { "Median Cut", "Octree", "Wu", NULL };
  static const char *const dithers[] = { "No Dither", "Diffusion", "Pattern", "Noise", NULL };
  static const int dith_of[] = { DI_NONE, DI_FS, DI_BAYER4, DI_RANDOM };
  Object *w, *pa, *pb, *ta, *tb, *fc, *nc, *mc, *dc, *qs, *pc, *tc, *sc, *mb;
  Rgba orig, opt;
  u8 *file = NULL;
  size_t len = 0;
  ULONG id;
  int dk = 1, k;
  char sa[80], sb[120], sz[16], path[512];
  if (!set_up) { sfw_defaults(&o); set_up = 1; }
  for (k = 0; k < 4; k++) if (dith_of[k] == o.dither) dk = k;
  memset(&opt, 0, sizeof opt);
  orig.w = d->w; orig.h = d->h;
  if (!(orig.px = doc_flat_rgba(d))) { say("Not enough memory to convert the picture."); return; }
  sz[0] = 0;
  w = dialog_window("Save for Web (Legacy)", VGroup,
        Child, HGroup,
          Child, VGroup, Child, QTitleObject(tr("Original")), Child, pa = picture(&orig), Child, ta = TextObject, MUIA_Text_Contents, (ULONG)"", End, End,
          Child, VGroup, Child, QTitleObject(tr("Optimized")), Child, pb = picture(NULL), Child, tb = TextObject, MUIA_Text_Contents, (ULONG)"", End, End,
        End,
        Child, ColGroup(4),
          Child, label("Format:"), Child, fc = cycle(sfw_names, o.fmt), Child, label("Colors:"), Child, nc = numstr(o.colors),
          Child, label("Palette:"), Child, mc = cycle(methods, o.method < 3 ? o.method : 0), Child, label("Dither:"), Child, dc = cycle(dithers, dk),
          Child, label("Quality:"), Child, qs = slider(1, 100, o.quality), Child, label("Scale (%):"), Child, sc = numstr(o.pct),
          Child, pc = check(o.progressive), Child, QLabel(tr("Progressive / Interlaced"), 0), Child, tc = check(o.transparency), Child, QLabel(tr("Transparency"), 0),
        End,
        Child, HGroup, Child, mb = button("Matte...", ID_DLGBTN0 + 1), Child, HSpace(0), Child, button("Preview", ID_DLGBTN0), End,
      End, "Save...");
  if (!w) { free(orig.px); return; }
  {
    size_t n = (size_t)d->w * d->h * 4;
    size_text(sz, n);
    snprintf(sa, sizeof sa, "%ld x %ld, %s", (long)d->w, (long)d->h, sz);
    set(ta, MUIA_Text_Contents, (ULONG)sa);
  }
  for (;;) {
    static const ULONG ends2[] = { ID_DLGOK, ID_DLGBTN0, ID_DLGBTN0 + 1, 0 };
    id = run_modal(w, ends2, NULL);
    o.fmt = (int)xget(fc, MUIA_Cycle_Active); o.colors = (int)numof(nc); o.method = (int)xget(mc, MUIA_Cycle_Active);
    o.dither = dith_of[xget(dc, MUIA_Cycle_Active) & 3]; o.quality = (int)xget(qs, MUIA_Numeric_Value); o.pct = (int)numof(sc);
    o.progressive = (int)xget(pc, MUIA_Selected); o.transparency = (int)xget(tc, MUIA_Selected);
    if (o.colors < 2) o.colors = 2;
    if (o.colors > 256) o.colors = 256;
    if (o.pct < 1 || o.pct > 1000) o.pct = 100;
    if (id == ID_DLGBTN0 + 1) { LONG c = pick_color("Matte", o.matte); if (c >= 0) o.matte = (u32)c; continue; }
    if (id != ID_DLGBTN0) break;
    /* the preview: written as it will be, then read back */
    set(app, MUIA_Application_Sleep, TRUE);
    set(pb, MUIA_M3Pic_Image, NULL);
    rgba_free(&opt); free(file);
    file = sfw_encode(d, &o, &len);
    if (file) sfw_preview(file, len, &opt);
    set(app, MUIA_Application_Sleep, FALSE);
    if (!file) { set(tb, MUIA_Text_Contents, (ULONG)tr("Not enough memory")); continue; }
    size_text(sz, len);
    snprintf(sb, sizeof sb, "%s %s, %ld s (14.4k), %ld s (56k)", sfw_names[o.fmt], sz, sfw_seconds(len, 14400), sfw_seconds(len, 57600));
    set(tb, MUIA_Text_Contents, (ULONG)sb);
    set(pb, MUIA_M3Pic_Image, (ULONG)&opt);
  }
  set(pa, MUIA_M3Pic_Image, NULL); set(pb, MUIA_M3Pic_Image, NULL);
  MUI_DisposeObject(w);
  rgba_free(&opt); free(orig.px);
  if (id != ID_DLGOK) { free(file); return; }
  if (!file) {
    set(app, MUIA_Application_Sleep, TRUE);
    file = sfw_encode(d, &o, &len);
    set(app, MUIA_Application_Sleep, FALSE);
    if (!file) { free(file); say("Not enough memory to convert the picture."); return; }
  }
  strcpy(path, m3dir);
  {
    DocView *v = cur_view();
    char nm[120];
    snprintf(nm, sizeof nm, "%s", v ? v->name : "Untitled");
    if (strrchr(nm, '.')) *strrchr(nm, '.') = 0;
    strncat(nm, sfw_ext(o.fmt), sizeof nm - 1 - strlen(nm));
    AddPart((STRPTR)path, (STRPTR)nm, sizeof path);
  }
  if (m3_file_request(1, "Save the optimized picture as", path, sizeof path, NULL)) {
    if (!file_write(path, file, len)) sayf("%s couldn't be written (the disk is full or write-protected?).", file_part(path));
    else { strcpy(m3dir, path); *PathPart((STRPTR)m3dir) = 0; }
  }
  free(file);
}

/* ================================================================== Export: picked layers, LUTs, preferences */
static void export_picked(Doc *d)
{
  char drawer[512], path[600], nm[80];
  int i, n = 0, any = doc_npicked(d) > 0;
  strcpy(drawer, m3dir);
  if (!m3_file_request(1, "Choose the drawer for the layers (type any name)", drawer, sizeof drawer, NULL)) return;
  *PathPart((STRPTR)drawer) = 0;
  set(app, MUIA_Application_Sleep, TRUE);
  for (i = 0; i < d->nlay; i++) {
    int k;
    if (!(any ? d->lay[i]->picked || i == d->active : i == d->active)) continue;
    /* <layer name>.png (upstream #15039), with what a file name can't hold changed */
    for (k = 0; d->lay[i]->name[k] && k < 70; k++) { char c = d->lay[i]->name[k]; nm[k] = c == '/' || c == ':' || c == '"' ? '_' : c; }
    nm[k] = 0; strcat(nm, ".png");
    path_join(path, sizeof path, drawer, nm);
    if (layer_to_file(d, i, path)) n++;
  }
  set(app, MUIA_Application_Sleep, FALSE);
  sayn("%ld layers were written.", n);
}
static void export_lut(Doc *d)
{
  static LONG grid = 33;
  char path[512];
  char *text;
  int k;
  if (!(k = ask("CUBE|3DL|Cancel", "%s", tr("Export the adjustment layers as\nwhich kind of colour lookup table?")))) return;
  if (!ask_number("Color Lookup Tables", "Grid points (2-64):", &grid, 2, 64)) return;
  set(app, MUIA_Application_Sleep, TRUE);
  text = k == 1 ? lut_cube_from_doc(d, (int)grid, cur_view() ? cur_view()->name : "Easel") : lut_3dl_from_doc(d, (int)grid);
  set(app, MUIA_Application_Sleep, FALSE);
  if (!text) { say("There are no visible adjustment layers to export."); return; }
  strcpy(path, m3dir);
  AddPart((STRPTR)path, (STRPTR)(k == 1 ? "Look.cube" : "Look.3dl"), sizeof path);
  if (m3_file_request(1, "Save the colour lookup table as", path, sizeof path, NULL) && !file_write(path, text, strlen(text)))
    sayf("%s couldn't be written (the disk is full or write-protected?).", file_part(path));
  free(text);
}
/* Export Preferences: what Quick Export writes */
int m3_quick_fmt = SFW_PNG24, m3_quick_ask;
static void export_prefs(void)
{
  static const char *const where[] = { "Next to the picture", "Ask each time", NULL };
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *fc, *wc;
  w = dialog_window("Export Preferences", ColGroup(2),
        Child, label("Quick Export Format:"), Child, fc = cycle(sfw_names, m3_quick_fmt),
        Child, label("Quick Export Location:"), Child, wc = cycle(where, m3_quick_ask),
      End, "OK");
  if (!w) return;
  if (run_modal(w, ends, NULL) == ID_DLGOK) { m3_quick_fmt = (int)xget(fc, MUIA_Cycle_Active); m3_quick_ask = (int)xget(wc, MUIA_Cycle_Active); }
  MUI_DisposeObject(w);
}
/* (main.c's Quick Export as PNG asks this) the format chosen; 1 if it isn't PNG-24 and was written */
int m3_quick_export(Doc *d, const char *docpath, const char *name)
{
  SfwOpts o;
  char path[600];
  u8 *data;
  size_t len;
  if (m3_quick_fmt == SFW_PNG24 && !m3_quick_ask) return 0;
  sfw_defaults(&o); o.fmt = m3_quick_fmt; o.colors = 256; o.quality = prefs.jpeg_quality;
  if (docpath && docpath[0]) { strcpy(path, docpath); *PathPart((STRPTR)path) = 0; } else strcpy(path, m3dir);
  {
    char nm[120];
    snprintf(nm, sizeof nm, "%s", name);
    if (strrchr(nm, '.')) *strrchr(nm, '.') = 0;
    strncat(nm, sfw_ext(o.fmt), sizeof nm - 1 - strlen(nm));
    AddPart((STRPTR)path, (STRPTR)nm, sizeof path);
  }
  if (m3_quick_ask && !m3_file_request(1, "Quick Export as", path, sizeof path, NULL)) return 1;
  set(app, MUIA_Application_Sleep, TRUE);
  data = sfw_encode(d, &o, &len);
  set(app, MUIA_Application_Sleep, FALSE);
  if (!data || !file_write(path, data, len)) sayf("%s couldn't be written (the disk is full or write-protected?).", file_part(path));
  free(data);
  return 1;
}

/* ================================================================== File > Package */
static void package_cmd(Doc *d)
{
  DocView *v = cur_view();
  char drawer[512], path[600], nm[120];
  int n;
  if (!v) return;
  strcpy(drawer, m3dir);
  snprintf(nm, sizeof nm, "%s", v->name);
  if (strrchr(nm, '.')) *strrchr(nm, '.') = 0;
  AddPart((STRPTR)drawer, (STRPTR)nm, sizeof drawer);
  if (!m3_file_request(1, "Package: the drawer to make (the picture and its linked files go in it)", drawer, sizeof drawer, NULL)) return;
  if (!make_drawer(drawer)) { sayf("The drawer %s couldn't be made.", drawer); return; }
  set(app, MUIA_Application_Sleep, TRUE);
  n = package_links(d, drawer);
  strncat(nm, ".ora", sizeof nm - 1 - strlen(nm));
  path_join(path, sizeof path, drawer, nm);
  if (n >= 0 && !mx_write(d, path)) n = -2;
  set(app, MUIA_Application_Sleep, FALSE);
  if (n == -1) say("A linked file couldn't be copied (is it there, and is there room?).");
  else if (n == -2) sayf("%s couldn't be written (the disk is full or write-protected?).", file_part(path));
  else sayn("The picture and %ld linked files are in the drawer.", n);
}

/* ================================================================== Generate > Image Assets */
static void image_assets(Doc *d)
{
  DocView *v = cur_view();
  char drawer[600], log[1200];
  int n;
  static char m[1400];
  if (!v) return;
  if (v->path[0]) { strcpy(drawer, v->path); if (strrchr(file_part(drawer), '.')) *strrchr(drawer, '.') = 0; strncat(drawer, "-assets", sizeof drawer - 1 - strlen(drawer)); }
  else { strcpy(drawer, m3dir); AddPart((STRPTR)drawer, (STRPTR)"Untitled-assets", sizeof drawer); }
  if (!make_drawer(drawer)) { sayf("The drawer %s couldn't be made.", drawer); return; }
  set(app, MUIA_Application_Sleep, TRUE);
  n = assets_generate(d, drawer, log, sizeof log);
  set(app, MUIA_Application_Sleep, FALSE);
  if (!n) { say("No layer asks for assets: name a layer like \"logo.png\" or \"50% icon.jpg8, icon.gif\"."); return; }
  snprintf(m, sizeof m, tr("%ld files were written into %s:\n%s"), (long)n, drawer, log);
  ask("OK", "%s", m);
}

/* ================================================================== sources: the open pictures or files */
#define MAXSRC 32
static Doc *src_doc[MAXSRC]; static char src_name[MAXSRC][108]; static int src_own[MAXSRC], nsrc;
static void src_free(void) { int i; for (i = 0; i < nsrc; i++) if (src_own[i]) doc_free(src_doc[i]); nsrc = 0; }
/* 1 if there are pictures (asked: the open ones, or files chosen); files are loaded one by one when
   load is 0 (src_doc NULL, src_name the path) */
static char src_path[MAXSRC][512];
static int src_choose(const char *title, int load)
{
  int k, i;
  src_free();
  k = ndocs ? ask("Open Pictures|Choose Files...|Cancel", "%s", tr("Which pictures?")) : 2;
  if (!k) return 0;
  if (k == 1) {
    for (i = 0; i < ndocs && nsrc < MAXSRC; i++) { src_doc[nsrc] = docs[i].doc; src_own[nsrc] = 0; strcpy(src_name[nsrc], docs[i].name); src_path[nsrc][0] = 0; nsrc++; }
    return nsrc;
  }
  k = m3_file_multi(title, src_path, MAXSRC);
  for (i = 0; i < k; i++) {
    strncpy(src_name[i], file_part(src_path[i]), sizeof src_name[0] - 1); src_name[i][sizeof src_name[0] - 1] = 0;
    src_doc[i] = NULL; src_own[i] = 1;
    if (load) {
      long pp = progress_push(1000L * i / k, 1000L * (i + 1) / k);
      src_doc[i] = m3_load(src_path[i]);
      progress_pop(pp);
      if (progress_aborted) { nsrc = i + 1; src_free(); return 0; }
    }
    nsrc = i + 1;
  }
  if (load) {
    /* the ones that couldn't be read left out */
    int j = 0;
    for (i = 0; i < nsrc; i++) if (src_doc[i]) { src_doc[j] = src_doc[i]; src_own[j] = 1; strcpy(src_name[j], src_name[i]); strcpy(src_path[j], src_path[i]); j++; }
    nsrc = j;
  }
  if (k) { strcpy(m3dir, src_path[0]); *PathPart((STRPTR)m3dir) = 0; }
  return nsrc;
}

/* ================================================================== Scripts > Image Processor */
static void image_processor(void)
{
  static ImProcOpts o;
  static int set_up;
  static char dest[512];
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *jc, *jq, *jr, *jw, *jh, *pc, *pr, *pw, *ph, *tc, *tr_, *tw, *th, *gc, *gr, *gw, *gh, *ic, *ir, *iw, *ih, *icc, *sc, *cc, *bc, *ds;
  int i, n = 0, ok;
  char err[160];
  if (!set_up) { improc_defaults(&o); set_up = 1; }
  if (!src_choose("Image Processor: the pictures to process", 0)) return;
  if (!dest[0]) strcpy(dest, m3dir);
  w = dialog_window("Image Processor", VGroup,
        Child, HGroup, Child, label("Save in drawer:"), Child, ds = textstr(dest, 500), End,
        Child, QTitleObject(tr("File Type")),
        Child, ColGroup(7),
          Child, jc = check(o.jpeg), Child, QLabel(tr("JPEG, quality"), 0), Child, jq = numstr(o.jpeg_quality), Child, jr = check(o.jpeg_resize), Child, label("Fit in:"), Child, jw = numstr(o.jpeg_w), Child, jh = numstr(o.jpeg_h),
          Child, pc = check(o.psd), Child, QLabel(tr("PSD"), 0), Child, HSpace(0), Child, pr = check(o.psd_resize), Child, label("Fit in:"), Child, pw = numstr(o.psd_w), Child, ph = numstr(o.psd_h),
          Child, tc = check(o.tiff), Child, QLabel(tr("TIFF"), 0), Child, HSpace(0), Child, tr_ = check(o.tiff_resize), Child, label("Fit in:"), Child, tw = numstr(o.tiff_w), Child, th = numstr(o.tiff_h),
          Child, gc = check(o.png), Child, QLabel(tr("PNG"), 0), Child, HSpace(0), Child, gr = check(o.png_resize), Child, label("Fit in:"), Child, gw = numstr(o.png_w), Child, gh = numstr(o.png_h),
          Child, ic = check(o.ilbm), Child, QLabel(tr("IFF ILBM, colours"), 0), Child, icc = numstr(o.ilbm_colors), Child, ir = check(o.ilbm_resize), Child, label("Fit in:"), Child, iw = numstr(o.ilbm_w), Child, ih = numstr(o.ilbm_h),
        End,
        Child, QTitleObject(tr("Preferences")),
        Child, HGroup, Child, sc = check(o.to_srgb), Child, QLabel(tr("Convert Profile to sRGB"), 0), Child, HSpace(8),
          Child, bc = check(o.icon), Child, QLabel(tr("Icons"), 0), Child, HSpace(0), End,
        Child, HGroup, Child, label("Copyright Info:"), Child, cc = textstr(o.copyright, 250), End,
      End, "Run");
  if (!w) { src_free(); return; }
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  if (ok) {
    o.jpeg = (int)xget(jc, MUIA_Selected); o.jpeg_quality = (int)numof(jq); o.jpeg_resize = (int)xget(jr, MUIA_Selected); o.jpeg_w = (i32)numof(jw); o.jpeg_h = (i32)numof(jh);
    o.psd = (int)xget(pc, MUIA_Selected); o.psd_resize = (int)xget(pr, MUIA_Selected); o.psd_w = (i32)numof(pw); o.psd_h = (i32)numof(ph);
    o.tiff = (int)xget(tc, MUIA_Selected); o.tiff_resize = (int)xget(tr_, MUIA_Selected); o.tiff_w = (i32)numof(tw); o.tiff_h = (i32)numof(th);
    o.png = (int)xget(gc, MUIA_Selected); o.png_resize = (int)xget(gr, MUIA_Selected); o.png_w = (i32)numof(gw); o.png_h = (i32)numof(gh);
    o.ilbm = (int)xget(ic, MUIA_Selected); o.ilbm_colors = (int)numof(icc); o.ilbm_resize = (int)xget(ir, MUIA_Selected); o.ilbm_w = (i32)numof(iw); o.ilbm_h = (i32)numof(ih);
    if (o.ilbm_colors == 1 || o.ilbm_colors > 256) o.ilbm_colors = 256;
    if (o.jpeg_quality < 1 || o.jpeg_quality > 100) o.jpeg_quality = 80;
    o.to_srgb = (int)xget(sc, MUIA_Selected); o.icon = (int)xget(bc, MUIA_Selected);
    snprintf(o.copyright, sizeof o.copyright, "%s", (const char *)xget(cc, MUIA_String_Contents));
    snprintf(dest, sizeof dest, "%s", (const char *)xget(ds, MUIA_String_Contents));
  }
  MUI_DisposeObject(w);
  if (!ok) { src_free(); return; }
  if (!dest[0] || !make_drawer(dest)) { sayf("The drawer %s couldn't be made.", dest); src_free(); return; }
  job_begin(NULL, "Image Processor");
  err[0] = 0;
  for (i = 0; i < nsrc && !progress_aborted; i++) {
    Doc *d = src_doc[i];
    long pp = progress_push(1000L * i / nsrc, 1000L * (i + 1) / nsrc);
    if (!d) d = m3_load(src_path[i]);
    if (d) { n += improc_doc(d, src_name[i], dest, &o, make_drawer, err, sizeof err); if (!src_doc[i]) doc_free(d); }
    progress_pop(pp);
    progress(1000L * (i + 1) / nsrc, 1000);
  }
  job_end();
  src_free();
  if (err[0]) sayf("Something couldn't be done:\n%s", err);
  else sayn("%ld files were written.", n);
}

/* ================================================================== Scripts > Statistics */
static void statistics(void)
{
  static int mode = STK_MEDIAN;
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Object *w, *mc;
  Doc *r;
  int ok;
  w = dialog_window("Image Statistics", HGroup, Child, label("Stack Mode:"), Child, mc = cycle(stack_names, mode), End, "OK");
  if (!w) return;
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  mode = (int)xget(mc, MUIA_Cycle_Active);
  MUI_DisposeObject(w);
  if (!ok) return;
  job_begin(NULL, "Statistics");
  if (src_choose("Statistics: the pictures to stack", 1) < 2) { job_end(); if (nsrc < 2) say("Choose two pictures or more."); src_free(); return; }
  r = doc_statistics(src_doc, nsrc, mode);
  job_end();
  src_free();
  if (!r) { if (!progress_aborted) say("Not enough memory to convert the picture."); return; }
  m3_show(r, "");
}

/* ================================================================== Automate > Contact Sheet II */
static void contact_sheet_cmd(void)
{
  static ContactOpts o;
  static int set_up;
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *const placing[] = { "across first", "down first", NULL };
  Object *w, *ws, *hs, *rs, *cs, *rws, *sp, *pl, *fl, *ro, *cp, *fs;
  int ok, p, pages;
  if (!set_up) { contact_defaults(&o); o.w = 1240; o.h = 1754; o.dpi = 150; set_up = 1; }
  w = dialog_window("Contact Sheet II", ColGroup(4),
        Child, label("Width (px):"), Child, ws = numstr(o.w), Child, label("Height (px):"), Child, hs = numstr(o.h),
        Child, label("Resolution (dpi):"), Child, rs = numstr(o.dpi), Child, label("Spacing (px):"), Child, sp = numstr(o.spacing),
        Child, label("Columns:"), Child, cs = numstr(o.cols), Child, label("Rows:"), Child, rws = numstr(o.rows),
        Child, label("Place:"), Child, pl = cycle(placing, !o.across), Child, ro = check(o.rotate), Child, QLabel(tr("Rotate for Best Fit"), 0),
        Child, fl = check(o.flatten), Child, QLabel(tr("Flatten All Layers"), 0), Child, HSpace(0), Child, HSpace(0),
        Child, cp = check(o.captions), Child, QLabel(tr("Use Filename as Caption"), 0), Child, label("Font Size (px):"), Child, fs = numstr(o.font_px),
      End, "OK");
  if (!w) return;
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  o.w = (i32)numof(ws); o.h = (i32)numof(hs); o.dpi = (int)numof(rs); o.spacing = (int)numof(sp); o.cols = (int)numof(cs); o.rows = (int)numof(rws);
  o.across = !xget(pl, MUIA_Cycle_Active); o.rotate = (int)xget(ro, MUIA_Selected); o.flatten = (int)xget(fl, MUIA_Selected);
  o.captions = (int)xget(cp, MUIA_Selected); o.font_px = (int)numof(fs);
  if (o.font_px < 4) o.font_px = 4;
  if (o.font_px > 200) o.font_px = 200;
  snprintf(o.family, sizeof o.family, "%s", font_name(opt.text_font));
  MUI_DisposeObject(w);
  if (!ok) return;
  if (o.w < 16 || o.h < 16 || o.w > MAXDIM || o.h > MAXDIM || o.cols < 1 || o.rows < 1) { say("The width and height must be between 1 and 16384 pixels."); return; }
  if (AvailMem(MEMF_ANY) < (ULONG)o.w * o.h * 8 + 200000) { sayn("The sheet needs about %ld KB of memory.", (long)o.w * o.h * 8 / 1024); return; }
  job_begin(NULL, "Contact Sheet II");
  if (!src_choose("Contact Sheet II: the pictures", 1)) { job_end(); return; }
  pages = contact_pages(nsrc, &o);
  {
    const char *names[MAXSRC];
    int i;
    for (i = 0; i < nsrc; i++) names[i] = src_name[i];
    for (p = 0; p < pages && !progress_aborted; p++) {
      Doc *s = contact_sheet(src_doc, names, nsrc, p, &o);
      if (!s) { say("Not enough memory to convert the picture."); break; }
      m3_show(s, "");
    }
  }
  job_end();
  src_free();
}

/* ================================================================== Import > Grab Screen */
static void grab_screen(void)
{
  struct Screen *s;
  ULONG ilock;
  LONG w, h;
  Doc *d = NULL;
  int k = ask("Now|In 5 Seconds|Cancel", "%s", tr("Grab the frontmost screen as a new picture? (Bring the screen to the front in the time given.)"));
  if (!k) return;
  if (k == 2) Delay(250);
  ilock = LockIBase(0);
  s = IntuitionBase->FirstScreen;
  if (s) { w = s->Width; h = s->Height; } else w = h = 0;
  /* (the screen kept from closing while it is read: it is the front one, and a public screen lock is
     not possible for every screen; reading takes a moment, so the lock on Intuition is let go first
     only for true-colour screens read through cybergraphics, which locks the bitmap itself) */
  if (s && w > 0 && h > 0) {
    if (truecolor(s)) {
      u8 *rgb = (u8 *)AllocVec((ULONG)w * h * 3, MEMF_ANY);
      if (rgb) {
        ReadPixelArray(rgb, 0, 0, (UWORD)(w * 3), &s->RastPort, 0, 0, (UWORD)w, (UWORD)h, RECTFMT_RGB);
        UnlockIBase(ilock); ilock = 0;
        d = doc_from_screen(NULL, NULL, 0, rgb, w, h, 0);
        FreeVec(rgb);
      }
    } else {
      int depth = GetBitMapAttr(s->RastPort.BitMap, BMA_DEPTH), n = 1 << (depth > 8 ? 8 : depth), i;
      u8 *pens = (u8 *)AllocVec((ULONG)((w + 15) & ~15) * h, MEMF_ANY), pal[256 * 3];
      struct RastPort tmp;
      struct BitMap *tb = AllocBitMap((ULONG)((w + 15) & ~15), 1, 8, 0, NULL);
      ULONG c[3];
      if (pens && tb) {
        InitRastPort(&tmp); tmp.BitMap = tb;
        for (i = 0; i < n; i++) { GetRGB32(s->ViewPort.ColorMap, (ULONG)i, 1, c); pal[i * 3] = (u8)(c[0] >> 24); pal[i * 3 + 1] = (u8)(c[1] >> 24); pal[i * 3 + 2] = (u8)(c[2] >> 24); }
        ReadPixelArray8(&s->RastPort, 0, 0, (UWORD)(w - 1), (UWORD)(h - 1), pens, &tmp);
        UnlockIBase(ilock); ilock = 0;
        if ((((w + 15) & ~15)) != w) { LONG y; for (y = 1; y < h; y++) memmove(pens + y * w, pens + y * ((w + 15) & ~15), (size_t)w); }
        d = doc_from_screen(pens, pal, n, NULL, w, h, depth <= 8);
      }
      if (tb) FreeBitMap(tb);
      if (pens) FreeVec(pens);
    }
  }
  if (ilock) UnlockIBase(ilock);
  if (!d) { say("The screen couldn't be read (not enough memory?)."); return; }
  m3_show(d, "");
}

/* ================================================================== Edit > Find and Replace Text */
/* Latin-1 from a gadget as UTF-8 */
static void l1_utf8(const char *s, char *out, size_t cap)
{
  size_t o = 0;
  for (; *s && o + 3 < cap; s++) {
    unsigned char c = (unsigned char)*s;
    if (c < 0x80) out[o++] = (char)c;
    else { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 0x3F)); }
  }
  out[o] = 0;
}
static void find_replace(Doc *d)
{
  static char find[128], repl[128];
  static int match_case, whole, all_layers = 1;
  static const ULONG ends[] = { ID_DLGOK, ID_DLGBTN0, ID_DLGBTN0 + 1, 0 };
  Object *w, *fs, *rs, *mc, *wc, *ac;
  ULONG id;
  int layer = d->active, pos = -1;
  w = dialog_window("Find and Replace Text", VGroup,
        Child, ColGroup(2), Child, label("Find What:"), Child, fs = textstr(find, 120), Child, label("Change To:"), Child, rs = textstr(repl, 120), End,
        Child, HGroup, Child, ac = check(all_layers), Child, QLabel(tr("Search All Layers"), 0), Child, mc = check(match_case), Child, QLabel(tr("Case Sensitive"), 0),
          Child, wc = check(whole), Child, QLabel(tr("Whole Word Only"), 0), Child, HSpace(0), End,
        Child, HGroup, Child, button("Find Next", ID_DLGBTN0), Child, button("Change", ID_DLGBTN0 + 1), Child, HSpace(0), End,
      End, "Change All");
  if (!w) return;
  set(w, MUIA_Window_ActiveObject, (ULONG)fs);
  for (;;) {
    char f8[400], r8[400];
    int n;
    id = run_modal(w, ends, NULL);
    snprintf(find, sizeof find, "%s", (const char *)xget(fs, MUIA_String_Contents));
    snprintf(repl, sizeof repl, "%s", (const char *)xget(rs, MUIA_String_Contents));
    match_case = (int)xget(mc, MUIA_Selected); whole = (int)xget(wc, MUIA_Selected); all_layers = (int)xget(ac, MUIA_Selected);
    if (id != ID_DLGOK && id != ID_DLGBTN0 && id != ID_DLGBTN0 + 1) break;
    l1_utf8(find, f8, sizeof f8); l1_utf8(repl, r8, sizeof r8);
    if (!f8[0]) continue;
    if (id == ID_DLGBTN0) {
      /* the next one: its layer made active */
      int nl, np;
      if (type_find_next(d, layer, pos + 1, f8, match_case, whole, &nl, &np) || (all_layers && type_find_next(d, 0, 0, f8, match_case, whole, &nl, &np))) {
        if (!all_layers && nl != d->active) { say("Not found."); continue; }
        layer = nl; pos = np; d->active = nl; refresh_all();
      } else say("Not found.");
      continue;
    }
    n = type_find_replace(d, all_layers ? -1 : d->active, f8, r8, match_case, whole, id == ID_DLGOK);
    refresh_all();
    if (id == ID_DLGOK) { sayn("%ld changed.", n); break; }
    if (!n) say("Not found.");
  }
  MUI_DisposeObject(w);
}

/* ================================================================== Reset Tool */
static void reset_tool(int t)
{
  ToolTip tip;
  tool_reset_tip(tool_names[t], &tip);
  opt.size[t] = tip.size; opt.hardness[t] = tip.hardness; opt.opacity[t] = tip.opacity; opt.flow[t] = tip.flow;
  opt.mode[t] = BM_NORMAL; opt.strength[t] = 50;
  if (t == TL_WAND || t == TL_BUCKET) { opt.tolerance = 32; opt.contiguous = 1; opt.all_layers = 0; opt.wand_sample = 0; opt.fill_opacity = 100; }
  if (t == TL_EYEDROPPER || t == TL_SAMPLER) { opt.sample = 0; opt.pick_from = 0; }
  if (t == TL_MAGLASSO) { opt.ml_width = 10; opt.ml_contrast = 10; opt.ml_freq = 57; }
  if (t == TL_LINE) opt.line_width = 1;
}

/* ================================================================== Apply Image and Calculations */
/* a source's three cycles: picture (the open ones the same size as d), layer (Merged, then its
   layers top first), channel; rebuilt when the picture changes */
static const char *doc_names[MAXDOCS + 1]; static int doc_index[MAXDOCS];
static const char *lay_names[2][MAXLAYERS + 2];
static int same_size_docs(Doc *d)
{
  int i, n = 0;
  for (i = 0; i < ndocs; i++) if (docs[i].doc->w == d->w && docs[i].doc->h == d->h) { doc_index[n] = i; doc_names[n++] = docs[i].name; }
  doc_names[n] = NULL;
  return n;
}
static void layer_names(int which, Doc *s)
{
  int i, n = 0;
  lay_names[which][n++] = tr("Merged");
  for (i = s->nlay - 1; i >= 0; i--) lay_names[which][n++] = s->lay[i]->name;
  lay_names[which][n] = NULL;
}
static int layer_of(Doc *s, int k) { return k <= 0 ? -1 : s->nlay - k; }
static void apply_calc(Doc *d, int calc)
{
  static const char *const results[] = { "New Channel", "Selection", NULL };
  static int blend = BM_MULTIPLY, opacity = 100, keep, result;
  static const ULONG ends[] = { ID_DLGOK, ID_DLGCHANGE, 0 };
  Object *w, *dc[2], *lc[2], *cc[2], *ic[2], *bc, *os, *kc = NULL, *rc = NULL;
  int ndoc = same_size_docs(d), dk[2] = { 0, 0 }, lk[2] = { 0, 0 }, ck[2] = { AC_RGB, AC_RGB }, inv[2] = { 0, 0 }, i, ok;
  Doc *sd[2];
  ULONG id;
  for (i = 0; i < ndoc; i++) if (docs[doc_index[i]].doc == d) dk[0] = dk[1] = i;
  if (calc) { ck[0] = ck[1] = AC_GRAY; }
  for (;;) {
    int s;
    for (s = 0; s < 2; s++) { sd[s] = docs[doc_index[dk[s]]].doc; layer_names(s, sd[s]); if (lk[s] > sd[s]->nlay) lk[s] = 0; }
    w = dialog_window(calc ? "Calculations" : "Apply Image", VGroup,
          Child, QTitleObject(tr(calc ? "Source 1" : "Source")),
          Child, ColGroup(4),
            Child, label("Picture:"), Child, dc[0] = cycle_raw(doc_names, dk[0]), Child, label("Layer:"), Child, lc[0] = cycle_raw(lay_names[0], lk[0]),
            Child, label("Channel:"), Child, cc[0] = cycle(ac_names, ck[0]), Child, ic[0] = check(inv[0]), Child, QLabel(tr("Invert"), 0),
          End,
          Child, calc ? QTitleObject(tr("Source 2")) : HVSpace,
          Child, calc ? ColGroup(4),
            Child, label("Picture:"), Child, dc[1] = cycle_raw(doc_names, dk[1]), Child, label("Layer:"), Child, lc[1] = cycle_raw(lay_names[1], lk[1]),
            Child, label("Channel:"), Child, cc[1] = cycle(ac_names, ck[1]), Child, ic[1] = check(inv[1]), Child, QLabel(tr("Invert"), 0),
          End : HVSpace,
          Child, QTitleObject(tr("Blending")),
          Child, ColGroup(4),
            Child, label("Blending:"), Child, bc = cycle(blend_names, blend), Child, label("Opacity (%):"), Child, os = slider(1, 100, opacity),
            Child, calc ? label("Result:") : (kc = check(keep)), Child, calc ? (rc = cycle(results, result)) : QLabel(tr("Preserve Transparency"), 0),
            Child, HSpace(0), Child, HSpace(0),
          End,
        End, "OK");
    if (!w) return;
    for (s = 0; s < (calc ? 2 : 1); s++) notify_change(dc[s], MUIA_Cycle_Active);
    id = run_modal(w, ends, NULL);
    for (s = 0; s < (calc ? 2 : 1); s++) {
      int nd = (int)xget(dc[s], MUIA_Cycle_Active);
      if (nd != dk[s]) lk[s] = 0; else lk[s] = (int)xget(lc[s], MUIA_Cycle_Active);
      dk[s] = nd; ck[s] = (int)xget(cc[s], MUIA_Cycle_Active); inv[s] = (int)xget(ic[s], MUIA_Selected);
    }
    blend = (int)xget(bc, MUIA_Cycle_Active); opacity = (int)xget(os, MUIA_Numeric_Value);
    if (kc) keep = (int)xget(kc, MUIA_Selected);
    if (rc) result = (int)xget(rc, MUIA_Cycle_Active);
    MUI_DisposeObject(w);
    if (id != ID_DLGCHANGE) break;
  }
  ok = id == ID_DLGOK;
  if (!ok) return;
  {
    AiSource a, b;
    a.doc = docs[doc_index[dk[0]]].doc; a.layer = layer_of(a.doc, lk[0]); a.channel = ck[0]; a.invert = inv[0];
    b.doc = docs[doc_index[dk[1]]].doc; b.layer = layer_of(b.doc, lk[1]); b.channel = ck[1]; b.invert = inv[1];
    set(app, MUIA_Application_Sleep, TRUE);
    if (!calc) { if (!apply_image(d, &a, blend, opacity, keep, NULL)) say("Choose a pixel layer first."); }
    else if (calc_result(d, calculations(&a, &b, blend, opacity, d->w, d->h), result == 1, NULL) == -1) say("Not enough memory to convert the picture.");
    set(app, MUIA_Application_Sleep, FALSE);
  }
}

/* ================================================================== Image > Analysis */
static Buf mlog; static int mlog_on;
static void set_scale(Doc *d)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  MeasureScale s;
  Object *w, *ps, *ls, *us;
  char unit[16];
  measure_scale_get(d, &s);
  if (ruler_on) { double dx = ruler_xy[2] - ruler_xy[0], dy = ruler_xy[3] - ruler_xy[1]; long t = (long)(sqrt(dx * dx + dy * dy) + 0.5); if (t > 0) s.px = (double)t; }
  w = dialog_window("Measurement Scale", ColGroup(2),
        Child, label("Pixel Length:"), Child, ps = decstr(s.px),
        Child, label("Logical Length:"), Child, ls = decstr(s.len),
        Child, label("Logical Units:"), Child, us = textstr(s.unit, 15),
        Child, HSpace(0), Child, TextObject, MUIA_Text_Contents, (ULONG)tr("(The Ruler's line gives the pixel length.)"), End,
      End, "OK");
  if (!w) return;
  if (run_modal(w, ends, NULL) == ID_DLGOK) {
    s.px = decof(ps); s.len = decof(ls);
    snprintf(unit, sizeof unit, "%s", (const char *)xget(us, MUIA_String_Contents));
    strcpy(s.unit, unit[0] ? unit : "pixels");
    measure_scale_set(d, &s);
  }
  MUI_DisposeObject(w);
}
static void record_measurements(Doc *d)
{
  static char m[600];
  const char *last;
  if (!mlog_on) { buf_init(&mlog); mlog_on = 1; }
  measure_record(d, ruler_on ? ruler_xy : NULL, &mlog);
  /* the last line shown */
  buf_addc(&mlog, 0); mlog.len--;
  last = mlog.data + mlog.len - 1;
  while (last > mlog.data && last[-1] != '\n') last--;
  if (last > mlog.data) { const char *p = last - 1; while (p > mlog.data && p[-1] != '\n') p--; snprintf(m, sizeof m, "%.*s%s", (int)(last - p), p, last); }
  else snprintf(m, sizeof m, "%s", last);
  { char *t; for (t = m; *t; t++) if (*t == '\t') *t = ' '; }
  ask("OK", "%s", m);
}
static void export_measurements(void)
{
  char path[512];
  if (!mlog_on || !mlog.len) { say("Nothing has been recorded yet\n(Image > Analysis > Record Measurements)."); return; }
  strcpy(path, m3dir); AddPart((STRPTR)path, (STRPTR)"Measurements.txt", sizeof path);
  if (m3_file_request(1, "Export the measurements as", path, sizeof path, NULL) && !file_write(path, mlog.data, mlog.len))
    sayf("%s couldn't be written (the disk is full or write-protected?).", file_part(path));
}
static void place_marker(Doc *d)
{
  static LONG len = 10, white;
  MeasureScale s;
  char t[64];
  int k;
  measure_scale_get(d, &s);
  snprintf(t, sizeof t, "%s (%s):", tr("Length"), s.unit);
  if (!ask_number("Measurement Scale Marker", t, &len, 1, 100000)) return;
  if (!(k = ask("Black|White|Cancel", "%s", tr("The marker's colour?")))) return;
  white = k == 2;
  if (scale_marker(d, (double)len, (int)white, d->h / 100 + 3) < 0) say("The marker would be longer than the picture\nis wide: set the scale first.");
}

/* ================================================================== File > New */
static NewPreset upresets[32]; static int nupresets, upresets_read;
static void presets_read(void)
{
  u8 *t; size_t n;
  char *p, *e;
  upresets_read = 1;
  if (!(t = file_read("PROGDIR:Easel.newpresets", &n))) return;
  t = (u8 *)realloc(t, n + 1); if (!t) return;
  t[n] = 0;
  for (p = (char *)t; *p && nupresets < 32; p = e) {
    e = strchr(p, '\n'); if (e) *e++ = 0; else e = p + strlen(p);
    if (newpreset_parse(p, &upresets[nupresets])) nupresets++;
  }
  free(t);
}
static void presets_write(void)
{
  Buf b;
  int i;
  char line[160];
  buf_init(&b);
  for (i = 0; i < nupresets; i++) { newpreset_format(&upresets[i], line, sizeof line); buf_adds(&b, line); buf_addc(&b, '\n'); }
  if (!b.oom) file_write("PROGDIR:Easel.newpresets", b.data ? b.data : "", b.len);
  buf_free(&b);
}
void m3_new_doc(void)
{
  static const char *const bgs[] = { "White", "Black", "Background Color", "Transparent", NULL };
  static const ULONG ends[] = { ID_DLGOK, ID_DLGCHANGE, ID_DLGBTN0, ID_DLGBTN0 + 1, 0 };
  static NewPreset cur;
  static int set_up, pk;
  const char *pnames[64], *parnames[PAR_N + 2];
  Object *w, *pc, *ws, *hs, *uc, *ds, *mc, *bc, *ac;
  int np, i, ak = 0;
  ULONG id;
  Doc *c = cur_doc(), *d;
  char err[120];
  if (!upresets_read) presets_read();
  if (!set_up) { cur = newdoc_builtin[0]; cur.dpi = prefs.dpi ? prefs.dpi : 72; set_up = 1; }
  for (;;) {
    np = 0;
    pnames[np++] = tr("Custom");
    for (i = 0; i < newdoc_nbuiltin && np < 62; i++) pnames[np++] = newdoc_builtin[i].name;
    for (i = 0; i < nupresets && np < 62; i++) pnames[np++] = upresets[i].name;
    pnames[np] = NULL;
    for (i = 0; i < PAR_N; i++) { parnames[i] = tr(par_presets[i].name); if (par_presets[i].ax == cur.ax && par_presets[i].ay == cur.ay) ak = i; }
    parnames[PAR_N] = NULL;
    if (pk >= np) pk = 0;
    w = dialog_window("New", VGroup,
          Child, ColGroup(2),
            Child, label("Preset:"), Child, pc = cycle_raw(pnames, pk),
            Child, label("Width:"), Child, HGroup, Child, ws = decstr(cur.w), Child, uc = cycle(newunit_names, cur.unit), End,
            Child, label("Height:"), Child, hs = decstr(cur.h),
            Child, label("Resolution (dpi):"), Child, ds = numstr(cur.dpi),
            Child, label("Color Mode:"), Child, mc = cycle(newmode_names, cur.mode),
            Child, label("Background Contents:"), Child, bc = cycle(bgs, cur.bg),
            Child, label("Pixel Aspect Ratio:"), Child, ac = cycle_raw(parnames, ak),
          End,
          Child, HGroup, Child, button("Save Preset...", ID_DLGBTN0), Child, button("Delete Preset", ID_DLGBTN0 + 1), Child, HSpace(0), End,
        End, "Create");
    if (!w) return;
    notify_change(pc, MUIA_Cycle_Active);
    set(w, MUIA_Window_ActiveObject, (ULONG)ws);
    id = run_modal(w, ends, NULL);
    {
      int nk = (int)xget(pc, MUIA_Cycle_Active);
      cur.w = decof(ws); cur.h = decof(hs); cur.unit = (int)xget(uc, MUIA_Cycle_Active); cur.dpi = (int)numof(ds);
      cur.mode = (int)xget(mc, MUIA_Cycle_Active); cur.bg = (int)xget(bc, MUIA_Cycle_Active);
      ak = (int)xget(ac, MUIA_Cycle_Active); cur.ax = par_presets[ak].ax; cur.ay = par_presets[ak].ay;
      if (cur.dpi < 1 || cur.dpi > 9999) cur.dpi = 72;
      MUI_DisposeObject(w);
      if (id == ID_DLGCHANGE && nk != pk) {
        pk = nk;
        if (pk >= 1 && pk <= newdoc_nbuiltin) cur = newdoc_builtin[pk - 1];
        else if (pk > newdoc_nbuiltin) cur = upresets[pk - 1 - newdoc_nbuiltin];
        continue;
      }
    }
    if (id == ID_DLGBTN0) {
      char nm[40] = "";
      if (nupresets >= 32) { say("There can be 32 saved presets: delete one first."); continue; }
      if (ask_string("Save Document Preset", "Preset Name:", nm, sizeof nm) && nm[0]) {
        upresets[nupresets] = cur; snprintf(upresets[nupresets].name, sizeof upresets[0].name, "%s", nm); nupresets++;
        pk = newdoc_nbuiltin + nupresets;
        presets_write();
      }
      continue;
    }
    if (id == ID_DLGBTN0 + 1) {
      if (pk > newdoc_nbuiltin) { memmove(&upresets[pk - 1 - newdoc_nbuiltin], &upresets[pk - newdoc_nbuiltin], (size_t)(nupresets - (pk - newdoc_nbuiltin)) * sizeof upresets[0]); nupresets--; pk = 0; presets_write(); }
      else say("Only presets you saved can be deleted.");
      continue;
    }
    if (id != ID_DLGOK) return;
    {
      double pw = units_to_px(cur.w, cur.unit, cur.dpi), ph = units_to_px(cur.h, cur.unit, cur.dpi);
      if (pw * ph * 4 + 100000 > (double)AvailMem(MEMF_ANY)) { sayn("A picture that size needs about %ld KB of memory.", (long)(pw * ph * 4 / 1024)); continue; }
    }
    err[0] = 0;
    if (!(d = newdoc_make(&cur, c ? c->bg : 0xFFFFFF, err, sizeof err))) { ask("OK", "%s", tr(err[0] ? err : "There isn't enough memory for a picture that size.")); continue; }
    if (c) { d->fg = c->fg; d->bg = c->bg; } else { d->fg = 0; d->bg = 0xFFFFFF; }
    m3_show(d, "");
    return;
  }
}

/* ================================================================== the menu items */
void m3_doc_closed(Doc *d) { measure_forget(d); }
static void tick(void (*menu_set)(ULONG, ULONG, ULONG), ULONG id, int on) { menu_set(id, MUIA_Menuitem_Checked, on ? TRUE : FALSE); }
void m3_menus(Doc *d, void (*menu_set)(ULONG id, ULONG attr, ULONG val))
{
  static const ULONG needdoc[] = { ID_PACKAGE, ID_SAVEFORWEB, ID_EXPPICKED, ID_EXPLUT, ID_ASSETS, ID_FINDREPLACE, ID_APPLYIMAGE, ID_CALCULATIONS,
    ID_MSCALE, ID_MRECORD, ID_MMARKER, ID_LAYERSLICE, ID_ARTBOARDLAYERS, ID_FRAMELAYERS, ID_FITARTBOARD, ID_SNAPLAYERS, ID_SNAPSLICES, ID_CLEARSLICES, 0 };
  int i;
  for (i = 0; needdoc[i]; i++) menu_set(needdoc[i], MUIA_Menuitem_Enabled, d != NULL);
  tick(menu_set, ID_PIXELART, m3_pixelart);
  tick(menu_set, ID_FLIPVIEW, m3_flip);
  tick(menu_set, ID_LOCKSLICES, m3_slices_locked);
  tick(menu_set, ID_SNAPLAYERS, d && (d->guides.snap & SNAP_LAYERS));
  tick(menu_set, ID_SNAPSLICES, d && (d->guides.snap & SNAP_SLICES));
}
void m3_command(Doc *d, ULONG id)
{
  Box b;
  int r;
  static int inited;
  if (!inited) { menu3_init(); inited = 1; }
  if (!m3dir[0]) { BPTR l = Lock((STRPTR)"PROGDIR:", ACCESS_READ); if (l) { NameFromLock(l, (STRPTR)m3dir, sizeof m3dir); UnLock(l); } }
  /* those that work without a picture */
  switch (id) {
  case ID_OPENAS: open_as_cmd(); return;
  case ID_EXPPREFS: export_prefs(); return;
  case ID_IMGPROC: image_processor(); return;
  case ID_STATISTICS: statistics(); return;
  case ID_CONTACT: contact_sheet_cmd(); return;
  case ID_GRABSCREEN: grab_screen(); return;
  case ID_RESETTOOL: reset_tool(tool); DoMethod(optbar, MUIM_Opts_Refresh); return;
  case ID_RESETALLTOOLS:
    if (ask("OK|Cancel", "%s", tr("Reset the settings of every tool?"))) { int t; for (t = 0; t < TL_N; t++) reset_tool(t); DoMethod(optbar, MUIM_Opts_Refresh); }
    return;
  case ID_MEXPORT: export_measurements(); return;
  case ID_MRULER: set_tool(TL_RULER); return;
  case ID_MCOUNT: set_tool(TL_COUNT); return;
  case ID_PIXELART:
    m3_pixelart = !m3_pixelart;
    if (m3_pixelart && cur_view()) { DocView *v = cur_view(); v->zoom = pixelart_zoom((long)(v->zoom * 1000 + 0.5), 0) / 1000.0; }
    DoMethod(canvas, MUIM_Canvas_Refresh); set(canvas, MUIA_Canvas_Zoomed, TRUE);
    return;
  case ID_FLIPVIEW: m3_flip = !m3_flip; DoMethod(canvas, MUIM_Canvas_Refresh); return;
  case ID_LOCKSLICES: m3_slices_locked = !m3_slices_locked; return;
  }
  if (!d) return;
  switch (id) {
  case ID_SAVEFORWEB: sfw_cmd(d); return;
  case ID_EXPPICKED: export_picked(d); return;
  case ID_EXPLUT: export_lut(d); return;
  case ID_ASSETS: image_assets(d); return;
  case ID_PACKAGE: package_cmd(d); return;
  case ID_FINDREPLACE: find_replace(d); return;
  case ID_APPLYIMAGE: apply_calc(d, 0); return;
  case ID_CALCULATIONS: apply_calc(d, 1); return;
  case ID_MSCALE: set_scale(d); return;
  case ID_MRECORD: record_measurements(d); return;
  case ID_MMARKER: place_marker(d); return;
  case ID_LAYERSLICE:
    if (slice_from_layer(d, d->active) < 0) say("The active layer is empty (or there are 64 slices already).");
    else { Annots *a = doc_annots(d, 0); if (a) a->show_slices = 1; }
    DoMethod(canvas, MUIM_Canvas_Refresh);
    return;
  case ID_ARTBOARDLAYERS: if (artboard_from_layers(d) < 0) say("Choose the layers first."); return;
  case ID_FRAMELAYERS: if (frame_from_layer(d, d->active) < 0) say("Choose a pixel layer with something on it first."); return;
  case ID_FITARTBOARD:
    r = doc_artboard_at(d, d->w / 2, d->h / 2);
    {
      int k;
      for (k = d->active; k >= 0; k = layer_parent(d, k)) if (d->lay[k]->artboard) { r = k; break; }
    }
    if (r < 0) { say("There is no artboard here."); return; }
    b = d->lay[r]->abox;
    DoMethod(canvas, MUIM_Canvas_Zoom, (LONG)(zoom_fit_box(b, _mwidth(canvas), _mheight(canvas), par_view_ratio(d, vx_par_correct)) * 1000 + 0.5), 0);
    DoMethod(canvas, MUIM_Canvas_ScrollTo, (LONG)((b.x0 + b.x1) / 2), (LONG)((b.y0 + b.y1) / 2));
    return;
  case ID_SNAPLAYERS: d->guides.snap ^= SNAP_LAYERS; return;
  case ID_SNAPSLICES: d->guides.snap ^= SNAP_SLICES; return;
  case ID_CLEARSLICES: {
    Annots *a = doc_annots(d, 0);
    if (m3_slices_locked) { say("The slices are locked (View > Lock Slices)."); return; }
    if (a && a->nslice && ask("OK|Cancel", "%s", tr("Delete every slice?"))) a->nslice = 0;
    DoMethod(canvas, MUIM_Canvas_Refresh);
    return; }
  }
}
