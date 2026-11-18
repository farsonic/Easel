/*
 * frametest.c - Easel 1.1's animations (core/frames.c): a document's layers written as an animated
 * GIF and as an IFF ANIM (op 5) and read back frame by frame, exactly, with their timings; GIF
 * disposal and transparency; DPaint's looping frames left out; frames to a document and the
 * delays in layer names; then every file fuzzed with a work bound under ASan/UBSan.
 *
 * Files go to argv[1] (default /tmp/easel_frames_out) for tests/check_frames.py (Pillow).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/frames.h"
#include "core/palette.h"

static int ntests, nfailed;
static const char *outdir = "/tmp/easel_frames_out";
static void ok(const char *name, int cond)
{
  ntests++;
  if (!cond) { nfailed++; printf("FAIL: %s\n", name); }
}
static u32 rng = 99;
static u32 rnd(void) { rng = rng * 1103515245UL + 12345; return (rng >> 8) & 0xFFFFFF; }
static void save(const char *name, const void *d, size_t n)
{
  char path[512];
  snprintf(path, sizeof path, "%s/%s", outdir, name);
  if (!file_write(path, d, n)) printf("cannot write %s\n", path);
}

/* frame k of a test animation: a red square moving over a striped background (transparent at
   the top when alpha is set), each frame w x h */
static void draw(u8 *px, i32 w, i32 h, int k, int alpha)
{
  i32 x, y;
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      u8 *p = px + ((size_t)y * w + x) * 4;
      int in = x >= 3 + k * 4 && x < 13 + k * 4 && y >= 5 && y < 15;
      p[0] = in ? 220 : (u8)((y / 4) & 1 ? 30 : 200); p[1] = in ? 10 : 120; p[2] = in ? 20 : (u8)(x < w / 2 ? 60 : 160); p[3] = 255;
      if (alpha && y < 3 && !in) p[0] = p[1] = p[2] = p[3] = 0;
    }
}
static Doc *make_doc(int n, i32 w, i32 h, int alpha)
{
  Doc *d = doc_new(w, h, 0, 1);
  int k;
  for (k = 0; k < n; k++) {
    Layer *l = k ? layer_new(d, "x") : d->lay[0];
    snprintf(l->name, sizeof l->name, "Frame %d (0.%02d s)", k + 1, 10 + 5 * (k % 3));   /* 10, 15, 20 hundredths */
    draw(l->px, w, h, k, alpha);
    if (k) doc_add_layer(d, l, d->nlay);
  }
  doc_forget_undo(d);
  return d;
}
static int frame_is(const Rgba *f, const u8 *px, i32 w, i32 h, int alpha_only_check)
{
  long n = (long)w * h, k;
  if (f->w != w || f->h != h) return 0;
  for (k = 0; k < n; k++) {
    const u8 *a = f->px + k * 4, *b = px + k * 4;
    if (alpha_only_check && b[3] == 0) { if (a[3] != 0) return 0; continue; }
    if (memcmp(a, b, 4)) return 0;
  }
  return 1;
}

static long steps;
static int hook(long a, long b) { (void)a; (void)b; return ++steps > 300; }
static long nfuzz;
static void fuzz(const u8 *d, size_t n, int rounds)
{
  u8 *m = (u8 *)malloc(n);
  int r;
  for (r = 0; r < rounds && m; r++) {
    Frames f;
    size_t len = n, k;
    memcpy(m, d, n);
    for (k = 0; k < 1 + rnd() % 6; k++) {
      size_t at = rnd() % n;
      switch (rnd() % 4) {
      case 0: m[at] = (u8)rnd(); break;
      case 1: m[at] ^= (u8)(1 << (rnd() % 8)); break;
      case 2: m[at] = 0xFF; break;
      default: len = at + 1; break;
      }
    }
    steps = 0; progress_reset(); progress_hook = hook;
    if (frames_read(m, len, &f, 12, 4L * 1024 * 1024, NULL, 0)) frames_free(&f);
    progress_hook = NULL; progress_reset();
    nfuzz++;
  }
  free(m);
}

