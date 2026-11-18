/*
 * easel.h - declarations shared by Easel's Amiga front end (MUI): the documents open, the tools
 * and their settings, the custom classes (canvas, toolbox, options bar, panels), the dialogs' look.
 *
 * For newcomers to MUI: each custom class here is a BOOPSI class made with MUI_CreateCustomClass.
 * Its attributes (MUIA_) are tags read with get() and written with set(), and "notify" means
 * other objects can watch the attribute with MUIM_Notify. Its methods (MUIM_) are messages sent
 * with DoMethod(obj, MUIM_..., ...). All of Easel's tags sit in the TAG_USER range at QTAG.
 */
#ifndef EASEL_AMIGA_H
#define EASEL_AMIGA_H

#include <stdio.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/classusr.h>
#include <libraries/mui.h>
#include "../core/doc.h"
#include "../core/paint.h"
#include "../core/adjust.h"
#include "../core/fonts.h"
#include "../core/gradient.h"
#include "../core/pattern.h"
#include "../core/brush.h"

extern struct Library *MUIMasterBase, *CyberGfxBase;

#define QTAG (TAG_USER | 0x45530000UL)

/* ------------------------------------------------------------------ the documents (main.c) */
typedef struct {
  Doc *doc;
  char path[512];                     /* where it was loaded from or saved to ("" new) */
  char name[108];                     /* the tab's name (Latin-1) */
  int fmt;                            /* FMT_ it was loaded from, for Save */
  double zoom;                        /* 1.0 = 100% */
  LONG sx, sy;                        /* the scroll: the zoomed image's pixel at the canvas's top left */
  int placed;                         /* zoom and scroll have been set for the window */
  Snapshot *snap;                     /* 0.5: what the History Brush paints back (taken at opening) */
  char recov[160]; ULONG asig;        /* 0.6: its autosaved copy in PROGDIR:Recovery, and the state it holds */
  /* 0.8: the Color Sampler tool's points (image pixels), shown on the canvas and in the Info panel */
  LONG samp_x[4], samp_y[4]; int nsamp;
  /* 0.8: the History panel's snapshots: whole copies of the picture as it was, and their names */
  Doc *hsnap[8]; char hsnap_name[8][24]; int nhsnap;
  double vrot;                        /* 1.2: Rotate View: the view's turn, degrees clockwise */
} DocView;
#define MAXHSNAP 8
extern int hist_pick;                 /* (panels.c) the snapshot clicked last in the History panel, -1 none */
#define MAXDOCS 12
extern DocView docs[MAXDOCS];
extern int ndocs, curdoc;
DocView *cur_view(void);              /* NULL when nothing is open */
Doc *cur_doc(void);

/* ------------------------------------------------------------------ tools and their settings */
enum {
  TL_MOVE, TL_MARQUEE, TL_EMARQUEE, TL_LASSO, TL_PLASSO, TL_WAND, TL_CROP, TL_EYEDROPPER,
  TL_CLONE, TL_BRUSH, TL_PENCIL, TL_ERASER, TL_BUCKET, TL_GRADIENT, TL_BLUR, TL_SHARPEN, TL_SMUDGE,
  TL_DODGE, TL_BURN, TL_SPONGE, TL_TEXT, TL_RECT, TL_ELLIPSE, TL_LINE, TL_HAND, TL_ZOOM,
  /* 0.5 */
  TL_PATTERN, TL_HISTORY, TL_REPLACE, TL_BGERASE, TL_MAGICERASE,
  /* 0.7 */
  TL_MAGLASSO, TL_RULER,
  /* 0.8 */
  TL_SAMPLER,
  /* 1.0: retouching (core/retouch.h) */
  TL_SPOTHEAL, TL_HEAL, TL_PATCH, TL_REDEYE,
  /* 0.9: type, pens and paths, shapes (TL_TEXT is the Horizontal Type tool; TL_RECT, TL_ELLIPSE
     and TL_LINE make shape layers too) */
  TL_TYPEMASK, TL_VTEXT, TL_PEN, TL_ADDANCHOR, TL_DELANCHOR, TL_CONVERT, TL_PATHSEL, TL_DIRECTSEL,
  TL_ROUNDRECT, TL_POLYGON, TL_CUSTOMSHAPE,
  /* 1.2 (tools2.c) */
  TL_QUICKSEL, TL_OBJSEL, TL_PCROP, TL_SLICE, TL_SLICESEL, TL_FRAME, TL_NOTE, TL_COUNT, TL_MIXER, TL_ARTHIST,
  TL_REMOVE, TL_CAMOVE, TL_ROTVIEW, TL_ARTBOARD,
  TL_N
};
/* a tool's icon (theme.h): the first ones in TL_ order, 0.5's (and 0.7's, 0.8's and 1.0's) after the
   rest, 0.9's after those */
