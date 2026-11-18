/*
 * menu3.h - Easel 1.3's menu, tool and automation additions (round 3 of the PhotoCraft gap work):
 * File > Export > Save for Web (Legacy) (also the "Save for Screen" preview), Export As's size
 * estimate, File > Scripts > Image Processor, the New dialog's units, colour modes, pixel aspect and
 * saved presets, View > Pixel Art Preview's zoom steps, Reset Tool, Copy Color as HTML, Edit > Fill
 * from History and Preserve Transparency, Edit > Stroke's mode, File > Export > Color Lookup
 * Tables, File > Automate > Contact Sheet II, File > Scripts > Statistics, File > Generate > Image
 * Assets, Edit > Find and Replace Text, Image > Apply Image and Calculations, Image > Analysis
 * (measurement scale, scale marker), Layer > New Layer Based Slice, Artboard and Frame
 * from Layers, File > Open as Photoshop Raw, the blend-mode and opacity keys, View > Snap To
 * > Layers and Slices, and a grabbed screen as a document. Portable C99, no AmigaOS calls.
 */
#ifndef EASEL_MENU3_H
#define EASEL_MENU3_H
#include <stddef.h>
#include "doc.h"
#include "codec.h"

/* ------------------------------------------------------------------ Save for Web (Legacy) */
enum { SFW_GIF, SFW_PNG8, SFW_PNG24, SFW_JPEG, SFW_ILBM, SFW_N };
extern const char *const sfw_names[SFW_N + 1];        /* "GIF", "PNG-8", "PNG-24", "JPEG", "IFF ILBM" (NULL ended) */
typedef struct {
  int fmt;                            /* SFW_ */
  int colors;                         /* GIF, PNG-8, ILBM: 2-256 */
  int method;                         /* palette.h's QM_ (Median Cut, Octree, Wu) */
  int dither;                         /* palette.h's DI_ */
  int quality;                        /* JPEG 1-100 */
  int progressive;                    /* JPEG progressive; GIF and PNG-8 interlaced */
  int transparency;                   /* GIF, PNG-8, PNG-24: kept (else laid over the matte) */
  u32 matte;                          /* 0xRRGGBB the transparent parts are laid over */
  int pct;                            /* scale, per cent (1-1000) */
} SfwOpts;
void sfw_defaults(SfwOpts *o);
const char *sfw_ext(int fmt);                         /* ".gif" ... */
/* the picture as the file Save for Web writes (malloc'd, *len its size); NULL if out of memory */
u8  *sfw_encode(Doc *d, const SfwOpts *o, size_t *len);
/* the optimized picture as it will look: the file decoded again into out. 1 done */
int  sfw_preview(const u8 *file, size_t len, Rgba *out);
/* seconds to send len bytes at bits per second (rounded up; at least 1) */
long sfw_seconds(size_t len, long bps);
/* a size as "12.3K" or "1.25M" (out at least 16 bytes) */
void size_text(char *out, size_t len);

/* ------------------------------------------------------------------ Export As's size estimate */
/* the bytes Export As would write for d in format fmt (FMT_, xwrite's too) at quality (JPEG, WebP),
   after its scale and matte (exportx.h's ExportOpts; NULL: as it is); 0 if it can't be worked out */
struct ExportOptsS;
size_t export_estimate(Doc *d, const void *exportopts, int fmt, int quality);

/* ------------------------------------------------------------------ Image Processor */
typedef struct {
  int jpeg, jpeg_quality, jpeg_resize; i32 jpeg_w, jpeg_h;   /* save as JPEG, quality, fitted inside w x h */
  int psd, psd_resize; i32 psd_w, psd_h;
  int tiff, tiff_resize; i32 tiff_w, tiff_h;
  int png, png_resize; i32 png_w, png_h;
  int ilbm, ilbm_resize; i32 ilbm_w, ilbm_h; int ilbm_colors;   /* (Easel's own: IFF ILBM, 0 24-bit) */
  int to_srgb;                        /* Convert Profile to sRGB */
  int icon;                           /* a Workbench icon beside each */
  char copyright[256];                /* Copyright Info ("" leaves File Info alone) */
} ImProcOpts;
void improc_defaults(ImProcOpts *o);
/* the size w x h fitted inside fw x fh, keeping its proportions (never enlarged when shrink_only) */
void fit_size(i32 w, i32 h, i32 fw, i32 fh, int shrink_only, i32 *nw, i32 *nh);
/* one picture d (left unchanged) written as o asks: drawer/JPEG/name.jpg, drawer/PSD/name.psd,
   drawer/TIFF/name.tif, drawer/PNG/name.png, drawer/ILBM/name.iff; make_drawer is called for each
   sub-drawer first (0: it failed). How many files were written; err says what went wrong */
