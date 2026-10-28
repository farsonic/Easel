/*
 * dtload.h - opening a picture through the Amiga's datatypes (datatypes.library and the
 * picture.datatype class), for files Easel's own decoders don't know: whatever datatypes the
 * system has installed (PCX, Targa, PhotoCD, ...). Opens and closes datatypes.library itself.
 *
 * With picture.datatype v43 or later the picture comes as 24-bit RGBA (PDTM_READPIXELARRAY),
 * alpha kept when the datatype says it has some; with an older one (OS 3.0 and 3.1) the picture's
 * own bitmap (up to 8 planes, HAM and Extra Half-Brite decoded) and colour map are read.
 *
 * Use it last, when codec_load_doc fails for a file that isn't damaged, just unknown.
 */
#ifndef EASEL_DTLOAD_H
#define EASEL_DTLOAD_H

#include <stddef.h>
#include "../core/doc.h"

/* the picture at path (an AmigaDOS path) as a one-layer document; NULL with a message in err */
Doc *dt_load(const char *path, char *err, size_t errcap);

#endif
