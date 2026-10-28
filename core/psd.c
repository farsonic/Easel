/*
 * psd.c - reading the flattened composite (the "merged image data" at the end of the file) of a
 * Photoshop PSD or PSB: raw, RLE (PackBits rows with a byte-count table), ZIP and ZIP with
 * prediction; bitmap, gray, duotone and multichannel (as gray), indexed (with resource 1047's
 * transparent index), RGB, CMYK and Lab; 1, 8, 16 and 32 bits. Layers aren't read: Photoshop
 * saves the composite precisely so that other programs needn't.
 *
 * Parts ported from PhotoCraft (https://github.com/storytold/photocraft), Copyright (c) 2026
 * ArtCraft Team and the PhotoCraft contributors, used under the MIT License (see Licenses.txt).
 * Ported for PSD/PSB (crates/psd/src: header.rs, file.rs, image_data.rs, compression.rs,
 * pixels.rs, tagged.rs, layer.rs): the header checks, the section walk (PSB's 8-byte lengths, the
 * global tagged blocks with PSB's long-length keys), when the merged image has a transparency
 * channel (a negative layer count, the Mtrn/Mt16/Mt32 markers, no layer info at all, or
 * Photoshop's "Transparency" alpha-channel name), PackBits, the ZIP predictor, the sample
 * conversions (16-bit rounding, 32-bit clamped, naive CMYK) and the removal of the white matte
 * Photoshop puts behind an RGB composite with transparency.
 *
 * Every field is big-endian and read a byte at a time. The composite is converted a row at a time
 * from the file (raw and RLE), so the only big allocation is the picture itself; ZIP data has to be
 * inflated whole first.
 */
#include <stdlib.h>
#include <string.h>
#include "rasters.h"

static u32 be16(const u8 *p) { return ((u32)p[0] << 8) | p[1]; }
static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
/* a PSB 64-bit length; 4GB and up reads as 0xFFFFFFFF, which no check lets through */
static u32 be64(const u8 *p) { return be32(p) ? 0xFFFFFFFFUL : be32(p + 4); }

/* the keys whose tagged-block lengths are 8 bytes in a PSB */
static int long_key(const u8 *k)
{
  static const char keys[] = "LMskLr16Lr32LayrMt16Mt32MtrnAlphFMsklnk2FEidFXidPxSDlnk3lnkEpthsextdextnFELScinfartd";
  size_t i;
  for (i = 0; i + 4 <= sizeof keys - 1; i += 4) if (!memcmp(keys + i, k, 4)) return 1;
  return 0;
}
static int is_sig(const u8 *p) { return !memcmp(p, "8BIM", 4) || !memcmp(p, "8B64", 4); }

/* PackBits into out until it holds cap bytes; 0 if the data runs out or a packet overflows the row
   (as PhotoCraft's decoder, which refuses rather than clips) */
static int unpack_row(const u8 *s, size_t n, u8 *out, size_t cap)
{
  size_t i = 0, o = 0, k;
  while (o < cap) {
    int c;
    if (i >= n) return 0;
    c = s[i++];
    if (c < 128) {
      k = (size_t)c + 1;
      if (k > n - i || k > cap - o) return 0;
      memcpy(out + o, s + i, k);
      o += k; i += k;
    } else if (c > 128) {
      k = 257 - (size_t)c;
      if (i >= n || k > cap - o) return 0;
      memset(out + o, s[i++], k);
      o += k;
    }
  }
  return 1;
}

/* does the alpha-channel name list (resource 1045 in UTF-16, else 1006 in Pascal strings) hold
   exactly `extra` names, the first "Transparency"? */
