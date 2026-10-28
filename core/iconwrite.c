/*
 * iconwrite.c - Workbench icons for pictures (see iconwrite.h). Written from the specifications:
 * the DiskObject (workbench/workbench.h), Gadget and Image (intuition/intuition.h) layouts as the
 * RKM Libraries describe a .info file, the NewIcons tool type coding and the OS 3.5 FORM ICON.
 *
 * A .info file is a DiskObject (78 bytes, its embedded Gadget's pointers standing for "present"
 * when not zero), then what those pointers point to, in order: the DrawerData (not written: these
 * are projects), the normal Image (20 bytes and its bitplanes, one plane after another, each row
 * a whole number of 16-bit words), the selected Image, the default tool and the tool types (each
 * string a 32-bit length with its NUL, then the bytes; the tool types after a 32-bit size of their
 * pointer array). A GlowIcon adds an IFF FORM ICON after all that.
 */
#include <stdlib.h>
#include <string.h>
#include "iconwrite.h"
#include "palette.h"

const char *const icon_kind_names[ICON_N + 1] = { "Classic", "NewIcons", "GlowIcons (OS 3.5)", "PNG (OS4, AROS)", NULL };

/* the classic palettes, as amigafmt.c's reader shows classic icons (the 2.x four, MagicWB's eight) */
static const u8 pal_mwb[8 * 3] = { 170, 170, 170, 0, 0, 0, 255, 255, 255, 102, 136, 187, 153, 153, 153, 187, 187, 187, 187, 170, 153, 255, 170, 153 };
int icon_classic_palette(int which, u8 *pal)
{
  int n = which == ICONPAL_MWB ? 8 : 4;
  memcpy(pal, pal_mwb, (size_t)n * 3);
  return n;
}

void icon_opts_default(IconOpts *o, int kind)
{
  memset(o, 0, sizeof *o);
  o->kind = kind;
  o->size = 64;
  o->classic_pal = kind == ICON_CLASSIC ? ICONPAL_MWB : ICONPAL_WB20;
  o->colors = 256;
  o->dither = DI_FS;
  o->selected = kind == ICON_GLOW;
}

/* ------------------------------------------------------------------ big-endian output */
static void put16(Buf *b, u32 v) { buf_addc(b, (int)((v >> 8) & 255)); buf_addc(b, (int)(v & 255)); }
static void put32(Buf *b, u32 v) { put16(b, v >> 16); put16(b, v & 0xFFFF); }
static void put_str(Buf *b, const char *s) { size_t l = strlen(s) + 1; put32(b, (u32)l); buf_add(b, s, l); }
static void set32(Buf *b, size_t at, u32 v)
{
  u8 *p = (u8 *)b->data + at;
  p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v;
}

/* ------------------------------------------------------------------ reduced images */
/* the thumbnail as indices into a classic palette: laid over colour 0 (the Workbench's grey, which
   is what shows round an icon), dithered, and anything mostly transparent made colour 0 */
static int classic_indices(const Rgba *t, const u8 *pal, int n, int dither, u8 *idx)
{
  long k, np = (long)t->w * t->h;
  u8 *flat = (u8 *)malloc((size_t)np * 4);
  int ok;
  if (!flat) return 0;
  for (k = 0; k < np; k++) {
    const u8 *s = t->px + k * 4;
    u8 *o = flat + k * 4;
    int a = s[3], c;
    for (c = 0; c < 3; c++) o[c] = (u8)((s[c] * a + pal[c] * (255 - a) + 127) / 255);
    o[3] = 255;
  }
  ok = map_rgba(flat, t->w, t->h, pal, n, dither, idx);
  free(flat);
  if (!ok) return 0;
  for (k = 0; k < np; k++) if (t->px[k * 4 + 3] < 128) idx[k] = 0;
  return 1;
}

/* the thumbnail to at most maxc colours (exact when it has that few), entry 0 kept for the
   transparent pixels when there are any (*transp set). Returns the number of entries, 0 if out of
   memory */
