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

/* ================================================================== 0.6: layers
 * Written from Adobe's Photoshop File Formats Specification (the layer and mask information
 * section, the layer records, their channel image data and the additional layer information
 * keys luni, lsct/lsdk, iOpa and lspf), not ported. Reading takes 8- and 16-bit RGB and grey files
 * (16-bit samples reduced to 8 bits, their layers found in the Lr16 block), PSD or PSB; anything
 * else, or a file without layers, or layers that can't be read, opens from its composite as above.
 * Writing makes an 8-bit RGB PSD: every layer cropped to what it shows, RLE compressed; groups as
 * section dividers; masks with their density and feather; then the merged composite (matted on
 * white when it has transparency, as Photoshop stores it).
 */
#include <math.h>

/* Photoshop's blend-mode keys by BM_ value */
static const char psd_keys[BM_N][5] = { "norm", "mul ", "scrn", "over", "dark", "lite", "diff", "lddg", "fsub", "div ",
  "idiv", "sLit", "hLit", "smud", "hue ", "sat ", "colr", "lum ", "diss", "lbrn", "dkCl", "lgCl", "vLit", "lLit",
  "pLit", "hMix", "fdiv" };
static int psd_mode(const u8 *k)
{
  int i;
  if (!memcmp(k, "pass", 4)) return BM_PASS;
  for (i = 0; i < BM_N; i++) if (!memcmp(k, psd_keys[i], 4)) return i;
  return BM_NORMAL;
}
static const char *psd_key(int mode) { return mode == BM_PASS ? "pass" : mode >= 0 && mode < BM_N ? psd_keys[mode] : psd_keys[0]; }
static i32 bes32(const u8 *p) { u32 v = be32(p); return v & 0x80000000UL ? -(i32)(~v) - 1 : (i32)v; }

/* ------------------------------------------------------------------ reading */
#define PSD_MAXCH 24
typedef struct {
  i32 t, l, b, r;
  int nch; int id[PSD_MAXCH]; u32 len[PSD_MAXCH];
  u8 key[4]; int opacity, clip, flags;
  int mask; i32 mt, ml, mb, mr; int mdef, mflags, mdensity, mfeather;
  int real; i32 rt, rl, rb, rr; int rdef, rflags;
  int blendif[4];
  char name[64];
  int section; u8 skey[4]; int has_skey;
  int fill, protect;
} PsdRec;

/* an IEEE double, big-endian, as a number (for the mask feather) */
static double be_double(const u8 *p)
{
  int ex = ((p[0] & 0x7F) << 4) | (p[1] >> 4);
  double m;
  if (!ex || ex == 0x7FF) return 0;
  m = ((double)(((u32)(p[1] & 15) << 16) | ((u32)p[2] << 8) | p[3]) * 4294967296.0 + be32(p + 4)) / 4503599627370496.0;
  m = ldexp(1 + m, ex - 1023);
  return p[0] & 0x80 ? -m : m;
}
/* UTF-16BE (n units) as UTF-8 into a name of cap bytes */
static void utf16_name(char *dst, size_t cap, const u8 *s, u32 n)
{
  size_t o = 0;
  u32 i;
  for (i = 0; i < n; i++) {
    u32 c = be16(s + i * 2);
    char tmp[4];
    int k;
    if (c >= 0xD800 && c < 0xDC00 && i + 1 < n && be16(s + i * 2 + 2) >= 0xDC00 && be16(s + i * 2 + 2) < 0xE000)
      c = 0x10000 + ((c - 0xD800) << 10) + (be16(s + (++i) * 2) - 0xDC00);
    if (!c) break;
    k = utf8_put(tmp, c);
    if (o + (size_t)k >= cap) break;
    memcpy(dst + o, tmp, (size_t)k);
    o += (size_t)k;
  }
  dst[o] = 0;
}
/* a Pascal (Mac Roman, taken as Latin-1) name as UTF-8 */
static void pascal_name(char *dst, size_t cap, const u8 *s, u32 n)
{
  size_t o = 0;
  u32 i;
  for (i = 0; i < n; i++) {
    char tmp[4];
    int k = utf8_put(tmp, s[i]);
    if (o + (size_t)k >= cap) break;
    memcpy(dst + o, tmp, (size_t)k);
    o += (size_t)k;
  }
  dst[o] = 0;
}

