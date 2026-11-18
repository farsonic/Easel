/*
 * r4test.c - Easel round 4: Find and Isolate Layers, Float and Drop, Levels' Show Clipping, the
 * channel keys, Color Range's localized clusters and preview modes, smart quotes and Check Spelling.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/doc.h"
#include "core/paint.h"
#include "core/layers.h"
#include "core/selx.h"
#include "core/type.h"
#include "core/r4.h"
#include "core/spell.h"
#include "core/cms.h"

static int tests, fails;
#define CHECK(c, msg) do { tests++; if (!(c)) { fails++; printf("FAIL %s (line %d)\n", msg, __LINE__); } } while (0)

static Layer *add_pixel(Doc *d, const char *name, u32 rgb)
{
  Layer *l = layer_new(d, name);
  long k;
  for (k = 0; k < (long)d->w * d->h; k++) { l->px[k * 4] = (u8)(rgb >> 16); l->px[k * 4 + 1] = (u8)(rgb >> 8); l->px[k * 4 + 2] = (u8)rgb; l->px[k * 4 + 3] = 255; }
  doc_add_layer(d, l, d->nlay);
  return l;
}

static void test_filter(void)
{
  Doc *d = doc_new(8, 8, 0xFFFFFF, 0);
  TextParams *tp = (TextParams *)calloc(1, sizeof *tp);
  add_pixel(d, "Sky", 0x0000FF);
  add_pixel(d, "Sea", 0x0000AA);
  text_params_init(tp, "Sans", 12, 0, 1, 6);
  text_set_string(tp, "Title");
  doc_new_text(d, tp, "Title text");
  add_pixel(d, "Sky glow", 0xFFAA00);
  CHECK(d->nlay == 5, "five layers");
  CHECK(!layer_filter_on(d) && layer_filter_pass(d, 1), "no filter: every layer passes");
  layer_filter_find(d, "SKY", LFK_ALL);
  CHECK(layer_filter_on(d) && layer_filter_count(d) == 2 && layer_filter_pass(d, 1) && layer_filter_pass(d, 4) && !layer_filter_pass(d, 2), "by name, any case");
  layer_filter_find(d, "", LFK_TYPE);
  CHECK(layer_filter_count(d) == 1 && layer_filter_pass(d, 3), "by kind: the type layer");
  layer_filter_find(d, "", LFK_ALL);
  CHECK(!layer_filter_on(d), "nothing set: off");
  {
    Doc *e = doc_new(4, 4, 0, 0);
    layer_filter_find(d, "Sea", LFK_ALL);
    CHECK(!layer_filter_on(e) && layer_filter_pass(e, 0), "another picture isn't filtered");
    doc_free(e);
  }
  layer_filter_find(d, "", LFK_ALL);
  d->active = 2; d->lay[4]->picked = 1;
  CHECK(layer_filter_isolate(d) == 1 && layer_filter_count(d) == 2 && layer_filter_pass(d, 2) && layer_filter_pass(d, 4), "Isolate: the active and picked layers");
  CHECK(layer_filter_isolate(d) == 0 && !layer_filter_on(d), "Isolate again: everything");
  d->lay[4]->picked = 0;
  /* a group isolated brings its members */
  d->active = 2;
  doc_group_layers(d);
  {
    int g = -1, i;
    for (i = 0; i < d->nlay; i++) if (d->lay[i]->kind == LK_GROUP) g = i;
    CHECK(g >= 0, "a group made");
    if (g >= 0) {
      d->active = g;
      layer_filter_isolate(d);
      CHECK(layer_filter_count(d) == 2 && layer_filter_pass(d, g) && layer_filter_pass(d, g - 1), "the group and its member");
      layer_filter_clear();
    }
  }
  free(tp);
  doc_free(d);
}

