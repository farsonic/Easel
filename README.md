# Easel 0.2

Easel is a picture editor for the Amiga, laid out the way PhotoCraft is. The options bar runs
along the top, the tools down the left, and open pictures are tabs over the canvas. The Color,
History and Layers panels sit down the right, the status bar along the bottom, and the menus
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
| Lasso / Polygonal Lasso | L | freehand; or click the corners, then double-click or press Return to close (Esc drops it) |
| Magic Wand | W | tolerance, contiguous, sample all layers |
| Crop | C | drag a box and adjust its handles; Return or a double-click inside crops, Esc cancels |
| Eyedropper | I | point, 3 × 3 or 5 × 5 sample, current or all layers; Alt-click sets the background colour |
| Clone Stamp | S | Alt-click sets the source, then paint |
| Brush / Pencil | B | size, hardness, blend mode, opacity, flow; Shift-click paints a straight line from the last stroke; Alt picks a colour |
| Eraser | E | erases to transparency |
| Gradient / Paint Bucket | G | linear, radial, angle, reflected, diamond; foreground to background or to transparent |
| Blur / Sharpen / Smudge | R | strength |
| Dodge / Burn / Sponge | O | exposure |
| Text | T | click where the text goes, then type it (a bar, \|, starts a new line); font, size, alignment; the text becomes a layer of its own |
| Rectangle / Ellipse / Line | U | corner radius, line weight, opacity |
| Hand | H | drag to scroll (or hold the space bar with any tool) |
| Zoom | Z | click to zoom in, Alt-click to zoom out |

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
- **View**: Zoom In, Zoom Out, Fit on Screen, 100%, 200%, 50%, and Dark Interface (on by
  default; turn it off for a light grey look).
- **Window**: Next and Previous Document.

## Files

| Format | Opens | Saves |
|---|---|---|
| OpenRaster (.ora) | layers, names, opacity, blend modes, visibility, locks | the same: Easel's own format, which GIMP, Krita and MyPaint also open |
| PNG | every kind, with transparency | 8-bit RGB or RGBA, with resolution |
| JPEG | baseline and progressive | baseline, quality 1 to 100 |
| BMP | 1, 4, 8, 16, 24 and 32-bit, RLE | 24-bit, or 32-bit with transparency |
| IFF ILBM | 1 to 8 planes, EHB, HAM6, HAM8, 24 and 32-bit deep, PBM | 24-bit, or 8-bit (256 colours) |
| GIF | the first frame, with transparency | — |
| AVIF | 8, 10 and 12-bit, 4:2:0, 4:2:2, 4:4:4 and grey, transparency, tiled (grid) pictures, rotation and mirroring | — |
| WebP | lossy, lossless, with transparency; the first frame of an animation | — |
| TIFF | the first page: uncompressed, PackBits, LZW, Deflate, CCITT fax, JPEG; 1 to 32-bit and float; grey, palette, RGB, CMYK, Lab; strips or tiles | — |
| Photoshop PSD/PSB | the flattened picture (not its layers), 1 to 32-bit, RGB, CMYK, Lab, grey, indexed | — |
| Targa, PCX, QOI | every common kind | — |
| PBM/PGM/PPM/PAM/PFM, XPM | every kind | — |
| DDS | uncompressed and BC1 to BC5, BC7 (the top level of a 2D texture) | — |
| Windows ICO/CUR | the biggest picture in the file | — |
| Radiance HDR, FITS, DPX | tone-mapped (HDR) or scaled to 8 bits | — |
| IFF ACBM, RGBN, RGB8, DEEP | 1 to 8 planes, EHB, HAM, deep; run-length kinds | — |
| IFF ILBM with SHAM, CTBL or PCHG | the palette changing line by line, as shown on the Amiga | — |
| IFF ANIM | the first frame | — |
| Workbench icons (.info) | classic (1.x, 2.x and MagicWB colours), NewIcons, GlowIcons, PNG icons | — |
| Sun raster, SGI, Degas, NEOchrome | every common kind | — |

Saving a picture with layers in a flat format offers to save it as .ora instead, or to flatten
it. File > Export always writes a flat copy and leaves the picture's own file alone.

## Settings

Easel keeps a few settings in `PROGDIR:Easel.prefs`: the dark interface, the public screen and
the last drawer used. Start it with `PUBSCREEN=name` (or a PUBSCREEN tool type) to open on
another screen. `DEBUG=file` writes a log.

## Not included

Easel leaves out printing and ARexx. Copying pictures to other programs through the clipboard,
and adjustment layers, may come later.

## Credits and licences

Easel includes other people's work: dav1d by VideoLAN and the dav1d authors (BSD licence),
which decodes AVIF; picture readers ported from PhotoCraft by the ArtCraft Team and the
PhotoCraft contributors (MIT licence); and the Liberation, Carlito and Caladea fonts (SIL Open
Font License). Licenses.txt, next to Easel, lists them all with their licence texts.

## Source

The Source drawer holds Easel's C source. `core/` is portable C99: the document, layers and blend
modes, undo, painting, selections, adjustments, filters, text and the file formats. `av1/` is dav1d
(the AV1 decoder for AVIF), adapted for vbcc. It has native tests (`tests/coretest.c`,
`tests/codectest.c`, `tests/rasterstest.c`, `tests/amigafmttest.c`, and `tests/check_avif.py`) run under the address and undefined-behaviour
sanitizers. `amiga/` is the MUI front end. `build.sh` runs the tests and builds Easel with vbcc.
