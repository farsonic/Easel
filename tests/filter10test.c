/*
 * filter10test.c - native tests of Easel 1.0's filters, adjustments and retouching (core/filters10.c,
 * core/adjust10.c, core/retouch.c and the healing brushes in core/stroke.c).
 *
 * For each: identity settings leave the pixels as they were (where that means something), the same
 * settings give the same pixels, nothing reads or writes out of bounds (the sanitizers), small
 * known answers, adjustment layers give what the destructive adjustment gives, and OpenRaster keeps
 * the new settings. Built and run by build.sh with the rest of the core.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../core/doc.h"
#include "../core/paint.h"
#include "../core/adjust.h"
#include "../core/adjust10.h"
#include "../core/filters10.h"
#include "../core/retouch.h"
#include "../core/codec.h"
#include "../core/layers.h"

static int tests, failed;
static void ok(const char *name, int cond) { tests++; if (!cond) { failed++; printf("FAIL %s\n", name); } }
static u8 *px(Doc *d, i32 x, i32 y) { return d->lay[d->active]->px + ((long)y * d->w + x) * 4; }
static unsigned long rs = 1;
static int rnd(int n) { rs = rs * 1103515245UL + 12345UL; return (int)((rs >> 8) % (unsigned long)n); }

static Doc *flat(i32 w, i32 h, u32 c) { return doc_new(w, h, c, 0); }
static Doc *noisy(i32 w, i32 h)
{
  Doc *d = doc_new(w, h, 0, 0);
  long k;
  for (k = 0; k < (long)w * h * 4; k++) d->lay[0]->px[k] = (u8)rnd(256);
  for (k = 0; k < (long)w * h; k++) d->lay[0]->px[k * 4 + 3] = 255;
  return d;
}
/* a smooth picture with fine texture on top: what healing is for */
static void texture_px(u8 *p, i32 x, i32 y)
{
  double t = 18 * sin(x * 1.3) * cos(y * 0.9) + 10 * sin((x + 2 * y) * 0.7);
  p[0] = (u8)(60 + x * 1.2 + t); p[1] = (u8)(90 + y * 0.8 + t); p[2] = (u8)(120 + (x + y) * 0.3 + t); p[3] = 255;
}
static Doc *textured(i32 w, i32 h)
{
  Doc *d = doc_new(w, h, 0, 0);
  i32 x, y;
  for (y = 0; y < h; y++) for (x = 0; x < w; x++) texture_px(px(d, x, y), x, y);
  return d;
}
static u8 *copy_px(Doc *d) { u8 *c = (u8 *)malloc((size_t)d->w * d->h * 4); memcpy(c, d->lay[0]->px, (size_t)d->w * d->h * 4); return c; }
static int same_px(Doc *d, const u8 *c) { return !memcmp(d->lay[0]->px, c, (size_t)d->w * d->h * 4); }
static int maxdiff(const u8 *a, const u8 *b, long n)
{
  long k;
  int m = 0;
  for (k = 0; k < n; k++) { int dd = abs(a[k] - b[k]); if (dd > m) m = dd; }
  return m;
}
static Doc *dup_doc(Doc *s)
{
  Doc *d = doc_new(s->w, s->h, 0, 0);
  memcpy(d->lay[0]->px, s->lay[0]->px, (size_t)s->w * s->h * 4);
  return d;
}
static u8 *composite(Doc *d)
{
  u8 *o = (u8 *)malloc((size_t)d->w * d->h * 4);
  doc_composite(d, doc_box(d), o, (long)d->w * 4);
  return o;
}
static void set_px(Doc *d, i32 x, i32 y, int r, int g, int b) { u8 *p = px(d, x, y); p[0] = (u8)r; p[1] = (u8)g; p[2] = (u8)b; p[3] = 255; }

