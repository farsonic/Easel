/*
 * actions.c - the Actions panel's recorder, saved layer styles, layer comps and histogram
 * statistics (see actions.h).
 *
 * The .actions text form, one item a line:
 *   EaselActions 1
 *   action <set>\t<name>\t<colour>
 *   step <id hex> <on> <dialog>\t<name>
 * (a step belongs to the action above it). Unknown lines are skipped.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "actions.h"
#include "fx.h"

static void cpy(char *o, const char *s, size_t cap)
{
  size_t i = 0;
  if (s) for (; s[i] && i < cap - 1; i++) o[i] = (s[i] == '\t' || s[i] == '\n' || s[i] == '\r') ? ' ' : s[i];
  o[i] = 0;
}

/* ------------------------------------------------------------------ actions */
void actlib_init(ActionLib *l) { memset(l, 0, sizeof *l); }
void actlib_free(ActionLib *l)
{
  int i;
  for (i = 0; i < l->n; i++) free(l->a[i].s);
  free(l->a);
  memset(l, 0, sizeof *l);
}
int act_new(ActionLib *l, const char *set, const char *name)
{
  Action *a;
  if (l->n >= l->cap) {
    int nc = l->cap ? l->cap * 2 : 8;
    Action *na = (Action *)realloc(l->a, sizeof(Action) * (size_t)nc);
    if (!na) return -1;
    l->a = na; l->cap = nc;
  }
  a = &l->a[l->n];
  memset(a, 0, sizeof *a);
  cpy(a->set, set && *set ? set : "Default Actions", sizeof a->set);
  cpy(a->name, name && *name ? name : "Action", sizeof a->name);
  return l->n++;
}
int act_delete(ActionLib *l, int i)
{
  if (i < 0 || i >= l->n) return 0;
  free(l->a[i].s);
  memmove(l->a + i, l->a + i + 1, sizeof(Action) * (size_t)(l->n - i - 1));
  l->n--;
  return 1;
}
int act_move(ActionLib *l, int from, int to)
{
  Action t;
  if (from < 0 || from >= l->n || to < 0 || to >= l->n) return 0;
  if (from == to) return 1;
  t = l->a[from];
  if (from < to) memmove(l->a + from, l->a + from + 1, sizeof(Action) * (size_t)(to - from));
  else memmove(l->a + to + 1, l->a + to, sizeof(Action) * (size_t)(from - to));
  l->a[to] = t;
  return 1;
}
int act_duplicate(ActionLib *l, int i)
{
  int k, j;
  char nm[ACT_NAMELEN + 8];
  if (i < 0 || i >= l->n) return -1;
  snprintf(nm, sizeof nm, "%s copy", l->a[i].name);
  if ((k = act_new(l, l->a[i].set, nm)) < 0) return -1;
  l->a[k].color = l->a[i].color;
  for (j = 0; j < l->a[i].n; j++) {
    const ActStep *s = &l->a[i].s[j];
    if (!act_add_step(l, k, s->id, s->name, s->dialog)) { act_delete(l, k); return -1; }
    l->a[k].s[j].on = s->on;
  }
  act_move(l, k, i + 1);
  return i + 1;
}
int act_insert_step(ActionLib *l, int i, int k, u32 id, const char *name, int dialog)
{
  Action *a;
  if (i < 0 || i >= l->n) return 0;
  a = &l->a[i];
  if (a->n >= ACT_MAXSTEPS) return 0;
  if (k < 0 || k > a->n) k = a->n;
  if (a->n >= a->cap) {
    int nc = a->cap ? a->cap * 2 : 8;
    ActStep *ns = (ActStep *)realloc(a->s, sizeof(ActStep) * (size_t)nc);
    if (!ns) return 0;
    a->s = ns; a->cap = nc;
  }
  memmove(a->s + k + 1, a->s + k, sizeof(ActStep) * (size_t)(a->n - k));
  a->s[k].id = id; a->s[k].on = 1; a->s[k].dialog = (u8)(dialog != 0);
  cpy(a->s[k].name, name ? name : "", sizeof a->s[k].name);
  a->n++;
  return 1;
}
int act_add_step(ActionLib *l, int i, u32 id, const char *name, int dialog) { return act_insert_step(l, i, -1, id, name, dialog); }
int act_delete_step(ActionLib *l, int i, int k)
{
  Action *a;
  if (i < 0 || i >= l->n || k < 0 || k >= (a = &l->a[i])->n) return 0;
  memmove(a->s + k, a->s + k + 1, sizeof(ActStep) * (size_t)(a->n - k - 1));
  a->n--;
  return 1;
}
int act_move_step(ActionLib *l, int i, int from, int to)
{
  Action *a;
  ActStep t;
  if (i < 0 || i >= l->n) return 0;
  a = &l->a[i];
  if (from < 0 || from >= a->n || to < 0 || to >= a->n) return 0;
  t = a->s[from];
  if (from < to) memmove(a->s + from, a->s + from + 1, sizeof(ActStep) * (size_t)(to - from));
  else memmove(a->s + to + 1, a->s + to, sizeof(ActStep) * (size_t)(from - to));
  a->s[to] = t;
  return 1;
}
int act_sets(const ActionLib *l, const char **names, int max)
{
  int i, k, n = 0;
  for (i = 0; i < l->n; i++) {
    for (k = 0; k < n; k++) if (!strcmp(names[k], l->a[i].set)) break;
    if (k == n && n < max) names[n++] = l->a[i].set;
  }
  return n;
}
int act_play(const ActionLib *l, int i, int from, int (*run)(void *ctx, u32 id, int dialog, const char *name), void *ctx)
{
  int k, done = 0;
  if (i < 0 || i >= l->n) return 0;
  for (k = from < 0 ? 0 : from; k < l->a[i].n; k++) {
    const ActStep *s = &l->a[i].s[k];
    if (!s->on) continue;
    if (!run(ctx, s->id, s->dialog, s->name)) break;
    done++;
  }
  return done;
}
char *actlib_save(const ActionLib *l)
{
  Buf b;
  int i, k;
  buf_init(&b);
  buf_adds(&b, "EaselActions 1\n");
  for (i = 0; i < l->n; i++) {
    const Action *a = &l->a[i];
    buf_printf(&b, "action %s\t%s\t%d\n", a->set, a->name, a->color);
    for (k = 0; k < a->n; k++) buf_printf(&b, "step %lX %d %d\t%s\n", (unsigned long)a->s[k].id, a->s[k].on, a->s[k].dialog, a->s[k].name);
  }
  if (b.oom) { buf_free(&b); return NULL; }
  return buf_take(&b);
}
/* the next line of text (NUL-ended in place of its newline) */
static char *next_line(char **p)
{
  char *s = *p, *e;
  if (!s || !*s) return NULL;
  e = strchr(s, '\n');
  if (e) { *e = 0; *p = e + 1; } else *p = s + strlen(s);
  if (e && e > s && e[-1] == '\r') e[-1] = 0;
  return s;
}
int actlib_load(ActionLib *l, const char *text, int merge)
{
  char *copy, *p, *ln;
  int cur = -1, ok = 0;
  if (!text || strncmp(text, "EaselActions", 12)) return 0;
  if (!(copy = str_dup(text))) return 0;
  if (!merge) actlib_free(l);
  p = copy;
  next_line(&p);
  while ((ln = next_line(&p))) {
    if (!strncmp(ln, "action ", 7)) {
      char *t1 = strchr(ln + 7, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
      if (!t1) continue;
      *t1 = 0;
      if (t2) *t2 = 0;
      if ((cur = act_new(l, ln + 7, t1 + 1)) < 0) break;
      l->a[cur].color = (u8)(t2 ? atoi(t2 + 1) : 0);
      ok = 1;
    } else if (!strncmp(ln, "step ", 5) && cur >= 0) {
      char *t = strchr(ln, '\t');
      unsigned long id;
      int on = 1, dlg = 0;
      if (sscanf(ln + 5, "%lx %d %d", &id, &on, &dlg) < 1) continue;
      if (act_add_step(l, cur, (u32)id, t ? t + 1 : "", dlg)) l->a[cur].s[l->a[cur].n - 1].on = (u8)(on != 0);
    }
  }
  free(copy);
  return ok;
}

/* ------------------------------------------------------------------ saved layer styles */
void stylelib_init(StyleLib *l) { memset(l, 0, sizeof *l); }
void stylelib_free(StyleLib *l) { free(l->s); memset(l, 0, sizeof *l); }
int stylelib_add(StyleLib *l, const char *name, const LayerFx *fx)
{
  if (l->n >= l->cap) {
    int nc = l->cap ? l->cap * 2 : 16;
    SavedStyle *ns = (SavedStyle *)realloc(l->s, sizeof(SavedStyle) * (size_t)nc);
    if (!ns) return -1;
    l->s = ns; l->cap = nc;
  }
  cpy(l->s[l->n].name, name && *name ? name : "Style", sizeof l->s[0].name);
  l->s[l->n].fx = *fx;
  l->s[l->n].fx.hidden = 0;
  return l->n++;
}
int stylelib_delete(StyleLib *l, int i)
{
  if (i < 0 || i >= l->n) return 0;
  memmove(l->s + i, l->s + i + 1, sizeof(SavedStyle) * (size_t)(l->n - i - 1));
  l->n--;
  return 1;
}
int stylelib_rename(StyleLib *l, int i, const char *name)
{
  if (i < 0 || i >= l->n || !name || !*name) return 0;
  cpy(l->s[i].name, name, sizeof l->s[i].name);
  return 1;
}
void stylelib_defaults(StyleLib *l)
{
  /* a few to start with, each from PhotoCraft's effect defaults */
  static const struct { const char *name; int k1, k2; u32 c; } st[] = {
    { "Drop Shadow", FX_DROPSHADOW, -1, 0 }, { "Soft Glow", FX_OUTERGLOW, -1, 0xFFFFBE },
    { "Embossed", FX_BEVEL, -1, 0 }, { "Outlined", FX_STROKE, -1, 0x000000 },
    { "Raised Button", FX_BEVEL, FX_DROPSHADOW, 0 }, { "Neon", FX_OUTERGLOW, FX_STROKE, 0x40FF40 }
  };
  int i;
  for (i = 0; i < (int)(sizeof st / sizeof st[0]); i++) {
    LayerFx fx;
    fx_defaults_all(&fx);
    fx.e[st[i].k1].on = 1;
    if (st[i].k2 >= 0) fx.e[st[i].k2].on = 1;
    if (st[i].c && st[i].k1 == FX_OUTERGLOW) fx.e[FX_OUTERGLOW].color = st[i].c;
    if (st[i].k2 == FX_STROKE) { fx.e[FX_STROKE].color = st[i].c; fx.e[FX_STROKE].size = 2; }
    stylelib_add(l, st[i].name, &fx);
  }
}
char *stylelib_save(const StyleLib *l)
{
  Buf b;
  int i;
  buf_init(&b);
  buf_adds(&b, "EaselStyles 1\n");
  for (i = 0; i < l->n; i++) {
    char *t = fx_save(&l->s[i].fx);
    buf_printf(&b, "%s\t%s\n", l->s[i].name, t ? t : "");
    free(t);
  }
  if (b.oom) { buf_free(&b); return NULL; }
  return buf_take(&b);
}
int stylelib_load(StyleLib *l, const char *text, int merge)
{
  char *copy, *p, *ln;
  int ok = 0;
  if (!text || strncmp(text, "EaselStyles", 11)) return 0;
  if (!(copy = str_dup(text))) return 0;
  if (!merge) stylelib_free(l);
  p = copy;
  next_line(&p);
  while ((ln = next_line(&p))) {
    char *t = strchr(ln, '\t');
    LayerFx fx;
    if (!t) continue;
    *t = 0;
    fx_load(&fx, t + 1);
    if (stylelib_add(l, ln, &fx) >= 0) ok = 1;
  }
  free(copy);
  return ok;
}

/* ------------------------------------------------------------------ layer comps */
int comp_capture(LayerComp *c, const Doc *d, const char *name, int what)
{
  int i;
  memset(c, 0, sizeof *c);
  cpy(c->name, name && *name ? name : "Layer Comp", sizeof c->name);
  c->what = (u8)(what ? what : COMP_VIS | COMP_LOOK);
  if (d->nlay && !(c->lay = (CompLayer *)calloc((size_t)d->nlay, sizeof(CompLayer)))) return 0;
  for (i = 0; i < d->nlay; i++) {
    const Layer *l = d->lay[i];
    CompLayer *o = &c->lay[i];
    memcpy(o->name, l->name, sizeof o->name);
    o->visible = l->visible; o->opacity = l->opacity; o->blend = l->blend; o->fill_cut = l->fill_cut; o->fx = l->fx;
  }
  c->nlay = d->nlay;
  return 1;
}
void comp_free(LayerComp *c) { free(c->lay); c->lay = NULL; c->nlay = 0; }
int comp_apply(Doc *d, const LayerComp *c)
{
  u8 used[MAXLAYERS];
  int i, k, changed = 0;
  memset(used, 0, sizeof used);
  doc_begin(d, "Apply Layer Comp");
  for (i = 0; i < d->nlay; i++) {
    Layer *l = d->lay[i];
    const CompLayer *o = NULL;
    int ch = 0;
    for (k = 0; k < c->nlay; k++) if (!used[k] && !strcmp(c->lay[k].name, l->name)) { o = &c->lay[k]; used[k] = 1; break; }
    if (!o && i < c->nlay && !used[i] && !strcmp(c->lay[i].name, l->name)) { o = &c->lay[i]; used[i] = 1; }
    if (!o) continue;
    if ((c->what & COMP_VIS) && l->visible != o->visible) { l->visible = o->visible; ch = 1; }
    if (c->what & COMP_LOOK) {
      if (l->opacity != o->opacity || l->fill_cut != o->fill_cut) { l->opacity = o->opacity; l->fill_cut = o->fill_cut; ch = 1; }
      if (l->blend != o->blend && (o->blend < BM_N || (o->blend == BM_PASS && l->kind == LK_GROUP))) { l->blend = o->blend; ch = 1; }
      if (memcmp(&l->fx, &o->fx, sizeof l->fx)) { doc_set_fx(d, i, &o->fx); ch = 1; }
    }
    changed += ch;
  }
  doc_end(d);
  return changed;
}

/* ------------------------------------------------------------------ histogram */
const char *const histo_channel_names[HC_N] = { "Luminosity", "Red", "Green", "Blue", "Alpha" };
int doc_histogram(Doc *d, Box b, int channel, u32 hist[256])
{
  u8 *row;
  i32 x, y;
  memset(hist, 0, sizeof(u32) * 256);
  if (box_empty(b)) b = d->sel ? d->selbox : doc_box(d);
  b = box_clip(b, doc_box(d));
  if (box_empty(b)) return 1;
  if (!(row = (u8 *)malloc((size_t)(b.x1 - b.x0) * 4))) return 0;
  for (y = b.y0; y < b.y1; y++) {
    doc_composite(d, box_make(b.x0, y, b.x1, y + 1), row, (long)(b.x1 - b.x0) * 4);
    for (x = b.x0; x < b.x1; x++) {
      const u8 *p = row + (x - b.x0) * 4;
      int v, w = sel_at(d, x, y);
      if (w < 128) continue;
      if (channel != HC_ALPHA && !p[3]) continue;
      switch (channel) {
      case HC_RED: v = p[0]; break;
      case HC_GREEN: v = p[1]; break;
      case HC_BLUE: v = p[2]; break;
      case HC_ALPHA: v = p[3]; break;
      default: v = (p[0] * 299 + p[1] * 587 + p[2] * 114 + 500) / 1000; break;
      }
      hist[v]++;
    }
  }
  free(row);
  return 1;
}
void histo_stats(const u32 hist[256], HistoStats *s)
{
  double sum = 0, sq = 0;
  long n = 0, half, acc = 0;
  int i;
  memset(s, 0, sizeof *s);
  s->min = -1;
  for (i = 0; i < 256; i++) {
    if (hist[i]) { if (s->min < 0) s->min = i; s->max = i; }
    n += (long)hist[i]; sum += (double)hist[i] * i;
  }
  s->pixels = n;
  if (!n) { s->min = 0; return; }
  s->mean = sum / n;
  for (i = 0; i < 256; i++) sq += hist[i] * (i - s->mean) * (i - s->mean);
  s->stddev = sqrt(sq / n);
  half = (n + 1) / 2;
  for (i = 0; i < 256; i++) { acc += (long)hist[i]; if (acc >= half) { s->median = i; break; } }
}
