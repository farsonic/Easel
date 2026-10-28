/*
 * clipboard.c - the Amiga clipboard through iffparse.library (see clipboard.h). Written from the
 * RKM Libraries' IFFParse chapter and the IFF ILBM and FTXT specifications.
 *
 * Pictures are made and read by Easel's own ILBM code (core/ilbm.c): going out, the finished FORM
 * is written into the clipboard as the body of a FORM pushed with iffparse; coming in, the first
 * FORM ILBM is found by raw steps and its bytes read whole into memory, then decoded as a file.
 */
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <libraries/iffparse.h>
/* a base of our own, so nothing clashes with another file's IFFParseBase */
#define IFFParseBase clip_IFFParseBase
#include <proto/exec.h>
#include <proto/iffparse.h>
#include "clipboard.h"
#include "../core/palette.h"

struct Library *clip_IFFParseBase;

#define ID_ILBM MAKE_ID('I', 'L', 'B', 'M')
#define ID_FTXT MAKE_ID('F', 'T', 'X', 'T')
#define ID_CHRS MAKE_ID('C', 'H', 'R', 'S')
#define CLIP_UNIT 0

/* the clipboard opened for reading or writing: the library, the handle and the device; 0 if any
   of them isn't there (everything already closed again) */
static struct IFFHandle *clip_open(long mode)
{
  struct IFFHandle *iff;
  if (!(IFFParseBase = OpenLibrary((STRPTR)"iffparse.library", 39))) return NULL;
  if ((iff = AllocIFF())) {
    if ((iff->iff_Stream = (ULONG)OpenClipboard(CLIP_UNIT))) {
      InitIFFasClip(iff);
      if (!OpenIFF(iff, mode)) return iff;
      CloseClipboard((struct ClipboardHandle *)iff->iff_Stream);
    }
    FreeIFF(iff);
  }
  CloseLibrary(IFFParseBase);
  IFFParseBase = NULL;
  return NULL;
}
static void clip_close(struct IFFHandle *iff)
{
  CloseIFF(iff);
  CloseClipboard((struct ClipboardHandle *)iff->iff_Stream);
  FreeIFF(iff);
  CloseLibrary(IFFParseBase);
  IFFParseBase = NULL;
}

/* a whole IFF FORM (as a file holds it: "FORM", size, type, chunks) written to the clipboard */
static int clip_write_form(const u8 *form, size_t len)
{
  struct IFFHandle *iff;
  ULONG type;
  int ok = 0;
  if (!form || len < 12 || memcmp(form, "FORM", 4)) return 0;
  if (!(iff = clip_open(IFFF_WRITE))) return 0;
  type = ((ULONG)form[8] << 24) | ((ULONG)form[9] << 16) | ((ULONG)form[10] << 8) | form[11];
  if (!PushChunk(iff, type, ID_FORM, IFFSIZE_UNKNOWN)) {
    ok = WriteChunkBytes(iff, (APTR)(form + 12), (LONG)(len - 12)) == (LONG)(len - 12);
    if (PopChunk(iff)) ok = 0;
  }
  clip_close(iff);
  return ok;
}

/* how many colours a picture has, counting up to 257 */
static int count_colors(const Rgba *im)
{
  u32 tab[521];
  u8 used[521];
  long k, n = (long)im->w * im->h;
  int count = 0;
  memset(used, 0, sizeof used);
  for (k = 0; k < n; k++) {
    const u8 *p = im->px + k * 4;
    u32 c = ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2], h = (c * 2654435761UL >> 7) % 521;
    while (used[h] && tab[h] != c) h = (h + 1) % 521;
    if (!used[h]) { used[h] = 1; tab[h] = c; if (++count > 256) return count; }
  }
  return count;
}

int clipboard_put_ilbm_ex(const Rgba *im, const u8 *pal, int npal, const u8 *idx, int mode)
{
  IlbmOpts o;
  Rgba flat;
  u8 *data;
  size_t len = 0;
  long k, n;
  int ok, transparent = 0;
  char err[100];
  if (!im || !im->px || im->w < 1 || im->h < 1) return 0;
  n = (long)im->w * im->h;
  for (k = 0; k < n && !transparent; k++) if (im->px[k * 4 + 3] < 128) transparent = 1;
  ilbm_opts_default(&o);
  o.mask = transparent ? IMK_PLANE : IMK_NONE;
  o.cycles = 0;
  if (pal && idx && npal >= 1 && npal <= 256) o.kind = IK_NORMAL;
  else if (mode == CLIP_ILBM_24) o.kind = IK_DEEP24;
  else if (mode == CLIP_ILBM_8) o.kind = IK_NORMAL;
  else {
    /* the colours as the ILBM will show them (over white) */
    if (!rgba_alloc(&flat, im->w, im->h)) return 0;
    memcpy(flat.px, im->px, (size_t)n * 4);
    rgba_flatten_white(&flat);
    o.kind = count_colors(&flat) <= 256 ? IK_NORMAL : IK_DEEP24;
    rgba_free(&flat);
  }
  data = ilbm_write(im, o.kind == IK_NORMAL ? pal : NULL, npal, o.kind == IK_NORMAL ? idx : NULL, NULL, 0, &o, &len, err, sizeof err);
  ok = clip_write_form(data, len);
  free(data);
  return ok;
}
int clipboard_put_ilbm(const Rgba *im) { return clipboard_put_ilbm_ex(im, NULL, 0, NULL, CLIP_ILBM_AUTO); }

