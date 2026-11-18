/*
 * screenpick.c - 1.1: the Eyedropper outside the picture. Dragged off the picture (over the panels,
 * another window, anywhere on the screen) it takes the colour of the screen's pixel under the
 * pointer, as PhotoCraft's Eyedropper does outside the document: through cybergraphics.library on
 * a screen of more than 256 colours, else from the screen's palette for the pen there.
 */
#include <graphics/gfx.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include "screenpick.h"

extern struct Library *CyberGfxBase;
/* cybergraphics.library's ReadRGBPixel (0x00RRGGBB at x, y), as a vbcc inline call */
ULONG __cgx_ReadRGBPixel(__reg("a6") void *, __reg("a1") struct RastPort *, __reg("d0") UWORD, __reg("d1") UWORD) = "\tjsr\t-108(a6)";

/* (see screenpick.h) */
LONG screen_pixel(struct Window *w, LONG wx, LONG wy)
{
  struct Screen *scr;
  LONG sx, sy;
  ULONG depth;
  if (!w || !(scr = w->WScreen)) return -1;
  sx = w->LeftEdge + wx; sy = w->TopEdge + wy;
  if (sx < 0 || sy < 0 || sx >= scr->Width || sy >= scr->Height) return -1;
  depth = GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH);
  if (depth > 8 && CyberGfxBase) return (LONG)(__cgx_ReadRGBPixel(CyberGfxBase, &scr->RastPort, (UWORD)sx, (UWORD)sy) & 0xFFFFFF);
  {
    LONG pen = ReadPixel(&scr->RastPort, sx, sy);
    ULONG rgb[3];
    if (pen < 0 || !scr->ViewPort.ColorMap) return -1;
    GetRGB32(scr->ViewPort.ColorMap, (ULONG)pen, 1, rgb);
    return (LONG)(((rgb[0] >> 24) << 16) | ((rgb[1] >> 24) << 8) | (rgb[2] >> 24));
  }
}
