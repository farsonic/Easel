/*
 * main.c - Easel, a picture editor for the Amiga (MUI) laid out as PhotoCraft is: the options bar
 * along the top, the tools down the left, the documents as tabs over the canvas, the Color,
 * Layers and History panels down the right, the status bar along the bottom, and the menus.
 *
 * This file holds the window, the menus and their commands, the open documents, the files
 * (opening and saving through core/codec.c), and the program's start and end.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <workbench/startup.h>
#include <libraries/asl.h>
#include <libraries/gadtools.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <proto/icon.h>
#include <clib/alib_protos.h>
#include "easel.h"
#include "theme.h"
#include "ctxmui.h"
#include "lang.h"
#include "ids.h"
#include "fxids.h"                    /* 1.1 */
#include "mxids.h"                    /* 1.2 */
#include "r3ids.h"                    /* r3-image */
#include "../core/vecx.h"
#include "../core/viewx.h"
#include "../core/fx.h"
#include "../core/layerops.h"
#include "splash.h"
#include "sysfonts.h"
#include "../core/codec.h"
#include "../core/image.h"
#include "../core/layers.h"
#include "../core/toolopts.h"
#include "../core/selmore.h"
#include "../core/palette.h"
#include "../core/amigamodes.h"
#include "../core/guides.h"
#include "../core/swatch.h"
#include "../core/xwrite.h"
#include "../core/xread2.h"                  /* 1.2: Cineon, HDR */
#include "../core/exportx.h"                 /* 1.2: Export As */
#include "../core/resample.h"
#include "../core/frames.h"
#include "../core/scripts.h"
#include "../core/svg.h"
#include "../core/type.h"
#include "palwin.h"
#include "modedlg.h"
#include "brushwin.h"
#include "gradwin.h"
#include "swatchwin.h"
#include "colorpick.h"
#include "clipboard.h"
#include "dtload.h"
#include "appdrop.h"
#include "../core/iconwrite.h"
#include "../core/channels.h"
#include "../core/xform.h"
#include "../core/crop.h"
#include "../core/matte.h"
#include "../core/util.h"
#include "../core/type.h"
#include "../core/amifont.h"
#include "../core/path.h"
#include "../core/pathops.h"
#include "../core/shape.h"
#include "../core/live.h"
#include "../core/gallery.h"
#include "dlgx.h"
#include "tools2.h"                     /* 1.2 */
#include "tools2ids.h"
#include "../core/layerx.h"
#include "../core/cafill.h"
#include "colorui.h"                    /* 1.2 */
#include "../core/colorx.h"

#ifndef MUIA_Window_DisableKeys
#define MUIA_Window_DisableKeys 0x80424c36UL
#endif
#ifndef MUIKEYF_WINDOW_CLOSE
#define MUIKEYF_WINDOW_CLOSE (1UL << MUIKEY_WINDOW_CLOSE)
#endif

const char verstag[] = "$VER: Easel 1.0 (28.10.2026) a picture editor for the Amiga (MUI)";

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *MUIMasterBase, *LayersBase, *DiskfontBase, *KeymapBase, *UtilityBase, *CyberGfxBase;
extern struct WBStartup *_WBenchMsg;

FILE *qlog;
char pub_screen[64];
Object *app, *win, *canvas, *toolbox, *optbar, *colorpanel, *layerpanel, *historypanel, *statusbar, *doctabs;
static Object *vscroll, *hscroll;
FontSet fonts;
int pref_dark = 1;
int pref_autosave = 5;                   /* minutes between autosaves (0: none) */
int pref_icons = 1;                      /* a Workbench icon written beside a saved picture that has none */
DocView docs[MAXDOCS];
int ndocs, curdoc = -1;
int t2_spare = -1;                       /* 1.2: (tools2.h) the spare page: the picture shown before */
Opts opt;
int tool = TL_BRUSH;
static Clip *clip;                       /* what Easel copied last */
static Object *navpanel, *infopanel;     /* (0.6) the Navigator and Info panels */
static Object *chanpanel;                /* (0.7) the Channels panel */
static Object *pathpanel;                /* (0.9) the Paths panel */
static Object *rightcol;                 /* the panels' column (Tab hides it) */
static Doc *toggled;                     /* Toggle Last State: the picture just stepped back */
static int fonts_assigned;
static int untitled;
static LONG win_box[4];

const char *const tool_names[TL_N] = {
  "Move Tool", "Rectangular Marquee Tool", "Elliptical Marquee Tool", "Lasso Tool", "Polygonal Lasso Tool", "Magic Wand Tool",
  "Crop Tool", "Eyedropper Tool", "Clone Stamp Tool", "Brush Tool", "Pencil Tool", "Eraser Tool", "Paint Bucket Tool",
  "Gradient Tool", "Blur Tool", "Sharpen Tool", "Smudge Tool", "Dodge Tool", "Burn Tool", "Sponge Tool", "Horizontal Type Tool",
  "Rectangle Tool", "Ellipse Tool", "Line Tool", "Hand Tool", "Zoom Tool",
  "Pattern Stamp Tool", "History Brush Tool", "Color Replacement Tool", "Background Eraser Tool", "Magic Eraser Tool",
  "Magnetic Lasso Tool", "Ruler Tool",
  "Color Sampler Tool",
  "Spot Healing Brush Tool", "Healing Brush Tool", "Patch Tool", "Red Eye Tool",     /* 1.0 */
  "Horizontal Type Mask Tool", "Vertical Type Tool", "Pen Tool", "Add Anchor Point Tool", "Delete Anchor Point Tool", "Convert Point Tool",
  "Path Selection Tool", "Direct Selection Tool", "Rounded Rectangle Tool", "Polygon Tool", "Custom Shape Tool",
  /* 1.2 */
  "Quick Selection Tool", "Object Selection Tool", "Perspective Crop Tool", "Slice Tool", "Slice Select Tool", "Frame Tool",
  "Note Tool", "Count Tool", "Mixer Brush Tool", "Art History Brush Tool", "Remove Tool", "Content-Aware Move Tool",
  "Rotate View Tool", "Artboard Tool"
};
char tool_keys[TL_N] = { 'V', 'M', 'M', 'L', 'L', 'W', 'C', 'I', 'S', 'B', 'B', 'E', 'G', 'G', 'R', 'R', 'R', 'O', 'O', 'O', 'T', 'U', 'U', 'U', 'H', 'Z',
  'S', 'Y', 'B', 'E', 'E', 'L', 'I', 'I', 'J', 'J', 'J', 'J',
  'T', 'T', 'P', 'P', 'P', 'P', 'A', 'A', 'U', 'U', 'U',
  'W', 'W', 'C', 'C', 'C', 'K', 'I', 'I', 'B', 'Y', 'J', 'J', 'R', 'V' };   /* 1.2 */

/* ------------------------------------------------------------------ the menus */
#define M(title) { NM_TITLE, (STRPTR)title, 0, 0, 0, 0 }
#define I(label, key, id) { NM_ITEM, (STRPTR)label, (STRPTR)key, 0, 0, (APTR)(id) }
#define H(label) { NM_ITEM, (STRPTR)label, 0, 0, 0, 0 }
#define S(label, key, id) { NM_SUB, (STRPTR)label, (STRPTR)key, 0, 0, (APTR)(id) }
#define T(label, key, id) { NM_ITEM, (STRPTR)label, (STRPTR)key, CHECKIT | MENUTOGGLE, 0, (APTR)(id) }
#define BAR { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 }
#define LC(label, k) { NM_SUB, (STRPTR)label, 0, CHECKIT, 0, (APTR)(ID_LABEL0 + (k)) }   /* 1.1: a colour label */
#define SBAR { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 }
#define ST(label, id) { NM_SUB, (STRPTR)label, 0, CHECKIT | MENUTOGGLE, 0, (APTR)(id) }   /* (1.1: a ticked subitem) */
#define RC(label, id) { NM_SUB, (STRPTR)label, 0, CHECKIT, 0, (APTR)(id) }                /* (1.2: one of a set, ticked) */
static struct NewMenu menus[] = {
  M("File"),
  I("New...", "N", ID_NEW), I("Open...", "O", ID_OPEN),
  H("Open Recent"), S("1", 0, ID_RECENT0), S("2", 0, ID_RECENT0 + 1), S("3", 0, ID_RECENT0 + 2), S("4", 0, ID_RECENT0 + 3), S("5", 0, ID_RECENT0 + 4),
  S("6", 0, ID_RECENT0 + 5), S("7", 0, ID_RECENT0 + 6), S("8", 0, ID_RECENT0 + 7), S("9", 0, ID_RECENT0 + 8), S("10", 0, ID_RECENT0 + 9), SBAR,
  S("Clear Recent List", 0, ID_CLEARRECENT),
  I("Open as Smart Object...", 0, ID_OPENSMART),                                         /* 1.2 */
  I("Place as Layer...", 0, ID_PLACE), I("Load Files into Stack...", 0, ID_STACK), I("Import Frames to Layers...", 0, ID_IMPFRAMES), BAR,
  I("Close", "W", ID_CLOSE), I("Close All", 0, ID_CLOSEALL), I("Close Others", 0, ID_CLOSEOTHERS), BAR,
  I("Save", "S", ID_SAVE), I("Save As...", 0, ID_SAVEAS), I("Save a Copy...", 0, ID_SAVECOPY), I("Revert", 0, ID_REVERT),
  H("Export"), S("Export As...", 0, ID_EXPORTAS), S("Layers to Files...", 0, ID_LAYERSTOFILES), SBAR, S("PNG...", 0, ID_EXPPNG), S("JPEG...", 0, ID_EXPJPEG), S("BMP...", 0, ID_EXPBMP), S("GIF...", 0, ID_EXPGIF), S("TIFF...", 0, ID_EXPTIFF), S("Targa...", 0, ID_EXPTGA), S("Photoshop PSD...", 0, ID_EXPPSD),
  S("WebP...", 0, ID_EXPXW0), S("PDF...", 0, ID_EXPXW0 + 1), S("QOI...", 0, ID_EXPXW0 + 2), S("Netpbm (PPM, PGM, PBM, PAM)...", 0, ID_EXPXW0 + 3),
  S("Windows Icon...", 0, ID_EXPXW0 + 4), S("DDS Texture...", 0, ID_EXPXW0 + 5), S("DPX...", 0, ID_EXPXW0 + 6), S("FITS...", 0, ID_EXPXW0 + 7),
  S("Cineon...", 0, ID_EXPXW0 + 8), S("Radiance HDR...", 0, ID_EXPXW0 + 9), SBAR,                 /* 1.2 */
  S("Layers as Animated GIF...", 0, ID_EXPANIMGIF), S("Layers as IFF ANIM...", 0, ID_EXPANIM), S("Quick Export as PNG", 0, ID_QUICKPNG), S("Paths to SVG...", 0, ID_EXPSVG), S("Layer Comps to Files...", 0, ID_COMPSTOFILES), SBAR,
  S("IFF ILBM (24-bit)...", 0, ID_EXPILBM), S("IFF ILBM (256 colours)...", 0, ID_EXPILBM8), S("IFF ILBM (Amiga options)...", 0, ID_EXPILBMOPT),
  /* 1.2: slices, artboards, and data for programmers */
  H("Export More"), S("Slices as PNG...", 0, ID_EXPSLICES), S("Artboards as PNG...", 0, ID_EXPARTBOARDS), SBAR,
  S("Raw Bitplanes and Palette...", 0, ID_EXPRAW), S("Bitplanes as C...", 0, ID_EXPCSRC), S("Bitplanes as Assembler...", 0, ID_EXPASM), SBAR,
  S("Sprites as C...", 0, ID_EXPSPRC), S("Sprites as Assembler...", 0, ID_EXPSPRASM), SBAR,
  S("Copper Gradient as C...", 0, ID_EXPCOPC), S("Copper Gradient as Assembler...", 0, ID_EXPCOPASM), BAR,
  H("Scripts"), S("Delete All Empty Layers", 0, ID_DELEMPTY), S("Flatten All Masks", 0, ID_FLATMASKS), S("Fit Image...", 0, ID_FITIMAGE), S("Flatten All Layer Effects", 0, ID_FLATTENFX),
  H("Automate"), S("Batch...", 0, ID_BATCH), S("Conditional Mode Change...", 0, ID_CONDMODE),
  I("File Info...", 0, ID_FILEINFO), BAR,
  I("About Easel...", "?", ID_ABOUT), I("Quit", "Q", ID_QUIT),
  M("Edit"),
  I("Undo", "Z", ID_UNDO), I("Redo", "Y", ID_REDO), I("Toggle Last State", 0, ID_TOGGLELAST), I("Step Backward", 0, ID_STEPBACK), I("Step Forward", 0, ID_STEPFWD), BAR,
  I("Fade...", 0, ID_FADE), BAR,
  I("Cut", "X", ID_CUT), I("Copy", "C", ID_COPY), I("Copy Merged", 0, ID_COPYMERGED), I("Paste", "V", ID_PASTE), I("Paste in Place", 0, ID_PASTEINPLACE),
  I("Paste Into", 0, ID_PASTEINTO), I("Paste Outside", 0, ID_PASTEOUTSIDE), I("Paste from Amiga Clipboard", 0, ID_PASTEAMIGA), I("Clear", 0, ID_CLEAR), BAR,
  I("Search...", 0, ID_SEARCH), BAR,
  I("Fill...", 0, ID_FILL), I("Fill with Foreground", 0, ID_FILLFG), I("Fill with Background", 0, ID_FILLBG), I("Content-Aware Fill...", 0, ID_CAFILL), I("Stroke...", 0, ID_STROKE), BAR,
  I("Define Brush Preset...", 0, ID_DEFBRUSH), I("Pick Up Brush", 0, ID_PICKUPBRUSH), I("Define Pattern...", 0, ID_DEFPATTERN), I("Fill with Pattern", 0, ID_FILLPATTERN),
  H("Presets"), S("Load Patterns (.pat)...", 0, ID_LOADPAT), S("Load Gradients (.grd)...", 0, ID_LOADGRD), SBAR,      /* 1.2 */
  S("Save Brushes as .abr...", 0, ID_SAVEABR), S("Save Patterns as .pat...", 0, ID_SAVEPAT), BAR,
  I("Free Transform", "T", ID_FREETRANSFORM), I("Numeric Transform...", 0, ID_NUMTRANSFORM),
  H("Transform"), S("Again", 0, ID_XF_AGAIN), SBAR, S("Scale", 0, ID_XF_SCALE), S("Rotate", 0, ID_XF_ROTATE), S("Skew", 0, ID_XF_SKEW),
  S("Distort", 0, ID_XF_DISTORT), S("Perspective", 0, ID_XF_PERSPECTIVE), SBAR, S("Rotate 180\260", 0, ID_ROTATE180), S("Rotate 90\260 Clockwise", 0, ID_ROTATE90R), S("Rotate 90\260 Anticlockwise", 0, ID_ROTATE90L), SBAR,
  S("Flip Horizontal", 0, ID_FLIPLH), S("Flip Vertical", 0, ID_FLIPLV), BAR,
  I("Take Snapshot (History Brush)", 0, ID_SNAPSHOT), I("History Options...", 0, ID_HISTOPTS), I("Rename Snapshot...", 0, ID_SNAPRENAME), H("Purge"), S("Undo History", 0, ID_PURGE), S("Clipboard", 0, ID_PURGECLIP), S("All", 0, ID_PURGEALL), BAR,
  I("Autosave...", 0, ID_AUTOSAVESET), T("Save Icons with Pictures", 0, ID_ICONPREF), BAR,
  I("Color Settings...", 0, ID_COLORSET), I("Assign Profile...", 0, ID_ASSIGNPROF), I("Convert to Profile...", 0, ID_CONVERTPROF), BAR,   /* 1.2 */
  I("Keyboard Shortcuts...", 0, ID_KEYS), I("Preferences...", "K", ID_PREFS),
  M("Image"),
  H("Mode"), S("RGB Color", 0, ID_MODE_RGB), S("Grayscale", 0, ID_MODE_GRAY), S("Indexed Color...", 0, ID_MODE_INDEXED), S("Bitmap...", 0, ID_MODE_BITMAP), SBAR,
  S("CMYK Color", 0, ID_MODE_CMYK), S("Lab Color", 0, ID_MODE_LAB), SBAR,       /* 1.2 */
  S("Color Table...", 0, ID_COLORTABLE), S("Amiga Screen Mode...", 0, ID_SCREENMODE),
  H("Adjustments"),
  S("Brightness/Contrast...", 0, ID_ADJ_BC), S("Levels...", "L", ID_ADJ_LEVELS), S("Curves...", "M", ID_ADJ_CURVES), S("Exposure...", 0, ID_ADJ_EXPOSURE), SBAR,
  S("Vibrance...", 0, ID_ADJ_VIBRANCE), S("Hue/Saturation...", "U", ID_ADJ_HUESAT), S("Color Balance...", "B", ID_ADJ_BALANCE), S("Black & White...", 0, ID_ADJ_BW),
  S("Photo Filter...", 0, ID_ADJ_PHOTO), S("Channel Mixer...", 0, ID_ADJ_MIXER), S("Color Lookup...", 0, ID_FX_LOOKUP), S("Gradient Map...", 0, ID_ADJ_GRADMAP), S("Selective Color...", 0, ID_ADJ_SELCOLOR), SBAR,
  S("Invert", "I", ID_ADJ_INVERT), S("Posterize...", 0, ID_ADJ_POSTERIZE), S("Threshold...", 0, ID_ADJ_THRESHOLD), SBAR,
  S("Desaturate", 0, ID_ADJ_DESAT), S("Equalize", 0, ID_ADJ_EQUALIZE), SBAR,
  S("Shadows/Highlights...", 0, ID_ADJ_SHADOWHI), S("HDR Toning...", 0, ID_HDRTONING), S("Match Color...", 0, ID_ADJ_MATCH), S("Replace Color...", 0, ID_ADJ_REPLACE),     /* 1.0; r3: HDR Toning */
  I("Auto Tone", 0, ID_AUTOTONE), I("Auto Contrast", 0, ID_AUTOCONTRAST), I("Auto Color", 0, ID_AUTOCOLOR), BAR,
  I("Image Size...", 0, ID_IMAGESIZE), I("Canvas Size...", 0, ID_CANVASSIZE),
  H("Image Rotation"), S("180\260", 0, ID_IMG_R180), S("90\260 Clockwise", 0, ID_IMG_R90R), S("90\260 Anticlockwise", 0, ID_IMG_R90L), S("Arbitrary...", 0, ID_IMG_RANY), SBAR,
  S("Flip Canvas Horizontal", 0, ID_IMG_FLIPH), S("Flip Canvas Vertical", 0, ID_IMG_FLIPV),
  I("Crop", 0, ID_CROP), I("Trim...", 0, ID_TRIMOPT), BAR, I("Duplicate", 0, ID_DUPIMAGE),
  M("Layer"),
  H("New"), S("Layer", 0, ID_NEWLAYER), S("Layer...", 0, ID_NEWLAYERDLG), S("Group", 0, ID_NEWGROUP), SBAR, S("Layer via Copy", 0, ID_LAYERFROMSEL), S("Layer via Cut", 0, ID_LAYERCUTSEL),
  SBAR, S("Background from Layer", 0, ID_BGFROMLAYER),
  I("Duplicate Layer", "J", ID_DUPLAYER), I("Delete Layer", 0, ID_DELLAYER), I("Delete Hidden Layers", 0, ID_DELHIDDEN), I("Layer Properties...", 0, ID_LAYERPROPS),
  I("Quick Export as PNG", 0, ID_LAYERQPNG),
  /* 1.1: layer styles, colour labels */
  H("Layer Style"), S("Blending Options...", 0, ID_FXBLENDOPT), SBAR, S("Drop Shadow...", 0, ID_FX0 + FX_DROPSHADOW), S("Inner Shadow...", 0, ID_FX0 + FX_INNERSHADOW),
  S("Outer Glow...", 0, ID_FX0 + FX_OUTERGLOW), S("Inner Glow...", 0, ID_FX0 + FX_INNERGLOW), S("Bevel & Emboss...", 0, ID_FX0 + FX_BEVEL),
  S("Satin...", 0, ID_FX0 + FX_SATIN), S("Color Overlay...", 0, ID_FX0 + FX_COLOR), S("Gradient Overlay...", 0, ID_FX0 + FX_GRADIENT),
  S("Stroke...", 0, ID_FX0 + FX_STROKE), S("Pattern Overlay...", 0, ID_FXPATTERN), SBAR, S("Copy Layer Style", 0, ID_FXCOPY), S("Paste Layer Style", 0, ID_FXPASTE), S("Clear Layer Style", 0, ID_FXCLEAR), SBAR,
  S("Global Light...", 0, ID_FXLIGHT), S("Create Layers", 0, ID_FXLAYERS), { NM_SUB, (STRPTR)"Hide All Effects", 0, CHECKIT | MENUTOGGLE, 0, (APTR)ID_FXHIDEALL },
  S("Scale Effects...", 0, ID_FXSCALE),
  H("Layer Color"), LC("None", 0), LC("Red", 1), LC("Orange", 2), LC("Yellow", 3), LC("Green", 4), LC("Blue", 5), LC("Violet", 6), LC("Gray", 7),
  LC("Seafoam", 8), LC("Indigo", 9), LC("Magenta", 10), LC("Fuchsia", 11),
  I("Copy CSS", 0, ID_COPYCSS), BAR,
  H("New Adjustment Layer"),
  S("Brightness/Contrast...", 0, ID_NEWADJ0 + AK_BRIGHTNESS), S("Levels...", 0, ID_NEWADJ0 + AK_LEVELS), S("Curves...", 0, ID_NEWADJ0 + AK_CURVES),
  S("Exposure...", 0, ID_NEWADJ0 + AK_EXPOSURE), SBAR, S("Vibrance...", 0, ID_NEWADJ0 + AK_VIBRANCE), S("Hue/Saturation...", 0, ID_NEWADJ0 + AK_HUESAT),
  S("Color Balance...", 0, ID_NEWADJ0 + AK_COLORBALANCE), S("Black & White...", 0, ID_NEWADJ0 + AK_BLACKWHITE), S("Photo Filter...", 0, ID_NEWADJ0 + AK_PHOTOFILTER),
  S("Channel Mixer...", 0, ID_NEWADJ0 + AK_CHANNELMIXER), SBAR, S("Invert", 0, ID_NEWADJ0 + AK_INVERT), S("Posterize...", 0, ID_NEWADJ0 + AK_POSTERIZE),
  S("Threshold...", 0, ID_NEWADJ0 + AK_THRESHOLD), S("Gradient Map...", 0, ID_NEWADJ0 + AK_GRADIENTMAP), S("Selective Color...", 0, ID_NEWADJ0 + AK_SELECTIVECOLOR),
  S("Replace Color...", 0, ID_NEWADJ0 + AK_REPLACECOLOR),
  H("New Fill Layer"), S("Solid Color...", 0, ID_NEWFILLSOLID), S("Gradient...", 0, ID_NEWFILLGRAD), S("Pattern...", 0, ID_NEWFILLPAT),
  I("Layer Content Options...", 0, ID_FILLSETTINGS),
  H("Type"), S("Edit Type", 0, ID_EDITTYPE), S("Character...", 0, ID_TYPECHAR), S("Paragraph...", 0, ID_TYPEPARA), SBAR, S("Create Work Path", 0, ID_TYPEWORKPATH),
  S("Warp Text...", 0, ID_WARPTEXT), S("Paste Lorem Ipsum", 0, ID_LOREM), SBAR, S("Convert to Paragraph Text", 0, ID_TOPARA), S("Convert to Point Text", 0, ID_TOPOINT), S("Horizontal", 0, ID_TYPEHORIZ), S("Vertical", 0, ID_TYPEVERT), SBAR, S("Rasterize Type", 0, ID_RASTERTYPE2),
  S("Convert to Shape", 0, ID_TOSHAPE), S("Update All Text Layers", 0, ID_UPDATETEXT),                 /* 1.2 */
  SBAR, ST("Standard Ligatures", ID_OT0), ST("Contextual Alternates", ID_OT0 + 9), ST("Discretionary Ligatures", ID_OT0 + 1), ST("Swash", ID_OT0 + 2),   /* 1.2: OpenType */
  ST("Oldstyle", ID_OT0 + 3), ST("Stylistic Alternates", ID_OT0 + 4), ST("Titling Alternates", ID_OT0 + 5), ST("Ornaments", ID_OT0 + 6), ST("Ordinals", ID_OT0 + 7),
  ST("Fractions", ID_OT0 + 8),
  I("Shape Properties...", 0, ID_SHAPEPROPS),
  H("Combine Shapes"), S("Unite Shapes", 0, ID_COMBINE0 + CS_UNITE), S("Subtract Front Shape", 0, ID_COMBINE0 + CS_SUBTRACT),      /* r3 */
  S("Intersect Shape Areas", 0, ID_COMBINE0 + CS_INTERSECT), S("Exclude Overlapping Shapes", 0, ID_COMBINE0 + CS_EXCLUDE), SBAR,
  S("Merge Shape Components", 0, ID_COMBINE0 + CS_MERGE), BAR,
  H("Layer Mask"), S("Reveal All", 0, ID_MASK_REVEALALL), S("Hide All", 0, ID_MASK_HIDEALL), S("Reveal Selection", 0, ID_MASK_REVEALSEL),
  S("Hide Selection", 0, ID_MASK_HIDESEL), S("From Transparency", 0, ID_MASK_ALPHA), SBAR, S("Delete", 0, ID_MASK_DELETE), S("Apply", 0, ID_MASK_APPLY), SBAR,
  S("Enable/Disable", 0, ID_MASK_ENABLE), S("Link/Unlink", 0, ID_MASK_LINK), S("Invert Mask", 0, ID_MASK_INVERT), S("Edit Mask/Layer", "\\", ID_MASK_EDIT),
  H("Vector Mask"), S("Reveal All", 0, ID_VM_REVEAL), S("Hide All", 0, ID_VM_HIDE), S("Current Path", 0, ID_VM_PATH), SBAR,      /* 1.2 */
  S("Delete", 0, ID_VM_DELETE), S("Enable/Disable", 0, ID_VM_ENABLE), S("Link/Unlink", 0, ID_VM_LINK), S("Rasterize", 0, ID_VM_RASTER),
  I("Create/Release Clipping Mask", "G", ID_CLIP), BAR,
  H("Smart Objects"), S("Convert to Smart Object", 0, ID_SO_CONVERT), S("New Smart Object via Copy", 0, ID_SO_COPY), SBAR,      /* 1.2 */
  S("Edit Contents", 0, ID_SO_EDIT), S("Show in Drawer", 0, ID_SO_SHOW), S("Update Modified Content", 0, ID_SO_UPDATE), S("Update All Modified Content", 0, ID_SO_UPDATEALL),
  S("Replace Contents...", 0, ID_SO_REPLACE), S("Relink to File...", 0, ID_SO_RELINK), S("Export Contents...", 0, ID_SO_EXPORT), SBAR,
  S("Convert to Linked...", 0, ID_SO_TOLINKED), S("Convert to Embedded", 0, ID_SO_TOEMBEDDED), S("Convert to Layers", 0, ID_SO_TOLAYERS), SBAR,
  S("Reset Transform", 0, ID_SO_RESET), S("Rasterize", 0, ID_SO_RASTER),
  H("Links and Artboards"), S("Link Layers", 0, ID_LINK), S("Unlink Layers", 0, ID_UNLINK), S("Select Linked Layers", 0, ID_SELLINKED), SBAR,
  S("New Artboard", 0, ID_NEWARTBOARD), S("Artboard from Group", 0, ID_ARTBOARDGROUP), S("Place in Frame", 0, ID_FRAMEPLACE),
  I("Group Layers", 0, ID_GROUPLAYERS), I("Ungroup Layers", 0, ID_UNGROUP),
  H("Rasterize"), S("Type", 0, ID_RASTERTYPE), S("Shape", 0, ID_RASTERSHAPE), S("Layer", 0, ID_RASTERIZE), S("Layer Style", 0, ID_FXRASTER), S("All Layers", 0, ID_RASTERALL),
  H("Stack Mode"), S("Mean", 0, ID_STACKMODE0 + STK_MEAN), S("Median", 0, ID_STACKMODE0 + STK_MEDIAN), S("Minimum", 0, ID_STACKMODE0 + STK_MIN),
  S("Maximum", 0, ID_STACKMODE0 + STK_MAX), S("Range", 0, ID_STACKMODE0 + STK_RANGE), S("Summation", 0, ID_STACKMODE0 + STK_SUM),
  S("Variance", 0, ID_STACKMODE0 + STK_VARIANCE), S("Standard Deviation", 0, ID_STACKMODE0 + STK_STDDEV), S("Entropy", 0, ID_STACKMODE0 + STK_ENTROPY),
  S("Skewness", 0, ID_STACKMODE0 + STK_SKEWNESS), S("Kurtosis", 0, ID_STACKMODE0 + STK_KURTOSIS),
  H("Matting"), S("Defringe...", 0, ID_DEFRINGE), S("Remove Black Matte", 0, ID_BLACKMATTE), S("Remove White Matte", 0, ID_WHITEMATTE), BAR,
  H("Arrange"), S("Bring to Front", "}", ID_TOFRONT), S("Bring Forward", "]", ID_RAISE), S("Send Backward", "[", ID_LOWER), S("Send to Back", "{", ID_TOBACK), SBAR, S("Reverse", 0, ID_REVERSE),
  H("Align"), S("Left Edges", 0, ID_AL_LEFT), S("Horizontal Centers", 0, ID_AL_HCENTER), S("Right Edges", 0, ID_AL_RIGHT), SBAR,
  S("Top Edges", 0, ID_AL_TOP), S("Vertical Centers", 0, ID_AL_VCENTER), S("Bottom Edges", 0, ID_AL_BOTTOM), SBAR,
  S("Distribute Horizontally", 0, ID_DIST_H), S("Distribute Vertically", 0, ID_DIST_V), SBAR,
  S("Distribute Left Edges", 0, ID_DIST_LEFT), S("Distribute Right Edges", 0, ID_DIST_LEFT + 1), S("Distribute Top Edges", 0, ID_DIST_LEFT + 2),
  S("Distribute Bottom Edges", 0, ID_DIST_LEFT + 3), S("Distribute Horizontal Spacing", 0, ID_DIST_LEFT + 4), S("Distribute Vertical Spacing", 0, ID_DIST_LEFT + 5),
  H("Lock"), S("Pixels", 0, ID_LOCKLAYER), S("Transparency", 0, ID_ALPHALOCK), S("Position", 0, ID_LOCKPOS), S("All", 0, ID_LOCKALL),
  I("Hide/Show Layer", 0, ID_HIDELAYER), BAR,
  I("Merge Down", "E", ID_MERGEDOWN), I("Merge Group", 0, ID_MERGEGROUP), I("Merge Layers", 0, ID_MERGELAYERS), I("Merge Visible", 0, ID_MERGEVISIBLE), I("Flatten Image", 0, ID_FLATTEN),
  M("Select"),
  I("All", "A", ID_SELALL), I("Deselect", "D", ID_DESELECT), I("Reselect", 0, ID_RESELECT), I("Inverse", 0, ID_INVERSE), BAR,
  I("All Layers", 0, ID_FX_ALLLAYERS), I("Deselect Layers", 0, ID_FX_NOLAYERS), BAR,
  I("Color Range...", 0, ID_COLORRANGE), I("Focus Area...", 0, ID_FX_FOCUS), I("Subject", 0, ID_SELSUBJECT), I("Sky", 0, ID_SELSKY), I("Layer Transparency", 0, ID_SELALPHA), I("Layer Mask", 0, ID_SELFROMMASK), BAR,
  I("Select and Mask...", 0, ID_FX_REFINE),
  H("Modify"), S("Border...", 0, ID_BORDER), S("Smooth...", 0, ID_SMOOTH), S("Expand...", 0, ID_EXPAND), S("Contract...", 0, ID_CONTRACT), S("Feather...", 0, ID_FEATHER),
  I("Grow", 0, ID_GROWSIM), I("Similar", 0, ID_SIMILAR), BAR,
  I("Transform Selection", 0, ID_XFSEL), T("Edit in Quick Mask Mode", 0, ID_QUICKMASK), BAR,
  I("Load Selection...", 0, ID_LOADSEL), I("Save Selection...", 0, ID_SAVESEL),
  M("Filter"),
  I("Last Filter", "F", ID_F_AGAIN), BAR, I("Filter Gallery...", 0, ID_FX_GALLERY),
  I("Lens Correction...", 0, ID_F_LENSCORR), I("Liquify...", 0, ID_LIQUIFY), BAR,
  H("Blur"), S("Average", 0, ID_F_AVERAGE), S("Blur", 0, ID_F_BLUR), S("Blur More", 0, ID_F_BLURMORE), SBAR,
  S("Gaussian Blur...", 0, ID_F_GAUSS), S("Box Blur...", 0, ID_F_BOX), S("Motion Blur...", 0, ID_F_MOTION), S("Radial Blur...", 0, ID_F_RADIAL),
  S("Spin Blur...", 0, ID_F_SPINBLUR), S("Lens Blur...", 0, ID_F_LENSBLUR), S("Shape Blur...", 0, ID_F_SHAPEBLUR), S("Smart Blur...", 0, ID_F_SMARTBLUR),
  S("Surface Blur...", 0, ID_F_SURFACEBLUR),
  H("Blur Gallery"), S("Field Blur...", 0, ID_FIELDBLUR), S("Iris Blur...", 0, ID_F_IRIS), S("Tilt-Shift...", 0, ID_F_TILTSHIFT),
  H("Sharpen"), S("Smart Sharpen...", 0, ID_F_SMARTSHARPEN), S("Unsharp Mask...", 0, ID_F_UNSHARP), SBAR, S("Sharpen", 0, ID_F_SHARPEN), S("Sharpen Edges", 0, ID_F_SHARPENEDGES), S("Sharpen More", 0, ID_F_SHARPENMORE),
  H("Noise"), S("Add Noise...", 0, ID_F_NOISE), S("Dust & Scratches...", 0, ID_F_DUST), S("Median...", 0, ID_F_MEDIAN), S("Reduce Noise...", 0, ID_F_REDUCENOISE),
  S("Despeckle", 0, ID_F_DESPECKLE),
  H("Distort"), S("Diffuse Glow...", 0, ID_F_DIFFUSEGLOW), S("Displace...", 0, ID_F_DISPLACE), S("Twirl...", 0, ID_F_TWIRL), S("Pinch...", 0, ID_F_PINCH), S("Spherize...", 0, ID_F_SPHERIZE), S("Ripple...", 0, ID_F_RIPPLE), S("Wave...", 0, ID_F_WAVE),
  S("Polar Coordinates...", 0, ID_F_POLAR), S("Shear...", 0, ID_F_SHEAR), S("ZigZag...", 0, ID_F_ZIGZAG), S("Glass...", 0, ID_F_GLASS), S("Ocean Ripple...", 0, ID_F_OCEAN),
  H("Pixelate"), S("Mosaic...", 0, ID_F_MOSAIC), S("Crystallize...", 0, ID_F_CRYSTALLIZE), S("Pointillize...", 0, ID_F_POINTILLIZE), S("Color Halftone...", 0, ID_F_HALFTONE),
  S("Mezzotint...", 0, ID_F_MEZZOTINT), S("Facet", 0, ID_F_FACET), S("Fragment", 0, ID_F_FRAGMENT),
  H("Stylize"), S("Emboss...", 0, ID_F_EMBOSS), S("Find Edges", 0, ID_F_EDGES), S("Trace Contour...", 0, ID_F_CONTOUR), S("Solarize", 0, ID_F_SOLARIZE),
  S("Oil Paint...", 0, ID_F_OIL), S("Diffuse...", 0, ID_F_DIFFUSE), S("Tiles...", 0, ID_F_TILES), S("Wind...", 0, ID_F_WIND),
  S("Extrude...", 0, ID_F_EXTRUDE), S("Glowing Edges...", 0, ID_F_GLOWEDGES), S("Detect Edges...", 0, ID_F_EDGEDETECT),
  H("Artistic"), S("Colored Pencil...", 0, ID_GX0 + GX_COLORED_PENCIL), S("Cutout...", 0, ID_F_CUTOUT), S("Dry Brush...", 0, ID_GX0 + GX_DRY_BRUSH),
  S("Film Grain...", 0, ID_F_FILMGRAIN), S("Fresco...", 0, ID_GX0 + GX_FRESCO), S("Neon Glow...", 0, ID_GX0 + GX_NEON_GLOW), S("Paint Daubs...", 0, ID_GX0 + GX_PAINT_DAUBS),
  S("Palette Knife...", 0, ID_GX0 + GX_PALETTE_KNIFE), S("Plastic Wrap...", 0, ID_F_PLASTIC), S("Poster Edges...", 0, ID_F_POSTEREDGES), S("Rough Pastels...", 0, ID_GX0 + GX_ROUGH_PASTELS),
  S("Smudge Stick...", 0, ID_GX0 + GX_SMUDGE_STICK), S("Sponge...", 0, ID_GX0 + GX_SPONGE), S("Underpainting...", 0, ID_GX0 + GX_UNDERPAINTING), S("Watercolor...", 0, ID_GX0 + GX_WATERCOLOR),
  H("Brush Strokes"), S("Accented Edges...", 0, ID_GX0 + GX_ACCENTED_EDGES), S("Angled Strokes...", 0, ID_GX0 + GX_ANGLED_STROKES), S("Crosshatch...", 0, ID_GX0 + GX_CROSSHATCH),
  S("Dark Strokes...", 0, ID_GX0 + GX_DARK_STROKES), S("Ink Outlines...", 0, ID_GX0 + GX_INK_OUTLINES), S("Spatter...", 0, ID_F_SPATTER), S("Sprayed Strokes...", 0, ID_GX0 + GX_SPRAYED_STROKES),
  S("Sumi-e...", 0, ID_GX0 + GX_SUMI_E),
  H("Sketch"), S("Bas Relief...", 0, ID_GX0 + GX_BAS_RELIEF), S("Chalk & Charcoal...", 0, ID_GX0 + GX_CHALK_CHARCOAL), S("Charcoal...", 0, ID_GX0 + GX_CHARCOAL),
  S("Chrome...", 0, ID_GX0 + GX_CHROME), S("Conte Crayon...", 0, ID_GX0 + GX_CONTE_CRAYON), S("Graphic Pen...", 0, ID_GX0 + GX_GRAPHIC_PEN), S("Halftone Pattern...", 0, ID_F_HALFTONEPAT),
  S("Note Paper...", 0, ID_GX0 + GX_NOTE_PAPER), S("Photocopy...", 0, ID_F_PHOTOCOPY), S("Plaster...", 0, ID_GX0 + GX_PLASTER), S("Reticulation...", 0, ID_GX0 + GX_RETICULATION),
  S("Stamp...", 0, ID_GX0 + GX_STAMP), S("Torn Edges...", 0, ID_GX0 + GX_TORN_EDGES), S("Water Paper...", 0, ID_GX0 + GX_WATER_PAPER),
  H("Texture"), S("Craquelure...", 0, ID_GX0 + GX_CRAQUELURE), S("Grain...", 0, ID_GX0 + GX_GRAIN), S("Mosaic Tiles...", 0, ID_F_MOSAICTILES), S("Patchwork...", 0, ID_F_PATCHWORK),
  S("Stained Glass...", 0, ID_F_STAINEDGLASS), S("Texturizer...", 0, ID_F_TEXTURIZER),
  H("Render"), S("Flame...", 0, ID_FLAME), S("Picture Frame...", 0, ID_PICFRAME), S("Tree...", 0, ID_TREE), SBAR,
  S("Clouds", 0, ID_F_CLOUDS), S("Difference Clouds", 0, ID_F_DIFFCLOUDS), S("Fibers...", 0, ID_F_FIBERS), S("Lens Flare...", 0, ID_F_FLARE),
  S("Lighting Effects...", 0, ID_F_LIGHTING), S("Vignette...", 0, ID_F_VIGNETTE),
  H("Video"), S("De-Interlace...", 0, ID_F_DEINTERLACE), S("NTSC Colors", 0, ID_F_NTSC), S("PAL Colors", 0, ID_F_PAL),
  H("Other"), S("Custom...", 0, ID_F_CUSTOM), S("High Pass...", 0, ID_F_HIGHPASS), S("Minimum...", 0, ID_F_MIN), S("Maximum...", 0, ID_F_MAX), S("Offset...", 0, ID_F_OFFSET),
  S("Color to Alpha...", 0, ID_F_COLORALPHA), S("HSB/HSL...", 0, ID_F_HSBHSL),
  M("View"),
  I("Zoom In", "+", ID_ZOOMIN), I("Zoom Out", "-", ID_ZOOMOUT), I("Fit on Screen", "0", ID_FIT), I("Fit Layer(s) on Screen", 0, ID_FITLAYERS),
  I("100%", "1", ID_ACTUAL), I("200%", 0, ID_ZOOM200), I("50%", 0, ID_ZOOM50), I("Print Size", 0, ID_PRINTSIZE), BAR,
  /* 1.2: the pixel aspect, the extras, Pattern Preview */
  H("Pixel Aspect Ratio"), ST("Square", ID_PAR0), ST("D1/DV NTSC (0.91)", ID_PAR0 + 1), ST("D1/DV PAL (1.09)", ID_PAR0 + 2), ST("D1/DV NTSC Widescreen (1.21)", ID_PAR0 + 3),
  ST("HDV 1080/DVCPRO HD 720 (1.33)", ID_PAR0 + 4), ST("D1/DV PAL Widescreen (1.46)", ID_PAR0 + 5), ST("DVCPRO HD 1080 (1.5)", ID_PAR0 + 6), ST("Anamorphic 2:1 (2)", ID_PAR0 + 7), SBAR,
  ST("Amiga PAL Low Res (10:11)", ID_PAR0 + 8), ST("Amiga PAL High Res (5:11)", ID_PAR0 + 9), ST("Amiga PAL Low Res Laced (20:11)", ID_PAR0 + 10),
  ST("Amiga NTSC Low Res (44:52)", ID_PAR0 + 11), ST("Amiga NTSC High Res (22:52)", ID_PAR0 + 12), S("From Amiga Screen Mode", 0, ID_PARSCREEN), SBAR, S("Custom...", 0, ID_PARCUSTOM),
  T("Pixel Aspect Ratio Correction", 0, ID_PARCORRECT), BAR,
  T("Extras", "H", ID_SHOWEDGES), H("Show"), ST("Selection Edges", ID_SHOWSELEDGES), ST("Layer Edges", ID_SHOWLAYEREDGES), ST("Target Path", ID_SHOWPATH),
  ST("Pixel Grid", ID_PIXGRID), ST("Notes", ID_SHOWNOTES), ST("Count", ID_SHOWCOUNT), ST("Slices", ID_SHOWSLICES), SBAR,   /* (1.2: notes, counts, slices) */
  S("Guides from TV Safe Areas", 0, ID_SAFEGUIDES), SBAR, S("All", 0, ID_SHOWALL), S("None", 0, ID_SHOWNONE),
  T("Rulers", "R", ID_RULERS), T("Grid", "'", ID_SHOWGRID), T("Guides", ";", ID_SHOWGUIDES), T("Snap", 0, ID_SNAP),
  H("Snap To"), ST("Guides", ID_SNAPTO0), ST("Grid", ID_SNAPTO0 + 1), ST("Document Bounds", ID_SNAPTO0 + 2), ST("Centre Lines", ID_SNAPTO0 + 3), ST("Pixels", ID_SNAPTO0 + 4),
  I("Grid Settings...", 0, ID_GRIDSET), I("New Guide...", 0, ID_NEWGUIDE), I("New Guide Layout...", 0, ID_GUIDELAYOUT), I("New Guides From Shape", 0, ID_GUIDESHAPE),
  T("Lock Guides", 0, ID_LOCKGUIDES), I("Clear Guides", 0, ID_CLEARGUIDES), BAR, T("Pattern Preview", 0, ID_PATPREVIEW), BAR,
  /* 1.2: proofing */
  T("Proof Colors", 0, ID_PROOF), T("Gamut Warning", 0, ID_GAMUT),
  H("Proof Setup"), RC("Working CMYK", ID_PROOFSET0 + PRF_CMYK), RC("Working Cyan Plate", ID_PROOFSET0 + PRF_CYAN), RC("Working Magenta Plate", ID_PROOFSET0 + PRF_MAGENTA),
  RC("Working Yellow Plate", ID_PROOFSET0 + PRF_YELLOW), RC("Working Black Plate", ID_PROOFSET0 + PRF_BLACK), RC("Working CMY Plates", ID_PROOFSET0 + PRF_CMY), SBAR,
  RC("Legacy Macintosh RGB (Gamma 1.8)", ID_PROOFSET0 + PRF_MAC), RC("Internet Standard RGB (sRGB)", ID_PROOFSET0 + PRF_INTERNET), RC("Monitor RGB", ID_PROOFSET0 + PRF_MONITOR), SBAR,
  RC("Color Blindness - Protanopia", ID_PROOFSET0 + PRF_PROTAN), RC("Color Blindness - Deuteranopia", ID_PROOFSET0 + PRF_DEUTAN), SBAR,
  S("Load Working CMYK Profile...", 0, ID_LOADCMYK), RC("Easel's Press (Built-in CMYK)", ID_PRESSCMYK),
  BAR,
  I("Hide/Show Panels (Tab)", 0, ID_TOGGLEPANELS), I("Hide/Show Right Panels (Shift-Tab)", 0, ID_TOGGLERIGHT), BAR,
  H("Screen Dither"), ST("Ordered", ID_DITHER0), ST("None", ID_DITHER1), ST("Error Diffusion", ID_DITHER2),
  H("Rotate View"), S("90\260 Clockwise", 0, ID_ROT_CW), S("90\260 Anticlockwise", 0, ID_ROT_CCW), S("180\260", 0, ID_ROT_180), SBAR, S("Reset View", 0, ID_ROT_RESET),
  BAR,
  T("Dark Interface", 0, ID_DARKUI),
  H("Screen Mode"), S("Window", 0, ID_SCR_WINDOW), S("Easel's Own Screen", 0, ID_SCR_OWN), S("Full Screen", 0, ID_SCR_FULL), SBAR, S("Choose Screen Mode...", 0, ID_SCR_CHOOSE),
  M("Window"),
  I("Next Document", "`", ID_NEXTDOC), I("Previous Document", 0, ID_PREVDOC), BAR,
  H("Arrange"), S("Match Zoom", 0, ID_MATCHZOOM), S("Match Location", 0, ID_MATCHLOC), S("Match All", 0, ID_MATCHALL), BAR,
  I("Palette", 0, ID_WIN_PALETTE), I("Brush Settings", 0, ID_WIN_BRUSHSET), I("DPaint Brush", 0, ID_WIN_DPAINT), BAR,
  /* 1.2 */
  I("Actions...", 0, ID_WIN_ACTIONS), I("Stop Recording", 0, ID_ACTSTOP), I("Play Action Again", 0, ID_ACTPLAY), I("Styles...", 0, ID_WIN_STYLES), I("Layer Comps...", 0, ID_WIN_COMPS),
  H("Histogram"), ST("Luminosity", ID_HISTCH0), ST("Red", ID_HISTCH0 + 1), ST("Green", ID_HISTCH0 + 2), ST("Blue", ID_HISTCH0 + 3), BAR,
  H("Animation"), S("Previous Frame", 0, ID_FRAMEPREV), S("Next Frame", 0, ID_FRAMENEXT), S("Play Frames", 0, ID_FRAMEPLAY), SBAR,
  ST("Onion Skin", ID_ONION), S("Onion Skin Settings...", 0, ID_ONIONSET), S("Frame Time...", 0, ID_FRAMETIME),
  I("Spare Page", 0, ID_SPARE), I("Copy to Spare", 0, ID_COPYSPARE),
  { NM_END, 0, 0, 0, 0, 0 }
};