static int names_transparency(const u8 *r1045, size_t l1045, const u8 *r1006, size_t l1006, u32 extra)
{
  static const char want[] = "Transparency";
  u32 count = 0;
  int first = 0;
  size_t at = 0;
  if (r1045) {
    while (at + 4 <= l1045) {
      u32 len = be32(r1045 + at), i, k;
      at += 4;
      if (len > (l1045 - at) / 2) break;
      if (count == 0) {                                  /* trailing NULs don't count */
        k = len;
        while (k > 0 && be16(r1045 + at + (k - 1) * 2) == 0) k--;
        first = k == 12;
        for (i = 0; i < k && first; i++) if (be16(r1045 + at + i * 2) != (u8)want[i]) first = 0;
      }
      at += (size_t)len * 2;
      count++;
    }
  } else if (r1006) {
    while (at < l1006) {
      u32 len = r1006[at];
      if (len > l1006 - at - 1) break;
      if (count == 0) first = len == 12 && !memcmp(r1006 + at + 1, want, 12);
      at += 1 + len;
      count++;
    }
  }
  return count == extra && first;
}

/* one sample of a row as 0-1 (16 and 32 bits) */
static double samplef(const u8 *row, int depth, i32 x)
{
  if (depth == 16) return be16(row + (size_t)x * 2) / 65535.0;
  {
    union { u32 u; float f; } v;
    double f;
    v.u = be32(row + (size_t)x * 4);
    f = v.f;
    return f == f ? f : 0;                               /* NaN is 0 */
  }
}
static int to8(double v) { return v > 0 ? v < 1 ? (int)(v * 255 + 0.5) : 255 : 0; }
/* one sample as 0-255: 1-bit (a set bit is black), 8, 16 (rounded) and 32 (clamped) */
static int sample8(const u8 *row, int depth, i32 x)
{
  if (depth == 1) return (row[x >> 3] >> (7 - (x & 7))) & 1 ? 0 : 255;
  if (depth == 8) return row[x];
  if (depth == 16) return (int)((be16(row + (size_t)x * 2) * 255UL + 32767) / 65535);
  return to8(samplef(row, depth, x));
}

