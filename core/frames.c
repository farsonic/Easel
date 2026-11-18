/*
 * frames.c - animations (Easel 1.1), see frames.h: every frame of an animated GIF (GIF89a's
 * Graphic Control Extension: delay, transparency and disposal) and of an IFF ANIM (ANHD and DLTA
 * as "ANIM - IFF CEL Animations" and the op 5 byte vertical delta of Jim Kent's ANIM5 describe;
 * 1.2: ops 7 and 8, the short and long vertical deltas),
 * and both written from a document's layers. Portable C99.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "frames.h"
#include "palette.h"

long gif_lzw(const u8 *s, size_t n, int mcs, u8 *out, long npx);   /* gif.c */

static u32 le16(const u8 *p) { return p[0] | ((u32)p[1] << 8); }
static u32 rd16(const u8 *p) { return ((u32)p[0] << 8) | p[1]; }
static u32 rd32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static void w16(Buf *b, u32 v) { buf_addc(b, (int)((v >> 8) & 255)); buf_addc(b, (int)(v & 255)); }
static void w32(Buf *b, u32 v) { w16(b, v >> 16); w16(b, v & 0xFFFF); }
static void wl16(Buf *b, u32 v) { buf_addc(b, (int)(v & 255)); buf_addc(b, (int)((v >> 8) & 255)); }
static void put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

void frames_free(Frames *f)
{
  int i;
  for (i = 0; i < f->n; i++) rgba_free(&f->fr[i]);
  f->n = 0;
}
static int fail(Frames *f, char *err, size_t errcap, const char *msg)
{
  if (f->n) return 1;                                  /* (the frames read so far are kept) */
  codec_err(err, errcap, msg);
  return 0;
}
/* room for one more frame: 0 (truncated set) when there isn't */
static int frame_room(Frames *f, int max, long limit)
{
  if (f->n >= max || f->n >= FRAMES_MAX || (limit > 0 && (double)(f->n + 1) * f->w * f->h * 4 > (double)limit)) { f->truncated = 1; return 0; }
  return 1;
}

/* ================================================================== GIF */
int gif_read_frames(const u8 *d, size_t n, Frames *f, int max, long limit, char *err, size_t errcap)
{
  const u8 *gct = NULL;
  int ngct = 0, trans = -1, disposal = 0, delay = 0;
  size_t o;
  u8 *canvas = NULL, *saved = NULL;
  memset(f, 0, sizeof *f);
  if (max <= 0) max = FRAMES_MAX;
  if (n < 13 || (memcmp(d, "GIF87a", 6) && memcmp(d, "GIF89a", 6))) { codec_err(err, errcap, "Not a GIF file"); return 0; }
  f->w = (i32)le16(d + 6); f->h = (i32)le16(d + 8);
  o = 13;
  if (d[10] & 0x80) { ngct = 2 << (d[10] & 7); gct = d + 13; o += (size_t)ngct * 3; }
  if (o > n || f->w < 1 || f->h < 1 || f->w > MAXDIM || f->h > MAXDIM || (long)f->w * f->h > CODEC_MAXPIXELS) { codec_err(err, errcap, "Bad GIF picture size"); return 0; }
  if (!(canvas = (u8 *)calloc((size_t)f->w * f->h, 4))) { codec_err(err, errcap, "Out of memory"); codec_oom = 1; return 0; }
  while (o < n && d[o] != 0x3B) {
    if (d[o] == 0x21) {
      if (o + 2 > n) break;
      if (d[o + 1] == 0xF9 && o + 7 < n && d[o + 2] >= 4) {
        disposal = (d[o + 3] >> 2) & 7;
        delay = (int)le16(d + o + 4);
        trans = (d[o + 3] & 1) ? d[o + 6] : -1;
      }
      o += 2;
      while (o < n && d[o]) o += (size_t)d[o] + 1;
      o++;
      continue;
    }
    if (d[o] != 0x2C || o + 10 > n) break;
    {
      i32 left = (i32)le16(d + o + 1), top = (i32)le16(d + o + 3), iw = (i32)le16(d + o + 5), ih = (i32)le16(d + o + 7), x, y;
      int inter = d[o + 9] & 0x40, nct = ngct, mcs;
      const u8 *ct = gct;
      size_t e, k, total = 0;
      u8 *data, *idx;
      if (d[o + 9] & 0x80) { nct = 2 << (d[o + 9] & 7); ct = d + o + 10; o += (size_t)nct * 3; }
      o += 10;
      if (o >= n) break;
      mcs = d[o++];
      if (mcs < 1 || mcs > 11 || iw < 1 || ih < 1 || (long)iw * ih > CODEC_MAXPIXELS) break;
      for (e = o; e < n && d[e]; e += (size_t)d[e] + 1) total += d[e];
      if (!frame_room(f, max, limit)) break;
      data = (u8 *)malloc(total + 1); idx = (u8 *)malloc((size_t)iw * ih);
      if (!data || !idx) { free(data); free(idx); codec_oom = 1; f->truncated = 1; break; }
      for (e = o, k = 0; e < n && d[e]; e += (size_t)d[e] + 1) {
        size_t l = d[e];
        if (e + 1 + l > n) l = n - e - 1;
        memcpy(data + k, d + e + 1, l); k += l;
      }
      o = e + 1;
      memset(idx, trans >= 0 ? trans : 0, (size_t)iw * ih);
      gif_lzw(data, k, mcs, idx, (long)iw * ih);       /* (a short frame keeps what it has) */
      free(data);
      if (disposal == 3) { free(saved); if ((saved = (u8 *)malloc((size_t)f->w * f->h * 4))) memcpy(saved, canvas, (size_t)f->w * f->h * 4); }
      {
        static const u8 pstart[4] = { 0, 4, 2, 1 }, pstep[4] = { 8, 8, 4, 2 };
        int pass = 0;
        i32 ry = 0;
        for (y = 0; y < ih; y++) {
          i32 cy;
          if (inter) { while (ry >= ih && pass < 3) { pass++; ry = pstart[pass]; } cy = ry; ry += pstep[pass]; }
          else cy = y;
          if (top + cy >= f->h) continue;
          for (x = 0; x < iw && left + x < f->w; x++) {
            int v = idx[(size_t)y * iw + x];
            u8 *p = canvas + ((size_t)(top + cy) * f->w + left + x) * 4;
            if (v == trans) continue;
            if (ct && v < nct) { p[0] = ct[v * 3]; p[1] = ct[v * 3 + 1]; p[2] = ct[v * 3 + 2]; }
            else p[0] = p[1] = p[2] = (u8)v;
            p[3] = 255;
          }
        }
      }
      free(idx);
      if (!rgba_alloc(&f->fr[f->n], f->w, f->h)) { codec_oom = 1; f->truncated = 1; break; }
      memcpy(f->fr[f->n].px, canvas, (size_t)f->w * f->h * 4);
      f->delay[f->n++] = delay;
      if (disposal == 2) {
        for (y = top; y < top + ih && y < f->h; y++)
          if (left < f->w) memset(canvas + ((size_t)y * f->w + left) * 4, 0, (size_t)((left + iw > f->w ? f->w : left + iw) - left) * 4);
      } else if (disposal == 3 && saved) memcpy(canvas, saved, (size_t)f->w * f->h * 4);
      trans = -1; disposal = 0; delay = 0;
      if (progress(f->n, f->n + 1)) break;
    }
  }
  free(canvas); free(saved);
  return fail(f, err, errcap, "The GIF file has no picture");
}

