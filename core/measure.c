/*
 * measure.c - Easel round 3: the Measurement Log (measure.h).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "measure.h"
#include "crop.h"
#include "toolsx.h"

const char *const measure_sources[3] = { "Selection", "Ruler Tool", "Count Tool" };

int measure_selection(Doc *d, MeasureRec *m)
{
  Box b = d->sel ? d->selbox : doc_box(d);
  u8 *row;
  i32 x, y;
  double sum = 0, wsum = 0;
  memset(m, 0, sizeof *m);
  m->source = MS_SELECTION;
  m->minv = 255;
  b = box_clip(b, doc_box(d));
  if (box_empty(b) || !(row = (u8 *)malloc((size_t)(b.x1 - b.x0) * 4))) return 0;
  for (y = b.y0; y < b.y1; y++) {
    doc_composite(d, box_make(b.x0, y, b.x1, y + 1), row, (long)(b.x1 - b.x0) * 4);
    for (x = b.x0; x < b.x1; x++) {
      int s = sel_at(d, x, y), in = s >= 128;
      const u8 *p = row + (x - b.x0) * 4;
      double g;
      if (!s) continue;
      /* the edges: sides of selected pixels next to unselected ones (or the canvas's edge) */
      if (in) {
        m->count++;
        if (x == 0 || sel_at(d, x - 1, y) < 128) m->perimeter++;
        if (x == d->w - 1 || sel_at(d, x + 1, y) < 128) m->perimeter++;
        if (y == 0 || sel_at(d, x, y - 1) < 128) m->perimeter++;
        if (y == d->h - 1 || sel_at(d, x, y + 1) < 128) m->perimeter++;
      }
      m->area += s / 255.0;
      /* the grey a pixel shows over white (as Photoshop measures the merged picture) */
      g = (0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]) * p[3] / 255.0 + 255.0 * (255 - p[3]) / 255.0;
      sum += g * s / 255.0; wsum += s / 255.0;
      if (in) { if (g < m->minv) m->minv = g; if (g > m->maxv) m->maxv = g; }
    }
  }
  free(row);
  if (wsum <= 0) { m->minv = 0; return 0; }
  m->mean = sum / wsum;
  m->density = m->mean * m->area;
  m->w = b.x1 - b.x0; m->h = b.y1 - b.y0;
  if (!m->count) m->minv = m->maxv = m->mean;
  return 1;
}
void measure_ruler(double x0, double y0, double x1, double y1, MeasureRec *m)
{
  RulerInfo r;
  memset(m, 0, sizeof *m);
  m->source = MS_RULER;
  ruler_measure(x0, y0, x1, y1, &r);
  m->length = r.length; m->angle = r.angle; m->w = r.w; m->h = r.h;
}
void measure_count(const Doc *d, MeasureRec *m)
{
  memset(m, 0, sizeof *m);
  m->source = MS_COUNT;
  m->count = d->annot ? d->annot->nmark : 0;
}
int mlog_add(MeasureLog *l, const MeasureRec *m, const char *docname)
{
  if (l->n >= MLOG_MAX) return -1;
  l->r[l->n] = *m;
  strncpy(l->r[l->n].doc, docname ? docname : "", sizeof l->r[0].doc - 1);
  l->r[l->n].doc[sizeof l->r[0].doc - 1] = 0;
  return l->n++;
}
void mlog_delete(MeasureLog *l, int i)
{
  if (i < 0 || i >= l->n) return;
  memmove(l->r + i, l->r + i + 1, sizeof l->r[0] * (size_t)(l->n - i - 1));
  l->n--;
}
/* a number with up to two decimals, without the C library's floating point printf (the Amiga's) */
static void num(char *o, size_t cap, double v)
{
  long t = (long)(v * 100 + (v < 0 ? -0.5 : 0.5));
  if (t % 100 == 0) snprintf(o, cap, "%ld", t / 100);
  else snprintf(o, cap, "%s%ld.%02ld", t < 0 ? "-" : "", labs(t) / 100, labs(t) % 100);
}
void mlog_line(const MeasureRec *m, int row, const char *src, char *out, size_t cap)
{
  char a[24], b[24], c[24], e[24];
  if (m->source == MS_RULER) {
    num(a, sizeof a, m->length); num(b, sizeof b, m->angle);
    snprintf(out, cap, "%d  %s  %s px  %s\xB0", row + 1, src, a, b);
  } else if (m->source == MS_COUNT) snprintf(out, cap, "%d  %s  %ld", row + 1, src, m->count);
  else {
    num(a, sizeof a, m->area); num(b, sizeof b, m->perimeter); num(c, sizeof c, m->mean); num(e, sizeof e, m->density);
    snprintf(out, cap, "%d  %s  %s px\xB2  %s px  %s (%s)", row + 1, src, a, b, c, e);
  }
}
char *mlog_text(const MeasureLog *l)
{
  Buf b;
  int i;
  buf_init(&b);
  buf_adds(&b, "Label\tDocument\tSource\tCount\tArea\tPerimeter\tWidth\tHeight\tLength\tAngle\tMean Gray Value\tMinimum Gray Value\tMaximum Gray Value\tIntegrated Density\n");
  for (i = 0; i < l->n; i++) {
    const MeasureRec *m = &l->r[i];
    char v[10][24];
    num(v[0], 24, m->area); num(v[1], 24, m->perimeter); num(v[2], 24, m->w); num(v[3], 24, m->h);
    num(v[4], 24, m->length); num(v[5], 24, m->angle); num(v[6], 24, m->mean); num(v[7], 24, m->minv);
    num(v[8], 24, m->maxv); num(v[9], 24, m->density);
    buf_printf(&b, "Measurement %d\t%s\t%s\t%ld\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", i + 1, m->doc, measure_sources[m->source], m->count,
               v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9]);
  }
  if (b.oom) { buf_free(&b); return NULL; }
  return buf_take(&b);
}
