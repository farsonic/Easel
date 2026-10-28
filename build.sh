#!/bin/sh
# Build Easel for AmigaOS (vbcc + NDK 3.2 + MUI 3.8 headers, 68040 with its FPU) after running the
# native core and codec tests (with the address and undefined-behaviour sanitizers).
set -e
export VBCC=${VBCC:-/opt/amiga/vbcc}
export PATH=$VBCC/bin:$PATH
NDK=${NDK:-/opt/amiga/ndk/Include_H}
MUI=${MUI:-/opt/amiga/mui/include}
cd "$(dirname "$0")"
CORE="core/util.c core/zip.c core/xml.c core/image.c core/doc.c core/paint.c core/stroke.c core/brush.c core/preset.c core/gradient.c core/pattern.c core/swatch.c core/adjust.c core/layers.c core/filters2.c core/selmore.c core/text.c core/fonts.c core/ttraster.c core/codec.c core/png.c core/jpegenc.c core/bmp.c core/ilbm.c core/gif.c core/ora.c core/avif.c core/tiff.c core/psd.c core/rasters.c core/webp.c core/amigafmt.c core/palette.c core/gifenc.c core/amigamodes.c core/guides.c"
if [ "$1" != "fast" ]; then
  av1/build-av1.sh native /tmp/easel_dav1d
  # each test linked with the whole core (and dav1d)
  t() { cc -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. -Iav1/include $CORE tests/$1.c /tmp/easel_dav1d/libdav1d.a -o /tmp/easel_$1 -lm -lpthread; }
  t coretest;     /tmp/easel_coretest | tail -2
  t codectest;    /tmp/easel_codectest | tail -2
  t rasterstest;  /tmp/easel_rasterstest 20 | tail -2
  t amigafmttest; /tmp/easel_amigafmttest | tail -2
  t layertest;    /tmp/easel_layertest | tail -2
  t quicktest;    /tmp/easel_quicktest | tail -1
  # 0.4: palettes, indexed documents and the Amiga writers, then their independent checks (Pillow)
  t palettetest;  /tmp/easel_palettetest | tail -2
  python3 tests/check_palette.py | tail -2
  # 0.5: brushes, strokes, gradients, patterns, presets
  t brushtest;    /tmp/easel_brushtest | tail -2
fi
av1/build-av1.sh amiga
vc +tools/vbcc-aos68k-fpu -c99 -O2 -cpu=68040 -fpu=68040 -I. -Iav1/include -I"$MUI" -I"$NDK" amiga/*.c amiga/bigstack.s $CORE av1/obj/*.o -o Easel -lamiga -lm040
ls -l Easel
