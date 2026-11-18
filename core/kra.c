/*
 * kra.c - Easel round 3: Krita's .kra (and .krz) documents with their layers (foreign.h). Written
 * from the files Krita writes (a zip: mimetype, maindoc.xml with the layer tree, each layer's tiles
 * under <image name>/layers/); no Krita code.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "foreign.h"
#include "zip.h"
#include "xml.h"

#define KRA_MAXNODES (MAXLAYERS * 2)

typedef struct {
  u8 group, depth, visible, locked, collapsed, pass, opacity;
  int blend;
  i32 x, y, mx, my;
  char name[64], file[64], mask[64], cs[24], maskcs[24];
} KNode;

static const struct { const char *id; u8 mode; } kops[] = {
  { "normal", BM_NORMAL }, { "multiply", BM_MULTIPLY }, { "screen", BM_SCREEN }, { "overlay", BM_OVERLAY },
  { "darken", BM_DARKEN }, { "lighten", BM_LIGHTEN }, { "diff", BM_DIFFERENCE }, { "add", BM_ADD },
  { "linear_dodge", BM_ADD }, { "subtract", BM_SUBTRACT }, { "dodge", BM_DODGE }, { "burn", BM_BURN },
  { "soft_light", BM_SOFTLIGHT }, { "soft_light_svg", BM_SOFTLIGHT }, { "hard_light", BM_HARDLIGHT },
  { "exclusion", BM_EXCLUSION }, { "hue", BM_HUE }, { "saturation", BM_SATURATION }, { "color", BM_COLOR },
  { "luminize", BM_LUMINOSITY }, { "dissolve", BM_DISSOLVE }, { "linear_burn", BM_LINEARBURN },
  { "darker color", BM_DARKERCOLOR }, { "lighter color", BM_LIGHTERCOLOR }, { "vivid_light", BM_VIVIDLIGHT },
  { "linear light", BM_LINEARLIGHT }, { "linear_light", BM_LINEARLIGHT }, { "pin_light", BM_PINLIGHT },
  { "hard mix", BM_HARDMIX }, { "hard_mix", BM_HARDMIX }, { "hard_mix_photoshop", BM_HARDMIX }, { "divide", BM_DIVIDE },
  { "reflect", BM_REFLECT }, { "glow", BM_GLOW }, { "negation", BM_NEGATION }, { "xor", BM_XOR },
  { NULL, 0 } };
static int kra_mode(const char *s)
{
  int i;
  if (s) for (i = 0; kops[i].id; i++) if (!strcmp(kops[i].id, s)) return kops[i].mode;
  return BM_NORMAL;
}
static void cpy(char *d, size_t cap, const char *s) { if (!s) s = ""; strncpy(d, s, cap - 1); d[cap - 1] = 0; }
static long num(const char *s, long def) { return s ? atol(s) : def; }

/* a zip entry by name, malloc'd; NULL if absent or damaged */
static u8 *entry(ZipReader *z, const char *name, size_t *len)
{
  int i = zip_find(z, name);
  return i < 0 ? NULL : zip_read(z, i, len);
}

/* the pixel size and kind of a colour space id: 1 grey, 3 RGB; bytes a sample; 0 if Easel can't read it */
static int cs_kind(const char *cs, int *bps)
{
  *bps = 1;
  if (!strcmp(cs, "RGBA")) return 3;
  if (!strcmp(cs, "GRAYA") || !strcmp(cs, "GRAYAU8")) return 1;
  *bps = 2;
  if (!strcmp(cs, "RGBA16")) return 3;
  if (!strcmp(cs, "GRAYA16") || !strcmp(cs, "GRAYAU16")) return 1;
  return 0;
}

/* a tile file's pixels into dst (w x h, chans bytes a pixel after conversion: 4 for colour layers,
   1 for masks), placed at ox, oy. kind 3 (BGRA), 1 (grey + alpha) or 0 (one alpha/mask sample).
   1 on success */
