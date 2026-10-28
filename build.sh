#!/bin/sh
# Build Easel for AmigaOS (vbcc + NDK 3.2 + MUI 3.8 headers, 68040 with its FPU) after running the
# native core and codec tests (with the address and undefined-behaviour sanitizers).
set -e
export VBCC=${VBCC:-/opt/amiga/vbcc}
export PATH=$VBCC/bin:$PATH
NDK=${NDK:-/opt/amiga/ndk/Include_H}
MUI=${MUI:-/opt/amiga/mui/include}
cd "$(dirname "$0")"
CORE="core/util.c core/zip.c core/xml.c core/image.c core/doc.c core/paint.c core/adjust.c core/layers.c core/filters2.c core/selmore.c core/text.c core/fonts.c core/ttraster.c core/codec.c core/png.c core/jpegenc.c core/bmp.c core/ilbm.c core/gif.c core/ora.c core/avif.c core/tiff.c core/psd.c core/rasters.c core/webp.c core/amigafmt.c"
if [ "$1" != "fast" ]; then
  av1/build-av1.sh native /tmp/easel_dav1d
  cc -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. core/util.c core/doc.c core/paint.c core/adjust.c core/text.c core/fonts.c core/ttraster.c tests/coretest.c -o /tmp/easel_coretest -lm
  /tmp/easel_coretest | tail -2
  cc -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. core/util.c core/zip.c core/xml.c core/image.c core/doc.c core/codec.c core/png.c core/jpegenc.c core/bmp.c core/ilbm.c core/gif.c core/ora.c core/avif.c core/tiff.c core/psd.c core/rasters.c core/webp.c core/amigafmt.c tests/codectest.c /tmp/easel_dav1d/libdav1d.a -Iav1/include -o /tmp/easel_codectest -lm
  (cd . && /tmp/easel_codectest | tail -2)
  cc -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. core/util.c core/zip.c core/xml.c core/image.c core/doc.c core/codec.c core/png.c core/jpegenc.c core/bmp.c core/ilbm.c core/gif.c core/ora.c core/avif.c core/tiff.c core/psd.c core/rasters.c core/webp.c core/amigafmt.c tests/rasterstest.c /tmp/easel_dav1d/libdav1d.a -Iav1/include -o /tmp/easel_rasterstest -lm
  /tmp/easel_rasterstest 20 | tail -2
  cc -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. core/util.c core/zip.c core/xml.c core/image.c core/doc.c core/codec.c core/png.c core/jpegenc.c core/bmp.c core/ilbm.c core/gif.c core/ora.c core/avif.c core/tiff.c core/psd.c core/rasters.c core/webp.c core/amigafmt.c tests/amigafmttest.c /tmp/easel_dav1d/libdav1d.a -Iav1/include -o /tmp/easel_amigafmttest -lm
  /tmp/easel_amigafmttest | tail -2
  cc -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. -Iav1/include core/util.c core/zip.c core/xml.c core/image.c core/doc.c core/paint.c core/adjust.c core/layers.c core/codec.c core/png.c core/jpegenc.c core/bmp.c core/ilbm.c core/gif.c core/ora.c core/avif.c core/tiff.c core/psd.c core/rasters.c core/webp.c core/amigafmt.c tests/layertest.c /tmp/easel_dav1d/libdav1d.a -o /tmp/easel_layertest -lm
  /tmp/easel_layertest | tail -2
  cc -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. core/util.c core/doc.c core/paint.c core/adjust.c core/filters2.c core/selmore.c tests/quicktest.c -o /tmp/easel_quicktest -lm
  /tmp/easel_quicktest | tail -1
fi
av1/build-av1.sh amiga
vc +tools/vbcc-aos68k-fpu -c99 -O2 -cpu=68040 -fpu=68040 -I. -Iav1/include -I"$MUI" -I"$NDK" amiga/*.c amiga/bigstack.s $CORE av1/obj/*.o -o Easel -lamiga -lm040
ls -l Easel
