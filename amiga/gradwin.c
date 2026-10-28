/*
 * gradwin.c - Easel 0.5's Gradient Editor (see gradwin.h). One class draws it all in Easel's
 * theme: the presets strip, the name, the gradient bar over a chequer with its opacity stops above
 * and colour stops below (the midpoints of the selected stop's segment as small diamonds), the
 * selected stop's settings as scrub fields, and the buttons.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "gradwin.h"
#include "colorpick.h"
#include "brushwin.h"

#define WHEEL_UP 0x7A
#define WHEEL_DOWN 0x7B
#define PW 44                          /* a preset's swatch */
#define PH 16
#define PROWS 3
#define BARH 26
static int fh(void) { return theme_font()->tf_YSize; }

static GradientList shared;
static int shared_made;
static struct MUI_CustomClass *gcls;
GradientList *gradwin_presets(void)
{
  if (!shared_made) { gradlist_init(&shared); gradlist_builtin(&shared); shared_made = 1; }
  return &shared;
}
void gradwin_free(void)
{
  if (shared_made) gradlist_free(&shared);
  shared_made = 0;
  if (gcls) { MUI_DeleteCustomClass(gcls); gcls = NULL; }
}

/* the parts' places */
typedef struct {
  LONG l, r, sy, cols, ny, bx0, bx1, by0, by1, oy, cy, fy[4], btny[2], fx0;
} GGeo;
enum { F_KIND, F_OPACITY, F_LOCATION, F_MIDPOINT };
static void ggeo(Object *obj, GGeo *g)
{
  LONG rh = fh() + 9;
  int i;
  g->l = _mleft(obj) + 8; g->r = _mright(obj) - 8;
  g->sy = _mtop(obj) + 6;
  g->cols = (g->r - g->l) / (PW + 4); if (g->cols < 1) g->cols = 1;
  g->ny = g->sy + PROWS * (PH + 4) + 6;
  g->oy = g->ny + fh() + 10;                 /* the opacity stops' row */
  g->by0 = g->oy + 12; g->by1 = g->by0 + BARH;
  g->cy = g->by1 + 2;                        /* the colour stops' row */
  g->bx0 = g->l + 6; g->bx1 = g->r - 6;
  for (i = 0; i < 4; i++) g->fy[i] = g->cy + 22 + i * rh;
  g->btny[0] = g->fy[3] + rh + 6; g->btny[1] = g->btny[0] + rh + 2;
  g->fx0 = g->l + (g->r - g->l) * 40 / 100;
}
struct GData {
  struct MUI_EventHandlerNode ehn;
  int shown, selkind, sel, drag, dragmid, firstrow, hot, hotsub, moved, grabv, popfor;
  LONG grabx;
  Gradient *g;
  GradientList *pl;
  int preset;                                /* the preset last picked, -1 */
  UBYTE *buf; LONG bufcap;
};
static const char *const kinds[3] = { "Color", "Foreground", "Background" };
static const char *const btn0[4] = { "New", "Delete", "Load...", "Save..." };
static const char *const btn1[3] = { "Delete Stop", "Reverse", "Noise" };

