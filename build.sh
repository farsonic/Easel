#!/bin/sh
# Build Easel for AmigaOS (vbcc + NDK 3.2 + MUI 3.8 headers, 68040 with its FPU) after running the
# native core and codec tests (with the address and undefined-behaviour sanitizers).
set -e
export VBCC=${VBCC:-/opt/amiga/vbcc}
export PATH=$VBCC/bin:$PATH
NDK=${NDK:-/opt/amiga/ndk/Include_H}
MUI=${MUI:-/opt/amiga/mui/include}
cd "$(dirname "$0")"
CORE="core/util.c core/zip.c core/xml.c core/image.c core/doc.c core/paint.c core/stroke.c core/brush.c core/preset.c core/gradient.c core/pattern.c core/swatch.c core/adjust.c core/layers.c core/filters2.c core/selmore.c core/text.c core/fonts.c core/ttraster.c core/codec.c core/png.c core/jpegenc.c core/bmp.c core/ilbm.c core/gif.c core/ora.c core/avif.c core/tiff.c core/psd.c core/rasters.c core/webp.c core/amigafmt.c core/palette.c core/gifenc.c core/amigamodes.c core/guides.c core/swatchpal.c core/iconwrite.c core/batch.c core/docinfo.c core/tiffw.c core/tgaw.c core/channels.c core/xform.c core/selx.c core/maglasso.c core/crop.c core/matte.c core/path.c core/pathops.c core/shape.c core/type.c core/live.c core/amifont.c core/adjust10.c core/filters10.c core/retouch.c core/xwrite.c core/frames.c core/scripts.c core/svg.c core/gallery.c core/adjustx.c core/selmask.c core/resample.c core/fx.c core/layerops.c"
CORE="$CORE core/toolopts.c"     # 1.1: tool options
CORE="$CORE core/layerx.c core/brushx.c core/cafill.c core/selsmart.c core/toolsx.c core/legacy.c"   # 1.2: the tools, brushes and layers extras
CORE="$CORE core/viewx.c core/actions.c core/picinfo.c"     # 1.2: menu and panel additions
CORE="$CORE core/cms.c core/iccio.c core/loadx.c core/colorx.c core/xread2.c core/gsub.c core/exportx.c core/psdfx.c core/lut3dl.c core/typex.c"   # 1.2: colour management, colour models, formats, type
CORE="$CORE core/lutlook.c core/foreign.c core/xcf.c core/kra.c core/pdn.c core/filesx.c core/camraw.c core/modex.c core/timeline.c core/measure.c"   # round 3: file formats, import and export
if [ "$1" != "fast" ]; then
  # 1.1: the interface languages (lang/strings.txt, amiga/lang_table.h, every string translated)
  python3 tools/mklang.py --check && python3 tools/langcheck.py | tail -1
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
  # 0.6: Workbench icons (each read back by the icon reader) and batch conversion
  t icontest;     /tmp/easel_icontest | tail -2
  # 0.6: exchange - TIFF and Targa writing, PSD layers, EXIF orientation, File Info; then Pillow
  # (and psd-tools and ImageMagick where installed) check the files it wrote
  t exchangetest; /tmp/easel_exchangetest | tail -2
  python3 tests/check_exchange.py | tail -2
  # 0.7: selections and transforms
  t selxtest;     /tmp/easel_selxtest | tail -2
  # 0.9: type and vectors
  t typetest;     /tmp/easel_typetest | tail -2
  t pathtest;     /tmp/easel_pathtest | tail -2
  # 0.8: workspace and safety - the undo scratch drawer, undo levels, progress and Abort
  t safetytest;   /tmp/easel_safetytest | tail -2
  # 1.0: filters, adjustments and retouching
  t filter10test; /tmp/easel_filter10test | tail -2
  # 1.1: tool options (tones, sampling, crop overlays, eraser to history, the Move tool's group)
  t tooltest;     /tmp/easel_tooltest | tail -2
  # 1.1: more writers and animations, with their independent checks
  t xwritetest;   /tmp/easel_xwritetest /tmp/easel_xwrite_out | tail -2
  python3 tests/check_xwrite.py /tmp/easel_xwrite_out | tail -2
  t frametest;    /tmp/easel_frametest /tmp/easel_frames_out | tail -2
  python3 tests/check_frames.py /tmp/easel_frames_out | tail -2
  t gallerytest;  /tmp/easel_gallerytest /tmp | tail -2
  python3 tests/check_fx.py /tmp | tail -1
  # 1.1: layer styles, colour labels, more blend modes and layer commands
  t fxtest;       /tmp/easel_fxtest | tail -1
  # 1.2: layer extras, brush extras, content-aware fill and smart selections, tool and Amiga extras
  t layerxtest;   /tmp/easel_layerxtest /tmp | tail -1
  t brushxtest;   /tmp/easel_brushxtest | tail -1
  t smarttest;    /tmp/easel_smarttest | tail -1
  t toolsxtest;   /tmp/easel_toolsxtest /tmp | tail -1
  # 1.2: menu and panel additions (pixel aspect, guide layouts, Trim, Distribute, actions, styles, comps, histogram)
  t menutest;     /tmp/easel_menutest | tail -1
  # 1.2: colour management (profiles, transforms, the press, proofs), CMYK and Lab
  # documents, profiles in files, palette and 1-bit files opening in their modes; then Pillow,
  # LittleCMS and ImageMagick check the files and colours
  t colourtest;   /tmp/easel_colourtest /tmp/easel_colour_out | tail -1
  python3 tests/check_colour.py /tmp/easel_colour_out | tail -1
