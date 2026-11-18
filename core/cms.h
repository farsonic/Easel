/*
 * cms.h - Easel 1.2: colour management. A small CMS in portable C99: ICC profiles (v2 and v4)
 * parsed - matrix/TRC RGB, grey TRC, and LUT-based (lut8, lut16, lutAtoB/lutBtoA) of any colour
 * space Easel meets (RGB, grey, CMYK, Lab) - with sRGB and Adobe RGB (1998) built in; transforms
 * between them in four intents with black point compensation; Easel's own press model for
 * CMYK (when no CMYK profile is loaded); and the canvas's view transforms: proof colours (working
 * CMYK and its plates, legacy Macintosh, Internet and monitor RGB, two kinds of colour blindness),
 * the gamut warning and the display gamma.
 *
 * How it is fast enough for a 68040: two matrix/TRC profiles convert through tables (256 linear
 * values in, a 4096-entry inverse curve out, an integer matrix between). Anything else is worked
 * out exactly, in doubles, at the points of a grid (17 or 33 a side) once, and each pixel is then
 * interpolated tetrahedrally from the grid, as lcms does.
 *
 * Colour modes: Easel's documents stay 8-bit RGB(A) inside whatever their colour model (doc.h's
 * cmode): a CMYK document is an RGB one whose colours were brought into the CMYK gamut when it was
 * converted, shown and edited in RGB, and separated into CMYK when it is saved (TIFF). 5 bytes a
 * pixel per layer, a second set of painting and compositing code and four channels for every filter
 * would be far too much for a 68040 with a few megabytes; the round trip is exact enough for print
 * work because every colour of the document is printable. A Lab document likewise keeps RGB (sRGB
 * holds every colour an 8-bit Lab file can carry that a screen can show) and is saved as CIELab.
 */
#ifndef EASEL_CMS_H
#define EASEL_CMS_H

#include "util.h"

/* a document's colour model (doc.h's cmode) */
enum { CM_RGB, CM_CMYK, CM_LAB };
/* rendering intents, as ICC numbers them */
enum { CI_PERCEPTUAL, CI_RELATIVE, CI_SATURATION, CI_ABSOLUTE };
extern const char *const cms_intent_names[5];     /* "Perceptual", ..., NULL */

#define ICC_SIG(a, b, c, d) (((u32)(a) << 24) | ((u32)(b) << 16) | ((u32)(c) << 8) | (u32)(d))
#define ICC_RGB  ICC_SIG('R', 'G', 'B', ' ')
#define ICC_GRAY ICC_SIG('G', 'R', 'A', 'Y')
#define ICC_CMYK ICC_SIG('C', 'M', 'Y', 'K')
#define ICC_LAB  ICC_SIG('L', 'a', 'b', ' ')
#define ICC_XYZ  ICC_SIG('X', 'Y', 'Z', ' ')

typedef struct IccProfile IccProfile;

/* ------------------------------------------------------------------ profiles */
/* a profile parsed from n bytes; NULL (with a message in err) if it isn't one Easel can use */
IccProfile *icc_parse(const u8 *d, size_t n, char *err, size_t errcap);
void        icc_free(IccProfile *p);
/* the built-in profiles (made once, never freed): sRGB IEC61966-2.1 and Adobe RGB (1998) */
const IccProfile *icc_srgb(void);
const IccProfile *icc_adobergb(void);
/* the bytes of a built-in profile (a v2 matrix/TRC profile, to embed in a file): CMS_SRGB or
   CMS_ADOBERGB, malloc'd; NULL if out of memory */
enum { CMS_SRGB, CMS_ADOBERGB };
u8         *icc_builtin_bytes(int which, u32 *len);
/* what a profile is: its colour space (ICC_RGB ...), its description (UTF-8 or ASCII), whether it
   is a matrix/TRC RGB profile */
u32         icc_space(const IccProfile *p);
const char *icc_desc(const IccProfile *p);
int         icc_is_matrix(const IccProfile *p);
/* the bytes a profile was parsed from (NULL for a built-in one) */
const u8   *icc_bytes(const IccProfile *p, u32 *len);
/* the description held in a profile's bytes, without parsing it all ("" if none) */
void        icc_desc_of(const u8 *d, size_t n, char *out, size_t cap);
/* 1 if two profiles' bytes describe the same colours (identical bytes past the header, or the
   same matrix and curves) */
int         icc_same(const IccProfile *a, const IccProfile *b);

/* one colour through a profile, exactly (doubles): device values 0..1 (n of them, the profile's
   channels) to the PCS as D50 XYZ (Y 1 for white), and back. intent CI_ (a LUT profile's own table
   for it, or its first) */
int         icc_to_xyz(const IccProfile *p, int intent, const double *in, double xyz[3]);
int         icc_from_xyz(const IccProfile *p, int intent, const double xyz[3], double *out);
int         icc_channels(const IccProfile *p);
/* D50 XYZ and CIELab */
void        xyz_to_lab(const double xyz[3], double lab[3]);
void        lab_to_xyz(const double lab[3], double xyz[3]);

/* ------------------------------------------------------------------ Easel's press model */
/* CMYK (0..1 ink) to D50 XYZ through Easel's built-in press: SWOP-like inks multiplied over white
   paper, 15% dot gain at 50% */
