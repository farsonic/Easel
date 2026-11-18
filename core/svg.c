/*
 * svg.c - SVG paths and shapes out and in (Easel 1.1), see svg.h. The SVG 1.1 specification's
 * basic shapes, path data (through path.c's path_from_svg), transforms and painting properties.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "svg.h"
#include "path.h"
#include "shape.h"
#include "xml.h"
#include "codec.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ================================================================== writing */
static void put_path(Buf *b, const Path *p, const char *id, const char *paint)
{
  char *dd = path_to_svg(p);
  if (!dd) { b->oom = 1; return; }
  if (dd[0]) {
    buf_adds(b, "  <path");
    if (id) { buf_adds(b, " id=\""); xml_escape(b, id, strlen(id)); buf_adds(b, "\""); }
    buf_printf(b, " %s d=\"", paint);
    buf_adds(b, dd);
    buf_adds(b, "\"/>\n");
  }
  free(dd);
}
u8 *svg_write_paths(Doc *d, int frame, size_t *len)
{
  Buf b;
  int i;
  buf_init(&b);
  buf_printf(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%ld\" height=\"%ld\" viewBox=\"0 0 %ld %ld\">\n",
             (long)d->w, (long)d->h, (long)d->w, (long)d->h);
  if (frame) buf_printf(&b, "  <rect id=\"Document Bounds\" x=\"0\" y=\"0\" width=\"%ld\" height=\"%ld\" fill=\"none\" stroke=\"none\"/>\n", (long)d->w, (long)d->h);
  for (i = 0; i < d->npaths; i++) if (d->path[i]) put_path(&b, d->path[i], d->path_name[i], "fill=\"none\" stroke=\"#000000\"");
  if (d->workpath) put_path(&b, d->workpath, "Work Path", "fill=\"none\" stroke=\"#000000\"");
  for (i = 0; i < d->nlay; i++) {
    const ShapeParams *sp = layer_shape(d->lay[i]);
    Path p;
    char paint[200];
    if (!sp) continue;
    p = shape_path(sp);
    if (sp->fill_on) snprintf(paint, sizeof paint, "fill=\"#%06lX\"%s", (unsigned long)(sp->fill_color & 0xFFFFFF), sp->evenodd ? " fill-rule=\"evenodd\"" : "");
    else snprintf(paint, sizeof paint, "fill=\"none\"");
    if (sp->fill_on && sp->fill_alpha) snprintf(paint + strlen(paint), sizeof paint - strlen(paint), " fill-opacity=\"%.3f\"", (255 - sp->fill_alpha) / 255.0);
    if (sp->stroke_on) {
      snprintf(paint + strlen(paint), sizeof paint - strlen(paint), " stroke=\"#%06lX\" stroke-width=\"%.3f\"", (unsigned long)(sp->stroke_color & 0xFFFFFF), sp->stroke_width);
      if (sp->stroke_alpha) snprintf(paint + strlen(paint), sizeof paint - strlen(paint), " stroke-opacity=\"%.3f\"", (255 - sp->stroke_alpha) / 255.0);
    }
    if (!d->lay[i]->visible) snprintf(paint + strlen(paint), sizeof paint - strlen(paint), " visibility=\"hidden\"");
    put_path(&b, &p, d->lay[i]->name, paint);
  }
  buf_adds(&b, "</svg>\n");
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return (u8 *)buf_take(&b);
}

/* ================================================================== reading */
int svg_detect(const u8 *d, size_t n)
{
  size_t i, lim = n < 4096 ? n : 4096;
  for (i = 0; i + 4 <= lim; i++) if (d[i] == '<' && !memcmp(d + i + 1, "svg", 3) && (i + 4 == lim || d[i + 4] == ' ' || d[i + 4] == '\n' || d[i + 4] == '\r' || d[i + 4] == '\t' || d[i + 4] == '>')) return 1;
  return 0;
}

/* the painting properties an element has (inherited from its group) */
typedef struct {
  double m[6];                        /* to the canvas */
  int fill_on, stroke_on, evenodd, hidden;
  u32 fill, stroke;
  double stroke_w, opacity, fill_op, stroke_op;
} Style;

