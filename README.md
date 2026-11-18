# Easel 1.0

[![Buy Me a Coffee](https://img.shields.io/badge/Buy%20me%20a%20coffee-farsonic-FFDD00?logo=buymeacoffee&logoColor=black)](https://buymeacoffee.com/farsonic)

Easel is a picture editor for the Amiga, laid out the way PhotoCraft is. The options bar runs
along the top, the tools down the left, and open pictures are tabs over the canvas. The Color,
Swatches, Brushes, History and Layers panels sit down the right, the status bar along the bottom, and the menus
are in the screen's title bar as usual. It is written in C for MUI and draws its own dark
interface and icons. None of the icons are copied from any other program.

## What it needs

- A 68040 or 68060 with its FPU. Easel is built for the 68040.
- MUI 3.8 or later (muimaster.library 19), or AROS or MorphOS, which have it built in.
- Memory to match your pictures. A layer takes width × height × 4 bytes, so an 800 × 600 layer
  takes about 2 MB and a 2000 × 1500 photo 12 MB, in one piece. Undo keeps up to 40 MB. If a
  picture won't fit, Easel says how much it needs and how much is free, and offers to open a
  JPEG at a smaller size that fits.
- Screen: a graphics card screen (15 to 32 bits, through cybergraphics.library) shows pictures
  exactly. On an AGA or ECS screen with 256 colours or fewer they are dithered to a colour cube.
- Size: Easel fits a 640 × 480 screen. On short screens the toolbox goes into two columns, and
  Color and History share one panel with tabs.

## New in 1.2: colour management, pixel aspect, more formats and type

- Colour management: ICC profiles (v2 and v4; matrix, grey and LUT profiles) are kept when a picture
  is opened and saved as PNG, JPEG, TIFF, PSD or WebP. Edit > Color Settings (proofing intent, black
  point compensation, the display's gamma), Assign Profile and Convert to Profile (sRGB and Adobe
  RGB built in, or any .icc file). Pictures with a profile are shown converted for the screen.
- View > Proof Colors and Proof Setup: working CMYK (Easel's own press or a CMYK profile you load)
  and its plates, legacy Macintosh, Internet and monitor RGB, and two kinds of colour blindness.
  View > Gamut Warning paints what can't be printed grey.
- Image > Mode > CMYK Color and Lab Color: the picture is still edited in RGB (a native CMYK mode
  would need five bytes a pixel and a second set of every tool), with its colours brought into the
  printable range, and is saved as a CMYK TIFF or JPEG or a CIELab TIFF. CMYK files open through
  their own profile.
- View > Pixel Aspect Ratio (the Amiga's PAL and NTSC modes, video formats, Custom) and Pixel Aspect
  Ratio Correction: hires and interlaced pictures look on the canvas as they do on their screen.
- Opening: palette PNG and BMP files open Indexed, 1-bit TIFF and PBM in Bitmap mode; Cineon,
  DICOM and OpenEXR (uncompressed, RLE, ZIP and PIZ) are read; IFF ANIM ops 7 and 8; PSD layer
  styles; Color Lookup reads .3dl LUTs.
- Saving: lossy WebP, progressive JPEG, palette BMP, Cineon, Radiance HDR, 12- and 16-bit DPX, PSD
  layer styles; File > Export > TIFF asks its compression. Export As has a resampling method, a
  canvas size, a matte, a choice of metadata and Convert to sRGB.
- Type: All Caps, Small Caps, superscript and subscript (Layer > Type > Character), OpenType
  ligatures, swashes, oldstyle figures, alternates, ordinals and fractions (Layer > Type), justified
  paragraphs with the last line centred or right, and left and right indents.

## New in 1.1: tool options

- Dodge and Burn have a Range (Shadows, Midtones, Highlights) and Protect Tones; the Sponge
  saturates or desaturates, with Vibrance. Blur, Sharpen and Smudge have a Mode; Sharpen has
  Protect Detail and Smudge Finger Painting.
- The Clone Stamp has a Mode, Aligned, and samples the current layer, the current layer and those
  below, or all layers (the Healing Brush too). The Eyedropper and Color Sampler average up to
  101 x 101 pixels, and the Eyedropper can sample Current & Below.
- The Eraser's Brush, Pencil and Block modes and Erase to History, the Pencil's Auto Erase, and the
  Brush's Smoothing and Airbrush are on the options bar. The History Brush has Flow and can paint
  from a History panel snapshot (From History).
- Move tool: Auto-Select a Layer or its Group, Show Transform Controls (drag a handle to scale or
  turn the layer), and an Align list (edges, centres, distribute).
- Crop tool: overlays (Rule of Thirds, Grid, Diagonal, Triangle, Golden Ratio) and Straighten (drag
  along what should be level). Zoom tool: Zoom In or Out, drag left or right to zoom, Fill Screen.
- Deluxe Paint: a stencil (Brush Settings: keep the background colour, or the palette colours locked
  in the Color Table, from being painted), range fills (the Gradient tool's Palette Range on indexed
  pictures, through the first cycling range), and grid snap for painting (View > Grid with Snap).
- Shapes draw from their centre with Alt held while dragging.
- The History panel's bin deletes the step in effect (and those after it) when no snapshot is picked.
- Keys: Page Up/Down (Ctrl: left/right), Home and End scroll; Ctrl-Space and Alt-Space zoom while
  held; Ctrl with a painting tool moves; hold ` to erase with the Brush; F steps through the screen
  modes; Shift-F5 Fill, Shift-F6 Feather, F12 Revert; Ctrl-H hides the selection's outline, grid
  and guides; Ctrl-Shift-D Reselect, Ctrl-Shift-F Fade, Ctrl-Alt-F Last Filter, Ctrl-Alt-G Clipping
  Mask, Ctrl-Shift-B Auto Color, Ctrl-Alt-Shift-L Auto Contrast, Ctrl-Alt-Shift-B Black & White,
  Ctrl-R rulers, Ctrl-' grid, Ctrl-; guides, Ctrl-Shift-; snap, Ctrl-Alt-S Save a Copy, Ctrl-Alt-W
  Close All.
## New in 1.1: files, animations and SVG

- More formats saved, each from File > Export, Save As (by the name's extension) and EaselConvert:
  WebP (lossless), PDF (one page, lossless or JPEG), PBM/PGM/PPM/PAM, QOI, Windows icons (every
  size up to 256), DDS textures (BC1, BC3 or uncompressed, with mipmaps), DPX (8 or 10 bits) and
  FITS. EaselConvert also writes TIFF, Targa and PSD now.
- Animations: File > Import Frames to Layers opens every frame of an animated GIF or an IFF ANIM
  (op 5, as Deluxe Paint writes them) as layers, each named with its time ("Frame 3 (0.10 s)");
  File > Export > Layers as Animated GIF and Layers as IFF ANIM write the layers back as frames.
- SVG: drawings open as shape layers that stay editable (paths, rectangles, circles, ellipses,
  lines and polygons, with their groups, transforms, fill and stroke); File > Export > Paths to SVG
  writes the paths and shape layers.
- File: Close Others, Quick Export as PNG, and Scripts: Delete All Empty Layers, Flatten All Masks
  and Fit Image. Edit > Purge: Undo History, Clipboard and All (every picture's history).
- Saving an opened JPEG keeps the quality it was saved at; the JPEG Options remember the last
  quality.
- Warp Text has six more styles: Arc Lower, Arc Upper, Shell Lower, Shell Upper, Fisheye and
  Inflate. Layer > Type > Paste Lorem Ipsum fills a type layer with placeholder text, and Convert to
  Paragraph Text and Convert to Point Text change its kind, the text staying where it is.
- View > Snap To chooses what snaps (guides, grid, the picture's edges, its centre lines, whole
  pixels); View > Lock Guides.
## After 1.0: the Filter Gallery, Color Lookup, Focus Area, Select and Mask

- Filter > Filter Gallery: a stack of up to four effects, applied in order as one step; each row's
  Settings shows that effect's sliders with the whole stack previewed on the picture. Filter > Last
  Filter repeats the stack.
- The rest of the gallery effects, each also in the Filter menu with its own dialog: Artistic
  (Colored Pencil, Dry Brush, Fresco, Neon Glow, Paint Daubs, Palette Knife, Rough Pastels, Smudge
  Stick, Sponge, Underpainting, Watercolor), Brush Strokes (Accented Edges, Angled Strokes,
  Crosshatch, Dark Strokes, Ink Outlines, Sprayed Strokes, Sumi-e), Sketch (Bas Relief, Chalk &
  Charcoal, Charcoal, Chrome, Conte Crayon, Graphic Pen, Note Paper, Plaster, Reticulation, Stamp,
  Torn Edges, Water Paper; drawn in the foreground colour on the background colour) and Texture
  (Craquelure, Grain). They work through the picture in 128-pixel tiles, so a large picture needs
  little memory beyond its undo copy; Easel says how much is needed if there isn't enough.
- Image > Adjustments > Color Lookup: 3D LUTs from .cube files (1D ones too), trilinear or
  tetrahedral, with dither and opacity (Easel looks in PROGDIR:LUTs first).
- Gradient Map has Custom: any gradient from the Gradient Editor. Black & White has Auto. Photo Filter
  opens at the settings last used. Equalize, with part of the layer selected, asks whether to even out
  the selected area only or the whole image by it.
- Image Size: Bicubic Smoother, Bicubic Sharper and Preserve Details (Lanczos), the resolution, and
  Resample off to change only the resolution.
- Select > Focus Area (where the picture is sharp), Select > Select and Mask (edge radius, smooth,
  feather, contrast, shift edge; out to the selection, a layer mask or a new layer with a mask), and
  All Layers / Deselect Layers.
- Filters and adjustments are greyed out in a Bitmap picture, all but Invert.
## New in 1.1: layer styles and more layer commands

- Layer > Layer Style: Drop Shadow, Inner Shadow, Outer Glow, Inner Glow, Bevel & Emboss (inner,
  outer, emboss, pillow), Satin, Color Overlay, Gradient Overlay and Stroke (outside, inside or
  centre), set in the Layer Style window with a live preview: tick an effect on the left, change it
  on the right. They follow the layer as it is painted, moved or masked, show at any fill opacity,
  and work on pixel, type, shape and fill layers. Also Copy, Paste and Clear Layer Style, Global
  Light, Create Layers (each effect a layer of its own), Hide All Effects, Scale Effects and
  Layer > Rasterize > Layer Style. A layer with a style shows "fx" in the Layers panel. Effects are
  worked out once and kept (a byte a pixel for each effect, over the area they reach), so a large
  layer with many effects needs memory to match. OpenRaster files keep them; PSD files get how they
  look, not the settings.
- Paint.NET's six blend modes: Reflect, Glow, Negation, Xor and its own Color Burn and Color Dodge.
- Layer > Layer Color: twelve colour labels, shown down the edge of the Layers panel.
- Layer > Stack Mode: Mean, Median, Minimum, Maximum, Range, Summation, Variance, Standard
  Deviation, Entropy, Skewness and Kurtosis of the picked layers (or all visible ones) as a new
  layer, for removing noise or passers-by from a series of shots.
- Delete Hidden Layers, Arrange > Reverse, New > Background from Layer, and Copy CSS (the layer's
  size, place, opacity, mode, fill, shadows and stroke as CSS, on the clipboard as text).
- Edit > Preferences > General: Skip Transform when Placing.

## New in 1.0: filters, adjustments and retouching

- Retouching tools (J): the Spot Healing Brush (paint over a blemish; Easel finds a patch nearby
  whose surroundings match and heals it in), the Healing Brush (Alt-click a source, paint as with
  the Clone Stamp; on release the texture comes from the source and the colour from around it),
  the Patch tool (draw round the area, then drag it to the source) and Red Eye (click or drag a box
  over the eye; pupil size and darken amount).
- Hue/Saturation works on the Master or any of the six colour ranges (with range and fuzziness).
  Black & White has six colour sliders and a tint. New: Replace Color, Shadows/Highlights and Match
  Color (from another open picture).
- Curves has separate RGB, Red, Green and Blue curves. Levels and Curves have Black, Grey and White
  Point buttons (they take the foreground colour) and Auto. Levels, Curves, Hue/Saturation and Black
  & White come with presets and can load and save your own (in PROGDIR:Presets). Photo Filter has
  the usual 20 named filters; Brightness/Contrast has Use Legacy. Channel Mixer, Selective Color and
  Color Balance remember each channel or range as you switch between them.
- New filters, each with a live preview: Average, Blur More, Radial Blur with a movable centre,
  Iris Blur, Tilt-Shift, Smart Sharpen, Sharpen Edges, Displace, Wave, Lens Correction, Diffuse
  Glow, Extrude, Glowing Edges, Detect Edges, Plastic Wrap, Lighting Effects (point, spot or
  infinite light, texture and gloss), PAL Colors, Maximum/Minimum with roundness, more Offset modes,
  Color to Alpha and HSB/HSL; and the gallery effects Cutout, Film Grain, Poster Edges, Spatter,
  Halftone Pattern, Photocopy, Mosaic Tiles, Patchwork, Stained Glass and Texturizer.
- Replace Color and the new Hue/Saturation, Black & White and Curves are adjustment layers too.

## New in 0.9: type and vectors

- Type stays editable: the Horizontal and Vertical Type tools (T) make type layers. Click to type
  a line, or drag a box for a paragraph that wraps; click the text again to edit it (Shift and the
  arrows select, Ctrl-A selects all, Esc finishes). Size, font, colour and alignment in the options
  bar apply to the selected letters. The Type Mask tool makes a selection in the shape of the text.
- Layer > Type: Edit Type, Character (font, size, leading, tracking, colour, underline,
  strikethrough), Paragraph (alignment, indents, spacing), Warp Text (Arc, Arch, Bulge, Flag, Wave
  and more), Horizontal/Vertical, Create Work Path and Rasterize Type.
- Amiga bitmap fonts from FONTS: can be used as well as TrueType ones; they are listed as
  "name (Amiga)".
- Paths: the Pen tool (P) with Add Anchor, Delete Anchor and Convert Point, and the Path and Direct
  Selection tools (A). A Paths tab beside Layers and Channels lists the work path and saved paths;
  fill or stroke a path, make a selection from it, or a path from the selection.
- Shape layers: Rectangle, Rounded Rectangle, Ellipse, Polygon (or star), Line (with arrowheads) and
  Custom Shape (U) draw shapes that stay editable, with fill and stroke. Double-click a shape's
  thumbnail for Shape Properties. Each tool can also draw a path or plain pixels instead.
- Type and shape layers move, turn, flip and transform without losing quality, and keep all of
  that in OpenRaster files.

## New in 0.8: workspace and safety

- A progress bar: anything that takes more than about half a second shows a Working window with how
  far it has got and an Abort button (Esc works too). Abort puts the picture back as it was.
- Edit > Preferences: General, Interface, Tools, File Handling, Units & Rulers, Guides & Grid and
  Scratch Drawer pages, applied as you change them and saved.
- Edit > Keyboard Shortcuts: change the Amiga keys of the menu commands, extra keys on the canvas and
  the tools' letters (saved in PROGDIR:Easel.keys).
- Undo keeps as many steps as you set; older steps can go to a scratch drawer on disk instead of
  memory.
- History snapshots: keep the picture as it is now, go back to a snapshot as a new step, or make a
  new document from any step.
- The Color Sampler tool (with the Eyedropper) leaves up to four marks whose colours show in the
  Info panel.
- View > Screen Mode: in a window, on Easel's own public screen, or full screen in any mode.
- Hold a tool's key to use that tool only until you let go.

## New in 0.7: selections and transforms

- Free Transform on the canvas (Edit > Free Transform, Amiga-T): drag a corner to scale (Shift keeps
  the proportions), a side to stretch, inside to move, outside to turn (Shift: 15 degree steps);
  Ctrl and a corner distorts, Ctrl-Shift and a side skews, Ctrl-Alt-Shift and a corner sets the
  perspective. Return applies it, Esc puts it back. Edit > Transform also has Scale, Rotate, Skew,
  Distort, Perspective and Again, and its flips and quarter turns are exact and follow the
  selection. (Turning a whole large layer takes a while on a 68040; a selection is quicker.)
- Select > Transform Selection, Save Selection and Load Selection (with Replace, Add, Subtract,
  Intersect and Invert), and a Channels tab beside Layers listing the saved selections: click one to
  load it, Shift-click to add.
- Quick Mask (Q, or Select > Edit in Quick Mask Mode): the selection shows as a red overlay to
  paint with the Brush, Pencil or Eraser (black takes away, white adds); Q again turns it back into
  a selection.
- Marquee styles: Normal, Fixed Ratio, Fixed Size, Single Row and Single Column; and Anti-alias for
  the lassos, the elliptical marquee and the Magic Wand.
- Select > Color Range: sampled colours (the foreground's) or Reds, Yellows, Greens, Cyans, Blues,
  Magentas, Highlights, Midtones, Shadows and Skin Tones, with Fuzziness, Invert and a preview.
- New tools: the Magnetic Lasso (with the lassos: it follows edges as you move; click to add points,
  Backspace takes one back, Return or a double-click closes it) and the Ruler (with the Eyedropper:
  measures length and angle; Straighten Layer, or the whole picture, levels what it measured).
- Crop presets, among them the Amiga's PAL and NTSC sizes (320 x 256, 320 x 200 and more).
- The Move tool copies when you Alt-drag.
- Layer > Matting: Defringe, Remove Black Matte and Remove White Matte.
- Crop, Canvas Size, Image Size and the rotations now keep layer masks and saved selections in step.

## New in 0.6: exchange

- More formats saved: TIFF (LZW), Targa and Photoshop PSD. A PSD opens and saves with its layers,
  names, opacity, blend modes, masks, clipping, fill opacity and groups.
- JPEGs from cameras are turned upright from their EXIF orientation.
- File > File Info: title, author, description, keywords, copyright and web address, kept in PNG,
  JPEG, TIFF, PSD and ORA files.
- The Amiga clipboard: Copy and Cut also put the picture on the clipboard as an IFF ILBM for other
  programs (exact up to 256 colours, 24-bit beyond), and Edit > Paste from Amiga Clipboard takes
  one in (Paste does too, when Easel has copied nothing itself).
- Pictures no format of Easel's reads are opened through the system's datatypes.
- Drop pictures onto Easel's window (or its icon while iconified) to open them.
- Saving writes a Workbench icon (a GlowIcon showing the picture) beside a picture that has none;
  Edit > Save Icons with Pictures turns this off.
- File: Open Recent (the last ten), Save a Copy, Export As (scaled by a percentage), Layers to
  Files, Load Files into Stack, and Place now goes on into Free Transform.
- Autosave: changed pictures are saved every five minutes (Edit > Autosave sets how often) to the
  Recovery drawer; if Easel or the Amiga stops unexpectedly, Easel offers them back next time.
- Edit > Fade (the last change mixed back by an opacity and mode) and Toggle Last State.
- Navigator and Info panels beside Layers: the whole picture with the part shown, and the pointer's
  place, its colour in RGB, HSB and hex, the selection's size and a brightness histogram.
- Tab hides the toolbox, options bar and panels; Shift-Tab just the panels.
- Shift-Alt and a letter sets the blend mode, as in Photoshop: the painting tool's, or with the Move
  tool the active layer's.
- **EaselConvert**, a Shell command for converting pictures in batches:
  `EaselConvert FROM/M/A,TO/K,FORMAT/K,WIDTH/N,HEIGHT/N,SCALE/N,QUALITY/N,COLORS/N,DITHER/S,ICON/S,ICONTYPE/K,DEFTOOL/K,QUIET/S`,
  for example `EaselConvert Work:Photos/#?.jpg TO RAM: FORMAT ILBM COLORS 32 DITHER WIDTH 320 ICON`.

## New in 0.5: brushes and painting

- Brush tips: round (with angle, roundness and flips), square, sampled from the picture (Edit >
  Define Brush Preset) and Photoshop .abr brushes (version 1, 2 and 6 to 10). The **Brushes**
  panel keeps the presets; Window > Brush Settings sets the tip, spacing, shape dynamics,
  scattering, colour dynamics, transfer, smoothing, symmetry (mirror, radial and mandala) and
  airbrush build-up.
- Deluxe Paint brushes: pick up part of the picture as a brush (Edit > Pick Up Brush, from the
  selection), flip, turn, scale, halve, double or outline it, and paint with it as Matte, Color,
  Replace, Smear, Shade, Blend or Cycle (Window > DPaint Brush). Brushes load and save as IFF ILBM.
- New tools: Pattern Stamp and History Brush (with the Clone Stamp, S and Y), Color Replacement
  (with the Brush), Background Eraser and Magic Eraser (with the Eraser).
- Gradients with any number of colour and opacity stops, each with a midpoint: the Gradient
  tool's list of presets, and the Gradient Editor (its Edit button). GIMP .ggr gradients load and
  save. The Gradient tool can dither.
- Patterns: Edit > Define Pattern makes one from the selection; the Paint Bucket and Edit > Fill
  with Pattern use the one chosen. Pattern sets load and save as IFF.
- The **Swatches** panel: click to set the foreground (Alt-click the background); load and save
  GIMP .gpl, Photoshop .aco, .act and .ase, IFF CMAP and Easel's own swatch files.
- A new colour picker, with HSB, RGB, Lab and hex.
- Edit > Take Snapshot sets what the History Brush paints back (at first, the picture as it opened).

## New in 0.4: palettes and the Amiga screen

- Image > Mode: RGB, Grayscale, Indexed Color (2 to 256 colours, from the picture with median
  cut, octree or Wu's method, an OCS/ECS 12-bit palette, the current palette or a preset such as
  Workbench, MagicWB or the web colours; 12 dithers including Floyd-Steinberg, Atkinson and
  ordered) and Bitmap.
- An indexed picture keeps its palette as you paint: everything drawn is brought back to its
  colours, in the same undo step.
- Image > Color Table (or Window > Palette): edit the colours (RGB or HSV), spread, exchange and
  copy, lock entries, make an Extra Half-Brite palette, and load or
  save palettes (.act, .aco, .ase, .gpl, JASC/RIFF .pal, IFF and PNG).
- Colour cycling: the CRNG, CCRT and DRNG ranges of IFF pictures are kept, can be edited, and
  play on the canvas.
- Image > Amiga Screen Mode: choose the display mode (PAL and NTSC, lores to super-hires, laced,
  Multiscan, Euro72, DblPAL, DblNTSC) and chipset; Easel warns when the picture doesn't fit it.
- Saving: palette ILBMs open as indexed pictures and save with their palette, cycling, ModeID and
  pixel aspect. File > Export > IFF ILBM (Amiga options) writes 1 to 8 planes, EHB, HAM6, HAM8,
  SHAM or 24-bit, with a mask plane or transparent colour. GIF can be saved and exported, and
  PNG keeps an indexed or grey picture's palette.
- View: rulers, a grid (Grid Settings), guides (New Guide, Clear Guides) and Snap, which the
  marquees, shapes, line, gradient and crop snap to.

## New in 0.3: live layers

- Layer groups (folders): new, group and ungroup, collapse, pass-through blending, merge.
- Layer masks: reveal or hide all, from the selection or from transparency; paint on the mask
  (click its thumbnail, or Layer > Layer Mask > Edit Mask/Layer); density and feather; enable,
  link, invert, apply or delete.
- Clipping masks, and locks for position and for everything.
- Adjustment layers for every adjustment (including the new Channel Mixer, Gradient Map and
  Selective Color), changed at any time by double-clicking them; solid colour and gradient fill
  layers.
- Fill opacity and Blend If in Layer Properties; Align and Distribute; Merge Layers; Paste Into.
- The Layers panel shows groups indented, mask thumbnails, clipping arrows and the four locks;
  Shift-click and Ctrl-click pick several layers; drag a layer onto an open group to put it in.
- OpenRaster files keep groups, masks, clipping, adjustment and fill layers.

## New in 0.2.1

- Nine more blend modes: Dissolve, Linear Burn, Darker Color, Lighter Color, Vivid Light, Linear
  Light, Pin Light, Hard Mix and Divide.
- 29 more filters, among them Lens Blur, Smart and Surface Blur, Reduce Noise, Dust & Scratches,
  Polar Coordinates, Glass, Ocean Ripple, Crystallize, Pointillize, Color Halftone, Mezzotint,
  Lens Flare, Lighting Effects, Fibers, De-Interlace, NTSC Colors and Custom; Filter > Last Filter.
- Select > Reselect, Grow and Similar.

## New in 0.2

- Opens many more kinds of picture, from today's web formats back to the Amiga and Atari
  classics: AVIF, WebP, TIFF, Photoshop PSD/PSB, Targa, PCX, PBM/PGM/PPM/PAM/PFM, XPM, DDS,
  QOI, Windows icons, Radiance HDR, FITS and DPX; and IFF ACBM, RGBN/RGB8, DEEP, ILBMs with
  SHAM/CTBL/PCHG palette changes, the first frame of an IFF ANIM, Workbench icons (classic,
  NewIcons and GlowIcons), Sun raster, SGI, Degas and NEOchrome. See Files below.
- AVIF pictures are decoded with dav1d, built for the 68040. A photo of a few megapixels takes
  a while on a 68040 (about 50 seconds for 1287 x 1931 in our tests); the busy pointer shows it
  is working.
- Licenses.txt lists the other people's work Easel contains, with their licences.

## New in 0.1.1

- Opening a big JPEG (a 2000 × 1500 photo, say) no longer fails with "damaged or unsupported"
  when memory is short. It is decoded straight into the picture, so opening it needs half the
  memory it did. If there still isn't room, Easel explains and offers a smaller size.
- The canvas no longer needs a second full-size copy of the picture. When there isn't room for
  one, it composites the rows it shows as it draws them.
- Every tool, setting, swatch, panel button and tab has a help bubble: hover over it.

## Installing

Open the Easel drawer and double-click **Install-Easel** (Commodore's Installer 43, or AROS's).
It copies Easel into a drawer of your choosing, or updates or removes an installed copy. You can
also just drag the Easel drawer wherever you like. Easel finds its Fonts and TrueType drawers
next to itself.

## The window

- **Options bar** (top): the settings of the tool in use. Drag a value left or right to change
  it, or click it to type a number. Lists drop down, and ticks toggle. The selection tools show
  four mode icons: new, add, subtract, intersect.
- **Toolbox** (left): one button for each group of tools. A small triangle means the group has
  more than one tool; click the triangle, or click the chosen tool again, to list them. Below the
  tools are the foreground and background colours (click one to pick a colour), the swap arrow
  and the default black-and-white pair.
- **Tabs** over the canvas: one for each open picture, showing its name, zoom, layer and an
  asterisk once it is changed. Click a tab to switch to it, or its cross to close it.
- **Color panel**: a saturation and brightness square for the foreground's hue, the hue strip,
  red, green and blue bars, the hex value (click it to type one) and swatches. Alt-click a
  swatch to set the background colour.
- **History panel**: every undo step. Click one to go back (or forward) to it.
- **Layers panel**:
  - The active layer's blend mode, its opacity (drag or click) and its locks.
  - A row for each layer, top first, with its eye, a thumbnail and its name.
  - Click a row to work on that layer, click the eye to hide or show it, double-click to open
    its properties, and drag a row up or down to reorder.
  - Buttons along the bottom: lock, merge down, new, duplicate and delete.
- **Status bar**: zoom, picture size and number of layers, then the pointer's position with the
  colour under it, and the size of a selection being drawn.

## Tools (key)

| Tool | Key | |
|---|---|---|
| Move | V | drag the layer, or what is selected; arrow keys nudge by 1 (Shift: 10); Auto-Select picks the layer under the pointer |
| Rectangular / Elliptical Marquee | M | Shift adds, Alt subtracts, both intersect; Shift while dragging makes a square or circle; feather |
| Lasso / Polygonal Lasso / Magnetic Lasso | L | freehand; or click the corners, then double-click or press Return to close (Esc drops it); the magnetic lasso follows edges |
| Magic Wand | W | tolerance, contiguous, sample all layers |
| Crop | C | drag a box and adjust its handles; Return or a double-click inside crops, Esc cancels |
| Eyedropper / Color Sampler / Ruler | I | point or up to 101 × 101 average, current, current and below, or all layers; Alt-click sets the background colour; the sampler leaves up to four marks; the ruler measures and straightens |
| Spot Healing Brush / Healing Brush / Patch / Red Eye | J | paint over a spot; Alt-click a source, then paint; draw round an area and drag it to the source; click a red eye |
| Clone Stamp / Pattern Stamp | S | Alt-click sets the clone source, then paint; the pattern stamp paints the chosen pattern |
| History Brush | Y | paints back the snapshot |
| Brush / Pencil / Color Replacement | B | size, hardness, blend mode, opacity, flow; Shift-click paints a straight line from the last stroke; Alt picks a colour; Brush Settings for tips and dynamics |
| Eraser / Background Eraser / Magic Eraser | E | erases to transparency; the background eraser takes away the colour under the brush's centre; the magic eraser the area of similar colour clicked |
| Gradient / Paint Bucket | G | gradient presets and the Gradient Editor; linear, radial, angle, reflected, diamond; the bucket fills with the foreground or a pattern |
| Blur / Sharpen / Smudge | R | mode and strength; protect detail, finger painting |
| Dodge / Burn / Sponge | O | range, exposure, protect tones; the sponge saturates or desaturates, with vibrance |
| Horizontal Type / Vertical Type / Type Mask | T | click and type, or drag a box for a paragraph; click a type layer's text to edit it; the type mask makes a selection |
| Pen / Add Anchor / Delete Anchor / Convert Point | P | click for corners, drag for curves; click the first point to close the path |
| Path Selection / Direct Selection | A | move a whole path, or its points and handles |
| Rectangle / Rounded Rectangle / Ellipse / Polygon / Line / Custom Shape | U | a shape layer, a path or pixels; fill, stroke, sides (or a star), corner radius, arrowheads |
| Hand | H | drag to scroll (or hold the space bar with any tool) |
| Zoom | Z | click to zoom in, Alt-click to zoom out (or the other way round); drag left or right to zoom |

Pressing a tool's key again (or with Shift) moves on to the next tool sharing the key. Other keys:

- `[` and `]` make the brush smaller or larger; with Shift they change its hardness.
- 1 to 0 set the opacity (10% to 100%).
- X swaps the colours; D sets the default colours.
- `+` and `-` zoom.
- Del or Backspace clears the selection. Alt-Backspace fills it with the foreground colour,
  Ctrl-Backspace with the background colour.

PhotoCraft's Ctrl shortcuts work as well as the menus' Amiga-key ones:

| Keys | Command | Keys | Command |
|---|---|---|---|
| Ctrl-Z | Undo | Ctrl-Shift-Z, Ctrl-Y | Redo |
| Ctrl-X / C / V | cut, copy, paste | Ctrl-Shift-C | copy merged |
| Ctrl-Shift-V | paste in place | Ctrl-A | select all |
| Ctrl-D | deselect | Ctrl-Shift-I | inverse |
| Ctrl-L | Levels | Ctrl-M | Curves |
| Ctrl-U | Hue/Saturation | Ctrl-B | Color Balance |
| Ctrl-I | Invert | Ctrl-Shift-U | Desaturate |
| Ctrl-Shift-L | Auto Tone | Ctrl-J | New Layer via Copy |
| Ctrl-Shift-J | New Layer via Cut | Ctrl-E | Merge Down |
| Ctrl-Shift-E | Merge Visible | Ctrl-Shift-N | New Layer |
| Ctrl-T | Free Transform | Ctrl-Alt-I | Image Size |
| Ctrl-Alt-C | Canvas Size | Ctrl-[ / Ctrl-] | send backward, bring forward |
| Ctrl-N | New | Ctrl-O | Open |
| Ctrl-S | Save | Ctrl-Shift-S | Save As |
| Ctrl-W | Close | Ctrl-0 | fit on screen |
| Ctrl-1 | 100% | | |

## Menus

- **File**:
  - New (with sizes from 320 × 256 PAL upwards; white, background colour or transparent), Open,
    and Place as Layer.
  - Close, Close All, Save, Save As and Revert.
  - Export to PNG, JPEG (with quality), BMP, IFF ILBM (24-bit, or 256 colours with a median-cut
    palette and dithering), and About.
- **Edit**:
  - Undo and Redo (each names the step), Step Backward and Forward.
  - Cut, Copy, Copy Merged, Paste, Paste in Place and Clear.
  - Fill (colour, mode, opacity) and Stroke (width, inside, centre or outside).
  - Free Transform (scale and rotate by numbers), and Transform: rotate 90° or 180°, flip.
  - Purge Undo History.
- **Image**:
  - Adjustments: Brightness/Contrast, Levels (with histogram), Curves (an editor: click to add
    points, drag them, drag one out to remove it), Exposure, Vibrance, Hue/Saturation (with
    Colorize), Color Balance, Black & White, Photo Filter, Invert, Posterize, Threshold,
    Desaturate and Equalize. Each shows its effect on the picture as you change it, and Cancel
    takes it back.
  - Auto Tone, Auto Contrast and Auto Color.
  - Image Size (bicubic, bilinear or nearest; pixels or per cent) and Canvas Size (with anchor).
  - Image Rotation (90°, 180° or any angle) and flips.
  - Crop, Trim and Duplicate.
- **Layer**:
  - New, Duplicate, Delete, Properties (name, opacity, blend mode, visibility).
  - New Layer via Copy or Cut.
  - Arrange, the two locks, and Hide/Show.
  - Merge Down, Merge Visible and Flatten.
  - 18 blend modes: Normal, Multiply, Screen, Overlay, Darken, Lighten, Difference, Add,
    Subtract, Color Dodge, Color Burn, Soft Light, Hard Light, Exclusion, Hue, Saturation,
    Color and Luminosity.
- **Select**: All, Deselect, Inverse, Color Range, Layer Transparency, and Modify: Border,
  Smooth, Expand, Contract and Feather.
- **Filter**:
  - Blur: Gaussian, Box, Motion, Radial and Blur.
  - Sharpen: Unsharp Mask, Sharpen and Sharpen More.
  - Noise: Add Noise, Median and Despeckle.
  - Distort: Twirl, Pinch, Spherize, Ripple and Wave.
  - Stylize: Emboss, Find Edges, Solarize, Oil Paint and Mosaic.
  - Render: Clouds and Vignette.
  - Other: High Pass, Minimum, Maximum and Offset.
- **View**: Zoom In, Zoom Out, Fit on Screen, 100%, 200%, 50%, Rulers, Grid, Guides, Snap,
  Grid Settings, New Guide, Clear Guides, and Dark Interface (on by default; turn it off for a
  light grey look).
- **Window**: Next and Previous Document, Palette, Brush Settings and DPaint Brush.

The menu lists above are 0.2's; the New in sections above add to them (the Layer menu now has
New Adjustment Layer, New Fill Layer, Layer Mask, Clipping, Group, Align and Lock; Image has
Mode; Edit has the brush and pattern commands).

## Files

| Format | Opens | Saves |
|---|---|---|
| OpenRaster (.ora) | layers and groups, names, opacity, blend modes, visibility, locks, masks, clipping, adjustment and fill layers | the same: Easel's own format, which GIMP, Krita and MyPaint also open |
| PNG | every kind, with transparency | 8-bit RGB or RGBA, palette or grey by the picture's mode, with resolution and File Info |
| JPEG | baseline and progressive | baseline, quality 1 to 100 |
| BMP | 1, 4, 8, 16, 24 and 32-bit, RLE | 24-bit, or 32-bit with transparency |
| IFF ILBM | 1 to 8 planes, EHB, HAM6, HAM8, 24 and 32-bit deep, PBM; a palette picture opens indexed, with its colour cycling | 24-bit, 256 colours, or with the Amiga options: 1 to 8 planes, EHB, HAM6/HAM8, SHAM, mask, cycling, ModeID |
| GIF | the first frame, with transparency; every frame as layers (Import Frames to Layers) | 256 colours or the picture's palette; animated, from the layers |
| TIFF (saving) | see the full list below | LZW: RGB(A), grey, palette, 1-bit |
| Targa (saving) | see the full list below | run-length: 24/32-bit, grey, colour-mapped |
| Photoshop PSD | layers (8 and 16-bit RGB and grey), else the flattened picture | layers, masks, groups, blend modes, and the merged picture |
| AVIF | 8, 10 and 12-bit, 4:2:0, 4:2:2, 4:4:4 and grey, transparency, tiled (grid) pictures, rotation and mirroring | — |
| WebP | lossy, lossless, with transparency; the first frame of an animation | lossless |
| TIFF | the first page: uncompressed, PackBits, LZW, Deflate, CCITT fax, JPEG; 1 to 32-bit and float; grey, palette, RGB, CMYK, Lab; strips or tiles | — |
| Photoshop PSD/PSB (other kinds) | the flattened picture: 1 to 32-bit, RGB, CMYK, Lab, grey, indexed | — |
| Targa, PCX, QOI | every common kind | Targa (above) |
| PBM/PGM/PPM/PAM/PFM, XPM | every kind | — |
| DDS | uncompressed and BC1 to BC5, BC7 (the top level of a 2D texture) | — |
| Windows ICO/CUR | the biggest picture in the file | — |
| Radiance HDR, FITS, DPX | tone-mapped (HDR) or scaled to 8 bits | — |
| IFF ACBM, RGBN, RGB8, DEEP | 1 to 8 planes, EHB, HAM, deep; run-length kinds | — |
| IFF ILBM with SHAM, CTBL or PCHG | the palette changing line by line, as shown on the Amiga | — |
| IFF ANIM | the first frame; every frame as layers (op 0 and 5) | from the layers (op 5) |
| PDF | — | one page: the picture lossless or as a JPEG |
| PBM/PGM/PPM/PAM, QOI, ICO, DDS, DPX, FITS (saving) | see the rows above | PBM/PGM/PPM/PAM; QOI; icons 16-256; DDS BC1, BC3 or uncompressed with mipmaps; DPX 8 or 10 bits; 8-bit FITS |
| SVG | as shape layers (paths and basic shapes, groups, transforms, fill, stroke) | the paths and shape layers |
| Workbench icons (.info) | classic (1.x, 2.x and MagicWB colours), NewIcons, GlowIcons, PNG icons | — |
| Sun raster, SGI, Degas, NEOchrome | every common kind | — |

Saving a picture with layers in a flat format offers to save it as .ora instead, or to flatten
it. File > Export always writes a flat copy and leaves the picture's own file alone.

## Settings

Easel keeps a few settings in `PROGDIR:Easel.prefs`: the dark interface, the public screen and
the last drawer used. Start it with `PUBSCREEN=name` (or a PUBSCREEN tool type) to open on
another screen. `DEBUG=file` writes a log.

## Not included

Easel leaves out printing and ARexx. Animated brushes and drawing guides by dragging from the
rulers may come later.

## Credits and licences

Easel itself is under the GNU General Public License, version 2 (GPL-2.0-only); see `LICENSE`.

Easel includes other people's work: dav1d by VideoLAN and the dav1d authors (BSD licence),
which decodes AVIF; picture readers ported from PhotoCraft by the ArtCraft Team and the
PhotoCraft contributors (MIT licence); and the Liberation, Carlito and Caladea fonts (SIL Open
Font License). Licenses.txt, next to Easel, lists them all with their licence texts.

## Source

The Source drawer holds Easel's C source. `core/` is portable C99: the document, layers and blend
modes, undo, painting, selections, adjustments, filters, text and the file formats. `av1/` is dav1d
(the AV1 decoder for AVIF), adapted for vbcc. It has native tests (`tests/coretest.c`,
`tests/codectest.c`, `tests/rasterstest.c`, `tests/amigafmttest.c`, `tests/layertest.c`,
`tests/quicktest.c`, `tests/palettetest.c`, `tests/brushtest.c`, `tests/check_avif.py` and
`tests/check_palette.py`) run under the address and undefined-behaviour sanitizers. `amiga/` is the MUI front end. `build.sh` runs the tests and builds Easel with vbcc.
