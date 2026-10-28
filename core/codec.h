/*
 * codec.h - Easel's picture files: reading and writing PNG, JPEG, BMP, IFF ILBM and GIF (read only)
 * as flat RGBA pictures, and OpenRaster (.ora) as layered documents.
 * Portable C99 (the native tests and vbcc for the Amiga, big-endian: every multi-byte number in a
 * file is read and written a byte at a time).
 *
 * Every decoder treats its input as hostile: each length and offset is checked against the data,
 * so a damaged or truncated file fails cleanly (with a message in err) and is never read past.
 * The code is split by format: codec.c (detection and dispatch), png.c, jpegenc.c, bmp.c, ilbm.c
 * (with the median-cut quantizer), gif.c and ora.c.
 */
#ifndef EASEL_CODEC_H
#define EASEL_CODEC_H

#include "util.h"
#include "doc.h"

/* a flat picture: w * h * 4 bytes of straight (not premultiplied) RGBA, rows top down, malloc'd */
typedef struct { i32 w, h; u8 *px; } Rgba;

/* the formats Easel writes come first (PNG to ORA); the rest are read only */
enum { FMT_UNKNOWN, FMT_PNG, FMT_JPEG, FMT_BMP, FMT_ILBM, FMT_GIF, FMT_ORA, FMT_AVIF,
       /* rasters.h's formats, in its order (FMT_AVIF + RAS_TIFF ...) */
       FMT_TIFF, FMT_PSD, FMT_TGA, FMT_PCX, FMT_PNM, FMT_XPM, FMT_DDS, FMT_QOI, FMT_ICO, FMT_HDR,
       FMT_FITS, FMT_DPX,
       /* amigafmt.h's formats, in its order (FMT_DPX + AMF_WEBP ...) */
       FMT_WEBP, FMT_ACBM, FMT_RGBN, FMT_RGB8, FMT_DEEP, FMT_ILBM_LINEPAL, FMT_ANIM, FMT_INFO,
       FMT_SUNRAS, FMT_SGI, FMT_DEGAS, FMT_NEO, FMT_N };
#define codec_writable(fmt) ((fmt) >= FMT_PNG && (fmt) <= FMT_ORA && (fmt) != FMT_GIF)

/* the largest picture any decoder will make, in pixels (each side is also kept within MAXDIM) */
#define CODEC_MAXPIXELS (64L * 1024 * 1024)

/* ------------------------------------------------------------------ codec.c */
int   rgba_alloc(Rgba *im, i32 w, i32 h);          /* transparent black; 0 if too big or out of memory */
void  rgba_free(Rgba *im);                          /* frees px and leaves the picture empty */
/* the format of a file from its first bytes (and, for a zip, its directory) */
int   codec_detect(const u8 *d, size_t n);
const char *codec_name(int fmt);                    /* "PNG", "JPEG", ... ("unknown") */
/* any flat format (PNG, JPEG, BMP, ILBM, GIF; an ORA gives its merged image) into *out; 1 on
   success, else 0 with a message in err (err may be NULL) */
int   codec_decode(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);
/* the resolution a file records (PNG pHYs, JPEG JFIF, BMP header, ORA xres), dots per inch; 0 if none */
int   codec_dpi(const u8 *d, size_t n);
/* any supported file as a document: an ORA with its layers, a flat picture as one "Background"
   layer. The undo history is empty and the document clean; NULL with a message in err */
Doc  *codec_load_doc(const u8 *d, size_t n, char *err, size_t errcap);
/* a JPEG opened at w x h, smaller than it is (when it won't fit in memory at its own size) */
Doc  *codec_load_doc_scaled(const u8 *d, size_t n, i32 w, i32 h, char *err, size_t errcap);
/* set when the last codec_decode or codec_load_doc failed for want of memory, not a bad file */
extern int codec_oom;
/* the picture over white with its alpha dropped (alpha set to 255), in place */
void  rgba_flatten_white(Rgba *im);
/* a copy shrunk to fit max x max (box-filtered, alpha weighted); a plain copy if it fits already */
int   rgba_thumbnail(const Rgba *im, i32 max, Rgba *out);
void  codec_err(char *err, size_t errcap, const char *msg);   /* copy msg into err, if err is not NULL */

