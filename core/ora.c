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
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "codec.h"
#include "zip.h"
#include "xml.h"

/* the blend modes by BM_ value; svg:subtract doesn't exist, Krita calls it krita:subtract */
static const char *const op_names[BM_N] = { "svg:src-over", "svg:multiply", "svg:screen", "svg:overlay", "svg:darken", "svg:lighten",
  "svg:difference", "svg:plus", "krita:subtract", "svg:color-dodge", "svg:color-burn", "svg:soft-light", "svg:hard-light",
  "svg:exclusion", "svg:hue", "svg:saturation", "svg:color", "svg:luminosity" };
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
typedef struct { char name[64]; char src[256]; i32 x, y; int opacity, visible, blend, locked, alpha_lock, selected; } OraLayer;

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

/* parse stack.xml into the image size, resolution and the layer list (topmost first). Groups
   (<stack> inside <stack>) keep a running opacity and visibility for their children */
static int parse_stack(const char *s, size_t n, i32 *w, i32 *h, int *dpi, OraLayer *ls, int max)
{
  Xml x;
  int depth = 0, cnt = 0, gop[64], gvis[64];
  xml_init(&x, s, n);
  gop[0] = 255; gvis[0] = 1;
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START && !strcmp(x.name, "image")) {
      const char *a;
      if ((a = xml_attr(&x, "w"))) *w = (i32)atol(a);
      if ((a = xml_attr(&x, "h"))) *h = (i32)atol(a);
      if ((a = xml_attr(&x, "xres"))) *dpi = atoi(a);
    } else if (x.type == XT_START && !strcmp(x.name, "stack")) {
      int op = depth ? unit255(xml_attr(&x, "opacity")) : 255;
      const char *v = depth ? xml_attr(&x, "visibility") : NULL;
      if (depth < 63) {
        gop[depth + 1] = (gop[depth] * op + 127) / 255;
        gvis[depth + 1] = gvis[depth] && !(v && !strcmp(v, "hidden"));
      }
      depth++;
    } else if (x.type == XT_END && !strcmp(x.name, "stack")) {
      if (depth > 0) depth--;
    } else if (x.type == XT_START && !strcmp(x.name, "layer") && cnt < max) {
      OraLayer *l = &ls[cnt];
      const char *a;
      int g = depth < 64 ? depth : 63;
      memset(l, 0, sizeof *l);
      if (!(a = xml_attr(&x, "src"))) continue;
      copy_name(l->src, sizeof l->src, a);
      copy_name(l->name, sizeof l->name, (a = xml_attr(&x, "name")) ? a : "Layer");
      l->x = clamp_pos(xml_attr(&x, "x"));
      l->y = clamp_pos(xml_attr(&x, "y"));
      l->opacity = (unit255(xml_attr(&x, "opacity")) * gop[g] + 127) / 255;
      a = xml_attr(&x, "visibility");
      l->visible = gvis[g] && !(a && !strcmp(a, "hidden"));
      l->blend = ora_op_mode(xml_attr(&x, "composite-op"));
      l->locked = is_true(xml_attr(&x, "edit-locked"));
      l->alpha_lock = is_true(xml_attr(&x, "alpha-preserve"));
      l->selected = is_true(xml_attr(&x, "selected"));
      cnt++;
    }
  }
  xml_free(&x);
  return cnt;
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
  if ((i = zip_find(&z, "stack.xml")) >= 0 && (xml = zip_read(&z, i, &xlen))) {
    cnt = parse_stack((const char *)xml, xlen, &w, &h, &dpi, ls, MAXLAYERS);
    free(xml);
  }
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
      Layer *l = layer_new(doc, o->name);
      if (!l) { codec_err(err, errcap, "Out of memory for the layers"); doc_free(doc); doc = NULL; goto fail; }
      l->opacity = (u8)o->opacity; l->visible = (u8)o->visible; l->blend = (u8)o->blend;
      l->locked = (u8)o->locked; l->alpha_lock = (u8)o->alpha_lock;
      /* a layer whose picture is missing or damaged stays empty rather than losing the rest */
      if (read_picture(&z, o->src, &im)) {
        for (y = 0; y < im.h; y++) {
          i32 cy = o->y + y, x0 = o->x < 0 ? -o->x : 0, x1 = o->x + im.w > w ? w - o->x : im.w;
          if (cy < 0 || cy >= h || x1 <= x0) continue;
          memcpy(l->px + ((size_t)cy * w + o->x + x0) * 4, im.px + ((size_t)y * im.w + x0) * 4, (size_t)(x1 - x0) * 4);
        }
        rgba_free(&im);
      }
      if (o->selected) doc->active = doc->nlay;
      doc->lay[doc->nlay++] = l;
    }
    for (i = 0; i < cnt; i++) any |= ls[i].selected;
    if (!any) doc->active = doc->nlay - 1;
  }
  if (dpi > 0 && dpi < 65536) doc->dpi = (u16)dpi;
  doc_forget_undo(doc);
  doc->dirty = 0;