/* ------------------------------------------------------------------ determinism and identity */
typedef int (*Fn)(Doc *d);
static int f_avg(Doc *d) { return flt_average(d); }
static int f_more(Doc *d) { return flt_blur_more(d); }
static int f_radial(Doc *d) { return flt_radial_blur_ex(d, 20, 0, 10, 12, 1); }
static int f_radialz(Doc *d) { return flt_radial_blur_ex(d, 30, 1, 30, 5, 2); }
static int f_tilt(Doc *d) { return flt_tilt_shift(d, 6, 20, 10, 8, 10); }
static int f_iris(Doc *d) { return flt_iris_blur(d, 6, 20, 15, 8, 6, 50); }
static int f_smart0(Doc *d) { return flt_smart_sharpen(d, 150, 15, 10, 0, 0); }
static int f_smart1(Doc *d) { return flt_smart_sharpen(d, 150, 25, 0, 1, 0); }
static int f_smart2(Doc *d) { return flt_smart_sharpen(d, 120, 30, 5, 2, 30); }
static int f_sedges(Doc *d) { return flt_sharpen_edges(d); }
static int f_sph1(Doc *d) { return flt_spherize_ex(d, 80, 1); }
static int f_sph2(Doc *d) { return flt_spherize_ex(d, -60, 2); }
static int f_wave(Doc *d) { return flt_wave_ex(d, 5, 10, 60, 2, 9, 100, 100, 0, 1, 77); }
static int f_wave2(Doc *d) { return flt_wave_ex(d, 3, 5, 20, 1, 6, 50, 100, 1, 0, 78); }
static int f_wave3(Doc *d) { return flt_wave_ex(d, 2, 5, 20, 1, 6, 100, 30, 2, 1, 79); }
static int f_lens(Doc *d) { return flt_lens_correction(d, 40, 30, -20, -50, 40, 110); }
static int f_extb(Doc *d) { return flt_extrude(d, 0, 6, 10, 1, 0, 1, 5); }
static int f_extb2(Doc *d) { return flt_extrude(d, 0, 7, 12, 0, 1, 0, 5); }
static int f_extp(Doc *d) { return flt_extrude(d, 1, 8, 20, 0, 1, 0, 5); }
static int f_glow(Doc *d) { return flt_glowing_edges(d, 4, 8, 3); }
static int f_plastic(Doc *d) { return flt_plastic_wrap(d, 15, 9, 7); }
static int f_e1(Doc *d) { return flt_edge_detect(d, 1, 1); }
static int f_e2(Doc *d) { return flt_edge_detect(d, 2, 0); }
static int f_e3(Doc *d) { return flt_edge_detect(d, 3, 1); }
static int f_minr(Doc *d) { return flt_minmax(d, 4, 0, 1); }
static int f_maxs(Doc *d) { return flt_minmax(d, 7, 1, 0); }
static int f_off(Doc *d) { return flt_offset_ex(d, 7, -5, 2, 0); }
static int f_offbg(Doc *d) { return flt_offset_ex(d, -3, 9, 1, 0x123456); }
static int f_c2a(Doc *d) { return flt_color_to_alpha(d, 0xFFFFFF); }
static int f_hsl(Doc *d) { return flt_hsb_hsl(d, 0); }
static int f_light0(Doc *d) { return flt_lighting_ex(d, 0, 10, 10, 30, 0, 30, 60, 0xFFFFFF, 20, 50, 40); }
static int f_light1(Doc *d) { return flt_lighting_ex(d, 1, 0, 0, 60, -45, 25, 70, 0xFFE0C0, 0, 0, 0); }
static int f_light2(Doc *d) { return flt_lighting_ex(d, 2, 0, 0, 30, 135, 30, 50, 0xFFFFFF, 10, 80, 60); }
static int f_sh(Doc *d) { return adj_shadows_highlights(d, 50, 50, 8, 40, 40, 6, 20, 15); }
static int f_match(Doc *d) { return adj_match_color(d, NULL, 0, 0, 120, 80, 10, 1); }
static int f_redeye(Doc *d) { return !box_empty(redeye_fix(d, doc_box(d), 50, 50)); }
static void determinism(void)
{
  static const struct { const char *name; Fn fn; } fl[] = {
    { "average", f_avg }, { "blur more", f_more }, { "radial spin", f_radial }, { "radial zoom", f_radialz }, { "tilt-shift", f_tilt },
    { "iris", f_iris }, { "smart sharpen gaussian", f_smart0 }, { "smart sharpen lens", f_smart1 }, { "smart sharpen motion", f_smart2 },
    { "sharpen edges", f_sedges }, { "spherize horizontal", f_sph1 }, { "spherize vertical", f_sph2 }, { "wave sine", f_wave },
    { "wave triangle", f_wave2 }, { "wave square", f_wave3 }, { "lens correction", f_lens }, { "extrude blocks", f_extb },
    { "extrude blocks level", f_extb2 }, { "extrude pyramids", f_extp }, { "glowing edges", f_glow }, { "plastic wrap", f_plastic },
    { "prewitt", f_e1 }, { "laplacian", f_e2 }, { "roberts", f_e3 }, { "minimum round", f_minr }, { "maximum square", f_maxs },
    { "offset repeat", f_off }, { "offset background", f_offbg }, { "color to alpha", f_c2a }, { "hsl", f_hsl },
    { "lighting point", f_light0 }, { "lighting spot", f_light1 }, { "lighting infinite", f_light2 }, { "shadows/highlights", f_sh },
    { "match color neutralize", f_match }, { "red eye (none in noise or some)", f_redeye }
  };
  int i;
  char msg[120];
  for (i = 0; i < (int)(sizeof fl / sizeof fl[0]); i++) {
    Doc *a, *b;
    u8 *c;
    rs = 99;
    a = noisy(41, 33);
    b = dup_doc(a);
    fl[i].fn(a); fl[i].fn(b);
    sprintf(msg, "%s: deterministic", fl[i].name);
    ok(msg, !memcmp(a->lay[0]->px, b->lay[0]->px, (size_t)41 * 33 * 4));
    /* through a feathered selection in a corner (the box's edge at the canvas's) */
    doc_free(b);
    b = noisy(41, 33);
    c = copy_px(b);
    sel_ellipse(b, -5, -4, 18, 15, SEL_REPLACE, 3);
    fl[i].fn(b);
    {
      i32 x, y;
      int outside = 1;
      for (y = 0; y < 33; y++) for (x = 0; x < 41; x++)
        if (!b->sel[(long)y * 41 + x] && memcmp(px(b, x, y), c + ((long)y * 41 + x) * 4, 4)) outside = 0;
      sprintf(msg, "%s: nothing outside the selection changes", fl[i].name);
      ok(msg, outside);
    }
    /* undo puts it back */
    if (doc_can_undo(b)) { doc_undo(b); sprintf(msg, "%s: undone", fl[i].name); ok(msg, same_px(b, c)); }
    free(c);
    doc_free(a); doc_free(b);
    /* a one-pixel and a thin picture */
    a = noisy(1, 1); fl[i].fn(a); doc_free(a);
    a = noisy(1, 9); fl[i].fn(a); doc_free(a);
    a = noisy(13, 2); fl[i].fn(a); doc_free(a);
  }
}

