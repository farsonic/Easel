/*
 * cms.c - Easel's colour management: ICC profiles, transforms, the press model and the canvas's
 * view transforms (see cms.h). Every number in a profile is big-endian and read a byte at a time;
 * every offset and count is checked against the profile's size.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cms.h"

const char *const cms_intent_names[5] = { "Perceptual", "Relative Colorimetric", "Saturation", "Absolute Colorimetric", NULL };
const char *const proof_names[PRF_N] = { "None", "Working CMYK", "Working Cyan Plate", "Working Magenta Plate", "Working Yellow Plate",
  "Working Black Plate", "Working CMY Plates", "Legacy Macintosh RGB (Gamma 1.8)", "Internet Standard RGB (sRGB)", "Monitor RGB",
  "Color Blindness - Protanopia", "Color Blindness - Deuteranopia" };

static const double D50[3] = { 0.9642, 1.0, 0.8249 };

/* ------------------------------------------------------------------ the parts of a profile */
enum { CV_IDENT, CV_GAMMA, CV_TABLE, CV_PARA };
typedef struct { u8 type, ptype; double g, a, b, c, d, e, f; u16 *tab; u32 n; } Curve;
enum { ST_CURVES, ST_CLUT, ST_MATRIX };
#define MAXCH 8
typedef struct {
  u8 kind, in, out;
  Curve *cv;                          /* ST_CURVES: in of them (in == out) */
  u8 grid[MAXCH]; u16 *clut;          /* ST_CLUT: grid points per input, out values each */
  double m[12];                       /* ST_MATRIX: 3 x 3 then an offset */
} Stage;
enum { PE_XYZ, PE_LAB2, PE_LAB4 };    /* how a LUT's PCS side is encoded */
typedef struct { int in, out, nst, enc; Stage st[6]; } Lut;
enum { PK_MATRIX, PK_GRAY, PK_LUT, PK_LABID };
struct IccProfile {
  u32 space, pcs, cls;
  int kind;
  double mat[9], inv[9], wp[3];
  Curve trc[3];
  Lut *a2b[3], *b2a[3];
  char desc[64];
  u8 *raw; u32 rawlen;
  int builtin;
};

static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static u32 be16(const u8 *p) { return (u32)p[0] << 8 | p[1]; }
static double s15(const u8 *p) { return (double)(i32)be32(p) / 65536.0; }
static double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static double curve_eval(const Curve *c, double x)
{
  double y;
  x = clamp01(x);
  switch (c->type) {
  case CV_GAMMA: return pow(x, c->g);
  case CV_TABLE: {
    double p = x * (c->n - 1);
    u32 i = (u32)p;
    if (i >= c->n - 1) return c->tab[c->n - 1] / 65535.0;
    p -= i;
    return (c->tab[i] * (1 - p) + c->tab[i + 1] * p) / 65535.0; }
  case CV_PARA:
    switch (c->ptype) {
    case 0: y = pow(x, c->g); break;
    case 1: y = x >= -c->b / c->a ? pow(c->a * x + c->b, c->g) : 0; break;
    case 2: y = x >= -c->b / c->a ? pow(c->a * x + c->b, c->g) + c->c : c->c; break;
    case 3: y = x >= c->d ? pow(c->a * x + c->b, c->g) : c->c * x; break;
    default: y = x >= c->d ? pow(c->a * x + c->b, c->g) + c->e : c->c * x + c->f; break;
    }
    return clamp01(y);
  }
  return x;
}
/* the input that gives y (the curve taken as monotonic) */
static double curve_inv(const Curve *c, double y)
{
  double lo = 0, hi = 1;
  int up, i;
  if (c->type == CV_IDENT) return clamp01(y);
  if (c->type == CV_GAMMA && c->g > 0) return pow(clamp01(y), 1.0 / c->g);
  up = curve_eval(c, 1) >= curve_eval(c, 0);
  for (i = 0; i < 30; i++) {
    double m = (lo + hi) / 2, v = curve_eval(c, m);
    if ((v < y) == up) lo = m; else hi = m;
  }
  return (lo + hi) / 2;
}
static void curve_free(Curve *c) { free(c->tab); c->tab = NULL; }

/* a curv or para at off (its size at most left bytes); *used its length, 4-byte aligned */
static int curve_parse(const u8 *d, u32 left, Curve *c, u32 *used)
{
  u32 t, n, i;
  memset(c, 0, sizeof *c);
  if (left < 12) return 0;
  t = be32(d);
  if (t == ICC_SIG('c', 'u', 'r', 'v')) {
    n = be32(d + 8);
    if (n > (left - 12) / 2) return 0;
    *used = (12 + 2 * n + 3) & ~3u;
    if (n == 0) { c->type = CV_IDENT; return 1; }
    if (n == 1) { c->type = CV_GAMMA; c->g = be16(d + 12) / 256.0; return 1; }
    if (!(c->tab = (u16 *)malloc(n * sizeof(u16)))) return 0;
    for (i = 0; i < n; i++) c->tab[i] = (u16)be16(d + 12 + 2 * i);
    c->type = CV_TABLE; c->n = n;
    return 1;
  }
  if (t == ICC_SIG('p', 'a', 'r', 'a')) {
    static const u8 np[5] = { 1, 3, 4, 5, 7 };
    u32 pt = be16(d + 8), k;
    double v[7] = { 1, 1, 0, 0, 0, 0, 0 };
    if (pt > 4 || left < 12 + 4u * np[pt]) return 0;
    for (k = 0; k < np[pt]; k++) v[k] = s15(d + 12 + 4 * k);
    c->type = CV_PARA; c->ptype = (u8)pt;
    c->g = v[0]; c->a = v[1]; c->b = v[2]; c->c = v[3]; c->d = v[4]; c->e = v[5]; c->f = v[6];
    if ((pt == 1 || pt == 2) && c->a == 0) return 0;
    *used = (12 + 4 * np[pt] + 3) & ~3u;
    return 1;
  }
  return 0;
}

