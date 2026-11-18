/*
 * picinfo.c - a picture file described in words (see picinfo.h).
 */
#include <stdlib.h>
#include <string.h>
#include "picinfo.h"
#include "batch.h"
#include "amigamodes.h"

char *picture_info(const u8 *data, size_t len, char *err, size_t errcap)
{
  static const char *const modes[] = { "RGB", "Indexed", "Grayscale", "Bitmap" };
  Doc *d = batch_load(data, len, err, errcap);
  Buf b;
  int i, npix = 0, ngrp = 0, nlive = 0, fmt = codec_detect(data, len);
  if (!d) return NULL;
  for (i = 0; i < d->nlay; i++) {
    int k = d->lay[i]->kind;
    if (k == LK_GROUP) ngrp++; else if (k == LK_PIXEL) npix++; else nlive++;
  }
  buf_init(&b);
  buf_printf(&b, "Format: %s\n", codec_name(fmt));
  buf_printf(&b, "Size: %ld x %ld pixels\n", (long)d->w, (long)d->h);
  buf_printf(&b, "Layers: %d (%d pixel, %d group, %d other)\n", d->nlay, npix, ngrp, nlive);
  buf_printf(&b, "Mode: %s", d->mode < 4 ? modes[d->mode] : "RGB");
  if (d->mode == MODE_INDEXED) buf_printf(&b, ", %d colours", d->npal);
  buf_adds(&b, "\n");
  buf_printf(&b, "Resolution: %d dpi\n", d->dpi ? d->dpi : 72);
  if (d->aspect_x && d->aspect_y) buf_printf(&b, "Pixel aspect: %d:%d\n", d->aspect_x, d->aspect_y);
  if (d->modeid) {
    const AmigaMode *m = amode_get(amode_find(d->modeid));
    const char *nm = m ? m->name : NULL;
    buf_printf(&b, "Screen mode: %08lX%s%s\n", (unsigned long)d->modeid, nm ? " " : "", nm ? nm : "");
  }
  if (d->ncycle) buf_printf(&b, "Colour cycles: %d\n", d->ncycle);
  if (d->info.title[0]) buf_printf(&b, "Title: %s\n", d->info.title);
  if (d->info.author[0]) buf_printf(&b, "Author: %s\n", d->info.author);
  if (d->info.copyright[0]) buf_printf(&b, "Copyright: %s\n", d->info.copyright);
  if (d->info.keywords[0]) buf_printf(&b, "Keywords: %s\n", d->info.keywords);
  if (d->info.description[0]) buf_printf(&b, "Description: %s\n", d->info.description);
  doc_free(d);
  if (b.oom) { buf_free(&b); if (err && errcap) { strncpy(err, "out of memory", errcap - 1); err[errcap - 1] = 0; } return NULL; }
  return buf_take(&b);
}