static int read_tiles(const u8 *d, size_t n, int kind, int bps, i32 ox, i32 oy, u8 *dst, i32 w, i32 h, char *err, size_t errcap)
{
  size_t pos = 0;
  long tw = 0, th = 0, ps = 0, ntiles = -1, t;
  int version = 0;
  u8 *raw = NULL, *pix = NULL;
  int want = kind == 3 ? 4 * bps : kind == 1 ? 2 * bps : bps;
  /* the header: lines of KEY VALUE up to DATA */
  while (pos < n && ntiles < 0) {
    char line[80];
    size_t k = 0;
    while (pos < n && d[pos] != '\n' && k < sizeof line - 1) line[k++] = (char)d[pos++];
    line[k] = 0;
    if (pos < n && d[pos] == '\n') pos++; else { codec_err(err, errcap, "Damaged Krita file (a layer's header)"); return 0; }
    if (!strncmp(line, "VERSION ", 8)) version = atoi(line + 8);
    else if (!strncmp(line, "TILEWIDTH ", 10)) tw = atol(line + 10);
    else if (!strncmp(line, "TILEHEIGHT ", 11)) th = atol(line + 11);
    else if (!strncmp(line, "PIXELSIZE ", 10)) ps = atol(line + 10);
    else if (!strncmp(line, "DATA ", 5)) ntiles = atol(line + 5);
  }
  if (version != 2) { codec_err(err, errcap, "A Krita layer in a version Easel doesn't know"); return 0; }
  if (tw < 1 || th < 1 || tw > 1024 || th > 1024 || ps != want || ntiles < 0 || ntiles > 10000000L) {
    codec_err(err, errcap, "Damaged Krita file (a layer's tile header)"); return 0;
  }
  if (!(raw = (u8 *)malloc((size_t)tw * th * ps)) || !(pix = (u8 *)malloc((size_t)tw * th * ps))) {
    free(raw); codec_oom = 1; codec_err(err, errcap, "Not enough memory for the Krita file"); return 0;
  }
  for (t = 0; t < ntiles; t++) {
    char line[80];
    size_t k = 0, sz;
    long tx, ty, np = tw * th, i;
    int c;
    char method[16];
    unsigned long lsz;
    while (pos < n && d[pos] != '\n' && k < sizeof line - 1) line[k++] = (char)d[pos++];
    line[k] = 0;
    if (pos >= n || d[pos] != '\n') goto bad;
    pos++;
    if (sscanf(line, "%ld,%ld,%15[^,],%lu", &tx, &ty, method, &lsz) != 4 || strcmp(method, "LZF")) goto bad;
    sz = (size_t)lsz;
    if (sz < 1 || sz > n - pos) goto bad;
    if (d[pos] == 1) { if (!lzf_decompress(d + pos + 1, sz - 1, raw, (size_t)np * ps)) goto bad; }
    else if (d[pos] == 0) { if (sz - 1 != (size_t)np * ps) goto bad; memcpy(raw, d + pos + 1, (size_t)np * ps); }
    else goto bad;
    pos += sz;
    /* byte planes to pixels */
    for (c = 0; c < ps; c++) for (i = 0; i < np; i++) pix[i * ps + c] = raw[(long)c * np + i];
    for (i = 0; i < np; i++) {
      i32 X = (i32)(tx + i % tw) + ox, Y = (i32)(ty + i / tw) + oy;
      const u8 *s = pix + i * ps;
      if (X < 0 || Y < 0 || X >= w || Y >= h) continue;
      if (kind == 3) {
        u8 *o = dst + ((long)Y * w + X) * 4;
        if (bps == 1) { o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = s[3]; }
        else { o[0] = s[5]; o[1] = s[3]; o[2] = s[1]; o[3] = s[7]; }      /* (little-endian: the high bytes) */
      } else if (kind == 1) {
        u8 *o = dst + ((long)Y * w + X) * 4;
        o[0] = o[1] = o[2] = s[bps - 1]; o[3] = s[2 * bps - 1];
      } else dst[(long)Y * w + X] = s[bps - 1];
    }
    if (progress(t, ntiles)) break;
  }
  free(raw); free(pix);
  return 1;
bad:
  free(raw); free(pix);
  codec_err(err, errcap, "Damaged Krita file (a tile)");
  return 0;
}

