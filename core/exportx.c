/*
 * exportx.c - File > Export As's options (see exportx.h).
 */
#include <stdlib.h>
#include <string.h>
#include "exportx.h"
#include "paint.h"
#include "colorx.h"
#include "codec.h"

void exportx_defaults(ExportOpts *o)
{
  memset(o, 0, sizeof *o);
  o->pct = 100; o->method = RS_BICUBIC; o->matte = EXM_NONE; o->metadata = EXD_ALL;
}
Doc *exportx_prepare(Doc *d, const ExportOpts *o)
{
  u8 *px;
  Doc *t;
  long k, np;
  if (!d || !(px = (u8 *)malloc((size_t)d->w * d->h * 4))) return NULL;
  doc_composite(d, doc_box(d), px, (long)d->w * 4);
  if (!(t = doc_adopt(d->w, d->h, px))) { free(px); return NULL; }
  t->dpi = d->dpi; t->aspect_x = d->aspect_x; t->aspect_y = d->aspect_y;
  t->fg = d->fg; t->bg = d->bg;
  if (d->icc && (t->icc = (u8 *)malloc(d->icclen)) != NULL) { memcpy(t->icc, d->icc, d->icclen); t->icclen = d->icclen; }
  /* the File Info kept, the copyright only, or none */
  if (o->metadata == EXD_ALL) t->info = d->info;
  else if (o->metadata == EXD_COPYRIGHT) { docinfo_clear(&t->info); memcpy(t->info.copyright, d->info.copyright, sizeof t->info.copyright); }
  else docinfo_clear(&t->info);
  /* scaled */
  if (o->pct > 0 && o->pct != 100) {
    long w = (long)d->w * o->pct / 100, h = (long)d->h * o->pct / 100;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > MAXDIM || h > MAXDIM || !image_resize(t, (i32)w, (i32)h, o->method)) { doc_free(t); return NULL; }
  }
  /* the canvas, about the centre */
  if ((o->canvas_w > 0 && o->canvas_w != t->w) || (o->canvas_h > 0 && o->canvas_h != t->h)) {
    i32 cw = o->canvas_w > 0 ? o->canvas_w : t->w, ch = o->canvas_h > 0 ? o->canvas_h : t->h, y;
    i32 dx = (cw - t->w) / 2, dy = (ch - t->h) / 2;
    u8 *n;
    if (cw > MAXDIM || ch > MAXDIM || !(n = (u8 *)calloc((size_t)cw * ch, 4))) { doc_free(t); return NULL; }
    for (y = 0; y < ch; y++) {
      i32 sy = y - dy, x0 = dx < 0 ? 0 : dx, x1 = dx + t->w > cw ? cw : dx + t->w;
      if (sy < 0 || sy >= t->h || x1 <= x0) continue;
      memcpy(n + ((size_t)y * cw + x0) * 4, t->lay[0]->px + ((size_t)sy * t->w + (x0 - dx)) * 4, (size_t)(x1 - x0) * 4);
    }
    {
      Doc *t2 = doc_adopt(cw, ch, n);
      if (!t2) { free(n); doc_free(t); return NULL; }
      t2->dpi = t->dpi; t2->aspect_x = t->aspect_x; t2->aspect_y = t->aspect_y; t2->info = t->info; t2->fg = t->fg; t2->bg = t->bg;
      t2->icc = t->icc; t2->icclen = t->icclen; t->icc = NULL;
      doc_free(t);
      t = t2;
    }
  }
  /* the matte: everything laid over it, opaque */
  if (o->matte != EXM_NONE) {
    u32 c = o->matte == EXM_WHITE ? 0xFFFFFF : o->matte == EXM_BLACK ? 0 : o->matte == EXM_FG ? d->fg : d->bg;
    int m[3];
    m[0] = (int)(c >> 16 & 255); m[1] = (int)(c >> 8 & 255); m[2] = (int)(c & 255);
    np = (long)t->w * t->h;
    for (k = 0; k < np; k++) {
      u8 *p = t->lay[0]->px + k * 4;
      int a = p[3], q;
      for (q = 0; q < 3; q++) p[q] = (u8)((p[q] * a + m[q] * (255 - a) + 127) / 255);
      p[3] = 255;
    }
  }
  /* sRGB */
  if (o->to_srgb && t->icc) {
    int ok = colorx_convert(t, CX_SRGB, NULL, 0, 1 /* CI_RELATIVE */, 1);
    if (!ok) { doc_free(t); return NULL; }
  }
  doc_forget_undo(t);
  t->dirty = 0;
  return t;
}
