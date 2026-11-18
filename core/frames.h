/*
 * frames.h - animations (Easel 1.1): every frame of an animated GIF or an IFF ANIM read as a stack
 * of pictures (File > Import > Frames to Layers), and a document's layers written as an animated
 * GIF or an IFF ANIM (op 5, DPaint's byte vertical delta), one layer a frame from the bottom up.
 * Portable C99.
 *
 * Frame timings: GIF keeps hundredths of a second, ANIM sixtieths (jiffies); Frames keeps
 * hundredths. A layer named "Frame 3 (0.25 s)" is shown for 0.25 s; a name without a time gets
 * the default.
 */
#ifndef EASEL_FRAMES_H
#define EASEL_FRAMES_H

#include "codec.h"

#define FRAMES_MAX MAXLAYERS

typedef struct {
  i32 w, h;                           /* every frame's size */
  int n;                              /* frames read */
  Rgba fr[FRAMES_MAX];                /* each a whole picture, as the animation shows it */
  int delay[FRAMES_MAX];              /* hundredths of a second (0 when the file gives none) */
  int truncated;                      /* 1 when the file had more frames than were read (memory or FRAMES_MAX) */
} Frames;
void frames_free(Frames *f);

/* every frame of an animated GIF: each drawn over the one before as its disposal method says
   (none or leave, restore to background, restore to previous), its transparency kept. At most max
   frames (and FRAMES_MAX), stopping early (truncated set) when the next frame wouldn't fit in
   memory_limit bytes in all (0: no limit). 1 with at least one frame, else 0 with a message */
int gif_read_frames(const u8 *d, size_t n, Frames *f, int max, long memory_limit, char *err, size_t errcap);
/* every frame of an IFF ANIM of ILBMs (1-8 planes, EHB and HAM included): the first a whole
   picture, the rest as op 5 byte vertical deltas (or op 0: whole bodies), with two buffers as
   ANHD's interleave says, palette changes kept. The two frames DPaint adds at the end to make the
   animation loop (copies of the first two) are left out. As gif_read_frames otherwise */
int anim_read_frames(const u8 *d, size_t n, Frames *f, int max, long memory_limit, char *err, size_t errcap);
/* either, by the file's kind (0 if it is neither) */
int frames_read(const u8 *d, size_t n, Frames *f, int max, long memory_limit, char *err, size_t errcap);
/* the frames as a document, a layer each ("Frame 1 (0.10 s)" ...), the first at the bottom.
   NULL if out of memory */
Doc *frames_to_doc(const Frames *f);

/* the delay a layer's name gives (hundredths of a second), def when it gives none */
int  frame_delay_of_name(const char *name, int def);

/* a document's pixel layers (hidden ones too; groups, adjustment and fill layers left out) as
   frames, bottom first, with their delays from their names (def: the default). An indexed
   document's palette is kept; anything else is quantized to one palette of at most maxc colours
   (palette.h's QM_ method and DI_ dither). */
typedef struct {
  int maxc, method, dither;
  int def_delay;                      /* hundredths of a second (0: 10) */
  int loops;                          /* GIF: how many times it plays, 0 for ever */
} AnimOpts;
void anim_opts_default(AnimOpts *o);
/* an animated GIF89a: a global palette, the NETSCAPE2.0 loop block, each frame only where it
   changed (or whole, restored to the background, when there is transparency). NULL with a
   message in err */
u8  *gif_write_anim(Doc *d, const AnimOpts *o, size_t *len, char *err, size_t errcap);
/* an IFF ANIM: the first frame as a ByteRun1 ILBM (with the document's ModeID and aspect), the rest
   as op 5 deltas for two buffers, and the two frames that make it loop. NULL with a message */
u8  *anim_write(Doc *d, const AnimOpts *o, size_t *len, char *err, size_t errcap);

#endif