/* step through the clipboard's contents to the first FORM of type; 1 when the handle's current
   chunk is that FORM (its type already read) */
static int find_form(struct IFFHandle *iff, ULONG type)
{
  for (;;) {
    LONG e = ParseIFF(iff, IFFPARSE_RAWSTEP);
    struct ContextNode *cn;
    if (e == IFFERR_EOC) continue;
    if (e) return 0;
    if ((cn = CurrentChunk(iff)) && cn->cn_ID == ID_FORM && cn->cn_Type == type) return 1;
  }
}

int clipboard_has_ilbm(void)
{
  struct IFFHandle *iff = clip_open(IFFF_READ);
  int has;
  if (!iff) return 0;
  has = find_form(iff, ID_ILBM);
  clip_close(iff);
  return has;
}

Doc *clipboard_get_doc(char *err, size_t errcap)
{
  struct IFFHandle *iff = clip_open(IFFF_READ);
  struct ContextNode *cn;
  u8 *buf = NULL;
  size_t len = 0;
  Doc *d = NULL;
  if (err && errcap) err[0] = 0;
  if (!iff) { codec_err(err, errcap, "The clipboard couldn't be opened (iffparse.library 39 is needed)"); return NULL; }
  if (!find_form(iff, ID_ILBM) || !(cn = CurrentChunk(iff))) codec_err(err, errcap, "The clipboard holds no picture");
  else if (cn->cn_Size < 4 || cn->cn_Size > 256L * 1024 * 1024 || !(buf = (u8 *)malloc((size_t)cn->cn_Size + 8)))
    codec_err(err, errcap, cn->cn_Size < 4 ? "The clipboard's picture is damaged" : "Not enough memory for the clipboard's picture");
  else {
    LONG body = cn->cn_Size - 4, got;
    len = (size_t)cn->cn_Size + 8;
    memcpy(buf, "FORM", 4);
    buf[4] = (u8)(cn->cn_Size >> 24); buf[5] = (u8)(cn->cn_Size >> 16); buf[6] = (u8)(cn->cn_Size >> 8); buf[7] = (u8)cn->cn_Size;
    memcpy(buf + 8, "ILBM", 4);
    got = ReadChunkBytes(iff, buf + 12, body);
    if (got != body) { free(buf); buf = NULL; codec_err(err, errcap, "The clipboard's picture couldn't be read"); }
  }
  clip_close(iff);
  if (!buf) return NULL;
  codec_oom = 0;
  if (ilbm_indexable(buf, len)) d = ilbm_read_indexed(buf, len, err, errcap);
  if (!d) d = codec_load_doc(buf, len, err, errcap);
  free(buf);
  return d;
}

int clipboard_put_text(const char *utf8)
{
  struct IFFHandle *iff;
  size_t n, cap;
  char *lat;
  int ok = 0;
  if (!utf8) return 0;
  n = strlen(utf8);
  cap = n + 1;
  if (!(lat = (char *)malloc(cap))) return 0;
  n = utf8_to_latin1(utf8, n, lat, cap);
  if ((iff = clip_open(IFFF_WRITE))) {
    if (!PushChunk(iff, ID_FTXT, ID_FORM, IFFSIZE_UNKNOWN)) {
      if (!PushChunk(iff, 0, ID_CHRS, IFFSIZE_UNKNOWN)) {
        ok = WriteChunkBytes(iff, lat, (LONG)n) == (LONG)n;
        if (PopChunk(iff)) ok = 0;
      }
      if (PopChunk(iff)) ok = 0;
    }
    clip_close(iff);
  }
  free(lat);
  return ok;
}

char *clipboard_get_text(void)
{
  struct IFFHandle *iff = clip_open(IFFF_READ);
  Buf b;
  int any = 0;
  if (!iff) return NULL;
  buf_init(&b);
  if (!StopChunk(iff, ID_FTXT, ID_CHRS)) {
    while (!ParseIFF(iff, IFFPARSE_SCAN)) {
      struct ContextNode *cn = CurrentChunk(iff);
      u8 chunk[256];
      LONG left = cn ? cn->cn_Size : 0, got, i;
      any = 1;
      while (left > 0 && (got = ReadChunkBytes(iff, chunk, left > (LONG)sizeof chunk ? (LONG)sizeof chunk : left)) > 0) {
        for (i = 0; i < got; i++) {
          char u[4];
          int k;
          if (!chunk[i]) continue;
          k = utf8_put(u, chunk[i]);                   /* Latin-1 is the first 256 code points */
          buf_add(&b, u, (size_t)k);
        }
        left -= got;
      }
    }
  }
  clip_close(iff);
  if (!any || b.oom) { buf_free(&b); return NULL; }
  if (!b.data) buf_adds(&b, "");
  return buf_take(&b);
}
