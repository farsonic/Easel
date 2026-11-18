/*
 * tiff.c - reading TIFF and BigTIFF: the first full-resolution page of a file, in either byte
 * order, from strips or tiles, chunky or planar. Compression: none, PackBits, LZW (new style and
 * the old bit-reversed style), Deflate (old and Adobe codes), CCITT modified Huffman RLE, Group 3
 * (1D and 2D) and Group 4, and new-style JPEG (through image.c's decoder, with JPEGTables).
 * Predictors 2 (horizontal) and 3 (floating point). Samples of 1-16 and 32 bits (unsigned or
 * signed) and 16/32/64-bit floats; gray (either polarity), palette, RGB, CMYK, YCbCr (without
 * subsampling, or under JPEG) and CIE Lab, with straight or premultiplied alpha.
 *
 * Parts ported from PhotoCraft (https://github.com/storytold/photocraft), Copyright (c) 2026
 * ArtCraft Team and the PhotoCraft contributors, used under the MIT License (see Licenses.txt).
 * Ported for TIFF/BigTIFF (crates/codecs/src/codecs/tiff_ifd.rs, tiff/decode.rs): the
 * bounds-checked directory walk (header, entry values inline or by offset, the entry budget
 * against overlapping tables, the choice of the first full-resolution page by NewSubfileType /
 * SubfileType), which extra sample is the alpha, the sample-count and chunk-table checks and the
 * decompression-bomb guard. PhotoCraft leaves decompression to the `tiff` crate, so PackBits, LZW,
 * the CCITT codes, the predictors and the sample conversion are written here from the TIFF 6.0
 * specification (Adobe) and ITU-T Recommendations T.4 / T.6.
 *
 * Every number is read a byte at a time in the file's order. Every offset and count is checked
 * against the file; a chunk that is missing or fails to decompress fails the whole picture with a
 * message rather than leaving holes.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rasters.h"
#include "cms.h"                       /* 1.2: cmyk_hook */

/* ------------------------------------------------------------------ the file */
typedef struct { const u8 *d; size_t n; int le, big; } Tf;

static u32 g16(const Tf *t, size_t o) { const u8 *p = t->d + o; return t->le ? p[0] | ((u32)p[1] << 8) : ((u32)p[0] << 8) | p[1]; }
static u32 g32(const Tf *t, size_t o)
{
  const u8 *p = t->d + o;
  return t->le ? p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24)
               : ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}
/* a BigTIFF 64-bit number; anything from 4GB up (more than a 32-bit machine can hold) reads as
   0xFFFFFFFF, which no check lets through */
static u32 g64(const Tf *t, size_t o)
{
  u32 lo = g32(t, t->le ? o : o + 4), hi = g32(t, t->le ? o + 4 : o);
  return hi ? 0xFFFFFFFFUL : lo;
}

/* bytes per value of a field type, 0 for types the specification doesn't define (skipped) */
static u32 type_size(u32 ty)
{
  switch (ty) {
  case 1: case 2: case 6: case 7: return 1;               /* BYTE ASCII SBYTE UNDEFINED */
  case 3: case 8: return 2;                               /* SHORT SSHORT */
  case 4: case 9: case 11: case 13: return 4;             /* LONG SLONG FLOAT IFD */
  case 5: case 10: case 12: case 16: case 17: case 18: return 8;  /* RATIONAL ... LONG8 IFD8 */
  }
  return 0;
}

/* one directory: where its entry table starts and how many entries it has */
typedef struct { size_t first; u32 count; } Dir;
/* one entry, located: ok is set only when all count values lie inside the file */
typedef struct { u32 tag, type, count; size_t at; int ok; } Ent;

static size_t ent_size(const Tf *t) { return t->big ? 20 : 12; }

/* the directory at off: its whole table and next-directory pointer must be inside the file, after
   the header. *next gets the next directory's offset (0 at the end of the chain) */
static int dir_at(const Tf *t, u32 off, Dir *dir, u32 *next)
{
  size_t hl = t->big ? 16 : 8, cnt, tab;
  if (off < hl || off >= t->n) return 0;
  if (t->big) {
    if (t->n - off < 8) return 0;
    cnt = g64(t, off);
    dir->first = (size_t)off + 8;
  } else {
    if (t->n - off < 2) return 0;
    cnt = g16(t, off);
    dir->first = (size_t)off + 2;
  }
  if (cnt > 65535) return 0;
  tab = cnt * ent_size(t) + (t->big ? 8 : 4);
  if (tab > t->n - dir->first) return 0;
  dir->count = (u32)cnt;
  *next = t->big ? g64(t, dir->first + cnt * 20) : g32(t, dir->first + cnt * 12);
  return 1;
}

/* the first entry with this tag (the table is inside the file: dir_at checked it) */
static int ent_find(const Tf *t, const Dir *dir, u32 tag, Ent *e)
{
  u32 i, sz;
  size_t es = ent_size(t), inl = t->big ? 8 : 4;
  for (i = 0; i < dir->count; i++) {
    size_t p = dir->first + i * es;
    if (g16(t, p) != tag) continue;
    e->tag = tag; e->type = g16(t, p + 2);
    e->count = t->big ? g64(t, p + 4) : g32(t, p + 4);
    e->ok = 0; e->at = 0;
    if (!(sz = type_size(e->type))) continue;            /* an unknown type is skipped */
    if (e->count <= inl / sz) e->at = p + (t->big ? 12 : 8);
    else e->at = t->big ? g64(t, p + 12) : g32(t, p + 8);
    e->ok = e->at <= t->n && e->count <= (t->n - e->at) / sz;
    return 1;
  }
  return 0;
}

