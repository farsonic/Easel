/*
 * filesui.c - round 3's file commands in the interface: File > Open As's choice of reader (the work is
 * core/filesx.c's codec_load_as; main.c reads the file and shows the picture).
 */
#include <string.h>
#include <stdio.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <libraries/mui.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "lang.h"
#include "ids.h"
#include "theme.h"
#include "filesui.h"
#include "../core/filesx.h"

static ULONG xget(Object *o, ULONG a) { ULONG v = 0; get(o, a, &v); return v; }

/* (see filesui.h) */
int filesui_open_as_format(const char *file, int current)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  static const char *names[OA_N + 1];
  static char head[200];
  Object *w, *list;
  int i, k = -1;
  for (i = 0; i < OA_N; i++) names[i] = tr(open_as_names[i]);
  names[OA_N] = NULL;
  snprintf(head, sizeof head, tr("Open %s as:"), file);
  w = dialog_window("Open As", VGroup,
        Child, TextObject, MUIA_Text_Contents, (ULONG)head, End,
        Child, list = ListviewObject, MUIA_Listview_Input, TRUE, MUIA_CycleChain, 1, MUIA_Listview_DoubleClick, TRUE,
          MUIA_Listview_List, ListObject, InputListFrame, (ULONG)(theme_dialog_font() ? MUIA_Font : TAG_IGNORE), (ULONG)theme_dialog_font(),
            MUIA_List_SourceArray, (ULONG)names, MUIA_List_Active, current >= 0 && current < OA_N ? current : 0, End, End,
      End, "Open");
  if (!w) return -1;
  DoMethod(list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
  if (run_modal(w, ends, NULL) == ID_DLGOK) k = (int)(LONG)xget(list, MUIA_List_Active);
  MUI_DisposeObject(w);
  return k >= 0 && k < OA_N ? k : -1;
}
