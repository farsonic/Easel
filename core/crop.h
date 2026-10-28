/*
 * crop.h - the Crop tool's presets and rotated crop, straightening, and the Ruler (Easel 0.7).
 */
#ifndef EASEL_CROP_H
#define EASEL_CROP_H

#include "doc.h"

/* ------------------------------------------------------------------ crop presets */
/* a preset: a ratio (w : h, kind CP_RATIO), a size in pixels (CP_PIXELS: the crop is resampled to
   it), or a size at a resolution (CP_INCHES: w x h inches at dpi). CP_FREE has no constraint, and
   CP_ORIGINAL keeps the picture's own ratio */
enum { CP_FREE, CP_ORIGINAL, CP_RATIO, CP_PIXELS, CP_INCHES };
typedef struct { const char *name; int kind; double w, h; int dpi; } CropPreset;
extern const CropPreset crop_presets[];
extern const int ncrop_presets;
/* the ratio a preset asks for (width / height) for picture d; 0 for none */
double crop_preset_ratio(const Doc *d, const CropPreset *p);
/* the box a crop drag from ax, ay to px, py makes under preset p (NULL: free), as marquee_box does;
   swap turns the ratio (the options bar's W/H swap button) */
Box  crop_preset_box(const Doc *d, const CropPreset *p, int swap, double ax, double ay, double px, double py, int from_centre);
/* the crop made under preset p: the picture cropped to b, then resampled (RS_ method) to the
   preset's size and its resolution set when it has one. One undo step; 0 on failure */
int  image_crop_preset(Doc *d, Box b, const CropPreset *p, int swap, int method);

/* ------------------------------------------------------------------ rotated crop and straightening */
/* the picture cropped to a rectangle w x h centred on cx, cy and turned angle degrees (clockwise):
   what is inside it becomes the new, upright canvas (resampled, RS_ interp). One undo step */
int  image_crop_rotated(Doc *d, double cx, double cy, double w, double h, double angle, int interp);
/* the tilt of the line from x0, y0 to x1, y1 away from level (or from upright, when it is nearer
   vertical), degrees clockwise, -45..45: the angle image_crop_rotated takes to straighten it */
double straighten_angle(double x0, double y0, double x1, double y1);
/* the Ruler's Straighten Layer, for the whole picture: every layer turned so the line is level and
   the canvas cropped to the largest rectangle with no empty corners (the Crop tool's Straighten
   does the same). One undo step; 0 if the line is level already or too short */
int  image_straighten(Doc *d, double x0, double y0, double x1, double y1);
/* the same on the active layer alone, about the canvas's centre, without cropping (Alt) */
int  layer_straighten(Doc *d, double x0, double y0, double x1, double y1, int interp);
/* the largest rectangle (*rw x *rh, sides along the turned frame's axes) that fits inside a w x h
   rectangle turned by angle degrees, both centred on the same point: what straightening keeps */
void rotated_inner_box(double w, double h, double angle, double *rw, double *rh);

/* ------------------------------------------------------------------ the Ruler */
/* what the Info panel and options bar show for a measuring line: its length, angle (degrees
   anticlockwise from 3 o'clock, as Photoshop shows it, -180..180), and width and height (signed).
   With the protractor (Alt-drag from an end), the angle between the two arms */
typedef struct { double length, angle, w, h; } RulerInfo;
void   ruler_measure(double x0, double y0, double x1, double y1, RulerInfo *r);
double ruler_protractor(double xv, double yv, double x0, double y0, double x1, double y1);   /* 0-180 */
#endif
