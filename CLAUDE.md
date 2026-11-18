# CLAUDE.md - working on Easel

Easel is a picture editor for AmigaOS 3.x (68040/68060 with FPU, MUI 3.8, AROS or MorphOS), written in
C and laid out the way PhotoCraft is: options bar, toolbox, panels (Layers, Channels, Paths, History,
Color, Swatches, Brushes...), live layers with masks and adjustment layers, type and vector shapes,
filters and retouching. It reads and writes PNG, JPEG, GIF, BMP, IFF ILBM (with HAM/EHB), TIFF, PSD
layers, OpenRaster, Targa, WebP, AVIF (dav1d) and more. GPL-2.0-only (see `LICENSE`; `Licenses.txt`
lists the work by others it contains, with their licences). This file is for anyone, person or agent,
picking the work up. Read it first, then `README.md` (what Easel does, version by version) and
`docs/photocraft-gap.md` (what is still missing).

## Layout

| Path | What |
|---|---|
| `core/` | Portable C99: the document, layers and blending (`doc.c`, `layers.c`, `live.c`), painting, brushes and strokes, selections, transforms, adjustments and filters, type and paths, every codec (`png.c`, `jpegenc.c`, `gif.c`, `ilbm.c`, `tiff.c`, `psd.c`, `ora.c`, `webp.c`, `avif.c`, `rasters.c`...), the TrueType rasteriser. No AmigaOS calls: built and tested on Linux/macOS. |
| `amiga/` | The MUI front end: `main.c`, `canvas.c` (the picture view), `panels.c`/`panels2.c`, `dialogs.c`, `jobs.c` (long jobs with progress and Abort), `keys.c`, `lang.c` (interface strings), `convert.c` (EaselConvert, the batch Shell command), `dtload.c` (datatypes as a last-resort reader). |
| `av1/` | dav1d (BSD 2-clause) ported to vbcc, for AVIF: `build-av1.sh amiga` (vbcc objects) or `native` (a sanitizer library for the tests). Changes are described in `Licenses.txt`. |
| `lang/` | `strings.txt`: every interface string with its German, French, Spanish and Italian rows (Photoshop's terms); `ignore.txt`: literals that are not words. `tools/mklang.py` makes `amiga/lang_table.h`, `tools/langcheck.py` and `tools/langfit.py` check it. |
| `tests/` | Native tests (`*test.c`), Python cross-checks against Pillow and others (`check_*.py`), test data generators (`make_*.py`). |
| `tools/` | `test.sh` (native tests, what CI runs), `package.sh` (builds the release `.lha`), `Install-Easel` (Installer script), `mkicons.py`, `vbcc-aos68k-fpu` (the vbcc config used for the 68040+FPU build). |
| `docs/` | `photocraft-gap.md` (every PhotoCraft feature rated have/partial/missing), `gui-guide/` (how the four MUI programs Quill, Ledger, Easel and Folio are laid out, with screenshots). |

## Build and test

Native tests (need a C compiler and Python 3 with Pillow; this is what CI runs and it must stay green):

    sh tools/test.sh            # TMP=<dir> to build somewhere other than /tmp

It builds dav1d with the sanitizers (a few minutes the first time), then runs every core and codec test
with ASan/UBSan, then the Python checks. Each test prints its pass/fail count.

Amiga build: `./build.sh` (`./build.sh fast` skips the tests). It builds dav1d's vbcc objects (slow:
decode.c alone takes a minute or two), then `Easel` and `EaselConvert`. It needs, at these default paths
(or set `VBCC`, `NDK`, `MUI`):

