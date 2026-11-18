/*
 * type.h - type layers (Easel 0.9): editable text kept as settings (Layer's live: a TextParams) and
 * drawn by the compositor whenever it is wanted, so it can be edited on the canvas, restyled and
 * resized without loss; plus the layout the Type tool needs to edit it in place (hit testing, the
 * caret, selection highlights), kerning from the font, Warp Text, and the Type Mask.
 *
 * Text is UTF-8 in runs of one style (font, size, colour, tracking, baseline shift, underline,
 * strikethrough, faux bold and italic). Point text (a click) runs from its first baseline at x, y
 * with lines only where the text has newlines; paragraph text (a drag) wraps inside a box. Fonts
 * are TrueType/OpenType faces (fonts.h, drawn by ttraster.c) found through text_face_lookup, or
 * Amiga bitmap fonts (amifont.h) through text_amifont_lookup.
 *
 * Positions are canvas pixels (doubles). Byte offsets into the text are caret positions (0 before
 * the first character, strlen after the last); they always fall between UTF-8 characters.
 */
#ifndef EASEL_TYPE_H
#define EASEL_TYPE_H

#include "doc.h"
#include "fonts.h"
#include "text.h"
#include "path.h"

#define TP_MAXTEXT 4096                /* bytes of text a type layer holds, the 0 included */
#define TP_MAXRUNS 64

/* alignments: text.h's TA_LEFT, TA_CENTER, TA_RIGHT, and */
enum { TA_JUSTIFY = 3,                 /* justified, the last line of each paragraph left */
       TA_JUSTIFY_ALL = 4 };           /* every line justified */
enum { TAA_NONE, TAA_SHARP, TAA_SMOOTH };   /* anti-aliasing: none, sharp (hinted), smooth */
enum { TW_NONE, TW_ARC, TW_ARCH, TW_BULGE, TW_FLAG, TW_WAVE, TW_FISH, TW_RISE, TW_SQUEEZE, TW_TWIST,
       TW_ARCLOWER, TW_ARCUPPER, TW_SHELLLOWER, TW_SHELLUPPER, TW_FISHEYE, TW_INFLATE,   /* 1.1 */
       TW_N };
extern const char *const text_warp_names[TW_N];

typedef struct {
  i32 start;                          /* the byte where this run begins (run 0 at 0; in order) */
  char family[48];                    /* the font's family (UTF-8) */
  u8 bold, italic;                    /* the face asked for */
  u8 faux_bold, faux_italic;
  u8 underline, strike;
  u8 pad[2];
  u32 color;                          /* 0xRRGGBB */
  double size;                        /* pixels to the em (1-2000) */
  double tracking;                    /* thousandths of an em added after each character */
  double baseline;                    /* baseline shift, pixels up */
} TextRun;
/* which fields text_style sets */
enum { TS_FAMILY = 1, TS_BOLD = 2, TS_ITALIC = 4, TS_FAUXBOLD = 8, TS_FAUXITALIC = 16, TS_UNDERLINE = 32,
       TS_STRIKE = 64, TS_COLOR = 128, TS_SIZE = 256, TS_TRACKING = 512, TS_BASELINE = 1024, TS_ALL = 2047 };

typedef struct TextParams {
  double x, y;                        /* point text: the first line's baseline starts here (the
                                         alignment point: left end, centre or right end); paragraph
                                         text: the box's top left */
  double bw, bh;                      /* paragraph text: the box's size (box set) */
  u8 box;                             /* 1 paragraph text in the box, 0 point text */
  u8 align;                           /* TA_ */
  u8 aa;                              /* TAA_ */
  u8 vertical;                        /* characters down in columns, the columns right to left */
  u8 warp;                            /* TW_ */
  u8 warp_vertical;                   /* (kept for Photoshop's Vertical warp; drawn as horizontal) */
  u8 kern;                            /* 1: the font's kerning pairs used (Metrics), 0: none */
  u8 xf_on;                           /* xf applies */
  double leading;                     /* baseline to baseline, pixels; 0 auto (the font's line spacing) */
  double indent;                      /* first line of each paragraph, pixels (paragraph text) */
  double space_before, space_after;   /* around each paragraph, pixels */
  double warp_bend, warp_h, warp_v;   /* -100..100 per cent */
  double xf[6];                       /* drawn through x' = xf0 x + xf1 y + xf2, y' = xf3 x + xf4 y + xf5 (Free Transform) */
  i32 nruns;
  i32 pad2;
  TextRun run[TP_MAXRUNS];
  char text[TP_MAXTEXT];
} TextParams;

/* ------------------------------------------------------------------ fonts */
/* how a run's family and style become a face: set by the UI (the default looks in text_fonts,
   with fonts.h's substitutions; NULL when there is no face at all) */
extern Face *(*text_face_lookup)(const char *family, int bold, int italic);
extern FontSet *text_fonts;
/* Amiga bitmap fonts: asked first for each run when set (family as the .font's name without
   ".font", size the pixel size wanted); NULL to use TrueType. The font must stay loaded while used */
