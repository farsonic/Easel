/*
 * palette.h - Easel 0.4's indexed documents: colour modes (Indexed, Grayscale, Bitmap), the colour
 * quantizers and dithers, palette editing, colour cycling and palette files.
 * Portable C99 (the native tests and vbcc for the 68k: integer maths in the inner loops, every
 * multi-byte number in a file read and written a byte at a time).
 *
 * How an indexed document works: its layers keep their RGBA pixels, so compositing and every tool
 * work as before, but each pixel layer also has idx (w * h palette indices), which is
 * authoritative. After anything changes a box of a layer, doc_index_sync snaps the RGBA there to
 * the nearest palette colour (locked entries preferred on a tie) and updates idx. After a palette
 * entry changes, doc_pal_changed re-derives every layer's RGB from idx (alpha is kept), which is
 * what makes DPaint-style palette editing and colour cycling work.
 *
 * Grayscale documents keep R = G = B in their RGBA (and a grey ramp in pal); Bitmap documents are
 * indexed with a two-entry black and white palette.
 */
#ifndef EASEL_PALETTE_H
#define EASEL_PALETTE_H

#include "util.h"
#include "doc.h"

/* ------------------------------------------------------------------ modes */
/* how the palette is made (doc_to_indexed's method). The adaptive ones build it from the picture;
   QM_FIXED uses fixed_pal as it is. OR in QM_OCS12 to round the made colours to the OCS/ECS
   chipset's 12 bits (4 bits a gun, 4096 colours); without it they are AGA's full 24 bits */
enum { QM_MEDIAN, QM_OCTREE, QM_WU, QM_FIXED, QM_N };
#define QM_OCS12 0x100
#define QM_MASK  0xFF
/* the dithers */
enum { DI_NONE, DI_FS, DI_ATKINSON, DI_BURKES, DI_SIERRA, DI_SIERRA_LITE, DI_STUCKI, DI_JJN,
       DI_BAYER2, DI_BAYER4, DI_BAYER8, DI_PATTERN, DI_RANDOM, DI_N };
extern const char *const quant_names[QM_N + 1];     /* NULL ended, for a cycle gadget */
extern const char *const dither_names[DI_N + 1];

/* the document made indexed, as one undo step ("Indexed Color"): ncolors 2-256 entries.
   Adaptive methods: entries the document has locked (pal_lock, below its npal) keep their place
   and colour; fixed_pal's nfixed colours are forced in (they take the next free places); the rest
   is made from the colours of the visible pixel layers. QM_FIXED: the palette is fixed_pal
   (nfixed entries, ncolors ignored). An already indexed document is requantized from its RGBA.
   If the layers have ncolors colours or fewer (with the forced and locked ones) the palette is
   exact and nothing is dithered. 1 done, 0 out of memory (nothing changed) */
int  doc_to_indexed(Doc *d, int ncolors, int method, int dither, const u8 *fixed_pal, int nfixed);
/* RGB again: the indices dropped (the pixels already show the palette's colours). An undo step */
int  doc_to_rgb(Doc *d);
/* Grayscale: every pixel layer's RGB made its luminance (alpha kept), pal a grey ramp. An undo step */
int  doc_to_gray(Doc *d);
/* Bitmap (black and white, as Photoshop's Bitmap mode): from the luminance, by method */
enum { BMM_THRESHOLD, BMM_PATTERN, BMM_DIFFUSION, BMM_HALFTONE, BMM_N };
extern const char *const bitmap_names[BMM_N + 1];
int  doc_to_bitmap(Doc *d, int method);

/* after a change to box b of layer li (any mode; nothing for RGB): Indexed and Bitmap snap the RGBA
   there to the palette and update idx (made for the whole layer first if it has none); Grayscale
   makes it grey. Not an undo step of its own: call it inside the step that made the change
   (doc_note_layer saves the indices with the pixels) */
