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
/* 1.2: CMYK (w * h * 4 bytes of ink, 0 none) as Photoshop writes it: Adobe APP14, no transform,
   inverted samples, full resolution */
u8   *jpeg_encode_cmyk(const u8 *cmyk, i32 w, i32 h, int quality, int dpi, size_t *len);

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

/* ------------------------------------------------------------------ 0.4: indexed writers */
/* png.c: colour type 3 at the fewest bits (1/2/4/8) for npal entries, with PLTE and (ntrns > 0) a
   tRNS of the first ntrns entries' alpha; and 8-bit grey (grey and alpha if anything isn't opaque) */
u8   *png_encode_indexed(const u8 *idx, i32 w, i32 h, const u8 *pal, int npal, const u8 *trns, int ntrns, int dpi, size_t *len);
u8   *png_encode_gray(const Rgba *im, int dpi, size_t *len);
/* gifenc.c: GIF89a of indices with a global palette (npal entries, padded to a power of two),
   transparent the index made transparent (-1 none), interlace 1 for the 4-pass order */
u8   *gif_encode(const u8 *idx, i32 w, i32 h, const u8 *pal, int npal, int transparent, int interlace, size_t *len);
/* any picture quantized (palette.h's QM_/DI_) to maxc colours, one more for transparency when
   any alpha is under 128 */
u8   *gif_encode_rgba(const Rgba *im, int maxc, int method, int dither, int interlace, size_t *len);
/* a document: an indexed one's exact palette and indices, anything else quantized as above */
u8   *gif_write_doc(Doc *d, int maxc, int method, int dither, int interlace, size_t *len);

/* ilbm.c: the ILBM writer's choices */
enum { IK_NORMAL, IK_EHB, IK_HAM6, IK_HAM8, IK_DEEP24, IK_SHAM, IK_N };
enum { IMK_NONE, IMK_PLANE, IMK_TRANSPARENT };
typedef struct {
  int kind;                           /* IK_ */
  int planes;                         /* IK_NORMAL: 1-8 (0: the fewest that hold the palette) */
  int mask;                           /* IMK_: a mask plane from alpha, or a transparent colour */
  int transparent;                    /* IMK_TRANSPARENT's entry (-1: an unused one, or one added) */
  int cycles;                         /* write the document's cycle ranges as CRNG */
  int method, dither;                 /* palette.h's QM_ and DI_ when it has to quantize */
  int compress;                       /* 1 ByteRun1, 0 none */
  u32 modeid;                         /* CAMG: 0 for the document's, or one chosen from the size */
  u8 aspect_x, aspect_y;              /* BMHD: 0 for the document's */
} IlbmOpts;
void  ilbm_opts_default(IlbmOpts *o);
/* any picture as an ILBM: pal/idx (npal entries) are used exactly when given (IK_NORMAL and IK_EHB),
   else the picture is quantized; cyc/ncyc the CRNG ranges. Malloc'd, NULL with a message in err */
u8   *ilbm_write(const Rgba *im, const u8 *pal, int npal, const u8 *idx, const CycleRange *cyc, int ncyc,
                 const IlbmOpts *o, size_t *len, char *err, size_t errcap);
/* a document: an indexed one's palette and indices kept exactly; its ModeID, aspect and cycles */
u8   *ilbm_write_doc(Doc *d, const IlbmOpts *o, size_t *len, char *err, size_t errcap);
/* an ILBM, PBM or ACBM of 1-8 planes without HAM as an indexed document: CMAP, indices, the CRNG,
   CCRT and DRNG ranges, CAMG and aspect kept; EHB gets a 64-entry palette; a mask plane or the
   transparent colour becomes alpha. NULL with a message in err */
Doc  *ilbm_read_indexed(const u8 *d, size_t n, char *err, size_t errcap);
/* 1 if ilbm_read_indexed can take the file (what the UI asks before opening one as indexed) */
int   ilbm_indexable(const u8 *d, size_t n);

/* ------------------------------------------------------------------ ora.c */
/* an OpenRaster file as a document (see ora.c for what is kept); NULL with a message in err */
Doc  *ora_read(const u8 *d, size_t n, char *err, size_t errcap);
/* the document as OpenRaster: each layer cropped to what it shows, plus the merged image and a
   thumbnail. Malloc'd (*len its size); NULL if out of memory */
u8   *ora_write(Doc *doc, size_t *len);
/* an OpenRaster composite-op for a BM_ mode and back (unknown names give BM_NORMAL) */
const char *ora_op_name(int mode);
int   ora_op_mode(const char *name);