static void lut_free(Lut *l)
{
  int i, k;
  if (!l) return;
  for (i = 0; i < l->nst; i++) {
    Stage *s = &l->st[i];
    if (s->cv) { for (k = 0; k < s->in; k++) curve_free(&s->cv[k]); free(s->cv); }
    free(s->clut);
  }
  free(l);
}
static Stage *lut_add(Lut *l, int kind, int in, int out)
{
  Stage *s;
  if (l->nst >= 6) return NULL;
  s = &l->st[l->nst++];
  memset(s, 0, sizeof *s);
  s->kind = (u8)kind; s->in = (u8)in; s->out = (u8)out;
  return s;
}
/* how many grid entries a CLUT of in inputs holds; 0 if too many */
static u32 clut_points(const u8 *grid, int in)
{
  u32 n = 1;
  int i;
  for (i = 0; i < in; i++) { if (grid[i] < 2) return 0; n *= grid[i]; if (n > 1u << 20) return 0; }
  return n;
}
/* curves for n channels from a lut8/lut16 table set: n tables of entries values (bytes 1 or 2) */
static int mft_curves(Stage *s, const u8 *p, int n, u32 entries, int bytes)
{
  int k;
  u32 i;
  if (!(s->cv = (Curve *)calloc((size_t)n, sizeof(Curve)))) return 0;
  for (k = 0; k < n; k++) {
    Curve *c = &s->cv[k];
    if (!(c->tab = (u16 *)malloc(entries * sizeof(u16)))) return 0;
    for (i = 0; i < entries; i++) c->tab[i] = (u16)(bytes == 1 ? p[i] * 257 : be16(p + 2 * i));
    c->type = CV_TABLE; c->n = entries;
    p += entries * bytes;
  }
  return 1;
}
/* curves for n channels in a lutAtoB/BtoA at off */
static int mab_curves(Stage *s, const u8 *d, u32 size, u32 off, int n)
{
  int k;
  if (!(s->cv = (Curve *)calloc((size_t)n, sizeof(Curve)))) return 0;
  for (k = 0; k < n; k++) {
    u32 used = 0;
    if (off >= size || !curve_parse(d + off, size - off, &s->cv[k], &used)) return 0;
    off += used;
  }
  return 1;
}
/* a LUT tag (size bytes at d); a2b 1 for device to PCS. pcs the profile's PCS */
static Lut *lut_parse(const u8 *d, u32 size, int a2b, u32 pcs)
{
  Lut *l;
  u32 t;
  int in, out;
  if (size < 32) return NULL;
  t = be32(d);
  in = d[8]; out = d[9];
  if (in < 1 || in > MAXCH || out < 1 || out > MAXCH) return NULL;
  if (!(l = (Lut *)calloc(1, sizeof *l))) return NULL;
  l->in = in; l->out = out;
  if (t == ICC_SIG('m', 'f', 't', '1') || t == ICC_SIG('m', 'f', 't', '2')) {
    int b = t == ICC_SIG('m', 'f', 't', '2') ? 2 : 1, g = d[10], k;
    u32 ie = b == 2 ? be16(d + 48) : 256, oe = b == 2 ? be16(d + 50) : 256, off = b == 2 ? 52 : 48, np;
    Stage *s;
    u8 grid[MAXCH];
    for (k = 0; k < in; k++) grid[k] = (u8)g;
    np = clut_points(grid, in);
    if (!np || ie < 2 || oe < 2 || ie > 4096 || oe > 4096) goto bad;
    if ((double)off + (double)ie * in * b + (double)np * out * b + (double)oe * out * b > size) goto bad;
    l->enc = pcs == ICC_XYZ ? PE_XYZ : b == 2 ? PE_LAB2 : PE_LAB4;
    if (!a2b && pcs == ICC_XYZ && in == 3) {          /* (the matrix applies to XYZ input only) */
      if (!(s = lut_add(l, ST_MATRIX, 3, 3))) goto bad;
      for (k = 0; k < 9; k++) s->m[k] = s15(d + 12 + 4 * k);
    }
    if (!(s = lut_add(l, ST_CURVES, in, in)) || !mft_curves(s, d + off, in, ie, b)) goto bad;
    off += ie * in * b;
    if (!(s = lut_add(l, ST_CLUT, in, out))) goto bad;
    memcpy(s->grid, grid, sizeof grid);
    if (!(s->clut = (u16 *)malloc((size_t)np * out * sizeof(u16)))) goto bad;
    { u32 i; for (i = 0; i < np * (u32)out; i++) s->clut[i] = (u16)(b == 1 ? d[off + i] * 257 : be16(d + off + 2 * i)); }
    off += np * out * b;
    if (!(s = lut_add(l, ST_CURVES, out, out)) || !mft_curves(s, d + off, out, oe, b)) goto bad;
    return l;
  }
  if (t == ICC_SIG('m', 'A', 'B', ' ') || t == ICC_SIG('m', 'B', 'A', ' ')) {
    u32 ob = be32(d + 12), om = be32(d + 16), oM = be32(d + 20), oc = be32(d + 24), oa = be32(d + 28);
    int isab = t == ICC_SIG('m', 'A', 'B', ' ');
    Stage *s;
    int k;
    l->enc = pcs == ICC_XYZ ? PE_XYZ : PE_LAB4;
    /* A2B: A, CLUT, M, matrix, B. B2A: B, matrix, M, CLUT, A. The CLUT's sides are in and out */
    for (k = 0; k < 5; k++) {
      int part = isab ? k : 4 - k;                     /* 0 A, 1 CLUT, 2 M, 3 matrix, 4 B */
      if (part == 0 && oa) { int n = isab ? in : out; if (!(s = lut_add(l, ST_CURVES, n, n)) || !mab_curves(s, d, size, oa, n)) goto bad; }
      if (part == 1 && oc) {
        u32 np, i;
        int prec, j;
        if (oc + 20 > size) goto bad;
        if (!(s = lut_add(l, ST_CLUT, in, out))) goto bad;
        for (j = 0; j < in; j++) s->grid[j] = d[oc + j];
        prec = d[oc + 16];
        if (!(np = clut_points(s->grid, in)) || (prec != 1 && prec != 2) || (double)oc + 20 + (double)np * out * prec > size) goto bad;
        if (!(s->clut = (u16 *)malloc((size_t)np * out * sizeof(u16)))) goto bad;
        for (i = 0; i < np * (u32)out; i++) s->clut[i] = (u16)(prec == 1 ? d[oc + 20 + i] * 257 : be16(d + oc + 20 + 2 * i));
      }
      if (part == 2 && oM) { if (!(s = lut_add(l, ST_CURVES, 3, 3)) || !mab_curves(s, d, size, oM, 3)) goto bad; }
      if (part == 3 && om) {
        int j;
        if (om + 48 > size || !(s = lut_add(l, ST_MATRIX, 3, 3))) goto bad;
        for (j = 0; j < 12; j++) s->m[j] = s15(d + om + 4 * j);
      }
      if (part == 4 && ob) { int n = isab ? out : in; if (!(s = lut_add(l, ST_CURVES, n, n)) || !mab_curves(s, d, size, ob, n)) goto bad; }
    }
    if (!l->nst) goto bad;
    return l;
  }
bad:
  lut_free(l);
  return NULL;
}
/* multilinear interpolation in a CLUT */
static void clut_eval(const Stage *s, const double *in, double *out)
{
  int n = s->in, k, o;
  u32 base = 0, stride[MAXCH], st = s->out;
  int idx[MAXCH];
  double fr[MAXCH];
  for (k = n - 1; k >= 0; k--) { stride[k] = st; st *= s->grid[k]; }
  for (k = 0; k < n; k++) {
    double p = clamp01(in[k]) * (s->grid[k] - 1);
    idx[k] = (int)p; if (idx[k] >= s->grid[k] - 1) idx[k] = s->grid[k] - 2;
    fr[k] = p - idx[k];
    base += (u32)idx[k] * stride[k];
  }
  for (o = 0; o < s->out; o++) out[o] = 0;
  for (k = 0; k < 1 << n; k++) {
    double w = 1;
    u32 at = base;
    int j;
    for (j = 0; j < n; j++) {
      if (k >> j & 1) { w *= fr[j]; at += stride[j]; } else w *= 1 - fr[j];
    }
    if (w == 0) continue;
    for (o = 0; o < s->out; o++) out[o] += w * s->clut[at + o];
  }
  for (o = 0; o < s->out; o++) out[o] /= 65535.0;
}
static void lut_eval(const Lut *l, const double *in, double *out)
{
  double a[MAXCH], b[MAXCH];
  int i, k, n = l->in;
  for (k = 0; k < n; k++) a[k] = in[k];
  for (i = 0; i < l->nst; i++) {
    const Stage *s = &l->st[i];
    if (s->kind == ST_CURVES) for (k = 0; k < s->in && k < n; k++) a[k] = curve_eval(&s->cv[k], a[k]);
    else if (s->kind == ST_CLUT) { clut_eval(s, a, b); n = s->out; for (k = 0; k < n; k++) a[k] = b[k]; }
    else {
      for (k = 0; k < 3; k++) b[k] = s->m[3 * k] * a[0] + s->m[3 * k + 1] * a[1] + s->m[3 * k + 2] * a[2] + s->m[9 + k];
      for (k = 0; k < 3; k++) a[k] = clamp01(b[k]);
      n = 3;
    }
  }
  for (k = 0; k < l->out; k++) out[k] = a[k];
}

