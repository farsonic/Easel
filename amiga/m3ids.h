/*
 * m3ids.h - the menu IDs of Easel 1.3's menu, tool and automation additions (menux3.c), in a range of
 * their own (7300-7399), and what menux3.c offers main.c and the canvas.
 */
#ifndef EASEL_M3IDS_H
#define EASEL_M3IDS_H
enum {
  ID_M3_FIRST = 7300,
  /* File */
  ID_OPENRAW, ID_SAVEFORWEB, ID_EXPPICKED, ID_EXPLUT, ID_ASSETS, ID_IMGPROC, ID_STATISTICS, ID_CONTACT,
  ID_GRABSCREEN, ID_PACKAGE,
  /* Edit */
  ID_FINDREPLACE, ID_RESETTOOL, ID_RESETALLTOOLS,
  /* Image */
  ID_APPLYIMAGE, ID_CALCULATIONS, ID_MSCALE, ID_MMARKER, ID_MRULER, ID_MCOUNT,
  /* Layer */
  ID_LAYERSLICE, ID_ARTBOARDLAYERS, ID_FRAMELAYERS,
  /* View */
  ID_PIXELART, ID_FLIPVIEW, ID_FITARTBOARD, ID_SNAPLAYERS, ID_SNAPSLICES, ID_LOCKSLICES, ID_CLEARSLICES,
  ID_M3_LAST
};

#ifdef EASEL_AMIGA_H
/* menux3.c: a menu item of these done; the items greyed and ticked */
void m3_command(Doc *d, ULONG id);
void m3_menus(Doc *d, void (*menu_set)(ULONG id, ULONG attr, ULONG val));
void m3_new_doc(void);                 /* File > New (the presets, units, modes, pixel aspect) */
void m3_doc_closed(Doc *d);            /* (main.c) its measurement scale forgotten */
extern int m3_pixelart;                /* View > Pixel Art Preview: whole zoom steps, the pixel grid from 800 % */
extern int m3_flip;                    /* View > Flip Horizontal: the view mirrored (the picture unchanged) */
extern int m3_slices_locked;           /* View > Lock Slices */
/* main.c's services */
Doc *m3_load(const char *path);        /* a picture file as a document, NULL (the user told) */
int  m3_file_multi(const char *title, char paths[][512], int max);
int  m3_file_request(int save, const char *title, char *path, int cap, const char *pattern);
int  m3_show(Doc *d, const char *path);
#endif
#endif