#define TOOL_ICON(t) ((t) >= TL_QUICKSEL ? IC_T_QUICKSEL + (t) - TL_QUICKSEL : (t) >= TL_TYPEMASK ? IC_T_TYPEMASK + (t) - TL_TYPEMASK : (t) <= TL_ZOOM ? IC_T_MOVE + (t) : IC_T_PATTERN + (t) - TL_PATTERN)
/* 0.9: the tools that make shapes (as shape layers or pixels), and those that work on paths */
#define SHAPE_TOOL(t) ((t) == TL_RECT || (t) == TL_ELLIPSE || (t) == TL_LINE || (t) == TL_ROUNDRECT || (t) == TL_POLYGON || (t) == TL_CUSTOMSHAPE)
#define PATH_TOOL(t) ((t) >= TL_PEN && (t) <= TL_DIRECTSEL)
#define TYPE_TOOL(t) ((t) == TL_TEXT || (t) == TL_VTEXT || (t) == TL_TYPEMASK)
typedef struct {
  /* brush-like tools (brush, pencil, eraser, clone, blur ... sponge): each its own */
  int size[TL_N], hardness[TL_N], opacity[TL_N], flow[TL_N], mode[TL_N], strength[TL_N];
  int sel_op;                         /* SEL_ from the options bar (shift/alt add and subtract too) */
  int feather;                        /* marquee and lasso */
  int tolerance, contiguous, all_layers, antialias;     /* wand and bucket */
  int fill_opacity;                   /* bucket */
  int grad_kind, grad_reverse, grad_fade;               /* gradient: GR_, reversed, foreground to transparent */
  int shape_fill_radius, line_width;  /* rectangle's corner radius, line width */
  int text_size, text_align, text_font;                /* text: pixels, TA_, the family's number */
  int sample;                         /* eyedropper: 0 point, 1 3x3, 2 5x5; all layers or one (all_layers) */
  int auto_select;                    /* move: the layer under the pointer chosen */
  int range;                          /* dodge/burn: 0 shadows, 1 midtones, 2 highlights; sponge 0 desaturate, 1 saturate */
  /* 0.5 */
  int fill_source;                    /* bucket: 0 foreground, 1 pattern */
  int grad_dither;                    /* gradient: dithered */
  int pattern;                        /* the pattern chosen (pattern stamp, bucket, Edit > Fill) */
  int pattern_aligned;                /* pattern stamp */
  int replace_mode, limits, sampling; /* colour replacement and background eraser: RPL_, LIM_, SMP_ */
  int protect_fg;                     /* background eraser */
  int grad_preset;                    /* gradient: the preset (grad_names), the last one "Custom" (cur_grad) */
  /* 0.7 */
  int sel_antialias;                  /* lassos, elliptical marquee, wand: Anti-alias */
  int mq_style, mq_w, mq_h;           /* marquee: MQS_, and the ratio or size */
  int ml_width, ml_contrast, ml_freq; /* magnetic lasso: pixels, per cent, 0-100 */
  int crop_preset, crop_swap;         /* crop: crop_presets[], width and height swapped */
  int ruler_image;                    /* ruler: Straighten turns the whole picture (and crops) */
  /* 1.0 */
  int redeye_pupil, redeye_darken;    /* red eye: per cent */
  /* 0.9 */
  int text_style, text_aa;            /* type: 0 regular, 1 bold, 2 italic, 3 bold italic; TAA_ (text_align: TA_ with justify) */
  int shape_mode;                     /* shapes: 0 a shape layer, 1 pixels */
  int shape_fill, shape_stroke;       /* filled with the foreground colour; stroked */
  int stroke_width, stroke_align;     /* pixels; SA_ */
  int stroke_color;                   /* 0xRRGGBB */
  int poly_sides, poly_star, poly_indent;   /* polygon: 3-100 sides, a star, its indent per cent */
  int arrow_start, arrow_end;         /* line: arrowheads */
  int round_radius;                   /* rounded rectangle: corner radius */
  /* 1.1 (core/toolopts.h) */
  int protect_tones, vibrance, sponge_mode;   /* dodge and burn (range is their TR_ less one); sponge: vibrance, 0 desaturate 1 saturate */
  int clone_sample, clone_aligned;    /* clone stamp, healing brush: 0 current layer, 1 current & below, 2 all; Aligned */
  int pick_from;                      /* eyedropper: the same three (sample is its pick_sizes[] index) */
  int erase_history;                  /* eraser: Erase to History */
  int finger_paint, protect_detail;   /* smudge; sharpen */
  int move_group, move_bounds;        /* move: Auto-Select picks the group; the layer's bounding box shown */
  int crop_overlay, crop_straighten;  /* crop: CO_; a drag draws the line to straighten by */
  int zoom_out;                       /* zoom: a click zooms out (Alt-click in) */
  int grad_palrange;                  /* gradient: an indexed picture's palette range (DPaint's range fill) */
  /* 1.2 (tools2.c) */
  int qs_enhance, obj_hard;           /* Quick Selection: Auto-Enhance; Object Selection: Hard Edge */
  int pcrop_w, pcrop_h;               /* Perspective Crop: the size (0: the perspective's own) */
  int frame_kind, frame_stroke;       /* Frame: 0 rectangle, 1 ellipse; stroke width */
  int mix_wet, mix_load, mix_mix, mix_autoload, mix_autoclean, mix_all;   /* Mixer Brush */
  int ah_style, ah_area, ah_tolerance;                    /* Art History Brush */
  int cam_extend, cam_structure, cam_color;               /* Content-Aware Move */
  int patch_mode, patch_transparent, patch_structure, patch_color;   /* Patch: 0 source, 1 destination, 2 content-aware */
  int heal_source, heal_diffusion, spot_type;             /* Healing Brush: sampled/pattern, 1-7; Spot Healing: SPOT_ */
  int crop_angle;                     /* Crop: the box's turn (degrees), a drag outside it turns it */
  /* r3-image */
  int custom_shape;                   /* Custom Shape: 0 the active path, 1 + k shape k of the library (core/vecx.h) */
} Opts;
extern const int sample_layers[3];
extern int extras_hidden;              /* (1.1) Ctrl-H: the selection's outline (and the guides and grid) not drawn */
#define RETOUCH_MODES 7               /* (1.1) Blur, Sharpen, Smudge: Normal, Darken, Lighten, Hue, Saturation, Color, Luminosity */
extern const int retouch_bm[RETOUCH_MODES];   /* (main.c) those as BM_ */    /* (1.1) the options bar's Current Layer, Current & Below, All Layers as SAMPLE_ */
/* 0.7: the marquee's styles in its options bar (the first three are selx.h's MQ_) */
enum { MQS_NORMAL, MQS_RATIO, MQS_SIZE, MQS_ROW, MQS_COL };
/* 0.5: the gradient and pattern the tools use (main.c) */
#define MAXNAMES 64
extern Gradient cur_grad;             /* the edited ("Custom") gradient */
extern PatternList patterns;          /* the patterns: built-in, defined and loaded */
extern const char *grad_names[MAXNAMES + 1], *pat_names[MAXNAMES];
extern int ngrad_names, npat_names;
void names_refresh(void);             /* grad_names and pat_names made again from the lists */
const Gradient *tool_gradient(void);  /* the gradient the Gradient tool draws */
const Pattern *tool_pattern(void);    /* the pattern chosen, or NULL */
extern Opts opt;
extern int tool;                      /* TL_ */
extern const char *const tool_names[TL_N];
extern char tool_keys[TL_N];           /* (0.8: Edit > Keyboard Shortcuts changes them) */
void set_tool(int t);                 /* (main.c) the tool chosen: canvas, toolbox and options bar told */
int  tool_for_key(int c);             /* the tool a letter (upper case) chooses, -1 none */
Brush tool_brush(int t);              /* the settings as a core Brush */

