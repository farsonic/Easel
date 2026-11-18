/*
 * pdn.c - Easel round 3: Paint.NET's .pdn documents (PDN3) with their layers (foreign.h). The file
 * is "PDN3", a 24-bit little-endian length and an XML header, the bytes 00 01, then the document
 * as .NET binary serialization (MS-NRBF, read here as plain records: nothing is run), then each
 * layer's pixels (BGRA, straight alpha) in numbered chunks, gzipped or raw. Written from the
 * MS-NRBF specification and the layout pypdn describes; no Paint.NET code.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "foreign.h"

#define NR_MAXOBJ 20000
#define NR_MAXVAL 200000

enum { V_NONE, V_NUM, V_STR, V_REF };
typedef struct { u8 kind; double num; long ref; const u8 *s; u32 slen; } Val;
typedef struct {
  int nmem;
  const u8 **mname; u32 *mlen;        /* member names (in the file) */
  u8 *btype, *ptype;                  /* binary and primitive types, NULL for classes without types */
  const u8 *name; u32 namelen;
} ClassDef;
typedef struct {
  long id;
  ClassDef *cls;                      /* NULL for an array */
  long val0; int nval;                /* its values: vals[val0 .. val0 + nval) */
} Obj;
typedef struct {
  const u8 *d; size_t n, pos;
  Obj *obj; long nobj;
  Val *vals; long nval;
  ClassDef **cls; long ncls;
  int bad;
  int depth;
} Nr;

static u32 le32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static int need(Nr *r, size_t k) { if (r->bad || r->pos > r->n || r->n - r->pos < k) { r->bad = 1; return 0; } return 1; }
static long rd_i32(Nr *r) { long v; if (!need(r, 4)) return 0; v = (long)(i32)le32(r->d + r->pos); r->pos += 4; return v; }
static int rd_u8(Nr *r) { if (!need(r, 1)) return 0; return r->d[r->pos++]; }
static int rd_str(Nr *r, const u8 **s, u32 *len)
{
  u32 l = 0;
  int sh = 0, b;
  do {
    b = rd_u8(r);
    if (r->bad || sh > 28) { r->bad = 1; return 0; }
    l |= (u32)(b & 127) << sh; sh += 7;
  } while (b & 128);
  if (!need(r, l)) return 0;
  *s = r->d + r->pos; *len = l; r->pos += l;
  return 1;
}
/* a primitive value of type t as a number (strings as strings) */
static int rd_prim(Nr *r, int t, Val *v)
{
  static const signed char size[19] = { 0, 1, 1, -1, 0, -2, 8, 2, 4, 8, 1, 4, 8, 8, 2, 4, 8, 0, -2 };
  const u8 *p;
  v->kind = V_NUM; v->num = 0;
  if (t < 1 || t > 18 || t == 4) { r->bad = 1; return 0; }
  if (size[t] == -2) { v->kind = V_STR; return rd_str(r, &v->s, &v->slen); }   /* Decimal and String */
  if (size[t] == -1) {                                    /* Char: one UTF-8 character */
    int b = rd_u8(r), k = b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : b >= 0xC0 ? 1 : 0;
    if (!need(r, (size_t)k)) return 0;
    r->pos += (size_t)k; v->num = b;
    return 1;
  }
  if (!need(r, (size_t)size[t])) return 0;
  p = r->d + r->pos; r->pos += (size_t)size[t];
  switch (t) {
  case 1: case 2: v->num = p[0]; break;
  case 10: v->num = (signed char)p[0]; break;
  case 7: v->num = (i16)(p[0] | (p[1] << 8)); break;
  case 14: v->num = p[0] | (p[1] << 8); break;
  case 8: v->num = (i32)le32(p); break;
  case 15: v->num = le32(p); break;
  case 9: case 12: case 13: case 16: {
    u32 lo = le32(p), hi = le32(p + 4);
    v->num = (t == 9 || t == 12) && (hi & 0x80000000UL) ? -((double)(~hi) * 4294967296.0 + (double)(~lo) + 1) : (double)hi * 4294967296.0 + lo;
    break; }
  case 11: { u32 u = le32(p); float f; memcpy(&f, &u, 4); v->num = f; break; }
  case 6: {
    u32 lo = le32(p), hi = le32(p + 4);
    int e = (int)((hi >> 20) & 2047);
    double m = (double)(hi & 0xFFFFF) * 4294967296.0 + lo;
    v->num = e == 0 || e == 2047 ? 0 : ldexp(1 + m / 4503599627370496.0, e - 1023);
    if (hi & 0x80000000UL) v->num = -v->num;
    break; }
  default: break;
  }
  return 1;
}

