/*
 * avif.h - AVIF pictures (AV1 in HEIF), read through dav1d: 8, 10 and 12 bits, 4:2:0, 4:2:2, 4:4:4
 * and monochrome, grids, alpha, and the clean aperture, rotation and mirror properties
 */
#ifndef EASEL_AVIF_H
#define EASEL_AVIF_H
#include "codec.h"

int avif_detect(const u8 *d, size_t n);            /* an ftyp naming avif or avis */
int avif_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap);

#endif