/* ================================================================== IFF ANIM */
typedef struct {
  i32 w, h, rb;                       /* size and bytes per plane row */
  int np, masking, transparent;
  u8 xasp, yasp;
  u8 cmap[768]; int ncmap;
  u32 camg; int havecamg;
  u8 *buf[2];                         /* np planes each, plane after plane (h * rb each) */
} AnimState;

/* ByteRun1 into out[0..size); 0 if the data runs out first */
static int unpack(const u8 *s, size_t n, size_t *pos, u8 *out, size_t size)
{
  size_t o = 0, i = *pos;
  while (o < size) {
    int c;
    if (i >= n) return 0;
    c = (signed char)s[i++];
    if (c >= 0) { size_t k = (size_t)c + 1; if (i + k > n || o + k > size) return 0; memcpy(out + o, s + i, k); i += k; o += k; }
    else if (c != -128) { size_t k = (size_t)(1 - c); if (i >= n || o + k > size) return 0; memset(out + o, s[i++], k); o += k; }
  }
  *pos = i;
  return 1;
}
/* a BODY (interleaved rows of each plane, and the mask plane if there is one) into the planes */
static int body_to_planes(AnimState *a, const u8 *c, size_t len, int comp, u8 *planes)
{
  i32 y;
  int p, nrow = a->np + (a->masking == 1);
  size_t pos = 0;
  u8 *row = (u8 *)malloc((size_t)a->rb);
  if (!row) return 0;
  for (y = 0; y < a->h; y++)
    for (p = 0; p < nrow; p++) {
      u8 *dst = p < a->np ? planes + ((size_t)p * a->h + y) * a->rb : row;
      if (comp == 1) { if (!unpack(c, len, &pos, dst, (size_t)a->rb)) { free(row); return 0; } }
      else { if (pos + (size_t)a->rb > len) { free(row); return 0; } memcpy(dst, c + pos, (size_t)a->rb); pos += (size_t)a->rb; }
    }
  free(row);
  return 1;
}
/* op 5: for each plane with data, each column a count of ops: 0 n v (n times v), 0x80 | n (skip n
   rows), else n bytes copied down the column */
