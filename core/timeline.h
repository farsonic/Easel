/*
 * timeline.h - Easel round 3: the Timeline window's frame commands (timeline.c), on legacy.h's frames
 * (an animation's frames are the picture's top-level layers, bottom first; their times in their
 * names, "Frame 3 (0.25 s)"). Each is one undo step; 1 if it changed the picture.
 */
#ifndef EASEL_TIMELINE_H
#define EASEL_TIMELINE_H
#include "doc.h"

/* a new frame after frame k: a copy of it (a pixel, type or shape layer; a group frame can't be copied) */
int  tl_new_frame(Doc *d, int k);
/* frame k deleted (a group with its members); the last frame stays */
int  tl_delete_frame(Doc *d, int k);
/* n frames (1-50) between frame k and the next, fading from one to the other (both pixel layers), each
   with frame k's time */
int  tl_tween(Doc *d, int k, int n);
/* the frames in the opposite order (single layers only) */
int  tl_reverse(Doc *d);
/* every frame's time set to cs hundredths (0: none, the export's default) */
int  tl_set_all_times(Doc *d, int cs);
/* the animation's length in hundredths of a second (a frame without a time counts def) */
long tl_length(const Doc *d, int def);
#endif