/* one layer record at d[pos..end); *next gets where the next one starts. 0 if it is damaged */
static int psd_record(const u8 *d, size_t pos, size_t end, int psb, PsdRec *r, size_t *next)
{
  size_t cs = psb ? 10 : 6, e, eend, q;
  u32 nch, i, el, ml, bl, nl;
  memset(r, 0, sizeof *r);
  if (end - pos < 18) return 0;
  r->t = bes32(d + pos); r->l = bes32(d + pos + 4); r->b = bes32(d + pos + 8); r->r = bes32(d + pos + 12);
  nch = be16(d + pos + 16);
  pos += 18;
  if (nch > PSD_MAXCH || (size_t)nch * cs > end - pos) return 0;
  r->nch = (int)nch;
  for (i = 0; i < nch; i++, pos += cs) {
    r->id[i] = (i16)be16(d + pos);
    r->len[i] = psb ? be64(d + pos + 2) : be32(d + pos + 2);
  }
  if (end - pos < 16 || memcmp(d + pos, "8BIM", 4)) return 0;
  memcpy(r->key, d + pos + 4, 4);
  r->opacity = d[pos + 8]; r->clip = d[pos + 9]; r->flags = d[pos + 10];
  el = be32(d + pos + 12);
  pos += 16;
  if (el > end - pos) return 0;
  e = pos; eend = pos + el;
  *next = eend;
  /* the layer mask */
  if (eend - e < 4 || (ml = be32(d + e)) > eend - e - 4) return 0;
  q = e + 4;
  if (ml >= 18) {
    size_t mend = q + ml, p = q + 18;
    r->mask = 1;
    r->mt = bes32(d + q); r->ml = bes32(d + q + 4); r->mb = bes32(d + q + 8); r->mr = bes32(d + q + 12);
    r->mdef = d[q + 16]; r->mflags = d[q + 17];
    r->mdensity = 255;
    if ((r->mflags & 16) && p < mend) {                  /* the mask parameters */
      int pf = d[p++];
      if ((pf & 1) && p < mend) r->mdensity = d[p++];
      if ((pf & 2) && mend - p >= 8) { double f = be_double(d + p); r->mfeather = f > 0 ? f < 1000 ? (int)(f + 0.5) : 1000 : 0; p += 8; }
      if ((pf & 4) && p < mend) p++;
      if ((pf & 8) && mend - p >= 8) p += 8;
    }
    if (ml != 20 && mend - p >= 18) {                    /* the real user mask (when there is a vector mask too) */
      r->real = 1;
      r->rflags = d[p]; r->rdef = d[p + 1];
      r->rt = bes32(d + p + 2); r->rl = bes32(d + p + 6); r->rb = bes32(d + p + 10); r->rr = bes32(d + p + 14);
    }
  }
  e = q + ml;
  /* blending ranges: the composite grey's (this layer's black and white, then the underlying's) */
  if (eend - e < 4 || (bl = be32(d + e)) > eend - e - 4) return 0;
  if (bl >= 8) {
    const u8 *g = d + e + 4;
    r->blendif[0] = g[0]; r->blendif[1] = 255 - g[3]; r->blendif[2] = g[4]; r->blendif[3] = 255 - g[7];
  }
  e += 4 + bl;
  /* the name, padded to four bytes */
  if (e >= eend) return 1;
  nl = d[e];
  if (nl > eend - e - 1) return 0;
  pascal_name(r->name, sizeof r->name, d + e + 1, nl);
  e += (nl + 4) & ~3u;
  /* the additional information */
  while (e < eend) {
    u32 len;
    const u8 *key;
    int k, lng;
    for (k = 0; k < 4 && eend - e >= 12 && !is_sig(d + e); k++) e++;   /* writers that pad differently */
    if (eend - e < 12 || !is_sig(d + e)) break;
    key = d + e + 4;
    lng = psb && long_key(key);
    if (eend - e < (size_t)(lng ? 16 : 12)) break;
    len = lng ? be64(d + e + 8) : be32(d + e + 8);
    e += lng ? 16 : 12;
    if (len > eend - e) break;
    if (!memcmp(key, "luni", 4) && len >= 4) {
      u32 cnt = be32(d + e);
      if (cnt <= (len - 4) / 2) utf16_name(r->name, sizeof r->name, d + e + 4, cnt);
    } else if ((!memcmp(key, "lsct", 4) || !memcmp(key, "lsdk", 4)) && len >= 4) {
      r->section = (int)be32(d + e);
      if (len >= 12 && is_sig(d + e + 4)) { memcpy(r->skey, d + e + 8, 4); r->has_skey = 1; }
    } else if (!memcmp(key, "iOpa", 4) && len >= 1) r->fill = d[e] + 1;
    else if (!memcmp(key, "lspf", 4) && len >= 4) r->protect = (int)be32(d + e);
    e += len;
  }
  return 1;
}

/* where a channel's samples go: a canvas of w x h, stride bytes a pixel, at offset off */
typedef struct { u8 *dst; i32 w, h; int stride, off; } Place;
/* one channel's data (compression word first) for a rect at l, t of cw x ch: rows inside the canvas
   placed, 16-bit samples rounded to 8. 0 if the data is damaged */
