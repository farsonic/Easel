/*
 * modex.h - Easel round 3: more of Image > Mode (modex.c).
 *
 * Indexed Color's Matte (as Photoshop's): the colour see-through pixels are laid over before the
 * palette is made. With Transparency kept, only the partly see-through ones (anti-aliased edges) are
 * matted and made opaque, and the fully see-through ones stay so; without it every pixel is laid over
 * the matte. Each pixel layer is matted (call inside the conversion's undo step, after
 * doc_note_all).
 */
#ifndef EASEL_MODEX_H
#define EASEL_MODEX_H
#include "doc.h"

enum { MT_NONE, MT_FG, MT_BG, MT_WHITE, MT_BLACK, MT_GRAY50, MT_NETSCAPE, MT_N };
extern const char *const matte_names[MT_N + 1];          /* "None", "Foreground Color" ... */
u32  matte_color(const Doc *d, int matte);               /* 0xRRGGBB for an MT_ (not MT_NONE) */
/* the pixel layers matted; how many pixels changed */
long layers_matte(Doc *d, u32 rgb, int keep_transparency);
#endif