/* ------------------------------------------------------------------ small helpers */
/* 0.8: a menu item's label (for the progress window), or NULL */
static const char *menu_label(ULONG id)
{
  int i;
  for (i = 0; menus[i].nm_Type != NM_END; i++)
    if ((menus[i].nm_Type == NM_ITEM || menus[i].nm_Type == NM_SUB) && menus[i].nm_Label != NM_BARLABEL && (ULONG)menus[i].nm_UserData == id) return tr((const char *)menus[i].nm_Label);
  return NULL;
}
/* 1.1: the menus in the language chosen, for the menu strip: a copy of menus[] with each label put
   through tr() (menus[] itself stays in English: Keyboard Shortcuts and Easel.keys name commands by it) */
#define NMENUS ((int)(sizeof menus / sizeof menus[0]))
static struct NewMenu menus_tr[NMENUS];
static struct NewMenu *menus_translated(void)
{
  int i;
  for (i = 0; i < NMENUS; i++) {
    menus_tr[i] = menus[i];
    if (menus[i].nm_Label && menus[i].nm_Label != NM_BARLABEL) menus_tr[i].nm_Label = (STRPTR)tr((const char *)menus[i].nm_Label);
  }
  return menus_tr;
}
DocView *cur_view(void) { return curdoc >= 0 && curdoc < ndocs ? &docs[curdoc] : NULL; }
Doc *cur_doc(void) { DocView *v = cur_view(); return v ? v->doc : NULL; }
static ULONG xget(Object *o, ULONG attr) { ULONG v = 0; get(o, attr, &v); return v; }
/* the main window drawn again throughout (a requester's or dialog's window closing over it isn't
   always repaired on its own) */
void redraw_window(void)
{
  Object *root = NULL;
  if (!win || !xget(win, MUIA_Window_Open)) return;
  get(win, MUIA_Window_RootObject, &root);
  if (root && DoMethod(root, MUIM_Group_InitChange)) DoMethod(root, MUIM_Group_ExitChange);
}
int ask(const char *gadgets, const char *fmt, const char *arg)
{
  int r = (int)MUI_Request(app, win, 0, (STRPTR)"Easel", (STRPTR)tr(gadgets), (STRPTR)tr(fmt), arg);
  redraw_window();
  return r;
}
static void menu_set(ULONG id, ULONG attr, ULONG val)
{
  Object *strip = win ? (Object *)xget(win, MUIA_Window_Menustrip) : NULL, *it;
  if (strip && (it = (Object *)DoMethod(strip, MUIM_FindUData, id))) set(it, attr, val);
}
/* the ASL file requester, starting in path's drawer with its file name; path is replaced by the
   choice. 1 if a file was chosen */
static int file_request(int save, const char *titletext, char *path, size_t cap, const char *pattern)
{
  struct FileRequester *fr;
  char dir[400], file[108];
  int ok = 0;
  strncpy(dir, path, sizeof dir - 1); dir[sizeof dir - 1] = 0;
  *PathPart((STRPTR)dir) = 0;
  strncpy(file, path[0] ? (const char *)FilePart((STRPTR)path) : "", sizeof file - 1); file[sizeof file - 1] = 0;
  if (!(fr = (struct FileRequester *)MUI_AllocAslRequestTags(ASL_FileRequest, ASLFR_TitleText, (ULONG)tr(titletext),
        ASLFR_DoSaveMode, save, ASLFR_InitialDrawer, (ULONG)dir, ASLFR_InitialFile, (ULONG)file,
        (ULONG)(pattern ? ASLFR_InitialPattern : TAG_IGNORE), (ULONG)pattern, (ULONG)(pattern ? ASLFR_DoPatterns : TAG_IGNORE), TRUE, TAG_DONE))) return 0;
  if (MUI_AslRequestTags(fr, ASLFR_Window, (ULONG)xget(win, MUIA_Window_Window), TAG_DONE)) {
    strncpy(path, (const char *)fr->fr_Drawer, cap - 1); path[cap - 1] = 0;
    ok = AddPart((STRPTR)path, fr->fr_File, cap) && fr->fr_File[0];
  }
  MUI_FreeAslRequest(fr);
  return ok;
}

