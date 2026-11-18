/*
 * loadx.h - Easel 1.2: what codec_load_doc adds to a document it has read (loadx.c): the file's ICC
 * profile and colour model, palette PNG and BMP files as Indexed documents, 1-bit TIFF and PBM
 * files in Bitmap mode.
 */
#ifndef EASEL_LOADX_H
#define EASEL_LOADX_H
#include "doc.h"
void codec_load_extras(Doc *doc, const u8 *d, size_t n);
/* around reading a CMYK file: its ink turned to RGB through its own CMYK profile, or Easel's press
   (cms.h's cmyk_hook); begin returns 1 if it set the hook (then call end) */
int  codec_cmyk_begin(const u8 *d, size_t n);
void codec_cmyk_end(void);
#endif