/* value i of an unsigned integer entry (BYTE, SHORT, LONG, IFD, LONG8, IFD8); 0 if unreadable */
static int ent_uint(const Tf *t, const Ent *e, u32 i, u32 *v)
{
  if (!e->ok || i >= e->count) return 0;
  switch (e->type) {
  case 1: *v = t->d[e->at + i]; return 1;
  case 3: *v = g16(t, e->at + (size_t)i * 2); return 1;
  case 4: case 13: *v = g32(t, e->at + (size_t)i * 4); return 1;
  case 16: case 18: *v = g64(t, e->at + (size_t)i * 8); return 1;
  }
  return 0;
}
/* the first value of a tag, or dflt when the tag is absent or unreadable */
static u32 tag_u(const Tf *t, const Dir *dir, u32 tag, u32 dflt)
{
  Ent e;
  u32 v;
  return ent_find(t, dir, tag, &e) && ent_uint(t, &e, 0, &v) ? v : dflt;
}

/* the page Photoshop would open: the first directory of the main chain that is neither a
   reduced-resolution copy nor a mask and has a size, else the first directory. At most 1000
   directories are looked at, and their entries together may not exceed what the file could hold
   (overlapping tables in a hostile file would otherwise cost a full parse each) */
static int pick_page(const Tf *t, u32 first, Dir *page)
{
  Dir dir;
  u32 off = first, next, budget = (u32)(t->n / 12) + 64;
  int k, have = 0;
  for (k = 0; k < 1000 && off; k++) {
    if (!dir_at(t, off, &dir, &next) || dir.count > budget) break;
    budget -= dir.count;
    if (!have) { *page = dir; have = 1; }
    if (!(tag_u(t, &dir, 254, 0) & 5) && tag_u(t, &dir, 255, 1) != 2 && tag_u(t, &dir, 255, 1) != 3 &&
        tag_u(t, &dir, 256, 0) && tag_u(t, &dir, 257, 0)) { *page = dir; return 1; }
    off = next;
  }
  return have;
}

/* ------------------------------------------------------------------ PackBits */
/* until out holds cap bytes: n 0-127 copies n + 1 bytes, -1..-127 repeats the next byte 1 - n
   times, -128 is nothing. Returns the bytes made (short of cap if the data ran out) */
static size_t unpackbits(const u8 *s, size_t n, u8 *out, size_t cap)
{
  size_t i = 0, o = 0, k;
  while (o < cap && i < n) {
    int c = s[i++];
    if (c < 128) {
      k = (size_t)c + 1;
      if (k > n - i) k = n - i;
      if (k > cap - o) k = cap - o;
      memcpy(out + o, s + i, k);
      o += k; i += (size_t)c + 1;
    } else if (c > 128) {
      if (i >= n) break;
      k = 257 - (size_t)c;
      if (k > cap - o) k = cap - o;
      memset(out + o, s[i++], k);
      o += k;
    }
  }
  return o;
}

/* ------------------------------------------------------------------ LZW */
/* the string table: each code is a shorter code plus one byte. 4096 entries, malloc'd (24K) */
typedef struct { u16 prefix[4096], len[4096]; u8 suffix[4096], first[4096]; } Lzw;

/* TIFF LZW into out (cap bytes); returns the bytes made, -1 for a bad code. New-style data is
   MSB-first with the code width growing one code early; old-style (which starts 00 01) is LSB-first
   and grows on time */
static long unlzw(Lzw *z, const u8 *s, size_t n, u8 *out, size_t cap)
{
  u32 bits = 0, nb = 0, code, next = 258, width = 9;
  size_t i = 0, o = 0;
  int old = -1, lsb = n >= 2 && s[0] == 0 && (s[1] & 1), early = !lsb;
  for (code = 0; code < 256; code++) { z->len[code] = 1; z->suffix[code] = z->first[code] = (u8)code; z->prefix[code] = 0; }
  while (o < cap) {
    u32 c, l, k;
    /* the next code */
    while (nb < width) {
      if (i >= n) return (long)o;                        /* out of data: what was made stands */
      if (lsb) bits |= (u32)s[i++] << nb; else bits = (bits << 8) | s[i++];
      nb += 8;
    }
    if (lsb) { code = bits & ((1u << width) - 1); bits >>= width; nb -= width; }
    else { code = (bits >> (nb - width)) & ((1u << width) - 1); nb -= width; }
    if (code == 257) break;                              /* end of information */
    if (code == 256) { next = 258; width = 9; old = -1; continue; }
    if (old < 0) {
      if (code > 255) return -1;
      out[o++] = (u8)code;
      old = (int)code;
      continue;
    }
    if (code > next || (code == next && next >= 4096)) return -1;
    /* the new entry: the previous string plus the first byte of this one (which, for the code
       about to be made, is the previous string's own first byte). The table stops growing at
       4096 until a clear code */
    if (next < 4096) {
      z->prefix[next] = (u16)old;
      z->suffix[next] = code == next ? z->first[old] : z->first[code];
      z->first[next] = z->first[old];
      z->len[next] = (u16)(z->len[old] + 1);
      next++;
    }
    c = code;
    /* the string, written back to front, clipped at cap */
    l = z->len[c];
    for (k = l; k-- > 0; ) {
      if (o + k < cap) out[o + k] = z->suffix[c];
      c = z->prefix[c];
    }
    o += l;
    if (o > cap) o = cap;
    old = (int)code;
    if (next + (u32)early >= (1u << width) && width < 12) width++;
  }
  return (long)o;
}

/* ------------------------------------------------------------------ CCITT */
/* the run-length codes of ITU-T T.4, as bit strings: terminating codes for runs 0-63, then the
   make-up codes for 64-1728 (and, shared by both colours, 1792-2560) */
static const char *const white_codes[] = {
  "00110101", "000111", "0111", "1000", "1011", "1100", "1110", "1111", "10011", "10100", "00111", "01000",
  "001000", "000011", "110100", "110101", "101010", "101011", "0100111", "0001100", "0001000", "0010111",
  "0000011", "0000100", "0101000", "0101011", "0010011", "0100100", "0011000", "00000010", "00000011",
  "00011010", "00011011", "00010010", "00010011", "00010100", "00010101", "00010110", "00010111", "00101000",
  "00101001", "00101010", "00101011", "00101100", "00101101", "00000100", "00000101", "00001010", "00001011",
  "01010010", "01010011", "01010100", "01010101", "00100100", "00100101", "01011000", "01011001", "01011010",
  "01011011", "01001010", "01001011", "00110010", "00110011", "00110100",
  /* 64, 128, ... 1728 */
  "11011", "10010", "010111", "0110111", "00110110", "00110111", "01100100", "01100101", "01101000", "01100111",
  "011001100", "011001101", "011010010", "011010011", "011010100", "011010101", "011010110", "011010111",
  "011011000", "011011001", "011011010", "011011011", "010011000", "010011001", "010011010", "011000",
  "010011011" };
