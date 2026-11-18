/*
 * toolsx.c - Easel 1.2's tool extras (toolsx.h): Perspective Crop, the turned crop box, notes, counts
 * and slices, Rotate View, the History Options and History Log.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "toolsx.h"
#include "paint.h"
#include "codec.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ Perspective Crop */
/* the square to quad map (Heckbert's closed form) */
int homography_unit(const double *q, double *h)
{
  double x0 = q[0], y0 = q[1], x1 = q[2], y1 = q[3], x2 = q[4], y2 = q[5], x3 = q[6], y3 = q[7];
  double sx = x0 - x1 + x2 - x3, sy = y0 - y1 + y2 - y3, dx1 = x1 - x2, dx2 = x3 - x2, dy1 = y1 - y2, dy2 = y3 - y2;
  double den = dx1 * dy2 - dx2 * dy1, g, hh;
  if (fabs(sx) < 1e-12 && fabs(sy) < 1e-12) {
    h[0] = x1 - x0; h[1] = x3 - x0; h[2] = x0; h[3] = y1 - y0; h[4] = y3 - y0; h[5] = y0; h[6] = 0; h[7] = 0; h[8] = 1;
  } else {
    if (fabs(den) < 1e-12) return 0;
    g = (sx * dy2 - dx2 * sy) / den; hh = (dx1 * sy - sx * dy1) / den;
    h[0] = x1 - x0 + g * x1; h[1] = x3 - x0 + hh * x3; h[2] = x0;
    h[3] = y1 - y0 + g * y1; h[4] = y3 - y0 + hh * y3; h[5] = y0;
    h[6] = g; h[7] = hh; h[8] = 1;
  }
  /* (degenerate: corners on a line) */
  return fabs(h[0] * h[4] - h[1] * h[3]) > 1e-9 || fabs(h[6]) + fabs(h[7]) > 1e-12;
}
void perspective_size(const double *q, double *w, double *h)
{
  double t = hypot(q[2] - q[0], q[3] - q[1]), b = hypot(q[4] - q[6], q[5] - q[7]);
  double l = hypot(q[6] - q[0], q[7] - q[1]), r = hypot(q[4] - q[2], q[5] - q[3]);
  *w = (t + b) / 2; *h = (l + r) / 2;
}
int image_perspective_crop(Doc *d, const double *q, i32 nw, i32 nh, int interp)
{
  double h[9], inv[9], pw, ph;
  if (!homography_unit(q, h)) return 0;
  perspective_size(q, &pw, &ph);
  if (nw <= 0) nw = (i32)floor(pw + 0.5);
  if (nh <= 0) nh = (i32)floor(ph + 0.5);
  if (nw < 1 || nh < 1 || nw > MAXDIM || nh > MAXDIM) return 0;
  /* new pixel centre (u, v) -> old: h applied to (u / nw, v / nh) */
  inv[0] = h[0] / nw; inv[1] = h[1] / nh; inv[2] = h[2];
  inv[3] = h[3] / nw; inv[4] = h[4] / nh; inv[5] = h[5];
  inv[6] = h[6] / nw; inv[7] = h[7] / nh; inv[8] = h[8];
  return image_remap(d, nw, nh, inv, interp, "Perspective Crop");
}

/* ------------------------------------------------------------------ the turned crop box */
void crop_corners(double cx, double cy, double w, double h, double angle, double *out)
{
  double a = angle * M_PI / 180, c = cos(a), s = sin(a);
  static const double ux[4] = { -0.5, 0.5, 0.5, -0.5 }, uy[4] = { -0.5, -0.5, 0.5, 0.5 };
  int k;
  for (k = 0; k < 4; k++) {
    double x = ux[k] * w, y = uy[k] * h;
    out[k * 2] = cx + x * c - y * s; out[k * 2 + 1] = cy + x * s + y * c;
  }
}
double crop_drag_angle(double cx, double cy, double ax, double ay, double px, double py)
{
  double a0 = atan2(ay - cy, ax - cx), a1 = atan2(py - cy, px - cx), d = (a1 - a0) * 180 / M_PI;
  while (d > 180) d -= 360;
  while (d < -180) d += 360;
  return d;
}
int crop_inside(double cx, double cy, double w, double h, double angle, double x, double y)
{
  double a = -angle * M_PI / 180, dx = x - cx, dy = y - cy, u = dx * cos(a) - dy * sin(a), v = dx * sin(a) + dy * cos(a);
  return fabs(u) <= w / 2 && fabs(v) <= h / 2;
}

