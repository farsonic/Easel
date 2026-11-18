/*
 * xread2.h - Easel 1.2: more picture readers and writers. Read: Kodak Cineon (.cin, 8/10/16-bit,
 * log or linear), DICOM (single frame, uncompressed, monochrome or RGB, with the window or the
 * data's range) and OpenEXR (scanline files, uncompressed, RLE, ZIPS, ZIP or PIZ, half, float or
 * uint channels; tone-mapped as Easel does Radiance files). Written: Cineon (10-bit log, the
 * usual 95/685 reference points) and Radiance HDR (RGBE with new-style run lengths, from the
 * picture's sRGB values). Portable C99; every length and offset is checked against the data.
 *
 * These formats aren't in codec.h's FMT_ table (to keep its numbering): codec_decode tries
 * xread2_decode for anything it doesn't know, and the writers have numbers of their own.
 */
#ifndef EASEL_XREAD2_H
#define EASEL_XREAD2_H

#include "codec.h"

enum { XR_NONE, XR_CINEON, XR_DICOM, XR_EXR };
int  xread2_detect(const u8 *d, size_t n);                    /* XR_ */
const char *xread2_name(int kind);                            /* "Cineon" ... */
int  xread2_decode(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);

/* an OpenEXR file's pixels as linear floats (w * h * 4: R, G, B, A; grey files fill R, G and B;
   a missing alpha is 1), malloc'd; NULL with a message in err */
float *exr_read_float(const u8 *d, size_t n, i32 *w, i32 *h, char *err, size_t errcap);
/* linear RGB(A) floats tone-mapped to 8-bit sRGB as Easel opens HDR pictures: scaled so the
   log-average luminance is 0.18, then Reinhard's c / (1 + c) */
void tonemap_float(const float *f, long np, u8 *rgba);

/* the formats written (numbers past xwrite.h's) */
#define FMT_CINEON_OUT (FMT_N + 2)
#define FMT_HDR_OUT    (FMT_N + 3)
u8  *cineon_encode(const Rgba *im, size_t *len);
/* RGBE of the picture's colours as linear light (sRGB decoded), scale 1 */
u8  *hdr_encode(const Rgba *im, size_t *len);

#endif
