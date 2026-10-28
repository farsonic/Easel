/*
 * channels.c - alpha channels and Quick Mask (see channels.h). Undo is doc.c's: doc_note_channels
 * copies the whole set before a change to it, doc_note_chan and doc_note_qmask note the tiles a
 * brush is about to paint.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "channels.h"

static int mul255(int a, int b) { int t = a * b + 128; return (t + (t >> 8)) >> 8; }

/* the selection's box worked out again; an empty mask is no selection */
static void update_box(Doc *d)
{
  i32 x, y;
  Box b = box_make(d->w, d->h, 0, 0);
  if (!d->sel) { d->selbox = doc_box(d); return; }
  for (y = 0; y < d->h; y++) {
    const u8 *row = d->sel + (long)y * d->w;
    for (x = 0; x < d->w; x++)
      if (row[x]) {
        if (x < b.x0) b.x0 = x;
        if (x >= b.x1) b.x1 = x + 1;
        if (y < b.y0) b.y0 = y;
        b.y1 = y + 1;
      }
  }
  if (box_empty(b)) { free(d->sel); d->sel = NULL; d->selbox = doc_box(d); return; }
  d->selbox = b;
}
/* plane a combined with b by op, into a */
static void combine(u8 *a, const u8 *b, long n, int op)
{
  long k;
  for (k = 0; k < n; k++) {
    int x = a[k], y = b[k];
    a[k] = (u8)(op == SEL_REPLACE ? y : op == SEL_ADD ? x + y - mul255(x, y) : op == SEL_SUBTRACT ? mul255(x, 255 - y) : mul255(x, y));
  }
}
void sel_apply_mask(Doc *d, u8 *m, int op, const char *name)
{
  long k, n = (long)d->w * d->h;
  if (!m && !(m = (u8 *)calloc((size_t)n, 1))) return;
  doc_begin(d, name);
  doc_note_sel(d);
  if (op == SEL_REPLACE || (!d->sel && (op == SEL_ADD || op == SEL_INTERSECT))) { free(d->sel); d->sel = m; }
  else if (!d->sel && op == SEL_SUBTRACT) { for (k = 0; k < n; k++) m[k] = (u8)(255 - m[k]); free(d->sel); d->sel = m; }
  else { combine(d->sel, m, n, op); free(m); }
  update_box(d);
  doc_end(d);
}