static long new_vals(Nr *r, int k)
{
  long at = r->nval;
  if (k < 0 || r->nval + k > NR_MAXVAL) { r->bad = 1; return -1; }
  memset(r->vals + at, 0, sizeof(Val) * (size_t)k);
  r->nval += k;
  return at;
}
static Obj *new_obj(Nr *r, long id)
{
  if (r->nobj >= NR_MAXOBJ) { r->bad = 1; return NULL; }
  r->obj[r->nobj].id = id; r->obj[r->nobj].cls = NULL; r->obj[r->nobj].val0 = 0; r->obj[r->nobj].nval = 0;
  return &r->obj[r->nobj++];
}
static ClassDef *find_cls(Nr *r, long objid)
{
  long i;
  for (i = 0; i < r->nobj; i++) if (r->obj[i].id == objid) return r->obj[i].cls;
  return NULL;
}

static int record(Nr *r, Val *out, int *nulls);

/* a class's member values into the object's slots */
static int class_values(Nr *r, ClassDef *c, long at)
{
  int i, nulls = 0;
  for (i = 0; i < c->nmem && !r->bad; i++) {
    Val *v = &r->vals[at + i];
    if (nulls > 0) { nulls--; v->kind = V_NONE; continue; }
    if (c->btype && c->btype[i] == 0) rd_prim(r, c->ptype[i], v);
    else if (!record(r, v, &nulls)) return 0;
    if (nulls > 0) nulls--;                              /* (this slot was the first of them) */
  }
  return !r->bad;
}
/* ClassInfo (and MemberTypeInfo when typed): a class definition kept for ClassWithId */
static ClassDef *class_info(Nr *r, int typed, long *objid)
{
  ClassDef *c;
  long k, cnt;
  if (r->ncls >= NR_MAXOBJ) { r->bad = 1; return NULL; }
  *objid = rd_i32(r);
  if (!(c = (ClassDef *)calloc(1, sizeof *c))) { r->bad = 1; return NULL; }
  r->cls[r->ncls++] = c;
  rd_str(r, &c->name, &c->namelen);
  cnt = rd_i32(r);
  if (r->bad || cnt < 0 || cnt > 10000 || (size_t)cnt > r->n - r->pos) { r->bad = 1; return NULL; }
  c->nmem = (int)cnt;
  if (!(c->mname = (const u8 **)calloc((size_t)cnt + 1, sizeof *c->mname)) || !(c->mlen = (u32 *)calloc((size_t)cnt + 1, sizeof *c->mlen))) { r->bad = 1; return NULL; }
  for (k = 0; k < cnt; k++) rd_str(r, &c->mname[k], &c->mlen[k]);
  if (!typed) return r->bad ? NULL : c;
  if (!(c->btype = (u8 *)calloc((size_t)cnt + 1, 1)) || !(c->ptype = (u8 *)calloc((size_t)cnt + 1, 1))) { r->bad = 1; return NULL; }
  for (k = 0; k < cnt; k++) c->btype[k] = (u8)rd_u8(r);
  for (k = 0; k < cnt && !r->bad; k++) {
    const u8 *s;
    u32 l;
    switch (c->btype[k]) {
    case 0: case 7: c->ptype[k] = (u8)rd_u8(r); break;
    case 3: rd_str(r, &s, &l); break;
    case 4: rd_str(r, &s, &l); rd_i32(r); break;
    case 1: case 2: case 5: case 6: break;
    default: r->bad = 1;
    }
  }
  return r->bad ? NULL : c;
}
/* array elements: n records (or primitives of type pt when pt > 0) into a new array object */
static int array_values(Nr *r, long id, long n, int pt)
{
  Obj *o;
  long at, i;
  int nulls = 0;
  if (n < 0 || n > NR_MAXVAL) { r->bad = 1; return 0; }
  if (pt > 0 && pt != 18 && (size_t)n > r->n - r->pos) { r->bad = 1; return 0; }
  if (pt == 2) {                                         /* bytes: skipped, not kept */
    if (!(o = new_obj(r, id))) return 0;
    r->pos += (size_t)n;
    return 1;
  }
  if ((at = new_vals(r, (int)n)) < 0 || !(o = new_obj(r, id))) return 0;
  o->val0 = at; o->nval = (int)n;
  for (i = 0; i < n && !r->bad; i++) {
    if (nulls > 0) { nulls--; continue; }
    if (pt > 0) rd_prim(r, pt, &r->vals[at + i]);
    else { if (!record(r, &r->vals[at + i], &nulls)) return 0; if (nulls > 0) nulls--; }
  }
  return !r->bad;
}

