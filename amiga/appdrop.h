/*
 * appdrop.h - files dropped on Easel from Workbench: on its window (an AppWindow) and, while it is
 * iconified, on its AppIcon; and the icon Easel shows when iconified (PROGDIR:Easel.info, else
 * Workbench's default tool icon).
 *
 * MUI does the Workbench side: a window made with MUIA_Window_AppWindow TRUE (an init-only
 * attribute, so it must be in the WindowObject's tags) sets its MUIA_AppMessage when icons are
 * dropped on it, and MUIA_Application_DropObject says which object gets the drops on the
 * AppIcon. appdrop_attach listens there and calls open() once for each file dropped, with its
 * full path (NameFromLock and AddPart of each WBArg).
 */
#ifndef EASEL_APPDROP_H
#define EASEL_APPDROP_H

#include <intuition/classusr.h>

/* win: the main window (made with MUIA_Window_AppWindow, TRUE); app: the application. Sets the
   iconified icon (MUIA_Application_DiskObject) and the AppIcon's drop object, and the notify that
   calls open(path) for each file dropped (from inside MUI's input handling, as any notify) */
void appdrop_attach(Object *win, Object *app, void (*open)(const char *path));
/* the icon freed and icon.library closed: after MUI_DisposeObject(app) */
void appdrop_free(void);

#endif
