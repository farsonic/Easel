/*
 * safetytest.c - Easel 0.8's safety nets in the core: undo steps spilled to a scratch drawer past the
 * memory budget and read back when undone (pixels, masks, the selection, channels and whole-document
 * copies, then redone), the undo levels limit, and the progress hook with Abort (a filter's step taken
 * back, Image Size leaving the picture as it was). Run under the address and undefined-behaviour
 * sanitizers.
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include "core/doc.h"
#include "core/paint.h"
#include "core/adjust.h"
#include "core/layers.h"
#include "core/filters2.h"
#include "core/channels.h"
#include "core/codec.h"
#include "core/palette.h"

static int fails, tests;
#define CHECK(c, msg) do { tests++; if (!(c)) { fails++; printf("FAIL %s (line %d)\n", msg, __LINE__); } } while (0)

static unsigned rnd = 4242;
static unsigned rn(void) { rnd = rnd * 1103515245u + 12345u; return (rnd >> 16) & 0x7fff; }

/* a copy of everything the steps change: size, the layers' pixels and masks, the selection, channels */
typedef struct { i32 w, h; int nlay, nchan; u8 *px[8], *mask[8], *sel, *chan[4]; } State;
static u8 *dupn(const u8 *p, size_t n) { u8 *q; if (!p) return NULL; q = (u8 *)malloc(n); memcpy(q, p, n); return q; }
static void state_take(Doc *d, State *s)
{
  int i;
  size_t n = (size_t)d->w * d->h;
  memset(s, 0, sizeof *s);
  s->w = d->w; s->h = d->h; s->nlay = d->nlay; s->nchan = d->nchan;
  for (i = 0; i < d->nlay && i < 8; i++) { s->px[i] = dupn(d->lay[i]->px, n * 4); s->mask[i] = dupn(d->lay[i]->mask, n); }
  s->sel = dupn(d->sel, n);
  for (i = 0; i < d->nchan && i < 4; i++) s->chan[i] = dupn(d->chan[i], n);
}
static int same_buf(const u8 *a, const u8 *b, size_t n) { if (!a || !b) return a == b; return !memcmp(a, b, n); }
static int state_same(Doc *d, const State *s)
{
  int i;
  size_t n = (size_t)d->w * d->h;
  if (d->w != s->w || d->h != s->h || d->nlay != s->nlay || d->nchan != s->nchan) return 0;
  for (i = 0; i < d->nlay && i < 8; i++) if (!same_buf(d->lay[i]->px, s->px[i], n * 4) || !same_buf(d->lay[i]->mask, s->mask[i], n)) return 0;
  if (!same_buf(d->sel, s->sel, n)) return 0;
  for (i = 0; i < d->nchan && i < 4; i++) if (!same_buf(d->chan[i], s->chan[i], n)) return 0;
  return 1;
}
static void state_free(State *s)
{
  int i;
  for (i = 0; i < 8; i++) { free(s->px[i]); free(s->mask[i]); }
  for (i = 0; i < 4; i++) free(s->chan[i]);
  free(s->sel);
}
/* a box of the active layer painted with noise, as a step of its own */
static void paint_step(Doc *d)
{
  i32 x0 = (i32)(rn() % (d->w - 40)), y0 = (i32)(rn() % (d->h - 40)), x, y;
  Box b = box_make(x0, y0, x0 + 20 + (i32)(rn() % 20), y0 + 20 + (i32)(rn() % 20));
  doc_begin(d, "Paint");
  doc_note_layer(d, d->active, b);
  for (y = b.y0; y < b.y1; y++) for (x = b.x0; x < b.x1; x++) { u8 *p = d->lay[d->active]->px + ((long)y * d->w + x) * 4; p[0] = (u8)rn(); p[1] = (u8)rn(); p[2] = (u8)rn(); p[3] = 255; }
  doc_end(d);
}
/* the EaselUndo files in a drawer */
static int scratch_files(const char *dir)
{
  DIR *dd = opendir(dir);
  struct dirent *e;
  int n = 0;
  if (!dd) return -1;
  while ((e = readdir(dd))) if (!strncmp(e->d_name, "EaselUndo-", 10)) n++;
  closedir(dd);
  return n;
}

