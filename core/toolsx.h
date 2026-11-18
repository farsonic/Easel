/*
 * toolsx.h - Easel 1.2's tool extras: Perspective Crop, the Crop tool's turned box, Note, Count, Slice
 * and Slice Select, Rotate View, Reset Tool, and the History Options and History Log.
 *
 * Notes, counts and slices are the document's annotations (Doc.annot, one flat malloc'd Annots),
 * drawn over the picture by the canvas and never part of it. They are kept in OpenRaster files
 * (easel:note, easel:count and easel:slice elements) and are not undone (as in Photoshop, where they
 * live outside the pixels; Clear All asks first).
 */
#ifndef EASEL_TOOLSX_H
#define EASEL_TOOLSX_H
#include <stdio.h>
#include "doc.h"

/* ------------------------------------------------------------------ Perspective Crop */
/* the homography taking the unit square's corners (0,0) (1,0) (1,1) (0,1) to q (x, y pairs in that
   order); 0 if the quad is degenerate. h: 3 x 3, row major */
int  homography_unit(const double *q, double *h);
/* the size a quad's perspective gives (the average lengths of its opposite sides) */
void perspective_size(const double *q, double *w, double *h);
/* the picture remade from the quad q (corners clockwise from the top left, canvas pixels) as an
   upright nw x nh canvas (0: the perspective size), RS_ interp. One undo step "Perspective Crop" */
int  image_perspective_crop(Doc *d, const double *q, i32 nw, i32 nh, int interp);

/* ------------------------------------------------------------------ the Crop tool's turned box */
/* the four corners of a box w x h centred on cx, cy turned by angle degrees clockwise (x, y pairs
   clockwise from the top left) */
void crop_corners(double cx, double cy, double w, double h, double angle, double *out);
/* a drag outside the box from (ax, ay) to (px, py) about the centre: the angle to add, degrees */
double crop_drag_angle(double cx, double cy, double ax, double ay, double px, double py);
/* is x, y inside the turned box? */
int  crop_inside(double cx, double cy, double w, double h, double angle, double x, double y);

/* ------------------------------------------------------------------ annotations */
#define MAXNOTES 64
#define NOTE_LEN 256
#define MAXCOUNTS 999
#define MAXCOUNTGROUPS 16
#define MAXSLICES 64
typedef struct { i32 x, y; u32 color; u8 open; char author[32]; char text[NOTE_LEN]; } Note;
typedef struct { char name[32]; u32 color; u8 size, label, visible, pad; } CountGroup;   /* marker size 1-10, label size 8-72 */
typedef struct { i32 x, y; u8 group; u8 pad[3]; } CountMark;
typedef struct { Box b; char name[32]; char url[96]; } Slice;
typedef struct Annots {
  Note note[MAXNOTES]; int nnote;
  CountGroup group[MAXCOUNTGROUPS]; int ngroup, cur_group;
  CountMark mark[MAXCOUNTS]; int nmark;
  Slice slice[MAXSLICES]; int nslice;
  u8 show_notes, show_count, show_slices, pad;
} Annots;
Annots *doc_annots(Doc *d, int make);  /* NULL when there are none (and make is 0, or out of memory) */
/* the Note tool */
int  note_add(Doc *d, i32 x, i32 y, const char *author, const char *text, u32 color);   /* its index, -1 if full */
int  note_set(Doc *d, int i, const char *text);
int  note_move(Doc *d, int i, i32 x, i32 y);
int  note_delete(Doc *d, int i);
int  note_clear(Doc *d);                                   /* Clear All: how many went */
int  note_at(Doc *d, i32 x, i32 y, int tol);               /* the note whose icon is at x, y, -1 none */
/* the Count tool: a marker in the current group (numbered within it from 1) */
int  count_add(Doc *d, i32 x, i32 y);                      /* its index, -1 if full */
int  count_at(Doc *d, i32 x, i32 y, int tol);              /* the marker at x, y, -1 none */
int  count_move(Doc *d, int i, i32 x, i32 y);
int  count_delete(Doc *d, int i);
int  count_clear(Doc *d, int group);                       /* the group's markers (-1: all of them) */
int  count_group_add(Doc *d, const char *name, u32 color); /* its index (made current), -1 if full */
int  count_group_delete(Doc *d, int g);
int  count_number(const Doc *d, int i);                    /* marker i's number in its group, 1 up */
int  count_in_group(const Doc *d, int g);
/* Record Measurements: one line a group, "Count 1<TAB>5" (malloc'd) */
char *count_record(const Doc *d);
/* the Slice tool */
int  slice_add(Doc *d, Box b);                             /* its index, -1 if full or empty */
int  slice_at(const Doc *d, i32 x, i32 y);                 /* the topmost slice at x, y, -1 none */
int  slice_set(Doc *d, int i, Box b);
int  slice_delete(Doc *d, int i);
int  slice_divide(Doc *d, int i, int across, int down);    /* into across x down slices; how many */
int  slices_from_guides(Doc *d);                           /* Slices From Guides: the grid the guides make; how many */
/* each slice's part of the merged picture as a PNG: name_NN.png in drawer; how many were written */
int  slices_export(Doc *d, const char *drawer, const char *base);
/* the text form kept in OpenRaster (one element each, as XML, malloc'd), and read back from stack.xml */
char *annots_xml(const Doc *d);
void  annots_read_xml(Doc *d, const char *xml, size_t n);

/* ------------------------------------------------------------------ Rotate View */
/* the canvas's view turned by angle degrees (clockwise) about the view's centre: screen point (sx, sy)
   relative to that centre to image point, and back */
void view_rotate(double angle, double sx, double sy, double *ix, double *iy);
void view_unrotate(double angle, double ix, double iy, double *sx, double *sy);
/* the angle snapped to 15 degree steps (Shift), kept in -180..180 */
double view_angle_snap(double a, int snap);

/* ------------------------------------------------------------------ History Options and the History Log */
typedef struct {
  int auto_snapshot;                  /* Automatically Create First Snapshot (when a picture is opened) */
  int snapshot_on_save;               /* New Snapshot When Saving */
  int ask_snapshot_name;              /* Show New Snapshot Dialog by Default */
  int log_on;                         /* the History Log kept */
  int log_where;                      /* HL_FILE, HL_META, HL_BOTH */
  int log_detail;                     /* HL_SESSIONS, HL_CONCISE, HL_DETAILED */
  char log_file[256];
} HistOptions;
enum { HL_FILE, HL_META, HL_BOTH };
enum { HL_SESSIONS, HL_CONCISE, HL_DETAILED };
extern HistOptions hist_opts;
/* a line of the log: Easel started or quit, a picture opened, saved or closed (what "event" says, with
   the picture's name), or (Concise and Detailed) a step named action, with detail for Detailed. The
   time is given by the caller ("2026-11-18 10:30:00"). Lines go to the file (appended) and, for the
   metadata, to d's File Info description (its History section); returns the line written (static) */
const char *histlog_event(Doc *d, const char *when, const char *event, const char *name);
const char *histlog_step(Doc *d, const char *when, const char *name, const char *action, const char *detail);
/* the options as preference lines ("history_auto_snapshot 1" ...) and back */
void histopts_write(FILE *f);
int  histopts_line(const char *line);
#endif
