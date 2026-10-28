/*
 * swatch.h - the Swatches panel's colours (0.5): a named list of named colours, with Easel's
 * defaults, and files: GIMP palettes (.gpl) and Easel's own text format here; Photoshop .aco,
 * .act, Adobe .ase and IFF CMAP through hooks.
 *
 * HOOKS: palette files are read and written by core/palette.c (another branch). Rather than
 * duplicate that code, swatch_hook_read / swatch_hook_write hold one function a format; the .gpl
 * and Easel formats are filled in here, the others are NULL until palette.c's are connected:
 *
 *     swatch_hook_read[SWF_ACO] = my_aco_reader;   (and so on, once at start-up)
 *
 * or by building with -DEASEL_HAVE_PALETTE, which makes swatch_init_hooks() call
 * palette_swatch_hooks() (to be provided by palette.c). Until then those formats report
 * "not supported".
 */
#ifndef EASEL_SWATCH_H
#define EASEL_SWATCH_H

#include "util.h"

typedef struct { u32 rgb; char name[32]; } Swatch;            /* 0xRRGGBB; name UTF-8, may be "" */
typedef struct { char name[64]; Swatch *s; int n, cap; } SwatchSet;

void swatches_init(SwatchSet *s);
void swatches_free(SwatchSet *s);
int  swatches_add(SwatchSet *s, u32 rgb, const char *name);    /* at the end; 0 if out of memory */
int  swatches_insert(SwatchSet *s, int at, u32 rgb, const char *name);
void swatches_remove(SwatchSet *s, int i);
void swatches_clear(SwatchSet *s);
int  swatches_find(const SwatchSet *s, u32 rgb);                /* the first with that colour, -1 none */
int  swatches_defaults(SwatchSet *s);                           /* Easel's default swatches added */

/* file formats */
enum { SWF_EASEL, SWF_GPL, SWF_ACO, SWF_ACT, SWF_ASE, SWF_CMAP, SWF_N };
extern const char *const swatch_ext[SWF_N];                     /* ".swatches", ".gpl", ".aco", ".act", ".ase", ".iff" */
/* a reader adds the file's colours to out (and may set out->name), returning how many (0: not
   that kind of file); a writer returns the file malloc'd with *len its size (NULL on failure) */
typedef int (*SwatchReader)(const u8 *data, size_t n, SwatchSet *out);
typedef u8 *(*SwatchWriter)(const SwatchSet *s, size_t *len);
extern SwatchReader swatch_hook_read[SWF_N];
extern SwatchWriter swatch_hook_write[SWF_N];
void swatch_init_hooks(void);                                   /* (called by the load and save functions too) */
void swatch_palette_hooks(void);                                /* (swatchpal.c) .aco, .act, .ase and IFF CMAP through palette.c */
int  swatch_format_of(const char *path);                        /* by extension; SWF_EASEL when unknown */
int  swatch_supported(int fmt, int writing);
/* the built-in formats */
int  swatches_read_gpl(const u8 *data, size_t n, SwatchSet *out);
u8  *swatches_write_gpl(const SwatchSet *s, size_t *len);
int  swatches_read_easel(const u8 *data, size_t n, SwatchSet *out);
u8  *swatches_write_easel(const SwatchSet *s, size_t *len);
/* by the file's extension (load also sniffs the content); load adds to s and returns how many,
   save returns 0 on failure or for a format with no writer connected */
int  swatches_load(SwatchSet *s, const char *path);
int  swatches_save(const SwatchSet *s, const char *path);

#endif
