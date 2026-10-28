/*
 * rasters.c - reading Targa, PCX, Netpbm (PBM/PGM/PPM/PAM/PFM), XPM, DDS, QOI, Windows ICO/CUR,
 * Radiance HDR, FITS and DPX into flat RGBA pictures, telling them (and tiff.c's and psd.c's
 * formats) apart, and the Lab-to-sRGB conversion tiff.c and psd.c share. See rasters.h.
 *
 * Parts ported from PhotoCraft (https://github.com/storytold/photocraft), Copyright (c) 2026
 * ArtCraft Team and the PhotoCraft contributors, used under the MIT License (see Licenses.txt):
 *   PCX     (crates/codecs/src/codecs/pcx.rs) - the strict header test, scanline RLE that never
 *           crosses a scanline, the minimum-size check before allocating, the palette choices;
 *   Netpbm  (pnm.rs) - the token reader (comments, the single separator byte, PAM's ENDHDR line),
 *           maxval scaling, PAM tuple types, PFM's byte order and bottom-up rows;
 *   XPM     (xpm.rs) - C string extraction that ignores comments, XPM2 lines, the c > g > g4 > m
 *           key priority, hex colours, web-colour fallbacks;
 *   DDS     (dds.rs, dds_bc.rs) - the header checks (cubemaps, volumes and arrays refused),
 *           channel masks, the pitch rule, BC1-BC3 decoding with PhotoCraft's rounding;
 *   FITS    (fits.rs) - the HDU walk, BSCALE/BZERO/BLANK, bottom-up rows unless ROWORDER says
 *           otherwise, the normalisation of non-8/16-bit data;
 *   DPX     (dpx.rs) - the header fields, packings 0/1/2 at 10 bits and 0/1 at 12 bits, padding;
 *   Lab     (crates/engine/src/cmd/preset_import_cmds.rs lab_to_rgb) - D50 Lab to sRGB.
 * The X11 colour names (from PhotoCraft's xpm/x11_colors.rs) are X.Org's rgb.txt, MIT licence,
 * Copyright The Open Group / Oracle; kept here as base names, their 1-4 variants and gray0-100.
 *
 * Written here from the published specifications, not ported: Targa (Truevision TGA 2.0 spec;
 * PhotoCraft's tga.rs only writes), QOI (QOI specification 1.0), ICO/CUR (Microsoft's icon
 * format), BC4, BC5 and BC7 (Microsoft's block-compression documentation; the BC7 partition and
 * anchor tables checked against Pillow's decoder output) and Radiance HDR (Greg Ward's RGBE
 * format).
 *
 * Multi-byte numbers are read a byte at a time in each format's order. Everything from the file is
 * checked before it is used; nothing big goes on the stack.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rasters.h"

static u32 le16(const u8 *p) { return p[0] | ((u32)p[1] << 8); }
static u32 le32(const u8 *p) { return p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static u32 be16(const u8 *p) { return ((u32)p[0] << 8) | p[1]; }
static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }

/* a picture size every decoder accepts */
static int size_ok(u32 w, u32 h) { return w >= 1 && h >= 1 && w <= MAXDIM && h <= MAXDIM && (long)w * h <= CODEC_MAXPIXELS; }
/* the picture, or a message saying why not (the size was checked already: so out of memory) */
static int alloc(Rgba *out, u32 w, u32 h, char *err, size_t errcap)
{
  if (rgba_alloc(out, (i32)w, (i32)h)) return 1;
  codec_err(err, errcap, "Not enough memory for the picture");
  return 0;
}
/* drop the half-made picture and say why */
static int fail(Rgba *out, char *err, size_t errcap, const char *msg)
{
  rgba_free(out);
  codec_err(err, errcap, msg);
  return 0;
}
/* 0-1 to 0-255 (NaN 0) */
static int unit8(double v) { return v > 0 ? v < 1 ? (int)(v * 255 + 0.5) : 255 : 0; }

/* linear light 0-1 to sRGB 0-255, through a table of 4097 steps (made once) */
static u8 srgb_tab[4097];
static int srgb_ready;
static int srgb8(double v)
{
  if (!srgb_ready) {
    int i;
    for (i = 0; i <= 4096; i++) {
      double x = i / 4096.0, c = x <= 0.0031308 ? 12.92 * x : 1.055 * pow(x, 1 / 2.4) - 0.055;
      srgb_tab[i] = (u8)(c * 255 + 0.5);
    }
    srgb_ready = 1;
  }
  if (!(v > 0)) return 0;
  if (v >= 1) return 255;
  return srgb_tab[(int)(v * 4096 + 0.5)];
}

/* CIE L*a*b* (D50, as Photoshop and TIFF store it) to sRGB through XYZ, with the Bradford-adapted
   D50-to-D65 matrix (as PhotoCraft's lab_to_rgb) */
static double lab_inv(double t) { return t > 6.0 / 29 ? t * t * t : 3 * (6.0 / 29) * (6.0 / 29) * (t - 4.0 / 29); }
void rasters_lab(int l, int a, int b, u8 *rgb)
{
  double fy = (l * 100.0 / 255 + 16) / 116, fx = fy + (a - 128) / 500.0, fz = fy - (b - 128) / 200.0;
  double x = 0.9642 * lab_inv(fx), y = lab_inv(fy), z = 0.8249 * lab_inv(fz);
  rgb[0] = (u8)srgb8(3.1339 * x - 1.6169 * y - 0.4906 * z);
  rgb[1] = (u8)srgb8(-0.9788 * x + 1.9161 * y + 0.0335 * z);
  rgb[2] = (u8)srgb8(0.0719 * x - 0.2290 * y + 1.4052 * z);
}

/* if no pixel kept any alpha, the alpha was never meant (writers that leave the byte 0): opaque */
static void alpha_or_opaque(Rgba *im)
{
  long i, np = (long)im->w * im->h;
  for (i = 0; i < np; i++) if (im->px[i * 4 + 3]) return;
  for (i = 0; i < np; i++) im->px[i * 4 + 3] = 255;
}

/* ------------------------------------------------------------------ Targa */
static u8 x5(u32 v) { return (u8)((v * 255 + 15) / 31); }
/* one true-colour or gray value of `depth` bits at p (abit: a 16-bit pixel's top bit is alpha) */
static void tga_value(const u8 *p, int depth, int gray, int abit, u8 *o)
{
  if (gray) { o[0] = o[1] = o[2] = p[0]; o[3] = depth == 16 ? p[1] : 255; return; }
  switch (depth) {
  case 15: case 16: {
    u32 v = le16(p);
    o[0] = x5((v >> 10) & 31); o[1] = x5((v >> 5) & 31); o[2] = x5(v & 31);
    o[3] = (u8)(depth == 16 && abit ? (v & 0x8000 ? 255 : 0) : 255);
    break;
  }
  case 24: o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; o[3] = 255; break;
  default: o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; o[3] = p[3]; break;
  }
}

/* the 18-byte header, an ID, the colour map, then the pixels (raw, or RLE packets that may run on
   from one row into the next, as TGA 1.0 allowed). Rows bottom up unless descriptor bit 5 says top
   down; bit 4 mirrors them */
int tga_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  u32 cmtype, type, cmfirst, cmlen, cmdepth, w, h, depth, desc, bpp, cmb, total, i, abit;
  size_t pos;
  int gray, mapped;
  out->w = out->h = 0; out->px = NULL;
  if (n < 18) { codec_err(err, errcap, "Not a Targa file"); return 0; }
  cmtype = d[1]; type = d[2]; cmfirst = le16(d + 3); cmlen = le16(d + 5); cmdepth = d[7];
  w = le16(d + 12); h = le16(d + 14); depth = d[16]; desc = d[17];
  gray = (type & 7) == 3; mapped = (type & 7) == 1;
  if (cmtype > 1 || !(type == 1 || type == 2 || type == 3 || type == 9 || type == 10 || type == 11)) { codec_err(err, errcap, "Not a Targa file"); return 0; }
  if ((mapped && !(depth == 8 || depth == 16)) || (gray && !(depth == 8 || depth == 16)) ||
      ((type & 7) == 2 && !(depth == 15 || depth == 16 || depth == 24 || depth == 32))) { codec_err(err, errcap, "Unsupported Targa depth"); return 0; }
  if (mapped && (cmtype != 1 || !(cmdepth == 15 || cmdepth == 16 || cmdepth == 24 || cmdepth == 32) || !cmlen)) { codec_err(err, errcap, "Damaged Targa colour map"); return 0; }
  if (!size_ok(w, h)) { codec_err(err, errcap, "Bad Targa picture size"); return 0; }
  abit = (desc & 15) >= 1;
  bpp = (depth + 7) / 8;
  cmb = cmtype ? cmlen * ((cmdepth + 7) / 8) : 0;
  pos = 18 + (size_t)d[0];
  if (cmtype && (cmdepth == 15 || cmdepth == 16 || cmdepth == 24 || cmdepth == 32) && pos + cmb > n) { codec_err(err, errcap, "Truncated Targa file"); return 0; }
  if (cmtype && !(cmdepth == 15 || cmdepth == 16 || cmdepth == 24 || cmdepth == 32)) cmb = cmlen * ((cmdepth + 7) / 8);
  if (pos + cmb > n) { codec_err(err, errcap, "Truncated Targa file"); return 0; }
  {
    const u8 *cmap = d + pos;
    pos += cmb;
    total = w * h;
    /* raw data must all be there; RLE makes at most 128 pixels from a packet of 2 bytes or more */
    if (type < 8 ? (double)(n - pos) < (double)total * bpp : (double)(n - pos) * 64 < (double)total) { codec_err(err, errcap, "Truncated Targa file"); return 0; }
    if (!alloc(out, w, h, err, errcap)) return 0;
    for (i = 0; i < total; ) {
      u32 cnt = 1, k, rep = 0;
      u8 px[4];
      if (type >= 8) {
        if (pos >= n) return fail(out, err, errcap, "Truncated Targa file");
        rep = d[pos] & 0x80; cnt = (d[pos] & 0x7F) + 1u;
        pos++;
        if (cnt > total - i) cnt = total - i;
      }
      for (k = 0; k < cnt; k++, i++) {
        u32 x = i % w, y = i / w;
        u8 *o;
        if (!rep || k == 0) {                            /* a raw pixel, or a run's one stored pixel */
          if (bpp > n - pos) return fail(out, err, errcap, "Truncated Targa file");
          if (mapped) {
            u32 idx = depth == 8 ? d[pos] : le16(d + pos);
            if (idx >= cmfirst && idx - cmfirst < cmlen) tga_value(cmap + (idx - cmfirst) * ((cmdepth + 7) / 8), (int)cmdepth, 0, 0, px);
            else { px[0] = px[1] = px[2] = 0; px[3] = 255; }
          } else tga_value(d + pos, (int)depth, gray, (int)abit, px);
          pos += bpp;
        }
        if (!(desc & 0x20)) y = h - 1 - y;
        if (desc & 0x10) x = w - 1 - x;
        o = out->px + ((size_t)y * w + x) * 4;
        memcpy(o, px, 4);
      }
    }
  }
  if (depth == 32 || (depth == 16 && !gray && abit) || cmdepth == 32) alpha_or_opaque(out);
  return 1;
}

/* plausible as a Targa: there's no signature, so the whole header has to make sense and the data
   fit (or the TGA 2.0 footer be there) */
static int tga_like(const u8 *d, size_t n)
{
  u32 cmtype, type, cmlen, cmdepth, w, h, depth, bpp, cmb;
  size_t pos;
  int footer;
  if (n < 19) return 0;
  cmtype = d[1]; type = d[2]; cmlen = le16(d + 5); cmdepth = d[7];
  w = le16(d + 12); h = le16(d + 14); depth = d[16];
  footer = n >= 44 && !memcmp(d + n - 18, "TRUEVISION-XFILE.", 17) && d[n - 1] == 0;
  if (cmtype > 1 || !(type == 1 || type == 2 || type == 3 || type == 9 || type == 10 || type == 11) || !w || !h) return 0;
  if ((d[17] & 0xC0) || (d[17] & 15) > 8) return 0;
  if ((type & 7) == 1 && (cmtype != 1 || depth != 8 || !cmlen || !(cmdepth == 15 || cmdepth == 16 || cmdepth == 24 || cmdepth == 32))) return 0;
  if ((type & 7) == 2 && !(depth == 15 || depth == 16 || depth == 24 || depth == 32)) return 0;
  if ((type & 7) == 3 && !(depth == 8 || depth == 16)) return 0;
  if (cmtype == 0 && (le16(d + 3) || cmlen || cmdepth) && !footer) return 0;
  if (cmtype == 1 && !(cmdepth == 15 || cmdepth == 16 || cmdepth == 24 || cmdepth == 32)) return 0;
  if (footer) return 1;
  bpp = (depth + 7) / 8;
  cmb = cmtype ? cmlen * ((cmdepth + 7) / 8) : 0;
  pos = 18 + (size_t)d[0] + cmb;
  if (pos >= n) return 0;
  return type < 8 ? (double)(n - pos) >= (double)w * h * bpp && (double)(n - pos) <= (double)w * h * bpp + 65536 : 1;
}

/* ------------------------------------------------------------------ PCX */
/* PhotoCraft's header test, plus 8-bit four-plane RGBA: version 0-5, RLE, a sane window, a line
   long enough for the width, and a bits/planes pair PCX has */
