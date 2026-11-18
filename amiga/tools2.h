/*
 * tools2.h - Easel 1.2's new tools' shared pieces on the Amiga side: the Mixer Brush's paint (kept
 * from stroke to stroke), pen tablets (an input handler reading Intuition's tablet events), the
 * View menu's Pixel Aspect Ratio Correction, and the menu commands of the gap work (tools2.c).
 */
#ifndef EASEL_TOOLS2_H
#define EASEL_TOOLS2_H
#include "../core/brushx.h"
#include "../core/toolsx.h"

extern MixerState tool_mixer;         /* the Mixer Brush's reservoir and pickup */
extern int view_aspect;               /* (canvas.c) the canvas shows the pixels' aspect */
extern int slice_pick;                /* (canvas.c) the slice the Slice Select tool picked, -1 none */
/* animation frames' onion skins (Window > Animation): on, frames before and after, opacity, the frame
   shown; the canvas composites through view_composite */
extern int onion_on, onion_before, onion_after, onion_opacity, onion_frame;
void view_composite(Doc *d, Box r, u8 *out, long stride);

/* tablets: an input handler at priority 51 (before Intuition) watching IECLASS_NEWPOINTERPOS events of
   subclass IESUBCLASS_NEWTABLET; nothing changes for a mouse. tablet_open returns 0 if input.device
   couldn't be opened */
int    tablet_open(void);
void   tablet_close(void);
/* the pen's pressure now (0-1, after the Preferences' curve), and its tilt; 1 and 0 when no tablet
   event has come for a second (a mouse) */
double tablet_pressure(double *tilt_x, double *tilt_y);
extern int tablet_curve_pc, tablet_floor_pc;   /* Preferences: the pressure curve, the floor */
extern int tablet_events;             /* how many tablet events have been seen (the Info panel, testing) */

/* menu commands (main.c hands its ID_ to tools2_command first; 1 if it was one of these) */
int  tools2_command(ULONG id);
void tools2_menus(void);              /* the menu items' ticks and ghosting */
void tools2_free(void);
/* the History Log's lines (with the time now), from main.c */
void t2_log_event(Doc *d, const char *event, const char *name);
void t2_log_steps(void);              /* a step added to the picture shown since the last call: logged */
/* (main.c) its helpers: the file requester, a new tab, opening a file, the spare page */
extern int t2_spare;
int  t2_file_request(int save, const char *title, char *path, size_t cap, const char *pattern);
int  t2_doc_show(Doc *d, const char *path);
void t2_open_path(const char *path);
void t2_doc_switch(int i);
Doc *t2_state_copy(Doc *d);
#endif
