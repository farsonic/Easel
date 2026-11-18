/*
 * typetest.c - Easel 0.9's type layers: drawing against the old one-shot Text tool, kerning, word
 * wrap in a box, alignment and justification, runs of different sizes and colours, faux styles,
 * the caret and hit testing (round trips, cursor keys), Warp Text staying in its bounds, vertical
 * type, Rasterize Type, undo and redo, OpenRaster, the Type Mask, Create Work Path, crops and
 * resizes, and Amiga bitmap fonts. Uses Liberation Sans from the system (the tests that need a
 * TrueType font are skipped if it isn't there). Run under the address and undefined-behaviour
 * sanitizers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "core/doc.h"
#include "core/paint.h"
#include "core/layers.h"
#include "core/codec.h"
#include "core/type.h"
#include "core/text.h"
#include "core/pathops.h"
#include "core/live.h"
#include "core/amifont.h"

static int fails, tests;
#define CHECK(c, msg) do { tests++; if (!(c)) { fails++; printf("FAIL %s (line %d)\n", msg, __LINE__); } } while (0)

static const char *const font_paths[] = {
  "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
  "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
  "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
  "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", NULL };
static const char *const bold_paths[] = {
  "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
  "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf", NULL };
static FontSet *fs;
static Face *face;
#define FAM "Sans"

static u8 *composite(Doc *d)
{
  u8 *o = (u8 *)malloc((size_t)d->w * d->h * 4);
  doc_composite(d, doc_box(d), o, (long)d->w * 4);
  return o;
}
static int maxdiff(const u8 *a, const u8 *b, long n)
{
  long k;
  int m = 0;
  for (k = 0; k < n; k++) { int v = abs(a[k] - b[k]); if (v > m) m = v; }
  return m;
}
static TextParams *newtp(const char *s, double size, u32 color, double x, double y)
{
  TextParams *tp = (TextParams *)malloc(sizeof *tp);
  text_params_init(tp, FAM, size, color, x, y);
  text_set_string(tp, s);
  return tp;
}
/* the extent of the opaque pixels of a rendering placed on a canvas */
static Box ink(const u8 *px, Box b)
{
  Box r = box_make(0, 0, 0, 0);
  i32 x, y;
  if (!px) return r;
  for (y = b.y0; y < b.y1; y++)
    for (x = b.x0; x < b.x1; x++)
      if (px[((size_t)(y - b.y0) * (b.x1 - b.x0) + (x - b.x0)) * 4 + 3] > 8) r = box_union(r, box_make(x, y, x + 1, y + 1));
  return r;
}
static long ink_count(const u8 *px, Box b)
{
  long n = 0, k, m = (long)(b.x1 - b.x0) * (b.y1 - b.y0);
  if (!px) return 0;
  for (k = 0; k < m; k++) n += px[k * 4 + 3];
  return n / 255;
}

static void test_editing(void)
{
  TextParams *tp = newtp("Hello world", 20, 0, 0, 0);
  TextRun st;
  memset(&st, 0, sizeof st);
  st.color = 0xFF0000; st.size = 40;
  CHECK(text_style(tp, 6, 11, &st, TS_COLOR | TS_SIZE) && tp->nruns == 2 && tp->run[1].start == 6 && tp->run[1].size == 40, "style splits a run");
  CHECK(text_run_at(tp, 3) == 0 && text_run_at(tp, 8) == 1 && text_run_at(tp, 6) == 0, "run at a caret position");
  text_insert(tp, 6, "big ", -1);
  CHECK(!strcmp(tp->text, "Hello big world") && tp->run[1].start == 10, "insert at a boundary joins the run before");
  text_insert(tp, 12, "!!", -1);
  CHECK(tp->run[1].start == 10 && !strcmp(tp->text, "Hello big wo!!rld"), "insert inside a run");
  text_insert(tp, 0, ">", -1);
  CHECK(tp->run[0].start == 0 && tp->run[1].start == 11, "insert at the start");
  text_delete(tp, 5, 13);
  CHECK(!strcmp(tp->text, ">Hell!!rld") && tp->nruns == 2 && tp->run[1].start == 5, "delete across runs");
  text_delete(tp, 5, 10);
  CHECK(!strcmp(tp->text, ">Hell") && tp->nruns == 1, "the run deleted with its text");
  text_insert(tp, 5, "o", 1);
  st.color = 0;  st.size = 20;
  text_style(tp, 0, 6, &st, TS_COLOR | TS_SIZE);
  CHECK(tp->nruns == 1, "alike runs merged");
  text_style(tp, 1, 3, &st, TS_SIZE);
  CHECK(tp->nruns == 1, "nothing changed, nothing split");
  st.underline = 1;
  text_style(tp, 1, 3, &st, TS_UNDERLINE);
  CHECK(tp->nruns == 3 && tp->run[1].underline && tp->run[1].start == 1 && tp->run[2].start == 3, "three runs");
  text_delete(tp, 0, 6);
  CHECK(tp->text[0] == 0 && tp->nruns == 1, "everything deleted: one run");
  {
    TextParams *big = newtp("", 10, 0, 0, 0);
    int i, ok = 1;
    for (i = 0; i < 5000 && ok; i++) ok = text_insert(big, i, "x", 1);
    CHECK(!ok && (int)strlen(big->text) == TP_MAXTEXT - 1, "inserting stops when full");
    free(big);
  }
  free(tp);
}

