/*
 * ctxtest.c - native tests of the right mouse button's menus (1.1, amiga/ctxmenu.c): what each
 * offers for each tool and state, in PhotoCraft's order, every item a command the menus have, and
 * no menu where there is nothing to offer.
 * cc -std=c99 -I. tests/ctxtest.c
 */
#include <stdio.h>
#include <string.h>
#include "../amiga/ctxmenu.c"

static int tests, failed;
static void ok(const char *name, int cond) { tests++; if (!cond) { failed++; printf("FAIL %s\n", name); } }
static CtxItem it[CTX_MAXITEMS];
static const char *title;
static int find(int n, const char *label) { int i; for (i = 0; i < n; i++) if (it[i].label && !strcmp(it[i].label, label)) return i; return -1; }
static int has_id(int n, unsigned long id) { int i; for (i = 0; i < n; i++) if (it[i].label && it[i].id == id) return i; return -1; }
/* every menu: no separator first, last or twice; every item with a label has a command (but a submenu's head) */
static int well_formed(int n)
{
  int i;
  if (n && (!it[0].label || !it[n - 1].label)) return 0;
  for (i = 1; i < n; i++) if (!it[i].label && !it[i - 1].label) return 0;
  for (i = 0; i < n; i++)
    if (it[i].label && !it[i].id && !(i + 1 < n && (it[i + 1].flags & CTX_SUB))) return 0;
  return 1;
}
static CtxState doc(int tool)
{
  CtxState s;
  memset(&s, 0, sizeof s);
  s.tool = tool; s.has_doc = 1; s.nlayers = 2; s.index = 1; s.ndocs = 1; s.chan = -1;
  return s;
}