static int psd_channel(const u8 *p, size_t len, int psb, int depth, i32 l, i32 t, i32 cw, i32 ch, const Place *pl)
{
  size_t bps = depth == 16 ? 2 : 1, rb = (size_t)cw * bps, pos = 2;
  u32 comp;
  u8 *row = NULL, *zip = NULL;
  i32 y, x;
  int ok = 0;
  if (len < 2) return 0;
  if (cw <= 0 || ch <= 0) return 1;
  comp = be16(p);
  if (comp == 0) { if ((double)rb * ch > (double)(len - 2)) return 0; }
  else if (comp == 1) {
    size_t cs = psb ? 4 : 2;
    if ((size_t)ch > (len - 2) / cs) return 0;
    pos = 2 + (size_t)ch * cs;
    if ((double)rb * ch > (double)(len - pos) * 128 + 1024) return 0;
  } else if (comp == 2 || comp == 3) {
    double total = (double)rb * ch;
    if (total > (double)(len - 2) * 1032 + 1024 || total > 256.0 * 1024 * 1024) return 0;
    if (!(zip = (u8 *)malloc((size_t)total))) { codec_oom = 1; return 0; }
    if (zlib_decompress(p + 2, len - 2, zip, (size_t)total) != (long)total) { free(zip); return 0; }
    if (comp == 3)
      for (y = 0; y < ch; y++) {
        u8 *r = zip + (size_t)y * rb;
        size_t k;
        if (depth == 16) for (k = 1; k < (size_t)cw; k++) { u32 v = be16(r + k * 2) + be16(r + k * 2 - 2); r[k * 2] = (u8)(v >> 8); r[k * 2 + 1] = (u8)v; }
        else for (k = 1; k < rb; k++) r[k] = (u8)(r[k] + r[k - 1]);
      }
  } else return 0;
  if (comp == 1 && !(row = (u8 *)malloc(rb))) { codec_oom = 1; return 0; }
  for (y = 0; y < ch; y++) {
    const u8 *r;
    i32 cy = t + y;
    if (comp == 0) r = p + 2 + (size_t)y * rb;
    else if (comp == 1) {
      u32 cnt = psb ? be32(p + 2 + (size_t)y * 4) : be16(p + 2 + (size_t)y * 2);
      if (cnt > len - pos) goto done;
      if (cy >= 0 && cy < pl->h && !unpack_row(p + pos, cnt, row, rb)) goto done;
      pos += cnt;
      r = row;
    } else r = zip + (size_t)y * rb;
    if (cy < 0 || cy >= pl->h) continue;
    for (x = l < 0 ? -l : 0; x < cw && l + x < pl->w; x++) {
      int v = depth == 16 ? (int)((be16(r + (size_t)x * 2) * 255UL + 32767) / 65535) : r[x];
      pl->dst[((size_t)cy * pl->w + (size_t)(l + x)) * pl->stride + pl->off] = (u8)v;
    }
  }
  ok = 1;
done:
  free(row); free(zip);
  return ok;
}

/* the layer info (from the count on) of a PSD: in its own section, or for 16-bit files the Lr16
   block among the global tagged blocks. 0 if there is none */
static int psd_layer_info(const u8 *d, size_t n, int psb, u32 depth, size_t *start, size_t *len)
{
  size_t pos = 26, end;
  u32 l;
  int k;
  for (k = 0; k < 2; k++) {                              /* colour mode data, image resources */
    if (n - pos < 4 || (l = be32(d + pos)) > n - pos - 4) return 0;
    pos += 4 + l;
  }
  if (n - pos < (psb ? 8u : 4u)) return 0;
  l = psb ? be64(d + pos) : be32(d + pos);
  pos += psb ? 8 : 4;
  if (l > n - pos) return 0;
  end = pos + l;
  if (end - pos < (psb ? 8u : 4u)) return 0;
  l = psb ? be64(d + pos) : be32(d + pos);
  pos += psb ? 8 : 4;
  if (l > end - pos) return 0;
  if (l >= 2 && be16(d + pos)) { *start = pos; *len = l; return 1; }
  pos += l;
  if (end - pos < 4 || (l = be32(d + pos)) > end - pos - 4) return 0;
  pos += 4 + l;                                          /* the global layer mask */
  while (end - pos >= 12 && is_sig(d + pos)) {
    const u8 *key = d + pos + 4;
    int lng = psb && long_key(key);
    size_t k2;
    if (end - pos < (size_t)(lng ? 16 : 12)) break;
    l = lng ? be64(d + pos + 8) : be32(d + pos + 8);
    pos += lng ? 16 : 12;
    if (l > end - pos) break;
    if (l >= 2 && ((depth == 16 && !memcmp(key, "Lr16", 4)) || (depth == 8 && !memcmp(key, "Layr", 4)))) { *start = pos; *len = l; return 1; }
    pos += l;
    for (k2 = 0; k2 <= 3; k2++) if (k2 == end - pos || (end - pos >= k2 + 4 && is_sig(d + pos + k2))) break;
    if (k2 <= 3) pos += k2;
  }
  return 0;
}

/* the resolution (resource 1005), dots per inch; 0 if none */
static int psd_dpi(const u8 *d, size_t n)
{
  size_t pos = 26, r, rend;
  u32 len;
  if (n - pos < 4 || (len = be32(d + pos)) > n - pos - 4) return 0;
  pos += 4 + len;
  if (n - pos < 4 || (len = be32(d + pos)) > n - pos - 4) return 0;
  r = pos + 4; rend = r + len;
  while (rend - r >= 12) {
    u32 id = be16(d + r + 4), nl = d[r + 6], sz;
    size_t q = r + 6 + ((nl + 2) & ~1u);
    if (q > rend || rend - q < 4) break;
    sz = be32(d + q);
    if (sz > rend - q - 4) break;
    if (id == 1005 && sz >= 4) { u32 v = (be32(d + q + 4) + 32768) >> 16; return v > 0 && v < 65536 ? (int)v : 0; }
    r = q + 4 + sz + (sz & 1);
  }
  return 0;
}

/* the records turned into the document's layers, bottom first: a bounding divider opens a group
   (its members follow, one deeper), the folder record above them is the group's header */
