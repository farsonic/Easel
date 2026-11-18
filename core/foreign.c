/*
 * foreign.c - Easel round 3: detection and dispatch for other programs' layered documents
 * (foreign.h), and the helpers their readers share: gzip, LZF, sample conversions.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "foreign.h"
#include "zip.h"

int foreign_detect(const u8 *d, size_t n)
{
  if (n >= 14 && !memcmp(d, "gimp xcf ", 9)) return FF_XCF;
  if (n >= 18 && d[0] == 0x1F && d[1] == 0x8B && d[2] == 8) return FF_XCF;     /* .xcf.gz (checked once inflated) */
  if (n >= 8 && !memcmp(d, "PDN3", 4)) return FF_PDN;
  if (n >= 4 && d[0] == 'P' && d[1] == 'K' && d[2] == 3 && d[3] == 4) {
    ZipReader z;
    int k = 0, i;
    if (n >= 58 && !memcmp(d + 30, "mimetype", 8) && !memcmp(d + 38, "application/x-krita", 19)) return FF_KRA;
    if (!zip_open(&z, d, n)) return FF_NONE;
    if ((i = zip_find(&z, "maindoc.xml")) >= 0 && zip_find(&z, "stack.xml") < 0) k = FF_KRA;
    zip_close(&z);
    return k;
  }
  return FF_NONE;
}
const char *foreign_name(int kind)
{
  return kind == FF_XCF ? "GIMP XCF" : kind == FF_KRA ? "Krita" : kind == FF_PDN ? "Paint.NET" : "unknown";
}
Doc *foreign_load_doc(const u8 *d, size_t n, char *err, size_t errcap)
{
  Doc *doc = NULL;
  codec_oom = 0;
  switch (foreign_detect(d, n)) {
  case FF_XCF:
    if (d[0] == 0x1F) {
      size_t len;
      u8 *x = gunzip(d, n, (size_t)512 * 1024 * 1024, &len);
      if (!x) { codec_err(err, errcap, codec_oom ? "Not enough memory to unpack the file" : "Not a picture Easel knows (a damaged or unknown gzip file)"); return NULL; }
      if (len >= 14 && !memcmp(x, "gimp xcf ", 9)) doc = xcf_read_doc(x, len, err, errcap);
      else codec_err(err, errcap, "Not a picture Easel knows (a gzip file without a GIMP picture)");
      free(x);
    } else doc = xcf_read_doc(d, n, err, errcap);
    break;
  case FF_KRA: doc = kra_read_doc(d, n, err, errcap); break;
  case FF_PDN: doc = pdn_read_doc(d, n, err, errcap); break;
  default: codec_err(err, errcap, "Not a layered document Easel knows"); return NULL;
  }
  if (doc) foreign_finish(doc);
  return doc;
}

u8 *gunzip(const u8 *d, size_t n, size_t max, size_t *len)
{
  size_t pos = 10;
  u32 isize;
  int flg;
  u8 *out;
  long got;
  if (n < 18 || d[0] != 0x1F || d[1] != 0x8B || d[2] != 8) return NULL;
  flg = d[3];
  if (flg & 4) { if (n - pos < 2) return NULL; pos += 2 + (d[pos] | (d[pos + 1] << 8)); }
  if (flg & 8) { while (pos < n && d[pos]) pos++; pos++; }
  if (flg & 16) { while (pos < n && d[pos]) pos++; pos++; }
  if (flg & 2) pos += 2;
  if (pos + 8 > n) return NULL;
  isize = (u32)d[n - 4] | ((u32)d[n - 3] << 8) | ((u32)d[n - 2] << 16) | ((u32)d[n - 1] << 24);
  if (isize > max) return NULL;
  if (!(out = (u8 *)malloc(isize ? isize : 1))) { codec_oom = 1; return NULL; }
  got = zinflate(d + pos, n - 8 - pos, out, isize);
  if (got != (long)isize) { free(out); return NULL; }
  {
    u32 crc = (u32)d[n - 8] | ((u32)d[n - 7] << 8) | ((u32)d[n - 6] << 16) | ((u32)d[n - 5] << 24);
    if (crc32_update(0, out, isize) != crc) { free(out); return NULL; }
  }
  *len = isize;
  return out;
}

int lzf_decompress(const u8 *in, size_t n, u8 *out, size_t outcap)
{
  size_t ip = 0, op = 0;
  while (ip < n) {
    unsigned c = in[ip++];
    if (c < 32) {
      size_t l = c + 1;
      if (l > n - ip || l > outcap - op) return 0;
      memcpy(out + op, in + ip, l);
      ip += l; op += l;
    } else {
      size_t l = c >> 5, back, k;
      if (l == 7) { if (ip >= n) return 0; l += in[ip++]; }
      if (ip >= n) return 0;
      back = ((size_t)(c & 31) << 8) + in[ip++] + 1;
      l += 2;
      if (back > op || l > outcap - op) return 0;
      for (k = 0; k < l; k++) { out[op] = out[op - back]; op++; }
    }
  }
  return op == outcap;
}

u8 unit_to8(double v)
{
  if (!(v > 0)) return 0;
  if (v >= 1) return 255;
  return (u8)(v * 255 + 0.5);
}
u8 lin_to_srgb8(double v)
{
  if (!(v > 0)) return 0;
  if (v >= 1) return 255;
  return unit_to8(v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1 / 2.4) - 0.055);
}
double half_to_double(unsigned h)
{
  int e = (h >> 10) & 31, m = h & 1023;
  double v;
  if (e == 0) v = ldexp((double)m, -24);
  else if (e == 31) v = m ? 0 : 65504;               /* NaN as 0, infinity as the largest half */
  else v = ldexp((double)(m | 1024), e - 25);
  return (h & 0x8000) ? -v : v;
}

void foreign_finish(Doc *doc)
{
  int i, k;
  for (i = doc->nlay - 1, k = 0; i >= 0; i--) {
    if (doc->lay[i]->depth > k) doc->lay[i]->depth = (u8)k;
    k = doc->lay[i]->depth + (doc->lay[i]->kind == LK_GROUP);
  }
  if (doc->active < 0 || doc->active >= doc->nlay) doc->active = doc->nlay - 1;
  while (doc->active > 0 && doc->lay[doc->active]->kind == LK_GROUP) doc->active--;
  doc_forget_undo(doc);
  doc->dirty = 0;
}