/* ------------------------------------------------------------------ png.c */
/* every colour type, bit depth 1-16 (16 keeps the high byte), interlaced or not; alpha kept */
int   png_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);
/* 8-bit RGBA, or RGB when every pixel is opaque; a pHYs chunk when dpi > 0. Returns the file,
   malloc'd (*len its size); NULL if out of memory */
u8   *png_encode(const Rgba *im, int dpi, size_t *len);
/* zlib (RFC 1950) around zip.c's raw deflate and inflate: compress is malloc'd; decompress
   writes exactly outcap bytes or fails (-1) */
u8   *zlib_compress(const u8 *in, size_t n, size_t *outlen);
long  zlib_decompress(const u8 *in, size_t n, u8 *out, size_t outcap);
u32   adler32(u32 a, const u8 *p, size_t n);       /* start with 1 */

/* ------------------------------------------------------------------ jpegenc.c (decoding: image.c) */
/* baseline and progressive JPEG through image.c (at most 8000 pixels a side); alpha 255 */
int   jpeg_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);
/* the same at w x h (no bigger than it is), decoded and shrunk in one pass */
int   jpeg_read_scaled(const u8 *d, size_t n, i32 w, i32 h, Rgba *out, char *err, size_t errcap);
/* baseline JPEG, YCbCr 4:2:0 (4:4:4 at quality 90 and above), the Annex K tables scaled for quality
   1-100 as IJG does, JFIF density from dpi (0: aspect only). Transparency is laid over white. */
u8   *jpeg_encode(const Rgba *im, int quality, int dpi, size_t *len);

/* ------------------------------------------------------------------ bmp.c */
/* 1/4/8-bit palette (uncompressed, RLE4, RLE8), 16/24/32-bit (BI_RGB or bitfields), either row order */
int   bmp_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);
/* 24-bit if every pixel is opaque, else 32-bit BGRA with a BITMAPV4HEADER saying so */
u8   *bmp_encode(const Rgba *im, size_t *len);

/* ------------------------------------------------------------------ ilbm.c */
/* IFF ILBM (1-8 planes, EHB, HAM6, HAM8, 24- and 32-bit deep; ByteRun1 or uncompressed; a mask plane
   skipped, a transparent colour made transparent) and PBM (Deluxe Paint's chunky form) */
int   ilbm_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);
/* ILBM, ByteRun1 compressed, laid over white: 24 planes, or (indexed256) 8 planes with a 256-colour
   CMAP - exact when there are at most 256 colours, median cut otherwise (Floyd-Steinberg if dither) */
u8   *ilbm_encode(const Rgba *im, int indexed256, int dither, size_t *len);
/* reduce opaque RGB to at most maxc colours: pal gets them (3 bytes each), idx (w * h, may be NULL
   to just get the palette) each pixel's entry. Exact when the picture has maxc colours or fewer.
   Returns the number of colours, 0 if out of memory */
int   quantize(const Rgba *im, int maxc, int dither, u8 *pal, u8 *idx);

/* ------------------------------------------------------------------ gif.c */
/* the first frame of a GIF, on its logical screen (outside the frame transparent), transparency kept */
int   gif_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);

/* ------------------------------------------------------------------ ora.c */
/* an OpenRaster file as a document (see ora.c for what is kept); NULL with a message in err */
Doc  *ora_read(const u8 *d, size_t n, char *err, size_t errcap);
/* the document as OpenRaster: each layer cropped to what it shows, plus the merged image and a
   thumbnail. Malloc'd (*len its size); NULL if out of memory */
u8   *ora_write(Doc *doc, size_t *len);
/* an OpenRaster composite-op for a BM_ mode and back (unknown names give BM_NORMAL) */
const char *ora_op_name(int mode);
int   ora_op_mode(const char *name);

#endif
