/*
 * ora.c - OpenRaster (.ora), the open layered format GIMP, Krita and MyPaint share: a zip holding
 * "mimetype" (stored, first), stack.xml (the layers, topmost first, with name, position, opacity,
 * visibility and blend mode), a PNG per layer, the merged picture and a thumbnail.
 *
 * Reading keeps each layer's name, opacity, visibility, blend mode and lock; layers smaller than
 * the canvas are placed at their x, y. Nested <stack> groups are flattened: their children become
 * ordinary layers with the group's opacity multiplied in (and hidden if the group is), and the
 * group's own blend mode is ignored, since Easel has no groups. Past MAXLAYERS the bottom layers
 * are dropped. A file without a usable stack.xml still opens from its merged image.
 *
 * Writing crops each layer to the pixels it really has, so mostly empty layers stay small.
 * Easel's layer lock goes in the spec's edit-locked attribute, its alpha lock in alpha-preserve
 * (as Krita does), and the active layer is marked selected.
 *
 * 0.7: Easel's alpha channels are kept as grey PNGs (data/channelN.png), each listed in an
 * <easel:channel src="..." name="..."/> element after the root stack, which other programs ignore.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "codec.h"
#include "adjust.h"
#include "zip.h"
#include "xml.h"
#include "live.h"                     /* 0.9 */
#include "layerx.h"                   /* 1.2 */
#include "toolsx.h"
#include "path.h"
#include "fx.h"                       /* 1.1 */

/* the blend modes by BM_ value; svg:subtract doesn't exist, Krita calls it krita:subtract */
static const char *const op_names[BM_N] = { "svg:src-over", "svg:multiply", "svg:screen", "svg:overlay", "svg:darken", "svg:lighten",
  "svg:difference", "svg:plus", "krita:subtract", "svg:color-dodge", "svg:color-burn", "svg:soft-light", "svg:hard-light",
  "svg:exclusion", "svg:hue", "svg:saturation", "svg:color", "svg:luminosity",
  "krita:dissolve", "krita:linear_burn", "krita:darker color", "krita:lighter color", "krita:vivid_light",
  "krita:linear light", "krita:pin_light", "krita:hard mix", "krita:divide",
  "krita:reflect", "krita:glow", "krita:negation", "krita:xor", "easel:pdn-color-burn", "easel:pdn-color-dodge" };
const char *ora_op_name(int mode) { return mode >= 0 && mode < BM_N ? op_names[mode] : op_names[0]; }
int ora_op_mode(const char *name)
{
  int i;
  if (!name) return BM_NORMAL;
  for (i = 0; i < BM_N; i++) if (!strcmp(name, op_names[i])) return i;
  if (!strcmp(name, "svg:subtract")) return BM_SUBTRACT;
  if (!strcmp(name, "svg:add") || !strcmp(name, "krita:linear_dodge")) return BM_ADD;
  return BM_NORMAL;
}

/* ------------------------------------------------------------------ reading */
/* one entry of stack.xml in document order (topmost first): a layer, or a group's header (its
   members follow it, one deeper). Easel's own settings come in easel: attributes */
typedef struct {
  char name[64]; char src[256]; char mask[256];
  i32 x, y, mx, my;
  int opacity, visible, blend, locked, alpha_lock, selected;
  int kind, depth, isolate, collapsed, clip, fill, lock_pos, lock_all;
  int mask_on, mask_linked, mask_fade, mask_feather;
  int blendif[4];
  AdjParams adj; FillParams fillp;
  char *live;                         /* 0.9: a type or shape layer's settings (easel:text, easel:shape), malloc'd */
  LayerFx fx; int label;              /* 1.1: the layer style (easel:effects) and colour label (easel:label) */
  char *xnums, *xvpath, *xspath; char xpat[64];   /* 1.2: layerx.h's settings (easel:layerx, -path, -smart, -pattern) */
  int link, artboard; Box abox; long abg;          /* 1.2: easel:link, easel:artboard */
} OraLayer;