/* one record: what a member holds (an object becomes a reference to it) */
static int record(Nr *r, Val *out, int *nulls)
{
  int t = rd_u8(r);
  long id;
  ClassDef *c;
  Obj *o;
  if (r->bad) return 0;
  if (++r->depth > 64) { r->bad = 1; return 0; }
  out->kind = V_NONE;
  switch (t) {
  case 0: r->pos += 16; if (r->pos > r->n) r->bad = 1; break;
  case 12: { const u8 *s; u32 l; rd_i32(r); rd_str(r, &s, &l); r->depth--; return record(r, out, nulls); }
  case 1:
    id = rd_i32(r);
    c = find_cls(r, rd_i32(r));
    if (!c) { r->bad = 1; break; }
    goto values;
  case 2: case 3: case 4: case 5:
    c = class_info(r, t == 4 || t == 5, &id);
    if (!c) break;
    if (t == 3 || t == 5) rd_i32(r);                    /* the library */
  values:
    {
      long at = new_vals(r, c->nmem);
      if (at < 0 || !(o = new_obj(r, id))) break;
      o->cls = c; o->val0 = at; o->nval = c->nmem;
      out->kind = V_REF; out->ref = id;
      class_values(r, c, at);
    }
    break;
  case 6:
    id = rd_i32(r);
    out->kind = V_STR; rd_str(r, &out->s, &out->slen);
    { long at = new_vals(r, 1); if (at >= 0 && (o = new_obj(r, id)) != NULL) { o->val0 = at; o->nval = 1; r->vals[at] = *out; } }
    break;
  case 7: {
    int bt, rank, k, pt = 0;
    long len = 1;
    id = rd_i32(r); bt = rd_u8(r); rank = (int)rd_i32(r);
    if (rank < 1 || rank > 4) { r->bad = 1; break; }
    for (k = 0; k < rank; k++) { long l = rd_i32(r); if (l < 0 || l > NR_MAXVAL) { r->bad = 1; break; } len *= l; if (len > NR_MAXVAL) { r->bad = 1; break; } }
    if (bt >= 3 && bt <= 5) for (k = 0; k < rank; k++) rd_i32(r);
    t = rd_u8(r);
    if (t == 0 || t == 7) pt = rd_u8(r);
    else if (t == 3) { const u8 *s; u32 l; rd_str(r, &s, &l); }
    else if (t == 4) { const u8 *s; u32 l; rd_str(r, &s, &l); rd_i32(r); }
    if (r->bad) break;
    array_values(r, id, len, t == 0 ? pt : 0);
    out->kind = V_REF; out->ref = id;
    break; }
  case 8: rd_prim(r, rd_u8(r), out); break;
  case 9: out->kind = V_REF; out->ref = rd_i32(r); break;
  case 10: break;
  case 13: *nulls = rd_u8(r); break;
  case 14: *nulls = (int)rd_i32(r); if (*nulls < 0) r->bad = 1; break;
  case 15: { long l; id = rd_i32(r); l = rd_i32(r); t = rd_u8(r); array_values(r, id, l, t); out->kind = V_REF; out->ref = id; break; }
  case 16: case 17: { long l; id = rd_i32(r); l = rd_i32(r); array_values(r, id, l, 0); out->kind = V_REF; out->ref = id; break; }
  default: r->bad = 1;
  }
  r->depth--;
  return !r->bad;
}

