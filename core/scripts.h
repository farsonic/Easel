/*
 * scripts.h - File > Scripts and Automate (Easel 1.1): Delete All Empty Layers, Flatten All Masks
 * and Fit Image, each one undo step; and Type > Paste Lorem Ipsum's text. Portable C99.
 */
#ifndef EASEL_SCRIPTS_H
#define EASEL_SCRIPTS_H

#include "doc.h"

/* every pixel layer with nothing in it (all transparent, no mask) and every empty group deleted,
   one step; how many went (0: none, and no step). The last layer is always kept */
int  doc_delete_empty_layers(Doc *d);
/* every layer mask applied to its layer's transparency (and deleted), one step; how many */
int  doc_flatten_all_masks(Doc *d);
/* the picture resized (bicubic) to fit inside maxw x maxh, its proportions kept; enlarge 0 leaves a
   picture that already fits alone. 1 if resized, 0 if nothing changed or out of memory */
int  doc_fit_image(Doc *d, i32 maxw, i32 maxh, int enlarge);
/* "Lorem ipsum dolor sit amet..." (about words words, at least 1), UTF-8, malloc'd */
char *lorem_ipsum(int words);

#endif