static const char *const black_codes[] = {
  "0000110111", "010", "11", "10", "011", "0011", "0010", "00011", "000101", "000100", "0000100", "0000101",
  "0000111", "00000100", "00000111", "000011000", "0000010111", "0000011000", "0000001000", "00001100111",
  "00001101000", "00001101100", "00000110111", "00000101000", "00000010111", "00000011000", "000011001010",
  "000011001011", "000011001100", "000011001101", "000001101000", "000001101001", "000001101010",
  "000001101011", "000011010010", "000011010011", "000011010100", "000011010101", "000011010110",
  "000011010111", "000001101100", "000001101101", "000011011010", "000011011011", "000001010100",
  "000001010101", "000001010110", "000001010111", "000001100100", "000001100101", "000001010010",
  "000001010011", "000000100100", "000000110111", "000000111000", "000000100111", "000000101000",
  "000001011000", "000001011001", "000000101011", "000000101100", "000001011010", "000001100110",
  "000001100111",
  "0000001111", "000011001000", "000011001001", "000001011011", "000000110011", "000000110100",
  "000000110101", "0000001101100", "0000001101101", "0000001001010", "0000001001011", "0000001001100",
  "0000001001101", "0000001110010", "0000001110011", "0000001110100", "0000001110101", "0000001110110",
  "0000001110111", "0000001010010", "0000001010011", "0000001010100", "0000001010101", "0000001011010",
  "0000001011011", "0000001100100", "0000001100101" };
static const char *const ext_codes[] = {             /* 1792, 1856, ... 2560 */
  "00000001000", "00000001100", "00000001101", "000000010010", "000000010011", "000000010100",
  "000000010101", "000000010110", "000000010111", "000000011100", "000000011101", "000000011110",
  "000000011111" };

/* the codes of one colour, parsed once from the strings and kept sorted by length: start[l] is the
   first code of length l (2-13) */
typedef struct { u16 code[104], run[104]; u8 start[15]; } Codes;
static Codes ccw, ccb;
static int cc_ready;

static void codes_build(Codes *c, const char *const *t, int nt)
{
  int len, i, k = 0;
  for (len = 0; len <= 14; len++) {
    c->start[len] = (u8)k;
    for (i = 0; i < nt + 13; i++) {
      const char *s = i < nt ? t[i] : ext_codes[i - nt];
      u32 v = 0;
      int j;
      if ((int)strlen(s) != len) continue;
      for (j = 0; s[j]; j++) v = (v << 1) | (u32)(s[j] - '0');
      c->code[k] = (u16)v;
      c->run[k] = (u16)(i < 64 ? i : i < nt ? (i - 63) * 64 : 1792 + (i - nt) * 64);
      k++;
    }
  }
}

/* a bit reader over a chunk; rev for FillOrder 2 (least significant bit first) */
typedef struct { const u8 *s; size_t n, pos; int bit, rev; } Bits;
static int bit1(Bits *b)
{
  int v;
  if (b->pos >= b->n) return -1;
  v = b->rev ? (b->s[b->pos] >> b->bit) & 1 : (b->s[b->pos] >> (7 - b->bit)) & 1;
  if (++b->bit == 8) { b->bit = 0; b->pos++; }
  return v;
}
static void bits_align(Bits *b) { if (b->bit) { b->bit = 0; b->pos++; } }

/* one run of a colour: make-up codes then a terminating code. -1 when no code matches */
static long cc_run(Bits *b, const Codes *c)
{
  long total = 0;
  for (;;) {
    u32 v = 0;
    int len, hit = -1;
    for (len = 1; len <= 13 && hit < 0; len++) {
      int x = bit1(b), k;
      if (x < 0) return -1;
      v = (v << 1) | (u32)x;
      for (k = c->start[len]; k < c->start[len + 1]; k++) if (c->code[k] == v) { hit = k; break; }
    }
    if (hit < 0) return -1;
    total += c->run[hit];
    if (c->run[hit] < 64) return total;
    if (total > 1L << 20) return -1;
  }
}

/* the 2D mode code at the reader: 0 pass, 1 horizontal, 2-8 vertical -3..+3 (5 is V0), -1 bad
   (this includes an EOL in the middle of a row, and the uncompressed-mode extension) */
static int cc_mode(Bits *b)
{
  int k, x;
  u32 v = 0;
  for (k = 1; k <= 7; k++) {
    if ((x = bit1(b)) < 0) return -1;
    v = (v << 1) | (u32)x;
    if (k == 1 && v == 1) return 5;                      /* 1        V0 */
    if (k == 3) { if (v == 1) return 1; if (v == 3) return 6; if (v == 2) return 4; }  /* 001 H, 011 VR1, 010 VL1 */
    if (k == 4 && v == 1) return 0;                      /* 0001     P */
    if (k == 6) { if (v == 3) return 7; if (v == 2) return 3; }   /* 000011 VR2, 000010 VL2 */
    if (k == 7) { if (v == 3) return 8; if (v == 2) return 2; }   /* 0000011 VR3, 0000010 VL3 */
  }
  return -1;
}

/* skip fill and an EOL (eleven or more zeros then a one) if one is next; 1 if one was skipped */
static int cc_eol(Bits *b)
{
  Bits t = *b;
  int zeros = 0, x;
  while ((x = bit1(&t)) == 0) zeros++;
  if (x == 1 && zeros >= 11) { *b = t; return 1; }
  return 0;
}

