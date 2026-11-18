/*
 * xwrite.h - more picture writers (Easel 1.1): WebP (lossless), PDF (one page), Netpbm (PBM, PGM,
 * PPM and PAM), QOI, Windows icons, DDS textures (uncompressed, BC1 or BC3, with mipmaps), DPX and
 * FITS; and the table the Save, Export and EaselConvert code uses to find a writer for a format.
 * Portable C99; every multi-byte number is written a byte at a time.
 *
 * Each encoder takes a flat RGBA picture (codec.h's Rgba) and returns the file malloc'd, with its
 * size in *len; NULL if out of memory or the picture is too big for the format.
 */
#ifndef EASEL_XWRITE_H
#define EASEL_XWRITE_H

#include "codec.h"

/* a format Easel writes but codec.h's FMT_ table doesn't have (it reads no PDF) */
#define FMT_PDF_OUT (FMT_N + 1)

/* WebP lossless (VP8L): the subtract-green and predictor transforms, LZ77 with a hash chain,
   length-limited Huffman codes. Alpha is kept (and flagged) when anything isn't opaque */
u8 *webp_encode_lossless(const Rgba *im, size_t *len);
/* 1.2: WebP lossy (VP8 key frame: 16x16 and chroma intra prediction, default probabilities, no loop
   filter) at quality 1-100, alpha (when anything isn't opaque) in an uncompressed ALPH chunk */
u8 *webp_encode_lossy(const Rgba *im, int quality, size_t *len);

/* PDF 1.4, one page the picture's size at dpi (0: 72), the picture as one image: Flate-compressed
   RGB (grey when gray is set) with a soft mask for transparency, or (jpeg_quality 1-100) a JPEG
   laid over white. info (may be NULL) goes into the document information dictionary */
u8 *pdf_encode(const Rgba *im, int dpi, int gray, int jpeg_quality, const DocInfo *info, size_t *len);

/* Netpbm: kind PNM_ */
enum { PNM_AUTO, PNM_PBM, PNM_PGM, PNM_PPM, PNM_PAM };
/* PNM_AUTO: PAM when anything isn't opaque (grey and alpha when every pixel is grey), else PGM when
   every pixel is grey, else PPM. PBM: black where the pixel is darker than half */
u8 *pnm_encode(const Rgba *im, int kind, size_t *len);

/* QOI (the Quite OK Image format): RGB, or RGBA when anything isn't opaque */
u8 *qoi_encode(const Rgba *im, size_t *len);

/* a Windows icon holding the picture at each of the standard sizes (16, 24, 32, 48, 64, 128 and
   256 a side) up to its own larger side (it fitted and centred on a square), or (maxsize > 0) only
   up to maxsize; sizes up to 64 as 32-bit DIBs with an AND mask, larger ones as PNG */
u8 *ico_encode(const Rgba *im, int maxsize, size_t *len);

/* DDS: kind DDS_; mipmaps 1 for the whole chain down to 1 x 1 */
enum { DDS_RGBA, DDS_BC1, DDS_BC3 };
u8 *dds_encode(const Rgba *im, int kind, int mipmaps, size_t *len);

/* DPX (SMPTE 268M): RGB, or RGBA when anything isn't opaque, at 8 bits or (bits10 1) 10 bits packed
   as Method A; 1.2: bits10 12 for 12 bits (Method A, one to a 16-bit word) or 16 for 16 bits; big-endian */
u8 *dpx_encode(const Rgba *im, int bits10, size_t *len);

/* FITS: 8-bit (BITPIX 8), one plane when every pixel is grey, else three (NAXIS3 = 3); the bottom
   row first, as FITS keeps them */
u8 *fits_encode(const Rgba *im, size_t *len);

/* the quality (1-100, as IJG and Easel scale it) a JPEG was saved at, estimated from its luminance
   quantization table; 0 if it isn't a JPEG or has none (so Save can keep an opened JPEG's quality) */
int jpeg_estimate_quality(const u8 *d, size_t n);

/* ------------------------------------------------------------------ the table */
/* 1 if xwrite writes fmt (FMT_WEBP, FMT_PNM, FMT_QOI, FMT_ICO, FMT_DDS, FMT_DPX, FMT_FITS, FMT_PDF_OUT) */
int  xwrite_handles(int fmt);
/* every format Easel writes, by file name extension (".webp", ".pdf", ".ppm" ...; any case;
   codec.h's writers too); FMT_UNKNOWN if none */
int  xwrite_format_of_ext(const char *ext);
/* the usual extension for a format Easel writes (".png" ...); "" if none */
const char *xwrite_ext(int fmt);
/* the format's name for messages and the Shell ("WebP", "PDF" ...) */
const char *xwrite_name(int fmt);
/* the variant an extension asks for (".pbm" PNM_PBM ...; DDS_BC3 for ".dds"); 0 if none */
int  xwrite_sub_of_ext(const char *ext);
/* a document written as one of xwrite's formats, from its composite: PNM and PDF follow its mode
   (Bitmap as PBM, Grayscale as a grey PDF); its dpi and File Info where the format keeps them.
   sub: PNM's PNM_ kind, DDS's DDS_ kind (with mipmaps), DPX 1 for 10 bits, ICO the largest size
   (0: 256); 1.2: WebP 1-100 for lossy at that quality (0 lossless). quality: JPEG inside the PDF (0: Flate) */
u8  *xwrite_doc(Doc *d, int fmt, int sub, int quality, size_t *len);

#endif