static void test_float(void)
{
  Doc *d = doc_new(10, 10, 0xFFFFFF, 0);
  Layer *l = add_pixel(d, "Red", 0xFF0000);
  int f, n;
  d->active = 1;
  CHECK(sel_float(d) == -1, "nothing selected: no float");
  sel_rect(d, 2, 2, 5, 6, SEL_REPLACE, 0);
  n = d->nlay;
  f = sel_float(d);
  CHECK(f == 2 && d->nlay == n + 1 && d->active == 2 && layer_is_floating(d->lay[2]), "floated onto a layer above");
  CHECK(d->lay[2]->px[(3 * 10 + 3) * 4 + 3] == 255 && d->lay[2]->px[(3 * 10 + 3) * 4] == 255 && d->lay[2]->px[(0 * 10 + 0) * 4 + 3] == 0, "the selected pixels, nothing else");
  CHECK(d->lay[1]->px[(3 * 10 + 3) * 4 + 3] == 0 && d->lay[1]->px[(8 * 10 + 8) * 4 + 3] == 255, "cut from the layer below");
  CHECK(d->sel != NULL, "the selection kept");
  CHECK(!strcmp(d->last_action, "Float") || doc_can_undo(d), "one step");
  /* moved by hand (as the Move tool would), then dropped */
  {
    Layer *fl = d->lay[2];
    i32 x, y;
    u8 tmp[10 * 10 * 4];
    memset(tmp, 0, sizeof tmp);
    for (y = 0; y < 10; y++) for (x = 0; x < 9; x++) memcpy(tmp + (y * 10 + x + 1) * 4, fl->px + (y * 10 + x) * 4, 4);
    memcpy(fl->px, tmp, sizeof tmp);
  }
  d->active = 1;
  CHECK(!sel_drop(d), "Drop needs the floating layer active");
  d->active = 2;
  CHECK(sel_drop(d) && d->nlay == n && d->lay[1]->px[(3 * 10 + 5) * 4 + 3] == 255 && d->lay[1]->px[(3 * 10 + 2) * 4 + 3] == 0, "dropped where it was moved to");
  doc_undo(d);
  CHECK(d->nlay == n + 1 && layer_is_floating(d->lay[2]), "Drop undone");
  doc_undo(d);
  CHECK(d->nlay == n && d->lay[1]->px[(3 * 10 + 3) * 4 + 3] == 255, "Float undone in one step");
  (void)l;
  doc_free(d);
}

static void test_clipping(void)
{
  u8 px[3 * 4] = { 0, 0, 0, 255,   10, 128, 255, 255,   200, 250, 30, 255 };
  int lo[3] = { 10, 10, 10 }, hi[3] = { 250, 250, 250 };
  u8 a[12];
  long before[5] = { 0, 100, 255, 0, 255 }, after[5] = { 0, 100, 255, 0, 255 };
  memcpy(a, px, 12);
  clip_view_rgba(a, 3, 0, lo, hi);
  CHECK(a[0] == 0 && a[1] == 0 && a[2] == 0, "black in every channel: black");
  CHECK(a[4] == 0 && a[5] == 255 && a[6] == 255, "clipped in red only: cyan");
  CHECK(a[8] == 255 && a[9] == 255 && a[10] == 255, "nothing clipped: white");
  memcpy(a, px, 12);
  clip_view_rgba(a, 3, 1, lo, hi);
  CHECK(a[0] == 0 && a[1] == 0 && a[2] == 0, "white view: black where nothing clips");
  CHECK(a[4] == 0 && a[5] == 0 && a[6] == 255, "blue at 255: blue");
  CHECK(a[8] == 0 && a[9] == 255 && a[10] == 0, "green at 250: green");
  CHECK(levels_clip_end(before, after) == -1, "nothing moved");
  after[2] = 240; CHECK(levels_clip_end(before, after) == 1, "input white moved: the white end");
  after[2] = 255; after[0] = 12; CHECK(levels_clip_end(before, after) == 0, "input black moved: the black end");
  {
    Doc *d = doc_new(4, 4, 0x808080, 0);
    int k = doc_clip_view_layer(d, 1, lo, hi), n = d->nlay;
    CHECK(k == 1 && !strcmp(d->lay[1]->name, "Clipping") && d->lay[1]->px[0] == 0 && d->lay[1]->px[3] == 255, "the clipping layer on top");
    doc_undo(d);
    CHECK(d->nlay == n - 1, "and taken back by undo (the preview)");
    doc_free(d);
  }
}

