/*
 * psdfx.h - Easel 1.2: layer styles in Photoshop files (psdfx.c): the "lfx2" tagged block written
 * from a LayerFx and read into one.
 */
#ifndef EASEL_PSDFX_H
#define EASEL_PSDFX_H
#include "doc.h"
/* the block (8BIMlfx2, its length, the descriptor) appended to b when some effect is on */
void psdfx_write(Buf *b, const LayerFx *fx);
/* an lfx2 block's data (len bytes after its length) into fx; 1 if any effect was read */
int  psdfx_read(const u8 *d, u32 len, LayerFx *fx);
#endif
