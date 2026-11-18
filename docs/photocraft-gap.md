# PhotoCraft against Easel: feature-gap analysis

This document compares PhotoCraft's features with Easel's, one feature to a row, and says what
Easel would need to match each one on an Amiga. It is meant to steer Easel towards a duplicate of
PhotoCraft's features that is as close to 1:1 as a 68040 allows, plus the Amiga-era features
PhotoCraft never had.

- **PhotoCraft** is a shallow clone at commit `eb6c1abf75c60dda13a3de0aa4e7bdd5f4c43915`
  (2026-10-27), in `/home/user/storytold/photocraft`.
- **Easel** is version 0.2, in `/home/user/easel`.
- **Date of analysis:** 2026-10-28.

## How to read this

**Where the lists come from.** Every list was taken from PhotoCraft's code, not its README:

- the `Tool` enum (68 tools);
- the 632 non-separator rows of the menu `CATALOG`;
- `BlendMode`, `Adjustment`, `LayerContent` and `GalleryFilter`;
- `BrushSettings`, the codec `Format` table and the preference structs.

Easel's status comes from its source:

- `amiga/main.c` (the `menus[]` NewMenu table);
- `amiga/ids.h`, `amiga/easel.h` (`Opts`, the `TL_` tools) and `amiga/toolbox.c` (the options bar);
- `core/doc.h`, `core/paint.h`, `core/adjust.h`, `core/text.h` and `core/codec.h`.

**Source abbreviations.** PhotoCraft paths are relative to its root.

| Short | File |
|---|---|
| TOOL | `crates/ui-egui/src/state/tool.rs`, `enum Tool` and `Tool::key()` |
| CAT | `crates/ui-egui/src/menu_catalog.rs`, `CATALOG`, cited by command id |
| OPT | `crates/ui-egui/src/panels/options_bar.rs`, and the tool UIs in `crates/ui-egui/src/tools/` |
| BLEND | `crates/color/src/blend.rs`, `enum BlendMode` |
| ADJ | `crates/doc/src/adjust.rs`, `enum Adjustment` |
| DOC | `crates/doc/src/lib.rs`: `Layer`, `LayerContent`, `Effects`, `Fill`, `Locks`, `LabelColor` |
| TXT | `crates/doc/src/text.rs`: `CharStyle`, `TextAlign`, `TextShape`, `AntiAlias` |
| VEC | `crates/doc/src/vector.rs`: `LiveShape`, `StrokeAlign`, `LineCap` |
| BRUSH | `crates/paint/src/brush.rs`, `struct BrushSettings` |
| GAL | `crates/algo/src/artistic.rs`, `enum GalleryFilter` (47 effects) |
| FMT | `crates/codecs/src/format.rs` and the capability matrix in `crates/codecs/README.md` |
| FFP | `docs/file-format-parity.md` |
| PREFS | `crates/engine/src/prefs.rs` |
| QNT | `crates/algo/src/quantize.rs` |
| E: | an Easel file, for example `E:amiga/main.c` |

**Columns.**

- **Easel**: one of four words.
  - **Have** means the feature is there.
  - **Partial** says what is missing.
  - **Missing** means Easel has none of it.
  - **Excluded** means the user has ruled it out (printing and ARexx). These rows count for
    nothing in the coverage figures.
- **Amiga feasibility** is judged for a 68040 with its FPU (25-40 MHz) and 16-64 MB of memory:
  - **Easy**: days of work, and fast enough.
  - **Moderate**: real work, or slow but usable with a busy pointer or progress.
  - **Hard**: weeks of work, or memory or CPU make it marginal.
  - **Not sensible**: needs a GPU, a network or AI service, or hardware the Amiga lacks. A stand-in
    is named where one exists.
  - **Done**: Easel already has it.
- **Pri** is the suggested priority: P1 (next), P2 (soon) or P3 (later or never). Rows Easel has
  are marked "—".

**Budgets to keep in mind.** An 8-bit RGBA layer costs w × h × 4 bytes. A 2000 × 1500 layer is
12 MB, the same layer at 16 bits per channel is 24 MB, and at 32-bit float it is 48 MB. A 3 × 3
convolution over 3 megapixels takes about 5-10 s on a 25 MHz 68040. Easel already composites
on demand by rectangle (`E:core/doc.h` `doc_composite`), which suits live layers and adjustment
layers well, since nothing needs a full composite buffer.

**Constraints.**

- **No printing and no ARexx.** File › Print, Print One Copy, print prepress and every scripting
  host (PhotoCraft's JSON control channel, its MCP server and Scripts › Browse, whose Amiga
  equivalent would be ARexx) are listed as **Excluded by request**.
- **No Microsoft or Adobe icons.** Every new tool, panel or menu icon must be drawn fresh in
  Easel's own style (`E:amiga/theme.c`, `E:tools/mkicons.py`).
- **No PhotoCraft artwork.** Don't trace or copy PhotoCraft's `assets/`,
  `crates/ui-egui/src/icon_data.rs` or `icons.rs`, or the emoji placeholders in `Tool::glyph()`.
- **Names that point at other platforms.** PhotoCraft has the System (Mac OS) and System
  (Windows) palettes and the Windows LCD anti-aliasing, and its smart-object command is called
  Reveal in Finder. Easel should use Amiga equivalents instead: the Workbench or MagicWB palettes,
  and "Show in Drawer".
- **Reading Adobe-defined files is allowed.** PSD, .abr, .atn and the like are file formats, not
  icons.

**How the menus are split.** Each of PhotoCraft's 632 menu rows appears exactly once in this
document, so nothing is counted twice:

- **Menus section:** File, Edit (except Preferences), Image (except Adjustments and Mode), Layer
  (except the mask, style, adjustment, fill, smart-object and shape submenus), View and Window ›
  Arrange/Workspace.
- **Other sections:**
  - Layers: the mask, style, adjustment, fill and smart-object submenus.
  - Blend modes.
  - Adjustments: Image › Adjustments.
  - Filters: the Filter menu.
  - Selection: the Select menu.
  - Type: the Type menu.
  - Vector: Layer › Combine Shapes.
  - Colour: Image › Mode, Proof Setup and Proof Colors.
  - Preferences: Edit › Preferences.
  - Panels: Window › panel items.

Rows that group several identical items say how many they cover.

## 1. Tools, with options per tool

PhotoCraft has 68 tools (`Tool::ALL` in TOOL), and its options bar is OPT. Easel has 26 tools
(`TL_` in `E:amiga/easel.h`), and its options bar is `E:amiga/toolbox.c` lines 311-342. Keys are
PhotoCraft's (`Tool::key()`).