fail:
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

/* mimetype first and stored, then the layer PNGs (stored: deflating them again gains nothing),
   stack.xml, the merged image and the thumbnail */
u8 *ora_write(Doc *doc, size_t *len)
{
  ZipWriter zw;
  Buf xml;
  Rgba merged, thumb, part;
  int i, ok = 1, dpi = doc->dpi ? doc->dpi : 72;
  u8 *png;
  size_t plen;
  char name[32];
  zip_begin(&zw);
  buf_init(&xml);
  merged.px = thumb.px = NULL;
  ok = zip_add_stored(&zw, "mimetype", "image/openraster", 16);
  buf_printf(&xml, "<?xml version='1.0' encoding='UTF-8'?>\n<image version=\"0.0.3\" w=\"%ld\" h=\"%ld\" xres=\"%d\" yres=\"%d\">\n<stack>\n",
             (long)doc->w, (long)doc->h, dpi, dpi);
  for (i = doc->nlay - 1; ok && i >= 0; i--) {
    Layer *l = doc->lay[i];
    Box b = opaque_box(l, doc->w, doc->h);
    i32 y;
    if (box_empty(b)) b = box_make(0, 0, 1, 1);
    if (!rgba_alloc(&part, b.x1 - b.x0, b.y1 - b.y0)) { ok = 0; break; }
    for (y = b.y0; y < b.y1; y++)
      memcpy(part.px + (size_t)(y - b.y0) * part.w * 4, l->px + ((size_t)y * doc->w + b.x0) * 4, (size_t)part.w * 4);
    png = png_encode(&part, 0, &plen);
    rgba_free(&part);
    sprintf(name, "data/layer%d.png", i);
    if (!png || !zip_add_stored(&zw, name, png, plen)) { free(png); ok = 0; break; }
    free(png);
    buf_adds(&xml, "<layer name=\"");
    xml_escape(&xml, l->name, strlen(l->name));
    buf_printf(&xml, "\" src=\"%s\" x=\"%ld\" y=\"%ld\" opacity=\"", name, (long)b.x0, (long)b.y0);
    put_unit(&xml, l->opacity);
    buf_printf(&xml, "\" visibility=\"%s\" composite-op=\"%s\"", l->visible ? "visible" : "hidden", ora_op_name(l->blend));
    if (l->locked) buf_adds(&xml, " edit-locked=\"true\"");
    if (l->alpha_lock) buf_adds(&xml, " alpha-preserve=\"true\"");
    if (i == doc->active) buf_adds(&xml, " selected=\"true\"");
    buf_adds(&xml, "/>\n");
  }
  buf_adds(&xml, "</stack>\n</image>\n");
  if (ok) ok = !xml.oom && zip_add(&zw, "stack.xml", xml.data, xml.len);
  /* the merged picture and its thumbnail (at most 256 pixels each way) */
  if (ok) ok = rgba_alloc(&merged, doc->w, doc->h);
  if (ok) {
    doc_composite(doc, doc_box(doc), merged.px, (long)doc->w * 4);
    png = png_encode(&merged, dpi, &plen);
    ok = png && zip_add_stored(&zw, "mergedimage.png", png, plen);
    free(png);
  }
  if (ok) ok = rgba_thumbnail(&merged, 256, &thumb);
  if (ok) {
    png = png_encode(&thumb, 0, &plen);
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
