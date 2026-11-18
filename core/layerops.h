/*
 * layerops.h - more of the Layer menu (Easel 1.1): colour labels, Delete Hidden Layers, Arrange >
 * Reverse, New > Background from Layer, Copy CSS, and Smart Objects > Stack Mode (each picked
 * layer a sample, the new layer their statistic pixel by pixel). Each is one undo step.
 */
#ifndef EASEL_LAYEROPS_H
#define EASEL_LAYEROPS_H
#include "doc.h"

#define NLABELS 8
extern const char *const label_names[NLABELS];       /* "None", "Red", ... "Gray" */
extern const u32 label_colors[NLABELS];              /* what the Layers panel shows them with */
int  doc_set_label(Doc *d, int label);               /* the picked layers (and the active one) */
int  doc_delete_hidden(Doc *d);                      /* every hidden layer and group; how many went (0: none, or all were) */
int  doc_reverse_layers(Doc *d);                     /* the picked layers at the active one's level, in reverse order */
int  doc_background_from_layer(Doc *d);              /* the active pixel layer made the opaque Background at the bottom */
/* Copy CSS: the active layer as a CSS rule (its box, opacity, mode, fill, shadow, stroke), malloc'd */
char *layer_css(Doc *d, int i);

enum { STK_MEAN, STK_MEDIAN, STK_MIN, STK_MAX, STK_RANGE, STK_SUM, STK_VARIANCE, STK_STDDEV,
       STK_ENTROPY, STK_SKEWNESS, STK_KURTOSIS, STK_N };
extern const char *const stack_names[STK_N];
/* a new layer on top: the statistic of the picked pixel layers (all the visible ones when fewer
   than two are picked) at each pixel and channel, counting only where a layer isn't transparent.
   Its index, or -1 (fewer than two layers, out of memory, aborted) */
int  doc_stack_mode(Doc *d, int mode);
#endif