int  improc_doc(Doc *d, const char *name, const char *drawer, const ImProcOpts *o, int (*make_drawer)(const char *path),
                char *err, size_t errcap);
/* a path joined: drawer + "/" + part (Amiga "DH0:" keeps no slash) into out */
void path_join(char *out, size_t cap, const char *drawer, const char *part);

/* ------------------------------------------------------------------ File > New */
enum { NU_PIXELS, NU_INCHES, NU_CM, NU_MM, NU_POINTS, NU_PICAS, NU_N };
extern const char *const newunit_names[NU_N + 1];
enum { NM_BITMAP, NM_GRAY, NM_RGB, NM_CMYK, NM_LAB, NM_N };
extern const char *const newmode_names[NM_N + 1];
enum { NB_WHITE, NB_BLACK, NB_BGCOLOR, NB_TRANSPARENT, NB_N };
typedef struct {
  char name[40];
  double w, h; int unit;              /* in unit (NU_) */
  int dpi;
  int mode;                           /* NM_ */
  int bg;                             /* NB_ (Bitmap: never transparent) */
  u8 ax, ay;                          /* pixel aspect (1:1 square) */
} NewPreset;
extern const NewPreset newdoc_builtin[];
extern const int newdoc_nbuiltin;
double units_to_px(double v, int unit, int dpi);      /* rounded by the caller */
double px_to_units(double px, int unit, int dpi);
/* a preset as a line of Easel.newpresets ("name|w|h|unit|dpi|mode|bg|ax|ay"), and back (1 if it was one) */
void newpreset_format(const NewPreset *p, char *out, size_t cap);
int  newpreset_parse(const char *line, NewPreset *p);
/* the document a preset describes (bgcolor: the background colour for NB_BGCOLOR); NULL with a
   message in err if it is too big or out of memory. History: one state, "New" */
Doc *newdoc_make(const NewPreset *p, u32 bgcolor, char *err, size_t errcap);

/* ------------------------------------------------------------------ View > Pixel Art Preview */
/* the zoom (per mille) a pixel-art view steps to from z: whole multiples above 100 % and 1/n
   below; dir +1 the next larger, -1 the next smaller, 0 the nearest */
long pixelart_zoom(long z, int dir);

/* ------------------------------------------------------------------ Reset Tool */
typedef struct { int size, hardness, spacing, smoothing, opacity, flow; } ToolTip;
/* PhotoCraft's tip for a tool after Reset Tool (by its name: "Brush", "Pencil", "Eraser",
   "Background Eraser", "Healing Brush", "Spot Healing Brush", "Clone Stamp", "Pattern Stamp",
   "Smudge", "Blur", "Sharpen", "Dodge", "Burn", "Sponge", "Remove", "History Brush", "Mixer Brush",
   "Color Replacement", "Art History Brush"); 0 for a tool it doesn't know (*t gets general defaults) */
int  tool_reset_tip(const char *name, ToolTip *t);

/* ------------------------------------------------------------------ Copy Color as HTML */
void color_html(u32 rgb, char *out);                  /* "color: #c8641e;" (out at least 24) */
void color_hex(u32 rgb, char *out);                   /* "c8641e" */

/* ------------------------------------------------------------------ Edit > Fill and Stroke */
/* the selection (or everything) on the active layer filled from src (w * h * 4 RGBA: a history
   snapshot's merged pixels) at opacity 0-100 in mode, keeping the layer's transparency when keep;
   one undo step "Fill" */