static LONG pos_x(GGeo *g, double p) { return g->bx0 + (LONG)(p * (g->bx1 - g->bx0) + 0.5); }
static double x_pos(GGeo *g, LONG x) { double p = (double)(x - g->bx0) / (g->bx1 - g->bx0); return p < 0 ? 0 : p > 1 ? 1 : p; }
static u32 stop_rgb(const Gradient *gr, const GradColorStop *s) { return s->kind == GS_FG ? gr->fg : s->kind == GS_BG ? gr->bg : s->rgb; }
static int ensure_buf(struct GData *d, LONG need)
{
  if (need <= d->bufcap) return d->buf != NULL;
  if (d->buf) FreeVec(d->buf);
  d->buf = (UBYTE *)AllocVec((ULONG)need, MEMF_ANY);
  d->bufcap = d->buf ? need : 0;
  return d->buf != NULL;
}
/* a gradient drawn over a chequer into w x h at x, y */
static void draw_grad(struct GData *d, Object *obj, const Gradient *gr, LONG x, LONG y, LONG w, LONG h)
{
  LONG i, j;
  u8 c[4];
  if (w <= 0 || h <= 0 || !ensure_buf(d, w * h * 3)) return;
  for (i = 0; i < w; i++) {
    gradient_sample(gr, w > 1 ? (double)i / (w - 1) : 0, c);
    for (j = 0; j < h; j++) {
      int chk = ((i >> 2) ^ (j >> 2)) & 1 ? 0xCC : 0x88, k;
      UBYTE *o = d->buf + (j * w + i) * 3;
      for (k = 0; k < 3; k++) o[k] = (UBYTE)(chk + ((c[k] - chk) * c[3]) / 255);
    }
  }
  draw_rgb(_rp(obj), _screen(obj), d->buf, w * 3, x, y, w, h);
}
static void scrub_box(struct RastPort *rp, LONG x0, LONG x1, LONG my, const char *label, const char *val, LONG lx, int hot, int frac1000)
{
  LONG vt = my - fh() / 2 - 3, vb = my + fh() / 2 + 3;
  theme_text(rp, theme_font(), lx, my - fh() / 2, label, tpen(hot ? T_TEXT : T_DIM));
  theme_fill(rp, x0, vt, x1, vb, tpen(T_WHITE));
  theme_rect(rp, x0, vt, x1, vb, tpen(hot ? T_ACCENT : T_BORDER2));
  if (frac1000 > 0) theme_fill(rp, x0 + 1, vb - 2, x0 + 1 + (x1 - x0 - 2) * frac1000 / 1000, vb - 1, tpen(T_ACCENT));
  theme_text(rp, theme_font(), x0 + 5, my - fh() / 2, val, tpen(T_TEXT));
}
static void ged_draw(struct GData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  struct Screen *scr = _screen(obj);
  GGeo g;
  Gradient *gr = d->g;
  int i, n = d->pl ? d->pl->n : 0;
  char buf[80];
  ggeo(obj, &g);
  theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_PANEL));
  /* the presets */
  if (d->firstrow > (n + g.cols - 1) / g.cols - PROWS) d->firstrow = (n + (int)g.cols - 1) / (int)g.cols - PROWS;
  if (d->firstrow < 0) d->firstrow = 0;
  for (i = d->firstrow * g.cols; i < n && i < (d->firstrow + PROWS) * g.cols; i++) {
    int k = i - d->firstrow * (int)g.cols;
    LONG x = g.l + (k % g.cols) * (PW + 4), y = g.sy + (k / g.cols) * (PH + 4);
    Gradient *p = &d->pl->g[i];
    p->fg = gr->fg; p->bg = gr->bg;
    draw_grad(d, obj, p, x, y, PW, PH);
    theme_rect(rp, x - 1, y - 1, x + PW, y + PH, tpen(i == d->preset ? T_ACCENT : d->hot == 100 + i ? T_TEXT : T_BORDER2));
  }
  /* the name */
  utf8_to_latin1(gr->name, strlen(gr->name), buf, sizeof buf);
  theme_text(rp, theme_font(), g.l, g.ny, tr("Name:"), tpen(T_DIM));
  theme_text(rp, theme_bold(), g.l + theme_textlen(rp, theme_font(), tr("Name:")) + 8, g.ny, buf, tpen(d->hot == 1 ? T_ACCENT : T_TEXT));
  /* the bar and its stops */
  draw_grad(d, obj, gr, g.bx0, g.by0, g.bx1 - g.bx0 + 1, BARH);
  theme_rect(rp, g.bx0 - 1, g.by0 - 1, g.bx1 + 1, g.by1, tpen(T_BORDER2));
  for (i = 0; i < gr->nopacity; i++) {
    LONG x = pos_x(&g, gr->opacity[i].pos), y = g.oy, gv = (LONG)(gr->opacity[i].opacity * 255 + 0.5);
    int on = d->selkind == 1 && d->sel == i;
    draw_rgb_box(rp, scr, x - 4, y, x + 4, y + 7, (ULONG)(gv * 0x010101));
    theme_rect(rp, x - 5, y - 1, x + 5, y + 8, tpen(on ? T_ACCENT : T_TEXT));
    SetAPen(rp, tpen(on ? T_ACCENT : T_TEXT)); Move(rp, x, y + 8); Draw(rp, x, y + 10);
  }
  for (i = 0; i < gr->ncolor; i++) {
    LONG x = pos_x(&g, gr->color[i].pos), y = g.cy;
    int on = d->selkind == 0 && d->sel == i;
    SetAPen(rp, tpen(on ? T_ACCENT : T_TEXT)); Move(rp, x, y); Draw(rp, x, y + 3);
    draw_rgb_box(rp, scr, x - 4, y + 4, x + 4, y + 12, stop_rgb(gr, &gr->color[i]));
    theme_rect(rp, x - 5, y + 3, x + 5, y + 13, tpen(on ? T_ACCENT : T_TEXT));
    if (gr->color[i].kind != GS_RGB) theme_text(rp, theme_small(), x - 2, y + 14, gr->color[i].kind == GS_FG ? "F" : "B", tpen(T_DIM));
  }
  /* the selected stop's midpoint, a diamond in its row */
  {
    int cnt = d->selkind ? gr->nopacity : gr->ncolor;
    if (d->sel >= 0 && d->sel < cnt - 1) {
      double p0 = d->selkind ? gr->opacity[d->sel].pos : gr->color[d->sel].pos, p1 = d->selkind ? gr->opacity[d->sel + 1].pos : gr->color[d->sel + 1].pos;
      double m = d->selkind ? gr->opacity[d->sel].mid : gr->color[d->sel].mid;
      LONG x = pos_x(&g, p0 + (p1 - p0) * m), y = d->selkind ? g.oy + 3 : g.cy + 8;
      SetAPen(rp, tpen(T_ACCENT));
      Move(rp, x, y - 3); Draw(rp, x + 3, y); Draw(rp, x, y + 3); Draw(rp, x - 3, y); Draw(rp, x, y - 3);
    }
  }
  /* the fields */
  if (d->sel >= 0) {
    LONG x0 = g.fx0, x1 = g.r, lx = g.l;
    if (d->selkind == 0 && d->sel < gr->ncolor) {
      GradColorStop *s = &gr->color[d->sel];
      LONG my = g.fy[F_KIND] + (fh() + 9) / 2;
      theme_text(rp, theme_font(), lx, my - fh() / 2, tr("Color:"), tpen(T_DIM));
      draw_rgb_box(rp, scr, x0, my - fh() / 2 - 3, x0 + 30, my + fh() / 2 + 3, stop_rgb(gr, s));
      theme_rect(rp, x0 - 1, my - fh() / 2 - 4, x0 + 31, my + fh() / 2 + 4, tpen(d->hot == 2 ? T_ACCENT : T_BORDER2));
      theme_fill(rp, x0 + 38, my - fh() / 2 - 3, x1, my + fh() / 2 + 3, tpen(T_WHITE));
      theme_rect(rp, x0 + 38, my - fh() / 2 - 3, x1, my + fh() / 2 + 3, tpen(d->hot == 3 ? T_ACCENT : T_BORDER2));
      theme_text(rp, theme_font(), x0 + 43, my - fh() / 2, tr(kinds[s->kind >= 0 && s->kind < 3 ? s->kind : 0]), tpen(T_TEXT));
      sprintf(buf, "%d%%", (int)floor(s->pos * 100 + 0.5));
      scrub_box(rp, x0, x1, g.fy[F_LOCATION] + (fh() + 9) / 2, tr("Location:"), buf, lx, d->hot == 10 + F_LOCATION, (int)(s->pos * 1000));
      if (d->sel < gr->ncolor - 1) {
        sprintf(buf, "%d%%", (int)floor(s->mid * 100 + 0.5));
        scrub_box(rp, x0, x1, g.fy[F_MIDPOINT] + (fh() + 9) / 2, tr("Midpoint:"), buf, lx, d->hot == 10 + F_MIDPOINT, (int)(s->mid * 1000));
      }
    } else if (d->selkind == 1 && d->sel < gr->nopacity) {
      GradOpacityStop *s = &gr->opacity[d->sel];
      sprintf(buf, "%d%%", (int)floor(s->opacity * 100 + 0.5));
      scrub_box(rp, x0, x1, g.fy[F_OPACITY] + (fh() + 9) / 2, tr("Opacity:"), buf, lx, d->hot == 10 + F_OPACITY, (int)(s->opacity * 1000));
      sprintf(buf, "%d%%", (int)floor(s->pos * 100 + 0.5));
      scrub_box(rp, x0, x1, g.fy[F_LOCATION] + (fh() + 9) / 2, tr("Location:"), buf, lx, d->hot == 10 + F_LOCATION, (int)(s->pos * 1000));
      if (d->sel < gr->nopacity - 1) {
        sprintf(buf, "%d%%", (int)floor(s->mid * 100 + 0.5));
        scrub_box(rp, x0, x1, g.fy[F_MIDPOINT] + (fh() + 9) / 2, tr("Midpoint:"), buf, lx, d->hot == 10 + F_MIDPOINT, (int)(s->mid * 1000));
      }
    }
  } else theme_text(rp, theme_font(), g.l, g.fy[0] + 4, tr("Click a stop to change it; click beside the bar to add one."), tpen(T_GROUPLABEL));
  /* the buttons */
  for (i = 0; i < 2; i++) {
    int k, nb = i ? 3 : 4;
    LONG bw = (g.r - g.l - (nb - 1) * 4) / nb, my = g.btny[i] + (fh() + 9) / 2;
    for (k = 0; k < nb; k++) {
      LONG x0 = g.l + k * (bw + 4), x1 = x0 + bw - 1, tw;
      const char *lab = tr(i ? btn1[k] : btn0[k]);
      int hot = d->hot == 20 + i && d->hotsub == k;
      theme_fill(rp, x0, my - fh() / 2 - 3, x1, my + fh() / 2 + 3, tpen(hot ? T_HOVER : T_WHITE));
      theme_rect(rp, x0, my - fh() / 2 - 3, x1, my + fh() / 2 + 3, tpen(T_BORDER2));
      tw = theme_textlen(rp, theme_font(), lab);
      theme_text(rp, theme_font(), x0 + (bw - tw) / 2, my - fh() / 2, lab, tpen(T_TEXT));
    }
  }
}
/* what is under the pointer: 100+i a preset, 1 the name, 2 the colour swatch, 3 the kind, 10+F_ a
   field, 20/21 a button row (*sub the button), 30 the opacity row (*sub the stop, or -1),
   31 the colour row, 32 the midpoint diamond; -1 nothing */
