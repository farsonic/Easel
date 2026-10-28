/*
 * iconwrite.h - Workbench icons (.info files) for pictures: a thumbnail of the picture as a
 * classic icon (planar, the 2.x four colours or MagicWB's eight), a NewIcon (the image coded into
 * the tool types, on top of a classic image), an OS 3.5 GlowIcon (FORM ICON with IMAG chunks, up
 * to 256 colours, run-length packed, on top of a classic image), or a PNG icon (OS4, AROS and
 * MorphOS take a plain PNG file named .info).
 * Portable C99 (the native tests and vbcc for the Amiga); every number is written a byte at a
 * time, big-endian, as the Amiga keeps it. amigafmt.c's info_read reads all four back.
 *
 * Written from the specifications: the DiskObject, Gadget and Image layouts (RKM Libraries,
 * workbench/workbench.h, intuition/intuition.h), the NewIcons format notes (Nicola Salmoria's,
 * as amigafmt.c's reader decodes them) and the OS 3.5 icon FORM (FACE and IMAG chunks).
 */
#ifndef EASEL_ICONWRITE_H
#define EASEL_ICONWRITE_H

#include "codec.h"

enum { ICON_CLASSIC, ICON_NEWICONS, ICON_GLOW, ICON_PNG, ICON_N };
extern const char *const icon_kind_names[ICON_N + 1];   /* NULL ended, for a cycle gadget */

/* a classic image's colours: the Workbench 2.x four (grey, black, white, blue), or MagicWB's
   eight (those four and dark grey, light grey, beige, pink: the 12-bit set, as Workbench screens
   with MagicWB have them) */
enum { ICONPAL_WB20, ICONPAL_MWB };

/* default tools for a picture's icon */
#define ICON_TOOL_EASEL  "Easel"
#define ICON_TOOL_VIEWER "SYS:Utilities/MultiView"

typedef struct {
  int kind;                           /* ICON_ */
  int size;                           /* the thumbnail's longest side, 8-256 (0: 64) */
  int classic_pal;                    /* ICONPAL_: the classic image (ICONPAL_WB20 under NewIcons and GlowIcons) */
  int colors;                         /* NewIcons and GlowIcons: at most this many, 2-256 (0: 256) */
  int dither;                         /* palette.h's DI_ for the reduced images (DI_FS by default) */
  int selected;                       /* also a selected image (the picture darkened); classic icons
                                         are complemented by Workbench instead */
  const char *deftool;                /* the default tool (NULL or "": none) */
} IconOpts;
void icon_opts_default(IconOpts *o, int kind);

/* a picture's icon: pic is shrunk to the thumbnail size (a smaller one is used as it is); its
   transparency is kept (the classic image's colour 0, NewIcons' colour 0, the GlowIcon's
   transparent colour, PNG's alpha). Malloc'd (*len its size); NULL with a message in err */
u8 *icon_write_ex(const Rgba *pic, const IconOpts *o, size_t *len, char *err, size_t errcap);
/* the same with the defaults for kind and a default tool (NULL: none) */
u8 *icon_write(const Rgba *pic, int kind, const char *deftool, size_t *len);

/* the classic palette writers use (n entries, 3 bytes each), for showing or testing */
int icon_classic_palette(int which, u8 *pal);

#endif