static int reduce(const Rgba *t, int maxc, int dither, u8 *pal, u8 *idx, int *transp)
{
  long k, np = (long)t->w * t->h;
  int any = 0, n, opaque = 0;
  for (k = 0; k < np; k++) { if (t->px[k * 4 + 3] < 128) any = 1; else opaque = 1; }
  if (maxc < 2) maxc = 2;
  if (maxc > 256) maxc = 256;
  *transp = any;
  if (!opaque) { memset(pal, 0, 6); memset(idx, 0, (size_t)np); return 2; }
  n = quantize_rgba(t->px, t->w, t->h, any ? maxc - 1 : maxc, QM_WU, dither, pal + (any ? 3 : 0), 0, idx);
  if (!n) return 0;
  if (any) {
    pal[0] = pal[1] = pal[2] = 0;
    for (k = 0; k < np; k++) idx[k] = t->px[k * 4 + 3] < 128 ? 0 : (u8)(idx[k] + 1);
    n++;
  }
  return n;
}
static int bits_for(int ncol) { int b = 1; while ((1 << b) < ncol) b++; return b; }
static void darken(const u8 *pal, int n, u8 *out)
{
  int i;
  for (i = 0; i < n * 3; i++) out[i] = (u8)(pal[i] * 5 / 8);
}

/* ------------------------------------------------------------------ the DiskObject */
typedef struct { i32 w, h; int depth; const u8 *idx; } Planes;
/* struct Image (20 bytes) and its planes from indices */
static void put_image(Buf *b, const Planes *p)
{
  size_t rowbytes = (size_t)((p->w + 15) >> 4) * 2;
  int pl;
  i32 x, y;
  u8 *row;
  put16(b, 0); put16(b, 0);                           /* LeftEdge, TopEdge */
  put16(b, (u32)p->w); put16(b, (u32)p->h); put16(b, (u32)p->depth);
  put32(b, 1);                                        /* ImageData: present */
  buf_addc(b, (1 << p->depth) - 1);                   /* PlanePick: every plane */
  buf_addc(b, 0);                                     /* PlaneOnOff */
  put32(b, 0);                                        /* NextImage */
  if (!(row = (u8 *)malloc(rowbytes))) { b->oom = 1; return; }
  for (pl = 0; pl < p->depth; pl++)
    for (y = 0; y < p->h; y++) {
      memset(row, 0, rowbytes);
      for (x = 0; x < p->w; x++) if ((p->idx[(size_t)y * p->w + x] >> pl) & 1) row[x >> 3] |= (u8)(0x80 >> (x & 7));
      buf_add(b, row, rowbytes);
    }
  free(row);
}
/* a project's DiskObject with its normal image, default tool and tool types */
static void put_diskobject(Buf *b, const Planes *img, const char *deftool, char **tt, int ntt)
{
  int i;
  put16(b, 0xE310); put16(b, 1);                      /* WB_DISKMAGIC, WB_DISKVERSION */
  /* the Gadget */
  put32(b, 0);                                        /* NextGadget */
  put16(b, 0); put16(b, 0);                           /* LeftEdge, TopEdge */
  put16(b, (u32)img->w); put16(b, (u32)img->h);       /* Width, Height */
  put16(b, 4 | 0);                                    /* Flags: GFLG_GADGIMAGE | GFLG_GADGHCOMP */
  put16(b, 3);                                        /* Activation: RELVERIFY | GADGIMMEDIATE */
  put16(b, 1);                                        /* GadgetType: BOOLGADGET */
  put32(b, 1);                                        /* GadgetRender: present */
  put32(b, 0);                                        /* SelectRender */
  put32(b, 0); put32(b, 0); put32(b, 0);              /* GadgetText, MutualExclude, SpecialInfo */
  put16(b, 0);                                        /* GadgetID */
  put32(b, 1);                                        /* UserData: WB_DISKREVISION 1 (OS 2.x) */
  buf_addc(b, 4); buf_addc(b, 0);                     /* do_Type WBPROJECT, pad */
  put32(b, deftool && deftool[0] ? 1 : 0);            /* do_DefaultTool */
  put32(b, ntt ? 1 : 0);                              /* do_ToolTypes */
  put32(b, 0x80000000UL); put32(b, 0x80000000UL);     /* do_CurrentX, Y: NO_ICON_POSITION */
  put32(b, 0); put32(b, 0);                           /* do_DrawerData, do_ToolWindow */
  put32(b, 4096);                                     /* do_StackSize */
  put_image(b, img);
  if (deftool && deftool[0]) put_str(b, deftool);
  if (ntt) {
    put32(b, (u32)(ntt + 1) * 4);
    for (i = 0; i < ntt; i++) put_str(b, tt[i]);
  }
}