/* a 0-1 attribute as 0-255 (absent: 255) */
static int unit255(const char *s)
{
  double v;
  if (!s) return 255;
  v = strtod(s, NULL);
  if (!(v > 0)) return 0;                                /* also catches NaN */
  if (v >= 1) return 255;
  return (int)(v * 255 + 0.5);
}
/* a layer offset, kept where sums with a width can't overflow */
static i32 clamp_pos(const char *s)
{
  long v = s ? atol(s) : 0;
  return (i32)(v < -MAXDIM ? -MAXDIM : v > MAXDIM ? MAXDIM : v);
}
static int is_true(const char *s) { return s && (!strcmp(s, "true") || !strcmp(s, "1")); }
/* copy a UTF-8 name into cap bytes, cutting only between characters */
static void copy_name(char *dst, size_t cap, const char *s)
{
  size_t l = strlen(s);
  if (l >= cap) { l = cap - 1; while (l > 0 && ((u8)s[l] & 0xC0) == 0x80) l--; }
  memcpy(dst, s, l);
  dst[l] = 0;
}
/* a picture inside the archive, decoded (a nested OpenRaster is refused) */
static int read_picture(ZipReader *z, const char *name, Rgba *im)
{
  int i = zip_find(z, name), ok = 0;
  u8 *p;
  size_t len;
  im->w = im->h = 0; im->px = NULL;
  if (i < 0 || !(p = zip_read(z, i, &len))) return 0;
  if (codec_detect(p, len) != FMT_ORA) ok = codec_decode(p, len, im, NULL, 0);
  free(p);
  return ok;
}
/* up to n whitespace-separated integers from s into v; how many */
static int read_ints(const char *s, int *v, int n)
{
  int k = 0;
  char *e;
  while (s && *s && k < n) {
    long x = strtol(s, &e, 10);
    if (e == s) break;
    v[k++] = (int)(x < -100000000L ? -100000000L : x > 100000000L ? 100000000L : x);
    s = e;
  }
  return k;
}
/* the attributes a layer and a group share */
static void common_attrs(Xml *x, OraLayer *l)
{
  const char *a;
  int v[48], n, k;
  copy_name(l->name, sizeof l->name, (a = xml_attr(x, "name")) ? a : (l->kind == LK_GROUP ? "Group" : "Layer"));
  l->x = clamp_pos(xml_attr(x, "x"));
  l->y = clamp_pos(xml_attr(x, "y"));
  l->opacity = unit255(xml_attr(x, "opacity"));
  a = xml_attr(x, "visibility");
  l->visible = !(a && !strcmp(a, "hidden"));
  l->blend = ora_op_mode(xml_attr(x, "composite-op"));
  l->locked = is_true(xml_attr(x, "edit-locked"));
  l->alpha_lock = is_true(xml_attr(x, "alpha-preserve"));
  l->selected = is_true(xml_attr(x, "selected"));
  l->clip = is_true(xml_attr(x, "clip"));
  l->fill = (a = xml_attr(x, "fill")) ? unit255(a) : 255;
  l->lock_pos = is_true(xml_attr(x, "lock-position"));
  l->lock_all = is_true(xml_attr(x, "lock-all"));
  if ((a = xml_attr(x, "effects"))) fx_load(&l->fx, a);                       /* 1.1 */
  if ((a = xml_attr(x, "label"))) { l->label = atoi(a); if (l->label < 0 || l->label > 11) l->label = 0; }
  if ((a = xml_attr(x, "blend-if")) && read_ints(a, v, 4) == 4)
    for (k = 0; k < 4; k++) l->blendif[k] = v[k] < 0 ? 0 : v[k] > 255 ? 255 : v[k];
  if ((a = xml_attr(x, "mask"))) {
    copy_name(l->mask, sizeof l->mask, a);
    l->mx = clamp_pos(xml_attr(x, "mask-x"));
    l->my = clamp_pos(xml_attr(x, "mask-y"));
    l->mask_on = !((a = xml_attr(x, "mask-enabled")) && !is_true(a));
    l->mask_linked = !((a = xml_attr(x, "mask-linked")) && !is_true(a));
    l->mask_fade = (a = xml_attr(x, "mask-density")) ? 255 - unit255(a) : 0;
    l->mask_feather = (a = xml_attr(x, "mask-feather")) ? atoi(a) : 0;
    if (l->mask_feather < 0) l->mask_feather = 0;
    if (l->mask_feather > 1000) l->mask_feather = 1000;
  }
  if ((a = xml_attr(x, "adjust"))) {
    n = read_ints(a, v, 48);
    if (n >= 2 && v[0] >= 0 && v[0] < AK_N) {
      l->kind = LK_ADJUST;
      l->adj.kind = (u8)v[0];
      for (k = 0; k < 16 && 2 + k < n; k++) l->adj.v[k] = v[2 + k];
      l->adj.npts = v[1] < 0 ? 0 : v[1] > 32 ? 32 : v[1];
    }
    if ((a = xml_attr(x, "adjust-points"))) {
      int pts[64];
      int m = read_ints(a, pts, 64);
      memcpy(l->adj.pts, pts, (size_t)m * sizeof(int));
      if (l->adj.npts * 2 > m) l->adj.npts = m / 2;
    }
  }
  if ((a = xml_attr(x, "layerx"))) l->xnums = str_dup(a);                        /* 1.2 */
  if ((a = xml_attr(x, "layerx-path"))) l->xvpath = str_dup(a);
  if ((a = xml_attr(x, "layerx-smart"))) l->xspath = str_dup(a);
  if ((a = xml_attr(x, "layerx-pattern"))) copy_name(l->xpat, sizeof l->xpat, a);
  if ((a = xml_attr(x, "link"))) l->link = atoi(a) & 0xFFFF;
  if ((a = xml_attr(x, "artboard")) && read_ints(a, v, 6) == 6) {
    l->artboard = v[0] == 2 ? 2 : 1; l->abox = box_make(v[1], v[2], v[3], v[4]); l->abg = v[5];
  }
  if ((a = xml_attr(x, "fill-layer")) && read_ints(a, v, 7) == 7) {
    l->kind = LK_FILL;
    l->fillp.kind = (u8)(v[0] == 2 ? 2 : v[0] != 0); l->fillp.c0 = (u32)v[1] & 0xFFFFFF; l->fillp.c1 = (u32)v[2] & 0xFFFFFF;
    l->fillp.gkind = v[3] < 0 || v[3] > 4 ? 0 : v[3]; l->fillp.angle = v[4] % 360; l->fillp.scale = v[5] < 1 ? 100 : v[5] > 1000 ? 1000 : v[5];
    l->fillp.reverse = v[6] != 0;
  }
}