static const char *skipsp(const char *s) { while (*s == ' ' || *s == ',' || *s == '\t' || *s == '\n' || *s == '\r') s++; return s; }
/* numbers from s (separated by spaces or commas) into v, at most max; how many */
static int numbers(const char *s, double *v, int max)
{
  int n = 0;
  while (s && n < max) {
    char *e;
    double x;
    s = skipsp(s);
    if (!*s) break;
    x = strtod(s, &e);
    if (e == s) break;
    if (!(x == x) || x > 1e7 || x < -1e7) x = 0;
    v[n++] = x;
    s = e;
  }
  return n;
}
static double num(const char *s, double def) { double v; return s && numbers(s, &v, 1) == 1 ? v : def; }
static void mat_mul(double *m, const double *t)   /* m = m * t */
{
  double r[6];
  r[0] = m[0] * t[0] + m[1] * t[3]; r[1] = m[0] * t[1] + m[1] * t[4]; r[2] = m[0] * t[2] + m[1] * t[5] + m[2];
  r[3] = m[3] * t[0] + m[4] * t[3]; r[4] = m[3] * t[1] + m[4] * t[4]; r[5] = m[3] * t[2] + m[4] * t[5] + m[5];
  memcpy(m, r, sizeof r);
}
static void parse_transform(const char *s, double *m)
{
  static const char *const fn[6] = { "matrix", "translate", "scale", "rotate", "skewX", "skewY" };
  while (s && *s) {
    int k, n;
    double v[6] = { 0, 0, 0, 0, 0, 0 }, t[6] = { 1, 0, 0, 0, 1, 0 };
    const char *p;
    s = skipsp(s);
    for (k = 0; k < 6; k++) if (!strncmp(s, fn[k], strlen(fn[k]))) break;
    if (k == 6 || !(p = strchr(s, '('))) return;
    n = numbers(p + 1, v, 6);
    switch (k) {
    case 0: if (n == 6) { t[0] = v[0]; t[3] = v[1]; t[1] = v[2]; t[4] = v[3]; t[2] = v[4]; t[5] = v[5]; } break;
    case 1: t[2] = v[0]; t[5] = n > 1 ? v[1] : 0; break;
    case 2: t[0] = n ? v[0] : 1; t[4] = n > 1 ? v[1] : t[0]; break;
    case 3: {
      double a = v[0] * M_PI / 180, c = cos(a), sn = sin(a);
      double r[6];
      r[0] = c; r[1] = -sn; r[2] = 0; r[3] = sn; r[4] = c; r[5] = 0;
      if (n >= 3) { double a1[6] = { 1, 0, v[1], 0, 1, v[2] }, a2[6] = { 1, 0, -v[1], 0, 1, -v[2] }; mat_mul(m, a1); mat_mul(m, r); memcpy(t, a2, sizeof t); }
      else memcpy(t, r, sizeof t);
      break;
    }
    case 4: t[1] = tan(v[0] * M_PI / 180); break;
    default: t[3] = tan(v[0] * M_PI / 180); break;
    }
    mat_mul(m, t);
    if (!(s = strchr(p, ')'))) return;
    s++;
  }
}
/* a colour: 1 with *c set, 0 for none, -1 if not understood (left as it was) */
static int parse_color(const char *s, u32 *c)
{
  static const struct { const char *n; u32 c; } names[] = {
    { "black", 0x000000 }, { "white", 0xFFFFFF }, { "red", 0xFF0000 }, { "lime", 0x00FF00 }, { "green", 0x008000 },
    { "blue", 0x0000FF }, { "yellow", 0xFFFF00 }, { "cyan", 0x00FFFF }, { "aqua", 0x00FFFF }, { "magenta", 0xFF00FF },
    { "fuchsia", 0xFF00FF }, { "gray", 0x808080 }, { "grey", 0x808080 }, { "silver", 0xC0C0C0 }, { "maroon", 0x800000 },
    { "olive", 0x808000 }, { "navy", 0x000080 }, { "purple", 0x800080 }, { "teal", 0x008080 }, { "orange", 0xFFA500 } };
  size_t i;
  s = skipsp(s);
  if (!strncmp(s, "none", 4)) return 0;
  if (*s == '#') {
    unsigned long v = 0;
    int n = 0;
    for (s++; n < 6 && ((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f') || (*s >= 'A' && *s <= 'F')); s++, n++)
      v = v * 16 + (unsigned long)(*s <= '9' ? *s - '0' : (*s | 32) - 'a' + 10);
    if (n == 3) { *c = (u32)(((v >> 8) & 15) * 0x110000 + ((v >> 4) & 15) * 0x1100 + (v & 15) * 0x11); return 1; }
    if (n == 6) { *c = (u32)v; return 1; }
    return -1;
  }
  if (!strncmp(s, "rgb(", 4)) {
    double v[3];
    int k;
    u32 r = 0;
    if (numbers(s + 4, v, 3) < 3) return -1;
    for (k = 0; k < 3; k++) { int x = (int)(strchr(s, '%') ? v[k] * 2.55 + 0.5 : v[k]); r = (r << 8) | (u32)(x < 0 ? 0 : x > 255 ? 255 : x); }
    *c = r;
    return 1;
  }
  if (!strncmp(s, "url(", 4)) { *c = 0; return 1; }     /* (gradients and patterns: black) */
  for (i = 0; i < sizeof names / sizeof names[0]; i++) if (!strncmp(s, names[i].n, strlen(names[i].n))) { *c = names[i].c; return 1; }
  return -1;
}
static void set_prop(Style *st, const char *k, const char *v)
{
  u32 c;
  int r;
  if (!strcmp(k, "fill")) { if ((r = parse_color(v, &c)) >= 0) { st->fill_on = r; if (r) st->fill = c; } }
  else if (!strcmp(k, "stroke")) { if ((r = parse_color(v, &c)) >= 0) { st->stroke_on = r; if (r) st->stroke = c; } }
  else if (!strcmp(k, "stroke-width")) st->stroke_w = num(v, st->stroke_w);
  else if (!strcmp(k, "fill-rule")) st->evenodd = !strncmp(skipsp(v), "evenodd", 7);
  else if (!strcmp(k, "opacity")) st->opacity *= num(v, 1);
  else if (!strcmp(k, "fill-opacity")) st->fill_op = num(v, 1);
  else if (!strcmp(k, "stroke-opacity")) st->stroke_op = num(v, 1);
  else if (!strcmp(k, "display")) { if (!strncmp(skipsp(v), "none", 4)) st->hidden = 1; }
  else if (!strcmp(k, "visibility")) st->hidden = !strncmp(skipsp(v), "hidden", 6);
}
static void apply_attrs(Xml *x, Style *st)
{
  static const char *const props[] = { "fill", "stroke", "stroke-width", "fill-rule", "opacity", "fill-opacity", "stroke-opacity", "display", "visibility" };
  const char *t, *sty;
  size_t i;
  if ((t = xml_attr(x, "transform"))) parse_transform(t, st->m);
  for (i = 0; i < sizeof props / sizeof props[0]; i++) if ((t = xml_attr(x, props[i]))) set_prop(st, props[i], t);
  if ((sty = xml_attr(x, "style"))) {
    /* "fill:#f00; stroke:none" */
    char k[32], v[96];
    while (*sty) {
      size_t a = 0, b = 0;
      while (*sty == ' ' || *sty == ';') sty++;
      while (*sty && *sty != ':' && *sty != ';' && a < sizeof k - 1) k[a++] = *sty++;
      k[a] = 0;
      while (a && k[a - 1] == ' ') k[--a] = 0;
      if (*sty != ':') { while (*sty && *sty != ';') sty++; continue; }
      sty++;
      while (*sty == ' ') sty++;
      while (*sty && *sty != ';' && b < sizeof v - 1) v[b++] = *sty++;
      v[b] = 0;
      set_prop(st, k, v);
    }
  }
}
/* an element's outline as a path in its own units; NULL if it isn't a shape (or is empty) */
static Path *element_path(Xml *x)
{
  Path *p = NULL;
  const char *nm = x->name;
  if (!strcmp(nm, "path")) { const char *dd = xml_attr(x, "d"); return dd ? path_from_svg(dd) : NULL; }
  if (!(p = path_new())) return NULL;
  if (!strcmp(nm, "rect")) {
    double X = num(xml_attr(x, "x"), 0), Y = num(xml_attr(x, "y"), 0), W = num(xml_attr(x, "width"), 0), H = num(xml_attr(x, "height"), 0);
    double rx = num(xml_attr(x, "rx"), num(xml_attr(x, "ry"), 0));
    if (W > 0 && H > 0) shape_rect_path(p, X, Y, X + W, Y + H, rx > 0 ? rx : 0);
  } else if (!strcmp(nm, "circle") || !strcmp(nm, "ellipse")) {
    double cx = num(xml_attr(x, "cx"), 0), cy = num(xml_attr(x, "cy"), 0);
    double rx = nm[0] == 'c' ? num(xml_attr(x, "r"), 0) : num(xml_attr(x, "rx"), 0), ry = nm[0] == 'c' ? rx : num(xml_attr(x, "ry"), 0);
    if (rx > 0 && ry > 0) shape_ellipse_path(p, cx - rx, cy - ry, cx + rx, cy + ry);
  } else if (!strcmp(nm, "line")) {
    path_move_to(p, num(xml_attr(x, "x1"), 0), num(xml_attr(x, "y1"), 0));
    path_line_to(p, num(xml_attr(x, "x2"), 0), num(xml_attr(x, "y2"), 0));
  } else if (!strcmp(nm, "polyline") || !strcmp(nm, "polygon")) {
    const char *pts = xml_attr(x, "points");
    double v[2];
    int first = 1;
    while (pts && numbers(pts, v, 2) == 2) {
      if (first) path_move_to(p, v[0], v[1]); else path_line_to(p, v[0], v[1]);
      first = 0;
      /* past the two numbers */
      { int k; for (k = 0; k < 2; k++) { char *e; pts = skipsp(pts); strtod(pts, &e); pts = e; } }
    }
    if (!first && nm[4] == 'g') path_close(p);
  } else { path_free(p); return NULL; }
  return p;
}

#define SVG_DEPTH 32
int svg_import(Doc *d, const u8 *s, size_t n, double scale, int *truncated, char *err, size_t errcap)
{
  Xml x;
  Style st[SVG_DEPTH + 1];
  int depth = 0, count = 0, in_svg = 0, t;
  if (truncated) *truncated = 0;
  memset(&st[0], 0, sizeof st[0]);
  st[0].m[0] = st[0].m[4] = scale > 0 ? scale : 1;
  st[0].fill_on = 1; st[0].fill = 0; st[0].stroke_w = 1; st[0].opacity = st[0].fill_op = st[0].stroke_op = 1;
  xml_init(&x, (const char *)s, n);
  doc_begin(d, "Place SVG");
  while ((t = xml_next(&x)) != XT_EOF) {
    if (t == XT_END) { if (depth > 0) depth--; continue; }
    if (t != XT_START) continue;
    if (depth >= SVG_DEPTH) { xml_skip(&x); continue; }
    st[depth + 1] = st[depth];
    if (!strcmp(x.name, "svg")) {
      /* the viewBox (of the outermost svg) to its width and height */
      const char *vb = xml_attr(&x, "viewBox");
      double v[4];
      if (!in_svg && vb && numbers(vb, v, 4) == 4 && v[2] > 0 && v[3] > 0) {
        double W = num(xml_attr(&x, "width"), v[2]), H = num(xml_attr(&x, "height"), v[3]);
        double tm[6];
        tm[0] = W / v[2]; tm[1] = 0; tm[2] = -v[0] * W / v[2]; tm[3] = 0; tm[4] = H / v[3]; tm[5] = -v[1] * H / v[3];
        mat_mul(st[depth + 1].m, tm);
      }
      in_svg = 1;
    }
    if (!strcmp(x.name, "defs") || !strcmp(x.name, "clipPath") || !strcmp(x.name, "mask") || !strcmp(x.name, "symbol") ||
        !strcmp(x.name, "marker") || !strcmp(x.name, "pattern") || !strcmp(x.name, "linearGradient") || !strcmp(x.name, "radialGradient") ||
        !strcmp(x.name, "style") || !strcmp(x.name, "text") || !strcmp(x.name, "metadata")) { xml_skip(&x); continue; }
    apply_attrs(&x, &st[depth + 1]);
    {
      Path *p = in_svg ? element_path(&x) : NULL;
      if (p) {
        Style *e = &st[depth + 1];
        double x0, y0, x1, y1, det = fabs(e->m[0] * e->m[4] - e->m[1] * e->m[3]);
        int open = !strcmp(x.name, "line") || !strcmp(x.name, "polyline");
        path_transform(p, e->m);
        if (path_bounds(p, &x0, &y0, &x1, &y1) && !e->hidden && (e->fill_on || e->stroke_on)) {
          ShapeParams sp;
          double fo = e->opacity * e->fill_op, so = e->opacity * e->stroke_op;
          const char *id = xml_attr(&x, "id");
          char name[64];
          if (d->nlay >= MAXLAYERS) { if (truncated) *truncated = 1; path_free(p); break; }
          shape_defaults(&sp, SH_CUSTOM, x0, y0, x1, y1, e->fill);
          sp.fill_on = (u8)(e->fill_on && !open);
          sp.stroke_on = (u8)e->stroke_on; sp.stroke_color = e->stroke;
          sp.stroke_width = e->stroke_w * sqrt(det);
          if (!(sp.stroke_width > 0)) sp.stroke_width = 1;
          if (sp.stroke_width > 1000) sp.stroke_width = 1000;
          sp.fill_alpha = (u8)(255 - (int)(255 * (fo < 0 ? 0 : fo > 1 ? 1 : fo) + 0.5));
          sp.stroke_alpha = (u8)(255 - (int)(255 * (so < 0 ? 0 : so > 1 ? 1 : so) + 0.5));
          sp.evenodd = sp.evenodd_path = (u8)e->evenodd;
          snprintf(name, sizeof name, "%s", id && id[0] ? id : x.name);
          if (doc_new_shape(d, &sp, p, name) >= 0) count++;
        }
        path_free(p);
      }
    }
    depth++;                                         /* (<x/> too: its END follows and brings it back) */
  }
  doc_end(d);
  xml_free(&x);
  if (!count) codec_err(err, errcap, "The SVG drawing has no shapes Easel can read");
  return count;
}

Doc *svg_load_doc(const u8 *s, size_t n, char *err, size_t errcap)
{
  Xml x;
  double W = 0, H = 0;
  int t, tr;
  Doc *d;
  if (!svg_detect(s, n)) { codec_err(err, errcap, "Not an SVG drawing"); return NULL; }
  xml_init(&x, (const char *)s, n);
  while ((t = xml_next(&x)) != XT_EOF) {
    if (t == XT_START && !strcmp(x.name, "svg")) {
      const char *vb = xml_attr(&x, "viewBox");
      double v[4];
      int havevb = vb && numbers(vb, v, 4) == 4 && v[2] > 0 && v[3] > 0;
      const char *ws = xml_attr(&x, "width"), *hs = xml_attr(&x, "height");
      W = ws && !strchr(ws, '%') ? num(ws, 0) : 0; H = hs && !strchr(hs, '%') ? num(hs, 0) : 0;
      if (ws && strstr(ws, "mm")) W *= 96 / 25.4;
      if (hs && strstr(hs, "mm")) H *= 96 / 25.4;
      if (W <= 0) W = havevb ? v[2] : 0;
      if (H <= 0) H = havevb ? v[3] : 0;
      break;
    }
  }
  xml_free(&x);
  if (!(W >= 1)) W = 800;
  if (!(H >= 1)) H = 600;
  if (W > MAXDIM) W = MAXDIM;
  if (H > MAXDIM) H = MAXDIM;
  if (W * H > 64.0 * 1024 * 1024) { codec_err(err, errcap, "The SVG drawing is too big"); return NULL; }
  if (!(d = doc_new((i32)(W + 0.5), (i32)(H + 0.5), 0, 1))) { codec_err(err, errcap, "Out of memory"); return NULL; }
  if (!svg_import(d, s, n, 1, &tr, err, errcap)) { doc_free(d); return NULL; }
  /* the empty Background layer the document started with goes */
  if (d->nlay > 1) { layer_free(d->lay[0]); memmove(&d->lay[0], &d->lay[1], (size_t)(d->nlay - 1) * sizeof d->lay[0]); d->nlay--; }
  d->active = d->nlay - 1;
  doc_forget_undo(d);
  d->dirty = 0;
  return d;
}