static void test_old_tool(void)
{
  Doc *d = doc_new(320, 90, 0xFFFFFF, 1);
  TextParams *tp;
  Box b, rb;
  u8 *px;
  int li, same = 1;
  i32 x, y, asc = (i32)((long)face->ascent * 32 / face->upm);
  b = text_layer(d, face, "Hello Easel, AV", 32, 0x204080, 10, 10, TA_LEFT, 1);
  CHECK(!box_empty(b), "old text tool drew");
  li = d->active;
  tp = newtp("Hello Easel, AV", 32, 0x204080, 10, 10 + asc);
  tp->kern = 0;
  CHECK(text_render(tp, d->w, d->h, &px, &rb) && px, "type rendered");
  for (y = 0; y < d->h && same; y++)
    for (x = 0; x < d->w && same; x++) {
      const u8 *o = d->lay[li]->px + ((size_t)y * d->w + x) * 4;
      u8 n[4] = { 0, 0, 0, 0 };
      if (px && x >= rb.x0 && x < rb.x1 && y >= rb.y0 && y < rb.y1) memcpy(n, px + ((size_t)(y - rb.y0) * (rb.x1 - rb.x0) + (x - rb.x0)) * 4, 4);
      if (o[3] != n[3] || (o[3] && memcmp(o, n, 3))) same = 0;
    }
  CHECK(same, "the type layer draws what the old Text tool drew (kerning off)");
  free(px);
  /* kerning narrows AV */
  {
    double w0, w1, h;
    TextParams *k = newtp("AVAVAV", 40, 0, 0, 50);
    u16 ga = ttf_glyph(face, 'A'), gv = ttf_glyph(face, 'V');
    CHECK(text_kern(face, ga, gv) < 0, "the font kerns A-V");
    CHECK(text_kern(face, ga, ga) == 0, "and not A-A");
    k->kern = 0; text_extent(k, &w0, &h);
    k->kern = 1; text_extent(k, &w1, &h);
    CHECK(w1 < w0 - 5, "kerned text is narrower");
    free(k);
  }
  free(tp);
  doc_free(d);
}

static void test_layout(void)
{
  TextParams *tp = newtp("The quick brown fox jumps over the lazy dog and keeps running far away", 20, 0, 0, 0);
  TextLayout *tl;
  int l, ok = 1;
  double w, h;
  tp->box = 1; tp->x = 30; tp->y = 20; tp->bw = 160; tp->bh = 400;
  tl = text_layout(tp);
  CHECK(tl && tl->nl >= 3, "wrapped into lines");
  for (l = 0; tl && l < tl->nl; l++) {
    if (tl->ln[l].x1 - tl->ln[l].x0 > 160 + 1e-6 || tl->ln[l].x0 < 30 - 1e-6) ok = 0;
    if (l && tl->ln[l].base <= tl->ln[l - 1].base) ok = 0;
    if (l && tl->ln[l].start != tl->ln[l - 1].end) ok = 0;
  }
  CHECK(ok, "every line inside the box, one below another, the text continuous");
  text_layout_free(tl);
  {
    u8 *px;
    Box rb, in;
    text_render(tp, 400, 300, &px, &rb);
    in = ink(px, rb);
    CHECK(in.x0 >= 28 && in.x1 <= 30 + 160 + 2 && in.y0 >= 20, "drawn inside the box");
    free(px);
  }
  /* alignment */
  tp->align = TA_CENTER;
  tl = text_layout(tp);
  for (ok = 1, l = 0; l < tl->nl; l++) if (fabs((tl->ln[l].x0 + tl->ln[l].x1) / 2 - 110) > 0.01) ok = 0;
  CHECK(ok, "centred lines");
  text_layout_free(tl);
  tp->align = TA_RIGHT;
  tl = text_layout(tp);
  for (ok = 1, l = 0; l < tl->nl; l++) if (fabs(tl->ln[l].x1 - 190) > 0.01) ok = 0;
  CHECK(ok, "right aligned lines");
  text_layout_free(tl);
  tp->align = TA_JUSTIFY;
  tl = text_layout(tp);
  for (ok = 1, l = 0; l < tl->nl - 1; l++) if (fabs(tl->ln[l].x1 - 190) > 0.01 || fabs(tl->ln[l].x0 - 30) > 0.01) ok = 0;
  CHECK(ok, "justified lines fill the box");
  CHECK(tl->ln[tl->nl - 1].x1 < 189, "the last line left as it is");
  text_layout_free(tl);
  tp->align = TA_JUSTIFY_ALL;
  tl = text_layout(tp);
  {
    const TLine *L = &tl->ln[tl->nl - 1];
    int sp = 0, k;
    for (k = L->start; k < L->end - 1; k++) sp |= tp->text[k] == ' ';
    CHECK(!sp || fabs(L->x1 - 190) < 0.01, "justify all: the last line too");
    CHECK(fabs(tl->ln[0].x1 - 190) < 0.01, "justify all: the first line");
  }
  text_layout_free(tl);
  /* point text: newlines only, aligned on x */
  text_params_init(tp, FAM, 20, 0, 100, 50);
  text_set_string(tp, "one\ntwo two\n\nfour");
  tp->align = TA_RIGHT;
  tl = text_layout(tp);
  CHECK(tl->nl == 4 && tl->ln[2].start == tl->ln[2].end && fabs(tl->ln[1].x1 - 100) < 1e-9 && fabs(tl->ln[0].base - 50) < 1e-9, "point text lines, right aligned at x, the first baseline at y");
  text_layout_free(tl);
  /* leading and paragraph spacing */
  tp->leading = 50; tp->space_after = 7; tp->space_before = 3;
  tl = text_layout(tp);
  CHECK(fabs(tl->ln[1].base - tl->ln[0].base - 60) < 1e-9 && fabs(tl->ln[3].base - tl->ln[2].base - 60) < 1e-9, "fixed leading plus paragraph spacing");
  text_layout_free(tl);
  tp->leading = 0; tp->space_after = tp->space_before = 0;
  /* overflow: a box too short hides the lines past its bottom */
  text_params_init(tp, FAM, 20, 0, 0, 0);
  text_set_string(tp, "aaa bbb ccc ddd eee fff ggg hhh");
  tp->box = 1; tp->bw = 60; tp->bh = 45;
  tl = text_layout(tp);
  CHECK(tl->nl > 3 && !tl->ln[0].hidden && tl->ln[tl->nl - 1].hidden, "lines past the box hidden");
  text_layout_free(tl);
  /* indent */
  tp->bh = 0; tp->indent = 15; tp->x = 5;
  tl = text_layout(tp);
  CHECK(fabs(tl->ln[0].x0 - 20) < 1e-9 && fabs(tl->ln[1].x0 - 5) < 1e-9, "first-line indent");
  text_layout_free(tl);
  CHECK(text_extent(tp, &w, &h) && w > 0 && h > 0, "extent");
  free(tp);
}