/* parse stack.xml into the image size, resolution and the entries (topmost first). A <stack> inside
   the root is a group: its header comes first, its members after it one deeper */
static int parse_stack(const char *s, size_t n, i32 *w, i32 *h, int *dpi, OraLayer *ls, int max)
{
  Xml x;
  int depth = 0, cnt = 0;
  xml_init(&x, s, n);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START && !strcmp(x.name, "image")) {
      const char *a;
      if ((a = xml_attr(&x, "w"))) *w = (i32)atol(a);
      if ((a = xml_attr(&x, "h"))) *h = (i32)atol(a);
      if ((a = xml_attr(&x, "xres"))) *dpi = atoi(a);
    } else if (x.type == XT_START && !strcmp(x.name, "stack")) {
      if (depth > 0 && cnt < max && depth < 200) {
        OraLayer *l = &ls[cnt];
        const char *a;
        memset(l, 0, sizeof *l);
        l->kind = LK_GROUP; l->depth = depth - 1;
        common_attrs(&x, l);
        a = xml_attr(&x, "isolation");
        l->isolate = !(a && !strcmp(a, "auto")) || l->blend != BM_NORMAL || l->opacity != 255;
        if (a && !strcmp(a, "auto") && l->blend == BM_NORMAL) l->isolate = 0;
        l->collapsed = is_true(xml_attr(&x, "collapsed"));
        cnt++;
      }
      depth++;
    } else if (x.type == XT_END && !strcmp(x.name, "stack")) {
      if (depth > 0) depth--;
    } else if (x.type == XT_START && !strcmp(x.name, "layer") && cnt < max) {
      OraLayer *l = &ls[cnt];
      const char *a;
      memset(l, 0, sizeof *l);
      if (!(a = xml_attr(&x, "src"))) continue;
      copy_name(l->src, sizeof l->src, a);
      l->kind = LK_PIXEL; l->depth = depth > 0 ? depth - 1 : 0;
      common_attrs(&x, l);
      /* 0.9: type and shape layers */
      if ((a = xml_attr(&x, "text")) != NULL && (l->live = str_dup(a)) != NULL) l->kind = LK_TEXT;
      else if ((a = xml_attr(&x, "shape")) != NULL && (l->live = str_dup(a)) != NULL) l->kind = LK_SHAPE;
      cnt++;
    }
  }
  xml_free(&x);
  return cnt;
}

/* 0.7: the alpha channels listed in stack.xml (easel:channel elements), read into the document */
static void read_channels(ZipReader *z, const char *s, size_t n, Doc *doc)
{
  Xml x;
  Rgba im;
  xml_init(&x, s, n);
  while (xml_next(&x) != XT_EOF && doc->nchan < MAXCHANNELS) {
    const char *src, *nm;
    u8 *p;
    i32 y, xx;
    if (x.type != XT_START || strcmp(x.name, "channel") || !(src = xml_attr(&x, "src"))) continue;
    if (!(p = (u8 *)calloc((size_t)doc->w * doc->h, 1))) break;
    if (read_picture(z, src, &im)) {
      for (y = 0; y < im.h && y < doc->h; y++)
        for (xx = 0; xx < im.w && xx < doc->w; xx++) p[(size_t)y * doc->w + xx] = im.px[((size_t)y * im.w + xx) * 4];
      rgba_free(&im);
    }
    nm = xml_attr(&x, "name");
    copy_name(doc->chan_name[doc->nchan], sizeof doc->chan_name[0], nm ? nm : "Alpha");
    doc->chan[doc->nchan++] = p;
  }
  xml_free(&x);
}

/* 0.9: the paths (easel:path elements: SVG path data, the anchors' types) read into the document */
static void read_paths(const char *s, size_t n, Doc *doc)
{
  Xml x;
  xml_init(&x, s, n);
  while (xml_next(&x) != XT_EOF) {
    const char *dd, *nm, *a;
    Path *p;
    if (x.type != XT_START || strcmp(x.name, "path") || !(dd = xml_attr(&x, "d"))) continue;
    if (!(p = path_from_svg(dd))) break;
    path_set_types(p, xml_attr(&x, "types"));
    p->evenodd = is_true(xml_attr(&x, "evenodd"));
    if (is_true(xml_attr(&x, "work"))) {
      path_free(doc->workpath); doc->workpath = p;
      if (is_true(xml_attr(&x, "active"))) doc->path_active = -1;
      continue;
    }
    if (doc->npaths >= MAXPATHS) { path_free(p); continue; }
    nm = xml_attr(&x, "name");
    copy_name(doc->path_name[doc->npaths], sizeof doc->path_name[0], nm ? nm : "Path");
    doc->path[doc->npaths++] = p;
    if ((a = xml_attr(&x, "active")) && is_true(a)) doc->path_active = doc->npaths;
  }
  xml_free(&x);
}

