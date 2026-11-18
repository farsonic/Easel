/*
 * brushxtest.c - Easel 1.2's brush extras (core/brushx.c and their use in core/stroke.c): Texture,
 * Dual Brush, Noise, Wet Edges, Brush Pose and Pen Tilt, the Mixer Brush, the Art History Brush,
 * healing from a pattern with Diffusion and a mode, Create Texture, tablet events made up as Intuition
 * would send them (no tablet here), and Photoshop .abr writing and .grd and .pat reading.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "core/paint.h"
#include "core/brushx.h"
#include "core/preset.h"
#include "core/gradient.h"
#include "core/pattern.h"
#include "core/retouch.h"

static int fails, tests;
#define CHECK(c, msg) do { tests++; if (!(c)) { fails++; printf("FAIL %s (line %d)\n", msg, __LINE__); } } while (0)

static Brush brush(int tool, int size, int hard)
{
  Brush b;
  memset(&b, 0, sizeof b);
  b.tool = tool; b.size = size; b.hardness = hard; b.opacity = 100; b.flow = 100; b.spacing = 10; b.strength = 50; b.bg = 0xFFFFFF;
  return b;
}
static Doc *clear_doc(i32 w, i32 h) { Doc *d = doc_new(w, h, 0, 1); return d; }
static int alpha(Doc *d, i32 x, i32 y) { return d->lay[d->active]->px[((long)y * d->w + x) * 4 + 3]; }
static const u8 *pxl(Doc *d, i32 x, i32 y) { return d->lay[d->active]->px + ((long)y * d->w + x) * 4; }
static void line(Doc *d, const Brush *b, u32 col, double x0, double y0, double x1, double y1)
{
  Stroke *s = stroke_begin(d, b, col, x0, y0, NULL);
  if (s) { stroke_to(s, x1, y1, NULL); stroke_end(s); }
}
static long sum_alpha(Doc *d)
{
  long k, t = 0;
  for (k = 0; k < (long)d->w * d->h; k++) t += d->lay[d->active]->px[k * 4 + 3];
  return t;
}

static void test_texture_dual_noise_wet(void)
{
  Doc *d = clear_doc(80, 40), *e;
  Pattern *pt = pattern_new("Stripes", 8, 8);
  Brush b = brush(PT_BRUSH, 20, 100);
  long plain, k;
  int x, y;
  for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) { u8 *q = pt->px + (y * 8 + x) * 4; q[0] = q[1] = q[2] = (u8)(x < 4 ? 255 : 0); q[3] = 255; }
  line(d, &b, 0xFF0000, 10, 20, 70, 20);
  plain = sum_alpha(d);
  CHECK(alpha(d, 34, 20) == 255 && alpha(d, 38, 20) == 255, "plain stroke solid");
  /* Texture, multiply at full depth: the black stripes take the paint away */
  e = clear_doc(80, 40);
  b.texture = pt; b.tex_depth = 100; b.tex_mode = TXM_MULTIPLY;
  line(e, &b, 0xFF0000, 10, 20, 70, 20);
  CHECK(alpha(e, 33, 20) == 255 && alpha(e, 37, 20) == 0, "textured: paint in the white stripes only");
  CHECK(brushx_texture(&b, 33, 0) == 255 && brushx_texture(&b, 37, 0) == 0, "texture height");
  b.tex_invert = 1;
  CHECK(brushx_texture(&b, 33, 0) == 0, "inverted");
  b.tex_invert = 0; b.tex_scale = 200;
  CHECK(brushx_texture(&b, 37, 0) == 255 && brushx_texture(&b, 41, 0) == 0, "scaled");
  b.tex_scale = 0; b.tex_mode = TXM_SUBTRACT; b.tex_depth = 50;
  CHECK(brushx_cov(&b, 255, 37, 20, 37, 20, 20, 0) == 127 || brushx_cov(&b, 255, 37, 20, 37, 20, 20, 0) == 128, "subtract half depth");
  b.tex_mode = TXM_HEIGHT; b.tex_depth = 40;
  CHECK(brushx_cov(&b, 200, 37, 20, 37, 20, 20, 0) == 0 && brushx_cov(&b, 200, 33, 20, 33, 20, 20, 0) == 200, "height");
  doc_free(e);
  /* Dual Brush: the second tip's scattered dabs leave gaps, the same every time */
  b = brush(PT_BRUSH, 20, 100);
  b.dual_on = 1; b.dual_size = 4; b.dual_count = 2; b.dual_scatter = 0; b.spacing = 60;
  e = clear_doc(80, 40);
  line(e, &b, 0xFF0000, 10, 20, 70, 20);
  {
    long a1 = sum_alpha(e);
    Doc *f = clear_doc(80, 40);
    line(f, &b, 0xFF0000, 10, 20, 70, 20);
    CHECK(a1 > 0 && a1 < plain, "dual brush: less paint");
    CHECK(!memcmp(e->lay[0]->px, f->lay[0]->px, (size_t)80 * 40 * 4), "dual brush replays exactly");
    doc_free(f);
  }
  doc_free(e);
  /* Noise in a soft brush's edge */
  b = brush(PT_BRUSH, 20, 0);
  e = clear_doc(80, 40);
  line(e, &b, 0xFF0000, 10, 20, 70, 20);
  {
    Doc *f = clear_doc(80, 40);
    int diff = 0;
    b.noise = 100;
    line(f, &b, 0xFF0000, 10, 20, 70, 20);
    for (k = 0; k < 80 * 40; k++) if (e->lay[0]->px[k * 4 + 3] != f->lay[0]->px[k * 4 + 3]) diff++;
    CHECK(diff > 50, "noise changes the soft edge");
    CHECK(alpha(f, 40, 20) == alpha(e, 40, 20), "but not the solid middle");
    doc_free(f);
  }
  doc_free(e);
  /* Wet Edges: the middle of a hard stroke lighter than its edge */
  b = brush(PT_BRUSH, 20, 100); b.wet_edges = 100;
  e = clear_doc(80, 40);
  line(e, &b, 0xFF0000, 10, 20, 70, 20);
  CHECK(alpha(e, 40, 20) < 200 && alpha(e, 40, 20) > 60, "wet: the middle is lighter");
  CHECK(brushx_wet(&b, 128) > 128 * 1 && brushx_wet(&b, 255) < 200, "wet curve: edge up, middle down");
  doc_free(e);
  doc_free(d);
  pattern_free(pt);
}