/* ------------------------------------------------------------------ filters: known answers */
static void filters(void)
{
  Doc *d, *e;
  u8 *c;
  int i, good;
  long k;
  /* identities on a flat picture: blurs, sharpening and distortions leave it */
  {
    static const Fn flatfn[] = { f_more, f_radial, f_radialz, f_tilt, f_iris, f_smart0, f_smart1, f_smart2, f_sedges, f_sph1, f_sph2, f_wave, f_minr, f_maxs, f_off };
    for (i = 0; i < (int)(sizeof flatfn / sizeof flatfn[0]); i++) {
      char msg[80];
      d = flat(30, 20, 0x4080C0);
      c = copy_px(d);
      flatfn[i](d);
      sprintf(msg, "flat picture unchanged (%d)", i);
      ok(msg, same_px(d, c));
      free(c); doc_free(d);
    }
  }
  /* zero settings change nothing */
  rs = 5; d = noisy(30, 25); c = copy_px(d);
  flt_spherize_ex(d, 0, 1); ok("spherize 0 horizontal", same_px(d, c));
  flt_wave_ex(d, 3, 10, 20, 0, 0, 100, 100, 0, 1, 1); ok("wave amplitude 0", same_px(d, c));
  flt_lens_correction(d, 0, 0, 0, 0, 50, 100); ok("lens correction zero", same_px(d, c));
  flt_plastic_wrap(d, 0, 5, 5); ok("plastic wrap strength 0", same_px(d, c));
  ok("shadows/highlights all zero does nothing", adj_shadows_highlights(d, 0, 50, 30, 0, 50, 30, 0, 0) == 0 && same_px(d, c));
  ok("match color identity does nothing", adj_match_color(d, NULL, 0, 0, 100, 100, 0, 0) == 0 && same_px(d, c));
  {
    u8 *map = (u8 *)malloc(7 * 5 * 4);
    for (k = 0; k < 7 * 5 * 4; k++) map[k] = 128;
    flt_displace(d, map, 7, 5, 50, 50, 1, 0); ok("displace by a mid-grey map", same_px(d, c));
    /* a map of 255 in red: everything moves right by scale % of 127 pixels */
    for (k = 0; k < 7 * 5; k++) map[k * 4] = 255;
    doc_free(d); d = noisy(30, 25); free(c); c = copy_px(d);
    flt_displace(d, map, 7, 5, 4, 0, 0, 1);
    good = 1;
    {
      /* 127/128 * 4% * 128 = 5.08 px: between the pixels 5 and 6 to the left */
      i32 x, y;
      for (y = 0; y < 25; y++) for (x = 0; x < 30; x++) {
        const u8 *q5 = c + ((long)y * 30 + (x + 25) % 30) * 4, *q6 = c + ((long)y * 30 + (x + 24) % 30) * 4, *p = px(d, x, y);
        int ch;
        for (ch = 0; ch < 3; ch++) if (abs(p[ch] - (int)(q5[ch] * 0.92 + q6[ch] * 0.08 + 0.5)) > 1) good = 0;
      }
    }
    ok("displace shifts by the map", good);
    free(map);
  }
  free(c); doc_free(d);
  /* average */
  d = flat(10, 10, 0); set_px(d, 0, 0, 100, 0, 0);
  for (k = 1; k < 100; k++) { u8 *p = d->lay[0]->px + k * 4; p[0] = 0; p[1] = 200; p[2] = 0; }
  flt_average(d);
  ok("average", px(d, 5, 5)[0] == 1 && px(d, 5, 5)[1] == 198 && px(d, 0, 0)[1] == 198);
  doc_free(d);
  /* the square maximum and minimum agree with the old filters */
  rs = 3; d = noisy(37, 29); e = dup_doc(d);
  flt_minmax(d, 3, 1, 0); flt_maximum(e, 3);
  ok("square maximum as Maximum", !memcmp(d->lay[0]->px, e->lay[0]->px, (size_t)37 * 29 * 4));
  doc_free(d); doc_free(e);
  rs = 4; d = noisy(37, 29); e = dup_doc(d);
  flt_minmax(d, 5, 0, 0); flt_minimum(e, 5);
  ok("square minimum as Minimum", !memcmp(d->lay[0]->px, e->lay[0]->px, (size_t)37 * 29 * 4));
  doc_free(d); doc_free(e);
  /* a round maximum of one bright pixel is a disc */
  d = flat(21, 21, 0); set_px(d, 10, 10, 255, 255, 255);
  flt_minmax(d, 5, 1, 1);
  ok("round maximum: a disc", px(d, 15, 10)[0] == 255 && px(d, 10, 5)[0] == 255 && px(d, 14, 13)[0] == 255 && px(d, 14, 14)[0] == 0 && px(d, 16, 10)[0] == 0);
  doc_free(d);
  /* offset agrees with the old one where they overlap */
  rs = 6; d = noisy(23, 17); e = dup_doc(d);
  flt_offset_ex(d, 5, -3, 3, 0); flt_offset(e, 5, -3, 1);
  ok("offset wrap", !memcmp(d->lay[0]->px, e->lay[0]->px, (size_t)23 * 17 * 4));
  doc_free(d); doc_free(e);
  rs = 6; d = noisy(23, 17); e = dup_doc(d);
  flt_offset_ex(d, 5, -3, 0, 0); flt_offset(e, 5, -3, 0);
  ok("offset transparent", !memcmp(d->lay[0]->px, e->lay[0]->px, (size_t)23 * 17 * 4));
  doc_free(d); doc_free(e);
  rs = 6; d = noisy(23, 17); c = copy_px(d);
  flt_offset_ex(d, 5, 0, 2, 0);
  ok("offset repeat edge", !memcmp(px(d, 3, 4), c + (4 * 23 + 0) * 4, 4) && !memcmp(px(d, 9, 4), c + (4 * 23 + 4) * 4, 4));
  flt_offset_ex(d, 0, 3, 1, 0xAABBCC);
  ok("offset background", px(d, 2, 1)[0] == 0xAA && px(d, 2, 1)[2] == 0xCC && px(d, 2, 1)[3] == 255);
  free(c); doc_free(d);
  /* color to alpha: the colour goes, and the rest over the colour gives what was there */
  rs = 8; d = noisy(16, 16); c = copy_px(d);
  set_px(d, 3, 3, 255, 255, 255);
  memcpy(c + (3 * 16 + 3) * 4, px(d, 3, 3), 4);
  flt_color_to_alpha(d, 0xFFFFFF);
  ok("color to alpha: the colour is transparent", px(d, 3, 3)[3] == 0);
  good = 1;
  for (k = 0; k < 256; k++) {
    const u8 *p = d->lay[0]->px + k * 4, *q = c + k * 4;
    int ch;
    if (k == 3 * 16 + 3) continue;
    for (ch = 0; ch < 3; ch++) { double back = p[ch] * p[3] / 255.0 + 255 * (1 - p[3] / 255.0); if (fabs(back - q[ch]) > 2.5) good = 0; }
  }
  ok("color to alpha: over the colour it looks the same", good);
  free(c); doc_free(d);
  /* HSL and HSB there and back */
  rs = 9; d = noisy(16, 16); c = copy_px(d);
  flt_hsb_hsl(d, 0); flt_hsb_hsl(d, 1);
  ok("rgb to hsl and back", maxdiff(d->lay[0]->px, c, 16 * 16 * 4) <= 6);
  flt_hsb_hsl(d, 2); flt_hsb_hsl(d, 3);
  ok("rgb to hsb and back", maxdiff(d->lay[0]->px, c, 16 * 16 * 4) <= 8);
  doc_free(d); d = flat(4, 4, 0xFF0000); flt_hsb_hsl(d, 2);
  ok("pure red in hsb", px(d, 1, 1)[0] == 0 && px(d, 1, 1)[1] == 255 && px(d, 1, 1)[2] == 255);
  free(c); doc_free(d);
  /* glowing edges: flat is black; an edge glows */
  d = flat(20, 20, 0x808080); flt_glowing_edges(d, 2, 10, 1);
  ok("glowing edges: flat goes black", px(d, 10, 10)[0] == 0 && px(d, 3, 17)[2] == 0);
  doc_free(d);
  d = flat(20, 20, 0); for (k = 0; k < 20 * 20; k++) if (k % 20 >= 10) memset(d->lay[0]->px + k * 4, 255, 3);
  flt_glowing_edges(d, 2, 10, 1);
  ok("glowing edges: the edge is bright", px(d, 10, 10)[0] > 200 && px(d, 2, 10)[0] == 0);
  doc_free(d);
  /* edge detection on flat */
  d = flat(10, 10, 0x336699); flt_edge_detect(d, 2, 0); ok("laplacian of flat is black", px(d, 5, 5)[0] == 0 && px(d, 0, 0)[2] == 0);
  flt_edge_detect(d, 1, 1); ok("prewitt inverted of black is white", px(d, 5, 5)[1] == 255);
  doc_free(d);
  /* smart sharpen raises contrast at an edge */
  d = flat(20, 20, 0x404040); for (k = 0; k < 20 * 20; k++) if (k % 20 >= 10) memset(d->lay[0]->px + k * 4, 0xC0, 3);
  flt_smart_sharpen(d, 100, 20, 0, 0, 0);
  ok("smart sharpen: edge contrast up", px(d, 9, 5)[0] < 0x40 && px(d, 10, 5)[0] > 0xC0);
  doc_free(d);
  /* tilt-shift leaves its band sharp */
  rs = 10; d = noisy(30, 30); c = copy_px(d);
  flt_tilt_shift(d, 4, 15, 0, 6, 4);
  ok("tilt-shift: the band stays sharp", !memcmp(px(d, 5, 15), c + (15 * 30 + 5) * 4, 4) && !memcmp(px(d, 20, 13), c + (13 * 30 + 20) * 4, 4));
  ok("tilt-shift: blurred outside", memcmp(px(d, 5, 2), c + (2 * 30 + 5) * 4, 4) != 0);
  free(c); doc_free(d);
  /* extrude pyramids lit from the top left on flat grey */
  d = flat(16, 16, 0x808080); flt_extrude(d, 1, 8, 8, 0, 1, 0, 1);
  ok("pyramids: top face lighter, bottom darker", px(d, 4, 1)[0] > 0x80 && px(d, 4, 6)[0] < 0x80);
  doc_free(d);
  /* lighting: a spot light lights inside its cone only */
  d = flat(40, 40, 0x808080);
  flt_lighting_ex(d, 1, 0, 20, 60, 0, 20, 80, 0xFFFFFF, 0, 0, 0);
  ok("spot light: lit in the cone, dark outside", px(d, 15, 20)[0] > px(d, 15, 2)[0] && px(d, 15, 2)[0] < 0x80);
  doc_free(d);
  /* lens correction: barrel taken out pulls the corners in from further out */
  d = flat(40, 40, 0); for (k = 0; k < 40 * 40; k++) d->lay[0]->px[k * 4] = (u8)(k % 40 * 6);
  c = copy_px(d);
  flt_lens_correction(d, -60, 0, 0, 0, 50, 100);
  ok("lens correction: the middle stays", abs(px(d, 20, 20)[0] - c[(20 * 40 + 20) * 4]) <= 3);
  ok("lens correction: the edge moves", abs(px(d, 3, 20)[0] - c[(20 * 40 + 3) * 4]) > 3);
  free(c); doc_free(d);
}