/* the zip, stack.xml, then each layer's PNG placed on a canvas-size layer, bottom first */
Doc *ora_read(const u8 *d, size_t n, char *err, size_t errcap)
{
  ZipReader z;
  OraLayer *ls = NULL;
  Doc *doc = NULL;
  Rgba im;
  i32 w = 0, h = 0, y;
  int dpi = 0, cnt = 0, i, k, any = 0;
  u8 *xml;
  size_t xlen;
  if (!zip_open(&z, d, n)) { codec_err(err, errcap, "Not an OpenRaster file (not a zip)"); return NULL; }
  if (!(ls = (OraLayer *)malloc(sizeof(OraLayer) * MAXLAYERS))) { zip_close(&z); codec_err(err, errcap, "Out of memory"); return NULL; }
  xml = NULL; xlen = 0;
  if ((i = zip_find(&z, "stack.xml")) >= 0 && (xml = zip_read(&z, i, &xlen)))
    cnt = parse_stack((const char *)xml, xlen, &w, &h, &dpi, ls, MAXLAYERS);
  if (cnt == 0 || w < 1 || h < 1 || w > MAXDIM || h > MAXDIM || (long)w * h > CODEC_MAXPIXELS) {
    /* no usable layer list: the merged image as the one layer */
    if (!read_picture(&z, "mergedimage.png", &im)) {
      codec_err(err, errcap, cnt ? "Bad OpenRaster canvas size" : "Damaged OpenRaster file (no stack.xml or merged image)");
      goto fail;
    }
    if (!(doc = doc_new(im.w, im.h, 0xFFFFFF, 0))) { rgba_free(&im); codec_err(err, errcap, "Out of memory"); goto fail; }
    free(doc->lay[0]->px);
    doc->lay[0]->px = im.px;
  } else {
    if (!(doc = doc_new(w, h, 0, 1))) { codec_err(err, errcap, "Out of memory"); goto fail; }
    layer_free(doc->lay[0]);
    doc->nlay = 0;
    for (k = cnt - 1; k >= 0; k--) {
      OraLayer *o = &ls[k];
      Layer *l;
      if (o->kind == LK_PIXEL || o->kind == LK_FILL || o->kind == LK_TEXT || o->kind == LK_SHAPE) l = layer_new(doc, o->name);
      else if ((l = (Layer *)calloc(1, sizeof *l)) != NULL) copy_name(l->name, sizeof l->name, o->name);
      if (!l) { codec_err(err, errcap, "Out of memory for the layers"); doc_free(doc); doc = NULL; goto fail; }
      l->kind = (u8)o->kind; l->depth = (u8)o->depth;
      l->opacity = (u8)o->opacity; l->visible = (u8)o->visible; l->blend = (u8)o->blend;
      l->locked = (u8)o->locked; l->alpha_lock = (u8)o->alpha_lock;
      l->clip = (u8)(o->clip && o->kind != LK_GROUP); l->fill_cut = (u8)(255 - o->fill);
      l->lock_pos = (u8)o->lock_pos; l->lock_all = (u8)o->lock_all; l->collapsed = (u8)o->collapsed;
      for (i = 0; i < 4; i++) l->blendif[i] = (u8)o->blendif[i];
      l->fx = o->fx; l->label = (u8)o->label;                /* 1.1 */
      if (o->kind == LK_GROUP && !o->isolate) l->blend = BM_PASS;
      if (o->kind == LK_ADJUST) l->adj = o->adj;
      if (o->kind == LK_FILL) { l->fillp = o->fillp; free(l->px); l->px = NULL; }
      /* 1.2: links, artboards, and layerx.h's settings (with their pattern) */
      l->link = (u16)o->link;
      if (o->artboard && o->kind == LK_GROUP) { l->artboard = (u8)o->artboard; l->abox = box_clip(o->abox, box_make(0, 0, w, h)); l->fillp.c0 = (u32)o->abg & 0xFFFFFF; }
      if (o->xnums) {
        Rgba pim;
        int hp = o->xpat[0] && read_picture(&z, o->xpat, &pim);
        layerx_from_text(l, o->xnums, o->xvpath, o->xspath, hp ? pim.px : NULL, hp ? pim.w : 0, hp ? pim.h : 0);
        if (hp) rgba_free(&pim);
      }
      /* a layer whose picture is missing or damaged stays empty rather than losing the rest */
      if (l->px && read_picture(&z, o->src, &im)) {
        for (y = 0; y < im.h && !progress(y, im.h); y++) {
          i32 cy = o->y + y, x0 = o->x < 0 ? -o->x : 0, x1 = o->x + im.w > w ? w - o->x : im.w;
          if (cy < 0 || cy >= h || x1 <= x0) continue;
          memcpy(l->px + ((size_t)cy * w + o->x + x0) * 4, im.px + ((size_t)y * im.w + x0) * 4, (size_t)(x1 - x0) * 4);
        }
        rgba_free(&im);
      }
      /* 0.9: a type or shape layer: its settings, and the picture kept as its rendering until they change */
      if ((o->kind == LK_TEXT || o->kind == LK_SHAPE) && l->px) {
        if (live_load(l, o->kind, o->live)) {
          Box b = box_make(w, h, 0, 0);
          i32 xx;
          for (y = 0; y < h; y++) for (xx = 0; xx < w; xx++) if (l->px[((size_t)y * w + xx) * 4 + 3]) b = box_union(b, box_make(xx, y, xx + 1, y + 1));
          if (b.x0 > b.x1 || box_empty(b)) { l->live_ok = 0; }
          else if ((l->livepx = (u8 *)malloc((size_t)(b.x1 - b.x0) * (b.y1 - b.y0) * 4)) != NULL) {
            for (y = b.y0; y < b.y1; y++) memcpy(l->livepx + (size_t)(y - b.y0) * (b.x1 - b.x0) * 4, l->px + ((size_t)y * w + b.x0) * 4, (size_t)(b.x1 - b.x0) * 4);
            l->livebox = b; l->live_ok = 1;
          }
          free(l->px); l->px = NULL;
        } else l->kind = LK_PIXEL;
      }
      /* the mask: grey (the red channel) placed on a canvas-size mask, white outside the picture */
      if (o->mask[0] && (l->mask = (u8 *)malloc((size_t)w * h)) != NULL) {
        memset(l->mask, 255, (size_t)w * h);
        if (read_picture(&z, o->mask, &im)) {
          for (y = 0; y < im.h; y++) {
            i32 cy = o->my + y, x;
            if (cy < 0 || cy >= h) continue;
            for (x = 0; x < im.w; x++) {
              i32 cx = o->mx + x;
              if (cx >= 0 && cx < w) l->mask[(size_t)cy * w + cx] = im.px[((size_t)y * im.w + x) * 4];
            }
          }
          rgba_free(&im);
        }
        l->mask_on = (u8)o->mask_on; l->mask_linked = (u8)o->mask_linked;
        l->mask_fade = (u8)o->mask_fade; l->mask_feather = (u16)o->mask_feather;
      }
      if (o->selected) doc->active = doc->nlay;
      doc->lay[doc->nlay++] = l;
    }
    for (i = 0; i < cnt; i++) any |= ls[i].selected;
    if (!any) doc->active = doc->nlay - 1;
    /* depths made consistent: a member can't be deeper than one below its group */
    for (i = doc->nlay - 1, k = 0; i >= 0; i--) {
      if (doc->lay[i]->depth > k) doc->lay[i]->depth = (u8)k;
      k = doc->lay[i]->depth + (doc->lay[i]->kind == LK_GROUP);
    }
  }
  if (dpi > 0 && dpi < 65536) doc->dpi = (u16)dpi;
  if (xml) read_channels(&z, (const char *)xml, xlen, doc);          /* 0.7 */
  if (xml) read_paths((const char *)xml, xlen, doc);                  /* 0.9 */
  if (xml) annots_read_xml(doc, (const char *)xml, xlen);             /* 1.2: notes, counts, slices */
  doc_forget_undo(doc);
  doc->dirty = 0;
fail:
  for (i = 0; ls && i < cnt; i++) { free(ls[i].live); free(ls[i].xnums); free(ls[i].xvpath); free(ls[i].xspath); }   /* 0.9, 1.2 */
  free(xml);
  free(ls);
  zip_close(&z);
  return doc;
}

