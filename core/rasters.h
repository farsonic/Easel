/*
 * rasters.h - reading more picture formats into Easel's flat RGBA pictures (codec.h's Rgba):
 * TIFF (tiff.c), Photoshop PSD/PSB composites (psd.c), and Targa, PCX, Netpbm (PBM/PGM/PPM/PAM/
 * PFM), XPM, DDS, QOI, Windows ICO/CUR, Radiance HDR, FITS and DPX (rasters.c). Read only.
 *
 * Every reader has codec.h's shape: 1 on success with the picture in *out (straight RGBA, rows top
 * down, malloc'd), else 0 with *out empty and a message in err (err may be NULL); codec_oom is set
 * when the failure was want of memory. Input is hostile: every length and offset is checked, a
 * truncated or damaged file fails cleanly and nothing is read past d + n.
 *
 * Portable C99 (natively for the tests, vbcc for the 68k Amiga, big-endian): multi-byte numbers
 * are read a byte at a time.
 */
#ifndef EASEL_RASTERS_H
#define EASEL_RASTERS_H

#include "codec.h"

/* what rasters_detect recognises (its own numbering, apart from codec.h's FMT_) */
enum {
  RAS_UNKNOWN, RAS_TIFF, RAS_PSD, RAS_TGA, RAS_PCX, RAS_PNM, RAS_XPM, RAS_DDS, RAS_QOI,
  RAS_ICO, RAS_HDR, RAS_FITS, RAS_DPX
};

/* the format from the file's first bytes. Targa has no signature, so it is only claimed when the
   whole 18-byte header is plausible and the pixel data fits (or the TGA 2.0 footer is there); test
   it after every format that has a signature. The third argument is unused (pass 0) */
int   rasters_detect(const u8 *d, size_t n, size_t namelen_unused);
const char *rasters_name(int fmt);                  /* "TIFF", "Targa", ... ("unknown") */
/* any of the formats below, by rasters_detect */
int   rasters_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);

/* ------------------------------------------------------------------ tiff.c */
/* classic TIFF and BigTIFF, either byte order; the first full-resolution page. Uncompressed,
   PackBits, LZW (new and old style), Deflate, CCITT RLE / Group 3 / Group 4, JPEG (new style);
   predictors 2 and 3; strips or tiles, chunky or planar; 1-16-bit and 32-bit integer and 16/32/
   64-bit float samples; gray, palette, RGB, CMYK, YCbCr (unsubsampled or JPEG) and CIE Lab, with
   alpha (straight or premultiplied) */
int   tiff_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);

/* ------------------------------------------------------------------ psd.c */
/* the flattened composite of a PSD or PSB: raw, RLE, ZIP or ZIP with prediction; bitmap, gray,
   duotone (as gray), indexed, RGB, CMYK, Lab and multichannel (first channels as gray); 1, 8, 16
   and 32 bits; the merged transparency where the file has one (white matte removed) */
int   psd_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);

/* ------------------------------------------------------------------ rasters.c */
/* Targa: colour-mapped, true colour and gray, raw or RLE, 8/15/16/24/32 bits, any origin */
int   tga_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);
/* PCX: 1-bit (1-4 planes), 2/4-bit, 8-bit with or without the VGA palette, 24-bit and 32-bit */
int   pcx_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);
/* Netpbm: P1-P6 (ASCII and binary, maxval up to 65535), P7 PAM (gray, RGB, CMYK, with alpha) and
   PFM (PF / Pf, either byte order; clamped to 0-1) */
int   pnm_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);
/* XPM2 and XPM3: hex colours, X11 colour names, None for transparency; never runs the C */
int   xpm_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);
/* DDS, the top mip level of a 2D texture: uncompressed (masks, 8-32 bits), BC1-BC5 and BC7,
   legacy or DX10 header */
int   dds_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);
/* QOI, every op, either colour space */
int   qoi_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);
/* ICO and CUR: the biggest (then deepest) entry, a PNG or a BMP with its AND mask */
int   ico_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);
/* Radiance RGBE (.hdr/.pic), flat or RLE, tone-mapped simply (exposure, Reinhard, sRGB) */
int   hdr_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);
/* FITS: the primary image or first IMAGE extension (a 3-plane cube as RGB), BITPIX 8/16/32/64/-32/-64,
   bottom-up unless ROWORDER says TOP-DOWN; 16-bit integers over 65535, floats normalised as PhotoCraft does */
int   fits_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);
/* DPX: 8/10/12/16-bit RGB(A) or luma, packed or filled, either byte order */
int   dpx_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);

/* shared by tiff.c and psd.c: CIE L*a*b* (D50; L 0-255 for 0-100, a and b offset by 128) to sRGB */
void  rasters_lab(int l, int a, int b, u8 *rgb);

#endif
