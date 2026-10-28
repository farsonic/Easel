/*
 * clipboard.h - the Amiga clipboard (clipboard.device unit 0, through iffparse.library): pictures
 * in and out as IFF ILBM, text in and out as IFF FTXT (CHRS chunks, Latin-1). Each function opens
 * and closes iffparse.library itself, so the caller needs nothing set up; each fails cleanly (0 or
 * NULL) when the library, the device or the memory isn't there.
 *
 * What goes out when a picture is copied (CLIP_ILBM_AUTO, the default): a picture of at most 256
 * colours (as laid over white) goes as an exact 1-8 plane ILBM with its CMAP, which every Amiga
 * program reads; a picture with more colours goes as a 24-plane deep ILBM, which keeps it whole
 * and which the programs that work in true colour (PPaint, ImageFX, Photogenics, ADPro, picture
 * .datatype v43 and so MultiView) read. Quantizing to 256 colours without asking would quietly
 * spoil the picture for those, so that is left as a choice (CLIP_ILBM_8) for pasting into
 * DPaint-era programs. Transparent pixels go as a mask plane.
 */
#ifndef EASEL_CLIPBOARD_H
#define EASEL_CLIPBOARD_H

#include <stddef.h>
#include "../core/codec.h"

enum { CLIP_ILBM_AUTO, CLIP_ILBM_24, CLIP_ILBM_8 };

/* a picture copied out (CLIP_ILBM_AUTO); 1 done */
int   clipboard_put_ilbm(const Rgba *im);
/* the same with a choice of CLIP_ILBM_; pal/idx (npal entries; may be NULL) are an indexed
   picture's own palette and indices, used exactly (whatever mode) */
int   clipboard_put_ilbm_ex(const Rgba *im, const u8 *pal, int npal, const u8 *idx, int mode);
/* 1 if the clipboard holds a picture (a FORM ILBM, or one inside a CAT or LIST) */
int   clipboard_has_ilbm(void);
/* the clipboard's picture as a document (an ILBM of up to 8 planes indexed, with its palette);
   NULL with a message in err */
Doc  *clipboard_get_doc(char *err, size_t errcap);
/* text out as FTXT: UTF-8 made Latin-1 (what has no Latin-1 stand-in becomes '?'); 1 done */
int   clipboard_put_text(const char *utf8);
/* the clipboard's text (every CHRS chunk of its FTXT) as UTF-8, malloc'd; NULL if none */
char *clipboard_get_text(void);

#endif