/* ------------------------------------------------------------------ modal dialogs */
ULONG run_modal(Object *w, const ULONG *ends, void (*each)(ULONG id))
{
  ULONG sigs = 0, id = 0;
  int i, done = 0;
  int (*hook)(long, long) = progress_hook;
  if (!w) return ID_DLGCANCEL;
  progress_hook = NULL;                         /* (0.8: previews show no progress window, unless they start a job) */
  DoMethod(app, OM_ADDMEMBER, (ULONG)w);
  DoMethod(w, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGCANCEL);
  set(win, MUIA_Window_Sleep, TRUE);
  set(w, MUIA_Window_Open, TRUE);
  while (!done) {
    id = DoMethod(app, MUIM_Application_NewInput, (ULONG)&sigs);
    if (id == ID_DLGCANCEL || id == MUIV_Application_ReturnID_Quit) { id = ID_DLGCANCEL; break; }
    for (i = 0; ends[i]; i++) if (id == ends[i]) done = 1;
    if (done) break;
    if (id && each) each(id);
    if (!id && each) each(0);                   /* (idle: a dialog's preview catches up) */
    if (sigs) sigs = Wait(sigs | SIGBREAKF_CTRL_C);
  }
  set(w, MUIA_Window_Open, FALSE);
  set(win, MUIA_Window_Sleep, FALSE);
  DoMethod(app, OM_REMMEMBER, (ULONG)w);
  redraw_window();
  progress_hook = hook;
  job_timer_reset();
  return id;
}
Object *dialog_window(const char *t, Object *contents, const char *oklabel)
{
  Object *ok, *cancel, *w;
  w = QWindowObject, MUIA_Window_Title, (ULONG)tr(t), MUIA_Window_RefWindow, (ULONG)win, MUIA_Window_LeftEdge, MUIV_Window_LeftEdge_Centered,
    MUIA_Window_TopEdge, MUIV_Window_TopEdge_Centered,
    WindowContents, VGroup, QDLG,
      Child, contents,
      Child, HGroup, Child, HSpace(0), Child, ok = QButton(tr(oklabel ? oklabel : "OK")), Child, cancel = QButton(tr("Cancel")), End,
    End,
  End;
  if (!w) { if (contents) MUI_DisposeObject(contents); return NULL; }
  DoMethod(ok, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
  DoMethod(cancel, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGCANCEL);
  return w;
}
int ask_string(const char *t, const char *label, char *buf, int cap)
{
  Object *str, *w;
  static const ULONG ends[] = { ID_DLGOK, 0 };
  int ok;
  w = dialog_window(t, HGroup, Child, QLabel(tr(label), MUIO_Label_SingleFrame),
                    Child, str = StringObject, StringFrame, MUIA_String_Contents, (ULONG)buf, MUIA_String_MaxLen, cap, MUIA_CycleChain, 1, MUIA_FixWidthTxt, (ULONG)"MMMMMMMMMMMMMMMMMMMMMMMM", End, End, NULL);
  if (!w) return 0;
  DoMethod(str, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGOK);
  set(w, MUIA_Window_ActiveObject, (ULONG)str);
  ok = run_modal(w, ends, NULL) == ID_DLGOK;
  if (ok) { strncpy(buf, (const char *)xget(str, MUIA_String_Contents), cap - 1); buf[cap - 1] = 0; }
  MUI_DisposeObject(w);
  return ok;
}
int ask_number(const char *t, const char *label, LONG *v, LONG min, LONG max)
{
  char buf[24];
  LONG n;
  sprintf(buf, "%ld", (long)*v);
  if (!ask_string(t, label, buf, sizeof buf)) return 0;
  n = atol(buf);
  if (n < min) n = min;
  if (n > max) n = max;
  *v = n;
  return 1;
}
/* the colour picker: MUI's Coloradjust (red, green and blue, or a colour wheel) and the hex value */
LONG pick_color(const char *t, ULONG start)
{
  Object *ca, *hex, *w;
  static const ULONG ends[] = { ID_DLGOK, 0 };
  ULONG rgb[3];
  char buf[16];
  LONG c = -1;
  rgb[0] = ((start >> 16) & 255) * 0x01010101UL; rgb[1] = ((start >> 8) & 255) * 0x01010101UL; rgb[2] = (start & 255) * 0x01010101UL;
  sprintf(buf, "%06lX", (unsigned long)(start & 0xFFFFFF));
  w = dialog_window(t, VGroup,
        Child, ca = ColoradjustObject, MUIA_Coloradjust_RGB, (ULONG)rgb, End,
        Child, HGroup, Child, QLabel(tr("Hex #"), MUIO_Label_SingleFrame),
          Child, hex = StringObject, StringFrame, MUIA_String_Contents, (ULONG)buf, MUIA_String_MaxLen, 8, MUIA_String_Accept, (ULONG)"0123456789abcdefABCDEF", MUIA_CycleChain, 1, End,
          Child, HSpace(0), End,
      End, NULL);
  if (!w) return -1;
  DoMethod(hex, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DLGCHANGE);
  for (;;) {
    static const ULONG ends2[] = { ID_DLGOK, ID_DLGCHANGE, 0 };
    ULONG id = run_modal(w, ends2, NULL);
    if (id == ID_DLGCHANGE) {
      ULONG v = strtoul((const char *)xget(hex, MUIA_String_Contents), NULL, 16);
      rgb[0] = ((v >> 16) & 255) * 0x01010101UL; rgb[1] = ((v >> 8) & 255) * 0x01010101UL; rgb[2] = (v & 255) * 0x01010101UL;
      set(ca, MUIA_Coloradjust_RGB, (ULONG)rgb);
      continue;
    }
    if (id == ID_DLGOK) {
      ULONG *p = (ULONG *)xget(ca, MUIA_Coloradjust_RGB);
      if (p) c = (LONG)(((p[0] >> 24) << 16) | ((p[1] >> 24) << 8) | (p[2] >> 24));
    }
    break;
  }
  (void)ends;
  MUI_DisposeObject(w);
  return c;
}

/* ------------------------------------------------------------------ fonts for the Text tool */
/* the TrueType families, then (0.9) the Amiga bitmap fonts found in FONTS: */
#define MAXBM 48
static const char *famlist[MAXFACES + MAXBM];
static char bmnames[MAXBM][32], bmlabels[MAXBM][44];
static int nfam = -1, ntt, nbm;
int font_count(void)
{
  int i;
  if (nfam < 0) {
    ntt = nfam = fonts_families(&fonts, famlist, MAXFACES);
    for (i = 0; i < nbm; i++) famlist[nfam++] = bmnames[i];
  }
  return nfam;
}
const char *font_name(int i) { return i >= 0 && i < font_count() ? famlist[i] : "Liberation Sans"; }
const char *font_label(int i) { return i >= ntt && i < font_count() ? bmlabels[i - ntt] : font_name(i); }
int font_find(const char *family)
{
  int i;
  for (i = 0; i < font_count(); i++) if (!Stricmp((STRPTR)famlist[i], (STRPTR)family)) return i;
  return -1;
}
/* 0.9: the bitmap fonts in FONTS: (a .font without an .otag beside it), for type layers */
static void bitmap_fonts_scan(void)
{
  struct FileInfoBlock *fib;
  struct Process *me = (struct Process *)FindTask(NULL);
  APTR pw = me->pr_WindowPtr;
  BPTR l, old;
  me->pr_WindowPtr = (APTR)-1;
  l = Lock((STRPTR)"FONTS:", ACCESS_READ);
  me->pr_WindowPtr = pw;
  if (!l) return;
  if ((fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL))) {
    old = CurrentDir(l);
    if (Examine(l, fib))
      while (ExNext(l, fib) && nbm < MAXBM) {
        char name[110], otag[120];
        int n = (int)strlen(fib->fib_FileName), k, dup = 0;
        BPTR o;
        if (fib->fib_DirEntryType >= 0 || n <= 5 || n - 5 >= 32 || Stricmp((STRPTR)fib->fib_FileName + n - 5, (STRPTR)".font")) continue;
        memcpy(name, fib->fib_FileName, n - 5); name[n - 5] = 0;
        sprintf(otag, "%s.otag", name);
        if ((o = Lock((STRPTR)otag, ACCESS_READ))) { UnLock(o); continue; }
        for (k = 0; k < nbm; k++) if (!Stricmp((STRPTR)bmnames[k], (STRPTR)name)) dup = 1;
        if (dup) continue;
        strcpy(bmnames[nbm], name);
        snprintf(bmlabels[nbm], sizeof bmlabels[0], "%s (Amiga)", name);
        nbm++;
      }
    CurrentDir(old);
    FreeDosObject(DOS_FIB, fib);
  }
  UnLock(l);
  nfam = -1;
}
/* the core's lookup of an Amiga bitmap font for a run: only for the families found in FONTS:, each
   size loaded once and kept until the end */
#define NAMICACHE 24
static struct { char fam[32]; int size; AmiFont *f; } amicache[NAMICACHE];
static int amicache_next;
static AmiFont *ami_lookup(const char *family, int size)
{
  int i;
  char path[64];
  for (i = 0; i < nbm; i++) if (!Stricmp((STRPTR)bmnames[i], (STRPTR)family)) break;
  if (i >= nbm) return NULL;
  for (i = 0; i < NAMICACHE; i++) if (amicache[i].fam[0] && amicache[i].size == size && !Stricmp((STRPTR)amicache[i].fam, (STRPTR)family)) return amicache[i].f;
  snprintf(path, sizeof path, "FONTS:%s.font", family);
  i = amicache_next; amicache_next = (amicache_next + 1) % NAMICACHE;
  if (amicache[i].f) amifont_free(amicache[i].f);
  strncpy(amicache[i].fam, family, sizeof amicache[i].fam - 1); amicache[i].fam[sizeof amicache[i].fam - 1] = 0;
  amicache[i].size = size;
  amicache[i].f = amifont_load(path, size);         /* (NULL remembered too: not tried again) */
  return amicache[i].f;
}
static void ami_free(void)
{
  int i;
  for (i = 0; i < NAMICACHE; i++) { if (amicache[i].f) amifont_free(amicache[i].f); amicache[i].f = NULL; amicache[i].fam[0] = 0; }
}

/* ------------------------------------------------------------------ tools */
/* 1.1: the Blur, Sharpen and Smudge tools' Mode list (toolbox.c) as blend modes */
const int retouch_bm[RETOUCH_MODES] = { BM_NORMAL, BM_DARKEN, BM_LIGHTEN, BM_HUE, BM_SATURATION, BM_COLOR, BM_LUMINOSITY };
static int retouch_blend(int k) { return k >= 0 && k < RETOUCH_MODES ? retouch_bm[k] : BM_NORMAL; }
Brush tool_brush(int t)
{
  Brush b;
  memset(&b, 0, sizeof b);
  b.tool = t == TL_PENCIL ? PT_PENCIL : t == TL_ERASER ? PT_ERASER : t == TL_CLONE ? PT_CLONE : t == TL_BLUR ? PT_BLUR :
           t == TL_SHARPEN ? PT_SHARPEN : t == TL_SMUDGE ? PT_SMUDGE : t == TL_DODGE ? PT_DODGE : t == TL_BURN ? PT_BURN :
           t == TL_SPONGE ? PT_SPONGE : t == TL_PATTERN ? PT_PATTERN : t == TL_HISTORY ? PT_HISTORY : t == TL_REPLACE ? PT_REPLACE :
           t == TL_BGERASE ? PT_BGERASE : t == TL_HEAL ? PT_HEAL : t == TL_SPOTHEAL ? PT_SPOTHEAL :
           t == TL_MIXER ? PT_MIXER : t == TL_REMOVE ? PT_SPOTHEAL : PT_BRUSH;      /* (1.2) */
  b.size = opt.size[t]; b.hardness = opt.hardness[t]; b.opacity = opt.opacity[t]; b.flow = opt.flow[t];
  b.spacing = 25; b.blend = opt.mode[t]; b.strength = opt.strength[t];
  /* 0.5: the tip, dynamics and symmetry from the Brush Settings window; then the options bar's */
  brushwin_apply(&b);
  if (b.dyn.sym_mode == SYM_PATH) {
    /* r3: symmetry along the active path, flattened for the strokes (none: no copies) */
    static Poly sym_poly;
    Doc *dd = cur_doc();
    Path *ap = dd ? active_path(dd) : NULL;
    poly_free(&sym_poly); poly_init(&sym_poly);
    brush_sym_path = ap && ap->na >= 2 && path_flatten(ap, 0.5, &sym_poly) ? &sym_poly : NULL;
  }
  b.pattern = tool_pattern(); b.pattern_aligned = opt.pattern_aligned;
  if (t == TL_REPLACE || t == TL_BGERASE) { b.sampling = opt.sampling; b.limits = opt.limits; b.tolerance = opt.tolerance; b.replace_mode = opt.replace_mode; b.protect_fg = opt.protect_fg; }
  if (t == TL_HISTORY) { DocView *v = cur_view(); b.snapshot = v ? v->snap : NULL; }
  /* 1.1: the options bar's tool options */
  if (t == TL_DODGE || t == TL_BURN) { b.tone_range = TR_SHADOWS + (opt.range > 2 ? 1 : opt.range); b.protect_tones = opt.protect_tones; }
  if (t == TL_SPONGE) { b.sponge_saturate = opt.sponge_mode == 1; b.vibrance = opt.vibrance; }
  if (t == TL_CLONE || t == TL_HEAL) b.sample_all = sample_layers[opt.clone_sample % 3];
  if (t == TL_HEAL) { b.blend = opt.mode[t] < BM_N ? opt.mode[t] : BM_NORMAL; b.heal_source = opt.heal_source; b.heal_diffusion = opt.heal_diffusion; }   /* (1.2: Mode, Source, Diffusion) */
  if (t == TL_SPOTHEAL) b.spot_type = opt.spot_type;
  if (t == TL_REMOVE) { b.spot_type = SPOT_CONTENT; b.blend = BM_NORMAL; }
  if (t == TL_MIXER) {
    /* 1.2: the Mixer Brush's paint and settings */
    tool_mixer.auto_load = opt.mix_autoload; tool_mixer.auto_clean = opt.mix_autoclean; tool_mixer.sample_all = opt.mix_all;
    b.mixer = &tool_mixer; b.mix_wet = opt.mix_wet; b.mix_load = opt.mix_load; b.mix_mix = opt.mix_mix;
  }
  if (t == TL_BLUR || t == TL_SHARPEN || t == TL_SMUDGE) b.blend = retouch_blend(opt.mode[t]);
  if (t == TL_SHARPEN) b.protect_detail = opt.protect_detail;
  if (t == TL_SMUDGE) b.finger_painting = opt.finger_paint;
  if (t == TL_ERASER && opt.erase_history) { DocView *v = cur_view(); b.erase_history = 1; b.snapshot = v ? v->snap : NULL; }
  return b;
}
/* ------------------------------------------------------------------ 0.5: gradients and patterns */
Gradient cur_grad;
PatternList patterns;
const char *grad_names[MAXNAMES + 1], *pat_names[MAXNAMES];
int ngrad_names, npat_names;
void names_refresh(void)
{
  GradientList *g = gradwin_presets();
  int i;
  ngrad_names = npat_names = 0;
  for (i = 0; g && i < g->n && i < MAXNAMES; i++) grad_names[ngrad_names++] = g->g[i].name;
  grad_names[ngrad_names++] = "Custom";
  for (i = 0; i < patterns.n && i < MAXNAMES; i++) pat_names[npat_names++] = patterns.p[i]->name;
  if (opt.grad_preset >= ngrad_names) opt.grad_preset = ngrad_names - 1;
  if (opt.pattern >= npat_names) opt.pattern = npat_names ? npat_names - 1 : 0;
}
const Gradient *tool_gradient(void)
{
  GradientList *g = gradwin_presets();
  Doc *d = cur_doc();
  if (opt.grad_palrange && d && d->mode == MODE_INDEXED) {
    /* 1.1: DPaint's range fill: the palette's first cycling range (else all of it) as the gradient */
    static Gradient pg;
    int lo, hi;
    doc_fill_range(d, &lo, &hi);
    if (gradient_from_palette(&pg, d->pal, d->npal, lo, hi)) return &pg;
  }
  return g && opt.grad_preset >= 0 && opt.grad_preset < g->n ? &g->g[opt.grad_preset] : &cur_grad;
}
const Pattern *tool_pattern(void) { return opt.pattern >= 0 && opt.pattern < patterns.n ? patterns.p[opt.pattern] : NULL; }
static void opts_defaults(void)
{
  int t;
  for (t = 0; t < TL_N; t++) { opt.size[t] = 20; opt.hardness[t] = 100; opt.opacity[t] = 100; opt.flow[t] = 100; opt.mode[t] = BM_NORMAL; opt.strength[t] = 50; }
  opt.hardness[TL_BRUSH] = 80; opt.size[TL_PENCIL] = 1; opt.size[TL_ERASER] = 40; opt.hardness[TL_ERASER] = 50;
  opt.size[TL_BLUR] = opt.size[TL_SHARPEN] = opt.size[TL_SMUDGE] = 30; opt.hardness[TL_BLUR] = opt.hardness[TL_SMUDGE] = 0;
  opt.size[TL_DODGE] = opt.size[TL_BURN] = opt.size[TL_SPONGE] = 50; opt.hardness[TL_DODGE] = opt.hardness[TL_BURN] = opt.hardness[TL_SPONGE] = 0;
  opt.tolerance = 32; opt.contiguous = 1; opt.antialias = 1; opt.fill_opacity = 100;
  opt.line_width = 3; opt.text_size = 48; opt.sample = 0;
  /* 0.7 */
  opt.sel_antialias = 1; opt.mq_w = 4; opt.mq_h = 3;
  opt.ml_width = 10; opt.ml_contrast = 10; opt.ml_freq = 57;
  /* 1.0 */
  opt.size[TL_SPOTHEAL] = 19; opt.size[TL_HEAL] = 19; opt.hardness[TL_HEAL] = 100; opt.redeye_pupil = 50; opt.redeye_darken = 50;
  /* 0.9 */
  opt.text_aa = 2; opt.shape_fill = 1; opt.stroke_width = 3; opt.stroke_color = 0x000000;
  opt.poly_sides = 5; opt.poly_indent = 50; opt.round_radius = 10; opt.arrow_end = 1;
  /* 1.1 */
  opt.range = 1; opt.protect_tones = 1; opt.vibrance = 1; opt.clone_aligned = 1; opt.crop_overlay = CO_THIRDS;
  /* 1.2 */
  opt.qs_enhance = 1; opt.size[TL_QUICKSEL] = 12; opt.frame_stroke = 0;
  opt.mix_wet = 50; opt.mix_load = 50; opt.mix_mix = 50; opt.mix_autoload = 1; opt.mix_autoclean = 1; opt.size[TL_MIXER] = 20;
  opt.ah_style = AH_TIGHT_SHORT; opt.ah_area = 50; opt.size[TL_ARTHIST] = 4; opt.size[TL_REMOVE] = 30;
  opt.cam_structure = 4; opt.cam_color = 0; opt.patch_structure = 4; opt.patch_color = 0; opt.heal_diffusion = 5;
}
const int sample_layers[3] = { SAMPLE_CURRENT, SAMPLE_BELOW, SAMPLE_ALL };
/* each letter's tool: the one used last of those sharing it, at first the group's usual one */
static signed char key_tool[26];
int tool_for_key(int c)
{
  static const int first[] = { TL_MOVE, TL_MARQUEE, TL_LASSO, TL_WAND, TL_CROP, TL_EYEDROPPER, TL_CLONE, TL_BRUSH, TL_ERASER, TL_GRADIENT,
    TL_BLUR, TL_DODGE, TL_TEXT, TL_RECT, TL_HAND, TL_ZOOM, TL_HISTORY, TL_SPOTHEAL, TL_PEN, TL_PATHSEL };
  int i;
  if (c < 'A' || c > 'Z') return -1;
  if (key_tool[c - 'A']) return key_tool[c - 'A'] - 1;
  for (i = 0; i < (int)(sizeof first / sizeof first[0]); i++) if (tool_keys[first[i]] == c) return first[i];
  for (i = 0; i < TL_N; i++) if (tool_keys[i] == c) return i;          /* (0.8: a letter given in Keyboard Shortcuts) */
  return -1;
}
void set_tool(int t)
{
  if (t < 0 || t >= TL_N) return;
  tool = t;
  if (tool_keys[t] >= 'A' && tool_keys[t] <= 'Z') key_tool[tool_keys[t] - 'A'] = (signed char)(t + 1);
  if (canvas) DoMethod(canvas, MUIM_Canvas_Tool);
  if (toolbox) DoMethod(toolbox, MUIM_Tools_Refresh);
  if (optbar) DoMethod(optbar, MUIM_Opts_Refresh);
}

/* ------------------------------------------------------------------ keeping everything up to date */
static void set_title(void)
{
  static char t[200];
  DocView *v = cur_view();
  if (v) snprintf(t, sizeof t, "Easel - %s%s%s", v->doc->info.copyright[0] ? "\251 " : "", v->name, v->doc->dirty ? " *" : "");   /* (1.2: \251 a copyrighted picture) */
  else strcpy(t, "Easel");
  if (win) set(win, MUIA_Window_Title, (ULONG)t);
}
/* 0.6: File > Open Recent (kept in the prefs) */
#define NRECENT 10
static char recent[NRECENT][512];
static void update_menus(void)
{
  Doc *d = cur_doc();
  static char undo[80], redo[80];
  int has = d != NULL;
  static const ULONG needdoc[] = { ID_CLOSE, ID_CLOSEALL, ID_SAVE, ID_SAVEAS, ID_REVERT, ID_PLACE, ID_EXPPNG, ID_EXPJPEG, ID_EXPBMP, ID_EXPILBM, ID_EXPILBM8,
    ID_CUT, ID_COPY, ID_COPYMERGED, ID_CLEAR, ID_FILL, ID_FILLFG, ID_FILLBG, ID_STROKE, ID_FREETRANSFORM, ID_IMAGESIZE, ID_CANVASSIZE, ID_CROP, ID_TRIM,
    ID_DUPIMAGE, ID_NEWLAYER, ID_DUPLAYER, ID_DELLAYER, ID_LAYERPROPS, ID_MERGEVISIBLE, ID_FLATTEN, ID_SELALL, ID_DESELECT, ID_INVERSE,
    ID_COLORRANGE, ID_SELALPHA, ID_AUTOTONE, ID_AUTOCONTRAST, ID_AUTOCOLOR, ID_ZOOMIN, ID_ZOOMOUT, ID_FIT, ID_ACTUAL, ID_ZOOM200, ID_ZOOM50,
    ID_PURGE, ID_LOCKLAYER, ID_ALPHALOCK, ID_HIDELAYER, ID_NEWGROUP, ID_GROUPLAYERS, ID_MERGELAYERS, ID_NEWFILLSOLID, ID_NEWFILLGRAD,
    ID_MASK_REVEALALL, ID_MASK_HIDEALL, ID_MASK_REVEALSEL, ID_MASK_HIDESEL, ID_MASK_ALPHA, ID_CLIP, ID_LOCKPOS, ID_LOCKALL,
    ID_AL_LEFT, ID_AL_HCENTER, ID_AL_RIGHT, ID_AL_TOP, ID_AL_VCENTER, ID_AL_BOTTOM, ID_DIST_H, ID_DIST_V, ID_RESELECT, ID_GROWSIM, ID_SIMILAR,
    ID_F_AGAIN, ID_MODE_RGB, ID_MODE_GRAY, ID_MODE_INDEXED, ID_MODE_BITMAP, ID_COLORTABLE, ID_SCREENMODE, ID_EXPGIF, ID_EXPILBMOPT,
    ID_RULERS, ID_SHOWGRID, ID_SHOWGUIDES, ID_SNAP, ID_GRIDSET, ID_NEWGUIDE, ID_CLEARGUIDES, ID_DEFBRUSH, ID_PICKUPBRUSH, ID_DEFPATTERN,
    ID_FILLPATTERN, ID_SNAPSHOT, ID_SAVECOPY, ID_EXPORTAS, ID_LAYERSTOFILES, ID_FADE, ID_TOGGLELAST,
    ID_NUMTRANSFORM, ID_XF_SCALE, ID_XF_ROTATE, ID_XF_SKEW, ID_XF_DISTORT, ID_XF_PERSPECTIVE, ID_XF_AGAIN, ID_ROTATE180, ID_ROTATE90R,
    ID_ROTATE90L, ID_FLIPLH, ID_FLIPLV, ID_DEFRINGE, ID_BLACKMATTE, ID_WHITEMATTE, ID_QUICKMASK,
    /* 1.1 (files) */
    ID_EXPXW0, ID_EXPXW0 + 1, ID_EXPXW0 + 2, ID_EXPXW0 + 3, ID_EXPXW0 + 4, ID_EXPXW0 + 5, ID_EXPXW0 + 6, ID_EXPXW0 + 7, ID_EXPXW0 + 8, ID_EXPXW0 + 9, ID_EXPANIMGIF, ID_EXPANIM,
    ID_CLOSEOTHERS, ID_QUICKPNG, ID_DELEMPTY, ID_FLATMASKS, ID_FITIMAGE, ID_LOREM, ID_EXPSVG, ID_LOCKGUIDES,
    ID_SNAPTO0, ID_SNAPTO0 + 1, ID_SNAPTO0 + 2, ID_SNAPTO0 + 3, ID_SNAPTO0 + 4, ID_TOPARA, ID_TOPOINT,
    /* 1.0 */
    ID_ADJ_REPLACE, ID_ADJ_SHADOWHI, ID_ADJ_MATCH, ID_F_PAL, ID_F_AVERAGE, ID_F_BLURMORE, ID_F_TILTSHIFT, ID_F_IRIS, ID_F_SMARTSHARPEN,
    ID_F_SHARPENEDGES, ID_F_DISPLACE, ID_F_LENSCORR, ID_F_EXTRUDE, ID_F_GLOWEDGES, ID_F_PLASTIC, ID_F_EDGEDETECT, ID_F_COLORALPHA, ID_F_HSBHSL,
    ID_F_FILMGRAIN, ID_F_CUTOUT, ID_F_POSTEREDGES, ID_F_DIFFUSEGLOW, ID_F_STAINEDGLASS, ID_F_HALFTONEPAT, ID_F_PHOTOCOPY, ID_F_TEXTURIZER,
    ID_F_MOSAICTILES, ID_F_PATCHWORK, ID_F_SPATTER, ID_FX_GALLERY, ID_FX_LOOKUP, ID_FX_FOCUS, ID_FX_REFINE, ID_FX_ALLLAYERS, ID_FX_NOLAYERS, 0 };
  /* 0.7: what works on the selection, greyed out while it is being edited as a Quick Mask */
  static const ULONG needsel[] = { ID_SELALL, ID_DESELECT, ID_RESELECT, ID_INVERSE, ID_COLORRANGE, ID_SELALPHA, ID_FEATHER, ID_EXPAND,
    ID_CONTRACT, ID_BORDER, ID_SMOOTH, ID_GROWSIM, ID_SIMILAR, ID_SAVESEL, ID_LOADSEL, ID_XFSEL, 0 };
  int qm = d && quickmask_active(d);
  static const ULONG needmask[] = { ID_MASK_DELETE, ID_MASK_APPLY, ID_MASK_ENABLE, ID_MASK_LINK, ID_MASK_INVERT, ID_MASK_EDIT, ID_SELFROMMASK, 0 };
  Layer *al = has ? d->lay[d->active] : NULL;
  int i;
  if (!win) return;
  for (i = 0; needdoc[i]; i++) menu_set(needdoc[i], MUIA_Menuitem_Enabled, has);
  {
    /* filters and adjustments: none in a Bitmap picture but Invert (as Photoshop; PhotoCraft #10545) */
    static int was_off;
    int fx_on = has && d->mode != MODE_BITMAP, off = has && !fx_on;
    ULONG k;
    if (off || was_off) for (k = ID_F_GAUSS; k < ID_F_LAST; k++) menu_set(k, MUIA_Menuitem_Enabled, fx_on);
    if (off || was_off) {
      for (k = ID_GX0; k < ID_GX0 + GX_N; k++) menu_set(k, MUIA_Menuitem_Enabled, fx_on);
      for (k = ID_ADJ_BC; k <= ID_ADJ_EQUALIZE; k++) if (k != ID_ADJ_INVERT) menu_set(k, MUIA_Menuitem_Enabled, fx_on);
    }
    was_off = off;
    for (k = ID_ADJ_REPLACE; k <= ID_ADJ_MATCH; k++) menu_set(k, MUIA_Menuitem_Enabled, fx_on);
    menu_set(ID_F_AGAIN, MUIA_Menuitem_Enabled, fx_on); menu_set(ID_FX_GALLERY, MUIA_Menuitem_Enabled, fx_on); menu_set(ID_FX_LOOKUP, MUIA_Menuitem_Enabled, fx_on);
    menu_set(ID_AUTOTONE, MUIA_Menuitem_Enabled, fx_on); menu_set(ID_AUTOCONTRAST, MUIA_Menuitem_Enabled, fx_on); menu_set(ID_AUTOCOLOR, MUIA_Menuitem_Enabled, fx_on);
  }
  for (i = 0; needmask[i]; i++) menu_set(needmask[i], MUIA_Menuitem_Enabled, al && al->mask);
  for (i = 0; needsel[i]; i++) menu_set(needsel[i], MUIA_Menuitem_Enabled, has && !qm);
  menu_set(ID_SELFROMMASK, MUIA_Menuitem_Enabled, al && al->mask && !qm);
  menu_set(ID_QUICKMASK, MUIA_Menuitem_Checked, qm);
  menu_set(ID_UNGROUP, MUIA_Menuitem_Enabled, al && al->kind == LK_GROUP);
  menu_set(ID_MERGEGROUP, MUIA_Menuitem_Enabled, al && al->kind == LK_GROUP);
  menu_set(ID_FILLSETTINGS, MUIA_Menuitem_Enabled, al && (al->kind == LK_FILL || al->kind == LK_ADJUST));
  menu_set(ID_RASTERIZE, MUIA_Menuitem_Enabled, al && (al->kind == LK_FILL || al->kind == LK_TEXT || al->kind == LK_SHAPE));
  layerstyle_menus(d, menu_set);                         /* 1.1 */
  colorui_menus(has ? d : NULL, menu_set);               /* 1.2 */
  mx_menus(d, menu_set);                                 /* 1.2 */
  menu_set(ID_PASTEOUTSIDE, MUIA_Menuitem_Enabled, has && clip != NULL && d->sel != NULL);
  /* 0.9: type and shape layers' items */
  {
    static const ULONG needtype[] = { ID_EDITTYPE, ID_TYPECHAR, ID_TYPEPARA, ID_TYPEWORKPATH, ID_WARPTEXT, ID_TYPEHORIZ, ID_TYPEVERT, ID_RASTERTYPE, ID_RASTERTYPE2, 0 };
    int ist = al && al->kind == LK_TEXT;
    for (i = 0; needtype[i]; i++) menu_set(needtype[i], MUIA_Menuitem_Enabled, ist || (type_editing && (needtype[i] == ID_TYPECHAR || needtype[i] == ID_TYPEPARA || needtype[i] == ID_WARPTEXT)));
    menu_set(ID_SHAPEPROPS, MUIA_Menuitem_Enabled, al && al->kind == LK_SHAPE);
    menu_set(ID_RASTERSHAPE, MUIA_Menuitem_Enabled, al && al->kind == LK_SHAPE);
  }
  menu_set(ID_PASTEINTO, MUIA_Menuitem_Enabled, has && clip != NULL && d->sel != NULL);
  menu_set(ID_PASTE, MUIA_Menuitem_Enabled, has && clip != NULL);
  menu_set(ID_PASTEINPLACE, MUIA_Menuitem_Enabled, has && clip != NULL);
  menu_set(ID_MERGEDOWN, MUIA_Menuitem_Enabled, has && d->active > 0);
  menu_set(ID_UNDO, MUIA_Menuitem_Enabled, has && doc_can_undo(d));
  menu_set(ID_REDO, MUIA_Menuitem_Enabled, has && doc_can_redo(d));
  menu_set(ID_STEPBACK, MUIA_Menuitem_Enabled, has && doc_can_undo(d));
  menu_set(ID_STEPFWD, MUIA_Menuitem_Enabled, has && doc_can_redo(d));
  snprintf(undo, sizeof undo, "%s %s", tr("Undo"), has && doc_can_undo(d) ? tr(doc_undo_name(d)) : "");
  snprintf(redo, sizeof redo, "%s %s", tr("Redo"), has && doc_can_redo(d) ? tr(doc_redo_name(d)) : "");
  menu_set(ID_UNDO, MUIA_Menuitem_Title, (ULONG)undo);
  menu_set(ID_REDO, MUIA_Menuitem_Title, (ULONG)redo);
  menu_set(ID_DARKUI, MUIA_Menuitem_Checked, pref_dark);
  menu_set(ID_SCR_WINDOW, MUIA_Menuitem_Enabled, prefs.screen_mode != SM_WINDOW);   /* (0.8: the mode in use greyed) */
  menu_set(ID_SCR_OWN, MUIA_Menuitem_Enabled, prefs.screen_mode != SM_SCREEN);
  menu_set(ID_SCR_FULL, MUIA_Menuitem_Enabled, prefs.screen_mode != SM_FULL);
  menu_set(ID_ICONPREF, MUIA_Menuitem_Checked, pref_icons);
  {
    static char labels[NRECENT][64];
    for (i = 0; i < NRECENT; i++) {
      int on = recent[i][0] && i < prefs.nrecent;        /* (0.8: as many as the preferences list) */
      if (on) { const char *f = (const char *)FilePart((STRPTR)recent[i]); snprintf(labels[i], sizeof labels[i], "%d  %s", i + 1, f); }
      else snprintf(labels[i], sizeof labels[i], "%d", i + 1);
      menu_set(ID_RECENT0 + i, MUIA_Menuitem_Title, (ULONG)labels[i]);
      menu_set(ID_RECENT0 + i, MUIA_Menuitem_Enabled, on);
    }
  }
  menu_set(ID_RULERS, MUIA_Menuitem_Checked, has && d->guides.show_rulers);
  menu_set(ID_SHOWGRID, MUIA_Menuitem_Checked, has && d->guides.show_grid);
  menu_set(ID_SHOWGUIDES, MUIA_Menuitem_Checked, has && d->guides.show_guides);
  menu_set(ID_SNAP, MUIA_Menuitem_Checked, has && d->guides.snap);
  {
    /* 1.1: View > Snap To and Lock Guides */
    static const u8 bits[5] = { SNAP_GUIDES, SNAP_GRID, SNAP_EDGES, SNAP_CENTER, SNAP_PIXEL };
    int k;
    for (k = 0; k < 5; k++) menu_set(ID_SNAPTO0 + k, MUIA_Menuitem_Checked, has && (d->guides.snap & bits[k]));
    menu_set(ID_LOCKGUIDES, MUIA_Menuitem_Checked, has && d->guides.lock_guides);
  }
}
void refresh_panels(void)
{
  palwin_set_doc(cur_doc());
  DoMethod(layerpanel, MUIM_Layers_Refresh);
  DoMethod(historypanel, MUIM_History_Refresh);
  DoMethod(navpanel, MUIM_Navigator_Refresh, 1);
  DoMethod(infopanel, MUIM_Info_Refresh, 1);
  DoMethod(chanpanel, MUIM_Channels_Refresh);
  DoMethod(pathpanel, MUIM_Paths_Refresh);
  DoMethod(colorpanel, MUIM_Color_Refresh);
  DoMethod(toolbox, MUIM_Tools_Refresh);
  DoMethod(doctabs, MUIM_DocTabs_Refresh);
  DoMethod(statusbar, MUIM_Status_Refresh);
  set_title();
  update_menus();
  tools2_menus();                                          /* 1.2 */
  t2_log_steps();                                          /* 1.2: the History Log */
}
void refresh_all(void)
{
  DoMethod(canvas, MUIM_Canvas_Refresh);
  refresh_panels();
}