static void apply_op5(AnimState *a, const u8 *c, size_t len, u8 *planes)
{
  int p;
  if (len < 64) return;
  for (p = 0; p < a->np && p < 8; p++) {
    u32 off = rd32(c + p * 4);
    i32 col;
    size_t i = off;
    if (!off || off >= len) continue;
    for (col = 0; col < a->rb; col++) {
      int nops, k;
      i32 y = 0;
      u8 *base = planes + (size_t)p * a->h * a->rb + col;
      if (i >= len) return;
      nops = c[i++];
      for (k = 0; k < nops; k++) {
        int op;
        if (i >= len) return;
        op = c[i++];
        if (op == 0) {
          int cnt, v, m;
          if (i + 2 > len) return;
          cnt = c[i]; v = c[i + 1]; i += 2;
          for (m = 0; m < cnt && y < a->h; m++, y++) base[(size_t)y * a->rb] = (u8)v;
        } else if (op & 0x80) y += op & 0x7F;
        else {
          int m;
          if (i + (size_t)op > len) return;
          for (m = 0; m < op; m++, y++) if (y < a->h) base[(size_t)y * a->rb] = c[i + m];
          i += (size_t)op;
        }
      }
    }
  }
}
/* 1.2: ops 7 and 8 (the "short/long vertical delta" of the ANIM7/ANIM8 players): the column is a
   word (bits & 1: a long) wide. Op 7: eight pointers to opcode lists and eight to data lists; per
   column a count of ops from the opcodes, each 0 (a count, then one data word repeated), 0x80 | n
   (skip n rows) or n (n data words copied down). Op 8: eight pointers, opcodes, counts and data all
   words (or longs) in one list, the skip bit the word's top bit */
static u32 rdw(const u8 *p, int ws) { return ws == 4 ? rd32(p) : rd16(p); }
static void put_word(u8 *dst, u32 v, int ws, int bytes)
{
  int k;
  for (k = 0; k < bytes; k++) dst[k] = (u8)(v >> (8 * (ws - 1 - k)));
}
static void apply_op78(AnimState *a, const u8 *c, size_t len, u8 *planes, int op8, int longw)
{
  int p, ws = longw ? 4 : 2;
  i32 ncol = (a->rb + ws - 1) / ws;
  if (len < (op8 ? 32u : 64u)) return;
  for (p = 0; p < a->np && p < 8; p++) {
    u32 oo = rd32(c + p * 4), od = op8 ? 0 : rd32(c + 32 + p * 4);
    size_t i = oo, di = od;
    i32 col;
    if (!oo || oo >= len || (!op8 && (!od || od >= len))) continue;
    for (col = 0; col < ncol; col++) {
      u8 *base = planes + (size_t)p * a->h * a->rb + (size_t)col * ws;
      int bytes = (col + 1) * ws <= a->rb ? ws : a->rb - col * ws;   /* (a last column narrower than a long) */
      u32 nops, k;
      i32 y = 0;
      if (op8) { if (i + (size_t)ws > len) return; nops = rdw(c + i, ws); i += (size_t)ws; }
      else { if (i >= len) return; nops = c[i++]; }
      if (nops > (u32)a->h * 2 + 2) return;
      for (k = 0; k < nops; k++) {
        u32 o, top = op8 ? (ws == 4 ? 0x80000000UL : 0x8000UL) : 0x80;
        if (op8) { if (i + (size_t)ws > len) return; o = rdw(c + i, ws); i += (size_t)ws; }
        else { if (i >= len) return; o = c[i++]; }
        if (o == 0) {
          u32 cnt, v, m;
          if (op8) { if (i + 2 * (size_t)ws > len) return; cnt = rdw(c + i, ws); v = rdw(c + i + ws, ws); i += 2 * (size_t)ws; }
          else { if (i >= len || di + (size_t)ws > len) return; cnt = c[i++]; v = rdw(c + di, ws); di += (size_t)ws; }
          for (m = 0; m < cnt && y < a->h; m++, y++) put_word(base + (size_t)y * a->rb, v, ws, bytes);
        } else if (o & top) y += (i32)(o & (top - 1));
        else {
          u32 m;
          size_t *src = op8 ? &i : &di;
          if (o > (u32)a->h || *src + (size_t)o * ws > len) return;
          for (m = 0; m < o; m++, y++) if (y >= 0 && y < a->h) put_word(base + (size_t)y * a->rb, rdw(c + *src + m * ws, ws), ws, bytes);
          *src += (size_t)o * ws;
        }
      }
    }
  }
}
/* the planes as an uncompressed ILBM in memory, read by ilbm_read (so EHB and HAM come out right) */
static int planes_to_rgba(AnimState *a, const u8 *planes, Rgba *out)
{
  Buf b;
  i32 y;
  int p, ok;
  buf_init(&b);
  buf_add(&b, "FORM\0\0\0\0ILBMBMHD", 16); w32(&b, 20);
  w16(&b, (u32)a->w); w16(&b, (u32)a->h); w16(&b, 0); w16(&b, 0);
  buf_addc(&b, a->np); buf_addc(&b, a->masking == 2 ? 2 : 0); buf_addc(&b, 0); buf_addc(&b, 0);
  w16(&b, (u32)a->transparent); buf_addc(&b, a->xasp); buf_addc(&b, a->yasp); w16(&b, (u32)a->w); w16(&b, (u32)a->h);
  if (a->ncmap) { buf_add(&b, "CMAP", 4); w32(&b, (u32)a->ncmap * 3); buf_add(&b, a->cmap, (size_t)a->ncmap * 3); if (a->ncmap & 1) buf_addc(&b, 0); }
  if (a->havecamg) { buf_add(&b, "CAMG", 4); w32(&b, 4); w32(&b, a->camg); }
  buf_add(&b, "BODY", 4); w32(&b, (u32)((size_t)a->rb * a->np * a->h));
  for (y = 0; y < a->h; y++)
    for (p = 0; p < a->np; p++) buf_add(&b, planes + ((size_t)p * a->h + y) * a->rb, (size_t)a->rb);
  if (b.oom) { buf_free(&b); return 0; }
  put32((u8 *)b.data + 4, (u32)(b.len - 8));
  ok = ilbm_read((const u8 *)b.data, b.len, out, NULL, 0);
  buf_free(&b);
  return ok;
}
static int same_px(const Rgba *a, const Rgba *b) { return a->w == b->w && a->h == b->h && !memcmp(a->px, b->px, (size_t)a->w * a->h * 4); }