static Doc *psd_layers(const u8 *d, int psb, u32 w, u32 h, u32 depth, int gray, size_t li, size_t lilen, char *err, size_t errcap)
{
  PsdRec *rec = NULL;
  Doc *doc = NULL;
  int cnt, i, c, lev = 0, k;
  size_t pos = li + 2, end = li + lilen, data;
  cnt = (i16)be16(d + li);
  if (cnt < 0) cnt = -cnt;
  if (cnt < 1 || (size_t)cnt > lilen / 18) return NULL;
  if (!(rec = (PsdRec *)malloc(sizeof(PsdRec) * (size_t)cnt))) { codec_oom = 1; return NULL; }
  for (i = 0; i < cnt; i++) if (!psd_record(d, pos, end, psb, &rec[i], &pos)) goto fail;
  data = pos;
  if (!(doc = doc_new((i32)w, (i32)h, 0, 1))) { codec_oom = 1; goto fail; }
  layer_free(doc->lay[0]);
  doc->nlay = 0;
  for (i = 0; i < cnt; i++) {
    PsdRec *r = &rec[i];
    Layer *l = NULL;
    size_t at = data;
    int group = r->section == 1 || r->section == 2, alpha = 0;
    for (c = 0; c < r->nch; c++) { if (r->len[c] > end - data) goto fail; data += r->len[c]; }
    if (r->section == 3) { lev++; continue; }            /* the bottom of a group */
    if (group && lev > 0) lev--;
    if (doc->nlay >= MAXLAYERS) continue;
    if (group) { if ((l = (Layer *)calloc(1, sizeof *l)) != NULL) { strcpy(l->name, "Group"); l->opacity = 255; l->visible = 1; } }
    else l = layer_new(doc, "Layer");
    if (!l) { codec_oom = 1; goto oom; }
    doc->lay[doc->nlay++] = l;
    if (r->name[0]) strcpy(l->name, r->name);
    l->kind = (u8)(group ? LK_GROUP : LK_PIXEL);
    l->depth = (u8)(lev > 200 ? 200 : lev);
    l->opacity = (u8)r->opacity;
    l->blend = (u8)psd_mode(group && r->has_skey ? r->skey : r->key);
    if (!group && l->blend == BM_PASS) l->blend = BM_NORMAL;
    l->visible = !(r->flags & 2);
    l->clip = (u8)(!group && r->clip);
    l->alpha_lock = (u8)((r->flags & 1) || (r->protect & 1));
    l->locked = (u8)((r->protect & 2) != 0);
    l->lock_pos = (u8)((r->protect & 4) != 0);
    l->lock_all = (u8)((r->protect & 0x80000000UL) != 0);
    l->collapsed = (u8)(r->section == 2);
    if (r->fill) l->fill_cut = (u8)(255 - (r->fill - 1));
    for (k = 0; k < 4; k++) l->blendif[k] = (u8)r->blendif[k];
    /* the pixels */
    for (c = 0; c < r->nch; c++) if (r->id[c] == -1) alpha = 1;
    if (!group) {
      i32 lw = r->r - r->l, lh = r->b - r->t, y, x;
      int ok = lw > 0 && lh > 0 && lw <= 3 * MAXDIM && lh <= 3 * MAXDIM;
      if (ok && !alpha)                                  /* no transparency channel: opaque where the layer is */
        for (y = r->t < 0 ? 0 : r->t; y < r->b && y < (i32)h; y++)
          for (x = r->l < 0 ? 0 : r->l; x < r->r && x < (i32)w; x++) l->px[((size_t)y * w + x) * 4 + 3] = 255;
      for (c = 0; c < r->nch && ok; c++) {
        Place pl;
        int id = r->id[c];
        pl.dst = l->px; pl.w = (i32)w; pl.h = (i32)h; pl.stride = 4;
        if (id >= 0 && id <= 2 && !(gray && id > 0)) {
          pl.off = id;
          if (!psd_channel(d + at, r->len[c], psb, (int)depth, r->l, r->t, lw, lh, &pl)) { if (codec_oom) goto oom; ok = 0; }
        } else if (id == -1) {
          pl.off = 3;
          if (!psd_channel(d + at, r->len[c], psb, (int)depth, r->l, r->t, lw, lh, &pl)) { if (codec_oom) goto oom; ok = 0; }
        }
        at += r->len[c];
      }
      if (gray) for (k = 0; k < (int)(w * h); k++) l->px[k * 4 + 1] = l->px[k * 4 + 2] = l->px[k * 4];
    }
    /* the mask: the real user mask when there is one, else the user mask */
    if (r->mask) {
      int want = r->real ? -3 : -2, have = 0;
      for (c = 0; c < r->nch; c++) if (r->id[c] == want) have = 1;
      if (have && (l->mask = (u8 *)malloc((size_t)w * h)) != NULL) {
        i32 mt = r->real ? r->rt : r->mt, ml = r->real ? r->rl : r->ml, mb = r->real ? r->rb : r->mb, mr = r->real ? r->rr : r->mr;
        int fl = r->real ? r->rflags : r->mflags;
        Place pl;
        memset(l->mask, r->real ? r->rdef : r->mdef, (size_t)w * h);
        pl.dst = l->mask; pl.w = (i32)w; pl.h = (i32)h; pl.stride = 1; pl.off = 0;
        for (c = 0, at = data; c < r->nch; c++) at -= r->len[c];
        for (c = 0; c < r->nch; c++) {
          if (r->id[c] == want && mr - ml > 0 && mb - mt > 0 && mr - ml <= 3 * MAXDIM && mb - mt <= 3 * MAXDIM)
            psd_channel(d + at, r->len[c], psb, (int)depth, ml, mt, mr - ml, mb - mt, &pl);
          at += r->len[c];
        }
        l->mask_on = !(fl & 2);
        l->mask_linked = !(fl & 1);
        l->mask_fade = (u8)(255 - r->mdensity);
        l->mask_feather = (u16)r->mfeather;
      } else if (have) { codec_oom = 1; goto oom; }
    }
  }
  free(rec);
  if (!doc->nlay) { doc_free(doc); return NULL; }
  /* depths made consistent: a member can't be deeper than one below its group */
  for (i = doc->nlay - 1, k = 0; i >= 0; i--) {
    if (doc->lay[i]->depth > k) doc->lay[i]->depth = (u8)k;
    k = doc->lay[i]->depth + (doc->lay[i]->kind == LK_GROUP);
  }
  doc->active = doc->nlay - 1;
  return doc;
oom:
  codec_err(err, errcap, "Not enough memory for the Photoshop file's layers");
fail:
  free(rec);
  if (doc) doc_free(doc);
  return NULL;
}

