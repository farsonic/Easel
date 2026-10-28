/*
 * swatchwin.c - Easel 0.5's Swatches panel (see swatchwin.h), drawn like the Color panel's
 * swatches with Easel's buttons along the bottom.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "theme.h"
#include "lang.h"
#include "swatchwin.h"
#include "brushwin.h"

#define WHEEL_UP 0x7A
#define WHEEL_DOWN 0x7B
#define SWS 14                                    /* a swatch's side */
static SwatchSet set;
static struct MUI_CustomClass *wcls;
static int fh(void) { return theme_font()->tf_YSize; }

struct WData { struct MUI_EventHandlerNode ehn; int shown, hot, hotbtn, sel, firstrow; Object *self; };
static LONG bar_h(void) { return fh() + 14; }
static int wcols(Object *obj) { int c = (int)((_mwidth(obj) - 8) / (SWS + 2)); return c < 1 ? 1 : c; }
static int wrows(Object *obj) { int r = (int)((_mheight(obj) - bar_h() - 6) / (SWS + 2)); return r < 1 ? 1 : r; }
static const int wbtn_icons[4] = { IC_NEWLAYER, IC_TRASH, IC_OPEN, IC_SAVE };
static void w_draw(struct WData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  struct Screen *scr = _screen(obj);
  LONG l = _mleft(obj), t = _mtop(obj), r = _mright(obj), b = _mbottom(obj), by = b - bar_h() + 1;
  int cols = wcols(obj), rows = wrows(obj), i;
  theme_fill(rp, l, t, r, b, tpen(T_PANEL));
  if (d->firstrow > (set.n + cols - 1) / cols - rows) d->firstrow = (set.n + cols - 1) / cols - rows;
  if (d->firstrow < 0) d->firstrow = 0;
  for (i = d->firstrow * cols; i < set.n && i < (d->firstrow + rows) * cols; i++) {
    int k = i - d->firstrow * cols;
    LONG x = l + 4 + (k % cols) * (SWS + 2), y = t + 4 + (k / cols) * (SWS + 2);
    draw_rgb_box(rp, scr, x, y, x + SWS - 1, y + SWS - 1, set.s[i].rgb);
    if (i == d->sel) { theme_rect(rp, x - 1, y - 1, x + SWS, y + SWS, tpen(T_ACCENT)); theme_rect(rp, x, y, x + SWS - 1, y + SWS - 1, tpen(T_WHITE)); }
    else if (i == d->hot) theme_rect(rp, x - 1, y - 1, x + SWS, y + SWS, tpen(T_TEXT));
  }
  SetAPen(rp, tpen(T_BORDER)); Move(rp, l, by); Draw(rp, r, by);
  for (i = 0; i < 4; i++) {
    LONG bx = r - 4 - (4 - i) * 24;
    if (d->hotbtn == i) theme_fill(rp, bx, by + 3, bx + 21, b - 2, tpen(T_HOVER));
    theme_icon(rp, wbtn_icons[i], bx + 3, by + (bar_h() - 16) / 2, 16, 0);
  }
  if (set.name[0]) {
    char nm[64];
    struct TextExtent te;
    ULONG fit;
    utf8_to_latin1(set.name, strlen(set.name), nm, sizeof nm);
    SetFont(rp, theme_font());
    fit = TextFit(rp, (STRPTR)nm, (ULONG)strlen(nm), &te, NULL, 1, (ULONG)(r - l - 4 * 24 - 16), 100);
    SetAPen(rp, tpen(T_DIM)); SetDrMd(rp, JAM1);
    Move(rp, l + 6, by + (bar_h() - fh()) / 2 + theme_font()->tf_Baseline);
    Text(rp, (STRPTR)nm, fit);
  }
}
static int w_hit(struct WData *d, Object *obj, LONG mx, LONG my, int *btn)
{
  int cols = wcols(obj), rows = wrows(obj), c, r, i;
  LONG by = _mbottom(obj) - bar_h() + 1;
  *btn = -1;
  if (mx < _mleft(obj) || mx > _mright(obj) || my < _mtop(obj) || my > _mbottom(obj)) return -1;
  if (my > by) {
    for (i = 0; i < 4; i++) { LONG bx = _mright(obj) - 4 - (4 - i) * 24; if (mx >= bx && mx <= bx + 21) *btn = i; }
    return -1;
  }
  c = (int)((mx - _mleft(obj) - 4) / (SWS + 2)); r = (int)((my - _mtop(obj) - 4) / (SWS + 2));
  if (c < 0 || c >= cols || r < 0 || r >= rows) return -1;
  i = (d->firstrow + r) * cols + c;
  return i < set.n ? i : -1;
}
static void w_button(struct WData *d, Object *obj, int k)
{
  static char path[512];
  Doc *doc = cur_doc();
  switch (k) {
  case 0: {
    char name[32] = "";
    if (!doc) return;
    if (!ask_string("New Swatch", "Name:", name, sizeof name)) return;
    if (swatches_add(&set, doc->fg, name)) d->sel = set.n - 1;
    break; }
  case 1:
    if (d->sel < 0 || d->sel >= set.n) { ask("OK", "%s", tr("Click a swatch with Shift to select it first.")); return; }
    swatches_remove(&set, d->sel);
    d->sel = -1;
    break;
  case 2: {
    int how, fmt, n;
    if (!ui_file_request(0, "Load Swatches", path, sizeof path, "#?.(swatches|gpl|aco|act|ase|iff|ilbm)")) return;
    fmt = swatch_format_of(path);
    if (!swatch_supported(fmt, 0)) { ask("OK", "%s", tr("Easel can't read that kind of palette file yet.")); return; }
    how = ask(tr("Replace|Add|Cancel"), "%s", tr("Replace the swatches, or add these to them?"));
    if (!how) return;
    if (how == 1) { swatches_clear(&set); set.name[0] = 0; }
    n = swatches_load(&set, path);
    if (!n) ask("OK", "%s", tr("No colours could be read from that file."));
    d->sel = -1;
    break; }
  case 3:
    if (!ui_file_request(1, "Save Swatches", path, sizeof path, "#?.(swatches|gpl|aco|act|ase|iff)")) return;
    if (!swatch_supported(swatch_format_of(path), 1)) { ask("OK", "%s", tr("Easel can't write that kind of palette file yet: try .gpl or .swatches.")); return; }
    if (!swatches_save(&set, path)) ask("OK", "%s", tr("The swatches couldn't be saved."));
    return;
  }
  if (d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
}
static ULONG wdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct WData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->hot = d->hotbtn = d->sel = -1;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY;
    return (ULONG)obj;
  case OM_GET:
    if (((struct opGet *)msg)->opg_AttrID == MUIA_Swatches_Changed) { *((struct opGet *)msg)->opg_Storage = TRUE; return TRUE; }
    break;
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 8 * (SWS + 2) + 8; m->MinMaxInfo->DefWidth += 16 * (SWS + 2) + 8; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += 2 * (SWS + 2) + bar_h() + 8; m->MinMaxInfo->DefHeight += 4 * (SWS + 2) + bar_h() + 8; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct WData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup: theme_cleanup(obj); break;
  case MUIM_Show: ((struct WData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct WData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) w_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Swatches_Refresh:
    if (((struct WData *)INST_DATA(cl, obj))->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  case MUIM_CreateShortHelp: {
    struct MUIP_CreateShortHelp *m = (struct MUIP_CreateShortHelp *)msg;
    static char name[80];
    static const char *const bn[4] = { "New Swatch", "Delete Swatch", "Load Swatches", "Save Swatches" };
    static const char *const bh[4] = { "Adds the foreground colour to the swatches.", "Deletes the selected swatch (Shift-click one to select it).",
      "Loads swatches: Easel's own, GIMP .gpl, and (once connected) .aco, .act, .ase and IFF CMAP.", "Saves the swatches; the file's extension says which format." };
    int btn, i;
    d = INST_DATA(cl, obj);
    i = w_hit(d, obj, m->mx, m->my, &btn);
    if (btn >= 0) return (ULONG)theme_bubble(tr(bn[btn]), tr(bh[btn]));
    if (i >= 0) {
      char nm[40];
      utf8_to_latin1(set.s[i].name, strlen(set.s[i].name), nm, sizeof nm);
      sprintf(name, "#%06lX%s%s", (unsigned long)set.s[i].rgb, nm[0] ? "  " : "", nm);
      return (ULONG)theme_bubble(name, tr("Click: the foreground colour. Alt-click: the background. Shift-click: select it."));
    }
    return 0;
  }
  case MUIM_DeleteShortHelp: return 0;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    int i, btn;
    Doc *doc = cur_doc();
    d = INST_DATA(cl, obj);
    if (!im || !d->shown) return 0;
    if (im->Class == IDCMP_RAWKEY) {
      if ((im->Code == WHEEL_UP || im->Code == WHEEL_DOWN) && im->MouseX >= _mleft(obj) && im->MouseX <= _mright(obj) && im->MouseY >= _mtop(obj) && im->MouseY <= _mbottom(obj)) {
        d->firstrow += im->Code == WHEEL_UP ? -1 : 1;
        MUI_Redraw(obj, MADF_DRAWOBJECT);
        return MUI_EventHandlerRC_Eat;
      }
      return 0;
    }
    i = w_hit(d, obj, im->MouseX, im->MouseY, &btn);
    if (im->Class == IDCMP_MOUSEMOVE) {
      if (i != d->hot || btn != d->hotbtn) { d->hot = i; d->hotbtn = btn; MUI_Redraw(obj, MADF_DRAWOBJECT); }
      return 0;
    }
    if (im->Class != IDCMP_MOUSEBUTTONS || im->Code != SELECTDOWN) return 0;
    if (btn >= 0) { w_button(d, obj, btn); return MUI_EventHandlerRC_Eat; }
    if (i < 0) return 0;
    d->sel = i;
    if (!(im->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) && doc) {
      if (im->Qualifier & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) doc->bg = set.s[i].rgb; else doc->fg = set.s[i].rgb;
      MUI_Redraw(obj, MADF_DRAWOBJECT);
      SetAttrs(obj, MUIA_Swatches_Changed, TRUE, TAG_DONE);
    } else MUI_Redraw(obj, MADF_DRAWOBJECT);
    return MUI_EventHandlerRC_Eat;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
void swatchwin_init(void)
{
  swatches_init(&set);
  swatches_defaults(&set);
  swatch_init_hooks();
}
void swatchwin_free(void) { swatches_free(&set); }
void swatchwin_class_free(void) { if (wcls) { MUI_DeleteCustomClass(wcls); wcls = NULL; } }
SwatchSet *swatchwin_set(void) { return &set; }
Object *swatchwin_panel(void)
{
  if (!wcls && !(wcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct WData), (APTR)wdispatch))) return NULL;
  return NewObject(wcls->mcc_Class, NULL, MUIA_FillArea, FALSE, TAG_DONE);
}
