/*
 * filesui.h - round 3's file commands in the interface (filesui.c).
 */
#ifndef EASEL_FILESUI_H
#define EASEL_FILESUI_H
/* File > Open As: the reader to open file (its name, shown) with, current chosen at first; -1 for Cancel */
int filesui_open_as_format(const char *file, int current);
/* round 3's commands (filesids.h: Window > Timeline, Notes, Measurement Log and their buttons, Record
   Measurements); 1 when the picture should be redrawn */
int  filesui_command(ULONG id);
/* the open windows' lists made again (main.c's refresh_panels) */
void filesui_refresh(void);
#endif
