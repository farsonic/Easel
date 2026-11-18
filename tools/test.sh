#!/bin/sh
# Easel's native tests (no Amiga toolchain needed): dav1d built with the sanitizers, then every core/codec
# test with ASan/UBSan, then the Python checks (need Pillow). The same steps build.sh runs before the
# Amiga build. TMP=<dir> to build elsewhere than /tmp.
set -e
cd "$(dirname "$0")/.."
T=${TMP:-/tmp}
CORE="core/util.c core/zip.c core/xml.c core/image.c core/doc.c core/paint.c core/stroke.c core/brush.c core/preset.c core/gradient.c core/pattern.c core/swatch.c core/adjust.c core/layers.c core/filters2.c core/selmore.c core/text.c core/fonts.c core/ttraster.c core/codec.c core/png.c core/jpegenc.c core/bmp.c core/ilbm.c core/gif.c core/ora.c core/avif.c core/tiff.c core/psd.c core/rasters.c core/webp.c core/amigafmt.c core/palette.c core/gifenc.c core/amigamodes.c core/guides.c core/swatchpal.c core/iconwrite.c core/batch.c core/docinfo.c core/tiffw.c core/tgaw.c core/channels.c core/xform.c core/selx.c core/maglasso.c core/crop.c core/matte.c core/path.c core/pathops.c core/shape.c core/type.c core/live.c core/amifont.c core/adjust10.c core/filters10.c core/retouch.c core/fx.c"
  av1/build-av1.sh native $T/easel_dav1d
  # each test linked with the whole core (and dav1d)
  t() { cc -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. -Iav1/include $CORE tests/$1.c $T/easel_dav1d/libdav1d.a -o $T/easel_$1 -lm -lpthread; }
  t coretest;     $T/easel_coretest | tail -2
  t codectest;    $T/easel_codectest | tail -2
  t rasterstest;  $T/easel_rasterstest 20 | tail -2
  t amigafmttest; $T/easel_amigafmttest | tail -2
  t layertest;    $T/easel_layertest | tail -2
  t quicktest;    $T/easel_quicktest | tail -1
  # 0.4: palettes, indexed documents and the Amiga writers, then their independent checks (Pillow)
  t palettetest;  $T/easel_palettetest | tail -2
  python3 tests/check_palette.py | tail -2
  # 0.5: brushes, strokes, gradients, patterns, presets
  t brushtest;    $T/easel_brushtest | tail -2
  # 0.6: Workbench icons (each read back by the icon reader) and batch conversion
  t icontest;     $T/easel_icontest | tail -2
  # 0.6: exchange - TIFF and Targa writing, PSD layers, EXIF orientation, File Info; then Pillow
  # (and psd-tools and ImageMagick where installed) check the files it wrote
  t exchangetest; $T/easel_exchangetest | tail -2
  python3 tests/check_exchange.py | tail -2
  # 0.7: selections and transforms
  t selxtest;     $T/easel_selxtest | tail -2
  # 0.9: type and vectors
  t typetest;     $T/easel_typetest | tail -2
  t pathtest;     $T/easel_pathtest | tail -2
  # 0.8: workspace and safety - the undo scratch drawer, undo levels, progress and Abort
  t safetytest;   $T/easel_safetytest | tail -2
  # 1.0: filters, adjustments and retouching
  t filter10test; $T/easel_filter10test | tail -2
  # 1.1: layer styles, colour labels, more blend modes and layer commands
  t fxtest;       $T/easel_fxtest | tail -1
