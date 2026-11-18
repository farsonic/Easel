/*
 * camraw.c - Easel round 3: camera raw files opened from the full-size JPEG the camera put in them
 * (filesx.h). A demosaic of the sensor data would need four times the picture's memory and minutes
 * on a 68040; every raw format keeps a camera-rendered JPEG, usually at full size, which is what
 * Easel opens: from the TIFF-structured ones (DNG, CR2, NEF/NRW, ARW/SR2/SRF, PEF, ORF, RW2, 3FR,
 * ERF, MEF, MOS, IIQ, KDC, DCR) the largest JPEG in IFD0's chain, the SubIFDs and Panasonic's
 * JpgFromRaw tag; from Fuji's RAF the JPEG its header points at; from Canon's CR3 (ISO media) the
 * largest JPEG among its boxes. The picture is turned upright by the raw's own orientation.
 */
#include <stdlib.h>
#include <string.h>
#include "filesx.h"

static u32 g16(const u8 *p, int le) { return le ? (u32)p[0] | ((u32)p[1] << 8) : ((u32)p[0] << 8) | p[1]; }
static u32 g32(const u8 *p, int le) { return le ? g16(p, 1) | (g16(p + 2, 1) << 16) : (g16(p, 0) << 16) | g16(p + 2, 0); }

typedef struct { const u8 *d; size_t n; int le; size_t best, bestlen; int orient, raw, cfa, visits; } Scan;

/* a JPEG at off (len bytes) that baseline or progressive decoding can take (not lossless SOF3) */
static void consider(Scan *s, u32 off, u32 len)
{
  size_t p;
  if (off < 8 || off >= s->n || len < 64 || len > s->n - off) return;
  if (s->d[off] != 0xFF || s->d[off + 1] != 0xD8) return;
  for (p = off + 2; p + 4 <= (size_t)off + len && s->d[p] == 0xFF; ) {
    int m = s->d[p + 1];
    if (m == 0xC3 || m == 0xC7 || m == 0xCB || m == 0xCF) return;      /* lossless: the raw data itself */
    if (m == 0xC0 || m == 0xC1 || m == 0xC2) break;
    if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { p += 2; continue; }
    p += 2 + g16(s->d + p + 2, 0);
  }
  if (len > s->bestlen) { s->best = off; s->bestlen = len; }
}
static void ifd(Scan *s, u32 off, int depth)
{
  u32 ne, k, jof = 0, jlen = 0, so = 0, sc = 0, comp = 0, next;
  if (depth > 4 || ++s->visits > 64 || off < 8 || off > s->n - 2) return;
  ne = g16(s->d + off, s->le);
  if (ne > (s->n - off - 6) / 12) return;
  for (k = 0; k < ne; k++) {
    const u8 *e = s->d + off + 2 + 12 * k;
    u32 tag = g16(e, s->le), type = g16(e + 2, s->le), cnt = g32(e + 4, s->le), v = g32(e + 8, s->le);
    if (type == 3 && cnt == 1) v = g16(e + 8, s->le);
    switch (tag) {
    case 0x0103: comp = v; if (v >= 32767) s->cfa = 1; break;          /* (the makers' raw compressions) */
    case 0x0106: if (v == 32803 || v == 34892) s->cfa = 1; break;     /* CFA or LinearRaw sensor data */
    case 0x0111: if (cnt == 1) so = v; break;
    case 0x0117: if (cnt == 1) sc = v; break;
    case 0x0201: jof = v; break;
    case 0x0202: jlen = v; break;
    case 0x0112: if (depth == 0 && v >= 1 && v <= 8) s->orient = (int)v; break;
    case 0x002E: if (type == 7) consider(s, v, cnt); break;          /* Panasonic's JpgFromRaw */
    case 0xC612: s->raw = 1; break;                                  /* DNGVersion */
    case 0x014A: {                                                   /* SubIFDs */
      u32 j;
      if (cnt == 1) ifd(s, v, depth + 1);
      else if (v < s->n && cnt <= (s->n - v) / 4 && cnt < 16) for (j = 0; j < cnt; j++) ifd(s, g32(s->d + v + 4 * j, s->le), depth + 1);
      break; }
    case 0x8769: ifd(s, v, depth + 1); break;                        /* (the EXIF IFD: some hide previews there) */
    default: break;
    }
  }
  if (jof && jlen) consider(s, jof, jlen);
  if ((comp == 6 || comp == 7) && so && sc) consider(s, so, sc);
  next = g32(s->d + off + 2 + 12 * ne, s->le);
  if (depth == 0 && next) ifd(s, next, 0);
}