void        press_to_xyz(const double cmyk[4], double xyz[3]);
/* the separation of a colour: medium grey component replacement, 300% total ink; returns the
   colour difference (CIE76 delta E) between the colour and what the press prints for it, so a
   colour is out of gamut when that is over a few units */
double      press_from_xyz(const double xyz[3], double cmyk[4]);

/* ------------------------------------------------------------------ transforms */
typedef struct CmsXform CmsXform;
/* RGB pictures from one profile to another (NULL src or dst: sRGB); bpc 1 for black point
   compensation. grid: 0 for the default (33). NULL if out of memory */
CmsXform   *cms_xform_new(const IccProfile *src, const IccProfile *dst, int intent, int bpc);
/* the transform's colours for n RGBA pixels, in place (alpha kept) */
void        cms_xform_apply(const CmsXform *x, u8 *rgba, long n);
/* the same for n pixels of 3 bytes (the canvas's view) */
void        cms_xform_apply_rgb(const CmsXform *x, u8 *rgb, long n);
void        cms_xform_free(CmsXform *x);
/* 1 if the transform changes nothing (the same profile on both sides) */
int         cms_xform_identity(const CmsXform *x);

/* RGB to CMYK for saving a CMYK document: cmyk NULL for Easel's press, else a CMYK profile; n
   pixels of RGBA in, 4 bytes of ink each out (0 no ink, 255 full) */
typedef struct CmsSep CmsSep;
CmsSep     *cms_sep_new(const IccProfile *src, const IccProfile *cmyk, int intent);
void        cms_sep_apply(const CmsSep *s, const u8 *rgba, u8 *cmyk, long n);
void        cms_sep_free(CmsSep *s);
/* and back: 4 bytes of ink to RGB (alpha 255) through the press or profile */
void        cms_cmyk_to_rgb(const IccProfile *cmyk, const IccProfile *dst, const u8 *in, u8 *rgba, long n);
/* opening a CMYK file: the readers (JPEG, TIFF, PSD) turn ink into RGB through cmyk_hook when it
   is set (else naively); cms_cmyk_begin sets it to the file's CMYK profile (NULL: Easel's press)
   into sRGB, through a 9-point 4-D table, and cms_cmyk_end clears it. ink 0 none, 255 full */
extern void (*cmyk_hook)(const u8 ink[4], u8 rgb[3]);
int         cms_cmyk_begin(const IccProfile *cmyk);
void        cms_cmyk_end(void);
/* sRGB (or src) to 8-bit CIELab as TIFF keeps it (L 0..255 for 0..100, a and b signed bytes) and back */
void        cms_rgb_to_lab8(const IccProfile *src, const u8 *rgba, u8 *lab, long n);
void        cms_lab8_to_rgb(const IccProfile *dst, const u8 *lab, u8 *rgba, long n);

/* ------------------------------------------------------------------ the canvas's view */
/* View > Proof Setup's choices */
enum { PRF_NONE, PRF_CMYK, PRF_CYAN, PRF_MAGENTA, PRF_YELLOW, PRF_BLACK, PRF_CMY, PRF_MAC, PRF_INTERNET, PRF_MONITOR,
       PRF_PROTAN, PRF_DEUTAN, PRF_N };
extern const char *const proof_names[PRF_N];      /* English, for the menu */
typedef struct {
  int proof;                          /* PF_ (shown only while proofing is on) */
  int on;                             /* View > Proof Colors */
  int gamut;                          /* View > Gamut Warning: colours the CMYK press can't print shown grey */
  int intent, bpc;                    /* how colours are proofed */
  int gamma10;                        /* the display's gamma in tenths (0: an sRGB display) */
  const IccProfile *cmyk;             /* the working CMYK profile (NULL: Easel's press) */
  const IccProfile *monitor;          /* round 3: the display's own RGB profile (NULL: sRGB, or gamma10) */
} ViewSpec;
/* the transform the canvas draws a document through (doc its profile, NULL for untagged = sRGB);
   NULL when it would change nothing, or for want of memory */
CmsXform   *cms_view_new(const IccProfile *doc, const ViewSpec *v);
/* the grey the gamut warning paints (0xRRGGBB) */
#define GAMUT_GREY 0x808080

/* ------------------------------------------------------------------ files */
/* the ICC profile a picture file holds (PNG iCCP, JPEG APP2 ICC_PROFILE, TIFF tag 34675, PSD
   resource 1039, WebP ICCP), malloc'd with its size in *len; NULL if it holds none */
u8         *icc_from_file(const u8 *d, size_t n, u32 *len);
/* a profile put into an encoded file (fmt codec.h's FMT_PNG, FMT_JPEG, FMT_TIFF, FMT_PSD or
   FMT_WEBP; others are left as they are): takes the file, returns it grown (or as it was); NULL if
   out of memory (the file freed) */
u8         *icc_embed(int fmt, u8 *file, size_t *len, const u8 *icc, u32 icclen);
/* the colour model a file's pixels were in before they were read as RGB (CM_CMYK for a CMYK JPEG,
   TIFF or PSD, CM_LAB for a Lab TIFF or PSD, else CM_RGB) */
int         cms_file_model(const u8 *d, size_t n);

#endif