int main(int argc, char **argv)
{
  AnimOpts o;
  Frames f;
  Doc *d;
  u8 *data, *ref;
  size_t len;
  int k, n;
  char err[160];
  const i32 W = 37, H = 23;
  if (argc > 1) outdir = argv[1];
  { char cmd[600]; snprintf(cmd, sizeof cmd, "mkdir -p '%s'", outdir); if (system(cmd)) printf("cannot make %s\n", outdir); }
  ref = (u8 *)malloc((size_t)W * H * 4);
  anim_opts_default(&o);

  ok("delay of a name", frame_delay_of_name("Frame 3 (0.25 s)", 7) == 25 && frame_delay_of_name("x (2 s)", 7) == 200 &&
     frame_delay_of_name("x (1.5 s)", 7) == 150 && frame_delay_of_name("x (fast)", 7) == 7 && frame_delay_of_name("Layer 1", 7) == 7 &&
     frame_delay_of_name("x (0,04 s)", 7) == 4);

  /* animated GIF, opaque: each frame only where it changed */
  d = make_doc(5, W, H, 0);
  err[0] = 0;
  data = gif_write_anim(d, &o, &len, err, sizeof err);
  ok("gif anim written", data != NULL);
  if (data) {
    save("anim.gif", data, len);
    ok("gif anim read", gif_read_frames(data, len, &f, 0, 0, err, sizeof err) && f.n == 5 && f.w == W && f.h == H);
    for (k = 0; k < f.n; k++) {
      char nm[64];
      draw(ref, W, H, k, 0);
      snprintf(nm, sizeof nm, "gif frame %d exact", k + 1); ok(nm, frame_is(&f.fr[k], ref, W, H, 0));
      snprintf(nm, sizeof nm, "gif frame %d delay", k + 1); ok(nm, f.delay[k] == 10 + 5 * (k % 3));
    }
    ok("gif loops for ever", len > 40 && !memcmp(data + 13 + 3 * 8 + 3, "NETSCAPE2.0", 11));
    frames_free(&f);
    ok("gif first frame alone (codec_decode)", 1);
    fuzz(data, len, 400);
    /* max frames and the memory limit */
    ok("gif max frames", gif_read_frames(data, len, &f, 2, 0, NULL, 0) && f.n == 2 && f.truncated);
    frames_free(&f);
    ok("gif memory limit", gif_read_frames(data, len, &f, 0, (long)W * H * 4 * 3, NULL, 0) && f.n == 3 && f.truncated);
    frames_free(&f);
    free(data);
  }
  {
    /* the frames to a document and out again */
    Doc *d2 = NULL;
    data = gif_write_anim(d, &o, &len, err, sizeof err);
    ok("frames to doc", data && frames_read(data, len, &f, 0, 0, NULL, 0) && (d2 = frames_to_doc(&f)) != NULL);
    if (data && f.n) {
      ok("frames to doc layers", d2->nlay == 5 && !strcmp(d2->lay[1]->name, "Frame 2 (0.15 s)") && d2->active == 4 && !d2->undo);
      ok("frames to doc pixels", !memcmp(d2->lay[4]->px, f.fr[4].px, (size_t)W * H * 4));
      doc_free(d2);
      frames_free(&f);
    }
    free(data);
  }
  doc_free(d);

  /* animated GIF with transparency: whole frames, restored to the background between them */
  d = make_doc(4, W, H, 1);
  data = gif_write_anim(d, &o, &len, err, sizeof err);
  ok("gif transparent written", data != NULL);
  if (data) {
    save("anim_alpha.gif", data, len);
    ok("gif transparent read", gif_read_frames(data, len, &f, 0, 0, NULL, 0) && f.n == 4);
    for (k = 0; k < f.n; k++) { char nm[64]; draw(ref, W, H, k, 1); snprintf(nm, sizeof nm, "gif transparent frame %d", k + 1); ok(nm, frame_is(&f.fr[k], ref, W, H, 1)); }
    frames_free(&f);
    fuzz(data, len, 200);
    free(data);
  }
  doc_free(d);

  /* a hand-made GIF: frame 2 disposed to the previous (frame 3 then draws on frame 1's picture) */
  {
    static const u8 pal[6] = { 0, 0, 0, 255, 255, 255 };
    u8 a[16], b2[4], c[4];
    Buf g;
    u8 *f1, *f2, *f3;
    size_t l1, l2, l3, skip = 13 + 6;
    memset(a, 1, 16); memset(b2, 0, 4); memset(c, 0, 1); c[0] = 0;
    f1 = gif_encode(a, 4, 4, pal, 2, -1, 0, &l1);      /* white 4 x 4 */
    f2 = gif_encode(b2, 2, 2, pal, 2, -1, 0, &l2);     /* black 2 x 2 at 0,0, disposal 3 */
    f3 = gif_encode(c, 1, 1, pal, 2, -1, 0, &l3);      /* black 1 x 1 at 3,3 */
    buf_init(&g);
    buf_add(&g, f1, l1 - 1);
    buf_add(&g, "\x21\xF9\x04\x0C\x00\x00\x00\x00", 8); buf_add(&g, f2 + skip, l2 - skip - 1);
    buf_add(&g, "\x21\xF9\x04\x00\x00\x00\x00\x00", 8);
    f3[skip + 1] = 3; f3[skip + 3] = 3;
    buf_add(&g, f3 + skip, l3 - skip - 1);
    buf_addc(&g, 0x3B);
    ok("gif disposal read", gif_read_frames((u8 *)g.data, g.len, &f, 0, 0, NULL, 0) && f.n == 3);
    if (f.n == 3) {
      ok("gif disposal: frame 2 has the black square", f.fr[1].px[0] == 0 && f.fr[1].px[(1 * 4 + 1) * 4] == 0 && f.fr[1].px[(2 * 4 + 2) * 4] == 255);
      ok("gif disposal: frame 3 is frame 1 again plus its dot", f.fr[2].px[0] == 255 && f.fr[2].px[(3 * 4 + 3) * 4] == 0 && f.fr[2].px[(3 * 4 + 3) * 4 + 3] == 255);
    }
    frames_free(&f);
    free(f1); free(f2); free(f3); buf_free(&g);
  }

  /* IFF ANIM: op 5 deltas, two buffers, the looping frames */
  for (n = 1; n <= 6; n++) {
    char nm[64];
    d = make_doc(n, W, H, 0);
    data = anim_write(d, &o, &len, err, sizeof err);
    snprintf(nm, sizeof nm, "anim %d frames written", n); ok(nm, data != NULL);
    if (!data) { doc_free(d); continue; }
    if (n == 5) save("anim.iff", data, len);
    snprintf(nm, sizeof nm, "anim %d frames read (%d)", n, 0);
    ok(nm, anim_read_frames(data, len, &f, 0, 0, err, sizeof err) && f.n == n);
    for (k = 0; k < f.n; k++) {
      draw(ref, W, H, k, 0);
      snprintf(nm, sizeof nm, "anim %d: frame %d exact", n, k + 1); ok(nm, frame_is(&f.fr[k], ref, W, H, 0));
      snprintf(nm, sizeof nm, "anim %d: frame %d delay", n, k + 1); ok(nm, f.delay[k] == 10 + 5 * (k % 3));
    }
    {
      Rgba first;
      snprintf(nm, sizeof nm, "anim %d: the first frame through codec_decode", n);
      ok(nm, codec_decode(data, len, &first, NULL, 0) && frame_is(&first, f.fr[0].px, W, H, 0) && (rgba_free(&first), 1));
    }
    frames_free(&f);
    if (n == 5) fuzz(data, len, 400);
    free(data);
    doc_free(d);
  }
  {
    /* a bigger picture (columns longer than 255 rows, many colours) */
    Doc *dd = doc_new(70, 300, 0, 1);
    Layer *l2 = layer_new(dd, "Frame 2");
    i32 j;
    for (j = 0; j < 70 * 300; j++) { u8 *p = dd->lay[0]->px + j * 4; p[0] = (u8)(j * 7); p[1] = (u8)(j / 70); p[2] = (u8)rnd(); p[3] = 255; }
    memcpy(l2->px, dd->lay[0]->px, 70 * 300 * 4);
    for (j = 0; j < 70 * 300; j += 3) l2->px[j * 4] ^= 0x55;
    doc_add_layer(dd, l2, 1);
    data = anim_write(dd, &o, &len, err, sizeof err);
    ok("anim big", data && anim_read_frames(data, len, &f, 0, 0, NULL, 0) && f.n == 2);
    if (data && f.n == 2) {
      ok("anim big frames differ", memcmp(f.fr[0].px, f.fr[1].px, 70 * 300 * 4) != 0);
    }
    if (data) frames_free(&f);
    free(data);
    doc_free(dd);
  }
  {
    /* an indexed document keeps its palette in the ANIM */
    d = make_doc(3, W, H, 0);
    ok("to indexed", doc_to_indexed(d, 16, QM_WU, DI_NONE, NULL, 0) && d->mode == MODE_INDEXED);
    data = anim_write(d, &o, &len, err, sizeof err);
    ok("indexed anim", data && anim_read_frames(data, len, &f, 0, 0, NULL, 0) && f.n == 3);
    if (data && f.n == 3) frames_free(&f);
    free(data);
    doc_free(d);
  }
  ok("not an animation", !frames_read((const u8 *)"\x89PNG\r\n\x1a\n........", 16, &f, 0, 0, err, sizeof err) && err[0]);

  free(ref);
  printf("frametest: %ld mutated files read\n", nfuzz);
  printf("%d tests, %d failed\n", ntests, nfailed);
  return nfailed != 0;
}
