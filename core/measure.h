/*
 * measure.h - Easel round 3: the Measurement Log (measure.c), as Photoshop's: Image > Analysis >
 * Record Measurements adds a row for the selection (its area, perimeter, size, and the merged
 * picture's grey values inside it), the Ruler tool's line (length, angle) or the Count tool's
 * markers; the rows are kept for the session and exported as tab-separated text.
 */
#ifndef EASEL_MEASURE_H
#define EASEL_MEASURE_H
#include "doc.h"

enum { MS_SELECTION, MS_RULER, MS_COUNT };
extern const char *const measure_sources[3];             /* "Selection", "Ruler Tool", "Count Tool" */
typedef struct {
  int source;                         /* MS_ */
  char doc[32];                       /* the picture's name */
  long count;                         /* pixels selected (or markers counted) */
  double area, perimeter;             /* pixels (partly selected ones count in part); edge length */
  double w, h;                        /* the selection's box, or the ruler's width and height */
  double length, angle;               /* the Ruler's */
  double mean, minv, maxv, density;   /* grey (luminosity 0-255) inside the selection; mean times area */
} MeasureRec;
#define MLOG_MAX 500
typedef struct { MeasureRec r[MLOG_MAX]; int n; } MeasureLog;

/* a selection's measurements (the whole canvas when nothing is selected); 0 if it is empty */
int  measure_selection(Doc *d, MeasureRec *m);
void measure_ruler(double x0, double y0, double x1, double y1, MeasureRec *m);
void measure_count(const Doc *d, MeasureRec *m);
int  mlog_add(MeasureLog *l, const MeasureRec *m, const char *docname);   /* its row, -1 if full */
void mlog_delete(MeasureLog *l, int i);
/* a row as the window shows it (into out, cap bytes; src the source's name, translated) */
void mlog_line(const MeasureRec *m, int row, const char *src, char *out, size_t cap);
/* the log as tab-separated text with a header line (malloc'd) */
char *mlog_text(const MeasureLog *l);
#endif