static int ged_hit(struct GData *d, Object *obj, LONG mx, LONG my, int *sub)
{
  GGeo g;
  Gradient *gr = d->g;
  int i;
  LONG rh = fh() + 9;
  ggeo(obj, &g);
  *sub = -1;
  if (mx < _mleft(obj) || mx > _mright(obj) || my < _mtop(obj) || my > _mbottom(obj)) return -1;
  if (my >= g.sy && my < g.sy + PROWS * (PH + 4)) {
    int c = (int)((mx - g.l) / (PW + 4)), r = (int)((my - g.sy) / (PH + 4)), k = (d->firstrow + r) * (int)g.cols + c;
    return c >= 0 && c < g.cols && d->pl && k < d->pl->n ? 100 + k : -1;
  }
  if (my >= g.ny && my < g.ny + fh() + 2) return 1;
  /* the midpoint diamond first (it sits between stops) */
  {
    int cnt = d->selkind ? gr->nopacity : gr->ncolor;
    if (d->sel >= 0 && d->sel < cnt - 1) {
      double p0 = d->selkind ? gr->opacity[d->sel].pos : gr->color[d->sel].pos, p1 = d->selkind ? gr->opacity[d->sel + 1].pos : gr->color[d->sel + 1].pos;
      double m = d->selkind ? gr->opacity[d->sel].mid : gr->color[d->sel].mid;
      LONG x = pos_x(&g, p0 + (p1 - p0) * m), y = d->selkind ? g.oy + 3 : g.cy + 8;
      if (labs(mx - x) <= 3 && labs(my - y) <= 4) return 32;
    }
  }
  if (my >= g.oy - 2 && my < g.by0) {
    for (i = 0; i < gr->nopacity; i++) if (labs(mx - pos_x(&g, gr->opacity[i].pos)) <= 5) { *sub = i; break; }
    return 30;
  }
  if (my >= g.by1 && my < g.cy + 16) {
    for (i = 0; i < gr->ncolor; i++) if (labs(mx - pos_x(&g, gr->color[i].pos)) <= 5) { *sub = i; break; }
    return 31;
  }
  for (i = 0; i < 4; i++)
    if (my >= g.fy[i] && my < g.fy[i] + rh && d->sel >= 0) {
      if (i == F_KIND) {
        if (d->selkind != 0) return -1;
        if (mx >= g.fx0 && mx <= g.fx0 + 31) return 2;
        if (mx >= g.fx0 + 38 && mx <= g.r) return 3;
        return -1;
      }
      if (i == F_OPACITY && d->selkind != 1) return -1;
      if (mx >= g.fx0 && mx <= g.r) return 10 + i;
      return -1;
    }
  for (i = 0; i < 2; i++)
    if (my >= g.btny[i] && my < g.btny[i] + rh) {
      int nb = i ? 3 : 4;
      LONG bw = (g.r - g.l - (nb - 1) * 4) / nb;
      int k = (int)((mx - g.l) / (bw + 4));
      if (k < 0 || k >= nb) return -1;
      *sub = k;
      return 20 + i;
    }
  return -1;
}
static void ged_changed(struct GData *d, Object *obj)
{
  if (d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
  SetAttrs(obj, MUIA_GradEd_Changed, TRUE, TAG_DONE);
}
/* the selected stop keeps being selected while the stops are sorted */
static void sort_keep(struct GData *d)
{
  Gradient *gr = d->g;
  if (d->selkind == 0 && d->sel >= 0 && d->sel < gr->ncolor) {
    GradColorStop s = gr->color[d->sel];
    int i;
    gradient_sort(gr);
    for (i = 0; i < gr->ncolor; i++) if (!memcmp(&gr->color[i], &s, sizeof s)) { d->sel = i; break; }
  } else if (d->selkind == 1 && d->sel >= 0 && d->sel < gr->nopacity) {
    GradOpacityStop s = gr->opacity[d->sel];
    int i;
    gradient_sort(gr);
    for (i = 0; i < gr->nopacity; i++) if (!memcmp(&gr->opacity[i], &s, sizeof s)) { d->sel = i; break; }
  } else gradient_sort(gr);
}
static void ged_button(struct GData *d, Object *obj, int row, int k)
{
  Gradient *gr = d->g;
  static char path[512];
  if (row == 0) switch (k) {
  case 0: {
    char name[64];
    strncpy(name, gr->name[0] ? gr->name : "Custom", sizeof name - 1); name[sizeof name - 1] = 0;
    if (!d->pl || !ask_string("New Gradient", "Name:", name, sizeof name)) return;
    strncpy(gr->name, name, sizeof gr->name - 1);
    if (gradlist_add(d->pl, gr)) d->preset = d->pl->n - 1;
    break; }
  case 1:
    if (!d->pl || d->preset < 0 || d->preset >= d->pl->n) { ask("OK", "%s", tr("Pick a preset to delete first.")); return; }
    gradlist_remove(d->pl, d->preset);
    d->preset = -1;
    break;
  case 2:
    if (!d->pl || !ui_file_request(0, "Load Gradients", path, sizeof path, "#?.(gradients|ggr)")) return;
    if (!gradlist_load(d->pl, path)) ask("OK", "%s", tr("No gradients could be read from that file."));
    break;
  case 3:
    if (!ui_file_request(1, "Save Gradients", path, sizeof path, "#?.(gradients|ggr)")) return;
    {
      size_t n = strlen(path);
      int ok;
      if (n > 4 && str_ieq(path + n - 4, ".ggr")) {
        /* GIMP's files hold one gradient: the one being edited */
        GradientList one;
        gradlist_init(&one);
        ok = gradlist_add(&one, gr) && gradlist_save(&one, path);
        gradlist_free(&one);
      } else ok = d->pl && gradlist_save(d->pl, path);
      if (!ok) ask("OK", "%s", tr("The gradients couldn't be saved."));
    }
    return;
  } else switch (k) {
  case 0:
    if (d->selkind == 0 ? gradient_del_color(gr, d->sel) : gradient_del_opacity(gr, d->sel)) d->sel = -1;
    break;
  case 1: gradient_reverse(gr); d->sel = -1; break;
  case 2: {
    LONG rough = 50;
    u32 fg = gr->fg, bg = gr->bg;
    if (!ask_number("Noise Gradient", "Roughness (per cent):", &rough, 0, 100)) return;
    gradient_noise(gr, (u32)(brush_hash((u32)(size_t)gr, (u32)rough, 7) & 0xFFFF), 12, (int)rough, 0);
    gr->fg = fg; gr->bg = bg;
    d->sel = -1;
    break; }
  }
  ged_changed(d, obj);
}
static ULONG gdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct GData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->sel = d->drag = d->hot = d->preset = -1;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY;
    return (ULONG)obj;
  case OM_DISPOSE:
    d = INST_DATA(cl, obj);
    if (d->buf) FreeVec(d->buf);
    break;
  case OM_SET: {
    struct TagItem *t;
    d = INST_DATA(cl, obj);
    if ((t = FindTagItem(MUIA_Popup_Chosen, ((struct opSet *)msg)->ops_AttrList)) && d->selkind == 0 && d->sel >= 0 && d->sel < d->g->ncolor) {
      d->g->color[d->sel].kind = (int)t->ti_Data;
      ged_changed(d, obj);
    }
    break;
  }
  case OM_GET:
    if (((struct opGet *)msg)->opg_AttrID == MUIA_GradEd_Changed) { *((struct opGet *)msg)->opg_Storage = TRUE; return TRUE; }
    break;
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    LONG h = 6 + PROWS * (PH + 4) + 6 + fh() + 10 + 12 + BARH + 2 + 22 + 4 * (fh() + 9) + 6 + 2 * (fh() + 11) + 8;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 360; m->MinMaxInfo->DefWidth += 440; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += h; m->MinMaxInfo->DefHeight += h; m->MinMaxInfo->MaxHeight += h;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct GData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup: theme_cleanup(obj); break;
  case MUIM_Show: ((struct GData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct GData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) ged_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_CreateShortHelp: {
    struct MUIP_CreateShortHelp *m = (struct MUIP_CreateShortHelp *)msg;
    static char name[80];
    int sub, h;
    d = INST_DATA(cl, obj);
    h = ged_hit(d, obj, m->mx, m->my, &sub);
    if (h >= 100 && d->pl) { utf8_to_latin1(d->pl->g[h - 100].name, strlen(d->pl->g[h - 100].name), name, sizeof name); return (ULONG)theme_bubble(name, tr("Click to edit a copy of this gradient.")); }
    if (h == 30) return (ULONG)theme_bubble(tr("Opacity Stops"), tr("Drag a stop along to move it, off the bar to delete it; click beside the bar to add one."));
    if (h == 31) return (ULONG)theme_bubble(tr("Color Stops"), tr("Drag a stop along to move it, off the bar to delete it; click beside the bar to add one."));
    if (h == 32) return (ULONG)theme_bubble(tr("Midpoint"), tr("Where the colours are half way between this stop and the next."));
    return 0;
  }
  case MUIM_DeleteShortHelp: return 0;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    int h, sub;
    GGeo g;
    Gradient *gr;
    d = INST_DATA(cl, obj);
    if (!im || !d->shown || !d->g) return 0;
    gr = d->g;
    ggeo(obj, &g);
    if (im->Class == IDCMP_RAWKEY) {
      if ((im->Code == WHEEL_UP || im->Code == WHEEL_DOWN) && im->MouseY >= g.sy && im->MouseY < g.sy + PROWS * (PH + 4) && im->MouseX >= _mleft(obj) && im->MouseX <= _mright(obj)) {
        d->firstrow += im->Code == WHEEL_UP ? -1 : 1;
        MUI_Redraw(obj, MADF_DRAWOBJECT);
        return MUI_EventHandlerRC_Eat;
      }
      return 0;
    }
    if (im->Class == IDCMP_MOUSEMOVE) {
      if (d->drag == 30 || d->drag == 31) {
        /* a stop dragged along, or off (well away from the bar) to be deleted */
        int col = d->drag == 31, cnt = col ? gr->ncolor : gr->nopacity;
        LONG far = col ? im->MouseY - (g.cy + 30) : (g.oy - 20) - im->MouseY;
        if (d->sel < 0 || d->sel >= cnt) return MUI_EventHandlerRC_Eat;
        if (far > 0 && cnt > 1) {
          if (col) gradient_del_color(gr, d->sel); else gradient_del_opacity(gr, d->sel);
          d->sel = -1; d->drag = -1;
        } else {
          double p = x_pos(&g, im->MouseX);
          if (col) gr->color[d->sel].pos = p; else gr->opacity[d->sel].pos = p;
          sort_keep(d);
        }
        ged_changed(d, obj);
        return MUI_EventHandlerRC_Eat;
      }
      if (d->drag == 32) {
        int col = d->selkind == 0, cnt = col ? gr->ncolor : gr->nopacity;
        if (d->sel >= 0 && d->sel < cnt - 1) {
          double p0 = col ? gr->color[d->sel].pos : gr->opacity[d->sel].pos, p1 = col ? gr->color[d->sel + 1].pos : gr->opacity[d->sel + 1].pos, m;
          m = p1 > p0 ? (x_pos(&g, im->MouseX) - p0) / (p1 - p0) : 0.5;
          if (m < 0.05) m = 0.05;
          if (m > 0.95) m = 0.95;
          if (col) gr->color[d->sel].mid = m; else gr->opacity[d->sel].mid = m;
          ged_changed(d, obj);
        }
        return MUI_EventHandlerRC_Eat;
      }
      if (d->drag >= 10 && d->drag < 14) {
        /* a field scrubbed: 0-100 (midpoint 5-95) per cent */
        LONG dx = im->MouseX - d->grabx, bw = g.r - g.fx0;
        int lo = d->drag == 10 + F_MIDPOINT ? 5 : 0, hi = d->drag == 10 + F_MIDPOINT ? 95 : 100, v = d->grabv + (int)(dx * 100 / (bw > 20 ? bw : 20));
        if (dx > 2 || dx < -2) d->moved = 1;
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        if (d->moved && d->sel >= 0) {
          if (d->drag == 10 + F_OPACITY) gr->opacity[d->sel].opacity = v / 100.0;
          else if (d->drag == 10 + F_MIDPOINT) { if (d->selkind) gr->opacity[d->sel].mid = v / 100.0; else gr->color[d->sel].mid = v / 100.0; }
          else { if (d->selkind) gr->opacity[d->sel].pos = v / 100.0; else gr->color[d->sel].pos = v / 100.0; sort_keep(d); }
          ged_changed(d, obj);
        }
        return MUI_EventHandlerRC_Eat;
      }
      h = ged_hit(d, obj, im->MouseX, im->MouseY, &sub);
      if (h != d->hot || sub != d->hotsub) { d->hot = h; d->hotsub = sub; MUI_Redraw(obj, MADF_DRAWOBJECT); }
      return 0;
    }
    if (im->Class != IDCMP_MOUSEBUTTONS) return 0;
    if (im->Code == SELECTUP) {
      if (d->drag >= 10 && d->drag < 14 && !d->moved && d->sel >= 0) {
        LONG v = d->grabv;
        int lo = d->drag == 10 + F_MIDPOINT ? 5 : 0, hi = d->drag == 10 + F_MIDPOINT ? 95 : 100;
        if (ask_number("Gradient Editor", "Per cent:", &v, lo, hi)) {
          if (d->drag == 10 + F_OPACITY) gr->opacity[d->sel].opacity = v / 100.0;
          else if (d->drag == 10 + F_MIDPOINT) { if (d->selkind) gr->opacity[d->sel].mid = v / 100.0; else gr->color[d->sel].mid = v / 100.0; }
          else { if (d->selkind) gr->opacity[d->sel].pos = v / 100.0; else gr->color[d->sel].pos = v / 100.0; sort_keep(d); }
        }
        d->drag = -1;
        ged_changed(d, obj);
        return MUI_EventHandlerRC_Eat;
      }
      if (d->drag >= 0) { d->drag = -1; return MUI_EventHandlerRC_Eat; }
      return 0;
    }
    if (im->Code != SELECTDOWN) return 0;
    if ((h = ged_hit(d, obj, im->MouseX, im->MouseY, &sub)) < 0) return 0;
    if (h >= 100 && d->pl) {
      u32 fg = gr->fg, bg = gr->bg;
      *gr = d->pl->g[h - 100];
      gr->fg = fg; gr->bg = bg;
      d->preset = h - 100; d->sel = -1;
      ged_changed(d, obj);
    } else if (h == 1) {
      char name[64];
      strncpy(name, gr->name, sizeof name - 1); name[sizeof name - 1] = 0;
      if (ask_string("Gradient Name", "Name:", name, sizeof name)) { strncpy(gr->name, name, sizeof gr->name - 1); ged_changed(d, obj); }
    } else if (h == 2 && d->selkind == 0 && d->sel >= 0) {
      LONG c = colorpick_run("Stop Colour", stop_rgb(gr, &gr->color[d->sel]));
      if (c >= 0) { gr->color[d->sel].rgb = (u32)c; gr->color[d->sel].kind = GS_RGB; ged_changed(d, obj); }
    } else if (h == 3 && d->selkind == 0 && d->sel >= 0) {
      struct Window *w = _window(obj);
      popup_list(obj, w->LeftEdge + g.fx0 + 38, w->TopEdge + g.fy[F_KIND] + fh() + 9, g.r - g.fx0 - 38, kinds, 3, gr->color[d->sel].kind, MUIA_Popup_Chosen);
    } else if (h >= 10 && h < 14) {
      d->drag = h; d->grabx = im->MouseX; d->moved = 0;
      if (h == 10 + F_OPACITY) d->grabv = (int)(gr->opacity[d->sel].opacity * 100 + 0.5);
      else if (h == 10 + F_MIDPOINT) d->grabv = (int)((d->selkind ? gr->opacity[d->sel].mid : gr->color[d->sel].mid) * 100 + 0.5);
      else d->grabv = (int)((d->selkind ? gr->opacity[d->sel].pos : gr->color[d->sel].pos) * 100 + 0.5);
    } else if (h == 20 || h == 21) ged_button(d, obj, h - 20, sub);
    else if (h == 32) d->drag = 32;
    else if (h == 30 || h == 31) {
      int col = h == 31;
      if (sub < 0) {
        /* beside the bar: a new stop, with the colour (or opacity) there */
        double p = x_pos(&g, im->MouseX);
        u8 c[4];
        gradient_sample(gr, p, c);
        if (col ? gradient_add_color(gr, p, GS_RGB, ((u32)c[0] << 16) | ((u32)c[1] << 8) | c[2]) : gradient_add_opacity(gr, p, c[3] / 255.0)) {
          int i, cnt = col ? gr->ncolor : gr->nopacity;
          for (i = 0; i < cnt; i++) if (fabs((col ? gr->color[i].pos : gr->opacity[i].pos) - p) < 1e-9) sub = i;
        }
      }
      d->selkind = col ? 0 : 1; d->sel = sub; d->drag = sub >= 0 ? h : -1;
      ged_changed(d, obj);
    }
    return MUI_EventHandlerRC_Eat;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
int gradwin_edit(Gradient *gin, GradientList *presets, u32 fg, u32 bg)
{
  static const ULONG ends[] = { ID_DLGOK, 0 };
  Gradient *work;
  Object *ed, *w;
  struct GData *d;
  int ok;
  if (!gcls && !(gcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct GData), (APTR)gdispatch))) return 0;
  if (!(work = (Gradient *)AllocVec(sizeof *work, MEMF_ANY))) return 0;
  *work = *gin;
  work->fg = fg; work->bg = bg;
  if (work->ncolor < 1) gradient_init(work, "Custom", fg, bg);
  gradient_sort(work);
  if (!(ed = NewObject(gcls->mcc_Class, NULL, MUIA_FillArea, FALSE, TAG_DONE))) { FreeVec(work); return 0; }
  d = INST_DATA(gcls->mcc_Class, ed);
  d->g = work; d->pl = presets;
  w = dialog_window("Gradient Editor", VGroup, Child, ed, End, NULL);
  if (!w) { FreeVec(work); return 0; }
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  if (ok) { *gin = *work; gin->fg = fg; gin->bg = bg; }
  MUI_DisposeObject(w);
  FreeVec(work);
  return ok;
}