/* ------------------------------------------------------------------ the canvas (canvas.c) */
#define MUIA_Canvas_VProp   (QTAG + 1)     /* (init) the flat scroll bars it keeps up to date */
#define MUIA_Canvas_HProp   (QTAG + 2)
#define MUIA_Canvas_Top     (QTAG + 3)     /* (set) from the scroll bars */
#define MUIA_Canvas_Left    (QTAG + 4)
#define MUIA_Canvas_Changed (QTAG + 5)     /* (notify) a tool changed the document */
#define MUIA_Canvas_Pointer (QTAG + 6)     /* (notify) the pointer moved over the image: the status bar */
#define MUIA_Canvas_Picked  (QTAG + 7)     /* (notify) the eyedropper changed a colour */
#define MUIA_Canvas_Zoomed  (QTAG + 8)     /* (notify) the zoom changed */
#define MUIA_Canvas_Tool    (QTAG + 9)     /* (notify) a key chose another tool (get: which) */
#define MUIM_Canvas_Refresh (QTAG + 0x100) /* the document is another or changed throughout: all redrawn */
#define MUIM_Canvas_Update  (QTAG + 0x101) /* struct MUIP_Canvas_Update: a part of the image changed */
struct MUIP_Canvas_Update { ULONG MethodID; LONG x0, y0, x1, y1; };
#define MUIM_Canvas_Zoom    (QTAG + 0x102) /* struct MUIP_Canvas_Zoom */
struct MUIP_Canvas_Zoom { ULONG MethodID; LONG permille; LONG how; };  /* how: 0 as given, 1 in a step, -1 out, 2 fit, 3 fill */
#define MUIM_Canvas_Tick    (QTAG + 0x103) /* (timer) the selection's outline marches */
#define MUIM_Canvas_Commit  (QTAG + 0x104) /* a crop or polygon being made finished (Return) */
#define MUIM_Canvas_Cancel  (QTAG + 0x105) /* and dropped (Esc) */
#define MUIM_Canvas_Tool    (QTAG + 0x106) /* the tool changed: what it was doing put away */
#define MUIM_Canvas_StrokeTick (QTAG + 0x107) /* (timer, 0.5) airbrush build-up and smoothing while the button is held */
#define MUIM_Canvas_ScrollTo (QTAG + 0x108) /* (0.6) struct MUIP_Canvas_ScrollTo: the view centred on an image point */
struct MUIP_Canvas_ScrollTo { ULONG MethodID; LONG x, y; };
#define MUIM_Canvas_Transform (QTAG + 0x109) /* (0.7) struct MUIP_Canvas_Transform: Free Transform's handles put up */
struct MUIP_Canvas_Transform { ULONG MethodID; LONG mode; LONG selection; };   /* mode: XM_; selection: Transform Selection */
enum { XM_FREE, XM_SCALE, XM_ROTATE, XM_SKEW, XM_DISTORT, XM_PERSPECTIVE };
/* 0.9: the Type tool's work, from the options bar, menus and panels: struct MUIP_Canvas_TypeCmd */
#define MUIM_Canvas_TypeCmd (QTAG + 0x10A)
struct MUIP_Canvas_TypeCmd { ULONG MethodID; LONG code; LONG arg; };
enum { TC_OPTS, TC_CHARACTER, TC_PARAGRAPH, TC_WARP, TC_EDIT, TC_COMMIT, TC_ORIENT, TC_COLOR,
       TC_OT };                       /* 1.2: an OpenType feature (arg its TO_ bit) turned on or off */