/* ------------------------------------------------------------------ documents */
static void base_name(const char *path, char *out, size_t cap)
{
  const char *f = (const char *)FilePart((STRPTR)path);
  strncpy(out, f, cap - 1); out[cap - 1] = 0;
}
/* a document added as a tab and shown (takes d) */
static Doc *state_copy(Doc *d);
static int doc_show(Doc *d, const char *path, int fmt)
{
  DocView *v;
  if (!d) return 0;
  if (ndocs >= MAXDOCS) { doc_free(d); ask("OK", "Easel can have %s pictures open at once.\nClose one to open another.", "12"); return 0; }
  DoMethod(canvas, MUIM_Canvas_Tool);
  v = &docs[ndocs];
  memset(v, 0, sizeof *v);
  v->doc = d; v->fmt = fmt; v->zoom = 1;
  if (path && path[0]) { strncpy(v->path, path, sizeof v->path - 1); base_name(path, v->name, sizeof v->name); }
  else sprintf(v->name, "%s-%d", tr("Untitled"), ++untitled);
  if (!d->fg && !d->bg) { d->fg = 0x000000; d->bg = 0xFFFFFF; }
  if (!d->guides.grid_x) { d->guides.grid_x = d->guides.grid_y = (u16)prefs.grid_spacing; d->guides.grid_sub = (u8)prefs.grid_sub; }   /* (0.8: the preferences' grid) */
  /* the History Brush's source: the picture as it opened, if there is room for a copy */
  if (AvailMem(MEMF_ANY | MEMF_LARGEST) > (ULONG)d->w * d->h * 8) v->snap = snapshot_take(d);
  /* 1.2: History Options: the first snapshot made at once (when there is room); the History Log */
  if (hist_opts.auto_snapshot && AvailMem(MEMF_ANY | MEMF_LARGEST) > (ULONG)d->w * d->h * 12 && (v->hsnap[0] = state_copy(d)) != NULL) {
    strncpy(v->hsnap_name[0], v->name, sizeof v->hsnap_name[0] - 1); v->nhsnap = 1;
  }
  t2_log_event(d, "Opened", v->name);
  if (curdoc >= 0) t2_spare = curdoc;
  curdoc = ndocs++;
  refresh_all();
  return 1;
}
static void doc_switch(int i)
{
  if (i < 0 || i >= ndocs || i == curdoc) return;
  DoMethod(canvas, MUIM_Canvas_Tool);
  t2_spare = curdoc;                                       /* (1.2: the spare page is the one before) */
  curdoc = i;
  refresh_all();
}
/* the whole of a file, malloc'd; NULL if it can't be read */
static u8 *read_file(const char *path, size_t *len)
{
  FILE *f = fopen(path, "rb");
  long n;
  u8 *buf;
  if (!f) return NULL;
  fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
  if (n <= 0 || !(buf = (u8 *)malloc((size_t)n))) { fclose(f); return NULL; }
  if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
  fclose(f);
  *len = (size_t)n;
  return buf;
}
static int write_file(const char *path, const u8 *data, size_t len)
{
  FILE *f = fopen(path, "wb");
  int ok;
  if (!f) return 0;
  ok = fwrite(data, 1, len, f) == len;
  if (fclose(f)) ok = 0;
  return ok;
}
/* a picture there wasn't the memory for: what it needs said plainly, and a JPEG offered at a
   size that fits in the largest free block (decoded straight to that size, so the full-size
   picture is never in memory) */
/* Decoding runs on a stack of its own: the AVIF decoder (dav1d) keeps big buffers on the stack,
   far more than Easel is started with. The arguments and result go through statics, since the
   function that runs there gets only one pointer. */
#define DECODE_STACK (384L * 1024)
long run_on_stack(struct StackSwapStruct *ss, long (*fn)(void *), void *arg);
static struct { const u8 *data; size_t len; char *err; size_t errcap; Doc *doc; } bigload;
static long bigload_run(void *unused)
{
  (void)unused;
  bigload.doc = codec_load_doc(bigload.data, bigload.len, bigload.err, bigload.errcap);
  return 0;
}
static Doc *load_doc_big_stack(const u8 *data, size_t len, char *err, size_t errcap)
{
  struct StackSwapStruct ss;
  u8 *stack = AllocVec(DECODE_STACK, MEMF_ANY);
  if (!stack) {
    codec_oom = 1;
    snprintf(err, errcap, "%s", tr("Not enough memory to open a picture"));
    return NULL;
  }
  bigload.data = data; bigload.len = len; bigload.err = err; bigload.errcap = errcap; bigload.doc = NULL;
  ss.stk_Lower = stack;
  ss.stk_Upper = (ULONG)(stack + DECODE_STACK);
  ss.stk_Pointer = (APTR)(stack + DECODE_STACK);
  run_on_stack(&ss, bigload_run, NULL);
  FreeVec(stack);
  return bigload.doc;
}

static Doc *load_smaller(const char *path, const char *why)
{
  static char msg[600];
  size_t len = 0;
  u8 *data;
  u32 w = 0, h = 0;
  int kind, k;
  ULONG largest = AvailMem(MEMF_ANY | MEMF_LARGEST), total = AvailMem(MEMF_ANY);
  Doc *d = NULL;
  const char *name = (const char *)FilePart((STRPTR)path);
  if (!(data = read_file(path, &len))) return NULL;
  kind = img_info(data, (u32)len, &w, &h);
  if (kind != IMG_JPEG || !w || !h) {
    if (w && h)
      snprintf(msg, sizeof msg, tr("There isn't enough memory to open %s.\nIt is %lu x %lu pixels, which needs %lu KB in one piece;\nthe largest free block is %lu KB (%lu KB free in all).\nClose other pictures or programs and try again."),
               name, (unsigned long)w, (unsigned long)h, (unsigned long)((unsigned long)w * h * 4 / 1024), (unsigned long)(largest / 1024), (unsigned long)(total / 1024));
    else snprintf(msg, sizeof msg, tr("There isn't enough memory to open %s:\n%s\nClose other pictures or programs and try again."), name, why);
    ask("OK", "%s", msg);
    free(data);
    return NULL;
  }
  /* the smallest reduction whose layer (and some room to work in) fits the largest block */
  for (k = 2; k <= 16; k++) {
    unsigned long need = (unsigned long)(w / k) * (h / k) * 4;
    if (need * 3 / 2 < largest && w / k >= 1 && h / k >= 1) break;
  }
  if (k > 16) {
    snprintf(msg, sizeof msg, tr("There isn't enough memory to open %s, even at a much smaller size\n(the largest free block is %lu KB)."), name, (unsigned long)(largest / 1024));
    ask("OK", "%s", msg);
    free(data);
    return NULL;
  }
  snprintf(msg, sizeof msg, tr("There isn't enough memory to open %s at its full size.\nIt is %lu x %lu pixels, which needs %lu KB in one piece;\nthe largest free block is %lu KB (%lu KB free in all).\n\nOpen it at %lu x %lu pixels instead?"),
           name, (unsigned long)w, (unsigned long)h, (unsigned long)((unsigned long)w * h * 4 / 1024), (unsigned long)(largest / 1024), (unsigned long)(total / 1024),
           (unsigned long)(w / k), (unsigned long)(h / k));
  if (ask("Open Smaller|Cancel", "%s", msg) == 1) {
    char err[160];
    set(app, MUIA_Application_Sleep, TRUE);
    err[0] = 0;
    d = codec_load_doc_scaled(data, len, (i32)(w / k), (i32)(h / k), err, sizeof err);
    set(app, MUIA_Application_Sleep, FALSE);
    if (!d) {
      snprintf(msg, sizeof msg, tr("%s couldn't be opened:\n%s"), name, err[0] ? err : tr("not enough memory"));
      ask("OK", "%s", msg);
    }
  }
  free(data);
  return d;
}
static Doc *load(const char *path, int *fmt)
{
  size_t len = 0;
  u8 *data;
  char err[160];
  Doc *d;
  static char msg[400];
  set(app, MUIA_Application_Sleep, TRUE);
  data = read_file(path, &len);
  if (!data) {
    set(app, MUIA_Application_Sleep, FALSE);
    snprintf(msg, sizeof msg, tr("%s couldn't be read."), (const char *)FilePart((STRPTR)path));
    ask("OK", "%s", msg);
    return NULL;
  }
  *fmt = codec_detect(data, len);
  err[0] = 0;
  /* 0.4: a palette ILBM (or PBM, ACBM) opens as an indexed picture, its colours and cycling kept */
  d = ilbm_indexable(data, len) ? ilbm_read_indexed(data, len, err, sizeof err) : NULL;
  if (!d && svg_detect(data, len)) d = svg_load_doc(data, len, err, sizeof err);   /* (1.1: SVG drawings as shape layers) */
  if (!d) d = load_doc_big_stack(data, len, err, sizeof err);
  free(data);
  set(app, MUIA_Application_Sleep, FALSE);
  if (!d && progress_aborted) return NULL;              /* (0.8: Abort pressed: nothing to say) */
  if (!d && !codec_oom) {
    /* the last resort: whatever datatypes the system has */
    char err2[160];
    set(app, MUIA_Application_Sleep, TRUE);
    d = dt_load(path, err2, sizeof err2);
    set(app, MUIA_Application_Sleep, FALSE);
  }
  if (!d && codec_oom) d = load_smaller(path, err);
  else if (!d) {
    snprintf(msg, sizeof msg, tr("%s couldn't be opened:\n%s"), (const char *)FilePart((STRPTR)path), err[0] ? err : tr("not a picture Easel knows"));
    ask("OK", "%s", msg);
  }
  return d;
}
/* the pictures Easel opens, for the file requester (any file can still be typed in) */
#define OPEN_PATTERN "#?.(png|jpg|jpeg|bmp|iff|ilbm|lbm|pic|gif|ora|avif|tif|tiff|psd|psb|tga|pcx|pbm|pgm|ppm|pnm|pam|pfm|xpm|dds|qoi|ico|cur|hdr|fits|fit|dpx|webp|svg|cin|dcm|dicom|exr|acbm|deep|rgbn|rgb8|anim|anm|info|ras|sun|sgi|rgb|bw|pi1|pi2|pi3|pc1|pc2|pc3|neo)"
static char lastdir[512];
static void recent_add(const char *path)
{
  int i, k = NRECENT - 1;
  if (!path || !path[0]) return;
  for (i = 0; i < NRECENT; i++) if (!Stricmp((STRPTR)recent[i], (STRPTR)path)) { k = i; break; }
  for (i = k; i > 0; i--) strcpy(recent[i], recent[i - 1]);
  strncpy(recent[0], path, sizeof recent[0] - 1); recent[0][sizeof recent[0] - 1] = 0;
}
static void open_path(const char *path)
{
  int fmt = 0, i;
  Doc *d;
  for (i = 0; i < ndocs; i++) if (docs[i].path[0] && !Stricmp((STRPTR)docs[i].path, (STRPTR)path)) { doc_switch(i); return; }
  job_begin(NULL, "Opening the picture");              /* (0.8: progress, and Abort) */
  d = load(path, &fmt);
  job_end();
  if (d) { doc_show(d, path, fmt); recent_add(path); }
  strncpy(lastdir, path, sizeof lastdir - 1);
}
static void open_file(void)
{
  char path[512];
  strcpy(path, lastdir);
  if (!file_request(0, "Open a picture", path, sizeof path, OPEN_PATTERN)) return;
  open_path(path);
}
/* the composite as a flat picture (malloc'd pixels in im) */
static int flat_image(Doc *d, Rgba *im)
{
  if (!rgba_alloc(im, d->w, d->h)) return 0;
  doc_composite(d, doc_box(d), im->px, (long)d->w * 4);
  return 1;
}
static int ext_fmt(const char *path)
{
  const char *e = strrchr((const char *)FilePart((STRPTR)path), '.');
  if (!e) return FMT_UNKNOWN;
  if (!Stricmp((STRPTR)e, (STRPTR)".png")) return FMT_PNG;
  if (!Stricmp((STRPTR)e, (STRPTR)".jpg") || !Stricmp((STRPTR)e, (STRPTR)".jpeg")) return FMT_JPEG;
  if (!Stricmp((STRPTR)e, (STRPTR)".bmp")) return FMT_BMP;
  if (!Stricmp((STRPTR)e, (STRPTR)".iff") || !Stricmp((STRPTR)e, (STRPTR)".ilbm") || !Stricmp((STRPTR)e, (STRPTR)".lbm")) return FMT_ILBM;
  if (!Stricmp((STRPTR)e, (STRPTR)".ora")) return FMT_ORA;
  if (!Stricmp((STRPTR)e, (STRPTR)".gif")) return FMT_GIF;
  if (!Stricmp((STRPTR)e, (STRPTR)".tif") || !Stricmp((STRPTR)e, (STRPTR)".tiff")) return FMT_TIFF;
  if (!Stricmp((STRPTR)e, (STRPTR)".tga")) return FMT_TGA;
  if (!Stricmp((STRPTR)e, (STRPTR)".psd")) return FMT_PSD;
  return xwrite_format_of_ext(e);                      /* (1.1: WebP, PDF, Netpbm, QOI, ICO, DDS, DPX, FITS) */
}
/* 1.1: what xwrite_doc's sub and quality are for the next write (export_sub -1: from the extension) */
static int export_sub = -1, export_pdf_quality;
#define SAVE_PATTERN "#?.(ora|psd|png|jpg|bmp|iff|gif|tif|tiff|tga|webp|pdf|ppm|pgm|pbm|pam|pnm|qoi|ico|dds|dpx|fits|cin|hdr)"
int export_quality = 85, export_indexed;
static int export_tiff_comp = TIFFC_LZW;             /* 1.2: File > Export > TIFF's choice (Save uses LZW) */
/* the document written to path as fmt; 1 if it was */
static int write_doc(Doc *d, const char *path, int fmt)
{
  u8 *data = NULL;
  size_t len = 0;
  Rgba im;
  int ok, model = 0;
  static char msg[300];
  set(app, MUIA_Application_Sleep, TRUE);
  /* 1.2: a CMYK or Lab document in its model where the format can hold it */
  if ((data = colorx_write_model(d, fmt, export_quality, colorui_cmyk(), &len)) != NULL) model = 1;
  else if (fmt == FMT_ORA) data = ora_write(d, &len);
  /* 0.4: GIF; an indexed (or grey) picture's own palette kept in PNG and ILBM */
  else if (fmt == FMT_GIF) data = gif_write_doc(d, 256, QM_WU, DI_FS, 0, &len);
  /* 0.6: TIFF (LZW), Targa (run-length), Photoshop with its layers; PNG and JPEG with File Info */
  else if (fmt == FMT_TIFF) data = tiff_encode_doc(d, export_tiff_comp, &len);
  else if (fmt == FMT_TGA) data = tga_write_doc(d, 1, &len);
  else if (fmt == FMT_PSD) data = psd_write_doc(d, &len);
  else if (fmt == FMT_PNG) data = png_write_doc(d, &len);
  else if (fmt == FMT_JPEG) data = jpeg_write_doc(d, export_quality, &len);
  else if (xwrite_handles(fmt)) {
    const char *e = strrchr((const char *)FilePart((STRPTR)path), '.');
    data = xwrite_doc(d, fmt, export_sub >= 0 ? export_sub : xwrite_sub_of_ext(e), fmt == FMT_PDF_OUT ? export_pdf_quality : 0, &len);
  }
  else if (fmt == FMT_ILBM && d->mode == MODE_INDEXED && !export_indexed) {
    IlbmOpts o;
    char err[120];
    ilbm_opts_default(&o);
    data = ilbm_write_doc(d, &o, &len, err, sizeof err);
  }
  else if (flat_image(d, &im)) {
    switch (fmt) {
    case FMT_JPEG: data = jpeg_encode(&im, export_quality, d->dpi, &len); break;
    case FMT_BMP: data = d->mode == MODE_INDEXED || d->mode == MODE_BITMAP ? colorx_bmp_indexed(d, &len) : bmp_encode(&im, &len); break;   /* (1.2: palette BMPs) */
    case FMT_ILBM: data = ilbm_encode(&im, export_indexed, 1, &len); break;
    default: data = png_encode(&im, d->dpi, &len); break;
    }
    rgba_free(&im);
  }
  if (progress_aborted) { free(data); set(app, MUIA_Application_Sleep, FALSE); return 0; }   /* (0.8: Abort: nothing written) */
  if (data && !model) data = colorx_embed(d, fmt, data, &len);   /* 1.2: the profile kept */
  ok = data && write_file(path, data, len);
  free(data);
  set(app, MUIA_Application_Sleep, FALSE);
  if (!ok) {
    snprintf(msg, sizeof msg, tr("%s couldn't be saved (out of memory, or the disk is full or write-protected)."), (const char *)FilePart((STRPTR)path));
    ask("OK", "%s", msg);
  }
  return ok;
}
/* ------------------------------------------------------------------ 0.6: autosave and recovery */
#define RECOVERY prefs.recovery         /* (0.8: a preference, PROGDIR:Recovery at first) */
/* a file's path in the Recovery drawer */
static void rec_path(char *out, size_t cap, const char *file)
{
  strncpy(out, RECOVERY, cap - 1); out[cap - 1] = 0;
  AddPart((STRPTR)out, (STRPTR)file, cap);
}
/* the state an autosave holds: the undo step on top and how deep */
static ULONG doc_sig(Doc *d) { return (ULONG)d->undo ^ ((ULONG)d->undo_depth << 20) ^ (ULONG)d->nlay; }
static void recovery_drop(DocView *v)
{
  if (v->recov[0]) { DeleteFile((STRPTR)v->recov); v->recov[0] = 0; }
  v->asig = 0;
}
/* every changed picture written to the Recovery drawer as .ora (to a new file, then put in place) */
static void autosave(int force)
{
  static int minutes;
  int i;
  BPTR l;
  if (!force && (!pref_autosave || ++minutes < pref_autosave)) return;
  minutes = 0;
  if (!(l = Lock((STRPTR)RECOVERY, ACCESS_READ)) && !(l = CreateDir((STRPTR)RECOVERY))) return;
  UnLock(l);
  for (i = 0; i < ndocs; i++) {
    DocView *v = &docs[i];
    char tmp[170], clean[64];
    u8 *data;
    size_t len;
    int k;
    if (!v->doc->dirty || doc_sig(v->doc) == v->asig) continue;
    if (!v->recov[0]) {
      for (k = 0; v->name[k] && k < 40; k++) clean[k] = strchr("/:*?#\"", v->name[k]) ? '_' : v->name[k];
      clean[k] = 0;
      { char fn[80]; snprintf(fn, sizeof fn, "%s-%lx.ora", clean, (unsigned long)(ULONG)v->doc); rec_path(v->recov, sizeof v->recov, fn); }
    }
    if (!(data = ora_write(v->doc, &len))) continue;
    snprintf(tmp, sizeof tmp, "%s.new", v->recov);
    if (write_file(tmp, data, len)) { DeleteFile((STRPTR)v->recov); Rename((STRPTR)tmp, (STRPTR)v->recov); v->asig = doc_sig(v->doc); }
    free(data);
  }
}
/* at start-up: pictures autosaved by an Easel that didn't close properly, offered back */
static void recovery_offer(void)
{
  static char names[16][160];
  struct FileInfoBlock *fib;
  BPTR l = Lock((STRPTR)RECOVERY, ACCESS_READ);
  int n = 0, i, r;
  static char msg[200];
  if (!l) return;
  if ((fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL))) {
    if (Examine(l, fib))
      while (ExNext(l, fib) && n < 16) {
        int len = (int)strlen(fib->fib_FileName);
        if (fib->fib_DirEntryType < 0 && len > 4 && !Stricmp((STRPTR)fib->fib_FileName + len - 4, (STRPTR)".ora"))
          rec_path(names[n++], sizeof names[0], fib->fib_FileName);
      }
    FreeDosObject(DOS_FIB, fib);
  }
  UnLock(l);
  if (!n) return;
  if (n == 1) snprintf(msg, sizeof msg, "%s", tr("Easel didn't close properly last time.\nA picture was saved for you. Open it?"));
  else snprintf(msg, sizeof msg, tr("Easel didn't close properly last time.\n%d pictures were saved for you. Open them?"), n);
  r = ask("Open|Delete Them|Later", "%s", msg);
  if (r == 0) return;
  for (i = 0; i < n; i++) {
    if (r == 1) {
      int fmt;
      Doc *d = load(names[i], &fmt);
      if (d && doc_show(d, "", FMT_ORA)) {
        DocView *v = &docs[curdoc];
        char *dash;
        base_name(names[i], v->name, sizeof v->name);
        if ((dash = strrchr(v->name, '-'))) *dash = 0;
        d->dirty = 1;
      }
    }
    DeleteFile((STRPTR)names[i]);
  }
  refresh_all();
}
static int save_as(void);
static void hsnaps_free(DocView *v);
static void save_prefs(void);
static void icon_for(Doc *d, const char *path);
static void paste_amiga(Doc *d);
static int save(void)
{
  DocView *v = cur_view();
  if (!v) return 0;
  if (!v->path[0] || !(codec_writable_06(v->fmt) || xwrite_handles(v->fmt))) return save_as();
  if (v->fmt != FMT_ORA && v->fmt != FMT_PSD && v->doc->nlay > 1 && prefs.ask_flatten &&
      ask("Save as ORA|Flatten and Save|Cancel", "%s", tr("This picture has layers, which the file's format can't keep.\nSave it as an OpenRaster (.ora) file, which keeps them?")) != 2) {
    return save_as();
  }
  if (v->fmt == FMT_JPEG) {
    /* 1.1: the quality the file was saved at, when it can be told, else the preference */
    size_t jl = 0;
    u8 *j = read_file(v->path, &jl);
    int q = j ? jpeg_estimate_quality(j, jl) : 0;
    free(j);
    export_quality = q ? q : prefs.jpeg_quality;
  }
  if (!write_doc(v->doc, v->path, v->fmt)) return 0;
  icon_for(v->doc, v->path);
  v->doc->dirty = 0;
  recovery_drop(v);
  /* 1.2: History Options' New Snapshot When Saving, and the History Log */
  if (hist_opts.snapshot_on_save && v->nhsnap < MAXHSNAP && (v->hsnap[v->nhsnap] = state_copy(v->doc)) != NULL) {
    snprintf(v->hsnap_name[v->nhsnap], sizeof v->hsnap_name[0], "%.12s %s", tr("Saved"), v->name); v->nhsnap++;
  }
  t2_log_event(v->doc, "Saved", v->name);
  refresh_panels();
  return 1;
}
static void with_ext(char *path, size_t cap, const char *ext)
{
  char *dot = strrchr((char *)FilePart((STRPTR)path), '.');
  if (dot) *dot = 0;
  strncat(path, ext, cap - 1 - strlen(path));
}
static int save_as(void)
{
  DocView *v = cur_view();
  char path[512];
  int fmt;
  if (!v) return 0;
  strcpy(path, v->path[0] ? v->path : lastdir);
  if (!v->path[0]) { *PathPart((STRPTR)path) = 0; AddPart((STRPTR)path, (STRPTR)v->name, sizeof path); }
  if (v->doc->nlay > 1) with_ext(path, sizeof path, ".ora");
  else if (!v->path[0]) {
    /* 0.8: a new picture in the format the preferences ask for */
    static const char *const ext[SF_N] = { ".ora", ".png", ".jpg", ".tif", ".psd", ".iff" };
    with_ext(path, sizeof path, ext[prefs.save_fmt]);
  }
  if (!file_request(1, "Save the picture as (.ora keeps the layers)", path, sizeof path, SAVE_PATTERN)) return 0;
  fmt = ext_fmt(path);
  if (fmt == FMT_UNKNOWN) { fmt = FMT_ORA; strncat(path, ".ora", sizeof path - 1 - strlen(path)); }
  if (fmt == FMT_JPEG) { export_quality = 0; dlg_export(FMT_JPEG); if (!export_quality) return 0; }
  if (!write_doc(v->doc, path, fmt)) return 0;
  icon_for(v->doc, path);
  strcpy(v->path, path); v->fmt = fmt;
  recent_add(path);
  base_name(path, v->name, sizeof v->name);
  v->doc->dirty = 0;
  recovery_drop(v);
  strcpy(lastdir, path);
  refresh_panels();
  return 1;
}
/* File > Export: a flat copy in the format asked for, the document's own file left as it is */
static void export_as(int fmt, int indexed)
{
  DocView *v = cur_view();
  char path[512];
  static const char *const ext[] = { "", ".png", ".jpg", ".bmp", ".iff", ".gif", ".ora", ".avif", ".tif", ".psd", ".tga" };
  if (!v) return;
  strcpy(path, v->path[0] ? v->path : lastdir);
  if (!v->path[0]) { *PathPart((STRPTR)path) = 0; AddPart((STRPTR)path, (STRPTR)v->name, sizeof path); }
  with_ext(path, sizeof path, fmt <= FMT_TGA ? ext[fmt] : xwrite_ext(fmt));
  if (fmt == FMT_JPEG) { export_quality = 0; dlg_export(FMT_JPEG); if (!export_quality) return; }
  if (!file_request(1, "Export as", path, sizeof path, NULL)) return;
  export_indexed = indexed;
  write_doc(v->doc, path, fmt);
  export_indexed = 0;
}
static void start_path(DocView *v, char *path, size_t cap);
/* 1.1: File > Export's new formats, with the one choice each has */
static void export_x(int fmt)
{
  int r;
  export_sub = -1; export_pdf_quality = 0;
  if (fmt == FMT_PDF_OUT) {
    if (!(r = ask("Lossless|JPEG|Cancel", "%s", tr("Put the picture in the PDF without loss (Flate), or as a JPEG (smaller)?")))) return;
    if (r == 2) { export_quality = 0; dlg_export(FMT_JPEG); if (!export_quality) return; export_pdf_quality = export_quality; }
  } else if (fmt == FMT_DDS) {
    if (!(r = ask("BC3 (DXT5)|BC1 (DXT1)|Uncompressed|Cancel", "%s", tr("How should the texture be stored? (Each comes with its mipmaps.)")))) return;
    export_sub = r == 1 ? DDS_BC3 : r == 2 ? DDS_BC1 : DDS_RGBA;
  } else if (fmt == FMT_WEBP) {
    /* 1.2: lossless, or lossy at a quality */
    static LONG wq = 80;
    if (!(r = ask("Lossless|Lossy|Cancel", "%s", tr("Save the WebP picture without loss, or lossy (much smaller)?")))) return;
    if (r == 2) { if (!ask_number("WebP Quality", "Quality (1-100):", &wq, 1, 100)) return; export_sub = (int)wq; }
    else export_sub = 0;
  } else if (fmt == FMT_DPX) {
    if (!(r = ask("8 Bits|10 Bits|12 Bits|16 Bits|Cancel", "%s", tr("Bits per channel in the DPX file?")))) return;
    export_sub = r == 1 ? 0 : r == 2 ? 1 : r == 3 ? 12 : 16;      /* (1.2: 12 and 16 bits) */
  }
  export_as(fmt, 0);
  export_sub = -1; export_pdf_quality = 0;
}
/* 1.1: File > Export > Layers as Animated GIF / IFF ANIM: each pixel layer a frame, bottom first */
static void export_anim(int gif)
{
  DocView *v = cur_view();
  char path[512], err[160];
  AnimOpts o;
  u8 *data;
  size_t len = 0;
  static char msg[300];
  if (!v) return;
  anim_opts_default(&o);
  start_path(v, path, sizeof path);
  with_ext(path, sizeof path, gif ? ".gif" : ".anim");
  if (!file_request(1, gif ? "Export the layers as an animated GIF" : "Export the layers as an IFF ANIM", path, sizeof path, NULL)) return;
  set(app, MUIA_Application_Sleep, TRUE);
  err[0] = 0;
  data = gif ? gif_write_anim(v->doc, &o, &len, err, sizeof err) : anim_write(v->doc, &o, &len, err, sizeof err);
  if (data && !write_file(path, data, len)) { free(data); data = NULL; strcpy(err, tr("the disk is full or write-protected")); }
  free(data);
  set(app, MUIA_Application_Sleep, FALSE);
  if (!data && !progress_aborted) { snprintf(msg, sizeof msg, tr("%s couldn't be saved:\n%s"), (const char *)FilePart((STRPTR)path), err[0] ? err : tr("not enough memory")); ask("OK", "%s", msg); }
}
/* 1.1: File > Import Frames to Layers: every frame of an animated GIF or IFF ANIM, a layer each */
static void import_frames(void)
{
  char path[512], err[160];
  static char msg[400];
  Frames *f;
  u8 *data;
  size_t len = 0;
  Doc *d = NULL;
  int r;
  strcpy(path, lastdir);
  if (!file_request(0, "Import the frames of an animated GIF or IFF ANIM", path, sizeof path, "#?.(gif|anim|anm|iff)")) return;
  strncpy(lastdir, path, sizeof lastdir - 1);
  if (!(f = (Frames *)malloc(sizeof *f))) return;
  set(app, MUIA_Application_Sleep, TRUE);
  err[0] = 0;
  if ((data = read_file(path, &len))) {
    /* the frames may take three quarters of the memory free (the layers need as much again) */
    r = frames_read(data, len, f, FRAMES_MAX, (long)(AvailMem(MEMF_ANY) / 8 * 3), err, sizeof err);
    free(data);
    if (r) d = frames_to_doc(f);
    else f->n = 0;
  } else strcpy(err, tr("it couldn't be read"));
  set(app, MUIA_Application_Sleep, FALSE);
  if (d && f->truncated) {
    snprintf(msg, sizeof msg, tr("Only the first %ld frames fitted in memory (or in the %ld layers a picture can have)."), (long)f->n, (long)FRAMES_MAX);
    ask("OK", "%s", msg);
  }
  frames_free(f);
  free(f);
  if (!d) { snprintf(msg, sizeof msg, tr("%s couldn't be opened:\n%s"), (const char *)FilePart((STRPTR)path), err[0] ? err : tr("not enough memory")); ask("OK", "%s", msg); return; }
  doc_show(d, "", FMT_ORA);
  base_name(path, docs[curdoc].name, sizeof docs[curdoc].name);
}
/* 1.1: File > Export > Quick Export as PNG: beside the picture's file (or in the last drawer), no questions */
static void quick_png(void)
{
  DocView *v = cur_view();
  char path[512];
  BPTR l;
  if (!v) return;
  start_path(v, path, sizeof path);
  with_ext(path, sizeof path, ".png");
  if (v->fmt == FMT_PNG && v->path[0] && !Stricmp((STRPTR)path, (STRPTR)v->path)) with_ext(path, sizeof path, "-copy.png");
  if ((l = Lock((STRPTR)path, ACCESS_READ))) {
    UnLock(l);
    if (!ask("Replace|Cancel", tr("%s is there already. Replace it?"), (const char *)FilePart((STRPTR)path))) return;
  }
  write_doc(v->doc, path, FMT_PNG);
}
/* 1.1: File > Export > Paths to SVG: the paths and shape layers, with the document's bounds */
static void export_svg(void)
{
  DocView *v = cur_view();
  char path[512];
  u8 *data;
  size_t len = 0;
  if (!v) return;
  start_path(v, path, sizeof path);
  with_ext(path, sizeof path, ".svg");
  if (!file_request(1, "Export the paths and shapes as SVG", path, sizeof path, "#?.svg")) return;
  if (!(data = svg_write_paths(v->doc, 1, &len)) || !write_file(path, data, len)) ask("OK", "%s", tr("The SVG file couldn't be written (out of memory, or the disk is full or write-protected)."));
  free(data);
}
/* 1.1: File > Close Others */
static int doc_close(int i);
static void close_others(void)
{
  int i;
  for (i = ndocs - 1; i >= 0; i--) if (i != curdoc && !doc_close(i)) return;
}
/* File > Export > IFF ILBM (Amiga options): planes, EHB, HAM, SHAM, mask, colour cycling, ModeID */
static void export_ilbm_options(void)
{
  DocView *v = cur_view();
  IlbmOpts o;
  char path[512], err[160];
  static char msg[300];
  u8 *data;
  size_t len = 0;
  if (!v) return;
  ilbm_opts_default(&o);
  if (!modedlg_ilbm(v->doc, &o)) return;
  strcpy(path, v->path[0] ? v->path : lastdir);
  if (!v->path[0]) { *PathPart((STRPTR)path) = 0; AddPart((STRPTR)path, (STRPTR)v->name, sizeof path); }
  with_ext(path, sizeof path, ".iff");
  if (!file_request(1, "Export as IFF ILBM", path, sizeof path, NULL)) return;
  set(app, MUIA_Application_Sleep, TRUE);
  err[0] = 0;
  data = ilbm_write_doc(v->doc, &o, &len, err, sizeof err);
  if (data && !write_file(path, data, len)) { free(data); data = NULL; strcpy(err, tr("the disk is full or write-protected")); }
  free(data);
  set(app, MUIA_Application_Sleep, FALSE);
  if (!data) { snprintf(msg, sizeof msg, tr("%s couldn't be saved:\n%s"), (const char *)FilePart((STRPTR)path), err[0] ? err : tr("not enough memory")); ask("OK", "%s", msg); }
}
/* the format a path's extension asks for, PNG when it names none Easel writes (and the extension added) */
static int out_fmt(char *path, size_t cap)
{
  int fmt = ext_fmt(path);
  if (fmt == FMT_UNKNOWN) { fmt = FMT_PNG; strncat(path, ".png", cap - 1 - strlen(path)); }
  return fmt;
}
/* the requester's starting name: the picture's own, or its tab's name in the last drawer */
static void start_path(DocView *v, char *path, size_t cap)
{
  strncpy(path, v->path[0] ? v->path : lastdir, cap - 1); path[cap - 1] = 0;
  if (!v->path[0]) { *PathPart((STRPTR)path) = 0; AddPart((STRPTR)path, (STRPTR)v->name, cap); }
}
/* File > Save a Copy: written in the format its name asks for; the picture's own file and name stay */
static void save_copy(void)
{
  DocView *v = cur_view();
  char path[512];
  int fmt;
  if (!v) return;
  start_path(v, path, sizeof path);
  with_ext(path, sizeof path, v->doc->nlay > 1 ? "-copy.ora" : "-copy.png");
  if (!file_request(1, "Save a copy as (the extension picks the format)", path, sizeof path, SAVE_PATTERN)) return;
  fmt = out_fmt(path, sizeof path);
  if (fmt == FMT_JPEG) { export_quality = 0; dlg_export(FMT_JPEG); if (!export_quality) return; }
  write_doc(v->doc, path, fmt);
}
/* File > Export > Export As: a flat copy, scaled by a percentage, in the format its name asks for */
/* 1.2: File > Export As: scale and its method, canvas size, matte, metadata, sRGB (core/exportx.c) */
static ExportOpts xo_last;
static int xo_set;
static void xo_take(Doc *d, const LONG *v, int c, int t)
{
  static const int methods[6] = { RS_NEAREST, RS_BILINEAR, RS_BICUBIC, RS_SMOOTHER, RS_SHARPER, RS_DETAILS };
  (void)d;
  xo_last.pct = (int)v[0]; xo_last.canvas_w = v[1]; xo_last.canvas_h = v[2]; xo_last.metadata = (int)v[3];
  xo_last.method = methods[c >= 0 && c < 6 ? c : 2]; xo_last.matte = (int)v[7]; xo_last.to_srgb = t;
}
static void export_scaled(void)
{
  static const char *const rs[] = { "Nearest Neighbour", "Bilinear", "Bicubic", "Bicubic Smoother", "Bicubic Sharper", "Preserve Details", NULL };
  static const char *const mt[] = { "None (keep transparency)", "White", "Black", "Foreground Color", "Background Color", NULL };
  static const char *rsl[7], *mtl[6];
  static const DlgParam p[4] = { { "Scale (%):", 1, 1000, 100 }, { "Canvas width (px, 0: as scaled):", 0, MAXDIM, 0 },
                                 { "Canvas height (px, 0: as scaled):", 0, MAXDIM, 0 }, { "Metadata (0 none, 1 copyright, 2 all):", 0, 2, 2 } };
  DocView *v = cur_view();
  char path[512];
  int fmt, i, mi = 2;
  LONG init[4];
  Doc *t;
  if (!v) return;
  if (!xo_set) { exportx_defaults(&xo_last); xo_set = 1; }
  for (i = 0; i < 6; i++) rsl[i] = tr(rs[i]);
  rsl[6] = NULL;
  for (i = 0; i < 5; i++) mtl[i] = tr(mt[i]);
  mtl[5] = NULL;
  { static const int methods[6] = { RS_NEAREST, RS_BILINEAR, RS_BICUBIC, RS_SMOOTHER, RS_SHARPER, RS_DETAILS }; for (i = 0; i < 6; i++) if (methods[i] == xo_last.method) mi = i; }
  init[0] = xo_last.pct; init[1] = xo_last.canvas_w; init[2] = xo_last.canvas_h; init[3] = xo_last.metadata;
  if (!dlg_sliders("Export As", p, 4, rsl, "Resample:", mi, mtl, "Matte:", xo_last.matte, "Convert to sRGB", xo_last.to_srgb, init, xo_take, NULL, 0)) return;
  start_path(v, path, sizeof path);
  with_ext(path, sizeof path, ".png");
  if (!file_request(1, "Export as (the extension picks the format)", path, sizeof path, SAVE_PATTERN)) return;
  fmt = out_fmt(path, sizeof path);
  if (fmt == FMT_ORA) fmt = FMT_PNG;
  if (fmt == FMT_JPEG) { export_quality = 0; dlg_export(FMT_JPEG); if (!export_quality) return; }
  set(app, MUIA_Application_Sleep, TRUE);
  t = exportx_prepare(v->doc, &xo_last);
  set(app, MUIA_Application_Sleep, FALSE);
  if (!t) { ask("OK", "%s", tr("There isn't enough memory for that.")); return; }
  write_doc(t, path, fmt);
  doc_free(t);
}
/* File > Export > Layers to Files: each layer with pixels as a file of its own, named after it */
static void layers_to_files(void)
{
  DocView *v = cur_view();
  char path[512], base[512], name[640], ext[8];
  int fmt, i, n = 0;
  static char msg[200];
  if (!v) return;
  start_path(v, path, sizeof path);
  with_ext(path, sizeof path, ".png");
  if (!file_request(1, "Layers to Files: the drawer, and the name they start with", path, sizeof path, SAVE_PATTERN)) return;
  fmt = out_fmt(path, sizeof path);
  if (fmt == FMT_ORA) fmt = FMT_PNG;
  if (fmt == FMT_JPEG) { export_quality = 0; dlg_export(FMT_JPEG); if (!export_quality) return; }
  strcpy(base, path);
  { char *dot = strrchr((char *)FilePart((STRPTR)base), '.'); strncpy(ext, dot ? dot : ".png", sizeof ext - 1); ext[sizeof ext - 1] = 0; if (dot) *dot = 0; }
  for (i = 0; i < v->doc->nlay; i++) {
    Layer *l = v->doc->lay[i];
    Doc *t;
    char clean[64];
    int k;
    if (!l->px) continue;
    for (k = 0; l->name[k] && k < (int)sizeof clean - 1; k++) clean[k] = strchr("/:*?#\"", l->name[k]) ? '_' : l->name[k];
    clean[k] = 0;
    snprintf(name, sizeof name, "%s_%02d_%s%s", base, i + 1, clean, ext);
    if (!(t = doc_new(v->doc->w, v->doc->h, 0, 1))) break;
    memcpy(t->lay[0]->px, l->px, (size_t)v->doc->w * v->doc->h * 4);
    t->dpi = v->doc->dpi;
    {
      long pp = progress_push(1000L * i / v->doc->nlay, 1000L * (i + 1) / v->doc->nlay);   /* (0.8: each layer a share) */
      if (write_doc(t, name, fmt)) n++;
      progress_pop(pp);
    }
    doc_free(t);
    if (progress_aborted) break;
  }
  snprintf(msg, sizeof msg, tr("%d layers were written."), n);
  ask("OK", "%s", msg);
}
/* the ASL requester picking several files; how many went into paths */
static int file_request_multi(const char *titletext, char paths[][512], int max)
{
  struct FileRequester *fr;
  int n = 0, i;
  char dir[400];
  strncpy(dir, lastdir, sizeof dir - 1); dir[sizeof dir - 1] = 0;
  *PathPart((STRPTR)dir) = 0;
  if (!(fr = (struct FileRequester *)MUI_AllocAslRequestTags(ASL_FileRequest, ASLFR_TitleText, (ULONG)tr(titletext),
        ASLFR_DoMultiSelect, TRUE, ASLFR_InitialDrawer, (ULONG)dir, ASLFR_InitialPattern, (ULONG)OPEN_PATTERN, ASLFR_DoPatterns, TRUE, TAG_DONE))) return 0;
  if (MUI_AslRequestTags(fr, ASLFR_Window, (ULONG)xget(win, MUIA_Window_Window), TAG_DONE)) {
    if (fr->fr_NumArgs > 0)
      for (i = 0; i < fr->fr_NumArgs && n < max; i++) {
        strncpy(paths[n], (const char *)fr->fr_Drawer, 511); paths[n][511] = 0;
        if (AddPart((STRPTR)paths[n], fr->fr_ArgList[i].wa_Name, 512)) n++;
      }
    else if (fr->fr_File[0]) { strncpy(paths[0], (const char *)fr->fr_Drawer, 511); paths[0][511] = 0; if (AddPart((STRPTR)paths[0], fr->fr_File, 512)) n = 1; }
  }
  MUI_FreeAslRequest(fr);
  if (n) strcpy(lastdir, paths[0]);
  return n;
}
/* File > Load Files into Stack: several pictures as the layers of one new picture, each named
   after its file and centred (the canvas is the first one's size) */
