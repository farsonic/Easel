/*
 * amigafmt.h - more picture files to RGBA: WebP (webp.c) and the Amiga's (and its neighbours')
 * legacy formats that ilbm.c doesn't cover (amigafmt.c). Portable C99, built natively for the
 * tests and with vbcc for the Amiga; every multi-byte number is read a byte at a time.
 *
 * Every reader has the same shape as codec.h's: the picture goes into *out (w * h straight RGBA,
 * rows top down, malloc'd); 1 on success, else 0 with *out left empty and a message in err (err
 * may be NULL). Input is hostile: each length and offset is checked against the data. codec_oom
 * is set when a reader fails for want of memory rather than because the file is bad.
 */
#ifndef EASEL_AMIGAFMT_H
#define EASEL_AMIGAFMT_H

#include "codec.h"

/* what amigafmt_detect recognises (none of codec.h's own formats: a plain ILBM or PNG is
   AMF_UNKNOWN here) */
enum {
  AMF_UNKNOWN,
  AMF_WEBP,          /* RIFF WEBP: lossy, lossless, extended with alpha, animated (first frame) */
  AMF_ACBM,          /* IFF ACBM: contiguous bitplanes */
  AMF_RGBN,          /* IFF RGBN: Impulse 4+4+4-bit run-length pixels */
  AMF_RGB8,          /* IFF RGB8: Impulse 8+8+8-bit run-length pixels */
  AMF_DEEP,          /* IFF DEEP: TVPaint chunky deep pixels */
  AMF_ILBM_LINEPAL,  /* IFF ILBM with a SHAM, CTBL or PCHG chunk: a palette that changes per line */
  AMF_ANIM,          /* IFF ANIM: the first frame */
  AMF_INFO,          /* Workbench .info icon (classic, NewIcons, GlowIcons; a PNG icon is just PNG) */
  AMF_SUNRAS,        /* Sun raster */
  AMF_SGI,           /* SGI image (.rgb, .bw, .sgi) */
  AMF_DEGAS,         /* Atari ST Degas / Degas Elite (.pi1-3, .pc1-3) */
  AMF_NEO            /* Atari ST NEOchrome (.neo) */
};
#define AMF_LAST AMF_NEO

/* the format from the file's first bytes (Degas and NEOchrome, which have no signature, also by
   their exact sizes) */
int   amigafmt_detect(const u8 *d, size_t n);
const char *amigafmt_name(int fmt);                 /* "WebP", "IFF ACBM", ... ("unknown") */
/* any of them by amigafmt_detect, into *out */
int   amigafmt_decode(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);

/* ------------------------------------------------------------------ webp.c */
/* lossy (VP8: segments, partitions, both loop filters; YUV to RGB and chroma upsampling as libwebp
   does them), lossless (VP8L), extended (VP8X) with ALPH alpha, and an animation's first frame on
   its canvas */
int   webp_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);

/* ------------------------------------------------------------------ amigafmt.c */
/* IFF ACBM (BMHD, CMAP, CAMG, ABIT): 1-8 planes, EHB, HAM6/8, 24/32 deep, mask plane and colour */
int   acbm_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);
/* IFF RGBN (13 planes) and RGB8 (25 planes), compression 4; a set genlock bit is transparent */
int   rgbn_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);
/* IFF DEEP (DGBL, DPEL, DLOC, DBOD, TVDC): 8-bit R/G/B/A elements (others skipped, 16-bit ones
   kept to their high byte), uncompressed, run-length or TVDC-compressed */
int   deep_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);
/* any IFF ILBM, with a SHAM, CTBL (Dynamic HiRes) or PCHG (12- or 32-bit changes, plain or
   Huffman-compressed) palette change list applied line by line; also HAM, EHB and masks */
int   sham_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);
/* IFF ANIM: the first frame (the first FORM ILBM, ACBM or PBM inside) */
int   anim_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);
/* a Workbench .info icon: its GlowIcon (OS 3.5 FORM ICON) image if it has one, else its
   NewIcons image (from the tooltypes), else its classic planar image; a PNG icon (OS4, AROS) is
   handed to png_read */
int   info_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);
/* the same with a choice: which image (0 normal, 1 selected) and, for a classic image, the
   palette (INFO_PAL_AUTO picks 1.x or 2.x+ from the icon's revision); a classic icon's colour 0
   is transparent when clear0 is set */
enum { INFO_PAL_AUTO, INFO_PAL_13, INFO_PAL_20, INFO_PAL_MWB };
int   info_read_ex(const u8 *d, size_t n, int which, int pal, int clear0, Rgba *out, char *err, size_t errcap);
/* Sun raster: 1, 8, 24 and 32 bits, standard, RGB or byte-encoded (RLE), with or without a map */
int   sunras_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);
/* SGI: 1-4 channels, 8 or 16 bits (high byte kept), verbatim or RLE */
int   sgi_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);
/* Atari Degas (PI1-3) and Degas Elite compressed (PC1-3); low resolution, medium and high */
int   degas_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);
/* Atari NEOchrome */
int   neo_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);

#endif