Box  fill_from_pixels(Doc *d, const u8 *src, int opacity, int blend, int keep);
/* fill_selection and stroke_selection_mode with Preserve Transparency */
Box  fill_keep(Doc *d, u32 color, int opacity, int blend, int keep);
Box  stroke_keep(Doc *d, int width, int where, u32 color, int opacity, int blend, int keep);

/* ------------------------------------------------------------------ Export > Color Lookup Tables */
/* what the document's visible adjustment layers do to colour, sampled on an n x n x n grid
   (2-64) as an Adobe .cube file (TITLE, LUT_3D_SIZE, red fastest), malloc'd; NULL if there are none
   (or out of memory) */
char *lut_cube_from_doc(Doc *d, int n, const char *title);
/* the same as a 3DL (0-1023, red slowest), malloc'd */
char *lut_3dl_from_doc(Doc *d, int n);

/* ------------------------------------------------------------------ Contact Sheet II */
typedef struct {
  i32 w, h; int dpi;                  /* the sheet */
  int cols, rows;
  int across;                         /* place across first (else down first) */
  int spacing;                        /* pixels between the cells (and round the edge) */
  int flatten;                        /* one layer; else a layer each, named after its picture */
  int rotate;                         /* turned to fit a cell's orientation best */
  u32 bg;
  int captions;                       /* each picture's name under it, as a type layer */
  int font_px;                        /* the captions' size (pixels) */
  char family[48];                    /* and font family */
} ContactOpts;
void contact_defaults(ContactOpts *o);
/* page p (0 up) of the sheet of pics[0..n) (any sizes), each fitted in its cell and centred;
   names[] name the layers (may be NULL). NULL if out of memory or the page is empty */
Doc *contact_sheet(Doc **pics, const char *const *names, int n, int page, const ContactOpts *o);
int  contact_pages(int n, const ContactOpts *o);

/* ------------------------------------------------------------------ Scripts > Statistics */
/* a new document of the first picture's size whose one layer is the stack mode (layerops.h's STK_)
   of all n pictures' composites (others are scaled to fit); NULL if fewer than two or out of memory */
Doc *doc_statistics(Doc **pics, int n, int mode);

/* ------------------------------------------------------------------ Generate > Image Assets */
/* a layer name as the assets it asks for ("logo.png, 50% logo-small.jpg8, icon.gif"): up to max
   entries of scale (per cent), file name and quality (JPEG 1-100 from a digit 1-10 or a number
   with %; PNG 8, 24 or 32; 0 the format's own). How many */
typedef struct { int pct; char file[64]; int quality; } AssetSpec;
int  assets_parse(const char *layername, AssetSpec *out, int max);
/* every layer (or group) whose name asks for assets written into drawer: its pixels (a group's
   members merged) trimmed to what isn't transparent, scaled. How many files; a line each in log */
int  assets_generate(Doc *d, const char *drawer, char *log, size_t logcap);

/* Export As for a layer: layer i (a group: its members merged) alone, trimmed to what isn't
   transparent, written in the format path's extension asks (PNG if none); 1 done */
int  layer_to_file(Doc *d, int i, const char *path);

/* File > Package: every linked smart object's source copied into drawer (under its own name, or
   with a number when two share one) and the layer relinked to the copy; how many, -1 if one
   couldn't be copied */
int  package_links(Doc *d, const char *drawer);

/* ------------------------------------------------------------------ Find and Replace Text */
/* every type layer's text (only layer `only` when it is >= 0): find replaced by repl (case-sensitive
   or not, whole words only or not, all of them or only the first found); one undo step. How many */
int  type_find_replace(Doc *d, int only, const char *find, const char *repl, int match_case, int whole, int all);
/* the next match from layer from (and byte at): 1 with *layer and *at set, else 0 */
int  type_find_next(Doc *d, int from, int at, const char *find, int match_case, int whole, int *layer, int *pos);

/* ------------------------------------------------------------------ Apply Image and Calculations */
enum { AC_RGB, AC_RED, AC_GREEN, AC_BLUE, AC_ALPHA, AC_GRAY, AC_SEL, AC_N };   /* the source channel */
extern const char *const ac_names[AC_N + 1];
/* the source: layer index of src (-1 the merged picture), channel AC_, inverted */
typedef struct { Doc *doc; int layer; int channel; int invert; } AiSource;
/* Image > Apply Image: the source blended (BM_, opacity 0-100) into d's active pixel layer through
   the selection, keeping its transparency when keep (and through mask m, w * h, may be NULL); one undo
   step. Both documents must be the same size. 0 if it can't be done */
