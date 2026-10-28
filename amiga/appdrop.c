/*
 * appdrop.c - Workbench drops on the window and the AppIcon, and the iconified icon (see
 * appdrop.h). Written from the MUI autodocs (MUIA_Window_AppWindow, MUIA_AppMessage,
 * MUIA_Application_DropObject, MUIA_Application_DiskObject) and workbench/startup.h.
 */
#include <string.h>
#include <exec/types.h>
#include <dos/dos.h>
#include <workbench/workbench.h>
#include <workbench/startup.h>
#include <libraries/mui.h>
/* a base of our own, so nothing clashes with another file's IconBase */
#define IconBase adrop_IconBase
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "appdrop.h"

struct Library *adrop_IconBase;
static struct DiskObject *app_icon;
static void (*drop_open)(const char *path);
static struct Hook drop_hook;

/* MUIM_CallHook's message: the arguments after the hook, here MUIV_TriggerValue (the AppMessage) */
static ULONG drop_func(__reg("a0") struct Hook *h, __reg("a2") Object *obj, __reg("a1") struct AppMessage **args)
{
  struct AppMessage *am = args ? *args : NULL;
  static char path[1024];
  LONG i;
  if (!am || !drop_open) return 0;
  for (i = 0; i < am->am_NumArgs; i++) {
    struct WBArg *wa = &am->am_ArgList[i];
    if (!wa->wa_Lock || !wa->wa_Name || !wa->wa_Name[0]) continue;     /* a drawer or a disk, not a file */
    if (!NameFromLock(wa->wa_Lock, (STRPTR)path, sizeof path) || !AddPart((STRPTR)path, wa->wa_Name, sizeof path)) continue;
    drop_open(path);
  }
  return 0;
}

void appdrop_attach(Object *win, Object *app, void (*open)(const char *path))
{
  drop_open = open;
  memset(&drop_hook, 0, sizeof drop_hook);
  drop_hook.h_Entry = (HOOKFUNC)drop_func;
  if (win) {
    set(win, MUIA_Window_AppWindow, TRUE);            /* init-only in MUI: the window should have it already */
    DoMethod(win, MUIM_Notify, MUIA_AppMessage, MUIV_EveryTime, (ULONG)win, 3, MUIM_CallHook, (ULONG)&drop_hook, MUIV_TriggerValue);
  }
  if (app) {
    if (win) set(app, MUIA_Application_DropObject, (ULONG)win);
    if (!app_icon && (IconBase = OpenLibrary((STRPTR)"icon.library", 37))) {
      struct Process *me = (struct Process *)FindTask(NULL);
      APTR pw = me->pr_WindowPtr;
      me->pr_WindowPtr = (APTR)-1;                    /* no "insert volume PROGDIR:" requester */
      app_icon = GetDiskObject((STRPTR)"PROGDIR:Easel");
      me->pr_WindowPtr = pw;
      if (!app_icon) app_icon = GetDefDiskObject(WBTOOL);
      if (app_icon) set(app, MUIA_Application_DiskObject, (ULONG)app_icon);
      else { CloseLibrary(IconBase); IconBase = NULL; }
    }
  }
}

void appdrop_free(void)
{
  if (app_icon) { FreeDiskObject(app_icon); app_icon = NULL; }
  if (IconBase) { CloseLibrary(IconBase); IconBase = NULL; }
  drop_open = NULL;
}