/* ------------------------------------------------------------------ writing */
/* opacity 0-255 as a decimal 0-1 with three places (enough to come back to the same value) */
static void put_unit(Buf *b, int v)
{
  int m = (v * 1000 + 127) / 255;
  buf_printf(b, "%d.%03d", m / 1000, m % 1000);
}
/* the part of a layer that has any alpha, as a box; empty if the layer is clear */
static Box opaque_box(const Layer *l, i32 w, i32 h)
{
  i32 x, y, x0 = w, y0 = h, x1 = 0, y1 = 0;
  for (y = 0; y < h; y++) {
    const u8 *p = l->px + (size_t)y * w * 4;
    for (x = 0; x < w; x++) if (p[x * 4 + 3]) { if (x < x0) x0 = x; if (x >= x1) x1 = x + 1; if (y < y0) y0 = y; y1 = y + 1; }
  }
  return box_make(x0, y0, x1, y1);
}

/* a canvas-size picture (pixels, or a mask as grey) cropped to box b, encoded and stored as name */
/* 0.8: the pictures written so far and how many in all, for progress (each PNG a share) */
static int ora_k, ora_n;
static int store_png(ZipWriter *zw, const char *name, const u8 *px, const u8 *mask, i32 w, Box b)
{
  Rgba part;
  u8 *png;
  size_t plen;
  i32 y, x;
  int ok;
  long pp;
  if (!rgba_alloc(&part, b.x1 - b.x0, b.y1 - b.y0)) return 0;
  pp = progress_push(ora_n ? 1000L * ora_k / ora_n : 0, ora_n ? 1000L * (ora_k + 1) / ora_n : 1000);
  if (ora_k < ora_n) ora_k++;
  for (y = b.y0; y < b.y1; y++) {
    if (px) memcpy(part.px + (size_t)(y - b.y0) * part.w * 4, px + ((size_t)y * w + b.x0) * 4, (size_t)part.w * 4);
    else for (x = b.x0; x < b.x1; x++) {
      u8 *q = part.px + ((size_t)(y - b.y0) * part.w + x - b.x0) * 4;
      q[0] = q[1] = q[2] = mask[(size_t)y * w + x]; q[3] = 255;
    }
  }
  png = png_encode(&part, 0, &plen);
  progress_pop(pp);
  rgba_free(&part);
  ok = png && zip_add_stored(zw, name, png, plen);
  free(png);
  return ok;
}
/* the settings a layer and a group share, as attributes */
static void put_common(Buf *xml, ZipWriter *zw, Doc *doc, int i, int *ok)
{
  Layer *l = doc->lay[i];
  char name[48];
  buf_adds(xml, " name=\"");
  xml_escape(xml, l->name, strlen(l->name));
  buf_adds(xml, "\" opacity=\"");
  put_unit(xml, l->opacity);
  buf_printf(xml, "\" visibility=\"%s\"", l->visible ? "visible" : "hidden");
  if (l->kind != LK_GROUP || l->blend != BM_PASS) buf_printf(xml, " composite-op=\"%s\"", ora_op_name(l->blend));
  if (l->locked) buf_adds(xml, " edit-locked=\"true\"");
  if (l->alpha_lock) buf_adds(xml, " alpha-preserve=\"true\"");
  if (i == doc->active) buf_adds(xml, " selected=\"true\"");
  if (l->clip) buf_adds(xml, " easel:clip=\"true\"");
  if (l->fill_cut) { buf_adds(xml, " easel:fill=\""); put_unit(xml, 255 - l->fill_cut); buf_adds(xml, "\""); }
  if (l->lock_pos) buf_adds(xml, " easel:lock-position=\"true\"");
  if (l->lock_all) buf_adds(xml, " easel:lock-all=\"true\"");
  if (l->label) buf_printf(xml, " easel:label=\"%d\"", l->label);                 /* 1.1 */
  if (fx_has(l)) {
    char *t = fx_save(&l->fx);
    if (t) { buf_adds(xml, " easel:effects=\""); xml_escape(xml, t, strlen(t)); buf_adds(xml, "\""); free(t); }
  }
  if (l->blendif[0] || l->blendif[1] || l->blendif[2] || l->blendif[3])
    buf_printf(xml, " easel:blend-if=\"%d %d %d %d\"", l->blendif[0], l->blendif[1], l->blendif[2], l->blendif[3]);
  if (l->mask) {
    sprintf(name, "data/mask%d.png", i);
    if (!store_png(zw, name, NULL, l->mask, doc->w, doc_box(doc))) *ok = 0;
    buf_printf(xml, " easel:mask=\"%s\" easel:mask-enabled=\"%s\" easel:mask-linked=\"%s\" easel:mask-density=\"", name,
               l->mask_on ? "true" : "false", l->mask_linked ? "true" : "false");
    put_unit(xml, 255 - l->mask_fade);
    buf_printf(xml, "\" easel:mask-feather=\"%d\"", l->mask_feather);
  }
  /* 1.2 */
  if (l->link) buf_printf(xml, " easel:link=\"%d\"", l->link);
  if (l->artboard) buf_printf(xml, " easel:artboard=\"%d %ld %ld %ld %ld %ld\"", l->artboard, (long)l->abox.x0, (long)l->abox.y0, (long)l->abox.x1, (long)l->abox.y1, (long)(l->fillp.c0 & 0xFFFFFF));
  {
    LayerXText t;
    if (layerx_to_text(l, &t)) {
      buf_adds(xml, " easel:layerx=\""); xml_escape(xml, t.nums, strlen(t.nums)); buf_adds(xml, "\"");
      if (*t.vpath) { buf_adds(xml, " easel:layerx-path=\""); xml_escape(xml, t.vpath, strlen(t.vpath)); buf_adds(xml, "\""); }
      if (*t.spath) { buf_adds(xml, " easel:layerx-smart=\""); xml_escape(xml, t.spath, strlen(t.spath)); buf_adds(xml, "\""); }
      if (t.pat) {
        sprintf(name, "data/pattern%d.png", i);
        if (!store_png(zw, name, t.pat, NULL, t.pw, box_make(0, 0, t.pw, t.ph))) *ok = 0;
        buf_printf(xml, " easel:layerx-pattern=\"%s\"", name);
      }
      layerx_text_free(&t);
    }
  }
}
/* layers lo..hi-1 at depth, topmost first; groups as nested stacks */
static void put_range(Buf *xml, ZipWriter *zw, Doc *doc, int lo, int hi, int *ok)
{
  int i = hi - 1;
  while (*ok && i >= lo) {
    Layer *l = doc->lay[i];
    char name[32];
    if (l->kind == LK_GROUP) {
      int s = i;
      while (s > lo && doc->lay[s - 1]->depth > l->depth) s--;
      buf_adds(xml, "<stack");
      put_common(xml, zw, doc, i, ok);
      buf_printf(xml, " isolation=\"%s\"", l->blend == BM_PASS ? "auto" : "isolate");
      if (l->collapsed) buf_adds(xml, " easel:collapsed=\"true\"");
      buf_adds(xml, ">\n");
      put_range(xml, zw, doc, s, i, ok);
      buf_adds(xml, "</stack>\n");
      i = s - 1;
      continue;
    }
    sprintf(name, "data/layer%d.png", i);
    if (l->kind == LK_PIXEL && l->px) {
      Box b = opaque_box(l, doc->w, doc->h);
      if (box_empty(b)) b = box_make(0, 0, 1, 1);
      if (!store_png(zw, name, l->px, NULL, doc->w, b)) { *ok = 0; break; }
      buf_printf(xml, "<layer src=\"%s\" x=\"%ld\" y=\"%ld\"", name, (long)b.x0, (long)b.y0);
    } else if (l->kind == LK_TEXT || l->kind == LK_SHAPE) {
      /* 0.9: a type or shape layer's rendering (so other programs show it), its settings in an attribute */
      u8 *px = (u8 *)malloc((size_t)doc->w * doc->h * 4);
      u8 op = l->opacity, bl = l->blend, fc = l->fill_cut, vis = l->visible, mon = l->mask_on, cl = l->clip;
      char *set;
      Box b;
      Layer tmp;
      if (!px) { *ok = 0; break; }
      l->opacity = 255; l->blend = BM_NORMAL; l->fill_cut = 0; l->visible = 1; l->mask_on = 0; l->clip = 0;
      doc_composite_range(doc, i, i + 1, l->depth, doc_box(doc), px, (long)doc->w * 4, 0);
      l->opacity = op; l->blend = bl; l->fill_cut = fc; l->visible = vis; l->mask_on = mon; l->clip = cl;
      tmp.px = px;
      b = opaque_box(&tmp, doc->w, doc->h);
      if (box_empty(b)) b = box_make(0, 0, 1, 1);
      if (!store_png(zw, name, px, NULL, doc->w, b)) { free(px); *ok = 0; break; }
      free(px);
      if (!(set = live_save(l))) { *ok = 0; break; }
      buf_printf(xml, "<layer src=\"%s\" x=\"%ld\" y=\"%ld\" easel:%s=\"", name, (long)b.x0, (long)b.y0, l->kind == LK_TEXT ? "text" : "shape");
      xml_escape(xml, set, strlen(set));
      buf_adds(xml, "\"");
      free(set);
    } else if (l->kind == LK_FILL) {
      /* the fill's own pixels, so other programs show it */
      u8 *px = (u8 *)malloc((size_t)doc->w * doc->h * 4);
      u8 op = l->opacity, bl = l->blend, fc = l->fill_cut, vis = l->visible, mon = l->mask_on, cl = l->clip;
      if (!px) { *ok = 0; break; }
      l->opacity = 255; l->blend = BM_NORMAL; l->fill_cut = 0; l->visible = 1; l->mask_on = 0; l->clip = 0;
      doc_composite_range(doc, i, i + 1, l->depth, doc_box(doc), px, (long)doc->w * 4, 0);
      l->opacity = op; l->blend = bl; l->fill_cut = fc; l->visible = vis; l->mask_on = mon; l->clip = cl;
      if (!store_png(zw, name, px, NULL, doc->w, doc_box(doc))) { free(px); *ok = 0; break; }
      free(px);
      buf_printf(xml, "<layer src=\"%s\" x=\"0\" y=\"0\" easel:fill-layer=\"%d %ld %ld %d %d %d %d\"", name, l->fillp.kind,
                 (long)(l->fillp.c0 & 0xFFFFFF), (long)(l->fillp.c1 & 0xFFFFFF), l->fillp.gkind, l->fillp.angle, l->fillp.scale, l->fillp.reverse);
    } else {
      /* an adjustment layer: a clear one-pixel picture (others show nothing), the settings in attributes */
      u8 clear[4] = { 0, 0, 0, 0 };
      int k;
      if (!store_png(zw, name, clear, NULL, 1, box_make(0, 0, 1, 1))) { *ok = 0; break; }
      buf_printf(xml, "<layer src=\"%s\" x=\"0\" y=\"0\" easel:adjust=\"%d %d", name, l->adj.kind, l->adj.npts);
      for (k = 0; k < 16; k++) buf_printf(xml, " %d", l->adj.v[k]);
      buf_adds(xml, "\"");
      if (l->adj.npts > 0 || l->adj.kind == AK_SELECTIVECOLOR) {
        int m = l->adj.kind == AK_SELECTIVECOLOR ? 36 : l->adj.npts * 2;
        buf_adds(xml, " easel:adjust-points=\"");
        for (k = 0; k < m && k < 64; k++) buf_printf(xml, k ? " %d" : "%d", l->adj.pts[k]);
        buf_adds(xml, "\"");
      }
    }
    put_common(xml, zw, doc, i, ok);
    buf_adds(xml, "/>\n");
    i--;
  }
}