/* ------------------------------------------------------------------ XYZ and Lab */
static double cuberoot(double t) { return t > 0 ? pow(t, 1.0 / 3) : t < 0 ? -pow(-t, 1.0 / 3) : 0; }
static double labf(double t) { return t > 216.0 / 24389 ? cuberoot(t) : (24389.0 / 27 * t + 16) / 116; }
static double labfi(double t) { return t * t * t > 216.0 / 24389 ? t * t * t : (116 * t - 16) / (24389.0 / 27); }
void xyz_to_lab(const double xyz[3], double lab[3])
{
  double fx = labf(xyz[0] / D50[0]), fy = labf(xyz[1] / D50[1]), fz = labf(xyz[2] / D50[2]);
  lab[0] = 116 * fy - 16; lab[1] = 500 * (fx - fy); lab[2] = 200 * (fy - fz);
}
void lab_to_xyz(const double lab[3], double xyz[3])
{
  double fy = (lab[0] + 16) / 116, fx = fy + lab[1] / 500, fz = fy - lab[2] / 200;
  xyz[0] = labfi(fx) * D50[0]; xyz[1] = labfi(fy) * D50[1]; xyz[2] = labfi(fz) * D50[2];
}
/* a LUT's PCS values (0..1) to XYZ and back */
static void pcs_decode(int enc, const double *v, double xyz[3])
{
  if (enc == PE_XYZ) { int k; for (k = 0; k < 3; k++) xyz[k] = v[k] * 65535.0 / 32768.0; return; }
  {
    double s = enc == PE_LAB2 ? 65535.0 / 65280.0 : 1.0, lab[3];
    lab[0] = v[0] * s * 100; lab[1] = v[1] * s * 255 - 128; lab[2] = v[2] * s * 255 - 128;
    lab_to_xyz(lab, xyz);
  }
}
static void pcs_encode(int enc, const double xyz[3], double *v)
{
  if (enc == PE_XYZ) { int k; for (k = 0; k < 3; k++) v[k] = clamp01(xyz[k] * 32768.0 / 65535.0); return; }
  {
    double s = enc == PE_LAB2 ? 65280.0 / 65535.0 : 1.0, lab[3];
    xyz_to_lab(xyz, lab);
    v[0] = clamp01(lab[0] / 100 * s); v[1] = clamp01((lab[1] + 128) / 255 * s); v[2] = clamp01((lab[2] + 128) / 255 * s);
  }
}
static int inv3(const double *m, double *o)
{
  double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
  if (fabs(det) < 1e-12) return 0;
  o[0] = (m[4] * m[8] - m[5] * m[7]) / det; o[1] = (m[2] * m[7] - m[1] * m[8]) / det; o[2] = (m[1] * m[5] - m[2] * m[4]) / det;
  o[3] = (m[5] * m[6] - m[3] * m[8]) / det; o[4] = (m[0] * m[8] - m[2] * m[6]) / det; o[5] = (m[2] * m[3] - m[0] * m[5]) / det;
  o[6] = (m[3] * m[7] - m[4] * m[6]) / det; o[7] = (m[1] * m[6] - m[0] * m[7]) / det; o[8] = (m[0] * m[4] - m[1] * m[3]) / det;
  return 1;
}
static void mul3(const double *m, const double *v, double *o)
{
  double a = m[0] * v[0] + m[1] * v[1] + m[2] * v[2], b = m[3] * v[0] + m[4] * v[1] + m[5] * v[2], c = m[6] * v[0] + m[7] * v[1] + m[8] * v[2];
  o[0] = a; o[1] = b; o[2] = c;
}

/* ------------------------------------------------------------------ parsing a profile */
static void ascii_copy(char *dst, size_t cap, const u8 *s, size_t n, int utf16)
{
  size_t i, o = 0;
  for (i = 0; i < n && o + 1 < cap; i += utf16 ? 2 : 1) {
    u32 c = utf16 ? (i + 1 < n ? be16(s + i) : 0) : s[i];
    if (!c) break;
    dst[o++] = (char)(c >= 32 && c < 127 ? c : '?');
  }
  dst[o] = 0;
}
/* a desc or mluc tag's text */
static void desc_parse(const u8 *t, u32 size, char *out, size_t cap)
{
  out[0] = 0;
  if (size < 12) return;
  if (be32(t) == ICC_SIG('d', 'e', 's', 'c')) { u32 n = be32(t + 8); if (n > size - 12) n = size - 12; ascii_copy(out, cap, t + 12, n, 0); }
  else if (be32(t) == ICC_SIG('m', 'l', 'u', 'c') && size >= 28) {
    u32 len = be32(t + 20), off = be32(t + 24);
    if (off < size && len <= size - off) ascii_copy(out, cap, t + off, len, 1);
  }
  else if (be32(t) == ICC_SIG('t', 'e', 'x', 't')) ascii_copy(out, cap, t + 8, size - 8, 0);
}
/* the tag sig's data and size; NULL if missing or out of bounds */
static const u8 *tag_find(const u8 *d, size_t n, u32 sig, u32 *size)
{
  u32 cnt, i;
  if (n < 132) return NULL;
  cnt = be32(d + 128);
  if (cnt > (n - 132) / 12) return NULL;
  for (i = 0; i < cnt; i++) {
    const u8 *e = d + 132 + 12 * i;
    u32 off = be32(e + 4), sz = be32(e + 8);
    if (be32(e) != sig) continue;
    if (off >= n || sz > n - off || sz < 8) return NULL;
    *size = sz;
    return d + off;
  }
  return NULL;
}
void icc_desc_of(const u8 *d, size_t n, char *out, size_t cap)
{
  u32 sz;
  const u8 *t = tag_find(d, n, ICC_SIG('d', 'e', 's', 'c'), &sz);
  out[0] = 0;
  if (t) desc_parse(t, sz, out, cap);
}
static int xyz_tag(const u8 *d, size_t n, u32 sig, double *v)
{
  u32 sz;
  const u8 *t = tag_find(d, n, sig, &sz);
  if (!t || sz < 20 || be32(t) != ICC_SIG('X', 'Y', 'Z', ' ')) return 0;
  v[0] = s15(t + 8); v[1] = s15(t + 12); v[2] = s15(t + 16);
  return 1;
}
static int curve_tag(const u8 *d, size_t n, u32 sig, Curve *c)
{
  u32 sz, used;
  const u8 *t = tag_find(d, n, sig, &sz);
  return t && curve_parse(t, sz, c, &used);
}
void icc_free(IccProfile *p)
{
  int k;
  if (!p || p->builtin) return;
  for (k = 0; k < 3; k++) { curve_free(&p->trc[k]); lut_free(p->a2b[k]); lut_free(p->b2a[k]); }
  free(p->raw);
  free(p);
}
static int space_channels(u32 s) { return s == ICC_GRAY ? 1 : s == ICC_CMYK ? 4 : s == ICC_RGB || s == ICC_LAB ? 3 : 0; }
IccProfile *icc_parse(const u8 *d, size_t n, char *err, size_t errcap)
{
  IccProfile *p;
  int k;
  const char *msg = "Not an ICC profile Easel can use";
  static const u32 a2bs[3] = { ICC_SIG('A', '2', 'B', '0'), ICC_SIG('A', '2', 'B', '1'), ICC_SIG('A', '2', 'B', '2') };
  static const u32 b2as[3] = { ICC_SIG('B', '2', 'A', '0'), ICC_SIG('B', '2', 'A', '1'), ICC_SIG('B', '2', 'A', '2') };
  if (n < 132 || be32(d + 36) != ICC_SIG('a', 'c', 's', 'p')) { if (err && errcap) { strncpy(err, "Not an ICC profile", errcap - 1); err[errcap - 1] = 0; } return NULL; }
  if (!(p = (IccProfile *)calloc(1, sizeof *p))) { if (err && errcap) { strncpy(err, "Out of memory", errcap - 1); err[errcap - 1] = 0; } return NULL; }
  p->cls = be32(d + 12); p->space = be32(d + 16); p->pcs = be32(d + 20);
  if (!space_channels(p->space) || (p->pcs != ICC_XYZ && p->pcs != ICC_LAB)) goto bad;
  icc_desc_of(d, n, p->desc, sizeof p->desc);
  if (!xyz_tag(d, n, ICC_SIG('w', 't', 'p', 't'), p->wp)) memcpy(p->wp, D50, sizeof p->wp);
  for (k = 0; k < 3; k++) {
    u32 sz;
    const u8 *t;
    if ((t = tag_find(d, n, a2bs[k], &sz)) != NULL) p->a2b[k] = lut_parse(t, sz, 1, p->pcs);
    if ((t = tag_find(d, n, b2as[k], &sz)) != NULL) p->b2a[k] = lut_parse(t, sz, 0, p->pcs);
    if (p->a2b[k] && (p->a2b[k]->in != space_channels(p->space) || p->a2b[k]->out != 3)) { lut_free(p->a2b[k]); p->a2b[k] = NULL; }
    if (p->b2a[k] && (p->b2a[k]->in != 3 || p->b2a[k]->out != space_channels(p->space))) { lut_free(p->b2a[k]); p->b2a[k] = NULL; }
  }
  if (p->space == ICC_RGB) {
    double r[3], g[3], b[3];
    if (xyz_tag(d, n, ICC_SIG('r', 'X', 'Y', 'Z'), r) && xyz_tag(d, n, ICC_SIG('g', 'X', 'Y', 'Z'), g) && xyz_tag(d, n, ICC_SIG('b', 'X', 'Y', 'Z'), b) &&
        curve_tag(d, n, ICC_SIG('r', 'T', 'R', 'C'), &p->trc[0]) && curve_tag(d, n, ICC_SIG('g', 'T', 'R', 'C'), &p->trc[1]) &&
        curve_tag(d, n, ICC_SIG('b', 'T', 'R', 'C'), &p->trc[2])) {
      for (k = 0; k < 3; k++) { p->mat[3 * k] = r[k]; p->mat[3 * k + 1] = g[k]; p->mat[3 * k + 2] = b[k]; }
      if (inv3(p->mat, p->inv) && !p->a2b[0] && !p->a2b[1]) p->kind = PK_MATRIX;
      else p->kind = p->a2b[0] || p->a2b[1] ? PK_LUT : PK_MATRIX;
      if (p->kind == PK_MATRIX && !inv3(p->mat, p->inv)) goto bad;
    } else if (p->a2b[0] || p->a2b[1]) p->kind = PK_LUT;
    else goto bad;
  } else if (p->space == ICC_GRAY) {
    if (curve_tag(d, n, ICC_SIG('k', 'T', 'R', 'C'), &p->trc[0])) p->kind = PK_GRAY;
    else if (p->a2b[0]) p->kind = PK_LUT;
    else goto bad;
  } else if (p->space == ICC_LAB && !p->a2b[0] && !p->a2b[1]) p->kind = PK_LABID;
  else if (p->a2b[0] || p->a2b[1]) p->kind = PK_LUT;
  else goto bad;
  if (p->kind == PK_LUT && !p->b2a[0] && !p->b2a[1] && p->space != ICC_RGB) { /* (input only: fine for reading, not for separating) */ }
  if (!(p->raw = (u8 *)malloc(n))) { msg = "Out of memory"; goto bad; }
  memcpy(p->raw, d, n); p->rawlen = (u32)n;
  return p;
bad:
  icc_free(p);
  if (err && errcap) { strncpy(err, msg, errcap - 1); err[errcap - 1] = 0; }
  return NULL;
}