static void load_stack(void)
{
  static char paths[32][512];
  int n = file_request_multi("Load Files into Stack", paths, 32), i, fmt;
  Doc *d = NULL;
  char name[108];
  if (!n) return;
  for (i = 0; i < n && !progress_aborted; i++) {
    long pp = progress_push(1000L * i / n, 1000L * (i + 1) / n);   /* (0.8: each file a share) */
    Doc *o = load(paths[i], &fmt);
    Rgba im;
    progress_pop(pp);
    if (!o) continue;
    base_name(paths[i], name, sizeof name);
    if (!d) {
      /* the first: flattened, it is the picture */
      if (!flat_image(o, &im)) { doc_free(o); continue; }
      d = doc_adopt(im.w, im.h, im.px);
      doc_free(o);
      if (!d) break;
      strncpy(d->lay[0]->name, name, sizeof d->lay[0]->name - 1);
      continue;
    }
    if (flat_image(o, &im)) {
      Clip c;
      c.w = im.w; c.h = im.h; c.px = im.px; c.x = (d->w - im.w) / 2; c.y = (d->h - im.h) / 2;
      if (clip_paste(d, &c, 1)) strncpy(d->lay[d->active]->name, name, sizeof d->lay[0]->name - 1);
      rgba_free(&im);
    }
    doc_free(o);
  }
  if (!d) return;
  if (progress_aborted) { doc_free(d); return; }        /* (0.8: Abort pressed) */
  doc_forget_undo(d);
  d->dirty = 1;
  doc_show(d, "", FMT_ORA);
}
/* Edit > Paste from Amiga Clipboard: the picture another program copied, as a new layer (or, with no
   picture open, a new picture) */
static void paste_amiga(Doc *d)
{
  char err[120];
  Doc *o;
  Rgba im;
  set(app, MUIA_Application_Sleep, TRUE);
  o = clipboard_get_doc(err, sizeof err);
  set(app, MUIA_Application_Sleep, FALSE);
  if (!o) { ask("OK", "%s", err[0] ? err : tr("There is no picture on the clipboard.")); return; }
  if (!d) { doc_show(o, "", FMT_ORA); return; }
  if (flat_image(o, &im)) {
    Clip c;
    c.w = im.w; c.h = im.h; c.px = im.px; c.x = (d->w - im.w) / 2; c.y = (d->h - im.h) / 2;
    clip_paste(d, &c, 1);
    rgba_free(&im);
  }
  doc_free(o);
}
/* a Workbench icon for a picture just saved, if it has none and the preference is on */
static void icon_for(Doc *d, const char *path)
{
  char ip[520];
  BPTR l;
  Rgba im;
  u8 *data;
  size_t len;
  if (!pref_icons) return;
  snprintf(ip, sizeof ip, "%s.info", path);
  if ((l = Lock((STRPTR)ip, ACCESS_READ))) { UnLock(l); return; }
  if (!flat_image(d, &im)) return;
  if ((data = icon_write(&im, ICON_GLOW, ICON_TOOL_EASEL, &len))) { write_file(ip, data, len); free(data); }
  rgba_free(&im);
}
/* the Brushes panel and DPaint window ask for a brush made from the picture: the selection (or all of it) */
static void brushui_request(int what)
{
  Doc *d = cur_doc();
  if (!d) return;
  if (what == BWR_DEFINE) brushwin_define(d);
  else if (what == BWR_PICKUP || what == BWR_PICKUP_SEL) {
    if (!d->sel) { ask("OK", "%s", tr("Select what to pick up as the brush first (with a marquee or lasso).")); return; }
    brushwin_pickup(d, d->selbox, what == BWR_PICKUP_SEL);
  }
  refresh_all();
}
static int may_discard(int i)
{
  static char msg[300];
  int r;
  if (!docs[i].doc->dirty) return 1;
  doc_switch(i);
  snprintf(msg, sizeof msg, tr("Save the changes to %s before closing?"), docs[i].name);
  r = ask("Save|Don't Save|Cancel", "%s", msg);
  if (r == 1) return save();
  return r == 2;
}
static int doc_close(int i)
{
  if (i < 0 || i >= ndocs) return 1;
  if (!may_discard(i)) return 0;
  DoMethod(canvas, MUIM_Canvas_Cancel);
  palwin_stop();
  if (curdoc == i) palwin_set_doc(NULL);
  recovery_drop(&docs[i]);
  comps_forget(docs[i].doc);                             /* 1.2 */
  hsnaps_free(&docs[i]);
  snapshot_free(docs[i].snap);
  doc_free(docs[i].doc);
  memmove(&docs[i], &docs[i + 1], (size_t)(ndocs - i - 1) * sizeof docs[0]);
  ndocs--;
  if (curdoc >= ndocs) curdoc = ndocs - 1;
  else if (curdoc > i) curdoc--;
  refresh_all();
  return 1;
}
static int close_all(void)
{
  while (ndocs) if (!doc_close(ndocs - 1)) return 0;
  return 1;
}
static void new_doc(void)
{
  LONG w = 800, h = 600, dpi = prefs.dpi;
  int bg = 0;
  Doc *d;
  Doc *c = cur_doc();
  if (!dlg_new_values(&w, &h, &dpi, &bg)) return;
  d = doc_new((i32)w, (i32)h, bg == 1 && c ? c->bg : 0xFFFFFF, bg == 2);
  if (!d) { ask("OK", "%s", tr("There isn't enough memory for a picture that size.")); return; }
  d->dpi = (u16)dpi;
  if (c) { d->fg = c->fg; d->bg = c->bg; } else { d->fg = 0; d->bg = 0xFFFFFF; }
  doc_show(d, "", FMT_ORA);
}
static void transform_dialog(void);
/* 0.8: the window closed and opened again (the theme, its text size or the screen changed) */
static void win_reopen(void)
{
  set(win, MUIA_Window_Open, FALSE);
  theme_set_dark(pref_dark);
  theme_set_large(prefs.large_text);
  set(win, MUIA_Window_Open, TRUE);
  refresh_all();
}
/* File > Place: another picture as a new layer, centred, then scaled and turned as asked */
static void place_file(void)
{
  Doc *d = cur_doc(), *o;
  char path[512];
  int fmt, i;
  if (!d) return;
  strcpy(path, lastdir);
  if (!file_request(0, "Place a picture as a layer", path, sizeof path, OPEN_PATTERN)) return;
  if (!(o = load(path, &fmt))) return;
  {
    Rgba im;
    Clip c;
    if (!flat_image(o, &im)) { doc_free(o); return; }
    c.w = im.w; c.h = im.h; c.px = im.px; c.x = (d->w - im.w) / 2; c.y = (d->h - im.h) / 2;
    clip_paste(d, &c, 1);
    rgba_free(&im);
    for (i = 0; i < 1; i++) ;
  }
  doc_free(o);
  {
    char name[64];
    base_name(path, name, sizeof name);
    doc_set_layer_props(d, d->active, name, -1, -1, -1);
  }
  refresh_all();
  /* 0.6: then its size and angle, as Place goes into Free Transform (1.1: unless the preferences skip it) */
  if (!prefs.skip_place_xf) transform_dialog();
}

/* ------------------------------------------------------------------ 0.8: History snapshots */
/* a whole copy of a picture as it is now (layers, selection, channels, palette; no undo), or NULL */
static Doc *state_copy(Doc *d)
{
  Doc *c = (Doc *)malloc(sizeof *c);
  size_t n = (size_t)d->w * d->h;
  int i, ok = 1;
  if (!c) return NULL;
  *c = *d;
  c->undo = c->redo = NULL; c->undo_depth = 0; c->undo_bytes = 0;
  c->sel = c->lastsel = c->qmask = NULL; c->nlay = 0; c->nchan = 0;
  c->npaths = 0; c->workpath = NULL; c->annot = NULL;     /* (1.2: the paths and notes stay the picture's: doc_free(c) mustn't free them) */
  for (i = 0; i < d->nlay && ok; i++) if ((c->lay[i] = layer_copy(d->lay[i], d->w, d->h)) != NULL) c->nlay++; else ok = 0;
  for (i = 0; i < d->nchan && ok; i++) if ((c->chan[i] = (u8 *)malloc(n)) != NULL) { memcpy(c->chan[i], d->chan[i], n); c->nchan++; } else ok = 0;
  if (ok && d->sel && (c->sel = (u8 *)malloc(n)) != NULL) memcpy(c->sel, d->sel, n);
  else if (d->sel) ok = 0;
  if (!ok) { doc_free(c); return NULL; }
  return c;
}
/* the picture made as snapshot s was, as one step that can be undone */
static int state_restore(Doc *d, Doc *s, const char *name)
{
  Doc *c = state_copy(s);
  int i;
  if (!c) return 0;
  doc_begin(d, name);
  doc_note_all(d);
  for (i = 0; i < d->nlay; i++) layer_free(d->lay[i]);
  for (i = 0; i < d->nchan; i++) free(d->chan[i]);
  free(d->sel); free(d->qmask);
  memcpy(d->lay, c->lay, sizeof d->lay); d->nlay = c->nlay; d->active = c->active;
  memcpy(d->chan, c->chan, sizeof d->chan); memcpy(d->chan_name, c->chan_name, sizeof d->chan_name); d->nchan = c->nchan;
  d->sel = c->sel; d->selbox = c->selbox; d->qmask = NULL;
  d->w = c->w; d->h = c->h;
  d->mode = c->mode; d->npal = c->npal; memcpy(d->pal, c->pal, sizeof d->pal); memcpy(d->pal_lock, c->pal_lock, sizeof d->pal_lock);
  memcpy(d->cycle, c->cycle, sizeof d->cycle); d->ncycle = c->ncycle; d->modeid = c->modeid;
  doc_end(d);
  free(c);                              /* (its buffers are the picture's now) */
  return 1;
}
/* 1.2: (tools2.h) main.c's helpers for tools2.c */
int  t2_file_request(int save, const char *title, char *path, size_t cap, const char *pattern) { return file_request(save, title, path, cap, pattern); }
int  t2_doc_show(Doc *d, const char *path) { return doc_show(d, path, FMT_ORA); }
void t2_open_path(const char *path) { open_path(path); }
void t2_doc_switch(int i) { doc_switch(i); }
Doc *t2_state_copy(Doc *d) { return state_copy(d); }
static void hsnaps_free(DocView *v)
{
  int i;
  for (i = 0; i < v->nhsnap; i++) doc_free(v->hsnap[i]);
  v->nhsnap = 0;
}

/* ------------------------------------------------------------------ 0.8: View > Screen Mode */
/* 0.8: Easel's own screen (View > Screen Mode), a public screen like the Workbench's or in the mode
   chosen, while it is open; and the public screen asked for at the start, for going back to a window */