/* mimetype first and stored, then the layer PNGs (stored: deflating them again gains nothing),
   stack.xml, the merged image and the thumbnail */
u8 *ora_write(Doc *doc, size_t *len)
{
  ZipWriter zw;
  Buf xml;
  Rgba merged, thumb;
  int ok = 1, dpi = doc->dpi ? doc->dpi : 72;
  u8 *png;
  size_t plen;
  zip_begin(&zw);
  buf_init(&xml);
  merged.px = thumb.px = NULL;
  ora_k = 0; ora_n = doc->nlay + doc->nchan + 2;            /* (0.8: the layers, channels, merged picture and thumbnail) */
  ok = zip_add_stored(&zw, "mimetype", "image/openraster", 16);
  buf_printf(&xml, "<?xml version='1.0' encoding='UTF-8'?>\n<image version=\"0.0.5\" w=\"%ld\" h=\"%ld\" xres=\"%d\" yres=\"%d\" xmlns:easel=\"http://easel.amiga/ora/1\"",
             (long)doc->w, (long)doc->h, dpi, dpi);
  ora_put_info(&xml, &doc->info);                        /* 0.6: File Info, as attributes of <image> */
  buf_adds(&xml, ">\n<stack>\n");
  put_range(&xml, &zw, doc, 0, doc->nlay, &ok);
  buf_adds(&xml, "</stack>\n");
  {
    /* 0.7: the alpha channels */
    int k;
    char name[40];
    for (k = 0; ok && k < doc->nchan; k++) {
      sprintf(name, "data/channel%d.png", k);
      if (!store_png(&zw, name, NULL, doc->chan[k], doc->w, doc_box(doc))) { ok = 0; break; }
      buf_printf(&xml, "<easel:channel src=\"%s\" name=\"", name);
      xml_escape(&xml, doc->chan_name[k], strlen(doc->chan_name[k]));
      buf_adds(&xml, "\"/>\n");
    }
  }
  {
    /* 0.9: the paths */
    int k;
    for (k = -1; ok && k < doc->npaths; k++) {
      Path *p = k < 0 ? doc->workpath : doc->path[k];
      char *dd, *ty;
      if (!p) continue;
      dd = path_to_svg(p); ty = path_types(p);
      if (!dd || !ty) { free(dd); free(ty); ok = 0; break; }
      buf_adds(&xml, "<easel:path name=\"");
      if (k < 0) buf_adds(&xml, "Work Path"); else xml_escape(&xml, doc->path_name[k], strlen(doc->path_name[k]));
      buf_adds(&xml, "\" d=\"");
      xml_escape(&xml, dd, strlen(dd));
      buf_printf(&xml, "\" types=\"%s\"%s%s%s/>\n", ty, k < 0 ? " work=\"true\"" : "", p->evenodd ? " evenodd=\"true\"" : "",
                 doc->path_active == (k < 0 ? -1 : k + 1) ? " active=\"true\"" : "");
      free(dd); free(ty);
    }
  }
  {
    char *an = annots_xml(doc);                            /* 1.2: notes, counts, slices */
    if (an) { buf_adds(&xml, an); free(an); }
  }
  buf_adds(&xml, "</image>\n");
  if (ok) ok = !xml.oom && zip_add(&zw, "stack.xml", xml.data, xml.len);
  /* the merged picture and its thumbnail (at most 256 pixels each way) */
  if (ok) ok = rgba_alloc(&merged, doc->w, doc->h);
  if (ok) {
    long pp = progress_push(1000L * ora_k / ora_n, 1000L * (ora_k + 1) / ora_n);
    doc_composite(doc, doc_box(doc), merged.px, (long)doc->w * 4);
    png = progress_aborted ? NULL : png_encode(&merged, dpi, &plen);
    progress_pop(pp);
    ora_k++;
    ok = png && zip_add_stored(&zw, "mergedimage.png", png, plen);
    free(png);
  }
  if (ok) ok = rgba_thumbnail(&merged, 256, &thumb);
  if (ok) {
    long pp = progress_push(1000L * ora_k / ora_n, 1000);
    png = progress_aborted ? NULL : png_encode(&thumb, 0, &plen);
    progress_pop(pp);
    ok = png && zip_add_stored(&zw, "Thumbnails/thumbnail.png", png, plen);
    free(png);
  }
  rgba_free(&merged); rgba_free(&thumb);
  buf_free(&xml);
  if (!ok || !zip_finish(&zw)) { zip_free(&zw); return NULL; }
  *len = zw.out.len;
  png = (u8 *)buf_take(&zw.out);
  zip_free(&zw);
  return png;
}
