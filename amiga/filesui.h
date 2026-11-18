/*
 * filesui.h - round 3's file commands in the interface (filesui.c).
 */
#ifndef EASEL_FILESUI_H
#define EASEL_FILESUI_H
/* File > Open As: the reader to open file (its name, shown) with, current chosen at first; -1 for Cancel */
int filesui_open_as_format(const char *file, int current);
#endif