static int pcx_like(const u8 *d, size_t n)
{
  u32 bits, planes, stride, x0, y0, x1, y1;
  if (n < 128 || d[0] != 10 || !(d[1] == 0 || d[1] == 2 || d[1] == 3 || d[1] == 4 || d[1] == 5) || d[2] != 1) return 0;
  bits = d[3]; planes = d[65]; stride = le16(d + 66);
  x0 = le16(d + 4); y0 = le16(d + 6); x1 = le16(d + 8); y1 = le16(d + 10);
  if (x1 < x0 || y1 < y0 || !(bits == 1 || bits == 2 || bits == 4 || bits == 8) || planes < 1 || planes > 4) return 0;
  if (!stride || stride < ((x1 - x0 + 1) * bits + 7) / 8) return 0;
  return bits == 8 ? planes != 2 : bits * planes <= 4;
}

/* one scanline (all its planes): a byte of 0xC0 and up repeats the next byte (its low 6 bits)
   times, anything else is itself. A packet may not run past the scanline */
static int pcx_row(const u8 *d, size_t end, size_t *pos, u8 *row, size_t len)
{
  size_t o = 0;
  while (o < len) {
    u32 c, cnt = 1;
    if (*pos >= end) return 0;
    c = d[(*pos)++];
    if (c >= 0xC0) {
      cnt = c & 0x3F;
      if (*pos >= end) return 0;
      c = d[(*pos)++];
    }
    if (!cnt || cnt > len - o) return 0;
    memset(row + o, (int)c, cnt);
    o += cnt;
  }
  return 1;
}

int pcx_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  u32 bits, planes, stride, w, h, x, y, min;
  size_t line, end, pos = 128;
  int vga;
  u8 *row;
  out->w = out->h = 0; out->px = NULL;
  if (!pcx_like(d, n)) { codec_err(err, errcap, "Not a PCX file, or an unsupported kind"); return 0; }
  bits = d[3]; planes = d[65]; stride = le16(d + 66);
  w = le16(d + 8) - le16(d + 4) + 1; h = le16(d + 10) - le16(d + 6) + 1;
  if (!size_ok(w, h)) { codec_err(err, errcap, "Bad PCX picture size"); return 0; }
  line = (size_t)stride * planes;
  /* the VGA palette: 0x0C and 768 bytes at the very end */
  vga = bits == 8 && planes == 1 && n >= 128 + 769 && d[n - 769] == 12;
  end = vga ? n - 769 : n;
  /* each scanline takes at least 2 bytes per 63 (PhotoCraft's bound), so a short file can't claim
     a big picture */
  min = (u32)((line / 63) * 2 + (line % 63 == 0 ? 0 : line % 63 == 1 ? 1 : 2));
  if ((double)(end - 128) < (double)min * h) { codec_err(err, errcap, "Truncated PCX file"); return 0; }
  if (!alloc(out, w, h, err, errcap)) return 0;
  if (!(row = (u8 *)malloc(line))) { codec_oom = 1; return fail(out, err, errcap, "Out of memory"); }
  for (y = 0; y < h; y++) {
    u8 *o = out->px + (size_t)y * w * 4;
    if (!pcx_row(d, end, &pos, row, line)) { free(row); return fail(out, err, errcap, "Damaged or truncated PCX file"); }
    for (x = 0; x < w; x++, o += 4) {
      o[3] = 255;
      if (bits == 8 && planes >= 3) {
        o[0] = row[x]; o[1] = row[stride + x]; o[2] = row[2 * stride + x];
        if (planes == 4) o[3] = row[3 * stride + x];
      } else if (bits == 8) {
        if (vga) { const u8 *p = d + n - 768 + row[x] * 3; o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; }
        else o[0] = o[1] = o[2] = row[x];                /* no palette: gray */
      } else if (bits == 1 && planes == 1) {
        o[0] = o[1] = o[2] = (u8)((row[x >> 3] >> (7 - (x & 7))) & 1 ? 255 : 0);   /* black and white, as Pillow */
      } else {                                           /* 2 or 4 bits, the planes' bits together: the 16-colour header palette */
        u32 bit = x * bits, p, v = 0;
        for (p = 0; p < planes; p++) v |= (u32)((row[p * stride + (bit >> 3)] >> (8 - bits - (bit & 7))) & ((1u << bits) - 1)) << (p * bits);
        o[0] = d[16 + v * 3]; o[1] = d[17 + v * 3]; o[2] = d[18 + v * 3];
      }
    }
  }
  free(row);
  return 1;
}

/* ------------------------------------------------------------------ Netpbm */
typedef struct { const u8 *d; size_t n, pos; } Rd;
static int is_ws(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
/* whitespace and comments (# to the end of the line) */
static void rd_skip(Rd *r)
{
  while (r->pos < r->n) {
    int c = r->d[r->pos];
    if (c == '#') while (r->pos < r->n && r->d[r->pos] != '\n' && r->d[r->pos] != '\r') r->pos++;
    else if (is_ws(c)) r->pos++;
    else break;
  }
}
/* the next token: *len bytes at *t; 0 at the end of the data */
static int rd_token(Rd *r, const u8 **t, size_t *len)
{
  size_t s;
  rd_skip(r);
  s = r->pos;
  while (r->pos < r->n && !is_ws(r->d[r->pos]) && r->d[r->pos] != '#') r->pos++;
  *t = r->d + s; *len = r->pos - s;
  return *len > 0;
}
/* a decimal number up to 2^31 - 1 */
static int rd_uint(Rd *r, u32 *v)
{
  const u8 *t;
  size_t len, i;
  u32 x = 0;
  if (!rd_token(r, &t, &len)) return 0;
  for (i = 0; i < len; i++) {
    if (t[i] < '0' || t[i] > '9' || x > 214748363UL) return 0;
    x = x * 10 + (u32)(t[i] - '0');
  }
  *v = x;
  return 1;
}
static int tok_is(const u8 *t, size_t len, const char *s) { return len == strlen(s) && !memcmp(t, s, len); }

/* nc samples of a pixel (binary: bytes, or big-endian 16-bit when maxval > 255; ASCII: numbers)
   scaled to 0-255 into o (gray as RGB, CMYK made RGB); 0 if the data ends or a sample is over max */
static int pnm_pixel(Rd *r, int ascii, u32 maxv, int nc, int cmyk, u8 *o)
{
  u32 s[5];
  int k;
  for (k = 0; k < nc; k++) {
    u32 v;
    if (ascii) { if (!rd_uint(r, &v)) return 0; }
    else if (maxv < 256) { if (r->pos >= r->n) return 0; v = r->d[r->pos++]; }
    else { if (r->n - r->pos < 2) return 0; v = be16(r->d + r->pos); r->pos += 2; }
    if (v > maxv) return 0;
    s[k] = maxv == 255 ? v : (v * 255 + maxv / 2) / maxv;
  }
  o[3] = 255;
  if (cmyk) {
    int kk = 255 - (int)s[3];
    for (k = 0; k < 3; k++) o[k] = (u8)(((255 - (int)s[k]) * kk + 127) / 255);
    if (nc == 5) o[3] = (u8)s[4];
  } else if (nc <= 2) { o[0] = o[1] = o[2] = (u8)s[0]; if (nc == 2) o[3] = (u8)s[1]; }
  else { o[0] = (u8)s[0]; o[1] = (u8)s[1]; o[2] = (u8)s[2]; if (nc == 4) o[3] = (u8)s[3]; }
  return 1;
}

int pnm_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  Rd r;
  u32 w = 0, h = 0, maxv = 1, x, y;
  int kind, nc = 1, cmyk = 0, ascii = 0;
  const char *msg = "Damaged Netpbm file";
  out->w = out->h = 0; out->px = NULL;
  if (n < 3 || d[0] != 'P') { codec_err(err, errcap, "Not a Netpbm file"); return 0; }
  kind = d[1];
  r.d = d; r.n = n; r.pos = 2;
  if (kind == '7') {
    /* PAM: KEY value lines up to ENDHDR, which ends its line */
    u32 depth = 0;
    int haw = 0, hah = 0, had = 0, ham = 0;
    char tt[32];
    size_t ttl = 0;
    for (;;) {
      const u8 *t;
      size_t len;
      if (!rd_token(&r, &t, &len)) { codec_err(err, errcap, "Truncated PAM header"); return 0; }
      if (tok_is(t, len, "ENDHDR")) break;
      if (tok_is(t, len, "WIDTH")) haw = rd_uint(&r, &w);
      else if (tok_is(t, len, "HEIGHT")) hah = rd_uint(&r, &h);
      else if (tok_is(t, len, "DEPTH")) had = rd_uint(&r, &depth);
      else if (tok_is(t, len, "MAXVAL")) ham = rd_uint(&r, &maxv);
      else if (tok_is(t, len, "TUPLTYPE")) {
        if (!rd_token(&r, &t, &len)) { codec_err(err, errcap, "Truncated PAM header"); return 0; }
        if (ttl + len + 1 >= sizeof tt) { codec_err(err, errcap, "Unsupported PAM tuple type"); return 0; }
        if (ttl) tt[ttl++] = ' ';
        memcpy(tt + ttl, t, len); ttl += len;
      } else { codec_err(err, errcap, "Unknown PAM header field"); return 0; }
    }
    tt[ttl] = 0;
    while (r.pos < n && r.d[r.pos] != '\n') {            /* the rest of the ENDHDR line */
      if (!is_ws(r.d[r.pos])) { codec_err(err, errcap, "Damaged PAM header"); return 0; }
      r.pos++;
    }
    if (r.pos >= n) { codec_err(err, errcap, "Truncated PAM file"); return 0; }
    r.pos++;
    if (!haw || !hah || !had || !ham) { codec_err(err, errcap, "Incomplete PAM header"); return 0; }
    if ((depth == 1 && (!ttl || !strcmp(tt, "GRAYSCALE") || !strcmp(tt, "BLACKANDWHITE"))) ||
        (depth == 2 && (!ttl || !strcmp(tt, "GRAYSCALE_ALPHA") || !strcmp(tt, "BLACKANDWHITE_ALPHA"))) ||
        (depth == 3 && (!ttl || !strcmp(tt, "RGB"))) || (depth == 4 && (!ttl || !strcmp(tt, "RGB_ALPHA")))) nc = (int)depth;
    else if (depth == 4 && !strcmp(tt, "CMYK")) { nc = 4; cmyk = 1; }
    else if (depth == 5 && !strcmp(tt, "CMYK_ALPHA")) { nc = 5; cmyk = 1; }
    else { codec_err(err, errcap, "Unsupported PAM tuple type"); return 0; }
  } else if (kind == 'F' || kind == 'f') {
    /* PFM: width, height, a scale whose sign gives the byte order; float rows bottom up */
    const u8 *t;
    size_t len, need;
    char num[64];
    double scale;
    int le;
    u32 k;
    nc = kind == 'F' ? 3 : 1;
    if (!rd_uint(&r, &w) || !rd_uint(&r, &h) || !rd_token(&r, &t, &len) || len >= sizeof num) { codec_err(err, errcap, "Damaged PFM header"); return 0; }
    memcpy(num, t, len); num[len] = 0;
    scale = strtod(num, NULL);
    if (!(scale == scale) || scale == 0 || scale > 1e300 || scale < -1e300) { codec_err(err, errcap, "Damaged PFM header"); return 0; }
    le = scale < 0;
    if (r.pos >= n || !is_ws(d[r.pos])) { codec_err(err, errcap, "Damaged PFM header"); return 0; }
    r.pos++;
    if (!size_ok(w, h)) { codec_err(err, errcap, "Bad PFM picture size"); return 0; }
    need = (size_t)w * h * nc * 4;
    if (need > n - r.pos) { codec_err(err, errcap, "Truncated PFM file"); return 0; }
    if (!alloc(out, w, h, err, errcap)) return 0;
    for (y = 0; y < h; y++) {
      const u8 *p = d + r.pos + (size_t)y * w * nc * 4;
      u8 *o = out->px + (size_t)(h - 1 - y) * w * 4;
      for (x = 0; x < w; x++, o += 4) {
        for (k = 0; k < (u32)nc; k++, p += 4) {
          union { u32 u; float f; } v;
          v.u = le ? le32(p) : be32(p);
          o[k] = (u8)unit8(v.f);
        }
        if (nc == 1) o[1] = o[2] = o[0];
        o[3] = 255;
      }
    }
    return 1;
  } else if (kind >= '1' && kind <= '6') {
    ascii = kind <= '3';
    if (!rd_uint(&r, &w) || !rd_uint(&r, &h)) { codec_err(err, errcap, "Damaged Netpbm header"); return 0; }
    if (kind != '1' && kind != '4' && !rd_uint(&r, &maxv)) { codec_err(err, errcap, "Damaged Netpbm header"); return 0; }
    nc = kind == '3' || kind == '6' ? 3 : 1;
    if (!ascii) {
      if (r.pos >= n || !is_ws(d[r.pos])) { codec_err(err, errcap, "Damaged Netpbm header"); return 0; }
      r.pos++;
    }
  } else { codec_err(err, errcap, "Not a Netpbm file"); return 0; }
  if (!size_ok(w, h)) { codec_err(err, errcap, "Bad Netpbm picture size"); return 0; }
  if (maxv < 1 || maxv > 65535) { codec_err(err, errcap, "Bad Netpbm maxval"); return 0; }
  /* the data has to be there before the picture is made: binary exactly, ASCII at a byte or more a
     sample (the PBM digits one a pixel) */
  if (kind == '4') { if ((double)((w + 7) / 8) * h > (double)(n - r.pos)) { codec_err(err, errcap, "Truncated Netpbm file"); return 0; } }
  else if (ascii) { if ((double)w * h * nc > (double)(n - r.pos)) { codec_err(err, errcap, "Truncated Netpbm file"); return 0; } }
  else if ((double)w * h * nc * (maxv > 255 ? 2 : 1) > (double)(n - r.pos)) { codec_err(err, errcap, "Truncated Netpbm file"); return 0; }
  if (!alloc(out, w, h, err, errcap)) return 0;
  for (y = 0; y < h; y++) {
    u8 *o = out->px + (size_t)y * w * 4;
    for (x = 0; x < w; x++, o += 4) {
      if (kind == '1' || kind == '4') {                  /* PBM: 1 is black */
        int bit;
        if (kind == '4') bit = (d[r.pos + (size_t)y * ((w + 7) / 8) + (x >> 3)] >> (7 - (x & 7))) & 1;
        else {
          rd_skip(&r);
          if (r.pos >= n || (d[r.pos] != '0' && d[r.pos] != '1')) return fail(out, err, errcap, r.pos >= n ? "Truncated Netpbm file" : msg);
          bit = d[r.pos++] == '1';
        }
        o[0] = o[1] = o[2] = (u8)(bit ? 0 : 255); o[3] = 255;
      } else if (!pnm_pixel(&r, ascii, maxv, nc, cmyk, o)) return fail(out, err, errcap, msg);
    }
  }
  return 1;
}

