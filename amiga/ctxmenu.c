/* ctxmenu.c - see ctxmenu.h. What each right-button menu offers, in PhotoCraft's order (its
   docs/context-menu-parity.md, which follows Photoshop 2026); rows Easel has no command for are left
   out rather than shown doing nothing. Plain C: tests/ctxtest.c runs it natively. */
#include <string.h>
#include "ctxmenu.h"

const char *const ctx_sample_names[CTX_NSAMPLE] = { "Point Sample", "3 by 3 Average", "5 by 5 Average", "11 by 11 Average",
                                                    "31 by 31 Average", "51 by 51 Average", "101 by 101 Average" };
static const char *const pick_names[3] = { "Current Layer", "Current & Below", "All Layers" };
static const char *const label_names[12] = { "None", "Red", "Orange", "Yellow", "Green", "Blue", "Violet", "Gray", "Seafoam", "Indigo", "Magenta", "Fuchsia" };

typedef struct { CtxItem *out; int n, max; } Build;
static void add(Build *b, const char *label, unsigned long id, int flags)
{
  if (b->n >= b->max) return;
  if (!label && (b->n == 0 || !b->out[b->n - 1].label)) return;       /* (no line first, or twice) */
  b->out[b->n].label = label; b->out[b->n].id = id; b->out[b->n].flags = flags; b->n++;
}
static void bar(Build *b) { add(b, NULL, 0, 0); }
static void on(Build *b, const char *label, unsigned long id, int enabled) { add(b, label, id, enabled ? 0 : CTX_OFF); }
static int done(Build *b) { while (b->n && !b->out[b->n - 1].label) b->n--; return b->n; }

