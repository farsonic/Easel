/* avifdump.c - decode a picture with Easel's codecs and write it as raw RGBA (for check_avif.py) */
#include <stdio.h>
#include <stdlib.h>
#include "core/codec.h"
#include "core/avif.h"

int main(int argc, char **argv)
{
  FILE *f;
  long n;
  u8 *d;
  Rgba im;
  char err[200];
  if (argc < 3) { fprintf(stderr, "usage: avifdump in.avif out.rgba\n"); return 2; }
  f = fopen(argv[1], "rb");
  if (!f) return 2;
  fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
  d = malloc(n ? n : 1);
  if (fread(d, 1, n, f) != (size_t)n) return 2;
  fclose(f);
  if (!avif_read(d, n, &im, err, sizeof err)) { printf("FAIL %s\n", err); free(d); return 1; }
  f = fopen(argv[2], "wb");
  fwrite(im.px, 4, (size_t)im.w * im.h, f);
  fclose(f);
  printf("%d %d\n", im.w, im.h);
  rgba_free(&im);
  free(d);
  return 0;
}