int anim_read_frames(const u8 *d, size_t n, Frames *f, int max, long limit, char *err, size_t errcap)
{
  AnimState a;
  size_t o, end;
  int frame = 0, interleave = 2;
  size_t psize = 0;
  memset(f, 0, sizeof *f);
  memset(&a, 0, sizeof a);
  if (max <= 0) max = FRAMES_MAX;
  if (n < 12 || memcmp(d, "FORM", 4) || memcmp(d + 8, "ANIM", 4)) { codec_err(err, errcap, "Not an IFF ANIM file"); return 0; }
  end = 8 + (size_t)rd32(d + 4);
  if (end > n || end < 12) end = n;
  for (o = 12; o + 12 <= end; ) {
    size_t flen = rd32(d + o + 4), fe = o + 8 + flen, c;
    int op = 0, have_body = 0;
    const u8 *dlta = NULL, *body = NULL;
    size_t dlen = 0, blen = 0;
    u32 reltime = 0, anhd_bits = 0;
    if (fe > end || flen < 4) break;
    if (memcmp(d + o, "FORM", 4) || memcmp(d + o + 8, "ILBM", 4)) { o = fe + (fe & 1); continue; }
    for (c = o + 12; c + 8 <= fe; ) {
      const u8 *id = d + c, *p = d + c + 8;
      size_t len = rd32(d + c + 4);
      if (c + 8 + len > fe) break;
      if (!memcmp(id, "BMHD", 4) && len >= 20 && !frame) {
        a.w = (i32)rd16(p); a.h = (i32)rd16(p + 2); a.np = p[8]; a.masking = p[9];
        a.transparent = (int)rd16(p + 12); a.xasp = p[14]; a.yasp = p[15];
        op = p[10];                                       /* (the first frame's compression, for its BODY) */
      } else if (!memcmp(id, "CMAP", 4)) {
        a.ncmap = (int)(len / 3 > 256 ? 256 : len / 3);
        memcpy(a.cmap, p, (size_t)a.ncmap * 3);
      } else if (!memcmp(id, "CAMG", 4) && len >= 4) { a.camg = rd32(p); a.havecamg = 1; }
      else if (!memcmp(id, "ANHD", 4) && len >= 24) {
        if (frame) op = p[0];
        reltime = rd32(p + 14);
        interleave = p[18] ? p[18] : 2;
        anhd_bits = rd32(p + 20);                         /* (1.2: ops 7 and 8: bit 0 long data) */
      } else if (!memcmp(id, "DLTA", 4)) { dlta = p; dlen = len; }
      else if (!memcmp(id, "BODY", 4)) { body = p; blen = len; have_body = 1; }
      c += 8 + len + (len & 1);
    }
    if (!frame) {
      if (a.w < 1 || a.h < 1 || a.w > MAXDIM || a.h > MAXDIM || a.np < 1 || a.np > 8 || (long)a.w * a.h > CODEC_MAXPIXELS || !have_body) {
        codec_err(err, errcap, "The ANIM file's first frame isn't a picture Easel can animate (1-8 planes)");
        return 0;
      }
      f->w = a.w; f->h = a.h;
      a.rb = ((a.w + 15) >> 4) * 2;
      psize = (size_t)a.rb * a.h * a.np;
      if (!(a.buf[0] = (u8 *)calloc(psize, 1)) || !(a.buf[1] = (u8 *)calloc(psize, 1))) { free(a.buf[0]); codec_oom = 1; codec_err(err, errcap, "Out of memory"); return 0; }
      if (!body_to_planes(&a, body, blen, op, a.buf[0])) { free(a.buf[0]); free(a.buf[1]); codec_err(err, errcap, "Damaged ANIM file (the first frame)"); return 0; }
      memcpy(a.buf[1], a.buf[0], psize);
    } else {
      u8 *t = interleave == 1 ? a.buf[0] : a.buf[frame & 1];   /* (interleave 1: each delta on the frame before) */
      if (op == 0 && have_body) { if (!body_to_planes(&a, body, blen, 1, t)) body_to_planes(&a, body, blen, 0, t); }
      else if (op == 5 && dlta) apply_op5(&a, dlta, dlen, t);
      else if ((op == 7 || op == 8) && dlta) apply_op78(&a, dlta, dlen, t, op == 8, (int)(anhd_bits & 1));   /* 1.2 */
      else if (op != 5 && op != 0 && op != 7 && op != 8) { f->truncated = 1; break; }   /* (ops 1-4, 6 and J aren't read) */
    }
    if (!frame_room(f, max, limit)) break;
    if (!planes_to_rgba(&a, interleave == 1 ? a.buf[0] : a.buf[frame & 1], &f->fr[f->n])) { if (frame) { f->truncated = 1; break; } free(a.buf[0]); free(a.buf[1]); codec_err(err, errcap, "Damaged ANIM file"); return 0; }
    if (f->fr[f->n].w != f->w || f->fr[f->n].h != f->h) { rgba_free(&f->fr[f->n]); break; }
    f->delay[f->n++] = (int)(reltime * 100 / 60);
    frame++;
    o = fe + (fe & 1);
    if (progress((long)o, (long)end)) break;
  }
  free(a.buf[0]); free(a.buf[1]);
  /* DPaint's two looping frames at the end */
  if (f->n >= 4 && same_px(&f->fr[f->n - 2], &f->fr[0]) && same_px(&f->fr[f->n - 1], &f->fr[1])) {
    rgba_free(&f->fr[f->n - 1]); rgba_free(&f->fr[f->n - 2]); f->n -= 2;
  }
  if (f->n > 1 && !f->delay[0]) f->delay[0] = f->delay[1];
  return fail(f, err, errcap, "The ANIM file has no frames");
}