static void test_pose_tilt(void)
{
  Doc *d = clear_doc(80, 40), *e = clear_doc(80, 40);
  Brush b = brush(PT_BRUSH, 20, 100);
  Stroke *s;
  b.dyn.pressure_size = 1;
  line(d, &b, 0, 10, 20, 70, 20);
  b.pose_flags = POSE_PRESSURE; b.pose_pressure = 50;
  line(e, &b, 0, 10, 20, 70, 20);
  CHECK(alpha(d, 40, 12) == 255 && alpha(e, 40, 12) == 0 && alpha(e, 40, 18) == 255, "Brush Pose pressure 50 %: half the size");
  doc_free(d); doc_free(e);
  /* Pen Tilt control for size: a pen lying flat paints small */
  d = clear_doc(80, 40);
  b = brush(PT_BRUSH, 20, 100);
  b.dyn.shape_on = 1; b.dyn.size.control = CTL_TILT; b.dyn.size.minimum = 0;
  s = stroke_begin(d, &b, 0, 10, 20, NULL);
  stroke_tilt(s, 0.8, 0);
  stroke_to(s, 70, 20, NULL);
  stroke_end(s);
  CHECK(alpha(d, 40, 20) == 255 && alpha(d, 40, 15) == 0, "tilted: smaller");
  doc_free(d);
  d = clear_doc(80, 40);
  b.pose_flags = POSE_TILT; b.pose_tilt_x = 0; b.pose_tilt_y = 0;
  line(d, &b, 0, 10, 20, 70, 20);
  CHECK(alpha(d, 40, 12) == 255, "Brush Pose tilt upright: full size");
  doc_free(d);
  /* rotation override turns an elliptical tip */
  d = clear_doc(60, 60); e = clear_doc(60, 60);
  b = brush(PT_BRUSH, 30, 100); b.roundness = 20;
  line(d, &b, 0, 30, 30, 30, 30);
  b.pose_flags = POSE_ROTATION; b.pose_rotation = 90;
  line(e, &b, 0, 30, 30, 30, 30);
  CHECK(alpha(d, 42, 30) == 255 && alpha(d, 30, 42) == 0 && alpha(e, 30, 42) == 255 && alpha(e, 42, 30) == 0, "rotation override");
  doc_free(d); doc_free(e);
}

