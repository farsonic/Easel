/*
 * r3image.c - round 3's picture commands: Layer > Combine Shapes, Fill Path's dialog (colour,
 * pattern or history), the new filters' dialogs, Select Sky and Subject, and the type additions.
 * The work is in the core (core/vecx.c, ...); the dialogs are dialogs.c's slider dialog (dlgx.h),
 * previewed on the picture, and long work runs as the command's job (progress and Abort).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "lang.h"
#include "ids.h"
#include "dlgx.h"
#include "r3ids.h"
#include "../core/util.h"
#include "../core/vecx.h"
#include "../core/pathops.h"
#include "../core/shape.h"
#include "../core/brush.h"

static void out_of_memory(void) { if (!progress_aborted) ask("OK", "%s", tr("There isn't enough memory for that.")); }
/* enough memory for need bytes? If not, the user is told how much is needed */
static int mem_ok(long need)
{
  ULONG total = AvailMem(MEMF_ANY);
  char msg[300];
  if (need <= 0 || (ULONG)need + 65536UL <= total) return 1;
  snprintf(msg, sizeof msg, tr("That needs about %lu KB of memory, and %lu KB is free.\nClose other pictures or programs and try again."),
           (unsigned long)(need / 1024), (unsigned long)(total / 1024));
  ask("OK", "%s", msg);
  return 0;
}

/* ------------------------------------------------------------------ Fill Path */
static const Path *fp_path;
static void fp_apply(Doc *d, const LONG *v, int c, int t)
{
  static const int modes[] = { BM_NORMAL, BM_MULTIPLY, BM_SCREEN, BM_OVERLAY, BM_DARKEN, BM_LIGHTEN };
  const Snapshot *snap = curdoc >= 0 && curdoc < MAXDOCS ? docs[curdoc].snap : NULL;
  int m = (int)v[7];
  if (!fp_path) return;
  path_fill_source(d, fp_path, c, d->fg, tool_pattern(), snap, (int)v[0], modes[m >= 0 && m < 6 ? m : 0], t);
}
void r3_fill_path(Doc *d, const Path *p)
{
  static const DlgParam ps[1] = { { "Opacity (%):", 1, 100, 100 } };
  static const char *const contents[] = { "Foreground Color", "Pattern", "History", NULL };
  static const char *const modes[] = { "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten", NULL };
  static LONG init[8] = { 100, 0, 0, 0, 0, 0, 0, 0 };
  if (!d || !p) return;
  fp_path = p;
  if (!dlg_sliders("Fill Path", ps, 1, contents, "Contents:", 0, modes, "Mode:", 0, "Anti-alias", 1, init, fp_apply,
                   TextObject, MUIA_Text_Contents, (ULONG)tr("Pattern: the pattern chosen for the tools.\nHistory: the picture as it was opened (the History Brush's source)."), MUIA_Font, MUIV_Font_Tiny, End, 0))
    ;
  fp_path = NULL;
}

/* ------------------------------------------------------------------ Combine Shapes */
static void combine_shapes(Doc *d, int op)
{
  int i, n = 0;
  for (i = 0; i < d->nlay; i++) if (layer_shape(d->lay[i]) && (i == d->active || d->lay[i]->picked)) n++;
  if (!n || (op != CS_MERGE && n < 2)) {
    ask("OK", "%s", tr(op == CS_MERGE ? "Choose a shape layer first." : "Pick two or more shape layers in the Layers panel first."));
    return;
  }
  job_begin(d, tr(op == CS_MERGE ? "Merge Shape Components" : "Combine Shapes"));
  i = doc_combine_shapes(d, op);
  if (job_end()) return;
  if (i < 0) ask("OK", "%s", tr("Nothing is left of the shapes, or there isn't enough memory for that."));
}

int r3_command(Doc *d, ULONG id)
{
  if (id < ID_R3_FIRST || id > ID_R3_LAST) return 0;
  if (!d) return 1;
  if (id >= ID_COMBINE0 && id < ID_COMBINE0 + CS_N) { combine_shapes(d, (int)(id - ID_COMBINE0)); return 1; }
  return 1;
}