int frames_read(const u8 *d, size_t n, Frames *f, int max, long limit, char *err, size_t errcap)
{
  if (n >= 6 && (!memcmp(d, "GIF87a", 6) || !memcmp(d, "GIF89a", 6))) return gif_read_frames(d, n, f, max, limit, err, errcap);
  if (n >= 12 && !memcmp(d, "FORM", 4) && !memcmp(d + 8, "ANIM", 4)) return anim_read_frames(d, n, f, max, limit, err, errcap);
  memset(f, 0, sizeof *f);
  codec_err(err, errcap, "Only animated GIFs and IFF ANIMs have frames to import");
  return 0;
}

/* ================================================================== to and from documents */
int frame_delay_of_name(const char *name, int def)
{
  const char *p = strrchr(name, '(');
  long whole = 0, frac = 0;
  int nd = 0, any = 0;
  if (!p) return def;
  for (p++; *p == ' '; p++) ;
  while (*p >= '0' && *p <= '9') { whole = whole * 10 + (*p++ - '0'); any = 1; if (whole > 100000) return def; }
  if (*p == '.' || *p == ',') for (p++; *p >= '0' && *p <= '9'; p++) { if (nd < 2) { frac = frac * 10 + (*p - '0'); nd++; } any = 1; }
  if (nd == 1) frac *= 10;
  while (*p == ' ') p++;
  if (!any || *p != 's') return def;
  return (int)(whole * 100 + frac);
}
Doc *frames_to_doc(const Frames *f)
{
  Doc *d;
  int i;
  if (f->n < 1 || !(d = doc_new(f->w, f->h, 0, 1))) return NULL;
  for (i = 0; i < f->n; i++) {
    Layer *l;
    char name[64];
    snprintf(name, sizeof name, "Frame %d (%d.%02d s)", i + 1, f->delay[i] / 100, f->delay[i] % 100);
    if (!i) l = d->lay[0];
    else if (!(l = layer_new(d, name))) { doc_free(d); return NULL; }
    strncpy(l->name, name, sizeof l->name - 1); l->name[sizeof l->name - 1] = 0;
    memcpy(l->px, f->fr[i].px, (size_t)f->w * f->h * 4);
    if (i && !doc_add_layer(d, l, d->nlay)) { layer_free(l); doc_free(d); return NULL; }
  }
  d->active = d->nlay - 1;
  doc_forget_undo(d);
  d->dirty = 0;
  return d;
}

void anim_opts_default(AnimOpts *o)
{
  memset(o, 0, sizeof *o);
  o->maxc = 256; o->method = QM_WU; o->dither = DI_FS; o->def_delay = 10;
}
/* the frames of a document as indices into one palette: frm[i] (w * h each) for the layers in
   order; trans the transparent entry (-1: none, when nothing is under half alpha) */