static void test_mixer(void)
{
  Doc *d = doc_new(60, 30, 0xFF0000, 0);
  Brush b = brush(PT_MIXER, 10, 100);
  MixerState m;
  const u8 *p;
  mixer_init(&m);
  b.mixer = &m;
  /* dry: only the reservoir's colour */
  b.mix_wet = 0; b.mix_load = 100; b.mix_mix = 50;
  line(d, &b, 0x0000FF, 5, 5, 55, 5);
  p = pxl(d, 30, 5);
  CHECK(p[2] > 240 && p[0] < 15, "dry mixer paints the loaded blue");
  /* wet and mixing: purple from the red canvas */
  b.mix_wet = 100; b.mix_mix = 50; b.mix_load = 100; b.spacing = 100;
  line(d, &b, 0x0000FF, 5, 15, 55, 15);
  b.spacing = 10;
  p = pxl(d, 6, 15);
  if (!(p[0] > 40 && p[2] > 40)) printf("mixer px %d %d %d %d\n", p[0], p[1], p[2], p[3]);
  CHECK(p[0] > 40 && p[2] > 40, "wet mixer mixes in the canvas red");
  /* a clean, wet brush smears the canvas (red over red) */
  m.auto_load = 0;
  mixer_clean(&m); m.has_color = 1; m.color = 0x0000FF;
  b.mix_wet = 100; b.mix_mix = 100;
  line(d, &b, 0x0000FF, 5, 25, 55, 25);
  p = pxl(d, 30, 25);
  CHECK(p[0] > 240 && p[2] < 15, "a clean wet brush lays the canvas's own colour");
  /* low load: the stroke dries out */
  {
    MixerState n;
    double cv[4] = { 0, 0, 0, 0 };
    u8 out[3];
    double f0, f1;
    int k;
    mixer_init(&n); mixer_load(&n, 0x00FF00);
    f0 = mixer_dab(&n, cv, 0, 0, 10, 0, out);
    CHECK(out[1] == 255 && out[0] == 0, "the loaded green");
    for (k = 0; k < 40; k++) mixer_dab(&n, cv, 0, 0, 10, 0, out);
    f1 = mixer_dab(&n, cv, 0, 0, 10, 0, out);
    CHECK(f0 > 0.9 && f1 < 0.1, "low load dries out");
    mixer_end(&n, 0x00FF00);
    CHECK(n.loaded && n.amount == 1, "auto load after the stroke");
  }
  doc_free(d);
}

