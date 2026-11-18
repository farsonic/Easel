/*
 * paths.c - the Paths panel (Easel 0.9), a tab beside Layers: the saved paths and the Work Path,
 * each with a thumbnail of its shape and its name. A click makes a path the active one (shown on
 * the canvas, and what the path tools and the buttons work on); a click below the rows makes none
 * active; a double-click saves the Work Path under a name, or renames a saved path; Ctrl-click on a
 * thumbnail makes that path the selection. The buttons along the bottom: Fill Path, Stroke Path,
 * Make Selection, Make Work Path from Selection, New Path and Delete Path.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <exec/memory.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "theme.h"
#include "ctxmui.h"
#include "lang.h"
#include "ids.h"
#include "../core/path.h"
#include "../core/util.h"

static int fh(void) { return theme_font()->tf_YSize; }
static struct MUI_CustomClass *pathcls;
int path_pick = -1;

/* the path the Paths panel has chosen (path_active: k + 1 saved path k, -1 the work path), or NULL */
Path *active_path(Doc *d)
{
  if (!d || !d->path_active) return NULL;
  if (d->path_active < 0) return d->workpath;
  return d->path_active <= d->npaths ? d->path[d->path_active - 1] : NULL;
}

struct PaData { struct MUI_EventHandlerNode ehn; int shown, lastrow; ULONG secs, micros; };
#define PA_TW 32                         /* the thumbnails' size */
#define PA_TH 24
static LONG pa_rowh(void) { return PA_TH + 6 > fh() + 8 ? PA_TH + 6 : fh() + 8; }
static LONG pa_bottom(Object *obj) { return _mbottom(obj) - 26; }
static const int pa_buttons[] = { IC_PATHFILL, IC_PATHSTROKE, IC_T_EMARQUEE, IC_PATHFROMSEL, IC_NEWLAYER, IC_TRASH };
static const ULONG pa_ids[] = { ID_PATHFILL, ID_PATHSTROKE, ID_PATHMKSEL, ID_PATHFROMSEL, ID_PATHNEW, ID_PATHDEL };
#define NPB 6
/* the rows: the saved paths, then the Work Path when there is one; row r's path number (-1 the
   Work Path), or -2 past the end */