static void test_runs(void)
{
  Doc *d = doc_new(400, 120, 0xFFFFFF, 0);
  TextParams *tp = newtp("small BIG", 20, 0xFF0000, 10, 80);
  TextRun st;
  TextLayout *tl;
  u8 *c;
  int red = 0, blue = 0, i;
  i32 x, y, ytop_red = 999, ytop_blue = 999;
  memset(&st, 0, sizeof st);
  st.size = 60; st.color = 0x0000FF;
  text_style(tp, 6, 9, &st, TS_SIZE | TS_COLOR);
  tl = text_layout(tp);
  CHECK(tl->nl == 1 && tl->ln[0].ascent > 50 * (double)face->ascent / face->upm, "the line as tall as its biggest run");
  text_layout_free(tl);
  i = doc_new_text(d, tp, NULL);
  CHECK(i == 1 && d->lay[1]->kind == LK_TEXT && !strcmp(d->lay[1]->name, "small BIG"), "type layer named after its text");
  c = composite(d);
  for (y = 0; y < d->h; y++)
    for (x = 0; x < d->w; x++) {
      const u8 *p = c + ((size_t)y * d->w + x) * 4;
      if (p[0] > 200 && p[1] < 60 && p[2] < 60) { red++; if (y < ytop_red) ytop_red = y; }
      if (p[2] > 200 && p[0] < 60 && p[1] < 60) { blue++; if (y < ytop_blue) ytop_blue = y; }
    }
  CHECK(red > 50 && blue > 300, "both colours drawn");
  CHECK(ytop_blue < ytop_red - 20, "the big run is taller");
  free(c);
  /* faux styles and lines add ink */
  {
    u8 *px;
    Box b;
    long base, more;
    TextParams *t2 = newtp("Faux", 40, 0, 10, 60);
    text_render(t2, 400, 120, &px, &b); base = ink_count(px, b); free(px);
    t2->run[0].faux_bold = 1;
    text_render(t2, 400, 120, &px, &b); more = ink_count(px, b); free(px);
    CHECK(more > base * 11 / 10, "faux bold is heavier");
    t2->run[0].faux_bold = 0; t2->run[0].faux_italic = 1;
    text_render(t2, 400, 120, &px, &b);
    CHECK(abs((int)(ink_count(px, b) - base)) < base / 10 && ink(px, b).x1 > 10, "faux italic: about the same ink, slanted");
    free(px);
    t2->run[0].faux_italic = 0; t2->run[0].underline = 1;
    text_render(t2, 400, 120, &px, &b);
    {
      Box in = ink(px, b);
      CHECK(in.y1 > 60 + 3 && ink_count(px, b) > base, "underlined below the baseline");
    }
    free(px);
    t2->run[0].underline = 0; t2->run[0].strike = 1;
    text_render(t2, 400, 120, &px, &b); more = ink_count(px, b); free(px);
    CHECK(more > base, "struck through");
    t2->run[0].strike = 0; t2->aa = TAA_NONE;
    text_render(t2, 400, 120, &px, &b);
    {
      long k, part = 0;
      for (k = 0; k < (long)(b.x1 - b.x0) * (b.y1 - b.y0); k++) if (px[k * 4 + 3] && px[k * 4 + 3] != 255) part++;
      CHECK(part == 0, "no anti-aliasing: hard edges");
    }
    free(px);
    t2->aa = TAA_SHARP;
    CHECK(text_render(t2, 400, 120, &px, &b) && px, "sharp (hinted) draws");
    free(px);
    /* a big size goes through the outlines */
    t2->aa = TAA_SMOOTH; t2->run[0].size = 600; t2->x = 0; t2->y = 500;
    CHECK(text_render(t2, 2000, 700, &px, &b) && px && ink(px, b).y1 - ink(px, b).y0 > 300, "600-pixel type drawn");
    free(px);
    free(t2);
  }
  free(tp);
  doc_free(d);
}