extern int type_editing;               /* text is being typed on the canvas */
/* 0.9: the paths changed from outside the canvas (the Paths panel): drawn again */
#define MUIM_Canvas_Paths   (QTAG + 0x10B)
/* 0.6: where the pointer is over the picture, and the colour there (for the Info panel) */
typedef struct { LONG x, y; int in, has_rgb; int rgba[4]; } CanvasPoint;
extern CanvasPoint canvas_pt;
/* the Navigator and Info panels (panels2.c) */
#define MUIM_Navigator_Refresh (QTAG + 0x340)   /* struct MUIP_Navigator_Refresh: the view moved, or (picture) the picture changed */
struct MUIP_Navigator_Refresh { ULONG MethodID; LONG picture; };
#define MUIM_Info_Refresh (QTAG + 0x341)        /* struct MUIP_Info_Refresh: the pointer moved, or (picture) the picture changed */
struct MUIP_Info_Refresh { ULONG MethodID; LONG picture; };
struct MUI_CustomClass *navigator_class(void);
struct MUI_CustomClass *info_class(void);
void panels2_free(void);
struct MUI_CustomClass *canvas_class(void);
void canvas_class_free(void);
/* the pointer's place in the image and the colour there, for the status bar ("" when outside) */
extern char canvas_info[96];
extern int crop_pending;               /* a crop box is up (Return applies it) */
extern int pcrop_pending;              /* 1.2: Perspective Crop's corners are out */
/* 0.7: Free Transform's handles are up (the options bar shows its numbers and Commit/Cancel) */
extern int xform_pending;
extern char xform_info[80];            /* W%, H% and the angle, for the options bar */
/* the Ruler's line (image pixels) while there is one, and what it measures for the options bar */
extern int ruler_on;
extern double ruler_xy[4];
extern char ruler_info[96];
/* the Channels panel (panels2.c): a row for each alpha channel, the buttons along the bottom.
   What it asks for comes to main.c as ID_CHANLOAD (chan_pick, chan_op), ID_CHANRENAME, ID_SAVESEL,
   ID_LOADSEL and ID_CHANDEL */
