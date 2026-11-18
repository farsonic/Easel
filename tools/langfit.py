#!/usr/bin/env python3
# langfit.py - checks that Easel's interface still fits a 640x480 screen in each language: the
# widths of the translations of lang/strings.txt measured with the fonts Easel draws in.
#
#   python3 tools/langfit.py [--font DIR] [--all]
#
# DIR is a Fonts drawer holding QuillSans/11, the font the options bar, toolbox and panels are drawn
# in (Easel's package has it; Quill's checkout has it in fonts-amiga/). Without one the check says so
# and passes. Menus, requesters and dialogs use the screen's and MUI's fonts, measured here as
# topaz 8 (8 pixels a letter, the widest usual case).
#
# It reports, for every language, where a translation makes something wider than the screen that
# the English keeps inside it (exit 1 if anything does; --all also lists the English overflows):
#   - the options bar of each tool (amiga/toolbox.c's fields_for, laid out as ob_layout does);
#   - each menu with its widest submenu beside it;
#   - every row's longest line (dialog texts, requesters, labels).
import os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mklang

ROOT = mklang.ROOT
LANGS = ['en', 'de', 'fr', 'es', 'it']
SCREEN = 640
INNER = SCREEN - 12          # a window's borders
TIGHT = 40                   # how far the options bar's last control may run past the edge

def load_font(path):
    """an Amiga bitmap font file (as tools/mkamigafonts.py writes them) as {char: advance}, default"""
    b = open(path, 'rb').read()
    code = b[32:]
    tf = 58
    ysize, style, flags, xsize = struct.unpack('>HBBH', code[tf + 20:tf + 26])
    lo, hi = code[tf + 32], code[tf + 33]
    space, kern = struct.unpack('>I', code[tf + 44:tf + 48])[0], struct.unpack('>I', code[tf + 48:tf + 52])[0]
    n = hi - lo + 2
    adv = {}
    for i in range(n):
        s = struct.unpack('>h', code[space + 2 * i:space + 2 * i + 2])[0] if space else xsize
        k = struct.unpack('>h', code[kern + 2 * i:kern + 2 * i + 2])[0] if kern else 0
        adv[lo + i] = s + k
    return adv, adv[lo + n - 1]

class Font:
    def __init__(self, adv=None, default=8):
        self.adv, self.default = adv or {}, default
    def width(self, t):
        return sum(self.adv.get(c, self.default) for c in t.encode('latin-1', 'replace'))

def find_font(arg):
    for d in ([arg] if arg else []) + [os.environ.get('EASEL_FONTS', ''), os.path.join(ROOT, 'Fonts'),
                                       os.path.join(ROOT, '..', '..', 'quill', 'fonts-amiga'),
                                       os.path.join(ROOT, '..', 'quill', 'fonts-amiga'), '/home/user/quill/fonts-amiga']:
        if d and os.path.exists(os.path.join(d, 'QuillSans', '11')): return os.path.join(d, 'QuillSans', '11')
    return None

def rows():
    items, bad = mklang.read_rows()
    return {v[0]: v for k, n, v in items if k == 'row'}

def tr(table, en, k):
    v = table.get(en)
    return v[k] if v else en

# ---------------------------------------------------------------- the options bar
def c_arrays(src):
    """static arrays of string literals in a C file: name -> [strings]"""
    out = {}
    for m in re.finditer(r'(?:static\s+)?const\s+char\s*\*\s*(?:const\s+)?(\w+)\s*\[[^]]*\]\s*=\s*\{([^}]*)\}', src):
        out[m.group(1)] = [mklang.unescape(s) for s in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2))]
    return out

def optbar_fields(src):
    """each tool case of fields_for as a list of (kind, label, choices-array name)"""
    body = src[src.index('static int fields_for'):]
    body = body[:body.index('\n}\n')]
    out = {}
    for m in re.finditer(r'case (TL_\w+(?::\s*case TL_\w+)*):(.*?)(?=\n  case TL_|\n  default:|\n  }\n)', body, re.S):
        tools = re.findall(r'TL_\w+', m.group(1))
        f = []
        for c in re.finditer(r'\b(SCRUB|CHOICE|CHECK|BUTTON|TEXT|SELOPS|SWATCH|ICONBTN|MENU)\((.*?)\);', m.group(2)):
            kind, args = c.group(1), c.group(2)
            lab = re.match(r'\s*"((?:[^"\\]|\\.)*)"', args)
            label = mklang.unescape(lab.group(1)) if lab else ''
            arr = None
            if kind in ('CHOICE', 'MENU'):
                parts = [p.strip() for p in args.split(',')]
                arr = parts[2] if kind == 'CHOICE' and len(parts) > 2 else parts[1] if len(parts) > 1 else None
            f.append((kind, label, arr))
        for t in tools: out[t] = f
    return out

def optbar_width(fields, arrays, table, k, font):
    x = 44
    last_control = x
    for kind, label, arr in fields:
        lab = tr(table, label, k) if label else ''
        lw = font.width(lab) + 5 if lab else 0
        if kind == 'SCRUB': vw = font.width('2500px') + 12
        elif kind == 'CHOICE':
            ch = arrays.get(arr, [])
            ew = max([font.width(c) for c in ch] + [40])
            vw = min(max([font.width(tr(table, c, k)) for c in ch] + [40]), ew + 8, 160) + 26
        elif kind == 'CHECK': lw, vw = 0, 16 + font.width(lab)
        elif kind == 'SELOPS': lw, vw = 0, 88
        elif kind == 'BUTTON': lw, vw = 0, font.width(lab) + 20
        elif kind == 'MENU': lw, vw = 0, font.width(lab) + 30
        elif kind == 'TEXT': lw, vw = 0, font.width(lab)
        elif kind == 'SWATCH': lw, vw = 0, 24 + lw
        else: lw, vw = 0, 22
        x1 = x + lw + vw
        if kind != 'TEXT': last_control = x1
        x = x1 + (18 if kind == 'SELOPS' else 6 if kind == 'ICONBTN' else 12)
    return last_control