static struct Screen *own_scr;
static ULONG own_id;
static char user_screen[64];
#define OWN_NAME "EASEL.1"
/* Full Screen: on Easel's own screen, whose title bar is then hidden (the menus still show it) */
static int full_screen(void)
{
  if (own_scr) ShowTitle(own_scr, prefs.screen_mode != SM_FULL);
  return own_scr && prefs.screen_mode == SM_FULL;
}
/* Easel's own screen opened (if it isn't, in prefs.screen_id's mode or the Workbench's); 1 if it is */
static int own_open(void)
{
  ULONG id = prefs.screen_id;
  if (own_scr && own_id == id) return 1;
  if (own_scr) { PubScreenStatus(own_scr, PSNF_PRIVATE); if (!CloseScreen(own_scr)) { PubScreenStatus(own_scr, 0); return 1; } own_scr = NULL; }
  own_scr = OpenScreenTags(NULL, SA_LikeWorkbench, TRUE, (ULONG)(id ? SA_DisplayID : TAG_IGNORE), id,
                           (ULONG)(id ? SA_Width : TAG_IGNORE), STDSCREENWIDTH, (ULONG)(id ? SA_Height : TAG_IGNORE), STDSCREENHEIGHT,
                           SA_Title, (ULONG)"Easel", SA_PubName, (ULONG)OWN_NAME, SA_ShowTitle, TRUE, TAG_DONE);
  if (!own_scr && id) own_scr = OpenScreenTags(NULL, SA_LikeWorkbench, TRUE, SA_Title, (ULONG)"Easel", SA_PubName, (ULONG)OWN_NAME, TAG_DONE);
  if (!own_scr) return 0;
  own_id = id;
  PubScreenStatus(own_scr, 0);
  return 1;
}
/* Easel's own screen closed, if nothing else is on it */
static void own_close(void)
{
  if (!own_scr) return;
  PubScreenStatus(own_scr, PSNF_PRIVATE);
  if (CloseScreen(own_scr)) own_scr = NULL; else PubScreenStatus(own_scr, 0);
}
static int win_make(void);
/* the window made again on the screen the preferences ask for (a window on the Workbench or the
   public screen asked for, Easel's own screen, or all of it), its pictures shown again */
static void screen_switch(int mode)
{
  int old = prefs.screen_mode;
  if (mode != SM_WINDOW && !own_open()) { ask("OK", "%s", tr("Easel's own screen couldn't be opened (not enough memory, or the mode isn't available).")); return; }
  prefs.screen_mode = mode;
  set(win, MUIA_Window_Open, FALSE);
  DoMethod(app, OM_REMMEMBER, (ULONG)win);
  MUI_DisposeObject(win);
  win = NULL;
  strcpy(pub_screen, mode == SM_WINDOW ? user_screen : OWN_NAME);
  if (mode == SM_WINDOW) own_close();
  if (!win_make()) {
    /* back where it was */
    prefs.screen_mode = old;
    strcpy(pub_screen, old == SM_WINDOW ? user_screen : OWN_NAME);
    if (old != SM_WINDOW) own_open();
    if (!win_make()) return;
  }
  appdrop_attach(win, app, open_path);
  set_tool(tool);
  refresh_all();
}
/* View > Screen Mode > Choose Screen Mode: the mode of Easel's own screen, from the ASL requester */
static void screen_choose(void)
{
  struct ScreenModeRequester *sm;
  if (!(sm = (struct ScreenModeRequester *)MUI_AllocAslRequestTags(ASL_ScreenModeRequest, ASLSM_TitleText, (ULONG)tr("Easel's own screen"),
        (ULONG)(prefs.screen_id ? ASLSM_InitialDisplayID : TAG_IGNORE), prefs.screen_id, TAG_DONE))) return;
  if (MUI_AslRequestTags(sm, ASLSM_Window, (ULONG)xget(win, MUIA_Window_Window), TAG_DONE)) {
    prefs.screen_id = sm->sm_DisplayID;
    MUI_FreeAslRequest(sm);
    screen_switch(prefs.screen_mode == SM_WINDOW ? SM_SCREEN : prefs.screen_mode);
    return;
  }
  MUI_FreeAslRequest(sm);
}

/* ------------------------------------------------------------------ commands */
static void layer_new_named(Doc *d)
{
  char name[32];
  static int n = 1;
  int i, used;
  Layer *l;
  /* "Layer N" with the first N not in use */
  for (;; n++) {
    sprintf(name, "%s %d", tr("Layer"), n);
    for (used = 0, i = 0; i < d->nlay; i++) if (!strcmp(d->lay[i]->name, name)) used = 1;
    if (!used) break;
  }
  if ((l = layer_new(d, name))) doc_add_layer(d, l, d->active + 1);
}
static void transform_dialog(void)
{
  /* Free Transform as numbers: scale (per cent across and down) and turn (degrees) about the
     middle of the selection or the layer */
  Doc *d = cur_doc();
  char buf[64] = "100 100 0";
  double sx = 100, sy = 100, ang = 0, cx, cy, m[6], c, s;
  Box b;
  if (!d) return;
  if (!ask_string("Free Transform", "Width %, height %, angle:", buf, sizeof buf)) return;
  if (sscanf(buf, "%lf %lf %lf", &sx, &sy, &ang) < 1) return;
  if (sscanf(buf, "%lf %lf", &sx, &sy) < 2) sy = sx;
  if (sx < 1 || sy < 1 || sx > 10000 || sy > 10000) return;
  if (!sel_bounds(d, &b)) b = doc_box(d);
  cx = (b.x0 + b.x1) / 2.0; cy = (b.y0 + b.y1) / 2.0;
  c = cos(-ang * 3.14159265358979 / 180); s = sin(-ang * 3.14159265358979 / 180);
  /* new to old: undo the turn, then the scale, about the middle */
  m[0] = c * 100 / sx; m[1] = -s * 100 / sx; m[3] = s * 100 / sy; m[4] = c * 100 / sy;
  m[2] = cx - m[0] * cx - m[1] * cy; m[5] = cy - m[3] * cx - m[4] * cy;
  if (d->lay[d->active]->live) {
    /* 0.9: a type or shape layer takes the forward map: the inverse of this one */
    double f[6], det = m[0] * m[4] - m[1] * m[3];
    if (fabs(det) < 1e-12) return;
    f[0] = m[4] / det; f[1] = -m[1] / det; f[3] = -m[3] / det; f[4] = m[0] / det;
    f[2] = -(f[0] * m[2] + f[1] * m[5]); f[5] = -(f[3] * m[2] + f[4] * m[5]);
    doc_live_transform(d, d->active, f);
    return;
  }
  transform_layer(d, m);
}
/* ------------------------------------------------------------------ 0.9: type and shape layers, paths */
/* a live (type or shape) layer's extent, worked out if need be */
static Box live_extent(Doc *d, int i)
{
  Layer *l = d->lay[i];
  const TextParams *tp = layer_text(l);
  if (tp) return text_bounds(tp);
  if (!l->live_ok) live_render(d, l);
  return l->live_ok && l->livepx ? l->livebox : box_make(0, 0, 0, 0);
}
/* Edit > Transform's quarter turns and flips of a live layer, about its middle: how 1, 2, 3
   quarter turns clockwise, 4 flipped across, 5 down; 1 if done */