/* ------------------------------------------------------------------ adjustments */
static void apply_p(Doc *d, const AdjParams *p) { adj_apply(d, p, "t"); }
static void adjustments(void)
{
  Doc *d, *e;
  u8 *c;
  AdjParams p, q;
  int i;
  long k;
  char buf[1024], path[64];
  /* Hue/Saturation: ranges at zero change nothing; master alone is Easel 0.9's */
  rs = 11; d = noisy(20, 20); c = copy_px(d);
  adj_defaults(AK_HUESAT, &p);
  ok("hue/sat defaults have ranges", p.npts == 15 && p.pts[3] == 15 && p.pts[4] == 30);
  {
    int m4[4] = { 0, 0, 0, 0 }, r30[30];
    memcpy(r30, p.pts, sizeof r30);
    adj_hue_saturation_ranges(d, m4, r30);
    ok("hue/sat ranges at zero: unchanged", same_px(d, c));
    m4[0] = 40; m4[1] = 20; m4[2] = -10;
    adj_hue_saturation_ranges(d, m4, r30);
    e = doc_new(20, 20, 0, 0); memcpy(e->lay[0]->px, c, 20 * 20 * 4);
    memset(&q, 0, sizeof q); q.kind = AK_HUESAT; q.v[0] = 40; q.v[1] = 20; q.v[2] = -10;
    apply_p(e, &q);
    ok("hue/sat master as before", maxdiff(d->lay[0]->px, e->lay[0]->px, 20 * 20 * 4) <= 1);
    doc_free(e);
    e = doc_new(20, 20, 0, 0); memcpy(e->lay[0]->px, c, 20 * 20 * 4);
    adj_hue_saturation(e, 40, 20, -10, 0);
    ok("hue/sat master near the exact one", maxdiff(d->lay[0]->px, e->lay[0]->px, 20 * 20 * 4) <= 12);
    doc_free(e);
  }
  free(c); doc_free(d);
  /* a Reds change moves red and leaves blue and grey */
  d = flat(3, 1, 0); set_px(d, 0, 0, 230, 20, 20); set_px(d, 1, 0, 20, 20, 230); set_px(d, 2, 0, 128, 128, 128);
  adj_defaults(AK_HUESAT, &p); p.pts[0] = 120; p.pts[2] = 30;
  apply_p(d, &p);
  ok("reds range: red turns green", px(d, 0, 0)[1] > 150 && px(d, 0, 0)[0] < 100);
  ok("reds range: blue stays", abs(px(d, 1, 0)[0] - 20) <= 2 && abs(px(d, 1, 0)[2] - 230) <= 2);
  ok("reds range: grey stays", abs(px(d, 2, 0)[0] - 128) <= 2 && abs(px(d, 2, 0)[1] - 128) <= 2);
  doc_free(d);
  /* Black & White, six colours */
  d = flat(4, 1, 0); set_px(d, 0, 0, 255, 0, 0); set_px(d, 1, 0, 255, 255, 0); set_px(d, 2, 0, 90, 90, 90); set_px(d, 3, 0, 0, 0, 255);
  adj_defaults(AK_BLACKWHITE, &p);
  ok("black & white defaults: six sliders", p.v[9] == 1 && p.v[1] == 60 && p.v[5] == 80);
  apply_p(d, &p);
  ok("b&w: red at 40%", abs(px(d, 0, 0)[0] - 102) <= 1 && px(d, 0, 0)[1] == px(d, 0, 0)[0]);
  ok("b&w: yellow at 60%", abs(px(d, 1, 0)[0] - 153) <= 1);
  ok("b&w: grey stays", abs(px(d, 2, 0)[0] - 90) <= 1);
  ok("b&w: blue at 20%", abs(px(d, 3, 0)[2] - 51) <= 1);
  doc_free(d);
  d = flat(2, 1, 0x808080);
  { int w6[6] = { 40, 60, 40, 60, 20, 80 }; adj_black_white6(d, w6, 1, 30, 40); }
  ok("b&w tint: coloured", px(d, 0, 0)[0] > px(d, 0, 0)[2] + 20);
  doc_free(d);
  /* Easel 0.9's three-weight Black & White is still read as such */
  memset(&p, 0, sizeof p); p.kind = AK_BLACKWHITE; p.v[0] = 100;
  d = flat(1, 1, 0x408020); apply_p(d, &p);
  ok("b&w 0.9 layout", abs(px(d, 0, 0)[0] - 0x40) <= 1);
  doc_free(d);
  /* Replace Color */
  d = flat(3, 1, 0); set_px(d, 0, 0, 200, 50, 50); set_px(d, 1, 0, 50, 200, 50); set_px(d, 2, 0, 205, 55, 50);
  ok("replace color", adj_replace_color(d, 0xC83232, 0, 120, 0, 0));
  ok("replace color: the colour changes", px(d, 0, 0)[1] > 150);
  ok("replace color: others stay", px(d, 1, 0)[1] == 200 && px(d, 2, 0)[0] == 205);
  adj_replace_color(d, 0x32C832, 60, 0, 0, -50);
  ok("replace color: fuzziness and lightness", px(d, 1, 0)[1] < 150);
  doc_free(d);
  /* Shadows/Highlights */
  d = flat(40, 40, 0x202020);
  for (k = 0; k < 40 * 40; k++) if (k % 40 >= 20) memset(d->lay[0]->px + k * 4, 0xE8, 3);
  set_px(d, 5, 5, 0, 0, 0);
  adj_shadows_highlights(d, 60, 50, 5, 60, 50, 5, 0, 0);
  ok("shadows lifted", px(d, 10, 20)[0] > 0x30);
  ok("black stays black", px(d, 5, 5)[0] == 0);
  ok("highlights lowered", px(d, 30, 20)[0] < 0xE8);
  ok("shadows/highlights keeps grey grey", px(d, 10, 20)[0] == px(d, 10, 20)[2]);
  doc_free(d);
  /* Match Color: to a source's mean; neutralize takes a cast out */
  rs = 12; d = noisy(30, 30);
  {
    u8 *src = (u8 *)malloc(10 * 10 * 4);
    double m[3] = { 0, 0, 0 };
    for (k = 0; k < 100; k++) { src[k * 4] = (u8)(180 + k % 20); src[k * 4 + 1] = (u8)(60 + k % 10); src[k * 4 + 2] = 40; src[k * 4 + 3] = 255; }
    adj_match_color(d, src, 10, 10, 100, 100, 0, 0);
    for (k = 0; k < 900; k++) { int ch; for (ch = 0; ch < 3; ch++) m[ch] += d->lay[0]->px[k * 4 + ch] / 900.0; }
    ok("match color: the means move to the source's", fabs(m[0] - 189.5) < 12 && fabs(m[1] - 64.5) < 12 && fabs(m[2] - 40) < 12);
    free(src);
  }
  doc_free(d);
  d = flat(10, 10, 0x9070A0);
  adj_match_color(d, NULL, 0, 0, 100, 100, 0, 1);
  ok("match color: neutralized", abs(px(d, 3, 3)[0] - px(d, 3, 3)[1]) <= 1 && abs(px(d, 3, 3)[2] - px(d, 3, 3)[1]) <= 1);
  doc_free(d);
  /* the Levels eyedroppers */
  {
    u32 col = 0x304A28;
    d = flat(3, 1, 0); set_px(d, 0, 0, 0x30, 0x4A, 0x28); set_px(d, 1, 0, 0xC0, 0xA0, 0xD0); set_px(d, 2, 0, 0x80, 0x60, 0x90);
    adj_defaults(AK_LEVELS, &p);
    adj_levels_pick(&p, 0, col);
    adj_levels_pick(&p, 2, 0xC0A0D0);
    e = dup_doc(d); apply_p(e, &p);
    ok("levels black point", px(e, 0, 0)[0] <= 1 && px(e, 0, 0)[1] <= 1 && px(e, 0, 0)[2] <= 1);
    ok("levels white point", px(e, 1, 0)[0] >= 254 && px(e, 1, 0)[1] >= 254 && px(e, 1, 0)[2] >= 254);
    adj_levels_pick(&p, 1, 0x806090);
    doc_free(e); e = dup_doc(d); apply_p(e, &p);
    ok("levels grey point", abs(px(e, 2, 0)[0] - px(e, 2, 0)[1]) <= 3 && abs(px(e, 2, 0)[2] - px(e, 2, 0)[1]) <= 3);
    doc_free(e);
    /* and Curves' */
    adj_defaults(AK_CURVES, &p);
    adj_curves_pick(&p, 0, col);
    adj_curves_pick(&p, 2, 0xC0A0D0);
    e = dup_doc(d); apply_p(e, &p);
    ok("curves made four", p.v[1] == 1);
    ok("curves black point", px(e, 0, 0)[0] <= 1 && px(e, 0, 0)[1] <= 1 && px(e, 0, 0)[2] <= 1);
    ok("curves white point", px(e, 1, 0)[0] >= 254 && px(e, 1, 0)[1] >= 254 && px(e, 1, 0)[2] >= 254);
    adj_curves_pick(&p, 1, 0x806090);
    doc_free(e); e = dup_doc(d); apply_p(e, &p);
    ok("curves grey point", abs(px(e, 2, 0)[0] - px(e, 2, 0)[1]) <= 3 && abs(px(e, 2, 0)[2] - px(e, 2, 0)[1]) <= 3);
    for (i = 0; i < 9; i++) adj_curves_pick(&p, 1, (u32)(0x101010 * (i + 2)));
    ok("curves points kept within eight", p.v[3] <= CURVE10_MAX && p.v[4] <= CURVE10_MAX && p.v[5] <= CURVE10_MAX);
    doc_free(e); doc_free(d);
  }
  /* an Easel 0.9 curve made four gives the same pixels */
  rs = 13; d = noisy(16, 16); e = dup_doc(d);
  adj_defaults(AK_CURVES, &p); p.v[0] = 2; p.npts = 3; p.pts[2] = 100; p.pts[3] = 160; p.pts[4] = 255; p.pts[5] = 255;
  q = p; curves10_convert(&q);
  apply_p(d, &p); apply_p(e, &q);
  ok("curves converted to four: the same", !memcmp(d->lay[0]->px, e->lay[0]->px, 16 * 16 * 4));
  doc_free(d); doc_free(e);
  /* presets */
  {
    static const int kinds[4] = { AK_LEVELS, AK_CURVES, AK_HUESAT, AK_BLACKWHITE };
    int kk, all = 1;
    for (kk = 0; kk < 4; kk++) {
      ok("presets exist", adj_preset_count(kinds[kk]) >= 8);
      for (i = 0; i < adj_preset_count(kinds[kk]); i++) {
        int n;
        adj_preset_get(kinds[kk], i, &p);
        n = adj_preset_format(&p, buf, sizeof buf);
        memset(&q, 0x55, sizeof q);
        if (!n || !adj_preset_parse(buf, &q) || q.kind != p.kind || memcmp(q.v, p.v, sizeof p.v) || q.npts != p.npts ||
            memcmp(q.pts, p.pts, (size_t)p.npts * 2 * sizeof(int))) all = 0;
        rs = 14; d = noisy(8, 8); apply_p(d, &p); doc_free(d);
        if (!adj_preset_name(kinds[kk], i)[0]) all = 0;
      }
    }
    ok("presets: written and read back", all);
    ok("no presets for other kinds", adj_preset_count(AK_INVERT) == 0);
    adj_preset_get(AK_CURVES, 7, &p);
    ok("the Negative preset", !strcmp(adj_preset_name(AK_CURVES, 7), "Negative"));
    d = flat(1, 1, 0x205080); apply_p(d, &p);
    ok("the Negative preset inverts", abs(px(d, 0, 0)[0] - 0xDF) <= 1 && abs(px(d, 0, 0)[2] - 0x7F) <= 1);
    doc_free(d);
    sprintf(path, "/tmp/easel_f10_preset.txt");
    adj_preset_get(AK_HUESAT, 4, &p);
    ok("preset saved", adj_preset_save(&p, path));
    memset(&q, 0, sizeof q);
    ok("preset loaded", adj_preset_load(path, &q) && q.kind == AK_HUESAT && q.pts[1] == 35 && q.npts == 15);
    ok("not a preset", !adj_preset_parse("hello\nkind Levels\n", &q) && !adj_preset_parse("EaselAdjust 1\nkind Nothing\n", &q));
  }
  /* adjustment layers give what the adjustments give */
  {
    static const int kinds[5] = { AK_HUESAT, AK_BLACKWHITE, AK_REPLACECOLOR, AK_CURVES, AK_LEVELS };
    adjlayer_init();
    for (i = 0; i < 5; i++) {
      u8 *comp, *ref;
      int li;
      char msg[80];
      rs = 15 + i; d = noisy(17, 11);
      adj_defaults(kinds[i], &p);
      switch (kinds[i]) {
      case AK_HUESAT: p.v[1] = 10; p.pts[0] = 60; p.pts[5 * 4 + 1] = -40; p.pts[5 * 2 + 2] = 20; break;
      case AK_BLACKWHITE: p.v[6] = 1; p.v[7] = 200; p.v[8] = 30; break;
      case AK_REPLACECOLOR: p.v[0] = 0x808080; p.v[1] = 120; p.v[2] = 90; p.v[3] = 30; break;
      case AK_CURVES: adj_curves_pick(&p, 0, 0x202830); adj_curves_pick(&p, 1, 0x707080); break;
      case AK_LEVELS: adj_levels_pick(&p, 2, 0xE0D0C0); break;
      }
      li = doc_new_adjust(d, &p, adj_names[kinds[i]]);
      comp = composite(d);
      e = doc_new(17, 11, 0, 0);
      memcpy(e->lay[0]->px, d->lay[0]->px, 17 * 11 * 4);
      apply_p(e, &p);
      ref = e->lay[0]->px;
      sprintf(msg, "adjustment layer %s as the adjustment (max %d)", adj_names[kinds[i]], maxdiff(ref, comp, 17 * 11 * 4));
      ok(msg, li == 1 && maxdiff(ref, comp, 17 * 11 * 4) <= 1);
      /* OpenRaster keeps the settings */
      {
        size_t len;
        u8 *file = ora_write(d, &len);
        char err[100];
        Doc *r = file ? ora_read(file, len, err, sizeof err) : NULL;
        sprintf(msg, "openraster keeps %s", adj_names[kinds[i]]);
        ok(msg, r && r->nlay == 2 && r->lay[1]->kind == LK_ADJUST && r->lay[1]->adj.kind == kinds[i] &&
                !memcmp(r->lay[1]->adj.v, p.v, sizeof p.v) && r->lay[1]->adj.npts == p.npts &&
                !memcmp(r->lay[1]->adj.pts, p.pts, (size_t)(kinds[i] == AK_CURVES ? 64 : p.npts * 2) * sizeof(int)));
        if (r) doc_free(r);
        free(file);
      }
      free(comp); doc_free(e); doc_free(d);
    }
  }
  /* Photo Filter's list */
  ok("photo filters", PHOTO_FILTERS == 20 && !strcmp(photo_filter_names[0], "Warming Filter (85)") && photo_filter_colors[0] == 0xEC8A00);
}

