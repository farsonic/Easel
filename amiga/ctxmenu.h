/*
 * ctxmenu.h - 1.1: the right mouse button's menus, as PhotoCraft has them (its
 * docs/context-menu-parity.md): on the picture, one for the tool in use and what is going on (a
 * selection, a pending transform or crop, a type layer ...); on a Layers panel row, its thumbnail or
 * its mask's thumbnail; on a Channels or Paths panel row; on a document tab.
 *
 * ctxmenu.c holds what each menu offers: plain C (no AmigaOS calls), so tests/ctxtest.c checks it
 * natively. Each item is the English label (translated when the menu is made) and the command it
 * sends, the same command id the menu bar's item sends (ids.h), so a context menu does nothing the
 * menus don't. ctxmui.c turns the items into MUI menus (MUIA_ContextMenu, MUIM_ContextMenuBuild)
 * and carries out the few commands only the context menus have (the CTX_ ids below).
 */
#ifndef EASEL_CTXMENU_H
#define EASEL_CTXMENU_H

#include "ids.h"
#ifndef EXEC_TYPES_H
typedef unsigned long ULONG;           /* (fxids.h's prototypes, when built without the NDK: the tests) */
#endif
#include "fxids.h"

/* where the right button was pressed */
enum { CTX_CANVAS, CTX_LAYERROW, CTX_LAYERTHUMB, CTX_MASKTHUMB, CTX_CHANNELROW, CTX_PATHROW, CTX_DOCTAB, CTX_N };
/* the tool in use, as far as its menu goes */
enum { CK_MOVE, CK_SELECT, CK_PAINT, CK_HISTORY, CK_FILL, CK_CROP, CK_EYEDROP, CK_SAMPLER, CK_RULER, CK_TYPE,
       CK_PATH, CK_SHAPE, CK_VIEW, CK_OTHER };
/* a layer's kind (core/doc.h's LK_, kept apart so this file needs no core header) */
enum { CL_PIXEL, CL_GROUP, CL_ADJUST, CL_FILL, CL_TEXT, CL_SHAPE };

/* the commands only the context menus send (ctxmui.c carries them out) */
enum {
  ID_CTX_LAYER0 = 6100,               /* .. + CTX_MAXUNDER - 1: choose the layer under the pointer */
  ID_CTX_SAMPLE0 = 6130,              /* .. + 6: the Eyedropper's and Color Sampler's sample size */
  ID_CTX_PICKFROM0 = 6140,            /* .. + 2: what the Eyedropper samples (current layer, and below, all) */
  ID_CTX_TABCLOSE = 6145, ID_CTX_TABOTHERS, ID_CTX_TABDUP, ID_CTX_TABINFO,   /* a tab's Close ... about that tab's picture */
  ID_CTX_LAST = 6150
};
#define CTX_MAXUNDER 16
#define CTX_NSAMPLE 7

/* an item: the English label (NULL: a separator line), the command, CTX_ flags */
#define CTX_OFF  1                    /* shown greyed */
#define CTX_TICK 2                    /* shown ticked */
#define CTX_SUB  4                    /* in a submenu of the last item without CTX_SUB */
typedef struct { const char *label; unsigned long id; int flags; } CtxItem;
#define CTX_MAXITEMS 64

/* what the menus depend on */
typedef struct {
  int tool;                           /* CK_ */
  int ctrl;                           /* Ctrl was held: the layers under the pointer, whatever the tool */
  int has_doc, has_sel, can_reselect, has_clip;   /* a picture; a selection; Reselect would work; something copied */
  int xform;                          /* a Free Transform is waiting to be applied */
  int crop;                           /* a crop box is waiting */
  int nunder; int under[CTX_MAXUNDER]; const char *under_name[CTX_MAXUNDER];   /* visible layers at the pointer, top first */
  int sample, pick_from;              /* the Eyedropper's sample size (0..6) and source (0..2) */
  int nsamplers;                      /* Color Sampler points placed */
  int ruler;                          /* the Ruler has a line */
  int has_path, work_path;            /* a path is chosen; it is the Work Path (not yet saved) */
  /* the active layer (or the one right-clicked, made active first) */
  int kind, background, has_mask, mask_on, has_fx, clipped, label, nlayers, index, npicked, hidden;
  int style_copied;                   /* Copy Layer Style has something to paste */
  int nchan, chan;                    /* alpha channels; the one right-clicked */
  int ndocs;                          /* pictures open */
} CtxState;

/* the menu for target in state s: its title (English) in *title and its items in out (at most max);
   returns how many, 0 for no menu */
int ctx_menu(int target, const CtxState *s, const char **title, CtxItem *out, int max);
/* the labels of the sample sizes (as the options bar has them) */
extern const char *const ctx_sample_names[CTX_NSAMPLE];

#endif