static void test_art_history(void)
{
  Doc *d = clear_doc(80, 80);
  u8 *src = (u8 *)malloc((size_t)80 * 80 * 4);
  Brush b = brush(PT_BRUSH, 4, 100);
  Box r;
  long k, red = 0, blue = 0;
  for (k = 0; k < 80 * 80; k++) { u8 *q = src + k * 4; int left = (k % 80) < 40; q[0] = (u8)(left ? 255 : 0); q[1] = 0; q[2] = (u8)(left ? 0 : 255); q[3] = 255; }
  r = art_history(d, &b, src, 40, 40, AH_TIGHT_SHORT, 40, 0, 1);
  CHECK(!box_empty(r), "strokes laid");
  for (k = 0; k < 80 * 80; k++) {
    const u8 *q = d->lay[0]->px + k * 4;
    if (q[3] > 200 && q[0] > 200 && q[2] < 50) red += (k % 80) < 44;
    if (q[3] > 200 && q[2] > 200 && q[0] < 50) blue += (k % 80) >= 36;
  }
  CHECK(red > 20 && blue > 20, "in the source's colours, each on its side");
  CHECK(doc_can_undo(d) && !strcmp(doc_undo_name(d), "Art History Brush"), "one step");
  {
    /* the tolerance: nothing where the picture already matches the source */
    Doc *e = clear_doc(80, 80);
    memcpy(e->lay[0]->px, src, (size_t)80 * 80 * 4);
    r = art_history(e, &b, src, 40, 40, AH_LOOSE_CURL, 40, 50, 2);
    CHECK(box_empty(r), "tolerance: a matching picture left alone");
    doc_free(e);
  }
  free(src);
  doc_free(d);
}

static void test_healing(void)
{
  /* a smooth gradient with a dark spot: Create Texture fills it to match */
  Doc *d = doc_new(60, 60, 0, 0);
  u8 *px = d->lay[0]->px, *mask, *out;
  i32 x, y;
  Box b = box_make(20, 20, 40, 40);
  for (y = 0; y < 60; y++) for (x = 0; x < 60; x++) { u8 *q = px + (y * 60 + x) * 4; q[0] = q[1] = q[2] = (u8)(100 + x); q[3] = 255; }
  for (y = 27; y < 33; y++) for (x = 27; x < 33; x++) px[(y * 60 + x) * 4] = px[(y * 60 + x) * 4 + 1] = px[(y * 60 + x) * 4 + 2] = 0;
  mask = (u8 *)calloc(20 * 20, 1); out = (u8 *)malloc(20 * 20 * 4);
  for (y = 5; y < 15; y++) for (x = 5; x < 15; x++) mask[y * 20 + x] = 255;
  CHECK(spot_texture_fill(px, 60, 60, b, mask, out, 7), "create texture");
  {
    const u8 *q = out + (10 * 20 + 10) * 4;
    CHECK(abs(q[0] - (100 + 30)) < 25, "the spot healed to the surroundings");
  }
  /* diffusion: 1 keeps the source's grain, 7 smooths it */
  {
    u8 base[16 * 16 * 4], src[16 * 16 * 4], m[16 * 16], o1[16 * 16 * 4], o7[16 * 16 * 4];
    long k, v1 = 0, v7 = 0;
    for (k = 0; k < 256; k++) {
      base[k * 4] = base[k * 4 + 1] = base[k * 4 + 2] = 128; base[k * 4 + 3] = 255;
      src[k * 4] = src[k * 4 + 1] = src[k * 4 + 2] = (u8)((k + k / 16) & 1 ? 160 : 96); src[k * 4 + 3] = 255;
      m[k] = (k % 16 > 2 && k % 16 < 13 && k / 16 > 2 && k / 16 < 13) ? 255 : 0;
    }
    heal_blend_ex(o1, base, src, m, 16, 16, 1);
    heal_blend_ex(o7, base, src, m, 16, 16, 7);
    for (k = 0; k < 256; k++) if (m[k]) { v1 += abs(o1[k * 4] - 128); v7 += abs(o7[k * 4] - 128); }
    CHECK(v1 > v7 * 2, "diffusion smooths the grain");
  }
  /* the Healing Brush from a pattern */
  {
    Doc *e = doc_new(40, 40, 0x808080, 0);
    Pattern *pt = pattern_new("P", 2, 2);
    Brush hb = brush(PT_HEAL, 10, 100);
    int k, varied = 0;
    for (k = 0; k < 4; k++) { pt->px[k * 4] = pt->px[k * 4 + 1] = pt->px[k * 4 + 2] = (u8)(k == 0 || k == 3 ? 200 : 50); pt->px[k * 4 + 3] = 255; }
    hb.heal_source = 1; hb.pattern = pt; hb.heal_diffusion = 1;
    line(e, &hb, 0, 20, 20, 21, 20);
    for (k = 15; k < 25; k++) if (e->lay[0]->px[(20 * 40 + k) * 4] != 0x80) varied++;
    CHECK(varied > 3, "healed with the pattern's texture");
    pattern_free(pt);
    doc_free(e);
  }
  free(mask); free(out);
  doc_free(d);
  /* Spot Healing types reach the stroke */
  {
    Doc *e = doc_new(40, 40, 0x808080, 0);
    Brush sb = brush(PT_SPOTHEAL, 8, 100);
    e->lay[0]->px[(20 * 40 + 20) * 4] = 0;
    sb.spot_type = SPOT_TEXTURE;
    line(e, &sb, 0, 20, 20, 21, 20);
    CHECK(abs(e->lay[0]->px[(20 * 40 + 20) * 4] - 0x80) < 10, "spot healing, create texture");
    doc_free(e);
  }
}