void doc_index_sync(Doc *d, int li, Box b);
/* every pixel layer synced throughout (after layers are added, pasted or merged) */
void doc_index_sync_all(Doc *d);
/* palette entries first..last changed: every indexed layer's RGB re-derived from idx there */
void doc_pal_changed(Doc *d, int first, int last);
/* the nearest entry to a colour (weighted RGB distance; a locked entry wins a tie) */
int  pal_nearest(const Doc *d, int r, int g, int b);

/* ------------------------------------------------------------------ preset palettes */
enum { PP_WB13, PP_WB20, PP_MWB, PP_WEB216, PP_GRAY2, PP_GRAY4, PP_GRAY16, PP_GRAY256, PP_RGB332,
       PP_OCS_CUBE, PP_EHB_DEFAULT, PP_N };
extern const char *const preset_names[PP_N + 1];
/* a preset into pal (3 bytes an entry); returns how many entries */
int  pal_preset(int which, u8 *pal);

/* ------------------------------------------------------------------ editing (each an undo step) */
int  pal_set_rgb(Doc *d, int i, int r, int g, int b);
/* h 0-359, s and v 0-255 */
int  pal_set_hsv(Doc *d, int i, int h, int s, int v);
void pal_rgb_to_hsv(int r, int g, int b, int *h, int *s, int *v);
void pal_hsv_to_rgb(int h, int s, int v, int *r, int *g, int *b);
/* Spread: the entries between a and b (either order) made a straight RGB ramp from a's colour to b's */
int  pal_spread(Doc *d, int a, int b);
/* Exchange: entries a and b swapped. remap 0: just the colours (the picture's pixels change colour,
   as DPaint's Ex does); 1: the indices too, so the picture looks the same */
int  pal_swap(Doc *d, int a, int b, int remap);
/* Copy: entry from's colour into entry to */
int  pal_copy(Doc *d, int from, int to);
/* the whole palette replaced by pal (n entries; the indices are kept, so the picture changes: a
   palette loaded DPaint style). n beyond npal grows npal */
int  pal_replace(Doc *d, const u8 *pal, int n);
/* Remap: the picture (as it looks) mapped to pal's n colours, which become the palette */
int  pal_remap(Doc *d, const u8 *pal, int n, int dither);
/* sort by key (unlocked entries only; locked ones keep their place), the indices remapped so the
   picture looks the same */
enum { PS_HUE, PS_LUM, PS_SAT, PS_USE };
int  pal_sort(Doc *d, int key);
/* entries no layer uses taken out (locked ones kept), the rest packed down; returns how many went */
int  pal_remove_unused(Doc *d);
/* how many entries the layers use, and (counts may be NULL) how many pixels each */
int  pal_count_used(const Doc *d, unsigned long *counts);
/* how many different colours the visible pixel layers have (any mode), counting up to max */
long doc_count_colors(const Doc *d, long max);
/* Extra Half-Brite: entries 32-63 made half of 0-31 and npal 64 */
int  pal_make_ehb(Doc *d);
/* (each edit above also exists unrecorded, for live slider drags: the UI then wraps a run of them in
   one doc_begin / doc_note_pal / doc_end of its own) */
void pal_put_rgb(Doc *d, int i, int r, int g, int b);

/* ------------------------------------------------------------------ colour cycling */
/* range r's entries rotated one place in dir (+1 forward: each colour moves to the next entry up,
   the last wrapping to lo; reversed by the range's reverse flag), the indices untouched and the
   layers' RGB re-derived. No undo. 1 if anything moved */
int  cycle_step(Doc *d, int r, int dir);
/* CRNG rate (16384 = 60 steps a second) to milliseconds between steps (0: not cycling), and back */
u32  cycle_interval_ms(u16 rate);
u16  cycle_rate_from_ms(u32 ms);
/* steps per second times 100, for showing */
u32  cycle_rate_hz100(u16 rate);
/* playing every active range together: elapsed milliseconds since the last call; each range steps
   as often as its rate says. acc is the caller's (MAXCYCLES entries, zeroed to start). Returns the
   number of steps taken (redraw if not 0) */