static int live_turn(Doc *d, int how)
{
  Box b = live_extent(d, d->active);
  double cx = (b.x0 + b.x1) / 2.0, cy = (b.y0 + b.y1) / 2.0, m[6];
  memset(m, 0, sizeof m);
  switch (how) {
  case 1: m[1] = -1; m[2] = cx + cy; m[3] = 1; m[5] = cy - cx; break;
  case 2: m[0] = -1; m[2] = 2 * cx; m[4] = -1; m[5] = 2 * cy; break;
  case 3: m[1] = 1; m[2] = cx - cy; m[3] = -1; m[5] = cy + cx; break;
  case 4: m[0] = -1; m[2] = 2 * cx; m[4] = 1; break;
  default: m[0] = 1; m[4] = -1; m[5] = 2 * cy; break;
  }
  if (!doc_live_transform(d, d->active, m)) { ask("OK", "%s", tr("The layer is locked.")); return 0; }
  return 1;
}
/* Layer > Shape Properties (and a double-click on a shape layer) */
static void shape_props(Doc *d)
{
  const ShapeParams *cur = layer_shape(d->lay[d->active]);
  ShapeParams sp;
  if (!cur) return;
  sp = *cur;
  if (dlg_shape(&sp) && memcmp(&sp, cur, sizeof sp)) {
    int pat_new = sp.fill_kind == SFILL_PATTERN && (cur->fill_kind != SFILL_PATTERN || !layerx_pattern_at(d->lay[d->active], 0, 0));
    if (pat_new && !tool_pattern()) { ask("OK", "%s", tr("Choose a pattern first.")); sp.fill_kind = cur->fill_kind; }
    if (!doc_set_shape(d, d->active, &sp, NULL)) ask("OK", "%s", tr("There isn't enough memory for that."));
    else if (sp.fill_kind == SFILL_PATTERN && !doc_set_shape_fill(d, d->active, SFILL_PATTERN, NULL, 0, 0, sp.grad_scale, 0, pat_new ? tool_pattern() : NULL))   /* (r3: its pattern) */
      ask("OK", "%s", tr("There isn't enough memory for that."));
  }
}
/* Latin-1 typed in a dialog to UTF-8 */
static void latin1_utf8(const char *s, char *u, size_t cap)
{
  size_t o = 0;
  for (; *s && o + 3 < cap; s++) {
    UBYTE c = (UBYTE)*s;
    if (c < 0x80) u[o++] = (char)c; else { u[o++] = (char)(0xC0 | (c >> 6)); u[o++] = (char)(0x80 | (c & 63)); }
  }
  u[o] = 0;
}
/* the Paths panel's double-click: the Work Path saved under a name, or a saved path renamed */
static void path_rename(Doc *d)
{
  char name[40], u[80];
  int k;
  if (d->path_active < 0 && d->workpath) {
    sprintf(name, "%s %d", tr("Path"), d->npaths + 1);
    if (!ask_string("Save Path", "Name:", name, sizeof name) || !name[0]) return;
    latin1_utf8(name, u, sizeof u);
    if ((k = doc_path_save_work(d, u)) >= 0) d->path_active = k + 1;
  } else if (d->path_active > 0 && d->path_active <= d->npaths) {
    k = d->path_active - 1;
    utf8_to_latin1(d->path_name[k], (u32)strlen(d->path_name[k]), name, sizeof name);
    if (!ask_string("Rename Path", "Name:", name, sizeof name) || !name[0]) return;
    latin1_utf8(name, u, sizeof u);
    doc_path_rename(d, k, u);
  }
}
/* the path the Paths panel's buttons and the path tools' options work on; NULL (said) if none */
static Path *path_for_command(Doc *d)
{
  Path *p = active_path(d);
  if (!p || !p->na) { ask("OK", "%s", tr("Choose a path in the Paths panel first (or draw one with the Pen tool).")); return NULL; }
  return p;
}
/* the commands that leave the text being typed alone (the rest end the typing first) */
static int type_keeps(ULONG id)
{
  static const ULONG keep[] = { ID_TEXTCOLOR, ID_TYPECHAR, ID_TYPEPARA, ID_WARPTEXT, ID_TYPEHORIZ, ID_TYPEVERT, ID_ZOOMIN, ID_ZOOMOUT, ID_FIT,
    ID_ACTUAL, ID_ZOOM200, ID_ZOOM50, ID_AUTOSAVE, ID_POINTER, ID_ZOOMED, ID_COLORCLICK, ID_PICKED, ID_COLORCHG, ID_SWAPCOLORS,
    ID_DEFCOLORS, ID_TOGGLEPANELS, ID_TOGGLERIGHT, ID_STROKECOLOR, ID_PANELTAB, ID_CANVASCHG, ID_PATHSCHG, 0 };
  int i;
  for (i = 0; keep[i]; i++) if (keep[i] == id) return 1;
  return 0;
}
static void command(ULONG id)
{
  Doc *d = cur_doc();
  Box ch;
  if (id >= ID_CTX_LAYER0 && id < ID_CTX_LAST) {     /* 1.1: the right button's menus' own commands (ctxmui.c) */
    int sw; ULONG then;
    if (!ctx_command(id, &sw, &then)) return;
    if (sw >= 0 && sw != curdoc) doc_switch(sw);
    if (then) command(then);
    return;
  }
  if (mx_recording) mx_record(id, menu_label(id));   /* 1.2: into the action being recorded (the History Log is t2_log_steps') */
  if (id > ID_T2_FIRST && id < ID_T2_LAST) { if (tools2_command(id)) refresh_all(); return; }   /* 1.2 (tools2.c) */
  if ((id >= ID_ADJ_BC && id <= ID_ADJ_EQUALIZE) || (id >= ID_ADJ_REPLACE && id <= ID_ADJ_MATCH)) { if (d) dlg_adjust(id); refresh_all(); return; }
  if (id == ID_F_OIL || id == ID_F_LENSCORR) { r3_command(d, id); refresh_all(); return; }               /* (r3: their new dialogs) */
  if (id >= ID_F_GAUSS && id < ID_F_LAST) { if (d) dlg_filter(id); refresh_all(); return; }
  if (id == ID_F_AGAIN) { if (d) dlg_filter_again(); refresh_all(); return; }
  if ((id >= ID_FX_GALLERY && id < ID_FX_LAST) || (id >= ID_GX0 && id <= ID_GX_LAST)) { if (d) dlgx_command(id); refresh_all(); return; }
  if (id >= ID_LS_FIRST && id <= ID_LS_LAST) { if (d) layerstyle_command(d, id); refresh_all(); return; }   /* 1.1 (fxdlg.c) */
  if (id >= ID_CU_FIRST && id <= ID_CU_LAST) { colorui_command(d, id); return; }   /* 1.2 (colorui.c) */
  if (id >= ID_MX_FIRST && id <= ID_MX_LAST && mx_command(d, id)) { refresh_all(); return; }               /* 1.2 (menux.c) */
  if (id >= ID_R3_FIRST && id <= ID_R3_LAST) { r3_command(d, id); refresh_all(); return; }                  /* r3-image (r3image.c) */
  if (id >= ID_RECENT0 && id < ID_RECENT0 + NRECENT) {
    char p[512];
    strcpy(p, recent[id - ID_RECENT0]);
    if (p[0]) open_path(p);
    return;
  }
  if (id >= ID_NEWADJ0 && id < ID_NEWADJ0 + AK_N) {
    /* a new adjustment layer above the active one, then its settings */
    AdjParams p;
    int kind = (int)(id - ID_NEWADJ0);
    char nm[64];
    if (!d) return;
    adj_defaults(kind, &p);
    sprintf(nm, "%s 1", adj_names[kind]);
    if (doc_new_adjust(d, &p, nm) >= 0 && kind != AK_INVERT) { refresh_all(); dlg_adjust(adj_menu_id(kind)); }
    refresh_all();
    return;
  }
  switch (id) {
  case ID_NEW: new_doc(); return;
  case ID_ICONPREF: pref_icons = !pref_icons; update_menus(); return;
  case ID_PASTEAMIGA: paste_amiga(d); refresh_all(); return;
  case ID_OPEN: open_file(); return;
  case ID_PLACE: place_file(); return;
  case ID_CLOSE: doc_close(curdoc); return;
  case ID_CLOSEALL: close_all(); return;
  case ID_SAVE: save(); return;
  case ID_SAVEAS: save_as(); return;
  case ID_EXPPNG: export_as(FMT_PNG, 0); return;
  case ID_EXPJPEG: export_as(FMT_JPEG, 0); return;
  case ID_EXPBMP: export_as(FMT_BMP, 0); return;
  case ID_EXPILBM: export_as(FMT_ILBM, 0); return;
  case ID_EXPILBM8: export_as(FMT_ILBM, 1); return;
  case ID_EXPGIF: export_as(FMT_GIF, 0); return;
  case ID_EXPTIFF: {
    /* 1.2: the compression asked */
    static const int tc[4] = { TIFFC_LZW, TIFFC_DEFLATE, TIFFC_PACKBITS, TIFFC_NONE };
    int r = ask("LZW|Deflate (ZIP)|PackBits|None|Cancel", "%s", tr("How should the TIFF file be compressed?"));
    if (!r) return;
    export_tiff_comp = tc[r - 1];
    export_as(FMT_TIFF, 0);
    export_tiff_comp = TIFFC_LZW;
    return; }
  case ID_EXPTGA: export_as(FMT_TGA, 0); return;
  case ID_EXPPSD: export_as(FMT_PSD, 0); return;
  case ID_EXPXW0: case ID_EXPXW0 + 1: case ID_EXPXW0 + 2: case ID_EXPXW0 + 3: case ID_EXPXW0 + 4: case ID_EXPXW0 + 5: case ID_EXPXW0 + 6: case ID_EXPXW0 + 7:
  case ID_EXPXW0 + 8: case ID_EXPXW0 + 9: {
    static const int fm[10] = { FMT_WEBP, FMT_PDF_OUT, FMT_QOI, FMT_PNM, FMT_ICO, FMT_DDS, FMT_DPX, FMT_FITS, FMT_CINEON_OUT, FMT_HDR_OUT };
    export_x(fm[id - ID_EXPXW0]);
    return;
  }
  case ID_EXPANIMGIF: export_anim(1); return;
  case ID_EXPANIM: export_anim(0); return;
  case ID_QUICKPNG: quick_png(); return;
  case ID_EXPSVG: export_svg(); return;
  case ID_IMPFRAMES: import_frames(); return;
  case ID_CLOSEOTHERS: close_others(); return;
  case ID_FILEINFO: if (d) dlg_file_info(d); return;
  case ID_EXPILBMOPT: export_ilbm_options(); return;
  case ID_SAVECOPY: save_copy(); return;
  case ID_EXPORTAS: export_scaled(); return;
  case ID_LAYERSTOFILES: layers_to_files(); return;
  case ID_STACK: load_stack(); return;
  case ID_CLEARRECENT: memset(recent, 0, sizeof recent); update_menus(); return;
  case ID_WIN_PALETTE: case ID_COLORTABLE: palwin_open(1); return;
  case ID_WIN_BRUSHSET: { Object *w = brushwin_settings_window(); if (w) set(w, MUIA_Window_Open, TRUE); return; }
  case ID_WIN_DPAINT: { Object *w = brushwin_dpaint_window(); if (w) set(w, MUIA_Window_Open, TRUE); return; }
  case ID_GRADEDIT: {
    /* the Gradient Editor, starting from the gradient chosen; what it makes becomes "Custom" */
    Gradient g = *tool_gradient();
    if (gradwin_edit(&g, gradwin_presets(), d ? d->fg : 0, d ? d->bg : 0xFFFFFF)) { cur_grad = g; names_refresh(); opt.grad_preset = ngrad_names - 1; }
    names_refresh();
    DoMethod(optbar, MUIM_Opts_Refresh);
    return; }
  case ID_REVERT: {
    DocView *v = cur_view();
    int fmt;
    Doc *n;
    if (!v || !v->path[0]) return;
    if (v->doc->dirty && ask("Revert|Cancel", "%s", tr("Go back to the picture as it was last saved? The changes are lost.")) != 1) return;
    if ((n = load(v->path, &fmt))) {
      DoMethod(canvas, MUIM_Canvas_Cancel); palwin_stop(); palwin_set_doc(NULL);
      doc_free(v->doc); v->doc = n; v->placed = 0;
      snapshot_free(v->snap); v->snap = snapshot_take(n);
      refresh_all();
    }
    return; }
  case ID_ABOUT:
    ask("OK", "%s", tr("\33cEasel 1.0\n\nA picture editor for the Amiga, laid out as PhotoCraft is.\n\n"
        "Live layers (groups, masks, adjustment and fill layers), palettes,\nindexed pictures and colour cycling, the Amiga's screen modes,\n"
        "brushes, gradients and patterns, selections and transforms,\neditable type, paths and shapes, retouching tools, adjustments and filters.\n"
        "Reads and writes PNG, JPEG, BMP, GIF, TIFF, Targa, Photoshop PSD,\nIFF ILBM and OpenRaster (.ora), and reads many more.\n\n"
        "Written in C for MUI; drawn with its own icons.\n\n"
        "AVIF decoding: dav1d, by VideoLAN and the dav1d authors.\n"
        "Picture readers and brush code ported from PhotoCraft,\nby the ArtCraft Team and the PhotoCraft contributors.\n"
        "Fonts: Liberation, Carlito and Caladea.\nSee Licenses.txt for their licences."));
    return;
  case ID_SCR_WINDOW: screen_switch(SM_WINDOW); save_prefs(); return;
  case ID_SCR_OWN: screen_switch(SM_SCREEN); save_prefs(); return;
  case ID_SCR_FULL: screen_switch(SM_FULL); save_prefs(); return;
  case ID_SCR_CHOOSE: screen_choose(); save_prefs(); return;
  case ID_DARKUI:
    pref_dark = !pref_dark;
    win_reopen();
    return;
  case ID_KEYS: dlg_keys(); memset(key_tool, 0, sizeof key_tool); return;
  case ID_PREFS: {
    int reopen = 0;
    if (dlg_preferences(&reopen)) {
      if (reopen) win_reopen();
      save_prefs();
      DoMethod(canvas, MUIM_Canvas_Refresh);
      refresh_panels();
      DoMethod(optbar, MUIM_Opts_Refresh);
    }
    return; }
  case ID_AUTOSAVE: job_begin(NULL, "Autosave"); autosave(0); job_end(); return;
  case ID_AUTOSAVESET: {
    char buf[16];
    sprintf(buf, "%d", pref_autosave);
    if (!ask_string("Autosave", "Minutes between autosaves (0 for none):", buf, sizeof buf)) return;
    pref_autosave = atoi(buf) < 0 ? 0 : atoi(buf) > 120 ? 120 : atoi(buf);
    return; }
  case ID_TOGGLEPANELS: case ID_TOGGLERIGHT: {
    /* Tab: the toolbox, options bar and panels hidden or shown again; Shift-Tab just the panels */
    static int hidden;
    hidden = !hidden;
    set(rightcol, MUIA_ShowMe, !hidden);
    if (id == ID_TOGGLEPANELS) { set(toolbox, MUIA_ShowMe, !hidden); set(optbar, MUIA_ShowMe, !hidden); }
    else { set(toolbox, MUIA_ShowMe, TRUE); set(optbar, MUIA_ShowMe, TRUE); }
    return; }
  case ID_NEXTDOC: if (ndocs > 1) doc_switch((curdoc + 1) % ndocs); return;
  case ID_PREVDOC: if (ndocs > 1) doc_switch((curdoc + ndocs - 1) % ndocs); return;
  case ID_SWAPCOLORS: if (d) { u32 t = d->fg; d->fg = d->bg; d->bg = t; refresh_panels(); } return;
  case ID_DEFCOLORS: if (d) { d->fg = 0; d->bg = 0xFFFFFF; refresh_panels(); } return;
  case ID_COMMIT: DoMethod(canvas, MUIM_Canvas_Commit); return;
  case ID_CROPSWAP: opt.crop_swap = !opt.crop_swap; DoMethod(optbar, MUIM_Opts_Refresh); return;
  case ID_CANCEL: DoMethod(canvas, MUIM_Canvas_Cancel); return;
  case ID_ZOOMIN: DoMethod(canvas, MUIM_Canvas_Zoom, 0, 1); refresh_panels(); return;
  case ID_ZOOMOUT: DoMethod(canvas, MUIM_Canvas_Zoom, 0, -1); refresh_panels(); return;
  case ID_FIT: DoMethod(canvas, MUIM_Canvas_Zoom, 0, 2); refresh_panels(); return;
  case ID_FILLSCREEN: DoMethod(canvas, MUIM_Canvas_Zoom, 0, 3); refresh_panels(); return;   /* (1.1) */
  case ID_ACTUAL: DoMethod(canvas, MUIM_Canvas_Zoom, 1000, 0); refresh_panels(); return;
  case ID_ZOOM200: DoMethod(canvas, MUIM_Canvas_Zoom, 2000, 0); refresh_panels(); return;
  case ID_ZOOM50: DoMethod(canvas, MUIM_Canvas_Zoom, 500, 0); refresh_panels(); return;
  }
  if (!d) return;
  switch (id) {
  case ID_UNDO: case ID_STEPBACK: doc_undo(d); break;
  case ID_FADE: dlg_fade(); break;
  case ID_TOGGLELAST:
    /* back one step, or forward again if that's what was done last */
    if (d->redo && toggled == d) { doc_redo(d); toggled = NULL; } else if (doc_undo(d)) toggled = d;
    break;
  case ID_REDO: case ID_STEPFWD: doc_redo(d); break;
  case ID_PURGE: doc_forget_undo(d); break;
  /* 1.1: Edit > Purge > Clipboard (Easel's own) and All (every picture's history and the clipboard) */
  case ID_PURGECLIP: clip_free(clip); clip = NULL; break;
  case ID_PURGEALL: {
    int k;
    if (!ask("Purge|Cancel", "%s", tr("Forget the undo history of every open picture, and what Easel has copied? This can't be undone."))) break;
    for (k = 0; k < ndocs; k++) doc_forget_undo(docs[k].doc);
    clip_free(clip); clip = NULL;
    break;
  }
  /* 0.4: modes, the palette, the Amiga screen mode */
  case ID_MODE_RGB: palwin_stop(); if (d->mode != MODE_RGB) doc_to_rgb(d); else if (d->cmode != CM_RGB) colorx_convert_model(d, CM_RGB, NULL); break;
  case ID_MODE_GRAY: palwin_stop(); if (d->mode != MODE_GRAY && !doc_to_gray(d)) ask("OK", "%s", tr("There isn't enough memory for that.")); break;
  case ID_MODE_INDEXED: {
    IndexedOpts o;
    palwin_stop();
    modedlg_indexed_defaults(&o);
    if (modedlg_indexed(d, &o) && !modedlg_apply_indexed(d, &o)) ask("OK", "%s", tr("There isn't enough memory for that."));
    break; }
  case ID_MODE_BITMAP: {
    int m = 0;
    palwin_stop();
    if (modedlg_bitmap(&m) && !doc_to_bitmap(d, m)) ask("OK", "%s", tr("There isn't enough memory for that."));
    break; }
  case ID_SCREENMODE: {
    ScreenModeChoice c;
    memset(&c, 0, sizeof c);
    c.modeid = d->modeid; c.mode = -1;
    if (modedlg_screenmode(d, &c)) modedlg_apply_screenmode(d, &c);
    break; }
  /* 0.4: rulers, grid and guides */
  case ID_RULERS: d->guides.show_rulers = !d->guides.show_rulers; DoMethod(canvas, MUIM_Canvas_Refresh); break;
  case ID_SHOWGRID: d->guides.show_grid = !d->guides.show_grid; break;
  case ID_SHOWEDGES: extras_hidden = !extras_hidden; DoMethod(canvas, MUIM_Canvas_Refresh); break;   /* (1.1: Ctrl-H, View > Extras) */
  case ID_SHOWGUIDES: d->guides.show_guides = !d->guides.show_guides; break;
  case ID_SNAP: d->guides.snap = d->guides.snap ? 0 : SNAP_GUIDES | SNAP_GRID | SNAP_EDGES | SNAP_CENTER; break;
  case ID_SNAPTO0: case ID_SNAPTO0 + 1: case ID_SNAPTO0 + 2: case ID_SNAPTO0 + 3: case ID_SNAPTO0 + 4: {
    static const u8 bits[5] = { SNAP_GUIDES, SNAP_GRID, SNAP_EDGES, SNAP_CENTER, SNAP_PIXEL };
    d->guides.snap ^= bits[id - ID_SNAPTO0];
    break;
  }
  case ID_LOCKGUIDES: d->guides.lock_guides = !d->guides.lock_guides; break;
  case ID_GRIDSET: {
    char buf[64];
    int sx, sy, sub = 1;
    sprintf(buf, "%d %d %d", grid_spacing_x(d), grid_spacing_y(d), d->guides.grid_sub > 1 ? d->guides.grid_sub : 1);
    if (!ask_string("Grid", "Spacing across, down (pixels), subdivisions:", buf, sizeof buf)) return;
    if (sscanf(buf, "%d %d %d", &sx, &sy, &sub) < 2) { if (sscanf(buf, "%d", &sx) != 1) return; sy = sx; }
    grid_set(d, sx, sy, d->guides.grid_ox, d->guides.grid_oy, sub);
    d->guides.show_grid = 1;
    break; }
  case ID_NEWGUIDE: {
    char buf[64] = "V 0";
    char o = 'V';
    int pos = 0;
    if (!ask_string("New Guide", "V (vertical) or H (horizontal), and its position in pixels:", buf, sizeof buf)) return;
    if (sscanf(buf, " %c %d", &o, &pos) != 2) return;
    guide_add(d, o == 'v' || o == 'V', pos);
    d->guides.show_guides = 1;
    break; }
  case ID_CLEARGUIDES: guide_clear(d); break;
  /* 0.5: brushes, patterns, the history brush */
  case ID_DEFBRUSH: brushwin_define(d); break;
  case ID_PICKUPBRUSH: brushui_request(BWR_PICKUP_SEL); break;
  case ID_DEFPATTERN: {
    Box b = d->sel ? d->selbox : doc_box(d);
    char name[64];
    Pattern *p;
    sprintf(name, "%s %d", tr("Pattern"), patterns.n + 1);
    if (!ask_string("Pattern Name", "Name:", name, sizeof name)) return;
    if (box_empty(b) || b.x1 - b.x0 > 1024 || b.y1 - b.y0 > 1024) { ask("OK", "%s", tr("A pattern is made from a selection up to 1024 pixels across.")); return; }
    if (!(p = pattern_from_doc(d, b, 1)) || (strcpy(p->name, name), !patlist_add(&patterns, p))) { ask("OK", "%s", tr("There isn't enough memory for that.")); return; }
    names_refresh();
    opt.pattern = patterns.n - 1;
    DoMethod(optbar, MUIM_Opts_Refresh);
    return; }
  case ID_FILLPATTERN: {
    const Pattern *p = tool_pattern();
    if (p) fill_pattern(d, p, 0, 0, 100, BM_NORMAL);
    break; }
  case ID_SNAPSHOT: {
    DocView *v = cur_view();
    Snapshot *s = snapshot_take(d);
    if (!s) { ask("OK", "%s", tr("There isn't enough memory for a snapshot.")); return; }
    snapshot_free(v->snap); v->snap = s;
    return; }
  case ID_CUT: case ID_COPY: case ID_COPYMERGED: {
    Clip *c = clip_copy(d, id == ID_COPYMERGED);
    if (!c) { ask("OK", "%s", tr("There is nothing to copy: the selection is empty.")); return; }
    clip_free(clip); clip = c;
    if (id == ID_CUT) clip_cut(d);
    {
      /* and on the Amiga clipboard, for other programs */
      Rgba r;
      r.w = c->w; r.h = c->h; r.px = c->px;
      clipboard_put_ilbm(&r);
    }
    break; }
  case ID_PASTE: case ID_PASTEINPLACE:
    if (clip) clip_paste(d, clip, id == ID_PASTEINPLACE);
    else paste_amiga(d);
    break;
  case ID_CLEAR: ch = clear_selection(d); if (box_empty(ch) && d->lay[d->active]->locked) ask("OK", "%s", tr("The layer is locked.")); break;
  case ID_FILL: dlg_fill(); break;
  case ID_FILLFG: fill_selection(d, d->fg, 100, BM_NORMAL); break;
  case ID_FILLBG: fill_selection(d, d->bg, 100, BM_NORMAL); break;
  case ID_STROKE: dlg_stroke(); break;
  /* 0.7: Free Transform on the canvas (its handles' mode from Edit > Transform), the numbers as before */
  case ID_FREETRANSFORM: case ID_XF_SCALE: case ID_XF_ROTATE: case ID_XF_SKEW: case ID_XF_DISTORT: case ID_XF_PERSPECTIVE:
    DoMethod(canvas, MUIM_Canvas_Transform, id == ID_XF_SCALE ? XM_SCALE : id == ID_XF_ROTATE ? XM_ROTATE : id == ID_XF_SKEW ? XM_SKEW :
             id == ID_XF_DISTORT ? XM_DISTORT : id == ID_XF_PERSPECTIVE ? XM_PERSPECTIVE : XM_FREE, 0);
    return;
  case ID_XFSEL:
    if (!d->sel) { ask("OK", "%s", tr("Make a selection first: Transform Selection changes its outline.")); return; }
    DoMethod(canvas, MUIM_Canvas_Transform, XM_FREE, 1);
    return;
  case ID_NUMTRANSFORM: transform_dialog(); break;
  case ID_XF_AGAIN:
    if (box_empty(xform_again(d, d->active, RS_BICUBIC)) && !progress_aborted) ask("OK", "%s", tr("There is no transform to do again, or the layer can't be changed."));
    break;
  /* (exact, and only the selected part when there is a selection) */
  case ID_ROTATE180: if (d->lay[d->active]->live) live_turn(d, 2); else xform_rotate90(d, d->active, 2); break;
  case ID_ROTATE90R: if (d->lay[d->active]->live) live_turn(d, 1); else xform_rotate90(d, d->active, 1); break;
  case ID_ROTATE90L: if (d->lay[d->active]->live) live_turn(d, 3); else xform_rotate90(d, d->active, 3); break;
  case ID_FLIPLH: if (d->lay[d->active]->live) live_turn(d, 4); else xform_flip(d, d->active, 0); break;
  case ID_FLIPLV: if (d->lay[d->active]->live) live_turn(d, 5); else xform_flip(d, d->active, 1); break;
  /* 0.9: Layer > Type, Rasterize, Shape Properties */
  case ID_EDITTYPE:
    if (!layer_text(d->lay[d->active])) return;
    if (tool != TL_TEXT && tool != TL_VTEXT) set_tool(layer_text(d->lay[d->active])->vertical ? TL_VTEXT : TL_TEXT);
    DoMethod(canvas, MUIM_Canvas_TypeCmd, TC_EDIT, 0);
    DoMethod(optbar, MUIM_Opts_Refresh);
    refresh_panels();
    return;
  case ID_TYPECHAR: DoMethod(canvas, MUIM_Canvas_TypeCmd, TC_CHARACTER, 0); refresh_panels(); return;
  case ID_TYPEPARA: DoMethod(canvas, MUIM_Canvas_TypeCmd, TC_PARAGRAPH, 0); refresh_panels(); return;
  case ID_WARPTEXT: DoMethod(canvas, MUIM_Canvas_TypeCmd, TC_WARP, 0); refresh_panels(); return;
  case ID_TYPEHORIZ: case ID_TYPEVERT: DoMethod(canvas, MUIM_Canvas_TypeCmd, TC_ORIENT, id == ID_TYPEVERT); refresh_panels(); return;
  case ID_TEXTCOLOR: {
    LONG c = colorpick_run("Color Picker (Text Color)", d->fg);
    if (c >= 0) { d->fg = (u32)c; DoMethod(canvas, MUIM_Canvas_TypeCmd, TC_COLOR, 0); }
    DoMethod(optbar, MUIM_Opts_Refresh);
    refresh_panels();
    return; }
  case ID_STROKECOLOR: {
    LONG c = colorpick_run("Color Picker (Stroke Color)", (ULONG)opt.stroke_color);
    if (c >= 0) opt.stroke_color = (int)c;
    DoMethod(optbar, MUIM_Opts_Refresh);
    return; }
  case ID_TYPEWORKPATH: {
    const TextParams *tp = layer_text(d->lay[d->active]);
    Path *p = tp ? text_to_path(tp) : NULL;
    if (!p) return;
    if (doc_path_set(d, -1, p, "Create Work Path")) d->path_active = -1;
    path_free(p);
    break; }
  /* 1.1: File > Scripts and Layer > Type > Paste Lorem Ipsum */
  case ID_DELEMPTY: if (!doc_delete_empty_layers(d)) ask("OK", "%s", tr("There are no empty layers to delete.")); break;
  case ID_FLATMASKS: if (!doc_flatten_all_masks(d)) ask("OK", "%s", tr("No layer has a mask.")); break;
  case ID_FITIMAGE: {
    char buf[32];
    long fw = 0, fh = 0;
    sprintf(buf, "%ld x %ld", (long)(d->w > 640 ? 640 : d->w), (long)(d->h > 512 ? 512 : d->h));
    if (!ask_string("Fit Image", "Fit inside (width x height):", buf, sizeof buf)) break;
    if (sscanf(buf, "%ld %*[xX*] %ld", &fw, &fh) != 2 || fw < 1 || fh < 1 || fw > MAXDIM || fh > MAXDIM) { ask("OK", "%s", tr("Give the size as width x height, for example 640 x 512.")); break; }
    if (!doc_fit_image(d, (i32)fw, (i32)fh, 0) && (fw < d->w || fh < d->h)) ask("OK", "%s", tr("There isn't enough memory for that."));
    break;
  }
  case ID_TOPARA: case ID_TOPOINT: {
    const TextParams *cur = layer_text(d->lay[d->active]);
    TextParams *tp = cur ? (TextParams *)malloc(sizeof *tp) : NULL;
    if (!cur) { ask("OK", "%s", tr("Choose a type layer first.")); break; }
    if (!tp) break;
    *tp = *cur;
    if (id == ID_TOPARA ? text_to_paragraph(tp) : text_to_point(tp)) doc_set_text(d, d->active, tp);
    free(tp);
    break;
  }
  case ID_LOREM: {
    char *lo = lorem_ipsum(60);
    const TextParams *cur = layer_text(d->lay[d->active]);
    TextParams *tp = (TextParams *)malloc(sizeof *tp);
    if (!lo || !tp) { free(lo); free(tp); break; }
    if (cur) {
      /* the active type layer: the text added at its end */
      *tp = *cur;
      if (text_insert(tp, (int)strlen(tp->text), " ", 1) && text_insert(tp, (int)strlen(tp->text), lo, (int)strlen(lo))) doc_set_text(d, d->active, tp);
    } else {
      /* a new paragraph of it in a box over the middle of the picture */
      text_params_init(tp, font_name(opt.text_font), opt.text_size, d->fg, d->w / 10, d->h / 10);
      tp->box = 1; tp->bw = d->w * 0.8; tp->bh = d->h * 0.8;
      text_set_string(tp, lo);
      doc_new_text(d, tp, "Lorem Ipsum");
    }
    free(lo); free(tp);
    break;
  }
  case ID_RASTERTYPE: case ID_RASTERTYPE2: if (d->lay[d->active]->kind == LK_TEXT) doc_rasterize(d, d->active); break;
  case ID_RASTERSHAPE: if (d->lay[d->active]->kind == LK_SHAPE) doc_rasterize(d, d->active); break;
  case ID_SHAPEPROPS: shape_props(d); break;
  /* 0.9: the Paths panel */
  case ID_PATHSCHG: DoMethod(canvas, MUIM_Canvas_Paths); DoMethod(pathpanel, MUIM_Paths_Refresh); return;
  case ID_PATHFILL: {
    Path *p = path_for_command(d);
    if (p && !layer_paintable(d, d->active)) ask("OK", "%s", tr("The layer can't be painted: choose a pixel layer that isn't locked or hidden."));
    else if (p) r3_fill_path(d, p);                      /* (r3: the colour, a pattern or the history) */
    break; }
  case ID_PATHSTROKE: {
    Path *p = path_for_command(d);
    Brush b = tool_brush(TL_BRUSH);
    if (p && box_empty(path_stroke(d, p, &b, d->fg, 0)) && !progress_aborted) ask("OK", "%s", tr("The layer can't be painted: choose a pixel layer that isn't locked or hidden."));
    break; }
  case ID_PATHMKSEL: { Path *p = path_for_command(d); if (p) path_to_selection(d, p, SEL_REPLACE, 0, 1); break; }
  case ID_PATHFROMSEL: {
    Path *p;
    if (!d->sel) { ask("OK", "%s", tr("Make a selection first: its outline becomes the Work Path.")); return; }
    if (!(p = path_from_selection(d, 2.0))) { if (!progress_aborted) ask("OK", "%s", tr("Nothing is selected.")); return; }
    if (doc_path_set(d, -1, p, "Make Work Path")) d->path_active = -1;
    path_free(p);
    break; }
  case ID_PATHNEW: { int k = doc_path_new(d, NULL); if (k >= 0) d->path_active = k + 1; break; }
  case ID_PATHDUP: if (d->path_active > 0) { int k = doc_path_duplicate(d, d->path_active - 1); if (k >= 0) d->path_active = k + 1; } break;
  case ID_PATHDEL:
    if (!d->path_active) return;
    doc_path_delete(d, d->path_active < 0 ? -1 : d->path_active - 1);
    d->path_active = 0;
    break;
  case ID_PATHRENAME: path_rename(d); break;
  /* 0.7: Layer > Matting */
  case ID_DEFRINGE: {
    static LONG width = 1;
    if (!ask_number("Defringe", "Width (px):", &width, 1, 200)) return;
    if (box_empty(layer_defringe(d, (int)width))) ask("OK", "%s", tr("Nothing was changed: the layer has no transparent edge, or it is locked."));
    break; }
  case ID_BLACKMATTE: case ID_WHITEMATTE:
    if (box_empty(id == ID_BLACKMATTE ? layer_remove_black_matte(d) : layer_remove_white_matte(d))) ask("OK", "%s", tr("Nothing was changed: the layer has no partly transparent edge, or it is locked."));
    break;
  /* 0.7: the Ruler's Straighten (the active layer, or with the tick the whole picture, cropped) */
  case ID_STRAIGHTEN: {
    int ok;
    if (!ruler_on) { ask("OK", "%s", tr("Drag the Ruler along something that should be level (or upright) first.")); return; }
    ok = opt.ruler_image ? image_straighten(d, ruler_xy[0], ruler_xy[1], ruler_xy[2], ruler_xy[3])
                         : layer_straighten(d, ruler_xy[0], ruler_xy[1], ruler_xy[2], ruler_xy[3], RS_BICUBIC);
    if (!ok) { if (!progress_aborted) ask("OK", "%s", tr("The line is level already, too short, or the layer can't be changed.")); return; }
    ruler_on = 0; ruler_info[0] = 0;
    DoMethod(optbar, MUIM_Opts_Refresh);
    break; }
  /* 0.8: the History panel's snapshots */
  case ID_HISTSNAP: {
    DocView *v = cur_view();
    Doc *c;
    static int nsnap;
    if (!v) return;
    if (v->nhsnap >= MAXHSNAP) { ask("OK", "%s", tr("There can be eight snapshots: delete one first.")); return; }
    if (!(c = state_copy(v->doc))) { ask("OK", "%s", tr("There isn't enough memory for a snapshot.")); return; }
    v->hsnap[v->nhsnap] = c;
    sprintf(v->hsnap_name[v->nhsnap], "%s %d", tr("Snapshot"), ++nsnap);
    if (hist_opts.ask_snapshot_name) ask_string("New Snapshot", "Name:", v->hsnap_name[v->nhsnap], sizeof v->hsnap_name[0]);   /* (1.2) */
    hist_pick = v->nhsnap++;
    refresh_panels();
    return; }
  case ID_HISTSNAPDEL: {
    DocView *v = cur_view();
    int k = hist_pick;
    if (v && (k < 0 || k >= v->nhsnap)) {
      /* 1.1: no snapshot picked: the step in effect deleted, with the ones after it */
      char msg[160];
      if (!doc_can_undo(v->doc)) return;
      snprintf(msg, sizeof msg, tr("Delete the step \"%s\" and the steps after it?"), doc_undo_name(v->doc));
      if (ask("Delete|Cancel", "%s", msg) != 1) return;
      if (!doc_delete_state(v->doc, -1)) ask("OK", "%s", tr("That step can't be deleted."));
      refresh_all();
      return;
    }
    if (!v || k < 0 || k >= v->nhsnap) return;
    doc_free(v->hsnap[k]);
    memmove(&v->hsnap[k], &v->hsnap[k + 1], (size_t)(v->nhsnap - k - 1) * sizeof v->hsnap[0]);
    memmove(v->hsnap_name[k], v->hsnap_name[k + 1], (size_t)(v->nhsnap - k - 1) * sizeof v->hsnap_name[0]);
    v->nhsnap--;
    hist_pick = -1;
    refresh_panels();
    return; }
  case ID_HISTSNAPGO: {
    DocView *v = cur_view();
    if (!v || hist_pick < 0 || hist_pick >= v->nhsnap) return;
    DoMethod(canvas, MUIM_Canvas_Cancel);
    if (!state_restore(v->doc, v->hsnap[hist_pick], v->hsnap_name[hist_pick])) ask("OK", "%s", tr("There isn't enough memory for that."));
    refresh_all();
    return; }
  case ID_HISTBRUSHSRC: {
    /* 1.1: the History Brush's source: the History panel's snapshot (its layer in use) */
    DocView *v = cur_view();
    Snapshot *sn;
    if (!v) return;
    if (hist_pick < 0 || hist_pick >= v->nhsnap) { ask("OK", "%s", tr("Click a snapshot in the History panel first (or make one with its camera button).")); return; }
    if (v->hsnap[hist_pick]->w != v->doc->w || v->hsnap[hist_pick]->h != v->doc->h) { ask("OK", "%s", tr("That snapshot is another size from the picture now, so the History Brush can't paint from it.")); return; }
    if (!(sn = snapshot_take(v->hsnap[hist_pick]))) { ask("OK", "%s", tr("There isn't enough memory for that.")); return; }
    snapshot_free(v->snap); v->snap = sn;
    return; }
  case ID_HISTNEWDOC: {
    DocView *v = cur_view();
    Doc *c;
    if (!v) return;
    if (!(c = state_copy(v->doc))) { ask("OK", "%s", tr("There isn't enough memory for that.")); return; }
    c->dirty = 1;
    doc_show(c, "", FMT_ORA);
    return; }
  case ID_SAMPCLEAR: { DocView *v = cur_view(); v->nsamp = 0; DoMethod(canvas, MUIM_Canvas_Refresh); refresh_panels(); return; }
  case ID_RULERCLEAR: ruler_on = 0; ruler_info[0] = 0; DoMethod(optbar, MUIM_Opts_Refresh); DoMethod(canvas, MUIM_Canvas_Refresh); return;
  /* 0.7: channels and Quick Mask */
  case ID_QUICKMASK:
    if (quickmask_active(d)) quickmask_leave(d);
    else if (!quickmask_enter(d)) ask("OK", "%s", tr("There isn't enough memory for that."));
    break;
  case ID_SAVESEL: dlg_save_selection(); break;
  case ID_LOADSEL: dlg_load_selection(); break;
  case ID_CHANLOAD: if (!quickmask_active(d)) chan_load_selection(d, chan_pick, chan_op, 0); break;
  case ID_CHANDEL: if (chan_pick >= 0 && chan_pick < d->nchan) { chan_delete(d, chan_pick); if (chan_pick >= d->nchan) chan_pick = d->nchan - 1; } break;
  case ID_CHANRENAME: {
    char buf[40], u[64];
    size_t i, o = 0;
    if (chan_pick < 0 || chan_pick >= d->nchan) return;
    utf8_to_latin1(d->chan_name[chan_pick], strlen(d->chan_name[chan_pick]), buf, sizeof buf);
    if (!ask_string("Channel Name", "Name:", buf, 32) || !buf[0]) return;
    for (i = 0; buf[i] && o + 4 < 32; i++) o += (size_t)utf8_put(u + o, (UBYTE)buf[i]);
    u[o] = 0;
    chan_rename(d, chan_pick, u);
    break; }
  case ID_AUTOTONE: adj_auto_tone(d); break;
  case ID_AUTOCONTRAST: adj_auto_contrast(d); break;
  case ID_AUTOCOLOR: adj_auto_color(d); break;
  case ID_IMAGESIZE: dlg_image_size(); break;
  case ID_CANVASSIZE: dlg_canvas_size(); break;
  case ID_IMG_R180: image_rotate(d, 2); break;
  case ID_IMG_R90R: image_rotate(d, 1); break;
  case ID_IMG_R90L: image_rotate(d, 3); break;
  case ID_IMG_RANY: dlg_rotate(); break;
  case ID_IMG_FLIPH: image_flip(d, 0); break;
  case ID_IMG_FLIPV: image_flip(d, 1); break;
  case ID_CROP: {
    Box b;
    if (crop_pending) { DoMethod(canvas, MUIM_Canvas_Commit); return; }
    if (!sel_bounds(d, &b)) { ask("OK", "%s", tr("Select the part to keep first (or use the Crop tool).")); return; }
    image_crop(d, b);
    break; }
  case ID_TRIM: image_trim(d); break;
  case ID_DUPIMAGE: {
    u8 *data;
    size_t len;
    Doc *n;
    char err[80];
    /* (through OpenRaster: every layer and its settings come across) */
    if (!(data = ora_write(d, &len))) return;
    n = codec_load_doc(data, len, err, sizeof err);
    free(data);
    if (n) {
      n->fg = d->fg; n->bg = d->bg;
      n->cmode = d->cmode;                               /* (1.2: the colour model and profile too) */
      if (d->icc && (n->icc = (u8 *)malloc(d->icclen)) != NULL) { memcpy(n->icc, d->icc, d->icclen); n->icclen = d->icclen; }
      doc_show(n, "", FMT_ORA);
    }
    return; }
  case ID_NEWLAYER: layer_new_named(d); break;
  case ID_DUPLAYER: doc_duplicate_layer(d, d->active); break;
  case ID_DELLAYER:
    if (d->nlay < 2) { ask("OK", "%s", tr("A picture needs at least one layer.")); return; }
    doc_delete_layer(d, d->active);
    break;
  case ID_LAYERPROPS:
    /* an adjustment or fill layer's own settings; otherwise its name, opacity, mode and the rest */
    if (d->lay[d->active]->kind == LK_ADJUST) dlg_adjust(adj_menu_id(d->lay[d->active]->adj.kind));
    else if (d->lay[d->active]->kind == LK_SHAPE) shape_props(d);
    else if (d->lay[d->active]->kind == LK_FILL) dlg_fill_layer(-1);
    else dlg_layer_props();
    break;
  case ID_LAYERFROMSEL: case ID_LAYERCUTSEL: {
    Clip *c = clip_copy(d, 0);
    if (!c) return;
    if (id == ID_LAYERCUTSEL) clip_cut(d);
    clip_paste(d, c, 1);
    clip_free(c);
    break; }
  case ID_RAISE: doc_arrange(d, d->active, 1); break;
  case ID_LOWER: doc_arrange(d, d->active, 2); break;
  case ID_TOFRONT: doc_arrange(d, d->active, 0); break;
  case ID_TOBACK: doc_arrange(d, d->active, 3); break;
  case ID_LOCKLAYER: d->lay[d->active]->locked = !d->lay[d->active]->locked; break;
  case ID_ALPHALOCK: d->lay[d->active]->alpha_lock = !d->lay[d->active]->alpha_lock; break;
  case ID_LOCKPOS: d->lay[d->active]->lock_pos = !d->lay[d->active]->lock_pos; break;
  case ID_LOCKALL: d->lay[d->active]->lock_all = !d->lay[d->active]->lock_all; break;
  case ID_HIDELAYER: doc_set_layer_props(d, d->active, NULL, -1, -1, !d->lay[d->active]->visible); break;
  case ID_MERGEDOWN: doc_merge_down(d, d->active); break;
  case ID_MERGEGROUP: doc_merge_group(d, d->active); break;
  case ID_MERGELAYERS: if (!doc_merge_picked(d)) ask("OK", "%s", tr("Pick two or more layers in the Layers panel first (Shift- or Ctrl-click).")); break;
  case ID_MERGEVISIBLE: doc_merge_visible(d); break;
  case ID_FLATTEN: doc_flatten(d); break;
  /* 0.3: live layers */
  case ID_NEWGROUP: doc_new_group(d, "Group 1"); break;
  case ID_GROUPLAYERS: doc_group_layers(d); break;
  case ID_UNGROUP: doc_ungroup(d, d->active); break;
  case ID_NEWFILLSOLID: dlg_fill_layer(0); break;
  case ID_NEWFILLGRAD: dlg_fill_layer(1); break;
  case ID_FILLSETTINGS:
    if (d->lay[d->active]->kind == LK_FILL) dlg_fill_layer(-1);
    else if (d->lay[d->active]->kind == LK_ADJUST) dlg_adjust(adj_menu_id(d->lay[d->active]->adj.kind));
    break;
  case ID_MASK_REVEALALL: doc_mask_add(d, d->active, MK_REVEAL_ALL); break;
  case ID_MASK_HIDEALL: doc_mask_add(d, d->active, MK_HIDE_ALL); break;
  case ID_MASK_REVEALSEL: doc_mask_add(d, d->active, MK_REVEAL_SEL); break;
  case ID_MASK_HIDESEL: doc_mask_add(d, d->active, MK_HIDE_SEL); break;
  case ID_MASK_ALPHA: doc_mask_add(d, d->active, MK_FROM_ALPHA); break;
  case ID_MASK_DELETE: doc_mask_delete(d, d->active, 0); break;
  case ID_MASK_APPLY: doc_mask_delete(d, d->active, 1); break;
  case ID_MASK_ENABLE: doc_mask_set(d, d->active, !d->lay[d->active]->mask_on, -1, -1, -1); break;
  case ID_MASK_LINK: doc_mask_set(d, d->active, -1, !d->lay[d->active]->mask_linked, -1, -1); break;
  case ID_MASK_INVERT: doc_mask_invert(d, d->active); break;
  case ID_MASK_EDIT: if (d->lay[d->active]->mask) d->lay[d->active]->edit_mask = !d->lay[d->active]->edit_mask; break;
  case ID_CLIP: doc_set_clip(d, d->active, !d->lay[d->active]->clip); break;
  case ID_RASTERIZE: doc_rasterize(d, d->active); break;
  case ID_AL_LEFT: doc_align(d, AL_LEFT); break;
  case ID_AL_HCENTER: doc_align(d, AL_HCENTER); break;
  case ID_AL_RIGHT: doc_align(d, AL_RIGHT); break;
  case ID_AL_TOP: doc_align(d, AL_TOP); break;
  case ID_AL_VCENTER: doc_align(d, AL_VCENTER); break;
  case ID_AL_BOTTOM: doc_align(d, AL_BOTTOM); break;
  case ID_DIST_H: doc_align(d, AL_DIST_H); break;
  case ID_DIST_V: doc_align(d, AL_DIST_V); break;
  case ID_PASTEINTO:
    /* the clipboard as a new layer, masked to the selection (the mask not linked, as in Photoshop) */
    if (!clip) return;
    if (!d->sel) { clip_paste(d, clip, 0); break; }
    doc_begin(d, "Paste Into");
    if (clip_paste(d, clip, 0) && doc_mask_add(d, d->active, MK_REVEAL_SEL)) d->lay[d->active]->mask_linked = 0;
    doc_end(d);
    break;
  case ID_PASTEOUTSIDE:
    /* 1.2: the same, masked to everything but the selection */
    if (!clip) return;
    if (!d->sel) { clip_paste(d, clip, 0); break; }
    doc_begin(d, "Paste Outside");
    if (clip_paste(d, clip, 0) && doc_mask_add(d, d->active, MK_HIDE_SEL)) d->lay[d->active]->mask_linked = 0;
    doc_end(d);
    break;
  case ID_SELFROMMASK: doc_sel_from_mask(d, d->active, SEL_REPLACE); break;
  case ID_RESELECT: sel_reselect(d); break;
  case ID_GROWSIM: sel_grow_similar(d, opt.tolerance); break;
  case ID_SIMILAR: sel_similar(d, opt.tolerance); break;
  case ID_SELALL: sel_all(d); break;
  case ID_DESELECT: sel_deselect(d); break;
  case ID_INVERSE: sel_invert(d); break;
  case ID_SELALPHA: sel_from_alpha(d, d->active); break;
  case ID_COLORRANGE: dlg_color_range(); break;
  case ID_FEATHER: case ID_EXPAND: case ID_CONTRACT: case ID_BORDER: case ID_SMOOTH: dlg_select(id); break;
  default: return;
  }
  refresh_all();
}