/* a layer's default pixel (filename.defaultpixel) laid over the canvas */
static void default_pixel(ZipReader *z, const char *path, int kind, int bps, u8 *dst, i32 w, i32 h)
{
  char p[200];
  size_t len;
  u8 *dp, v[4];
  long k, np = (long)w * h;
  snprintf(p, sizeof p, "%s.defaultpixel", path);
  if (!(dp = entry(z, p, &len))) return;
  if (kind == 3 && len >= (size_t)4 * bps) { v[0] = dp[3 * bps - 1]; v[1] = dp[2 * bps - 1]; v[2] = dp[bps - 1]; v[3] = dp[4 * bps - 1]; }
  else if (kind == 1 && len >= (size_t)2 * bps) { v[0] = v[1] = v[2] = dp[bps - 1]; v[3] = dp[2 * bps - 1]; }
  else if (kind == 0 && len >= (size_t)bps) v[0] = dp[bps - 1];
  else { free(dp); return; }
  free(dp);
  if (kind) { if (v[3]) for (k = 0; k < np; k++) memcpy(dst + k * 4, v, 4); }
  else memset(dst, v[0], (size_t)np);
}

/* the merged image, flat, for documents in colour spaces Easel doesn't read */
static Doc *kra_merged(ZipReader *z, char *err, size_t errcap)
{
  size_t len;
  u8 *p = entry(z, "mergedimage.png", &len);
  Rgba im;
  Doc *doc;
  if (!p) { codec_err(err, errcap, "The Krita file's colour space isn't one Easel reads, and it has no merged image"); return NULL; }
  if (!png_read(p, len, &im, err, errcap)) { free(p); return NULL; }
  free(p);
  if (!(doc = doc_adopt(im.w, im.h, im.px))) { rgba_free(&im); codec_oom = 1; codec_err(err, errcap, "Not enough memory for the picture"); return NULL; }
  return doc;
}

