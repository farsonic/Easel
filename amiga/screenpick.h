/* screenpick.h - 1.1: the colour of a screen pixel, for the Eyedropper outside the picture */
#ifndef EASEL_SCREENPICK_H
#define EASEL_SCREENPICK_H
#include <exec/types.h>
struct Window;
/* the colour (0xRRGGBB) of the screen's pixel at window w's x, y (which may be outside the
   window), -1 if that is off the screen or can't be read */
LONG screen_pixel(struct Window *w, LONG wx, LONG wy);
#endif
