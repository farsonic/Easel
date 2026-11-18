#!/bin/sh
# Easel's native tests (no Amiga toolchain needed): dav1d built with the sanitizers, then every core/codec
# test with ASan/UBSan, then the Python checks (need Pillow). The same steps build.sh runs before the
# Amiga build. TMP=<dir> to build elsewhere than /tmp.
set -e
cd "$(dirname "$0")/.."
T=${TMP:-/tmp}
CORE="core/util.c core/zip.c core/xml.c core/image.c core/doc.c core/paint.c core/stroke.c core/brush.c core/preset.c core/gradient.c core/pattern.c core/swatch.c core/adjust.c core/layers.c core/filters2.c core/selmore.c core/text.c core/fonts.c core/ttraster.c core/codec.c core/png.c core/jpegenc.c core/bmp.c core/ilbm.c core/gif.c core/ora.c core/avif.c core/tiff.c core/psd.c core/rasters.c core/webp.c core/amigafmt.c core/palette.c core/gifenc.c core/amigamodes.c core/guides.c core/swatchpal.c core/iconwrite.c core/batch.c core/docinfo.c core/tiffw.c core/tgaw.c core/channels.c core/xform.c core/selx.c core/maglasso.c core/crop.c core/matte.c core/path.c core/pathops.c core/shape.c core/type.c core/live.c core/amifont.c core/adjust10.c core/filters10.c core/retouch.c core/xwrite.c core/frames.c core/scripts.c core/svg.c core/gallery.c core/adjustx.c core/selmask.c core/resample.c core/fx.c core/layerops.c"
CORE="$CORE core/toolopts.c"     # 1.1: tool options
CORE="$CORE core/layerx.c"       # 1.2: vector masks, links, patterns, smart objects, artboards, frames
CORE="$CORE core/brushx.c"       # 1.2: brush extras, Mixer and Art History brushes, tablets, .abr/.grd/.pat
CORE="$CORE core/cafill.c core/selsmart.c"   # 1.2: content-aware fill, Quick and Object Selection
CORE="$CORE core/toolsx.c core/legacy.c"     # 1.2: tool extras, history log, Amiga-era extras
CORE="$CORE core/viewx.c core/actions.c core/picinfo.c"     # 1.2: menu and panel additions
CORE="$CORE core/cms.c core/iccio.c core/loadx.c core/colorx.c core/xread2.c core/gsub.c core/exportx.c core/psdfx.c core/lut3dl.c core/typex.c"   # 1.2: colour management, colour models, formats, type
  # 1.1: the interface languages - amiga/lang_table.h as lang/strings.txt makes it, every string of the
  # interface translated and every row used (langcheck lists what is missing), then tr() and the table
  python3 tools/mklang.py --check
  python3 tools/langcheck.py > $T/easel_langcheck.txt || { cat $T/easel_langcheck.txt; exit 1; }
  tail -1 $T/easel_langcheck.txt
  cc -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. tests/langtest.c -o $T/easel_langtest
  $T/easel_langtest > $T/easel_langtest.txt || { cat $T/easel_langtest.txt; exit 1; }
  tail -1 $T/easel_langtest.txt
  # the translations still fit 640x480 (measured with QuillSans 11 when a Fonts drawer is found; EASEL_FONTS=<dir>)
  python3 tools/langfit.py > $T/easel_langfit.txt || { cat $T/easel_langfit.txt; exit 1; }
  tail -1 $T/easel_langfit.txt
  # 1.1: the context menus' contents (amiga/ctxmenu.c is plain C)
  cc -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. tests/ctxtest.c -o $T/easel_ctxtest
  $T/easel_ctxtest > $T/easel_ctxtest.txt || { cat $T/easel_ctxtest.txt; exit 1; }
  tail -1 $T/easel_ctxtest.txt
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
  # 1.1: tool options (tones, sampling, crop overlays, eraser to history, the Move tool's group)
  t tooltest;     $T/easel_tooltest | tail -2
  # 1.1: more writers (WebP lossless, PDF, Netpbm, QOI, ICO, DDS, DPX, FITS), read back, fuzzed, then
  # checked by Pillow (and ImageMagick and poppler where installed)
  t xwritetest;   $T/easel_xwritetest $T/easel_xwrite_out | tail -2
  python3 tests/check_xwrite.py $T/easel_xwrite_out | tail -2
  # 1.1: animations - layers to animated GIF and IFF ANIM (op 5) and every frame read back; Pillow checks the GIFs
  t frametest;    $T/easel_frametest $T/easel_frames_out | tail -2
  python3 tests/check_frames.py $T/easel_frames_out | tail -2
  # filter gallery effects and stacks, Color Lookup, Equalize and Black & White Auto, Focus Area, Select and Mask
  t gallerytest;  $T/easel_gallerytest $T | tail -2
  python3 tests/check_fx.py $T | tail -1
  # 1.1: layer styles, colour labels, more blend modes and layer commands
  t fxtest;       $T/easel_fxtest | tail -1
  # 1.2: layer extras (vector masks, linked layers, pattern fills and overlays, smart objects, artboards, frames)
  t layerxtest;   $T/easel_layerxtest $T | tail -1
  # 1.2: brush extras (Texture, Dual Brush, Noise, Wet Edges, Brush Pose, Mixer, Art History, healing, tablets, files)
  t brushxtest;   $T/easel_brushxtest | tail -1
  # 1.2: content-aware fill (PatchMatch), Remove, Patch, Content-Aware Move; Quick and Object Selection
  t smarttest;    $T/easel_smarttest | tail -1
  # 1.2: Perspective Crop, notes, counts, slices, Rotate View, history log; Amiga-era extras
  t toolsxtest;   $T/easel_toolsxtest $T | tail -1
  # 1.2: menu and panel additions (pixel aspect, guide layouts, Trim, Distribute, actions, styles, comps, histogram)
  t menutest;     $T/easel_menutest | tail -1
  # 1.2: colour management (profiles, transforms, the press, proofs), CMYK and Lab
  # documents, profiles in files, palette and 1-bit files opening in their modes; then Pillow,
  # LittleCMS and ImageMagick check the files and colours
  t colourtest;   $T/easel_colourtest $T/easel_colour_out | tail -1
  python3 tests/check_colour.py $T/easel_colour_out | tail -1