Doc *kra_read_doc(const u8 *d, size_t n, char *err, size_t errcap)
{
  ZipReader z;
  Xml x;
  u8 *xml;
  size_t xl;
  KNode *nodes = NULL;
  int nn = 0, stack[64], sp = 0, i, inimage = 0;
  long w = 0, h = 0, dpi = 0;
  char iname[64] = "", ics[24] = "RGBA";
  Doc *doc = NULL;
  codec_oom = 0;
  if (!zip_open(&z, d, n)) { codec_err(err, errcap, "Not a Krita file"); return NULL; }
  if (!(xml = entry(&z, "maindoc.xml", &xl))) { zip_close(&z); codec_err(err, errcap, "Damaged Krita file (maindoc.xml)"); return NULL; }
  if (!(nodes = (KNode *)calloc(KRA_MAXNODES, sizeof *nodes))) { free(xml); zip_close(&z); codec_oom = 1; codec_err(err, errcap, "Not enough memory"); return NULL; }
  xml_init(&x, (const char *)xml, xl);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START && !strcmp(x.name, "IMAGE") && !inimage) {
      inimage = 1;
      w = num(xml_attr(&x, "width"), 0); h = num(xml_attr(&x, "height"), 0);
      dpi = num(xml_attr(&x, "x-res"), 0);
      cpy(iname, sizeof iname, xml_attr(&x, "name"));
      cpy(ics, sizeof ics, xml_attr(&x, "colorspacename"));
    } else if (x.type == XT_START && !strcmp(x.name, "layer") && inimage) {
      const char *a = xml_attr(&x, "nodetype");
      KNode *k;
      if (nn >= KRA_MAXNODES || sp >= 64) { xml_skip(&x); continue; }
      k = &nodes[nn];
      k->group = a && !strcmp(a, "grouplayer");
      k->depth = (u8)sp;
      cpy(k->name, sizeof k->name, xml_attr(&x, "name"));
      cpy(k->file, sizeof k->file, xml_attr(&x, "filename"));
      cpy(k->cs, sizeof k->cs, xml_attr(&x, "colorspacename"));
      k->x = (i32)num(xml_attr(&x, "x"), 0); k->y = (i32)num(xml_attr(&x, "y"), 0);
      k->opacity = (u8)(num(xml_attr(&x, "opacity"), 255) & 255);
      if (num(xml_attr(&x, "opacity"), 255) > 255) k->opacity = 255;
      k->visible = (u8)(num(xml_attr(&x, "visible"), 1) != 0);
      k->locked = (u8)(num(xml_attr(&x, "locked"), 0) != 0);
      k->collapsed = (u8)(num(xml_attr(&x, "collapsed"), 0) != 0);
      k->pass = (u8)(num(xml_attr(&x, "passthrough"), 0) != 0);
      k->blend = kra_mode(xml_attr(&x, "compositeop"));
      /* only paint layers and groups have pixels Easel can show; the rest are skipped with what they hold */
      if (!k->group && !(a && !strcmp(a, "paintlayer"))) { xml_skip(&x); continue; }
      nn++;
      stack[sp++] = nn - 1;                              /* (popped at its END, which <layer/> has too) */
    } else if (x.type == XT_END && !strcmp(x.name, "layer") && sp > 0) sp--;
    else if (x.type == XT_START && !strcmp(x.name, "mask") && sp > 0) {
      const char *a = xml_attr(&x, "nodetype");
      KNode *k = &nodes[stack[sp - 1]];
      if (a && !strcmp(a, "transparencymask") && !k->mask[0]) {
        cpy(k->mask, sizeof k->mask, xml_attr(&x, "filename"));
        cpy(k->maskcs, sizeof k->maskcs, xml_attr(&x, "colorspacename"));
        k->mx = (i32)num(xml_attr(&x, "x"), k->x); k->my = (i32)num(xml_attr(&x, "y"), k->y);
      }
    }
  }
  xml_free(&x);
  free(xml);
  if (w < 1 || h < 1 || w > MAXDIM || h > MAXDIM || w * h > CODEC_MAXPIXELS) { codec_err(err, errcap, "Damaged Krita file (its size)"); goto out; }
  {
    int bps;
    if (!cs_kind(ics, &bps)) { doc = kra_merged(&z, err, errcap); goto out; }
    for (i = 0; i < nn; i++) if (!nodes[i].group && !cs_kind(nodes[i].cs, &bps)) { doc = kra_merged(&z, err, errcap); goto out; }
  }
  if (!nn) { doc = kra_merged(&z, err, errcap); goto out; }
  if (!(doc = doc_new((i32)w, (i32)h, 0, 1))) { codec_oom = 1; codec_err(err, errcap, "Not enough memory for the Krita picture"); goto out; }
  layer_free(doc->lay[0]);
  doc->nlay = 0;
  if (dpi > 0 && dpi < 65536) doc->dpi = (u16)dpi;
  /* the tree is listed top first, each group before its members: taken backwards (as for GIMP) */
  for (i = nn - 1; i >= 0 && doc->nlay < MAXLAYERS; i--) {
    KNode *k = &nodes[i];
    Layer *l;
    char path[200];
    if (k->group) {
      if (!(l = (Layer *)calloc(1, sizeof *l))) { codec_oom = 1; goto oom; }
      l->kind = LK_GROUP;
      l->collapsed = k->collapsed;
    } else if (!(l = layer_new(doc, "Layer"))) { codec_oom = 1; goto oom; }
    doc->lay[doc->nlay++] = l;
    cpy(l->name, sizeof l->name, k->name[0] ? k->name : k->group ? "Group" : "Layer");
    l->depth = k->depth; l->opacity = k->opacity; l->visible = k->visible; l->locked = k->locked;
    l->blend = (u8)(k->group && k->pass ? BM_PASS : k->blend);
    if (k->group) continue;
    {
      int bps, kind = cs_kind(k->cs, &bps);
      size_t len;
      u8 *td;
      snprintf(path, sizeof path, "%s/layers/%s", iname, k->file);
      default_pixel(&z, path, kind, bps, l->px, (i32)w, (i32)h);
      if ((td = entry(&z, path, &len)) != NULL) {
        int ok = read_tiles(td, len, kind, bps, k->x, k->y, l->px, (i32)w, (i32)h, err, errcap);
        free(td);
        if (!ok) goto fail;
      }
      if (k->mask[0]) {
        int mb = strstr(k->maskcs, "16") ? 2 : 1;
        if (!(l->mask = (u8 *)malloc((size_t)w * h))) { codec_oom = 1; goto oom; }
        memset(l->mask, 255, (size_t)w * h);
        snprintf(path, sizeof path, "%s/layers/%s", iname, k->mask);
        default_pixel(&z, path, 0, mb, l->mask, (i32)w, (i32)h);
        if ((td = entry(&z, path, &len)) != NULL) {
          int ok = read_tiles(td, len, 0, mb, k->mx, k->my, l->mask, (i32)w, (i32)h, err, errcap);
          free(td);
          if (!ok) goto fail;
        }
        l->mask_on = 1; l->mask_linked = 1;
      }
    }
    if (progress_aborted) goto fail;
  }
  doc->active = doc->nlay - 1;
  goto out;
oom:
  codec_err(err, errcap, "Not enough memory for the Krita file's layers");
fail:
  doc_free(doc);
  doc = NULL;
out:
  free(nodes);
  zip_close(&z);
  return doc;
}