struct AmiFont;
extern struct AmiFont *(*text_amifont_lookup)(const char *family, int size);
/* the font files kept open between renderings closed (at exit, or after the font list changes) */
void text_cache_flush(void);

/* ------------------------------------------------------------------ settings */
/* one run of family at size px in color, point text at x, y, left aligned, smooth, kerned */
void text_params_init(TextParams *tp, const char *family, double size, u32 color, double x, double y);
int  text_set_string(TextParams *tp, const char *utf8);         /* the text replaced; runs after its end dropped */
/* editing the text (the runs follow it): inserted text takes the style of the character before it
   (or of run 0 at the start); 0 if it wouldn't fit */
int  text_insert(TextParams *tp, int at, const char *utf8, int len);
int  text_delete(TextParams *tp, int from, int to);
/* the fields of st named by mask (TS_) set on the text from..to (to > from) */
int  text_style(TextParams *tp, int from, int to, const TextRun *st, u32 mask);
int  text_run_at(const TextParams *tp, int at);                  /* the run byte at is in (the one before for the caret) */

/* ------------------------------------------------------------------ layout */
typedef struct {
  i32 byte;                           /* where its character is in the text */
  u32 cp;                             /* the character */
  u16 glyph, run;
  i32 line;
  double x, y;                        /* its pen position (baseline), layout space (before warp and xf) */
  double adv;                         /* how far it moves the pen (along the line) */
} TGlyph;
typedef struct {
  i32 start, end;                     /* its text: start..end (end before the newline, if any) */
  i32 g0, g1;                         /* its glyphs g0..g1-1 */
  double x0, x1;                      /* where it starts and ends (vertical: y) */
  double base;                        /* its baseline (vertical: the column's centre x) */
  double ascent, descent;             /* (pixels) */
  u8 hidden;                          /* past the bottom of the box: not drawn */
} TLine;
typedef struct TextLayout {
  TGlyph *g; i32 ng;
  TLine *ln; i32 nl;
  double bx0, by0, bx1, by1;          /* the text's extent, layout space */
  u8 vertical;
  TextParams *tp;                     /* (a copy: the layout stands alone) */
} TextLayout;
TextLayout *text_layout(const TextParams *tp);                   /* NULL if out of memory */
void text_layout_free(TextLayout *tl);
/* the caret position nearest canvas point x, y (through the inverse of xf; warp is not undone) */
int  text_hit(const TextLayout *tl, double x, double y);
/* the caret at byte index as a line from x0, y0 (top) to x1, y1 (bottom), canvas pixels; 0 if no such index */
int  text_caret_pos(const TextLayout *tl, int index, double *x0, double *y0, double *x1, double *y1);
enum { TCM_LEFT, TCM_RIGHT, TCM_UP, TCM_DOWN, TCM_HOME, TCM_END, TCM_WORDLEFT, TCM_WORDRIGHT, TCM_START, TCM_END_ALL };
int  text_caret_move(const TextLayout *tl, int index, int how);   /* the caret after a cursor key */
/* the highlight of the text from..to as quadrilaterals (8 doubles each: four corners, canvas pixels);
   how many (at most max) */
int  text_sel_quads(const TextLayout *tl, int from, int to, double *q, int max);
/* the extent of the drawn text on the canvas (warp and xf included), whole pixels */
Box  text_bounds(const TextParams *tp);
/* the width and height of the text laid out (layout space) */
int  text_extent(const TextParams *tp, double *w, double *h);

/* ------------------------------------------------------------------ drawing */
/* the text drawn in RGBA over transparency, in box *b (clipped to a w x h canvas), malloc'd into
   *px. 1 with *px NULL when nothing shows; 0 if out of memory */
int  text_render(const TextParams *tp, i32 w, i32 h, u8 **px, Box *b);
/* the text's coverage (0-255) into mask (w * h, overwritten): the Type Mask's shape */
int  text_coverage(const TextParams *tp, i32 w, i32 h, u8 *mask);
/* the text's outlines as a path (Type > Create Work Path): straight segments; NULL if out of memory */
Path *text_to_path(const TextParams *tp);
/* the kerning between two glyphs of a face, font units (0 when it has none) */
int  text_kern(Face *f, u16 left, u16 right);

/* ------------------------------------------------------------------ in the document */
const TextParams *layer_text(const Layer *l);                    /* NULL if it isn't a type layer */
/* a new type layer above the active one, named after its text when name is NULL; one undo step.
   Its index, -1 on failure */
int  doc_new_text(Doc *d, const TextParams *tp, const char *name);
/* type layer i's settings replaced; one undo step ("Edit Type"). To make a whole editing session
   one step, put doc_begin(d, "Type") and doc_end(d) around it */
int  doc_set_text(Doc *d, int i, const TextParams *tp);
#endif