| PhotoCraft tool and options (source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Move (V): Auto-Select Layer/Group, transform controls, align bar (`tools/align_bar.rs`), ⌥-drag duplicates (`tools/move_mods.rs`), nudge; TOOL `Move` | **Partial**: Auto-Select Layer and nudge are there; Group mode, transform handles, align buttons and Alt-drag duplicate are missing | Easy | P1 |
| Artboard (V); TOOL `Artboard` | **Missing** | Moderate: artboards are clipped top-level groups, so groups come first | P3 |
| Rectangular Marquee (M): New/Add/Subtract/Intersect, Feather, Style Normal/Fixed Ratio/Fixed Size, W/H swap; OPT 246-315 | **Partial**: the four modes and Feather are there; the fixed-ratio and fixed-size styles are missing | Easy | P1 |
| Elliptical Marquee (M): the same plus Anti-alias | **Partial**: the modes and Feather are there; the Style options and the Anti-alias toggle are missing | Easy | P1 |
| Single Row Marquee: a 1-pixel row | **Missing** | Easy | P2 |
| Single Column Marquee | **Missing** | Easy | P2 |
| Lasso (L): modes, Feather, Anti-alias; OPT 315 | **Partial**: there's no Anti-alias toggle | Easy | P2 |
| Polygonal Lasso (L): the same; Return or a double-click closes, Backspace removes a point | **Partial**: the closing works, but there's no Anti-alias toggle and no Backspace to remove the last point | Easy | P2 |
| Magnetic Lasso (L): Width, Contrast, Frequency, pen pressure; `tools/magnetic_lasso_ui.rs`, `algo/src/magnetic.rs` | **Missing** | Moderate: a live-wire (Sobel cost plus Dijkstra in a moving window) is cheap enough | P2 |
| Magic Wand (W): Tolerance, Anti-alias, Contiguous, Sample All Layers, Select Subject button; OPT 360 | **Partial**: Tolerance, Contiguous and All Layers are there; Anti-alias, the sample size and Select Subject are missing | Easy | P1 |
| Quick Selection (W): a brush-driven graph cut; `algo/src/segment.rs` (`quick`) | **Missing** | Hard: a min-cut on a quarter-size proxy is about seconds per stroke | P3 |
| Object Selection (W): GrabCut in a box, Object Finder; `tools/object_finder.rs` | **Missing** | Hard: GrabCut's iterations take tens of seconds on a 68040 | P3 |
| Crop (C): ratio and size presets (`tools/crop_presets.rs`), Straighten (`crop_straighten.rs`), overlays (`crop_overlay.rs`), Delete Cropped Pixels, Content-Aware fill, rotating the box | **Partial**: there's a box with handles, and Return or Esc; presets, straightening, rotation, overlays and a non-destructive crop are missing | Easy (presets, overlay); Moderate (rotation) | P1 |
| Perspective Crop (C); `tools/perspective_crop_ui.rs` | **Missing** | Moderate: a projective resample, like the arbitrary rotate | P3 |
| Slice (C) | **Missing** | Easy, but of little use without web export | P3 |
| Slice Select (C) | **Missing** | Easy | P3 |
| Frame (K): a placeholder frame with a stroke; `tools/frame_tool.rs`, `Group::frame_stroke` | **Missing** | Moderate: needs groups and vector masks | P3 |
| Eyedropper (I): sample point/3/5/11/31/51/101, Current/Current & Below/All Layers; `eyedropper_ui.rs` | **Partial**: point, 3 × 3 and 5 × 5 from the current or all layers; the larger averages and Current & Below are missing | Easy | P2 |
| Color Sampler (I): up to 10 fixed readouts in Info | **Missing** | Easy, once an Info panel exists | P2 |
| Ruler (I): measures angle and length; Straighten Layer | **Missing** | Easy | P2 |
| Note (I): annotations | **Missing** | Easy | P3 |
| Count (I): numbered markers | **Missing** | Easy | P3 |
| Brush (B): Size, Hardness, Mode, Opacity, Flow, airbrush build-up, Smoothing, pressure for size and opacity, preset picker (`tools/brush_picker.rs`), symmetry (`tools/symmetry_ui.rs`); OPT 134-220 | **Partial**: Size, Hardness, Mode, Opacity and Flow are there; Smoothing, build-up, presets, pressure and symmetry are missing | Easy (smoothing, build-up, symmetry); Moderate (pressure) | P1 |
| Pencil (B): Size, Mode, Opacity, Smoothing, Auto Erase; OPT 173 | **Partial**: Smoothing and Auto Erase are missing | Easy | P2 |
| Mixer Brush (B): Wet, Load, Mix, Flow, load and clean after each stroke, Sample All Layers; `paint/src/mixer.rs` | **Missing** | Moderate: a per-dab pickup buffer is fine at small sizes | P3 |
| Color Replacement (B): Mode Hue/Saturation/Color/Luminosity, Sampling Continuous/Once/Background, Limits Contiguous/Discontiguous/Find Edges, Tolerance; `paint/src/replace.rs` | **Missing** | Easy | P2 |
| Eraser (E): Mode Brush/Pencil/Block, Opacity, Flow, Erase to History; `tools/eraser_ui.rs` | **Partial**: Size, Hardness, Opacity and Flow are there; the Pencil and Block modes and Erase to History are missing | Easy | P2 |
| Background Eraser (E): Sampling, Limits, Tolerance, Protect Foreground; `paint/src/bg_erase.rs` | **Missing** | Easy | P2 |
| Magic Eraser (E): Tolerance, Anti-alias, Contiguous, All Layers, Opacity | **Missing** | Easy: Easel's wand plus clear | P2 |
| Gradient (G): presets and the gradient editor (stops, midpoints, opacity stops), five styles, Mode, Opacity, Reverse, Dither, Classic/new; OPT 400, `tools/gradient_ui.rs` | **Partial**: five styles, Mode, Opacity, Reverse, and foreground to background or to transparent; the multi-stop editor, presets, the Dither toggle and an editable live gradient are missing | Easy | P1 |
| Paint Bucket (G): Fill from Foreground or Pattern, Mode, Opacity, Tolerance, Anti-alias, Contiguous, All Layers; OPT 375 | **Partial**: Pattern and Mode are missing | Easy | P2 |
| Horizontal Type (T): family, style, size, AA method, alignment, colour, warp, Character and Paragraph panels, commit and cancel; `tools/type_tool.rs` | **Partial**: point text rendered once into a pixel layer, with font, size, alignment and AA; it can't be edited afterwards, and there are no styles or panels (`E:core/text.h`) | Moderate | P1 |
| Vertical Type (T) | **Missing** | Moderate | P3 |
| Horizontal Type Mask (T): the glyphs as a selection | **Missing** | Easy: render to the selection mask instead of a layer | P2 |
| Vertical Type Mask (T) | **Missing** | Moderate | P3 |
| Hand (H): Fit Screen, Fill Screen, scroll all windows | **Have**: 100%, Fit, Zoom In/Out buttons and the Space bar (no Fill Screen) | Done | — |
| Rotate View (R): view angle, reset | **Missing** | Hard: rotating the display on every redraw is slow; a 90° step would be Easy | P3 |
| Zoom (Z): Resize Windows to Fit, scrubby zoom, Fit/Fill/100% | **Partial**: click and Alt-click, and the buttons; scrubby and marquee zoom are missing | Easy | P2 |
| Remove (J): non-local patch completion; `algo/src/remove.rs` | **Missing** | Hard: minutes for a small area | P3 |
| Spot Healing Brush (J): Type Content-Aware/Create Texture/Proximity Match, Sample All Layers | **Missing** | Moderate (Proximity Match); Hard (Content-Aware) | P2 |
| Healing Brush (J): Source Sampled/Pattern, Aligned, Sample current/below/all, Mode, Diffusion | **Missing** | Moderate: a Poisson blend over the dab bounds suits the FPU | P2 |
| Patch (J): Source/Destination, Transparent, Diffusion, Content-Aware | **Missing** | Moderate | P3 |
| Content-Aware Move (J): Move/Extend, Structure, Color, Transform On Drop | **Missing** | Hard | P3 |
| Red Eye (J): Pupil Size, Darken Amount; `algo/src/redeye.rs` | **Missing** | Easy | P2 |
| Clone Stamp (S): Size, Hardness, Mode, Opacity, Flow, Aligned, Sample current/below/all, Clone Source overlay with 5 sources | **Partial**: Size, Hardness, Opacity, Flow and Alt-click source are there; Mode, an Aligned toggle, layer sampling and the overlay are missing | Easy | P1 |
| Pattern Stamp (S): pattern, Aligned, Impressionist | **Missing** | Easy, once patterns exist | P2 |
| History Brush (Y): paints from a chosen history state | **Missing** | Moderate: needs a full-layer snapshot as the source (RAM) | P2 |
| Art History Brush (Y): 10 stroke styles, Area, Tolerance; `algo/src/art_history.rs` | **Missing** | Moderate | P3 |
| Blur tool: Mode, Strength, Sample All Layers | **Partial**: Size, Hardness and Strength are there; Mode and Sample All are missing | Easy | P3 |
| Sharpen tool: as Blur, plus Protect Detail | **Partial**: Mode, Sample All and Protect Detail are missing | Easy | P3 |
| Smudge: Strength, Finger Painting, Sample All | **Partial**: Finger Painting and Sample All are missing | Easy | P3 |
| Dodge (O): Range Shadows/Midtones/Highlights, Exposure, Protect Tones | **Partial**: Exposure only. `Opts.range` exists in `E:amiga/easel.h`, but the options bar (`E:amiga/toolbox.c:332`) doesn't show it and `Brush` in `E:core/paint.h` has no range | Easy | P1 |
| Burn (O): the same | **Partial**: as Dodge | Easy | P1 |
| Sponge (O): Mode Desaturate/Saturate, Flow, Vibrance | **Partial**: Flow only; the Mode isn't shown (as Dodge), and Vibrance is missing | Easy | P1 |
| Pen (P): Shape/Path/Pixels, path operations, rubber band, auto add and delete | **Missing** | Moderate: Bézier paths with handles drawn in COMPLEMENT, like the crop box | P2 |
| Freeform Pen (P): curve fitting, magnetic option | **Missing** | Moderate | P3 |
| Curvature Pen (P) | **Missing** | Moderate | P3 |
| Add Anchor Point (P) | **Missing** | Easy, once paths exist | P2 |
| Delete Anchor Point (P) | **Missing** | Easy | P2 |
| Convert Point (P) | **Missing** | Easy | P2 |
| Path Selection (A) | **Missing** | Easy | P2 |
| Direct Selection (A) | **Missing** | Easy | P2 |
| Rectangle (U): Shape/Path/Pixels, Fill, Stroke colour/width/dashes/align/caps, W/H, path operations, per-corner radii (`tools/corner_radius.rs`, VEC `LiveShape::Rect`) | **Partial**: filled pixels in the foreground colour with one radius and opacity; there's no stroke-only shape, no outline and no live shape layer | Easy (pixel stroke); Moderate (shape layers) | P1 |
| Ellipse (U): the same | **Partial**: as Rectangle | Easy | P1 |
| Triangle (U); VEC `LiveShape::Polygon` | **Missing** | Easy | P2 |
| Polygon (U): sides, star ratio, smooth corners | **Missing** | Easy | P2 |
| Line (U): weight, arrowheads, stroke | **Partial**: weight and opacity; arrowheads are missing | Easy | P2 |
| Custom Shape (U): shape library, Shapes panel | **Missing** | Moderate: needs vector paths | P3 |

## 2. Menus, item by item

The menu tree is CAT (`crates/ui-egui/src/menu_catalog.rs`): 632 items, and every one is wired to a
command (`docs/parity-checklist.md`). Easel's menus are `menus[]` in `E:amiga/main.c` lines 84-135.
Items handled elsewhere in this document are listed in "How to read this".

### 2.1 File

Re-checked 2026-11-18 (Easel 1.1) against `menus[]` in `E:amiga/main.c`.

| PhotoCraft item (CAT id) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| New… `file.new`: presets (`engine/src/cmd/document_preset_cmds.rs`), colour mode (Bitmap at 1 bit, #14497), bit depth, resolution, units, background contents, pixel aspect | **Partial**: sizes from 320 × 256 PAL up, dpi, and white, background or transparent; colour mode, units, saved presets and the pixel aspect are missing (the mode is set afterwards in Image › Mode) | Easy | P2 |
| Open… `file.open` | **Have**: about 45 formats (`E:core/codec.h` `FMT_`), SVG drawings as shape layers (1.1), anything else through datatypes | Done | — |
| Open As… `file.openAs`: forces the format | **Missing** | Easy | P3 |
| Open as Smart Object… (#12212 since eb6c1ab) | **Missing**: there are no smart objects | Moderate | P3 |
| Open EXR Parts… `file.openExrParts` | **Missing** | Moderate: needs an EXR reader first | P3 |
| Close `file.close` | **Have** | Done | — |
| Close All `file.closeAll` | **Have** | Done | — |
| Close Others `file.closeOthers` | **Have** (1.1) | Done | — |
| Save `file.save` | **Have**: ORA or a flat format (1.1: every format Easel writes), and offers to flatten | Done | — |
| Save As… `file.saveAs` | **Have** | Done | — |
| Save a Copy… `file.saveACopy` | **Have** (0.6): in the format its name asks for, layers kept in ORA and PSD | Done | — |
| Revert `file.revert` | **Have** | Done | — |
| Export › Quick Export as PNG | **Have** (1.1): beside the picture's file, or in the last drawer, without a requester | Done | — |
| Export › Export As…: format, quality, scale, transparency, metadata, preview, size estimate | **Partial** (0.6): Export As (with scale) takes the format from the name (every writer), with JPEG quality; one item per format too. Preview, size estimate, matte and metadata choice are missing | Easy (matte); Moderate (preview) | P2 |
| Export › Export Preferences… | **Missing** | Easy | P3 |
| Export › Save for Web (Legacy)…: palette, dither, colours, 2-up preview | **Missing** | Moderate. Amiga stand-in: a "Save for Screen" dialog (section 21.5) | P2 |
| Export › Artboards to Files… | **Missing** | Easy, once artboards exist | P3 |
| Export › Artboards to PDF… | **Missing**: Easel writes a one-page PDF (1.1) but has no artboards | Easy, once artboards exist | P3 |
| Export › Layers to Files… | **Have** (0.6) | Done | — |
| Export › Layer Comps to Files… | **Missing** | Easy, once layer comps exist | P3 |
| Export › Color Lookup Tables… (.cube) | **Missing** | Easy: the adjustment layers exist | P3 |
| Export › Data Sets as Files… | **Missing** | Moderate | P3 |
| Export › Paths to Illustrator… | **Have** (1.1) as SVG: Export › Paths to SVG, with the shape layers and the document's bounds | Done | — |
| Export › Render Video… | **Have** (1.1) as the Amiga's and the web's animations: Export › Layers as IFF ANIM and Layers as Animated GIF (MP4 and H.264 are **Left out**: far too slow on a 68040) | Done | — |
| Generate › Image Assets | **Missing** | Easy | P3 |
| Place Embedded… `file.placeEmbedded` | **Partial** (0.6): Place as Layer goes on into Free Transform; there's no smart object | Moderate (smart objects) | P3 |
| Place Linked… | **Missing** | Moderate | P3 |
| Package… | **Missing** | Easy | P3 |
| Automate › Batch… | **Partial**: EaselConvert (Shell) converts, resizes and reduces colours in batches; there are no actions to run | Moderate: needs actions | P2 |
| Automate › Create Droplet… | **Missing** | Easy. Amiga stand-in: a Workbench tool icon that runs an action on the files dropped on it | P3 |
| Automate › Crop and Straighten Photos | **Missing** | Moderate (`algo/src/scancrop.rs`) | P3 |
| Automate › Contact Sheet II… | **Missing** | Easy | P3 |
| Automate › Conditional Mode Change… | **Missing** | Easy | P3 |
| Automate › Fit Image… | **Have** (1.1): File › Scripts › Fit Image (`doc_fit_image`) | Done | — |
| Automate › Lens Correction… | **Missing** (Filter › Lens Correction exists, 1.0) | Easy, once batches run filters | P3 |
| Automate › Merge to HDR Pro… | **Missing** | Hard: float stacks of several exposures overflow 16-64 MB | P3 |
| Automate › Photomerge… | **Missing** | Hard: feature matching and warping take too much CPU and RAM (`algo/src/panorama.rs`) | P3 |
| Scripts › Image Processor… (batch convert and resize; the open documents as a source, #12858) | **Partial**: EaselConvert does it from the Shell; there's no window for it | Easy | P2 |
| Scripts › Delete All Empty Layers | **Have** (1.1): one undo step (`doc_delete_empty_layers` in `E:core/scripts.c`) | Done | — |
| Scripts › Flatten All Layer Effects | **Missing** | Easy, once effects exist | P3 |
| Scripts › Flatten All Masks | **Have** (1.1): one undo step (`doc_flatten_all_masks`) | Done | — |
| Scripts › Script Events Manager… | **Missing** | Easy if it only runs actions on events (open, save) | P3 |
| Scripts › Load Files into Stack… | **Have** (0.6) | Done | — |
| Scripts › Statistics… (stack modes) | **Missing** | Moderate | P3 |
| Scripts › Browse… (run a script) | **Excluded** by request: the Amiga script host is ARexx | — | — |
| Import › Variable Data Sets… | **Missing** | Moderate | P3 |
| Import › Video Frames to Layers… | **Have** (1.1): File › Import Frames to Layers, every frame of an animated GIF or IFF ANIM a layer, its time in the name | Done | — |
| Import › Notes… | **Missing** | Easy | P3 |
| Import › WIA Support… | **Left out**: WIA is the Windows scanner API (datatypes and a scanner program's files stand in) | Not sensible | — |
| File Info… (XMP/IPTC/EXIF metadata) | **Have** (0.6): six fields, kept in PNG, JPEG, TIFF, PSD, ORA and (1.1) PDF | Done | — |
| File Info: a copyrighted picture shows © before its name in the tab and title (#13985) | **Missing** | Easy | P3 |
| Print… | **Excluded** by request | — | — |
| Print One Copy | **Excluded** by request | — | — |
| Exit `file.exit` | **Have**: Quit | Done | — |

### 2.2 Edit (Edit › Preferences is in section 17)

Re-checked 2026-11-18 (Easel 1.1).

| PhotoCraft item (CAT id) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Undo `edit.undo` | **Have**: names the step; levels and memory are preferences, older steps can go to a scratch drawer (0.8) | Done | — |
| Redo `edit.redo` | **Have** | Done | — |
| Toggle Last State `edit.toggleLastState` | **Have** (0.6) | Done | — |
| Fade… `edit.fade` (with every blend mode, #13852) | **Have** (0.6): opacity and every blend mode (`doc_fade`) | Done | — |
| Cut `edit.cut` | **Have**: Easel's clipboard and the Amiga's (0.6) | Done | — |
| Copy `edit.copy` | **Have**: Easel's and the Amiga's | Done | — |
| Copy Merged `edit.copyMerged` | **Have** | Done | — |
| Paste `edit.paste` | **Have** (0.6): from Easel's clipboard, else the Amiga's (iffparse); Paste from Amiga Clipboard too | Done | — |
| Paste Special › Paste in Place | **Have** | Done | — |
| Paste Special › Paste Into (with a mask) | **Have** (0.3) | Done | — |
| Paste Special › Paste Outside | **Missing** | Easy | P3 |
| Clear `edit.clear` | **Have** | Done | — |
| Search… (command palette, ⌘F) | **Missing** | Easy: a MUI list of the menu commands, filtered as you type | P3 |
| Check Spelling… | **Missing** | Moderate: needs dictionaries (Quill's could be shared) | P3 |
| Find and Replace Text… | **Missing** | Easy: type layers exist (0.9) | P3 |
| Fill… `edit.fill`: Foreground/Background/Color/Pattern/History/Black/50% Gray/White/Content-Aware, Mode, Opacity, Preserve Transparency | **Partial**: foreground, background, black, 50% grey, white or a colour, with mode and opacity; the pattern through Edit › Fill with Pattern (0.5). History, Content-Aware and Preserve Transparency are missing | Easy | P3 |
| Stroke… `edit.stroke`: width, colour, location, mode, opacity, preserve transparency | **Partial**: width, inside, centre or outside, and opacity; Mode and Preserve Transparency are missing | Easy | P3 |
| Content-Aware Fill… | **Missing** (the Spot Healing Brush, 1.0, does small areas) | Hard: PatchMatch at a reduced size takes minutes | P3 |
| Content-Aware Scale | **Missing** | Moderate: seam carving (`algo/src/seam.rs`) | P3 |
| Puppet Warp | **Missing** | Hard: an interactive mesh deformation | P3 |
| Perspective Warp | **Missing** | Hard | P3 |
| Free Transform `edit.freeTransform`: on-canvas handles, scale, rotate, skew, distort, perspective, reference point, warp | **Have** (0.7): on the canvas with handles; warp is missing (below) | Done | — |
| Transform › Again | **Have** (0.7) | Done | — |
| Transform › Scale | **Have** (0.7) | Done | — |
| Transform › Rotate | **Have** (0.7) | Done | — |
| Transform › Skew | **Have** (0.7) | Done | — |
| Transform › Distort | **Have** (0.7) | Done | — |
| Transform › Perspective | **Have** (0.7) | Done | — |
| Transform › Warp (bend presets and mesh) | **Missing** (type layers have Warp Text's 15 styles) | Hard | P3 |
| Transform › Split Warp Horizontally/Vertically/Crosswise, Remove Warp Split (4 items) | **Missing** | Hard | P3 |
| Transform › Rotate 180° | **Have** | Done | — |
| Transform › Rotate 90° Clockwise | **Have** | Done | — |
| Transform › Rotate 90° Counter Clockwise | **Have** | Done | — |
| Transform › Flip Horizontal | **Have** | Done | — |
| Transform › Flip Vertical | **Have** | Done | — |
| Auto-Align Layers… | **Missing** | Hard | P3 |
| Auto-Blend Layers… | **Missing** | Hard | P3 |
| Define Brush Preset… | **Have** (0.5), and Pick Up Brush for DPaint brushes | Done | — |
| Define Pattern… | **Have** (0.5) | Done | — |
| Define Custom Shape… | **Partial**: any saved path is drawn by the Custom Shape tool (0.9); there's no shape library to add it to | Easy | P3 |
| Purge › Undo | **Have**: Purge Undo History | Done | — |
| Purge › Clipboard | **Missing** | Easy | P3 |
| Purge › Histories (every document) | **Partial**: only the current document's history can be purged | Easy | P3 |
| Purge › Video Cache | **Left out**: Easel has no video cache | Not sensible | — |
| Purge › All | **Missing** | Easy | P3 |
| Presets › Preset Manager… | **Missing** (brushes, gradients, patterns and swatches each load and save their own) | Easy | P3 |
| Presets › Migrate Presets | **Left out**: it imports from an older desktop install | Not sensible | — |
| Presets › Export/Import Presets… | **Partial**: each kind loads and saves on its own (0.5, 1.0 adjustment presets) | Easy | P3 |
| Color Settings… (working spaces, policies) | **Missing** | Moderate, once there's a CMS | P3 |
| Assign Profile… | **Missing** | Moderate | P3 |
| Convert to Profile… (4 intents, BPC) | **Missing** | Moderate: matrix/TRC profiles are cheap, CLUT profiles slower | P3 |
| Keyboard Shortcuts… (+ .kys) | **Have** (0.8): Edit › Keyboard Shortcuts, PROGDIR:Easel.keys (.kys isn't read) | Done | — |
| Menus… (hide or colour items) | **Missing** | Moderate: items can be hidden, but Intuition menus have no colours | P3 |
| Toolbar… (customise the toolbox) | **Missing** | Easy | P3 |

### 2.3 Image (Adjustments are in section 5, Mode in section 11)

| PhotoCraft item (CAT id) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Auto Tone `image.autoTone` | **Have** | Done | — |
| Auto Contrast `image.autoContrast` | **Have** | Done | — |
| Auto Color `image.autoColor` | **Have** | Done | — |
| Image Size… `image.imageSize`: Bicubic/Smoother/Sharper/Bilinear/Nearest/Preserve Details, units, resolution, scale styles | **Partial**: bicubic, bilinear or nearest, in pixels or per cent; resolution and units, Preserve Details and the Smoother/Sharper variants are missing | Easy | P2 |
| Canvas Size… `image.canvasSize` | **Have**: anchor and fill | Done | — |
| Image Rotation › 180° | **Have** | Done | — |
| Image Rotation › 90° Clockwise | **Have** | Done | — |
| Image Rotation › 90° Counter Clockwise | **Have** | Done | — |
| Image Rotation › Arbitrary… | **Have** | Done | — |
| Image Rotation › Flip Canvas Horizontal | **Have** | Done | — |
| Image Rotation › Flip Canvas Vertical | **Have** | Done | — |
| Crop `image.crop` | **Have** | Done | — |
| Trim… (transparent, top-left or bottom-right colour; which sides) | **Partial**: transparent edges only, from all sides | Easy | P3 |
| Reveal All | **Missing**: Easel's layers are exactly canvas-sized, so there are no off-canvas pixels | Moderate: needs layer offsets or tiles (section 19) | P3 |
| Duplicate… | **Have** | Done | — |
| Apply Image… | **Missing** | Easy | P3 |
| Calculations… | **Missing** | Easy, once channels exist | P3 |
| Variables › Define… / Data Sets… (2 items) | **Missing** | Moderate | P3 |
| Apply Data Set… | **Missing** | Moderate | P3 |
| Trap… | **Excluded** by request: print prepress | — | — |
| Analysis › Set Measurement Scale, Select Data Points, Record Measurements, Ruler Tool, Count Tool, Place Scale Marker (6 items) | **Missing** | Easy | P3 |

### 2.4 Layer (masks, styles, fill, adjustment and smart objects are in section 3; Combine Shapes is in section 10)

| PhotoCraft item (CAT id) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| New › Layer… `layer.new.layer` (name, mode, opacity, clip, colour) | **Partial**: New Layer has no dialog; the name and mode are set afterwards in Properties | Easy | P3 |
| New › Layer from Background… | **Partial**: Easel has no locked Background, so every layer is already a normal layer | Easy | P3 |
| New › Background from Layer | **Missing** | Easy | P3 |
| New › Group… | **Missing**: there are no groups (`E:core/doc.h` `Layer` is flat; ORA groups are flattened in `E:core/ora.c`) | Moderate: a temporary buffer for each group while compositing a rectangle | P1 |
| New › Group from Layers… | **Missing** | Moderate: needs multi-layer selection | P1 |
| New › Artboard… / Artboard from Group / Artboard from Layers (3 items) | **Missing** | Moderate | P3 |
| New › Frame from Layers | **Missing** | Moderate | P3 |
| New › Layer via Copy | **Have** | Done | — |
| New › Layer via Cut | **Have** | Done | — |
| Duplicate Layer… | **Have** | Done | — |
| Delete › Layer | **Have** | Done | — |
| Delete › Hidden Layers | **Missing** | Easy | P3 |
| Quick Export as PNG (layer) | **Missing** | Easy | P3 |
| Export As… (layer) | **Missing** | Easy | P3 |
| Rename Layer | **Have**: through Layer Properties | Done | — |
| Layer Content Options… | **Missing** | Easy, once adjustment and fill layers exist | P2 |
| Mask All Objects | **Missing** | Not sensible: it relies on object recognition. Stand-in: none | P3 |
| New Layer Based Slice | **Missing** | Easy | P3 |
| Group Layers / Ungroup Layers (2 items) | **Missing** | Moderate, with groups | P1 |
| Hide Layers | **Have**: Hide/Show Layer | Done | — |
| Arrange › Bring to Front | **Have** | Done | — |
| Arrange › Bring Forward | **Have** | Done | — |
| Arrange › Send Backward | **Have** | Done | — |
| Arrange › Send to Back | **Have** | Done | — |
| Arrange › Reverse | **Missing** | Easy, with multi-select | P3 |
| Align › Top/Vertical Centers/Bottom/Left/Horizontal Centers/Right (6 items) | **Missing** | Easy: align to the selection or to the other selected layers | P2 |
| Distribute › 8 items | **Missing** | Easy | P3 |
| Lock Layers… (transparency, pixels, position, artboard, all; DOC `Locks`) | **Partial**: locking pixels and transparency only | Easy | P2 |
| Link Layers | **Missing** | Easy | P3 |
| Select Linked Layers | **Missing** | Easy | P3 |
| Merge Layers `layer.mergeLayers` | **Have**: Merge Down; merging a multi-selection is missing | Done | — |
| Merge Visible | **Have** | Done | — |
| Flatten Image | **Have** | Done | — |
| Matting › Color Decontaminate… | **Missing** | Moderate | P3 |
| Matting › Defringe… | **Missing** | Easy | P2 |
| Matting › Remove Black Matte | **Missing** | Easy | P3 |
| Matting › Remove White Matte | **Missing** | Easy | P3 |
| Rasterize › Type, Shape, Fill Content, Vector Mask, Smart Object, Video, Layer Style, Layer, All Layers (9 items) | **Missing**: everything in Easel is already pixels | Easy, once live layer kinds exist | P2 |
| Video Layers › 12 items (new blank or from file, insert, duplicate or delete frame, replace or interpret footage, restore, reload, rasterize) | **Missing** | Not sensible for real video. Stand-in: IFF ANIM frames as a frame stack (section 21) | P3 |

### 2.5 View (Proof Setup, Proof Colors, Gamut Warning and 32-bit preview are in section 11)

Re-checked 2026-11-18 (Easel 1.1).

| PhotoCraft item (CAT id) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Pixel Aspect Ratio › Custom, Square, D1/DV NTSC, PAL, widescreen, HDV, DVCPRO, Anamorphic (9 items) | **Partial**: a picture's pixel aspect is kept and set from its Amiga screen mode (Image › Amiga Screen Mode, ILBM's BMHD), but the canvas always draws square pixels and there are no video presets | Easy: a vertical zoom of its own in `E:amiga/canvas.c` (to_iy/to_sy and the rows drawn). Very relevant on the Amiga, where PAL/NTSC lores and hires-interlace pixels aren't square | P1 |
| Pixel Aspect Ratio Correction (toggle) | **Missing** | Easy, with the above | P1 |
| Zoom In | **Have** | Done | — |
| Zoom Out | **Have** | Done | — |
| Fit on Screen | **Have** | Done | — |
| Fit Layer(s) on Screen | **Missing** | Easy | P3 |
| Fit Artboard on Screen | **Missing** | Easy, once artboards exist | P3 |
| 100% | **Have** | Done | — |
| 200% | **Have** (Easel adds 50%) | Done | — |
| Print Size | **Missing** | Easy (zoom to the dpi) | P3 |
| Flip Horizontal (the view only) | **Missing** | Moderate: mirror the rows as they're drawn | P3 |
| Screen Mode › Standard, Full Screen With Menu Bar, Full Screen (3 items) | **Have** (0.8): View › Screen Mode: in a window, on Easel's own screen, or full screen in any mode | Done | — |
| Extras (toggle all overlays) | **Missing** (Tab hides the panels, which is something else) | Easy | P2 |
| Show › Layer Edges | **Missing**: `ID_SHOWEDGES` is defined in `E:amiga/ids.h` but never used | Easy | P3 |
| Show › Selection Edges | **Missing**: the marching ants can't be hidden | Easy | P3 |
| Show › Grid | **Have** (0.4) | Done | — |
| Show › Guides / Canvas Guides (2 items) | **Have** (0.4) | Done | — |
| Show › Pixel Grid | **Missing** | Easy: the pixel-art grid at high zoom | P2 |
| Show › Target Path, Artboard Guides, Count, Smart Guides, Slices, Notes, Brush Preview, Mesh, Edit Pins, All, Show Extras Options (11 items) | **Missing** (the active path always shows) | Easy to Moderate, each with its feature | P3 |
| Rulers | **Have** (0.4) | Done | — |
| Snap | **Have** (0.4) | Done | — |
| Snap To › Guides, Grid, Layers, Slices, Document Bounds, All (6 items) | **Partial** (1.1): View › Snap To › Guides, Grid, Document Bounds, Centre Lines and Pixels; Layers and Slices are missing | Easy (layers) | P3 |
| Lock Guides, Clear Guides, Clear Selected Artboard Guides, Clear Canvas Guides, New Guide…, New Guide Layout…, New Guides From Shape (7 items) | **Partial**: New Guide and Clear Guides (0.4), Lock Guides (1.1); New Guide Layout and New Guides From Shape are missing (no artboards) | Easy | P2 |
| Lock Slices / Clear Slices (2 items) | **Missing** | Easy | P3 |
| Pattern Preview (tiles the document) | **Missing** | Easy: handy for Workbench backdrop patterns | P2 |
| Pixel Art Preview | **Missing** | Easy | P2 |

### 2.6 Window › Arrange and Workspace (the panels are in section 15)

Re-checked 2026-11-18 (Easel 1.1): unchanged since 0.2 apart from the rows' notes.

| PhotoCraft item (CAT id) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Arrange › Tile All Vertically/Horizontally, 2-up, 3-up, 4-up, 6-up, Cascade, Tile (11 items; borders between them dragged, #10695) | **Missing**: one canvas, with tabs | Moderate: several canvas objects or windows | P3 |
| Arrange › Consolidate All to Tabs | **Have**: Easel only has tabs | Done | — |
| Arrange › Float in Window / Float All in Windows / New Window for Document (3 items) | **Missing** | Moderate | P3 |
| Arrange › Match Zoom / Location / Rotation / All (4 items) | **Missing** | Easy | P3 |
| Workspace › Essentials, Photography, Painting, Pixel Art, Graphic and Web, Motion (6 items) | **Missing** (Tab and Shift-Tab hide the panels) | Moderate: needs dockable panels | P3 |
| Workspace › Reset, New, Delete, Lock Workspace (4 items; Keyboard Shortcuts… repeats Edit's) | **Missing** | Moderate | P3 |

## 3. Layers: types, masks, groups, clipping, styles and effects, smart objects, adjustment layers

PhotoCraft's model is `Layer` and `LayerContent` in DOC. Easel's is `Layer` and `Doc` in
`E:core/doc.h`: up to 100 flat 8-bit RGBA layers, each canvas-sized, with a name, opacity, blend
mode, visibility, a pixel lock and an alpha lock.

### 3.1 Layer kinds and properties

| PhotoCraft feature (source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Pixel layer, `LayerContent::Raster` (sparse tiles, any size or offset) | **Partial**: full canvas-size buffers with no offset, so off-canvas pixels are lost | Moderate (tiles, section 19) | P2 |
| Group, `LayerContent::Group` (nesting to 100 levels, expand and collapse) | **Missing** | Moderate | P1 |
| Adjustment layer, `LayerContent::Adjustment` | **Missing**: the README says "may come later" | Moderate: Easel's per-rectangle composite can apply a LUT-type adjustment as it goes, at little cost | P1 |
| Fill layer, `LayerContent::Fill` (Solid, Gradient, Pattern; DOC `Fill`) | **Missing** | Easy | P2 |
| Type layer, `LayerContent::Text` | **Missing**: text is rasterised | Moderate | P1 |
| Shape layer, `LayerContent::Shape` | **Missing** | Moderate | P2 |
| Smart Object, `LayerContent::Smart` | **Missing** | Hard: keeps the full-size source as well as its render | P3 |
| Opacity | **Have** | Done | — |
| Fill Opacity (content but not effects), `Layer::fill_opacity` | **Missing** | Easy, once effects exist | P2 |
| Blend mode | **Have**: 18 of the 34 (section 4) | Done | — |
| Visibility | **Have** | Done | — |
| Colour labels (12, `LabelColor`) | **Missing** | Easy | P3 |
| Link groups, `Layer::link_group` | **Missing** | Easy | P3 |
| Blending Options › Blend If (this layer and underlying ranges), `BlendIf` | **Missing** | Easy: a per-pixel test as it composites | P2 |
| Advanced Blending: channels excluded, knockout, blend interior effects or clipped as a group, transparency shapes, mask hides effects (`AdvancedBlending`, `excluded_channels`) | **Missing** | Moderate | P3 |
| Multi-layer selection (Ctrl or Shift-click in Layers) | **Missing** | Easy | P2 |
| Background layer semantics (`has_background_traits`) | **Partial**: there's no locked Background; Clear on a layer that can't be transparent uses the background colour | Easy | P3 |
| Layer XMP metadata, `Layer::xmp` | **Missing** | Not sensible: only for round trips with scripts | P3 |

### 3.2 Masks and clipping (Layer › Layer Mask, Vector Mask, Create Clipping Mask)

| PhotoCraft feature (CAT id / source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Layer Mask › Reveal All / Hide All | **Missing** | Easy: 1 byte a pixel, like the selection | P1 |
| Layer Mask › Reveal Selection / Hide Selection | **Missing** | Easy | P1 |
| Layer Mask › From Transparency | **Missing** | Easy | P2 |
| Layer Mask › Delete | **Missing** | Easy | P1 |
| Layer Mask › Apply | **Missing** | Easy | P1 |
| Layer Mask › Enable Layer Mask (toggle) | **Missing** | Easy | P2 |
| Layer Mask › Link Layer Mask (toggle) | **Missing** | Easy | P2 |
| Painting on the mask, Alt-click to view it, mask thumbnail (`panels/mask_thumbs_ui.rs`) | **Missing** | Easy | P1 |
| Mask properties: density and feather (`panels/mask_props_ui.rs`) | **Missing** | Easy | P2 |
| Vector Mask › Reveal All, Hide All, Current Path, Delete, Enable, Link (6 items) | **Missing** | Moderate: needs paths | P3 |
| Create Clipping Mask `layer.createClippingMask` (`Layer::clipped`) | **Missing** | Easy: composite with the base layer's alpha | P1 |

### 3.3 Layer styles and effects (Layer › Layer Style, 18 items; DOC `Effects`)

Effects are drawn from the layer's alpha: a blur, an offset and a colour. A 68040 can't redraw them
each time the canvas is drawn, so each layer's rendered effects must be cached. That costs RAM, so
effects should be cached only for layers that have them.

| PhotoCraft item (CAT id) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Blending Options… | **Missing** | Easy (dialog) | P2 |
| Bevel & Emboss… (style, technique, depth, size, soften, angle, gloss contour, texture) | **Missing** | Moderate | P2 |
| Stroke… (size, position, fill type, overprint) | **Missing** | Easy | P1 |
| Inner Shadow… | **Missing** | Easy | P2 |
| Inner Glow… | **Missing** | Easy | P2 |
| Satin… | **Missing** | Moderate | P3 |
| Color Overlay… | **Missing** | Easy | P2 |
| Gradient Overlay… | **Missing** | Easy | P2 |
| Pattern Overlay… | **Missing** | Easy, once patterns exist | P3 |
| Outer Glow… | **Missing** | Easy | P2 |
| Drop Shadow… | **Missing** | Easy | P1 |
| Copy Layer Style | **Missing** | Easy | P2 |
| Paste Layer Style | **Missing** | Easy | P2 |
| Clear Layer Style | **Missing** | Easy | P2 |
| Global Light… | **Missing** | Easy | P3 |
| Create Layer (effects to pixel layers) | **Missing** | Easy | P2 |
| Hide All Effects | **Missing** | Easy | P2 |
| Scale Effects… | **Missing** | Easy | P3 |

### 3.4 Fill layers (Layer › New Fill Layer)

| PhotoCraft item | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Solid Color… | **Missing** | Easy | P2 |
| Gradient… | **Missing** | Easy | P2 |
| Pattern… | **Missing** | Easy | P3 |

### 3.5 Adjustment layers (Layer › New Adjustment Layer, 16 items; ADJ)

Easel already has a destructive version of most of these (section 5), so the work is mainly the
layer kind, the Properties panel, and applying the adjustment in `doc_composite`.

| PhotoCraft item | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Brightness/Contrast… | **Missing** as a layer (destructive: Have) | Easy | P1 |
| Levels… | **Missing** as a layer (destructive: Have) | Easy | P1 |
| Curves… | **Missing** as a layer (destructive: Have) | Easy | P1 |
| Exposure… | **Missing** as a layer (destructive: Have) | Easy | P2 |
| Vibrance… | **Missing** as a layer (destructive: Have) | Easy | P2 |
| Hue/Saturation… | **Missing** as a layer (destructive: Partial) | Easy | P1 |
| Color Balance… | **Missing** as a layer (destructive: Have) | Easy | P1 |
| Black & White… | **Missing** as a layer (destructive: Partial) | Easy | P2 |
| Photo Filter… | **Missing** as a layer (destructive: Have) | Easy | P2 |
| Channel Mixer… | **Missing** | Easy | P2 |
| Color Lookup… (.cube/.3dl/.look) | **Missing** | Moderate: a 17³ u8 table with trilinear lookup | P3 |
| Invert | **Missing** as a layer (destructive: Have) | Easy | P2 |
| Posterize… | **Missing** as a layer (destructive: Have) | Easy | P2 |
| Threshold… | **Missing** as a layer (destructive: Have) | Easy | P2 |
| Gradient Map… | **Missing** | Easy | P2 |
| Selective Color… | **Missing** | Easy | P3 |

### 3.6 Smart objects and smart filters (Layer › Smart Objects, 16 items plus Stack Mode; Layer › Smart Filter, 5 items)

| PhotoCraft item | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Convert to Smart Object (lossless transforms) | **Missing** | Hard: keeps the source at full size, and 16 MB machines can't afford it. A stand-in would keep a transform matrix and resample from the original pixels, for one level only | P3 |
| New Smart Object via Copy | **Missing** | Hard | P3 |
| Reveal in Finder (Easel would call it "Show in Drawer") | **Missing** | Easy, once linked smart objects exist | P3 |
| Update Modified Content / Update All Modified Content (2 items) | **Missing** | Moderate | P3 |
| Edit Contents | **Missing** | Moderate | P3 |
| Relink to File… | **Missing** | Moderate | P3 |
| Replace Contents… | **Missing** | Moderate | P3 |
| Export Contents… | **Missing** | Easy | P3 |
| Convert to Linked… / Convert to Embedded (2 items) | **Missing** | Moderate | P3 |
| Convert to Layers | **Missing** | Moderate | P3 |
| Stack Mode › Entropy, Kurtosis, Maximum, Mean, Median, Minimum, Range, Skewness, Standard Deviation, Summation, Variance (11 items) | **Missing** | Moderate: per-pixel statistics over the layers, streamed a row at a time | P3 |
| Rasterize (smart object) | **Missing** | Easy | P3 |
| Warp / Perspective Warp / Puppet Warp on a smart object (3 items) | **Missing** | Hard | P3 |
| Smart Filter › Disable Smart Filters, Delete Filter Mask, Disable Filter Mask, Blending Options…, Clear Smart Filters (5 items); `SmartFilter` in DOC | **Missing** | Hard: every edit re-runs each filter at 68040 speed. A stand-in is "Fade" plus the History Brush | P3 |

## 4. Blend modes

PhotoCraft has 34 modes in BLEND: `PassThrough`, the 27 Photoshop modes of `LAYER_MODES`, and the
six `PAINT_NET_MODES`. Easel has 18 modes in `BM_` (`E:core/doc.h`) and `blend_pixel`. Each
missing mode is a few lines of integer or FPU arithmetic per pixel.

| PhotoCraft mode (BLEND) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Pass Through (groups only) | **Missing** | Easy, once groups exist | P1 |
| Normal | **Have** | Done | — |
| Dissolve | **Missing** | Easy | P2 |
| Darken | **Have** | Done | — |
| Multiply | **Have** | Done | — |
| Color Burn | **Have** | Done | — |
| Linear Burn | **Missing** | Easy | P1 |
| Darker Color | **Missing** | Easy | P2 |
| Lighten | **Have** | Done | — |
| Screen | **Have** | Done | — |
| Color Dodge | **Have** | Done | — |
| Linear Dodge (Add) | **Have**: called "Add" | Done | — |
| Lighter Color | **Missing** | Easy | P2 |
| Overlay | **Have** | Done | — |
| Soft Light | **Have** | Done | — |
| Hard Light | **Have** | Done | — |
| Vivid Light | **Missing** | Easy | P1 |
| Linear Light | **Missing** | Easy | P1 |
| Pin Light | **Missing** | Easy | P1 |
| Hard Mix | **Missing** | Easy | P2 |
| Difference | **Have** | Done | — |
| Exclusion | **Have** | Done | — |
| Subtract | **Have** | Done | — |
| Divide | **Missing** | Easy | P1 |
| Hue | **Have** | Done | — |
| Saturation | **Have** | Done | — |
| Color | **Have** | Done | — |
| Luminosity | **Have** | Done | — |
| Reflect (Paint.NET) | **Missing** | Easy | P3 |
| Glow (Paint.NET) | **Missing** | Easy | P3 |
| Negation (Paint.NET) | **Missing** | Easy | P3 |
| Xor (Paint.NET) | **Missing** | Easy | P3 |
| Paint.NET Color Burn | **Missing** | Easy | P3 |
| Paint.NET Color Dodge | **Missing** | Easy | P3 |
| Painting-only Clear and Behind (`input/blend_keys.rs`; PhotoCraft hasn't implemented Behind either) | **Partial**: the Eraser does what Clear does; the Brush's Mode list has neither | Easy | P3 |

## 5. Adjustments (Image › Adjustments, 22 items)

PhotoCraft's parameters are the `Adjustment` variants in ADJ and `engine/src/cmd/adjust_cmds.rs`.
Easel's are in `E:core/adjust.h` and `dlg_adjust` in `E:amiga/dialogs.c`, and every one previews
live.

| PhotoCraft item (CAT id; parameters) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Brightness/Contrast… (brightness, contrast, Use Legacy) | **Have**: no Legacy toggle | Done | — |
| Levels… (per channel, input and output, gamma, Auto, eyedroppers, presets, CMYK and Lab spaces) | **Partial**: RGB/R/G/B, histogram, gamma and output are there; the black, grey and white eyedroppers, presets (.alv) and Auto options are missing | Easy | P2 |
| Curves… (per channel, spline or pencil (`pencil`), eyedroppers, presets .acv) | **Partial**: the point editor and channels are there; the pencil mode, eyedroppers and presets are missing | Easy | P2 |
| Exposure… (exposure, offset, gamma) | **Have** | Done | — |
| Vibrance… (vibrance, saturation) | **Have** | Done | — |
| Hue/Saturation… (master plus six hue ranges, Colorize; `HueRange`) | **Partial**: master and Colorize only; the six ranges are missing | Easy | P1 |
| Color Balance… (shadows, midtones, highlights, preserve luminosity) | **Have** | Done | — |
| Black & White… (six weights R/Y/G/C/B/M, tint) | **Partial**: three weights (R/G/B) and no tint | Easy | P2 |
| Photo Filter… (colour, density, preserve luminosity) | **Have**: the named filter presets are missing | Done | — |
| Channel Mixer… (3 × 4 matrix, monochrome) | **Missing** | Easy | P1 |
| Color Lookup… (3D LUT, trilinear or tetrahedral, dither) | **Missing** | Moderate | P3 |
| Invert | **Have** | Done | — |
| Posterize… | **Have** | Done | — |
| Threshold… | **Have** | Done | — |
| Gradient Map… (stops, reverse, dither) | **Missing** | Easy: also good for palette-style colouring | P1 |
| Selective Color… (9 ranges × CMYK, relative or absolute) | **Missing** | Easy | P2 |
| Shadows/Highlights… | **Missing** | Moderate: needs a large-radius blur (Easel's box-blur passes would do) | P2 |
| HDR Toning… | **Missing** | Hard: needs data above 8 bits; Easel tone-maps HDR files when it opens them | P3 |
| Desaturate | **Have** | Done | — |
| Match Color… | **Missing** | Easy: match the mean and variance of another document | P3 |
| Replace Color… | **Missing** | Easy: Color Range plus Hue/Saturation | P2 |
| Equalize | **Have** | Done | — |

## 6. Filters (the Filter menu: 75 items, plus the 47 Filter Gallery effects in GAL)

Easel's filters are in `E:core/adjust.h` and `dlg_filter` in `E:amiga/dialogs.c`, and they preview
live. PhotoCraft's are in `crates/algo/src/` (`blur.rs`, `distort.rs`, `stylize.rs`, `render.rs`,
`pixelate.rs`, `other.rs`, `artistic.rs` and so on) and `crates/engine/src/filters`.

| PhotoCraft item (CAT id) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Last Filter `filter.lastFilter` (⌘⌥F) | **Missing** | Easy | P1 |
| Convert for Smart Filters | **Missing** | Hard (section 3.6) | P3 |
| Filter Gallery… (a stack of effects with preview) `filter.filterGallery` | **Missing** | Moderate: a proxy preview, applied in full on OK | P3 |
| Gallery › Artistic: Colored Pencil, Cutout, Dry Brush, Film Grain, Fresco, Neon Glow, Paint Daubs, Palette Knife, Plastic Wrap, Poster Edges, Rough Pastels, Smudge Stick, Sponge, Underpainting, Watercolor (15; GAL) | **Missing** | Easy to Moderate: neighbourhood filters, each a few seconds a megapixel | P3 |
| Gallery › Brush Strokes: Accented Edges, Angled Strokes, Crosshatch, Dark Strokes, Ink Outlines, Spatter, Sprayed Strokes, Sumi-e (8) | **Missing** | Easy to Moderate | P3 |
| Gallery › Distort: Diffuse Glow, Glass, Ocean Ripple (3) | **Missing** | Easy | P3 |
| Gallery › Sketch: Bas Relief, Chalk & Charcoal, Charcoal, Chrome, Conté Crayon, Graphic Pen, Halftone Pattern, Note Paper, Photocopy, Plaster, Reticulation, Stamp, Torn Edges, Water Paper (14) | **Missing** | Easy to Moderate | P3 |
| Gallery › Stylize: Glowing Edges (1) | **Missing** | Easy | P3 |
| Gallery › Texture: Craquelure, Grain, Mosaic Tiles, Patchwork, Stained Glass, Texturizer (6) | **Missing** | Easy to Moderate | P3 |
| Camera Raw Filter… | **Missing** | Hard: dozens of controls on float data | P3 |
| Adaptive Wide Angle… | **Missing** | Hard | P3 |
| Lens Correction… (distortion, chromatic aberration, vignette, perspective) | **Missing** | Moderate. Easy subset: a barrel or pincushion slider, plus Easel's Vignette | P3 |
| Liquify… | **Missing** | Moderate: warp a mesh on a proxy, then resample in full | P3 |
| Vanishing Point… | **Missing** | Hard | P3 |
| Blur › Average | **Missing** | Easy | P2 |
| Blur › Blur | **Have** | Done | — |
| Blur › Blur More | **Missing** | Easy | P2 |
| Blur › Box Blur… | **Have** | Done | — |
| Blur › Gaussian Blur… | **Have** | Done | — |
| Blur › Lens Blur… (depth map, iris, specular) | **Missing** | Hard | P3 |
| Blur › Motion Blur… | **Have** | Done | — |
| Blur › Radial Blur… (spin or zoom, movable centre, quality) | **Partial**: spin or zoom from the middle only | Easy | P3 |
| Blur › Shape Blur… | **Missing** | Moderate | P3 |
| Blur › Smart Blur… | **Missing** | Moderate | P2 |
| Blur › Surface Blur… | **Missing** | Moderate | P2 |
| Blur Gallery › Field Blur… | **Missing** | Moderate: variable-radius blur | P3 |
| Blur Gallery › Iris Blur… | **Missing** | Moderate | P3 |
| Blur Gallery › Tilt-Shift… | **Missing** | Moderate | P3 |
| Blur Gallery › Path Blur… | **Missing** | Hard | P3 |
| Blur Gallery › Spin Blur… | **Missing** | Moderate | P3 |
| Distort › Displace… (displacement map file) | **Missing** | Easy | P2 |
| Distort › Pinch… | **Have** | Done | — |
| Distort › Polar Coordinates… | **Missing** | Easy | P1 |
| Distort › Ripple… | **Have** | Done | — |
| Distort › Shear… | **Missing** | Easy | P2 |
| Distort › Spherize… | **Have** | Done | — |
| Distort › Twirl… | **Have** | Done | — |
| Distort › Wave… (generators, sine/triangle/square, ranges, randomize) | **Partial**: wavelength and amplitude only | Easy | P3 |
| Distort › ZigZag… | **Missing** | Easy | P2 |
| Noise › Add Noise… | **Have**: uniform or Gaussian, monochromatic | Done | — |
| Noise › Despeckle | **Have** | Done | — |
| Noise › Dust & Scratches… | **Missing** | Easy: a median with a threshold | P1 |
| Noise › Median… | **Have** | Done | — |
| Noise › Reduce Noise… | **Missing** | Moderate (`algo/src/denoise.rs`) | P3 |
| Pixelate › Color Halftone… | **Missing** | Easy | P2 |
| Pixelate › Crystallize… | **Missing** | Easy | P2 |
| Pixelate › Facet | **Missing** | Easy | P3 |
| Pixelate › Fragment | **Missing** | Easy | P3 |
| Pixelate › Mezzotint… (10 types) | **Missing** | Easy | P2 |
| Pixelate › Mosaic… | **Have** | Done | — |
| Pixelate › Pointillize… | **Missing** | Easy | P3 |
| Render › Flame… | **Missing** | Moderate | P3 |
| Render › Picture Frame… | **Missing** | Moderate | P3 |
| Render › Tree… | **Missing** | Moderate | P3 |
| Render › Clouds | **Have** | Done | — |
| Render › Difference Clouds | **Missing** | Easy: Clouds with Difference mode | P1 |
| Render › Fibers… | **Missing** | Easy | P3 |
| Render › Lens Flare… | **Missing** | Easy | P2 |
| Render › Lighting Effects… | **Missing** | Moderate | P3 |
| Render › Relight (`filter.render.relight`; engine only, not in a menu) | **Missing** | Moderate | P3 |
| Sharpen › Sharpen | **Have** | Done | — |
| Sharpen › Sharpen Edges | **Missing** | Easy | P2 |
| Sharpen › Sharpen More | **Have** | Done | — |
| Sharpen › Smart Sharpen… | **Missing** | Moderate | P2 |
| Sharpen › Unsharp Mask… | **Have** | Done | — |
| Stylize › Diffuse… | **Missing** | Easy | P2 |
| Stylize › Emboss… | **Have** | Done | — |
| Stylize › Extrude… | **Missing** | Moderate | P3 |
| Stylize › Find Edges | **Have** | Done | — |
| Stylize › Oil Paint… (stylization, cleanliness, scale, bristle detail, lighting) | **Partial**: radius and levels (`flt_oil_paint`); the lighting and bristle controls are missing | Moderate | P3 |
| Stylize › Solarize | **Have** | Done | — |
| Stylize › Tiles… | **Missing** | Easy | P3 |
| Stylize › Trace Contour… | **Missing** | Easy | P2 |
| Stylize › Wind… | **Missing** | Easy | P2 |
| Video › De-Interlace… | **Missing** | Easy: useful for frame-grabber captures (DigiView, VLab, FrameMachine) | P2 |
| Video › NTSC Colors | **Missing** | Easy: add a PAL-legal limit for genlock work | P2 |
| Other › Custom… (5 × 5 kernel, scale, offset) | **Missing** | Easy: ImageFX and ADPro had it | P1 |
| Other › HSB/HSL | **Missing** | Easy | P3 |
| Other › High Pass… | **Have** | Done | — |
| Other › Maximum… | **Have** | Done | — |
| Other › Minimum… | **Have** | Done | — |
| Other › Offset… | **Have**: wrap or not | Done | — |
| Other › Color to Alpha (`filter.other.colorToAlpha`; engine only) | **Missing** | Easy: good for sprites and brushes | P2 |
| Live preview on the canvas, through the selection | **Have**: the dialogs' Preview tick (`E:amiga/dialogs.c` `sd_apply`) | Done | — |

## 7. Selection features (the Select menu, 25 items, and the selection model)

Easel's selection is an 8-bit mask (`Doc::sel`, `E:core/paint.h` `sel_*`), so feathered and
anti-aliased edges already work. PhotoCraft's commands are in `engine/src/cmd/selection_cmds.rs`,
`select_extra_cmds.rs` and `smartselect_cmds.rs`, with the algorithms in `algo/src/selection.rs`,
`segment.rs` and `matting.rs`.

| PhotoCraft item (CAT id / source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| All `select.all` | **Have** | Done | — |
| Deselect | **Have** | Done | — |
| Reselect (⇧⌘D) | **Missing**: `ID_RESELECT` is defined in `E:amiga/ids.h` but not in the menu or the code | Easy | P1 |
| Inverse Selection | **Have** | Done | — |
| Convert to Shape (selection to path) | **Missing** | Moderate: outline tracing plus curve fitting | P3 |
| All Layers / Deselect Layers (2 items) | **Missing** | Easy, with multi-layer selection | P2 |
| Find Layers | **Missing** | Easy | P3 |
| Isolate Layers | **Missing** | Easy | P3 |
| Color Range… (sampled colours with add and subtract, Reds…Magentas, Highlights/Midtones/Shadows, Skin Tones, Out of Gamut, localized clusters, preview modes) | **Partial**: one colour and fuzziness (`sel_color_range`) | Easy | P2 |
| Focus Area… | **Missing** | Moderate: Laplacian energy (`segment.rs` `focus`) | P3 |
| Subject | **Missing** | Hard: PhotoCraft's version is classical saliency plus GrabCut, with no AI, so it's possible, but it takes minutes on a proxy | P3 |
| Sky | **Missing** | Hard: the same | P3 |
| Select and Mask… (edge detection radius, smooth, feather, contrast, shift edge, decontaminate, output to mask or layer, view modes) | **Missing** | Moderate (radius and smooth); Hard (hair matting) | P3 |
| Modify › Border… | **Have** | Done | — |
| Modify › Smooth… | **Have** | Done | — |
| Modify › Expand… | **Have** | Done | — |
| Modify › Contract… | **Have** | Done | — |
| Modify › Feather… | **Have** | Done | — |
| Grow | **Missing**: `ID_GROWSIM` is defined but not wired up | Easy: a contiguous wand from the selection's colours | P1 |
| Similar | **Missing** | Easy | P1 |
| Transform Selection | **Missing** | Moderate: shares the on-canvas transform handles | P2 |
| Edit in Quick Mask Mode (Q) | **Missing** | Easy: paint the existing mask, drawn as a red overlay | P1 |
| Load Selection… | **Missing** | Easy, once alpha channels exist | P2 |
| Save Selection… | **Missing** | Easy | P2 |
| Float and drop the selection (`select.float`, `select.drop`) | **Partial**: Move drags the selected pixels, but there's no floating selection | Easy | P3 |
| Alpha and spot channels (Channels panel; PSD and TIFF extra channels) | **Missing** | Easy (alpha channels as extra 8-bit masks); Not sensible (spot inks are a print feature) | P2 |
| Marching ants and the selection modes on Shift, Alt and Shift-Alt | **Have** | Done | — |
| Anti-aliased, feathered 8-bit selection | **Have** | Done | — |

## 8. Painting and brushes

PhotoCraft's engine is `crates/paint` (BRUSH; `dynamics.rs`, `render.rs`, `mixer.rs`,
`symmetry.rs`, `tablet` crate). Easel's is `Brush` and `stroke_*` in `E:core/paint.h` and
`E:core/paint.c`: round dabs with size, hardness, opacity, flow, spacing and blend mode.

| PhotoCraft feature (source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Round tip: size and hardness | **Have** (1-2500 px) | Done | — |
| Spacing (`spacing`, `spacing_enabled`) | **Partial**: fixed at 25% (`E:amiga/main.c:294`) and not shown | Easy | P1 |
| Angle and roundness (elliptical tip) | **Missing** | Easy | P2 |
| Flip X/Y of the tip | **Missing** | Easy | P3 |
| Sampled (bitmap) tips, `TipShape::Sampled` | **Missing** | Easy: the same as a DPaint brush (section 21) | P1 |
| Aliased or square tip (`aliased`, `square`) | **Partial**: the Pencil is hard-edged; there's no square tip | Easy | P2 |
| Shape Dynamics: size, angle and roundness jitter; controls Fade/Pressure/Tilt/Direction (`ShapeDynamics`, `Control`) | **Missing** | Easy (jitter, fade, direction); Moderate (pressure, tilt) | P2 |
| Scattering: both axes, count, count jitter | **Missing** | Easy | P2 |
| Texture: pattern, scale, mode, depth | **Missing** | Moderate | P3 |
| Dual Brush | **Missing** | Moderate | P3 |
| Color Dynamics: foreground/background jitter, hue, saturation, brightness, purity | **Missing** | Easy | P2 |
| Transfer: opacity and flow jitter | **Missing** | Easy | P3 |
| Brush Pose: tilt, rotation and pressure overrides | **Missing** | Easy | P3 |
| Noise | **Missing** | Easy | P3 |
| Wet Edges | **Missing** | Easy | P3 |
| Build-up (airbrush) and its rate | **Missing** | Easy: dabs on a timer while the button is held | P2 |
| Smoothing, including Pulled String (`Smoothing`, `dynamics.rs` `Smoother`) | **Missing** | Easy | P2 |
| Protect Texture | **Missing** | Easy | P3 |
| Brush presets and the Brushes panel (`BrushPreset`, `presets.rs`) | **Missing** | Easy | P2 |
| .abr brush import and export (`psd/src/abr.rs`) | **Missing** | Moderate | P3 |
| Painting symmetry: Vertical, Horizontal, Dual, Diagonal, from a path (`SymmetryMode`) | **Missing** | Easy: DPaint users will expect it | P2 |
| Pen pressure, tilt and rotation (`crates/tablet`, `input/stylus.rs`) and the pressure curve | **Missing** | Moderate: Intuition's `IESUBCLASS_NEWTABLET` and `TABLETA_Pressure` (V39 and later) carry pressure from tablet drivers | P2 |
| Brush outline cursor (Cursors preferences) | **Have**: drawn in COMPLEMENT (`E:amiga/canvas.c`) | Done | — |
| Alt for the eyedropper with painting tools | **Have** | Done | — |
| Shift-click draws a straight line | **Have** | Done | — |
| Gradient presets and the gradient editor: stops, midpoints, opacity stops, .grd (`gradient_fill_cmds.rs`, DOC `Fill::Gradient`) | **Missing** | Easy | P1 |
| Pattern library: built-ins, Define Pattern, .pat (`pattern_cmds.rs`) | **Missing** | Easy | P2 |
| Colour picker dialog: HSB/RGB/Lab/CMYK/hex, web colours only, sampling the screen (`docs/color-picker.md`) | **Partial**: `pick_color` gives RGB, HSV and hex | Easy | P2 |
| Deterministic, replayable strokes (`BrushSettings::seed`) | **Missing** | Easy: needed for actions | P3 |

## 9. Type and text

PhotoCraft's model is `TextLayer` in DOC and TXT, with commands in `engine/src/cmd/type_*`. Easel's
(0.9) is the type layer in `E:core/type.h`: a `TextParams` (UTF-8 text in style runs, point or
paragraph text, alignment, leading, indents, warp, a transform) drawn by the compositor whenever it
is wanted, edited on the canvas with a caret (`E:amiga/canvas.c`), with the Character, Paragraph
and Warp Text dialogs in `E:amiga/typedlg.c`. Fonts are TrueType/OpenType (`E:core/ttraster.c`)
or Amiga bitmap fonts (`E:core/amifont.c`). Re-checked 2026-11-18 (Easel 1.1).

| PhotoCraft feature (CAT id / source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Editable type layers, edited on the canvas with a caret (`type_caret_cmds.rs`) | **Have** (0.9): `LK_TEXT`, `text_hit`, `text_caret_move`, Shift and the arrows select, Esc finishes | Done | — |
| Font families and styles: weight, italic, PostScript name, variable axes, font library and favourites | **Partial**: family, bold and italic faces per run (`TextRun`); variable fonts draw at their default design (`cff2` in `E:core/ttraster.h`); weights beyond bold, axes and favourites are missing | Easy (weights); Moderate (variable axes) | P3 |
| Faux Bold and Faux Italic | **Have** (0.9): `faux_bold`, `faux_italic` | Done | — |
| Size | **Have** (1-2000 px a run) | Done | — |
| Leading | **Have** (0.9): `leading`, auto or pixels | Done | — |
| Tracking, and kerning metric/optical/manual | **Partial**: tracking per run and the font's kerning pairs (`kern`, `text_kern`); optical and manual kerning are missing | Moderate (optical) | P3 |
| Baseline shift, horizontal and vertical scale | **Partial**: baseline shift per run; horizontal and vertical scale are missing | Easy | P3 |
| Underline and strikethrough | **Have** (0.9) | Done | — |
| All Caps, Small Caps, superscript and subscript (`Caps`, `FontBaseline`) | **Missing** (size and baseline shift can be set by hand) | Easy: per-glyph size in the layout (`E:core/type.c`) | P3 |
| Colour per character run (`TextRun`) | **Have** (0.9) | Done | — |
| Alignment Left/Center/Right | **Have** | Done | — |
| Justify Left/Center/Right/All (`TextAlign`) | **Partial**: `TA_JUSTIFY` (last line left) and `TA_JUSTIFY_ALL`; last line centred or right is missing | Easy | P3 |
| Indents, space before and after, hyphenation (`ParagraphStyle`) | **Partial**: first-line indent and space before and after (`indent`, `space_before`, `space_after`); left/right indents and hyphenation are missing | Easy (indents); Moderate (hyphenation) | P3 |
| Paragraph text in a box with wrapping (`TextShape::Box`) | **Have** (0.9): drag a box with the Type tool | Done | — |
| Type on a path (`TextShape::Path`) | **Missing** | Moderate: paths exist (`E:core/path.h`) | P3 |
| Area type (`TextShape::Area`) | **Missing** | Moderate | P3 |
| Type › Orientation › Horizontal / Vertical (2 items) | **Have** (0.9): Layer › Type › Horizontal / Vertical, the Vertical Type tool | Done | — |
| Type › Anti-Alias › None, Sharp, Crisp, Strong, Smooth, Windows LCD, Windows (7 items) | **Have** for the Amiga's three: None, Sharp (hinted) and Smooth (`TAA_`); Crisp and Strong are missing; the Windows ones are **Left out** (they are Windows' own rendering) | Easy (Crisp, Strong) | P3 |
| Type › OpenType › Standard Ligatures, Contextual Alternates, Discretionary Ligatures, Swash, Oldstyle, Stylistic Alternates, Titling Alternates, Ornaments, Ordinals, Fractions (10 items) | **Missing**: no GSUB lookups | Moderate: GSUB parsing in `ttraster.c` | P3 |
| Type › Panels › Character, Paragraph, Glyphs, Character Styles, Paragraph Styles (5 items; the panels are rated in section 15) | **Partial**: Character and Paragraph are dialogs (Layer › Type); Glyphs and the style panels are missing | Easy to Moderate | P3 |
| Type › Create Work Path | **Have** (0.9): `text_to_path` | Done | — |
| Type › Convert to Shape | **Missing** (Create Work Path, then a Custom Shape from it, does the same in two steps) | Easy | P3 |
| Type › Rasterize Type Layer | **Have** (0.9) | Done | — |
| Type › Convert to Paragraph Text / Convert to Point Text (2 items) | **Missing** | Easy | P3 |
| Type › Warp Text… (Arc, Arc Lower and Upper, Arch, Bulge, Shell Lower and Upper, Flag, Wave, Fish, Rise, Fisheye, Inflate, Squeeze, Twist; `TextWarp`) | **Have** (0.9; 1.1 added Arc Lower and Upper, Shell Lower and Upper, Fisheye and Inflate): all 15, with bend and both distortions (`map_pt` in `E:core/type.c`). The Vertical direction is kept but drawn as horizontal | Done | — |
| Type › Font Preview Size › Small…Huge (5 items) | **Missing** | Easy | P3 |
| Type › Language Options › Default, East Asian, Middle Eastern, Middle Eastern & South Asian composer (4 items) | **Missing** | Hard: complex shaping and RTL text | P3 |
| Type › Update All Text Layers | **Missing** (type layers are drawn from their settings, but nothing redraws them all after the fonts change) | Easy | P3 |
| Type › Replace All Missing Fonts / Resolve Missing Fonts… (2 items) | **Partial**: a missing family is substituted automatically (`E:core/fonts.h`); there's no dialog to choose the replacement | Easy | P3 |
| Type › Paste Lorem Ipsum | **Have** (1.1): Layer › Type › Paste Lorem Ipsum, into the active type layer or as a new paragraph (`lorem_ipsum` in `E:core/scripts.c`) | Done | — |
| Type › Load / Save Default Type Styles (2 items) | **Missing** | Easy | P3 |
| Type options bar: a list of preset font sizes (PhotoCraft #12718, since eb6c1ab) | **Missing**: the size is a slider | Easy | P3 |
| Type options bar: a button showing the Character and Paragraph panels (#13020) | **Missing** | Easy | P3 |
| Cmd-. (Ctrl-.) cancels a type editing session (#12076) | **Partial**: Esc finishes editing; there's no key that throws the edit away (Undo does) | Easy | P3 |

## 10. Vector paths and shapes

PhotoCraft's model is VEC (`Path`, `Subpath`, `LiveShape`, `ShapeStroke`), with commands in
`engine/src/cmd/vector_cmds.rs` and `path_edit_cmds.rs`. Easel's (0.9) are Bézier paths
(`E:core/path.h`: the work path and saved paths, the Pen and selection tools, the Paths panel),
their operations (`E:core/pathops.h`) and shape layers (`E:core/shape.h`), drawn by the compositor
whenever wanted. Re-checked 2026-11-18 (Easel 1.1).

| PhotoCraft feature (CAT id / source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Bézier paths: subpaths, path operations, fill rule, work path and saved paths | **Have** (0.9) | Done | — |
| Fill Path (with a colour, pattern or history) | **Partial**: with a colour, opacity and mode (`path_fill`); pattern and history fills are missing | Easy | P3 |
| Stroke Path (with a painting tool, simulated pressure) | **Have** (0.9): `path_stroke` with the brush and simulated pressure | Done | — |
| Path to selection (`path.toSelection`) | **Have** (0.9): `path_to_selection` with the four operations, feather and anti-alias | Done | — |
| Selection to work path (tolerance) | **Have** (0.9): `path_from_selection` | Done | — |
| Shape layers: resolution-independent, solid/gradient/pattern fill, stroke width, dashes, align inside/centre/outside, caps, joins (`StrokeAlign`, `LineCap`) | **Partial** (0.9): solid fill and stroke with opacity, width, dashes, alignment, caps, joins and miter limit; gradient and pattern fills are missing | Easy | P2 |
| Live shape properties: per-corner radii, polygon sides and star ratio, editing in Properties | **Partial**: Shape Properties edits one corner radius, polygon sides, star indent and angle, line arrowheads; per-corner radii are missing | Easy | P3 |
| Layer › Combine Shapes › Unite Shapes, Subtract Front Shape, Intersect Shape Areas, Exclude Overlapping Shapes, Merge Shape Components (5 items) | **Missing** | Moderate: polygon boolean operations | P3 |
| Custom shape library (built-ins and Shapes panel; PhotoCraft can't read .csh either) | **Partial**: the Custom Shape tool draws the active path (any saved path is a shape); there's no built-in library or Shapes panel | Easy | P3 |
| Anti-aliased pixel shapes (the Pixels mode) | **Have**: every shape tool can draw pixels (or a path) instead of a layer | Done | — |
| Snap vector tools and transforms to the pixel grid (preference) | **Have** (0.4, 1.1 in the menu): View › Snap To › Pixels (`SNAP_PIXEL` in `E:core/guides.h`) | Done | — |
| Export › Paths to Illustrator / paths to a file, with Document Bounds (#14068 since eb6c1ab) | **Have** (1.1) as SVG: File › Export › Paths to SVG writes the saved paths, the work path and every shape layer (fill, stroke, opacity), and the document's bounds as a frame (`svg_write_paths` in `E:core/svg.c`). An Illustrator .ai isn't written | Done | — |
| Shapes from an SVG drawing (`io::svg` as shape layers or a vector smart object) | **Partial** (1.1): an SVG opens as editable shape layers: paths, rectangles, circles, ellipses, lines, polylines and polygons, in groups, with transforms, fill, stroke, opacity and the viewBox (`svg_load_doc`). Text, images, gradients, CSS classes, clipping and filters are left out; there's no vector smart object | Moderate | P3 |
| Path mode for the Shape tools (#9453) | **Have** (0.9): each shape tool draws a shape layer, a path or pixels | Done | — |

## 11. Colour management, colour modes and bit depths

PhotoCraft's model is `ColorMode` in `crates/color/src/lib.rs`, with the depth as runtime data.
Image › Mode is CAT `image.mode.*`, colour management is `crates/cms`, and indexed conversion is
QNT. Easel (0.4) has RGB, Grayscale, Indexed and Bitmap documents (`MODE_` in `E:core/doc.h`,
conversion in `E:core/palette.c`), all 8 bits a channel, with no colour profiles. It shows pictures
on RTG screens exactly, or dithered to a colour cube on AGA and ECS (an indexed picture with its
own palette). Re-checked 2026-11-18 (Easel 1.1).

| PhotoCraft feature (CAT id / source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Mode › Bitmap (50% threshold, pattern dither, diffusion, halftone screen, custom pattern; QNT 1-bit methods) | **Have** (0.4): 50% Threshold, Pattern Dither, Diffusion Dither and Halftone Screen (`bitmap_names`); a custom pattern is missing | Done | — |
| Mode › Grayscale (a single-channel document) | **Have** (0.4): `MODE_GRAY`, saved as grey PNG, TIFF, Targa, PGM and PDF | Done | — |
| Mode › Duotone (mono, duo, tri, quadtone with curves) | **Missing** (Photoshop duotones open as their grey composite) | Moderate | P3 |
| Mode › Indexed Color: palettes Exact, System (Mac OS), System (Windows), Web, Uniform, Perceptual, Selective, Adaptive, Custom, Previous; forced colours; transparency; matte; dither None/Diffusion/Pattern/Noise with amount; preserve exact colours | **Have** (0.4): median cut, octree and Wu from the picture, the current palette, Workbench, MagicWB, AGA and web presets instead of the Mac and Windows ones, OCS 12-bit, forced colours, transparency, 12 dithers; exact when the picture has few enough colours. The matte (below) and a dither amount are missing | Done | — |
| Indexed Color › Matte (#9887 since eb6c1ab): the colour transparent edges are blended against | **Missing** | Easy | P3 |
| Mode › RGB Color | **Have** | Done | — |
| Mode › CMYK Color | **Missing** (CMYK JPEG, TIFF and PSD open converted to RGB) | Moderate: 5 bytes a pixel with alpha | P3 |
| Mode › Lab Color | **Missing** (Lab TIFF and PSD open converted) | Moderate | P3 |
| Mode › Multichannel | **Missing** | Moderate | P3 |
| Mode › 8 Bits/Channel | **Have** | Done | — |
| Mode › 16 Bits/Channel | **Missing**: 16-bit PNG, TIFF and PSD layers are cut to 8 bits on opening | Hard: doubles the memory (24 MB for a 2000 × 1500 layer), and every routine needs a 16-bit path | P3 |
| Mode › 32 Bits/Channel (float, HDR) | **Left out**: 16 bytes a pixel and slow floats on a 68040; HDR, PFM, float TIFF and FITS are tone-mapped or scaled on opening instead | Not sensible | — |
| Mode › Color Table… (edit the palette, load and save .act) | **Have** (0.4): Image › Color Table | Done | — |
| A 1-bit TIFF (or PBM) opens in Bitmap mode (#10539 since eb6c1ab) | **Missing**: it opens as RGB (a 1-bit ILBM opens indexed) | Easy | P3 |
| ICC profiles kept when opening and saving (PNG iCCP, JPEG APP2, TIFF tag 34675, PSD) | **Missing** | Easy (keep the bytes); Moderate (honour them) | P3 |
| A pure-code CMS: v2/v4 matrix/TRC and CLUT profiles, 4 intents, black point compensation (`crates/cms`) | **Missing** | Moderate: matrix/TRC transforms through lookup tables are fast; CLUT transforms are slower | P3 |
| View › Proof Colors (⌘Y), computed on the GPU in PhotoCraft | **Missing** | Moderate: a CPU 3D LUT applied as the canvas is drawn | P3 |
| View › Gamut Warning (⇧⌘Y) | **Missing** | Moderate | P3 |
| Proof Setup › Custom, Working CMYK and the Cyan/Magenta/Yellow/Black/CMY plates (7 items) | **Missing** | Moderate | P3 |
| Proof Setup › Legacy Macintosh RGB, Internet Standard RGB, Monitor RGB (3 items) | **Missing** | Easy: gamma 1.8 and 2.2 matrices | P3 |
| Proof Setup › Color Blindness Protanopia and Deuteranopia (2 items) | **Missing** | Easy: one 3 × 3 matrix each | P3 |
| View › 32-bit Preview Options… | **Left out**: there are no 32-bit documents | Not sensible | — |
| Monitor profile for the display (`apps/photocraft/src/monitor_profile.rs`) | **Missing** | Moderate. Amiga stand-in: a display gamma preference | P3 |

## 12. History and undo

PhotoCraft keeps copy-on-write tiles and history commands (`engine/src/cmd/history_*`). Easel
keeps undo steps that save only the changed regions (`doc_note_layer`), within 40 MB
(`E:core/doc.h`), and has a History panel.

| PhotoCraft feature (source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Multi-level undo and redo, named steps | **Have** | Done | — |
| History panel: click a state to go back or forward | **Have** (`E:amiga/panels.c` `history_class`) | Done | — |
| Step Backward / Step Forward | **Have** | Done | — |
| Snapshots: new, rename, delete, select (`history.newSnapshot` and others) | **Missing** | Moderate: a full copy costs RAM, so offer to put snapshots in T: or a scratch drawer | P2 |
| New Document from a state (`history.newDocument`) | **Missing** | Easy | P3 |
| Delete a state (`history.deleteState`) | **Missing** | Easy | P3 |
| History Options: non-linear history, automatic first snapshot (`PREFS` history options) | **Missing** | Moderate | P3 |
| History states limit (`PREFS` `history_states`) and memory use | **Partial**: a fixed 40 MB budget, too much for a 16 MB machine and too little for 64 MB | Easy: a preference | P1 |
| History log: session, concise, detailed; to the metadata or a file (Preferences › History Log) | **Missing** | Easy | P3 |
| Autosave and crash recovery (`PREFS` `autosave`, `autosave_minutes`, `recover_on_launch`; `.pcraft`) | **Missing** | Moderate: an ORA to T: or a chosen drawer every N minutes. Very valuable on the Amiga | P2 |

## 13. File formats

PhotoCraft's formats are in FMT, FFP and `crates/io` (PSD/PSB, layered TIFF, ORA, PDN, XCF, KRA,
SVG, `.pcraft`, Affinity, `crates/raw`, `crates/heif`). Easel's readers are in `E:core/codec.h`
(`FMT_`), `E:core/rasters.c`, `E:core/amigafmt.c`, `E:core/psd.c`, `E:core/ora.c`,
`E:core/frames.c` (1.1: animation frames) and `E:core/svg.c` (1.1); its writers in `codec.h`,
`E:core/tiffw.c`, `E:core/tgaw.c`, `psd.c`, `ora.c` and (1.1) `E:core/xwrite.c` and `frames.c`.
Every writer is read back by Easel's own reader in the native tests and checked by an independent
one (Pillow, and ImageMagick, poppler and psd-tools where installed: `E:tests/check_*.py`); the
1.1 readers and writers are also fuzzed with a work bound per file (`E:tests/xwritetest.c`,
`E:tests/frametest.c`). Re-checked 2026-11-18 (Easel 1.1).

### 13.1 Read

| Format (PhotoCraft source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| PSD/PSB with layers, groups, masks, effects, text, smart objects, 1-32 bits, every mode (`crates/psd`) | **Partial** (0.6): 8- and 16-bit RGB and grey layers with names, opacity, blend modes, visibility, clipping, fill opacity, locks, Blend If, masks (density, feather) and groups; any other kind opens flattened. Effects, type, smart objects (and their filters, such as Puppet Warp, #8140), frame animation (#8633) and CMYK/Lab layers are missing | Moderate (effects); Hard (full fidelity) | P3 |
| TIFF: every page, layered TIFF, 8/16/32f, CMYK/Lab, BigTIFF | **Partial**: the first page, flattened, cut to 8 bits; every compression and colour space (`E:core/tiff.c`) | Easy (more pages, as layers); Moderate (layers) | P3 |
| JPEG: 8-bit Gray/RGB/CMYK, ICC/EXIF/XMP, EXIF orientation (`algo/src/exif.rs`) | **Have**: baseline and progressive, grey, RGB and CMYK, turned upright from EXIF (0.6), File Info from XMP and the comment; opened at a reduced size when memory is short. ICC profiles aren't kept (section 11) | Done | — |
| PNG: 8/16-bit, palette PNGs as Indexed, APNG first frame, sTER | **Partial**: every kind (16 bits keep the high byte, an APNG shows its first frame); a palette PNG opens as RGB, not indexed | Easy | P3 |
| GIF: first frame, and every frame for video layers | **Have**: the first frame when opened; every frame (with disposal, transparency and times) through File › Import Frames to Layers (1.1) | Done | — |
| BMP (palette BMPs open as Indexed) | **Have** (palette BMPs open as RGB) | Done | — |
| WebP: lossless, lossy, animation's first frame | **Have** | Done | — |
| AVIF (PhotoCraft can't read it, FFP #463) | **Have**: dav1d, 8/10/12-bit, grids; Easel is ahead here | Done | — |
| HEIF/HEIC (`crates/heif`) | **Missing** | Hard: HEVC decoding takes minutes per photo on a 68040 | P3 |
| OpenEXR (half and float, deep, Cryptomatte) | **Missing** | Moderate: PIZ and ZIP decoding, then the tone-map Easel uses for HDR | P3 |
| Radiance HDR | **Have**: tone-mapped | Done | — |
| PBM/PGM/PPM/PAM/PFM | **Have** | Done | — |
| FITS | **Have**: scaled to 8 bits | Done | — |
| Targa | **Have** | Done | — |
| DPX | **Have** | Done | — |
| Cineon (.cin) | **Missing** | Easy | P3 |
| DICOM (single frame, uncompressed) | **Missing** | Easy | P3 |
| PCX (FMT `Pcx`) | **Have** | Done | — |
| XPM | **Have** | Done | — |
| DDS (BC1-BC3 in PhotoCraft) | **Have**: also BC4, BC5 and BC7 | Done | — |
| QOI | **Have** | Done | — |
| ICO | **Have**: the biggest picture (also CUR) | Done | — |
| Amiga IFF ILBM/PBM (PhotoCraft: 1-8 planes, 24-bit) | **Have**: also EHB, HAM6/8, 32-bit deep, ACBM, RGBN/RGB8, DEEP, SHAM/CTBL/PCHG, every frame of an ANIM (1.1, op 0 and 5) and Workbench icons; palette pictures open indexed with their cycling; Easel is far ahead | Done | — |
| Camera raw: DNG, CR2 (and sRAW/mRAW, #7122), NEF/NRW, ARW, RW2, PEF, ORF, RAF (`crates/raw`) | **Missing** | Hard: a full-size demosaic of 24 MP needs 96 MB. Stand-in: open the embedded JPEG preview (Easy), or demosaic at half size (Moderate) | P3 |
| SVG as editable shape layers or a vector smart object | **Partial** (1.1): opens as shape layers (section 10, `E:core/svg.c`); text, images and gradients are left out | Moderate | P3 |
| OpenRaster with groups and masks (`docs/ora.md`) | **Have** (0.3): groups, masks, clipping, adjustment, fill, type and shape layers | Done | — |
| Paint.NET .pdn | **Missing** | Moderate | P3 |
| GIMP .xcf (layers, groups, masks, channels, selection) | **Missing** | Moderate: tiles with RLE or zlib | P3 |
| Krita .kra | **Missing** | Moderate: zip, XML and LZF tiles | P3 |
| Affinity .af/.afphoto/.afdesign/.afpub | **Missing** | Hard | P3 |
| PhotoCraft .pcraft (native, incremental) | **Missing** | Moderate | P3 |
| Metadata kept: ICC, EXIF, XMP, text and dpi (FMT caps) | **Partial**: dpi, and File Info (title, author, description, keywords, copyright, URL) from PNG text chunks, JPEG XMP and comment, TIFF tags, PSD and ORA (0.6); ICC and EXIF aren't kept | Easy (keep the blocks) | P3 |
| Open As (a format forced, #14286) | **Missing** | Easy | P3 |

### 13.2 Write

| Format (PhotoCraft source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| PSD/PSB with layers (`crates/psd`) | **Have** (0.6): 8-bit RGB with layers, masks, groups, blend modes, File Info and the merged composite (which PhotoCraft's maximizeCompatibility choice, #10001, makes optional); PSB isn't written | Done | — |
| TIFF: 8/16/32f, none/LZW/ZIP, layered (`tiffCompression`) | **Partial** (0.6): 8-bit RGB(A), grey, palette and 1-bit, uncompressed, PackBits, LZW or Deflate (`tiff_encode_doc`; the menus write LZW); 16/32 bits and layers are missing | Easy (the compression choice); Moderate (layers) | P3 |
| JPEG: quality 0-12 (or 1-100), keeps an opened file's quality, ICC/EXIF | **Have**: quality 1-100, JFIF dpi, File Info as XMP and comment; 1.1: Save keeps an opened JPEG's quality (`jpeg_estimate_quality`). Progressive JPEG (#13773) and ICC/EXIF aren't written | Done | — |
| PNG: 8/16-bit, gray, palette for Indexed, ICC, text | **Have**: RGB(A), palette with tRNS for Indexed and Bitmap, grey (0.4), File Info text chunks and XMP (0.6), pHYs; 16-bit and ICC are missing (no 16-bit documents) | Done | — |
| GIF: 256-colour palette | **Have** (0.4): the picture's palette or 256 colours; 1.1: animated, from the layers | Done | — |
| BMP (palette BMP for Indexed) | **Partial**: 24-bit, or 32-bit with transparency; palette BMPs are missing | Easy | P3 |
| WebP: lossless, and lossy VP8 | **Partial** (1.1): lossless (VP8L with the subtract-green and predictor transforms, LZ77 and Huffman codes: `webp_encode_lossless`; Pillow reads it exactly); lossy VP8 is missing | Hard (a lossy VP8 encoder is slow) | P3 |
| AVIF (optional `avif` feature) | **Left out**: AV1 encoding takes hours on a 68040 | Not sensible | — |
| OpenEXR | **Left out**: there are no float documents | Not sensible | — |
| Radiance HDR | **Missing** | Easy, but of little use without float data | P3 |
| PBM/PGM/PPM/PAM/PFM | **Have** (1.1): PBM for Bitmap, PGM for grey, PPM, PAM with alpha (`pnm_encode`); PFM is missing (no float data) | Done | — |
| FITS (header cards kept) | **Have** (1.1): 8-bit, one plane or three (`fits_encode`); an opened file's header cards aren't kept | Done | — |
| Targa (RLE) | **Have** (0.6): raw or RLE, true colour, grey and colour-mapped | Done | — |
| DPX (8/10/12/16 bits) | **Have** (1.1): 8 bits, or 10 bits packed (Method A), RGB or RGBA (`dpx_encode`; ImageMagick reads both); 12 and 16 bits are missing | Done | — |
| Cineon | **Missing** | Easy | P3 |
| ICO | **Have** (1.1): every standard size from 16 to 256 up to the picture's, small ones as DIBs with a mask, 128 and 256 as PNG (`ico_encode`) | Done | — |
| QOI | **Have** (1.1): `qoi_encode` | Done | — |
| DDS (uncompressed or BC1-BC3, mipmaps) | **Have** (1.1): uncompressed BGRA, BC1 (with transparency) or BC3, with the mipmaps (`dds_encode`); BC2 is missing | Done | — |
| Amiga IFF ILBM (PhotoCraft writes 24-plane ByteRun1 with a 1-bit mask) | **Have**: 24-bit, 1-8 planes, EHB, HAM6/8, SHAM, mask plane or transparent colour, CRNG, ModeID (0.4); an IFF ANIM from the layers (1.1) | Done | — |
| OpenRaster | **Have**: Easel's own format | Done | — |
| PDF, one flattened page (`io::pdf::export`) | **Have** (1.1): one page the picture's size at its resolution, the picture lossless (Flate, with a soft mask for transparency) or as a JPEG, grey for Grayscale and Bitmap pictures, File Info as the document information (`pdf_encode`; poppler reads it exactly) | Done | — |
| .pcraft | **Missing** | Moderate | P3 |

### 13.3 Presets and data files (read and write; FFP "Presets and data files")

| Format | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Brushes .abr: read and write | **Partial** (0.5): read (versions 1, 2 and 6-10); not written. Easel's own brush presets and DPaint ILBM brushes load and save | Moderate | P3 |
| Gradients .grd: read and write | **Missing** both ways (GIMP .ggr gradients load and save, 0.5) | Easy | P3 |
| Patterns .pat: read and write | **Missing** both ways (pattern sets load and save as IFF, 0.5) | Easy | P3 |
| Swatches .aco, .act, .ase: read and write | **Have** (0.5): .aco, .act, .ase, GIMP .gpl, JASC/RIFF .pal, IFF CMAP and Easel's own, both ways | Done | — |
| LUTs .cube (read and write), .3dl and .look (read) | **Missing** | Moderate | P3 |
| Keyboard shortcuts .kys: read and write | **Missing** (Easel keeps its own in PROGDIR:Easel.keys, 0.8) | Easy | P3 |
| ICC profiles: read | **Missing** | Moderate | P3 |
| Actions .atn: read and write (`psd/src/atn.rs`) | **Missing** | Moderate: map the events to Easel's commands, once there are actions | P3 |
| Styles .asl: read and write | **Missing** | Moderate | P3 |
| Contours .shc: read and write | **Missing** | Easy | P3 |

## 14. Import and export options

PhotoCraft's options are in `crates/codecs` (`EncodeOptions`), `engine/src/cmd/file_cmds.rs`,
`export_preset_cmds.rs` and PREFS (export, file handling). Easel's are in `dlg_export` in
`E:amiga/dialogs.c`, the Save and Export logic in `E:amiga/main.c`, the Amiga ILBM options in
`E:amiga/modedlg.c` and EaselConvert's arguments (`E:amiga/convert.c`). Re-checked 2026-11-18
(Easel 1.1).

| PhotoCraft option (source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| JPEG options: quality, keeping an opened file's quality (FFP #2399), remembering the last quality and format (#9661) | **Have** (1.1): the quality slider remembers the last quality; Save keeps an opened JPEG's quality, estimated from its tables | Done | — |
| JPEG options: the size the file will be (#9952), progressive at every quality (#13773) | **Missing** | Easy (progressive); Moderate (the size, an encode per change) | P3 |
| PNG options: depth, palette, metadata | **Partial**: chosen by the picture's mode (palette for Indexed and Bitmap, grey for Grayscale), File Info always kept; there are no choices | Easy | P3 |
| WebP options: lossless, quality | **Partial** (1.1): lossless only | Hard (lossy) | P3 |
| TIFF options: compression none/LZW/ZIP, layers | **Partial**: the writer has None, PackBits, LZW and Deflate, but Export always uses LZW; no layers | Easy | P3 |
| DDS options: BC1/BC2/BC3, mipmaps | **Have** (1.1): BC3, BC1 or uncompressed, each with mipmaps (BC2 missing) | Done | — |
| DPX bits (8/10/12/16) | **Partial** (1.1): 8 or 10 | Easy | P3 |
| PDF: lossless or JPEG (PhotoCraft's export settings) | **Have** (1.1): asked on export | Done | — |
| IFF options: PhotoCraft has none (24-plane only) | **Have**: 24-bit, 1-8 planes, EHB, HAM6/8, SHAM, mask or transparent colour, cycling, ModeID, compression (`IlbmOpts`). Easel is ahead; see section 21 | Done | — |
| Export As: scale, resample method, canvas size, transparency or matte | **Partial** (0.6): scaled by a percentage (bicubic), the format by the name's extension (every writer, 1.1); the resample method, canvas size and matte are missing | Easy | P2 |
| Export metadata choice: none, copyright, all (PREFS `metadata`; the CLI's `--metadata`, #9975) | **Missing**: File Info is always written where the format keeps it | Easy | P3 |
| Convert to sRGB on export | **Missing** | Moderate, once there's a CMS | P3 |
| Warnings about what a format loses (`fidelity_warnings`: depth reduced, CMYK converted, layers flattened) | **Have**: saving layers to a flat format offers ORA or flattening | Done | — |
| Quick Export format and location preferences | **Partial** (1.1): Quick Export as PNG writes beside the picture's file (or in the last drawer); there's no choice of format or place | Easy | P3 |
| Export dialogs start in the last exported folder (#10448) | **Partial**: they start in the picture's own drawer, or the last drawer used | Easy | P3 |
| Place options: resize while placing, always create smart objects; Skip Transform when Placing (#9075) | **Partial** (0.6): Place goes on into Free Transform to size it; there are no smart objects and no preference to skip the transform | Easy | P3 |
| Paste or open from the system clipboard as a document (PREFS `export_clipboard`) | **Have** (0.6): Edit › Paste from Amiga Clipboard (ILBM through iffparse) | Done | — |
| Drag and drop files onto the window | **Have** (0.6): AppWindow and AppIcon (`E:amiga/appdrop.c`) | Done | — |
| Opening a JPEG at a reduced size when memory is short (Easel only) | **Have** (Easel extra) | Done | — |
| Frames of an animation: how many, and the memory they may take (Easel only) | **Have** (1.1): Import Frames to Layers takes up to 100 frames within three eighths of the free memory and says when it stopped | Done | — |

## 15. Panels

PhotoCraft's panels are the Window menu items in CAT (`window.panel.*`), with their UI in
`crates/ui-egui/src/panels/`. Easel has the Color, History and Layers panels, the toolbox, the
options bar, the document tabs and the status bar (`E:amiga/panels.c`, `E:amiga/toolbox.c`).

| PhotoCraft panel (CAT id) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Tools `window.panel.tools` | **Have**: flyouts, two columns on short screens | Done | — |
| Options | **Have** | Done | — |
| Color (F6): RGB/HSB/CMYK/Lab/Gray sliders, ramp, gamut alarm | **Partial**: saturation and brightness square, hue strip, RGB bars, hex and swatches; the HSB/Lab/CMYK sliders and a choice of ramp are missing | Easy | P3 |
| Swatches: add, delete, groups, import and export | **Partial**: fixed swatches in the Color panel | Easy | P2 |
| Layers (F7): fill, mask thumbnails, effect rows, group tree, filter by kind (`layer_filter_ui.rs`), colour labels, multi-select, clipping indicators, smart filter rows | **Partial**: mode, opacity, locks, eye, thumbnail, name, drag to reorder, buttons; everything else listed is missing | Easy to Moderate, growing with section 3 | P1 |
| History | **Have** (snapshots are rated in section 12) | Done | — |
| Channels: composite and colour channels, alpha and spot channels | **Missing** | Easy (view R/G/B and alpha channels) | P2 |
| Paths | **Missing** | Easy, once paths exist | P2 |
| Actions (⌥F9) | **Missing** | Moderate | P2 |
| Adjustments: one-click adjustment layers and presets | **Missing** | Easy, once adjustment layers exist | P2 |
| Properties: adjustment settings, mask density and feather, live shape, type | **Missing** | Easy to Moderate | P2 |
| Brush Settings (F5): the dynamics sections | **Missing** | Easy | P2 |
| Brushes (presets) | **Missing** | Easy | P1 |
| Character | **Missing** | Easy, once type layers exist | P1 |
| Paragraph | **Missing** | Easy | P2 |
| Character Styles | **Missing** | Easy | P3 |
| Paragraph Styles | **Missing** | Easy | P3 |
| Glyphs | **Missing** | Easy | P3 |
| Clone Source: five sources, offset, scale, rotation, overlay | **Missing** | Easy | P3 |
| Gradients | **Missing** | Easy | P2 |
| Patterns | **Missing** | Easy | P3 |
| Histogram: live, with mean, standard deviation and median | **Missing**: a histogram exists only inside Levels (`histogram` in `E:core/adjust.h`) | Easy | P2 |
| Info (F8): readouts, colour samplers, selection size, document info | **Partial**: the status bar shows the position, the colour there and the size of a selection being drawn | Easy | P2 |
| Navigator: a thumbnail with the view box | **Missing** | Easy: very useful on 640 × 480 and 640 × 256 screens | P2 |
| Layer Comps | **Missing** | Easy | P3 |
| Measurement Log | **Missing** | Easy | P3 |
| Modifier Keys (on-screen Shift, Alt and Ctrl for pens) | **Missing** | Easy | P3 |
| Notes | **Missing** | Easy | P3 |
| Shapes | **Missing** | Easy, once shapes exist | P3 |
| Styles | **Missing** | Easy, once effects exist | P3 |
| Timeline (video and frame animation) | **Missing** | Moderate. Amiga stand-in: an IFF ANIM frame strip (section 21) | P2 |
| Tool Presets | **Missing** | Easy | P3 |
| Docking, floating, collapsing to icons, tabbed groups (`panel_docking.rs`) | **Partial**: a fixed right-hand column; Color and History share a tabbed panel on short screens | Moderate: MUI can't dock windows, but panels can be shown, hidden and grouped | P3 |
| Status bar: zoom, size, profile, timing | **Have** | Done | — |
| Document tabs | **Have** | Done | — |

## 16. Shortcuts

PhotoCraft's single keys are `Tool::key()` in TOOL; its menu shortcuts are in CAT; the special keys
are in `crates/ui-egui/src/input/` (`hold_keys.rs`, `spring_tools.rs`, `blend_keys.rs`,
`page_keys.rs`, `layout_keys.rs`). Easel's are in the README tables and `E:amiga/canvas.c`.

| PhotoCraft shortcut (source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Tool letters V M L W C I B E G T H Z O S U (TOOL) | **Have**. Easel adds R for Blur/Sharpen/Smudge, which PhotoCraft gives to Rotate View | Done | — |
| Tool letters K (Frame), J (healing), Y (History Brush), P (Pen), A (Path Selection), R (Rotate View) | **Missing**: the tools don't exist yet | Easy, with the tools | P2 |
| ⇧+letter cycles a tool group | **Have** | Done | — |
| Spring-loaded tool keys: hold a letter for a temporary tool (`spring_tools.rs`) | **Missing** | Easy | P3 |
| Space for the Hand while held | **Have** | Done | — |
| ⌘Space and ⌥Space to zoom while held (`hold_keys.rs`) | **Missing** | Easy (Ctrl-Space, Alt-Space) | P2 |
| ⌘ for the Move tool while held | **Missing** | Easy | P3 |
| ⌥ for the eyedropper with painting tools | **Have** | Done | — |
| `~` to erase with the Brush | **Missing** | Easy | P3 |
| `[` `]` for size, ⇧`[` ⇧`]` for hardness, and keyboard-layout independence (`layout_keys.rs`) | **Have**: MUI keys follow the Amiga keymap | Done | — |
| 1-0 for opacity (and quick two-digit typing) | **Have**: single digits | Done | — |
| Blend mode keys: ⇧⌥ plus a letter, ⇧+ and ⇧- to step (`blend_keys.rs`) | **Missing** | Easy | P2 |
| Page Up/Down, Home and End scroll the canvas (`page_keys.rs`) | **Missing** | Easy | P3 |
| D (default colours) and X (swap) | **Have** | Done | — |
| Q (Quick Mask) | **Missing** | Easy, with Quick Mask | P1 |
| F (screen modes) | **Missing** | Easy | P3 |
| Tab hides the panels | **Missing** | Easy: valuable on small screens | P2 |
| Menu shortcuts that Easel shares with CAT (⌘Z, ⇧⌘Z/⌘Y, X/C/V, ⇧⌘C, ⇧⌘V, A, D, ⇧⌘I, L, M, U, B, I, ⇧⌘U, ⇧⌘L, J, ⇧⌘J, E, ⇧⌘E, ⇧⌘N, T, ⌥⌘I, ⌥⌘C, ⌘[ ⌘], N, O, S, ⇧⌘S, W, 0, 1), with Ctrl in place of ⌘ | **Have** (README table) | Done | — |
| Menu shortcuts Easel lacks: ⇧⌘D, ⌥⌘Z, ⇧⌘F, ⌥⌘G, ⇧⌘B (Auto Color), ⌥⇧⌘L, ⌥⇧⌘B, ⌘R, ⌘', ⌘;, ⇧⌘;, ⌥⌘;, ⌘H, ⌘Y, ⇧⌘Y, ⌘K, ⌥⌘F, ⇧F5, ⇧F6, F5-F8, ⌥⌘A, ⇧⌘T, ⌥⌘S, F12, ⌥⌘W, ⇧⌘[ ⇧⌘], ⌘= / ⌘- (Easel uses + and -) | **Partial**: about 30 are missing, mostly for missing features | Easy | P1 |
| Amiga-key menu shortcuts (Right Amiga) | **Have** (Easel extra, in the NewMenu table) | Done | — |
| Arrow keys nudge by 1, ⇧ by 10 | **Have** | Done | — |
| Return and Esc commit and cancel | **Have** | Done | — |
| ⌥/⌘ Backspace fill with foreground or background, Del clears | **Have** | Done | — |

## 17. Preferences (Edit › Preferences, 18 items; PREFS)

Easel keeps the dark interface, the public screen and the last drawer in `PROGDIR:Easel.prefs`
(`E:amiga/main.c` `load_prefs` and `save_prefs`). It also takes `PUBSCREEN=` and `DEBUG=` as
arguments or tool types.

| PhotoCraft preference page (CAT id; key fields) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Settings/General: colour picker, image interpolation, beep when done, export clipboard, resize during place, zoom with the scroll wheel, animated zoom | **Missing** | Easy | P2 |
| AI Integrations: agent control and the control port (`Integrations`) | **Excluded** by request: this is the external-control role ARexx would fill | — | — |
| Interface: theme dark or light (and Pro, Studio, Classic), canvas colour, UI scale, language, font size, tooltips, animations | **Partial**: Dark Interface on or off (View menu), help bubbles always on, five languages (English, German, French, Spanish, Italian) from Locale (`E:amiga/lang.c`); canvas colour, scale and font size are missing | Easy | P2 |
| Workspace: auto-collapse panels, open as tabs, narrow options bar | **Missing** | Easy | P3 |
| Tools: Shift for tool switching, zoom to the clicked point, flick panning, snap to the pixel grid, show transform values, overscroll, right-click with painting tools, tablet pressure and curve, nudge bundling | **Missing** | Easy | P2 |
| History Log | **Missing** | Easy | P3 |
| File Handling: previews, lowercase extensions, autosave interval, recovery, ignore the EXIF profile, layered TIFF and PSD compatibility prompts, number of recent files, LUT folder | **Partial**: remembers the last drawer | Easy | P2 |
| Export: Quick Export format and location, JPEG and WebP defaults, metadata, convert to sRGB | **Missing** | Easy | P3 |
| Performance: memory use, history states, cache levels and tile size, GPU options | **Partial**: undo is fixed at 40 MB; there's no memory setting. GPU: Not sensible | Easy | P1 |
| Scratch Disks | **Missing** | Moderate: swap undo steps and hidden layers to a chosen drawer (T:, Work:). Valuable on 16 MB machines | P2 |
| Cursors: painting cursor standard/precise/tip/full size, crosshair, other cursors | **Partial**: an outline of the brush only | Easy | P3 |
| Transparency & Gamut: checker size and colours, gamut warning colour | **Partial**: a fixed checkerboard | Easy | P3 |
| Units & Rulers: units, column size, new-document resolution, point size | **Missing** | Easy | P2 |
| Guides, Grid & Slices: colours and styles, gridline every N, subdivisions | **Missing** | Easy | P2 |
| Plug-ins: additional plug-ins folder | **Missing** | Easy, once plug-ins exist | P3 |
| Type: smart quotes, font preview, missing-glyph protection | **Missing** | Easy | P3 |
| Enhanced Controls | **Missing** | Easy | P3 |
| Raw Defaults | **Missing** | Not sensible without raw support | P3 |

## 18. Automation and actions

PhotoCraft's automation is `crates/automation`, `engine/src/cmd/actions_*`, `atn_cmds.rs`,
`apps/photocraft-cli` and `book/src/automation/`. Easel has EaselConvert, a Shell command for
batch conversion (0.6, `E:amiga/convert.c` with `E:core/batch.c`), and no actions. Re-checked
2026-11-18 (Easel 1.1).

| PhotoCraft feature (source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Actions: record, play, stop, insert, conditional steps, dialog toggles, sets, colours (`actions.*`) | **Missing** | Moderate: record each `ID_` command with its dialog values (needs a command and parameter layer, below) | P2 |
| One registry of 500+ commands with JSON parameters, used by the UI, CLI and agents | **Missing**: menu IDs go straight to code (`E:amiga/ids.h`) | Moderate: a table of commands with parameter blocks; actions and batch depend on it | P2 |
| Command-line front end: `photocraft-cli convert/info/run/batch/droplet/commands` | **Partial**: EaselConvert converts and resizes any picture Easel reads (datatypes as a last resort) to every format Easel writes (1.1: also TIFF, Targa, PSD, WebP, PDF, Netpbm, QOI, ICO, DDS, DPX and FITS), reduces colours and writes icons, in the ADPro tradition; `info`, `run` and actions are missing | Easy (info) | P3 |
| JSON-lines control channel (`photocraft --control`, `control-protocol.md`) | **Excluded** by request: external control is ARexx's role | — | — |
| MCP server for AI agents (`photocraft-cli mcp`) | **Excluded** by request (external control); also Not sensible (AI service) | — | — |
| Running scripts (Scripts › Browse) | **Excluded** by request (ARexx) | — | — |

## 19. Plug-ins

PhotoCraft's plug-ins are WebAssembly modules run in the wasmi interpreter, with ABI v1
(`crates/plugins`, `docs/plugins.md`). Easel has no plug-ins of its own; the system's datatypes
open what its own readers don't (0.6). Re-checked 2026-11-18 (Easel 1.1).

| PhotoCraft feature (source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| WebAssembly filter plug-ins (install, list, run, remove, reload) | **Left out**: a WASM interpreter is 50-100 times slower than native code on a 68040. Stand-in (missing): native plug-ins as Amiga shared libraries (`PROGDIR:Plugins/*.library`), like ImageFX modules and ADPro loaders and savers | Not sensible | — |
| Filter › Plug-ins submenu and Install Plug-in… | **Missing** | Easy, with native plug-ins | P3 |
| A generated parameter dialog with live preview | **Missing** | Easy: reuse `slider_dialog` (`E:amiga/dialogs.c`) | P3 |
| Hosting .8BF, UXP or CEP (PhotoCraft has none either) | **Left out**: Windows and macOS code | Not sensible | — |
| Format plug-ins | **Have** (0.6) through the Amiga's own: datatypes open any picture a datatype reads (`E:amiga/dtload.c`, EaselConvert too) | Done | — |

## 20. Anything else

| PhotoCraft feature (source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Copy-on-write sparse tiles (256²): huge canvases and cheap undo (`crates/raster`, `paint/src/tile.rs`) | **Partial**: undo saves only changed boxes, but layers are full buffers in one piece (a 2000 × 1500 layer needs 12 MB of contiguous memory) | Moderate: tiles would also end the need for one big block of memory, which matters on fragmented Amiga RAM | P2 |
| GPU compositor on wgpu (Metal, Vulkan, DX12, WebGPU) | **Partial**: compositing by rectangle on the CPU is the equivalent | Not sensible on a GPU. Stand-in: Easel's composite of the visible rows, written with `WritePixelArray` on RTG screens | P3 |
| Multithreaded filters | **Missing** | Not sensible: one CPU. Stand-in: run long jobs as a subtask, with progress and an Abort button | P2 |
| Background jobs with progress and cancel (`ui-egui/src/jobs_ui.rs`) | **Missing**: a busy pointer only (AVIF takes about 50 s) | Moderate: a MUI progress gauge, with the input checked between rows | P2 |
| Recent files and the Home screen (`PREFS` `recent_files`, `show_home_recent_files`) | **Missing** (the last drawer only) | Easy: a File › Open Recent submenu | P2 |
| Context menus (`docs/context-menu-inventory.md`) | **Missing** | Easy: `MUIA_ContextMenu` on the canvas and the layer rows | P2 |
| Localization: en, zh-Hans, es, it, ko, ru… (`docs/localization*.md`) | **Partial**: English, German, French, Spanish, Italian (`E:amiga/lang_table.h`) | Easy for other Latin-1 languages; Hard for CJK and RTL | P3 |
| Help bubbles on every control | **Have** | Done | — |
| AI and generative features: Generative Fill and Expand, Neural Filters, Sky Replacement, Super Zoom (PhotoCraft lacks them too, `docs/gaps.md` G15) | **Missing** | Not sensible (an ML runtime or online service). Stand-in: the classical methods (PatchMatch, GrabCut) | P3 |
| Software update checks (`updates_cmds.rs`) | **Missing** | Not sensible: a network service | P3 |
| Web/WebAssembly build | **Missing** | Not sensible | P3 |
| Colour picker sampling outside the canvas (anywhere on the screen) | **Missing** | Easy: `ReadPixel` or `ReadPixelArray` on the screen | P3 |

Easel already has a few things PhotoCraft lacks: Filter › Render › Vignette, AVIF reading, the
Amiga and Atari formats it reads, 8-plane ILBM writing, and opening a JPEG at a reduced size when
memory is short.


## 21. Legacy extras: Amiga-era features PhotoCraft lacks

These are what users of Deluxe Paint (DPaint), Personal Paint (PPaint), ImageFX, ADPro, TVPaint and
Brilliance would look for. They aren't PhotoCraft features, so they're left out of the coverage
figures in section 23. Easel's status was re-checked on 2026-11-18 (Easel 1.1) against its code:

- `E:core/ilbm.c` reads and writes 1-8 planes, EHB, HAM6/HAM8, SHAM and 24-bit, with CAMG, the
  aspect, a mask plane or transparent colour and CRNG; `ilbm_read_indexed` keeps the CMAP, CRNG,
  CCRT and DRNG.
- `E:core/amigafmt.c` reads ACBM, RGBN, DEEP, SHAM/CTBL/PCHG, ANIM's first frame and icons;
  `E:core/frames.c` (1.1) reads every frame of an ANIM (op 0 and 5) and writes ANIM op 5.
- `E:amiga/clipboard.c` (iffparse, `clipboard.device`), `E:amiga/dtload.c` (datatypes),
  `E:amiga/appdrop.c` (AppWindow and AppIcon), `E:core/amifont.c` (bitmap fonts).

### 21.1 Palette and indexed work

| Feature | Expected from | Easel today | Amiga feasibility | Pri |
|---|---|---|---|---|
| Indexed documents (2-256 colours) as a working mode, painting with pens rather than RGB | DPaint, PPaint, Brilliance | **Have** (0.4): Image › Mode › Indexed Color; everything drawn is brought back to the palette (`doc_index_sync` in `E:core/palette.h`) | Done | — |
| Keep an opened ILBM's palette (CMAP), and save it back with the same palette | all | **Have** (0.4): `ilbm_read_indexed`, `ilbm_write_doc` | Done | — |
| Palette editor: RGB/HSV sliders, Spread (a gradient between two entries), Copy, Swap, Exchange, Undo, ranges | DPaint, PPaint | **Have** (0.4): Image › Color Table / Window › Palette (`E:amiga/palwin.c`) | Done | — |
| Palette locking: lock entries so a remap or quantize keeps them; reserve the Workbench or system pens | PPaint, ADPro (locked pens), ImageFX | **Have** (0.4): `pal_lock` in `E:core/doc.h`, forced colours (Workbench 4, primaries...) in `E:amiga/modedlg.c` | Done | — |
| Remap to a palette: the nearest colour or a dither; load the palette from an ILBM, `.pal`, `.act` or a prefs file | ADPro, ImageFX, PPaint | **Have** (0.4): Indexed Color with the current palette or a preset; palettes load from .act, .aco, .ase, .gpl, .pal, IFF and PNG (`palfile_read`) | Done | — |
| Colour reduction choices: median cut, Floyd-Steinberg, Burkes/Stucki/Sierra/Jarvis, ordered Bayer 2/4/8, random, none | ADPro, ImageFX | **Have** (0.4): median cut, octree, Wu; 12 dithers (`dither_names` in `E:core/palette.c`) | Done | — |
| OCS/ECS 12-bit palettes (4 bits per gun, 4096 colours) and AGA 24-bit palettes when quantizing | all | **Have** (0.4): `QM_OCS12` | Done | — |
| Halfbrite (EHB) editing: edit the 32 base colours and see their 32 half-bright twins | DPaint IV, PPaint | **Have** (0.4): Make Extra Half-Brite palette in the Color Table; EHB writing | Done | — |
| Stencils: protect chosen colours, or an area, from painting | DPaint ("Stencil", "Fix Background"), PPaint | **Missing** | Easy: a frozen mask, like the selection | P2 |
| Shade, Cycle, Tint and Blend draw modes over a palette range | DPaint | **Missing**. Partial equivalents: Smudge for Smear, Blur for Smooth | Easy, with indexed mode | P2 |
| Range fills: a gradient through a palette range, with dither styles | DPaint | **Missing** (only RGB gradients) | Easy | P2 |

### 21.2 Colour cycling

| Feature | Expected from | Easel today | Amiga feasibility | Pri |
|---|---|---|---|---|
| Read the CRNG (DPaint), CCRT (Graphicraft) and DRNG (DPaint IV enhanced) ranges | DPaint, PPaint, Brilliance | **Have** (0.4): `ilbm_read_indexed` | Done | — |
| Play cycling (Tab in DPaint), with speed and direction | DPaint | **Have** (0.4): cycling plays on the canvas (`E:amiga/palwin.c`) | Done | — |
| Edit and save cycle ranges | DPaint, PPaint | **Have** (0.4): edited in the Color Table, written as CRNG | Done | — |

### 21.3 Brushes (DPaint style)

| Feature | Expected from | Easel today | Amiga feasibility | Pri |
|---|---|---|---|---|
| Pick up a custom brush: a rectangle, lasso or whole layer, with the background colour as transparent | DPaint, PPaint, Brilliance | **Missing** | Easy | P1 |
| Paint with the custom brush in Matte, Color or Replace mode, and stamp it | DPaint | **Missing** | Easy | P1 |
| Save and load brushes as ILBM, with the GRAB hotspot | DPaint | **Missing** | Easy | P1 |
| Brush handle, and brush transforms: flip, rotate 90° or any angle, double or halve, stretch, shear, bend, outline | DPaint | **Missing** | Easy (flip, rotate, scale, outline); Moderate (bend) | P2 |
| Anim brushes: a multi-frame brush that steps as it paints, saved as an ANIM | DPaint, Brilliance | **Missing** | Moderate | P3 |
| Perspective: place a brush on a 3D plane | DPaint | **Missing** | Moderate | P3 |
| Spare page (J) and copy to spare | DPaint | **Partial**: tabs and Next Document; no one-key swap and no copy to spare | Easy | P3 |
| Magnify: a split view, zoomed beside 1:1 | DPaint, PPaint | **Missing** | Easy: two canvas objects, or a Navigator | P2 |
| Grid snap for painting | DPaint | **Missing** (as View › Snap) | Easy | P1 |
| Symmetry: N-fold point, mirror, tile | DPaint | **Missing** (as painting symmetry) | Easy | P2 |

### 21.4 Writing Amiga picture modes

| Feature | Expected from | Easel today | Amiga feasibility | Pri |
|---|---|---|---|---|
| HAM6 writing, with a good encoder (optimised base palette, look-ahead) | ADPro, ImageFX, PPaint | **Have** (0.4): File › Export › IFF ILBM (Amiga options) | Done | — |
| HAM8 writing | ADPro, ImageFX | **Have** (0.4) | Done | — |
| EHB writing (32 colours plus half-bright, 64 in all) | DPaint, ADPro | **Have** (0.4) | Done | — |
| SHAM, PCHG and CTBL writing (a palette per line, "dynamic" HAM and hires) | ADPro, ImageFX, Photon Paint | **Partial** (0.4): SHAM is written (`IK_SHAM`); PCHG and CTBL aren't | Moderate | P3 |
| A choice of bitplane depth (1-8) and the ByteRun1 or uncompressed setting | all | **Have** (0.4): `IlbmOpts` planes and compress | Done | — |
| Masking in the ILBM header: `mskHasMask` and `mskHasTransparentColor`, and a transparent colour for genlock | DPaint, TVPaint, ImageFX | **Have** (0.4): `IMK_PLANE`, `IMK_TRANSPARENT` | Done | — |
| Export as raw interleaved bitplanes plus a palette, or as C or assembler source, or as hardware sprites | coders' converters, PPaint | **Missing** | Easy | P3 |

### 21.5 Screen-mode-aware output

| Feature | Expected from | Easel today | Amiga feasibility | Pri |
|---|---|---|---|---|
| Pick the target ModeID from the ASL ScreenMode requester, and write CAMG and the page size to match | all | **Have** (0.4): Image › Amiga Screen Mode (`E:amiga/modedlg.c`, `E:core/amigamodes.c`), CAMG and page size written | Done | — |
| Enforce the chipset's limits: OCS/ECS 32 colours in lores, 16 in hires, 64 EHB, 4096 HAM; AGA 256 or HAM8 | all | **Have** (0.4): the mode dialog warns when the picture doesn't fit the mode and chipset | Done | — |
| Pixel aspect for each mode (PAL and NTSC lores, hires, interlace, super-hires): show it on the canvas and correct it on import and export | DPaint, ADPro | **Partial**: each mode's aspect is known, kept in the document (`aspect_x`, `aspect_y`) and written to ILBM's BMHD; the canvas doesn't draw it (section 2.5) | Easy: a separate vertical zoom in `E:amiga/canvas.c` | P1 |
| Overscan presets (352 × 283, 704 × 566, 368 × 241…) and safe-area guides | DPaint, Brilliance, TVPaint | **Partial**: New and the Crop presets have the PAL and NTSC sizes; overscan sizes and safe-area guides are missing | Easy | P2 |
| A "Save for Screen" preview that dithers and quantizes to the chosen mode before saving | ADPro, ImageFX | **Partial**: Image › Mode › Indexed Color previews the palette and dither on the canvas, and undo takes it back; there's no side-by-side preview at export | Moderate | P2 |
| A display dither choice on palette screens: none, ordered or Floyd-Steinberg; a HAM8 preview on AGA | ImageFX, TVPaint | **Partial**: AGA and ECS screens get a fixed colour cube with ordered dither (`draw_rgb` in `E:amiga/widgets.c`); an indexed picture shows its own palette | Moderate | P2 |
| Copper-list gradients: design a per-line background colour gradient, preview it with a user copper list, save it as PCHG or SHAM | Brilliance, DPaint IV, ImageFX | **Missing** | Moderate | P3 |

### 21.6 Workbench icons

| Feature | Expected from | Easel today | Amiga feasibility | Pri |
|---|---|---|---|---|
| Save as a classic `.info` icon (planar, 4 or 8 colours, in the WB 1.x, 2.x or MagicWB palette) | IconEdit, PPaint | **Have** (0.6): `E:core/iconwrite.c`, `ICONPAL_WB20`/`ICONPAL_MWB` | Done | — |
| Save as NewIcons (the image in tool types) | NewIcons tools | **Have** (0.6) | Done | — |
| Save as GlowIcons (an IFF FORM ICON with ARGB or palette images, OS 3.5) | IconEdit 3.5 | **Have** (0.6): the default when saving a picture's icon | Done | — |
| Save as a PNG icon (OS4, AROS, MorphOS) | — | **Have** (0.6) | Done | — |
| Two images for selected and unselected, and the icon's tool type and default tool | IconEdit | **Partial**: a selected image (the picture darkened) and the default tool (`IconOpts`); tool types can't be set | Easy | P3 |

### 21.7 Animation

| Feature | Expected from | Easel today | Amiga feasibility | Pri |
|---|---|---|---|---|
| Open every frame of an IFF ANIM (ANIM5/7/8) as frames or layers | DPaint, Brilliance, PPaint | **Have** (1.1): File › Import Frames to Layers reads ANIM op 0 and op 5 with both buffers, palette changes and the frame times; DPaint's two looping frames are left out (`anim_read_frames` in `E:core/frames.c`). Ops 7 and 8 aren't read (the frames up to one are kept). Animated GIFs too, with their disposal | Done | — |
| Edit the frames: a frame strip, add or delete or move frames, onion skin, play, timing (ANHD and DPAN) | DPaint, Brilliance, TVPaint | **Partial**: frames are layers, so the Layers panel adds, deletes and moves them; each frame's time is in its layer's name ("Frame 3 (0.25 s)"); there's no frame strip, onion skin or playback | Moderate | P2 |
| Write an IFF ANIM (ANIM5 delta, and ANIM7/8) | DPaint, Brilliance | **Have** (1.1): File › Export › Layers as IFF ANIM writes op 5 for two buffers with the loop frames (`anim_write`); ANIM7/8 aren't written | Done | — |
| Animated GIF writing | PPaint 7 | **Have** (1.1): File › Export › Layers as Animated GIF: one palette, loops, each frame only where it changed (`gif_write_anim`; Pillow checks it, `E:tests/check_frames.py`) | Done | — |

### 21.8 Exchange with the rest of the Amiga

| Feature | Expected from | Easel today | Amiga feasibility | Pri |
|---|---|---|---|---|
| The clipboard through iffparse (`clipboard.device` unit 0): copy out as ILBM, paste ILBM in, FTXT text into the Text tool | all | **Have** (0.6): `E:amiga/clipboard.c` (ILBM both ways, FTXT) | Done | — |
| Datatypes import: open anything `picture.datatype` (V43) has a datatype for | MultiView, PPaint 7, ImageFX 4 | **Have** (0.6): `E:amiga/dtload.c`, the last resort when no reader of Easel's knows the file | Done | — |
| Datatypes export: `DTM_WRITE` where the datatype supports it | PPaint 7 | **Missing** | Moderate: few datatypes can write | P3 |
| Amiga bitmap fonts (`diskfont.library` `.font`) and ColorFonts in the Text tool | DPaint, PPaint | **Partial** (0.9): bitmap fonts from FONTS: in type layers (`E:core/amifont.c`); ColorFonts' own colours aren't used (they draw in the text colour) | Easy | P3 |
| Grab a public screen as a new document | ScreenGrab tools, PPaint | **Missing** | Easy | P3 |
| AppIcon and AppWindow drops (open files dropped on the window or the icon) | all | **Have** (0.6): `E:amiga/appdrop.c` | Done | — |
| A Shell CLI for batch conversion (not ARexx) | ADPro, ImageFX | **Have** (0.6): EaselConvert (`E:amiga/convert.c`, `E:core/batch.c`); 1.1: it writes every format Easel writes | Done | — |
| ARexx port and scripts | ADPro, ImageFX, DPaint V, PPaint, TVPaint | **Excluded** by request | — | — |
| Printing (printer.device, TurboPrint) | DPaint, PPaint | **Excluded** by request | — | — |

## 22. Recommended roadmap

The plan groups the P1 and P2 rows marked Missing or Partial, and the legacy extras, into
releases. Each release has one theme and is roughly the size of 0.2's work, which added about 20
decoders, among them dav1d's port, `E:core/rasters.c`, `E:core/amigafmt.c`, `E:core/webp.c` and
`E:core/tiff.c`. P3 rows are left for later, or never. Every release keeps the rules: no printing,
no ARexx, and new icons drawn in Easel's own style.

**Quick wins** (any 0.2.x point release; each is a few hours):

- Wire up `ID_RESELECT` and `ID_GROWSIM`: Reselect, Grow, Similar.
- Show the Dodge/Burn Range and Sponge Mode (`Opts.range` exists).
- Show brush Spacing.
- Filters: Last Filter, Difference Clouds, Polar Coordinates, Dust & Scratches, Custom 5 × 5.
- The eight missing Photoshop blend modes.
- The missing Ctrl shortcuts for existing commands.
- Make the undo budget a preference.

### 0.3: Live layers (sections 3 and 4)

- **Groups:** Group, Group from Layers, Ungroup, nesting, Pass Through; and multi-layer selection
  in the Layers panel.
- **Layer masks:** Reveal or Hide All, Reveal or Hide Selection, From Transparency, Apply, Delete,
  Enable, Link, painting and viewing the mask, density and feather. Also Paste Into.
- **Clipping masks**, Fill Opacity and Blend If.
- **Adjustment layers** for the 15 adjustments Easel already has, plus Channel Mixer and Gradient
  Map, applied in `doc_composite`.
- **Panels:** a Properties panel and an Adjustments panel.
- **Fill layers:** Solid Color and Gradient, with Layer Content Options.
- **Layer menu:** the Rasterize submenu, Lock Position and Lock All, and Align.
- **Files:** OpenRaster groups and masks, read and written.
- **Memory:** an undo and memory preference, since live layers cost RAM.

### 0.4: Palettes, pixels and the Amiga screen (section 21.1, 21.2, 21.4 and 21.5; sections 2.5 and 11)

- **Indexed documents:** indexed mode, keeping an ILBM's CMAP, the palette editor, palette locking
  and remapping, and Image › Mode › Color Table (`.act`, CMAP).
- **Quantizing:** the dither choices, and OCS 12-bit and AGA palettes.
- **Other modes:** Bitmap and Grayscale.
- **Picture modes:** writing EHB, HAM6 and HAM8, a choice of bitplane depth, and the ILBM mask and
  transparent colour.
- **Screen modes:** a ModeID requester, CAMG, and chipset limits.
- **Pixel aspect:** pixel aspect ratio with Amiga presets, and overscan presets.
- **Colour cycling:** reading, playing, editing and saving CRNG, CCRT and DRNG.
- **Painting:** stencils.
- **Rulers and grids:** rulers, guides, grid, snap, pixel grid, and Pattern and Pixel Art Preview.
- **Files:** GIF writing, and palette and gray PNG.

### 0.5: Brushes and painting (sections 1 and 8; 21.3)

- **Brush tips:** spacing, angle and roundness, and sampled tips.
- **DPaint custom brushes:**
  - picking them up;
  - Matte, Color and Replace modes;
  - ILBM brushes with GRAB;
  - flip, rotate and scale;
  - Define Brush Preset.
- **Panels:** Brushes and Brush Settings.
- **Brush dynamics:** Shape, Scattering and Color Dynamics, airbrush build-up, Smoothing, painting
  symmetry, and tablet pressure.
- **Gradients and patterns:** the gradient editor and Gradients panel; Define Pattern, Pattern
  Stamp and pattern fills.
- **Tool upgrades:** Eraser modes, Pencil Auto Erase, and the Clone Stamp's options.
- **New tools:** Magic Eraser, Background Eraser, Color Replacement, History Brush.
- **Colours:** the Swatches panel, palette files (`.aco`, `.act`, `.ase`, CMAP), and a better
  colour picker.

### 0.6: Exchange (sections 13, 14 and 18; 21.6 and 21.8)

- **Amiga clipboard:** the iffparse clipboard (ILBM in and out, FTXT).
- **Amiga integration:** datatypes import, and AppWindow and AppIcon drops.
- **Workbench icons:** writing classic, GlowIcons, NewIcons and PNG icons.
- **Formats:**
  - writing TIFF and Targa;
  - reading and writing PSD layers;
  - applying the JPEG EXIF orientation.
- **Exporting:**
  - Save a Copy;
  - Export As with scale;
  - Layers to Files;
  - File Info metadata;
  - Place with a transform.
- **Batch work:** a Shell front end for conversion (in the spirit of Image Processor), and Load
  Files into Stack.
- **File menu:** Open Recent.

### 0.7: Selections and transforms (sections 1, 2.2 and 7)

- **Selection modes:** Quick Mask (Q), Save and Load Selection with alpha channels, and a Channels
  panel.
- **Transforms:** Transform Selection, an on-canvas Free Transform (scale, rotate, skew, distort,
  perspective), and Transform Again.
- **Selection tools:**
  - the marquee's fixed ratio and size;
  - Single Row and Single Column;
  - anti-aliasing in the wand and lasso;
  - the Magnetic Lasso;
  - a better Color Range.
- **Crop and straighten:** crop presets, straightening and rotation; and the Ruler with Straighten
  Layer.
- **Move tool:** transform controls, the align bar, and Alt-drag to duplicate.
- **Layer menu:** Defringe.

### 0.8: Workspace and safety (sections 12, 15, 16, 17 and 20)

- **Panels:** Navigator, Histogram, Info with Color Samplers.
- **Screen:** Tab to hide the panels, and Easel's own screen or full screen.
- **Menus and keys:**
  - context menus;
  - a keyboard shortcut editor;
  - the blend-mode, zoom and spring keys.
- **Preferences:** the General, Interface, Tools, File Handling, Units and Guides/Grid pages.
- **Safety:**
  - autosave and crash recovery;
  - a scratch drawer for undo and snapshots;
  - History snapshots;
  - Toggle Last State and Fade;
  - background jobs with progress and Abort.

### 0.9: Type and vectors (sections 9 and 10)

- **Type layers:** editable type layers with Character and Paragraph panels.
- **Type settings:** leading, tracking, kerning, underline, colour per run, styles and weights,
  paragraph boxes, and Warp Text.
- **Type extras:** the Type Mask, Amiga bitmap fonts, and Rasterize Type.
- **Paths:**
  - the Pen and its anchor tools;
  - Path and Direct Selection;
  - the Paths panel;
  - Fill and Stroke Path;
  - converting between paths and selections.
- **Shapes:**
  - shape layers with strokes;
  - Triangle and Polygon;
  - line arrowheads;
  - outlined Rectangle and Ellipse.

### 1.0: Filters, adjustments and retouching (sections 1, 5 and 6)

- **Adjustments:**
  - the six Hue/Saturation ranges;
  - Black & White with six sliders and a tint;
  - Selective Color and Replace Color;
  - Shadows/Highlights;
  - Levels and Curves eyedroppers and presets.
- **Filters:**
  - Displace, Shear and ZigZag;
  - Color Halftone, Crystallize and Mezzotint;
  - Diffuse, Trace Contour and Wind;
  - Smart and Surface Blur, Average and Blur More;
  - Smart Sharpen and Sharpen Edges;
  - Lens Flare and Color to Alpha;
  - De-Interlace and NTSC/PAL Colors.
- **Retouching:** the Healing Brush, Spot Healing (Proximity Match), Red Eye, and Pattern Stamp
  (if it isn't done in 0.5).

### 1.1: Animation (section 21.7; section 15 Timeline)

- **IFF ANIM:** opening every frame (or a range), a frame strip with onion skin and playback,
  timing, writing ANIM5/7/8, and Video Frames to Layers.
- **Brushes:** anim brushes, if there's time.

### 1.2: Automation (section 18)

- **Commands:** a command and parameter registry under the `ID_` commands.
- **Actions:** recording and playing them, an Actions panel, and Batch.
- **Droplets:** droplet tool icons for Workbench.
- **Not included:** ARexx stays excluded. Actions are internal, and the Shell front end from 0.6 is
  the external interface.

After 1.2, what's left is mostly P3: smart objects, effects caching at scale, Filter Gallery, CMYK
and Lab, ICC, 16-bit, and the Hard content-aware and AI-substitute features.

## 23. Coverage summary

- **Counting:** each table row in sections 1-20 counts once. Rows that group several identical
  menu items count once.
- **Coverage:** (Have + ½ Partial) ÷ (Have + Partial + Missing).
- **Left out:** Excluded rows (printing, ARexx-style control and scripting, print prepress) and
  the legacy extras of section 21.

| Area | Have | Partial | Missing | Excluded | Rated rows | Coverage |
|---|---|---|---|---|---|---|
| 1. Tools | 1 | 25 | 42 | 0 | 68 | 20% |
| 2. Menus (File, Edit, Image, Layer, View, Window) | 51 | 18 | 125 | 4 | 194 | 31% |
| 3. Layers | 3 | 2 | 75 | 0 | 80 | 5% |
| 4. Blend modes | 18 | 1 | 16 | 0 | 35 | 53% |
| 5. Adjustments | 10 | 4 | 8 | 0 | 22 | 55% |
| 6. Filters | 24 | 3 | 57 | 0 | 84 | 30% |
| 7. Selection | 10 | 2 | 16 | 0 | 28 | 39% |
| 8. Painting and brushes | 4 | 3 | 22 | 0 | 29 | 19% |
| 9. Type and text | 2 | 2 | 27 | 0 | 31 | 10% |
| 10. Vector paths and shapes | 1 | 0 | 10 | 0 | 11 | 9% |
| 11. Colour management, modes, depths | 2 | 1 | 18 | 0 | 21 | 12% |
| 12. History and undo | 3 | 1 | 6 | 0 | 10 | 35% |
| 13. File formats (read, write, presets) | 20 | 6 | 38 | 0 | 64 | 36% |
| 14. Import and export options | 3 | 2 | 11 | 0 | 16 | 25% |
| 15. Panels | 5 | 5 | 25 | 0 | 35 | 21% |
| 16. Shortcuts | 12 | 1 | 10 | 0 | 23 | 54% |
| 17. Preferences | 0 | 5 | 12 | 1 | 17 | 15% |
| 18. Automation and actions | 0 | 0 | 3 | 3 | 3 | 0% |
| 19. Plug-ins | 0 | 0 | 5 | 0 | 5 | 0% |
| 20. Anything else | 1 | 3 | 8 | 0 | 12 | 21% |
| **All areas** | **170** | **84** | **534** | **8** | **788** | **26.9%** |

**Overall:**

- **Coverage:** about 27% (26.9%).
- **Fully had:** 170 of 788 rated rows (21.6%).
- **At least partly had:** 254 rows (32.2%).

**Legacy extras (section 21):** 0 Have, 6 Partial, 48 Missing, 2 Excluded. Easel reads nearly
every Amiga format, but it writes almost none of the Amiga-specific ones yet. The exceptions are
24-bit and 8-plane ILBM.

**Where Easel is strongest and weakest:**

- **Strongest:** the core adjustments (55%), shortcuts (54%) and blend modes (53%). File reading
  is ahead of PhotoCraft for AVIF and every Amiga or Atari format.
- **Weakest:**
  - layers (5%): no groups, masks, clipping, effects, adjustment or fill layers, or smart objects;
  - vectors (9%) and type (10%): no paths or shape layers, and type isn't editable;
  - colour modes (12%): RGB 8-bit only, no indexed mode, no CMS;
  - plug-ins and automation: none.

**How the 0.3-1.2 roadmap would change this.** Closing all of its P1 and P2 rows (283 of them) would bring coverage to
about 59%. Most of the rest is P3: work that is Hard or Not sensible on a 68040, such as
smart objects, 16- and 32-bit depths, content-aware and AI stand-ins, camera raw, CMYK and Lab, and
WebAssembly plug-ins.