/* ------------------------------------------------------------------ XPM */
/* the X11 colour names (X.Org rgb.txt, normalized: lower case, no spaces), sorted: flags 1 when the
   name itself is a colour (v[0..2]), 2 when name1-name4 are (v[3..14]) */
typedef struct { const char *name; u8 flags, v[15]; } X11Colour;
static const X11Colour x11[] = {
  { "aliceblue", 1, { 240, 248, 255 } },
  { "antiquewhite", 3, { 250, 235, 215, 255, 239, 219, 238, 223, 204, 205, 192, 176, 139, 131, 120 } },
  { "aquamarine", 3, { 127, 255, 212, 127, 255, 212, 118, 238, 198, 102, 205, 170, 69, 139, 116 } },
  { "azure", 3, { 240, 255, 255, 240, 255, 255, 224, 238, 238, 193, 205, 205, 131, 139, 139 } },
  { "beige", 1, { 245, 245, 220 } },
  { "bisque", 3, { 255, 228, 196, 255, 228, 196, 238, 213, 183, 205, 183, 158, 139, 125, 107 } },
  { "black", 1, { 0, 0, 0 } },
  { "blanchedalmond", 1, { 255, 235, 205 } },
  { "blue", 3, { 0, 0, 255, 0, 0, 255, 0, 0, 238, 0, 0, 205, 0, 0, 139 } },
  { "blueviolet", 1, { 138, 43, 226 } },
  { "brown", 3, { 165, 42, 42, 255, 64, 64, 238, 59, 59, 205, 51, 51, 139, 35, 35 } },
  { "burlywood", 3, { 222, 184, 135, 255, 211, 155, 238, 197, 145, 205, 170, 125, 139, 115, 85 } },
  { "cadetblue", 3, { 95, 158, 160, 152, 245, 255, 142, 229, 238, 122, 197, 205, 83, 134, 139 } },
  { "chartreuse", 3, { 127, 255, 0, 127, 255, 0, 118, 238, 0, 102, 205, 0, 69, 139, 0 } },
  { "chocolate", 3, { 210, 105, 30, 255, 127, 36, 238, 118, 33, 205, 102, 29, 139, 69, 19 } },
  { "coral", 3, { 255, 127, 80, 255, 114, 86, 238, 106, 80, 205, 91, 69, 139, 62, 47 } },
  { "cornflowerblue", 1, { 100, 149, 237 } },
  { "cornsilk", 3, { 255, 248, 220, 255, 248, 220, 238, 232, 205, 205, 200, 177, 139, 136, 120 } },
  { "cyan", 3, { 0, 255, 255, 0, 255, 255, 0, 238, 238, 0, 205, 205, 0, 139, 139 } },
  { "darkblue", 1, { 0, 0, 139 } },
  { "darkcyan", 1, { 0, 139, 139 } },
  { "darkgoldenrod", 3, { 184, 134, 11, 255, 185, 15, 238, 173, 14, 205, 149, 12, 139, 101, 8 } },
  { "darkgray", 1, { 169, 169, 169 } },
  { "darkgreen", 1, { 0, 100, 0 } },
  { "darkgrey", 1, { 169, 169, 169 } },
  { "darkkhaki", 1, { 189, 183, 107 } },
  { "darkmagenta", 1, { 139, 0, 139 } },
  { "darkolivegreen", 3, { 85, 107, 47, 202, 255, 112, 188, 238, 104, 162, 205, 90, 110, 139, 61 } },
  { "darkorange", 3, { 255, 140, 0, 255, 127, 0, 238, 118, 0, 205, 102, 0, 139, 69, 0 } },
  { "darkorchid", 3, { 153, 50, 204, 191, 62, 255, 178, 58, 238, 154, 50, 205, 104, 34, 139 } },
  { "darkred", 1, { 139, 0, 0 } },
  { "darksalmon", 1, { 233, 150, 122 } },
  { "darkseagreen", 3, { 143, 188, 143, 193, 255, 193, 180, 238, 180, 155, 205, 155, 105, 139, 105 } },
  { "darkslateblue", 1, { 72, 61, 139 } },
  { "darkslategray", 3, { 47, 79, 79, 151, 255, 255, 141, 238, 238, 121, 205, 205, 82, 139, 139 } },
  { "darkslategrey", 1, { 47, 79, 79 } },
  { "darkturquoise", 1, { 0, 206, 209 } },
  { "darkviolet", 1, { 148, 0, 211 } },
  { "deeppink", 3, { 255, 20, 147, 255, 20, 147, 238, 18, 137, 205, 16, 118, 139, 10, 80 } },
  { "deepskyblue", 3, { 0, 191, 255, 0, 191, 255, 0, 178, 238, 0, 154, 205, 0, 104, 139 } },
  { "dimgray", 1, { 105, 105, 105 } },
  { "dimgrey", 1, { 105, 105, 105 } },
  { "dodgerblue", 3, { 30, 144, 255, 30, 144, 255, 28, 134, 238, 24, 116, 205, 16, 78, 139 } },
  { "firebrick", 3, { 178, 34, 34, 255, 48, 48, 238, 44, 44, 205, 38, 38, 139, 26, 26 } },
  { "floralwhite", 1, { 255, 250, 240 } },
  { "forestgreen", 1, { 34, 139, 34 } },
  { "gainsboro", 1, { 220, 220, 220 } },
  { "ghostwhite", 1, { 248, 248, 255 } },
  { "gold", 3, { 255, 215, 0, 255, 215, 0, 238, 201, 0, 205, 173, 0, 139, 117, 0 } },
  { "goldenrod", 3, { 218, 165, 32, 255, 193, 37, 238, 180, 34, 205, 155, 29, 139, 105, 20 } },
  { "gray", 1, { 190, 190, 190 } },
  { "green", 3, { 0, 255, 0, 0, 255, 0, 0, 238, 0, 0, 205, 0, 0, 139, 0 } },
  { "greenyellow", 1, { 173, 255, 47 } },
  { "grey", 1, { 190, 190, 190 } },
  { "honeydew", 3, { 240, 255, 240, 240, 255, 240, 224, 238, 224, 193, 205, 193, 131, 139, 131 } },
  { "hotpink", 3, { 255, 105, 180, 255, 110, 180, 238, 106, 167, 205, 96, 144, 139, 58, 98 } },
  { "indianred", 3, { 205, 92, 92, 255, 106, 106, 238, 99, 99, 205, 85, 85, 139, 58, 58 } },
  { "ivory", 3, { 255, 255, 240, 255, 255, 240, 238, 238, 224, 205, 205, 193, 139, 139, 131 } },
  { "khaki", 3, { 240, 230, 140, 255, 246, 143, 238, 230, 133, 205, 198, 115, 139, 134, 78 } },
  { "lavender", 1, { 230, 230, 250 } },
  { "lavenderblush", 3, { 255, 240, 245, 255, 240, 245, 238, 224, 229, 205, 193, 197, 139, 131, 134 } },
  { "lawngreen", 1, { 124, 252, 0 } },
  { "lemonchiffon", 3, { 255, 250, 205, 255, 250, 205, 238, 233, 191, 205, 201, 165, 139, 137, 112 } },
  { "lightblue", 3, { 173, 216, 230, 191, 239, 255, 178, 223, 238, 154, 192, 205, 104, 131, 139 } },
  { "lightcoral", 1, { 240, 128, 128 } },
  { "lightcyan", 3, { 224, 255, 255, 224, 255, 255, 209, 238, 238, 180, 205, 205, 122, 139, 139 } },
  { "lightgoldenrod", 3, { 238, 221, 130, 255, 236, 139, 238, 220, 130, 205, 190, 112, 139, 129, 76 } },
  { "lightgoldenrodyellow", 1, { 250, 250, 210 } },
  { "lightgray", 1, { 211, 211, 211 } },
  { "lightgreen", 1, { 144, 238, 144 } },
  { "lightgrey", 1, { 211, 211, 211 } },
  { "lightpink", 3, { 255, 182, 193, 255, 174, 185, 238, 162, 173, 205, 140, 149, 139, 95, 101 } },
  { "lightsalmon", 3, { 255, 160, 122, 255, 160, 122, 238, 149, 114, 205, 129, 98, 139, 87, 66 } },
  { "lightseagreen", 1, { 32, 178, 170 } },
  { "lightskyblue", 3, { 135, 206, 250, 176, 226, 255, 164, 211, 238, 141, 182, 205, 96, 123, 139 } },
  { "lightslateblue", 1, { 132, 112, 255 } },
  { "lightslategray", 1, { 119, 136, 153 } },
  { "lightslategrey", 1, { 119, 136, 153 } },
  { "lightsteelblue", 3, { 176, 196, 222, 202, 225, 255, 188, 210, 238, 162, 181, 205, 110, 123, 139 } },
  { "lightyellow", 3, { 255, 255, 224, 255, 255, 224, 238, 238, 209, 205, 205, 180, 139, 139, 122 } },
  { "limegreen", 1, { 50, 205, 50 } },
  { "linen", 1, { 250, 240, 230 } },
  { "magenta", 3, { 255, 0, 255, 255, 0, 255, 238, 0, 238, 205, 0, 205, 139, 0, 139 } },
  { "maroon", 3, { 176, 48, 96, 255, 52, 179, 238, 48, 167, 205, 41, 144, 139, 28, 98 } },
  { "mediumaquamarine", 1, { 102, 205, 170 } },
  { "mediumblue", 1, { 0, 0, 205 } },
  { "mediumorchid", 3, { 186, 85, 211, 224, 102, 255, 209, 95, 238, 180, 82, 205, 122, 55, 139 } },
  { "mediumpurple", 3, { 147, 112, 219, 171, 130, 255, 159, 121, 238, 137, 104, 205, 93, 71, 139 } },
  { "mediumseagreen", 1, { 60, 179, 113 } },
  { "mediumslateblue", 1, { 123, 104, 238 } },
  { "mediumspringgreen", 1, { 0, 250, 154 } },
  { "mediumturquoise", 1, { 72, 209, 204 } },
  { "mediumvioletred", 1, { 199, 21, 133 } },
  { "midnightblue", 1, { 25, 25, 112 } },
  { "mintcream", 1, { 245, 255, 250 } },
  { "mistyrose", 3, { 255, 228, 225, 255, 228, 225, 238, 213, 210, 205, 183, 181, 139, 125, 123 } },
  { "moccasin", 1, { 255, 228, 181 } },
  { "navajowhite", 3, { 255, 222, 173, 255, 222, 173, 238, 207, 161, 205, 179, 139, 139, 121, 94 } },
  { "navy", 1, { 0, 0, 128 } },
  { "navyblue", 1, { 0, 0, 128 } },
  { "oldlace", 1, { 253, 245, 230 } },
  { "olivedrab", 3, { 107, 142, 35, 192, 255, 62, 179, 238, 58, 154, 205, 50, 105, 139, 34 } },
  { "orange", 3, { 255, 165, 0, 255, 165, 0, 238, 154, 0, 205, 133, 0, 139, 90, 0 } },
  { "orangered", 3, { 255, 69, 0, 255, 69, 0, 238, 64, 0, 205, 55, 0, 139, 37, 0 } },
  { "orchid", 3, { 218, 112, 214, 255, 131, 250, 238, 122, 233, 205, 105, 201, 139, 71, 137 } },
  { "palegoldenrod", 1, { 238, 232, 170 } },
  { "palegreen", 3, { 152, 251, 152, 154, 255, 154, 144, 238, 144, 124, 205, 124, 84, 139, 84 } },
  { "paleturquoise", 3, { 175, 238, 238, 187, 255, 255, 174, 238, 238, 150, 205, 205, 102, 139, 139 } },
  { "palevioletred", 3, { 219, 112, 147, 255, 130, 171, 238, 121, 159, 205, 104, 137, 139, 71, 93 } },
  { "papayawhip", 1, { 255, 239, 213 } },
  { "peachpuff", 3, { 255, 218, 185, 255, 218, 185, 238, 203, 173, 205, 175, 149, 139, 119, 101 } },
  { "peru", 1, { 205, 133, 63 } },
  { "pink", 3, { 255, 192, 203, 255, 181, 197, 238, 169, 184, 205, 145, 158, 139, 99, 108 } },
  { "plum", 3, { 221, 160, 221, 255, 187, 255, 238, 174, 238, 205, 150, 205, 139, 102, 139 } },
  { "powderblue", 1, { 176, 224, 230 } },
  { "purple", 3, { 160, 32, 240, 155, 48, 255, 145, 44, 238, 125, 38, 205, 85, 26, 139 } },
  { "red", 3, { 255, 0, 0, 255, 0, 0, 238, 0, 0, 205, 0, 0, 139, 0, 0 } },
  { "rosybrown", 3, { 188, 143, 143, 255, 193, 193, 238, 180, 180, 205, 155, 155, 139, 105, 105 } },
  { "royalblue", 3, { 65, 105, 225, 72, 118, 255, 67, 110, 238, 58, 95, 205, 39, 64, 139 } },
  { "saddlebrown", 1, { 139, 69, 19 } },
  { "salmon", 3, { 250, 128, 114, 255, 140, 105, 238, 130, 98, 205, 112, 84, 139, 76, 57 } },
  { "sandybrown", 1, { 244, 164, 96 } },
  { "seagreen", 3, { 46, 139, 87, 84, 255, 159, 78, 238, 148, 67, 205, 128, 46, 139, 87 } },
  { "seashell", 3, { 255, 245, 238, 255, 245, 238, 238, 229, 222, 205, 197, 191, 139, 134, 130 } },
  { "sienna", 3, { 160, 82, 45, 255, 130, 71, 238, 121, 66, 205, 104, 57, 139, 71, 38 } },
  { "skyblue", 3, { 135, 206, 235, 135, 206, 255, 126, 192, 238, 108, 166, 205, 74, 112, 139 } },
  { "slateblue", 3, { 106, 90, 205, 131, 111, 255, 122, 103, 238, 105, 89, 205, 71, 60, 139 } },
  { "slategray", 3, { 112, 128, 144, 198, 226, 255, 185, 211, 238, 159, 182, 205, 108, 123, 139 } },
  { "slategrey", 1, { 112, 128, 144 } },
  { "snow", 3, { 255, 250, 250, 255, 250, 250, 238, 233, 233, 205, 201, 201, 139, 137, 137 } },
  { "springgreen", 3, { 0, 255, 127, 0, 255, 127, 0, 238, 118, 0, 205, 102, 0, 139, 69 } },
  { "steelblue", 3, { 70, 130, 180, 99, 184, 255, 92, 172, 238, 79, 148, 205, 54, 100, 139 } },
  { "tan", 3, { 210, 180, 140, 255, 165, 79, 238, 154, 73, 205, 133, 63, 139, 90, 43 } },
  { "thistle", 3, { 216, 191, 216, 255, 225, 255, 238, 210, 238, 205, 181, 205, 139, 123, 139 } },
  { "tomato", 3, { 255, 99, 71, 255, 99, 71, 238, 92, 66, 205, 79, 57, 139, 54, 38 } },
  { "turquoise", 3, { 64, 224, 208, 0, 245, 255, 0, 229, 238, 0, 197, 205, 0, 134, 139 } },
  { "violet", 1, { 238, 130, 238 } },
  { "violetred", 3, { 208, 32, 144, 255, 62, 150, 238, 58, 140, 205, 50, 120, 139, 34, 82 } },
  { "wheat", 3, { 245, 222, 179, 255, 231, 186, 238, 216, 174, 205, 186, 150, 139, 126, 102 } },
  { "white", 1, { 255, 255, 255 } },
  { "whitesmoke", 1, { 245, 245, 245 } },
  { "yellow", 3, { 255, 255, 0, 255, 255, 0, 238, 238, 0, 205, 205, 0, 139, 139, 0 } },
  { "yellowgreen", 1, { 154, 205, 50 } },
};
/* gray0 ... gray100 (and grey) */
static const u8 x11_gray[101] = {
  0, 3, 5, 8, 10, 13, 15, 18, 20, 23, 26, 28, 31, 33, 36, 38, 41, 43, 46, 48, 51, 54, 56, 59, 61, 64, 66, 69, 71, 74,
  77, 79, 82, 84, 87, 89, 92, 94, 97, 99, 102, 105, 107, 110, 112, 115, 117, 120, 122, 125, 127, 130, 133, 135, 138,
  140, 143, 145, 148, 150, 153, 156, 158, 161, 163, 166, 168, 171, 173, 176, 179, 181, 184, 186, 189, 191, 194, 196,
  199, 201, 204, 207, 209, 212, 214, 217, 219, 222, 224, 227, 229, 232, 235, 237, 240, 242, 245, 247, 250, 252, 255 };