/* ------------------------------------------------------------------ the window */
static const char *const head_color[] = { "Color", "Swatches", "Brushes", "History", NULL };
static Object *toppages, *tophead, *swatchpanel, *brushpanel;
/* 0.4/0.5: what the Palette window, the Swatches and the Brushes panel tell the rest */
/* ------------------------------------------------------------------ 1.2: services for menux.c and panelsx.c */
void do_command(ULONG id) { command(id); }
const struct NewMenu *menu_table(void) { return menus; }
int mx_open(const char *path)
{
  int i, n = ndocs;
  for (i = 0; i < ndocs; i++) if (docs[i].path[0] && !Stricmp((STRPTR)docs[i].path, (STRPTR)path)) return 0;   /* (open already: left alone) */
  open_path(path);
  return ndocs > n;
}
int mx_write(Doc *d, const char *path) { return write_doc(d, path, ext_fmt(path)); }
void mx_close_current(void) { if (curdoc >= 0) { docs[curdoc].doc->dirty = 0; doc_close(curdoc); } }
int mx_file_request(int save, const char *title, char *path, int cap, int drawers) { (void)drawers; return file_request(save, title, path, (size_t)cap, NULL); }

static void pal_redraw(void)
{
  DoMethod(canvas, MUIM_Canvas_Refresh);
  if (!palwin_cycling()) { DoMethod(layerpanel, MUIM_Layers_Refresh); DoMethod(historypanel, MUIM_History_Refresh); update_menus(); }
}
static void pal_picked(int i)
{
  Doc *d = cur_doc();
  if (!d || i < 0 || i >= 256) return;
  d->fg = ((u32)d->pal[i * 3] << 16) | ((u32)d->pal[i * 3 + 1] << 8) | d->pal[i * 3 + 2];
  DoMethod(colorpanel, MUIM_Color_Refresh); DoMethod(toolbox, MUIM_Tools_Refresh);
}
static void brush_picked(const Brush *b)
{
  int t = tool;
  if (!(t == TL_BRUSH || t == TL_PENCIL || t == TL_ERASER || t == TL_CLONE || (t >= TL_BLUR && t <= TL_SPONGE) || (t >= TL_PATTERN && t <= TL_BGERASE) || t == TL_HEAL || t == TL_SPOTHEAL)) set_tool(t = TL_BRUSH);
  opt.size[t] = b->size; opt.hardness[t] = b->hardness;
  if (b->opacity) opt.opacity[t] = b->opacity;
  if (b->flow) opt.flow[t] = b->flow;
  DoMethod(optbar, MUIM_Opts_Refresh);
}
static void brush_changed(void) { DoMethod(optbar, MUIM_Opts_Refresh); DoMethod(canvas, MUIM_Canvas_Refresh); }
static const char *const head_layers[] = { "Layers", "Channels|Chan.", "Paths", "Navigator|Nav.", "Info", NULL };
static Object *lowpages, *lowhead;
static Object *flat(int horiz) { return NewObject(flatscroll_class()->mcc_Class, NULL, MUIA_FlatScroll_Horiz, horiz, TAG_DONE); }
static Object *head(const char *const *labels) { return NewObject(head_class()->mcc_Class, NULL, MUIA_Head_Labels, (ULONG)labels, TAG_DONE); }
static int win_make(void)
{
  Object *strip = MUI_MakeObject(MUIO_MenustripNM, (ULONG)menus_translated(), 0);
  /* the whole screen below its title bar, as PhotoCraft fills its screen (0.8: or all of Easel's own
     screen, without borders, in Full Screen) */
  struct Screen *scr = LockPubScreen((STRPTR)(pub_screen[0] ? pub_screen : NULL));
  LONG sw = 800, sh = 580, top = 12;
  int full = full_screen();
  if (scr) { sw = scr->Width; top = full ? 0 : scr->BarHeight + 1; sh = scr->Height - top; UnlockPubScreen(NULL, scr); }
  win = QWindowObject,
    (ULONG)(full ? MUIA_Window_Borderless : TAG_IGNORE), TRUE, (ULONG)(full ? MUIA_Window_Backdrop : TAG_IGNORE), TRUE,
    (ULONG)(full ? MUIA_Window_DragBar : TAG_IGNORE), FALSE, (ULONG)(full ? MUIA_Window_DepthGadget : TAG_IGNORE), FALSE,
    (ULONG)(full ? MUIA_Window_SizeGadget : TAG_IGNORE), FALSE, (ULONG)(full ? MUIA_Window_CloseGadget : TAG_IGNORE), FALSE,
    MUIA_Window_Title, (ULONG)"Easel",
    (ULONG)(own_scr ? TAG_IGNORE : MUIA_Window_ID), MAKE_ID('E','A','S','L'),
    MUIA_Window_DisableKeys, MUIKEYF_WINDOW_CLOSE,
    MUIA_Window_AppWindow, TRUE,
    MUIA_Window_Menustrip, (ULONG)strip,
    MUIA_Window_Width, sw,
    MUIA_Window_Height, sh,
    MUIA_Window_LeftEdge, 0,
    MUIA_Window_TopEdge, top,
    WindowContents, VGroup, InnerSpacing(0, 0), MUIA_Group_Spacing, 0,
      Child, optbar = NewObject(optbar_class()->mcc_Class, NULL, MUIA_ShortHelp, (ULONG)" ", TAG_DONE),
      Child, HGroup, MUIA_Group_Spacing, 0,
        Child, toolbox = NewObject(toolbox_class()->mcc_Class, NULL, MUIA_ShortHelp, (ULONG)" ", TAG_DONE),
        Child, VGroup, MUIA_Group_Spacing, 0,
          Child, doctabs = NewObject(doctabs_class()->mcc_Class, NULL, MUIA_ShortHelp, (ULONG)" ", TAG_DONE),
          Child, HGroup, MUIA_Group_Spacing, 0,
            Child, canvas = NewObject(canvas_class()->mcc_Class, NULL, MUIA_FillArea, FALSE, MUIA_ContextMenu, (ULONG)mx_context_menu(), TAG_DONE),
            Child, vscroll = flat(0),
          End,
          Child, hscroll = flat(1),
        End,
        Child, rightcol = VGroup, MUIA_Group_Spacing, 0, MUIA_HorizWeight, 0,
          Child, tophead = head(head_color),
          Child, toppages = VGroup, MUIA_Group_PageMode, TRUE, InnerSpacing(0, 0), MUIA_VertWeight, 0,
            Child, colorpanel = NewObject(color_class()->mcc_Class, NULL, MUIA_ShortHelp, (ULONG)" ", TAG_DONE),
            Child, swatchpanel = swatchwin_panel(),
            Child, brushpanel = brushwin_panel(),
            Child, historypanel = NewObject(history_class()->mcc_Class, NULL, MUIA_ShortHelp, (ULONG)" ", TAG_DONE),
          End,
          Child, lowhead = head(head_layers),
          Child, lowpages = VGroup, MUIA_Group_PageMode, TRUE, InnerSpacing(0, 0), MUIA_VertWeight, 300,
            Child, layerpanel = NewObject(layers_class()->mcc_Class, NULL, MUIA_VertWeight, 300, MUIA_ShortHelp, (ULONG)" ", TAG_DONE),
            Child, chanpanel = NewObject(channels_class()->mcc_Class, NULL, MUIA_ShortHelp, (ULONG)" ", TAG_DONE),
            Child, pathpanel = NewObject(paths_class()->mcc_Class, NULL, MUIA_ShortHelp, (ULONG)" ", TAG_DONE),
            Child, navpanel = NewObject(navigator_class()->mcc_Class, NULL, TAG_DONE),
            Child, infopanel = NewObject(info_class()->mcc_Class, NULL, TAG_DONE),
          End,
        End,
      End,
      Child, statusbar = NewObject(status_class()->mcc_Class, NULL, TAG_DONE),
    End,
  End;
  if (!win) { if (strip) MUI_DisposeObject(strip); return 0; }
  DoMethod(app, OM_ADDMEMBER, (ULONG)win);
  SetAttrs(canvas, MUIA_Canvas_VProp, (ULONG)vscroll, MUIA_Canvas_HProp, (ULONG)hscroll, TAG_DONE);
  DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)app, 2, MUIM_Application_ReturnID, ID_QUIT);
  DoMethod(vscroll, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, (ULONG)canvas, 3, MUIM_Set, MUIA_Canvas_Top, MUIV_TriggerValue);
  DoMethod(hscroll, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, (ULONG)canvas, 3, MUIM_Set, MUIA_Canvas_Left, MUIV_TriggerValue);
  DoMethod(canvas, MUIM_Notify, MUIA_Canvas_Changed, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_CANVASCHG);
  DoMethod(canvas, MUIM_Notify, MUIA_Canvas_Pointer, MUIV_EveryTime, (ULONG)statusbar, 1, MUIM_Status_Refresh);
  DoMethod(canvas, MUIM_Notify, MUIA_Canvas_Pointer, MUIV_EveryTime, (ULONG)infopanel, 2, MUIM_Info_Refresh, 0);
  DoMethod(lowhead, MUIM_Notify, MUIA_Head_Active, MUIV_EveryTime, (ULONG)lowpages, 3, MUIM_Set, MUIA_Group_ActivePage, MUIV_TriggerValue);
  DoMethod(canvas, MUIM_Notify, MUIA_Canvas_Picked, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_PICKED);
  DoMethod(canvas, MUIM_Notify, MUIA_Canvas_Zoomed, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_ZOOMED);
  DoMethod(toolbox, MUIM_Notify, MUIA_Tools_Pick, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_TOOLPICK);
  DoMethod(toolbox, MUIM_Notify, MUIA_Tools_Color, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_COLORCLICK);
  DoMethod(colorpanel, MUIM_Notify, MUIA_Color_Changed, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_COLORCHG);
  DoMethod(layerpanel, MUIM_Notify, MUIA_Layers_Changed, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_LAYERSCHG);
  DoMethod(historypanel, MUIM_Notify, MUIA_History_Changed, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_HISTCHG);
  DoMethod(doctabs, MUIM_Notify, MUIA_DocTabs_Pick, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DOCPICK);
  DoMethod(doctabs, MUIM_Notify, MUIA_DocTabs_Close, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_DOCCLOSE);
  DoMethod(tophead, MUIM_Notify, MUIA_Head_Active, MUIV_EveryTime, (ULONG)toppages, 3, MUIM_Set, MUIA_Group_ActivePage, MUIV_TriggerValue);
  set(win, MUIA_Window_Open, TRUE);
  if (!xget(win, MUIA_Window_Open)) return 0;
  if (full) ShowTitle(own_scr, FALSE);    /* (0.8: again, now the backdrop window is there) */
  if (swatchpanel) DoMethod(swatchpanel, MUIM_Notify, MUIA_Swatches_Changed, MUIV_EveryTime, (ULONG)app, 2, MUIM_Application_ReturnID, ID_COLORCHG);
  {
    struct Window *w = (struct Window *)xget(win, MUIA_Window_Window);
    if (w) LOG("screen %ld x %ld, window %ld,%ld %ld x %ld", (long)w->WScreen->Width, (long)w->WScreen->Height, (long)w->LeftEdge, (long)w->TopEdge, (long)w->Width, (long)w->Height);
  }
  return 1;
}

/* ------------------------------------------------------------------ starting up */
static BPTR home_lock, home_old;
static void scan_step(int done, int total)
{
  static char what[80];
  snprintf(what, sizeof what, tr("Looking for installed fonts (%d of %d)"), done, total);
  splash_step(10 + (total ? 30 * done / total : 0), what);
}
static void wb_tooltypes(char *screen, int size)
{
  struct Library *IconBase;
  struct WBArg *wa = &_WBenchMsg->sm_ArgList[0];
  struct DiskObject *dob;
  BPTR old;
  STRPTR v;
  if (!(IconBase = OpenLibrary((STRPTR)"icon.library", 37))) return;
  old = CurrentDir(wa->wa_Lock);
  if ((dob = GetDiskObject((STRPTR)wa->wa_Name))) {
    if ((v = FindToolType((STRPTR *)dob->do_ToolTypes, (STRPTR)"PUBSCREEN")) && !screen[0]) { strncpy(screen, (char *)v, size - 1); screen[size - 1] = 0; }
    FreeDiskObject(dob);
  }
  CurrentDir(old);
  CloseLibrary(IconBase);
}
static int home_has_fonts(BPTR dir)
{
  BPTR old = CurrentDir(dir), l = Lock((STRPTR)"Fonts/QuillSans.font", ACCESS_READ);
  CurrentDir(old);
  if (l) UnLock(l);
  return l != 0;
}
/* PROGDIR: made right if a shell started Easel without it (it holds Fonts and TrueType) */
static void find_home(const char *argv0)
{
  char name[256];
  const char *tries[2];
  int i;
  struct Process *me = (struct Process *)FindTask(NULL);
  APTR pw = me->pr_WindowPtr;
  BPTR l;
  me->pr_WindowPtr = (APTR)-1;
  l = GetProgramDir() ? Lock((STRPTR)"PROGDIR:Fonts/QuillSans.font", ACCESS_READ) : 0;
  if (l) { UnLock(l); me->pr_WindowPtr = pw; return; }
  tries[0] = argv0;
  tries[1] = GetProgramName((STRPTR)name, sizeof name) ? name : NULL;
  for (i = 0; i < 2 && !home_lock; i++) {
    BPTR f, d;
    if (!tries[i] || !tries[i][0] || !(f = Lock((STRPTR)tries[i], ACCESS_READ))) continue;
    d = ParentDir(f);
    UnLock(f);
    if (d && home_has_fonts(d)) home_lock = d; else if (d) UnLock(d);
  }
  if (home_lock) home_old = SetProgramDir(home_lock);
  me->pr_WindowPtr = pw;
}
static void free_home(void)
{
  if (!home_lock) return;
  SetProgramDir(home_old);
  UnLock(home_lock);
  home_lock = 0;
}
static void add_fonts(void)
{
  BPTR l = Lock((STRPTR)"PROGDIR:Fonts", ACCESS_READ), sys;
  struct Process *me = (struct Process *)FindTask(NULL);
  APTR pw = me->pr_WindowPtr;
  if (!l) return;
  me->pr_WindowPtr = (APTR)-1;
  if ((sys = Lock((STRPTR)"FONTS:", ACCESS_READ))) {
    LONG same = SameLock(l, sys);
    UnLock(sys);
    if (same == LOCK_SAME) { UnLock(l); me->pr_WindowPtr = pw; return; }
  }
  me->pr_WindowPtr = pw;
  if (AssignAdd((STRPTR)"FONTS", l)) fonts_assigned = 1; else UnLock(l);
}
static void remove_fonts(void)
{
  BPTR l;
  if (!fonts_assigned) return;
  if ((l = Lock((STRPTR)"PROGDIR:Fonts", ACCESS_READ))) { RemAssignList((STRPTR)"FONTS", l); UnLock(l); }
  fonts_assigned = 0;
}
static void load_prefs(void)
{
  FILE *f = fopen("PROGDIR:Easel.prefs", "r");
  char line[600];
  int nr = 0;
  if (!f) return;
  while (fgets(line, sizeof line, f)) {
    line[strcspn(line, "\r\n")] = 0;
    if (!strncmp(line, "dark=", 5)) pref_dark = atoi(line + 5);
    else if (!strncmp(line, "autosave=", 9)) pref_autosave = atoi(line + 9);
    else if (!strncmp(line, "icons=", 6)) pref_icons = atoi(line + 6);
    else if (!strncmp(line, "screen=", 7)) { strncpy(pub_screen, line + 7, sizeof pub_screen - 1); }
    else if (!strncmp(line, "dir=", 4)) { strncpy(lastdir, line + 4, sizeof lastdir - 1); }
    else if (!strncmp(line, "recent=", 7) && nr < NRECENT) { strncpy(recent[nr], line + 7, sizeof recent[0] - 1); nr++; }
    else prefs_line(line);
  }
  fclose(f);
}
static void save_prefs(void)
{
  FILE *f = fopen("PROGDIR:Easel.prefs", "w");
  if (!f) return;
  fprintf(f, "dark=%d\n", pref_dark);
  fprintf(f, "autosave=%d\n", pref_autosave);
  fprintf(f, "icons=%d\n", pref_icons);
  if (user_screen[0]) fprintf(f, "screen=%s\n", user_screen);     /* (0.8: not Easel's own screen) */
  if (lastdir[0]) fprintf(f, "dir=%s\n", lastdir);
  { int i; for (i = 0; i < NRECENT; i++) if (recent[i][0]) fprintf(f, "recent=%s\n", recent[i]); }
  prefs_write(f);                        /* 0.8: the Preferences window's settings */
  fclose(f);
}

int main(int argc, char **argv)
{
  ULONG sigs = 0;
  int rc = 20, i;
  char start[8][512];
  int nstart = 0;
  char arg_screen[64] = "";
  ULONG start_cmd = 0;                   /* CMD=n: a menu command run once the pictures are open (for testing) */
  if (!(IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39)) ||
      !(GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39)) ||
      !(LayersBase = OpenLibrary((STRPTR)"layers.library", 39)) ||
      !(UtilityBase = OpenLibrary((STRPTR)"utility.library", 39)) ||
      !(KeymapBase = OpenLibrary((STRPTR)"keymap.library", 37))) goto done;
  if (!(MUIMasterBase = OpenLibrary((STRPTR)MUIMASTER_NAME, 19))) { printf("Easel needs MUI (muimaster.library 19 or later).\n"); goto done; }
  CyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library", 40);
  adjlayer_init();                       /* adjustment layers composite through adjust.c */
  layerx_init(); cafill_init();          /* 1.2: vector masks, patterns, group effects; Spot Healing's Content-Aware type */
  mixer_init(&tool_mixer);
  find_home(argc > 0 ? argv[0] : NULL);
  for (i = 1; i < argc; i++) {
    if (!strncmp(argv[i], "DEBUG=", 6)) qlog = fopen(argv[i] + 6, "w");
    else if (!Strnicmp((STRPTR)argv[i], (STRPTR)"PUBSCREEN=", 10)) { strncpy(arg_screen, argv[i] + 10, sizeof arg_screen - 1); }
    else if (!Strnicmp((STRPTR)argv[i], (STRPTR)"CMD=", 4)) start_cmd = (ULONG)atol(argv[i] + 4);
    else if (nstart < 8) { strncpy(start[nstart], argv[i], 511); start[nstart++][511] = 0; }
  }
  if (argc == 0 && _WBenchMsg) {
    for (i = 1; i < _WBenchMsg->sm_NumArgs && nstart < 8; i++) {
      struct WBArg *wa = &_WBenchMsg->sm_ArgList[i];
      if (NameFromLock(wa->wa_Lock, (STRPTR)start[nstart], 512) && AddPart((STRPTR)start[nstart], wa->wa_Name, 512)) nstart++;
    }
    wb_tooltypes(arg_screen, sizeof arg_screen);
  }
  prefs_defaults();
  load_prefs();
  prefs_apply();
  theme_set_large(prefs.large_text);
  if (arg_screen[0]) strcpy(pub_screen, arg_screen);
  if (pub_screen[0]) {
    struct Screen *sc = LockPubScreen((STRPTR)pub_screen);
    if (sc) UnlockPubScreen(NULL, sc); else pub_screen[0] = 0;
  }
  strcpy(user_screen, pub_screen);
  /* 0.8: Easel's own screen, when the preferences ask for it (not when PUBSCREEN= names one) */
  if (prefs.screen_mode != SM_WINDOW && !arg_screen[0]) { if (own_open()) strcpy(pub_screen, OWN_NAME); else prefs.screen_mode = SM_WINDOW; }
  else if (arg_screen[0]) prefs.screen_mode = SM_WINDOW;
  LOG("Easel starting, cybergraphics %p", (void *)CyberGfxBase);
  lang_set(prefs.language >= 0 && prefs.language < UI_NLANG ? prefs.language : lang_system());   /* (1.1) */
  theme_set_dark(pref_dark);
  opts_defaults();
  splash_open(pub_screen, "Easel 1.0");
  splash_step(3, tr("Adding Easel's fonts drawer"));
  DiskfontBase = OpenLibrary((STRPTR)"diskfont.library", 39);
  add_fonts();
  splash_step(6, tr("Reading the TrueType fonts"));
  fonts_init(&fonts, "PROGDIR:TrueType");
  splash_step(10, tr("Looking for installed fonts"));
  sysfonts_scan(&fonts, scan_step);
  /* 0.9: type layers find their faces among these, and Amiga bitmap fonts in FONTS: */
  text_fonts = &fonts;
  bitmap_fonts_scan();
  text_amifont_lookup = ami_lookup;
  splash_step(45, tr("Preparing"));
  if (!canvas_class() || !toolbox_class() || !optbar_class() || !color_class() || !layers_class() || !history_class() || !head_class() ||
      !doctabs_class() || !status_class() || !flatscroll_class() || !qbutton_class() || !navigator_class() || !info_class() || !channels_class() || !paths_class()) { printf("Easel: MUI couldn't make its classes\n"); goto done; }
  /* 0.5: brushes, swatches, patterns, gradients (before the window, which shows their panels) */
  swatch_palette_hooks();
  brushwin_init();
  brushui.picked = brush_picked; brushui.changed = brush_changed; brushui.request = brushui_request;
  swatchwin_init();
  patlist_init(&patterns);
  patlist_builtin(&patterns);
  gradient_init(&cur_grad, "Custom", 0x000000, 0xFFFFFF);
  names_refresh();
  splash_step(60, tr("Building the window"));
  app = ApplicationObject,
    MUIA_Application_Title, (ULONG)"Easel",
    MUIA_Application_Version, (ULONG)verstag,
    MUIA_Application_Description, (ULONG)"A picture editor",
    MUIA_Application_Base, (ULONG)"EASEL",
  End;
  if (!app) { printf("Easel: the application could not be made (MUI classes missing?)\n"); goto done; }
  splash_step(75, tr("Opening the window"));
  keys_init(menus);                      /* 0.8: the shortcuts in Easel.keys, before the menus are made */
  mx_init();                             /* 1.2: the saved actions */
  if (!win_make()) { printf("Easel: the window could not be made\n"); goto done; }
  palwin_create(app);
  appdrop_attach(win, app, open_path);
  palwin_redraw = pal_redraw; palwin_picked = pal_picked;
  for (i = 0; i < nstart; i++) {
    static char what[300];
    snprintf(what, sizeof what, tr("Opening %s"), (const char *)FilePart((STRPTR)start[i]));
    splash_front();
    splash_step(85, what);
    open_path(start[i]);
  }
  splash_close();
  recovery_offer();
  for (i = 0; i < font_count(); i++) if (!strcmp(font_name(i), "Liberation Sans")) opt.text_font = i;
  set_tool(TL_BRUSH);
  refresh_all();
  jobs_window = 1;
  if (start_cmd) DoMethod(app, MUIM_Application_ReturnID, start_cmd);   /* (through the loop: as a job) */
  if (!theme_fonts_ok()) ask("OK", "%s", tr("Easel couldn't open its own screen fonts, so text is drawn in the\nsystem font. Keep the Fonts drawer next to Easel."));
  for (;;) {
    ULONG id = DoMethod(app, MUIM_Application_NewInput, (ULONG)&sigs);
    if (id) LOG("id %lu", (unsigned long)id);
    if (id == MUIV_Application_ReturnID_Quit || id == ID_QUIT) { if (close_all()) break; }
    else if (id == ID_OPTCHG) DoMethod(canvas, MUIM_Canvas_TypeCmd, TC_OPTS, 0);    /* 0.9: the Type tool takes the options bar's change */
    else if (id == ID_PATHSCHG) { DoMethod(canvas, MUIM_Canvas_Paths); DoMethod(pathpanel, MUIM_Paths_Refresh); }   /* (no job: a double-click's second click isn't lost) */
    else if (id == ID_CANVASCHG || id == ID_LAYERSCHG || id == ID_HISTCHG || id == ID_CHANSCHG) {
      /* 0.9: the text being typed: kept when the Layers panel changes something, forgotten when
         the History panel has gone back over it */
      if (type_editing && id != ID_CANVASCHG) DoMethod(canvas, MUIM_Canvas_TypeCmd, TC_COMMIT, id == ID_HISTCHG ? -1 : 0);
      if (id != ID_CANVASCHG) DoMethod(canvas, MUIM_Canvas_Refresh);
      refresh_panels();
    }
    else if (id == ID_PICKED || id == ID_COLORCHG) { DoMethod(colorpanel, MUIM_Color_Refresh); DoMethod(toolbox, MUIM_Tools_Refresh); if (TYPE_TOOL(tool)) DoMethod(optbar, MUIM_Opts_Refresh); }
    else if (id == ID_ZOOMED) { DoMethod(doctabs, MUIM_DocTabs_Refresh); DoMethod(statusbar, MUIM_Status_Refresh); DoMethod(navpanel, MUIM_Navigator_Refresh, 0); }
    else if (id == ID_TOOLPICK) set_tool((int)xget(toolbox, MUIA_Tools_Pick));
    else if (id == ID_COLORCLICK) {
      Doc *d = cur_doc();
      int which = (int)xget(toolbox, MUIA_Tools_Color);
      LONG c;
      if (d && (c = colorpick_run(which == 1 ? "Color Picker (Foreground Color)" : "Color Picker (Background Color)", which == 1 ? d->fg : d->bg)) >= 0) {
        if (which == 1) d->fg = (u32)c; else d->bg = (u32)c;
      }
      refresh_panels();
    }
    else if (id == ID_DOCPICK) doc_switch((int)xget(doctabs, MUIA_DocTabs_Pick));
    else if (id == ID_DOCCLOSE) doc_close((int)xget(doctabs, MUIA_DocTabs_Close));
    else if (id) {
      /* (0.8: as a job, so a long operation shows its progress and can be aborted) */
      if (type_editing && !type_keeps(id)) DoMethod(canvas, MUIM_Canvas_TypeCmd, TC_COMMIT, 0);   /* (0.9) */
      set(app, MUIA_Application_Sleep, TRUE);
      job_begin(cur_doc(), menu_label(id));
      command(id);
      if (job_end()) refresh_all();
      set(app, MUIA_Application_Sleep, FALSE);
    }
    if (sigs) {
      sigs = Wait(sigs | SIGBREAKF_CTRL_C);
      if (sigs & SIGBREAKF_CTRL_C) break;
    }
  }
  save_prefs();
  rc = 0;
done:
  palwin_stop();
  for (i = 0; i < ndocs; i++) { hsnaps_free(&docs[i]); snapshot_free(docs[i].snap); doc_free(docs[i].doc); }
  ndocs = 0; curdoc = -1;
  palwin_dispose();
  clip_free(clip); clip = NULL;
  if (app) MUI_DisposeObject(app);
  mx_free();                             /* 1.2: the actions and styles saved; the canvas's context menu */
  if (own_scr) { strcpy(pub_screen, user_screen); splash_close(); own_close(); }   /* 0.8: Easel's own screen */
  appdrop_free();
  brushwin_free(); brushwin_classes_free();
  swatchwin_free(); swatchwin_class_free();
  gradwin_free();
  colorpick_free();
  patlist_free(&patterns);
  pens_free();
  canvas_class_free();
  toolbox_free();
  panels_free();
  panels2_free();
  paths_free();
  jobs_free();
  tools2_free();                         /* 1.2: the tablet's handler, the smart objects' scratch copies */
  colorui_free();                                        /* 1.2 */
  popup_free();
  qbutton_class_free();
  text_cache_flush();                    /* 0.9: the type layers' open font files */
  text_amifont_lookup = NULL; ami_free();
  text_fonts = NULL;
  fonts_free(&fonts);
  theme_free_fonts();
  remove_fonts();
  free_home();
  splash_close();
  if (qlog) { fclose(qlog); qlog = NULL; }
  if (CyberGfxBase) CloseLibrary(CyberGfxBase);
  if (DiskfontBase) CloseLibrary(DiskfontBase);
  if (MUIMasterBase) CloseLibrary(MUIMasterBase);
  if (KeymapBase) CloseLibrary(KeymapBase);
  if (UtilityBase) CloseLibrary(UtilityBase);
  if (LayersBase) CloseLibrary(LayersBase);
  if (GfxBase) CloseLibrary((struct Library *)GfxBase);
  if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
  (void)win_box;
  return rc;
}