/* a row's changing elements as packed 1-bit pixels (white 0, black 1) */
static void cc_fill(const i32 *ch, int nc, u8 *row, i32 w)
{
  int i;
  memset(row, 0, ((size_t)w + 7) / 8);
  for (i = 0; i + 1 < nc + 1; i += 2) {
    i32 a = ch[i], e = i + 1 < nc ? ch[i + 1] : w, x;
    if (a >= w) break;
    if (e > w) e = w;
    for (x = a; x < e; x++) row[x >> 3] |= (u8)(0x80 >> (x & 7));
  }
}

/* CCITT data (mode 2 RLE, 3 Group 3, 4 Group 4) into rows of packed 1-bit pixels. t4 is
   T4Options / T6Options. ref and cur hold w + 4 changing elements each. 0 if the data is bad */
static int unccitt(const u8 *s, size_t n, int mode, u32 t4, int rev, u8 *out, i32 w, i32 rows, i32 *ref, i32 *cur)
{
  Bits b;
  i32 y;
  int nr = 0, nc;
  if (!cc_ready) { codes_build(&ccw, white_codes, 91); codes_build(&ccb, black_codes, 91); cc_ready = 1; }
  b.s = s; b.n = n; b.pos = 0; b.bit = 0; b.rev = rev;
  if ((mode == 3 && (t4 & 2)) || (mode == 4 && (t4 & 2))) return 0;   /* uncompressed mode */
  ref[0] = ref[1] = ref[2] = w;                          /* the line above the first: all white */
  for (y = 0; y < rows; y++) {
    int twod = mode == 4;
    nc = 0;
    if (mode == 2) bits_align(&b);
    if (mode == 3) {
      cc_eol(&b);                                        /* with or without fill bits before it */
      if (t4 & 1) { int x = bit1(&b); if (x < 0) return 0; twod = !x; }
    }
    if (!twod) {
      /* 1D: runs alternate white and black from white, to the width */
      i32 a = 0;
      int black = 0;
      while (a < w) {
        long r = cc_run(&b, black ? &ccb : &ccw);
        if (r < 0 || r > w - a) return 0;
        a += (i32)r;
        if (a < w) cur[nc++] = a;                        /* a change of colour inside the row */
        black ^= 1;
        if (nc > w + 1) return 0;
      }
    } else {
      i32 a0 = -1;
      int colour = 0, i = 0;
      while (a0 < w) {
        i32 b1, b2;
        int m;
        /* b1: the first change on the line above, right of a0, to the colour opposite a0's */
        while (i > 0 && ref[i - 1] > a0) i--;
        while (ref[i] <= a0 && i < nr) i++;
        if ((i & 1) != colour) i++;
        b1 = ref[i]; b2 = ref[i + 1];
        m = cc_mode(&b);
        if (m < 0) return 0;
        if (m == 0) a0 = b2;                             /* pass: a0 moves under b2, same colour */
        else if (m == 1) {                               /* horizontal: two runs */
          long r1, r2;
          i32 s0 = a0 < 0 ? 0 : a0;
          r1 = cc_run(&b, colour ? &ccb : &ccw);
          if (r1 < 0) return 0;
          r2 = cc_run(&b, colour ? &ccw : &ccb);
          if (r2 < 0 || r1 + r2 > w - s0) return 0;
          cur[nc++] = s0 + (i32)r1;
          cur[nc++] = s0 + (i32)(r1 + r2);
          a0 = s0 + (i32)(r1 + r2);
        } else {                                         /* vertical: a1 within 3 of b1 */
          i32 a1 = b1 + (m - 5);
          if (a1 < (a0 < 0 ? 0 : a0) || a1 > w) return 0;
          cur[nc++] = a1;
          a0 = a1;
          colour ^= 1;
        }
        if (nc > w + 1) return 0;
      }
      while (nc > 0 && cur[nc - 1] >= w) nc--;           /* a change at the right edge is no change */
    }
    cc_fill(cur, nc, out + (size_t)y * (((size_t)w + 7) / 8), w);
    /* this line becomes the reference: its changes, then w as the sentinel (three times, so
       b1 and b2 always exist) */
    memcpy(ref, cur, sizeof(i32) * (size_t)nc);
    nr = nc;
    ref[nr] = ref[nr + 1] = ref[nr + 2] = w;
  }
  return 1;
}

/* ------------------------------------------------------------------ the page */
/* what the decoding needs to know about the samples */
typedef struct {
  i32 w, h;
  u32 spp, bits, fmt, photo, ncolor, alpha, premul, planar, pred, comp, fill;
  int be;                   /* the decompressed chunk's 16/32/64-bit samples are big-endian */
  int slot[64];             /* where sample k goes: 0-3 the RGBA slots, 4 the extra alpha plane, -1 nowhere */
  u8 pal[256][3];
  u8 *ab;                   /* CMYK's alpha (its four colour samples fill the RGBA slots) */
} Lay;

/* half, single and double floats to 0-255 (clamped to 0-1; NaN is 0) */
static int f2b(double v) { return v > 0 ? v < 1 ? (int)(v * 255 + 0.5) : 255 : 0; }
static double half_f(u32 h)
{
  int e = (int)((h >> 10) & 31);
  double m = (double)(h & 1023);
  double v = e == 0 ? ldexp(m, -24) : e == 31 ? (m ? 0 : 1e9) : ldexp(m + 1024, e - 25);
  return h & 0x8000 ? -v : v;
}
static double single_f(u32 u)
{
  int e = (int)((u >> 23) & 255);
  double m = (double)(u & 0x7FFFFF);
  double v = e == 0 ? ldexp(m, -149) : e == 255 ? (m ? 0 : 1e9) : ldexp(m + 8388608.0, e - 150);
  return u & 0x80000000UL ? -v : v;
}
static double double_f(u32 hi, u32 lo)
{
  int e = (int)((hi >> 20) & 2047);
  double m = (double)(hi & 0xFFFFF) * 4294967296.0 + lo;
  double v = e == 0 ? 0 : e == 2047 ? (m ? 0 : 1e9) : ldexp(m + 4503599627370496.0, e - 1075);
  return hi & 0x80000000UL ? -v : v;
}