static void test_keys(void)
{
  CHECK(channel_for_key(2, 4) == 0 && channel_for_key(3, 4) == 1 && channel_for_key(5, 4) == 3, "Alt+2 composite, Alt+3 red, Alt+5 blue");
  CHECK(channel_for_key(6, 4) == -1 && channel_for_key(1, 4) == -1, "Alt+6 and Alt+1 pick nothing with four channels");
  CHECK(rawkey_digit(0x02) == 2 && rawkey_digit(0x0A) == 0 && rawkey_digit(0x10) == -1, "the top row's raw keys");
}

static void test_color_range(void)
{
  Doc *d = doc_new(100, 20, 0xFFFFFF, 0);
  Layer *l = d->lay[0];
  ColorRange cr;
  u8 *m;
  i32 x, y;
  u8 out[3], p[4] = { 200, 100, 50, 255 };
  /* two red patches, one at each end */
  for (y = 5; y < 15; y++) for (x = 0; x < 100; x++) if (x < 10 || x >= 90) { u8 *q = l->px + (y * 100 + x) * 4; q[0] = 255; q[1] = q[2] = 0; }
  memset(&cr, 0, sizeof cr);
  cr.preset = CR_SAMPLED; cr.colors[0] = 0xFF0000; cr.ncolors = 1; cr.fuzziness = 40; cr.all_layers = 1;
  m = color_range_mask(d, &cr);
  CHECK(m && m[10 * 100 + 5] == 255 && m[10 * 100 + 95] == 255 && m[0] == 0, "both patches without localizing");
  free(m);
  cr.localized = 1; cr.lrange = 30; cr.cx[0] = 5; cr.cy[0] = 10; cr.has_xy[0] = 1;
  m = color_range_mask(d, &cr);
  CHECK(m && m[10 * 100 + 5] == 255 && m[10 * 100 + 95] == 0, "localized: only the patch it was picked in");
  CHECK(m && m[10 * 100 + 9] > 200, "near the sample, almost fully");
  free(m);
  cr.has_xy[0] = 0;
  m = color_range_mask(d, &cr);
  CHECK(m && m[10 * 100 + 95] == 255, "a colour without a place counts everywhere");
  free(m);
  doc_free(d);
  cr_view_pixel(CRV_SELECTION, 128, p, out); CHECK(out[0] == 128 && out[2] == 128, "Selection: the mask as grey");
  cr_view_pixel(CRV_IMAGE, 0, p, out); CHECK(out[0] == 200 && out[1] == 100, "Image: the picture");
  cr_view_pixel(CRV_BLACKMATTE, 0, p, out); CHECK(out[0] == 0 && out[1] == 0, "Black Matte: unselected black");
  cr_view_pixel(CRV_WHITEMATTE, 0, p, out); CHECK(out[0] == 255 && out[2] == 255, "White Matte: unselected white");
  cr_view_pixel(CRV_WHITEMATTE, 255, p, out); CHECK(out[0] == 200 && out[2] == 50, "selected: the picture");
  cr_view_pixel(CRV_QUICKMASK, 0, p, out); CHECK(out[0] > 200 && out[1] < 100, "Quick Mask: red over the unselected");
  cr_view_pixel(CRV_GRAY, 255, p, out); CHECK(out[0] == out[1] && out[0] > 100 && out[0] < 140, "Grayscale: the picture's grey");
}

static void test_quotes(void)
{
  char o[200];
  smart_quotes("He said \"it's 'fine'\" (\"yes\")", o, sizeof o);
  CHECK(!strcmp(o, "He said \xE2\x80\x9Cit\xE2\x80\x99s \xE2\x80\x98" "fine\xE2\x80\x99\xE2\x80\x9D (\xE2\x80\x9Cyes\xE2\x80\x9D)"), "curly quotes, apostrophe as a closing single quote");
  smart_quotes("\"'nested'\"", o, sizeof o);
  CHECK(!strcmp(o, "\xE2\x80\x9C\xE2\x80\x98nested\xE2\x80\x99\xE2\x80\x9D"), "an opening quote after an opening quote");
  CHECK(smart_quotes("\"\"\"", o, 7) == 6 && strlen(o) == 6, "cut short at the buffer's end");
  CHECK(smart_quote_for('"', 0, o) == 3 && !strcmp(o, "\xE2\x80\x9C"), "typed at the start: opening");
  CHECK(smart_quote_for('\'', 'n', o) == 3 && !strcmp(o, "\xE2\x80\x99"), "after a letter: an apostrophe");
}

