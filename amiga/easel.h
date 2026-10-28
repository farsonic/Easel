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
} DocView;
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
  TL_PATTERN, TL_HISTORY, TL_REPLACE, TL_BGERASE, TL_MAGICERASE, TL_N
};
/* a tool's icon (theme.h): the first ones in TL_ order, 0.5's after the rest */
#define TOOL_ICON(t) ((t) <= TL_ZOOM ? IC_T_MOVE + (t) : IC_T_PATTERN + (t) - TL_PATTERN)
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
} Opts;
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
extern const char tool_keys[TL_N];
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
struct MUI_CustomClass *canvas_class(void);
void canvas_class_free(void);
/* the pointer's place in the image and the colour there, for the status bar ("" when outside) */
extern char canvas_info[96];
extern int crop_pending;               /* a crop box is up (Return applies it) */

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
void pens_free(void);
/* the nearest pen to an 0xRRGGBB on scr (ReleasePen it) */
LONG rgb_pen(struct Screen *scr, ULONG c);

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
int    ask_number(const char *title, const char *label, LONG *v, LONG min, LONG max);
/* the colour picker: 0xRRGGBB, or -1 if cancelled */
LONG   pick_color(const char *title, ULONG start);
/* the family names of the fonts the Text tool can use, and one by number */
int    font_count(void);
const char *font_name(int i);

/* dialogs.c */
void dlg_new(void);
void dlg_image_size(void);
void dlg_canvas_size(void);
void dlg_rotate(void);
void dlg_adjust(ULONG id);           /* an Image > Adjustments item */
void dlg_filter(ULONG id);           /* a Filter item */
void dlg_layer_props(void);
int  dlg_fill_layer(int make_kind);   /* make_kind: 0 solid, 1 gradient (a new fill layer); -1 the active one's settings */
void dlg_filter_again(void);         /* Filter > Last Filter */
ULONG adj_menu_id(int kind);         /* the Image > Adjustments item for an AK_ kind */
void dlg_fill(void);
void dlg_stroke(void);
void dlg_select(ULONG id);           /* Select > Modify ..., Color Range */
void dlg_export(int fmt);            /* JPEG quality, ILBM colours... then saved */
int  dlg_new_values(LONG *w, LONG *h, LONG *dpi, int *bg);   /* (main.c's New) */

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