static void test_caret(void)
{
  TextParams *tp = newtp("First line\nSecond, longer line here", 24, 0, 20, 40);
  TextLayout *tl;
  int i, n = (int)strlen(tp->text), ok = 1, q;
  double x0, y0, x1, y1, quads[8 * 8];
  tl = text_layout(tp);
  for (i = 0; i <= n; i++) {
    if (!text_caret_pos(tl, i, &x0, &y0, &x1, &y1)) { ok = 0; break; }
    if (text_hit(tl, x0, (y0 + y1) / 2) != i) { ok = 0; printf("  caret %d -> hit %d\n", i, text_hit(tl, x0, (y0 + y1) / 2)); }
  }
  CHECK(ok, "caret position -> hit gives the same index");
  CHECK(!text_caret_pos(tl, n + 1, &x0, &y0, &x1, &y1), "no caret past the end");
  text_caret_pos(tl, 0, &x0, &y0, &x1, &y1);
  CHECK(fabs(x0 - 20) < 1e-9 && y0 < 40 && y1 > 40, "the caret at the start crosses the baseline");
  CHECK(text_hit(tl, 0, 0) == 0 && text_hit(tl, 1000, 40) == 10 && text_hit(tl, 1000, 1000) == n, "hits outside the text");
  CHECK(text_caret_move(tl, 3, TCM_DOWN) > 11 && text_caret_move(tl, 3, TCM_DOWN) < 16, "down a line keeps the column");
  CHECK(text_caret_move(tl, 20, TCM_UP) <= 10, "up a line");
  CHECK(text_caret_move(tl, 15, TCM_HOME) == 11 && text_caret_move(tl, 15, TCM_END) == n, "home and end");
  CHECK(text_caret_move(tl, 0, TCM_LEFT) == 0 && text_caret_move(tl, 0, TCM_RIGHT) == 1, "left and right");
  CHECK(text_caret_move(tl, 0, TCM_WORDRIGHT) == 6 && text_caret_move(tl, 10, TCM_WORDLEFT) == 6, "word moves");
  CHECK(text_caret_move(tl, 5, TCM_END_ALL) == n && text_caret_move(tl, 5, TCM_START) == 0, "start and end of the text");
  q = text_sel_quads(tl, 2, 15, quads, 8);
  CHECK(q == 2 && quads[0] > 20 && quads[8 + 0] == 20, "selection highlight: two quads");
  text_layout_free(tl);
  /* UTF-8: carets between characters */
  text_set_string(tp, "na\xC3\xAFve caf\xC3\xA9");
  tl = text_layout(tp);
  CHECK(text_caret_move(tl, 2, TCM_RIGHT) == 4 && text_caret_move(tl, 4, TCM_LEFT) == 2, "a two-byte character is one step");
  for (ok = 1, i = 0; i <= (int)strlen(tp->text); i = text_caret_move(tl, i, TCM_RIGHT)) {
    text_caret_pos(tl, i, &x0, &y0, &x1, &y1);
    if (text_hit(tl, x0, (y0 + y1) / 2) != i) ok = 0;
    if (i == (int)strlen(tp->text)) break;
  }
  CHECK(ok, "UTF-8 round trip");
  text_layout_free(tl);
  /* wrapped and transformed */
  text_set_string(tp, "wrap these words around inside the box please");
  tp->box = 1; tp->x = 10; tp->y = 10; tp->bw = 120; tp->bh = 0;
  tp->xf_on = 1; tp->xf[0] = cos(0.3); tp->xf[1] = -sin(0.3); tp->xf[2] = 50; tp->xf[3] = sin(0.3); tp->xf[4] = cos(0.3); tp->xf[5] = 5;
  tl = text_layout(tp);
  for (ok = 1, i = 0; i <= (int)strlen(tp->text); i++) {
    int h;
    text_caret_pos(tl, i, &x0, &y0, &x1, &y1);
    h = text_hit(tl, (x0 + x1) / 2, (y0 + y1) / 2);
    /* a wrapped line's end is the next line's start: the same place */
    if (h != i) {
      double a0, b0, a1, b1;
      text_caret_pos(tl, h, &a0, &b0, &a1, &b1);
      if (fabs(a0 - x0) > 0.01 || fabs(b0 - y0) > 0.01) ok = 0;
    }
  }
  CHECK(ok && tl->nl >= 3, "round trip through wrapping and a rotation");
  text_layout_free(tl);
  /* vertical */
  text_params_init(tp, FAM, 20, 0, 100, 10);
  text_set_string(tp, "ABC\nDE");
  tp->vertical = 1;
  tl = text_layout(tp);
  CHECK(tl->nl == 2 && tl->ln[1].base < tl->ln[0].base && tl->g[1].y > tl->g[0].y && tl->g[1].x == tl->g[0].x, "vertical: down the columns, right to left");
  text_caret_pos(tl, 1, &x0, &y0, &x1, &y1);
  CHECK(fabs(y0 - y1) < 1e-9 && x1 > x0 && text_hit(tl, (x0 + x1) / 2, y0) == 1, "vertical caret across the column");
  text_layout_free(tl);
  {
    u8 *px;
    Box b, in;
    text_render(tp, 300, 300, &px, &b);
    in = ink(px, b);
    CHECK(px && in.y1 - in.y0 > 2 * (in.x1 - in.x0) / 3 && in.x1 <= 115, "vertical type drawn tall and narrow");
    free(px);
  }
  free(tp);
}