/* ------------------------------------------------------------------ the built-in profiles */
static const double srgb_mat[9] = { 0.4360747, 0.3850649, 0.1430804, 0.2225045, 0.7168786, 0.0606169, 0.0139322, 0.0971045, 0.7141733 };
static const double adobe_mat[9] = { 0.6097559, 0.2052401, 0.1492240, 0.3111242, 0.6256560, 0.0632197, 0.0194811, 0.0608902, 0.7448387 };
static IccProfile srgb_p, adobe_p;
static int builtins_made;
static void builtins(void)
{
  int k;
  if (builtins_made) return;
  builtins_made = 1;
  memset(&srgb_p, 0, sizeof srgb_p); memset(&adobe_p, 0, sizeof adobe_p);
  srgb_p.space = adobe_p.space = ICC_RGB; srgb_p.pcs = adobe_p.pcs = ICC_XYZ;
  srgb_p.cls = adobe_p.cls = ICC_SIG('m', 'n', 't', 'r');
  srgb_p.kind = adobe_p.kind = PK_MATRIX; srgb_p.builtin = adobe_p.builtin = 1;
  memcpy(srgb_p.mat, srgb_mat, sizeof srgb_mat); memcpy(adobe_p.mat, adobe_mat, sizeof adobe_mat);
  inv3(srgb_p.mat, srgb_p.inv); inv3(adobe_p.mat, adobe_p.inv);
  memcpy(srgb_p.wp, D50, sizeof D50); memcpy(adobe_p.wp, D50, sizeof D50);
  for (k = 0; k < 3; k++) {
    Curve *c = &srgb_p.trc[k];
    c->type = CV_PARA; c->ptype = 3; c->g = 2.4; c->a = 1 / 1.055; c->b = 0.055 / 1.055; c->c = 1 / 12.92; c->d = 0.04045;
    adobe_p.trc[k].type = CV_GAMMA; adobe_p.trc[k].g = 563.0 / 256;
  }
  strcpy(srgb_p.desc, "sRGB IEC61966-2.1");
  strcpy(adobe_p.desc, "Adobe RGB (1998)");
}
const IccProfile *icc_srgb(void) { builtins(); return &srgb_p; }
const IccProfile *icc_adobergb(void) { builtins(); return &adobe_p; }
u32 icc_space(const IccProfile *p) { return p ? p->space : ICC_RGB; }
const char *icc_desc(const IccProfile *p) { return p ? p->desc : "sRGB IEC61966-2.1"; }
const u8 *icc_bytes(const IccProfile *p, u32 *len) { if (!p || !p->raw) return NULL; *len = p->rawlen; return p->raw; }
int icc_is_matrix(const IccProfile *p) { return !p || p->kind == PK_MATRIX; }
int icc_channels(const IccProfile *p) { return p ? space_channels(p->space) : 3; }

static int curve_same(const Curve *a, const Curve *b)
{
  int i;
  for (i = 0; i <= 16; i++) if (fabs(curve_eval(a, i / 16.0) - curve_eval(b, i / 16.0)) > 1e-4) return 0;
  return 1;
}
int icc_same(const IccProfile *a, const IccProfile *b)
{
  int k;
  if (!a) a = icc_srgb();
  if (!b) b = icc_srgb();
  if (a == b) return 1;
  if (a->raw && b->raw && a->rawlen == b->rawlen && a->rawlen > 128 && !memcmp(a->raw + 128, b->raw + 128, a->rawlen - 128)) return 1;
  if (a->kind != PK_MATRIX || b->kind != PK_MATRIX) return 0;
  for (k = 0; k < 9; k++) if (fabs(a->mat[k] - b->mat[k]) > 2e-3) return 0;
  for (k = 0; k < 3; k++) if (!curve_same(&a->trc[k], &b->trc[k])) return 0;
  return 1;
}

