/*
 * filesx.h - Easel round 3: more of what files keep and how they are opened (filesx.c).
 *
 * EXIF: the block an opened JPEG (APP1), PNG (eXIf) or WebP (EXIF chunk) has is kept with the
 * document (Doc.exif) and written back into JPEG and PNG files. Since Easel turns pictures upright
 * when it opens them, the kept block says orientation 1; its pixel dimensions are set to the
 * picture's when it is written, and its thumbnail (IFD1, which would show the old picture) is dropped.
 */
#ifndef EASEL_FILESX_H
#define EASEL_FILESX_H
#include "codec.h"

/* the EXIF block (TIFF structure) in a JPEG, PNG or WebP file, malloc'd; NULL if none */
u8  *exif_from_file(const u8 *d, size_t n, u32 *len);
/* an EXIF block made fit to keep: orientation 1, no thumbnail IFD. 0 if it isn't a valid block */
int  exif_normalize(u8 *e, u32 len);
/* a tag's first value (SHORT or LONG; in IFD0 or the EXIF IFD); 1 if found */
int  exif_get(const u8 *e, u32 len, int tag, long *val);
/* the document's EXIF (with its size) added to an encoded JPEG or PNG (other formats unchanged):
   takes the file and returns it grown (or as it was); NULL if out of memory */
u8  *exif_embed(const Doc *d, int fmt, u8 *file, size_t *len);
/* what codec_load_doc adds from a file: its EXIF */
void filesx_load_extras(Doc *doc, const u8 *d, size_t n);

#endif
