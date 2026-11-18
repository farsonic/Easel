/*
 * foreign.h - Easel round 3: other programs' layered documents, opened with their layers (read
 * only; Save offers OpenRaster or PSD, as for any layered picture):
 *
 *   xcf.c  GIMP .xcf (and .xcf.gz): versions 0-25, RGB, grey and indexed, every precision (8/16/32-bit
 *          integer, half, float and double, linear or perceptual: made 8-bit sRGB), tiles raw, RLE or
 *          zlib; layers with their offsets, names, opacity, visibility, blend modes (GIMP's 2.10 and
 *          legacy sets mapped to Easel's), locks, colour tags, layer masks, nested groups (pass
 *          through), extra channels as alpha channels and the selection; resolution and the
 *          icc-profile parasite.
 *   kra.c  Krita .kra (and .krz): maindoc.xml's layer tree (paint layers and groups, nested, names,
 *          offsets, opacity, visibility, blend modes, locks, collapsed, pass-through), the tiles (LZF
 *          or raw, byte-planar BGRA, 8 or 16 bits, RGBA and grey), the first transparency mask as
 *          the layer mask, the default pixel, the resolution; any other colour space opens from
 *          mergedimage.png.
 *   pdn.c  Paint.NET .pdn (PDN3, Paint.NET 3 to 5): the .NET binary serialization (MS-NRBF) read
 *          without running anything, the layers' names, visibility, opacity, blend modes (Paint.NET's
 *          own included) and BGRA pixels (gzip or raw chunks).
 *
 * Every length, offset and count is checked against the file; a damaged file fails with a message.
 */
#ifndef EASEL_FOREIGN_H
#define EASEL_FOREIGN_H
#include "codec.h"

enum { FF_NONE, FF_XCF, FF_KRA, FF_PDN };
int  foreign_detect(const u8 *d, size_t n);                  /* FF_ (an .xcf.gz counts as FF_XCF) */
const char *foreign_name(int kind);                          /* "GIMP XCF", "Krita", "Paint.NET" */
/* the document (undo empty, clean); NULL with a message in err */
Doc *foreign_load_doc(const u8 *d, size_t n, char *err, size_t errcap);

Doc *xcf_read_doc(const u8 *d, size_t n, char *err, size_t errcap);
Doc *kra_read_doc(const u8 *d, size_t n, char *err, size_t errcap);
Doc *pdn_read_doc(const u8 *d, size_t n, char *err, size_t errcap);

/* helpers the readers share (foreign.c) */
/* a gzip file's contents (one member), malloc'd (*len its size); NULL if it isn't one, is damaged or
   would inflate past max bytes */
u8  *gunzip(const u8 *d, size_t n, size_t max, size_t *len);
/* liblzf's format into exactly outcap bytes; 1 if it fits exactly */
int  lzf_decompress(const u8 *in, size_t n, u8 *out, size_t outcap);
/* a linear-light value 0..1 as 8-bit sRGB, and a sRGB-encoded one as 8 bits (both clamped) */
u8   lin_to_srgb8(double v);
u8   unit_to8(double v);
/* an IEEE half (its 16 bits) as a double */
double half_to_double(unsigned h);
/* depths made consistent and the selection's box worked out once the layers are in: what each reader
   does before handing a document back */
void foreign_finish(Doc *doc);
#endif