int  apply_image(Doc *d, const AiSource *s, int blend, int opacity, int keep, const u8 *mask);
/* Image > Calculations: two sources' channels (AC_RGB taken as grey) blended into one; the result
   (w * h, malloc'd), NULL if the sizes differ or out of memory */
u8  *calculations(const AiSource *a, const AiSource *b, int blend, int opacity, i32 w, i32 h);
/* the result put as a new channel (its index), or made the selection (-2); one undo step; -1 failed */
int  calc_result(Doc *d, u8 *m, int as_selection, const char *name);

/* ------------------------------------------------------------------ Image > Analysis */
typedef struct { double px; double len; char unit[16]; } MeasureScale;   /* px pixels = len unit */
/* the document's scale (pixels when none was set); kept for the session, not in the file */
void measure_scale_get(const Doc *d, MeasureScale *s);
void measure_scale_set(const Doc *d, const MeasureScale *s);
void measure_forget(const Doc *d);                     /* (a document closed) */
double measure_len(const Doc *d, double px);           /* a length in pixels in the scale's units */
/* Place Scale Marker: a new layer with a bar len units long (in the scale) at the bottom left,
   black or white; its index or -1 */
int  scale_marker(Doc *d, double len, int white, int height);

/* ------------------------------------------------------------------ slices, artboards, frames */
int  slice_from_layer(Doc *d, int i);                  /* New Layer Based Slice: its index, -1 none */
Box  layer_bounds(const Doc *d, int i);                /* what a layer (or group) covers; empty if nothing */
int  artboard_from_layers(Doc *d);                     /* the picked layers in a new artboard round them; its index */
int  frame_from_layer(Doc *d, int i);                  /* the layer placed in a new frame of its size; its index */

/* ------------------------------------------------------------------ File > Open as Photoshop Raw */
typedef struct { i32 w, h; int channels; int interleaved; long header; int bits16; int bigendian; } RawOpts;
/* data read as Photoshop Raw (headerless 1-4 channel samples) as a one-layer document */
Doc *open_raw(const u8 *data, size_t n, const RawOpts *raw, char *err, size_t errcap);
/* Photoshop Raw's guess: a square (or w x h for the given channels) that the size fits */
void raw_guess(size_t n, RawOpts *r);

/* ------------------------------------------------------------------ keys */
/* Shift + and Shift - : the next or previous blend mode in the Layers panel's order */
int  blend_step(int mode, int dir);
/* opacity typed as digits: one digit 10 %-100 % ("0" 100), two quickly ("4", "5" in a third of a
   second) 45 %, "00" 0 %. State kept by the caller (zeroed at first); ms: the time of the key */
typedef struct { int first; unsigned long ms; int pending; } OpacityKeys;
int  opacity_key(OpacityKeys *k, int digit, unsigned long ms);

/* the tool each letter chose last (26 entries, tool + 1, 0 none) as a preference value ("0,3,0,...")
   and back (tools past ntools dropped); 1 if it was one */
void toolkeys_format(const signed char *k, char *out, size_t cap);
int  toolkeys_parse(const char *s, signed char *k, int ntools);

/* ------------------------------------------------------------------ View > Snap To > Layers, Slices */
#define SNAP_LAYERS 64                 /* (guides.h's flags) the edges and centres of the other layers */
#define SNAP_SLICES 128                /* the slices' edges */
void menu3_init(void);                 /* guides.c's hook set */

/* ------------------------------------------------------------------ File > Import > Screen */
/* a grabbed screen as a document: chunky pens (w * h) with their colours (pal, 3 bytes each), or
   RGB (3 bytes a pixel) when pens is NULL; NULL if out of memory */
Doc *doc_from_screen(const u8 *pens, const u8 *pal, int npal, const u8 *rgb, i32 w, i32 h, int keep_palette);
#endif
