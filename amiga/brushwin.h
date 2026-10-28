/*
 * brushwin.h - Easel 0.5's brush windows (MUI, Easel's own dark theme):
 *
 *   the Brushes panel     the presets as previews of their tips; a click picks one. Under them:
 *                         New (Define Brush Preset from the selection), Delete, Load and Save
 *                         (Easel's .brushes text files; Load also reads Photoshop .abr);
 *   Brush Settings        a window of sections, PhotoCraft's: Brush Tip Shape, Shape Dynamics,
 *                         Scattering, Color Dynamics, Transfer, Smoothing, Symmetry, Airbrush,
 *                         and the painting tools' own options;
 *   DPaint Brush          the custom brush: pick up, its mode (Matte, Color, Replace, Smear, Shade,
 *                         Blend, Cycle), flip, rotate, scale, outline, load and save as ILBM.
 *
 * WIRING (main.c):
 *   - brushwin_init() once after the application exists; brushwin_free() at the end (after the
 *     windows are gone with the application), then brushwin_classes_free().
 *   - The panel: Child, brushwin_panel()  (a group: the previews and their scroll bar).
 *   - The windows: brushwin_settings_window() and brushwin_dpaint_window() make them (once) and
 *     add them to the application; open with set(w, MUIA_Window_Open, TRUE).
 *   - tool_brush(): after filling in size, hardness, opacity, flow and blend from Opts, call
 *     brushwin_apply(&b) to add everything these windows set (tip, angle, roundness, spacing,
 *     dynamics, symmetry, the DPaint brush, the tool options).
 *   - The callbacks in brushui (set them after brushwin_init):
 *       picked(b)   a preset was picked: copy b->size, hardness, opacity, flow (and blend) into Opts;
 *       changed()   a setting changed: redraw the options bar and the brush outline;
 *       request(r)  the windows need the canvas: BWR_DEFINE (Define Brush Preset: call
 *                   brushwin_define(doc)), BWR_PICKUP (let the user drag a box on the canvas, then
 *                   brushwin_pickup(doc, box, 0)), BWR_PICKUP_SEL (brushwin_pickup(doc, selbox, 1)).
 *   - Strokes: stroke_wants_tick(&b) says whether a timer is needed while the button is down;
 *     call stroke_tick(stroke, &changed) every stroke_tick_ms(&b) ms (MUIM_Application_AddInputHandler
 *     with MUIIHNF_TIMER, as the canvas's marching ants do) and redraw *changed. Pass tablet
 *     pressure (0-1, TABLETA_Pressure / the tablet's range) to stroke_begin_p and stroke_to_p.
 */
#ifndef EASEL_BRUSHWIN_H
#define EASEL_BRUSHWIN_H

#include "easel.h"
#include "../core/preset.h"

#define MUIM_BrushPanel_Refresh (QTAG + 0x1500)   /* the presets changed: previews made again */
#define MUIA_BrushPanel_First   (QTAG + 0x1501)   /* (set, from its scroll bar) the first row shown */
#define MUIM_Sheet_Refresh      (QTAG + 0x1510)   /* a settings sheet shows its values again */
#define MUIA_Sheet_First        (QTAG + 0x1511)   /* (set, from its scroll bar) pixels scrolled */
#define MUIA_Sheet_Kind         (QTAG + 0x1512)   /* (init) SHEET_SETTINGS or SHEET_DPAINT */
#define MUIA_Sheet_Scroll       (QTAG + 0x1513)   /* (init) its flat scroll bar */

enum { SHEET_SETTINGS, SHEET_DPAINT };
enum { BWR_NONE, BWR_DEFINE, BWR_PICKUP, BWR_PICKUP_SEL };

typedef struct {
  Brush b;                            /* the windows' settings: brushwin_apply copies them into a tool's brush */
  PresetList presets;                 /* the Brushes panel's presets */
  int current;                        /* the preset picked last, -1 none */
  CustomBrush *custom;                /* the DPaint brush (owned), or NULL */
  int use_custom;                     /* the brush tools paint with it */
  int pick_merged, pick_key;          /* pick up from everything seen; the background colour transparent */
  char path[512];                     /* the last file loaded or saved */
  void (*picked)(const Brush *b);
  void (*changed)(void);
  void (*request)(int what);
} BrushUI;
extern BrushUI brushui;

void   brushwin_init(void);
void   brushwin_free(void);
void   brushwin_classes_free(void);
/* the windows' settings laid on a brush made from the options bar */
void   brushwin_apply(Brush *b);
/* the windows and the panel shown afresh (after the settings were changed from outside) */
void   brushwin_refresh(void);
Object *brushwin_panel(void);
Object *brushwin_settings_window(void);
Object *brushwin_dpaint_window(void);
/* Define Brush Preset from the selection (or the whole layer); 0 if nothing paints there */
int    brushwin_define(Doc *d);
/* a DPaint brush picked up from box b (with use_sel, the selection's pixels in it); 0 if empty */
int    brushwin_pickup(Doc *d, Box b, int use_sel);

/* the ASL file requester (shared by the 0.5 windows): path in and out; 1 if a file was chosen */
int    ui_file_request(int save, const char *title, char *path, size_t cap, const char *pattern);

#endif