/* sample idx (counted across the row) of a decompressed row, as 0-255 (a palette index as itself) */
static int sample8(const Lay *L, const u8 *row, u32 idx, int k)
{
  u32 v, b = L->bits;
  if (b == 8) v = row[idx];
  else if (b == 16) { const u8 *p = row + (size_t)idx * 2; v = L->be ? ((u32)p[0] << 8) | p[1] : p[0] | ((u32)p[1] << 8); }
  else if (b == 32) {
    const u8 *p = row + (size_t)idx * 4;
    v = L->be ? ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3] : p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
  } else if (b == 64) {
    const u8 *p = row + (size_t)idx * 8;
    u32 hi, lo;
    if (L->be) { hi = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; lo = ((u32)p[4] << 24) | ((u32)p[5] << 16) | ((u32)p[6] << 8) | p[7]; }
    else { lo = p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); hi = p[4] | ((u32)p[5] << 8) | ((u32)p[6] << 16) | ((u32)p[7] << 24); }
    return f2b(double_f(hi, lo));
  } else {                                               /* packed, most significant bit first */
    u32 bit = idx * b, i;
    v = 0;
    for (i = 0; i < b; i++, bit++) v = (v << 1) | ((row[bit >> 3] >> (7 - (bit & 7))) & 1);
  }
  if (L->fmt == 3) return f2b(b == 16 ? half_f(v) : single_f(v));
  if (L->photo == 3 && k == 0) return (int)v;            /* a palette index */
  /* signed samples (and CIELab's a* and b*) shift to unsigned */
  if (L->fmt == 2 || (L->photo == 8 && (k == 1 || k == 2))) v ^= 1UL << (b - 1);
  if (b == 8) return (int)v;
  if (b > 16) return (int)(v >> (b - 8));
  return (int)((v * 255 + ((1UL << b) - 1) / 2) / ((1UL << b) - 1));
}

/* undo predictor 2 (horizontal differencing, per sample, in the file's byte order) or 3 (bytes
   shuffled by significance, then differenced) on one row of cw pixels of spc samples */