static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
/* a colour value: None, #rgb with 1-4 hex digits a channel, or an X11 name; 0 if unknown */
static int xpm_colour(const u8 *s, size_t len, u8 *o)
{
  char nm[48];
  size_t i, k = 0;
  if (len == 4 && (s[0] | 32) == 'n' && (s[1] | 32) == 'o' && (s[2] | 32) == 'n' && (s[3] | 32) == 'e') { o[0] = o[1] = o[2] = o[3] = 0; return 1; }
  o[3] = 255;
  if (len && s[0] == '#') {
    size_t dg = (len - 1) / 3;
    int c;
    if (len - 1 != dg * 3 || dg < 1 || dg > 4) return 0;
    for (c = 0; c < 3; c++) {
      u32 v = 0, mx = (1UL << (dg * 4)) - 1;
      for (i = 0; i < dg; i++) { int h = hexv(s[1 + c * dg + i]); if (h < 0) return 0; v = v * 16 + (u32)h; }
      o[c] = (u8)((v * 255 + mx / 2) / mx);
    }
    return 1;
  }
  for (i = 0; i < len; i++) {                            /* lower case, without spaces, - and _ */
    int c = s[i];
    if (c == ' ' || c == '-' || c == '_' || c == '\t') continue;
    if (k + 1 >= sizeof nm) return 0;
    nm[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
  }
  nm[k] = 0;
  if (k > 4 && (!memcmp(nm, "gray", 4) || !memcmp(nm, "grey", 4)) && nm[4] >= '0' && nm[4] <= '9') {
    int v = 0;
    for (i = 4; i < k; i++) { if (nm[i] < '0' || nm[i] > '9' || v > 100) return 0; v = v * 10 + nm[i] - '0'; }
    if (v > 100) return 0;
    o[0] = o[1] = o[2] = x11_gray[v];
    return 1;
  }
  {
    int vi = 0, lo = 0, hi = (int)(sizeof x11 / sizeof x11[0]) - 1;
    if (k > 1 && nm[k - 1] >= '1' && nm[k - 1] <= '4') { vi = nm[k - 1] - '0'; nm[k - 1] = 0; }
    while (lo <= hi) {
      int mid = (lo + hi) / 2, c = strcmp(nm, x11[mid].name);
      if (!c) {
        if (vi && (x11[mid].flags & 2)) { memcpy(o, x11[mid].v + 3 * vi, 3); return 1; }
        if (!vi && (x11[mid].flags & 1)) { memcpy(o, x11[mid].v, 3); return 1; }
        return 0;
      }
      if (c < 0) hi = mid - 1; else lo = mid + 1;
    }
    if (vi) return 0;
  }
  /* web names X11 lacks */
  if (!strcmp(nm, "silver")) { o[0] = o[1] = o[2] = 192; return 1; }
  if (!strcmp(nm, "olive")) { o[0] = 128; o[1] = 128; o[2] = 0; return 1; }
  if (!strcmp(nm, "teal")) { o[0] = 0; o[1] = 128; o[2] = 128; return 1; }
  if (!strcmp(nm, "fuchsia")) { o[0] = 255; o[1] = 0; o[2] = 255; return 1; }
  return 0;
}

/* the text lines of an XPM: each C string of an XPM3 (comments skipped, escapes undone), or each
   line of an XPM2 after its "! XPM2" line. Their bytes go into buf (n bytes is enough), their
   starts and lengths into ls / ll; returns the count, -1 if damaged */
static long xpm_lines(const u8 *d, size_t n, u8 *buf, size_t *ls, size_t *ll, size_t maxl)
{
  size_t i = 0, o = 0;
  long cnt = 0;
  if (n >= 9 && !memcmp(d, "/* XPM */", 9)) {
    while (i < n) {
      if (d[i] == '/' && i + 1 < n && d[i + 1] == '*') {
        i += 2;
        while (i + 1 < n && !(d[i] == '*' && d[i + 1] == '/')) i++;
        if (i + 1 >= n) return -1;                       /* unterminated comment */
        i += 2;
      } else if (d[i] == '/' && i + 1 < n && d[i + 1] == '/') {
        while (i < n && d[i] != '\n') i++;
      } else if (d[i] == '"') {
        size_t s = o;
        int closed = 0;
        i++;
        while (i < n) {
          int c = d[i++];
          if (c == '"') { closed = 1; break; }
          if (c == '\\') {
            if (i >= n) return -1;
            c = d[i++];
            c = c == 'n' ? '\n' : c == 'r' ? '\r' : c == 't' ? '\t' : c == '0' ? 0 : c;
          }
          buf[o++] = (u8)c;
        }
        if (!closed || (size_t)cnt >= maxl) return -1;
        ls[cnt] = s; ll[cnt] = o - s; cnt++;
      } else i++;
    }
    return cnt;
  }
  /* XPM2: the marker line, then lines (comments and blank lines only before the first) */
  while (i < n && d[i] != '\n') i++;
  i++;
  while (i < n) {
    size_t s = i, e;
    while (i < n && d[i] != '\n') i++;
    e = i;
    if (e > s && d[e - 1] == '\r') e--;
    i++;
    if (!cnt && (e == s || d[s] == '!')) continue;
    if ((size_t)cnt >= maxl) return -1;
    memcpy(buf + o, d + s, e - s);
    ls[cnt] = o; ll[cnt] = e - s; cnt++;
    o += e - s;
  }
  return cnt;
}

/* a palette entry: its key (cpp bytes) and colour */
typedef struct { u8 key[8]; u8 rgba[4]; } XpmEnt;
static size_t xpm_cpp;
static int xpm_cmp(const void *a, const void *b) { return memcmp(((const XpmEnt *)a)->key, ((const XpmEnt *)b)->key, xpm_cpp); }

int xpm_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  u8 *buf = NULL;
  size_t *ls = NULL, *ll = NULL, maxl, cpp;
  long nl, i;
  u32 w = 0, h = 0, nc = 0, x, y;
  XpmEnt *pal = NULL;
  const char *msg = "Damaged XPM file";
  int ok = 0;
  out->w = out->h = 0; out->px = NULL;
  if (!((n >= 9 && !memcmp(d, "/* XPM */", 9)) || (n >= 7 && !memcmp(d, "! XPM2", 6) && (d[6] == '\n' || d[6] == '\r')))) { codec_err(err, errcap, "Not an XPM file"); return 0; }
  maxl = n / 2 + 2;
  if (!(buf = (u8 *)malloc(n + 1)) || !(ls = (size_t *)malloc(sizeof(size_t) * maxl)) || !(ll = (size_t *)malloc(sizeof(size_t) * maxl))) { codec_oom = 1; msg = "Out of memory"; goto done; }
  if ((nl = xpm_lines(d, n, buf, ls, ll, maxl)) < 1) goto done;
  /* the header: width height colours chars-per-pixel */
  {
    Rd r;
    u32 c;
    r.d = buf + ls[0]; r.n = ll[0]; r.pos = 0;
    if (!rd_uint(&r, &w) || !rd_uint(&r, &h) || !rd_uint(&r, &nc) || !rd_uint(&r, &c)) { msg = "Damaged XPM header"; goto done; }
    cpp = c;
  }
  if (!size_ok(w, h)) { msg = "Bad XPM picture size"; goto done; }
  if (nc < 1 || nc > 65536 || cpp < 1 || cpp > 8) { msg = "Bad XPM palette size"; goto done; }
  if ((u32)nl < 1 + nc + h) { msg = "Truncated XPM file"; goto done; }
  if (!(pal = (XpmEnt *)malloc(sizeof(XpmEnt) * nc))) { codec_oom = 1; msg = "Out of memory"; goto done; }
  /* the palette: after the key, "c" (colour) beats "g", "g4" and "m" whatever their order */
  for (i = 0; i < (long)nc; i++) {
    const u8 *s = buf + ls[1 + i];
    size_t len = ll[1 + i], p, vs = 0, ve = 0;
    int best = 0;
    if (len < cpp) { msg = "Damaged XPM palette"; goto done; }
    memset(pal[i].key, 0, 8);
    memcpy(pal[i].key, s, cpp);
    p = cpp;
    while (p < len) {
      size_t ks, ke, s0, e0;
      int pri;
      while (p < len && is_ws(s[p])) p++;
      ks = p;
      while (p < len && !is_ws(s[p])) p++;
      ke = p;
      if (ke == ks) break;
      pri = tok_is(s + ks, ke - ks, "c") ? 4 : tok_is(s + ks, ke - ks, "g") ? 3 : tok_is(s + ks, ke - ks, "g4") ? 2 : tok_is(s + ks, ke - ks, "m") ? 1 : 0;
      /* the value: every word up to the next key (a name may have spaces) */
      s0 = e0 = p;
      for (;;) {
        size_t q = p, ws, we;
        while (q < len && is_ws(s[q])) q++;
        ws = q;
        while (q < len && !is_ws(s[q])) q++;
        we = q;
        if (we == ws || tok_is(s + ws, we - ws, "c") || tok_is(s + ws, we - ws, "g") || tok_is(s + ws, we - ws, "g4") ||
            tok_is(s + ws, we - ws, "m") || tok_is(s + ws, we - ws, "s")) break;
        if (e0 == s0) s0 = ws;
        e0 = we; p = q;
      }
      if (pri > best && e0 > s0) { best = pri; vs = s0; ve = e0; }
    }
    if (!best || !xpm_colour(s + vs, ve - vs, pal[i].rgba)) { msg = "Unsupported or missing XPM colour"; goto done; }
  }
  xpm_cpp = cpp;
  qsort(pal, nc, sizeof(XpmEnt), xpm_cmp);
  for (i = 1; i < (long)nc; i++) if (!memcmp(pal[i - 1].key, pal[i].key, cpp)) { msg = "Duplicate XPM palette key"; goto done; }
  /* every row must be the right length before the picture is made */
  for (y = 0; y < h; y++) if (ll[1 + nc + y] != (size_t)w * cpp) { msg = "An XPM row has the wrong width"; goto done; }
  if (!alloc(out, w, h, err, errcap)) { msg = NULL; goto done; }
  for (y = 0; y < h; y++) {
    const u8 *s = buf + ls[1 + nc + y];
    u8 *o = out->px + (size_t)y * w * 4;
    for (x = 0; x < w; x++, o += 4, s += cpp) {
      long lo = 0, hi = (long)nc - 1, hit = -1;
      while (lo <= hi) {
        long mid = (lo + hi) / 2;
        int c = memcmp(s, pal[mid].key, cpp);
        if (!c) { hit = mid; break; }
        if (c < 0) hi = mid - 1; else lo = mid + 1;
      }
      if (hit < 0) { msg = "Unknown XPM palette key"; goto done; }
      memcpy(o, pal[hit].rgba, 4);
    }
  }
  ok = 1;
done:
  free(buf); free(ls); free(ll); free(pal);
  if (!ok) { rgba_free(out); if (msg) codec_err(err, errcap, msg); }
  return ok;
}