/* the layered document when the file has layers it can read, else its composite as one layer */
Doc *psd_read_doc(const u8 *d, size_t n, char *err, size_t errcap)
{
  u32 w, h, depth, mode;
  int psb, dpi;
  size_t li, lilen;
  Doc *doc = NULL;
  Rgba im;
  codec_oom = 0;
  if (n >= 26 && !memcmp(d, "8BPS", 4) && (be16(d + 4) == 1 || be16(d + 4) == 2)) {
    psb = be16(d + 4) == 2;
    h = be32(d + 14); w = be32(d + 18); depth = be16(d + 22); mode = be16(d + 24);
    if ((mode == 1 || mode == 3) && (depth == 8 || depth == 16) && w >= 1 && h >= 1 && w <= MAXDIM && h <= MAXDIM &&
        (long)w * h <= CODEC_MAXPIXELS && psd_layer_info(d, n, psb, depth, &li, &lilen)) {
      doc = psd_layers(d, psb, w, h, depth, mode == 1, li, lilen, err, errcap);
      if (!doc && codec_oom) return NULL;
    }
  }
  if (!doc) {
    if (!psd_read(d, n, &im, err, errcap)) return NULL;
    if (!(doc = doc_adopt(im.w, im.h, im.px))) { rgba_free(&im); codec_oom = 1; codec_err(err, errcap, "Not enough memory for the picture"); return NULL; }
  }
  if ((dpi = psd_dpi(d, n)) > 0) doc->dpi = (u16)dpi;
  doc_forget_undo(doc);
  doc->dirty = 0;
  return doc;
}

/* ------------------------------------------------------------------ writing */
static void w16(Buf *b, u32 v) { buf_addc(b, (int)((v >> 8) & 255)); buf_addc(b, (int)(v & 255)); }
static void w32(Buf *b, u32 v) { w16(b, v >> 16); w16(b, v & 0xFFFF); }

/* PackBits for one row into b (or just counted when b is NULL); its length */
static size_t pack_row(Buf *b, const u8 *s, size_t n)
{
  size_t i = 0, out = 0;
  while (i < n) {
    size_t r = 1;
    while (i + r < n && r < 128 && s[i + r] == s[i]) r++;
    if (r >= 2) {
      if (b) { buf_addc(b, (int)(257 - r) & 255); buf_addc(b, s[i]); }
      out += 2; i += r;
    } else {
      size_t l = 1;
      while (i + l < n && l < 128 && (i + l + 1 >= n || s[i + l] != s[i + l + 1])) l++;
      if (b) { buf_addc(b, (int)(l - 1)); buf_add(b, s + i, l); }
      out += 1 + l; i += l;
    }
  }
  return out;
}

/* one record of the layer list: a layer, a group's header, or the divider at the bottom of a group */
enum { PR_LAYER, PR_GROUP, PR_END };
typedef struct {
  int i, what;
  Box b, mb;                          /* the pixels' and the mask's rect (empty: none) */
  int mdef;
  u8 *px;                             /* the pixels (a fill layer's rendered; NULL for none) */
  int ownpx;
  u32 len[5]; int nch;
} PsdOut;

/* the records for layers lo..hi-1, topmost first (groups as header, members, divider) */
static int psd_list(Doc *doc, int lo, int hi, PsdOut *o, int k)
{
  int i = hi - 1;
  while (i >= lo) {
    Layer *l = doc->lay[i];
    if (l->kind == LK_GROUP) {
      int s = i;
      while (s > lo && doc->lay[s - 1]->depth > l->depth) s--;
      o[k].i = i; o[k++].what = PR_GROUP;
      k = psd_list(doc, s, i, o, k);
      o[k].i = i; o[k++].what = PR_END;
      i = s - 1;
    } else { o[k].i = i; o[k++].what = PR_LAYER; i--; }
  }
  return k;
}