#define MUIM_Channels_Refresh (QTAG + 0x342)
struct MUI_CustomClass *channels_class(void);
extern int chan_pick, chan_op;
/* 0.9: the Paths panel (paths.c): the Work Path and the saved paths, each with a thumbnail; the
   buttons along the bottom. What it asks for comes to main.c as the ID_PATH... commands about
   path_pick (-1 the work path, k saved path k) */
#define MUIM_Paths_Refresh (QTAG + 0x343)
struct MUI_CustomClass *paths_class(void);
extern int path_pick;
struct Path *active_path(Doc *d);      /* the path the Paths panel has chosen, NULL for none */
void paths_free(void);

/* ------------------------------------------------------------------ toolbox, options bar (toolbox.c) */
#define MUIM_Tools_Refresh  (QTAG + 0x200) /* the tool or the colours changed */
#define MUIA_Tools_Pick     (QTAG + 0x201) /* (notify) a tool was clicked: its TL_ */
#define MUIA_Tools_Color    (QTAG + 0x202) /* (notify) 1 the foreground swatch clicked, 2 the background */
struct MUI_CustomClass *toolbox_class(void);
#define MUIM_Opts_Refresh   (QTAG + 0x210) /* the tool changed: its settings shown */
struct MUI_CustomClass *optbar_class(void);
void toolbox_free(void);