#define NSTEPS 24
static void test_scratch(void)
{
  char dir[] = "/tmp/easel_scratchXXXXXX";
  State st[NSTEPS + 1];
  Doc *d;
  int i, k, ok;
  long saved_budget = undo_budget;
  CHECK(mkdtemp(dir) != NULL, "scratch: a temporary drawer");
  strcpy(undo_scratch, dir);
  undo_budget = 200L * 1024;                  /* (a little over one 200 x 200 copy of the document) */
  d = doc_new(200, 200, 0x336699, 0);
  doc_add_layer(d, layer_new(d, "Layer 1"), 1);
  doc_forget_undo(d);
  state_take(d, &st[0]);
  for (i = 1; i <= NSTEPS; i++) {
    int depth = d->undo_depth;
    switch (i % 8) {
    case 3: sel_rect(d, 10 + i, 10, 120, 90 + i, SEL_REPLACE, 0); break;          /* the selection */
    case 5: doc_mask_add(d, d->active, MK_REVEAL_SEL); break;                      /* a mask */
    case 6: chan_save_selection(d, -1, 0, "Alpha"); break;                          /* a channel */
    case 7: image_resize(d, d->w + 8, d->h - 4, RS_BILINEAR); break;                 /* a whole copy */
    default: paint_step(d); break;
    }
    if (d->undo_depth == depth) paint_step(d);   /* (that made no step: a painted one instead) */
    state_take(d, &st[i]);
  }
  CHECK(doc_spilled_steps(d) > 0, "scratch: steps were spilled past the budget");
  CHECK(scratch_files(dir) == doc_spilled_steps(d), "scratch: one file for each spilled step");
  CHECK(undo_scratch_bytes > 0, "scratch: its size counted");
  CHECK(d->undo_bytes <= undo_budget + 200L * 200 * 8, "scratch: memory kept near the budget");
  /* every step undone, each state checked, then redone */
  for (ok = 1, i = NSTEPS; i > 0; i--) {
    if (!doc_undo(d)) { ok = 0; break; }
    if (!state_same(d, &st[i - 1])) { printf("  undo to %d differs\n", i - 1); ok = 0; break; }
  }
  CHECK(ok, "scratch: every step undone from the drawer, exactly");
  CHECK(!doc_can_undo(d), "scratch: back at the start");
  for (ok = 1, i = 1; i <= NSTEPS; i++) {
    if (!doc_redo(d)) { ok = 0; break; }
    if (!state_same(d, &st[i])) { printf("  redo to %d differs\n", i); ok = 0; break; }
  }
  CHECK(ok, "scratch: and redone, exactly");
  /* more steps spill again; freeing the document deletes the files */
  for (k = 0; k < 6; k++) paint_step(d);
  CHECK(scratch_files(dir) > 0, "scratch: spilled again");
  doc_free(d);
  CHECK(scratch_files(dir) == 0, "scratch: files deleted with the document");
  CHECK(undo_scratch_bytes == 0, "scratch: nothing counted any more");
  for (i = 0; i <= NSTEPS; i++) state_free(&st[i]);
  /* a drawer that can't be written: steps are forgotten as before */
  strcpy(undo_scratch, "/nonexistent-easel-drawer");
  d = doc_new(200, 200, 0, 0);
  for (k = 0; k < 20; k++) paint_step(d);
  CHECK(doc_spilled_steps(d) == 0 && doc_can_undo(d), "scratch: an unwritable drawer forgets steps");
  doc_free(d);
  undo_scratch[0] = 0;
  rmdir(dir);
  undo_budget = saved_budget;
}
static void test_levels(void)
{
  Doc *d = doc_new(100, 100, 0, 0);
  int k, n = 0;
  undo_levels = 5;
  for (k = 0; k < 12; k++) paint_step(d);
  while (doc_undo(d)) n++;
  CHECK(n == 5, "levels: only the last five steps kept");
  undo_levels = 100;
  doc_free(d);
}