/* ------------------------------------------------------------------ DDS */
/* 565 to 8-bit channels, rounded (PhotoCraft's rgb565) */
static void rgb565(u32 c, u8 *o) { o[0] = (u8)(((c >> 11) * 255 + 15) / 31); o[1] = (u8)((((c >> 5) & 63) * 255 + 31) / 63); o[2] = (u8)(((c & 31) * 255 + 15) / 31); }
/* a BC1 colour block (8 bytes) into 16 texels; four colours always unless bc1 and c0 <= c1 */
static void bc_colour(const u8 *b, int bc1, u8 t[16][4])
{
  u8 pal[4][4];
  u32 c0 = le16(b), c1 = le16(b + 2), idx = le32(b + 4);
  int i, k;
  rgb565(c0, pal[0]); rgb565(c1, pal[1]);
  pal[0][3] = pal[1][3] = pal[2][3] = 255;
  if (!bc1 || c0 > c1) {
    for (k = 0; k < 3; k++) { pal[2][k] = (u8)((2 * pal[0][k] + pal[1][k] + 1) / 3); pal[3][k] = (u8)((pal[0][k] + 2 * pal[1][k] + 1) / 3); }
    pal[3][3] = 255;
  } else {
    for (k = 0; k < 3; k++) pal[2][k] = (u8)((pal[0][k] + pal[1][k] + 1) / 2);
    pal[3][0] = pal[3][1] = pal[3][2] = pal[3][3] = 0;
  }
  for (i = 0; i < 16; i++) memcpy(t[i], pal[(idx >> (2 * i)) & 3], 4);
}
/* a BC3/BC4 value block (8 bytes) into 16 values: eight interpolated, or six plus 0 and 255 */
static void bc_alpha(const u8 *b, u8 *v)
{
  u32 a0 = b[0], a1 = b[1], pal[8], k, lo = le16(b + 2) | ((u32)b[4] << 16), hi = le16(b + 5) | ((u32)b[7] << 16);
  int i;
  pal[0] = a0; pal[1] = a1;
  if (a0 > a1) for (k = 2; k < 8; k++) pal[k] = ((8 - k) * a0 + (k - 1) * a1 + 3) / 7;
  else { for (k = 2; k < 6; k++) pal[k] = ((6 - k) * a0 + (k - 1) * a1 + 2) / 5; pal[6] = 0; pal[7] = 255; }
  for (i = 0; i < 16; i++) v[i] = (u8)pal[i < 8 ? (lo >> (3 * i)) & 7 : (hi >> (3 * (i - 8))) & 7];
}

/* BC7: the tables (partitions as bit masks, verified against Pillow) */
/* BC7 partitions: bit i (2 bits for three subsets) is texel i's subset */
static const u16 bc7_p2[64] = {
  0xCCCC, 0x8888, 0xEEEE, 0xECC8, 0xC880, 0xFEEC, 0xFEC8, 0xEC80, 0xC800, 0xFFEC, 0xFE80, 0xE800,
  0xFFE8, 0xFF00, 0xFFF0, 0xF000, 0xF710, 0x008E, 0x7100, 0x08CE, 0x008C, 0x7310, 0x3100, 0x8CCE,
  0x088C, 0x3110, 0x6666, 0x366C, 0x17E8, 0x0FF0, 0x718E, 0x399C, 0xAAAA, 0xF0F0, 0x5A5A, 0x33CC,
  0x3C3C, 0x55AA, 0x9696, 0xA55A, 0x73CE, 0x13C8, 0x324C, 0x3BDC, 0x6996, 0xC33C, 0x9966, 0x0660,
  0x0272, 0x04E4, 0x4E40, 0x2720, 0xC936, 0x936C, 0x39C6, 0x639C, 0x9336, 0x9CC6, 0x817E, 0xE718,
  0xCCF0, 0x0FCC, 0x7744, 0xEE22,
};
static const u32 bc7_p3[64] = {
  0xAA685050, 0x6A5A5040, 0x5A5A4200, 0x5450A0A8, 0xA5A50000, 0xA0A05050, 0x5555A0A0, 0x5A5A5050,
  0xAA550000, 0xAA555500, 0xAAAA5500, 0x90909090, 0x94949494, 0xA4A4A4A4, 0xA9A59450, 0x2A0A4250,
  0xA5945040, 0x0A425054, 0xA5A5A500, 0x55A0A0A0, 0xA8A85454, 0x6A6A4040, 0xA4A45000, 0x1A1A0500,
  0x0050A4A4, 0xAAA59090, 0x14696914, 0x69691400, 0xA08585A0, 0xAA821414, 0x50A4A450, 0x6A5A0200,
  0xA9A58000, 0x5090A0A8, 0xA8A09050, 0x24242424, 0x00AA5500, 0x24924924, 0x24499224, 0x50A50A50,
  0x500AA550, 0xAAAA4444, 0x66660000, 0xA5A0A5A0, 0x50A050A0, 0x69286928, 0x44AAAA44, 0x66666600,
  0xAA444444, 0x54A854A8, 0x95809580, 0x96969600, 0xA85454A8, 0x80959580, 0xAA141414, 0x96960000,
  0xAAAA1414, 0xA05050A0, 0xA0A5A5A0, 0x96000000, 0x40804080, 0xA9A8A9A8, 0xAAAAAA44, 0x2A4A5254,
};
static const u8 bc7_a2[64] = {
  15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 2, 8, 2, 2, 8, 8, 15, 2, 8, 2, 2, 8, 8, 2, 2,
  15, 15, 6, 8, 2, 8, 15, 15, 2, 8, 2, 2, 2, 15, 15, 6, 6, 2, 6, 8, 15, 15, 2, 2, 15, 15, 15, 15, 15, 2, 2, 15 };
static const u8 bc7_a3a[64] = {
  3, 3, 15, 15, 8, 3, 15, 15, 8, 8, 6, 6, 6, 5, 3, 3, 3, 3, 8, 15, 3, 3, 6, 10, 5, 8, 8, 6, 8, 5, 15, 15,
  8, 15, 3, 5, 6, 10, 8, 15, 15, 3, 15, 5, 15, 15, 15, 15, 3, 15, 5, 5, 5, 8, 5, 10, 5, 10, 8, 13, 15, 12, 3, 3 };
static const u8 bc7_a3b[64] = {
  15, 8, 8, 3, 15, 15, 3, 8, 15, 15, 15, 15, 15, 15, 15, 8, 15, 8, 15, 3, 15, 8, 15, 8, 3, 15, 6, 10, 15, 15, 10, 8,
  15, 3, 15, 10, 10, 8, 9, 10, 6, 15, 8, 15, 3, 6, 6, 8, 15, 3, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 3, 15, 15, 8 };
/* per mode: subsets, partition bits, rotation bits, index-selection bit, colour bits, alpha bits,
   a p-bit per endpoint, a p-bit per subset, index bits, second index bits */
static const u8 bc7_modes[8][10] = {
  { 3, 4, 0, 0, 4, 0, 1, 0, 3, 0 }, { 2, 6, 0, 0, 6, 0, 0, 1, 3, 0 }, { 3, 6, 0, 0, 5, 0, 0, 0, 2, 0 }, { 2, 6, 0, 0, 7, 0, 1, 0, 2, 0 },
  { 1, 0, 2, 1, 5, 6, 0, 0, 2, 3 }, { 1, 0, 2, 0, 7, 8, 0, 0, 2, 2 }, { 1, 0, 0, 0, 7, 7, 1, 0, 4, 0 }, { 2, 6, 0, 0, 5, 5, 1, 0, 2, 0 } };
static const u8 bc7_w2[4] = { 0, 21, 43, 64 }, bc7_w3[8] = { 0, 9, 18, 27, 37, 46, 55, 64 },
  bc7_w4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };
static u32 bc7_bits(const u8 *b, int *pos, int nb)
{
  u32 v = 0;
  int i;
  for (i = 0; i < nb; i++, (*pos)++) v |= (u32)((b[*pos >> 3] >> (*pos & 7)) & 1) << i;
  return v;
}
static int bc7_mix(int a, int b, int bits, u32 idx)
{
  int w = bits == 2 ? bc7_w2[idx] : bits == 3 ? bc7_w3[idx] : bc7_w4[idx];
  return ((64 - w) * a + w * b + 32) >> 6;
}
static void bc7_block(const u8 *b, u8 t[16][4])
{
  const u8 *m;
  int mode, pos, ns, part, rot, isel, s, e, c, i, cbits, abits, p = 0;
  u8 ep[3][2][4], idx1[16], idx2[16];
  for (mode = 0; mode < 8 && !((b[0] >> mode) & 1); mode++);
  if (mode == 8) { memset(t, 0, 64); return; }           /* a reserved mode: transparent black */
  m = bc7_modes[mode];
  pos = mode + 1; ns = m[0];
  part = (int)bc7_bits(b, &pos, m[1]); rot = (int)bc7_bits(b, &pos, m[2]); isel = (int)bc7_bits(b, &pos, m[3]);
  for (c = 0; c < 3; c++) for (s = 0; s < ns; s++) for (e = 0; e < 2; e++) ep[s][e][c] = (u8)bc7_bits(b, &pos, m[4]);
  for (s = 0; s < ns; s++) for (e = 0; e < 2; e++) ep[s][e][3] = (u8)(m[5] ? bc7_bits(b, &pos, m[5]) : 255);
  cbits = m[4]; abits = m[5];
  if (m[6] || m[7]) {                                    /* p-bits: a low bit for every channel */
    for (s = 0; s < ns; s++)
      for (e = 0; e < 2; e++) {
        if (m[6] || e == 0) p = (int)bc7_bits(b, &pos, 1);
        for (c = 0; c < 4; c++) if (c < 3 || abits) ep[s][e][c] = (u8)((ep[s][e][c] << 1) | p);
      }
    cbits++;
    if (abits) abits++;
  }
  for (s = 0; s < ns; s++)                               /* to 8 bits, the top bits repeated below */
    for (e = 0; e < 2; e++)
      for (c = 0; c < 4; c++) {
        int bits = c < 3 ? cbits : abits;
        if (c == 3 && !abits) continue;
        ep[s][e][c] = (u8)((ep[s][e][c] << (8 - bits)) | (ep[s][e][c] >> (2 * bits - 8)));
      }
  for (i = 0; i < 16; i++) {
    int anchor = i == 0 || (ns == 2 && i == bc7_a2[part]) || (ns == 3 && (i == bc7_a3a[part] || i == bc7_a3b[part]));
    idx1[i] = (u8)bc7_bits(b, &pos, m[8] - anchor);
  }
  for (i = 0; i < 16; i++) idx2[i] = (u8)(m[9] ? bc7_bits(b, &pos, m[9] - (i == 0)) : 0);
  for (i = 0; i < 16; i++) {
    int sub = ns == 1 ? 0 : ns == 2 ? (bc7_p2[part] >> i) & 1 : (int)((bc7_p3[part] >> (2 * i)) & 3), cb = m[8], ab = m[8];
    u32 ci = idx1[i], ai = idx1[i];
    if (sub >= ns) sub = 0;
    if (m[9]) {
      if (isel) { cb = m[9]; ci = idx2[i]; } else { ab = m[9]; ai = idx2[i]; }
    }
    for (c = 0; c < 3; c++) t[i][c] = (u8)bc7_mix(ep[sub][0][c], ep[sub][1][c], cb, ci);
    t[i][3] = (u8)(m[5] ? bc7_mix(ep[sub][0][3], ep[sub][1][3], ab, ai) : 255);
    if (rot) { u8 tmp = t[i][3]; t[i][3] = t[i][rot - 1]; t[i][rot - 1] = tmp; }
  }
}