static void test_tablet(void)
{
  unsigned long t1[] = { TABLET_TAG_BASE + 6, 0x40000000UL, TABLET_TAG_BASE + 3, 0x20000000UL, TABLET_TAG_BASE + 4, 0xE0000000UL, 0, 0 };
  unsigned long t3[] = { TABLET_TAG_BASE + 8, 0, 0, 0 };
  unsigned long t2[] = { 1, 99, TABLET_TAG_BASE + 1, 300, TABLET_TAG_BASE + 2, 1200, 2, (unsigned long)t3 };
  TabletState st;
  CHECK(tablet_parse(t1, &st) && fabs(st.pressure - 0.5) < 0.001, "pressure from TABLETA_Pressure");
  CHECK(fabs(st.tilt_y - 0.25) < 0.001 && fabs(st.tilt_x - 0.25) < 0.001 && st.has_tilt, "tilt from AngleX/AngleY");
  CHECK(tablet_parse(t2, &st) && fabs(st.pressure - 0.25) < 0.001 && !st.in_proximity, "pressure from TabletZ/RangeZ, TAG_IGNORE and TAG_MORE");
  CHECK(!tablet_parse(t3, &st) && st.pressure == 1, "no pressure: full");
  CHECK(tablet_curve(0.5, 100, 0) == 0.5 && tablet_curve(0.5, 200, 0) > 0.7 && tablet_curve(0.05, 100, 10) == 0, "curve and floor");
  {
    /* events into a stroke: pressure drives the size */
    Doc *d = clear_doc(80, 40);
    Brush b = brush(PT_BRUSH, 20, 100);
    Stroke *s;
    b.dyn.pressure_size = 1;
    tablet_parse(t1, &st);
    s = stroke_begin_p(d, &b, 0, 10, 20, st.pressure, NULL);
    stroke_to_p(s, 70, 20, st.pressure, NULL);
    stroke_end(s);
    CHECK(alpha(d, 40, 17) == 255 && alpha(d, 40, 13) == 0, "half pressure, half size");
    doc_free(d);
  }
}