/* the bytes of a v2 matrix/TRC display profile */
static void put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static void put16(u8 *p, u32 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void puts15(u8 *p, double v) { put32(p, (u32)(i32)floor(v * 65536 + 0.5)); }
u8 *icc_builtin_bytes(int which, u32 *len)
{
  const IccProfile *p = which == CMS_ADOBERGB ? icc_adobergb() : icc_srgb();
  const char *desc = p->desc, *cprt = "No copyright, use freely";
  int ntab = which == CMS_ADOBERGB ? 1 : 1024;      /* sRGB as a 1024-entry table (v2 readers know curv only) */
  u32 descsz = 12 + (u32)strlen(desc) + 1 + 12 + 67, cprtsz = 8 + (u32)strlen(cprt) + 1, trcsz = 12 + 2 * ntab;
  u32 off[9], size[9], sigs[9], pos, total, k;
  u8 *b;
  sigs[0] = ICC_SIG('d', 'e', 's', 'c'); size[0] = descsz;
  sigs[1] = ICC_SIG('c', 'p', 'r', 't'); size[1] = cprtsz;
  sigs[2] = ICC_SIG('w', 't', 'p', 't'); size[2] = 20;
  sigs[3] = ICC_SIG('r', 'X', 'Y', 'Z'); size[3] = 20;
  sigs[4] = ICC_SIG('g', 'X', 'Y', 'Z'); size[4] = 20;
  sigs[5] = ICC_SIG('b', 'X', 'Y', 'Z'); size[5] = 20;
  sigs[6] = ICC_SIG('r', 'T', 'R', 'C'); size[6] = trcsz;
  sigs[7] = ICC_SIG('g', 'T', 'R', 'C'); size[7] = trcsz;
  sigs[8] = ICC_SIG('b', 'T', 'R', 'C'); size[8] = trcsz;
  pos = 128 + 4 + 9 * 12;
  for (k = 0; k < 9; k++) {
    if (k == 7 || k == 8) { off[k] = off[6]; continue; }       /* (the three curves share one) */
    off[k] = pos; pos = (pos + size[k] + 3) & ~3u;
  }
  total = pos;
  if (!(b = (u8 *)calloc(1, total))) return NULL;
  put32(b, total); put32(b + 4, ICC_SIG('n', 'o', 'n', 'e')); put32(b + 8, 0x02100000);
  put32(b + 12, ICC_SIG('m', 'n', 't', 'r')); put32(b + 16, ICC_RGB); put32(b + 20, ICC_XYZ);
  put16(b + 24, 2026); put16(b + 26, 11); put16(b + 28, 18);
  put32(b + 36, ICC_SIG('a', 'c', 's', 'p'));
  put32(b + 64, 0);                                             /* perceptual */
  puts15(b + 68, D50[0]); puts15(b + 72, D50[1]); puts15(b + 76, D50[2]);
  put32(b + 128, 9);
  for (k = 0; k < 9; k++) { u8 *e = b + 132 + 12 * k; put32(e, sigs[k]); put32(e + 4, off[k]); put32(e + 8, size[k]); }
  /* desc: ASCII, then empty Unicode and ScriptCode parts */
  { u8 *t = b + off[0]; put32(t, sigs[0]); put32(t + 8, (u32)strlen(desc) + 1); memcpy(t + 12, desc, strlen(desc)); }
  { u8 *t = b + off[1]; put32(t, ICC_SIG('t', 'e', 'x', 't')); memcpy(t + 8, cprt, strlen(cprt)); }
  { u8 *t = b + off[2]; put32(t, ICC_XYZ); puts15(t + 8, D50[0]); puts15(t + 12, D50[1]); puts15(t + 16, D50[2]); }
  for (k = 0; k < 3; k++) { u8 *t = b + off[3 + k]; put32(t, ICC_XYZ); puts15(t + 8, p->mat[k]); puts15(t + 12, p->mat[3 + k]); puts15(t + 16, p->mat[6 + k]); }
  {
    u8 *t = b + off[6];
    put32(t, ICC_SIG('c', 'u', 'r', 'v')); put32(t + 8, (u32)ntab);
    if (ntab == 1) put16(t + 12, 563);
    else for (k = 0; k < (u32)ntab; k++) put16(t + 12 + 2 * k, (u32)floor(curve_eval(&p->trc[0], k / (double)(ntab - 1)) * 65535 + 0.5));
  }
  *len = total;
  return b;
}

/* ------------------------------------------------------------------ one colour, exactly */
static const Lut *pick(Lut *const *t, int intent)
{
  int i = intent == CI_ABSOLUTE ? CI_RELATIVE : intent;
  if (i < 0 || i > 2) i = 0;
  if (t[i]) return t[i];
  if (t[0]) return t[0];
  return t[1] ? t[1] : t[2];
}
int icc_to_xyz(const IccProfile *p, int intent, const double *in, double xyz[3])
{
  double v[MAXCH];
  int k;
  if (!p) p = icc_srgb();
  switch (p->kind) {
  case PK_MATRIX:
    for (k = 0; k < 3; k++) v[k] = curve_eval(&p->trc[k], in[k]);
    mul3(p->mat, v, xyz);
    break;
  case PK_GRAY: {
    double y = curve_eval(&p->trc[0], in[0]);
    for (k = 0; k < 3; k++) xyz[k] = y * D50[k];
    break; }
  case PK_LABID: { double lab[3]; lab[0] = in[0] * 100; lab[1] = in[1] * 255 - 128; lab[2] = in[2] * 255 - 128; lab_to_xyz(lab, xyz); break; }
  default: {
    const Lut *l = pick(p->a2b, intent);
    if (!l) return 0;
    lut_eval(l, in, v);
    pcs_decode(l->enc, v, xyz);
    break; }
  }
  if (intent == CI_ABSOLUTE) for (k = 0; k < 3; k++) xyz[k] *= p->wp[k] / D50[k];
  return 1;
}
int icc_from_xyz(const IccProfile *p, int intent, const double xyz0[3], double *out)
{
  double v[MAXCH], xyz[3];
  int k;
  if (!p) p = icc_srgb();
  for (k = 0; k < 3; k++) xyz[k] = intent == CI_ABSOLUTE ? xyz0[k] * D50[k] / p->wp[k] : xyz0[k];
  switch (p->kind) {
  case PK_MATRIX:
    mul3(p->inv, xyz, v);
    for (k = 0; k < 3; k++) out[k] = curve_inv(&p->trc[k], clamp01(v[k]));
    return 1;
  case PK_GRAY: out[0] = curve_inv(&p->trc[0], clamp01(xyz[1])); return 1;
  case PK_LABID: { double lab[3]; xyz_to_lab(xyz, lab); out[0] = clamp01(lab[0] / 100); out[1] = clamp01((lab[1] + 128) / 255); out[2] = clamp01((lab[2] + 128) / 255); return 1; }
  default: {
    const Lut *l = pick(p->b2a, intent);
    if (!l) {
      /* an RGB LUT profile with a matrix as well: through the matrix */
      if (p->space == ICC_RGB && (p->mat[0] != 0 || p->mat[4] != 0) && inv3(p->mat, v)) {
        double w[3];
        mul3(v, xyz, w);
        for (k = 0; k < 3; k++) out[k] = curve_inv(&p->trc[k], clamp01(w[k]));
        return 1;
      }
      return 0;
    }
    pcs_encode(l->enc, xyz, v);
    lut_eval(l, v, out);
    return 1; }
  }
}

/* ------------------------------------------------------------------ Easel's press */
/* the inks' colours in linear sRGB (SWOP-like process inks over white paper) */
static const double ink[4][3] = { { 0.02, 0.42, 0.86 }, { 0.84, 0.02, 0.26 }, { 1.0, 0.89, 0.03 }, { 0.017, 0.013, 0.014 } };
static void press_lin(const double cmyk[4], double rgb[3])
{
  int c, i;
  for (c = 0; c < 3; c++) {
    double t = 1;
    for (i = 0; i < 4; i++) {
      double v = clamp01(cmyk[i]), e = v + 0.6 * v * (1 - v);   /* (dot gain: 15% at 50%) */
      t *= 1 - e + e * ink[i][c];
    }
    rgb[c] = t;
  }
}
void press_to_xyz(const double cmyk[4], double xyz[3])
{
  double rgb[3];
  press_lin(cmyk, rgb);
  mul3(srgb_mat, rgb, xyz);
}
static double enc_srgb(double v) { v = clamp01(v); return v <= 0.0031308 ? 12.92 * v : 1.055 * pow(v, 1 / 2.4) - 0.055; }
static double dec_srgb(double v) { v = clamp01(v); return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }
static void press_err(const double *cmyk, const double t[3], double e[3])
{
  double rgb[3];
  int k;
  press_lin(cmyk, rgb);
  for (k = 0; k < 3; k++) e[k] = cuberoot(rgb[k] + 1e-4) - cuberoot(t[k] + 1e-4);
}
double press_from_xyz(const double xyz[3], double cmyk[4])
{
  static double sinv[9];
  static int sinv_ok;
  double t[3], lab0[3], lab1[3], x1[3], kmin, k;
  int it, j;
  if (!sinv_ok) { inv3(srgb_mat, sinv); sinv_ok = 1; }
  mul3(sinv, xyz, t);
  for (j = 0; j < 3; j++) { if (t[j] < 0) t[j] = 0; cmyk[j] = 1 - enc_srgb(t[j]); }
  kmin = cmyk[0] < cmyk[1] ? cmyk[0] : cmyk[1]; if (cmyk[2] < kmin) kmin = cmyk[2];
  k = kmin <= 0.2 ? 0 : kmin * (kmin - 0.2) / 0.8;  /* medium grey component replacement */
  cmyk[3] = k;
  for (j = 0; j < 3; j++) cmyk[j] = k < 1 ? clamp01((cmyk[j] - k) / (1 - k)) : 0;
  /* Gauss-Newton on C, M and Y (K kept), in a cube-root space that weighs the darks as the eye does */
  for (it = 0; it < 14; it++) {
    double e[3], J[9], Ji[9], step[3], h = 1e-4, n2 = 0;
    press_err(cmyk, t, e);
    for (j = 0; j < 3; j++) n2 += e[j] * e[j];
    if (n2 < 1e-10) break;
    for (j = 0; j < 3; j++) {
      double c2[4], e2[3];
      int r;
      memcpy(c2, cmyk, sizeof c2);
      c2[j] = cmyk[j] + (cmyk[j] + h > 1 ? -h : h);
      press_err(c2, t, e2);
      for (r = 0; r < 3; r++) J[3 * r + j] = (e2[r] - e[r]) / (c2[j] - cmyk[j]);
    }
    if (!inv3(J, Ji)) break;
    mul3(Ji, e, step);
    for (j = 0; j < 3; j++) {
      double s = step[j];
      if (s > 0.25) s = 0.25;
      if (s < -0.25) s = -0.25;
      cmyk[j] = clamp01(cmyk[j] - s);
    }
  }
  /* total ink at most 300% */
  {
    double sum = cmyk[0] + cmyk[1] + cmyk[2];
    if (sum + cmyk[3] > 3.0 && sum > 0) for (j = 0; j < 3; j++) cmyk[j] *= (3.0 - cmyk[3]) / sum;
  }
  press_to_xyz(cmyk, x1);
  xyz_to_lab(xyz, lab0); xyz_to_lab(x1, lab1);
  return sqrt((lab0[0] - lab1[0]) * (lab0[0] - lab1[0]) + (lab0[1] - lab1[1]) * (lab0[1] - lab1[1]) + (lab0[2] - lab1[2]) * (lab0[2] - lab1[2]));
}

/* ------------------------------------------------------------------ transforms */
struct CmsXform {
  int identity, fast, nout;
  /* fast: linear values 0..4095 per channel in, a 2.14 fixed-point matrix, 4097-entry curves out */
  u16 lin[3][256]; i32 m[9]; u8 out[3][4097];
  /* else: a grid of g points a side, nout values (8.8 fixed) at each, and a gamut grid (delta E) */
  int g; u16 *grid; u8 *gam;
  u8 gi[256]; u16 gf[256];            /* each 8-bit value's grid cell and place in it (0..256) */
};
struct CmsSep { CmsXform x; };

static CmsXform *xf_alloc(void)
{
  CmsXform *x = (CmsXform *)calloc(1, sizeof *x);
  return x;
}
static int grid_alloc(CmsXform *x, int g, int nout, int gamut)
{
  long n = (long)g * g * g, v;
  x->g = g; x->nout = nout;
  if (!(x->grid = (u16 *)malloc((size_t)n * nout * sizeof(u16)))) return 0;
  if (gamut && !(x->gam = (u8 *)malloc((size_t)n))) return 0;
  for (v = 0; v < 256; v++) {
    long p = v * (g - 1);                   /* in 255ths of a cell */
    int i = (int)(p / 255);
    if (i >= g - 1) i = g - 2;
    x->gi[v] = (u8)i;
    x->gf[v] = (u16)(((p - (long)i * 255) * 256 + 127) / 255);
  }
  return 1;
}
typedef void (*GridFn)(void *ctx, const double rgb[3], double *out, double *de);
static void grid_fill(CmsXform *x, GridFn fn, void *ctx)
{
  int g = x->g, r, gg, b, k;
  long at = 0;
  for (r = 0; r < g; r++)
    for (gg = 0; gg < g; gg++)
      for (b = 0; b < g; b++, at++) {
        double in[3], o[4], de = 0;
        in[0] = r / (double)(g - 1); in[1] = gg / (double)(g - 1); in[2] = b / (double)(g - 1);
        fn(ctx, in, o, x->gam ? &de : NULL);
        for (k = 0; k < x->nout; k++) x->grid[at * x->nout + k] = (u16)floor(clamp01(o[k]) * 255 * 256 + 0.5);
        if (x->gam) x->gam[at] = (u8)(de * 4 > 255 ? 255 : de * 4);
      }
}
/* tetrahedral interpolation of the grid at an 8-bit colour: nout values (0..255), and the gamut value */
static void grid_at(const CmsXform *x, const u8 *px, int *o, int *gam)
{
  int g = x->g, n = x->nout, k;
  int ri = x->gi[px[0]], gi = x->gi[px[1]], bi = x->gi[px[2]];
  long fr = x->gf[px[0]], fg = x->gf[px[1]], fb = x->gf[px[2]];
  long sr = (long)g * g, sg = g, base = ri * sr + gi * sg + bi;
  long c1, c2, a, b2, cc;
  long s1, s2, s3;                  /* the three corners after the base, as offsets */
  /* which of the six tetrahedra */
  if (fr >= fg) {
    if (fg >= fb) { s1 = sr; s2 = sr + sg; a = fr; b2 = fg; cc = fb; }
    else if (fr >= fb) { s1 = sr; s2 = sr + 1; a = fr; b2 = fb; cc = fg; }
    else { s1 = 1; s2 = sr + 1; a = fb; b2 = fr; cc = fg; }
  } else {
    if (fb >= fg) { s1 = 1; s2 = sg + 1; a = fb; b2 = fg; cc = fr; }
    else if (fb >= fr) { s1 = sg; s2 = sg + 1; a = fg; b2 = fb; cc = fr; }
    else { s1 = sg; s2 = sr + sg; a = fg; b2 = fr; cc = fb; }
  }
  s3 = sr + sg + 1;
  for (k = 0; k < n; k++) {
    long v0 = x->grid[base * n + k], v1 = x->grid[(base + s1) * n + k], v2 = x->grid[(base + s2) * n + k], v3 = x->grid[(base + s3) * n + k];
    long v = v0 * 256 + (v1 - v0) * a + (v2 - v1) * b2 + (v3 - v2) * cc;      /* 8.8 times 256 */
    v = (v + 32768) >> 16;
    o[k] = v < 0 ? 0 : v > 255 ? 255 : (int)v;
  }
  if (gam && x->gam) {
    long v0 = x->gam[base], v1 = x->gam[base + s1], v2 = x->gam[base + s2], v3 = x->gam[base + s3];
    c1 = v0 * 256 + (v1 - v0) * a + (v2 - v1) * b2 + (v3 - v2) * cc;
    c2 = c1 >> 8;
    *gam = (int)c2;
  }
}
static void fast_make(CmsXform *x, const IccProfile *s, const IccProfile *d, int bpc)
{
  double m[9];
  int k, v;
  /* d's inverse matrix times s's matrix; with bpc nothing changes (matrix profiles' black is 0) */
  (void)bpc;
  {
    int i, j;
    for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) m[3 * i + j] = d->inv[3 * i] * s->mat[j] + d->inv[3 * i + 1] * s->mat[3 + j] + d->inv[3 * i + 2] * s->mat[6 + j];
  }
  for (k = 0; k < 9; k++) x->m[k] = (i32)floor(m[k] * 16384 + 0.5);
  for (k = 0; k < 3; k++) {
    for (v = 0; v < 256; v++) x->lin[k][v] = (u16)floor(curve_eval(&s->trc[k], v / 255.0) * 4095 + 0.5);
    for (v = 0; v <= 4096; v++) x->out[k][v] = (u8)floor(curve_inv(&d->trc[k], v / 4095.0 > 1 ? 1 : v / 4095.0) * 255 + 0.5);
  }
  x->fast = 1;
}
typedef struct { const IccProfile *s, *d; int intent, bpc; double sb[3], db[3]; } ConvCtx;
static void black_point(const IccProfile *p, int intent, double *bp)
{
  double z[MAXCH] = { 0 }, out[MAXCH];
  int k, n = icc_channels(p);
  if (!p) p = icc_srgb();
  if (p->space == ICC_CMYK) { for (k = 0; k < 4; k++) z[k] = 1; }       /* (full ink) */
  else if (p->space == ICC_LAB) { z[0] = 0; z[1] = z[2] = 0.5; }
  if (p->kind == PK_LUT && p->space != ICC_RGB && p->space != ICC_GRAY) {
    /* an output profile's darkest: black through B2A and back */
    double x0[3] = { 0, 0, 0 };
    if (icc_from_xyz(p, intent, x0, out)) for (k = 0; k < n; k++) z[k] = out[k];
  }
  if (!icc_to_xyz(p, intent, z, bp)) bp[0] = bp[1] = bp[2] = 0;
}
static void conv_fn(void *c0, const double rgb[3], double *out, double *de)
{
  ConvCtx *c = (ConvCtx *)c0;
  double xyz[3];
  int k;
  (void)de;
  if (!icc_to_xyz(c->s, c->intent, rgb, xyz)) { for (k = 0; k < 3; k++) out[k] = rgb[k]; return; }
  if (c->bpc) for (k = 0; k < 3; k++) {
    double w = D50[k], sb = c->sb[k], db = c->db[k];
    if (w - sb > 1e-6) xyz[k] = (xyz[k] - sb) * (w - db) / (w - sb) + db;
  }
  if (!icc_from_xyz(c->d, c->intent, xyz, out)) for (k = 0; k < 3; k++) out[k] = rgb[k];
}
CmsXform *cms_xform_new(const IccProfile *src, const IccProfile *dst, int intent, int bpc)
{
  CmsXform *x = xf_alloc();
  if (!x) return NULL;
  if (!src) src = icc_srgb();
  if (!dst) dst = icc_srgb();
  x->nout = 3;
  if (icc_same(src, dst)) { x->identity = 1; return x; }
  if (src->kind == PK_MATRIX && dst->kind == PK_MATRIX && intent != CI_ABSOLUTE) { fast_make(x, src, dst, bpc); return x; }
  {
    ConvCtx c;
    c.s = src; c.d = dst; c.intent = intent; c.bpc = bpc && intent != CI_ABSOLUTE;
    if (c.bpc) { black_point(src, intent, c.sb); black_point(dst, intent, c.db); }
    if (!grid_alloc(x, 33, 3, 0)) { cms_xform_free(x); return NULL; }
    grid_fill(x, conv_fn, &c);
  }
  return x;
}
int cms_xform_identity(const CmsXform *x) { return x && x->identity; }
void cms_xform_free(CmsXform *x)
{
  if (!x) return;
  free(x->grid); free(x->gam); free(x);
}
void cms_xform_apply(const CmsXform *x, u8 *p, long n)
{
  long i;
  if (!x || x->identity) return;
  if (x->fast) {
    for (i = 0; i < n; i++, p += 4) {
      i32 a = x->lin[0][p[0]], b = x->lin[1][p[1]], c = x->lin[2][p[2]];
      int k;
      for (k = 0; k < 3; k++) {
        i32 v = (x->m[3 * k] * a + x->m[3 * k + 1] * b + x->m[3 * k + 2] * c + 8192) >> 14;
        p[k] = x->out[k][v < 0 ? 0 : v > 4096 ? 4096 : v];
      }
    }
    return;
  }
  for (i = 0; i < n; i++, p += 4) {
    int o[4], gam = 0;
    grid_at(x, p, o, x->gam ? &gam : NULL);
    if (x->gam && gam > 4 * 6) { gamut_warn_mix(p, o); continue; }
    p[0] = (u8)o[0]; p[1] = (u8)o[1]; p[2] = (u8)o[2];
  }
}

