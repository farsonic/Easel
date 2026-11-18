/*
 * ctxmui.h - 1.1: the right mouse button's menus in MUI (ctxmui.c; what they hold: ctxmenu.h).
 * A class with one calls ctx_attach in OM_NEW and ctx_detach in OM_DISPOSE, answers
 * MUIM_ContextMenuBuild with ctx_show (after making what was clicked the active thing, as a left
 * click would) and MUIM_ContextMenuChoice with ctx_choice. main.c hands ctx_command the commands.
 */
#ifndef EASEL_CTXMUI_H
#define EASEL_CTXMUI_H
#include "ctxmenu.h"

void    ctx_attach(Object *obj, int target);      /* CTX_ */
void    ctx_detach(Object *obj, int target);
int     ctx_tool_kind(int tool);                  /* TL_ -> CK_ */
void    ctx_state(CtxState *s);                   /* the picture, its active layer and the tool in use */
void    ctx_set_under(CtxState *s, const int *layers, int n);   /* the layers under the pointer, top first */
Object *ctx_show(int target, const CtxState *s);  /* the strip with target's menu in it, NULL for none */
ULONG   ctx_choice(Object *obj, Object *item);    /* MUIM_ContextMenuChoice */
/* the context menus' own commands (ID_CTX_): 1 if id was one; then *doc is a picture to switch to
   first (-1 none) and *then the menu command to carry out after it (0 none) */
int     ctx_command(ULONG id, int *doc, ULONG *then);
extern int ctx_tab;                               /* the tab right-clicked last */
#endif