static int pa_rows(Doc *d) { return d ? d->npaths + (d->workpath ? 1 : 0) : 0; }
static int pa_row_path(Doc *d, int r) { return r < d->npaths ? r : r == d->npaths && d->workpath ? -1 : -2; }
static int pa_active_of(int k) { return k < 0 ? -1 : k + 1; }
/* a path drawn as a thumbnail: filled in the text colour over the panel, the canvas's proportions */
static void pa_thumb(Object *obj, Doc *doc, const Path *p, LONG x, LONG y, int lit)
{
  UBYTE buf[PA_TW * PA_TH * 3];
  u8 cov[PA_TW * PA_TH];
  struct RastPort *rp = _rp(obj);
  LONG tw, th, i;
  Path *c;
  Poly pl;
  double m[6];
  if ((long)doc->w * PA_TH > (long)doc->h * PA_TW) { tw = PA_TW; th = (LONG)((long)doc->h * PA_TW / doc->w); }
  else { th = PA_TH; tw = (LONG)((long)doc->w * PA_TH / doc->h); }
  if (tw < 1) tw = 1;
  if (th < 1) th = 1;
  memset(cov, 0, sizeof cov);
  if (p && p->na && (c = path_copy(p))) {
    m[0] = (double)tw / doc->w; m[1] = 0; m[2] = 0; m[3] = 0; m[4] = (double)th / doc->h; m[5] = 0;
    path_transform(c, m);
    poly_init(&pl);
    if (path_flatten(c, 0.25, &pl)) {
      /* the outline too, so an open path shows */
      Poly st;
      StrokeStyle ss;
      memset(&ss, 0, sizeof ss);
      ss.width = 1; ss.cap = CAP_ROUND; ss.join = JOIN_ROUND;
      poly_init(&st);
      poly_stroke(&pl, &ss, &st);
      poly_raster(&pl, p->evenodd, box_make(0, 0, tw, th), cov, tw);
      if (st.n) {
        u8 c2[PA_TW * PA_TH];
        poly_raster(&st, 0, box_make(0, 0, tw, th), c2, tw);
        for (i = 0; i < tw * th; i++) if (c2[i] > cov[i]) cov[i] = c2[i];
      }
      poly_free(&st);
    }
    poly_free(&pl);
    path_free(c);
  }
  for (i = 0; i < tw * th; i++) {
    /* dark on white (as the panel's thumbnails of masks are) */
    UBYTE v = (UBYTE)(255 - cov[i] * 200 / 255);
    buf[i * 3] = buf[i * 3 + 1] = buf[i * 3 + 2] = v;
  }
  x += (PA_TW - tw) / 2; y += (PA_TH - th) / 2;
  draw_rgb(rp, _screen(obj), buf, tw * 3, x, y, tw, th);
  theme_rect(rp, x - 1, y - 1, x + tw, y + th, tpen(lit ? T_ACCENT : T_BORDER2));
}
static void pa_draw(struct PaData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  Doc *doc = cur_doc();
  LONG l = _mleft(obj), r = _mright(obj), y = _mtop(obj) + 2, rh = pa_rowh();
  int i, n;
  char name[40];
  theme_fill(rp, l, _mtop(obj), r, _mbottom(obj), tpen(T_PANEL));
  if (!doc) return;
  n = pa_rows(doc);
  for (i = 0; i < n && y + rh <= pa_bottom(obj); i++, y += rh) {
    int k = pa_row_path(doc, i), on = doc->path_active == pa_active_of(k);
    if (on) theme_fill(rp, l, y, r, y + rh - 1, tpen(T_SELECTION));
    pa_thumb(obj, doc, k < 0 ? doc->workpath : doc->path[k], l + 8, y + (rh - PA_TH) / 2, on);
    if (k < 0) strcpy(name, tr("Work Path"));
    else utf8_to_latin1(doc->path_name[k], (u32)strlen(doc->path_name[k]), name, sizeof name);
    SetSoftStyle(rp, k < 0 ? FSF_ITALIC : 0, FSF_ITALIC);      /* (the Work Path in italics, as PhotoCraft shows it) */
    theme_text(rp, theme_font(), l + 8 + PA_TW + 10, y + (rh - fh()) / 2, name, tpen(T_TEXT));
    SetSoftStyle(rp, 0, FSF_ITALIC);
    SetAPen(rp, tpen(T_BORDER)); Move(rp, l, y + rh - 1); Draw(rp, r, y + rh - 1);
  }
  if (!n) theme_text(rp, theme_font(), l + 8, y + 4, tr("No paths: draw one with the Pen tool"), tpen(T_DIM));
  SetAPen(rp, tpen(T_BORDER)); Move(rp, l, pa_bottom(obj)); Draw(rp, r, pa_bottom(obj));
  for (i = 0; i < NPB; i++) {
    int dim = (i < 3 && !active_path(doc)) || (i == 3 && !doc->sel) || (i == 5 && !active_path(doc));
    theme_icon(rp, pa_buttons[i], r - (NPB - i) * 24, pa_bottom(obj) + 5, 16, dim);
  }
}
/* the row at window y (-1 none) */
static int pa_row_at(Object *obj, Doc *doc, LONG my)
{
  LONG k;
  if (!doc || my < _mtop(obj) + 2 || my >= pa_bottom(obj)) return -1;
  k = (my - _mtop(obj) - 2) / pa_rowh();
  return k < pa_rows(doc) ? (int)k : -1;
}
/* the button at window x, y along the bottom (-1 none) */
static int pa_button_at(Object *obj, LONG mx, LONG my)
{
  int i;
  if (my < pa_bottom(obj) || my > _mbottom(obj)) return -1;
  for (i = 0; i < NPB; i++) { LONG bx = _mright(obj) - (NPB - i) * 24; if (mx >= bx - 4 && mx < bx + 20) return i; }
  return -1;
}
static ULONG padispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct PaData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    memset(INST_DATA(cl, obj), 0, sizeof(struct PaData));
    ((struct PaData *)INST_DATA(cl, obj))->lastrow = -1;
    ctx_attach(obj, CTX_PATHROW);                /* (1.1: the right button's menu) */
    return (ULONG)obj;
  case OM_DISPOSE: ctx_detach(obj, CTX_PATHROW); break;
  case MUIM_ContextMenuBuild: {
    /* a path's row becomes the active path first, as a click would */
    CtxState s;
    Doc *doc = cur_doc();
    int row = pa_row_at(obj, doc, ((struct MUIP_ContextMenuBuild *)msg)->my);
    if (doc && row >= 0) {
      int k = pa_row_path(doc, row);
      path_pick = k;
      if (doc->path_active != pa_active_of(k)) { doc->path_active = pa_active_of(k); MUI_Redraw(obj, MADF_DRAWOBJECT); }
    }
    ctx_state(&s);
    return (ULONG)ctx_show(CTX_PATHROW, &s);
  }
  case MUIM_ContextMenuChoice: return ctx_choice(obj, ((struct MUIP_ContextMenuChoice *)msg)->item);
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 100; m->MinMaxInfo->DefWidth += 200; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += 60; m->MinMaxInfo->DefHeight += 160; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    d = INST_DATA(cl, obj);
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS;
    DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn);
    return TRUE;
  case MUIM_Cleanup:
    d = INST_DATA(cl, obj);
    DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn);
    theme_cleanup(obj);
    break;
  case MUIM_Show: ((struct PaData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct PaData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) pa_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Paths_Refresh:
    d = INST_DATA(cl, obj);
    if (d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  case MUIM_CreateShortHelp: {
    struct MUIP_CreateShortHelp *m = (struct MUIP_CreateShortHelp *)msg;
    static const char *const n[] = { "Fill Path", "Stroke Path", "Load Path as a Selection", "Make Work Path from Selection", "New Path", "Delete Path" };
    static const char *const b[] = { "Fills the active path with the foreground colour on the layer in use.",
      "Paints along the active path with the Brush tool's settings, in the foreground colour.",
      "Makes the inside of the active path the selection.",
      "Traces the selection's outline as the Work Path.",
      "Makes a new, empty path to draw in.",
      "Deletes the active path." };
    int k = pa_button_at(obj, m->mx, m->my);
    if (k >= 0) return (ULONG)theme_bubble(tr(n[k]), tr(b[k]));
    if (pa_row_at(obj, cur_doc(), m->my) >= 0)
      return (ULONG)theme_bubble(tr("Path"), tr("Click: make it the active path (shown, and what the path tools change). Double-click: save the Work Path, or rename the path. Ctrl-click the thumbnail: load it as the selection."));
    return 0; }
  case MUIM_DeleteShortHelp: return 0;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    Doc *doc = cur_doc();
    LONG mx, my;
    int row, k, ctrl;
    d = INST_DATA(cl, obj);
    if (!im || !d->shown || !doc || im->Class != IDCMP_MOUSEBUTTONS || im->Code != SELECTDOWN) return 0;
    mx = im->MouseX; my = im->MouseY;
    if (mx < _mleft(obj) || mx > _mright(obj) || my < _mtop(obj) || my > _mbottom(obj)) return 0;
    ctrl = (im->Qualifier & IEQUALIFIER_CONTROL) != 0;
    if ((k = pa_button_at(obj, mx, my)) >= 0) {
      path_pick = doc->path_active < 0 ? -1 : doc->path_active - 1;
      DoMethod(_app(obj), MUIM_Application_ReturnID, pa_ids[k]);
      return MUI_EventHandlerRC_Eat;
    }
    row = pa_row_at(obj, doc, my);
    if (row < 0) {
      /* below the rows: no path active (none shown) */
      if (my < pa_bottom(obj) && doc->path_active) { doc->path_active = 0; DoMethod(_app(obj), MUIM_Application_ReturnID, ID_PATHSCHG); }
      return MUI_EventHandlerRC_Eat;
    }
    k = pa_row_path(doc, row);
    path_pick = k;
    doc->path_active = pa_active_of(k);
    if (row == d->lastrow && DoubleClick(d->secs, d->micros, im->Seconds, im->Micros)) {
      d->lastrow = -1;
      DoMethod(_app(obj), MUIM_Application_ReturnID, ID_PATHRENAME);
      return MUI_EventHandlerRC_Eat;
    }
    d->lastrow = row; d->secs = im->Seconds; d->micros = im->Micros;
    DoMethod(_app(obj), MUIM_Application_ReturnID, ctrl && mx < _mleft(obj) + 8 + PA_TW + 4 ? ID_PATHMKSEL : ID_PATHSCHG);
    return MUI_EventHandlerRC_Eat; }
  }
  return DoSuperMethodA(cl, obj, msg);
}
struct MUI_CustomClass *paths_class(void)
{
  if (!pathcls) pathcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct PaData), (APTR)padispatch);
  return pathcls;
}
void paths_free(void)
{
  if (pathcls) { MUI_DeleteCustomClass(pathcls); pathcls = NULL; }
}