static void test_spelling(void)
{
  static const char list[] = "cat\nmat\nsat\nthe\non\nspelling\nAmiga\nhello\nworld\n";
  Spell s;
  char *sug[8];
  int n, k, li, ws, we;
  Doc *d = doc_new(60, 20, 0xFFFFFF, 0);
  TextParams *tp = (TextParams *)calloc(1, sizeof *tp);
  CHECK(spell_load_mem(&s, list, sizeof list - 1) && s.n == 9, "the list read");
  CHECK(spell_ok(&s, "Cat", 3) && spell_ok(&s, "AMIGA", 5) && spell_ok(&s, "cat's", 5) && spell_ok(&s, "MP3", 3), "case, possessives and digits");
  CHECK(!spell_ok(&s, "speling", 7), "a misspelling");
  n = spell_suggest(&s, "speling", 7, sug, 8);
  CHECK(n >= 1 && !strcmp(sug[0], "spelling"), "spelling offered first");
  for (k = 0; k < n; k++) free(sug[k]);
  n = spell_suggest(&s, "Wrold", 5, sug, 8);
  CHECK(n >= 1 && !strcmp(sug[0], "World"), "a capital kept");
  for (k = 0; k < n; k++) free(sug[k]);
  text_params_init(tp, "Sans", 12, 0, 1, 10);
  text_set_string(tp, "the cat sat");
  doc_new_text(d, tp, "A");
  text_set_string(tp, "helo wrold on the mat");
  doc_new_text(d, tp, "B");
  CHECK(spell_next_type(&s, d, -1, 0, &li, &ws, &we) && li == 2 && ws == 0 && we == 4, "the top layer first: helo");
  CHECK(spell_next_type(&s, d, li, we, &li, &ws, &we) && li == 2 && ws == 5 && we == 10, "then wrold");
  CHECK(spell_change(d, li, ws, we, "world") && !strcmp(layer_text(d->lay[2])->text, "helo world on the mat"), "changed");
  spell_add(&s, "helo", 4);
  CHECK(!spell_next_type(&s, d, 2, 0, &li, &ws, &we), "helo ignored: nothing left");
  text_set_string(tp, "wrold and wrold");
  doc_new_text(d, tp, "C");
  CHECK(spell_change_all(d, "wrold", "world") == 2 && !strcmp(layer_text(d->lay[3])->text, "world and world"), "Change All");
  doc_undo(d);
  CHECK(!strcmp(layer_text(d->lay[3])->text, "wrold and wrold"), "one undo step");
  spell_free(&s);
  CHECK(spell_ok(&s, "zzxq", 4), "no list: everything passes");
  free(tp);
  doc_free(d);
}

/* Preferences > Transparency & Gamut: the gamut warning's colour and opacity */
static void test_gamut(void)
{
  u8 p[3];
  int o[3] = { 200, 100, 0 };
  gamut_warn_mix(p, o);
  CHECK(p[0] == 128 && p[1] == 128 && p[2] == 128, "the default: grey, fully");
  gamut_warn_color = 0xFF00FF; gamut_warn_opacity = 50;
  gamut_warn_mix(p, o);
  CHECK(p[0] == 228 && p[1] == 50 && p[2] == 128, "magenta at half over the colour");
  gamut_warn_color = GAMUT_GREY; gamut_warn_opacity = 100;
}

int main(void)
{
  test_filter();
  test_float();
  test_clipping();
  test_keys();
  test_color_range();
  test_quotes();
  test_spelling();
  test_gamut();
  printf("%d tests, %d failed\n", tests, fails);
  return fails != 0;
}