/* channel c of a record's rect, row y, into row (c: 0-2 colour, 3 alpha, 4 the mask) */
static void out_row(const Doc *doc, const PsdOut *r, int c, i32 y, u8 *row)
{
  i32 x;
  if (c == 4) { memcpy(row, doc->lay[r->i]->mask + (size_t)y * doc->w + r->mb.x0, (size_t)(r->mb.x1 - r->mb.x0)); return; }
  for (x = r->b.x0; x < r->b.x1; x++) row[x - r->b.x0] = r->px[((size_t)y * doc->w + x) * 4 + (c == 3 ? 3 : c)];
}
/* one channel's data (RLE, or nothing for an empty rect), written to b or just measured */
static u32 out_channel(Buf *b, const Doc *doc, const PsdOut *r, int c, u8 *row)
{
  Box bx = c == 4 ? r->mb : r->b;
  i32 y;
  u32 total = 2;
  if (box_empty(bx)) { if (b) w16(b, 0); return 2; }
  if (b) w16(b, 1);
  for (y = bx.y0; y < bx.y1; y++) {
    size_t l;
    out_row(doc, r, c, y, row);
    l = pack_row(NULL, row, (size_t)(bx.x1 - bx.x0));
    if (b) w16(b, (u32)l);
    total += 2 + (u32)l;
  }
  if (b) for (y = bx.y0; y < bx.y1; y++) { out_row(doc, r, c, y, row); pack_row(b, row, (size_t)(bx.x1 - bx.x0)); }
  return total;
}

/* the part of a canvas-size RGBA with any alpha */
static Box alpha_box(const u8 *px, i32 w, i32 h)
{
  i32 x, y, x0 = w, y0 = h, x1 = 0, y1 = 0;
  for (y = 0; y < h; y++) {
    const u8 *p = px + (size_t)y * w * 4;
    for (x = 0; x < w; x++) if (p[x * 4 + 3]) { if (x < x0) x0 = x; if (x >= x1) x1 = x + 1; if (y < y0) y0 = y; y1 = y + 1; }
  }
  return box_make(x0, y0, x1, y1);
}
/* the part of a mask that differs from def */
static Box mask_box(const u8 *m, i32 w, i32 h, int def)
{
  i32 x, y, x0 = w, y0 = h, x1 = 0, y1 = 0;
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) if (m[(size_t)y * w + x] != def) { if (x < x0) x0 = x; if (x >= x1) x1 = x + 1; if (y < y0) y0 = y; y1 = y + 1; }
  return box_make(x0, y0, x1, y1);
}