fi
av1/build-av1.sh amiga
# Easel itself: every amiga/*.c but convert.c, which is EaselConvert's main
EASEL_SRC=$(ls amiga/*.c | grep -v '^amiga/convert\.c$')
vc +tools/vbcc-aos68k-fpu -c99 -O2 -cpu=68040 -fpu=68040 -I. -Iav1/include -I"$MUI" -I"$NDK" $EASEL_SRC amiga/bigstack.s $CORE av1/obj/*.o -o Easel -lamiga -lm040
ls -l Easel
# 0.6: EaselConvert, the Shell command for batch conversion (the core's codecs, datatypes as a last resort)
# (the core files it needs: the codecs, the document, resizing and its dependencies; if a link
# fails for an undefined symbol, add the core file that defines it here)
CONVERT_CORE="core/util.c core/zip.c core/xml.c core/image.c core/doc.c core/paint.c core/brush.c core/gradient.c core/adjust.c core/adjust10.c core/codec.c core/png.c core/jpegenc.c core/bmp.c core/ilbm.c core/gif.c core/ora.c core/avif.c core/tiff.c core/psd.c core/rasters.c core/webp.c core/amigafmt.c core/palette.c core/gifenc.c core/iconwrite.c core/batch.c core/docinfo.c core/tiffw.c core/tgaw.c core/path.c core/live.c core/type.c core/shape.c core/fonts.c core/ttraster.c core/amifont.c core/text.c core/xwrite.c core/resample.c core/fx.c core/picinfo.c core/amigamodes.c core/cms.c core/iccio.c core/loadx.c core/colorx.c core/xread2.c core/gsub.c core/psdfx.c core/layers.c core/layerx.c core/toolsx.c core/foreign.c core/xcf.c core/kra.c core/pdn.c core/filesx.c core/camraw.c"   # (round 3: the other programs' files, EXIF, camera raw; r2-tools: OpenRaster keeps layerx.h's settings and the notes)
vc +tools/vbcc-aos68k-fpu -c99 -O2 -cpu=68040 -fpu=68040 -I. -Iav1/include -I"$NDK" amiga/convert.c amiga/dtload.c amiga/bigstack.s $CONVERT_CORE av1/obj/*.o -o EaselConvert -lamiga -lm040
ls -l EaselConvert