typedef struct { int n; u8 *idx[FRAMES_MAX]; u8 pal[768]; int npal, trans; int delay[FRAMES_MAX]; } Indexed;
static void ix_free(Indexed *x) { int i; for (i = 0; i < x->n; i++) free(x->idx[i]); x->n = 0; }
static int doc_indexed_frames(Doc *d, const AnimOpts *o, int want_trans, Indexed *x, char *err, size_t errcap)
{
  int i, k, anyt = 0, maxc = o->maxc >= 2 && o->maxc <= 256 ? o->maxc : 256;
  long npx = (long)d->w * d->h, j;
  Layer *L[FRAMES_MAX];
  int nl = 0;
  memset(x, 0, sizeof *x);
  x->trans = -1;
  for (i = 0; i < d->nlay; i++) if (d->lay[i]->kind == LK_PIXEL && d->lay[i]->px && nl < FRAMES_MAX) L[nl++] = d->lay[i];
  if (!nl) { codec_err(err, errcap, "The picture has no pixel layers to make frames of"); return 0; }
  if (want_trans) for (i = 0; i < nl && !anyt; i++) for (j = 0; j < npx; j++) if (L[i]->px[j * 4 + 3] < 128) { anyt = 1; break; }
  if (d->mode == MODE_INDEXED || d->mode == MODE_BITMAP) {
    x->npal = d->npal; memcpy(x->pal, d->pal, (size_t)d->npal * 3);
    if (anyt && x->npal == 256) anyt = 0;                       /* (no room for a transparent entry) */
  } else {
    /* the palette from a sample of every frame: up to 256K pixels in all */
    long total = npx * nl, step = total / 262144 + 1, ns = 0, at;
    u8 *s = (u8 *)malloc((size_t)(total / step + 2) * 4), *sidx;
    if (!s) { codec_err(err, errcap, "Out of memory"); return 0; }
    for (at = 0; at < total; at += step) {
      const u8 *p = L[at / npx]->px + (at % npx) * 4;
      if (p[3] < 128) continue;
      memcpy(s + ns * 4, p, 3); s[ns * 4 + 3] = 255; ns++;
    }
    if (!ns) { memset(s, 0, 3); s[3] = 255; ns = 1; }
    if (!(sidx = (u8 *)malloc((size_t)ns))) { free(s); codec_err(err, errcap, "Out of memory"); return 0; }
    x->npal = quantize_rgba(s, (i32)ns, 1, anyt ? maxc - 1 : maxc, o->method, DI_NONE, x->pal, 0, sidx);
    free(s); free(sidx);
    if (!x->npal) { codec_err(err, errcap, "Out of memory"); return 0; }
  }
  if (anyt) { x->trans = x->npal; memset(x->pal + x->npal * 3, 0, 3); x->npal++; }
  for (i = 0; i < nl; i++) {
    u8 *flat = (u8 *)malloc((size_t)npx * 4);
    if (!(x->idx[i] = (u8 *)malloc((size_t)npx)) || !flat) { free(flat); x->n = i + 1; ix_free(x); codec_err(err, errcap, "Out of memory"); return 0; }
    x->n = i + 1;
    /* each frame as the animation shows it: laid over black (ANIM) or with its transparency (GIF) */
    for (j = 0; j < npx; j++) {
      const u8 *p = L[i]->px + j * 4;
      for (k = 0; k < 3; k++) flat[j * 4 + k] = (u8)(p[k] * p[3] / 255);
      if (anyt && p[3] < 128) memcpy(flat + j * 4, x->pal, 3);   /* (an exact colour: the dither spreads no error from it) */
      flat[j * 4 + 3] = 255;
    }
    if (!map_rgba(flat, d->w, d->h, x->pal, anyt ? x->npal - 1 : x->npal, d->mode == MODE_INDEXED ? DI_NONE : o->dither, x->idx[i])) {
      free(flat); ix_free(x); codec_err(err, errcap, "Out of memory"); return 0;
    }
    free(flat);
    if (anyt) for (j = 0; j < npx; j++) if (L[i]->px[j * 4 + 3] < 128) x->idx[i][j] = (u8)x->trans;
    x->delay[i] = frame_delay_of_name(L[i]->name, o->def_delay > 0 ? o->def_delay : 10);
    if (progress(i + 1, nl)) { ix_free(x); return 0; }
  }
  return 1;
}

/* ------------------------------------------------------------------ animated GIF */
u8 *gif_write_anim(Doc *d, const AnimOpts *o, size_t *len, char *err, size_t errcap)
{
  Indexed x;
  Buf b;
  int i, k = 1, full;
  u8 *sub = NULL;
  if (d->w > 65535 || d->h > 65535) { codec_err(err, errcap, "Too big for a GIF"); return NULL; }
  if (!doc_indexed_frames(d, o, 1, &x, err, errcap)) return NULL;
  while ((1 << k) < x.npal) k++;
  full = x.trans >= 0;                                 /* (transparent frames: whole, cleared between) */
  buf_init(&b);
  buf_add(&b, "GIF89a", 6); wl16(&b, (u32)d->w); wl16(&b, (u32)d->h);
  buf_addc(&b, 0x80 | ((k - 1) << 4) | (k - 1)); buf_addc(&b, x.trans >= 0 ? x.trans : 0); buf_addc(&b, 0);
  for (i = 0; i < (1 << k); i++) { if (i < x.npal) buf_add(&b, x.pal + i * 3, 3); else { buf_addc(&b, 0); buf_addc(&b, 0); buf_addc(&b, 0); } }
  buf_addc(&b, 0x21); buf_addc(&b, 0xFF); buf_addc(&b, 11); buf_add(&b, "NETSCAPE2.0", 11);
  buf_addc(&b, 3); buf_addc(&b, 1); wl16(&b, (u32)(o->loops > 0 ? o->loops : 0)); buf_addc(&b, 0);
  if (!(sub = (u8 *)malloc((size_t)d->w * d->h))) { buf_free(&b); ix_free(&x); codec_err(err, errcap, "Out of memory"); return NULL; }
  for (i = 0; i < x.n; i++) {
    i32 x0 = 0, y0 = 0, x1 = d->w, y1 = d->h, xx, yy;
    u8 *g;
    size_t glen, skip;
    if (!full && i) {
      /* only the box that changed since the frame before */
      x0 = d->w; y0 = d->h; x1 = y1 = 0;
      for (yy = 0; yy < d->h; yy++)
        for (xx = 0; xx < d->w; xx++)
          if (x.idx[i][(size_t)yy * d->w + xx] != x.idx[i - 1][(size_t)yy * d->w + xx]) {
            if (xx < x0) x0 = xx;
            if (xx >= x1) x1 = xx + 1;
            if (yy < y0) y0 = yy;
            if (yy >= y1) y1 = yy + 1;
          }
      if (x1 <= x0) { x0 = y0 = 0; x1 = y1 = 1; }
    }
    for (yy = y0; yy < y1; yy++) memcpy(sub + (size_t)(yy - y0) * (x1 - x0), x.idx[i] + (size_t)yy * d->w + x0, (size_t)(x1 - x0));
    buf_addc(&b, 0x21); buf_addc(&b, 0xF9); buf_addc(&b, 4);
    buf_addc(&b, ((full ? 2 : 1) << 2) | (x.trans >= 0)); wl16(&b, (u32)x.delay[i]); buf_addc(&b, x.trans >= 0 ? x.trans : 0); buf_addc(&b, 0);
    if (!(g = gif_encode(sub, x1 - x0, y1 - y0, x.pal, x.npal, -1, 0, &glen))) { b.oom = 1; break; }
    /* gif_encode's file without its header, screen, table and trailer: the image descriptor and data */
    skip = 13 + (size_t)3 * (1 << k);
    if (glen < skip + 11) { free(g); b.oom = 1; break; }
    g[skip + 1] = (u8)(x0 & 255); g[skip + 2] = (u8)(x0 >> 8); g[skip + 3] = (u8)(y0 & 255); g[skip + 4] = (u8)(y0 >> 8);
    buf_add(&b, g + skip, glen - skip - 1);
    free(g);
    if (progress(i + 1, x.n)) break;
  }
  buf_addc(&b, 0x3B);
  free(sub);
  ix_free(&x);
  if (b.oom || progress_aborted) { buf_free(&b); codec_err(err, errcap, "Out of memory"); return NULL; }
  *len = b.len;
  return (u8 *)buf_take(&b);
}