/* a channel mask: where it starts and its largest value; 0 if empty or not one run of bits */
static int dds_field(u32 mask, u32 bits, u32 *shift, u32 *max)
{
  *shift = 0; *max = 0;
  if (!mask) return 1;
  while (!((mask >> *shift) & 1)) (*shift)++;
  *max = mask >> *shift;
  if ((*max & (*max + 1)) || (bits < 32 && (mask >> bits))) return 0;
  return 1;
}

int dds_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  u32 flags, w, h, pfl, bits = 0, pitch, kind = 0, rowb, rows, bx, by, x, y, i;
  u32 mask[4] = { 0, 0, 0, 0 }, shift[4], max[4];
  size_t start = 128;
  int gray = 0, premul = 0;
  const u8 *fcc;
  out->w = out->h = 0; out->px = NULL;
  if (n < 128 || memcmp(d, "DDS ", 4)) { codec_err(err, errcap, "Not a DDS file"); return 0; }
  if (le32(d + 4) != 124) { codec_err(err, errcap, "Damaged DDS header"); return 0; }
  flags = le32(d + 8); h = le32(d + 12); w = le32(d + 16); pitch = le32(d + 20);
  if (le32(d + 112) & 0x200) { codec_err(err, errcap, "DDS cubemaps are not supported"); return 0; }
  if ((le32(d + 112) & 0x200000) || ((flags & 0x800000) && le32(d + 24) > 1)) { codec_err(err, errcap, "DDS volume textures are not supported"); return 0; }
  pfl = le32(d + 80); fcc = d + 84;
  /* kind: 0 masks, 1-5 BC1-BC5, 7 BC7 */
  if ((pfl & 4) && !memcmp(fcc, "DX10", 4)) {
    u32 dxgi;
    static const u32 rgba[4] = { 0xFF, 0xFF00, 0xFF0000UL, 0xFF000000UL };
    if (n < 148) { codec_err(err, errcap, "Truncated DDS file"); return 0; }
    dxgi = le32(d + 128);
    if (le32(d + 136) & 4) { codec_err(err, errcap, "DDS cubemaps are not supported"); return 0; }
    if (le32(d + 132) != 3) { codec_err(err, errcap, "Only 2D DDS textures are supported"); return 0; }
    if (le32(d + 140) > 1) { codec_err(err, errcap, "DDS texture arrays are not supported"); return 0; }
    start = 148;
    if (dxgi >= 27 && dxgi <= 29) { bits = 32; memcpy(mask, rgba, sizeof mask); }
    else if (dxgi == 87 || dxgi == 90 || dxgi == 91) { bits = 32; mask[0] = 0xFF0000UL; mask[1] = 0xFF00; mask[2] = 0xFF; mask[3] = 0xFF000000UL; }
    else if (dxgi == 88 || dxgi == 92 || dxgi == 93) { bits = 32; mask[0] = 0xFF0000UL; mask[1] = 0xFF00; mask[2] = 0xFF; }
    else if (dxgi == 85) { bits = 16; mask[0] = 0xF800; mask[1] = 0x7E0; mask[2] = 0x1F; }
    else if (dxgi == 86) { bits = 16; mask[0] = 0x7C00; mask[1] = 0x3E0; mask[2] = 0x1F; mask[3] = 0x8000; }
    else if (dxgi == 61 || dxgi == 60) { bits = 8; mask[0] = 0xFF; gray = 1; }
    else if (dxgi >= 70 && dxgi <= 72) kind = 1;
    else if (dxgi >= 73 && dxgi <= 75) kind = 2;
    else if (dxgi >= 76 && dxgi <= 78) kind = 3;
    else if (dxgi == 79 || dxgi == 80) kind = 4;
    else if (dxgi == 82 || dxgi == 83) kind = 5;
    else if (dxgi >= 97 && dxgi <= 99) kind = 7;
    else { codec_err(err, errcap, "Unsupported DDS pixel format"); return 0; }
  } else if (pfl & 4) {
    if (!memcmp(fcc, "DXT1", 4)) kind = 1;
    else if (!memcmp(fcc, "DXT3", 4) || !memcmp(fcc, "DXT2", 4)) { kind = 2; premul = fcc[3] == '2'; }
    else if (!memcmp(fcc, "DXT5", 4) || !memcmp(fcc, "DXT4", 4)) { kind = 3; premul = fcc[3] == '4'; }
    else if (!memcmp(fcc, "ATI1", 4) || !memcmp(fcc, "BC4U", 4)) kind = 4;
    else if (!memcmp(fcc, "ATI2", 4) || !memcmp(fcc, "BC5U", 4)) kind = 5;
    else { codec_err(err, errcap, "Unsupported DDS compression"); return 0; }
  } else if (pfl & 0x40) {                               /* RGB, by masks */
    bits = le32(d + 88);
    for (i = 0; i < 3; i++) mask[i] = le32(d + 92 + i * 4);
    if (pfl & 1) mask[3] = le32(d + 104);
  } else if (pfl & 0x20000) {                            /* luminance */
    bits = le32(d + 88); gray = 1;
    mask[0] = le32(d + 92);
    if (pfl & 1) mask[3] = le32(d + 104);
  } else { codec_err(err, errcap, "Unsupported DDS pixel format"); return 0; }
  if (!size_ok(w, h)) { codec_err(err, errcap, "Bad DDS picture size"); return 0; }
  if (kind == 0) {
    if (!(bits == 8 || bits == 16 || bits == 24 || bits == 32)) { codec_err(err, errcap, "Unsupported DDS pixel size"); return 0; }
    for (i = 0; i < 4; i++) if (!dds_field(mask[i], bits, &shift[i], &max[i])) { codec_err(err, errcap, "Damaged DDS channel mask"); return 0; }
    if (!mask[0] && !mask[1] && !mask[2]) { codec_err(err, errcap, "The DDS file has no colour channels"); return 0; }
    rowb = w * (bits / 8);
    if ((flags & 8) && pitch) {                          /* DDSD_PITCH: the row stride */
      if (pitch < rowb) { codec_err(err, errcap, "Damaged DDS row pitch"); return 0; }
      rowb = pitch;
    }
    rows = h;
  } else { rowb = ((w + 3) / 4) * (kind == 1 || kind == 4 ? 8 : 16); rows = (h + 3) / 4; }
  if (start > n || (double)rowb * rows > (double)(n - start)) { codec_err(err, errcap, "Truncated DDS file"); return 0; }
  if (!alloc(out, w, h, err, errcap)) return 0;
  if (kind == 0) {
    u32 bpp = bits / 8;
    for (y = 0; y < h; y++) {
      const u8 *p = d + start + (size_t)y * rowb;
      u8 *o = out->px + (size_t)y * w * 4;
      for (x = 0; x < w; x++, p += bpp, o += 4) {
        u32 v = 0, k;
        for (k = bpp; k-- > 0; ) v = (v << 8) | p[k];
        for (k = 0; k < 4; k++) o[k] = (u8)(mask[k] ? ((((v >> shift[k]) & max[k]) * 255UL + max[k] / 2) / max[k]) : 0);
        if (!mask[3]) o[3] = 255;
        if (gray) o[1] = o[2] = o[0];
      }
    }
    return 1;
  }
  for (by = 0; by < rows; by++)
    for (bx = 0; bx < (w + 3) / 4; bx++) {
      const u8 *b = d + start + (size_t)by * rowb + (size_t)bx * (kind == 1 || kind == 4 ? 8 : 16);
      u8 t[16][4], v[16], v2[16];
      int k;
      switch (kind) {
      case 1: bc_colour(b, 1, t); break;
      case 2: bc_colour(b + 8, 0, t); for (k = 0; k < 16; k++) t[k][3] = (u8)(((b[k / 2] >> (4 * (k & 1))) & 15) * 17); break;
      case 3: bc_colour(b + 8, 0, t); bc_alpha(b, v); for (k = 0; k < 16; k++) t[k][3] = v[k]; break;
      case 4: bc_alpha(b, v); for (k = 0; k < 16; k++) { t[k][0] = t[k][1] = t[k][2] = v[k]; t[k][3] = 255; } break;
      case 5: bc_alpha(b, v); bc_alpha(b + 8, v2); for (k = 0; k < 16; k++) { t[k][0] = v[k]; t[k][1] = v2[k]; t[k][2] = 0; t[k][3] = 255; } break;
      default: bc7_block(b, t); break;
      }
      for (k = 0; k < 16; k++) {
        u32 px = bx * 4 + (u32)(k & 3), py = by * 4 + (u32)(k >> 2);
        if (px < w && py < h) {
          u8 *o = out->px + ((size_t)py * w + px) * 4;
          memcpy(o, t[k], 4);
          if (premul && o[3] && o[3] < 255) {            /* DXT2/DXT4: premultiplied, made straight */
            int c;
            for (c = 0; c < 3; c++) { int q = (o[c] * 255 + o[3] / 2) / o[3]; o[c] = (u8)(q > 255 ? 255 : q); }
          }
        }
      }
    }
  return 1;
}

/* ------------------------------------------------------------------ QOI */
/* "qoif", width, height, channels, colour space; then ops from (0,0,0,255), each pixel also
   remembered at its hash; then seven 0 bytes and a 1 */
int qoi_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  u32 w, h, total, i = 0;
  u8 idx[64][4], px[4] = { 0, 0, 0, 255 };
  size_t p = 14;
  int ch;
  out->w = out->h = 0; out->px = NULL;
  if (n < 14 || memcmp(d, "qoif", 4)) { codec_err(err, errcap, "Not a QOI file"); return 0; }
  w = be32(d + 4); h = be32(d + 8); ch = d[12];
  if (ch != 3 && ch != 4) { codec_err(err, errcap, "Damaged QOI header"); return 0; }
  if (!size_ok(w, h)) { codec_err(err, errcap, "Bad QOI picture size"); return 0; }
  total = w * h;
  if ((double)(n - 14) * 62 < (double)total) { codec_err(err, errcap, "Truncated QOI file"); return 0; }
  if (!alloc(out, w, h, err, errcap)) return 0;
  memset(idx, 0, sizeof idx);
  while (i < total) {
    u32 run = 1;
    int b;
    if (p >= n) return fail(out, err, errcap, "Truncated QOI file");
    b = d[p++];
    if (b == 0xFE) {
      if (n - p < 3) return fail(out, err, errcap, "Truncated QOI file");
      px[0] = d[p]; px[1] = d[p + 1]; px[2] = d[p + 2]; p += 3;
    } else if (b == 0xFF) {
      if (n - p < 4) return fail(out, err, errcap, "Truncated QOI file");
      memcpy(px, d + p, 4); p += 4;
    } else if ((b & 0xC0) == 0x00) memcpy(px, idx[b], 4);
    else if ((b & 0xC0) == 0x40) {
      px[0] = (u8)(px[0] + ((b >> 4) & 3) - 2); px[1] = (u8)(px[1] + ((b >> 2) & 3) - 2); px[2] = (u8)(px[2] + (b & 3) - 2);
    } else if ((b & 0xC0) == 0x80) {
      int dg, b2;
      if (p >= n) return fail(out, err, errcap, "Truncated QOI file");
      b2 = d[p++]; dg = (b & 0x3F) - 32;
      px[0] = (u8)(px[0] + dg + (b2 >> 4) - 8); px[1] = (u8)(px[1] + dg); px[2] = (u8)(px[2] + dg + (b2 & 15) - 8);
    } else run = (u32)(b & 0x3F) + 1;
    memcpy(idx[(px[0] * 3 + px[1] * 5 + px[2] * 7 + px[3] * 11) & 63], px, 4);
    if (run > total - i) run = total - i;
    while (run--) {
      u8 *o = out->px + (size_t)i++ * 4;
      memcpy(o, px, 4);
      if (ch == 3) o[3] = 255;
    }
  }
  return 1;
}

/* ------------------------------------------------------------------ ICO / CUR */
/* an icon's bits per pixel from its data (a PNG's IHDR or the DIB header), for choosing */
static u32 ico_bpp(const u8 *p, size_t len)
{
  static const u8 sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
  if (len >= 26 && !memcmp(p, sig, 8)) {
    static const u8 ch[7] = { 1, 0, 3, 1, 2, 0, 4 };
    return p[24] * (p[25] <= 6 ? ch[p[25]] : 1);
  }
  return len >= 16 ? le16(p + 14) : 0;
}