/* ------------------------------------------------------------------ NewIcons */
/* the tool type lines: "IM1=" (or "IM2=") and up to NI_LINE characters. Each character carries
   7 bits; a line holds whole groups of 7 values (bpp characters for the pixels, 8 for the palette's
   bytes), so no value is split between lines and a line ends with no spare bits (only the last
   line of the palette and of the pixels is padded). Runs of zero characters become 0xD1-0xFF */
#define NI_LINE 112
typedef struct { char **s; int n, cap; int oom; } Lines;
typedef struct { Lines *ls; char line[NI_LINE + 16]; int len, start; u32 acc; int nacc, zeros; } NiOut;
static void lines_add(Lines *l, const char *s, size_t n)
{
  char *c;
  if (l->oom) return;
  if (l->n == l->cap) {
    int nc = l->cap ? l->cap * 2 : 32;
    char **ns = (char **)realloc(l->s, sizeof(char *) * (size_t)nc);
    if (!ns) { l->oom = 1; return; }
    l->s = ns; l->cap = nc;
  }
  if (!(c = str_ndup(s, n))) { l->oom = 1; return; }
  l->s[l->n++] = c;
}
static void lines_free(Lines *l) { int i; for (i = 0; i < l->n; i++) free(l->s[i]); free(l->s); memset(l, 0, sizeof *l); }
static void ni_flushzeros(NiOut *o)
{
  while (o->zeros > 0) { int k = o->zeros > 47 ? 47 : o->zeros; o->line[o->len++] = (char)(0xD0 + k); o->zeros -= k; }
}
static void ni_char(NiOut *o, int v)
{
  if (!v) { o->zeros++; return; }
  ni_flushzeros(o);
  o->line[o->len++] = (char)(v < 0x50 ? v + 0x20 : v + 0x51);
}
static void ni_put(NiOut *o, int v, int k)
{
  o->acc = (o->acc << k) | (u32)v; o->nacc += k;
  while (o->nacc >= 7) { o->nacc -= 7; ni_char(o, (int)((o->acc >> o->nacc) & 0x7F)); }
}
static void ni_endline(NiOut *o)
{
  if (o->nacc) { ni_char(o, (int)((o->acc << (7 - o->nacc)) & 0x7F)); o->nacc = 0; }
  ni_flushzeros(o);
  o->acc = 0;
  if (o->len > 4) lines_add(o->ls, o->line, (size_t)o->len);
  o->len = 4;
}
/* values (k bits each) in groups of 7, at most groups a line; the first line's prefix (the header)
   counts against it */
static void ni_values(NiOut *o, const u8 *v, long n, int k, int groups_first, int groups)
{
  long i;
  int g = groups_first, inl = 0;
  for (i = 0; i < n; i++) {
    if (inl == g * 7) { ni_endline(o); g = groups; inl = 0; }
    ni_put(o, v[i], k);
    inl++;
  }
  ni_endline(o);
}
static void ni_image(Lines *ls, int which, const u8 *idx, i32 w, i32 h, const u8 *pal, int ncol, int transp)
{
  NiOut o;
  int bpp = bits_for(ncol);
  memset(&o, 0, sizeof o);
  o.ls = ls;
  memcpy(o.line, which ? "IM2=" : "IM1=", 4);
  o.line[4] = transp ? 'B' : 'C';
  o.line[5] = (char)(w + 0x21); o.line[6] = (char)(h + 0x21);
  o.line[7] = (char)((ncol >> 6) + 0x21); o.line[8] = (char)((ncol & 63) + 0x21);
  o.len = 9;
  ni_values(&o, pal, (long)ncol * 3, 8, (NI_LINE - 5) / 8, NI_LINE / 8);
  ni_values(&o, idx, (long)w * h, bpp, NI_LINE / bpp, NI_LINE / bpp);
}