/* ------------------------------------------------------------------ panels (panels.c) */
/* the Color panel: a saturation and brightness square, a hue strip, the swatches */
#define MUIM_Color_Refresh  (QTAG + 0x300)
#define MUIA_Color_Changed  (QTAG + 0x301) /* (notify) the foreground or background colour was changed */
struct MUI_CustomClass *color_class(void);
/* the Layers panel: blend mode and opacity, then a row for each layer (top first), the buttons */
#define MUIM_Layers_Refresh (QTAG + 0x310)
#define MUIA_Layers_Changed (QTAG + 0x311) /* (notify) the panel changed the document (props, order, active) */
#define MUIA_Layers_Command (QTAG + 0x312) /* (get) the button pressed (ID_), after ID_LAYERCMD */
#define MUIA_Layers_AdjPick (QTAG + 0x313) /* (set by its list) the adjustment chosen for a new adjustment layer */
struct MUI_CustomClass *layers_class(void);
/* the History panel: the undo steps, the one in effect lit; a click goes back to it */
#define MUIM_History_Refresh (QTAG + 0x320)
#define MUIA_History_Changed (QTAG + 0x321)
struct MUI_CustomClass *history_class(void);
/* a panel's title bar with its tabs (the panels themselves are switched by main.c) */
#define MUIA_Head_Labels    (QTAG + 0x330) /* (init) const char *const *, NULL ended */
#define MUIA_Head_Active    (QTAG + 0x331) /* the tab shown (notify) */
struct MUI_CustomClass *head_class(void);
/* the documents' tabs above the canvas */
#define MUIM_DocTabs_Refresh (QTAG + 0x340)
#define MUIA_DocTabs_Pick   (QTAG + 0x341) /* (notify) a tab clicked: its document */
#define MUIA_DocTabs_Close  (QTAG + 0x342) /* (notify) a tab's cross clicked */
struct MUI_CustomClass *doctabs_class(void);
/* the status bar: zoom, the document's size and mode, what the pointer is over */
#define MUIM_Status_Refresh (QTAG + 0x350)
struct MUI_CustomClass *status_class(void);
#define MUIA_FlatScroll_Horiz (QTAG + 0x360)  /* (init) across instead of up and down */
#define MUIA_Popup_Chosen   (QTAG + 0x370) /* (set by popup_list) the entry chosen */
struct MUI_CustomClass *flatscroll_class(void);
void panels_free(void);

/* ------------------------------------------------------------------ a drop-down list (widgets.c) */
/* labels shown in a borderless window below (x, y on the screen), w wide at least, cur lit; the
   choice is set on owner as attribute attr (MUIM_Set pushed, so after the window has gone) */
void popup_list(Object *owner, LONG x, LONG y, LONG w, const char *const *labels, int n, int cur, ULONG attr);
void popup_free(void);
/* a colour drawn as a box at x0,y0-x1,y1: on true-colour screens exact, else the nearest pen */
void draw_rgb_box(struct RastPort *rp, struct Screen *scr, LONG x0, LONG y0, LONG x1, LONG y1, ULONG rgb);
/* RGB rows (3 bytes a pixel, mod bytes a row) drawn at x, y: WritePixelArray on true colour,
   else dithered to a cube of pens obtained from scr on first use (and kept till pens_free) */
void draw_rgb(struct RastPort *rp, struct Screen *scr, const UBYTE *rgb, LONG mod, LONG x, LONG y, LONG w, LONG h);
int  truecolor(struct Screen *scr);
extern int display_dither;            /* (1.2) palette screens: 0 ordered dither, 1 the nearest pens, 2 error diffusion */
void pens_free(void);
/* the nearest pen to an 0xRRGGBB on scr (ReleasePen it) */
LONG rgb_pen(struct Screen *scr, ULONG c);