/* ------------------------------------------------------------------ retouching */
static double err_box(Doc *d, Doc *ref, Box b)
{
  double e = 0;
  i32 x, y;
  for (y = b.y0; y < b.y1; y++) for (x = b.x0; x < b.x1; x++) {
    int c;
    for (c = 0; c < 3; c++) { double dd = px(d, x, y)[c] - px(ref, x, y)[c]; e += dd * dd; }
  }
  return sqrt(e / ((double)(b.x1 - b.x0) * (b.y1 - b.y0) * 3));
}
static void stroke_line(Doc *d, Brush *b, double x0, double y0, double x1, double y1)
{
  Box ch;
  Stroke *s = stroke_begin(d, b, 0, x0, y0, &ch);
  int i;
  for (i = 1; i <= 20; i++) stroke_to(s, x0 + (x1 - x0) * i / 20, y0 + (y1 - y0) * i / 20, &ch);
  stroke_end(s);
}
static void retouching(void)
{
  Doc *orig, *d, *cl;
  Brush b;
  Box area = box_make(12, 26, 68, 34);
  double e0, e1, e2;
  i32 x, y;
  /* a scratch across a texture, healed from below: better than leaving it, and better than cloning
     (the source is a different colour: the healing takes the colour from round the scratch) */
  orig = textured(80, 60);
  d = dup_doc(orig);
  for (x = 15; x < 65; x++) { set_px(d, x, 29, 255, 255, 255); set_px(d, x, 30, 255, 255, 255); }
  e0 = err_box(d, orig, area);
  cl = dup_doc(d);
  memset(&b, 0, sizeof b);
  b.tool = PT_HEAL; b.size = 7; b.hardness = 100; b.opacity = 100; b.flow = 100; b.spacing = 25; b.clone_dx = 0; b.clone_dy = 14;
  stroke_line(d, &b, 15, 29.5, 64, 29.5);
  e1 = err_box(d, orig, area);
  b.tool = PT_CLONE;
  stroke_line(cl, &b, 15, 29.5, 64, 29.5);
  e2 = err_box(cl, orig, area);
  ok("healing brush: the scratch is healed", e1 < e0 / 4);
  ok("healing brush: better than the clone stamp", e1 < e2);
  ok("healing brush: one undo step", doc_can_undo(d));
  doc_undo(d);
  ok("healing brush: undone", fabs(err_box(d, orig, area) - e0) < 1e-9);
  doc_free(d); doc_free(cl);
  /* a dot spot-healed */
  d = dup_doc(orig);
  for (y = 38; y < 43; y++) for (x = 48; x < 53; x++) set_px(d, x, y, 0, 0, 0);
  e0 = err_box(d, orig, box_make(44, 34, 57, 47));
  memset(&b, 0, sizeof b);
  b.tool = PT_SPOTHEAL; b.size = 10; b.hardness = 100; b.opacity = 100; b.flow = 100; b.spacing = 25;
  stroke_line(d, &b, 50, 40, 50.5, 40.5);
  e1 = err_box(d, orig, box_make(44, 34, 57, 47));
  ok("spot healing: the dot is gone", e1 < e0 / 5);
  {
    int dark = 0;
    for (y = 38; y < 43; y++) for (x = 48; x < 53; x++) if (px(d, x, y)[0] < 30 && px(d, x, y)[1] < 30) dark = 1;
    ok("spot healing: no black left", !dark);
  }
  doc_free(d);
  /* the Patch tool: the selection healed from beside it */
  d = dup_doc(orig);
  for (x = 15; x < 65; x++) set_px(d, x, 20, 0, 255, 0);
  e0 = err_box(d, orig, box_make(12, 17, 68, 24));
  sel_rect(d, 13, 18, 67, 23, SEL_REPLACE, 0);
  ok("patch", !box_empty(patch_heal(d, 0, 9)));
  ok("patch: healed", err_box(d, orig, box_make(12, 17, 68, 24)) < e0 / 4);
  doc_free(d);
  /* heal_blend with the source the same as the base changes nothing */
  {
    u8 base[6 * 5 * 4], mask[30], out[6 * 5 * 4];
    int k;
    for (k = 0; k < 120; k++) base[k] = (u8)rnd(256);
    for (k = 0; k < 30; k++) mask[k] = (u8)(k % 6 && k % 6 < 5 && k > 6 && k < 24 ? 255 : 0);
    ok("heal_blend: the same source", heal_blend(out, base, base, mask, 6, 5) && !memcmp(out, base, sizeof out));
    for (k = 0; k < 30; k++) mask[k] = 255;
    ok("heal_blend: all of it", heal_blend(out, base, base, mask, 6, 5) && !memcmp(out, base, sizeof out));
  }
  doc_free(orig);
  /* red eye */
  d = flat(40, 30, 0xDCAA8C);
  for (y = 5; y < 25; y++) for (x = 8; x < 32; x++) { double dx = x - 20, dy = y - 15; if (dx * dx / 144 + dy * dy / 64 < 1) set_px(d, x, y, 245, 245, 240); }
  for (y = 0; y < 30; y++) for (x = 0; x < 40; x++) { double dx = x + 0.5 - 20.5, dy = y + 0.5 - 15.5; if (dx * dx + dy * dy < 30) set_px(d, x, y, 200, 30, 40); }
  ok("red eye", !box_empty(redeye_fix(d, box_make(5, 2, 36, 28), 50, 50)));
  {
    int red = 0, skin = 1;
    for (y = 0; y < 30; y++) for (x = 0; x < 40; x++) {
      const u8 *p = px(d, x, y);
      double dx = x + 0.5 - 20.5, dy = y + 0.5 - 15.5;
      if (dx * dx + dy * dy < 30 && p[0] > p[1] + 30) red = 1;
      if ((x < 4 || y < 2) && (p[0] != 0xDC || p[1] != 0xAA)) skin = 0;
    }
    ok("red eye: the red is gone", !red);
    ok("red eye: the skin stays", skin);
    ok("red eye: darker", px(d, 20, 15)[0] < 60);
  }
  ok("red eye: nothing to do in skin", box_empty(redeye_fix(d, box_make(0, 0, 6, 6), 50, 50)));
  doc_free(d);
}

int main(void)
{
  determinism();
  filters();
  adjustments();
  retouching();
  printf("%d tests, %d failed\n", tests, failed);
  return failed != 0;
}