/* ------------------------------------------------------------------ 0.6: exchange */
/* the formats Easel 0.6 writes (codec_writable is kept as it was, for the 0.5 UI) */
#define codec_writable_06(fmt) (codec_writable(fmt) || (fmt) == FMT_GIF || (fmt) == FMT_TIFF || (fmt) == FMT_PSD || (fmt) == FMT_TGA)

/* tiffw.c: TIFF, one image in strips, big-endian. Compression TIFFC_ (LZW and Deflate with the
   horizontal predictor on 8-bit RGB and grey) */
enum { TIFFC_NONE, TIFFC_PACKBITS, TIFFC_LZW, TIFFC_DEFLATE, TIFFC_N };
extern const char *const tiff_comp_names[TIFFC_N + 1];       /* "None", "PackBits", "LZW", "Deflate", NULL */
/* RGB, or RGBA (unassociated alpha) when anything isn't opaque; dpi 0 for none. Malloc'd, NULL if
   out of memory */
u8   *tiff_encode(const Rgba *im, int compression, int dpi, size_t *len);
/* a document by its mode: RGB(A); Grayscale as grey (with alpha); Indexed as a palette TIFF (RGBA
   when anything is transparent); Bitmap as 1-bit. Its dpi and File Info */
u8   *tiff_encode_doc(Doc *d, int compression, size_t *len);

/* tgaw.c: Targa, top-down rows and the TGA 2.0 footer, raw or RLE (rle 1) */
u8   *tga_encode(const Rgba *im, int rle, size_t *len);          /* 24-bit, 32 when anything isn't opaque */
/* 8-bit indices into an npal-entry map (24-bit; 32-bit with entry transparent clear, -1 none) */
u8   *tga_encode_indexed(const u8 *idx, i32 w, i32 h, const u8 *pal, int npal, int transparent, int rle, size_t *len);
/* a document: Indexed and Bitmap colour-mapped, Grayscale 8-bit grey (32-bit colour with alpha), RGB true colour */
u8   *tga_write_doc(Doc *d, int rle, size_t *len);

/* psd.c: a Photoshop file as a layered document (8- and 16-bit RGB and grey; names, opacity, blend
   modes, visibility, clipping, fill opacity, locks, Blend If, layer masks with density and
   feather, groups with pass-through); any other kind, or one without layers, opens from its
   composite as one layer. NULL with a message in err */
Doc  *psd_read_doc(const u8 *d, size_t n, char *err, size_t errcap);
/* the document as an 8-bit RGB PSD with its layers (fill layers as their pixels, adjustment layers
   as empty pixel layers), groups, masks, resolution, File Info (XMP) and the merged composite */
u8   *psd_write_doc(Doc *d, size_t *len);

/* jpegenc.c and docinfo.c: the EXIF orientation (1-8; 1 if none), which jpeg_read applies, and the
   transformation that makes a picture with orientation o upright (5-8 swap w and h) */
int   jpeg_orientation(const u8 *d, size_t n);
void  rgba_orient(Rgba *im, int o);

/* docinfo.c: File Info (doc.h's DocInfo) */
void  docinfo_clear(DocInfo *f);
int   docinfo_empty(const DocInfo *f);
/* n bytes of UTF-8 into one of the fields (cap its size), cut between characters */
void  docinfo_set(char *dst, size_t cap, const char *s, size_t n);
/* the File Info a file holds (PNG, JPEG, TIFF, PSD, ORA); 1 if it had any. codec_load_doc fills
   the document's with it */
int   codec_read_info(const u8 *d, size_t n, DocInfo *f);
/* an XMP packet of the six fields appended to b, and one parsed (fields it has replace f's) */
void  xmp_write(Buf *b, const DocInfo *f);
int   xmp_parse(const char *s, size_t n, DocInfo *f);
/* an encoded PNG or JPEG given the File Info (PNG tEXt/iTXt and XMP; JPEG XMP and a comment):
   takes the file and returns it grown (or as it was when f is empty); NULL if out of memory */
u8   *png_add_info(u8 *png, size_t *len, const DocInfo *f);
u8   *jpeg_add_info(u8 *jpg, size_t *len, const DocInfo *f);
/* the document as PNG (in its mode, as doc_png_encode) and as JPEG (quality 1-100), with its dpi
   and File Info */
u8   *png_write_doc(Doc *d, size_t *len);
u8   *jpeg_write_doc(Doc *d, int quality, size_t *len);
/* ora.c's stack.xml gets the File Info as easel: attributes of <image> */
void  ora_put_info(Buf *xml, const DocInfo *f);

#endif