static void test_warp(void)
{
  TextParams *tp = newtp("Warped text here", 30, 0x000000, 20, 100);
  u8 *px, *p0;
  Box b, b0, in, in0, tb;
  int w, ok = 1;
  text_render(tp, 500, 300, &p0, &b0);
  in0 = ink(p0, b0);
  for (w = TW_ARC; w < TW_N; w++) {
    tp->warp = (u8)w; tp->warp_bend = 50; tp->warp_h = 10; tp->warp_v = -10;
    if (!text_render(tp, 500, 300, &px, &b) || !px) { ok = 0; continue; }
    in = ink(px, b);
    tb = text_bounds(tp);
    if (in.x0 < tb.x0 - 1 || in.y0 < tb.y0 - 1 || in.x1 > tb.x1 + 1 || in.y1 > tb.y1 + 1) { ok = 0; printf("  warp %s out of its bounds\n", text_warp_names[w]); }
    if (w == TW_ARC) CHECK(in.y1 - in.y0 > in0.y1 - in0.y0 + 10 && in.x1 - in.x0 < in0.x1 - in0.x0, "arc: taller and narrower than flat");
    if (w == TW_ARC) {
      /* the middle higher than the ends (bend up) */
      i32 x, y, top_mid = 9999, top_end = 9999, mid = (in.x0 + in.x1) / 2;
      for (y = in.y0; y < in.y1; y++) for (x = in.x0; x < in.x1; x++) {
        u8 a = px[((size_t)(y - b.y0) * (b.x1 - b.x0) + (x - b.x0)) * 4 + 3];
        if (a < 128) continue;
        if (abs(x - mid) < 10 && y < top_mid) top_mid = y;
        if (x < in.x0 + 15 && y < top_end) top_end = y;
      }
      CHECK(top_mid < top_end - 10, "arc bends up");
    }
    if (labs(ink_count(px, b) - ink_count(p0, b0)) > ink_count(p0, b0) / 2) { ok = 0; printf("  warp %s lost its ink\n", text_warp_names[w]); }
    /* 1.1's: each changes the shape (its ink's box is not the flat text's) */
    if (w >= TW_ARCLOWER && in.y1 - in.y0 == in0.y1 - in0.y0 && in.x1 - in.x0 == in0.x1 - in0.x0) { ok = 0; printf("  warp %s left the text flat\n", text_warp_names[w]); }
    free(px);
  }
  CHECK(ok, "every warp draws inside its bounds");
  tp->warp = TW_FLAG; tp->warp_bend = -100;
  CHECK(text_render(tp, 500, 300, &px, &b) && px, "full bend down");
  free(px);
  free(p0);
  free(tp);
}