/* ------------------------------------------------------------------ GlowIcons */
typedef struct { Buf *b; u32 acc; int nacc; } BitW;
static void bw_put(BitW *w, u32 v, int k)
{
  w->acc = (w->acc << k) | (v & ((1u << k) - 1)); w->nacc += k;
  while (w->nacc >= 8) { w->nacc -= 8; buf_addc(w->b, (int)((w->acc >> w->nacc) & 255)); }
}
static void bw_flush(BitW *w) { if (w->nacc) { buf_addc(w->b, (int)((w->acc << (8 - w->nacc)) & 255)); w->nacc = 0; } }
/* the OS 3.5 run length: a code n then n + 1 literal values (n < 128), or 257 - n repeats of one
   (n > 128), values depth bits wide on one bit stream */
static void glow_pack(Buf *b, const u8 *v, long n, int depth)
{
  BitW w;
  long i = 0, j, run, k;
  w.b = b; w.acc = 0; w.nacc = 0;
  while (i < n) {
    for (run = 1; i + run < n && run < 128 && v[i + run] == v[i]; run++) ;
    if (run >= 2) { bw_put(&w, (u32)(257 - run), 8); bw_put(&w, v[i], depth); i += run; continue; }
    for (j = i; j < n && j - i < 128 && !(j + 1 < n && v[j] == v[j + 1]); j++) ;
    bw_put(&w, (u32)(j - i - 1), 8);
    for (k = i; k < j; k++) bw_put(&w, v[k], depth);
    i = j;
  }
  bw_flush(&w);
}
/* one IMAG chunk: the indices packed if that is smaller, and the palette */
static void glow_imag(Buf *b, const u8 *idx, i32 w, i32 h, const u8 *pal, int ncol, int transp)
{
  Buf ip, pp;
  long np = (long)w * h;
  int depth = bits_for(ncol), icomp, pcomp;
  size_t isz, psz, start;
  buf_init(&ip); buf_init(&pp);
  glow_pack(&ip, idx, np, depth);
  glow_pack(&pp, pal, (long)ncol * 3, 8);
  if (ip.oom || pp.oom) { b->oom = 1; buf_free(&ip); buf_free(&pp); return; }
  icomp = ip.len < (size_t)np; pcomp = pp.len < (size_t)ncol * 3;
  isz = icomp ? ip.len : (size_t)np;
  psz = pcomp ? pp.len : (size_t)ncol * 3;
  buf_add(b, "IMAG", 4);
  start = b->len;
  put32(b, (u32)(10 + isz + psz));
  buf_addc(b, 0);                                     /* the transparent colour: entry 0 */
  buf_addc(b, ncol - 1);
  buf_addc(b, (transp ? 1 : 0) | 2);                  /* flags: transparent colour, palette */
  buf_addc(b, icomp); buf_addc(b, pcomp); buf_addc(b, depth);
  put16(b, (u32)(isz - 1)); put16(b, (u32)(psz - 1));
  if (icomp) buf_add(b, ip.data, ip.len); else buf_add(b, idx, (size_t)np);
  if (pcomp) buf_add(b, pp.data, pp.len); else buf_add(b, pal, (size_t)ncol * 3);
  if ((b->len - start - 4) & 1) buf_addc(b, 0);
  buf_free(&ip); buf_free(&pp);
}
static void glow_form(Buf *b, const u8 *idx, i32 w, i32 h, const u8 *pal, const u8 *selpal, int ncol, int transp)
{
  size_t start;
  buf_add(b, "FORM", 4);
  start = b->len;
  put32(b, 0);
  buf_add(b, "ICON", 4);
  buf_add(b, "FACE", 4); put32(b, 6);
  buf_addc(b, w - 1); buf_addc(b, h - 1);
  buf_addc(b, 0);                                     /* flags: framed */
  buf_addc(b, 0x11);                                  /* aspect 1:1 */
  put16(b, (u32)(ncol * 3 - 1));                      /* the most palette bytes an image has, less 1 */
  glow_imag(b, idx, w, h, pal, ncol, transp);
  if (selpal) glow_imag(b, idx, w, h, selpal, ncol, transp);
  if (!b->oom) set32(b, start, (u32)(b->len - start - 4));
}