static Obj *obj_of(Nr *r, const Val *v)
{
  long i;
  if (!v || v->kind != V_REF) return NULL;
  for (i = 0; i < r->nobj; i++) if (r->obj[i].id == v->ref) return &r->obj[i];
  return NULL;
}
/* a member by name (exactly, or as the part after "Class+" when suffix is set) */
static Val *member(Nr *r, Obj *o, const char *name)
{
  int i;
  size_t nl = strlen(name);
  if (!o || !o->cls) return NULL;
  for (i = 0; i < o->cls->nmem; i++) {
    u32 l = o->cls->mlen[i];
    const u8 *m = o->cls->mname[i];
    if (l == nl && !memcmp(m, name, nl)) return &r->vals[o->val0 + i];
  }
  for (i = 0; i < o->cls->nmem; i++) {                   /* "Base+name", or the auto-property "<name>k__BackingField" */
    u32 l = o->cls->mlen[i];
    const u8 *m = o->cls->mname[i];
    if (l > nl && m[l - nl - 1] == '+' && !memcmp(m + l - nl, name, nl)) return &r->vals[o->val0 + i];
    if (l == nl + 17 && m[0] == '<' && !memcmp(m + 1, name, nl) && !memcmp(m + 1 + nl, ">k__BackingField", 16)) return &r->vals[o->val0 + i];
  }
  return NULL;
}
static double num_of(Nr *r, Obj *o, const char *name, double def)
{
  Val *v = member(r, o, name);
  Obj *e;
  if (v && v->kind == V_NUM) return v->num;
  if (v && (e = obj_of(r, v)) != NULL && (v = member(r, e, "value__")) != NULL && v->kind == V_NUM) return v->num;   /* an enum */
  return def;
}
static int cls_is(Obj *o, const char *s)
{
  size_t l = strlen(s), k;
  if (!o || !o->cls || o->cls->namelen < l) return 0;
  for (k = 0; k + l <= o->cls->namelen; k++) if (!memcmp(o->cls->name + k, s, l)) return 1;
  return 0;
}

/* Paint.NET's blend modes (LayerBlendMode's order) and the 3.x blend operation classes' names */
static const u8 pdn_modes[14] = { BM_NORMAL, BM_MULTIPLY, BM_ADD, BM_PDNBURN, BM_PDNDODGE, BM_REFLECT, BM_GLOW, BM_OVERLAY,
                                  BM_DIFFERENCE, BM_NEGATION, BM_LIGHTEN, BM_DARKEN, BM_SCREEN, BM_XOR };
static const char *const pdn_ops[14] = { "+Normal", "+Multiply", "+Additive", "+ColorBurn", "+ColorDodge", "+Reflect", "+Glow",
                                         "+Overlay", "+Difference", "+Negation", "+Lighten", "+Darken", "+Screen", "+Xor" };
static int layer_mode(Nr *r, Obj *bl)
{
  Obj *p = obj_of(r, member(r, bl, "properties")), *op;
  double m;
  int i;
  if (!p) return BM_NORMAL;
  if ((m = num_of(r, p, "blendMode", -1)) >= 0) return m < 14 ? pdn_modes[(int)m] : BM_NORMAL;
  if ((op = obj_of(r, member(r, p, "blendOp"))) != NULL)
    for (i = 13; i >= 0; i--) if (cls_is(op, pdn_ops[i])) return pdn_modes[i];
  return BM_NORMAL;
}