/* ------------------------------------------------------------------ channels */
static void set_name(Doc *d, int ch, const char *name)
{
  size_t l;
  char tmp[32];
  if (!name || !*name) {
    /* "Alpha N": the lowest N not in use */
    int n, i, used;
    for (n = 1; n < 1000; n++) {
      sprintf(tmp, "Alpha %d", n);
      for (used = 0, i = 0; i < d->nchan && !used; i++) used = i != ch && !strcmp(d->chan_name[i], tmp);
      if (!used) break;
    }
    name = tmp;
  }
  l = strlen(name);
  if (l > 31) { l = 31; while (l > 0 && ((u8)name[l] & 0xC0) == 0x80) l--; }   /* (cut between UTF-8 characters) */
  memmove(d->chan_name[ch], name, l);
  d->chan_name[ch][l] = 0;
}
/* plane p (taken) inserted as channel at, named; its index or -1 (p freed) */
static int insert_chan(Doc *d, u8 *p, int at, const char *name, const char *action)
{
  int i;
  if (d->nchan >= MAXCHANNELS) { free(p); return -1; }
  if (at < 0 || at > d->nchan) at = d->nchan;
  doc_begin(d, action);
  doc_note_channels(d);
  for (i = d->nchan; i > at; i--) { d->chan[i] = d->chan[i - 1]; memcpy(d->chan_name[i], d->chan_name[i - 1], 32); }
  d->chan[at] = p; d->chan_name[at][0] = 0;
  d->nchan++;
  set_name(d, at, name);
  doc_end(d);
  return at;
}
int chan_save_selection(Doc *d, int into, int op, const char *name)
{
  long n = (long)d->w * d->h;
  u8 *p = (u8 *)malloc((size_t)n);
  if (!p) return -1;
  if (d->sel) memcpy(p, d->sel, (size_t)n); else memset(p, 255, (size_t)n);
  if (into < 0 || into >= d->nchan) return insert_chan(d, p, -1, name, "Save Selection");
  doc_begin(d, "Save Selection");
  doc_note_chan(d, into, doc_box(d));
  combine(d->chan[into], p, n, op);
  doc_end(d);
  free(p);
  return into;
}
int chan_load_selection(Doc *d, int ch, int op, int invert)
{
  long k, n = (long)d->w * d->h;
  u8 *m;
  if (ch < 0 || ch >= d->nchan || !(m = (u8 *)malloc((size_t)n))) return 0;
  for (k = 0; k < n; k++) m[k] = invert ? (u8)(255 - d->chan[ch][k]) : d->chan[ch][k];
  sel_apply_mask(d, m, op, "Load Selection");
  return 1;
}
int chan_delete(Doc *d, int ch)
{
  int i;
  if (ch < 0 || ch >= d->nchan) return 0;
  doc_begin(d, "Delete Channel");
  doc_note_channels(d);
  free(d->chan[ch]);
  for (i = ch; i + 1 < d->nchan; i++) { d->chan[i] = d->chan[i + 1]; memcpy(d->chan_name[i], d->chan_name[i + 1], 32); }
  d->nchan--;
  d->chan[d->nchan] = NULL; d->chan_name[d->nchan][0] = 0;
  doc_end(d);
  return 1;
}
int chan_duplicate(Doc *d, int ch, const char *name)
{
  long n = (long)d->w * d->h;
  char nm[40];
  u8 *p;
  if (ch < 0 || ch >= d->nchan || d->nchan >= MAXCHANNELS || !(p = (u8 *)malloc((size_t)n))) return -1;
  memcpy(p, d->chan[ch], (size_t)n);
  if (!name || !*name) { sprintf(nm, "%.26s copy", d->chan_name[ch]); name = nm; }
  return insert_chan(d, p, ch + 1, name, "Duplicate Channel");
}
int chan_rename(Doc *d, int ch, const char *name)
{
  if (ch < 0 || ch >= d->nchan || !name || !*name || !strcmp(name, d->chan_name[ch])) return 0;
  doc_begin(d, "Channel Options");
  doc_note_channels(d);
  set_name(d, ch, name);
  doc_end(d);
  return 1;
}
int chan_move(Doc *d, int from, int to)
{
  u8 *p;
  char nm[32];
  int i;
  if (from < 0 || from >= d->nchan || to < 0 || to >= d->nchan || from == to) return 0;
  doc_begin(d, "Move Channel");
  doc_note_channels(d);
  p = d->chan[from]; memcpy(nm, d->chan_name[from], 32);
  if (from < to) for (i = from; i < to; i++) { d->chan[i] = d->chan[i + 1]; memcpy(d->chan_name[i], d->chan_name[i + 1], 32); }
  else for (i = from; i > to; i--) { d->chan[i] = d->chan[i - 1]; memcpy(d->chan_name[i], d->chan_name[i - 1], 32); }
  d->chan[to] = p; memcpy(d->chan_name[to], nm, 32);
  doc_end(d);
  return 1;
}
int chan_invert(Doc *d, int ch)
{
  long k, n = (long)d->w * d->h;
  if (ch < 0 || ch >= d->nchan) return 0;
  doc_begin(d, "Invert");
  doc_note_chan(d, ch, doc_box(d));
  for (k = 0; k < n; k++) d->chan[ch][k] = (u8)(255 - d->chan[ch][k]);
  doc_end(d);
  return 1;
}
int chan_from_layer(Doc *d, int layer, int which, const char *name)
{
  static const char *const names[] = { "Red", "Green", "Blue", "Luminosity", "Alpha" };
  long k, n = (long)d->w * d->h;
  const u8 *px;
  char nm[40];
  u8 *p;
  if (layer < 0 || layer >= d->nlay || !(px = d->lay[layer]->px) || which < CH_RED || which > CH_ALPHA) return -1;
  if (d->nchan >= MAXCHANNELS || !(p = (u8 *)malloc((size_t)n))) return -1;
  for (k = 0; k < n; k++) {
    const u8 *q = px + k * 4;
    p[k] = which <= CH_BLUE ? q[which] : which == CH_ALPHA ? q[3] : (u8)((q[0] * 77 + q[1] * 150 + q[2] * 29 + 128) >> 8);
  }
  if (!name || !*name) { sprintf(nm, "%s copy", names[which]); name = nm; }
  return insert_chan(d, p, -1, name, "New Channel");
}
/* a brush target's note callback gets one of these (one a channel: only one stroke runs at a time) */
typedef struct { Doc *d; int ch; } ChanCtx;
static ChanCtx chan_ctx[MAXCHANNELS];
static void note_chan_cb(void *ctx, Box r) { ChanCtx *c = (ChanCtx *)ctx; doc_note_chan(c->d, c->ch, r); }
int chan_target(Doc *d, int ch, PaintTarget *t)
{
  if (ch < 0 || ch >= d->nchan) return 0;
  chan_ctx[ch].d = d; chan_ctx[ch].ch = ch;
  t->px = d->chan[ch]; t->w = d->w; t->h = d->h; t->bpp = 1; t->alpha_lock = 0;
  t->note = note_chan_cb; t->ctx = &chan_ctx[ch];
  return 1;
}

/* ------------------------------------------------------------------ Quick Mask */
int quickmask_active(const Doc *d) { return d->qmask != NULL; }
int quickmask_enter(Doc *d)
{
  long n = (long)d->w * d->h;
  u8 *p;
  if (d->qmask || !(p = (u8 *)malloc((size_t)n))) return 0;
  if (d->sel) memcpy(p, d->sel, (size_t)n); else memset(p, 255, (size_t)n);
  doc_begin(d, "Quick Mask");
  doc_note_channels(d);
  doc_note_sel(d);
  d->qmask = p;
  free(d->sel); d->sel = NULL; d->selbox = doc_box(d);
  doc_end(d);
  return 1;
}
int quickmask_leave(Doc *d)
{
  long k, n = (long)d->w * d->h;
  u8 *p;
  int all = 1;
  if (!d->qmask || !(p = (u8 *)malloc((size_t)n))) return 0;
  memcpy(p, d->qmask, (size_t)n);
  doc_begin(d, "Quick Mask");
  doc_note_channels(d);
  doc_note_sel(d);
  free(d->qmask); d->qmask = NULL;
  for (k = 0; k < n && all; k++) all = p[k] == 255;
  free(d->sel);
  /* everything white: no selection. (All black ends the same way: Easel's empty mask is no selection) */
  if (all) { free(p); d->sel = NULL; d->selbox = doc_box(d); }
  else { d->sel = p; update_box(d); }
  doc_end(d);
  return 1;
}
static void note_qmask_cb(void *ctx, Box r) { doc_note_qmask((Doc *)ctx, r); }
int quickmask_target(Doc *d, PaintTarget *t)
{
  if (!d->qmask) return 0;
  t->px = d->qmask; t->w = d->w; t->h = d->h; t->bpp = 1; t->alpha_lock = 0;
  t->note = note_qmask_cb; t->ctx = d;
  return 1;
}