- **vbcc** 0.9h with vasm and vlink and the m68k-amigaos target, in `/opt/amiga/vbcc`
  (http://www.compilers.de/vbcc.html), with the 68040 FPU maths library (`-lm040`). Easel uses the
  config in `tools/vbcc-aos68k-fpu`.
- **NDK 3.2** C headers in `/opt/amiga/ndk/Include_H` (Aminet `dev/misc/NDK3.2.lha`).
- **MUI 3.8 developer headers** in `/opt/amiga/mui/include` (Aminet `dev/mui/mui38dev.lha`, its
  `C/Include`; not in this repository because it may only be redistributed whole).

Release: `tools/package.sh <version> <workdir>` builds `Easel-<version>.lha` (it takes the Fonts and
TrueType drawers from the previous package's work drawer; see the script) and refuses to finish if any
certificate or key material went in. Packages are not committed; attach them to GitHub releases.

Testing on an Amiga: FS-UAE with the AROS ROM and Zune covers most checks; AmigaOS 3.x with MUI is the
reference. RTG (15-32 bit) and AGA (dithered, 256 colours or fewer) screens both need checking.

## Rules of the project

- C99 that vbcc compiles (watch for: `memchr` with bytes > 0x7F passed as int, `%.*s` with precision 0
  printing everything, rare `-O2` miscompiles - rewrite the loop). 68040 + FPU code.
- The window fits 640x480; long jobs run through `amiga/jobs.c` with progress and Abort; memory is
  checked before big allocations and the user is told how much is needed.
- Every interface string goes through `tr()` (`amiga/lang.c`) and has a row in `lang/strings.txt` with
  its German, French, Spanish and Italian translations, in Photoshop's terms for each language. After
  adding or changing strings: `python3 tools/langcheck.py --rows` prints the missing rows to paste and
  translate, then `python3 tools/mklang.py` makes `amiga/lang_table.h`; `tools/test.sh` fails while a
  string is untranslated or a row is unused, and `tools/langfit.py` checks the translations still fit
  640x480. Menus, cycle gadgets (`tr_list`) and the options bar's fields are translated when shown, so
  write their labels in English.
- A new feature gets a native test, an update to its row in `docs/photocraft-gap.md`, and a README note.
- Easel leaves out printing and ARexx on purpose.
- Don't use `rm -rf` in scripts or by hand on shared machines; move things aside.
- Commit identity: commits so far use `Claude <noreply@anthropic.com>` or `Easel <easel@localhost>`.
  Never put a personal email address in commits, code or docs.

## Where work stands

Easel 1.0 is released (filters, adjustments and retouching; before that type and vectors, workspace and
safety, selections and transforms, exchange formats, brushes, palettes, live layers). Since then, not
released yet: 1.1 and 1.2 (tool options, layers, brushes, colour management, formats) and round 3
(1.3: file formats, menus, tools and shortcuts, filters, selection, type and vectors; README "New in
1.3"). All branches are merged into `main`. `docs/photocraft-gap.md` lists what is still missing area
by area; its summary (section 23, counted with `python3 tools/gapcount.py docs/photocraft-gap.md`)
stands at 661 Have, 78 Partial, 69 Missing, 34 Excluded of PhotoCraft `c337d6e` (about 87%). Round 3's
interface was first run in an emulator in round 4 (below); a real Amiga check comes before 1.3 ships.

Round 4 (branch `r4`): round 3 was run in FS-UAE (A4000/68040 without JIT, AROS ROM with Zune,
uaegfx 640x480) in English and German and what it showed was fixed (Liquify's brush, the Measurement
Log's overflow, long requesters, the Edit menu's height; see README "New in 1.3: checked in an
emulator"); then Find and Isolate Layers, Float and Drop, Check Spelling (the package carries a Dict
drawer), Levels' Show Clipping and channel keys, Color Range's clusters, views and Out of Gamut, Select
and Mask's views, the Preset Manager, Character and Paragraph Styles, the Adjustments window and four
preferences (`core/r4.c`, `core/spell.c`, `amiga/r4ui.c`, `tests/r4test.c`). Driving the emulator:
start arguments `CMD=<id>`, `CMDFILE=<file>` (a menu command number written to the file is run once a
second, then the file is deleted) and `LANG=en|de|fr|es|it`, with `DEBUG=<log>`. Alt doesn't reach the
emulated Amiga through FS-UAE here, so Shift-Alt-click samplers and the Alt+digit channel keys still
want a real Amiga; so do MUI 3.8's own look of the new windows.

The interface is in English, German, French, Spanish and Italian (`lang/strings.txt`, Photoshop's terms
in each language). The translations have been checked for fit with `tools/langfit.py`, not yet read on
a real Amiga screen: a native speaker's look at each language, and a run in German at 640x480 on
AmigaOS with MUI (dialogs and requesters use MUI's fonts there), are still to do. The right mouse
button's menus (`amiga/ctxmenu.c`, `amiga/ctxmui.c`) also want checking on MUI 3.8 and on Zune.

## Issues

Bugs and requests come in as GitHub issues (templates in `.github/ISSUE_TEMPLATE/`). To work on them:

1. `gh issue list --repo farsonic/Easel` and `gh issue view N --repo farsonic/Easel --comments`.
2. Reproduce first. Codec and filter bugs can usually be reproduced natively: load the attached picture
   with the core codec, write a failing test in `tests/`, then fix it in `core/`. Interface bugs need the
   Amiga build; say what you could and couldn't verify.
3. Fix on a branch named `issue-N-short-name`, keep `tools/test.sh` green, and open a pull request whose
   description says `Fixes #N`, what changed and how it was tested. Ask on the issue when information is
   missing rather than guessing. Don't close issues by hand unless asked; merging the PR closes them.
4. Attached pictures may be private: don't commit them unless the reporter agrees; make a small test
   picture of your own instead.

`.github/workflows/claude.yml` lets people mention `@claude` in an issue or pull request to have Claude
Code look at it (it needs the `ANTHROPIC_API_KEY` repository secret or the Claude GitHub App).
