/*
 * picinfo.h - Easel 1.2: a picture file described in words, for EaselConvert's INFO switch (as
 * photocraft-cli's info command): its format, size, layers, colour mode, resolution, pixel aspect,
 * Amiga screen mode and File Info. Portable C99.
 */
#ifndef EASEL_PICINFO_H
#define EASEL_PICINFO_H
#include "codec.h"
/* the file in memory described, one "Key: value" a line (malloc'd), or NULL with a message in err */
char *picture_info(const u8 *data, size_t len, char *err, size_t errcap);
#endif