/* the layer record itself (rect, channels, blend, mask data, ranges, name, tagged blocks) */
static void put_record(Buf *b, Doc *doc, const PsdOut *r, int id)
{
  static const int ids[5] = { -1, 0, 1, 2, -2 };
  Layer *l = doc->lay[r->i];
  Buf x;
  char name[256];
  size_t nl, i;
  int c, flags = 8, prot = 0, mode = l->blend;
  w32(b, (u32)r->b.y0); w32(b, (u32)r->b.x0); w32(b, (u32)r->b.y1); w32(b, (u32)r->b.x1);
  w16(b, (u32)r->nch);
  for (c = 0; c < r->nch; c++) { w16(b, (u32)ids[c] & 0xFFFF); w32(b, r->len[c]); }
  buf_add(b, "8BIM", 4);
  if (r->what == PR_END) mode = BM_NORMAL;
  else if (r->what == PR_LAYER && mode == BM_PASS) mode = BM_NORMAL;
  buf_add(b, psd_key(mode), 4);
  buf_addc(b, r->what == PR_END ? 255 : l->opacity);
  buf_addc(b, r->what == PR_LAYER && l->clip ? 1 : 0);
  if (l->alpha_lock && r->what == PR_LAYER) flags |= 1;
  if (!l->visible && r->what != PR_END) flags |= 2;
  if (r->what != PR_LAYER) flags |= 16;
  buf_addc(b, flags); buf_addc(b, 0);
  /* the extra data, built apart to know its length */
  buf_init(&x);
  if (r->what == PR_LAYER && l->mask) {
    int par = (l->mask_fade ? 1 : 0) | (l->mask_feather ? 2 : 0);
    size_t ml = 18 + (par ? 1 + (par & 1) + (par & 2 ? 8 : 0) : 0);
    if (ml < 20) ml = 20;
    if (par && ml == 20) ml = 22;                        /* 20 would read as the plain record with padding */
    if (ml & 1) ml++;
    w32(&x, (u32)ml);
    w32(&x, (u32)r->mb.y0); w32(&x, (u32)r->mb.x0); w32(&x, (u32)r->mb.y1); w32(&x, (u32)r->mb.x1);
    buf_addc(&x, r->mdef);
    buf_addc(&x, (l->mask_linked ? 0 : 1) | (l->mask_on ? 0 : 2) | (par ? 16 : 0));
    i = 18;
    if (par) {
      buf_addc(&x, par); i++;
      if (par & 1) { buf_addc(&x, 255 - l->mask_fade); i++; }
      if (par & 2) {                                     /* the feather, a whole number of pixels, as a double */
        u32 v = l->mask_feather, e = 0;
        while ((v >> (e + 1)) != 0) e++;
        w32(&x, ((1023 + e) << 20) | ((v - (1u << e)) << (20 - e))); w32(&x, 0);
        i += 8;
      }
    }
    for (; i < ml; i++) buf_addc(&x, 0);
  } else w32(&x, 0);
  /* blending ranges: composite grey (Blend If), then each colour channel's left open */
  w32(&x, 32);
  buf_addc(&x, l->blendif[0]); buf_addc(&x, l->blendif[0]); buf_addc(&x, 255 - l->blendif[1]); buf_addc(&x, 255 - l->blendif[1]);
  buf_addc(&x, l->blendif[2]); buf_addc(&x, l->blendif[2]); buf_addc(&x, 255 - l->blendif[3]); buf_addc(&x, 255 - l->blendif[3]);
  for (c = 0; c < 3; c++) { buf_addc(&x, 0); buf_addc(&x, 0); buf_addc(&x, 255); buf_addc(&x, 255); buf_addc(&x, 0); buf_addc(&x, 0); buf_addc(&x, 255); buf_addc(&x, 255); }
  /* the name in Latin-1 ('?' for anything else), padded to four bytes */
  {
    const char *s = r->what == PR_END ? "</Layer group>" : l->name;
    nl = utf8_to_latin1(s, strlen(s), name, sizeof name);
    if (nl > 255) nl = 255;
    for (i = 0; i < nl; i++) if ((u8)name[i] >= 128) name[i] = '?';
    buf_addc(&x, (int)nl); buf_add(&x, name, nl);
    for (i = nl + 1; i & 3; i++) buf_addc(&x, 0);
  }
  /* luni: the name in UTF-16 */
  if (r->what != PR_END) {
    u16 u[130];
    int nu = 0, used;
    size_t o = 0, sl = strlen(l->name);
    while (o < sl && nu < 126) {
      u32 cp = utf8_get(l->name + o, sl - o, &used);
      o += (size_t)used;
      if (cp >= 0x10000) { cp -= 0x10000; u[nu++] = (u16)(0xD800 + (cp >> 10)); u[nu++] = (u16)(0xDC00 + (cp & 0x3FF)); }
      else u[nu++] = (u16)cp;
    }
    buf_add(&x, "8BIMluni", 8);
    w32(&x, (u32)(4 + nu * 2 + ((nu & 1) ? 2 : 0)));
    w32(&x, (u32)nu);
    for (c = 0; c < nu; c++) w16(&x, u[c]);
    if (nu & 1) w16(&x, 0);
  }
  /* lsct: the group's header (open or closed, its blend) and the divider */
  if (r->what == PR_GROUP) { buf_add(&x, "8BIMlsct", 8); w32(&x, 12); w32(&x, l->collapsed ? 2 : 1); buf_add(&x, "8BIM", 4); buf_add(&x, psd_key(l->blend), 4); }
  if (r->what == PR_END) { buf_add(&x, "8BIMlsct", 8); w32(&x, 4); w32(&x, 3); }
  if (r->what != PR_END && l->fill_cut) { buf_add(&x, "8BIMiOpa", 8); w32(&x, 4); buf_addc(&x, 255 - l->fill_cut); buf_addc(&x, 0); buf_addc(&x, 0); buf_addc(&x, 0); }
  if (r->what != PR_END) {
    if (l->alpha_lock) prot |= 1;
    if (l->locked) prot |= 2;
    if (l->lock_pos) prot |= 4;
    if (prot || l->lock_all) { buf_add(&x, "8BIMlspf", 8); w32(&x, 4); w32(&x, (u32)prot | (l->lock_all ? 0x80000000UL : 0)); }
  }
  buf_add(&x, "8BIMlyid", 8); w32(&x, 4); w32(&x, (u32)id);
  w32(b, (u32)x.len);
  buf_add(b, x.data, x.len);
  if (x.oom) b->oom = 1;
  buf_free(&x);
}

/* a fill layer's own pixels (as if alone, at full opacity and Normal) */
static u8 *fill_pixels(Doc *doc, int i)
{
  Layer *l = doc->lay[i];
  u8 *px = (u8 *)malloc((size_t)doc->w * doc->h * 4);
  u8 op = l->opacity, bl = l->blend, fc = l->fill_cut, vis = l->visible, mon = l->mask_on, cl = l->clip;
  if (!px) return NULL;
  l->opacity = 255; l->blend = BM_NORMAL; l->fill_cut = 0; l->visible = 1; l->mask_on = 0; l->clip = 0;
  doc_composite_range(doc, i, i + 1, l->depth, doc_box(doc), px, (long)doc->w * 4, 0);
  l->opacity = op; l->blend = bl; l->fill_cut = fc; l->visible = vis; l->mask_on = mon; l->clip = cl;
  return px;
}

