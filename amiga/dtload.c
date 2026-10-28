/*
 * dtload.c - pictures through datatypes (see dtload.h). Written from the RKM / NDK
 * documentation of datatypes.library and picture.datatype (datatypes/pictureclass.h): V43's
 * PDTM_READPIXELARRAY, and for older systems the class's bitmap (PDTA_BitMap) and colour
 * registers (PDTA_CRegs, 32 bits a gun).
 */
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
/* a base of our own, so nothing clashes with another file's DataTypesBase */
#define DataTypesBase dtl_DataTypesBase
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/datatypes.h>
#include <clib/alib_protos.h>
#include "dtload.h"
#include "../core/codec.h"

struct Library *dtl_DataTypesBase;

#ifndef mskHasAlpha
#define mskHasAlpha 4
#endif

static void say(char *err, size_t errcap, const char *msg)
{
  if (err && errcap) { strncpy(err, msg, errcap - 1); err[errcap - 1] = 0; }
}

/* V43: the whole picture as RGBA in one call; 1 if the datatype did it */
static int read_v43(Object *o, u8 *px, i32 w, i32 h, int masking)
{
  struct pdtBlitPixelArray m;
  ULONG r, alpha = 0;
  long k, n = (long)w * h;
  int any = 0, anyalpha = 0;
  memset(px, 0, (size_t)n * 4);
  m.MethodID = PDTM_READPIXELARRAY;
  m.pbpa_PixelData = px;
  m.pbpa_PixelFormat = PBPAFMT_RGBA;
  m.pbpa_PixelArrayMod = (ULONG)w * 4;
  m.pbpa_Left = 0; m.pbpa_Top = 0;
  m.pbpa_Width = (ULONG)w; m.pbpa_Height = (ULONG)h;
  r = DoMethodA(o, (Msg)&m);
  /* some datatypes return 0 even when they did it: look at what came back */
  for (k = 0; k < n * 4 && !any; k++) if (px[k]) any = 1;
  if (!r && !any) return 0;
  /* alpha only when the datatype says there is some (PDTA_AlphaChannel, 47.9), or the header's
     masking says so, and never a picture all transparent */
  if (GetDTAttrs(o, PDTA_AlphaChannel, (ULONG)&alpha, TAG_DONE) != 1) alpha = masking == mskHasAlpha;
  if (alpha) for (k = 0; k < n && !anyalpha; k++) if (px[k * 4 + 3]) anyalpha = 1;
  if (!alpha || !anyalpha) for (k = 0; k < n; k++) px[k * 4 + 3] = 255;
  return 1;
}

/* older picture.datatype: the bitmap read a line at a time (any bitmap ReadPixelLine8 can read,
   up to 8 planes) through the colour registers; HAM and Extra Half-Brite by the ModeID */