/* ------------------------------------------------------------------ the icon */
u8 *icon_write_ex(const Rgba *pic, const IconOpts *o, size_t *len, char *err, size_t errcap)
{
  Rgba t;
  Buf b;
  int size = o->size ? o->size : 64, maxsize = o->kind == ICON_NEWICONS ? 93 : 256;
  u8 cpal[8 * 3], *cidx = NULL, *ridx = NULL, *rpal = NULL;
  int ncp, ok = 0;
  Planes img;
  Lines tt;
  *len = 0;
  memset(&tt, 0, sizeof tt);
  if (!pic || !pic->px || pic->w < 1 || pic->h < 1) { codec_err(err, errcap, "There is no picture for the icon"); return NULL; }
  if (size < 8) size = 8;
  if (size > maxsize) size = maxsize;
  if (!rgba_thumbnail(pic, size, &t)) { codec_err(err, errcap, "Out of memory"); return NULL; }
  if (o->kind == ICON_PNG) {
    u8 *png = png_encode(&t, 0, len);
    rgba_free(&t);
    if (!png) codec_err(err, errcap, "Out of memory");
    return png;
  }
  buf_init(&b);
  ncp = icon_classic_palette(o->kind == ICON_CLASSIC ? o->classic_pal : ICONPAL_WB20, cpal);
  if (!(cidx = (u8 *)malloc((size_t)t.w * t.h)) || !classic_indices(&t, cpal, ncp, o->dither, cidx)) goto done;
  img.w = t.w; img.h = t.h; img.depth = ncp == 8 ? 3 : 2; img.idx = cidx;
  if (o->kind == ICON_NEWICONS || o->kind == ICON_GLOW) {
    int ncol, transp;
    u8 selpal[256 * 3];
    if (!(ridx = (u8 *)malloc((size_t)t.w * t.h)) || !(rpal = (u8 *)calloc(256 * 3, 1))) goto done;
    if (!(ncol = reduce(&t, o->colors ? o->colors : 256, o->dither, rpal, ridx, &transp))) goto done;
    darken(rpal, ncol, selpal);
    if (o->kind == ICON_NEWICONS) {
      lines_add(&tt, " ", 1);
      lines_add(&tt, "*** DON'T EDIT THE FOLLOWING LINES!! ***", 40);
      ni_image(&tt, 0, ridx, t.w, t.h, rpal, ncol, transp);
      if (o->selected) ni_image(&tt, 1, ridx, t.w, t.h, selpal, ncol, transp);
      if (tt.oom) goto done;
      put_diskobject(&b, &img, o->deftool, tt.s, tt.n);
    } else {
      put_diskobject(&b, &img, o->deftool, NULL, 0);
      glow_form(&b, ridx, t.w, t.h, rpal, o->selected ? selpal : NULL, ncol, transp);
    }
  } else put_diskobject(&b, &img, o->deftool, NULL, 0);
  ok = !b.oom;
done:
  free(cidx); free(ridx); free(rpal);
  lines_free(&tt);
  rgba_free(&t);
  if (!ok) { buf_free(&b); codec_err(err, errcap, "Out of memory"); return NULL; }
  *len = b.len;
  return (u8 *)buf_take(&b);
}

u8 *icon_write(const Rgba *pic, int kind, const char *deftool, size_t *len)
{
  IconOpts o;
  icon_opts_default(&o, kind);
  o.deftool = deftool;
  return icon_write_ex(pic, &o, len, NULL, 0);
}
