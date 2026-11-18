/*
 * colorx.h - Easel 1.2: colour management on documents (cms.h does the colours): Edit > Assign
 * Profile and Convert to Profile, Image > Mode > CMYK Color and Lab Color, writing a document in
 * its colour model (CMYK and Lab TIFF, CMYK JPEG) and with its profile embedded, and an 8-bit
 * palette BMP for Indexed documents. Portable C99; each command is one undo step.
 */
#ifndef EASEL_COLORX_H
#define EASEL_COLORX_H

#include "doc.h"
#include "cms.h"

/* the document's profile parsed (NULL when it has none, or it can't be parsed: sRGB); icc_free it */
IccProfile *colorx_doc_profile(const Doc *d);
/* which profile: none (untagged), a built-in one, or bytes given */
enum { CX_NONE = -1, CX_SRGB = CMS_SRGB, CX_ADOBERGB = CMS_ADOBERGB, CX_BYTES = 2 };
/* the profile given, the colour numbers kept ("Assign Profile"). 0 if bytes aren't an RGB or grey
   profile matching the document, or for want of memory */
int  colorx_assign(Doc *d, int which, const u8 *bytes, u32 len);
/* every layer's colours (pixels, fill layers' colours, the palette) converted from the document's
   profile into the one given, which it then has ("Convert to Profile") */
int  colorx_convert(Doc *d, int which, const u8 *bytes, u32 len, int intent, int bpc);
/* Image > Mode > RGB/CMYK/Lab Color (CM_): a CMYK document has every colour brought into the
   press's (or cmyk profile's) gamut. Indexed, Grayscale and Bitmap documents are made RGB first */
int  colorx_convert_model(Doc *d, int cm, const IccProfile *cmyk);
/* the document written in its model when the format can hold it: a CMYK document as a CMYK TIFF
   or JPEG, a Lab one as a CIELab TIFF (with alpha when anything isn't opaque); NULL when the
   document is RGB or the format can't (then write it as RGB) */
u8  *colorx_write_model(Doc *d, int fmt, int quality, const IccProfile *cmyk, size_t *len);
/* the document's profile put into a file just written (as RGB) when the file can hold one and the
   profile fits the file's colours; takes the file, returns it (NULL if out of memory) */
u8  *colorx_embed(const Doc *d, int fmt, u8 *data, size_t *len);
/* everything together, for the tests and EaselConvert: PNG, JPEG (quality 90), TIFF (LZW), PSD,
   WebP (lossless) */
u8  *colorx_write_doc(Doc *d, int fmt, size_t *len);
/* an Indexed (or Bitmap) document as an 8-bit (4- or 1-bit when the palette is small enough)
   palette BMP, uncompressed; anything else as bmp_encode writes it */
u8  *colorx_bmp_indexed(Doc *d, size_t *len);

#endif