/* the directory (type 1 icon, 2 cursor), the biggest entry (then the deepest), then that entry:
   a PNG as it is, or a DIB (of twice the height: the colours, then the 1-bit AND mask) given a
   BMP file header so bmp.c can read it, the mask applied unless 32-bit alpha says more */
int ico_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  static const u8 sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
  u32 cnt, i, best = 0, bestarea = 0, bestbpp = 0, off, len;
  const u8 *e;
  int found = 0;
  out->w = out->h = 0; out->px = NULL;
  if (n < 6 || le16(d) != 0 || (le16(d + 2) != 1 && le16(d + 2) != 2) || !(cnt = le16(d + 4))) { codec_err(err, errcap, "Not an ICO or CUR file"); return 0; }
  for (i = 0; i < cnt && 6 + (size_t)i * 16 + 16 <= n; i++) {
    u32 ew, eh, area, bpp;
    e = d + 6 + i * 16;
    ew = e[0] ? e[0] : 256; eh = e[1] ? e[1] : 256;
    off = le32(e + 12); len = le32(e + 8);
    if (off >= n || len > n - off || len < 16) continue;
    area = ew * eh; bpp = ico_bpp(d + off, len);
    if (!found || area > bestarea || (area == bestarea && bpp > bestbpp)) { best = i; bestarea = area; bestbpp = bpp; found = 1; }
  }
  if (!found) { codec_err(err, errcap, "The icon file has no readable image"); return 0; }
  e = d + 6 + best * 16;
  off = le32(e + 12); len = le32(e + 8);
  if (len >= 8 && !memcmp(d + off, sig, 8)) return png_read(d + off, len, out, err, errcap);
  {
    u32 hs = le32(d + off), dw, dh, bpp, comp, ncol, palb, stride, mstride, x, y, xor_at;
    u8 *b;
    int ok, alpha = 0;
    if (hs < 40 || hs > 1024 || len < hs) { codec_err(err, errcap, "Damaged icon image"); return 0; }
    dw = le32(d + off + 4); dh = le32(d + off + 8); bpp = le16(d + off + 14); comp = le32(d + off + 16);
    if (dh & 0x80000000UL) { codec_err(err, errcap, "Damaged icon image"); return 0; }
    dh /= 2;                                             /* the colours and the mask, one above the other */
    if (!size_ok(dw, dh) || !(bpp == 1 || bpp == 4 || bpp == 8 || bpp == 16 || bpp == 24 || bpp == 32)) { codec_err(err, errcap, "Damaged icon image"); return 0; }
    ncol = bpp <= 8 ? (le32(d + off + 32) && le32(d + off + 32) <= 256 ? le32(d + off + 32) : 1u << bpp) : 0;
    palb = ncol * 4 + (comp == 3 && hs == 40 ? 12 : 0);
    if (!(b = (u8 *)malloc((size_t)len + 14))) { codec_oom = 1; codec_err(err, errcap, "Out of memory"); return 0; }
    b[0] = 'B'; b[1] = 'M';
    b[2] = (u8)(len + 14); b[3] = (u8)((len + 14) >> 8); b[4] = (u8)((len + 14) >> 16); b[5] = (u8)((len + 14) >> 24);
    memset(b + 6, 0, 4);
    xor_at = 14 + hs + palb;
    b[10] = (u8)xor_at; b[11] = (u8)(xor_at >> 8); b[12] = (u8)(xor_at >> 16); b[13] = (u8)(xor_at >> 24);
    memcpy(b + 14, d + off, len);
    b[22] = (u8)dh; b[23] = (u8)(dh >> 8); b[24] = (u8)(dh >> 16); b[25] = (u8)(dh >> 24);
    ok = bmp_read(b, (size_t)len + 14, out, err, errcap);
    free(b);
    if (!ok) return 0;
    /* the AND mask (rows bottom up, 1 transparent) for anything without real alpha */
    if (bpp == 32) {
      long k, np = (long)dw * dh;
      for (k = 0; k < np && !alpha; k++) if (out->px[k * 4 + 3] != 255) alpha = 1;
    }
    stride = ((dw * bpp + 31) / 32) * 4; mstride = ((dw + 31) / 32) * 4;
    xor_at = hs + palb + stride * dh;
    if (!alpha && xor_at <= len && (double)mstride * dh <= (double)(len - xor_at))
      for (y = 0; y < dh; y++) {
        const u8 *m = d + off + xor_at + (size_t)(dh - 1 - y) * mstride;
        for (x = 0; x < dw; x++) if ((m[x >> 3] >> (7 - (x & 7))) & 1) out->px[((size_t)y * dw + x) * 4 + 3] = 0;
      }
    return 1;
  }
}

/* ------------------------------------------------------------------ Radiance HDR */
/* the header lines to a blank line, the resolution line, then RGBE scanlines: new-style RLE (each
   channel run-length coded on its own), or flat pixels with old-style repeats. The RGBE values go
   into the picture first, then are tone-mapped there: scaled so the log-average luminance is 0.18,
   then Reinhard's c / (1 + c) and the sRGB curve */
int hdr_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  size_t p = 0;
  u32 w = 0, h = 0, x, y;
  int flip = 0, xyze = 0;
  long i, np;
  double sum = 0, scale;
  out->w = out->h = 0; out->px = NULL;
  if (n < 6 || memcmp(d, "#?", 2)) { codec_err(err, errcap, "Not a Radiance HDR file"); return 0; }
  for (;;) {                                             /* header lines */
    size_t s = p;
    while (p < n && d[p] != '\n') p++;
    if (p >= n) { codec_err(err, errcap, "Truncated Radiance HDR header"); return 0; }
    if (p - s >= 7 && !memcmp(d + s, "FORMAT=", 7)) xyze = !(p - s >= 22 && !memcmp(d + s + 7, "32-bit_rle_rgbe", 15));
    p++;
    if (p - s == 1) break;
  }
  if (xyze) { codec_err(err, errcap, "Only RGBE Radiance files are supported"); return 0; }
  {
    char line[64];
    size_t s = p, k;
    char ys;
    unsigned long a, b;
    while (p < n && d[p] != '\n') p++;
    if (p >= n || p - s >= sizeof line) { codec_err(err, errcap, "Damaged Radiance HDR resolution"); return 0; }
    memcpy(line, d + s, p - s); line[p - s] = 0;
    p++;
    for (k = 0; line[k]; k++) if (!(line[k] == ' ' || line[k] == '+' || line[k] == '-' || line[k] == 'X' || line[k] == 'Y' || (line[k] >= '0' && line[k] <= '9'))) break;
    if (line[k] || (line[0] != '-' && line[0] != '+') || line[1] != 'Y') { codec_err(err, errcap, "Unsupported Radiance HDR orientation"); return 0; }
    ys = line[0];
    a = strtoul(line + 2, NULL, 10);
    {
      char *q = strchr(line + 2, 'X');
      if (!q || q[-1] != '+') { codec_err(err, errcap, "Unsupported Radiance HDR orientation"); return 0; }
      b = strtoul(q + 1, NULL, 10);
    }
    if (a > MAXDIM || b > MAXDIM) { codec_err(err, errcap, "Bad Radiance HDR picture size"); return 0; }
    h = (u32)a; w = (u32)b; flip = ys == '+';
  }
  if (!size_ok(w, h)) { codec_err(err, errcap, "Bad Radiance HDR picture size"); return 0; }
  if ((double)(n - p) * 32 < (double)w * h) { codec_err(err, errcap, "Truncated Radiance HDR file"); return 0; }
  if (!alloc(out, w, h, err, errcap)) return 0;
  for (y = 0; y < h; y++) {
    u8 *o = out->px + (size_t)(flip ? h - 1 - y : y) * w * 4;
    if (w >= 8 && w < 32768 && n - p >= 4 && d[p] == 2 && d[p + 1] == 2 && !(d[p + 2] & 0x80)) {
      int c;
      if (((u32)d[p + 2] << 8 | d[p + 3]) != w) return fail(out, err, errcap, "Damaged Radiance HDR scanline");
      p += 4;
      for (c = 0; c < 4; c++)
        for (x = 0; x < w; ) {
          u32 cnt;
          if (p >= n) return fail(out, err, errcap, "Truncated Radiance HDR file");
          cnt = d[p++];
          if (cnt > 128) {
            cnt -= 128;
            if (p >= n || cnt > w - x) return fail(out, err, errcap, "Damaged Radiance HDR scanline");
            while (cnt--) o[(x++) * 4 + c] = d[p];
            p++;
          } else {
            if (!cnt || cnt > w - x || cnt > n - p) return fail(out, err, errcap, "Damaged Radiance HDR scanline");
            while (cnt--) o[(x++) * 4 + c] = d[p++];
          }
        }
    } else {
      int shift = 0;
      for (x = 0; x < w; ) {
        if (n - p < 4) return fail(out, err, errcap, "Truncated Radiance HDR file");
        if (d[p] == 1 && d[p + 1] == 1 && d[p + 2] == 1) {   /* old-style repeat of the pixel before */
          u32 cnt = (u32)d[p + 3] << shift;
          if (!x || shift > 16 || cnt > w - x) return fail(out, err, errcap, "Damaged Radiance HDR scanline");
          while (cnt--) { memcpy(o + x * 4, o + (x - 1) * 4, 4); x++; }
          shift += 8;
        } else { memcpy(o + x * 4, d + p, 4); x++; shift = 0; }
        p += 4;
      }
    }
  }
  /* tone mapping */
  np = (long)w * h;
  for (i = 0; i < np; i++) {
    const u8 *q = out->px + i * 4;
    double f = q[3] ? ldexp(1.0, q[3] - 136) : 0, l = (0.2126 * q[0] + 0.7152 * q[1] + 0.0722 * q[2]) * f;
    sum += log(1e-4 + l);
  }
  scale = 0.18 / exp(sum / np);
  for (i = 0; i < np; i++) {
    u8 *q = out->px + i * 4;
    double f = q[3] ? ldexp(1.0, q[3] - 136) * scale : 0;
    int c;
    for (c = 0; c < 3; c++) { double v = q[c] * f; q[c] = (u8)srgb8(v / (1 + v)); }
    q[3] = 255;
  }
  return 1;
}

/* ------------------------------------------------------------------ FITS */
/* a header card's value (the text after "= ", up to a / comment), as a number; 0 if none */
static int fits_num(const u8 *card, double *v)
{
  char s[72];
  size_t k = 0, i;
  char *end;
  if (card[8] != '=' || card[9] != ' ') return 0;
  for (i = 10; i < 80 && card[i] != '/'; i++) s[k++] = (char)card[i];
  s[k] = 0;
  *v = strtod(s, &end);
  return end != s;
}
static int fits_key(const u8 *card, const char *k) { size_t l = strlen(k); return !memcmp(card, k, l) && (l == 8 || card[l] == ' '); }

int fits_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  size_t hdu = 0, data = 0;
  long bitpix = 0, naxis = 0, ax[3] = { 0, 0, 0 }, hd;
  double bzero = 0, bscale = 1, blank = 0, mn = 0, mx = 0, v;
  int haveblank = 0, topdown = 0, found = 0, planes, intok;
  u32 w, h, x, y;
  long i, np, k;
  out->w = out->h = 0; out->px = NULL;
  if (n < 2880 || memcmp(d, "SIMPLE  =", 9)) { codec_err(err, errcap, "Not a FITS file"); return 0; }
  /* the primary HDU, else the first IMAGE extension (at most 8 HDUs looked at) */
  for (hd = 0; hd < 8 && !found; hd++) {
    size_t c = hdu, size;
    int end = 0, image = hd == 0;
    long pcount = 0, gcount = 1;
    bitpix = 0; naxis = 0; ax[0] = ax[1] = ax[2] = 1; bzero = 0; bscale = 1; haveblank = 0; topdown = 0;
    if (hd && (n - hdu < 80 || memcmp(d + hdu, "XTENSION=", 9))) break;
    if (hd && n - hdu >= 20 && !memcmp(d + hdu + 10, "'IMAGE", 6)) image = 1;
    for (; !end && c + 80 <= n; c += 80) {
      const u8 *cd = d + c;
      if (fits_key(cd, "END")) end = 1;
      else if (fits_key(cd, "BITPIX") && fits_num(cd, &v)) bitpix = (long)v;
      else if (fits_key(cd, "NAXIS") && fits_num(cd, &v)) naxis = (long)v;
      else if (fits_key(cd, "NAXIS1") && fits_num(cd, &v)) ax[0] = v >= 0 && v < 1e9 ? (long)v : -1;
      else if (fits_key(cd, "NAXIS2") && fits_num(cd, &v)) ax[1] = v >= 0 && v < 1e9 ? (long)v : -1;
      else if (fits_key(cd, "NAXIS3") && fits_num(cd, &v)) ax[2] = v >= 0 && v < 1e9 ? (long)v : -1;
      else if (fits_key(cd, "BZERO") && fits_num(cd, &v)) bzero = v;
      else if (fits_key(cd, "BSCALE") && fits_num(cd, &v)) bscale = v;
      else if (fits_key(cd, "BLANK") && fits_num(cd, &v)) { blank = v; haveblank = 1; }
      else if (fits_key(cd, "PCOUNT") && fits_num(cd, &v)) pcount = v >= 0 && v < 1e9 ? (long)v : -1;
      else if (fits_key(cd, "GCOUNT") && fits_num(cd, &v)) gcount = v >= 0 && v < 1e9 ? (long)v : -1;
      else if (fits_key(cd, "ZIMAGE")) { codec_err(err, errcap, "Tile-compressed FITS files are not supported"); return 0; }
      else if (fits_key(cd, "ROWORDER") && cd[8] == '=') { size_t q; for (q = 10; q + 8 < 80; q++) if (!memcmp(cd + q, "TOP-DOWN", 8)) topdown = 1; }
    }
    if (!end) { codec_err(err, errcap, "Truncated FITS header"); return 0; }
    data = (c + 2879) / 2880 * 2880;
    if (!(bitpix == 8 || bitpix == 16 || bitpix == 32 || bitpix == 64 || bitpix == -32 || bitpix == -64) || naxis < 0 || naxis > 999 ||
        ax[0] < 0 || ax[1] < 0 || ax[2] < 0 || pcount < 0 || gcount < 0) { codec_err(err, errcap, "Damaged FITS header"); return 0; }
    if (image && naxis >= 2 && ax[0] > 0 && ax[1] > 0) { found = 1; break; }
    /* the data of an HDU we skip */
    size = naxis ? (size_t)((labs(bitpix) / 8) * (double)ax[0] * (naxis > 1 ? ax[1] : 1) * (naxis > 2 ? ax[2] : 1)) : 0;
    if ((double)size + pcount > (double)(n - data)) break;
    hdu = data + (size + (size_t)pcount + 2879) / 2880 * 2880;
    if (hdu >= n) break;
  }
  if (!found) { codec_err(err, errcap, "The FITS file has no image"); return 0; }
  w = (u32)ax[0]; h = (u32)ax[1];
  planes = naxis >= 3 && ax[2] == 3 ? 3 : 1;
  if (!size_ok(w, h)) { codec_err(err, errcap, "Bad FITS picture size"); return 0; }
  if ((double)w * h * planes * (labs(bitpix) / 8) > (double)(n - data)) { codec_err(err, errcap, "Truncated FITS file"); return 0; }
  np = (long)w * h;
  /* the physical values (BZERO + BSCALE x stored), NaN and BLANK as 0, their range */