u32 gamut_warn_color = GAMUT_GREY;
int gamut_warn_opacity = 100;
void gamut_warn_mix(u8 *p, const int *o)
{
  int k, a = gamut_warn_opacity < 0 ? 0 : gamut_warn_opacity > 100 ? 100 : gamut_warn_opacity;
  for (k = 0; k < 3; k++) {
    int c = (int)((gamut_warn_color >> (16 - 8 * k)) & 255);
    p[k] = (u8)((o[k] * (100 - a) + c * a + 50) / 100);
  }
}
void cms_xform_apply_rgb(const CmsXform *x, u8 *p, long n)
{
  u8 t[4 * 64];
  long i, k;
  if (!x || x->identity) return;
  for (i = 0; i < n; i += 64) {
    long m = n - i < 64 ? n - i : 64;
    for (k = 0; k < m; k++) { t[k * 4] = p[(i + k) * 3]; t[k * 4 + 1] = p[(i + k) * 3 + 1]; t[k * 4 + 2] = p[(i + k) * 3 + 2]; t[k * 4 + 3] = 255; }
    cms_xform_apply(x, t, m);
    for (k = 0; k < m; k++) { p[(i + k) * 3] = t[k * 4]; p[(i + k) * 3 + 1] = t[k * 4 + 1]; p[(i + k) * 3 + 2] = t[k * 4 + 2]; }
  }
}

