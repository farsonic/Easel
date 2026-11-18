/*
 * mxids.h - the menu IDs of Easel 1.2's menu and panel additions (View > Pixel Aspect Ratio, Show,
 * Pattern Preview, New Guide Layout..., Window > Arrange > Match, Edit > Search and Paste Outside,
 * Image > Trim..., Layer > New > Layer..., Distribute, Rasterize > All Layers, the Actions, Styles,
 * Layer Comps and Histogram windows, File > Automate), handled by menux.c and panelsx.c. Kept apart
 * from ids.h, in a range of their own (5600-5899).
 */
#ifndef EASEL_MXIDS_H
#define EASEL_MXIDS_H
#define ID_MX_FIRST     5600
#define ID_PAR0         5600           /* + PAR_N - 1 (core/viewx.h): a pixel aspect preset */
#define ID_PARSCREEN    5615           /* from the Amiga screen mode */
#define ID_PARCUSTOM    5616
#define ID_PARCORRECT   5617           /* Pixel Aspect Ratio Correction (ticked) */
#define ID_PIXGRID      5620
#define ID_PATPREVIEW   5621
#define ID_SHOWSELEDGES 5622
#define ID_SHOWLAYEREDGES 5623
#define ID_SHOWPATH     5624
#define ID_SHOWALL      5625
#define ID_SHOWNONE     5626
#define ID_FITLAYERS    5627
#define ID_PRINTSIZE    5628
#define ID_GUIDELAYOUT  5629
#define ID_GUIDESHAPE   5630
#define ID_MATCHZOOM    5631
#define ID_MATCHLOC     5632
#define ID_MATCHALL     5633
#define ID_SEARCH       5640           /* Edit > Search (the command list) */
#define ID_PASTEOUTSIDE 5641           /* (main.c: it needs Easel's clipboard) */
#define ID_TRIMOPT      5645
#define ID_NEWLAYERDLG  5650
#define ID_DIST_LEFT    5651           /* .. ID_DIST_LEFT + 5: core/layers.h's AL_DIST_LEFT .. AL_SPACE_V */
#define ID_RASTERALL    5658
#define ID_LAYERQPNG    5659
#define ID_FLATTENFX    5660
#define ID_BATCH        5661
#define ID_CONDMODE     5662
#define ID_COMPSTOFILES 5663
#define ID_WIN_ACTIONS  5670
#define ID_WIN_STYLES   5671
#define ID_WIN_COMPS    5672
#define ID_ACTSTOP      5673           /* Stop Recording */
#define ID_ACTPLAY      5674           /* Play the action chosen in the Actions window again */
#define ID_HISTCH0      5675           /* + HC_LUM .. HC_BLUE: the Info panel's histogram channel */
#define ID_MX_LAST      5699
/* (the windows' own buttons) */
#define ID_MXB0         5700           /* .. + 39 */

#include "../core/doc.h"
/* menux.c: a menu item of these done (0 if id isn't one it does); the items greyed and ticked */
int  mx_command(Doc *d, ULONG id);
void mx_menus(Doc *d, void (*menu_set)(ULONG id, ULONG attr, ULONG val));
/* a command chosen (main.c calls it for every one): recorded into the action being recorded */
void mx_record(ULONG id, const char *label);
extern int mx_recording;
void mx_init(void);                    /* the saved actions and styles read (PROGDIR:Easel.actions, .styles) */
void mx_free(void);
/* the canvas's View settings (menux.c), read by canvas.c */
extern int vx_par_correct;             /* pixels drawn in the picture's aspect */
extern int vx_pixgrid, vx_tile;        /* the pixel grid; Pattern Preview */
extern int vx_seledges, vx_layeredges, vx_path;   /* Show > Selection Edges, Layer Edges, Target Path */
extern int vx_hist_channel;            /* the Info panel's histogram: HC_ (core/actions.h) */
/* the canvas's context menu (a menustrip made by menux.c, disposed with the canvas) */
Object *mx_context_menu(void);
void    mx_context_free(void);

/* main.c's services for menux.c and panelsx.c */
void do_command(ULONG id);             /* a menu command, as if chosen */
const struct NewMenu *menu_table(void);
int  mx_open(const char *path);        /* a picture opened (1 if it is now the current one) */
int  mx_write(Doc *d, const char *path);   /* written in the format its name asks for */
void mx_close_current(void);           /* the current picture closed without asking */
int  mx_file_request(int save, const char *title, char *path, int cap, int drawers);

/* panelsx.c: the windows */
void dlg_actions(void);
void dlg_styles(void);
void dlg_comps(void);
void dlg_search(void);
void dlg_batch(void);
int  actions_play_current(void);       /* the action chosen last played on the current picture */
void comps_forget(Doc *d);             /* (a picture closed) its layer comps dropped */
#endif