static void test_layer_ops(void)
{
  Doc *d = doc_new(300, 160, 0xFFFFFF, 0);
  TextParams *tp = newtp("Layer text", 36, 0x993300, 20, 70), *t2;
  u8 *c0, *c1, *c2, *ora;
  size_t len;
  char err[128];
  int i;
  i = doc_new_text(d, tp, NULL);
  CHECK(i == 1 && !strcmp(doc_undo_name(d), "New Type Layer"), "new type layer, one step");
  c0 = composite(d);
  /* edit */
  t2 = (TextParams *)malloc(sizeof *t2);
  *t2 = *layer_text(d->lay[1]);
  text_insert(t2, 10, " edited", -1);
  t2->run[0].color = 0x003399;
  CHECK(doc_set_text(d, 1, t2) && !strcmp(doc_undo_name(d), "Edit Type"), "edited");
  c1 = composite(d);
  CHECK(maxdiff(c0, c1, 300 * 160 * 4) > 100, "the picture changed");
  doc_undo(d);
  c2 = composite(d);
  CHECK(!memcmp(c0, c2, 300 * 160 * 4) && !strcmp(layer_text(d->lay[1])->text, "Layer text"), "undone");
  free(c2);
  doc_redo(d);
  c2 = composite(d);
  CHECK(!memcmp(c1, c2, 300 * 160 * 4), "redone");
  free(c2);
  /* an editing session as one step */
  doc_begin(d, "Type");
  text_insert(t2, 0, "A", -1); doc_set_text(d, 1, t2);
  text_insert(t2, 0, "B", -1); doc_set_text(d, 1, t2);
  doc_end(d);
  CHECK(!strcmp(doc_undo_name(d), "Type") && !strncmp(layer_text(d->lay[1])->text, "BA", 2), "a session");
  doc_undo(d);
  CHECK(!strcmp(layer_text(d->lay[1])->text, "Layer text edited"), "undone in one step");
  /* OpenRaster */
  t2->run[0].color = 0x993300;
  strcpy(t2->text, "Two & \"runs\"\nline <2> %;");
  t2->nruns = 2; t2->run[1] = t2->run[0]; t2->run[1].start = 4; t2->run[1].size = 20; t2->run[1].underline = 1;
  strcpy(t2->run[1].family, "Sans; odd=name&");
  t2->box = 1; t2->bw = 200; t2->bh = 120; t2->x = 10; t2->y = 10; t2->align = TA_CENTER; t2->warp = TW_ARC; t2->warp_bend = 20;
  doc_set_text(d, 1, t2);
  free(c1); c1 = composite(d);
  ora = ora_write(d, &len);
  {
    Doc *e = ora ? ora_read(ora, len, err, sizeof err) : NULL;
    CHECK(e && e->nlay == 2 && e->lay[1]->kind == LK_TEXT, "ORA keeps the type layer");
    if (e && layer_text(e->lay[1])) {
      const TextParams *r = layer_text(e->lay[1]);
      CHECK(!strcmp(r->text, t2->text) && r->nruns == 2 && r->run[1].start == 4 && r->run[1].underline && !strcmp(r->run[1].family, "Sans; odd=name&"), "text and runs back");
      CHECK(r->box && r->bw == 200 && r->align == TA_CENTER && r->warp == TW_ARC && r->warp_bend == 20, "settings back");
      c2 = composite(e);
      CHECK(maxdiff(c1, c2, 300 * 160 * 4) <= 1, "the same picture from the stored rendering");
      free(c2);
      e->lay[1]->live_ok = 0;
      c2 = composite(e);
      CHECK(maxdiff(c1, c2, 300 * 160 * 4) == 0, "and rendered again");
      free(c2);
      {
        TextParams *t3 = (TextParams *)malloc(sizeof *t3);
        *t3 = *r; t3->warp = TW_NONE;
        CHECK(doc_set_text(e, 1, t3), "editable after loading");
        free(t3);
      }
    }
    doc_free(e);
  }
  free(ora);
  free(c1);
  /* Rasterize Type */
  free(c0); c0 = composite(d);
  CHECK(doc_rasterize(d, 1) && d->lay[1]->kind == LK_PIXEL && !d->lay[1]->live && !strcmp(doc_undo_name(d), "Rasterize Type"), "rasterized");
  c2 = composite(d);
  CHECK(!memcmp(c0, c2, 300 * 160 * 4), "rasterized equals the live rendering");
  free(c2);
  doc_undo(d);
  CHECK(d->lay[1]->kind == LK_TEXT, "rasterize undone");
  /* duplicate, merge down */
  CHECK(doc_duplicate_layer(d, 1) && d->nlay == 3 && d->lay[2]->kind == LK_TEXT && d->lay[2]->live != d->lay[1]->live, "duplicated with its own settings");
  doc_delete_layer(d, 2);
  CHECK(doc_merge_down(d, 1) && d->nlay == 1, "merged down");
  c2 = composite(d);
  CHECK(maxdiff(c0, c2, 300 * 160 * 4) <= 1, "merged picture the same");
  free(c2);
  doc_undo(d);
  /* Type Mask */
  {
    u8 *m = (u8 *)malloc(300 * 160);
    long k, diff = 0, sum = 0;
    text_coverage(layer_text(d->lay[1]), 300, 160, m);
    sel_from_text(d, layer_text(d->lay[1]), SEL_REPLACE);
    CHECK(d->sel != NULL && !strcmp(doc_undo_name(d), "Type Mask"), "Type Mask selection");
    for (k = 0; d->sel && k < 300 * 160; k++) { diff += abs(d->sel[k] - m[k]); sum += m[k]; }
    CHECK(d->sel && diff == 0 && sum > 255 * 200, "the selection is the text's coverage");
    free(m);
    sel_none(d);
  }
  /* Create Work Path */
  {
    Path *p = text_to_path(layer_text(d->lay[1]));
    u8 *m = (u8 *)malloc(300 * 160);
    double a = 0, b = 0;
    long k;
    CHECK(p && p->ns > 5, "text outlines as a path");
    if (p) path_to_selection(d, p, SEL_REPLACE, 0, 1);
    text_coverage(layer_text(d->lay[1]), 300, 160, m);
    for (k = 0; d->sel && k < 300 * 160; k++) { a += d->sel[k]; b += m[k]; }
    CHECK(b > 0 && fabs(a - b) / b < 0.05, "the path covers what the text does");
    path_free(p);
    free(m);
    sel_none(d);
  }
  /* crop, resize and rotate */
  free(c0);
  {
    TextParams *t4 = newtp("Resize me", 30, 0, 40, 100);
    Box b0, b1;
    doc_set_text(d, 1, t4);
    b0 = text_bounds(layer_text(d->lay[1]));
    CHECK(image_crop(d, box_make(10, 20, 290, 150)), "crop");
    CHECK(layer_text(d->lay[1])->x == 30 && layer_text(d->lay[1])->y == 80 && !layer_text(d->lay[1])->xf_on, "type moved with the crop");
    CHECK(image_resize(d, 560, 260, RS_BILINEAR), "resize x2");
    CHECK(layer_text(d->lay[1])->run[0].size == 60 && layer_text(d->lay[1])->x == 60 && !layer_text(d->lay[1])->xf_on, "type size doubled");
    b1 = text_bounds(layer_text(d->lay[1]));
    CHECK(abs((b1.x1 - b1.x0) - 2 * (b0.x1 - b0.x0)) <= 3, "twice as wide");
    c0 = composite(d);
    CHECK(maxdiff(c0, c0, 1) == 0, "composites");
    free(c0);
    CHECK(image_resize(d, 560, 130, RS_BILINEAR) && layer_text(d->lay[1])->xf_on, "squashed: kept as a transform");
    image_rotate(d, 1);
    c0 = composite(d);
    {
      long k, dark = 0;
      for (k = 0; k < (long)d->w * d->h; k++) if (c0[k * 4] < 100) dark++;
      CHECK(dark > 200, "rotated type still drawn");
    }
    free(c0);
    CHECK(canvas_resize(d, d->w + 20, d->h + 20, 0, 0xFFFFFF), "canvas size");
    for (i = 0; i < 4; i++) doc_undo(d);
    CHECK(d->w == 280 && layer_text(d->lay[1])->run[0].size == 30, "undone back to the crop");
    doc_undo(d);
    CHECK(d->w == 300 && layer_text(d->lay[1])->x == 40, "and the crop");
    /* the Move tool */
    {
      double m[6] = { 1, 0, 5, 0, 1, -3 };
      doc_live_transform(d, 1, m);
      CHECK(layer_text(d->lay[1])->x == 45 && layer_text(d->lay[1])->y == 97 && !strcmp(doc_undo_name(d), "Move"), "moved");
    }
    free(t4);
  }
  free(t2);
  free(tp);
  doc_free(d);
}