/* separations */
typedef struct { const IccProfile *s, *cmyk; int intent; } SepCtx;
static void sep_fn(void *c0, const double rgb[3], double *out, double *de)
{
  SepCtx *c = (SepCtx *)c0;
  double xyz[3];
  int k;
  (void)de;
  if (!icc_to_xyz(c->s, CI_RELATIVE, rgb, xyz)) xyz[0] = xyz[1] = xyz[2] = 0;
  if (c->cmyk) { if (!icc_from_xyz(c->cmyk, c->intent, xyz, out)) for (k = 0; k < 4; k++) out[k] = 0; }
  else press_from_xyz(xyz, out);
}
CmsSep *cms_sep_new(const IccProfile *src, const IccProfile *cmyk, int intent)
{
  CmsSep *s = (CmsSep *)calloc(1, sizeof *s);
  SepCtx c;
  if (!s) return NULL;
  if (cmyk && cmyk->space != ICC_CMYK) cmyk = NULL;
  if (!grid_alloc(&s->x, 17, 4, 0)) { cms_sep_free(s); return NULL; }
  c.s = src ? src : icc_srgb(); c.cmyk = cmyk; c.intent = intent;
  grid_fill(&s->x, sep_fn, &c);
  return s;
}
void cms_sep_apply(const CmsSep *s, const u8 *rgba, u8 *cmyk, long n)
{
  long i;
  for (i = 0; i < n; i++, rgba += 4, cmyk += 4) {
    int o[4];
    grid_at(&s->x, rgba, o, NULL);
    cmyk[0] = (u8)o[0]; cmyk[1] = (u8)o[1]; cmyk[2] = (u8)o[2]; cmyk[3] = (u8)o[3];
  }
}
void cms_sep_free(CmsSep *s) { if (s) { free(s->x.grid); free(s->x.gam); free(s); } }
void cms_cmyk_to_rgb(const IccProfile *cmyk, const IccProfile *dst, const u8 *in, u8 *rgba, long n)
{
  /* a 9-point 4-D table would do it faster; colours are cached by the last one seen instead (files
     are mostly runs of the same ink) */
  long i;
  u32 last = 0xFFFFFFFFUL;
  u8 lr = 0, lg = 0, lb = 0;
  if (cmyk && cmyk->space != ICC_CMYK) cmyk = NULL;
  for (i = 0; i < n; i++, in += 4, rgba += 4) {
    u32 key = (u32)in[0] << 24 | (u32)in[1] << 16 | (u32)in[2] << 8 | in[3];
    if (key != last) {
      double c[4], xyz[3], o[3];
      int k;
      for (k = 0; k < 4; k++) c[k] = in[k] / 255.0;
      if (!cmyk || !icc_to_xyz(cmyk, CI_RELATIVE, c, xyz)) press_to_xyz(c, xyz);
      icc_from_xyz(dst, CI_RELATIVE, xyz, o);
      lr = (u8)floor(clamp01(o[0]) * 255 + 0.5); lg = (u8)floor(clamp01(o[1]) * 255 + 0.5); lb = (u8)floor(clamp01(o[2]) * 255 + 0.5);
      last = key;
    }
    rgba[0] = lr; rgba[1] = lg; rgba[2] = lb; rgba[3] = 255;
  }
}
void cms_rgb_to_lab8(const IccProfile *src, const u8 *rgba, u8 *lab, long n)
{
  long i;
  for (i = 0; i < n; i++, rgba += 4, lab += 3) {
    double in[3], xyz[3], l[3];
    int a, b, L;
    in[0] = rgba[0] / 255.0; in[1] = rgba[1] / 255.0; in[2] = rgba[2] / 255.0;
    icc_to_xyz(src, CI_RELATIVE, in, xyz);
    xyz_to_lab(xyz, l);
    L = (int)floor(l[0] * 255 / 100 + 0.5); a = (int)floor(l[1] + 0.5); b = (int)floor(l[2] + 0.5);
    lab[0] = (u8)(L < 0 ? 0 : L > 255 ? 255 : L);
    lab[1] = (u8)(i32)(a < -128 ? -128 : a > 127 ? 127 : a);
    lab[2] = (u8)(i32)(b < -128 ? -128 : b > 127 ? 127 : b);
  }
}
void cms_lab8_to_rgb(const IccProfile *dst, const u8 *lab, u8 *rgba, long n)
{
  long i;
  for (i = 0; i < n; i++, lab += 3, rgba += 4) {
    double l[3], xyz[3], o[3];
    l[0] = lab[0] * 100.0 / 255; l[1] = lab[1] < 128 ? lab[1] : lab[1] - 256.0; l[2] = lab[2] < 128 ? lab[2] : lab[2] - 256.0;
    lab_to_xyz(l, xyz);
    icc_from_xyz(dst, CI_RELATIVE, xyz, o);
    rgba[0] = (u8)floor(clamp01(o[0]) * 255 + 0.5); rgba[1] = (u8)floor(clamp01(o[1]) * 255 + 0.5); rgba[2] = (u8)floor(clamp01(o[2]) * 255 + 0.5); rgba[3] = 255;
  }
}

