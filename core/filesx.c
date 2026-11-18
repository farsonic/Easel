/*
 * filesx.c - Easel round 3: EXIF kept from file to file (filesx.h).
 */
#include <stdlib.h>
#include <string.h>
#include "filesx.h"
#include "zip.h"

static u32 g16(const u8 *p, int le) { return le ? (u32)p[0] | ((u32)p[1] << 8) : ((u32)p[0] << 8) | p[1]; }
static u32 g32(const u8 *p, int le) { return le ? g16(p, 1) | (g16(p + 2, 1) << 16) : (g16(p, 0) << 16) | g16(p + 2, 0); }
static void p16(u8 *p, int le, u32 v) { if (le) { p[0] = (u8)v; p[1] = (u8)(v >> 8); } else { p[0] = (u8)(v >> 8); p[1] = (u8)v; } }
static void p32(u8 *p, int le, u32 v) { if (le) { p16(p, 1, v & 0xFFFF); p16(p + 2, 1, v >> 16); } else { p16(p, 0, v >> 16); p16(p + 2, 0, v & 0xFFFF); } }

static u8 *dup_block(const u8 *p, u32 n)
{
  u8 *e = (u8 *)malloc(n ? n : 1);
  if (e) memcpy(e, p, n);
  return e;
}
u8 *exif_from_file(const u8 *d, size_t n, u32 *len)
{
  static const u8 pngsig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
  size_t pos;
  if (n >= 4 && d[0] == 0xFF && d[1] == 0xD8) {
    pos = 2;
    while (pos + 4 <= n && d[pos] == 0xFF) {
      int m = d[pos + 1];
      u32 l = ((u32)d[pos + 2] << 8) | d[pos + 3];
      if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) { pos += 2; continue; }
      if (m == 0xDA || m == 0xD9 || l < 2 || l > n - pos - 2) break;
      if (m == 0xE1 && l >= 16 && !memcmp(d + pos + 4, "Exif\0", 5)) { *len = l - 8; return dup_block(d + pos + 10, l - 8); }
      pos += 2 + l;
    }
    return NULL;
  }
  if (n >= 8 && !memcmp(d, pngsig, 8)) {
    pos = 8;
    while (pos + 12 <= n) {
      u32 l = g32(d + pos, 0);
      if (l > n - pos - 12) break;
      if (!memcmp(d + pos + 4, "eXIf", 4) && l >= 8) { *len = l; return dup_block(d + pos + 8, l); }
      if (!memcmp(d + pos + 4, "IDAT", 4)) break;
      pos += 12 + l;
    }
    return NULL;
  }
  if (n >= 20 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4)) {
    pos = 12;
    while (pos + 8 <= n) {
      u32 l = g32(d + pos + 4, 1);
      if (l > n - pos - 8) break;
      if (!memcmp(d + pos, "EXIF", 4) && l >= 8) {
        const u8 *e = d + pos + 8;
        if (l >= 14 && !memcmp(e, "Exif\0", 5)) { e += 6; l -= 6; }   /* (some writers keep JPEG's prefix) */
        *len = l; return dup_block(e, l);
      }
      pos += 8 + l + (l & 1);
    }
  }
  return NULL;
}