/* Free Transform waiting: its modes, the turns and flips, then apply or cancel */
static const char *xform_menu(Build *b)
{
  add(b, "Scale", ID_XF_SCALE, 0); add(b, "Rotate", ID_XF_ROTATE, 0); add(b, "Skew", ID_XF_SKEW, 0);
  add(b, "Distort", ID_XF_DISTORT, 0); add(b, "Perspective", ID_XF_PERSPECTIVE, 0); bar(b);
  add(b, "Rotate 180\260", ID_ROTATE180, 0); add(b, "Rotate 90\260 Clockwise", ID_ROTATE90R, 0);
  add(b, "Rotate 90\260 Anticlockwise", ID_ROTATE90L, 0); bar(b);
  add(b, "Flip Horizontal", ID_FLIPLH, 0); add(b, "Flip Vertical", ID_FLIPLV, 0); bar(b);
  add(b, "Apply", ID_COMMIT, 0); add(b, "Cancel", ID_CANCEL, 0);
  return "Free Transform";
}
/* the visible layers with something under the pointer, the topmost first (Move, or Ctrl with any tool) */
static const char *layer_list(Build *b, const CtxState *s)
{
  int i;
  for (i = 0; i < s->nunder && i < CTX_MAXUNDER; i++)
    add(b, s->under_name[i], ID_CTX_LAYER0 + i, s->under[i] == s->index ? CTX_TICK : 0);
  return "Layers";
}
/* the selection tools: Photoshop 2026's list with a selection, the ways to make one without */
static const char *selection_menu(Build *b, const CtxState *s)
{
  if (!s->has_sel) {
    add(b, "Select All", ID_SELALL, 0); on(b, "Reselect", ID_RESELECT, s->can_reselect);
    add(b, "Color Range...", ID_COLORRANGE, 0); on(b, "Load Selection...", ID_LOADSEL, s->nchan > 0);
    return "Selection";
  }
  add(b, "Deselect", ID_DESELECT, 0); add(b, "Select Inverse", ID_INVERSE, 0); add(b, "Feather...", ID_FEATHER, 0);
  add(b, "Select and Mask...", ID_FX_REFINE, 0); bar(b);
  add(b, "Save Selection...", ID_SAVESEL, 0); add(b, "Make Work Path...", ID_PATHFROMSEL, 0); bar(b);
  on(b, "Layer via Copy", ID_LAYERFROMSEL, s->kind == CL_PIXEL); on(b, "Layer via Cut", ID_LAYERCUTSEL, s->kind == CL_PIXEL);
  add(b, "New Layer", ID_NEWLAYER, 0); bar(b);
  add(b, "Free Transform", ID_FREETRANSFORM, 0); add(b, "Transform Selection", ID_XFSEL, 0);
  add(b, "Distort", ID_XF_DISTORT, 0); add(b, "Perspective", ID_XF_PERSPECTIVE, 0); bar(b);
  on(b, "Fill...", ID_FILL, s->kind == CL_PIXEL); on(b, "Stroke...", ID_STROKE, s->kind == CL_PIXEL); bar(b);
  add(b, "Last Filter", ID_F_AGAIN, 0); bar(b);
  add(b, "Fade...", ID_FADE, 0);
  return "Selection";
}
/* the canvas: what the tool in use offers */
static const char *canvas_menu(Build *b, const CtxState *s)
{
  int i;
  if (!s->has_doc) return NULL;
  if (s->xform) return xform_menu(b);
  if (s->ctrl || s->tool == CK_MOVE) return s->nunder ? layer_list(b, s) : NULL;
  switch (s->tool) {
  case CK_SELECT: return selection_menu(b, s);
  case CK_PAINT: case CK_HISTORY:
    /* PhotoCraft opens its Brush Preset picker: here the Brush Settings window and the brush commands */
    add(b, "Brush Settings...", ID_WIN_BRUSHSET, 0); add(b, "DPaint Brush", ID_WIN_DPAINT, 0); bar(b);
    on(b, "Define Brush Preset...", ID_DEFBRUSH, s->has_sel); on(b, "Pick Up Brush", ID_PICKUPBRUSH, s->has_sel);
    if (s->tool == CK_HISTORY) { bar(b); add(b, "Take Snapshot (History Brush)", ID_SNAPSHOT, 0); }
    return "Brush";
  case CK_FILL:
    on(b, "Fill...", ID_FILL, s->kind == CL_PIXEL); on(b, "Fill with Foreground", ID_FILLFG, s->kind == CL_PIXEL);
    on(b, "Fill with Background", ID_FILLBG, s->kind == CL_PIXEL); on(b, "Fill with Pattern", ID_FILLPATTERN, s->kind == CL_PIXEL); bar(b);
    add(b, "Solid Color...", ID_NEWFILLSOLID, 0); add(b, "Gradient...", ID_NEWFILLGRAD, 0);
    return "Fill";
  case CK_CROP:
    if (!s->crop) return NULL;                  /* (PhotoCraft: the untouched frame has no menu) */
    add(b, "Crop", ID_COMMIT, 0); add(b, "Cancel", ID_CANCEL, 0);
    return "Crop";
  case CK_EYEDROP:
    for (i = 0; i < CTX_NSAMPLE; i++) add(b, ctx_sample_names[i], ID_CTX_SAMPLE0 + i, i == s->sample ? CTX_TICK : 0);
    bar(b);
    for (i = 0; i < 3; i++) add(b, pick_names[i], ID_CTX_PICKFROM0 + i, i == s->pick_from ? CTX_TICK : 0);
    bar(b); add(b, "Copy Color as HTML", ID_CTX_COPYHTML, 0); add(b, "Copy Color's Hex Code", ID_CTX_COPYHEX, 0);   /* 1.3 (#12182) */
    return "Eyedropper";
  case CK_SAMPLER:
    for (i = 0; i < CTX_NSAMPLE; i++) add(b, ctx_sample_names[i], ID_CTX_SAMPLE0 + i, i == s->sample ? CTX_TICK : 0);
    bar(b); on(b, "Clear", ID_SAMPCLEAR, s->nsamplers > 0);
    return "Color Sampler";
  case CK_TYPE:
    if (s->kind != CL_TEXT) return NULL;        /* (over committed type: the active type layer's menu) */
    add(b, "Edit Type", ID_EDITTYPE, 0); add(b, "Character...", ID_TYPECHAR, 0); add(b, "Paragraph...", ID_TYPEPARA, 0); bar(b);
    add(b, "Rasterize Type", ID_RASTERTYPE2, 0); add(b, "Create Work Path", ID_TYPEWORKPATH, 0); bar(b);
    add(b, "Horizontal", ID_TYPEHORIZ, 0); add(b, "Vertical", ID_TYPEVERT, 0); bar(b);
    add(b, "Convert to Paragraph Text", ID_TOPARA, 0); add(b, "Convert to Point Text", ID_TOPOINT, 0);
    add(b, "Warp Text...", ID_WARPTEXT, 0);
    return "Type";
  case CK_PATH:
    if (!s->has_path) return NULL;
    on(b, "Delete Path", ID_PATHDEL, 1); bar(b);
    add(b, "Make Selection", ID_PATHMKSEL, 0); on(b, "Fill Path", ID_PATHFILL, s->kind == CL_PIXEL);
    on(b, "Stroke Path", ID_PATHSTROKE, s->kind == CL_PIXEL); bar(b);
    add(b, "Free Transform", ID_FREETRANSFORM, 0);
    if (s->work_path) { bar(b); add(b, "Save Path", ID_PATHRENAME, 0); }
    return "Path";
  case CK_SHAPE:
    if (s->kind != CL_SHAPE) return NULL;
    add(b, "Shape Properties...", ID_SHAPEPROPS, 0); add(b, "Free Transform", ID_FREETRANSFORM, 0); bar(b);
    add(b, "Rasterize", ID_RASTERSHAPE, 0);
    return "Shape";
  case CK_VIEW:
    add(b, "Fit on Screen", ID_FIT, 0); add(b, "Fill Screen", ID_FILLSCREEN, 0); add(b, "100%", ID_ACTUAL, 0);
    add(b, "200%", ID_ZOOM200, 0); bar(b);
    add(b, "Zoom In", ID_ZOOMIN, 0); add(b, "Zoom Out", ID_ZOOMOUT, 0);
    return "View";
  case CK_RULER:
    if (!s->ruler) return NULL;
    add(b, "Straighten Layer", ID_STRAIGHTEN, 0); add(b, "Clear", ID_RULERCLEAR, 0);
    return "Ruler";
  }
  return NULL;
}
/* a Layers panel row (the layer right-clicked is the active one by now; with several picked the
   picked ones are what Merge and Group take) */