/* ------------------------------------------------------------------ the canvas's view */
typedef struct { const IccProfile *doc, *cmyk; ViewSpec v; double gamma; } ViewCtx;
/* XYZ to what the display is sent */
static void to_monitor(const ViewCtx *c, const double xyz[3], double *out)
{
  int k;
  if (c->v.monitor && icc_from_xyz(c->v.monitor, CI_RELATIVE, xyz, out)) return;   /* round 3: through the monitor's profile */
  if (c->gamma <= 0) { icc_from_xyz(icc_srgb(), CI_RELATIVE, xyz, out); return; }
  {
    double lin[3];
    mul3(icc_srgb()->inv, xyz, lin);
    for (k = 0; k < 3; k++) out[k] = pow(clamp01(lin[k]), 1 / c->gamma);
  }
}
static void view_fn(void *c0, const double rgb[3], double *out, double *de)
{
  ViewCtx *c = (ViewCtx *)c0;
  double xyz[3], ck[4];
  int k, pf = c->v.on ? c->v.proof : PRF_NONE;
  if (pf == PRF_MONITOR) { for (k = 0; k < 3; k++) out[k] = rgb[k]; }
  else if (pf == PRF_MAC || pf == PRF_INTERNET) {
    double lin[3];
    for (k = 0; k < 3; k++) lin[k] = pf == PRF_MAC ? pow(rgb[k], 1.8) : dec_srgb(rgb[k]);
    mul3(srgb_mat, lin, xyz);
    to_monitor(c, xyz, out);
  } else {
    icc_to_xyz(c->doc, c->v.intent, rgb, xyz);
    if (pf == PRF_PROTAN || pf == PRF_DEUTAN) {
      /* Machado, Oliveira and Fernandes (2009) at full severity, on linear sRGB */
      static const double pro[9] = { 0.152286, 1.052583, -0.204868, 0.114503, 0.786281, 0.099216, -0.003882, -0.048116, 1.051998 };
      static const double deu[9] = { 0.367322, 0.860646, -0.227968, 0.280085, 0.672501, 0.047413, -0.011820, 0.042940, 0.968881 };
      double lin[3], l2[3];
      mul3(icc_srgb()->inv, xyz, lin);
      for (k = 0; k < 3; k++) lin[k] = clamp01(lin[k]);
      mul3(pf == PRF_PROTAN ? pro : deu, lin, l2);
      for (k = 0; k < 3; k++) l2[k] = clamp01(l2[k]);
      mul3(srgb_mat, l2, xyz);
      to_monitor(c, xyz, out);
    } else if (pf >= PRF_CMYK && pf <= PRF_CMY) {
      double px[3];
      if (c->cmyk) { if (!icc_from_xyz(c->cmyk, c->v.intent, xyz, ck)) ck[0] = ck[1] = ck[2] = ck[3] = 0; }
      else press_from_xyz(xyz, ck);
      if (pf >= PRF_CYAN && pf <= PRF_BLACK) {
        /* one plate: its ink as grey */
        double g = 1 - clamp01(ck[pf - PRF_CYAN]);
        for (k = 0; k < 3; k++) out[k] = g;
      } else {
        if (pf == PRF_CMY) ck[3] = 0;
        if (!c->cmyk || !icc_to_xyz(c->cmyk, c->v.intent == CI_ABSOLUTE ? CI_ABSOLUTE : CI_RELATIVE, ck, px)) press_to_xyz(ck, px);
        to_monitor(c, px, out);
      }
    } else to_monitor(c, xyz, out);
  }
  if (de) {
    /* how far the press (or CMYK profile) is from the colour */
    double x1[3], l0[3], l1[3];
    icc_to_xyz(c->doc, CI_RELATIVE, rgb, xyz);
    if (c->cmyk) {
      if (icc_from_xyz(c->cmyk, CI_RELATIVE, xyz, ck) && icc_to_xyz(c->cmyk, CI_RELATIVE, ck, x1)) {
        xyz_to_lab(xyz, l0); xyz_to_lab(x1, l1);
        *de = sqrt((l0[0] - l1[0]) * (l0[0] - l1[0]) + (l0[1] - l1[1]) * (l0[1] - l1[1]) + (l0[2] - l1[2]) * (l0[2] - l1[2]));
      } else *de = 0;
    } else *de = press_from_xyz(xyz, ck);
  }
}
CmsXform *cms_view_new(const IccProfile *doc, const ViewSpec *v)
{
  ViewCtx c;
  CmsXform *x;
  int proofing = v->on && v->proof != PRF_NONE;
  const IccProfile *mon = v->monitor && v->monitor->space == ICC_RGB && !icc_same(v->monitor, icc_srgb()) ? v->monitor : NULL;
  if (!proofing && !v->gamut && v->gamma10 == 0 && !mon && (!doc || icc_same(doc, icc_srgb()))) return NULL;
  if (doc && doc->space != ICC_RGB) doc = NULL;
  if (!proofing && !v->gamut && (v->gamma10 == 0 || mon) && (!doc || doc->kind == PK_MATRIX) && (!mon || mon->kind == PK_MATRIX)) {
    if (!(x = xf_alloc())) return NULL;
    x->nout = 3;
    fast_make(x, doc ? doc : icc_srgb(), mon ? mon : icc_srgb(), 0);
    return x;
  }
  c.doc = doc; c.v = *v; c.v.monitor = mon; c.cmyk = v->cmyk && v->cmyk->space == ICC_CMYK ? v->cmyk : NULL; c.gamma = v->gamma10 / 10.0;
  if (!(x = xf_alloc())) return NULL;
  if (!grid_alloc(x, proofing || v->gamut ? 17 : 33, 3, v->gamut)) { cms_xform_free(x); return NULL; }
  grid_fill(x, view_fn, &c);
  return x;
}

/* ------------------------------------------------------------------ opening CMYK files */
void (*cmyk_hook)(const u8 ink[4], u8 rgb[3]);
#define KG 9
static CmsXform *kslice[KG];
typedef struct { const IccProfile *cmyk; double k; } KCtx;
static void kslice_fn(void *c0, const double cmy[3], double *out, double *de)
{
  KCtx *c = (KCtx *)c0;
  double ck[4], xyz[3];
  (void)de;
  ck[0] = cmy[0]; ck[1] = cmy[1]; ck[2] = cmy[2]; ck[3] = c->k;
  if (!c->cmyk || !icc_to_xyz(c->cmyk, CI_RELATIVE, ck, xyz)) press_to_xyz(ck, xyz);
  icc_from_xyz(icc_srgb(), CI_RELATIVE, xyz, out);
}
static void kslice_hook(const u8 ink[4], u8 rgb[3])
{
  long p = (long)ink[3] * (KG - 1);
  int i = (int)(p / 255), o0[4], o1[4], k;
  long f;
  if (i >= KG - 1) i = KG - 2;
  f = ((p - (long)i * 255) * 256 + 127) / 255;
  grid_at(kslice[i], ink, o0, NULL);
  grid_at(kslice[i + 1], ink, o1, NULL);
  for (k = 0; k < 3; k++) rgb[k] = (u8)((o0[k] * (256 - f) + o1[k] * f + 128) >> 8);
}
void cms_cmyk_end(void)
{
  int i;
  for (i = 0; i < KG; i++) { cms_xform_free(kslice[i]); kslice[i] = NULL; }
  cmyk_hook = NULL;
}
int cms_cmyk_begin(const IccProfile *cmyk)
{
  int i;
  KCtx c;
  cms_cmyk_end();
  if (cmyk && cmyk->space != ICC_CMYK) cmyk = NULL;
  c.cmyk = cmyk;
  for (i = 0; i < KG; i++) {
    if (!(kslice[i] = xf_alloc()) || !grid_alloc(kslice[i], 9, 3, 0)) { cms_cmyk_end(); return 0; }
    c.k = i / (double)(KG - 1);
    grid_fill(kslice[i], kslice_fn, &c);
  }
  cmyk_hook = kslice_hook;
  return 1;
}