int psd_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  u32 ver, chans, w, h, depth, mode, len, comp, cc, need, c, x, extra;
  size_t pos, end, rb, nrows, start[5];
  const u8 *pal = NULL, *r1045 = NULL, *r1006 = NULL;
  size_t l1045 = 0, l1006 = 0;
  int psb, have_li = 0, merged_neg = 0, marker = 0, in_block = 0, alpha, trans = -1, ok = 0;
  u8 *rows[5] = { NULL, NULL, NULL, NULL, NULL }, *zip = NULL;
  i32 y;
  const char *msg = "Damaged Photoshop file";
  out->w = out->h = 0; out->px = NULL;
  if (n < 26 || memcmp(d, "8BPS", 4)) { codec_err(err, errcap, "Not a Photoshop file"); return 0; }
  ver = be16(d + 4); chans = be16(d + 12); h = be32(d + 14); w = be32(d + 18); depth = be16(d + 22); mode = be16(d + 24);
  if (ver != 1 && ver != 2) { codec_err(err, errcap, "Unsupported Photoshop file version"); return 0; }
  psb = ver == 2;
  if (chans < 1 || chans > 56) { codec_err(err, errcap, "Damaged Photoshop file (channel count)"); return 0; }
  if (w < 1 || h < 1 || w > MAXDIM || h > MAXDIM || (long)w * h > CODEC_MAXPIXELS) { codec_err(err, errcap, "Bad Photoshop picture size"); return 0; }
  if (depth != 1 && depth != 8 && depth != 16 && depth != 32) { codec_err(err, errcap, "Unsupported Photoshop bit depth"); return 0; }
  switch (mode) {
  case 0: cc = 1; if (depth != 1) { codec_err(err, errcap, "Damaged Photoshop file (bitmap depth)"); return 0; } break;
  case 1: case 8: cc = 1; break;
  case 2: cc = 1; if (depth != 8) { codec_err(err, errcap, "Damaged Photoshop file (indexed depth)"); return 0; } break;
  case 7: cc = 1; break;                                 /* multichannel: the first channel, as gray */
  case 3: case 9: cc = 3; break;
  case 4: cc = 4; break;
  default: codec_err(err, errcap, "Unsupported Photoshop colour mode"); return 0;
  }
  if (depth == 1 && mode != 0) { codec_err(err, errcap, "Unsupported Photoshop bit depth"); return 0; }
  if (chans < cc) { codec_err(err, errcap, "The Photoshop file has fewer channels than its colour mode needs"); return 0; }
  pos = 26;
  /* colour mode data: the palette of an indexed picture (256 reds, greens, blues) */
  if (n - pos < 4 || (len = be32(d + pos)) > n - pos - 4) { codec_err(err, errcap, "Truncated Photoshop file"); return 0; }
  if (mode == 2) {
    if (len < 768) { codec_err(err, errcap, "The indexed Photoshop file has no palette"); return 0; }
    pal = d + pos + 4;
  }
  pos += 4 + (size_t)len;
  /* image resources: alpha-channel names and the transparent index */
  if (n - pos < 4 || (len = be32(d + pos)) > n - pos - 4) { codec_err(err, errcap, "Truncated Photoshop file"); return 0; }
  {
    size_t r = pos + 4, rend = pos + 4 + len;
    while (rend - r >= 12) {
      u32 id = be16(d + r + 4), nl = d[r + 6], sz;
      size_t q = r + 6 + ((nl + 2) & ~1u);               /* the Pascal name, padded to even */
      if (q > rend || rend - q < 4) break;
      sz = be32(d + q);
      if (sz > rend - q - 4) break;
      if (id == 1045) { r1045 = d + q + 4; l1045 = sz; }
      if (id == 1006) { r1006 = d + q + 4; l1006 = sz; }
      if (id == 1047 && sz >= 2) trans = (int)be16(d + q + 4);
      r = q + 4 + sz + (sz & 1);
    }
    pos = rend;
  }
  /* layer and mask information: the layer count's sign, then the global tagged blocks */
  if (n - pos < (psb ? 8u : 4u)) { codec_err(err, errcap, "Truncated Photoshop file"); return 0; }
  len = psb ? be64(d + pos) : be32(d + pos);
  pos += psb ? 8 : 4;
  if (len > n - pos) { codec_err(err, errcap, "Truncated Photoshop file"); return 0; }
  end = pos + len;
  if (len) {
    size_t s = pos, lil;
    int section_layers = 0;
    if (end - s >= (psb ? 8u : 4u)) {
      lil = psb ? be64(d + s) : be32(d + s);
      s += psb ? 8 : 4;
      if (lil > end - s) { codec_err(err, errcap, "Damaged Photoshop file (layer info)"); return 0; }
      if (lil >= 2) {
        int cnt = (i16)be16(d + s);
        have_li = 1; merged_neg = cnt < 0; section_layers = cnt != 0;
      }
      s += lil;
      if (end - s >= 4) {
        u32 gl = be32(d + s);
        int seen = depth == 8 ? section_layers : 0;
        s += 4;
        if (gl <= end - s) {
          s += gl;
          /* the tagged blocks, each padded so the next signature follows (PhotoCraft looks up to
             three bytes on, else takes zero padding to a multiple of four) */
          while (end - s >= 12 && is_sig(d + s)) {
            const u8 *key = d + s + 4;
            int lng = psb && long_key(key);
            u32 bl;
            size_t k;
            if (end - s < (size_t)(lng ? 16 : 12)) break;
            bl = lng ? be64(d + s + 8) : be32(d + s + 8);
            s += lng ? 16 : 12;
            if (bl > end - s) break;
            if (!memcmp(key, "Mtrn", 4) || !memcmp(key, "MTrn", 4) || !memcmp(key, "Mt16", 4) || !memcmp(key, "Mt32", 4)) marker = 1;
            /* 16- and 32-bit files keep their layers in an Lr16 / Lr32 block, which stands in for
               the section's layer info; an 8-bit file may too, when its section has none */
            if (!seen && bl >= 2 && ((depth == 16 && !memcmp(key, "Lr16", 4)) || (depth == 32 && !memcmp(key, "Lr32", 4)) ||
                (depth == 8 && (!memcmp(key, "Layr", 4) || !memcmp(key, "Lr16", 4) || !memcmp(key, "Lr32", 4))))) {
              int cnt = (i16)be16(d + s);
              seen = 1; have_li = 1; merged_neg = cnt < 0; in_block = 1;
            }
            s += bl;
            for (k = 0; k <= 3; k++) if (k == end - s || (end - s >= k + 4 && is_sig(d + s + k))) break;
            if (k > 3) k = (4 - bl % 4) % 4 <= end - s ? (4 - bl % 4) % 4 : 0;
            s += k;
          }
        }
      }
    }
  }
  pos = end;
  /* the merged transparency: an extra channel, when the file says so (or has no layers at all) */
  extra = chans - cc;
  alpha = mode != 7 && extra > 0 && (!have_li || merged_neg || marker || (in_block && names_transparency(r1045, l1045, r1006, l1006, extra)));
  need = cc + (alpha ? 1 : 0);
  if (mode == 7) need = 1;
  /* the image data: compression, then the channels' planes */
  if (n - pos < 2) { codec_err(err, errcap, "The Photoshop file has no composite image"); return 0; }
  comp = be16(d + pos);
  pos += 2;
  rb = depth == 1 ? (w + 7) / 8 : (size_t)w * (depth / 8);
  nrows = (size_t)chans * h;
  if (comp == 0) {
    if ((double)rb * h * need > (double)(n - pos)) { codec_err(err, errcap, "Truncated Photoshop file"); return 0; }
    for (c = 0; c < need; c++) start[c] = pos + (size_t)c * h * rb;
  } else if (comp == 1) {
    size_t cs = psb ? 4 : 2, sum = 0, tab = pos, data;
    u32 i;
    if (nrows > (n - pos) / cs) { codec_err(err, errcap, "Truncated Photoshop file"); return 0; }
    data = pos + nrows * cs;
    /* where each channel's rows start (a row is at most 64 times its packed size, so a short file
       can't claim a huge picture) */
    for (i = 0; i < nrows; i++) {
      u32 cnt = psb ? be32(d + tab + (size_t)i * 4) : be16(d + tab + (size_t)i * 2);
      if (i % h == 0 && i / h < need) start[i / h] = data + sum;
      if (cnt > n - data - sum) { codec_err(err, errcap, "Truncated Photoshop file"); return 0; }
      sum += cnt;
    }
    if ((double)rb * h * need > (double)(n - data) * 64) { codec_err(err, errcap, "Truncated Photoshop file"); return 0; }
  } else if (comp == 2 || comp == 3) {
    double total = (double)rb * h * chans;
    u32 i;
    if (total > (double)(n - pos) * 1032 + 1024 || total > 256.0 * 1024 * 1024) { codec_err(err, errcap, "Truncated Photoshop file"); return 0; }
    if (!(zip = (u8 *)malloc((size_t)total))) { codec_oom = 1; codec_err(err, errcap, "Out of memory"); return 0; }
    if (zlib_decompress(d + pos, n - pos, zip, (size_t)total) != (long)total) { free(zip); codec_err(err, errcap, "Damaged ZIP data in the Photoshop file"); return 0; }
    if (comp == 3) {                                     /* undo the prediction, row by row */
      u8 *tmp = NULL;
      if (depth == 32 && !(tmp = (u8 *)malloc(rb))) { free(zip); codec_oom = 1; codec_err(err, errcap, "Out of memory"); return 0; }
      for (i = 0; i < nrows; i++) {
        u8 *r = zip + (size_t)i * rb;
        size_t k;
        if (depth == 16)
          for (k = 1; k < w; k++) { u32 v = be16(r + k * 2) + be16(r + k * 2 - 2); r[k * 2] = (u8)(v >> 8); r[k * 2 + 1] = (u8)v; }
        else if (depth == 32) {                          /* bytes differenced, in four planes */
          for (k = 1; k < rb; k++) r[k] = (u8)(r[k] + r[k - 1]);
          memcpy(tmp, r, rb);
          for (k = 0; k < w; k++) { r[4 * k] = tmp[k]; r[4 * k + 1] = tmp[w + k]; r[4 * k + 2] = tmp[2 * w + k]; r[4 * k + 3] = tmp[3 * w + k]; }
        } else for (k = 1; k < rb; k++) r[k] = (u8)(r[k] + r[k - 1]);
      }
      free(tmp);
    }
    for (c = 0; c < need; c++) start[c] = (size_t)c * h * rb;
  } else { codec_err(err, errcap, "Unsupported Photoshop compression"); return 0; }

  if (!rgba_alloc(out, (i32)w, (i32)h)) { msg = "Not enough memory for the picture"; goto done; }
  if (comp == 1) for (c = 0; c < need; c++) if (!(rows[c] = (u8 *)malloc(rb))) { codec_oom = 1; msg = "Out of memory"; goto done; }
  for (y = 0; y < (i32)h; y++) {
    const u8 *r[5];
    u8 *o = out->px + (size_t)y * w * 4;
    for (c = 0; c < need; c++) {
      if (comp == 0) r[c] = d + start[c] + (size_t)y * rb;
      else if (comp == 1) {
        u32 i = c * h + (u32)y, cnt = psb ? be32(d + pos + (size_t)i * 4) : be16(d + pos + (size_t)i * 2);
        if (!unpack_row(d + start[c], cnt, rows[c], rb)) { msg = "Damaged RLE data in the Photoshop file"; goto done; }
        start[c] += cnt;
        r[c] = rows[c];
      } else r[c] = zip + start[c] + (size_t)y * rb;
    }
    for (x = 0; x < w; x++, o += 4) {
      int a = alpha ? sample8(r[cc], (int)depth, (i32)x) : 255;
      switch (mode) {
      case 0: case 1: case 7: case 8: o[0] = o[1] = o[2] = (u8)sample8(r[0], (int)depth, (i32)x); break;
      case 2: { int i = r[0][x]; o[0] = pal[i]; o[1] = pal[256 + i]; o[2] = pal[512 + i]; if (i == trans) a = 0; break; }
      case 3:
        if (alpha && depth != 8) {
          /* matted against white: straight = (c + a - 1) / a, at full precision */
          double af = samplef(r[3], (int)depth, (i32)x);
          if (af > 1) af = 1;
          for (c = 0; c < 3; c++) { double cf = samplef(r[c], (int)depth, (i32)x); o[c] = (u8)to8(af > 0 ? (cf + af - 1) / af : cf); }
        } else {
          for (c = 0; c < 3; c++) o[c] = (u8)sample8(r[c], (int)depth, (i32)x);
          if (alpha && a > 0)
            for (c = 0; c < 3; c++) { int v = o[c] + a - 255; v = v > 0 ? (v * 255 + a / 2) / a : 0; o[c] = (u8)(v > 255 ? 255 : v); }
        }
        break;
      case 4: {                                          /* stored inverted: r = c' * k' / 255 */
        int k = sample8(r[3], (int)depth, (i32)x);
        for (c = 0; c < 3; c++) o[c] = (u8)((sample8(r[c], (int)depth, (i32)x) * k + 127) / 255);
        break;
      }
      case 9: rasters_lab(sample8(r[0], (int)depth, (i32)x), sample8(r[1], (int)depth, (i32)x), sample8(r[2], (int)depth, (i32)x), o); break;
      }
      o[3] = (u8)a;
    }
  }
  ok = 1;
done:
  for (c = 0; c < 5; c++) free(rows[c]);
  free(zip);
  if (!ok) { rgba_free(out); codec_err(err, errcap, msg); }
  return ok;
}