# ---------------------------------------------------------------- the menus
def menus(src):
    """main.c's menus as [(title, [(item, [subitems])])]"""
    body = src[src.index('static struct NewMenu menus[]'):]
    body = body[:body.index('NM_END')]
    out = []
    for m in re.finditer(r'\b(M|I|H|S|T|LC|ST)\("((?:[^"\\]|\\.)*)"|\{ NM_SUB, \(STRPTR\)"((?:[^"\\]|\\.)*)"', body):
        kind, lab = (m.group(1), m.group(2)) if m.group(1) else ('S', m.group(3))
        lab = mklang.unescape(lab)
        if kind == 'M': out.append((lab, []))
        elif kind in ('I', 'H', 'T'): out[-1][1].append((lab, []))
        elif out and out[-1][1]: out[-1][1][-1][1].append(lab)
    return out

HELP_ARRAYS = re.compile(r'^(b|bb|bh|lb|swb|tool_help)$')
def bubble_only():
    """the English of help bubbles (theme_bubble wraps them at 46 letters): the help arrays of the
    panels and the toolbox, and the options bar's help (toolbox.c's return "...")"""
    out = set()
    for f in os.listdir(os.path.join(ROOT, 'amiga')):
        if not f.endswith('.c'): continue
        src = open(os.path.join(ROOT, 'amiga', f), encoding='latin-1').read()
        for m in re.finditer(r'const\s+char\s*\*\s*(?:const\s+)?(\w+)\s*\[[^]]*\]\s*=\s*\{(.*?)\};', src, re.S):
            if HELP_ARRAYS.match(m.group(1)):
                for t in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2)): out.add(mklang.unescape(t))
        if f == 'toolbox.c':
            # the options bar's help table { "Label:", "help" } and its hints (TEXT("..."): the bar
            # ends where the window does, so a long hint is cut, not a window made too wide)
            for m in re.finditer(r'\{\s*"(?:[^"\\]|\\.)*",\s*"((?:[^"\\]|\\.)*)"\s*\}', src): out.add(mklang.unescape(m.group(1)))
            for m in re.finditer(r'\bTEXT\("((?:[^"\\]|\\.)*)"', src): out.add(mklang.unescape(m.group(1)))
        for m in re.finditer(r'(?:return|theme_bubble\([^,]*,)\s*(?:\(ULONG\))?\s*(?:tr\()?"((?:[^"\\]|\\.)*)"', src):
            out.add(mklang.unescape(m.group(1)))
    return out

def main():
    allx = '--all' in sys.argv
    farg = sys.argv[sys.argv.index('--font') + 1] if '--font' in sys.argv else None
    fp = find_font(farg)
    if not fp:
        print('langfit: no QuillSans/11 font found (give --font DIR or EASEL_FONTS): not checked'); return 0
    adv, dflt = load_font(fp)
    sans, topaz = Font(adv, dflt), Font({}, 8)
    table = rows()
    problems, notes = [], []
    tsrc = open(os.path.join(ROOT, 'amiga', 'toolbox.c'), encoding='latin-1').read()
    arrays = c_arrays(tsrc)
    for tool, fields in sorted(optbar_fields(tsrc).items()):
        w = [optbar_width(fields, arrays, table, k, sans) for k in range(5)]
        for k in range(1, 5):
            # (the bar ends at the window's edge: a last control up to TIGHT pixels past it is cut, not lost)
            if w[k] > INNER + TIGHT and w[0] <= INNER: problems.append('options bar %s: %s %d px (English %d)' % (tool, LANGS[k], w[k], w[0]))
            elif w[k] > INNER and w[0] <= INNER and allx: notes.append('options bar %s: %s %d px, its last control cut (English %d)' % (tool, LANGS[k], w[k], w[0]))
        if w[0] > INNER and allx: notes.append('options bar %s: English %d px already' % (tool, w[0]))
    msrc = open(os.path.join(ROOT, 'amiga', 'main.c'), encoding='latin-1').read()
    for title, items in menus(msrc):
        for k in range(5):
            iw = max([topaz.width(tr(table, it, k)) for it, subs in items] + [0]) + 40
            sw = max([topaz.width(tr(table, s, k)) for it, subs in items for s in subs] + [0]) + 24
            if k == 0: w0 = iw + sw
            elif iw + sw > SCREEN and w0 <= SCREEN: problems.append('menu %s: %s %d px with its widest submenu (English %d)' % (title, LANGS[k], iw + sw, w0))
    help_only = bubble_only()
    for en, v in table.items():
        if en in help_only: continue
        lines = [max(topaz.width(l) for l in t.split('\n')) for t in v]
        for k in range(1, 5):
            if lines[k] > INNER and lines[0] <= INNER:
                problems.append('row "%s": %s %d px wide in topaz 8 (English %d)' % (en[:50].replace('\n', ' '), LANGS[k], lines[k], lines[0]))
        if lines[0] > INNER and allx: notes.append('row "%s": English %d px' % (en[:50].replace('\n', ' '), lines[0]))
    for p in problems: print(p)
    for n in notes: print('(' + n + ')')
    print('langfit: %d places a translation makes wider than %d px (font %s)' % (len(problems), INNER, os.path.relpath(fp, ROOT)))
    return 1 if problems else 0

if __name__ == '__main__':
    sys.exit(main())