Doc *pdn_read_doc(const u8 *d, size_t n, char *err, size_t errcap)
{
  Nr r;
  u32 hl;
  Val root;
  int nulls = 0, i, nl;
  Obj *docobj, *list, *items;
  long w, h, k;
  Doc *doc = NULL;
  codec_oom = 0;
  memset(&r, 0, sizeof r);
  if (n < 10 || memcmp(d, "PDN3", 4)) { codec_err(err, errcap, "Not a Paint.NET file"); return NULL; }
  hl = (u32)d[4] | ((u32)d[5] << 8) | ((u32)d[6] << 16);
  if (hl > n - 7 || n - 7 - hl < 2 || d[7 + hl] != 0 || d[8 + hl] != 1) { codec_err(err, errcap, "A Paint.NET file in a form Easel doesn't know"); return NULL; }
  r.d = d; r.n = n; r.pos = 9 + hl;
  r.obj = (Obj *)malloc(sizeof(Obj) * NR_MAXOBJ);
  r.vals = (Val *)malloc(sizeof(Val) * NR_MAXVAL);
  r.cls = (ClassDef **)calloc(NR_MAXOBJ, sizeof *r.cls);
  if (!r.obj || !r.vals || !r.cls) { codec_oom = 1; codec_err(err, errcap, "Not enough memory for the Paint.NET file"); goto out; }
  /* the records up to MessageEnd: the header names the root object */
  {
    long rootid = -1;
    if (rd_u8(&r) != 0) { codec_err(err, errcap, "Damaged Paint.NET file (no serialization header)"); goto out; }
    rootid = rd_i32(&r); r.pos += 12;
    for (;;) {
      if (r.bad) { codec_err(err, errcap, "Damaged Paint.NET file (its document)"); goto out; }
      if (r.pos < r.n && r.d[r.pos] == 11) { r.pos++; break; }
      record(&r, &root, &nulls);
      if (progress_aborted) goto out;
    }
    root.kind = V_REF; root.ref = rootid;
  }
  docobj = obj_of(&r, &root);
  w = (long)num_of(&r, docobj, "width", 0); h = (long)num_of(&r, docobj, "height", 0);
  if (w < 1 || h < 1 || w > MAXDIM || h > MAXDIM || w * h > CODEC_MAXPIXELS) { codec_err(err, errcap, "Damaged Paint.NET file (its size), or too big for Easel"); goto out; }
  list = obj_of(&r, member(&r, docobj, "layers"));
  items = obj_of(&r, member(&r, list, "_items"));
  nl = (int)num_of(&r, list, "_size", items ? items->nval : 0);
  if (!items || items->cls || nl < 1 || nl > items->nval) { codec_err(err, errcap, "Damaged Paint.NET file (its layers)"); goto out; }
  if (!(doc = doc_new((i32)w, (i32)h, 0, 1))) { codec_oom = 1; codec_err(err, errcap, "Not enough memory for the Paint.NET picture"); goto out; }
  layer_free(doc->lay[0]);
  doc->nlay = 0;
  /* the pixels follow, a block for each layer's surface in turn */
  for (i = 0; i < nl; i++) {
    Obj *bl = obj_of(&r, &r.vals[items->val0 + i]), *props, *surf, *blk;
    Layer *l;
    long sw, sh, stride, len, csize, nch, c, y;
    u8 *buf;
    int fmt;
    if (!bl) { codec_err(err, errcap, "Damaged Paint.NET file (a layer)"); goto bad; }
    props = obj_of(&r, member(&r, bl, "Layer+properties"));
    if (!props) props = obj_of(&r, member(&r, bl, "properties"));
    surf = obj_of(&r, member(&r, bl, "surface"));
    blk = obj_of(&r, member(&r, surf, "scan0"));
    sw = (long)num_of(&r, surf, "width", 0); sh = (long)num_of(&r, surf, "height", 0); stride = (long)num_of(&r, surf, "stride", 0);
    len = (long)num_of(&r, blk, "length64", num_of(&r, blk, "length", 0));
    if (!surf || !blk || sw != w || sh != h || stride < w * 4 || stride > w * 4 + 64 || len < stride * (h - 1) + w * 4 || len > stride * h + 64) {
      codec_err(err, errcap, "Damaged Paint.NET file (a layer's surface)"); goto bad;
    }
    if (doc->nlay >= MAXLAYERS) break;
    if (!(l = layer_new(doc, "Layer"))) { codec_oom = 1; goto oom; }
    doc->lay[doc->nlay++] = l;
    {
      Val *nm = member(&r, props, "name");
      if (nm && nm->kind == V_REF) { Obj *so = obj_of(&r, nm); nm = so && !so->cls && so->nval == 1 ? &r.vals[so->val0] : NULL; }
      if (nm && nm->kind == V_STR) { size_t q = nm->slen < sizeof l->name - 1 ? nm->slen : sizeof l->name - 1; memcpy(l->name, nm->s, q); l->name[q] = 0; }
      l->visible = num_of(&r, props, "visible", 1) != 0;
      {
        /* a double 0..1 (Paint.NET 3), or a byte 0..255 */
        double op = num_of(&r, props, "opacity", 1);
        if (op > 1.0001) op /= 255;
        l->opacity = (u8)(op <= 0 ? 0 : op >= 1 ? 255 : op * 255 + 0.5);
      }
      l->blend = (u8)layer_mode(&r, bl);
    }
    /* the block: a format byte, the chunk size, then numbered chunks */
    if (!need(&r, 5)) { codec_err(err, errcap, "Damaged Paint.NET file (a layer's pixels are missing)"); goto bad; }
    fmt = r.d[r.pos]; csize = (long)be32(r.d + r.pos + 1); r.pos += 5;
    if ((fmt != 0 && fmt != 1) || csize < 1 || csize > 64L * 1024 * 1024) { codec_err(err, errcap, "Damaged Paint.NET file (a layer's chunks)"); goto bad; }
    nch = (len + csize - 1) / csize;
    if (!(buf = (u8 *)calloc((size_t)len, 1))) { codec_oom = 1; goto oom; }
    for (c = 0; c < nch; c++) {
      long num, sz, want;
      if (!need(&r, 8)) break;
      num = (long)be32(r.d + r.pos); sz = (long)be32(r.d + r.pos + 4); r.pos += 8;
      if (num < 0 || num >= nch || sz < 0 || !need(&r, (size_t)sz)) { r.bad = 1; break; }
      want = len - num * csize < csize ? len - num * csize : csize;
      if (fmt == 1) {
        if (sz != want) { r.bad = 1; break; }
        memcpy(buf + num * csize, r.d + r.pos, (size_t)sz);
      } else {
        size_t gl;
        u8 *g = gunzip(r.d + r.pos, (size_t)sz, (size_t)want, &gl);
        if (!g || (long)gl != want) { free(g); r.bad = 1; break; }
        memcpy(buf + num * csize, g, gl);
        free(g);
      }
      r.pos += (size_t)sz;
      if (progress(c, nch)) break;
    }
    if (r.bad || c < nch) { free(buf); codec_err(err, errcap, "Damaged Paint.NET file (a layer's pixels)"); goto bad; }
    for (y = 0; y < h; y++) {
      const u8 *s = buf + y * stride;
      u8 *o = l->px + (size_t)y * w * 4;
      for (k = 0; k < w; k++, s += 4, o += 4) { o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = s[3]; }
    }
    free(buf);
  }
  doc->active = doc->nlay - 1;
  goto out;
oom:
  codec_err(err, errcap, "Not enough memory for the Paint.NET file's layers");
bad:
  doc_free(doc);
  doc = NULL;
out:
  if (r.cls) for (k = 0; k < r.ncls; k++) if (r.cls[k]) { free(r.cls[k]->mname); free(r.cls[k]->mlen); free(r.cls[k]->btype); free(r.cls[k]->ptype); free(r.cls[k]); }
  free(r.cls); free(r.obj); free(r.vals);
  return doc;
}
