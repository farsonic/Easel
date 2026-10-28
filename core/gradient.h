/*
 * gradient.h - Easel's gradients (0.5): colour stops and opacity stops, each with a midpoint to
 * the next, as Photoshop's and PhotoCraft's Gradient Editor make them; a list of presets; files
 * in GIMP's .ggr format and Easel's own text format; and noise gradients.
 *
 * gradient_sample is the one function everything paints with: the Gradient tool's fill
 * (paint.h's fill_gradient_ex), Gradient Map adjustment layers and the editor's preview.
 *
 * Parts ported from PhotoCraft (https://github.com/storytold/photocraft), Copyright (c) 2026
 * ArtCraft Team and the PhotoCraft contributors, used under the MIT License (see Licenses.txt):
 * the ramp with midpoints (crates/compose/src/gradient_fill.rs) and the built-in presets
 * (crates/engine/src/presets/gradients.rs).
 */
#ifndef EASEL_GRADIENT_H
#define EASEL_GRADIENT_H

#include "util.h"

#define GRAD_MAXSTOPS 32
enum { GS_RGB, GS_FG, GS_BG };                  /* a colour stop's colour: its own, or the foreground or background colour */
typedef struct {
  double pos;                                   /* 0-1 */
  double mid;                                   /* 0.05-0.95: where, between this stop and the next, the colour is half way */
  int kind;                                     /* GS_ */
  u32 rgb;                                      /* 0xRRGGBB (GS_RGB) */
} GradColorStop;
typedef struct { double pos, mid, opacity; } GradOpacityStop;   /* opacity 0-1 */
typedef struct Gradient {
  char name[64];                                /* UTF-8 */
  int ncolor, nopacity;                         /* at least 1 each when sampled (none: black, opaque) */
  GradColorStop color[GRAD_MAXSTOPS];           /* kept in order of pos by gradient_sort */
  GradOpacityStop opacity[GRAD_MAXSTOPS];
  u32 fg, bg;                                   /* what GS_FG and GS_BG stops take (set before sampling) */
  int hsv;                                      /* blend colours round the hue circle instead of in RGB (GIMP's HSV segments) */
} Gradient;

/* a two-stop gradient from c0 to c1 (opaque), and Foreground to Background */
void   gradient_init(Gradient *g, const char *name, u32 c0, u32 c1);
/* the colour and alpha at t (0-1; outside is clamped) as straight RGBA */
void   gradient_sample(const Gradient *g, double t, u8 rgba[4]);
/* n samples from t = 0 to 1 into lut (n * 4 bytes): fast painting */
void   gradient_lut(const Gradient *g, u8 *lut, int n);
/* where u (0-1 across a segment) lands once its midpoint is m (PhotoCraft's midpoint_remap) */
double gradient_midpoint(double u, double m);
/* stops kept in order; added (sorted in; 0 if full) and deleted (one of each kind is kept) */
void   gradient_sort(Gradient *g);
int    gradient_add_color(Gradient *g, double pos, int kind, u32 rgb);
int    gradient_add_opacity(Gradient *g, double pos, double opacity);
int    gradient_del_color(Gradient *g, int i);
int    gradient_del_opacity(Gradient *g, int i);
void   gradient_reverse(Gradient *g);
/* a noise gradient: n random colour stops (seeded), roughness 0-100 how far each strays from the
   one before; with transparency random opacity stops too */
void   gradient_noise(Gradient *g, u32 seed, int n, int roughness, int transparency);

/* ------------------------------------------------------------------ the presets */
typedef struct { Gradient *g; int n, cap; } GradientList;
void   gradlist_init(GradientList *l);
void   gradlist_free(GradientList *l);
int    gradlist_add(GradientList *l, const Gradient *g);       /* a copy at the end; 0 if out of memory */
void   gradlist_remove(GradientList *l, int i);
int    gradlist_builtin(GradientList *l);                      /* Easel's defaults added; how many */

/* ------------------------------------------------------------------ files */
/* GIMP gradients (.ggr): segments of left, middle, right with RGBA colours either side; linear
   segments read exactly, curved, sine and sphere ones as linear with their middle; foreground and
   background colour types kept. Write: malloc'd text (*len), NULL if out of memory */
char  *gradient_write_ggr(const Gradient *g, size_t *len);
int    gradient_read_ggr(const char *text, size_t n, Gradient *g);        /* 0 if it isn't one */
/* Easel's own text file of one or more gradients:
     EaselGradients 1
     gradient "Name"
     color <pos> <mid> rgb RRGGBB | fg | bg
     opacity <pos> <mid> <opacity>
     end
   load adds what it reads to l and returns how many (0: not such a file) */
char  *gradlist_write(const GradientList *l, size_t *len);
int    gradlist_read(GradientList *l, const char *text, size_t n);
int    gradlist_save(const GradientList *l, const char *path);           /* by extension: .ggr writes the first only */
int    gradlist_load(GradientList *l, const char *path);                 /* .ggr or Easel's; how many added */

#endif