/* ------------------------------------------------------------------ 0.8: preferences (prefs.c) */
enum { SF_ORA, SF_PNG, SF_JPEG, SF_TIFF, SF_PSD, SF_ILBM, SF_N };               /* the format new pictures save as */
enum { UN_PIXELS, UN_INCHES, UN_CM, UN_MM, UN_PERCENT, UN_N };                   /* rulers' and Info's units */
enum { SM_WINDOW, SM_SCREEN, SM_FULL };                                           /* View > Screen Mode */
typedef struct {
  int tips, large_text;               /* help bubbles; the interface's large text */
  int cursor;                         /* painting cursor: 0 the brush's outline, 1 a crosshair, 2 both */
  int caps_precise, xform_numbers;    /* Caps Lock gives the crosshair; Free Transform's numbers shown */
  int save_fmt, jpeg_quality, ask_flatten, nrecent;    /* SF_, 1-100, before saving layers flat, 0-10 */
  char recovery[256];                 /* the autosave drawer */
  int units, dpi;                     /* UN_, a new picture's resolution */
  ULONG guide_color, grid_color;      /* 0xRRGGBB */
  int grid_spacing, grid_sub;         /* a new picture's grid */
  int scratch_on, scratch_mb; char scratch[256];        /* undo steps spilled to a drawer, up to so much */
  int undo_mb, undo_levels;           /* undo's memory and states */
  int screen_mode; ULONG screen_id;   /* SM_, and the ModeID of Easel's own screen (0: Workbench's) */
  int skip_place_xf;                  /* 1.1: Place as Layer doesn't go on into Free Transform */
  int language;                       /* 1.1: LANG_ (lang.h), or -1: the one Locale prefs prefer */
  /* 1.2: Transparency (core/viewx.h: the checkerboard's size 0-3 and CHK_ scheme, the custom colours);
     grid and guide line styles (LS_); Units & Rulers' screen resolution, column and gutter widths
     (pixels) and point size (0 PostScript, 72 a inch; 1 traditional, 72.27) */
  int checker_size, checker_scheme; ULONG checker_c1, checker_c2;
  int grid_style, guide_style;
  int screen_dpi, column_w, gutter_w, point_kind;
  /* round 3: the Export page (Quick Export's format QX_, where it writes QW_ and the drawer, its
     metadata EXD_ and sRGB; lossy WebP's quality), Place's smart objects, a beep when a long job ends */
  int qx_fmt, qx_where, qx_meta, qx_srgb, webp_quality;
  char qx_dir[256];
  int place_smart, beep_done;
  char monitor_icc[256];              /* round 3: the monitor's ICC profile file ("": none, sRGB) */
} Prefs;
enum { QX_PNG, QX_JPEG, QX_GIF, QX_WEBP, QX_TIFF, QX_N };
enum { QW_BESIDE, QW_LAST, QW_DRAWER, QW_ASK };
extern Prefs prefs;
extern int pref_autosave, pref_icons;
extern const char *const unit_names[UN_N + 1];
/* 0.8: Edit > Keyboard Shortcuts (keys.c): the commands gathered from the menu table (before the
   window is made) and Easel.keys read; the command an extra key pressed on the canvas gives (0 none) */
struct NewMenu;
void  keys_init(struct NewMenu *menus);
ULONG keys_extra(int c, UWORD code, UWORD qual);
void  dlg_keys(void);
void prefs_defaults(void);
int  prefs_line(const char *line);    /* a line of Easel.prefs taken, if it is one of prefs' */
void prefs_write(FILE *f);
void prefs_apply(void);               /* the settings handed to the core and the theme */
int  dlg_preferences(int *reopen);    /* Edit > Preferences: 1 if OK (reopen: the window must open again) */
/* a length of px pixels in the units chosen (length: the side it is measured along, for per cent) */
void unit_text(char *out, double px, long length, int dpi, int name);

/* ------------------------------------------------------------------ main.c helpers */
extern Object *app, *win, *canvas, *toolbox, *optbar, *colorpanel, *layerpanel, *historypanel, *statusbar, *doctabs;
extern FontSet fonts;
extern int pref_dark;
extern char pub_screen[64];
/* after a change to the document: canvas, panels and menus brought up to date */
void refresh_all(void);
void refresh_panels(void);
/* a requester (gadgets "OK|Cancel"; returns the button, 1 the first, 0 the last) */
int    ask(const char *gadgets, const char *fmt, const char *arg);
ULONG  run_modal(Object *w, const ULONG *ends, void (*each)(ULONG id));
Object *dialog_window(const char *title, Object *contents, const char *oklabel);
int    ask_string(const char *title, const char *label, char *buf, int cap);
void   redraw_window(void);              /* (main.c) the main window drawn again throughout */
int    ask_number(const char *title, const char *label, LONG *v, LONG min, LONG max);
/* the colour picker: 0xRRGGBB, or -1 if cancelled */
LONG   pick_color(const char *title, ULONG start);
/* the family names of the fonts the Text tool can use, and one by number; 0.9: as the font list
   shows it (Amiga bitmap fonts marked), and the number of a family (-1 none) */