/* header, resources (resolution, XMP), the layers, then the merged composite */
u8 *psd_write_doc(Doc *doc, size_t *len)
{
  PsdOut *o = NULL;
  Buf b, x;
  u8 *row = NULL, *comp = NULL, *res = NULL;
  int nrec = 0, k, c, alpha = 0, nch;
  size_t lmstart, listart, i;
  i32 y, w = doc->w, h = doc->h;
  u16 *counts = NULL;
  buf_init(&b);
  if (!(o = (PsdOut *)calloc((size_t)doc->nlay * 2 + 1, sizeof *o)) || !(row = (u8 *)malloc((size_t)w * 4 + 16)) ||
      !(comp = (u8 *)malloc((size_t)w * h * 4))) goto done;
  nrec = psd_list(doc, 0, doc->nlay, o, 0);
  /* bottom first, as the file has them */
  for (k = 0; k < nrec / 2; k++) { PsdOut t = o[k]; o[k] = o[nrec - 1 - k]; o[nrec - 1 - k] = t; }
  for (k = 0; k < nrec; k++) {
    PsdOut *r = &o[k];
    Layer *l = doc->lay[r->i];
    r->b = box_make(0, 0, 0, 0); r->mb = box_make(0, 0, 0, 0); r->mdef = 255;
    r->nch = 4;
    if (r->what == PR_LAYER) {
      if (l->kind == LK_FILL) { if (!(r->px = fill_pixels(doc, r->i))) goto done; r->ownpx = 1; }
      else r->px = l->kind == LK_PIXEL ? l->px : NULL;
      if (r->px) r->b = alpha_box(r->px, w, h);
      if (l->mask) {
        r->mdef = l->mask[0] == 0 ? 0 : 255;
        r->mb = mask_box(l->mask, w, h, r->mdef);
        r->nch = 5;
      }
    }
    for (c = 0; c < r->nch; c++) r->len[c] = r->px || c == 4 ? out_channel(NULL, doc, r, c == 0 ? 3 : c == 4 ? 4 : c - 1, row) : 2;
  }
  doc_composite(doc, doc_box(doc), comp, (long)w * 4);
  for (i = 0; i < (size_t)w * h; i++) if (comp[i * 4 + 3] != 255) { alpha = 1; break; }
  nch = alpha ? 4 : 3;
  /* the header: version 1, nch channels, 8 bits, RGB */
  buf_add(&b, "8BPS\0\1\0\0\0\0\0\0", 12);
  w16(&b, (u32)nch); w32(&b, (u32)h); w32(&b, (u32)w); w16(&b, 8); w16(&b, 3);
  w32(&b, 0);                                            /* no colour mode data */
  /* image resources: 1005 resolution, 1060 XMP */
  buf_init(&x);
  {
    u32 dpi = doc->dpi ? doc->dpi : 72;
    buf_add(&x, "8BIM\003\355\0\0", 8); w32(&x, 16);
    w32(&x, dpi << 16); w16(&x, 1); w16(&x, 1); w32(&x, dpi << 16); w16(&x, 1); w16(&x, 1);
  }
  if (!docinfo_empty(&doc->info)) {
    Buf p;
    buf_init(&p);
    xmp_write(&p, &doc->info);
    buf_add(&x, "8BIM\004\044\0\0", 8); w32(&x, (u32)p.len); buf_add(&x, p.data, p.len);
    if (p.len & 1) buf_addc(&x, 0);
    if (p.oom) x.oom = 1;
    buf_free(&p);
  }
  w32(&b, (u32)x.len); buf_add(&b, x.data, x.len);
  if (x.oom) b.oom = 1;
  buf_free(&x);
  /* the layer and mask information */
  lmstart = b.len; w32(&b, 0);
  listart = b.len; w32(&b, 0);
  w16(&b, (u32)(alpha ? -nrec : nrec) & 0xFFFF);
  for (k = 0; k < nrec; k++) put_record(&b, doc, &o[k], k + 1);
  for (k = 0; k < nrec; k++)
    for (c = 0; c < o[k].nch; c++) {
      if (o[k].px || c == 4) out_channel(&b, doc, &o[k], c == 0 ? 3 : c == 4 ? 4 : c - 1, row);
      else w16(&b, 0);
    }
  while ((b.len - listart - 4) & 3) buf_addc(&b, 0);
  if (!b.oom) { u8 *p = (u8 *)b.data + listart; u32 v = (u32)(b.len - listart - 4); p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
  w32(&b, 0);                                            /* no global layer mask */
  if (!b.oom) { u8 *p = (u8 *)b.data + lmstart; u32 v = (u32)(b.len - lmstart - 4); p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
  /* the merged image, RLE: the row counts of every channel, then the rows; matted on white */
  if (alpha)
    for (i = 0; i < (size_t)w * h; i++) {
      u8 *p = comp + i * 4;
      int a = p[3];
      for (c = 0; c < 3; c++) p[c] = (u8)((p[c] * a + 255 * (255 - a) + 127) / 255);
    }
  if (!(counts = (u16 *)malloc(sizeof(u16) * (size_t)h * nch))) goto done;
  w16(&b, 1);
  for (k = 0; k < 2; k++)
    for (c = 0; c < nch; c++)
      for (y = 0; y < h; y++) {
        i32 xx;
        const u8 *p = comp + (size_t)y * w * 4 + c;
        for (xx = 0; xx < w; xx++) row[xx] = p[xx * 4];
        if (k == 0) { counts[c * h + y] = (u16)pack_row(NULL, row, (size_t)w); w16(&b, counts[c * h + y]); }
        else pack_row(&b, row, (size_t)w);
      }
  if (b.oom) goto done;
  *len = b.len;
  res = (u8 *)buf_take(&b);
done:
  if (o) for (k = 0; k < nrec; k++) if (o[k].ownpx) free(o[k].px);
  free(o); free(row); free(comp); free(counts);
  buf_free(&b);
  return res;
}