static const char *layer_menu(Build *b, const CtxState *s)
{
  int i, pix = s->kind == CL_PIXEL;
  add(b, "Blending Options...", ID_FXBLENDOPT, s->kind == CL_GROUP || s->kind == CL_ADJUST ? CTX_OFF : 0);
  add(b, "Layer Properties...", ID_LAYERPROPS, 0); bar(b);
  add(b, "Duplicate Layer", ID_DUPLAYER, 0); on(b, "Delete Layer", ID_DELLAYER, s->nlayers > 1); bar(b);
  add(b, "Group Layers", ID_GROUPLAYERS, 0);
  if (s->kind == CL_GROUP) add(b, "Ungroup Layers", ID_UNGROUP, 0);
  bar(b);
  add(b, "Quick Export as PNG", ID_QUICKPNG, 0); add(b, "Export As (with scale)...", ID_EXPORTAS, 0); bar(b);
  if (s->kind == CL_TEXT) add(b, "Rasterize Type", ID_RASTERTYPE, 0);
  else if (s->kind == CL_SHAPE) add(b, "Rasterize", ID_RASTERSHAPE, 0);
  else if (s->kind == CL_FILL) add(b, "Rasterize", ID_RASTERIZE, 0);
  if (s->has_fx) add(b, "Layer Style", ID_FXRASTER, 0);                   /* Layer > Rasterize > Layer Style */
  bar(b);
  if (!s->has_mask) on(b, "Add Layer Mask", s->has_sel ? ID_MASK_REVEALSEL : ID_MASK_REVEALALL, !s->background);
  else { add(b, "Enable/Disable", ID_MASK_ENABLE, 0); add(b, "Apply", ID_MASK_APPLY, pix ? 0 : CTX_OFF); add(b, "Delete", ID_MASK_DELETE, 0); }
  bar(b);
  on(b, "Create/Release Clipping Mask", ID_CLIP, s->index > 0 && !s->background);
  bar(b);
  on(b, "Copy Layer Style", ID_FXCOPY, s->has_fx); on(b, "Paste Layer Style", ID_FXPASTE, s->style_copied);
  on(b, "Clear Layer Style", ID_FXCLEAR, s->has_fx); bar(b);
  add(b, "Layer Color", 0, 0);
  for (i = 0; i < 12; i++) add(b, label_names[i], ID_LABEL0 + i, CTX_SUB | (i == s->label ? CTX_TICK : 0));
  bar(b);
  if (s->npicked > 1) add(b, "Merge Layers", ID_MERGELAYERS, 0);
  else if (s->kind == CL_GROUP) add(b, "Merge Group", ID_MERGEGROUP, 0);
  else on(b, "Merge Down", ID_MERGEDOWN, s->index > 0);
  add(b, "Merge Visible", ID_MERGEVISIBLE, 0); add(b, "Flatten Image", ID_FLATTEN, 0);
  return "Layer";
}
/* a layer's thumbnail (Photoshop: Select Pixels, and the masks' choices) */
static const char *thumb_menu(Build *b, const CtxState *s)
{
  add(b, "Layer Transparency", ID_SELALPHA, s->kind == CL_PIXEL ? 0 : CTX_OFF);
  if (!s->has_mask) { bar(b); on(b, "Add Layer Mask", s->has_sel ? ID_MASK_REVEALSEL : ID_MASK_REVEALALL, !s->background); }
  bar(b);
  if (s->kind == CL_TEXT) { add(b, "Edit Type", ID_EDITTYPE, 0); add(b, "Rasterize Type", ID_RASTERTYPE, 0); }
  else if (s->kind == CL_SHAPE) { add(b, "Shape Properties...", ID_SHAPEPROPS, 0); add(b, "Rasterize", ID_RASTERSHAPE, 0); }
  else if (s->kind == CL_FILL || s->kind == CL_ADJUST) add(b, "Layer Content Options...", ID_FILLSETTINGS, 0);
  else add(b, "Layer Properties...", ID_LAYERPROPS, 0);
  return "Layer";
}
/* a layer mask's thumbnail (PhotoCraft #4575) */
static const char *mask_menu(Build *b, const CtxState *s)
{
  add(b, s->mask_on ? "Disable Layer Mask" : "Enable Layer Mask", ID_MASK_ENABLE, 0);
  add(b, "Delete Layer Mask", ID_MASK_DELETE, 0);
  on(b, "Apply Layer Mask", ID_MASK_APPLY, s->kind == CL_PIXEL); bar(b);
  add(b, "Invert Mask", ID_MASK_INVERT, 0); add(b, "Link/Unlink", ID_MASK_LINK, 0); bar(b);
  add(b, "Layer Mask", ID_SELFROMMASK, 0);                                /* Select > Layer Mask */
  add(b, "Select and Mask...", ID_FX_REFINE, s->has_sel ? 0 : CTX_OFF);
  return "Layer Mask";
}
static const char *channel_menu(Build *b, const CtxState *s)
{
  if (s->chan < 0 || s->chan >= s->nchan) { add(b, "Save Selection as Channel", ID_SAVESEL, 0); return "Channels"; }
  add(b, "Load Channel as Selection", ID_CHANLOAD, 0); add(b, "Rename Channel...", ID_CHANRENAME, 0);
  add(b, "Delete Channel", ID_CHANDEL, 0); bar(b);
  add(b, "Save Selection as Channel", ID_SAVESEL, 0);
  return "Channel";
}
static const char *path_menu(Build *b, const CtxState *s)
{
  if (!s->has_path) { add(b, "New Path", ID_PATHNEW, 0); on(b, "Make Work Path from Selection", ID_PATHFROMSEL, s->has_sel); return "Paths"; }
  add(b, "Make Selection", ID_PATHMKSEL, 0); on(b, "Fill Path", ID_PATHFILL, s->kind == CL_PIXEL);
  on(b, "Stroke Path", ID_PATHSTROKE, s->kind == CL_PIXEL); bar(b);
  if (s->work_path) add(b, "Save Path", ID_PATHRENAME, 0);
  else { add(b, "Duplicate Path", ID_PATHDUP, 0); add(b, "Rename Path", ID_PATHRENAME, 0); }
  add(b, "Delete Path", ID_PATHDEL, 0);
  return "Path";
}
static const char *tab_menu(Build *b, const CtxState *s)
{
  add(b, "Close", ID_CTX_TABCLOSE, 0); on(b, "Close Others", ID_CTX_TABOTHERS, s->ndocs > 1); add(b, "Close All", ID_CLOSEALL, 0); bar(b);
  add(b, "Duplicate", ID_CTX_TABDUP, 0); add(b, "File Info...", ID_CTX_TABINFO, 0);
  return "Document";
}

int ctx_menu(int target, const CtxState *s, const char **title, CtxItem *out, int max)
{
  Build b;
  const char *t = NULL;
  b.out = out; b.n = 0; b.max = max;
  if (!s || (!s->has_doc && target != CTX_DOCTAB)) { if (title) *title = NULL; return 0; }
  switch (target) {
  case CTX_CANVAS: t = canvas_menu(&b, s); break;
  case CTX_LAYERROW: t = layer_menu(&b, s); break;
  case CTX_LAYERTHUMB: t = thumb_menu(&b, s); break;
  case CTX_MASKTHUMB: t = s->has_mask ? mask_menu(&b, s) : thumb_menu(&b, s); break;
  case CTX_CHANNELROW: t = channel_menu(&b, s); break;
  case CTX_PATHROW: t = path_menu(&b, s); break;
  case CTX_DOCTAB: t = s->ndocs ? tab_menu(&b, s) : NULL; break;
  }
  if (title) *title = t;
  return t ? done(&b) : 0;
}