/* ------------------------------------------------------------------ IFF ANIM */
static void to_planes(const u8 *idx, i32 w, i32 h, int np, i32 rb, u8 *planes)
{
  i32 x, y;
  int p;
  memset(planes, 0, (size_t)rb * h * np);
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      int v = idx[(size_t)y * w + x];
      for (p = 0; p < np; p++) if (v & (1 << p)) planes[((size_t)p * h + y) * rb + (x >> 3)] |= (u8)(0x80 >> (x & 7));
    }
}
/* one column's op 5 ops (base to now, stride rb, h rows) appended to b; how many ops */
static int col_ops(const u8 *base, const u8 *now, i32 h, i32 rb, Buf *b)
{
  i32 y = 0;
  int nops = 0;
  size_t at = b->len;
  buf_addc(b, 0);
#define B(y) base[(size_t)(y) * rb]
#define N(y) now[(size_t)(y) * rb]
  while (y < h) {
    i32 s = 0, r;
    while (y + s < h && B(y + s) == N(y + s)) s++;
    if (y + s >= h) break;                                         /* (nothing changes below) */
    while (s > 0) { int k = s > 127 ? 127 : (int)s; buf_addc(b, 0x80 | k); nops++; s -= k; y += k; }
    r = 1;
    while (y + r < h && r < 255 && N(y + r) == N(y)) r++;
    if (r >= 3) { buf_addc(b, 0); buf_addc(b, (int)r); buf_addc(b, N(y)); nops++; y += r; continue; }
    /* bytes copied: up to two unchanged in a row, or the start of a run of three */
    r = 0;
    while (y + r < h && r < 127) {
      if (y + r + 1 < h && B(y + r) == N(y + r) && B(y + r + 1) == N(y + r + 1)) break;
      if (y + r + 2 < h && N(y + r) == N(y + r + 1) && N(y + r) == N(y + r + 2) && r) break;
      r++;
    }
    if (!r) r = 1;
    buf_addc(b, (int)r);
    for (s = 0; s < r; s++) buf_addc(b, N(y + s));
    nops++; y += r;
  }
  if (nops > 255) {
    /* too many: the whole column copied instead */
    b->len = at + 1; nops = 0;
    for (y = 0; y < h; ) { i32 r = h - y > 127 ? 127 : h - y, s; buf_addc(b, (int)r); for (s = 0; s < r; s++) buf_addc(b, N(y + s)); y += r; nops++; }
  }
#undef B
#undef N
  if (!b->oom) b->data[at] = (char)nops;
  return nops;
}
u8 *anim_write(Doc *d, const AnimOpts *o, size_t *len, char *err, size_t errcap)
{
  Indexed x;
  Buf b;
  int np = 1, i, nseq, j;
  i32 rb = ((d->w + 15) >> 4) * 2;
  size_t psize;
  u8 *pl[FRAMES_MAX], *buf[2] = { NULL, NULL };
  if (!doc_indexed_frames(d, o, 0, &x, err, errcap)) return NULL;
  while ((1 << np) < x.npal) np++;
  psize = (size_t)rb * d->h * np;
  memset(pl, 0, sizeof pl);
  for (i = 0; i < x.n; i++) if (!(pl[i] = (u8 *)malloc(psize))) break;
  buf[0] = (u8 *)malloc(psize); buf[1] = (u8 *)malloc(psize);
  if (i < x.n || !buf[0] || !buf[1]) {
    for (i = 0; i < x.n; i++) free(pl[i]);
    free(buf[0]); free(buf[1]); ix_free(&x); codec_err(err, errcap, "Out of memory"); return NULL;
  }
  for (i = 0; i < x.n; i++) to_planes(x.idx[i], d->w, d->h, np, rb, pl[i]);
  buf_init(&b);
  buf_add(&b, "FORM\0\0\0\0ANIM", 12);
  {
    /* the first frame: a whole ILBM */
    IlbmOpts io;
    Rgba im;
    size_t flen = 0;
    u8 *f0;
    long k, npx = (long)d->w * d->h;
    ilbm_opts_default(&io);
    io.kind = IK_NORMAL; io.planes = np; io.compress = 1;
    io.modeid = d->modeid; io.aspect_x = d->aspect_x; io.aspect_y = d->aspect_y;
    if (!rgba_alloc(&im, d->w, d->h)) f0 = NULL;
    else {
      for (k = 0; k < npx; k++) { const u8 *c = x.pal + x.idx[0][k] * 3; im.px[k * 4] = c[0]; im.px[k * 4 + 1] = c[1]; im.px[k * 4 + 2] = c[2]; im.px[k * 4 + 3] = 255; }
      f0 = ilbm_write(&im, x.pal, x.npal, x.idx[0], d->cycle, d->ncycle, &io, &flen, err, errcap);
      rgba_free(&im);
    }
    if (!f0 || flen < 12) b.oom = 1;
    else {
      /* with an ANHD (op 0) for its time, after the FORM ILBM header */
      size_t at = b.len;
      int q;
      buf_add(&b, f0, 12);
      buf_add(&b, "ANHD", 4); w32(&b, 40);
      buf_addc(&b, 0); buf_addc(&b, 0); w16(&b, (u32)d->w); w16(&b, (u32)d->h); w16(&b, 0); w16(&b, 0);
      w32(&b, 0); w32(&b, (u32)(x.delay[0] * 60 / 100)); buf_addc(&b, 0); buf_addc(&b, 0); w32(&b, 0);
      for (q = 0; q < 16; q++) buf_addc(&b, 0);
      buf_add(&b, f0 + 12, flen - 12);
      if (flen & 1) buf_addc(&b, 0);
      if (!b.oom) put32((u8 *)b.data + at + 4, (u32)(flen - 8 + 48));
      free(f0);
    }
  }
  memcpy(buf[0], pl[0], psize); memcpy(buf[1], pl[0], psize);
  nseq = x.n > 1 ? x.n + 1 : 0;                        /* frames 2..n, then two back to frames 1 and 2 */
  for (j = 1; j <= nseq && !b.oom; j++) {
    int target = j < x.n ? j : j - x.n, p;
    u8 *t = buf[j & 1], *now = pl[target];
    size_t form = b.len, dl;
    u32 ptrs[16];
    Buf dd;
    buf_add(&b, "FORM\0\0\0\0ILBMANHD", 16); w32(&b, 40);
    buf_addc(&b, 5); buf_addc(&b, 0); w16(&b, (u32)d->w); w16(&b, (u32)d->h); w16(&b, 0); w16(&b, 0);
    w32(&b, 0); w32(&b, (u32)(x.delay[target] * 60 / 100)); buf_addc(&b, 0); buf_addc(&b, 0); w32(&b, 0);
    for (p = 0; p < 16; p++) buf_addc(&b, 0);
    buf_init(&dd);
    memset(ptrs, 0, sizeof ptrs);
    for (p = 0; p < 64; p++) buf_addc(&dd, 0);
    for (p = 0; p < np; p++) {
      const u8 *bp = t + (size_t)p * d->h * rb, *np_ = now + (size_t)p * d->h * rb;
      i32 c;
      if (!memcmp(bp, np_, (size_t)d->h * rb)) continue;
      ptrs[p] = (u32)dd.len;
      for (c = 0; c < rb; c++) col_ops(bp + c, np_ + c, d->h, rb, &dd);
    }
    if (dd.oom) { b.oom = 1; buf_free(&dd); break; }
    for (p = 0; p < 16; p++) put32((u8 *)dd.data + p * 4, ptrs[p]);
    buf_add(&b, "DLTA", 4); w32(&b, (u32)dd.len); buf_add(&b, dd.data, dd.len); if (dd.len & 1) buf_addc(&b, 0);
    dl = b.len - form - 8;
    if (!b.oom) put32((u8 *)b.data + form + 4, (u32)dl);
    buf_free(&dd);
    memcpy(t, now, psize);
    if (progress(j, nseq)) break;
  }
  for (i = 0; i < x.n; i++) free(pl[i]);
  free(buf[0]); free(buf[1]);
  ix_free(&x);
  if (b.oom || progress_aborted) { buf_free(&b); if (err && !err[0]) codec_err(err, errcap, "Out of memory"); return NULL; }
  put32((u8 *)b.data + 4, (u32)(b.len - 8));
  *len = b.len;
  return (u8 *)buf_take(&b);
}