int  cycle_advance(Doc *d, u32 *acc, u32 elapsed_ms);
/* the smallest interval any active range needs, for a timer (ms; 0 if none is active) */
u32  cycle_timer_ms(const Doc *d);
/* one IFF chunk (id "CRNG", "CCRT" or "DRNG", data len bytes) added to ranges (*n of them, up to
   MAXCYCLES). 1 if it was one and was understood */
int  cycle_parse_chunk(const char *id, const u8 *data, size_t len, CycleRange *ranges, int *n);
/* every CRNG, CCRT and DRNG chunk of an IFF FORM (ILBM, PBM, ACBM...) into ranges; returns how many */
int  cycle_read_iff(const u8 *d, size_t n, CycleRange *ranges, int max);
/* the ranges as IFF chunks appended to b: which 0 CRNG, 1 CCRT, 2 DRNG */
void cycle_write_chunks(Buf *b, const CycleRange *ranges, int n, int which);

/* ------------------------------------------------------------------ palette files */
enum { PF_ACT, PF_ACO, PF_ASE, PF_GPL, PF_JASC, PF_RIFF, PF_IFF, PF_PNG, PF_N };
extern const char *const palfile_names[PF_N + 1];   /* "Adobe Color Table (.act)" ... */
extern const char *const palfile_ext[PF_N];         /* "act" ... */
typedef struct {
  int n;                              /* entries, 1-256 */
  u8 rgb[256 * 3];
  int transparent;                    /* -1 none (ACT, PNG tRNS) */
  CycleRange cycle[MAXCYCLES]; int ncycle;   /* (IFF) */
  char name[64];                      /* (GPL, ASE group) */
} PalFile;
/* the format of a palette file from its bytes (and its name's extension, for .act); -1 if unknown */
int  palfile_detect(const u8 *d, size_t n, const char *name);
/* read: 1, or 0 with a message in err */
int  palfile_read(const u8 *d, size_t n, const char *name, PalFile *out, char *err, size_t errcap);
/* written as fmt, malloc'd (*len its size); NULL if out of memory */
u8  *palfile_write(int fmt, const PalFile *p, size_t *len);
/* the document's palette as a PalFile, and a PalFile as the document's palette (pal_replace) */
void palfile_from_doc(const Doc *d, PalFile *p);

/* ------------------------------------------------------------------ for the writers */
/* the composite as indices: for each pixel, the topmost visible indexed layer that covers it fully
   (opaque, Normal, full opacity) gives its index; anything else is mapped to the nearest entry.
   alpha (may be NULL) gets the composite's alpha. Malloc'd w * h; NULL if out of memory or not
   indexed */
u8  *doc_flat_indices(Doc *d, u8 **alpha);
/* the composite as RGBA (malloc'd w * h * 4) */
u8  *doc_flat_rgba(Doc *d);
/* any picture to n colours by method (QM_) and dither: pal gets the colours, idx (w * h) each
   pixel's entry; pixels with alpha < 128 are left out of the palette. The first nfixed of pal are
   kept as they are (forced). Returns the number of entries, 0 if out of memory */
int  quantize_rgba(const u8 *px, i32 w, i32 h, int n, int method, int dither, u8 *pal, int nfixed, u8 *idx);
/* map a picture to a palette with a dither (idx w * h) */
int  map_rgba(const u8 *px, i32 w, i32 h, const u8 *pal, int n, int dither, u8 *idx);
/* the pixels whose alpha is under 128 given a transparent entry: an entry added if npal < 256 (made
   black, *npal grown), else one the opaque pixels don't use. Returns it, or -1 if nothing is
   transparent or no entry is free (then the pixels are left as they are) */
int  flat_transparent(u8 *idx, const u8 *alpha, long n, u8 *pal, int *npal);
/* the document as a PNG in its mode: Indexed and Bitmap with PLTE (and tRNS when transparent),
   Grayscale as grey (with alpha if any), RGB as png_encode does */
u8  *doc_png_encode(Doc *d, int dpi, size_t *len);

#endif