/* a tiny Amiga font: 'A' (a box, rows 1-6) and 'B' (all rows), 8 pixels high, as a hunk file */
static size_t make_font(u8 *o)
{
  u8 h[152];
  size_t n = 0;
  int i;
  memset(h, 0, sizeof h);
#define W16(at, v) (h[at] = (u8)((v) >> 8), h[(at) + 1] = (u8)(v))
#define W32(at, v) (W16(at, (v) >> 16), W16((at) + 2, (v) & 0xFFFF))
  W32(0, 0x70FF4E75UL);
  W16(18, 0x0F80);
  strcpy((char *)h + 26, "test.font");
  W16(78, 8); h[81] = 0x60; W16(82, 8); W16(84, 6); W16(86, 1);
  h[90] = 'A'; h[91] = 'B';
  W32(92, 110); W16(96, 2); W32(98, 126); W32(102, 138); W32(106, 144);
  for (i = 0; i < 8; i++) {
    u8 a = (i >= 1 && i <= 6) ? 0xFC : 0;
    h[110 + 2 * i] = (u8)(a | 0x03); h[111 + 2 * i] = 0xF0;
  }
  W16(126, 0); W16(128, 6); W16(130, 6); W16(132, 6); W16(134, 12); W16(136, 4);
  W16(138, 7); W16(140, 7); W16(142, 5);
  W16(144, 0); W16(146, 1); W16(148, 0);
#define P32(v) (o[n] = (u8)((v) >> 24), o[n + 1] = (u8)((v) >> 16), o[n + 2] = (u8)((v) >> 8), o[n + 3] = (u8)(v), n += 4)
  P32(0x3F3UL); P32(0); P32(1); P32(0); P32(0); P32(38);
  P32(0x3E9UL); P32(38);
  memcpy(o + n, h, sizeof h); n += sizeof h;
  P32(0x3F2UL);
  return n;
}
static AmiFont *test_font;
static AmiFont *ami_lookup(const char *family, int size) { (void)size; return !strcmp(family, "TestAmi") ? test_font : NULL; }
static void test_amifont(void)
{
  u8 buf[256], cov[64];
  size_t n = make_font(buf);
  int adv, kern, w;
  AmiFont *f = amifont_read(buf, n);
  CHECK(f && f->ysize == 8 && f->baseline == 6 && f->lo == 'A' && f->hi == 'B' && !strcmp(f->name, "test.font"), "Amiga font read");
  if (!f) return;
  amifont_char(f, 'A', &adv, &kern, &w, cov);
  CHECK(adv == 7 && kern == 0 && w == 6 && cov[0] == 0 && cov[6] == 255 && cov[6 + 5] == 255 && cov[7 * 6] == 0, "glyph A");
  amifont_char(f, 'B', &adv, &kern, &w, cov);
  CHECK(adv == 8 && kern == 1 && w == 6 && cov[0] == 255 && cov[47] == 255, "glyph B (kerned)");
  amifont_char(f, 'z', &adv, &kern, &w, NULL);
  CHECK(adv == 5 && w == 4, "a missing character: the spare glyph");
  CHECK(amifont_read(buf, 40) == NULL && amifont_read((const u8 *)"nonsense, not a hunk file at all......", 38) == NULL, "junk refused");
  /* a .font file naming it */
  {
    u8 fc[4 + 260];
    FILE *fp;
    AmiFont *g;
    memset(fc, 0, sizeof fc);
    fc[0] = 0x0F; fc[1] = 0x00; fc[3] = 1;
    strcpy((char *)fc + 4, "easel_typetest_8");
    fc[4 + 256 + 1] = 8;
    if ((fp = fopen("/tmp/easel_typetest_8", "wb"))) { fwrite(buf, 1, n, fp); fclose(fp); }
    if ((fp = fopen("/tmp/easel_typetest.font", "wb"))) { fwrite(fc, 1, sizeof fc, fp); fclose(fp); }
    g = amifont_load("/tmp/easel_typetest.font", 11);
    CHECK(g && g->ysize == 8, "loaded through its .font file");
    amifont_free(g);
  }
  /* type in it, at twice its size */
  {
    TextParams *tp = newtp("AB", 16, 0x00FF00, 10, 20);
    u8 *px;
    Box b;
    strcpy(tp->run[0].family, "TestAmi");
    test_font = f;
    text_amifont_lookup = ami_lookup;
    CHECK(text_render(tp, 100, 50, &px, &b) && px, "type in a bitmap font");
#define A(x, y) (px && (x) >= b.x0 && (x) < b.x1 && (y) >= b.y0 && (y) < b.y1 ? px[((size_t)((y) - b.y0) * (b.x1 - b.x0) + ((x) - b.x0)) * 4 + 3] : 0)
    CHECK(A(12, 10) == 255 && A(12, 7) == 0 && A(21, 19) == 255 && A(22, 10) == 0, "A scaled 2x on the baseline");
    CHECK(A(26, 6) == 255 && A(37, 21) == 255 && A(25, 10) == 0, "B after A's advance and its kern");
#undef A
    {
      TextLayout *tl = text_layout(tp);
      CHECK(tl && fabs(tl->g[1].x - 24) < 1e-9, "advance from the font's spacing");
      text_layout_free(tl);
    }
    free(px);
    text_amifont_lookup = NULL;
    free(tp);
  }
  amifont_free(f);
}

int main(void)
{
  int i;
  test_editing();
  test_amifont();
  for (i = 0; font_paths[i]; i++) { FILE *fp = fopen(font_paths[i], "rb"); if (fp) { fclose(fp); break; } }
  if (!font_paths[i]) printf("no TrueType font found: the type layout tests are skipped\n");
  else {
    fs = (FontSet *)calloc(1, sizeof *fs);
    fonts_add(fs, font_paths[i], FAM, 0, 0);
    for (i = 0; bold_paths[i]; i++) { FILE *fp = fopen(bold_paths[i], "rb"); if (fp) { fclose(fp); fonts_add(fs, bold_paths[i], FAM, 1, 0); break; } }
    text_fonts = fs;
    face = fonts_face(fs, FAM, 0, 0);
    CHECK(face && ttf_load(face), "the test font loads");
    if (face) {
      test_old_tool();
      test_layout();
      test_runs();
      test_caret();
      test_warp();
      test_layer_ops();
    }
    text_cache_flush();
    fonts_free(fs);
    free(fs);
  }
  printf("%d tests, %d failed\n", tests, fails);
  return fails != 0;
}
