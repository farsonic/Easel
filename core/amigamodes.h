/*
 * amigamodes.h - Easel 0.4: the classic Amiga display modes (OCS, ECS and AGA) as a table, with
 * their ModeIDs (graphics/modeid.h), colour limits, sizes, overscan presets and pixel aspects, and
 * helpers that suggest a mode for a document, check a document against a mode, and give a mode's
 * aspect. Portable C99 (no Amiga headers: the ModeIDs are written out).
 *
 * Aspects: ax:ay is what goes in an ILBM's BMHD x/yAspect (pixel width : height). The lores and
 * hires figures are the ones the EA ILBM specification gives (10:11, 5:11, 20:11 laced); NTSC uses
 * the display database's ticks (44:52 for lores). tx:ty are graphics.library's DisplayInfo
 * Resolution ticks for the mode (PAL lores 44:44). Values marked AM_EST_* in flags are estimates:
 * overscan sizes are the usual Overscan preferences defaults (each machine's own can differ), and
 * the multiscan and productivity aspects are worked out from a 4:3 screen.
 */
#ifndef EASEL_AMIGAMODES_H
#define EASEL_AMIGAMODES_H

#include "util.h"
#include "doc.h"

enum { AC_OCS, AC_ECS, AC_AGA };                 /* chipsets, oldest first */
#define AM_EST_OVERSCAN 1                        /* flags: the overscan sizes are estimates */
#define AM_EST_ASPECT   2                        /* the aspect is an estimate */
#define AM_LACED        4                        /* interlaced (or flicker-fixed double lines) */
#define AM_PAL          8                        /* a PAL-family mode (50 Hz); NTSC otherwise for TV modes */
#define AM_NTSC         16

typedef struct {
  const char *name;
  u32 modeid;                                    /* without the HAM (0x800) and EHB (0x80) bits */
  u8 chipset;                                    /* the oldest chipset that has it (AC_) */
  u8 depth, ham, ehb;                            /* on that chipset: planes (colours 1 << depth), HAM6, EHB allowed */
  u8 aga_depth, aga_ham, aga_ehb;                /* on AGA (HAM: 6 or 8 planes) */
  u16 w, h;                                      /* the nominal size */
  u16 std_w, std_h;                              /* standard overscan */
  u16 max_w, max_h;                              /* the largest overscan */
  u8 ax, ay;                                     /* BMHD pixel aspect */
  u8 tx, ty;                                     /* DisplayInfo Resolution ticks */
  u8 flags;
} AmigaMode;

int  amode_count(void);
const AmigaMode *amode_get(int i);               /* NULL out of range */
int  amode_find(u32 modeid);                     /* the table's index for a ModeID (HAM/EHB bits ignored), -1 if none */
/* what the mode allows on a chipset: planes, colours (without HAM/EHB), and whether HAM6, HAM8 and
   EHB can be used. 0 if the chipset hasn't the mode at all */
int  amode_limits(const AmigaMode *m, int chipset, int *depth, long *colors, int *ham6, int *ham8, int *ehb);
/* the aspect for a ModeID (BMHD's x/yAspect); 1 if the mode is known, else 1:1 and 0 */
int  amode_aspect(u32 modeid, u8 *ax, u8 *ay);
/* the size of an overscan preset for a mode: 0 nominal, 1 standard, 2 the largest */
void amode_size(const AmigaMode *m, int overscan, int *w, int *h);
/* the best mode for a document on a chipset (pal 1 for the PAL family, 0 NTSC): the smallest whose
   largest overscan holds the picture and whose colours (or HAM) hold its palette. -1 if none fits */
int  amode_suggest(const Doc *d, int chipset, int pal);
/* the document against a mode on a chipset, as saved with kind (codec.h's IK_) and planes (0: as
   many as the palette needs): warnings, one a line, into warn (cap bytes). Returns how many */
int  amode_check(const Doc *d, const AmigaMode *m, int chipset, int kind, int planes, char *warn, size_t cap);
/* the ModeID to write as CAMG for a mode and kind (the HAM or EHB bit added) */
u32  amode_camg(const AmigaMode *m, int kind);

#endif