/* ------------------------------------------------------------------ annotations */
Annots *doc_annots(Doc *d, int make)
{
  Annots *a = d->annot;
  if (a || !make) return a;
  if (!(a = (Annots *)calloc(1, sizeof *a))) return NULL;
  a->show_notes = a->show_count = a->show_slices = 1;
  strcpy(a->group[0].name, "Count Group 1"); a->group[0].color = 0xFF0000; a->group[0].size = 2; a->group[0].label = 8; a->group[0].visible = 1;
  a->ngroup = 1;
  d->annot = a;
  return a;
}
static void scopy(char *dst, size_t cap, const char *s) { if (!s) s = ""; strncpy(dst, s, cap - 1); dst[cap - 1] = 0; }
int note_add(Doc *d, i32 x, i32 y, const char *author, const char *text, u32 color)
{
  Annots *a = doc_annots(d, 1);
  Note *n;
  if (!a || a->nnote >= MAXNOTES) return -1;
  n = &a->note[a->nnote];
  memset(n, 0, sizeof *n);
  n->x = x; n->y = y; n->color = color & 0xFFFFFF; n->open = 1;
  scopy(n->author, sizeof n->author, author); scopy(n->text, sizeof n->text, text);
  a->show_notes = 1;
  return a->nnote++;
}
int note_set(Doc *d, int i, const char *text) { Annots *a = d->annot; if (!a || i < 0 || i >= a->nnote) return 0; scopy(a->note[i].text, NOTE_LEN, text); return 1; }
int note_move(Doc *d, int i, i32 x, i32 y) { Annots *a = d->annot; if (!a || i < 0 || i >= a->nnote) return 0; a->note[i].x = x; a->note[i].y = y; return 1; }
int note_delete(Doc *d, int i)
{
  Annots *a = d->annot;
  if (!a || i < 0 || i >= a->nnote) return 0;
  memmove(&a->note[i], &a->note[i + 1], (size_t)(a->nnote - i - 1) * sizeof(Note));
  a->nnote--;
  return 1;
}
int note_clear(Doc *d) { Annots *a = d->annot; int n = a ? a->nnote : 0; if (a) a->nnote = 0; return n; }
int note_at(Doc *d, i32 x, i32 y, int tol)
{
  Annots *a = d->annot;
  int i;
  if (!a) return -1;
  for (i = a->nnote - 1; i >= 0; i--) if (x >= a->note[i].x - tol && x <= a->note[i].x + 12 + tol && y >= a->note[i].y - tol && y <= a->note[i].y + 12 + tol) return i;
  return -1;
}
int count_add(Doc *d, i32 x, i32 y)
{
  Annots *a = doc_annots(d, 1);
  if (!a || a->nmark >= MAXCOUNTS) return -1;
  if (a->cur_group < 0 || a->cur_group >= a->ngroup) a->cur_group = 0;
  a->mark[a->nmark].x = x; a->mark[a->nmark].y = y; a->mark[a->nmark].group = (u8)a->cur_group;
  a->show_count = 1;                                       /* (adding a marker shows the count again, as upstream #11845) */
  return a->nmark++;
}
int count_at(Doc *d, i32 x, i32 y, int tol)
{
  Annots *a = d->annot;
  int i;
  if (!a) return -1;
  for (i = a->nmark - 1; i >= 0; i--) if (abs(a->mark[i].x - x) <= tol && abs(a->mark[i].y - y) <= tol && a->group[a->mark[i].group].visible) return i;
  return -1;
}
int count_move(Doc *d, int i, i32 x, i32 y) { Annots *a = d->annot; if (!a || i < 0 || i >= a->nmark) return 0; a->mark[i].x = x; a->mark[i].y = y; return 1; }
int count_delete(Doc *d, int i)
{
  Annots *a = d->annot;
  if (!a || i < 0 || i >= a->nmark) return 0;
  memmove(&a->mark[i], &a->mark[i + 1], (size_t)(a->nmark - i - 1) * sizeof(CountMark));
  a->nmark--;
  return 1;
}
int count_clear(Doc *d, int g)
{
  Annots *a = d->annot;
  int i, k = 0, n = 0;
  if (!a) return 0;
  for (i = 0; i < a->nmark; i++) { if (g >= 0 && a->mark[i].group != g) a->mark[k++] = a->mark[i]; else n++; }
  a->nmark = k;
  return n;
}
int count_group_add(Doc *d, const char *name, u32 color)
{
  Annots *a = doc_annots(d, 1);
  CountGroup *g;
  if (!a || a->ngroup >= MAXCOUNTGROUPS) return -1;
  g = &a->group[a->ngroup];
  memset(g, 0, sizeof *g);
  if (name && *name) scopy(g->name, sizeof g->name, name); else sprintf(g->name, "Count Group %d", a->ngroup + 1);
  g->color = color & 0xFFFFFF; g->size = 2; g->label = 8; g->visible = 1;
  a->cur_group = a->ngroup;
  return a->ngroup++;
}
int count_group_delete(Doc *d, int g)
{
  Annots *a = d->annot;
  int i;
  if (!a || g < 0 || g >= a->ngroup || a->ngroup < 2) return 0;
  count_clear(d, g);
  for (i = 0; i < a->nmark; i++) if (a->mark[i].group > g) a->mark[i].group--;
  memmove(&a->group[g], &a->group[g + 1], (size_t)(a->ngroup - g - 1) * sizeof(CountGroup));
  a->ngroup--;
  if (a->cur_group >= a->ngroup) a->cur_group = a->ngroup - 1;
  return 1;
}
int count_number(const Doc *d, int i)
{
  const Annots *a = d->annot;
  int k, n = 0;
  if (!a || i < 0 || i >= a->nmark) return 0;
  for (k = 0; k <= i; k++) if (a->mark[k].group == a->mark[i].group) n++;
  return n;
}
int count_in_group(const Doc *d, int g)
{
  const Annots *a = d->annot;
  int k, n = 0;
  if (!a) return 0;
  for (k = 0; k < a->nmark; k++) if (a->mark[k].group == g) n++;
  return n;
}
char *count_record(const Doc *d)
{
  const Annots *a = d->annot;
  char *s;
  int g;
  size_t at = 0;
  if (!(s = (char *)malloc(64 + (size_t)MAXCOUNTGROUPS * 64))) return NULL;
  s[0] = 0;
  for (g = 0; a && g < a->ngroup; g++) at += (size_t)sprintf(s + at, "%s\t%d\n", a->group[g].name, count_in_group(d, g));
  return s;
}
int slice_add(Doc *d, Box b)
{
  Annots *a;
  b = box_clip(b, doc_box(d));
  if (box_empty(b) || !(a = doc_annots(d, 1)) || a->nslice >= MAXSLICES) return -1;
  a->slice[a->nslice].b = b;
  sprintf(a->slice[a->nslice].name, "slice_%02d", a->nslice + 1);
  a->slice[a->nslice].url[0] = 0;
  a->show_slices = 1;
  return a->nslice++;
}
int slice_at(const Doc *d, i32 x, i32 y)
{
  const Annots *a = d->annot;
  int i;
  if (!a) return -1;
  for (i = a->nslice - 1; i >= 0; i--) { Box b = a->slice[i].b; if (x >= b.x0 && x < b.x1 && y >= b.y0 && y < b.y1) return i; }
  return -1;
}
int slice_set(Doc *d, int i, Box b)
{
  Annots *a = d->annot;
  b = box_clip(b, doc_box(d));
  if (!a || i < 0 || i >= a->nslice || box_empty(b)) return 0;
  a->slice[i].b = b;
  return 1;
}
int slice_delete(Doc *d, int i)
{
  Annots *a = d->annot;
  if (!a || i < 0 || i >= a->nslice) return 0;
  memmove(&a->slice[i], &a->slice[i + 1], (size_t)(a->nslice - i - 1) * sizeof(Slice));
  a->nslice--;
  return 1;
}
int slice_divide(Doc *d, int i, int across, int down)
{
  Annots *a = d->annot;
  Box b;
  int x, y, n = 0;
  if (!a || i < 0 || i >= a->nslice || across < 1 || down < 1 || a->nslice - 1 + across * down > MAXSLICES) return 0;
  b = a->slice[i].b;
  if (across > b.x1 - b.x0 || down > b.y1 - b.y0) return 0;
  slice_delete(d, i);
  for (y = 0; y < down; y++) for (x = 0; x < across; x++) {
    Box s = box_make(b.x0 + (b.x1 - b.x0) * x / across, b.y0 + (b.y1 - b.y0) * y / down, b.x0 + (b.x1 - b.x0) * (x + 1) / across, b.y0 + (b.y1 - b.y0) * (y + 1) / down);
    if (slice_add(d, s) >= 0) n++;
  }
  return n;
}
static int cmp_i32(const void *a, const void *b) { i32 x = *(const i32 *)a, y = *(const i32 *)b; return x < y ? -1 : x > y; }
int slices_from_guides(Doc *d)
{
  i32 xs[MAXGUIDES + 2], ys[MAXGUIDES + 2];
  int nx = 0, ny = 0, i, j, n = 0;
  Annots *a;
  xs[nx++] = 0; ys[ny++] = 0;
  for (i = 0; i < d->guides.nv; i++) if (d->guides.v[i] > 0 && d->guides.v[i] < d->w) xs[nx++] = d->guides.v[i];
  for (i = 0; i < d->guides.nh; i++) if (d->guides.h[i] > 0 && d->guides.h[i] < d->h) ys[ny++] = d->guides.h[i];
  xs[nx++] = d->w; ys[ny++] = d->h;
  qsort(xs, (size_t)nx, sizeof(i32), cmp_i32); qsort(ys, (size_t)ny, sizeof(i32), cmp_i32);
  if (!(a = doc_annots(d, 1))) return 0;
  a->nslice = 0;
  for (j = 0; j + 1 < ny; j++) for (i = 0; i + 1 < nx; i++)
    if (xs[i + 1] > xs[i] && ys[j + 1] > ys[j] && slice_add(d, box_make(xs[i], ys[j], xs[i + 1], ys[j + 1])) >= 0) n++;
  return n;
}
int slices_export(Doc *d, const char *drawer, const char *base)
{
  const Annots *a = d->annot;
  int i, n = 0;
  if (!a) return 0;
  for (i = 0; i < a->nslice; i++) {
    Box b = a->slice[i].b;
    Rgba im;
    u8 *png;
    size_t len;
    char path[400];
    size_t dl = drawer ? strlen(drawer) : 0;
    if (!rgba_alloc(&im, b.x1 - b.x0, b.y1 - b.y0)) continue;
    doc_composite(d, b, im.px, (long)im.w * 4);
    png = png_encode(&im, d->dpi, &len);
    rgba_free(&im);
    if (!png) continue;
    snprintf(path, sizeof path, "%s%s%s_%02d.png", drawer ? drawer : "", dl && drawer[dl - 1] != ':' && drawer[dl - 1] != '/' ? "/" : "", base && *base ? base : "slice", i + 1);
    if (file_write(path, png, len)) n++;
    free(png);
  }
  return n;
}
/* XML: attribute values escaped */
static size_t esc(char *o, const char *s)
{
  size_t k = 0;
  for (; *s; s++) {
    const char *r = *s == '&' ? "&amp;" : *s == '<' ? "&lt;" : *s == '>' ? "&gt;" : *s == '"' ? "&quot;" : *s == '\n' ? "&#10;" : NULL;
    if (r) { strcpy(o + k, r); k += strlen(r); } else o[k++] = *s;
  }
  o[k] = 0;
  return k;
}
char *annots_xml(const Doc *d)
{
  const Annots *a = d->annot;
  char *s;
  size_t at = 0;
  int i;
  if (!a || (!a->nnote && !a->nmark && !a->nslice && a->ngroup <= 1)) return NULL;
  if (!(s = (char *)malloc((size_t)a->nnote * (NOTE_LEN * 6 + 256) + (size_t)a->nmark * 64 + (size_t)a->nslice * 800 + (size_t)a->ngroup * 256 + 64))) return NULL;
  s[0] = 0;
  for (i = 0; i < a->nnote; i++) {
    at += (size_t)sprintf(s + at, "<easel:note x=\"%ld\" y=\"%ld\" color=\"%lu\" author=\"", (long)a->note[i].x, (long)a->note[i].y, (unsigned long)a->note[i].color);
    at += esc(s + at, a->note[i].author);
    at += (size_t)sprintf(s + at, "\" text=\"");
    at += esc(s + at, a->note[i].text);
    at += (size_t)sprintf(s + at, "\"/>\n");
  }
  for (i = 0; i < a->ngroup; i++) {
    at += (size_t)sprintf(s + at, "<easel:count-group color=\"%lu\" size=\"%d\" label=\"%d\" visible=\"%d\" name=\"", (unsigned long)a->group[i].color, a->group[i].size, a->group[i].label, a->group[i].visible);
    at += esc(s + at, a->group[i].name);
    at += (size_t)sprintf(s + at, "\"/>\n");
  }
  for (i = 0; i < a->nmark; i++) at += (size_t)sprintf(s + at, "<easel:count x=\"%ld\" y=\"%ld\" group=\"%d\"/>\n", (long)a->mark[i].x, (long)a->mark[i].y, a->mark[i].group);
  for (i = 0; i < a->nslice; i++) {
    at += (size_t)sprintf(s + at, "<easel:slice x0=\"%ld\" y0=\"%ld\" x1=\"%ld\" y1=\"%ld\" name=\"", (long)a->slice[i].b.x0, (long)a->slice[i].b.y0, (long)a->slice[i].b.x1, (long)a->slice[i].b.y1);
    at += esc(s + at, a->slice[i].name);
    at += (size_t)sprintf(s + at, "\" url=\"");
    at += esc(s + at, a->slice[i].url);
    at += (size_t)sprintf(s + at, "\"/>\n");
  }
  return s;
}
#include "xml.h"
static long attr_l(Xml *x, const char *name, long def) { const char *v = xml_attr(x, name); return v ? atol(v) : def; }
static unsigned long attr_u(Xml *x, const char *name, unsigned long def) { const char *v = xml_attr(x, name); return v ? strtoul(v, NULL, 10) : def; }
void annots_read_xml(Doc *d, const char *xml, size_t n)
{
  Xml x;
  Annots *a = NULL;
  int groups = 0;
  xml_init(&x, xml, n);
  while (xml_next(&x) != XT_EOF) {
    const char *v;
    if (x.type != XT_START) continue;
    if (!strcmp(x.name, "note") || !strcmp(x.name, "count") || !strcmp(x.name, "count-group") || !strcmp(x.name, "slice")) {
      if (!a && !(a = doc_annots(d, 1))) break;
    } else continue;
    if (!strcmp(x.name, "note")) {
      i32 nx = (i32)attr_l(&x, "x", 0), ny = (i32)attr_l(&x, "y", 0);
      int i = note_add(d, nx, ny, xml_attr(&x, "author"), xml_attr(&x, "text"), (u32)attr_u(&x, "color", 0xFFFF00));
      if (i >= 0) a->note[i].open = 0;
    } else if (!strcmp(x.name, "count-group")) {
      int g;
      if (!groups++) a->ngroup = 0;
      if ((g = count_group_add(d, xml_attr(&x, "name"), (u32)attr_u(&x, "color", 0xFF0000))) >= 0) {
        int sz = (int)attr_l(&x, "size", 2), lb = (int)attr_l(&x, "label", 8);
        a->group[g].size = (u8)(sz < 1 ? 1 : sz > 10 ? 10 : sz); a->group[g].label = (u8)(lb < 8 ? 8 : lb > 72 ? 72 : lb);
        a->group[g].visible = (u8)(!(v = xml_attr(&x, "visible")) || atoi(v));
      }
      a->cur_group = 0;
    } else if (!strcmp(x.name, "count")) {
      int k = (int)attr_l(&x, "group", 0), i;
      if (k < 0 || k >= a->ngroup) k = 0;
      a->cur_group = k;
      i = count_add(d, (i32)attr_l(&x, "x", 0), (i32)attr_l(&x, "y", 0));
      (void)i;
      a->cur_group = 0;
    } else {
      Box b = box_make((i32)attr_l(&x, "x0", 0), (i32)attr_l(&x, "y0", 0), (i32)attr_l(&x, "x1", 0), (i32)attr_l(&x, "y1", 0));
      int i = slice_add(d, b);
      if (i >= 0) { scopy(a->slice[i].name, 32, xml_attr(&x, "name")); scopy(a->slice[i].url, 96, xml_attr(&x, "url")); }
    }
  }
  xml_free(&x);
}