/* ------------------------------------------------------------------ progress and Abort */
static long calls, stop_at, last_done;
static int monotone;
static int hook(long done, long total)
{
  if (done < last_done || done > total) monotone = 0;
  last_done = done;
  return ++calls >= stop_at;
}
static void test_progress(void)
{
  Doc *d = doc_new(300, 200, 0, 0);
  State before;
  long x;
  for (x = 0; x < 300L * 200 * 4; x++) d->lay[0]->px[x] = (u8)rn();
  doc_forget_undo(d);
  state_take(d, &before);
  progress_hook = hook;
  /* told along the way, never backwards */
  calls = 0; stop_at = 1L << 30; last_done = 0; monotone = 1; progress_reset();
  flt_gaussian_blur(d, 4.0);
  CHECK(calls > 10 && monotone && !progress_aborted, "progress: Gaussian Blur reports as it goes");
  CHECK(last_done >= 990, "progress: and the count reaches the end");
  doc_discard_last(d);
  CHECK(state_same(d, &before), "progress: (taken back)");
  /* aborted part way: the loops stop; the step is taken back as the UI does */
  calls = 0; stop_at = 5; last_done = 0; progress_reset();
  flt_gaussian_blur(d, 4.0);
  CHECK(progress_aborted && calls == 5, "progress: Abort stops Gaussian Blur");
  if (doc_can_undo(d)) doc_discard_last(d);
  CHECK(state_same(d, &before), "progress: the aborted blur taken back");
  calls = 0; stop_at = 3; last_done = 0; progress_reset();
  CHECK(!image_resize(d, 600, 400, RS_BICUBIC), "progress: Abort stops Image Size");
  CHECK(!doc_can_undo(d) && state_same(d, &before), "progress: the picture as it was, no step");
  calls = 0; stop_at = 4; last_done = 0; progress_reset();
  adj_invert(d);
  CHECK(progress_aborted, "progress: Abort stops an adjustment");
  if (doc_can_undo(d)) doc_discard_last(d);
  CHECK(state_same(d, &before), "progress: the aborted adjustment taken back");
  /* every kind of long operation stops at Abort and leaves the picture as it was */
  {
    static const char *const what[] = { "Motion Blur", "Lens Blur", "Rotate 33", "Canvas Size", "Indexed Color", "Merge Visible", "Feather", "Levels", "Wave" };
    int k;
    doc_add_layer(d, layer_new(d, "Layer 1"), 1);
    for (x = 0; x < 300L * 200 * 4; x++) d->lay[1]->px[x] = (u8)rn();
    d->active = 0;
    sel_rect(d, 20, 20, 250, 180, SEL_REPLACE, 0);
    doc_forget_undo(d);
    state_free(&before); state_take(d, &before);
    for (k = 0; k < 9; k++) {
      int depth = d->undo_depth;
      calls = 0; stop_at = 3; last_done = 0; progress_reset();
      switch (k) {
      case 0: flt_motion_blur(d, 30, 20); break;
      case 1: flt_lens_blur(d, 8, 0, 255); break;
      case 2: image_rotate_angle(d, 33); break;
      case 3: canvas_resize(d, 500, 400, 4, 0xFFFFFF); break;
      case 4: doc_to_indexed(d, 64, QM_WU, DI_FS, NULL, 0); break;
      case 5: doc_merge_visible(d); break;
      case 6: sel_feather(d, 40); break;
      case 7: adj_levels(d, 0, 20, 230, 1.2, 0, 255); break;
      case 8: flt_wave(d, 40, 10); break;
      }
      if (!progress_aborted) printf("  %s wasn't stopped\n", what[k]);
      CHECK(progress_aborted, "progress: Abort stops each kind of operation");
      if (d->undo_depth > depth) doc_discard_last(d);   /* (as the UI's job_end does) */
      if (!state_same(d, &before)) printf("  %s changed the picture\n", what[k]);
      CHECK(state_same(d, &before), "progress: and leaves the picture as it was");
    }
  }
  /* reading and writing: aborted, nothing comes of them */
  {
    size_t len = 0;
    u8 *png, *ora;
    char err[100];
    progress_hook = NULL; progress_reset();
    png = png_write_doc(d, &len);
    CHECK(png != NULL, "progress: (a PNG to read)");
    calls = 0; stop_at = 2; last_done = 0; progress_hook = hook; progress_reset();
    CHECK(codec_load_doc(png, len, err, sizeof err) == NULL && progress_aborted, "progress: Abort stops opening a picture");
    calls = 0; stop_at = 1L << 30; last_done = 0; monotone = 1; progress_reset();
    { Doc *t = codec_load_doc(png, len, err, sizeof err); CHECK(t && calls > 3, "progress: opening reports as it goes"); doc_free(t); }
    free(png);
    calls = 0; stop_at = 2; last_done = 0; progress_reset();
    CHECK(png_write_doc(d, &len) == NULL, "progress: Abort stops saving a PNG");
    calls = 0; stop_at = 6; last_done = 0; progress_reset();
    CHECK((ora = ora_write(d, &len)) == NULL, "progress: Abort stops saving an OpenRaster file");
    free(ora);
    calls = 0; stop_at = 1L << 30; last_done = 0; monotone = 1; progress_reset();
    ora = ora_write(d, &len);
    CHECK(ora && monotone && last_done >= 900, "progress: saving an OpenRaster file reports in order to the end");
    free(ora);
  }
  /* no hook: nothing stops */
  progress_hook = NULL; progress_reset();
  CHECK(image_resize(d, 150, 100, RS_BILINEAR) && d->w == 150, "progress: without a hook all is done");
  state_free(&before);
  doc_free(d);
}

int main(void)
{
  test_scratch();
  test_levels();
  test_progress();
  printf("safetytest: %d/%d passed\n", tests - fails, tests);
  return fails != 0;
}