static int known_make(const u8 *d, size_t n, int le)
{
  /* IFD0's Make, for the plain-TIFF raws (NEF, ARW, PEF, 3FR...) that only the camera's name tells apart */
  static const char *const makes[] = { "NIKON", "SONY", "PENTAX", "Hasselblad", "Leaf", "Phase One", "Mamiya", "KODAK", "Kodak", "EPSON", "SEIKO", "RICOH", "Canon", "OLYMPUS", "Panasonic", "LEICA", "SAMSUNG", "FUJIFILM", NULL };
  u32 off = g32(d + 4, le), ne, k;
  if (off < 8 || off > n - 2) return 0;
  ne = g16(d + off, le);
  if (ne > (n - off - 6) / 12) return 0;
  for (k = 0; k < ne; k++) {
    const u8 *e = d + off + 2 + 12 * k;
    if (g16(e, le) == 0x010F && g16(e + 2, le) == 2) {
      u32 cnt = g32(e + 4, le), v = cnt <= 4 ? off + 2 + 12 * k + 8 : g32(e + 8, le);
      int i;
      if (v >= n || cnt > n - v) return 0;
      for (i = 0; makes[i]; i++) { size_t l = strlen(makes[i]); if (cnt >= l && !memcmp(d + v, makes[i], l)) return 1; }
      return 0;
    }
  }
  return 0;
}

/* the kind: 1 TIFF-structured, 2 RAF, 3 CR3; 0 not a raw */
static int raw_kind(const u8 *d, size_t n)
{
  if (n >= 100 && !memcmp(d, "FUJIFILMCCD-RAW", 15)) return 2;
  if (n >= 16 && !memcmp(d + 4, "ftypcrx ", 8)) return 3;
  if (n < 16) return 0;
  if ((d[0] == 'I' && d[1] == 'I' && (d[2] == 'U' || (d[2] == 'R' && (d[3] == 'O' || d[3] == 'S')))) || (d[0] == 'M' && d[1] == 'M' && d[2] == 'O' && d[3] == 'R')) return 1;   /* RW2, ORF */
  if ((d[0] == 'I' && d[1] == 'I' && d[2] == 42 && d[3] == 0) || (d[0] == 'M' && d[1] == 'M' && d[2] == 0 && d[3] == 42)) {
    Scan s;
    int le = d[0] == 'I';
    if (d[8] == 'C' && d[9] == 'R') return 1;                          /* CR2 */
    memset(&s, 0, sizeof s); s.d = d; s.n = n; s.le = le;
    ifd(&s, g32(d + 4, le), 0);
    if (s.raw) return 1;                                               /* DNG */
    return known_make(d, n, le) && s.cfa && s.bestlen ? 1 : 0;
  }
  return 0;
}
int camraw_detect(const u8 *d, size_t n) { return raw_kind(d, n) != 0; }

int camraw_decode(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  Scan s;
  int k = raw_kind(d, n);
  memset(&s, 0, sizeof s);
  s.d = d; s.n = n; s.orient = 1;
  if (k == 1) {
    s.le = d[0] == 'I';
    ifd(&s, g32(d + 4, s.le), 0);
  } else if (k == 2) {
    u32 off = g32(d + 84, 0), len = g32(d + 88, 0);
    consider(&s, off, len);
  } else if (k == 3) {
    /* the largest JPEG among the boxes (the full-size preview is a plain JPEG in a uuid box) */
    size_t p;
    for (p = 8; p + 4 < n; p++)
      if (d[p] == 0xFF && d[p + 1] == 0xD8 && d[p + 2] == 0xFF && p >= 8) {
        /* the box around it gives its size: the 4 bytes before a 'PRVW'/'THMB' payload are no help, so
           the JPEG is measured by its EOI */
        size_t q = p + 2, end = 0;
        while (q + 4 <= n && d[q] == 0xFF) {
          int m = d[q + 1];
          if (m == 0xDA) {
            q += 2 + g16(d + q + 2, 0);
            while (q + 1 < n && !(d[q] == 0xFF && d[q + 1] == 0xD9)) q++;
            if (q + 1 < n) end = q + 2;
            break;
          }
          if (m == 0xD8 || m == 0x01) { q += 2; continue; }
          q += 2 + g16(d + q + 2, 0);
        }
        if (end && end - p < 0xFFFFFFFFUL) { consider(&s, (u32)p, (u32)(end - p)); p = end - 1; }
      }
  }
  if (!s.bestlen) { codec_err(err, errcap, "The camera raw file has no picture Easel can show (Easel opens the JPEG a camera keeps in its raw files)"); return 0; }
  if (!jpeg_read(d + s.best, s.bestlen, out, err, errcap)) return 0;
  if (k == 1 && s.orient > 1 && jpeg_orientation(d + s.best, s.bestlen) == 1) rgba_orient(out, s.orient);
  return 1;
}
