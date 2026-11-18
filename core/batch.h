/*
 * batch.h - converting picture files, for the EaselConvert Shell command (amiga/convert.c) and the
 * native tests: any picture Easel reads, optionally resized, written as PNG, JPEG, BMP, IFF ILBM
 * (24-bit or a number of colours), GIF or OpenRaster, optionally with a Workbench icon beside it;
 * 1.1: also TIFF, Targa, PSD and every xwrite.h format (WebP, PDF, Netpbm, QOI, ICO, DDS, DPX, FITS).
 * Portable C99.
 */
#ifndef EASEL_BATCH_H
#define EASEL_BATCH_H

#include "codec.h"

typedef struct {
  int format;                         /* FMT_PNG, FMT_JPEG, FMT_BMP, FMT_ILBM, FMT_GIF or FMT_ORA;
                                         FMT_UNKNOWN: from the output name's extension (PNG if none) */
  i32 width, height;                  /* the new size: both, or one (the other in proportion); 0 0 keeps it */
  int scale;                          /* per cent (used when width and height are 0); 0 keeps the size */
  int quality;                        /* JPEG 1-100 (0: 85) */
  int colors;                         /* ILBM and GIF: 2-256 colours (ILBM 0: 24-bit, or an indexed
                                         picture's own palette; GIF 0: 256) */
  int dither;                         /* Floyd-Steinberg when reducing the colours */
  int icon;                           /* 0 none, else iconwrite.h's ICON_ kind + 1 */
  const char *deftool;                /* the icon's default tool (NULL: none) */
  /* a last resort for a file Easel's decoders don't know (the Amiga's datatypes); NULL: none */
  Doc *(*fallback)(const char *path, char *err, size_t errcap);
  int variant;                        /* 1.1: xwrite_doc's sub (PNM kind, DDS kind, DPX 10 bits, ICO size);
                                         0 with the format from the name: what its extension asks */
} BatchOpts;
void batch_opts_default(BatchOpts *o);

/* the FMT_ for a format name (PNG, JPEG or JPG, BMP, ILBM, IFF or LBM, GIF, ORA; 1.1: TIFF, TGA or
   TARGA, PSD, WEBP, PDF, PNM/PBM/PGM/PPM/PAM or NETPBM, QOI, ICO or ICON, DDS, DPX, FITS; any case)
   or a file name's extension; FMT_UNKNOWN if none (FMT_PDF_OUT for PDF, see xwrite.h) */
int  batch_format_named(const char *name);
int  batch_format_of_path(const char *path);
const char *batch_ext(int fmt);                     /* ".png" ... */

/* a picture file in memory as a document: an ILBM of up to 8 planes opens indexed (its palette
   kept), anything else as codec_load_doc does. NULL with a message in err */
Doc *batch_load(const u8 *data, size_t len, char *err, size_t errcap);
/* the document resized as o asks (no change if it asks none); 0 if out of memory */
int  batch_resize(Doc *d, const BatchOpts *o);
/* the document as a file in o->format (which must be set), malloc'd; NULL with a message in err */
u8  *batch_encode(Doc *d, const BatchOpts *o, size_t *len, char *err, size_t errcap);
/* the whole conversion, file to file: in read, resized, written to out (o->format, or from out's
   extension), and the icon written to out.info when asked. 1 done, else 0 with a message in err */
int  batch_convert(const char *in, const char *out, const BatchOpts *o, char *err, size_t errcap);

#endif
