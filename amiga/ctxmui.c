/*
 * ctxmui.c - 1.1: the right mouse button's menus made for MUI (see ctxmenu.h). Each object that has
 * one (the canvas, the Layers, Channels and Paths panels, the document tabs) gets a menu strip of its
 * own at OM_NEW (MUIA_ContextMenu); MUIM_ContextMenuBuild then fills the strip with one menu, made
 * again each time from ctxmenu.c's items for what is under the pointer, and MUIM_ContextMenuChoice
 * sends the item's command to main.c's loop as a menu would (MUIM_Application_ReturnID).
 */
#include <string.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "ids.h"
#include "lang.h"
#include "ctxmenu.h"
#include "ctxmui.h"
#include "../core/fx.h"
#include "../core/layers.h"
#include "../core/util.h"

static struct { Object *strip, *menu, *owner; } ctx[CTX_N];
static char under_names[CTX_MAXUNDER][40];
static int under_index[CTX_MAXUNDER], nunder_now;

void ctx_attach(Object *obj, int target)
{
  if (target < 0 || target >= CTX_N) return;
  if (!ctx[target].strip) ctx[target].strip = MenustripObject, End;
  if (ctx[target].strip) { ctx[target].owner = obj; set(obj, MUIA_ContextMenu, (ULONG)ctx[target].strip); }
}
void ctx_detach(Object *obj, int target)
{
  if (target < 0 || target >= CTX_N || !ctx[target].strip || ctx[target].owner != obj) return;
  set(obj, MUIA_ContextMenu, 0);
  ctx[target].owner = NULL;                    /* (the strip is disposed by ctx_free, after the application) */
}
/* (see ctxmui.h) the strips disposed (MUI leaves a context menu when its object goes) */
void ctx_free(void)
{
  int i;
  for (i = 0; i < CTX_N; i++) if (ctx[i].strip) { MUI_DisposeObject(ctx[i].strip); ctx[i].strip = ctx[i].menu = NULL; }
}
/* the tool's kind, for its menu */
int ctx_tool_kind(int t)
{
  switch (t) {
  case TL_MOVE: return CK_MOVE;
  case TL_MARQUEE: case TL_EMARQUEE: case TL_LASSO: case TL_PLASSO: case TL_MAGLASSO: case TL_WAND: return CK_SELECT;
  case TL_BRUSH: case TL_PENCIL: case TL_ERASER: case TL_BGERASE: case TL_CLONE: case TL_PATTERN: case TL_REPLACE:
  case TL_BLUR: case TL_SHARPEN: case TL_SMUDGE: case TL_DODGE: case TL_BURN: case TL_SPONGE: case TL_SPOTHEAL: case TL_HEAL: return CK_PAINT;
  case TL_HISTORY: return CK_HISTORY;
  case TL_BUCKET: case TL_GRADIENT: return CK_FILL;
  case TL_CROP: return CK_CROP;
  case TL_EYEDROPPER: return CK_EYEDROP;
  case TL_SAMPLER: return CK_SAMPLER;
  case TL_RULER: return CK_RULER;
  case TL_TEXT: case TL_VTEXT: return CK_TYPE;
  case TL_PEN: case TL_ADDANCHOR: case TL_DELANCHOR: case TL_CONVERT: case TL_PATHSEL: case TL_DIRECTSEL: return CK_PATH;
  case TL_RECT: case TL_ELLIPSE: case TL_LINE: case TL_ROUNDRECT: case TL_POLYGON: case TL_CUSTOMSHAPE: return CK_SHAPE;
  case TL_HAND: case TL_ZOOM: return CK_VIEW;
  }
  return CK_OTHER;
}
/* (see ctxmui.h) the state the menus depend on, from the picture and tool in use */
void ctx_state(CtxState *s)
{
  Doc *d = cur_doc();
  DocView *v = cur_view();
  int i;
  memset(s, 0, sizeof *s);
  s->tool = ctx_tool_kind(tool);
  s->ndocs = ndocs;
  s->sample = opt.sample; s->pick_from = opt.pick_from;
  s->xform = xform_pending; s->crop = crop_pending; s->ruler = ruler_on;
  s->chan = -1; s->style_copied = 1;
  if (!d || d->active < 0 || d->active >= d->nlay) return;
  s->has_doc = 1;
  s->has_sel = d->sel != NULL; s->can_reselect = d->lastsel != NULL;
  s->nsamplers = v ? v->nsamp : 0;
  s->has_path = active_path(d) != NULL; s->work_path = d->path_active == -1;
  s->nchan = d->nchan;
  s->nlayers = d->nlay; s->index = d->active; s->background = d->active == 0 && d->nlay == 1;
  {
    Layer *l = d->lay[d->active];
    s->kind = l->kind == LK_GROUP ? CL_GROUP : l->kind == LK_ADJUST ? CL_ADJUST : l->kind == LK_FILL ? CL_FILL :
              l->kind == LK_TEXT ? CL_TEXT : l->kind == LK_SHAPE ? CL_SHAPE : CL_PIXEL;
    s->has_mask = l->mask != NULL; s->mask_on = l->mask_on; s->has_fx = fx_has(l); s->clipped = l->clip;
    s->label = l->label; s->hidden = !l->visible;
  }
  for (i = 0; i < d->nlay; i++) if (d->lay[i]->picked || i == d->active) s->npicked++;
}
/* (see ctxmui.h) the layers the canvas found under the pointer, top first, for the Move tool's list */
void ctx_set_under(CtxState *s, const int *idx, int n)
{
  Doc *d = cur_doc();
  int i;
  nunder_now = 0;
  for (i = 0; d && i < n && i < CTX_MAXUNDER; i++) {
    utf8_to_latin1(d->lay[idx[i]]->name, strlen(d->lay[idx[i]]->name), under_names[i], sizeof under_names[i]);
    under_index[i] = idx[i];
    s->under[i] = idx[i]; s->under_name[i] = under_names[i];
    nunder_now++;
  }
  s->nunder = nunder_now;
}
/* (see ctxmui.h) target's menu for state s in its strip: the strip, or NULL for no menu */
Object *ctx_show(int target, const CtxState *s)
{
  static CtxItem it[CTX_MAXITEMS];
  const char *title;
  Object *parent = NULL, *o;
  int n, i;
  if (target < 0 || target >= CTX_N || !ctx[target].strip) return NULL;
  if (ctx[target].menu) { DoMethod(ctx[target].strip, MUIM_Family_Remove, (ULONG)ctx[target].menu); MUI_DisposeObject(ctx[target].menu); ctx[target].menu = NULL; }
  if (!(n = ctx_menu(target, s, &title, it, CTX_MAXITEMS))) return NULL;
  if (!(ctx[target].menu = MenuObject, MUIA_Menu_Title, (ULONG)tr(title), End)) return NULL;
  for (i = 0; i < n; i++) {
    /* the Move tool's list holds layer names, not words to translate */
    const char *lab = !it[i].label ? (const char *)NM_BARLABEL : it[i].id >= ID_CTX_LAYER0 && it[i].id < ID_CTX_LAYER0 + CTX_MAXUNDER ? it[i].label : tr(it[i].label);
    o = MenuitemObject, MUIA_Menuitem_Title, (ULONG)lab, MUIA_UserData, it[i].id,
          MUIA_Menuitem_Enabled, !(it[i].flags & CTX_OFF),
          (ULONG)((it[i].flags & CTX_TICK) ? MUIA_Menuitem_Checkit : TAG_IGNORE), TRUE,
          (ULONG)((it[i].flags & CTX_TICK) ? MUIA_Menuitem_Checked : TAG_IGNORE), TRUE, End;
    if (!o) continue;
    if ((it[i].flags & CTX_SUB) && parent) DoMethod(parent, MUIM_Family_AddTail, (ULONG)o);
    else { DoMethod(ctx[target].menu, MUIM_Family_AddTail, (ULONG)o); parent = o; }
  }
  DoMethod(ctx[target].strip, MUIM_Family_AddTail, (ULONG)ctx[target].menu);
  return ctx[target].strip;
}
/* (see ctxmui.h) an item chosen: its command to main.c's loop */
ULONG ctx_choice(Object *obj, Object *item)
{
  ULONG id = 0;
  if (item) get(item, MUIA_UserData, &id);
  if (id) DoMethod(_app(obj), MUIM_Application_ReturnID, id);
  return 0;
}
/* (see ctxmui.h) the commands only the context menus send */
int ctx_tab = -1;
int ctx_command(ULONG id, int *doc, ULONG *then)
{
  Doc *d = cur_doc();
  *doc = -1; *then = 0;
  if (id >= ID_CTX_TABCLOSE && id <= ID_CTX_TABINFO) {
    static const ULONG cmd[4] = { ID_CLOSE, ID_CLOSEOTHERS, ID_DUPIMAGE, ID_FILEINFO };
    if (ctx_tab >= 0 && ctx_tab < ndocs) *doc = ctx_tab;
    *then = cmd[id - ID_CTX_TABCLOSE];
    return 1;
  }
  if (id >= ID_CTX_LAYER0 && id < ID_CTX_LAYER0 + CTX_MAXUNDER) {
    int k = (int)(id - ID_CTX_LAYER0);
    if (d && k < nunder_now && under_index[k] < d->nlay) { doc_pick_layer(d, under_index[k], 0); refresh_all(); }
    return 1;
  }
  if (id >= ID_CTX_SAMPLE0 && id < ID_CTX_SAMPLE0 + CTX_NSAMPLE) { opt.sample = (int)(id - ID_CTX_SAMPLE0); DoMethod(optbar, MUIM_Opts_Refresh); return 1; }
  if (id >= ID_CTX_PICKFROM0 && id < ID_CTX_PICKFROM0 + 3) { opt.pick_from = (int)(id - ID_CTX_PICKFROM0); DoMethod(optbar, MUIM_Opts_Refresh); return 1; }
  return 0;
}