/* ------------------------------------------------------------------ Rotate View */
void view_rotate(double angle, double sx, double sy, double *ix, double *iy)
{
  double a = -angle * M_PI / 180, c = cos(a), s = sin(a);
  *ix = sx * c - sy * s; *iy = sx * s + sy * c;
}
void view_unrotate(double angle, double ix, double iy, double *sx, double *sy)
{
  double a = angle * M_PI / 180, c = cos(a), s = sin(a);
  *sx = ix * c - iy * s; *sy = ix * s + iy * c;
}
double view_angle_snap(double a, int snap)
{
  if (snap) a = floor(a / 15 + 0.5) * 15;
  while (a > 180) a -= 360;
  while (a <= -180) a += 360;
  return a;
}

/* ------------------------------------------------------------------ History Options and the History Log */
HistOptions hist_opts = { 1, 0, 0, 0, HL_FILE, HL_CONCISE, "PROGDIR:Easel-History.log" };
static char logline[512];
static void log_out(Doc *d, const char *line)
{
  if (hist_opts.log_where != HL_META && hist_opts.log_file[0]) {
    FILE *f = fopen(hist_opts.log_file, "a");
    if (f) { fputs(line, f); fputc('\n', f); fclose(f); }
  }
  if (hist_opts.log_where != HL_FILE && d) {
    /* the picture's File Info description gets a History section, the newest lines kept */
    char *desc = d->info.description, *h = strstr(desc, "History:\n");
    size_t cap = sizeof d->info.description, room;
    if (!h) {
      size_t l = strlen(desc);
      if (l + 12 >= cap) return;
      if (l) { desc[l++] = '\n'; desc[l] = 0; }
      strcpy(desc + l, "History:\n");
      h = desc + l;
    }
    room = cap - 1 - strlen(desc);
    while (strlen(line) + 1 > room) {
      /* the oldest line after "History:" goes */
      char *first = h + 9, *nl = strchr(first, '\n');
      if (!nl) { *first = 0; room = cap - 1 - strlen(desc); if (strlen(line) + 1 > room) return; break; }
      memmove(first, nl + 1, strlen(nl + 1) + 1);
      room = cap - 1 - strlen(desc);
    }
    strcat(desc, line); strcat(desc, "\n");
  }
}
const char *histlog_event(Doc *d, const char *when, const char *event, const char *name)
{
  if (!hist_opts.log_on) return NULL;
  snprintf(logline, sizeof logline, "%s %s%s%s", when ? when : "", event ? event : "", name && *name ? ": " : "", name ? name : "");
  log_out(d, logline);
  return logline;
}
const char *histlog_step(Doc *d, const char *when, const char *name, const char *action, const char *detail)
{
  if (!hist_opts.log_on || hist_opts.log_detail == HL_SESSIONS || !action || !*action) return NULL;
  if (hist_opts.log_detail == HL_DETAILED && detail && *detail) snprintf(logline, sizeof logline, "%s %s: %s (%s)", when ? when : "", name ? name : "", action, detail);
  else snprintf(logline, sizeof logline, "%s %s: %s", when ? when : "", name ? name : "", action);
  log_out(d, logline);
  return logline;
}
void histopts_write(FILE *f)
{
  fprintf(f, "history_auto_snapshot %d\nhistory_snapshot_on_save %d\nhistory_ask_snapshot %d\nhistory_log %d %d %d\nhistory_log_file %s\n",
          hist_opts.auto_snapshot, hist_opts.snapshot_on_save, hist_opts.ask_snapshot_name, hist_opts.log_on, hist_opts.log_where, hist_opts.log_detail, hist_opts.log_file);
}
int histopts_line(const char *l)
{
  int a, b, c;
  if (!strncmp(l, "history_auto_snapshot ", 22)) { hist_opts.auto_snapshot = atoi(l + 22) != 0; return 1; }
  if (!strncmp(l, "history_snapshot_on_save ", 25)) { hist_opts.snapshot_on_save = atoi(l + 25) != 0; return 1; }
  if (!strncmp(l, "history_ask_snapshot ", 21)) { hist_opts.ask_snapshot_name = atoi(l + 21) != 0; return 1; }
  if (!strncmp(l, "history_log ", 12) && sscanf(l + 12, "%d %d %d", &a, &b, &c) == 3) {
    hist_opts.log_on = a != 0; hist_opts.log_where = b >= HL_FILE && b <= HL_BOTH ? b : HL_FILE; hist_opts.log_detail = c >= HL_SESSIONS && c <= HL_DETAILED ? c : HL_CONCISE;
    return 1;
  }
  if (!strncmp(l, "history_log_file ", 17)) {
    size_t n;
    scopy(hist_opts.log_file, sizeof hist_opts.log_file, l + 17);
    n = strlen(hist_opts.log_file);
    while (n && (hist_opts.log_file[n - 1] == '\n' || hist_opts.log_file[n - 1] == '\r')) hist_opts.log_file[--n] = 0;
    return 1;
  }
  return 0;
}
