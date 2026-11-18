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

PhotoCraft has 68 tools (`Tool::ALL` in TOOL), and its options bar is OPT. Easel 1.1 has 49 tools (1.2: 63)
(`TL_` in `E:amiga/easel.h`), and its options bar is `fields_for` in `E:amiga/toolbox.c`. Keys are
PhotoCraft's (`Tool::key()`). Rechecked 2026-11-18 against Easel 1.1 and PhotoCraft `c337d6e`
(rows marked *upstream* are PhotoCraft changes since `eb6c1ab`, from its merged pull requests).

| PhotoCraft tool and options (source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Move (V): Auto-Select Layer/Group, transform controls, align bar (`tools/align_bar.rs`), ⌥-drag duplicates (`tools/move_mods.rs`), nudge; TOOL `Move` | **Have**: Auto-Select with Layer or Group (`auto_select_target`, `E:core/toolopts.c`), Show Transform Controls (`move_bounds`, a handle starts Free Transform, `E:amiga/canvas.c`), the Align list (`F_MENU`, `alignids` in `E:amiga/toolbox.c`), Alt-drag copies (`move_selection_copy`), nudge | Done | — |
| *upstream* Move: Distribute buttons, the gear menu (#11540); Auto-Select target dropdown Group, Layer (#12166) | **Have**: Distribute Horizontally/Vertically in the Align list; the Layer/Group choice | Done | — |
| Artboard (V); TOOL `Artboard` | **Have** (1.2): the Artboard tool drags out artboards (groups clipped to their box, with a white background; `doc_new_artboard` in `E:core/layerx.c`), a drag on one moves it; Layer › Links and Artboards › New Artboard / Artboard from Group; File › Export More › Artboards as PNG; kept in OpenRaster (`easel:artboard`) | Done | — |
| Rectangular Marquee (M): New/Add/Subtract/Intersect, Feather, Style Normal/Fixed Ratio/Fixed Size, W/H swap; OPT 246-315 | **Have** (0.7: `MQS_` styles, `marquee_box` in `E:core/selx.c`) | Done | — |
| Elliptical Marquee (M): the same plus Anti-alias | **Have** (`sel_ellipse_ex` with Anti-alias) | Done | — |
| Single Row Marquee: a 1-pixel row | **Have** (`MQS_ROW`, `sel_single_row`) | Done | — |
| Single Column Marquee | **Have** (`MQS_COL`, `sel_single_col`) | Done | — |
| Lasso (L): modes, Feather, Anti-alias; OPT 315 | **Have** (`sel_polygon_ex`) | Done | — |
| Polygonal Lasso (L): the same; Return or a double-click closes, Backspace removes a point | **Have**: 1.1 adds Backspace (`keys`, `E:amiga/canvas.c`) | Done | — |
| Magnetic Lasso (L): Width, Contrast, Frequency, pen pressure; `tools/magnetic_lasso_ui.rs`, `algo/src/magnetic.rs` | **Partial**: Width, Contrast, Frequency (`E:core/maglasso.c`); no pen pressure | Moderate (pressure, with tablet input) | P3 |
| *upstream* Magnetic Lasso: Shift-[ and Shift-] jump to the width limits (#11701); Caps Lock shows the width circle (#12151) | **Partial** (1.3): Shift-[ and Shift-] take the width to 1 and 256 px, [ and ] step it a pixel (`E:amiga/canvas.c`); Caps Lock doesn't show the width circle | Easy | P3 |
| Magic Wand (W): Tolerance, Anti-alias, Contiguous, Sample All Layers, Select Subject button; OPT 360 | **Have** (1.3): Tolerance, Anti-alias, Contiguous, All Layers and the Sample Size (Point to 101 by 101 Average, the seed the square's average: `similar_sample` in `E:core/paint.c`, `E:core/menu3.c`, `E:tests/menu3test.c`). Select Subject: **Left out** (a trained segmentation model; no Amiga stand-in) | Done | — |
| Quick Selection (W): a brush-driven graph cut; `algo/src/segment.rs` (`quick`) | **Have** (1.2): no graph cut but region growing: colour clusters from what the brush covers and from a ring round it that isn't selected, grown out from the brush to the nearer-coloured pixels and stopped at strong edges, within a few brush widths (`quick_select` in `E:core/selsmart.c`); Add/Subtract, Sample All Layers, Auto-Enhance, one undo step a drag | Done | — |
| Object Selection (W): GrabCut in a box, Object Finder; `tools/object_finder.rs`; *upstream* Hard Edge (#13039) | **Have** (1.2): a box round the object; GrabCut-like rounds of colour clusters (inside and a band outside), classing, smoothing and refitting, then what joins the box's middle with its holes filled (`object_select` in `E:core/selsmart.c`); Hard Edge, Sample All Layers, objects on transparent layers (as upstream #12869). Object Finder: **Left out** (a trained model) | Done | — |
| Crop (C): ratio and size presets (`tools/crop_presets.rs`), Straighten (`crop_straighten.rs`), overlays (`crop_overlay.rs`), Delete Cropped Pixels, Content-Aware fill, rotating the box | **Partial**: presets with W/H swap, overlays and Straighten (1.1); 1.2 turns the box with a drag outside it (Shift: 15° steps; `crop_corners`, `crop_drag_angle` in `E:core/toolsx.c`, applied by `image_crop_rotated`) and Edit › Content-Aware Fill fills what a crop leaves empty; no non-destructive crop | Hard (non-destructive: layers are canvas-sized) | P3 |
| *upstream* Crop: Straighten's label and spirit-level glyph (#11490), Reset button (#9730), Always Show Overlay (#11022), the Contextual Task Bar under the frame (#11669) | **Partial**: Straighten is labelled and Cancel resets; no task bar (it would cover the picture on a 640 × 480 screen) | Easy | P3 |
| Perspective Crop (C); `tools/perspective_crop_ui.rs` and its upstream fixes (#12221-#12237, #12421) | **Have** (1.2): drag a box, move its four corners, Return (or Crop on the options bar) remaps the picture upright through the homography (`image_perspective_crop` in `E:core/toolsx.c`, `image_remap`); W and H on the options bar, 0 for the quad's own perspective size (as #12421) | Done | — |
| Slice (C) | **Have** (1.2): the Slice tool drags out slices (up to 64, numbered on the canvas; `slice_add` in `E:core/toolsx.c`), Slices From Guides, File › Export More › Slices as PNG (`slices_export`), kept in OpenRaster | Done | — |
| Slice Select (C) | **Have** (1.2): click to pick, drag to move; Divide Slice, Delete Slice on the options bar (`slice_divide`) | Done | — |
| Frame (K): a placeholder frame with a stroke; `tools/frame_tool.rs`, `Group::frame_stroke`; *upstream* reworked (#8920) | **Have** (1.2): the Frame tool drags a rectangular or elliptical frame: a group with a vector mask of that shape, a grey placeholder with a cross and a stroke in the foreground colour (`doc_new_frame` in `E:core/layerx.c`); Layer › Links and Artboards › Place in Frame scales the active layer to fill it (`doc_frame_place`) | Done | — |
| Eyedropper (I): sample point/3/5/11/31/51/101, Current/Current & Below/All Layers; `eyedropper_ui.rs` | **Have** (1.1: `doc_pick_ex` and `pick_sizes`, `E:core/toolopts.c`; the square is composited once) | Done | — |
| *upstream* Eyedropper: right-click menu with the sample sizes, Copy Color as HTML and its hex code (#12182) | **Have** (1.3): the right-click menu has the seven sample sizes, Current Layer / Current & Below / All Layers, ticked, and Copy Color as HTML (`color: #c8641e;`) and Copy Color's Hex Code to the Amiga clipboard (`color_html` in `E:core/menu3.c`, `E:amiga/ctxmenu.c`, `E:tests/ctxtest.c`) | Done | — |
| Color Sampler (I): up to 10 fixed readouts in Info | **Have** (1.3): up to ten (`MAXSAMPLERS`, `DocView.samp_x`), shown in Info (two to a line past four), with the Eyedropper's sample size (as *upstream* #12175) | Done | — |
| Ruler (I): measures angle and length; Straighten Layer | **Have** (0.7, `E:core/crop.h` `ruler_measure`, `layer_straighten`) | Done | — |
| Note (I): annotations | **Have** (1.2): notes (up to 64, in their colour, never part of the picture; `note_add` and friends in `E:core/toolsx.c`): click adds one, double-click edits, Alt-click deletes, drag moves, Clear All asks first (as upstream #12217); View › Show › Notes; kept in OpenRaster (`easel:note`) | Done | — |
| Count (I): numbered markers | **Have** (1.2): numbered marks in up to 16 colour groups (`count_add`, `count_group_add`), Alt-click removes, drag moves, adding a mark shows the count again (upstream #11845), Record Measurements gives "group<TAB>count" lines (`count_record`); kept in OpenRaster | Done | — |
| Brush (B): Size, Hardness, Mode, Opacity, Flow, airbrush build-up, Smoothing, pressure for size and opacity, preset picker (`tools/brush_picker.rs`), symmetry (`tools/symmetry_ui.rs`); OPT 134-220 | **Have**: all of it (0.5; 1.1 the options bar's Smoothing and Airbrush); 1.2 reads the pen: an input handler before Intuition takes `IESUBCLASS_NEWTABLET` events' `TABLETA_Pressure` (or TabletZ/RangeZ) and `TABLETA_AngleX/Y` (`tablet_parse` in `E:core/brushx.c`, `E:amiga/tools2.c`) into `stroke_begin_p`/`stroke_to_p` and `stroke_tilt`, through a pressure curve and floor (`tablet_curve`); without tablet hardware here the path is tested with tag lists made as Intuition sends them (`E:tests/brushxtest.c`) | Done | — |
| *upstream* Brush: Alt+digit sets Smoothing (#12034), opacity minimum 1 % and "00" (#11823), Shift-drags constrained (#11926), the HUD's size/hardness drag (#12056), Reset Tool's defaults (#11856) | **Partial**: Alt+digit Smoothing and Shift+digit Flow (1.1, `keys`); 1.3: two digits typed quickly set the opacity exactly, "00" the minimum 1 % (`opacity_key`), and Reset Tool gives PhotoCraft's 13 px soft tip (`tool_reset_tip`, `E:core/menu3.c`, `E:tests/menu3test.c`); no HUD drag | Easy | P3 |
| Pencil (B): Size, Mode, Opacity, Smoothing, Auto Erase; OPT 173; *upstream* pressure toggles (#12477), Auto Erase only on the exact foreground (#12209) | **Have**: Smoothing and Auto Erase on the options bar (1.1), Auto Erase on the exact colour (`stroke_begin_target`); pressure as the Brush | Done | — |
| Mixer Brush (B): Wet, Load, Mix, Flow, load and clean after each stroke, Sample All Layers; `paint/src/mixer.rs` | **Have** (1.2): PhotoCraft's reservoir and pickup model (`mixer_dab` in `E:core/brushx.c`, `PT_MIXER` in `E:core/stroke.c`): Wet, Load, Mix, Flow, Load and Clean buttons, Auto Load, Auto Clean, Sample All Layers | Done | — |
| Color Replacement (B): Mode Hue/Saturation/Color/Luminosity, Sampling Continuous/Once/Background, Limits Contiguous/Discontiguous/Find Edges, Tolerance; `paint/src/replace.rs` | **Have** (0.5, `PT_REPLACE`) | Done | — |
| Eraser (E): Mode Brush/Pencil/Block, Opacity, Flow, Erase to History; `tools/eraser_ui.rs` | **Have**: the modes (0.5, `ER_`) and Erase to History (1.1, `Brush.erase_history` in `E:core/stroke.c`) on the options bar | Done | — |
| Background Eraser (E): Sampling, Limits, Tolerance, Protect Foreground; `paint/src/bg_erase.rs`; *upstream* graded Find Edges, fading Protect Foreground, decontamination (#12152-#12154) | **Partial**: the options (0.5, `PT_BGERASE`); the upstream refinements are not ported | Easy | P3 |
| Magic Eraser (E): Tolerance, Anti-alias, Contiguous, All Layers, Opacity | **Have** (0.5, `magic_erase`) | Done | — |
| Gradient (G): presets and the gradient editor (stops, midpoints, opacity stops), five styles, Mode, Opacity, Reverse, Dither, Classic/new; OPT 400, `tools/gradient_ui.rs` | **Have** (0.5, `E:core/gradient.c`, `E:amiga/gradwin.c`); editable live gradients are gradient fill layers | Done | — |
| Paint Bucket (G): Fill from Foreground or Pattern, Mode, Opacity, Tolerance, Anti-alias, Contiguous, All Layers; OPT 375 | **Have** (1.3): Foreground or Pattern (0.5), Mode (`fill_bucket_mode`), Opacity, Tolerance, Anti-alias, Contiguous, All Layers and the Sample Size (`E:core/menu3.c`, `E:tests/menu3test.c`) | Done | — |
| Horizontal Type (T): family, style, size, AA method, alignment, colour, warp, Character and Paragraph panels, commit and cancel; `tools/type_tool.rs` | **Have**: editable type layers with all of these (0.9, `E:core/type.c`; section 9 for the details) | Done | — |
| Vertical Type (T) | **Have** (0.9, `TL_VTEXT`) | Done | — |
| Horizontal Type Mask (T): the glyphs as a selection | **Have** (0.9, `TL_TYPEMASK`) | Done | — |
| Vertical Type Mask (T) | **Have** (1.3): the Type Mask tool's Vertical tick sets the text down in columns, as the Vertical Type tool does (`tmask_vertical`, `E:amiga/toolbox.c`; the layout is `text_layout`'s, tested in `E:tests/typetest.c`) | Done | — |
| Hand (H): Fit Screen, Fill Screen, scroll all windows | **Have**: 100%, Fit Screen, Fill Screen (1.1), Zoom In/Out, the Space bar. Scroll All Windows (*upstream* #9144): **Left out**, Easel shows one picture at a time in tabs | Done | — |
| Rotate View (R): view angle, reset | **Partial** (1.2): the Rotate View tool (Shift: 15° steps, double-click resets) and View › Rotate View (90°, 180°, Reset View) turn the canvas's drawing (`render_turned` in `E:amiga/canvas.c`, `view_rotate` in `E:core/toolsx.c`); painting, picking, Quick Selection, the Art History Brush, Hand and Zoom work on a turned view, the other tools ask for it to be put straight; Pattern Preview tiles a turned view too and selection edges are hidden on it; it needs the whole composite in memory | Moderate (the other tools' overlays turned too) | P3 |
| Zoom (Z): Resize Windows to Fit, scrubby zoom, Fit/Fill/100%; *upstream* Zoom In / Zoom Out mode (#13013), the bar's buttons (#12286) | **Have** (1.1: the mode, a drag zooms, Fill Screen; `DR_ZOOMSCRUB` in `E:amiga/canvas.c`); Resize Windows: **Left out** (tabs, one window) | Done | — |
| Remove (J): non-local patch completion; `algo/src/remove.rs` | **Have** (1.2): the Remove tool brushes over what should go; when the stroke ends it is filled by PatchMatch over a pyramid (`ca_fill_rgba` in `E:core/cafill.c`), in bands of rows with progress and Abort. Slow on a 68040: seconds for a small blemish, about a minute for 100 × 100 pixels | Done | — |
| Spot Healing Brush (J): Type Content-Aware/Create Texture/Proximity Match, Sample All Layers | **Have**: Proximity Match (1.0); 1.2 adds Create Texture (blocks of texture from round the spot healed into it, `spot_texture_fill` in `E:core/brushx.c`) and Content-Aware (PatchMatch, `E:core/cafill.c`) | Done | — |
| Healing Brush (J): Source Sampled/Pattern, Aligned, Sample current/below/all, Mode, Diffusion | **Have**: Sampled, Aligned and Sample (1.1); 1.2 adds Source: Pattern, Mode (the healed pixels laid in that mode) and Diffusion 1-7 (how much of the source's fine detail is kept, `heal_blend_ex`) | Done | — |
| Patch (J): Source/Destination, Transparent, Diffusion, Content-Aware; *upstream* Shift constrains (#12274), Destination moves the selection (#12298) | **Have**: Source (1.0); 1.2 adds Destination (the patch healed in where it is dragged, the selection going with it, as #12298), Transparent (its texture only) and Content-Aware with Structure and Color (`patch_dest`, `patch_content` in `E:core/cafill.c`); Diffusion as the Healing Brush's | Done | — |
| Content-Aware Move (J): Move/Extend, Structure, Color, Transform On Drop | **Have** (1.2): draw round it, drag it: Move (the hole filled by PatchMatch from round it) or Extend (copied), Structure (the patch size) and Color (how much the edge is healed in) (`doc_ca_move` in `E:core/cafill.c`); no Transform On Drop (Free Transform afterwards does it) | Done | — |
| Red Eye (J): Pupil Size, Darken Amount; `algo/src/redeye.rs` | **Have** (1.0, `redeye_fix`) | Done | — |
| Clone Stamp (S): Size, Hardness, Mode, Opacity, Flow, Aligned, Sample current/below/all, Clone Source overlay with 5 sources | **Partial**: everything but the Clone Source panel and its overlay (1.1: Mode, Aligned and Current & Below, `doc_composite_below`) | Easy | P3 |
| Pattern Stamp (S): pattern, Aligned, Impressionist; *upstream* Behind in the Mode list (#13028) | **Partial**: pattern and Aligned (0.5); no Impressionist, no Mode | Easy | P3 |
| History Brush (Y): paints from a chosen history state; *upstream* opacity/flow and source selection (#4446) | **Have**: the snapshot (0.5), Flow and a History panel snapshot as the source (1.1, `ID_HISTBRUSHSRC`) | Done | — |
| Art History Brush (Y): 10 stroke styles, Area, Tolerance; `algo/src/art_history.rs` | **Have** (1.2): the ten styles (Tight/Loose, Short/Medium/Long, Dab, the curls), Area and Tolerance, strokes along the snapshot's edges in its colours (`art_history` in `E:core/brushx.c`), starting with Tight Short (as upstream #12726); one undo step a drag | Done | — |
| Blur tool: Mode, Strength, Sample All Layers | **Partial**: Mode (1.1, `mode_back` in `E:core/stroke.c`) and Strength; no Sample All Layers | Easy | P3 |
| Sharpen tool: as Blur, plus Protect Detail; *upstream* gentle Protect Detail (#12999) | **Partial**: Mode and Protect Detail (1.1); no Sample All Layers | Easy | P3 |
| Smudge: Strength, Finger Painting, Sample All; *upstream* Mode (#13031) | **Partial**: Mode, Strength, Finger Painting (1.1); no Sample All Layers | Easy | P3 |
| Dodge (O): Range Shadows/Midtones/Highlights, Exposure, Protect Tones | **Have** (1.1: `tone_lut`, `tone_apply` in `E:core/toolopts.c`, ported from PhotoCraft's `algo/src/retouch.rs`) | Done | — |
| Burn (O): the same | **Have** (as Dodge) | Done | — |
| Sponge (O): Mode Desaturate/Saturate, Flow, Vibrance; *upstream* the Flow field (#13022) | **Have** (1.1: `tone_sponge`) | Done | — |
| Pen (P): Shape/Path/Pixels, path operations, rubber band, auto add and delete; *upstream* Rubber Band in the gear (#13035), Alt-drag on a segment (#13037), Make: Selection/Mask/Shape (#12993) | **Partial**: the Pen and its anchor tools (0.9, `E:core/path.c`); no rubber band, Alt-drag on a segment or Make buttons | Easy | P3 |
| Freeform Pen (P): curve fitting, magnetic option | **Missing** | Moderate | P3 |
| Curvature Pen (P) | **Missing** | Moderate | P3 |
| Add Anchor Point (P) | **Have** (0.9) | Done | — |
| Delete Anchor Point (P) | **Have** (0.9) | Done | — |
| Convert Point (P) | **Have** (0.9) | Done | — |
| Path Selection (A) | **Have** (0.9) | Done | — |
| Direct Selection (A) | **Have** (0.9) | Done | — |
| Rectangle (U): Shape/Path/Pixels, Fill, Stroke colour/width/dashes/align/caps, W/H, path operations, per-corner radii (`tools/corner_radius.rs`, VEC `LiveShape::Rect`); *upstream* the gear's Square, Fixed Size, Proportional, From Center (#13894) | **Partial**: shape layers or pixels with fill and stroke (0.9, `E:core/shape.c`), Shift a square, Alt from the centre (1.1); no dashes, per-corner radii, Fixed Size or Proportional | Easy | P3 |
| Ellipse (U): the same | **Partial**: as Rectangle | Easy | P3 |
| Triangle (U); VEC `LiveShape::Polygon`; *upstream* Alt at the press picks the colour (#13910) | **Partial**: the Polygon tool with 3 sides (`SH_TRIANGLE` in `E:core/shape.h` has no tool of its own) | Easy | P3 |
| Polygon (U): sides, star ratio, smooth corners; *upstream* a Star tool in the slot (#12142) | **Partial**: sides and star (0.9); no smooth corners | Easy | P3 |
| Line (U): weight, arrowheads, stroke | **Have** (0.9) | Done | — |
| Custom Shape (U): shape library, Shapes panel | **Partial**: the active path as a shape (0.9); no library or panel | Moderate | P3 |
| Reset Tool / Reset All Tools (the options bar's tool icon menu; *upstream* defaults for many tools, #11856 #12243 #12253 #12383 #12691 #12862 #12989) | **Have** (1.3): Edit › Reset Tool and Reset All Tools, with PhotoCraft's tips per tool (Brush 13 px soft, Pencil 1 px hard, Eraser 13 px hard, Clone Stamp 21 px soft, Burn, Dodge and Sponge 65 px soft, Healing Brush 19 px hard ...: `tool_reset_tip`, `E:core/menu3.c`, `E:tests/menu3test.c`) | Done | — |

## 2. Menus, item by item

The menu tree is CAT (`crates/ui-egui/src/menu_catalog.rs`): 632 items, and every one is wired to a
command (`docs/parity-checklist.md`). Easel's menus are `menus[]` in `E:amiga/main.c` lines 84-135.
Items handled elsewhere in this document are listed in "How to read this".

### 2.1 File

Re-checked 2026-11-18 (Easel 1.1) against `menus[]` in `E:amiga/main.c`.

| PhotoCraft item (CAT id) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| New… `file.new`: presets (`engine/src/cmd/document_preset_cmds.rs`), colour mode (Bitmap at 1 bit, #14497), bit depth, resolution, units, background contents, pixel aspect | **Have** (1.3): presets (twelve built in, from PAL Low Res to A4 at 300 ppi and a 1-bit fax page, and saved ones in PROGDIR:Easel.newpresets, Save Preset and Delete Preset), width and height in pixels, inches, centimetres, millimetres, points or picas, resolution, colour mode (Bitmap, Grayscale, RGB, CMYK, Lab; Bitmap never transparent, as #14497), background (white, black, background colour, transparent), pixel aspect (the View menu's presets), the first history state "New" (#14083); `newdoc_make`, `E:core/menu3.c`, `E:tests/menu3test.c`. Bit depth: Easel's documents are 8 bits a channel | Done | — |
| Open… `file.open` | **Have**: about 45 formats (`E:core/codec.h` `FMT_`), SVG drawings as shape layers (1.1), anything else through datatypes | Done | — |
| Open As… `file.openAs`: forces the format | **Have** (round 3): File › Open As reads a file with the reader chosen (`codec_load_as` in `E:core/filesx.c`); 1.3 adds File › Open as Photoshop Raw (width, height, channels, header, interleaved or planar, 16 bits; `open_raw`, `raw_guess`, `E:core/menu3.c`, `E:tests/menu3test.c`) | Done | — |
| Open as Smart Object… (#12212 since eb6c1ab) | **Have** (1.2, stale here): File › Open as Smart Object (`ID_OPENSMART`, `smart_open` in `E:core/layerx.c`) | Done | — |
| Open EXR Parts… `file.openExrParts` | **Missing** | Moderate: needs an EXR reader first | P3 |
| Close `file.close` | **Have** | Done | — |
| Close All `file.closeAll` | **Have** | Done | — |
| Close Others `file.closeOthers` | **Have** (1.1) | Done | — |
| Save `file.save` | **Have**: ORA or a flat format (1.1: every format Easel writes), and offers to flatten | Done | — |
| Save As… `file.saveAs` | **Have** | Done | — |
| Save a Copy… `file.saveACopy` | **Have** (0.6): in the format its name asks for, layers kept in ORA and PSD | Done | — |
| Revert `file.revert` | **Have** | Done | — |
| Export › Quick Export as PNG | **Have** (1.1): beside the picture's file, or in the last drawer, without a requester | Done | — |
| Export › Export As…: format, quality, scale, transparency, metadata, preview, size estimate | **Have** (1.3): Export As (0.6, 1.2: scale, method, canvas, matte, metadata, sRGB in `E:core/exportx.c`) now says the size the file will have before writing it (`export_estimate`, `E:core/menu3.c`, `E:tests/menu3test.c`); the preview is Save for Web's | Done | — |
| Export › Export Preferences… | **Have** (round 3): Preferences › Export holds Quick Export's format, place, metadata and sRGB (section 17); Save for Web (1.3) offers the palette formats besides | Done | — |
| Export › Save for Web (Legacy)…: palette, dither, colours, 2-up preview | **Have** (1.3): File › Export More › Save for Web (Legacy): GIF, PNG-8, PNG-24, JPEG or IFF ILBM, colours, palette (Median Cut, Octree, Wu), dither, quality, progressive or interlaced, transparency and matte, scale; the original and the optimized picture side by side (the file written and read back), its size and download time (`sfw_encode`, `sfw_preview`, `E:core/menu3.c`, `E:tests/menu3test.c`). It is also the "Save for Screen" of section 21 | Done | — |
| Export › Artboards to Files… | **Have** (1.2, stale here): File › Export More › Artboards as PNG (`ID_EXPARTBOARDS`) | Done | — |
| Export › Artboards to PDF… | **Partial**: artboards exist (1.2) and Easel writes PDF (1.1), but one page: the artboards aren't pages of one PDF | Easy | P3 |
| Export › Layers to Files… | **Have** (0.6) | Done | — |
| Export › Layer Comps to Files… | **Have** (1.2): File › Export › Layer Comps to Files (and the Layer Comps window's Export to Files): each comp applied (one Apply Layer Comp step each, the picture ending on the last, as upstream #14003) and written as `<picture>-<comp>.png` | Done | — |
| Export › Color Lookup Tables… (.cube) | **Have** (1.3): what the visible adjustment layers do, sampled on a 2-64 point grid, as a .cube (TITLE, LUT_3D_SIZE, red fastest) or a .3dl (`lut_cube_from_doc`, `lut_3dl_from_doc`, `E:core/menu3.c`, `E:tests/menu3test.c`) | Done | — |
| Export › Data Sets as Files… | **Missing** | Moderate | P3 |
| Export › Paths to Illustrator… | **Have** (1.1) as SVG: Export › Paths to SVG, with the shape layers and the document's bounds | Done | — |
| Export › Render Video… | **Have** (1.1) as the Amiga's and the web's animations: Export › Layers as IFF ANIM and Layers as Animated GIF (MP4 and H.264 are **Left out**: far too slow on a 68040) | Done | — |
| Generate › Image Assets | **Have** (1.3): File › Generate › Image Assets writes every layer or group named like files ("logo.png, 50% logo-small.jpg8, icon.gif": scale, JPEG quality, PNG-8) trimmed into a drawer beside the picture (`assets_parse`, `assets_generate`, `E:core/menu3.c`, `E:tests/menu3test.c`) | Done | — |
| Place Embedded… `file.placeEmbedded` | **Partial** (0.6): Place as Layer goes on into Free Transform as a pixel layer; smart objects exist since 1.2 (Open as Smart Object, Convert to Smart Object) but Place doesn't make one | Easy | P3 |
| Place Linked… | **Partial**: Layer › Smart Objects › Convert to Linked (1.2) links a layer to a file; placing straight as a linked smart object isn't offered | Easy | P3 |
| Package… | **Have** (1.3): File › Package makes a drawer with the picture (OpenRaster) and a copy of every linked smart object's file, the layers relinked to the copies (`package_links`, `E:core/menu3.c`, `E:tests/menu3test.c`) | Done | — |
| Automate › Batch… | **Have** (1.2): File › Automate › Batch runs a recorded action (Window › Actions) on every picture in a drawer and saves each to another drawer as PNG, JPEG, ILBM, ORA, TIFF, PSD, BMP or GIF (`dlg_batch` in `E:amiga/panelsx.c`); EaselConvert still converts from the Shell | Done | — |
| Automate › Create Droplet… | **Missing** | Easy. Amiga stand-in: a Workbench tool icon that runs an action on the files dropped on it | P3 |
| Automate › Crop and Straighten Photos | **Missing** | Moderate (`algo/src/scancrop.rs`) | P3 |
| Automate › Contact Sheet II… | **Have** (1.3): the open pictures or files chosen, on sheets of a size and resolution, columns and rows, across or down first, spacing, rotated for the best fit, each picture a layer named after it with its file name as a caption (a type layer), or flattened into one layer named Contact Sheet II (#13793); as many sheets as needed (`contact_sheet`, `E:core/menu3.c`, `E:tests/menu3test.c`) | Done | — |
| Automate › Conditional Mode Change… | **Have** (1.2): source modes ticked, a target mode; recordable in an action | Done | — |
| Automate › Fit Image… | **Have** (1.1): File › Scripts › Fit Image (`doc_fit_image`) | Done | — |
| Automate › Lens Correction… | **Missing** (Filter › Lens Correction exists, 1.0) | Easy, once batches run filters | P3 |
| Automate › Merge to HDR Pro… | **Missing** | Hard: float stacks of several exposures overflow 16-64 MB | P3 |
| Automate › Photomerge… | **Missing** | Hard: feature matching and warping take too much CPU and RAM (`algo/src/panorama.rs`) | P3 |
| Scripts › Image Processor… (batch convert and resize; the open documents as a source, #12858) | **Have** (1.3): File › Scripts › Image Processor: the open pictures (#12858) or files chosen, saved as JPEG (quality), PSD, TIFF, PNG and IFF ILBM (colours), each fitted in a size if asked, into sub-drawers, with Convert Profile to sRGB, Copyright Info and icons, with progress and Abort (`improc_doc`, `E:core/menu3.c`, `E:tests/menu3test.c`); Run Action plays an action from Window › Actions on each first | Done | — |
| Scripts › Delete All Empty Layers | **Have** (1.1): one undo step (`doc_delete_empty_layers` in `E:core/scripts.c`) | Done | — |
| Scripts › Flatten All Layer Effects | **Have** (1.2): every layer's style drawn into its pixels (type and shape layers rasterized first), one undo step | Done | — |
| Scripts › Flatten All Masks | **Have** (1.1): one undo step (`doc_flatten_all_masks`) | Done | — |
| Scripts › Script Events Manager… | **Missing** | Easy if it only runs actions on events (open, save) | P3 |
| Scripts › Load Files into Stack… | **Have** (0.6) | Done | — |
| Scripts › Statistics… (stack modes) | **Have** (1.3): File › Scripts › Statistics: the open pictures or files chosen stacked and reduced by any of the eleven stack modes into a new picture (`doc_statistics`, `E:core/menu3.c`, `E:tests/menu3test.c`) | Done | — |
| Scripts › Browse… (run a script) | **Excluded** by request: the Amiga script host is ARexx | — | — |
| Import › Variable Data Sets… | **Missing** | Moderate | P3 |
| Import › Video Frames to Layers… | **Have** (1.1): File › Import Frames to Layers, every frame of an animated GIF or IFF ANIM a layer, its time in the name | Done | — |
| Import › Notes… | **Missing** | Easy | P3 |
| Import › WIA Support… | **Left out**: WIA is the Windows scanner API (datatypes and a scanner program's files stand in) | Not sensible | — |
| File Info… (XMP/IPTC/EXIF metadata) | **Have** (0.6): six fields, kept in PNG, JPEG, TIFF, PSD, ORA and (1.1) PDF | Done | — |
| File Info: a copyrighted picture shows © before its name in the tab and title (#13985) | **Have** (1.3): the window's title (1.2) and now the document tab too show © before the name (`tab_label` in `E:amiga/panels.c`) | Done | — |
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
| Paste Special › Paste Outside | **Have** (1.2): Edit › Paste Outside: a new layer masked to everything but the selection | Done | — |
| Clear `edit.clear` | **Have** | Done | — |
| Search… (command palette, ⌘F) | **Have** (1.2): Edit › Search lists every menu command as Menu › Item › Subitem and narrows the list as you type (each word anywhere in the name); Return or a double-click runs it (`dlg_search` in `E:amiga/panelsx.c`) | Done | — |
| Check Spelling… | **Have** (round 4): Edit › Check Spelling checks every type layer's words against Quill's British English word list (`PROGDIR:Dict/british-english`, which the package now carries in a Dict drawer) with suggestions one edit away; Ignore, Ignore All, Add (to `PROGDIR:Easel.words`), Change and Change All, each change one undo step (`E:core/spell.c`, `E:amiga/r4ui.c`, `E:tests/r4test.c`) | Done | — |
| Find and Replace Text… | **Have** (1.3): Edit › Find and Replace Text: all type layers or the active one, case-sensitive, whole words, Find Next (its layer made active), Change, Change All as one undo step (`type_find_replace`, `type_find_next`, `E:core/menu3.c`, `E:tests/menu3test.c`) | Done | — |
| Fill… `edit.fill`: Foreground/Background/Color/Pattern/History/Black/50% Gray/White/Content-Aware, Mode, Opacity, Preserve Transparency | **Partial**: foreground, background, black, 50% grey, white, a colour or (1.3) History (the History Brush's snapshot, `fill_from_pixels`), with mode, opacity and (1.3) Preserve Transparency (`fill_keep`, `E:core/menu3.c`, `E:tests/menu3test.c`); the pattern through Edit › Fill with Pattern (0.5) and Content-Aware through Edit › Content-Aware Fill (1.2), not in this dialog | Easy | P3 |
| Stroke… `edit.stroke`: width, colour, location, mode, opacity, preserve transparency | **Have** (1.3): width, inside, centre or outside, mode (`stroke_selection_mode` in `E:core/paint.c`), opacity and Preserve Transparency (`stroke_keep`, `E:core/menu3.c`, `E:tests/menu3test.c`); in the foreground colour | Done | — |
| Content-Aware Fill… | **Have** (1.2, stale here): Edit › Content-Aware Fill (PatchMatch, `E:core/cafill.c`, `E:tests/smarttest.c`) | Done | — |
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
| Purge › Undo | **Have**: Edit › Purge › Undo History | Done | — |
| Purge › Clipboard | **Have** (1.1): Edit › Purge › Clipboard (Easel's own) | Done | — |
| Purge › Histories (every document) | **Have** (1.1): Edit › Purge › All forgets every open picture's history (with the clipboard); Undo History the current one's | Done | — |
| Purge › Video Cache | **Left out**: Easel has no video cache | Not sensible | — |
| Purge › All | **Have** (1.1), after asking | Done | — |
| Presets › Preset Manager… | **Missing** (brushes, gradients, patterns and swatches each load and save their own) | Easy | P3 |
| Presets › Migrate Presets | **Left out**: it imports from an older desktop install | Not sensible | — |
| Presets › Export/Import Presets… | **Partial**: each kind loads and saves on its own (0.5, 1.0 adjustment presets) | Easy | P3 |
| Color Settings… (working spaces, policies) | **Have** (1.2, stale here): Edit › Color Settings (`ID_COLORSET`, `E:amiga/colorui.c`, `E:core/cms.c`; section 11 has the details) | Done | — |
| Assign Profile… | **Have** (1.2, stale here): Edit › Assign Profile (`colorx_assign` in `E:core/colorx.c`; section 11) | Done | — |
| Convert to Profile… (4 intents, BPC) | **Have** (1.2, stale here): Edit › Convert to Profile with the intents and Black Point Compensation (`colorx_convert`; section 11) | Done | — |
| Keyboard Shortcuts… (+ .kys) | **Have** (0.8): Edit › Keyboard Shortcuts, PROGDIR:Easel.keys (.kys isn't read) | Done | — |
| Menus… (hide or colour items) | **Missing** | Moderate: items can be hidden, but Intuition menus have no colours | P3 |
| Toolbar… (customise the toolbox) | **Missing** | Easy | P3 |

### 2.3 Image (Adjustments are in section 5, Mode in section 11)

Rechecked 2026-11-18 against Easel 1.0 and PhotoCraft `c337d6e`.

| PhotoCraft item (CAT id) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Auto Tone `image.autoTone` | **Have** | Done | — |
| Auto Contrast `image.autoContrast` | **Have** | Done | — |
| Auto Color `image.autoColor` | **Have** | Done | — |
| Image Size… `image.imageSize`: Bicubic/Smoother/Sharper/Bilinear/Nearest/Preserve Details, units, resolution, scale styles | **Partial**: Bicubic, and new in this round Bicubic Smoother, Bicubic Sharper and Preserve Details (Lanczos-3, checked against Pillow), Bilinear, Nearest; the resolution, and Resample off to change only the resolution (`E:core/resample.c`, `dlg_image_size`). Sizes in inches, centimetres or columns (PhotoCraft #6214) and Scale Styles are missing | Easy | P3 |
| Image Size: Alt+1…8 pick the resample method (PhotoCraft #15054) | **Missing**: the method is a cycle gadget | Easy | P3 |
| Image Size, Fit Image, Canvas Size, Rotate Canvas and Duplicate drop the selection (PhotoCraft #13913, #14534, #14564, #15075) | **Have**, differently: Easel keeps the selection in step with the canvas instead (0.7); Duplicate starts without one, as PhotoCraft's does | Done | — |
| Canvas Size… `image.canvasSize` | **Have**: anchor and fill | Done | — |
| Image Rotation › 180° | **Have** | Done | — |
| Image Rotation › 90° Clockwise | **Have** | Done | — |
| Image Rotation › 90° Counter Clockwise | **Have** | Done | — |
| Image Rotation › Arbitrary… | **Have** | Done | — |
| Image Rotation › Flip Canvas Horizontal | **Have** | Done | — |
| Image Rotation › Flip Canvas Vertical | **Have** | Done | — |
| Crop `image.crop` | **Have** | Done | — |
| Trim… (transparent, top-left or bottom-right colour; which sides) | **Have** (1.2): Image › Trim…: based on transparent pixels or the top-left or bottom-right colour, any of the four sides (`image_trim_ex` in `E:core/viewx.c`) | Done | — |
| Reveal All | **Missing**: Easel's layers are exactly canvas-sized, so there are no off-canvas pixels | Moderate: needs layer offsets or tiles (section 19) | P3 |
| Duplicate… | **Have** | Done | — |
| Apply Image… | **Have** (1.3): Image › Apply Image: any open picture of the same size, Merged or a layer, its RGB, red, green, blue, transparency, grey or selection, inverted or not, in any blend mode at an opacity through the selection, with Preserve Transparency (`apply_image`, `E:core/menu3.c`, `E:tests/menu3test.c`) | Done | — |
| Calculations… | **Have** (1.3): Image › Calculations: two sources (picture, layer, channel, inverted) blended at an opacity into a new channel or the selection (`calculations`, `calc_result`, `E:core/menu3.c`, `E:tests/menu3test.c`) | Done | — |
| Variables › Define… / Data Sets… (2 items) | **Missing** | Moderate | P3 |
| Apply Data Set… | **Missing** | Moderate | P3 |
| Trap… | **Excluded** by request: print prepress | — | — |
| Analysis › Set Measurement Scale, Select Data Points, Record Measurements, Ruler Tool, Count Tool, Place Scale Marker (6 items) | **Partial** (1.3): Image › Analysis › Set Measurement Scale (pixel length, from the Ruler's line, logical length and units), Record Measurements and Measurement Log (round 3's log, `E:core/measure.c`; round 4: in the picture's scale, with the Scale Units and Scale Factor columns exported, `measure_set_scale`, `E:tests/filestest.c`), Ruler Tool, Count Tool and Place Scale Marker (a bar the scale's length, `scale_marker`, `E:core/menu3.c`, `E:tests/menu3test.c`); Select Data Points is missing and the scale isn't kept in the file | Easy | P3 |

### 2.4 Layer (masks, styles, fill, adjustment and smart objects are in section 3; Combine Shapes is in section 10)

Rechecked 2026-11-18 against Easel 1.0 plus the 1.1 layer work (`E:amiga/main.c` `menus[]`, `E:core/layers.h`,
`E:core/layerops.h`, `E:core/fx.h`).

| PhotoCraft item (CAT id) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| New › Layer… `layer.new.layer` (name, mode, opacity, clip, colour) | **Have** (1.2): New › Layer… asks the name, clipping, colour label, mode and opacity first; New › Layer still makes "Layer N" at once | Done | — |
| New › Layer from Background… | **Have**: nothing to convert - Easel has no locked Background, every layer (the bottom one too) is a normal layer already; Layer › New › Background from Layer goes the other way | Done | — |
| New › Background from Layer | **Have** (1.1): `doc_background_from_layer` in `E:core/layerops.c`: the active pixel layer laid over the background colour, moved to the bottom, named Background, position locked | Done | — |
| New › Group… | **Have**: `doc_new_group` (`E:core/layers.c`); groups nest, collapse and pass through (0.3) | Done | — |
| New › Group from Layers… | **Have**: Group Layers (`doc_group_layers`) groups the picked layers | Done | — |
| New › Artboard… / Artboard from Group / Artboard from Layers (3 items) | **Have** (1.2 and 1.3): Layer › Links and Artboards › New Artboard, Artboard from Group (1.2) and Artboard from Layers (1.3: the picked layers grouped in an artboard round them, `artboard_from_layers`, `E:core/menu3.c`, `E:tests/menu3test.c`) | Done | — |
| New › Frame from Layers | **Have** (1.3): Layer › Links and Artboards › Frame from Layers puts the active layer in a new frame of its size (`frame_from_layer`, `E:core/menu3.c`, `E:tests/menu3test.c`) | Done | — |
| New › Layer via Copy | **Have** | Done | — |
| New › Layer via Cut | **Have** | Done | — |
| Duplicate Layer… | **Have** | Done | — |
| Delete › Layer | **Have** | Done | — |
| Delete › Hidden Layers | **Have** (1.1): Delete Hidden Layers (`doc_delete_hidden`): hidden layers, and hidden groups with their members; at least one layer is kept | Done | — |
| Quick Export as PNG (layer) | **Have** (1.2): Layer › Quick Export as PNG writes the active layer alone (with its mask and style) beside the picture as `<layer name>.png` | Done | — |
| Export As… (layer); upstream #15039 puts several layers into a folder as `<layer name>.<ext>` | **Have** (1.3): File › Export More › Picked Layers to Files writes the picked layers (or the active one) into a drawer as `<layer name>.png`, each trimmed (`layer_to_file`, `E:core/menu3.c`, `E:tests/menu3test.c`); Layers to Files (all of them) as before | Done | — |
| Rename Layer | **Have**: through Layer Properties | Done | — |
| Layer Content Options… | **Have**: `ID_FILLSETTINGS` edits a fill or adjustment layer's settings | Done | — |
| Mask All Objects | **Left out**: it needs object recognition (an ML model) | Not sensible | — |
| New Layer Based Slice | **Have** (1.3): Layer › Links and Artboards › New Layer Based Slice: a slice round the active layer, named after it (`slice_from_layer`, `E:core/menu3.c`, `E:tests/menu3test.c`) | Done | — |
| Group Layers / Ungroup Layers (2 items) | **Have**: `doc_group_layers`, `doc_ungroup` | Done | — |
| Hide Layers | **Have**: Hide/Show Layer | Done | — |
| Arrange › Bring to Front | **Have** | Done | — |
| Arrange › Bring Forward | **Have** | Done | — |
| Arrange › Send Backward | **Have** | Done | — |
| Arrange › Send to Back | **Have** | Done | — |
| Arrange › Reverse | **Have** (1.1): `doc_reverse_layers`, the picked layers at the active one's level | Done | — |
| Align › Top/Vertical Centers/Bottom/Left/Horizontal Centers/Right (6 items) | **Have**: `doc_align` (0.3), to the selection or the canvas | Done | — |
| Distribute › 8 items | **Have** (1.2): Align › Distribute Left, Right, Top and Bottom Edges, Horizontally and Vertically (centres), Horizontal and Vertical Spacing (equal gaps), `AL_DIST_LEFT`..`AL_SPACE_V` in `E:core/layers.h` | Done | — |
| Lock Layers… (transparency, pixels, position, artboard, all; DOC `Locks`) | **Have**: Layer › Lock: Pixels, Transparency, Position, All (artboards don't exist) | Done | — |
| Link Layers | **Have** (1.2, stale here): Layer › Links and Artboards › Link Layers, Unlink Layers (`doc_link_layers`, `E:core/layerx.c`) | Done | — |
| Select Linked Layers | **Have** (1.2, stale here): `doc_select_linked` | Done | — |
| Merge Layers `layer.mergeLayers` | **Have**: Merge Down, Merge Group and Merge Layers (`doc_merge_picked`, the picked layers) | Done | — |
| Merge Visible | **Have** | Done | — |
| Flatten Image | **Have** | Done | — |
| Matting › Color Decontaminate… | **Missing** | Moderate | P3 |
| Matting › Defringe… | **Have**: `E:core/matte.c` (0.7) | Done | — |
| Matting › Remove Black Matte | **Have** (0.7) | Done | — |
| Matting › Remove White Matte | **Have** (0.7) | Done | — |
| Rasterize › Type, Shape, Fill Content, Vector Mask, Smart Object, Video, Layer Style, Layer, All Layers (9 items) | **Have**: Type, Shape, Layer, Layer Style (1.1) and All Layers (1.2: every type, shape and fill layer, one step); Vector Mask, Smart Object and Video have nothing to rasterize in Easel | Done | — |
| Video Layers › 12 items (new blank or from file, insert, duplicate or delete frame, replace or interpret footage, restore, reload, rasterize) | **Missing** | Not sensible for real video. Stand-in: IFF ANIM frames as a frame stack (section 21) | P3 |
| Layer › Copy CSS (upstream #10569) | **Have** (1.1): `layer_css` (`E:core/layerops.c`): the layer's box, opacity, mode, solid or gradient fill, drop, inner and glow shadows and stroke as one CSS rule, put on the Amiga clipboard as text (FTXT) | Done | — |

### 2.5 View (Proof Setup, Proof Colors, Gamut Warning and 32-bit preview are in section 11)

Re-checked 2026-11-18 (Easel 1.1).

| PhotoCraft item (CAT id) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Pixel Aspect Ratio › Custom, Square, D1/DV NTSC, PAL, widescreen, HDV, DVCPRO, Anamorphic (9 items) | **Have** (1.2): View › Pixel Aspect Ratio: Square, the seven video presets, five Amiga ones (PAL and NTSC low and high res, PAL laced), From Amiga Screen Mode and Custom… (a ratio or width:height); kept as the picture's BMHD aspect, one undo step (`par_set` in `E:core/viewx.c`). The canvas draws pixels that tall (`zy` in `E:amiga/canvas.c`: the view, rulers, grid, guides, brush outline, Navigator) | Done | — |
| Pixel Aspect Ratio Correction (toggle) | **Have** (1.2): View › Pixel Aspect Ratio Correction, on at first | Done | — |
| Zoom In | **Have** | Done | — |
| Zoom Out | **Have** | Done | — |
| Fit on Screen | **Have** | Done | — |
| Fit Layer(s) on Screen | **Have** (1.2): the picked layers' content (or the active one's) fitted and centred | Done | — |
| Fit Artboard on Screen | **Have** (1.3): View › Fit Artboard on Screen zooms to the artboard the active layer is in | Done | — |
| 100% | **Have** | Done | — |
| 200% | **Have** (Easel adds 50%) | Done | — |
| Print Size | **Have** (1.2): the zoom for the picture's dpi on a screen of the Units & Rulers page's screen resolution (72 at first) | Done | — |
| Flip Horizontal (the view only) | **Have** (1.3): View › Flip Horizontal mirrors the view (the turned view's drawing and pointer mapping, `render_turned` and `to_img` in `E:amiga/canvas.c`); painting and the tools that work on a turned view work on it, the picture is untouched | Done | — |
| Screen Mode › Standard, Full Screen With Menu Bar, Full Screen (3 items) | **Have** (0.8): View › Screen Mode: in a window, on Easel's own screen, or full screen in any mode | Done | — |
| Extras (toggle all overlays) | **Have** (1.1, menu item 1.2): View › Extras (Amiga-H; Ctrl-H on the canvas) hides the selection outline, guides, grid, pixel grid, layer edges and target path | Done | — |
| Show › Layer Edges | **Have** (1.2): the active pixel layer's box, dotted, in the overlay | Done | — |
| Show › Selection Edges | **Have** (1.2): the marching ants can be hidden while the selection stays | Done | — |
| Show › Grid | **Have** (0.4) | Done | — |
| Show › Guides / Canvas Guides (2 items) | **Have** (0.4) | Done | — |
| Show › Pixel Grid | **Have** (1.2): a thin line between image pixels from 800% up (`pixgrid_line`) | Done | — |
| Show › Target Path, Artboard Guides, Count, Smart Guides, Slices, Notes, Brush Preview, Mesh, Edit Pins, All, Show Extras Options (11 items) | **Partial** (1.2): Show › Target Path, Notes, Count and Slices, All and None; artboard guides, smart guides, brush preview, mesh and pins have no feature behind them yet | Easy to Moderate, each with its feature | P3 |
| Rulers | **Have** (0.4) | Done | — |
| Snap | **Have** (0.4) | Done | — |
| Snap To › Guides, Grid, Layers, Slices, Document Bounds, All (6 items) | **Have** (1.3): View › Snap To › Guides, Grid, Document Bounds, Centre Lines, Pixels (1.1) and Layers (the other visible layers' edges and centres) and Slices (`snap_extra`, `SNAP_LAYERS`, `SNAP_SLICES`, `E:core/menu3.c`, `E:tests/menu3test.c`) | Done | — |
| Lock Guides, Clear Guides, Clear Selected Artboard Guides, Clear Canvas Guides, New Guide…, New Guide Layout…, New Guides From Shape (7 items) | **Have**: New Guide and Clear Guides (0.4), Lock Guides (1.1), New Guide Layout… (1.2: columns and rows with widths and gutters, margins, centred, clearing the old, `guide_layout`) and New Guides From Shape (1.2: the active layer's or the selection's edges and centre lines); the artboard items have no artboards to act on | Done | — |
| Lock Slices / Clear Slices (2 items) | **Have** (1.3): View › Lock Slices (the Slice and Slice Select tools leave them be) and Clear Slices | Done | — |
| Pattern Preview (tiles the document) | **Partial** (1.2): View › Pattern Preview draws the picture tiled round itself, and edits show in every tile; painting doesn't wrap across the edges yet (upstream #9348) | Easy | P3 |
| Pixel Art Preview | **Have** (1.3): View › Pixel Art Preview: the zoom keeps to whole steps (100 %, 200 % ... 6400 % and 1/2, 1/3 ... below), pixels drawn hard, with the pixel grid from 800 % (`pixelart_zoom`, `E:core/menu3.c`, `E:tests/menu3test.c`) | Done | — |

### 2.6 Window › Arrange and Workspace (the panels are in section 15)

Re-checked 2026-11-18 (Easel 1.1): unchanged since 0.2 apart from the rows' notes.

| PhotoCraft item (CAT id) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Arrange › Tile All Vertically/Horizontally, 2-up, 3-up, 4-up, 6-up, Cascade, Tile (11 items; borders between them dragged, #10695) | **Missing**: one canvas, with tabs | Moderate: several canvas objects or windows | P3 |
| Arrange › Consolidate All to Tabs | **Have**: Easel only has tabs | Done | — |
| Arrange › Float in Window / Float All in Windows / New Window for Document (3 items) | **Missing** | Moderate | P3 |
| Arrange › Match Zoom / Location / Rotation / All (4 items) | **Have** (1.2): Window › Arrange › Match Zoom, Match Location (the same part of each picture) and Match All; Easel has no view rotation to match | Done | — |
| Workspace › Essentials, Photography, Painting, Pixel Art, Graphic and Web, Motion (6 items) | **Missing** (Tab and Shift-Tab hide the panels) | Moderate: needs dockable panels | P3 |
| Workspace › Reset, New, Delete, Lock Workspace (4 items; Keyboard Shortcuts… repeats Edit's) | **Missing** | Moderate | P3 |

## 3. Layers: types, masks, groups, clipping, styles and effects, smart objects, adjustment layers

PhotoCraft's model is `Layer` and `LayerContent` in DOC. Easel's is `Layer` and `Doc` in
`E:core/doc.h`: up to 100 8-bit RGBA layers, each canvas-sized, kept bottom first with a nesting depth
(groups), each a pixel, group, adjustment, fill, type or shape layer (`LK_`), with a name, opacity,
fill opacity, blend mode, visibility, four locks, a mask, clipping, Blend If, a colour label and (1.1)
a layer style. Rechecked 2026-11-18: Easel 0.3 (live layers), 0.6 (PSD layers) and 0.9 (type and
shape layers) made most of the old "Missing" rows stale.

### 3.1 Layer kinds and properties

| PhotoCraft feature (source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Pixel layer, `LayerContent::Raster` (sparse tiles, any size or offset) | **Partial**: full canvas-size buffers with no offset, so off-canvas pixels are lost | Moderate (tiles, section 20) | P2 |
| Group, `LayerContent::Group` (nesting to 100 levels, expand and collapse) | **Have**: `LK_GROUP` with `depth` and `collapsed` (`E:core/layers.c`), pass-through or isolated, masks on groups; OpenRaster and PSD keep them | Done | — |
| Adjustment layer, `LayerContent::Adjustment` | **Have**: `LK_ADJUST` for every adjustment (`AdjParams`, applied row by row in `doc_composite`) | Done | — |
| Fill layer, `LayerContent::Fill` (Solid, Gradient, Pattern; DOC `Fill`) | **Have**: solid colour and gradient (`FillParams`); 1.2 pattern fill layers with a scale (`doc_new_pattern_fill` in `E:core/layerx.c`, the pattern kept with the layer) | Done | — |
| Type layer, `LayerContent::Text` | **Have**: `LK_TEXT` (0.9, `E:core/type.h`) | Done | — |
| Shape layer, `LayerContent::Shape` | **Have**: `LK_SHAPE` (0.9, `E:core/shape.h`) | Done | — |
| Smart Object, `LayerContent::Smart` | **Have** (1.2): memory-conscious: the source is a PNG in the scratch drawer (or the linked file) and is read back only while the layer is redrawn after a transform, so a smart object costs what a pixel layer does; Free Transform on one works from the source (scaling down and up again loses nothing); embedded sources go inside OpenRaster files (`E:core/layerx.c`) | Done | — |
| Opacity | **Have** | Done | — |
| Fill Opacity (content but not effects), `Layer::fill_opacity` | **Have**: `fill_cut`; in 1.1 the layer style is drawn at the layer's opacity whatever the fill (`fx_under`/`fx_over` in `E:core/fx.c`) | Done | — |
| Blend mode | **Have**: all 34 (section 4) | Done | — |
| Visibility | **Have** | Done | — |
| Colour labels (12, `LabelColor`) | **Have** (1.1): Layer › Layer Color with PhotoCraft's twelve (None, Red, Orange, Yellow, Green, Blue, Violet, Gray, Seafoam, Indigo, Magenta, Fuchsia; `doc_set_label` on the picked layers), shown down the Layers panel's eye column, kept in OpenRaster (`easel:label`) | Done | — |
| Link groups, `Layer::link_group` | **Have** (1.2): Layer › Links and Artboards › Link Layers, Unlink Layers, Select Linked Layers (`doc_link_layers`); the Move tool moves linked layers together in one step (`doc_move_linked`); kept in OpenRaster | Done | — |
| Blending Options › Blend If (this layer and underlying ranges), `BlendIf` | **Partial**: Blend If on grey, this layer and underlying, black and white points (`blendif` in `E:core/doc.h`, Layer Properties); the per-channel ranges and split (feathered) points are missing | Easy | P3 |
| Advanced Blending: channels excluded, knockout, blend interior effects or clipped as a group, transparency shapes, mask hides effects (`AdvancedBlending`, `excluded_channels`) | **Missing**: Easel blends interior effects on their own and clips as a group, as Photoshop's defaults do | Moderate | P3 |
| Multi-layer selection (Ctrl or Shift-click in Layers) | **Have**: `picked`, `doc_pick_layer` (0.3) | Done | — |
| Background layer semantics (`has_background_traits`) | **Partial**: there's no locked Background; Clear on a layer that can't be transparent uses the background colour; 1.1's Background from Layer makes an opaque, position-locked bottom layer | Easy | P3 |
| Layer XMP metadata, `Layer::xmp` | **Left out**: only for round trips with scripts | Not sensible | — |

### 3.2 Masks and clipping (Layer › Layer Mask, Vector Mask, Create Clipping Mask)

| PhotoCraft feature (CAT id / source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Layer Mask › Reveal All / Hide All | **Have**: `doc_mask_add(MK_REVEAL_ALL / MK_HIDE_ALL)` | Done | — |
| Layer Mask › Reveal Selection / Hide Selection | **Have** | Done | — |
| Layer Mask › From Transparency | **Have** (`MK_FROM_ALPHA`) | Done | — |
| Layer Mask › Delete | **Have**: `doc_mask_delete` | Done | — |
| Layer Mask › Apply | **Have**: `doc_mask_delete(..., 1)` | Done | — |
| Layer Mask › Enable Layer Mask (toggle) | **Have** | Done | — |
| Layer Mask › Link Layer Mask (toggle) | **Have** | Done | — |
| Painting on the mask, Alt-click to view it, mask thumbnail (`panels/mask_thumbs_ui.rs`) | **Partial**: paint on the mask (click its thumbnail or Edit Mask/Layer), mask thumbnails with a red cross when off; viewing the mask alone (Alt-click) is missing | Easy | P3 |
| Mask properties: density and feather (`panels/mask_props_ui.rs`) | **Have**: `mask_fade`, `mask_feather` (Layer Properties) | Done | — |
| Vector Mask › Reveal All, Hide All, Current Path, Delete, Enable, Link (6 items) | **Have** (1.2): Layer › Vector Mask (and Rasterize): a path whose inside shows, drawn anti-aliased, combined with the layer mask, with feather and density; linked, it moves with the layer and follows crops and rotations (`doc_vmask_add` and friends in `E:core/layerx.c`); kept in OpenRaster as SVG path data | Done | — |
| Create Clipping Mask `layer.createClippingMask` (`Layer::clipped`) | **Have**: `doc_set_clip`, arrows in the Layers panel | Done | — |
| Drag a layer mask onto another layer or group to move it, Alt to copy it (upstream #10549) | **Missing** | Easy | P3 |

### 3.3 Layer styles and effects (Layer › Layer Style, 18 items; DOC `Effects`)

Easel 1.1 has them (`E:core/fx.c`, `E:core/fxdef.h`; the Layer Style window in `E:amiga/fxdlg.c`).
Each effect is worked out from the layer's shape (its alpha times its mask) with exact Euclidean
distance transforms (spread, choke, stroke) and three box blurs (softness), into one byte a pixel per
effect over the box the effects reach, and kept with the layer until its pixels, mask or style change;
`doc_composite` then only blends them in, row by row. They work on pixel, fill, type and shape layers
(1.2: and on groups; not on adjustment layers, unlike PhotoCraft) and are kept in OpenRaster (`easel:effects`).
PSD files get the merged look but not the effect settings (no `lfx2` writer or reader yet).
`E:tests/fxtest.c` checks each one.

| PhotoCraft item (CAT id) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Blending Options… | **Have**: opens Layer Properties (opacity, fill, mode, Blend If) | Done | — |
| Bevel & Emboss… (style, technique, depth, size, soften, angle, gloss contour, texture) | **Partial**: Inner Bevel, Outer Bevel, Emboss, Pillow Emboss; depth, direction, size, soften, angle, altitude, global light, highlight and shadow modes, colours and opacities; the techniques (Chisel), gloss contours, texture and Stroke Emboss (upstream #9337) are missing | Moderate | P3 |
| Stroke… (size, position, fill type, overprint) | **Partial**: size, outside, inside or centre, mode, opacity, colour; gradient and pattern fills are missing | Easy | P3 |
| Inner Shadow… | **Have**: mode, colour, opacity, angle, global light, distance, choke, size | Done | — |
| Inner Glow… | **Partial**: mode, colour, opacity, edge or centre, choke, size; contours, gradient glows and noise are missing | Easy | P3 |
| Satin… | **Have**: mode, colour, opacity, angle, distance, size, invert | Done | — |
| Color Overlay… | **Have** | Done | — |
| Gradient Overlay… | **Have**: two colours, mode, opacity, reverse, linear, radial, angle, reflected or diamond, angle, scale, offset (multi-stop gradients from the Gradient Editor are missing) | Done | — |
| Gradient Overlay moved by a drag on the canvas (upstream #10840) | **Partial**: moved by the Offset sliders in the Layer Style window, not by dragging on the canvas | Easy | P3 |
| Pattern Overlay… | **Have** (1.2): Layer › Layer Style › Pattern Overlay: the chosen pattern over the layer's content, mode, opacity, scale (`doc_set_pattern_overlay` in `E:core/layerx.c`), drawn below the colour and gradient overlays | Done | — |
| Outer Glow… | **Partial**: mode, colour, opacity, spread, size; contours (upstream #11410 fixes their flat case), gradient glows and noise are missing | Easy | P3 |
| Drop Shadow… | **Have**: mode, colour, opacity, angle, global light, distance, spread, size, Layer Knocks Out Drop Shadow | Done | — |
| Copy Layer Style | **Have** | Done | — |
| Paste Layer Style | **Have**: onto the picked layers (`doc_paste_fx`) | Done | — |
| Clear Layer Style | **Have** | Done | — |
| Global Light… | **Have**: per document (`doc_global_light`; changing the angle in an effect that uses it moves it) | Done | — |
| Create Layer (effects to pixel layers) | **Have**: Create Layers (`doc_fx_to_layers`): shadow and glow below, the rest above, each with its mode and opacity | Done | — |
| Hide All Effects | **Have** (a ticked item; the Layers panel greys a hidden style's "fx") | Done | — |
| Scale Effects… | **Have**: sizes, distances, soften and gradient offset by 1-1000 % | Done | — |
| Effects on groups and adjustment layers (PhotoCraft's `lfxs`) | **Partial** (1.2): every effect on groups, worked out from the members' composite (its alpha, made while the effects are, `E:core/fx.c`) and drawn again when a member changes; not on adjustment layers | Moderate (adjustment layers' shape is the whole canvas) | P3 |

### 3.4 Fill layers (Layer › New Fill Layer)

| PhotoCraft item | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Solid Color… | **Have**: `doc_new_fill` (0.3) | Done | — |
| Gradient… | **Have**: two colours, five styles, angle, scale, reverse (multi-stop gradients are missing) | Done | — |
| Pattern… | **Have** (1.2): Layer › New Fill Layer › Pattern (the chosen pattern, a scale) | Done | — |

### 3.5 Adjustment layers (Layer › New Adjustment Layer, 16 items; ADJ)

Easel 0.3 made every adjustment a layer too (`doc_new_adjust`, `AK_` kinds in `E:core/adjust.h`, the
Layer › New Adjustment Layer submenu); 1.0 added Replace Color and the per-range Hue/Saturation,
Black & White and per-channel Curves.

| PhotoCraft item | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Brightness/Contrast… | **Have** (`AK_BRIGHTNESS`) | Done | — |
| Levels… | **Have** (`AK_LEVELS`, per channel) | Done | — |
| Curves… | **Have** (`AK_CURVES`, per channel) | Done | — |
| Exposure… | **Have** | Done | — |
| Vibrance… | **Have** | Done | — |
| Hue/Saturation… | **Have** (with the six ranges, 1.0) | Done | — |
| Color Balance… | **Have** | Done | — |
| Black & White… | **Have** (six sliders and tint, 1.0) | Done | — |
| Photo Filter… | **Have** | Done | — |
| Channel Mixer… | **Have** | Done | — |
| Color Lookup… (.cube/.3dl/.look) | **Missing** | Moderate: a 17³ u8 table with trilinear lookup | P3 |
| Invert | **Have** | Done | — |
| Posterize… | **Have** | Done | — |
| Threshold… | **Have** | Done | — |
| Gradient Map… | **Have** | Done | — |
| Selective Color… | **Have** | Done | — |

### 3.6 Smart objects and smart filters (Layer › Smart Objects, 16 items plus Stack Mode; Layer › Smart Filter, 5 items)

| PhotoCraft item | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Convert to Smart Object (lossless transforms) | **Have** (1.2): the layer's content box written to the scratch drawer as PNG (`doc_smart_convert`); Free Transform then works from it | Done | — |
| New Smart Object via Copy | **Have** (1.2, `doc_smart_copy`: a layer with its own copy of the source) | Done | — |
| Reveal in Finder (Easel would call it "Show in Drawer") | **Have** (1.2): Show in Drawer tells where the source is | Done | — |
| Update Modified Content / Update All Modified Content (2 items) | **Have** (1.2): the source (or every linked one) read again and the layer redrawn | Done | — |
| Edit Contents | **Have** (1.2): the source opens as a picture of its own; saved, Update Modified Content brings it back | Done | — |
| Relink to File… | **Have** (1.2, `doc_smart_relink`) | Done | — |
| Replace Contents… | **Have** (1.2, `doc_smart_replace`: the new picture fitted to the old one's size) | Done | — |
| Export Contents… | **Have** (1.2: the source as PNG) | Done | — |
| Convert to Linked… / Convert to Embedded (2 items) | **Have** (1.2, `doc_smart_link`, `doc_smart_embed`) | Done | — |
| Convert to Layers | **Have** (1.2: the source as a pixel layer at its place, untransformed; the smart object hidden) | Done | — |
| Reset Transform (upstream #11675) | **Have** (1.2: back to 100 % and upright about its centre, `doc_smart_reset`) | Done | — |
| File › Open as Smart Object… (upstream #12212) | **Have** (1.2: a new picture whose one layer is the file as a smart object, `smart_open`) | Done | — |
| Stack Mode › Entropy, Kurtosis, Maximum, Mean, Median, Minimum, Range, Skewness, Standard Deviation, Summation, Variance (11 items) | **Have** (1.1): Layer › Stack Mode (`doc_stack_mode` in `E:core/layerops.c`): a new layer of the statistic of the picked pixel layers (or every visible one), pixel by pixel, skipping transparent pixels, as a job with progress; Photoshop applies it to a smart object, Easel to the layers themselves | Done | — |
| Rasterize (smart object) | **Have** (1.2, Layer › Smart Objects › Rasterize) | Done | — |
| Warp / Perspective Warp / Puppet Warp on a smart object (3 items) | **Missing** | Hard | P3 |
| Smart Filter › Disable Smart Filters, Delete Filter Mask, Disable Filter Mask, Blending Options…, Clear Smart Filters (5 items); `SmartFilter` in DOC | **Missing** | Hard: every edit re-runs each filter at 68040 speed. Stand-ins: Edit › Fade and the History Brush | P3 |

## 4. Blend modes

PhotoCraft has 34 modes in BLEND: `PassThrough`, the 27 Photoshop modes of `LAYER_MODES`, and the
six `PAINT_NET_MODES`. Easel has all of them: the 27 in `BM_` (`E:core/doc.h`, 0.2.1), Pass Through
for groups (`BM_PASS`, 0.3) and, in 1.1, Paint.NET's six (`BM_REFLECT` ... `BM_PDNDODGE`, with
Paint.NET's UserBlendOps formulas, checked in `E:tests/fxtest.c`). Every mode list (Layers panel,
Layer Properties, the painting tools, Fill, Fade, the layer styles) offers all of them. OpenRaster
keeps them (Krita's names, `easel:pdn-color-*` for the two Krita lacks); PSD has no keys for the
Paint.NET modes, so they are saved as Normal (Reflect, Glow, Negation, Xor) or as Photoshop's burn and
dodge.

| PhotoCraft mode (BLEND) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Pass Through (groups only) | **Have** (`BM_PASS`) | Done | — |
| Normal | **Have** | Done | — |
| Dissolve | **Have** (`blend_pixel_at`, by position) | Done | — |
| Darken | **Have** | Done | — |
| Multiply | **Have** | Done | — |
| Color Burn | **Have** | Done | — |
| Linear Burn | **Have** | Done | — |
| Darker Color | **Have** | Done | — |
| Lighten | **Have** | Done | — |
| Screen | **Have** | Done | — |
| Color Dodge | **Have** | Done | — |
| Linear Dodge (Add) | **Have** | Done | — |
| Lighter Color | **Have** | Done | — |
| Overlay | **Have** | Done | — |
| Soft Light | **Have** | Done | — |
| Hard Light | **Have** | Done | — |
| Vivid Light | **Have** | Done | — |
| Linear Light | **Have** | Done | — |
| Pin Light | **Have** | Done | — |
| Hard Mix | **Have** | Done | — |
| Difference | **Have** | Done | — |
| Exclusion | **Have** | Done | — |
| Subtract | **Have** | Done | — |
| Divide | **Have** | Done | — |
| Hue | **Have** | Done | — |
| Saturation | **Have** | Done | — |
| Color | **Have** | Done | — |
| Luminosity | **Have** | Done | — |
| Reflect (Paint.NET) | **Have** (1.1) | Done | — |
| Glow (Paint.NET) | **Have** (1.1) | Done | — |
| Negation (Paint.NET) | **Have** (1.1) | Done | — |
| Xor (Paint.NET) | **Have** (1.1) | Done | — |
| Paint.NET Color Burn | **Have** (1.1) | Done | — |
| Paint.NET Color Dodge | **Have** (1.1) | Done | — |
| Painting-only Clear and Behind (`input/blend_keys.rs`; upstream #13028 gives the Pattern Stamp Behind) | **Have** (round 3): the Brush's and Pencil's Mode lists end with Behind (paints only where the layer is see-through, under what is there) and Clear (takes the paint's coverage away), painting-only modes past the layer modes (`BM_BEHIND`, `BM_CLEAR`, `paint_px_mode` in `E:core/paint.c`; `E:tests/filestest.c`); the Eraser does what Clear does too | Done | — |
| Separators between the mode groups in the lists (upstream #5532) | **Missing**: Easel's lists are plain (the Layers panel's pop-up list draws no separators) | Easy | P3 |

## 5. Adjustments (Image › Adjustments, 22 items)

PhotoCraft's parameters are the `Adjustment` variants in ADJ and `engine/src/cmd/adjust_cmds.rs`.
Easel's are in `E:core/adjust.h`, `E:core/adjust10.h` and `E:core/adjustx.h`, with `dlg_adjust` in
`E:amiga/dialogs.c` and Color Lookup in `E:amiga/filtersx.c`, and every one previews live.
Rechecked 2026-11-18 against Easel 1.0 and PhotoCraft `c337d6e`.

| PhotoCraft item (CAT id; parameters) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Brightness/Contrast… (brightness, contrast, Use Legacy) | **Have**: Use Legacy too (`adj10_brightness_lut`) | Done | — |
| Levels… (per channel, input and output, gamma, Auto, eyedroppers, presets, CMYK and Lab spaces) | **Have**: RGB/R/G/B, histogram, gamma, output, Black/Grey/White Point from the foreground colour (`adj_levels_pick`), Auto (`adj_levels_auto`), built-in and saved presets (`adj_preset_*`). CMYK and Lab don't apply: Easel works in RGB | Done | — |
| Levels: Alt-drag shows the clipping (PhotoCraft #3693) | **Have** (round 4) as Levels' Show Clipping tick: while it is on, the preview shows the picture as the end last moved clips it (white with black or coloured pixels at the output black, black with lit ones at the output white); OK applies the levels themselves (`clip_view_rgba`, `doc_clip_view_layer`, `levels_clip_end` in `E:core/r4.c`, `E:tests/r4test.c`). MUI's sliders can't tell an Alt-drag | Done | — |
| Curves… (per channel, spline or pencil (`pencil`), eyedroppers, presets .acv) | **Partial**: four curves (RGB, R, G, B; `adj10_curves_lut`), up to eight points, eyedroppers (`adj_curves_pick`), Auto, presets; round 3 reads and writes Photoshop's .acv files (Load/Save with a name ending .acv; `curves_acv_read`, `curves_acv_write` in `E:core/adjust10.c`). The pencil (freehand) mode is missing | Easy | P3 |
| Curves: Alt+2…6 pick the dialog's channel (PhotoCraft #10475) | **Have** (round 4): Alt+2 picks the composite, Alt+3 red, Alt+4 green, Alt+5 blue in Curves and in Levels (Easel has no further channels for Alt+6; `channel_for_key`, `rawkey_digit` in `E:core/r4.c`, the graph's key handler in `E:amiga/dialogs.c`) | Done | — |
| Exposure… (exposure, offset, gamma) | **Have** | Done | — |
| Vibrance… (vibrance, saturation) | **Have** | Done | — |
| Hue/Saturation… (master plus six hue ranges, Colorize; `HueRange`) | **Have**: Master and the six ranges with range and fuzziness (`adj_hue_saturation_ranges`), Colorize, presets; dragging a range's zones on a colour bar (PhotoCraft #9272) is missing (they are sliders) | Done | — |
| Color Balance… (shadows, midtones, highlights, preserve luminosity) | **Have** | Done | — |
| Black & White… (six weights R/Y/G/C/B/M, tint, Auto) | **Have**: six weights, tint, presets (`adj_black_white6`); Auto (PhotoCraft #9301) is new in this round: the weights that spread the layer's greys widest without clipping (`bw_auto_weights` in `E:core/adjustx.c`, the dialog's Auto button) | Done | — |
| Photo Filter… (colour, density, preserve luminosity; opens at its last settings) | **Have**: the twenty named filters (`photo_filter_names`) or the foreground colour; it now opens at the last settings used (PhotoCraft #9970; `photo_last` in `E:amiga/dialogs.c`) | Done | — |
| Channel Mixer… (3 × 4 matrix, monochrome) | **Have**: `adj_channel_mixer`, and as an adjustment layer | Done | — |
| Color Lookup… (3D LUT, trilinear or tetrahedral, dither; the LUT folder listed, PhotoCraft #9827) | **Partial**: new in this round: .cube files (3D, and 1D made 3D), trilinear or tetrahedral, dither, opacity (`adj_color_lookup` in `E:core/adjustx.c`; checked against numpy by `E:tests/check_fx.py`). Missing: it works on pixels, not as an adjustment layer, and there is no built-in set of looks (the file requester starts in `PROGDIR:LUTs`) | Moderate (an adjustment layer would need the table kept with the layer) | P3 |
| Invert | **Have** | Done | — |
| Posterize… | **Have** | Done | — |
| Threshold… | **Have** | Done | — |
| Gradient Map… (stops, midpoints, smoothness, reverse, dither; PhotoCraft #9319, #10797) | **Have**: six ready gradients and, new in this round, Custom: any gradient made in the Gradient Editor (stops dragged on and off, midpoints, colours), sampled at 32 places so its midpoints come through (`gm_btn`/`gradmap_preset` in `E:amiga/dialogs.c`); Reverse; as pixels or an adjustment layer (`adj_gradient_map`). Dither is missing | Done | — |
| Selective Color… (9 ranges × CMYK, relative or absolute) | **Have**: `adj_selective_color` | Done | — |
| Shadows/Highlights… | **Have**: amounts, tones, radii, colour correction, midtone contrast (`adj_shadows_highlights`) | Done | — |
| HDR Toning… | **Have** (r3): Local Adaptation on the active layer: Radius, Strength (how firmly edges stop it: a guided filter of the log luminance), Gamma, Exposure, Detail, Shadow, Highlight, Vibrance and Saturation (`flt_hdr_toning` in `E:core/filtr3.c`, Image › Adjustments › HDR Toning…), worked out in bands of rows. Photoshop's other methods (Equalize Histogram, Exposure and Gamma, Highlight Compression) and its toning curve and presets are left out; the layer isn't flattened first | Done | — |
| Desaturate | **Have** | Done | — |
| Match Color… | **Have**: from another open picture, luminance, intensity, fade, neutralize (`adj_match_color`) | Done | — |
| Replace Color… (fuzziness, hue, saturation, lightness; Add to and Subtract from Sample, PhotoCraft #10443) | **Have**: one colour (the foreground's) with fuzziness, as pixels or an adjustment layer (`adj_replace_color`); round 3 adds Add to Sample and Subtract from Sample (the dialog's Foreground Color list; up to ten colours kept with the layer, `adj_replace_color_ex`, `replace_color_sample` in `E:core/adjust10.c`) | Done | — |
| Equalize (with a selection: the selected area only, or the whole image based on it; PhotoCraft #9604) | **Have**: with a selection, Equalize now asks which (`adj_equalize_ex` in `E:core/adjustx.c`, `dlgx_equalize`) | Done | — |
| Bitmap documents grey out adjustments and filters, all but Invert (PhotoCraft #10545) | **Have**: new in this round (the menu update in `E:amiga/main.c`) | Done | — |

## 6. Filters (the Filter menu: 75 items, plus the 47 Filter Gallery effects in GAL)

Easel's filters are in `E:core/adjust.h`, `E:core/filters2.h`, `E:core/filters10.h` and
`E:core/gallery.h`, with `dlg_filter` in `E:amiga/dialogs.c` and the gallery's dialogs in
`E:amiga/filtersx.c`; they preview live on the canvas and run as jobs with progress and Abort.
PhotoCraft's are in `crates/algo/src/` (`blur.rs`, `distort.rs`, `stylize.rs`, `render.rs`,
`pixelate.rs`, `other.rs`, `artistic.rs`, `artistic_fx.rs` and so on) and `crates/engine/src/filters`.
Rechecked 2026-11-18 against Easel 1.0 and PhotoCraft `c337d6e`.

| PhotoCraft item (CAT id) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Last Filter `filter.lastFilter` (⌘⌥F) | **Have**: `dlg_filter_again`, with the settings last used (a Filter Gallery stack too) | Done | — |
| Convert for Smart Filters | **Missing** | Hard (section 3.6) | P3 |
| Filter Gallery… (a stack of effects with preview) `filter.filterGallery` | **Partial**: new in this round: a stack of up to four effects applied in order as one step; each row's Settings shows its sliders with the whole stack previewed on the canvas (`filter_gallery` in `E:amiga/filtersx.c`, `flt_gallery` in `E:core/gallery.c`, worked out in 128-pixel tiles). Missing: thumbnails of the effects, a preview pane in the dialog, and the fifteen gallery effects of Easel 1.0 (Cutout, Film Grain, Poster Edges, Plastic Wrap, Glowing Edges, Diffuse Glow, Glass, Ocean Ripple, Spatter, Halftone Pattern, Photocopy, Mosaic Tiles, Patchwork, Stained Glass, Texturizer) can't join a stack yet: they run from their own menu items | Moderate | P3 |
| Gallery › Artistic: Colored Pencil, Cutout, Dry Brush, Film Grain, Fresco, Neon Glow, Paint Daubs, Palette Knife, Plastic Wrap, Poster Edges, Rough Pastels, Smudge Stick, Sponge, Underpainting, Watercolor (15; GAL) | **Have**: Cutout, Film Grain, Plastic Wrap and Poster Edges since 1.0 (`E:core/filters10.c`), the other eleven new in this round (`E:core/gallery.c`, following PhotoCraft's `artistic_fx.rs`), each with PhotoCraft's settings | Done | — |
| Gallery › Brush Strokes: Accented Edges, Angled Strokes, Crosshatch, Dark Strokes, Ink Outlines, Spatter, Sprayed Strokes, Sumi-e (8) | **Have**: Spatter since 1.0, the other seven new in this round (`E:core/gallery.c`) | Done | — |
| Gallery › Distort: Diffuse Glow, Glass, Ocean Ripple (3) | **Have**: `flt_diffuse_glow`, `flt_glass`, `flt_ocean_ripple` (Glass has no texture choice: frosted only) | Done | — |
| Gallery › Sketch: Bas Relief, Chalk & Charcoal, Charcoal, Chrome, Conté Crayon, Graphic Pen, Halftone Pattern, Note Paper, Photocopy, Plaster, Reticulation, Stamp, Torn Edges, Water Paper (14) | **Have**: Halftone Pattern and Photocopy since 1.0, the other twelve new in this round (`E:core/gallery.c`), in the foreground and background colours | Done | — |
| Gallery › Stylize: Glowing Edges (1) | **Have**: `flt_glowing_edges` | Done | — |
| Gallery › Texture: Craquelure, Grain, Mosaic Tiles, Patchwork, Stained Glass, Texturizer (6) | **Have**: Craquelure and Grain (ten grain types) new in this round, the rest since 1.0 | Done | — |
| Camera Raw Filter… | **Missing** | Hard: dozens of controls on float data | P3 |
| Adaptive Wide Angle… | **Missing** | Hard | P3 |
| Lens Correction… (distortion, chromatic aberration, vignette, perspective; Auto Scale, PhotoCraft #14367) | **Have**: distortion, both fringes, vignette and midpoint (`flt_lens_correction`); round 3 adds the Transform group (vertical and horizontal perspective, angle) and the scale with Auto Scale (the smallest scale that leaves no edge uncovered), all in one pass (`flt_lens_correction_ex`, `lens_auto_scale` in `E:core/filtr3.c`); the perspective is checked against Pillow's perspective transform (`E:tests/check_r3.py`) | Done | — |
| Liquify… | **Have** (r3): Filter › Liquify… opens its tool window and the brush works on the picture itself (the canvas hands its drags to it): Forward Warp, Reconstruct, Smooth, Twirl Clockwise and Counterclockwise, Pucker, Bloat, Push Left, with brush size, density, pressure and rate, and Restore All; OK applies the warp mesh as one step with progress and Abort (`liquify_stroke`, `flt_liquify` in `E:core/filtr3.c`, `liquify` in `E:amiga/r3image.c`). Freeze/Thaw Mask, Face-Aware Liquify and saved meshes are left out | Done | — |
| Vanishing Point… | **Missing** | Hard | P3 |
| Blur › Average | **Have**: `flt_average` | Done | — |
| Blur › Blur | **Have** | Done | — |
| Blur › Blur More | **Have**: `flt_blur_more` | Done | — |
| Blur › Box Blur… | **Have** | Done | — |
| Blur › Gaussian Blur… | **Have** | Done | — |
| Blur › Lens Blur… (depth map, iris, specular) | **Have**: round 3 replaces 1.0's version (radius, specular brightness and threshold) with one that has the iris (round, or three to eight blades, turned) and a depth map (the layer's transparency, its mask or an alpha channel, inverted or not) with its blur focal distance: each pixel takes the iris blur at the radius its distance from the focal depth asks, between four levels (`flt_lens_blur_ex` in `E:core/filtr3.c`, in bands); the disc is checked against numpy (`E:tests/check_r3.py`). Noise and the iris's curvature are left out | Done | — |
| Blur › Motion Blur… | **Have** | Done | — |
| Blur › Radial Blur… (spin or zoom, movable centre, quality) | **Have**: `flt_radial_blur_ex` (centre and Draft/Good/Best) | Done | — |
| Blur › Shape Blur… | **Have**: five shapes (`flt_shape_blur`); custom shapes as the kernel are missing | Done | — |
| Blur › Smart Blur… | **Have**: `flt_smart_blur` (Normal, Edge Only, Overlay Edge) | Done | — |
| Blur › Surface Blur… | **Have**: `flt_surface_blur` | Done | — |
| Blur Gallery › Field Blur… | **Have** (r3): up to four pins, each placed (per cent of the picture) and with its own blur; between them the blur follows the pins inversely to the square of the distance (`flt_field_blur` in `E:core/filtr3.c`, worked out in bands); one pin matches Pillow's Gaussian blur (`E:tests/check_r3.py`). The pins are set by numbers, not dragged on the canvas | Done | — |
| Blur Gallery › Iris Blur… | **Have**: `flt_iris_blur` (one ellipse, set by numbers) | Done | — |
| Blur Gallery › Tilt-Shift… | **Have**: `flt_tilt_shift` | Done | — |
| Blur Gallery › Path Blur… | **Have** (r3): the picture smeared along the path chosen in the Paths panel (each pixel along the direction of the path where it passes nearest: a field of tangents a quarter of the picture's size), Speed, Taper, Centered Blur (`flt_path_blur` in `E:core/filtr3.c`, in bands). Photoshop's several paths with their own end speeds, strobe flashes and Final Effect blur shapes are left out | Done | — |
| Blur Gallery › Spin Blur… | **Have**: `flt_spin_blur` (one centre) | Done | — |
| Distort › Displace… (displacement map file) | **Have**: `flt_displace` (stretch or tile, wrap or repeat) | Done | — |
| Distort › Pinch… | **Have** | Done | — |
| Distort › Polar Coordinates… | **Have**: `flt_polar_coordinates` | Done | — |
| Distort › Ripple… | **Have** | Done | — |
| Distort › Shear… | **Have**: three points (`flt_shear`) | Done | — |
| Distort › Spherize… | **Have**: Normal, Horizontal Only, Vertical Only (`flt_spherize_ex`) | Done | — |
| Distort › Twirl… | **Have** | Done | — |
| Distort › Wave… (generators, sine/triangle/square, ranges, randomize) | **Have**: `flt_wave_ex` | Done | — |
| Distort › ZigZag… | **Have**: `flt_zigzag` | Done | — |
| Noise › Add Noise… | **Have**: uniform or Gaussian, monochromatic | Done | — |
| Noise › Despeckle | **Have** | Done | — |
| Noise › Dust & Scratches… | **Have**: `flt_dust_scratches` | Done | — |
| Noise › Median… | **Have** | Done | — |
| Noise › Reduce Noise… | **Have**: strength, preserve details, colour noise (`flt_reduce_noise`); the per-channel advanced mode is missing | Done | — |
| Pixelate › Color Halftone… | **Have**: `flt_color_halftone` | Done | — |
| Pixelate › Crystallize… | **Have**: `flt_crystallize` | Done | — |
| Pixelate › Facet | **Have**: `flt_facet` | Done | — |
| Pixelate › Fragment | **Have**: `flt_fragment` | Done | — |
| Pixelate › Mezzotint… (10 types) | **Have**: `flt_mezzotint` | Done | — |
| Pixelate › Mosaic… | **Have** | Done | — |
| Pixelate › Pointillize… | **Have**: `flt_pointillize` | Done | — |
| Render › Flame… | **Have** (r3): flames along the path chosen in the Paths panel (or a line across the lower part): One Flame Along Path, Multiple Flames Along Path, Multiple Flames One Direction; length, width, interval, angle, turbulence, jag, opacity, Normal/Violent/Flat, natural colours or the foreground colour (`flt_flame` in `E:core/filtr3.c`: strands of tapered capsules added as light) | Done | — |
| Render › Picture Frame… | **Have** (r3): seven frames (Vine, Flowers, Leaves, Dots, Waves, Stars, Simple Border) round the picture: margin, size, number, arrangement, seed, the vine line on or off, flowers in the foreground colour (`flt_picture_frame` in `E:core/filtr3.c`). Easel's own designs; PhotoCraft's (and Photoshop's) 47 frames are not copied | Done | — |
| Render › Tree… | **Have** (r3): ten trees (Oak, Pine, Maple, Birch, Willow, Palm, Bush, Cherry Blossom, Bare Tree, Cypress) standing on the bottom edge: base position, size, branch height and thickness, leaves amount and size, light direction, arrangement (seed), the type's colours or custom ones (`flt_tree` in `E:core/filtr3.c`: recursive branching drawn as anti-aliased capsules and ellipses) | Done | — |
| Render › Clouds (tails compressed, not clipped: PhotoCraft #9997) | **Have**: `flt_clouds`; PhotoCraft's softer tails are not matched | Done | — |
| Render › Difference Clouds | **Have**: `flt_difference_clouds` | Done | — |
| Render › Fibers… | **Have**: `flt_fibers` | Done | — |
| Render › Lens Flare… (PhotoCraft #8705 matched Photoshop's look) | **Have**: four lens types, centre, brightness (`flt_lens_flare`); its look is Easel's own, not matched to Photoshop's | Done | — |
| Render › Lighting Effects… | **Have**: point, spot or infinite light, texture and gloss (`flt_lighting_ex`); one light, no saved styles | Done | — |
| Render › Relight (`filter.render.relight`; engine only, not in a menu) | **Missing** | Moderate | P3 |
| Sharpen › Sharpen | **Have** | Done | — |
| Sharpen › Sharpen Edges | **Have**: `flt_sharpen_edges` | Done | — |
| Sharpen › Sharpen More | **Have** | Done | — |
| Sharpen › Smart Sharpen… | **Have**: Gaussian, Lens or Motion Blur removal, noise reduction (`flt_smart_sharpen`); the shadow and highlight fades are missing | Done | — |
| Sharpen › Unsharp Mask… | **Have** | Done | — |
| Stylize › Diffuse… | **Have**: `flt_diffuse` | Done | — |
| Stylize › Emboss… | **Have** | Done | — |
| Stylize › Extrude… | **Have**: `flt_extrude` | Done | — |
| Stylize › Find Edges | **Have** (and Detect Edges: Sobel, Prewitt, Laplacian, Roberts) | Done | — |
| Stylize › Oil Paint… (stylization, cleanliness, scale, bristle detail, lighting) | **Have**: round 3 replaces 1.0's radius-and-levels version with PhotoCraft's controls: Stylization, Cleanliness, Scale, Bristle Detail, Lighting on/off, Angle and Shine (`flt_oil_paint_ex` in `E:core/filtr3.c`, ported from PhotoCraft's `algo/src/oil.rs`: a structure tensor's stroke directions, a line integral convolution along them, noise convolved the same way for the bristles, a lit relief), worked out in bands | Done | — |
| Stylize › Solarize | **Have** | Done | — |
| Stylize › Tiles… | **Have**: `flt_tiles` | Done | — |
| Stylize › Trace Contour… | **Have**: `flt_trace_contour` | Done | — |
| Stylize › Wind… | **Have**: Wind, Blast, Stagger (`flt_wind`) | Done | — |
| Video › De-Interlace… | **Have**: `flt_de_interlace` | Done | — |
| Video › NTSC Colors | **Have**, and PAL Colors (`flt_ntsc_colors`) | Done | — |
| Other › Custom… (5 × 5 kernel, scale, offset) | **Have**: the dialog is now the whole 5 × 5 grid of weights with Scale and Offset, previewed as they are typed (`custom_filter` in `E:amiga/filtersx.c`, `flt_custom`) | Done | — |
| Other › HSB/HSL | **Have**: `flt_hsb_hsl` | Done | — |
| Other › High Pass… | **Have** | Done | — |
| Other › Maximum… | **Have**: square or round (`flt_minmax`); it previews at the scale it applies (PhotoCraft #10524) | Done | — |
| Other › Minimum… | **Have**: as Maximum | Done | — |
| Other › Offset… | **Have**: transparent, background, repeat edge or wrap (`flt_offset_ex`) | Done | — |
| Other › Color to Alpha (`filter.other.colorToAlpha`; engine only) | **Have**: `flt_color_to_alpha` | Done | — |
| Live preview on the canvas, through the selection | **Have**: the dialogs' Preview tick (`E:amiga/dialogs.c` `sd_apply`) | Done | — |
| Filter dialogs: a click on the image centres the preview window (PhotoCraft #9981) | **Left out**: Easel previews on the whole canvas, not in a pane inside the dialog | Not sensible | — |

## 7. Selection features (the Select menu, 25 items, and the selection model)

Easel's selection is an 8-bit mask (`Doc::sel`, `E:core/paint.h` `sel_*`), so feathered and
anti-aliased edges already work. PhotoCraft's commands are in `engine/src/cmd/selection_cmds.rs`,
`select_extra_cmds.rs` and `smartselect_cmds.rs`, with the algorithms in `algo/src/selection.rs`,
`segment.rs` and `matting.rs`. Rechecked 2026-11-18 against Easel 1.0 and PhotoCraft `c337d6e`.

| PhotoCraft item (CAT id / source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| All `select.all` | **Have** | Done | — |
| Deselect | **Have**: `sel_deselect` keeps the selection for Reselect | Done | — |
| Reselect (⇧⌘D) | **Have**: `sel_reselect` (`E:core/selmore.c`) | Done | — |
| Inverse Selection | **Have** | Done | — |
| Convert to Shape (selection to path) | **Have**: the selection's outline traced and fitted with curves (`path_from_selection`, the Paths panel's Make Work Path) | Done | — |
| All Layers / Deselect Layers (2 items) | **Have**: new in this round (`doc_pick_all_layers`, `doc_pick_no_layers` in `E:core/selmask.c`; the Layers panel's picked layers, used by Merge Layers, Group and Align) | Done | — |
| Find Layers | **Have** (round 4): Select › Find Layers asks for a part of the name and a kind (Pixel, Adjustment, Type, Shape, Smart Object, Group, Fill); the Layers panel then shows only the layers that match, closed groups' members too, and says "Filtered" under them; an empty name and All show every layer (`layer_filter_` in `E:core/r4.c`, `E:tests/r4test.c`) | Done | — |
| Isolate Layers | **Have** (round 4): Select › Isolate Layers (ticked) shows only the active and picked layers (a group with its members) in the Layers panel, marked "Isolated", until chosen again (`layer_filter_isolate`) | Done | — |
| Color Range… (sampled colours with add and subtract, Reds…Magentas, Highlights/Midtones/Shadows, Skin Tones, Out of Gamut, localized clusters, preview modes) | **Partial**: sampled colours with + and −, the colour and tone ranges, Skin Tones, fuzziness, range, Invert, a preview (`sel_color_range_ex`, `E:core/selx.h`); round 4: colours sampled by clicking the preview (Shift adds, Alt takes away) with their places, Localized Color Clusters (each sample counts only near where it was picked, out to Range per cent of the picture; `cluster_weight` in `E:core/selx.c`) and the preview's Selection, Image, Grayscale, Black Matte, White Matte and Quick Mask views (`cr_view_pixel` in `E:core/r4.c`, `E:tests/r4test.c`). Out of Gamut is missing | Easy | P3 |
| Focus Area… | **Have**: new in this round: In-Focus Range, Image Noise Level, Soften Edge, the current layer or all layers, output to a new, added, subtracted or intersected selection (`sel_focus_area` in `E:core/selmask.c`: Laplacian energy against the picture's own 99th percentile) | Done | — |
| Subject | **Have** (r3): Select › Subject: frequency-tuned saliency (each pixel's distance in Lab from the picture's mean colour, leaning towards the middle) finds what stands out, and its box goes to Object Selection's colour clusters and smoothing (`sel_subject` in `E:core/selr3.c`). No trained model: a busy picture can pick the wrong thing | Done | — |
| Sky | **Have** (r3): Select › Sky, PhotoCraft's classical method (`segment/sky.rs`): a sky likelihood (blue, overcast or sunset colours where the picture is smooth) grown from the top rows through gradual changes, sky seen through gaps joined by its colour, specks and holes cleaned, the edge softened by a guided filter (`sel_sky` in `E:core/selr3.c`) | Done | — |
| Select and Mask… (edge detection radius, smooth, feather, contrast, shift edge, decontaminate, output to mask or layer, view modes) | **Partial**: Edge Radius (pixels in the band round the edge placed between the local colours inside and outside), Smooth, Feather, Contrast, Shift Edge, output to the selection, a layer mask or a new layer with a layer mask (`sel_refine` in `E:core/selmask.c`, `select_and_mask` in `E:amiga/filtersx.c`); round 3 adds Decontaminate Colors with its amount for the new layer (`doc_decontaminate` in `E:core/selr3.c`: the edge pixels take the colours of the nearest fully shown ones). Missing: the refine brushes, hair matting and the view modes (the preview is the marching ants) | Moderate (refine brushes need the canvas's brush hook, as Liquify's); Hard (hair matting) | P3 |
| Modify › Border… | **Have** | Done | — |
| Modify › Smooth… | **Have** | Done | — |
| Modify › Expand… | **Have** | Done | — |
| Modify › Contract… | **Have** | Done | — |
| Modify › Feather… | **Have** | Done | — |
| Grow | **Have**: `sel_grow_similar`, with the Magic Wand's tolerance | Done | — |
| Similar | **Have**: `sel_similar` | Done | — |
| Transform Selection | **Have**: the on-canvas transform handles on the selection (`ID_XFSEL`) | Done | — |
| Edit in Quick Mask Mode (Q) | **Have**: `quickmask_enter`/`quickmask_leave` (`E:core/channels.h`), a red overlay painted with the brushes | Done | — |
| Load Selection… | **Have**: `chan_load_selection` (replace, add, subtract, intersect, invert) | Done | — |
| Save Selection… | **Have**: `chan_save_selection` | Done | — |
| Float and drop the selection (`select.float`, `select.drop`) | **Have** (round 4): Select › Float cuts the selected pixels of the active pixel layer onto a "Floating Selection" layer above it (one undo step, the selection kept), to move or transform; Select › Drop merges it back down into the layer below (`sel_float`, `sel_drop` in `E:core/r4.c`, `E:tests/r4test.c`). Move with Alt still copies without floating | Done | — |
| Alpha and spot channels (Channels panel; PSD and TIFF extra channels) | **Partial**: up to 24 alpha channels in a Channels tab, saved in OpenRaster (`E:core/channels.h`); spot inks are a print feature and stay out | Easy (reading PSD/TIFF extra channels into them) | P3 |
| Marching ants and the selection modes on Shift, Alt and Shift-Alt | **Have** | Done | — |
| Anti-aliased, feathered 8-bit selection | **Have** | Done | — |


## 8. Painting and brushes

PhotoCraft's engine is `crates/paint` (BRUSH; `dynamics.rs`, `render.rs`, `mixer.rs`,
`symmetry.rs`, `tablet` crate). Easel's is `Brush` and `stroke_*` in `E:core/paint.h` and
`E:core/stroke.c`, with the tips, DPaint brushes, dynamics, presets and snapshots of
`E:core/brush.h` (0.5) and the Brush Settings window and Brushes panel of `E:amiga/brushwin.c`.
Rechecked 2026-11-18 against Easel 1.1.

| PhotoCraft feature (source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Round tip: size and hardness | **Have** (1-2500 px) | Done | — |
| Spacing (`spacing`, `spacing_enabled`) | **Have**: 1-1000 % in Brush Settings (`brushwin_apply`) | Done | — |
| Angle and roundness (elliptical tip) | **Have** (0.5) | Done | — |
| Flip X/Y of the tip | **Have** (0.5) | Done | — |
| Sampled (bitmap) tips, `TipShape::Sampled` | **Have** (0.5: `Tip`, Edit > Define Brush Preset, `tip_from_doc`) | Done | — |
| Aliased or square tip (`aliased`, `square`) | **Have** (the Pencil, and Square in Brush Settings) | Done | — |
| Shape Dynamics: size, angle and roundness jitter; controls Fade/Pressure/Tilt/Direction (`ShapeDynamics`, `Control`) | **Have**: jitters with Fade, Pressure, Direction and Initial Direction (`CTL_`); 1.2 adds Pen Tilt (`CTL_TILT`: the angle the pen leans, or upright-to-flat for size; from the tablet's tilt or Brush Pose) | Done | — |
| Scattering: both axes, count, count jitter | **Have** (0.5) | Done | — |
| Texture: pattern, scale, mode, depth | **Have** (1.2): Brush Settings › Texture: pattern, scale, depth, Multiply/Subtract/Darken/Height/Linear Height, invert, brightness, contrast (`brushx_cov` in `E:core/brushx.c`, per pixel of each dab) | Done | — |
| Dual Brush | **Have** (1.2): a second tip (round or a built-in one) scattered round each dab in Multiply, Darken or Subtract, with size, scatter and count; replays exactly (`brushx_cov`) | Done | — |
| Color Dynamics: foreground/background jitter, hue, saturation, brightness, purity | **Have** (0.5, with Apply Per Stroke) | Done | — |
| Transfer: opacity and flow jitter | **Have** (0.5) | Done | — |
| Brush Pose: tilt, rotation and pressure overrides | **Have** (1.2: `POSE_TILT`, `POSE_ROTATION`, `POSE_PRESSURE`) | Done | — |
| Noise | **Have** (1.2: fixed grain in the soft edge, the same at the same pixel) | Done | — |
| Wet Edges | **Have** (1.2: `brushx_wet`, the stroke's middle lighter than its edges) | Done | — |
| Build-up (airbrush) and its rate | **Have** (0.5, `stroke_tick`; 1.1 puts Airbrush on the options bar). *upstream* about ten dabs a second (#12169): Easel's rate is set in Brush Settings | Done | — |
| Smoothing, including Pulled String (`Smoothing`, `dynamics.rs` `Smoother`) | **Have** (0.5; 1.1: on the options bar and Alt+digit) | Done | — |
| Protect Texture | **Have** (1.2: a picked preset keeps the brush's texture, `brushx_protect_texture`) | Done | — |
| Brush presets and the Brushes panel (`BrushPreset`, `presets.rs`) | **Have** (0.5, `E:core/preset.c`) | Done | — |
| .abr brush import and export (`psd/src/abr.rs`) | **Have**: import (versions 1, 2 and 6-10, `presets_read_abr`); 1.2 writes version 2 (computed and sampled tips with their names, `abr_write`), read back by the reader | Done | — |
| Painting symmetry: Vertical, Horizontal, Dual, Diagonal, from a path (`SymmetryMode`) | **Have**: vertical, horizontal, both, radial and mandala (`SYM_`, `brush_symmetry`); 1.2 adds Diagonal (both diagonals) and Tile; round 3 adds Path: each dab mirrored across the active path where it passes nearest (across its tangent there; `SYM_PATH`, `brush_sym_path` in `E:core/brush.c`) | Done | — |
| Pen pressure, tilt and rotation (`crates/tablet`, `input/stylus.rs`) and the pressure curve; *upstream* Pressure for Size starts off (#11847) | **Have** (1.2): an input handler at priority 51 reads Intuition's `IESUBCLASS_NEWTABLET` events (`TABLETA_Pressure`, TabletZ/RangeZ, `TABLETA_AngleX/Y`, InProximity, `TAG_MORE`) into the strokes, with a pressure curve and floor in Preferences (`tabletcurve`, `tabletfloor` in Easel.prefs); Pressure for Size starts off. No tablet here: tested with synthetic tag lists. No pen rotation (Intuition's AngleZ is not used by drivers known to us) | Done | — |
| Brush outline cursor (Cursors preferences) | **Have**: drawn in COMPLEMENT (`E:amiga/canvas.c`), outline, crosshair or both (0.8 Preferences), Caps Lock for the crosshair | Done | — |
| Alt for the eyedropper with painting tools | **Have** | Done | — |
| Shift-click draws a straight line | **Have** | Done | — |
| Gradient presets and the gradient editor: stops, midpoints, opacity stops, .grd (`gradient_fill_cmds.rs`, DOC `Fill::Gradient`) | **Have**: all of it (0.5); 1.2 reads Photoshop .grd, version 5 descriptors (RGB, HSB, grey, CMYK and Lab stops, foreground and background stops, opacity stops) and version 3 (`grd_read` in `E:core/brushx.c`; Edit › Presets › Load Gradients) | Done | — |
| Pattern library: built-ins, Define Pattern, .pat (`pattern_cmds.rs`) | **Have**: built-ins, Define Pattern, IFF pattern sets (0.5); 1.2 reads Photoshop .pat (grey, indexed and RGB, raw or PackBits, with transparency) and writes it (`pat_read`, `pat_write`; Edit › Presets) | Done | — |
| Colour picker dialog: HSB/RGB/Lab/CMYK/hex, web colours only, sampling the screen (`docs/color-picker.md`) | **Have**: HSB, RGB, Lab and hex (0.5, `E:amiga/colorpick.c`); round 3 adds CMYK fields (the plain device conversion, `rgb_to_cmyk100` in `E:core/colr3.c`) and Only Web Colors (the 216 web-safe colours, `web_safe`); the screen is sampled by dragging the Eyedropper outside the picture (1.1, `E:amiga/screenpick.c`) rather than from the picker itself | Done | — |
| Deterministic, replayable strokes (`BrushSettings::seed`) | **Have** (0.5: `BrushDynamics.seed`, counter-based `brush_hash`) | Done | — |
| *upstream* Painting tools: Shift+Alt+click adds a colour sampler (#12245) | **Have** (r3): with a painting tool, Shift-Alt-click places a colour sampler (up to ten) (dragged into place before letting go), as the Color Sampler tool does (`press` in `E:amiga/canvas.c`) | Done | — |
| *upstream* Brush: the reference's alerts when it can't paint (#12254) | **Have**: locked, hidden, no pixels, no snapshot or pattern (`press`, `E:amiga/canvas.c`) | Done | — |
| DPaint stencil and grid snap for painting (Easel extra, section 21) | **Have** (1.1: `Brush.stencil`, `grid_snap_paint`) | Done | — |

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
| Baseline shift, horizontal and vertical scale | **Have**: baseline shift per run; round 3 adds horizontal and vertical scale per run (the Character dialog; `hscale`, `vscale` in `TextRun`, kept in OpenRaster), drawn through the glyphs' outlines (and Amiga bitmap fonts scaled) | Done | — |
| Underline and strikethrough | **Have** (0.9) | Done | — |
| All Caps, Small Caps, superscript and subscript (`Caps`, `FontBaseline`) | **Have** (1.2): the Character dialog's Case (All Caps; Small Caps from the font's `smcp`, else capitals at 75%) and Position (superscript and subscript at 58.3% of the size, 33.3% up or down, Photoshop's defaults), per glyph in the layout (`TO_` in `E:core/type.h`, `text_upper`); kept in ORA | Done | — |
| Colour per character run (`TextRun`) | **Have** (0.9) | Done | — |
| Alignment Left/Center/Right | **Have** | Done | — |
| Justify Left/Center/Right/All (`TextAlign`) | **Have** (1.2): `TA_JUSTIFY` (last line left), `TA_JUSTIFY_CENTER`, `TA_JUSTIFY_RIGHT` and `TA_JUSTIFY_ALL` | Done | — |
| Indents, space before and after, hyphenation (`ParagraphStyle`) | **Have**: first-line indent, space before and after, left and right indents; round 3 adds hyphenation of paragraph text (Paragraph › Hyphenate): a word that doesn't fit is broken at its last hyphenation point that fits, with a hyphen drawn (Liang's patterns from a Hyphen .dic file in `PROGDIR:Dict`, the interface's language's or British English: Quill's reader, `E:core/hyphen.c`) | Done | — |
| Paragraph text in a box with wrapping (`TextShape::Box`) | **Have** (0.9): drag a box with the Type tool | Done | — |
| Type on a path (`TextShape::Path`) | **Have** (r3): Layer › Type › Type on Active Path puts the type layer's text along the path chosen in the Paths panel (its first subpath; from its start, middle or end as the text is aligned; the glyphs bent to it as a warp bends them), Type off the Path takes it off; the path moves, turns and scales with the layer and is kept in OpenRaster (`path` in `TextParams`, `doc_text_on_path` in `E:core/type.c`). The caret isn't shown along the path: edit the text with the layer off the path, or through Character | Done | — |
| Area type (`TextShape::Area`) | **Have** (r3): Layer › Type › Type in Active Path flows the type layer's text inside the closed path chosen in the Paths panel: paragraph text whose every line takes the width the shape has at its height (moving down where the shape is too narrow for a word), with the paragraph settings, justification and hyphenation; Type off the Path makes it paragraph text in the shape's box (`area` in `TextParams`, `doc_text_in_area`, `area_span` in `E:core/type.c`) | Done | — |
| Type › Orientation › Horizontal / Vertical (2 items) | **Have** (0.9): Layer › Type › Horizontal / Vertical, the Vertical Type tool | Done | — |
| Type › Anti-Alias › None, Sharp, Crisp, Strong, Smooth, Windows LCD, Windows (7 items) | **Have**: None, Sharp (hinted), Smooth and, new in round 3, Crisp (a steeper edge) and Strong (heavier) (`TAA_` in `E:core/type.h`, on the Type tool's options bar); the Windows ones are **Left out** (they are Windows' own rendering) | Done | — |
| Type › OpenType › Standard Ligatures, Contextual Alternates, Discretionary Ligatures, Swash, Oldstyle, Stylistic Alternates, Titling Alternates, Ornaments, Ordinals, Fractions (10 items) | **Have**: Layer › Type's ten toggles; GSUB single, alternate, ligature, extension and, since round 3, contextual and chained contextual lookups (types 5 and 6, all three formats; `E:core/gsub.c`), so Contextual Alternates work and Fractions turn the figures either side of a slash into numerators and denominators (as HarfBuzz does); checked against fontTools' reading of Carlito (`E:tests/check_colour.py`, `E:tests/check_r3.py`). Type 8 (reverse chaining, for Arabic) isn't applied | Done | — |
| Type › Panels › Character, Paragraph, Glyphs, Character Styles, Paragraph Styles (5 items; the panels are rated in section 15) | **Partial**: Character and Paragraph are dialogs (Layer › Type); Glyphs and the style panels are missing | Easy to Moderate | P3 |
| Type › Create Work Path | **Have** (0.9): `text_to_path` | Done | — |
| Type › Convert to Shape | **Have** (1.2): Layer › Type › Convert to Shape: the type layer becomes a custom shape layer of its outlines, in place, filled with its colour; one undo step (`doc_text_to_shape` in `E:core/typex.c`) | Done | — |
| Type › Rasterize Type Layer | **Have** (0.9) | Done | — |
| Type › Convert to Paragraph Text / Convert to Point Text (2 items) | **Have** (1.1): Layer › Type, the text left where it was; to point text, a line break where each line wrapped (`text_to_paragraph`, `text_to_point` in `E:core/type.c`) | Done | — |
| Type › Warp Text… (Arc, Arc Lower and Upper, Arch, Bulge, Shell Lower and Upper, Flag, Wave, Fish, Rise, Fisheye, Inflate, Squeeze, Twist; `TextWarp`) | **Have** (0.9; 1.1 added Arc Lower and Upper, Shell Lower and Upper, Fisheye and Inflate): all 15, with bend and both distortions (`map_pt` in `E:core/type.c`). The Vertical direction is kept but drawn as horizontal | Done | — |
| Type › Font Preview Size › Small…Huge (5 items) | **Missing** | Easy | P3 |
| Type › Language Options › Default, East Asian, Middle Eastern, Middle Eastern & South Asian composer (4 items) | **Missing** | Hard: complex shaping and RTL text | P3 |
| Type › Update All Text Layers | **Have** (1.2): Layer › Type › Update All Text Layers: every type layer drawn again with the font files reopened (`doc_update_text`) | Done | — |
| Type › Replace All Missing Fonts / Resolve Missing Fonts… (2 items) | **Partial**: a missing family is substituted automatically (`E:core/fonts.h`); there's no dialog to choose the replacement | Easy | P3 |
| Type › Paste Lorem Ipsum | **Have** (1.1): Layer › Type › Paste Lorem Ipsum, into the active type layer or as a new paragraph (`lorem_ipsum` in `E:core/scripts.c`) | Done | — |
| Type › Load / Save Default Type Styles (2 items) | **Missing** | Easy | P3 |
| Type options bar: a list of preset font sizes (PhotoCraft #12718, since eb6c1ab) | **Missing**: the size is a slider | Easy | P3 |
| Type options bar: a button showing the Character and Paragraph panels (#13020) | **Have** (stale row): the Type tool's options bar has had Character and Paragraph buttons since 1.1 (`ICONBTN("Character"...)` in `E:amiga/toolbox.c`); they open the dialogs, Easel's form of those panels | Done | — |
| Cmd-. (Ctrl-.) cancels a type editing session (#12076) | **Have** (r3): Ctrl-. (and Amiga-.) throws the edit away and ends it, the layer as it was (a new one not made); Esc and Enter still finish it (`ty_key` in `E:amiga/canvas.c`) | Done | — |

## 10. Vector paths and shapes

PhotoCraft's model is VEC (`Path`, `Subpath`, `LiveShape`, `ShapeStroke`), with commands in
`engine/src/cmd/vector_cmds.rs` and `path_edit_cmds.rs`. Easel's (0.9) are Bézier paths
(`E:core/path.h`: the work path and saved paths, the Pen and selection tools, the Paths panel),
their operations (`E:core/pathops.h`) and shape layers (`E:core/shape.h`), drawn by the compositor
whenever wanted. Re-checked 2026-11-18 (Easel 1.1).

| PhotoCraft feature (CAT id / source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Bézier paths: subpaths, path operations, fill rule, work path and saved paths | **Have** (0.9) | Done | — |
| Fill Path (with a colour, pattern or history) | **Have**: Fill Path's dialog (round 3): the foreground colour, the pattern chosen for the tools or the history (the picture as opened), opacity, mode and anti-aliasing, previewed (`path_fill_source` in `E:core/pathops.c`) | Done | — |
| Stroke Path (with a painting tool, simulated pressure) | **Have** (0.9): `path_stroke` with the brush and simulated pressure | Done | — |
| Path to selection (`path.toSelection`) | **Have** (0.9): `path_to_selection` with the four operations, feather and anti-alias | Done | — |
| Selection to work path (tolerance) | **Have** (0.9): `path_from_selection` | Done | — |
| Shape layers: resolution-independent, solid/gradient/pattern fill, stroke width, dashes, align inside/centre/outside, caps, joins (`StrokeAlign`, `LineCap`) | **Have**: solid fill and stroke with opacity, width, dashes, alignment, caps, joins and miter limit (0.9); round 3 adds gradient fills (the Gradient tool's gradient, up to eight colour and opacity stops, Linear/Radial/Angle/Reflected/Diamond, angle, scale, reverse, aligned with the shape) and pattern fills (the pattern kept with the layer) in Shape Properties (`doc_set_shape_fill`, `shape_render_layer` in `E:core/shape.c`; kept in OpenRaster) | Done | — |
| Live shape properties: per-corner radii, polygon sides and star ratio, editing in Properties | **Have**: Shape Properties edits the corner radius, now one per corner (round 3: `radii` in `ShapeParams`, `shape_rect_path4`), polygon sides, star indent and angle, line arrowheads | Done | — |
| Layer › Combine Shapes › Unite Shapes, Subtract Front Shape, Intersect Shape Areas, Exclude Overlapping Shapes, Merge Shape Components (5 items) | **Have** (r3): Layer › Combine Shapes › Unite Shapes, Subtract Front Shape, Intersect Shape Areas, Exclude Overlapping Shapes (the picked shape layers into the lowest, one step) and Merge Shape Components (a layer's subpaths resolved into one outline): the fills drawn into a mask up to four times finer than the canvas, combined and traced back into curves (`doc_combine_shapes` in `E:core/vecx.c`), within a third of a pixel | Done | — |
| Custom shape library (built-ins and Shapes panel; PhotoCraft can't read .csh either) | **Have**: the Custom Shape tool draws the active path or, new in round 3, one of sixteen built-in shapes (arrows, heart, speech bubble, check mark, cross, lightning, moon, raindrop, ring, puzzle piece, banner, shield, paw print, leaf, gear) chosen on its options bar (`shape_library_path` in `E:core/vecx.c`); there's no Shapes panel | Done | — |
| Anti-aliased pixel shapes (the Pixels mode) | **Have**: every shape tool can draw pixels (or a path) instead of a layer | Done | — |
| Snap vector tools and transforms to the pixel grid (preference) | **Have** (0.4, 1.1 in the menu): View › Snap To › Pixels (`SNAP_PIXEL` in `E:core/guides.h`) | Done | — |
| Export › Paths to Illustrator / paths to a file, with Document Bounds (#14068 since eb6c1ab) | **Have** (1.1) as SVG: File › Export › Paths to SVG writes the saved paths, the work path and every shape layer (fill, stroke, opacity), and the document's bounds as a frame (`svg_write_paths` in `E:core/svg.c`). An Illustrator .ai isn't written | Done | — |
| Shapes from an SVG drawing (`io::svg` as shape layers or a vector smart object) | **Partial** (1.1): an SVG opens as editable shape layers: paths, rectangles, circles, ellipses, lines, polylines and polygons, in groups, with transforms, fill, stroke, opacity and the viewBox (`svg_load_doc`). Text, images, gradients, CSS classes, clipping and filters are left out; there's no vector smart object | Moderate | P3 |
| Path mode for the Shape tools (#9453) | **Have** (0.9): each shape tool draws a shape layer, a path or pixels | Done | — |

## 11. Colour management, colour modes and bit depths

PhotoCraft's model is `ColorMode` in `crates/color/src/lib.rs`, with the depth as runtime data.
Image › Mode is CAT `image.mode.*`, colour management is `crates/cms`, and indexed conversion is
QNT. Easel (0.4) has RGB, Grayscale, Indexed and Bitmap documents (`MODE_` in `E:core/doc.h`,
conversion in `E:core/palette.c`), all 8 bits a channel; 1.2 adds colour management (`E:core/cms.c`:
ICC profiles, a small CMS, proofing; `E:core/colorx.c`: Assign and Convert to Profile) and CMYK and
Lab documents, kept as RGB inside and saved in their model (the reasons are in their rows). It shows
pictures on RTG screens exactly, or dithered to a colour cube on AGA and ECS (an indexed picture with
its own palette). Re-checked 2026-11-18 (Easel 1.1); round 2 work marked 1.2.

| PhotoCraft feature (CAT id / source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Mode › Bitmap (50% threshold, pattern dither, diffusion, halftone screen, custom pattern; QNT 1-bit methods) | **Have** (0.4): 50% Threshold, Pattern Dither, Diffusion Dither and Halftone Screen (`bitmap_names`); a custom pattern is missing | Done | — |
| Mode › Grayscale (a single-channel document) | **Have** (0.4): `MODE_GRAY`, saved as grey PNG, TIFF, Targa, PGM and PDF | Done | — |
| Mode › Duotone (mono, duo, tri, quadtone with curves) | **Missing** (Photoshop duotones open as their grey composite) | Moderate | P3 |
| Mode › Indexed Color: palettes Exact, System (Mac OS), System (Windows), Web, Uniform, Perceptual, Selective, Adaptive, Custom, Previous; forced colours; transparency; matte; dither None/Diffusion/Pattern/Noise with amount; preserve exact colours | **Have** (0.4): median cut, octree and Wu from the picture, the current palette, Workbench, MagicWB, AGA and web presets instead of the Mac and Windows ones, OCS 12-bit, forced colours, transparency, 12 dithers; exact when the picture has few enough colours; round 3: the matte (below). A dither amount is missing | Done | — |
| Indexed Color › Matte (#9887 since eb6c1ab): the colour transparent edges are blended against | **Have** (round 3): Indexed Color's Matte (None, Foreground, Background, White, Black, 50% Grey, Netscape Grey) and Transparency, as Photoshop's: with Transparency kept only the partly see-through pixels are matted and made opaque, without it every see-through pixel is laid over the matte (white when there is none); one undo step with the conversion (`layers_matte` in `E:core/modex.c`, `E:tests/filestest.c`) | Done | — |
| Mode › RGB Color | **Have** | Done | — |
| Mode › CMYK Color | **Have** (1.2): Image › Mode › CMYK Color. A CMYK document stays 8-bit RGB inside (`cmode` in `E:core/doc.h`): its colours are brought into the press's gamut when it is converted (through the separation and back), it is shown and edited as RGB, and it is separated again when saved, as a CMYK TIFF or a CMYK JPEG (Adobe APP14). Why: a native CMYK mode needs 5 bytes a pixel per layer and four-channel versions of every paint, blend, filter and adjustment routine, too much for a 68040 with a few MB; because every colour of the document is printable the round trip loses at most a level or two. The separation is Easel's own SWOP-like press model (medium GCR, 300% ink, 15% dot gain) or a CMYK profile loaded in View › Proof Setup; CMYK JPEG, TIFF and PSD files open through their own CMYK profile or the press, as CMYK documents. The Channels panel doesn't show C, M, Y and K | Done | — |
| Mode › Lab Color | **Have** (1.2): Image › Mode › Lab Color, designed as CMYK is: edited as RGB (8-bit Lab holds nothing a screen shows that sRGB doesn't), saved as a CIELab TIFF (D50, a* and b* signed); Lab TIFF and PSD files open as Lab documents. The L, a and b channels aren't shown separately | Done | — |
| Mode › Multichannel | **Missing** | Moderate | P3 |
| Mode › 8 Bits/Channel | **Have** | Done | — |
| Mode › 16 Bits/Channel | **Missing** (1.2 decided against it for now): 16-bit PNG, TIFF and PSD layers are cut to 8 bits on opening. A 16-bit mode doubles every layer (24 MB for one 2000 × 1500 layer, more than most 68040 machines have free with undo) and needs a 16-bit path through every routine; colour conversions are done exactly in floating point from the 8-bit values instead | Hard | P3 |
| Mode › 32 Bits/Channel (float, HDR) | **Left out**: 16 bytes a pixel and slow floats on a 68040; HDR, PFM, float TIFF and FITS are tone-mapped or scaled on opening instead | Not sensible | — |
| Mode › Color Table… (edit the palette, load and save .act) | **Have** (0.4): Image › Color Table | Done | — |
| A 1-bit TIFF (or PBM) opens in Bitmap mode (#10539 since eb6c1ab) | **Have** (1.2): a 1-bit TIFF or PBM whose pixels are all black or white opens in Bitmap mode (`E:core/loadx.c`) | Done | — |
| ICC profiles kept when opening and saving (PNG iCCP, JPEG APP2, TIFF tag 34675, PSD) | **Have** (1.2): read from and written to PNG iCCP, JPEG APP2, TIFF tag 34675, PSD resource 1039 and WebP ICCP (`E:core/iccio.c`); kept by undo, Duplicate and EaselConvert; Pillow reads them back byte for byte. Edit › Assign Profile (none, sRGB, Adobe RGB, a file) and Convert to Profile | Done | — |
| A pure-code CMS: v2/v4 matrix/TRC and CLUT profiles, 4 intents, black point compensation (`crates/cms`) | **Have** (1.2): `E:core/cms.c`: v2 and v4 matrix/TRC (curv and para), grey TRC and LUT profiles (lut8, lut16, lutAtoB/lutBtoA, XYZ or Lab PCS) of RGB, grey, CMYK and Lab; sRGB and Adobe RGB built in; the four intents (absolute through the media white) and black point compensation; matrix pairs through tables, anything else through a 33-point grid with tetrahedral interpolation. LittleCMS (through Pillow) converts within a level or two | Done | — |
| View › Proof Colors (⌘Y), computed on the GPU in PhotoCraft | **Have** (1.2): View › Proof Colors: the canvas draws through a 17-point 3D table worked out once (`cms_view_new`), so it costs one interpolation a pixel on a 68040 | Done | — |
| View › Gamut Warning (⇧⌘Y) | **Have** (1.2): colours the press (or the CMYK profile) can't print more than about 6 ΔE closely are painted grey | Done | — |
| Proof Setup › Custom, Working CMYK and the Cyan/Magenta/Yellow/Black/CMY plates (7 items) | **Have** (1.2): View › Proof Setup › Working CMYK, the Cyan, Magenta, Yellow and Black plates (each ink as grey) and Working CMY Plates; Custom is Load Working CMYK Profile (any CMYK ICC profile) or Easel's Press; the intent and black point compensation are in Edit › Color Settings | Done | — |
| Proof Setup › Legacy Macintosh RGB, Internet Standard RGB, Monitor RGB (3 items) | **Have** (1.2): gamma 1.8 (sRGB primaries), the numbers as sRGB, and the numbers sent to the screen unconverted | Done | — |
| Proof Setup › Color Blindness Protanopia and Deuteranopia (2 items) | **Have** (1.2): Machado, Oliveira and Fernandes' (2009) full-severity matrices on linear sRGB | Done | — |
| View › 32-bit Preview Options… | **Left out**: there are no 32-bit documents | Not sensible | — |
| Monitor profile for the display (`apps/photocraft/src/monitor_profile.rs`) | **Have** (round 3): Edit › Color Settings first asks for the monitor's ICC profile (kept in Easel.prefs as `monitoricc=`); the canvas's view, proofs included, goes out through it instead of sRGB or a display gamma (`ViewSpec.monitor` in `E:core/cms.h`; matrix profiles through the fast matrix path). Checked in `E:tests/filestest.c` with Adobe RGB as the monitor | Done | — |

## 12. History and undo

PhotoCraft keeps copy-on-write tiles and history commands (`engine/src/cmd/history_*`). Easel
keeps undo steps that save only the changed regions (`doc_note_layer`), within a memory budget and
a number of steps set in Preferences (0.8: `undo_budget`, `undo_levels`, older steps spilled to a
scratch drawer), and has a History panel (`history_class`, `E:amiga/panels.c`). Rechecked
2026-11-18 against Easel 1.1.

| PhotoCraft feature (source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Multi-level undo and redo, named steps | **Have** | Done | — |
| History panel: click a state to go back or forward | **Have** (`E:amiga/panels.c` `history_class`) | Done | — |
| Step Backward / Step Forward | **Have** | Done | — |
| Snapshots: new, rename, delete, select (`history.newSnapshot` and others) | **Have**: new, delete and go back to (0.8, up to eight whole copies, `DocView.hsnap`); 1.2 adds Edit › Rename Snapshot and the New Snapshot dialog | Done | — |
| New Document from a state (`history.newDocument`) | **Have** (0.8, `ID_HISTNEWDOC`) | Done | — |
| Delete a state (`history.deleteState`) | **Have** (1.1: the panel's bin with no snapshot picked, `doc_delete_state` in `E:core/toolopts.c`; linear history, so the steps after it go too) | Done | — |
| History Options: non-linear history, automatic first snapshot (`PREFS` history options); *upstream* Automatically Create First Snapshot, New Snapshot When Saving, Show New Snapshot Dialog (#3634) | **Partial** (1.2): Edit › History Options: Automatically Create First Snapshot, Automatically Create New Snapshot When Saving, Show New Snapshot Dialog by Default (as #3634; kept in Easel.prefs, `hist_opts` in `E:core/toolsx.c`); no non-linear history | Moderate (non-linear) | P3 |
| History states limit (`PREFS` `history_states`) and memory use | **Have** (0.8: Preferences' undo memory and steps, `undo_budget`, `undo_levels`, and the scratch drawer) | Done | — |
| History log: session, concise, detailed; to the metadata or a file (Preferences › History Log) | **Have** (1.2): History Options' History Log: Sessions Only, Concise or Detailed, to a text file, to File Info's description (its History section, the oldest lines dropped when full) or both (`histlog_event`, `histlog_step` in `E:core/toolsx.c`) | Done | — |
| Autosave and crash recovery (`PREFS` `autosave`, `autosave_minutes`, `recover_on_launch`; `.pcraft`) | **Have** (0.6: Edit > Autosave to the Recovery drawer, offered back at the next start) | Done | — |
| *upstream* History Brush: source selection (#4446) | **Have** (1.1: From History, a History panel snapshot as the source) | Done | — |
| *upstream* one history step per action, kept across saves (persisted single-action grouping) | **Partial**: one step per action; the history is not kept in files | Moderate | P3 |
| Edit > Fade and Toggle Last State | **Have** (0.6, `doc_fade`) | Done | — |

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
| PSD/PSB with layers, groups, masks, effects, text, smart objects, 1-32 bits, every mode (`crates/psd`) | **Partial** (0.6): 8- and 16-bit RGB and grey layers (PSD and PSB) with names, opacity, blend modes, visibility, clipping, fill opacity, locks, Blend If, masks (density, feather) and groups; 1.2: layer effects (lfx2: the nine Easel draws), the ICC profile, CMYK and Lab files as CMYK and Lab documents (flattened, through their profile); any other kind opens flattened (File › Open As can ask for the flattened composite). Type, smart objects (and their filters, such as Puppet Warp, #8140), frame animation (#8633) and CMYK/Lab layers are missing | Hard (full fidelity) | P3 |
| TIFF: every page, layered TIFF, 8/16/32f, CMYK/Lab, BigTIFF | **Have** (round 3): a layered TIFF (Photoshop's layers in tag 37724, ImageSourceData) opens with its layers, 8 or 16 bits (`tiff_read_layers` in `E:core/filesx.c`, through the PSD layer reader); a TIFF of several pages opens with a layer a page; every compression and colour space (`E:core/tiff.c`); 1.2: CMYK through the file's profile or Easel's press, CMYK and Lab files open as CMYK and Lab documents, 1-bit as Bitmap, the profile kept. 16 bits are cut to 8 (Easel's documents are 8-bit, section 11); BigTIFF is refused | Done | — |
| JPEG: 8-bit Gray/RGB/CMYK, ICC/EXIF/XMP, EXIF orientation (`algo/src/exif.rs`) | **Have**: baseline and progressive, grey, RGB and CMYK, turned upright from EXIF (0.6), File Info from XMP and the comment; opened at a reduced size when memory is short; 1.2: the ICC profile kept, CMYK through the file's profile or the press (a CMYK document) | Done | — |
| PNG: 8/16-bit, palette PNGs as Indexed, APNG first frame, sTER | **Have**: every kind (16 bits keep the high byte, an APNG shows its first frame); 1.2: a palette PNG opens Indexed with its palette (`E:core/loadx.c`), the ICC profile is kept | Done | — |
| GIF: first frame, and every frame for video layers | **Have**: the first frame when opened; every frame (with disposal, transparency and times) through File › Import Frames to Layers (1.1) | Done | — |
| BMP (palette BMPs open as Indexed) | **Have** (1.2): 1-, 4- and 8-bit palette BMPs open Indexed with their colour table | Done | — |
| WebP: lossless, lossy, animation's first frame | **Have** | Done | — |
| AVIF (PhotoCraft can't read it, FFP #463) | **Have**: dav1d, 8/10/12-bit, grids; Easel is ahead here | Done | — |
| HEIF/HEIC (`crates/heif`) | **Left out**: HEIC pictures are HEVC (H.265) streams; an HEVC decoder is a large, patent-encumbered codebase, and decoding one 12 MP photo would take minutes on a 68040 (AVIF, HEIF's AV1 form, is read through dav1d). Datatypes, where the system has an HEIF one, open them | Not sensible | — |
| OpenEXR (half and float, deep, Cryptomatte) | **Have** (1.2): scanline files, uncompressed, RLE, ZIPS, ZIP and PIZ, half, float and uint channels (R, G, B, A or Y), tone-mapped as Radiance files are (`E:core/xread2.c`); round 3: single-part tiled files (level 0 of any level mode) and the PXR24, B44 and B44A compressions, checked against files written by `E:tests/make_foreign.py`'s own encoders. DWA, subsampled, deep and multi-part files are refused (deep and Cryptomatte data have no picture to show in an 8-bit editor) | Done | — |
| Radiance HDR | **Have**: tone-mapped | Done | — |
| PBM/PGM/PPM/PAM/PFM | **Have** | Done | — |
| FITS | **Have**: scaled to 8 bits | Done | — |
| Targa | **Have** | Done | — |
| DPX | **Have** | Done | — |
| Cineon (.cin) | **Have** (1.2): 8, 10 (packed three to a word) and 16 bits, grey or RGB, the log values through the usual reference points (black 95, white 685); ImageMagick agrees | Done | — |
| DICOM (single frame, uncompressed) | **Have** (1.2): explicit and implicit little-endian, 8 or 16 bits, MONOCHROME1/2 or RGB, the rescale and the window (else the data's range); sequences skipped; compressed transfer syntaxes refused | Done | — |
| PCX (FMT `Pcx`) | **Have** | Done | — |
| XPM | **Have** | Done | — |
| DDS (BC1-BC3 in PhotoCraft) | **Have**: also BC4, BC5 and BC7 | Done | — |
| QOI | **Have** | Done | — |
| ICO | **Have**: the biggest picture (also CUR) | Done | — |
| Amiga IFF ILBM/PBM (PhotoCraft: 1-8 planes, 24-bit) | **Have**: also EHB, HAM6/8, 32-bit deep, ACBM, RGBN/RGB8, DEEP, SHAM/CTBL/PCHG, every frame of an ANIM (1.1, op 0 and 5; 1.2, ops 7 and 8, short and long) and Workbench icons; palette pictures open indexed with their cycling; Easel is far ahead | Done | — |
| Camera raw: DNG, CR2 (and sRAW/mRAW, #7122), NEF/NRW, ARW, RW2, PEF, ORF, RAF (`crates/raw`) | **Partial** (round 3): every one opens from the full-size JPEG the camera keeps in it (`E:core/camraw.c`: the largest baseline or progressive JPEG in IFD0's chain and the SubIFDs of DNG, CR2, NEF, ARW, PEF, ORF, RW2, 3FR, ERF, MEF, MOS, IIQ, KDC and DCR, Panasonic's JpgFromRaw, Fuji's RAF header, Canon's CR3 boxes), turned upright by the raw's orientation. The sensor data isn't developed: a 24 MP demosaic needs 96 MB and minutes on a 68040 | Hard (development) | P3 |
| SVG as editable shape layers or a vector smart object | **Partial** (1.1): opens as shape layers (section 10, `E:core/svg.c`); text, images and gradients are left out | Moderate | P3 |
| OpenRaster with groups and masks (`docs/ora.md`) | **Have** (0.3): groups, masks, clipping, adjustment, fill, type and shape layers | Done | — |
| Paint.NET .pdn | **Have** (round 3): PDN3 files (Paint.NET 3 to 5) with their layers: the MS-NRBF document read as plain records (nothing is run), names, visibility, opacity, blend modes (Paint.NET's own: Reflect, Glow, Negation, Xor, its Color Burn and Dodge) and BGRA pixels in gzip or raw chunks in any order (`E:core/pdn.c`); checked against files `E:tests/make_foreign.py` writes the way BinaryFormatter does, and fuzzed. No real Paint.NET files were at hand | Done | — |
| GIMP .xcf (layers, groups, masks, channels, selection) | **Have** (round 3): versions 0-25 and .xcf.gz, RGB, grey and indexed, 8/16/32-bit integer, half, float and double (linear or perceptual, made 8-bit sRGB), tiles raw, RLE or zlib; layers with offsets, names, opacity, visibility, blend modes (the 2.10 and legacy sets mapped), locks, colour tags, layer masks, nested groups (pass through), channels as alpha channels and the selection; resolution and the icc-profile parasite (`E:core/xcf.c`). ImageMagick flattens a test file as Easel composites it; fuzzed | Done | — |
| Krita .kra | **Have** (round 3): .kra and .krz: the layer tree (paint layers and groups, nested, names, offsets, opacity, visibility, blend modes, locks, collapsed, pass-through), LZF or raw tiles of 8- or 16-bit RGBA and grey, the first transparency mask as the layer mask, default pixels, resolution; other colour spaces open from mergedimage.png; vector, filter and file layers are left out (`E:core/kra.c`) | Done | — |
| Affinity .af/.afphoto/.afdesign/.afpub | **Left out**: an undocumented proprietary format (Serif publishes no description, and it changes between versions); nothing could be checked against it | Not sensible | — |
| PhotoCraft .pcraft (native, incremental) | **Left out**: PhotoCraft's own store (craft-pcraft-bundle) mirrors its document model, which isn't Easel's; OpenRaster and PSD (both read by PhotoCraft) carry Easel's layers to it | Not sensible | — |
| Metadata kept: ICC, EXIF, XMP, text and dpi (FMT caps) | **Have**: dpi, File Info (title, author, description, keywords, copyright, URL) from PNG text chunks, JPEG XMP and comment, TIFF tags, PSD and ORA (0.6), and (1.2) ICC profiles; round 3: the EXIF block of an opened JPEG, PNG or WebP is kept (`Doc.exif`) and written back into JPEG (APP1) and PNG (eXIf) files, its orientation set to 1 (Easel turns pictures upright), its pixel sizes to the picture's and its thumbnail dropped; Export As keeps it with All metadata (`E:core/filesx.c`; Pillow reads the tags back) | Done | — |
| Open As (a format forced, #14286) | **Have** (round 3): File › Open As… reads a file with the reader chosen from 30 (each flat format, TIFF and PSD with or without their layers, OpenRaster, GIMP, Krita, Paint.NET, camera raw), whatever its bytes or name say; a file that isn't one is refused with a message naming the file and the format (`codec_load_as` in `E:core/filesx.c`, `E:amiga/filesui.c`) | Done | — |

### 13.2 Write

| Format (PhotoCraft source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| PSD/PSB with layers (`crates/psd`) | **Have** (0.6): 8-bit RGB with layers, masks, groups, blend modes, File Info and the merged composite (which PhotoCraft's maximizeCompatibility choice, #10001, makes optional); 1.2: layer styles (lfx2, which psd-tools reads) and the ICC profile; round 3: PSB (version 2 lengths and row counts) when the name ends .psb (`psb_write_doc`; psd-tools reads its layers and composite) | Done | — |
| TIFF: 8/16/32f, none/LZW/ZIP, layered (`tiffCompression`) | **Have** (0.6): 8-bit RGB(A), grey, palette and 1-bit, uncompressed, PackBits, LZW or Deflate (`tiff_encode_doc`); 1.2: File › Export › TIFF asks which, the ICC profile is kept, CMYK and Lab documents are written as CMYK (with the working CMYK profile) and CIELab TIFFs; round 3: a picture with layers is written as a layered TIFF, its layers in tag 37724 as Photoshop does beside the flat picture (`tiff_encode_layered`; ImageMagick finds the layers and their places, Pillow the flat picture), so Save keeps them without asking. 16/32 bits are left out (8-bit documents) | Done | — |
| JPEG: quality 0-12 (or 1-100), keeps an opened file's quality, ICC/EXIF | **Have**: quality 1-100, JFIF dpi, File Info as XMP and comment; 1.1: Save keeps an opened JPEG's quality (`jpeg_estimate_quality`); 1.2: progressive (#13773; libjpeg decodes it to the baseline file's pixels), the ICC profile, CMYK JPEGs for CMYK documents. EXIF isn't written | Done | — |
| PNG: 8/16-bit, gray, palette for Indexed, ICC, text | **Have**: RGB(A), palette with tRNS for Indexed and Bitmap, grey (0.4), File Info text chunks and XMP (0.6), pHYs, the ICC profile (1.2); 16-bit is missing (no 16-bit documents) | Done | — |
| GIF: 256-colour palette | **Have** (0.4): the picture's palette or 256 colours; 1.1: animated, from the layers | Done | — |
| BMP (palette BMP for Indexed) | **Have** (1.2): 24-bit, 32-bit with transparency, and 1-, 4- or 8-bit palette BMPs for Indexed and Bitmap documents (`colorx_bmp_indexed`; Pillow reads them as palette pictures) | Done | — |
| WebP: lossless, and lossy VP8 | **Have**: lossless (1.1, VP8L: `webp_encode_lossless`) and (1.2) lossy: VP8 key frames with 16x16 and chroma intra prediction, the default token probabilities and no loop filter, alpha in ALPH (`webp_encode_lossy`); libwebp decodes it exactly as Easel does, within a few dB of its own encoder at the same quality | Done | — |
| AVIF (optional `avif` feature) | **Left out**: AV1 encoding takes hours on a 68040 | Not sensible | — |
| OpenEXR | **Left out**: there are no float documents | Not sensible | — |
| Radiance HDR | **Have** (1.2): RGBE with new-style run lengths, from the picture's colours as linear light (`hdr_encode` in `E:core/xread2.c`); ImageMagick reads it back | Done | — |
| PBM/PGM/PPM/PAM/PFM | **Have** (1.1): PBM for Bitmap, PGM for grey, PPM, PAM with alpha (`pnm_encode`); PFM is missing (no float data) | Done | — |
| FITS (header cards kept) | **Have** (1.1): 8-bit, one plane or three (`fits_encode`); an opened file's header cards aren't kept | Done | — |
| Targa (RLE) | **Have** (0.6): raw or RLE, true colour, grey and colour-mapped | Done | — |
| DPX (8/10/12/16 bits) | **Have** (1.1): 8 bits, or 10 bits packed (Method A), RGB or RGBA (`dpx_encode`; ImageMagick reads both); 1.2: 12 bits (Method A) and 16 bits | Done | — |
| Cineon | **Have** (1.2): 10-bit log RGB packed three to a word, the usual 95/685 reference points (`cineon_encode`); ImageMagick reads it as Easel does | Done | — |
| ICO | **Have** (1.1): every standard size from 16 to 256 up to the picture's, small ones as DIBs with a mask, 128 and 256 as PNG (`ico_encode`) | Done | — |
| QOI | **Have** (1.1): `qoi_encode` | Done | — |
| DDS (uncompressed or BC1-BC3, mipmaps) | **Have** (1.1): uncompressed BGRA, BC1 (with transparency) or BC3, with the mipmaps (`dds_encode`); BC2 is missing | Done | — |
| Amiga IFF ILBM (PhotoCraft writes 24-plane ByteRun1 with a 1-bit mask) | **Have**: 24-bit, 1-8 planes, EHB, HAM6/8, SHAM, mask plane or transparent colour, CRNG, ModeID (0.4); an IFF ANIM from the layers (1.1) | Done | — |
| OpenRaster | **Have**: Easel's own format | Done | — |
| PDF, one flattened page (`io::pdf::export`) | **Have** (1.1): one page the picture's size at its resolution, the picture lossless (Flate, with a soft mask for transparency) or as a JPEG, grey for Grayscale and Bitmap pictures, File Info as the document information (`pdf_encode`; poppler reads it exactly) | Done | — |
| .pcraft | **Left out**: as reading .pcraft above | Not sensible | — |

### 13.3 Presets and data files (read and write; FFP "Presets and data files")

| Format | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Brushes .abr: read and write | **Have** (0.5): read (versions 1, 2 and 6-10); 1.2: written (version 2, `abr_write` in `E:core/brushx.c`). Easel's own brush presets and DPaint ILBM brushes load and save | Done | — |
| Gradients .grd: read and write | **Have**: read (1.2: version 5 descriptors and version 3, `grd_read` in `E:core/brushx.c`); round 3: written (version 5, every preset, from the Gradient Editor's Save with a .grd name; `E:core/grdw.c`; psd-tools' descriptor reader takes it); GIMP .ggr gradients load and save (0.5) | Done | — |
| Patterns .pat: read and write | **Have** (1.2): read (8BPT version 1: grey, indexed and RGB, raw or PackBits) and written (RGB, raw: `pat_read`, `pat_write` in `E:core/brushx.c`); pattern sets also load and save as IFF (0.5) | Done | — |
| Swatches .aco, .act, .ase: read and write | **Have** (0.5): .aco, .act, .ase, GIMP .gpl, JASC/RIFF .pal, IFF CMAP and Easel's own, both ways | Done | — |
| LUTs .cube (read and write), .3dl and .look (read) | **Have**: .cube read and written by Color Lookup (1.1, `E:core/adjustx.c`), .3dl read (1.2, `E:core/lut3dl.c`); round 3: SpeedGrade .look read (XML in UTF-8 or UTF-16, hex floats; `E:core/lutlook.c`) | Done | — |
| Keyboard shortcuts .kys: read and write | **Missing** (Easel keeps its own in PROGDIR:Easel.keys, 0.8): Photoshop's set format isn't described anywhere Easel could check it against | Easy, once described | P3 |
| ICC profiles: read | **Have** (1.2): Assign Profile, Convert to Profile and Load Working CMYK Profile read .icc/.icm files (`icc_parse`) | Done | — |
| Actions .atn: read and write (`psd/src/atn.rs`) | **Missing**: Easel has actions (1.2, `E:core/actions.c`), but mapping Photoshop's events (hundreds, with their descriptors) to Easel's commands is a project of its own | Moderate | P3 |
| Styles .asl: read and write | **Missing** | Moderate | P3 |
| Contours .shc: read and write | **Missing**: Easel's layer styles have no contours yet (section 3), so there is nothing to put them in | Easy, after contours | P3 |

## 14. Import and export options

PhotoCraft's options are in `crates/codecs` (`EncodeOptions`), `engine/src/cmd/file_cmds.rs`,
`export_preset_cmds.rs` and PREFS (export, file handling). Easel's are in `dlg_export` in
`E:amiga/dialogs.c`, the Save and Export logic in `E:amiga/main.c`, the Amiga ILBM options in
`E:amiga/modedlg.c` and EaselConvert's arguments (`E:amiga/convert.c`). Re-checked 2026-11-18
(Easel 1.1).

| PhotoCraft option (source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| JPEG options: quality, keeping an opened file's quality (FFP #2399), remembering the last quality and format (#9661) | **Have** (1.1): the quality slider remembers the last quality; Save keeps an opened JPEG's quality, estimated from its tables | Done | — |
| JPEG options: the size the file will be (#9952), progressive at every quality (#13773) | **Have** (1.2): JPEG Options has a Progressive tick (written at every quality); round 3: it shows the size the file will be, worked out at each change from a mosaic of 36 blocks taken from all over the picture (exact for pictures under 160,000 pixels, within a fifth in `E:tests/filestest.c`; `jpeg_size_estimate` in `E:core/filesx.c`) | Done | — |
| PNG options: depth, palette, metadata | **Partial**: chosen by the picture's mode (palette for Indexed and Bitmap, grey for Grayscale), File Info always kept; there are no choices | Easy | P3 |
| WebP options: lossless, quality | **Have** (1.2): File › Export › WebP asks lossless or lossy, and lossy's quality | Done | — |
| TIFF options: compression none/LZW/ZIP, layers | **Have** (1.2): File › Export › TIFF asks for LZW, Deflate, PackBits or none (Save uses LZW); round 3: a picture with layers keeps them (layered TIFF, section 13.2) | Done | — |
| DDS options: BC1/BC2/BC3, mipmaps | **Have** (1.1): BC3, BC1 or uncompressed, each with mipmaps (BC2 missing) | Done | — |
| DPX bits (8/10/12/16) | **Have** (1.2): 8, 10, 12 or 16, asked on export | Done | — |
| PDF: lossless or JPEG (PhotoCraft's export settings) | **Have** (1.1): asked on export | Done | — |
| IFF options: PhotoCraft has none (24-plane only) | **Have**: 24-bit, 1-8 planes, EHB, HAM6/8, SHAM, mask or transparent colour, cycling, ModeID, compression (`IlbmOpts`). Easel is ahead; see section 21 | Done | — |
| Export As: scale, resample method, canvas size, transparency or matte | **Have** (1.2): File › Export › Export As: the scale with any of Image Size's six methods, the canvas size (about the centre), a matte (none, white, black, the foreground or background colour), the metadata and sRGB (`E:core/exportx.c`); the format by the name's extension | Done | — |
| Export metadata choice: none, copyright, all (PREFS `metadata`; the CLI's `--metadata`, #9975) | **Have** (1.2): Export As keeps all of the File Info, only the copyright, or none | Done | — |
| Convert to sRGB on export | **Have** (1.2): Export As's Convert to sRGB (through the CMS, the copy tagged sRGB) | Done | — |
| Warnings about what a format loses (`fidelity_warnings`: depth reduced, CMYK converted, layers flattened) | **Have**: saving layers to a flat format offers ORA or flattening | Done | — |
| Quick Export format and location preferences | **Have** (round 3): Preferences › Export: Quick Export's format (PNG, JPEG, GIF, WebP, TIFF; the menu item says which), where it writes (beside the picture, the last drawer exported to, a drawer of its own, or ask each time), the metadata kept and Convert to sRGB (through Export As's `exportx_prepare`) | Done | — |
| Export dialogs start in the last exported folder (#10448) | **Have** (round 3): Export As, File › Export's formats, Layers to Files, the animations and Paths to SVG start in the drawer the last export went to (the picture's own drawer before the first) | Done | — |
| Place options: resize while placing, always create smart objects; Skip Transform when Placing (#9075) | **Have** (0.6): Place goes on into Free Transform to size it; 1.1: Skip Transform when Placing (Preferences › General); round 3: Always create Smart Objects when placing (the placed layer made a smart object, `doc_smart_convert`) | Done | — |
| Paste or open from the system clipboard as a document (PREFS `export_clipboard`) | **Have** (0.6): Edit › Paste from Amiga Clipboard (ILBM through iffparse) | Done | — |
| Drag and drop files onto the window | **Have** (0.6): AppWindow and AppIcon (`E:amiga/appdrop.c`) | Done | — |
| Opening a JPEG at a reduced size when memory is short (Easel only) | **Have** (Easel extra) | Done | — |
| Frames of an animation: how many, and the memory they may take (Easel only) | **Have** (1.1): Import Frames to Layers takes up to 100 frames within three eighths of the free memory and says when it stopped | Done | — |

## 15. Panels

PhotoCraft's panels are the Window menu items in CAT (`window.panel.*`), with their UI in
`crates/ui-egui/src/panels/`. Rechecked 2026-11-18: Easel's right-hand column has two tabbed groups,
Color, Swatches, Brushes and History above, Layers, Channels, Paths, Navigator and Info below
(`head_color` and `head_layers` in `E:amiga/main.c`; `E:amiga/panels.c`, `E:amiga/panels2.c`,
`E:amiga/swatchwin.c`, `E:amiga/brushwin.c`), besides the toolbox, the options bar, the document tabs,
the status bar and the Palette, Brush Settings and DPaint Brush windows.

| PhotoCraft panel (CAT id) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Tools `window.panel.tools` | **Have**: flyouts, two columns on short screens | Done | — |
| Options | **Have** | Done | — |
| Color (F6): RGB/HSB/CMYK/Lab/Gray sliders, ramp, gamut alarm | **Partial**: saturation and brightness square, hue strip, RGB bars, hex and swatches (the colour picker has HSB, RGB, Lab and hex); HSB/Lab/CMYK sliders in the panel and a choice of ramp are missing | Easy | P3 |
| Swatches: add, delete, groups, import and export | **Have**: the Swatches tab (0.5): click, Alt-click, add, delete; .gpl, .aco, .act, .ase, IFF CMAP and Easel's own files (groups are missing) | Done | — |
| Layers (F7): fill, mask thumbnails, effect rows, group tree, filter by kind (`layer_filter_ui.rs`), colour labels, multi-select, clipping indicators, smart filter rows | **Partial**: mode, opacity, fill, the four locks, eye, thumbnail, mask thumbnail, group tree with collapse, clipping arrows, multi-select, drag to reorder and into groups, and (1.1) colour labels and an "fx" mark for a layer with a style; per-effect rows, filtering by kind and smart filter rows are missing | Easy | P3 |
| Select Pixels on a layer thumbnail's right-click menu (upstream #12450) | **Have** (1.1): a layer thumbnail's right-click menu starts with Layer Transparency (Select Pixels), then Add Layer Mask and the layer kind's own command (`E:amiga/ctxmenu.c` `thumb_menu`, `E:tests/ctxtest.c`) | Done | — |
| History | **Have** (with snapshots, 0.8) | Done | — |
| Channels: composite and colour channels, alpha and spot channels | **Partial**: the Channels tab lists the alpha channels (saved selections) and the Quick Mask (0.7); the composite and red, green and blue channels and spot channels are missing | Easy | P3 |
| Paths | **Have**: the Paths tab (0.9) | Done | — |
| Actions (⌥F9) | **Have** (1.2) as a window: Window › Actions lists the actions by set with their steps; New, Duplicate, Rename, Set, Delete; Record (each menu command chosen becomes a step), Stop Recording, Play, Play from Step, Play Action Again; steps turned on and off, moved, deleted, Insert Stop with a message; Load and Save `.actions` files, kept in `PROGDIR:Easel.actions` (`E:core/actions.c`, `E:amiga/panelsx.c`). A plain step recorder, no scripting language | Done | — |
| Adjustments: one-click adjustment layers and presets | **Missing** as a panel; Layer › New Adjustment Layer makes them | Easy | P3 |
| Properties: adjustment settings, mask density and feather, live shape, type | **Partial**: as dialogs (double-click an adjustment, fill or shape layer; Layer Properties for mask density and feather); no panel | Easy to Moderate | P3 |
| Brush Settings (F5): the dynamics sections | **Have**: Window › Brush Settings (0.5) | Done | — |
| Brushes (presets) | **Have**: the Brushes tab (0.5) | Done | — |
| Character | **Partial**: Layer › Type › Character… is a dialog (0.9), not a panel | Easy | P3 |
| Paragraph | **Partial**: Layer › Type › Paragraph… (a dialog) | Easy | P3 |
| Character Styles | **Missing** | Easy | P3 |
| Paragraph Styles | **Missing** | Easy | P3 |
| Glyphs | **Missing** | Easy | P3 |
| Clone Source: five sources, offset, scale, rotation, overlay | **Missing** | Easy | P3 |
| Gradients | **Partial**: the Gradient tool's preset list and the Gradient Editor (0.5), not a panel | Easy | P3 |
| Patterns | **Partial**: Edit › Define Pattern and the pattern chosen in the options bar; no panel | Easy | P3 |
| Histogram: live, with mean, standard deviation and median | **Have** (1.2) in the Info tab: the histogram of the channel chosen in Window › Histogram (Luminosity, Red, Green, Blue), within the selection, with mean, standard deviation, median and pixel count (marked * when sampled from up to 96 rows; `histo_stats` in `E:core/actions.c`) | Done | — |
| Info (F8): readouts, colour samplers, selection size, document info | **Have**: the Info tab (0.6): position, colour in RGB, HSB and hex, picture size and mode, selection size, the Color Sampler marks (0.8) | Done | — |
| Navigator: a thumbnail with the view box | **Have**: the Navigator tab (0.6) | Done | — |
| Layer Comps | **Have** (1.2) as a window: Window › Layer Comps: New (visibility, appearance or both), Apply, Update, Delete, Previous, Next, Export to Files; each Apply an undo step (`comp_capture`, `comp_apply`). Position isn't kept: Easel's layers are canvas-sized, so a layer's position is its pixels; comps aren't saved with the picture | Done | — |
| Measurement Log | **Have** (round 3): Window › Measurement Log: Record Measurements adds the Ruler's line (length, angle), the selection (area, perimeter, width, height, mean, minimum and maximum grey of the merged picture, integrated density) or the Count tool's markers; rows deleted, cleared and exported as tab-separated text (`E:core/measure.c`, `E:amiga/filesui.c`) | Done | — |
| Modifier Keys (on-screen Shift, Alt and Ctrl for pens) | **Have** (round 3): Window › Modifier Keys: Shift, Alt and Ctrl buttons held down until pressed again, which the canvas takes with the keyboard's own for clicks and drags (`modkeys_qual`, `E:amiga/filesui.c`; interface only, not natively testable) | Done | — |
| Notes | **Have** (round 3): Window › Notes lists the picture's notes (the Note tool's, 1.2) with their authors; Edit Note, Delete Note, Delete All and Export Notes (a text file) | Done | — |
| Shapes | **Partial**: the Custom Shape tool's list (0.9), not a panel | Easy | P3 |
| Styles | **Have** (1.2) as a window: Window › Styles: six built-in styles and saved ones (New Style from the active layer), Apply (or double-click) to the picked layers, Rename, Delete, Load, Save, Reset; kept in `PROGDIR:Easel.styles` (`stylelib_` in `E:core/actions.c`) | Done | — |
| Timeline (video and frame animation) | **Have** (round 3) for frame animation: Window › Timeline lists the frames (the top-level layers, with their times) and the animation's length; picking a frame shows it; first, previous, Play, next, last; New Frame, Delete Frame, Tween (frames fading from one to the next), Frame Time (this frame or all) and Reverse Frames, each one undo step (`E:core/timeline.c`); onion skins and Play Frames (1.2) and the GIF and IFF ANIM writers (1.1) work on the same frames. Video layers are left out (no video decoding on a 68040) | Done | — |
| Tool Presets | **Missing** | Easy | P3 |
| Docking, floating, collapsing to icons, tabbed groups (`panel_docking.rs`) | **Partial**: a fixed right-hand column of two tabbed groups; Tab and Shift-Tab hide them | Moderate: MUI can't dock windows, but panels can be shown, hidden and grouped | P3 |
| Status bar: zoom, size, profile, timing | **Have** | Done | — |
| Document tabs | **Have** | Done | — |

## 16. Shortcuts

PhotoCraft's single keys are `Tool::key()` in TOOL; its menu shortcuts are in CAT; the special keys
are in `crates/ui-egui/src/input/` (`hold_keys.rs`, `spring_tools.rs`, `blend_keys.rs`,
`page_keys.rs`, `layout_keys.rs`). Easel's are in the README tables, `keys` in `E:amiga/canvas.c`
and Edit > Keyboard Shortcuts (0.8, `E:amiga/keys.c`). Rechecked 2026-11-18 against Easel 1.1.

| PhotoCraft shortcut (source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Tool letters V M L W C I B E G T H Z O S U (TOOL) | **Have**. Easel adds R for Blur/Sharpen/Smudge, which PhotoCraft gives to Rotate View | Done | — |
| Tool letters K (Frame), J (healing), Y (History Brush), P (Pen), A (Path Selection), R (Rotate View) | **Have** (1.2, stale here): K is the Frame tool and R the Rotate View tool (`tool_keys` in `E:amiga/main.c`), with J, Y, P and A (0.5-1.0) | Done | — |
| ⇧+letter cycles a tool group | **Have** | Done | — |
| Spring-loaded tool keys: hold a letter for a temporary tool (`spring_tools.rs`) | **Have** (0.8, `spring_code` in `E:amiga/canvas.c`) | Done | — |
| Space for the Hand while held | **Have** | Done | — |
| ⌘Space and ⌥Space to zoom while held (`hold_keys.rs`) | **Have** (1.1: Ctrl-Space and Alt-Space, `press`) | Done | — |
| ⌘ for the Move tool while held | **Have** (1.1: Ctrl with the painting, filling and sampling tools) | Done | — |
| ⌥ for the eyedropper with painting tools | **Have** | Done | — |
| `~` to erase with the Brush | **Have** (1.1: hold the ` key, `Data.tilde`) | Done | — |
| `[` `]` for size, ⇧`[` ⇧`]` for hardness, and keyboard-layout independence (`layout_keys.rs`) | **Have**: MUI keys follow the Amiga keymap | Done | — |
| 1-0 for opacity (and quick two-digit typing) | **Have** (1.3): digits for opacity, two typed within a third of a second set it exactly ("4", "5": 45 %; "00": the minimum), Shift+digit Flow and Alt+digit Smoothing (`opacity_key`, `E:core/menu3.c`, `E:tests/menu3test.c`) | Done | — |
| Blend mode keys: ⇧⌥ plus a letter, ⇧+ and ⇧- to step (`blend_keys.rs`) | **Have** (1.3): Shift-Alt and a letter (0.6), and Shift + and Shift - step through the modes in the Layers panel's order: the tool's, or the layer's with the Move tool (`blend_step`, `E:core/menu3.c`, `E:tests/menu3test.c`) | Done | — |
| Page Up/Down, Home and End scroll the canvas (`page_keys.rs`) | **Have** (1.1: on keyboards that have them; Ctrl with Page Up/Down scrolls across) | Done | — |
| D (default colours) and X (swap) | **Have** | Done | — |
| Q (Quick Mask) | **Have** (0.7) | Done | — |
| F (screen modes) | **Have** (1.1: a window, Easel's own screen, full screen, as View > Screen Mode) | Done | — |
| Tab hides the panels | **Have** (0.6; Shift-Tab the right-hand panels) | Done | — |
| Menu shortcuts that Easel shares with CAT (⌘Z, ⇧⌘Z/⌘Y, X/C/V, ⇧⌘C, ⇧⌘V, A, D, ⇧⌘I, L, M, U, B, I, ⇧⌘U, ⇧⌘L, J, ⇧⌘J, E, ⇧⌘E, ⇧⌘N, T, ⌥⌘I, ⌥⌘C, ⌘[ ⌘], N, O, S, ⇧⌘S, W, 0, 1), with Ctrl in place of ⌘ | **Have** (README table) | Done | — |
| Menu shortcuts Easel lacked: ⇧⌘D, ⌥⌘Z, ⇧⌘F, ⌥⌘G, ⇧⌘B, ⌥⇧⌘L, ⌥⇧⌘B, ⌘R, ⌘', ⌘;, ⇧⌘;, ⌘H, ⌥⌘F, ⇧F5, ⇧F6, ⌥⌘S, F12, ⌥⌘W, ⇧⌘T, ⇧⌘[ ⇧⌘], ⌘= / ⌘- | **Have** (1.1, the Ctrl block of `keys`; ⌘K is Preferences) | Done | — |
| Menu shortcuts still missing: ⌘Y and ⇧⌘Y (proof colours and gamut warning, section 11), ⌥⌘; (Lock Guides), ⌥⌘A (Select All Layers), F5-F8 (the panels) | **Partial** (1.3): Shift-Ctrl-Y Gamut Warning, Ctrl-Alt-; Lock Guides and Ctrl-Alt-A Select All Layers (`E:amiga/canvas.c`); Ctrl-Y stays Redo (as Amiga-Y) and F5-F8 aren't panels' keys | Easy | P3 |
| Amiga-key menu shortcuts (Right Amiga) | **Have** (Easel extra, in the NewMenu table; changeable in Edit > Keyboard Shortcuts) | Done | — |
| Arrow keys nudge by 1, ⇧ by 10 | **Have** | Done | — |
| Return and Esc commit and cancel | **Have** | Done | — |
| ⌥/⌘ Backspace fill with foreground or background, Del clears | **Have** | Done | — |
| *upstream* Curves: Alt+2..6 pick the channel (#10475); Image Size: Alt+1..8 pick the resampling (#15054); Ctrl+. cancels typing (#12076) | **Partial** (round 4): Curves' (and Levels') Alt+2 ... Alt+5 pick the channel (section 5), and Ctrl-. throws a type edit away (round 3); Image Size's Alt+1..8 for the resampling are missing (the dialog has no key handler) | Easy | P3 |
| *upstream* Keyboard Shortcuts records keys the menus use (#10451); tool keys keep their last tool across launches (#11322) | **Have** (1.3): Edit > Keyboard Shortcuts (0.8) warns when an Amiga key is on two commands, and the tool each letter chose last is kept in Easel.prefs across launches (`toolkeys_format`, `toolkeys_parse`, `E:core/menu3.c`, `E:tests/menu3test.c`) | Done | — |

## 17. Preferences (Edit › Preferences, 18 items; PREFS)

Rechecked 2026-11-18. Edit › Preferences (0.8, `E:amiga/prefs.c`) has the pages General, Interface,
Tools, File Handling, Units & Rulers, Guides & Grid and Scratch Drawer, kept in `PROGDIR:Easel.prefs`
with the dark interface, the public screen, the screen mode and the last drawer. Easel also takes
`PUBSCREEN=` and `DEBUG=` as arguments or tool types.

| PhotoCraft preference page (CAT id; key fields) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Settings/General: colour picker, image interpolation, beep when done, export clipboard, resize during place, zoom with the scroll wheel, animated zoom | **Partial**: autosave interval, icons with pictures, help bubbles (and the history and undo memory below), Skip Transform when Placing (1.1); round 3: Beep when long jobs are done (over five seconds) and Always create Smart Objects when placing; the picker, interpolation, clipboard export and wheel settings are missing | Easy | P3 |
| Skip Transform when Placing (upstream #9075) | **Have** (1.1): General page; Place as Layer then doesn't go on into Free Transform (`prefs.skip_place_xf`) | Done | — |
| AI Integrations: agent control and the control port (`Integrations`) | **Excluded** by request: this is the external-control role ARexx would fill | — | — |
| Interface: theme dark or light (and Pro, Studio, Classic), canvas colour, UI scale, language, font size, tooltips, animations (upstream #9480 removed the animations setting) | **Partial**: Dark interface, Large interface text and help bubbles; the language (1.1): Locale prefs' choice or one of English, Deutsch, Français, Español, Italiano (Preferences › Interface, kept as `language=` in Easel.prefs; see Localization below); round 4: Canvas colour (the theme's, black, dark, medium or light grey, white; `board` in `E:amiga/canvas.c`); the interface's scale is missing | Easy | P3 |
| Workspace: auto-collapse panels, open as tabs, narrow options bar | **Missing** | Easy | P3 |
| Tools: Shift for tool switching, zoom to the clicked point, flick panning, snap to the pixel grid, show transform values, overscroll, right-click with painting tools, tablet pressure and curve, nudge bundling | **Partial**: painting cursor, Caps Lock precise cross, show transform values | Easy | P3 |
| History Log | **Have** (1.2): the General page's History log and Log file are the same setting as Edit › History Options' History Log (section on History above: `hist_opts` in `E:core/toolsx.c`, each step to `PROGDIR:Easel-History.log` at first) | Done | — |
| File Handling: previews, lowercase extensions, autosave interval, recovery, ignore the EXIF profile, layered TIFF and PSD compatibility prompts, number of recent files, LUT folder | **Partial**: default save format, JPEG quality, ask before flattening, recent files listed, recovery drawer (autosave on General) | Easy | P3 |
| Export: Quick Export format and location, JPEG and WebP defaults, metadata, convert to sRGB | **Have** (round 3): the Export page: Quick Export's format, where it writes and its drawer, the metadata it keeps, Convert to sRGB and lossy WebP's quality (also File › Export › WebP's starting value); JPEG's quality stays on File Handling | Done | — |
| Performance: memory use, history states, cache levels and tile size, GPU options | **Have**: history states and undo memory (General); cache levels, tiles and GPU options don't apply to Easel | Done | — |
| Effect Cache (upstream #10921) | **Left out**: Easel always keeps each styled layer's drawn effects (core/fx.c); there is nothing to set | Not needed | — |
| Scratch Disks | **Have**: the Scratch Drawer page (0.8): old undo steps go to a drawer, up to a size | Done | — |
| Cursors: painting cursor standard/precise/tip/full size, crosshair, other cursors | **Partial**: brush outline, crosshair or both, Caps Lock precise | Easy | P3 |
| Transparency & Gamut: checker size and colours, gamut warning colour | **Have** (1.2, round 4): the Transparency & Gamut page: checkerboard size (none, small, medium, large) and colours (light, medium, dark, five tints or two custom colours, `checker_at` in `E:core/viewx.c`); round 4 adds the gamut warning's colour and opacity for View › Gamut Warning (`gamut_warn_mix` in `E:core/cms.c`, `E:tests/r4test.c`) | Done | — |
| Units & Rulers: units, column size, new-document resolution, point size | **Have** (1.2): units, new picture resolution, and now screen resolution (for Print Size), column and gutter widths (New Guide Layout starts with the gutter) and point size (PostScript or traditional) | Done | — |
| Guides, Grid & Slices: colours and styles, gridline every N, subdivisions | **Have** (1.2): guide and grid colours, gridline every, subdivisions, and grid and guide styles (lines, dashed lines, dots); slices don't exist in Easel | Done | — |
| Plug-ins: additional plug-ins folder | **Left out**: Easel has no plug-ins (section 19) | Not sensible | — |
| Type: smart quotes, font preview, missing-glyph protection | **Partial** (round 4): a Type page with Use Smart Quotes (quotes typed with the Type tool become typographer's quotes after a space, bracket or dash and closing ones elsewhere; `smart_quote_for`, `smart_quotes` in `E:core/r4.c`, `E:tests/r4test.c`); the font preview and missing-glyph protection are missing | Easy | P3 |
| Enhanced Controls | **Missing** | Easy | P3 |
| Raw Defaults | **Left out**: Easel opens a raw file's own JPEG (section 13) and develops nothing, so there is nothing to default | Not sensible | — |

## 18. Automation and actions

PhotoCraft's automation is `crates/automation`, `engine/src/cmd/actions_*`, `atn_cmds.rs`,
`apps/photocraft-cli` and `book/src/automation/`. Easel has EaselConvert, a Shell command for
batch conversion (0.6, `E:amiga/convert.c` with `E:core/batch.c`), and no actions. Re-checked
2026-11-18 (Easel 1.1).

| PhotoCraft feature (source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Actions: record, play, stop, insert, conditional steps, dialog toggles, sets, colours (`actions.*`) | **Partial** (1.2): Window › Actions records menu commands as steps and plays them back, with sets, Insert Stop, steps turned off, Play from Step, Load and Save, and File › Automate › Batch (section 15). Dialog toggles (a dialog's settings recorded and replayed without it), conditional steps and colours shown in the list are missing: every dialog shows as its step plays | Moderate: dialogs need their settings as parameters | P2 |
| One registry of 500+ commands with JSON parameters, used by the UI, CLI and agents | **Partial** (1.2): the menu table is the registry Edit › Search, Keyboard Shortcuts and the action recorder all use (each command by its ID and name); there are no parameter blocks, so commands with dialogs can't be run without them | Moderate | P2 |
| Command-line front end: `photocraft-cli convert/info/run/batch/droplet/commands` | **Partial**: EaselConvert converts and resizes any picture Easel reads (datatypes as a last resort) to every format Easel writes, reduces colours and writes icons, in the ADPro tradition, and (1.2) INFO describes each picture (format, size, layers, mode, resolution, aspect, screen mode, File Info; `picture_info` in `E:core/picinfo.c`); `run` and actions from the Shell are missing | Easy | P3 |
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
| Filter › Plug-ins submenu and Install Plug-in… | **Left out**: there is no plug-in interface to install into: WebAssembly is left out above, and native libraries would need an ABI kept stable across Easel's releases for the few third-party filters an Amiga editor could expect; formats come through datatypes instead | Not sensible | — |
| A generated parameter dialog with live preview | **Left out**: it goes with the plug-ins (Easel's own filters already share one slider dialog with a preview, `E:amiga/dialogs.c`) | Not sensible | — |
| Hosting .8BF, UXP or CEP (PhotoCraft has none either) | **Left out**: Windows and macOS code | Not sensible | — |
| Format plug-ins | **Have** (0.6) through the Amiga's own: datatypes open any picture a datatype reads (`E:amiga/dtload.c`, EaselConvert too) | Done | — |

## 20. Anything else

Rechecked 2026-11-18.

| PhotoCraft feature (source) | Easel | Amiga feasibility | Pri |
|---|---|---|---|
| Copy-on-write sparse tiles (256²): huge canvases and cheap undo (`crates/raster`, `paint/src/tile.rs`) | **Partial**: undo saves only changed 64 × 64 tiles (and can spill to the scratch drawer), but layers are full buffers in one piece (a 2000 × 1500 layer needs 12 MB of contiguous memory) | Moderate: tiles would also end the need for one big block of memory, which matters on fragmented Amiga RAM | P2 |
| GPU compositor on wgpu (Metal, Vulkan, DX12, WebGPU) | **Left out**: no GPU. Easel composites only the rectangle shown, on the CPU, and keeps layer effects drawn (1.1) | Not sensible | — |
| Multithreaded filters | **Left out**: one CPU. The stand-in is there: long jobs show progress and can be aborted (`E:amiga/jobs.c`) | Not sensible | — |
| Background jobs with progress and cancel (`ui-egui/src/jobs_ui.rs`) | **Have**: `E:amiga/jobs.c` (0.8): a Working window after half a second, with progress and Abort; an aborted job leaves the picture as it was | Done | — |
| Recent files and the Home screen (`PREFS` `recent_files`, `show_home_recent_files`) | **Partial**: File › Open Recent (ten, 0.6); there's no Home screen | Easy | P3 |
| Context menus (`docs/context-menu-inventory.md`; upstream adds many, e.g. #12450, #13278) | **Have** (1.1): MUI context menus (`MUIA_ContextMenu`, `MUIM_ContextMenuBuild`, `E:amiga/ctxmui.c`) built from a plain-C model (`E:amiga/ctxmenu.c`, tested in `E:tests/ctxtest.c`) in PhotoCraft's `context-menu-parity.md` order. On the canvas, per tool: the selection tools' Photoshop 2026 list with a selection (Deselect, Select Inverse, Feather, Select and Mask, Save Selection, Make Work Path, Layer via Copy/Cut, New Layer, Free Transform, Transform Selection, Distort, Perspective, Fill, Stroke, Last Filter, Fade) and Select All / Reselect / Color Range / Load Selection without; Move (and Ctrl with any tool): the visible layers under the pointer; a pending Free Transform: its modes, turns, flips, Apply, Cancel; a pending crop: Crop, Cancel; painting tools: Brush Settings, DPaint Brush, Define Brush Preset, Pick Up Brush (History Brush: Take Snapshot); Gradient and Paint Bucket: the fills; Eyedropper and Color Sampler: sample size (and source); Type over a type layer (made active first): Edit Type, Character, Paragraph, Rasterize, Create Work Path, Horizontal/Vertical, Convert to Paragraph/Point Text, Warp Text; Pen and path tools: the path's commands; shape tools: Shape Properties, Free Transform, Rasterize; Hand and Zoom: Fit on Screen, Fill Screen, 100%, 200%, Zoom In, Zoom Out; Ruler: Straighten Layer, Clear. Layers panel rows (the layer clicked made active unless it is among the picked ones): Blending Options, Layer Properties, Duplicate/Delete, Group/Ungroup, Quick Export, Export As, Rasterize, the mask, Create/Release Clipping Mask, Copy/Paste/Clear Layer Style, Layer Color (submenu), Merge Down/Layers/Group, Merge Visible, Flatten; a layer thumbnail and a mask thumbnail have their own menus; Channels and Paths rows; document tabs (Close, Close Others, Close All, Duplicate, File Info). Where the tool has no menu of its own the canvas shows the general one 1.2 added (`E:amiga/menux.c`: Undo, Redo, the clipboard, selection, layer and view commands). Every item sends the menu bar's own command. Left out: Photoshop's Generative Fill and Content-Aware rows (no such commands), the pasteboard colour menu, and the options bar's Reset Tool | Done | — |
| Localization: en, zh-Hans, es, it, ko, ru… (`docs/localization*.md`) | **Have** (1.1) for English, German, French, Spanish and Italian, in Photoshop's own terms for each (Ebene, calque, capa, livello; Tonwertkorrektur, Niveaux, Niveles, Livelli …): `E:lang/strings.txt` (about 2,170 rows: every menu, dialog, panel, tool, help bubble and message, the core's undo step names, filter, preset and layer effect names), made into `E:amiga/lang_table.h` by `E:tools/mklang.py`; `E:tools/langcheck.py` (in `E:tools/test.sh`) fails on any string without its four translations or any row no longer used; `E:tools/langfit.py` measures every translation with Easel's screen font and topaz 8 so the options bar, menus and requesters still fit 640×480; `E:tests/langtest.c`. The language follows Locale prefs' preferred languages (`lang_system`) or Preferences › Interface › Language; menus, cycle gadgets (`tr_list`), the options bar and pop-ups are translated. Chinese, Korean and Russian are left out: the Amiga's screen fonts are Latin-1 | Done for Latin-1 languages; Hard for CJK and Cyrillic | — |
| Help bubbles on every control | **Have** | Done | — |
| AI and generative features: Generative Fill and Expand, Neural Filters, Sky Replacement, Super Zoom (PhotoCraft lacks them too, `docs/gaps.md` G15) | **Left out**: they need an ML runtime or an online service. Stand-ins: the classical methods (Easel 1.0's healing tools) | Not sensible | — |
| Software update checks (`updates_cmds.rs`) | **Left out**: a network service | Not sensible | — |
| Web/WebAssembly build | **Left out** | Not sensible | — |
| Colour picker sampling outside the canvas (anywhere on the screen) | **Have** (1.1): the Eyedropper dragged off the picture takes the colour of the screen pixel under the pointer (panels, other windows): `ReadRGBPixel` on RTG screens, the pen's palette colour on native ones (`E:amiga/screenpick.c`). Not tested natively (it reads the Amiga screen) | Done | — |

Easel already has a few things PhotoCraft lacks: Filter › Render › Vignette, AVIF reading, the
Amiga and Atari formats it reads, 8-plane ILBM writing, opening a JPEG at a reduced size when
memory is short, and the Amiga-era features of section 21.

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
| Stencils: protect chosen colours, or an area, from painting | DPaint ("Stencil", "Fix Background"), PPaint | **Have** (1.1): Brush Settings' Stencil keeps the background colour or the Color Table's locked colours from every painting tool, or (Stencil Inverted) paints only them (`Brush.stencil`, `stencil_colors` in `E:core/toolopts.c`); an area is protected with the selection or Quick Mask | Done | — |
| Shade, Cycle, Tint and Blend draw modes over a palette range | DPaint | **Have** (1.2): Tint (`CB_TINT`) joins Smear, Shade, Blend and Cycle, and Brush Settings' Draw Mode gives them to any brush, not just a picked-up one (`DMODE` in `E:core/stroke.c`) | Done | — |
| Range fills: a gradient through a palette range, with dither styles | DPaint | **Have** (1.1): the Gradient tool's Palette Range on indexed pictures fills through the first cycling range (else the whole palette), dithered back to the palette (`gradient_from_palette`, `doc_fill_range`) | Done | — |

### 21.2 Colour cycling

| Feature | Expected from | Easel today | Amiga feasibility | Pri |
|---|---|---|---|---|
| Read the CRNG (DPaint), CCRT (Graphicraft) and DRNG (DPaint IV enhanced) ranges | DPaint, PPaint, Brilliance | **Have** (0.4): `ilbm_read_indexed` | Done | — |
| Play cycling (Tab in DPaint), with speed and direction | DPaint | **Have** (0.4): cycling plays on the canvas (`E:amiga/palwin.c`) | Done | — |
| Edit and save cycle ranges | DPaint, PPaint | **Have** (0.4): edited in the Color Table, written as CRNG | Done | — |

### 21.3 Brushes (DPaint style)

| Feature | Expected from | Easel today | Amiga feasibility | Pri |
|---|---|---|---|---|
| Pick up a custom brush: a rectangle, lasso or whole layer, with the background colour as transparent | DPaint, PPaint, Brilliance | **Have** (0.5: Edit > Pick Up Brush, `cbrush_pickup`) | Done | — |
| Paint with the custom brush in Matte, Color or Replace mode, and stamp it | DPaint | **Have** (0.5: Window > DPaint Brush, `CB_MATTE`, `CB_COLOR`, `CB_REPLACE`) | Done | — |
| Save and load brushes as ILBM, with the GRAB hotspot | DPaint | **Have** (0.5: `cbrush_write_ilbm`, `cbrush_read_ilbm`) | Done | — |
| Brush handle, and brush transforms: flip, rotate 90° or any angle, double or halve, stretch, shear, bend, outline | DPaint | **Have**: flip, turn, scale, halve, double and outline (0.5); 1.2 adds Shear and Bend (`cbrush_shear`, `cbrush_bend` in `E:core/legacy.c`) | Done | — |
| Anim brushes: a multi-frame brush that steps as it paints, saved as an ANIM | DPaint, Brilliance | **Partial** (1.2): animated brushes picked up from the frames (layers), a frame a dab (`animbrush_pickup`, `animbrush_stroke` in `E:core/legacy.c`); not yet on a tool or saved as an ANIM brush | Easy (the UI) | P3 |
| Perspective: place a brush on a 3D plane | DPaint | **Missing** | Moderate | P3 |
| Spare page (J) and copy to spare | DPaint | **Have** (1.2): Window › Spare Page (the picture shown before) and Copy to Spare (a copy of the picture into it, one undo step there) | Done | — |
| Magnify: a split view, zoomed beside 1:1 | DPaint, PPaint | **Partial**: the Navigator panel (0.6) shows the whole picture beside the zoomed view; no zoomed split | Easy | P3 |
| Grid snap for painting | DPaint | **Have** (1.1): with the grid shown and View > Snap on, the painting tools go from grid point to grid point (`grid_snap_paint`) | Done | — |
| Symmetry: N-fold point, mirror, tile | DPaint | **Have**: mirror and N-fold (0.5); 1.2 adds Tile (the canvas as 2-8 tiles across and down, `SYM_TILE`) and both diagonals | Done | — |

### 21.4 Writing Amiga picture modes

| Feature | Expected from | Easel today | Amiga feasibility | Pri |
|---|---|---|---|---|
| HAM6 writing, with a good encoder (optimised base palette, look-ahead) | ADPro, ImageFX, PPaint | **Have** (0.4): File › Export › IFF ILBM (Amiga options) | Done | — |
| HAM8 writing | ADPro, ImageFX | **Have** (0.4) | Done | — |
| EHB writing (32 colours plus half-bright, 64 in all) | DPaint, ADPro | **Have** (0.4) | Done | — |
| SHAM, PCHG and CTBL writing (a palette per line, "dynamic" HAM and hires) | ADPro, ImageFX, Photon Paint | **Partial** (0.4): SHAM is written (`IK_SHAM`); PCHG and CTBL aren't | Moderate | P3 |
| A choice of bitplane depth (1-8) and the ByteRun1 or uncompressed setting | all | **Have** (0.4): `IlbmOpts` planes and compress | Done | — |
| Masking in the ILBM header: `mskHasMask` and `mskHasTransparentColor`, and a transparent colour for genlock | DPaint, TVPaint, ImageFX | **Have** (0.4): `IMK_PLANE`, `IMK_TRANSPARENT` | Done | — |
| Export as raw interleaved bitplanes plus a palette, or as C or assembler source, or as hardware sprites | coders' converters, PPaint | **Have** (1.2): File › Export More: raw bitplanes (interleaved or plane after plane) with LoadRGB4 or LoadRGB32 palettes, the same as C (`__chip UWORD`) or assembler (`dc.w`, `SECTION DATA_C`), and 16-pixel hardware sprites with their control words (`bitplanes_raw`, `bitplanes_source`, `sprites_source` in `E:core/legacy.c`) | Done | — |

### 21.5 Screen-mode-aware output

| Feature | Expected from | Easel today | Amiga feasibility | Pri |
|---|---|---|---|---|
| Pick the target ModeID from the ASL ScreenMode requester, and write CAMG and the page size to match | all | **Have** (0.4): Image › Amiga Screen Mode (`E:amiga/modedlg.c`, `E:core/amigamodes.c`), CAMG and page size written | Done | — |
| Enforce the chipset's limits: OCS/ECS 32 colours in lores, 16 in hires, 64 EHB, 4096 HAM; AGA 256 or HAM8 | all | **Have** (0.4): the mode dialog warns when the picture doesn't fit the mode and chipset | Done | — |
| Pixel aspect for each mode (PAL and NTSC lores, hires, interlace, super-hires): show it on the canvas and correct it on import and export | DPaint, ADPro | **Have** (1.2): one implementation, described in section 2.5: View › Pixel Aspect Ratio (with the Amiga PAL and NTSC presets and From Amiga Screen Mode, `par_presets`/`par_set` in `E:core/viewx.c`) and Pixel Aspect Ratio Correction (`par_view_ratio`, `vx_par_correct`: the canvas's vertical zoom `zy` in `E:amiga/canvas.c`, which rulers, grid, guides, tools and Rotate View follow); the aspect is the document's `aspect_x`/`aspect_y`, kept in ILBM's BMHD; the overscan sizes below carry theirs | Done | — |
| Overscan presets (352 × 283, 704 × 566, 368 × 241…) and safe-area guides | DPaint, Brilliance, TVPaint | **Have** (1.2): the PAL and NTSC overscan sizes with their aspects (`overscan_sizes` in `E:core/legacy.c`) and View › Show › Guides from TV Safe Areas (action-safe 90 %, title-safe 80 %, `guides_safe_areas`) | Done | — |
| A "Save for Screen" preview that dithers and quantizes to the chosen mode before saving | ADPro, ImageFX | **Have** (1.3): File › Export More › Save for Web (Legacy) quantizes and dithers to the colours asked (IFF ILBM among the formats) and shows the result beside the original before saving, with its size (`sfw_encode`, `E:core/menu3.c`, `E:tests/menu3test.c`); Image › Mode › Indexed Color still previews on the canvas | Done | — |
| A display dither choice on palette screens: none, ordered or Floyd-Steinberg; a HAM8 preview on AGA | ImageFX, TVPaint | **Partial** (1.2): View › Screen Dither: Ordered, None (the nearest pens) or Error Diffusion (Floyd-Steinberg within each part drawn, `draw_rgb` in `E:amiga/widgets.c`); no HAM8 preview | Moderate | P3 |
| Copper-list gradients: design a per-line background colour gradient, preview it with a user copper list, save it as PCHG or SHAM | Brilliance, DPaint IV, ImageFX | **Partial** (1.2): the Gradient tool's gradient down the lines as a copper list (WAIT/MOVE, the PAL wrap at line 255, OCS 12-bit or AGA 24-bit with BPLCON3's LOCT) in C or assembler (`copper_gradient` in `E:core/legacy.c`); no live preview, no PCHG or SHAM | Moderate | P3 |

### 21.6 Workbench icons

| Feature | Expected from | Easel today | Amiga feasibility | Pri |
|---|---|---|---|---|
| Save as a classic `.info` icon (planar, 4 or 8 colours, in the WB 1.x, 2.x or MagicWB palette) | IconEdit, PPaint | **Have** (0.6): `E:core/iconwrite.c`, `ICONPAL_WB20`/`ICONPAL_MWB` | Done | — |
| Save as NewIcons (the image in tool types) | NewIcons tools | **Have** (0.6) | Done | — |
| Save as GlowIcons (an IFF FORM ICON with ARGB or palette images, OS 3.5) | IconEdit 3.5 | **Have** (0.6): the default when saving a picture's icon | Done | — |
| Save as a PNG icon (OS4, AROS, MorphOS) | — | **Have** (0.6) | Done | — |
| Two images for selected and unselected, and the icon's tool type and default tool | IconEdit | **Have**: a selected image and the default tool (0.6); 1.2 adds the icon's own tool types (`IconOpts.tooltypes`, every kind of icon) | Done | — |

### 21.7 Animation

| Feature | Expected from | Easel today | Amiga feasibility | Pri |
|---|---|---|---|---|
| Open every frame of an IFF ANIM (ANIM5/7/8) as frames or layers | DPaint, Brilliance, PPaint | **Have** (1.1): File › Import Frames to Layers reads ANIM op 0 and op 5 with both buffers, palette changes and the frame times; DPaint's two looping frames are left out (`anim_read_frames` in `E:core/frames.c`). Ops 7 and 8 aren't read (the frames up to one are kept). Animated GIFs too, with their disposal | Done | — |
| Edit the frames: a frame strip, add or delete or move frames, onion skin, play, timing (ANHD and DPAN) | DPaint, Brilliance, TVPaint | **Partial**: frames are layers (the Layers panel adds, deletes and moves them); 1.2 adds Window › Animation: Previous and Next Frame, Play Frames (at each frame's time, Abort stops it), Onion Skin with frames before and after at falling opacity (`frames_onion` in `E:core/legacy.c`) and Frame Time (`frame_time`, `frame_set_time`); no frame strip | Moderate (a strip panel) | P3 |
| Write an IFF ANIM (ANIM5 delta, and ANIM7/8) | DPaint, Brilliance | **Have** (1.1): File › Export › Layers as IFF ANIM writes op 5 for two buffers with the loop frames (`anim_write`); ANIM7/8 aren't written | Done | — |
| Animated GIF writing | PPaint 7 | **Have** (1.1): File › Export › Layers as Animated GIF: one palette, loops, each frame only where it changed (`gif_write_anim`; Pillow checks it, `E:tests/check_frames.py`) | Done | — |

### 21.8 Exchange with the rest of the Amiga

| Feature | Expected from | Easel today | Amiga feasibility | Pri |
|---|---|---|---|---|
| The clipboard through iffparse (`clipboard.device` unit 0): copy out as ILBM, paste ILBM in, FTXT text into the Text tool | all | **Have** (0.6): `E:amiga/clipboard.c` (ILBM both ways, FTXT) | Done | — |
| Datatypes import: open anything `picture.datatype` (V43) has a datatype for | MultiView, PPaint 7, ImageFX 4 | **Have** (0.6): `E:amiga/dtload.c`, the last resort when no reader of Easel's knows the file | Done | — |
| Datatypes export: `DTM_WRITE` where the datatype supports it | PPaint 7 | **Missing** | Moderate: few datatypes can write | P3 |
| Amiga bitmap fonts (`diskfont.library` `.font`) and ColorFonts in the Text tool | DPaint, PPaint | **Partial** (0.9): bitmap fonts from FONTS: in type layers (`E:core/amifont.c`); ColorFonts' own colours aren't used (they draw in the text colour) | Easy | P3 |
| Grab a public screen as a new document | ScreenGrab tools, PPaint | **Have** (1.3): File › Import › Grab Screen (now or in five seconds): the frontmost screen read (ReadPixelArray on RTG, ReadPixelArray8 and its palette on a palette screen, which opens indexed) as a new picture (`doc_from_screen`, `E:core/menu3.c`, `E:tests/menu3test.c`) | Done | — |
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
| 1. Tools | 54 | 18 | 2 | 0 | 74 | 85% |
| 2. Menus (File, Edit, Image, Layer, View, Window) | 154 | 10 | 31 | 8 | 195 | 82% |
| 3. Layers | 69 | 10 | 5 | 1 | 84 | 88% |
| 4. Blend modes | 35 | 0 | 1 | 0 | 36 | 97% |
| 5. Adjustments | 21 | 2 | 2 | 0 | 25 | 88% |
| 6. Filters | 78 | 1 | 5 | 1 | 84 | 93% |
| 7. Selection | 22 | 4 | 2 | 0 | 28 | 86% |
| 8. Painting and brushes | 32 | 0 | 0 | 0 | 32 | 100% |
| 9. Type and text | 26 | 4 | 4 | 0 | 34 | 82% |
| 10. Vector paths and shapes | 13 | 1 | 0 | 0 | 14 | 96% |
| 11. Colour management, modes, depths | 18 | 0 | 3 | 2 | 21 | 86% |
| 12. History and undo | 11 | 2 | 0 | 0 | 13 | 92% |
| 13. File formats (read, write, presets) | 52 | 3 | 4 | 6 | 59 | 91% |
| 14. Import and export options | 19 | 1 | 0 | 0 | 20 | 98% |
| 15. Panels | 20 | 10 | 6 | 0 | 36 | 69% |
| 16. Shortcuts | 24 | 1 | 1 | 0 | 26 | 94% |
| 17. Preferences | 7 | 6 | 3 | 4 | 16 | 62% |
| 18. Automation and actions | 0 | 3 | 0 | 3 | 3 | 50% |
| 19. Plug-ins | 1 | 0 | 0 | 4 | 1 | 100% |
| 20. Anything else | 5 | 2 | 0 | 5 | 7 | 86% |
| **All areas** | **661** | **78** | **69** | **34** | **808** | **86.6%** |

**Overall** (recounted 2026-11-18 after round 3, Easel 1.3; when this summary was first counted, before 0.9, it was 26.9%):

- **Coverage:** about 87% (86.6%).
- **Fully had:** 661 of 808 rated rows (81.8%).
- **At least partly had:** 739 rows (91.5%).

**Legacy extras (section 21):** 45 Have, 7 Partial, 2 Missing, 2 Excluded. What is left there is
listed in section 21.

**Where Easel is weakest now:** preferences (section 17), panels (15: docking and workspaces), and the
heavy PhotoCraft features that are hard on a 68040 (Camera Raw, Vanishing Point, Puppet and
Perspective Warp, Content-Aware Scale, Photomerge); see the Missing rows of each section.

**How the 0.3-1.2 roadmap would change this.** Closing all of its P1 and P2 rows (283 of them) would bring coverage to
about 59%. Most of the rest is P3: work that is Hard or Not sensible on a 68040, such as
smart objects, 16- and 32-bit depths, content-aware and AI stand-ins, camera raw, CMYK and Lab, and
WebAssembly plug-ins.