static int unpredict(const Lay *L, u8 *row, u8 *tmp, u32 cw, u32 spc, int le)
{
  u32 i, n = cw * spc;
  if (L->pred == 2) {
    if (L->bits == 8) for (i = spc; i < n; i++) row[i] = (u8)(row[i] + row[i - spc]);
    else if (L->bits == 16) {
      for (i = spc; i < n; i++) {
        u8 *p = row + i * 2, *q = row + (i - spc) * 2;
        u32 v = le ? (p[0] | ((u32)p[1] << 8)) + (q[0] | ((u32)q[1] << 8)) : (((u32)p[0] << 8) | p[1]) + (((u32)q[0] << 8) | q[1]);
        if (le) { p[0] = (u8)v; p[1] = (u8)(v >> 8); } else { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
      }
    } else if (L->bits == 32) {
      for (i = spc; i < n; i++) {
        u8 *p = row + i * 4, *q = row + (i - spc) * 4;
        int k, c = 0;
        for (k = 0; k < 4; k++) {                        /* a 32-bit add, a byte at a time */
          int j = le ? k : 3 - k, s = p[j] + q[j] + c;
          p[j] = (u8)s; c = s >> 8;
        }
      }
    } else return 0;
  } else if (L->pred == 3) {
    u32 bps = L->bits / 8, nb = n * bps, b;
    if (L->fmt != 3 || bps < 2) return 0;
    for (i = spc; i < nb; i++) row[i] = (u8)(row[i] + row[i - spc]);
    memcpy(tmp, row, nb);
    for (i = 0; i < n; i++)                              /* the planes, most significant first, back to big-endian samples */
      for (b = 0; b < bps; b++) row[i * bps + b] = tmp[b * n + i];
  }
  return 1;
}

/* the decoded samples of the RGBA slots made into colours: the polarity, the palette, CMYK, YCbCr,
   Lab; premultiplied alpha made straight */
static void finish(const Lay *L, Rgba *out, int jpeg)
{
  long i, np = (long)L->w * L->h;
  for (i = 0; i < np; i++) {
    u8 *p = out->px + i * 4;
    int a = L->alpha ? (L->ab ? L->ab[i] : p[3]) : 255;
    if (jpeg) { p[3] = 255; continue; }
    switch (L->photo) {
    case 0: p[0] = p[1] = p[2] = (u8)(255 - p[0]); break;
    case 1: p[1] = p[2] = p[0]; break;
    case 3: { const u8 *c = L->pal[p[0]]; p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; break; }
    case 5: {
      int k = 255 - p[3];
      if (cmyk_hook) { u8 ink[4]; memcpy(ink, p, 4); cmyk_hook(ink, p); break; }   /* (1.2) */
      p[0] = (u8)(((255 - p[0]) * k + 127) / 255); p[1] = (u8)(((255 - p[1]) * k + 127) / 255); p[2] = (u8)(((255 - p[2]) * k + 127) / 255);
      break;
    }
    case 6: {                                            /* full-range BT.601, as JPEG has it */
      double y = p[0], cb = p[1] - 128.0, cr = p[2] - 128.0;
      p[0] = (u8)f2b((y + 1.402 * cr) / 255); p[1] = (u8)f2b((y - 0.344136 * cb - 0.714136 * cr) / 255); p[2] = (u8)f2b((y + 1.772 * cb) / 255);
      break;
    }
    case 8: case 9: rasters_lab(p[0], p[1], p[2], p); break;
    }
    p[3] = (u8)a;
    if (L->premul && a > 0 && a < 255) {
      int k;
      for (k = 0; k < 3; k++) { int c = (p[k] * 255 + a / 2) / a; p[k] = (u8)(c > 255 ? 255 : c); }
    }
  }
}

/* a JPEG strip or tile: JPEGTables (SOI, tables, EOI) spliced in front of the chunk's frame, and for
   RGB photometric an Adobe marker saying the components are not YCbCr. The pixels land at x0, y0 */
static int jpeg_chunk(const u8 *tab, size_t tl, const u8 *s, size_t n, int rgb, Rgba *out, i32 x0, i32 y0, char *err, size_t errcap)
{
  static const u8 adobe[16] = { 0xFF, 0xEE, 0, 14, 'A', 'd', 'o', 'b', 'e', 0, 100, 0, 0, 0, 0, 0 };
  u8 *buf;
  size_t len = 0;
  Rgba t;
  i32 x, y;
  int ok;
  if (n < 4 || s[0] != 0xFF || s[1] != 0xD8) { codec_err(err, errcap, "Damaged JPEG data in the TIFF file"); return 0; }
  if (tl < 4 || tab[0] != 0xFF || tab[1] != 0xD8) tl = 0;
  if (!(buf = (u8 *)malloc(tl + n + 16))) { codec_oom = 1; codec_err(err, errcap, "Out of memory"); return 0; }
  buf[len++] = 0xFF; buf[len++] = 0xD8;
  if (rgb) { memcpy(buf + len, adobe, 16); len += 16; }
  if (tl) {                                              /* the tables without their SOI and EOI */
    size_t e = tl;
    if (tab[e - 2] == 0xFF && tab[e - 1] == 0xD9) e -= 2;
    memcpy(buf + len, tab + 2, e - 2); len += e - 2;
  }
  memcpy(buf + len, s + 2, n - 2); len += n - 2;
  ok = jpeg_read(buf, len, &t, err, errcap);
  free(buf);
  if (!ok) return 0;
  for (y = 0; y < t.h && y0 + y < out->h; y++)
    for (x = 0; x < t.w && x0 + x < out->w; x++) memcpy(out->px + ((size_t)(y0 + y) * out->w + x0 + x) * 4, t.px + ((size_t)y * t.w + x) * 4, 3);
  rgba_free(&t);
  return 1;
}

int tiff_read(const u8 *d, size_t n, Rgba *out, char *err, size_t errcap)
{
  Tf t;
  Dir dir;
  Ent eoff, ecnt, e;
  Lay L;
  u32 first, cw, ch, across, down, perplane, nchunks, ci, spc, k, t4 = 0, v;
  size_t rowbytes, chunksize;
  double stored;
  u8 *buf = NULL, *tmp = NULL;
  i32 *ref = NULL, *cur = NULL;
  Lzw *lzw = NULL;
  const u8 *jtab = NULL;
  size_t jtl = 0;
  int tiled, ok = 0, alpha_at = -1, jpeg;
  const char *msg = "Damaged TIFF file";
  out->w = out->h = 0; out->px = NULL;
  memset(&L, 0, sizeof L);
  /* the header: II or MM, then 42 (classic) or 43 (BigTIFF: offset size 8, reserved 0) */
  if (n < 8 || !((d[0] == 'I' && d[1] == 'I') || (d[0] == 'M' && d[1] == 'M'))) { codec_err(err, errcap, "Not a TIFF file"); return 0; }
  t.d = d; t.n = n; t.le = d[0] == 'I'; t.big = 0;
  if (g16(&t, 2) == 42) first = g32(&t, 4);
  else if (g16(&t, 2) == 43 && n >= 16 && g16(&t, 4) == 8 && g16(&t, 6) == 0) { t.big = 1; first = g64(&t, 8); }
  else { codec_err(err, errcap, "Not a TIFF file"); return 0; }
  if (!pick_page(&t, first, &dir)) { codec_err(err, errcap, "The TIFF file's first image directory is missing or cut off"); return 0; }

  /* the picture's description */
  L.w = (i32)tag_u(&t, &dir, 256, 0); L.h = (i32)tag_u(&t, &dir, 257, 0);
  if (tag_u(&t, &dir, 256, 0) > MAXDIM || tag_u(&t, &dir, 257, 0) > MAXDIM || L.w < 1 || L.h < 1 || (long)L.w * L.h > CODEC_MAXPIXELS) {
    codec_err(err, errcap, "Bad TIFF picture size"); return 0;
  }
  L.spp = tag_u(&t, &dir, 277, 1);
  if (L.spp < 1 || L.spp > 64) { codec_err(err, errcap, "Unsupported number of TIFF samples per pixel"); return 0; }
  L.bits = 1;
  if (ent_find(&t, &dir, 258, &e)) {
    if (!ent_uint(&t, &e, 0, &L.bits)) { codec_err(err, errcap, msg); return 0; }
    for (k = 1; k < L.spp && k < e.count; k++)
      if (!ent_uint(&t, &e, k, &v) || v != L.bits) { codec_err(err, errcap, "TIFF samples of different sizes are not supported"); return 0; }
  }
  L.fmt = tag_u(&t, &dir, 339, 1);
  if (L.fmt == 4 || L.fmt == 0) L.fmt = 1;               /* "undefined" reads as unsigned */
  if (!((L.fmt == 1 && ((L.bits >= 1 && L.bits <= 16) || L.bits == 32)) || (L.fmt == 2 && (L.bits == 8 || L.bits == 16 || L.bits == 32)) ||
        (L.fmt == 3 && (L.bits == 16 || L.bits == 32 || L.bits == 64)))) {
    codec_err(err, errcap, "Unsupported TIFF sample format"); return 0;
  }
  L.comp = tag_u(&t, &dir, 259, 1);
  L.photo = tag_u(&t, &dir, 262, L.spp >= 3 ? 2 : 1);
  L.planar = tag_u(&t, &dir, 284, 1) == 2 && L.spp > 1;
  L.pred = tag_u(&t, &dir, 317, 1);
  L.fill = tag_u(&t, &dir, 266, 1);
  jpeg = L.comp == 7;
  switch (L.photo) {
  case 0: case 1: case 3: L.ncolor = 1; break;
  case 2: case 6: case 8: case 9: L.ncolor = 3; break;
  case 5:
    if (tag_u(&t, &dir, 332, 1) == 2) { codec_err(err, errcap, "Only CMYK separations are supported"); return 0; }
    L.ncolor = 4; break;
  default: codec_err(err, errcap, "Unsupported TIFF colour space"); return 0;
  }
  if (L.spp < L.ncolor) { codec_err(err, errcap, "The TIFF file has fewer samples than its colour space needs"); return 0; }
  /* the alpha: the first extra sample marked associated (1) or unassociated (2); gray with one
     unmarked extra sample counts too */
  if (ent_find(&t, &dir, 338, &e)) {
    for (k = 0; k < e.count && k < 64; k++)
      if (ent_uint(&t, &e, k, &v) && (v == 1 || v == 2)) {
        if (L.ncolor + k < L.spp) { alpha_at = (int)k; L.premul = v == 1; }
        break;
      }
  } else if (L.ncolor == 1 && L.spp == 2 && L.photo != 3) alpha_at = 0;
  L.alpha = alpha_at >= 0;
  if (L.photo == 3 && (L.bits > 8 || L.fmt != 1)) { codec_err(err, errcap, "Unsupported TIFF palette depth"); return 0; }
  if (L.photo == 6 && !jpeg) {
    Ent s;
    u32 sh = 2, sv = 2;
    if (ent_find(&t, &dir, 530, &s)) { ent_uint(&t, &s, 0, &sh); ent_uint(&t, &s, 1, &sv); }
    if (sh != 1 || sv != 1) { codec_err(err, errcap, "Subsampled YCbCr TIFF files are not supported"); return 0; }
  }
  if ((L.photo == 8 || L.photo == 9) && L.fmt != 1) { codec_err(err, errcap, "Unsupported TIFF Lab samples"); return 0; }
  if (jpeg && (L.planar || L.bits != 8 || !(L.photo == 1 || L.photo == 2 || L.photo == 6) || L.spp != L.ncolor)) {
    codec_err(err, errcap, "Unsupported kind of JPEG-compressed TIFF"); return 0;
  }
  if (!(L.comp == 1 || L.comp == 2 || L.comp == 3 || L.comp == 4 || L.comp == 5 || L.comp == 7 || L.comp == 8 || L.comp == 32946 || L.comp == 32773)) {
    codec_err(err, errcap, L.comp == 6 ? "Old-style JPEG TIFF files are not supported" : "Unsupported TIFF compression"); return 0;
  }
  if ((L.comp >= 2 && L.comp <= 4) && (L.bits != 1 || L.spp != 1)) { codec_err(err, errcap, "Damaged TIFF file (CCITT data that isn't 1-bit)"); return 0; }
  if (L.pred != 1 && L.pred != 2 && L.pred != 3) { codec_err(err, errcap, "Unsupported TIFF predictor"); return 0; }
  if (L.pred != 1 && (L.comp == 1 || jpeg || (L.comp >= 2 && L.comp <= 4))) L.pred = 1;   /* meaningless there: ignored */
  if (L.comp == 3) t4 = tag_u(&t, &dir, 292, 0);
  if (L.comp == 4) t4 = tag_u(&t, &dir, 293, 0);
  /* where each sample goes */
  for (k = 0; k < 64; k++) L.slot[k] = -1;
  for (k = 0; k < L.ncolor; k++) L.slot[k] = (int)k;
  if (L.alpha) L.slot[L.ncolor + alpha_at] = L.ncolor == 4 ? 4 : 3;
  /* the palette: 3 x 2^bits SHORTs, red then green then blue */
  if (L.photo == 3) {
    u32 np = 1u << L.bits, i, r, g, b;
    if (!ent_find(&t, &dir, 320, &e) || e.count < 3 * np) { codec_err(err, errcap, "The TIFF palette is missing"); return 0; }
    for (i = 0; i < np; i++) {
      if (!ent_uint(&t, &e, i, &r) || !ent_uint(&t, &e, np + i, &g) || !ent_uint(&t, &e, 2 * np + i, &b)) { codec_err(err, errcap, "The TIFF palette is cut off"); return 0; }
      L.pal[i][0] = (u8)(r >> 8); L.pal[i][1] = (u8)(g >> 8); L.pal[i][2] = (u8)(b >> 8);
    }
  }
  /* the chunks: strips, or tiles */
  tiled = ent_find(&t, &dir, 324, &eoff);
  if (tiled) {
    cw = tag_u(&t, &dir, 322, 0); ch = tag_u(&t, &dir, 323, 0);
    if (!ent_find(&t, &dir, 325, &ecnt)) ecnt.ok = 0, ecnt.count = 0;
    if (cw < 1 || ch < 1 || cw > MAXDIM || ch > MAXDIM || (double)cw * ch > 4.0 * L.w * L.h + 262144.0) {
      codec_err(err, errcap, "Bad TIFF tile size"); return 0;
    }
  } else {
    if (!ent_find(&t, &dir, 273, &eoff)) { codec_err(err, errcap, "The TIFF file has no image data"); return 0; }
    if (!ent_find(&t, &dir, 279, &ecnt)) ecnt.ok = 0, ecnt.count = 0;
    cw = (u32)L.w; ch = tag_u(&t, &dir, 278, (u32)L.h);
    if (ch < 1 || ch > (u32)L.h) ch = (u32)L.h;
  }
  across = (u32)(((u32)L.w + cw - 1) / cw); down = (u32)(((u32)L.h + ch - 1) / ch);
  perplane = across * down;
  nchunks = perplane * (L.planar ? L.spp : 1);
  if (!eoff.ok || eoff.count < nchunks) { codec_err(err, errcap, "The TIFF file's strip or tile table is cut off"); return 0; }
  if (ecnt.count && (!ecnt.ok || ecnt.count < nchunks)) { codec_err(err, errcap, "The TIFF file's strip or tile table is cut off"); return 0; }
  if (!ecnt.count && (L.comp != 1 || nchunks != 1)) { codec_err(err, errcap, "The TIFF file has no strip byte counts"); return 0; }
  spc = L.planar ? 1 : L.spp;
  if ((double)cw * spc * L.bits > 2147483647.0) { codec_err(err, errcap, "Bad TIFF tile size"); return 0; }
  rowbytes = ((size_t)cw * spc * L.bits + 7) / 8;
  chunksize = rowbytes * ch;
  /* decompression-bomb guard: no method makes more than a few thousand bytes of one (PackBits 128,
     Deflate about 1032, LZW about 2730; CCITT needs a bit a row), so a file far too small for the
     size it claims is refused before anything is allocated */
  stored = (double)L.w * L.h * L.spp * L.bits / 8;
  if (L.comp >= 2 && L.comp <= 4) { if ((double)n * 8 < (double)L.h) { codec_err(err, errcap, "Truncated TIFF file"); return 0; } }
  else if (!jpeg && stored > (double)n * (L.comp == 1 ? 1 : L.comp == 32773 ? 130 : 4096) + 1048576.0) { codec_err(err, errcap, "Truncated TIFF file"); return 0; }
  if (jpeg && ent_find(&t, &dir, 347, &e) && e.ok && type_size(e.type) == 1) { jtab = d + e.at; jtl = e.count; }

  if (!rgba_alloc(out, L.w, L.h)) { msg = "Not enough memory for the picture"; goto done; }
  if (L.alpha && L.ncolor == 4 && !(L.ab = (u8 *)calloc((size_t)L.w * L.h, 1))) { codec_oom = 1; msg = "Out of memory"; goto done; }
  if (!jpeg) {
    if (!(buf = (u8 *)malloc(chunksize + 8)) || !(tmp = (u8 *)malloc(rowbytes + 8))) { codec_oom = 1; msg = "Out of memory"; goto done; }
    if (L.comp == 5 && !(lzw = (Lzw *)malloc(sizeof(Lzw)))) { codec_oom = 1; msg = "Out of memory"; goto done; }
    if (L.comp >= 2 && L.comp <= 4 &&
        (!(ref = (i32 *)malloc(sizeof(i32) * ((size_t)cw + 8))) || !(cur = (i32 *)malloc(sizeof(i32) * ((size_t)cw + 8))))) { codec_oom = 1; msg = "Out of memory"; goto done; }
  }
  L.be = !t.le;
  for (ci = 0; ci < nchunks && !progress(ci, nchunks); ci++) {
    u32 off, cnt, plane = ci / perplane, cx = ci % perplane, rows, r, c;
    i32 x0 = (i32)((cx % across) * cw), y0 = (i32)((cx / across) * ch);
    const u8 *src;
    u8 *rev = NULL;
    size_t got;
    if (!ent_uint(&t, &eoff, ci, &off)) goto done;
    if (ecnt.count) { if (!ent_uint(&t, &ecnt, ci, &cnt)) goto done; }
    else cnt = off < n ? (u32)(n - off) : 0;
    rows = tiled ? ch : (u32)(L.h - y0) < ch ? (u32)(L.h - y0) : ch;
    if (L.planar && L.slot[plane] < 0) continue;          /* a plane nothing is taken from */
    if (off > n || cnt > n - off) {
      if (L.comp == 1 || off >= n) { msg = "Truncated TIFF file"; goto done; }
      cnt = (u32)(n - off);                              /* compressed data cut off: as far as it goes */
    }
    src = d + off;
    if (jpeg) {
      if (!jpeg_chunk(jtab, jtl, src, cnt, L.photo == 2, out, x0, y0, err, errcap)) { msg = NULL; goto done; }
      continue;
    }
    if (L.fill == 2 && cnt) {                            /* FillOrder 2: the bytes' bits reversed */
      u32 i;
      if (!(rev = (u8 *)malloc(cnt))) { codec_oom = 1; msg = "Out of memory"; goto done; }
      for (i = 0; i < cnt; i++) {
        u8 b = src[i], o = 0;
        int j;
        for (j = 0; j < 8; j++) if (b & (1 << j)) o |= (u8)(0x80 >> j);
        rev[i] = o;
      }
      if (L.comp < 2 || L.comp > 4) src = rev;
    }
    /* the chunk's bytes */
    got = rows * rowbytes;
    switch (L.comp) {
    case 1:                                              /* a byte count that is short but has the data after it is let off */
      if (got > (rev ? cnt : n - off)) { free(rev); msg = "Truncated TIFF file"; goto done; }
      memcpy(buf, src, got); break;
    case 32773: got = unpackbits(src, cnt, buf, rows * rowbytes) == rows * rowbytes; break;
    case 5: { long r5 = unlzw(lzw, src, cnt, buf, rows * rowbytes); got = r5 == (long)(rows * rowbytes); break; }
    case 8: case 32946: got = zlib_decompress(src, cnt, buf, rows * rowbytes) == (long)(rows * rowbytes); break;
    default: got = unccitt(d + off, cnt, (int)L.comp, t4, L.fill == 2, buf, (i32)cw, (i32)rows, ref, cur); break;
    }
    free(rev);
    if (!got) { msg = "Damaged TIFF image data"; goto done; }
    /* each row: the predictor, then its pixels into the slots */
    for (r = 0; r < rows && y0 + (i32)r < L.h; r++) {
      u8 *row = buf + r * rowbytes, *o;
      L.be = L.pred == 3 ? 1 : !t.le;
      if (L.pred != 1 && !unpredict(&L, row, tmp, cw, spc, t.le)) { msg = "Unsupported TIFF predictor for these samples"; goto done; }
      o = out->px + ((size_t)(y0 + (i32)r) * L.w + x0) * 4;
      for (c = 0; c < cw && x0 + (i32)c < L.w; c++, o += 4) {
        if (L.planar) {
          int s = L.slot[plane], v8 = sample8(&L, row, c, (int)plane);
          if (s == 4) L.ab[(size_t)(y0 + (i32)r) * L.w + x0 + c] = (u8)v8; else o[s] = (u8)v8;
        } else
          for (k = 0; k < L.spp; k++) {
            int s = L.slot[k];
            if (s < 0) continue;
            if (s == 4) L.ab[(size_t)(y0 + (i32)r) * L.w + x0 + c] = (u8)sample8(&L, row, c * spc + k, (int)k);
            else o[s] = (u8)sample8(&L, row, c * spc + k, (int)k);
          }
      }
    }
  }
  finish(&L, out, jpeg);
  ok = 1;
done:
  free(buf); free(tmp); free(lzw); free(ref); free(cur); free(L.ab);
  if (!ok) {
    rgba_free(out);
    if (msg) codec_err(err, errcap, msg);
  }
  return ok;
}