int    font_count(void);
const char *font_name(int i);
const char *font_label(int i);
int    font_find(const char *family);

/* 0.9: type and shape dialogs (typedlg.c), each changing the settings given; 1 if OK. Character:
   the style of the text from..to (all of it when from == to) and the leading */
struct TextParams; struct ShapeParams;
int dlg_character(struct TextParams *tp, int from, int to);
int dlg_paragraph(struct TextParams *tp);
int dlg_warp(struct TextParams *tp);
int dlg_shape(struct ShapeParams *sp);

/* dialogs.c */
void dlg_new(void);
void dlg_image_size(void);
void dlg_canvas_size(void);
void dlg_rotate(void);
void dlg_adjust(ULONG id);           /* an Image > Adjustments item */
void dlg_filter(ULONG id);           /* a Filter item */
void dlg_layer_props(void);
int  dlg_fill_layer(int make_kind);   /* make_kind: 0 solid, 1 gradient (a new fill layer); -1 the active one's settings */
void dlg_fade(void);                  /* Edit > Fade (0.6) */
void dlg_file_info(Doc *d);           /* File > File Info (0.6) */
void dlg_filter_again(void);         /* Filter > Last Filter */
ULONG adj_menu_id(int kind);         /* the Image > Adjustments item for an AK_ kind */
void dlg_fill(void);
void dlg_stroke(void);
void dlg_select(ULONG id);           /* Select > Modify ..., Color Range */
void dlg_color_range(void);          /* (0.7) Select > Color Range */
void dlg_save_selection(void);       /* (0.7) Select > Save Selection */
void dlg_load_selection(void);       /* (0.7) Select > Load Selection */
void dlg_export(int fmt);            /* JPEG quality, ILBM colours... then saved */
int  dlg_new_values(LONG *w, LONG *h, LONG *dpi, int *bg);   /* (main.c's New) */

/* ------------------------------------------------------------------ 0.8: long operations (jobs.c) */
/* a job begun on d (or NULL): while it runs, a slow operation in the core shows a progress window
   with an Abort button once it has taken half a second. job_end: 1 if Abort was pressed, in which
   case the undo step the job made was taken back. Jobs nest */
void job_begin(Doc *d, const char *name);
int  job_end(void);
int  job_running(void);
void jobs_free(void);
void job_timer_reset(void);            /* (after a dialog) the half second counted afresh */
extern int jobs_window;                /* the progress window may open (not while starting up) */

/* ------------------------------------------------------------------ shared bits */
/* the public screen Easel's windows open on ("": the default one); QWindowObject is a window on it */
#define QDLG (ULONG)(theme_dialog_font() ? MUIA_Font : TAG_IGNORE), (ULONG)theme_dialog_font(), \
             (ULONG)(theme_dialog_back() ? MUIA_Background : TAG_IGNORE), (ULONG)theme_dialog_back()
struct TextFont *theme_dialog_font(void);
const char *theme_dialog_back(void);
/* (qbutton.c) a dialog button, a label and a section title drawn in Easel's theme */
Object *QButton(const char *label);
Object *QLabel(const char *label, ULONG flags);
Object *QTitleObject(const char *t);
struct MUI_CustomClass *qbutton_class(void);
void qbutton_class_free(void);
#define QWindowObject WindowObject, (ULONG)(pub_screen[0] ? MUIA_Window_PublicScreen : TAG_IGNORE), (ULONG)pub_screen

/* debugging log (DEBUG=file), or NULL */
extern FILE *qlog;
#define LOG(...) do { if (qlog) { fprintf(qlog, __VA_ARGS__); fputc('\n', qlog); fflush(qlog); } } while (0)

#endif
