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
/* Layered TIFF, as Photoshop writes it: the layers (as PSD keeps them) in tag 37724
   (ImageSourceData) beside the flat picture other programs show. Written with the document's mode
   and compression (tiff_encode_doc) when it has more than one layer; read with them (8 and 16 bits),
   else a TIFF of several pages opens with each page as a layer (the first page's size) */
u8  *tiff_encode_layered(Doc *d, int compression, size_t *len);
Doc *tiff_read_layers(const u8 *d, size_t n, char *err, size_t errcap);
/* a TIFF's pages (IFDs), at most max, their offsets in ifd; how many */
int  tiff_pages(const u8 *d, size_t n, u32 *ifd, int max);
/* a new first IFD for a TIFF with a tag added (type, count, data stored at the end): takes the file and
   returns it grown (or unchanged when it can't); NULL if out of memory */
u8  *tiff_add_tag(u8 *t, size_t *len, int tag, int type, const u8 *data, u32 count);

/* camraw.c: camera raw files (DNG, CR2, CR3, NEF, ARW, PEF, ORF, RW2, RAF and the other TIFF-structured
   ones) as the full-size JPEG the camera keeps in them, turned upright; codec_decode tries it first */
int  camraw_detect(const u8 *d, size_t n);
int  camraw_decode(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);

/* JPEG Options' file size: the picture flattened over white, at most JSZ_MAX pixels (a mosaic of
   blocks from all over it when it is bigger), made once for the dialog; then the size a JPEG of the whole picture will be at
   quality q (progressive or not), worked out from that copy's (exact when it is the picture itself) */
#define JSZ_MAX 160000L
int  jpeg_size_thumb(Doc *d, Rgba *out);
long jpeg_size_estimate(const Rgba *thumb, long w, long h, int quality, int progressive);

/* what codec_load_doc adds from a file: its EXIF */
void filesx_load_extras(Doc *doc, const u8 *d, size_t n);

#endif