/* the TIFF header's byte order and IFD0's offset; 0 if it isn't one */
static int header(const u8 *e, u32 len, int *le, u32 *ifd0)
{
  if (len < 8) return 0;
  if (e[0] == 'I' && e[1] == 'I') *le = 1; else if (e[0] == 'M' && e[1] == 'M') *le = 0; else return 0;
  if (g16(e + 2, *le) != 42) return 0;
  *ifd0 = g32(e + 4, *le);
  return *ifd0 >= 8 && *ifd0 <= len - 2;
}
/* the entry for tag in the IFD at off (its offset in e), 0 if absent */
static u32 find_entry(const u8 *e, u32 len, int le, u32 off, int tag)
{
  u32 k, cnt;
  if (off < 8 || off > len - 2) return 0;
  cnt = g16(e + off, le);
  if (cnt > (len - off - 2) / 12) return 0;
  for (k = 0; k < cnt; k++) if ((int)g16(e + off + 2 + k * 12, le) == tag) return off + 2 + k * 12;
  return 0;
}
static int entry_value(const u8 *e, int le, u32 at, long *v)
{
  u32 type = g16(e + at + 2, le);
  if (type == 3) { *v = (long)g16(e + at + 8, le); return 1; }
  if (type == 4) { *v = (long)g32(e + at + 8, le); return 1; }
  return 0;
}
static int entry_set(u8 *e, int le, u32 at, u32 v)
{
  u32 type = g16(e + at + 2, le);
  if (g32(e + at + 4, le) != 1) return 0;
  if (type == 3 && v <= 0xFFFF) { p16(e + at + 8, le, v); return 1; }
  if (type == 4) { p32(e + at + 8, le, v); return 1; }
  return 0;
}
int exif_normalize(u8 *e, u32 len)
{
  int le;
  u32 ifd0, at, cnt;
  if (!header(e, len, &le, &ifd0)) return 0;
  cnt = g16(e + ifd0, le);
  if (cnt > (len - ifd0 - 2) / 12) return 0;
  if ((at = find_entry(e, len, le, ifd0, 0x0112)) != 0) entry_set(e, le, at, 1);
  if (len - ifd0 - 2 - cnt * 12 >= 4) p32(e + ifd0 + 2 + cnt * 12, le, 0);   /* no IFD1 (the thumbnail) */
  return 1;
}
int exif_get(const u8 *e, u32 len, int tag, long *val)
{
  int le;
  u32 ifd0, at;
  long sub;
  if (!e || !header(e, len, &le, &ifd0)) return 0;
  if ((at = find_entry(e, len, le, ifd0, tag)) != 0) return entry_value(e, le, at, val);
  if ((at = find_entry(e, len, le, ifd0, 0x8769)) != 0 && entry_value(e, le, at, &sub) && sub > 0 && (u32)sub < len &&
      (at = find_entry(e, len, le, (u32)sub, tag)) != 0) return entry_value(e, le, at, val);
  return 0;
}
/* the pixel sizes in the EXIF IFD set to w x h */
static void set_size(u8 *e, u32 len, i32 w, i32 h)
{
  int le;
  u32 ifd0, at;
  long sub;
  if (!header(e, len, &le, &ifd0)) return;
  if ((at = find_entry(e, len, le, ifd0, 0x8769)) == 0 || !entry_value(e, le, at, &sub) || sub <= 0 || (u32)sub >= len) return;
  if ((at = find_entry(e, len, le, (u32)sub, 0xA002)) != 0) entry_set(e, le, at, (u32)w);
  if ((at = find_entry(e, len, le, (u32)sub, 0xA003)) != 0) entry_set(e, le, at, (u32)h);
}

u8 *exif_embed(const Doc *d, int fmt, u8 *file, size_t *len)
{
  u8 *e, *out;
  u32 el;
  size_t at;
  if (!file || !d || !d->exif || d->exiflen < 8) return file;
  if (fmt == FMT_JPEG && (d->exiflen > 65533 - 6 || *len < 4 || file[0] != 0xFF || file[1] != 0xD8)) return file;
  if (fmt != FMT_JPEG && fmt != FMT_PNG) return file;
  el = d->exiflen;
  if (!(e = dup_block(d->exif, el))) { free(file); return NULL; }
  exif_normalize(e, el);
  set_size(e, el, d->w, d->h);
  if (fmt == FMT_JPEG) {
    /* APP1 just after SOI, or after a JFIF APP0 */
    at = 2;
    if (*len >= 6 && file[2] == 0xFF && file[3] == 0xE0) { size_t l = ((size_t)file[4] << 8) | file[5]; if (l + 4 <= *len) at = 4 + l; }
    if (!(out = (u8 *)malloc(*len + el + 10))) { free(e); free(file); return NULL; }
    memcpy(out, file, at);
    out[at] = 0xFF; out[at + 1] = 0xE1; out[at + 2] = (u8)((el + 8) >> 8); out[at + 3] = (u8)(el + 8);
    memcpy(out + at + 4, "Exif\0\0", 6);
    memcpy(out + at + 10, e, el);
    memcpy(out + at + 10 + el, file + at, *len - at);
    *len += el + 10;
  } else {
    /* eXIf before the first IDAT */
    u32 crc;
    at = 8;
    while (at + 12 <= *len) {
      u32 l = g32(file + at, 0);
      if (!memcmp(file + at + 4, "IDAT", 4) || l > *len - at - 12) break;
      at += 12 + l;
    }
    if (!(out = (u8 *)malloc(*len + el + 12))) { free(e); free(file); return NULL; }
    memcpy(out, file, at);
    p32(out + at, 0, el);
    memcpy(out + at + 4, "eXIf", 4);
    memcpy(out + at + 8, e, el);
    crc = crc32_update(0, out + at + 4, el + 4);
    p32(out + at + 8 + el, 0, crc);
    memcpy(out + at + 12 + el, file + at, *len - at);
    *len += el + 12;
  }
  free(e);
  free(file);
  return out;
}

void filesx_load_extras(Doc *doc, const u8 *d, size_t n)
{
  u32 el;
  u8 *e;
  if (!doc || doc->exif) return;
  if ((e = exif_from_file(d, n, &el)) != NULL) {
    if (exif_normalize(e, el)) { doc->exif = e; doc->exiflen = el; } else free(e);
  }
}