int main(void)
{
  CtxState s;
  int n, k, t, tg, bad;
  /* no picture: no menu on the canvas or the panels; a tab needs a picture too */
  memset(&s, 0, sizeof s);
  ok("no doc canvas", ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS) == 0 && !title);
  ok("no doc tab", ctx_menu(CTX_DOCTAB, &s, &title, it, CTX_MAXITEMS) == 0);
  ok("NULL state", ctx_menu(CTX_CANVAS, NULL, &title, it, CTX_MAXITEMS) == 0);

  /* selection tools: Photoshop 2026's order with a selection */
  s = doc(CK_SELECT); s.has_sel = 1;
  n = ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS);
  ok("selection title", title && !strcmp(title, "Selection"));
  ok("selection first rows", n > 10 && it[0].id == ID_DESELECT && it[1].id == ID_INVERSE && it[2].id == ID_FEATHER && it[3].id == ID_FX_REFINE);
  ok("selection order", find(n, "Save Selection...") < find(n, "Layer via Copy") && find(n, "Layer via Copy") < find(n, "Free Transform") &&
     find(n, "Free Transform") < find(n, "Fill...") && find(n, "Fill...") < find(n, "Last Filter") && find(n, "Last Filter") < find(n, "Fade..."));
  ok("selection well formed", well_formed(n));
  s.kind = CL_TEXT; n = ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS);
  ok("layer via copy greyed on type", (it[find(n, "Layer via Copy")].flags & CTX_OFF) && (it[find(n, "Fill...")].flags & CTX_OFF));
  /* ... and without one */
  s = doc(CK_SELECT);
  n = ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS);
  ok("no selection", n == 4 && it[0].id == ID_SELALL && it[1].id == ID_RESELECT && (it[1].flags & CTX_OFF) && it[2].id == ID_COLORRANGE && (it[3].flags & CTX_OFF));
  s.can_reselect = 1; s.nchan = 1; n = ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS);
  ok("reselect and load on", !(it[1].flags & CTX_OFF) && !(it[3].flags & CTX_OFF));

  /* the Move tool (and Ctrl with any tool): the layers under the pointer, the active one ticked */
  s = doc(CK_MOVE); s.nunder = 2; s.under[0] = 1; s.under_name[0] = "Top"; s.under[1] = 0; s.under_name[1] = "Background";
  n = ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS);
  ok("move list", n == 2 && !strcmp(it[0].label, "Top") && it[0].id == ID_CTX_LAYER0 && (it[0].flags & CTX_TICK) && it[1].id == ID_CTX_LAYER0 + 1 && !(it[1].flags & CTX_TICK));
  s.nunder = 0; ok("move over nothing", ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS) == 0);
  s = doc(CK_PAINT); s.ctrl = 1; s.nunder = 1; s.under[0] = 0; s.under_name[0] = "Background";
  ok("ctrl list", ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS) == 1 && !strcmp(title, "Layers"));

  /* a pending Free Transform wins over the tool */
  s = doc(CK_SELECT); s.has_sel = 1; s.xform = 1;
  n = ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS);
  ok("transform", !strcmp(title, "Free Transform") && it[0].id == ID_XF_SCALE && it[n - 2].id == ID_COMMIT && it[n - 1].id == ID_CANCEL && well_formed(n));

  /* painting, fill, crop, eyedropper, sampler, type, path, shape, view, ruler */
  s = doc(CK_PAINT); n = ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS);
  ok("paint", it[0].id == ID_WIN_BRUSHSET && (it[find(n, "Define Brush Preset...")].flags & CTX_OFF) && has_id(n, ID_SNAPSHOT) < 0);
  s.tool = CK_HISTORY; n = ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS);
  ok("history brush snapshot", has_id(n, ID_SNAPSHOT) >= 0);
  s = doc(CK_FILL); n = ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS);
  ok("fill", it[0].id == ID_FILL && has_id(n, ID_NEWFILLGRAD) >= 0 && well_formed(n));
  s = doc(CK_CROP); ok("crop untouched", ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS) == 0);
  s.crop = 1; n = ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS);
  ok("crop pending", n == 2 && it[0].id == ID_COMMIT && it[1].id == ID_CANCEL);
  s = doc(CK_EYEDROP); s.sample = 2; s.pick_from = 1; n = ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS);
  ok("eyedropper", n == CTX_NSAMPLE + 1 + 3 + 3 && it[n - 2].id == ID_CTX_COPYHTML && it[n - 1].id == ID_CTX_COPYHEX && (it[2].flags & CTX_TICK) && !(it[0].flags & CTX_TICK) && it[CTX_NSAMPLE + 2].id == ID_CTX_PICKFROM0 + 1 && (it[CTX_NSAMPLE + 2].flags & CTX_TICK));
  s = doc(CK_SAMPLER); n = ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS);
  ok("sampler clear greyed", it[n - 1].id == ID_SAMPCLEAR && (it[n - 1].flags & CTX_OFF));
  s = doc(CK_TYPE); ok("type over pixels", ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS) == 0);
  s.kind = CL_TEXT; n = ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS);
  ok("type", it[0].id == ID_EDITTYPE && has_id(n, ID_WARPTEXT) >= 0 && has_id(n, ID_RASTERTYPE2) >= 0 && well_formed(n));
  s = doc(CK_PATH); ok("path none", ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS) == 0);
  s.has_path = 1; s.work_path = 1; n = ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS);
  ok("path", has_id(n, ID_PATHMKSEL) >= 0 && has_id(n, ID_PATHRENAME) >= 0 && well_formed(n));
  s = doc(CK_SHAPE); s.kind = CL_SHAPE; n = ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS);
  ok("shape", it[0].id == ID_SHAPEPROPS && has_id(n, ID_RASTERSHAPE) >= 0);
  s = doc(CK_VIEW); n = ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS);
  ok("view (PhotoCraft #6005 order)", n == 7 && it[0].id == ID_FIT && it[1].id == ID_FILLSCREEN && it[2].id == ID_ACTUAL && it[3].id == ID_ZOOM200 && !it[4].label && it[5].id == ID_ZOOMIN && it[6].id == ID_ZOOMOUT);
  s = doc(CK_RULER); ok("ruler none", ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS) == 0);
  s.ruler = 1; ok("ruler", ctx_menu(CTX_CANVAS, &s, &title, it, CTX_MAXITEMS) == 2 && it[0].id == ID_STRAIGHTEN);

  /* the Layers panel: a row, its thumbnail, its mask */
  s = doc(CK_MOVE); n = ctx_menu(CTX_LAYERROW, &s, &title, it, CTX_MAXITEMS);
  ok("layer row", !strcmp(title, "Layer") && it[0].id == ID_FXBLENDOPT && has_id(n, ID_MERGEDOWN) >= 0 && has_id(n, ID_FLATTEN) >= 0 && well_formed(n));
  k = find(n, "Layer Color");
  ok("colour labels submenu", k >= 0 && (it[k + 1].flags & CTX_SUB) && it[k + 1].id == ID_LABEL0 && (it[k + 1].flags & CTX_TICK) && (it[k + 12].flags & CTX_SUB) && !(it[k + 13].flags & CTX_SUB));
  ok("add mask without one", has_id(n, ID_MASK_REVEALALL) >= 0 && has_id(n, ID_MASK_DELETE) < 0);
  s.has_sel = 1; n = ctx_menu(CTX_LAYERROW, &s, &title, it, CTX_MAXITEMS);
  ok("add mask from the selection", has_id(n, ID_MASK_REVEALSEL) >= 0);
  s.has_mask = 1; s.npicked = 3; s.kind = CL_TEXT; n = ctx_menu(CTX_LAYERROW, &s, &title, it, CTX_MAXITEMS);
  ok("layer row masked, picked, type", has_id(n, ID_MASK_DELETE) >= 0 && has_id(n, ID_MERGELAYERS) >= 0 && has_id(n, ID_MERGEDOWN) < 0 && has_id(n, ID_RASTERTYPE) >= 0);
  s = doc(CK_MOVE); s.kind = CL_GROUP; n = ctx_menu(CTX_LAYERROW, &s, &title, it, CTX_MAXITEMS);
  ok("group row", has_id(n, ID_UNGROUP) >= 0 && has_id(n, ID_MERGEGROUP) >= 0 && (it[0].flags & CTX_OFF));
  s = doc(CK_MOVE); s.index = 0; s.nlayers = 1; s.background = 1; n = ctx_menu(CTX_LAYERROW, &s, &title, it, CTX_MAXITEMS);
  ok("background row", (it[has_id(n, ID_DELLAYER)].flags & CTX_OFF) && (it[has_id(n, ID_MERGEDOWN)].flags & CTX_OFF) && (it[has_id(n, ID_CLIP)].flags & CTX_OFF));
  s = doc(CK_MOVE); n = ctx_menu(CTX_LAYERTHUMB, &s, &title, it, CTX_MAXITEMS);
  ok("thumbnail", it[0].id == ID_SELALPHA && has_id(n, ID_LAYERPROPS) >= 0);
  s.has_mask = 1; s.mask_on = 1; n = ctx_menu(CTX_MASKTHUMB, &s, &title, it, CTX_MAXITEMS);
  ok("mask thumbnail", !strcmp(title, "Layer Mask") && !strcmp(it[0].label, "Disable Layer Mask") && it[1].id == ID_MASK_DELETE && it[2].id == ID_MASK_APPLY && well_formed(n));
  s.mask_on = 0; ctx_menu(CTX_MASKTHUMB, &s, &title, it, CTX_MAXITEMS);
  ok("mask off", !strcmp(it[0].label, "Enable Layer Mask"));

  /* the Channels and Paths panels, the document tabs */
  s = doc(CK_MOVE); n = ctx_menu(CTX_CHANNELROW, &s, &title, it, CTX_MAXITEMS);
  ok("channels empty", n == 1 && it[0].id == ID_SAVESEL);
  s.nchan = 2; s.chan = 1; n = ctx_menu(CTX_CHANNELROW, &s, &title, it, CTX_MAXITEMS);
  ok("channel row", it[0].id == ID_CHANLOAD && it[1].id == ID_CHANRENAME && it[2].id == ID_CHANDEL);
  s = doc(CK_MOVE); n = ctx_menu(CTX_PATHROW, &s, &title, it, CTX_MAXITEMS);
  ok("paths empty", it[0].id == ID_PATHNEW && (it[1].flags & CTX_OFF));
  s.has_path = 1; n = ctx_menu(CTX_PATHROW, &s, &title, it, CTX_MAXITEMS);
  ok("saved path row", it[0].id == ID_PATHMKSEL && has_id(n, ID_PATHDUP) >= 0 && has_id(n, ID_PATHDEL) >= 0);
  s.work_path = 1; n = ctx_menu(CTX_PATHROW, &s, &title, it, CTX_MAXITEMS);
  ok("work path row", has_id(n, ID_PATHDUP) < 0 && !strcmp(it[has_id(n, ID_PATHRENAME)].label, "Save Path"));
  s = doc(CK_MOVE); n = ctx_menu(CTX_DOCTAB, &s, &title, it, CTX_MAXITEMS);
  ok("tab", it[0].id == ID_CTX_TABCLOSE && (it[1].flags & CTX_OFF) && it[2].id == ID_CLOSEALL && it[4].id == ID_CTX_TABDUP);
  s.ndocs = 3; ctx_menu(CTX_DOCTAB, &s, &title, it, CTX_MAXITEMS);
  ok("tab others", !(it[1].flags & CTX_OFF));

  /* every tool and target in every state: well formed, within the room given, ids in range */
  bad = 0;
  for (tg = 0; tg < CTX_N; tg++)
    for (t = 0; t <= CK_OTHER; t++)
      for (k = 0; k < 256; k++) {
        s = doc(t);
        s.has_sel = k & 1; s.xform = (k >> 1) & 1; s.crop = (k >> 2) & 1; s.has_path = (k >> 3) & 1; s.kind = (k >> 4) % 6;
        s.has_mask = (k >> 5) & 1; s.ctrl = (k >> 6) & 1; s.has_fx = (k >> 7) & 1; s.nunder = s.ctrl;
        s.under[0] = 0; s.under_name[0] = "Layer 1";
        n = ctx_menu(tg, &s, &title, it, CTX_MAXITEMS);
        if (n < 0 || n > CTX_MAXITEMS || !well_formed(n) || (n && !title)) bad++;
        while (n-- > 0) if (it[n].label && it[n].id > ID_CTX_LAST) bad++;
      }
  ok("all menus well formed", bad == 0);
  /* a small room cuts the menu, never past it */
  s = doc(CK_SELECT); s.has_sel = 1;
  ok("room", ctx_menu(CTX_CANVAS, &s, &title, it, 3) == 3);

  printf("%d tests, %d failed\n", tests, failed);
  return failed != 0;
}
