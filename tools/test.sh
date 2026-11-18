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
CORE="$CORE core/vecx.c core/filtr3.c core/selr3.c core/hyphen.c core/colr3.c"   # r3-image: vector, filter, selection and type extras
CORE="$CORE core/lutlook.c core/foreign.c core/xcf.c core/kra.c core/pdn.c core/filesx.c core/camraw.c core/modex.c core/timeline.c core/measure.c core/grdw.c"   # round 3: file formats, import and export
CORE="$CORE core/menu3.c"       # 1.3: round 3's menu, tool and automation additions
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
  # r N CMD...: run CMD, print its last N lines, and stop with its output if it failed (a pipe into
  # tail would hide the exit status)
  r() { n=$1; shift; s=0; "$@" > $T/easel_run.txt || s=$?; tail -$n $T/easel_run.txt; [ $s -eq 0 ] || { echo "FAILED (exit $s): $*"; cat $T/easel_run.txt; exit 1; }; }
  t coretest;     r 2 $T/easel_coretest
  t codectest;    r 2 $T/easel_codectest
  t rasterstest;  r 2 $T/easel_rasterstest 20
  t amigafmttest; r 2 $T/easel_amigafmttest
  t layertest;    r 2 $T/easel_layertest
  t quicktest;    r 1 $T/easel_quicktest
  # 0.4: palettes, indexed documents and the Amiga writers, then their independent checks (Pillow)
  t palettetest;  r 2 $T/easel_palettetest
  r 2 python3 tests/check_palette.py
  # 0.5: brushes, strokes, gradients, patterns, presets
  t brushtest;    r 2 $T/easel_brushtest
  # 0.6: Workbench icons (each read back by the icon reader) and batch conversion
  t icontest;     r 2 $T/easel_icontest
  # 0.6: exchange - TIFF and Targa writing, PSD layers, EXIF orientation, File Info; then Pillow
  # (and psd-tools and ImageMagick where installed) check the files it wrote
  t exchangetest; r 2 $T/easel_exchangetest
  r 2 python3 tests/check_exchange.py
  # 0.7: selections and transforms
  t selxtest;     r 2 $T/easel_selxtest
  # 0.9: type and vectors
  t typetest;     r 2 $T/easel_typetest
  t pathtest;     r 2 $T/easel_pathtest
  # 0.8: workspace and safety - the undo scratch drawer, undo levels, progress and Abort
  t safetytest;   r 2 $T/easel_safetytest
  # 1.0: filters, adjustments and retouching
  t filter10test; r 2 $T/easel_filter10test
  # 1.1: tool options (tones, sampling, crop overlays, eraser to history, the Move tool's group)
  t tooltest;     r 2 $T/easel_tooltest
  # 1.1: more writers (WebP lossless, PDF, Netpbm, QOI, ICO, DDS, DPX, FITS), read back, fuzzed, then
  # checked by Pillow (and ImageMagick and poppler where installed)
  t xwritetest;   r 2 $T/easel_xwritetest $T/easel_xwrite_out
  r 2 python3 tests/check_xwrite.py $T/easel_xwrite_out
  # 1.1: animations - layers to animated GIF and IFF ANIM (op 5) and every frame read back; Pillow checks the GIFs
  t frametest;    r 2 $T/easel_frametest $T/easel_frames_out
  r 2 python3 tests/check_frames.py $T/easel_frames_out
  # filter gallery effects and stacks, Color Lookup, Equalize and Black & White Auto, Focus Area, Select and Mask
  t gallerytest;  r 2 $T/easel_gallerytest $T
  r 1 python3 tests/check_fx.py $T
  # 1.1: layer styles, colour labels, more blend modes and layer commands
  t fxtest;       r 1 $T/easel_fxtest
  # 1.2: layer extras (vector masks, linked layers, pattern fills and overlays, smart objects, artboards, frames)
  t layerxtest;   r 1 $T/easel_layerxtest $T
  # 1.2: brush extras (Texture, Dual Brush, Noise, Wet Edges, Brush Pose, Mixer, Art History, healing, tablets, files)
  t brushxtest;   r 1 $T/easel_brushxtest
  # 1.2: content-aware fill (PatchMatch), Remove, Patch, Content-Aware Move; Quick and Object Selection
  t smarttest;    r 1 $T/easel_smarttest
  # 1.2: Perspective Crop, notes, counts, slices, Rotate View, history log; Amiga-era extras
  t toolsxtest;   r 1 $T/easel_toolsxtest $T
  # 1.2: menu and panel additions (pixel aspect, guide layouts, Trim, Distribute, actions, styles, comps, histogram)
  t menutest;     r 1 $T/easel_menutest
  # 1.3: Save for Web, Image Processor, New presets, Contact Sheet, Statistics, Image Assets, Find and
  # Replace Text, Apply Image, Calculations, Analysis, Open As, snapping to layers ...
  t menu3test;    r 1 $T/easel_menu3test $T
  # 1.2: colour management (profiles, transforms, the press, proofs), CMYK and Lab
  # documents, profiles in files, palette and 1-bit files opening in their modes; then Pillow,
  # LittleCMS and ImageMagick check the files and colours
  t colourtest;   r 1 $T/easel_colourtest $T/easel_colour_out
  r 1 python3 tests/check_colour.py $T/easel_colour_out
  # r3-image: shape fills and combining, the new filters, Sky and Subject, type extras
  t r3imagetest;  r 1 $T/easel_r3imagetest $T
  r 1 python3 tests/check_r3.py $T
  # round 3: other programs' layered files (GIMP, Krita, Paint.NET; written from their descriptions by
  # make_foreign.py), .look LUTs, EXIF kept, PSB and layered TIFF, ...; ImageMagick, Pillow and psd-tools check
  python3 tests/make_foreign.py $T/easel_foreign > /dev/null
  t filestest;    r 1 $T/easel_filestest $T/easel_foreign
  r 1 python3 tests/check_files.py $T/easel_foreign