static const char *read_bitmap(Object *o, u8 *px, i32 w, i32 h)
{
  struct BitMap *bm = NULL, *tmpbm = NULL;
  ULONG *cregs = NULL, ncol = 0, modeid = 0;
  struct RastPort rp, tmprp;
  ULONG depth;
  u8 pal[256 * 3], *line = NULL;
  i32 x, y, aw = (w + 15) & ~15;
  int i, ham = 0, ehb;
  GetDTAttrs(o, PDTA_BitMap, (ULONG)&bm, PDTA_CRegs, (ULONG)&cregs, PDTA_NumColors, (ULONG)&ncol, PDTA_ModeID, (ULONG)&modeid, TAG_DONE);
  if (!bm) {
    DoDTMethod(o, NULL, NULL, DTM_PROCLAYOUT, NULL, 1L);
    GetDTAttrs(o, PDTA_DestBitMap, (ULONG)&bm, TAG_DONE);
  }
  if (!bm) return "The datatype gave no picture";
  depth = GetBitMapAttr(bm, BMA_DEPTH);
  if (depth < 1 || depth > 8) return "The datatype's picture is too deep to read without picture.datatype 43";
  if (ncol > 256) ncol = 256;
  memset(pal, 0, sizeof pal);
  for (i = 0; i < (int)ncol && cregs; i++) { pal[i * 3] = (u8)(cregs[i * 3] >> 24); pal[i * 3 + 1] = (u8)(cregs[i * 3 + 1] >> 24); pal[i * 3 + 2] = (u8)(cregs[i * 3 + 2] >> 24); }
  if (modeid != INVALID_ID && (modeid & HAM_KEY)) ham = depth == 8 ? 6 : 4;
  ehb = modeid != INVALID_ID && (modeid & EXTRAHALFBRITE_KEY) && depth == 6;
  if (ehb) for (i = 32; i < 64; i++) { pal[i * 3] = pal[(i - 32) * 3] >> 1; pal[i * 3 + 1] = pal[(i - 32) * 3 + 1] >> 1; pal[i * 3 + 2] = pal[(i - 32) * 3 + 2] >> 1; }
  if (!(line = (u8 *)AllocVec((ULONG)aw, MEMF_ANY)) || !(tmpbm = AllocBitMap((ULONG)aw, 1, depth, 0, bm))) {
    if (line) FreeVec(line);
    return "Out of memory";
  }
  InitRastPort(&rp);
  rp.BitMap = bm;
  tmprp = rp;
  tmprp.Layer = NULL;
  tmprp.BitMap = tmpbm;
  for (y = 0; y < h; y++) {
    u8 *o8 = px + (size_t)y * w * 4;
    int r = pal[0], g = pal[1], b = pal[2];
    ReadPixelLine8(&rp, 0, (ULONG)y, (ULONG)w, line, &tmprp);
    for (x = 0; x < w; x++, o8 += 4) {
      int v = line[x];
      if (ham) {
        int ctl = v >> ham, d = v & ((1 << ham) - 1), dv = ham == 4 ? d * 17 : (d << 2) | (d >> 4);
        if (ctl == 0) { r = pal[d * 3]; g = pal[d * 3 + 1]; b = pal[d * 3 + 2]; }
        else if (ctl == 1) b = dv;
        else if (ctl == 2) r = dv;
        else g = dv;
      } else { r = pal[v * 3]; g = pal[v * 3 + 1]; b = pal[v * 3 + 2]; }
      o8[0] = (u8)r; o8[1] = (u8)g; o8[2] = (u8)b; o8[3] = 255;
    }
  }
  WaitBlit();
  FreeBitMap(tmpbm);
  FreeVec(line);
  return NULL;
}

Doc *dt_load(const char *path, char *err, size_t errcap)
{
  Object *o;
  struct BitMapHeader *bmh = NULL;
  i32 w, h;
  u8 *px = NULL;
  Doc *d = NULL;
  const char *msg = NULL;
  if (err && errcap) err[0] = 0;
  if (!(DataTypesBase = OpenLibrary((STRPTR)"datatypes.library", 39))) { say(err, errcap, "datatypes.library isn't there"); return NULL; }
  o = NewDTObject((CONST_STRPTR)path, DTA_SourceType, DTST_FILE, DTA_GroupID, GID_PICTURE,
                  PDTA_Remap, FALSE, PDTA_DestMode, PMODE_V43, TAG_DONE);
  if (!o) {
    LONG e = IoErr();
    STRPTR s = e >= DTERROR_UNKNOWN_DATATYPE ? GetDTString((ULONG)e) : NULL;
    say(err, errcap, s ? (const char *)s : e == ERROR_OBJECT_WRONG_TYPE || e == DTERROR_UNKNOWN_DATATYPE ? "No datatype knows this file" : "The datatype couldn't read the file");
    CloseLibrary(DataTypesBase); DataTypesBase = NULL;
    return NULL;
  }
  GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, TAG_DONE);
  if (!bmh || bmh->bmh_Width < 1 || bmh->bmh_Height < 1 || bmh->bmh_Width > MAXDIM || bmh->bmh_Height > MAXDIM) msg = "The datatype gave no picture size";
  else {
    w = bmh->bmh_Width; h = bmh->bmh_Height;
    if ((long)w * h > CODEC_MAXPIXELS || !(px = (u8 *)malloc((size_t)w * h * 4))) { msg = "Not enough memory for the picture"; codec_oom = 1; }
    else if (!read_v43(o, px, w, h, bmh->bmh_Masking)) msg = read_bitmap(o, px, w, h);
  }
  DisposeDTObject(o);
  CloseLibrary(DataTypesBase); DataTypesBase = NULL;
  if (!msg && !(d = doc_adopt(w, h, px))) msg = "Not enough memory for the picture";
  if (msg) { free(px); say(err, errcap, msg); return NULL; }
  doc_forget_undo(d);
  d->dirty = 0;
  return d;
}