#define FITS_VAL(idx, out_v) do { \
    const u8 *q_ = d + data + (size_t)(idx) * (labs(bitpix) / 8); double r_; int bl_ = 0; \
    switch (bitpix) { \
    case 8: r_ = q_[0]; break; \
    case 16: r_ = (double)(i16)be16(q_); break; \
    case 32: r_ = (double)(i32)be32(q_); break; \
    case 64: r_ = (double)(i32)be32(q_) * 4294967296.0 + be32(q_ + 4); break; \
    case -32: { union { u32 u; float f; } f_; f_.u = be32(q_); r_ = f_.f; break; } \
    default: { u32 hi_ = be32(q_), lo_ = be32(q_ + 4); int e_ = (int)((hi_ >> 20) & 2047); \
      double m_ = (double)(hi_ & 0xFFFFF) * 4294967296.0 + lo_; \
      r_ = e_ == 0 ? 0 : e_ == 2047 ? (m_ ? 0 : 1e300) : ldexp(m_ + 4503599627370496.0, e_ - 1075); if (hi_ & 0x80000000UL) r_ = -r_; } \
    } \
    if (haveblank && bitpix > 0 && r_ == blank) bl_ = 1; \
    out_v = bl_ || r_ != r_ ? 0 : bzero + bscale * r_; } while (0)
  intok = bitpix > 0 && bscale == floor(bscale) && bzero == floor(bzero);
  for (i = 0; i < np * planes; i++) {
    FITS_VAL(i, v);
    if (!i || v < mn) mn = v;
    if (!i || v > mx) mx = v;
  }
  if (!alloc(out, w, h, err, errcap)) return 0;
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      u8 *o = out->px + ((size_t)(topdown ? y : h - 1 - y) * w + x) * 4;
      for (k = 0; k < planes; k++) {
        double s;
        FITS_VAL((long)k * np + (long)y * w + x, v);
        /* exact 8-bit data as it is; integers that fit 16 bits over 65535; floats up to 2 taken as
           0-1 already, else over 65535 when they fit it, else over the maximum */
        if (intok && bitpix == 8 && mn >= 0 && mx <= 255) s = v / 255;
        else if (intok && mn >= 0 && mx <= 65535) s = v / 65535;
        else if (mx <= 2) s = v;
        else if (mx <= 65535) s = v / 65535;
        else s = v / mx;
        o[k] = (u8)unit8(s);
      }
      if (planes == 1) o[1] = o[2] = o[0];
      o[3] = 255;
    }
#undef FITS_VAL
  return 1;
}

/* ------------------------------------------------------------------ DPX */
int dpx_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  int be;
  u32 w, h, desc, bits, packing, enc, off, pad, nc, per, line, stride, x, y, k, mx;
#define DPX16(o) (be ? be16(d + (o)) : le16(d + (o)))
#define DPX32(o) (be ? be32(d + (o)) : le32(d + (o)))
  out->w = out->h = 0; out->px = NULL;
  if (n < 2048 || !(!memcmp(d, "SDPX", 4) || !memcmp(d, "XPDS", 4))) { codec_err(err, errcap, "Not a DPX file"); return 0; }
  be = d[0] == 'S';
  if (DPX16(770) < 1 || DPX16(770) > 8) { codec_err(err, errcap, "Damaged DPX header"); return 0; }
  w = DPX32(772); h = DPX32(776);
  desc = d[800]; bits = d[803]; packing = DPX16(804); enc = DPX16(806);
  off = DPX32(808); if (!off || off == 0xFFFFFFFFUL) off = DPX32(4);
  pad = DPX32(812); if (pad == 0xFFFFFFFFUL) pad = 0;
  nc = desc == 6 ? 1 : desc == 50 ? 3 : desc == 51 ? 4 : 0;
  if (!nc) { codec_err(err, errcap, "Unsupported DPX descriptor (only luma, RGB and RGBA)"); return 0; }
  if (DPX32(780) == 1) { codec_err(err, errcap, "Signed DPX samples are not supported"); return 0; }
  if (enc) { codec_err(err, errcap, "Run-length encoded DPX files are not supported"); return 0; }
  if (!((bits == 8 && packing == 0) || (bits == 16 && packing == 0) || (bits == 10 && packing <= 2) || (bits == 12 && packing <= 1))) {
    codec_err(err, errcap, "Unsupported DPX bit size or packing"); return 0;
  }
  if (!size_ok(w, h)) { codec_err(err, errcap, "Bad DPX picture size"); return 0; }
  per = w * nc;
  line = (bits == 10 && packing) ? (per + 2) / 3 * 4 : (bits == 12 && packing) ? (per + 1) / 2 * 4 : (u32)(((double)per * bits + 31) / 32) * 4;
  if (pad > 1u << 24) { codec_err(err, errcap, "Damaged DPX header"); return 0; }
  stride = line + pad;
  if (off > n) { codec_err(err, errcap, "Truncated DPX file"); return 0; }
  /* 8- and 16-bit lines some writers leave unpadded, when the padded ones wouldn't fit */
  if ((bits == 8 || bits == 16) && !pad && line != per * (bits / 8) && (double)stride * (h - 1) + line > (double)(n - off)) line = stride = per * (bits / 8);
  if ((double)stride * (h - 1) + line > (double)(n - off)) { codec_err(err, errcap, "Truncated DPX file"); return 0; }
  if (!alloc(out, w, h, err, errcap)) return 0;
  mx = (1u << bits) - 1;
  for (y = 0; y < h; y++) {
    const u8 *l = d + off + (size_t)y * stride;
    u32 acc_hi = 0, have = 0, word = 0;
    u8 *o = out->px + (size_t)y * w * 4;
    for (k = 0; k < per; k++) {
      u32 c;
      if (bits == 8) c = l[k];
      else if (bits == 16) c = be ? be16(l + k * 2) : le16(l + k * 2);
      else if (bits == 10 && packing) {
        u32 wd = be ? be32(l + k / 3 * 4) : le32(l + k / 3 * 4);
        c = (wd >> ((packing == 1 ? 22 : 20) - 10 * (k % 3))) & 0x3FF;
      } else if (bits == 12 && packing) c = ((be ? be16(l + k * 2) : le16(l + k * 2)) >> 4) & 0xFFF;
      else {                                             /* packing 0: one bit stream through the 32-bit words, high bits first */
        if (have < bits) {
          u32 wd = be ? be32(l + word * 4) : le32(l + word * 4);
          word++;
          c = ((acc_hi << (bits - have)) | (wd >> (32 - (bits - have)))) & mx;
          acc_hi = wd; have += 32 - bits;
          acc_hi &= have ? (u32)((1UL << have) - 1) : 0;
        } else {
          have -= bits;
          c = (acc_hi >> have) & mx;
          acc_hi &= have ? (u32)((1UL << have) - 1) : 0;
        }
      }
      o[(k / nc) * 4 + (nc == 1 ? 0 : k % nc)] = (u8)((c * 255 + mx / 2) / mx);
    }
    for (x = 0; x < w; x++, o += 4) {
      if (nc == 1) o[1] = o[2] = o[0];
      if (nc != 4) o[3] = 255;
    }
  }
  return 1;
#undef DPX16
#undef DPX32
}

/* ------------------------------------------------------------------ detection */
int rasters_detect(const u8 *d, size_t n, size_t namelen_unused)
{
  (void)namelen_unused;
  if (n >= 8 && ((d[0] == 'I' && d[1] == 'I' && (d[2] == 42 || d[2] == 43) && d[3] == 0) || (d[0] == 'M' && d[1] == 'M' && d[2] == 0 && (d[3] == 42 || d[3] == 43)))) return RAS_TIFF;
  if (n >= 26 && !memcmp(d, "8BPS", 4) && d[4] == 0 && (d[5] == 1 || d[5] == 2)) return RAS_PSD;
  if (n >= 128 && !memcmp(d, "DDS ", 4)) return RAS_DDS;
  if (n >= 14 && !memcmp(d, "qoif", 4)) return RAS_QOI;
  if ((n >= 9 && !memcmp(d, "/* XPM */", 9)) || (n >= 7 && !memcmp(d, "! XPM2", 6))) return RAS_XPM;
  if (n >= 3 && d[0] == 'P' && ((d[1] >= '1' && d[1] <= '7') || d[1] == 'F' || d[1] == 'f') && is_ws(d[2])) return RAS_PNM;
  if (n >= 6 && (!memcmp(d, "#?RADIANCE", n >= 10 ? 10 : 0) || !memcmp(d, "#?RGBE", 6))) return RAS_HDR;
  if (n >= 2880 && !memcmp(d, "SIMPLE  =", 9)) return RAS_FITS;
  if (n >= 2048 && (!memcmp(d, "SDPX", 4) || !memcmp(d, "XPDS", 4))) return RAS_DPX;
  if (n >= 22 && le16(d) == 0 && (le16(d + 2) == 1 || le16(d + 2) == 2) && le16(d + 4) >= 1 && d[9] == 0) {
    /* an icon directory: the first entry's data must be in the file and look like a PNG or a DIB */
    u32 cnt = le16(d + 4), off = le32(d + 18), len = le32(d + 14);
    if (6 + (size_t)cnt * 16 <= n && off >= 6 + cnt * 16 && off < n && len <= n - off && len >= 8 &&
        ((d[off] == 137 && d[off + 1] == 'P') || (len >= 40 && le32(d + off) == 40))) return RAS_ICO;
  }
  if (pcx_like(d, n)) return RAS_PCX;
  if (tga_like(d, n)) return RAS_TGA;
  return RAS_UNKNOWN;
}

const char *rasters_name(int fmt)
{
  static const char *const names[] = { "unknown", "TIFF", "Photoshop", "Targa", "PCX", "Netpbm", "XPM", "DDS", "QOI", "ICO", "Radiance HDR", "FITS", "DPX" };
  return fmt >= 0 && fmt <= RAS_DPX ? names[fmt] : names[0];
}

int rasters_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  out->w = out->h = 0; out->px = NULL;
  codec_oom = 0;
  switch (rasters_detect(d, n, 0)) {
  case RAS_TIFF: return tiff_read(d, n, out, err, errcap);
  case RAS_PSD: return psd_read(d, n, out, err, errcap);
  case RAS_TGA: return tga_read(d, n, out, err, errcap);
  case RAS_PCX: return pcx_read(d, n, out, err, errcap);
  case RAS_PNM: return pnm_read(d, n, out, err, errcap);
  case RAS_XPM: return xpm_read(d, n, out, err, errcap);
  case RAS_DDS: return dds_read(d, n, out, err, errcap);
  case RAS_QOI: return qoi_read(d, n, out, err, errcap);
  case RAS_ICO: return ico_read(d, n, out, err, errcap);
  case RAS_HDR: return hdr_read(d, n, out, err, errcap);
  case RAS_FITS: return fits_read(d, n, out, err, errcap);
  case RAS_DPX: return dpx_read(d, n, out, err, errcap);
  }
  codec_err(err, errcap, "Unknown file format");
  return 0;
}