/* ------------------------------------------------------------------ files */
static u8 buf[8192]; static size_t bn;
static void p8(u32 v) { buf[bn++] = (u8)v; }
static void p16(u32 v) { p8(v >> 8); p8(v); }
static void p32(u32 v) { p16(v >> 16); p16(v); }
static void pkey(const char *k) { size_t n = strlen(k); if (n == 4) { p32(0); } else p32((u32)n); memcpy(buf + bn, k, n); bn += n; }
static void pustr(const char *s) { p32((u32)strlen(s) + 1); while (*s) p16((u8)*s++); p16(0); }
static void pdoub(double v) { union { double d; u8 b[8]; } u; int k, be; u.d = 1.0; be = u.b[0] == 0x3F; u.d = v; for (k = 0; k < 8; k++) p8(u.b[be ? k : 7 - k]); }
static void pobj(const char *cls, u32 n) { pustr(""); pkey(cls); p32(n); }
static void pitem(const char *k, const char *t) { pkey(k); memcpy(buf + bn, t, 4); bn += 4; }
static void stop(double r, double g, double bl, int loc, const char *type)
{
  pobj("Clrt", 4);
  pitem("Clr ", "Objc"); pobj("RGBC", 3); pitem("Rd  ", "doub"); pdoub(r); pitem("Grn ", "doub"); pdoub(g); pitem("Bl  ", "doub"); pdoub(bl);
  pitem("Type", "enum"); pkey("Clry"); pkey(type);
  pitem("Lctn", "long"); p32((u32)loc);
  pitem("Mdpn", "long"); p32(50);
}
static void test_files(void)
{
  /* .abr: written and read back */
  {
    Brush bs[2];
    const char *names[2] = { "Round", "Spatter" };
    Tip *t = tip_procedural(PTIP_SPATTER, 16, 3);
    PresetList pl;
    size_t n;
    u8 *abr;
    char err[80];
    bs[0] = brush(PT_BRUSH, 33, 80); bs[0].angle = 30; bs[0].roundness = 50; bs[0].spacing = 40;
    bs[1] = brush(PT_BRUSH, 16, 100); bs[1].tip = t;
    abr = abr_write(bs, names, 2, &n);
    presets_init(&pl);
    CHECK(abr && presets_read_abr(&pl, abr, n, "Test", err, sizeof err) == 2, ".abr round trip");
    CHECK(pl.n == 2 && pl.p[0].b.size == 33 && pl.p[0].b.hardness == 80 && pl.p[0].b.angle == 30 && pl.p[0].b.roundness == 50 && pl.p[0].b.spacing == 40, "computed tip kept");
    CHECK(pl.n == 2 && pl.p[1].tip && pl.p[1].tip->w == 16 && !memcmp(pl.p[1].tip->a, t->a, 256) && !strcmp(pl.p[1].name, "Spatter"), "sampled tip and its name kept");
    presets_free(&pl); free(abr); tip_free(t);
  }
  /* .grd version 5: a descriptor made here as Photoshop lays it out */
  {
    Gradient g[4];
    char err[80];
    bn = 0;
    memcpy(buf, "8BGR", 4); bn = 4; p16(5); p32(16);
    pobj("null", 1);
    pitem("GrdL", "VlLs"); p32(1);
    memcpy(buf + bn, "Objc", 4); bn += 4; pobj("Grdn", 1);
    pitem("Grad", "Objc"); pobj("Grdn", 5);
    pitem("Nm  ", "TEXT"); pustr("Sunset");
    pitem("GrdF", "enum"); pkey("GrdF"); pkey("CstS");
    pitem("Intr", "doub"); pdoub(4096);
    pitem("Clrs", "VlLs"); p32(3);
    memcpy(buf + bn, "Objc", 4); bn += 4; stop(255, 0, 0, 0, "UsrS");
    memcpy(buf + bn, "Objc", 4); bn += 4; stop(0, 0, 0, 2048, "FrgC");
    memcpy(buf + bn, "Objc", 4); bn += 4; stop(0, 0, 255, 4096, "UsrS");
    pitem("Trns", "VlLs"); p32(2);
    memcpy(buf + bn, "Objc", 4); bn += 4; pobj("TrnS", 3); pitem("Opct", "UntF"); memcpy(buf + bn, "#Prc", 4); bn += 4; pdoub(100); pitem("Lctn", "long"); p32(0); pitem("Mdpn", "long"); p32(50);
    memcpy(buf + bn, "Objc", 4); bn += 4; pobj("TrnS", 3); pitem("Opct", "UntF"); memcpy(buf + bn, "#Prc", 4); bn += 4; pdoub(25); pitem("Lctn", "long"); p32(4096); pitem("Mdpn", "long"); p32(50);
    CHECK(grd_read(buf, bn, g, 4, err, sizeof err) == 1, ".grd v5 read");
    CHECK(!strcmp(g[0].name, "Sunset") && g[0].ncolor == 3 && g[0].nopacity == 2, "name and stops");
    CHECK(g[0].color[0].rgb == 0xFF0000 && g[0].color[1].kind == GS_FG && fabs(g[0].color[1].pos - 0.5) < 1e-6 && g[0].color[2].rgb == 0x0000FF, "colours, foreground stop");
    CHECK(fabs(g[0].opacity[1].opacity - 0.25) < 1e-6, "opacity stop");
    /* broken: cut short */
    CHECK(grd_read(buf, bn / 2, g, 4, err, sizeof err) == 0, "cut short: none, no crash");
    /* version 3 */
    bn = 0; memcpy(buf, "8BGR", 4); bn = 4; p16(3); p16(1);
    p8(4); memcpy(buf + bn, "Mono", 4); bn += 4;
    p16(2);
    p32(0); p32(50); p16(0); p16(0); p16(0); p16(0); p16(0); p16(0);
    p32(4096); p32(50); p16(0); p16(65535); p16(65535); p16(65535); p16(0); p16(0);
    p16(1); p32(0); p32(50); p16(255);
    p16(0); p16(0); p16(0);
    CHECK(grd_read(buf, bn, g, 4, err, sizeof err) == 1 && g[0].ncolor == 2 && g[0].color[1].rgb == 0xFFFFFF && !strcmp(g[0].name, "Mono"), ".grd v3");
  }
  /* .pat: written and read back, then one in PackBits made by hand */
  {
    Pattern *p = pattern_new("Tiles", 5, 3);
    const Pattern *ps[1];
    PatternList l;
    size_t n;
    u8 *pat;
    char err[80];
    int k;
    for (k = 0; k < 15; k++) { p->px[k * 4] = (u8)(k * 10); p->px[k * 4 + 1] = (u8)(255 - k); p->px[k * 4 + 2] = (u8)(k * 3); p->px[k * 4 + 3] = (u8)(k < 5 ? 128 : 255); }
    ps[0] = p;
    pat = pat_write(ps, 1, &n);
    patlist_init(&l);
    CHECK(pat && pat_read(&l, pat, n, err, sizeof err) == 1, ".pat round trip");
    CHECK(l.n == 1 && l.p[0]->w == 5 && l.p[0]->h == 3 && !memcmp(l.p[0]->px, p->px, 60) && !strcmp(l.p[0]->name, "Tiles"), "pixels, transparency and name kept");
    CHECK(pat_read(&l, pat, n / 2, err, sizeof err) == 0, "cut short: none");
    free(pat);
    /* grey, PackBits: 4 x 2, rows of 10s and 200s */
    bn = 0; memcpy(buf, "8BPT", 4); bn = 4; p16(1); p32(1);
    p32(1); p32(1); p16(2); p16(4); pustr("G"); p8(0);
    p32(3); { size_t at = bn; p32(0);
      p32(0); p32(0); p32(2); p32(4); p32(24);
      p32(1); { size_t at2 = bn; p32(0);
        p32(8); p32(0); p32(0); p32(2); p32(4); p16(8); p8(1);
        p16(2); p16(2); p8(0xFD); p8(10); p8(0xFD); p8(200);
        buf[at2] = 0; buf[at2 + 1] = 0; buf[at2 + 2] = 0; buf[at2 + 3] = (u8)(bn - at2 - 4); }
      p32(0); p32(0);
      buf[at] = 0; buf[at + 1] = 0; buf[at + 2] = (u8)((bn - at - 4) >> 8); buf[at + 3] = (u8)(bn - at - 4); }
    CHECK(pat_read(&l, buf, bn, err, sizeof err) == 1 && l.n == 2 && l.p[1]->px[0] == 10 && l.p[1]->px[4 * 4] == 200 && l.p[1]->px[3] == 255, ".pat grey PackBits");
    patlist_free(&l);
    pattern_free(p);
  }
}

int main(void)
{
  test_texture_dual_noise_wet();
  test_pose_tilt();
  test_mixer();
  test_art_history();
  test_healing();
  test_tablet();
  test_files();
  printf("%d tests, %d failed\n", tests, fails);
  return fails != 0;
}
