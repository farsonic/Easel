#!/usr/bin/env python3
# langcheck.py - checks that every interface string in Easel's Amiga source has its translations,
# and that every row of lang/strings.txt is still used.
#
#   python3 tools/langcheck.py            list what is missing (exit 1 if anything is)
#   python3 tools/langcheck.py --rows     the same, printed as rows to paste into lang/strings.txt
#                                         (English in each column: translate the four after it)
#   python3 tools/langcheck.py --unused   only list the rows of strings.txt the source no longer has
#
# Rows the source no longer has fail the check too (take them out of strings.txt).
#
# Every string literal in amiga/*.c and amiga/*.h (adjacent literals joined, as C does; not convert.c,
# EaselConvert's Shell command, which speaks English), and the undo steps' names the core gives
# doc_begin() (the History panel and Edit > Undo show them through tr()) and the names in the core's
# lists that the interface shows (CORE_LISTS: filters, presets, layer effects ...), must be one of:
#   - a row of lang/strings.txt (the English column), so tr() finds its translations;
#   - in lang/ignore.txt (words that are not the interface's: file names, format codes, font names,
#     log lines ...);
#   - plainly not words (see not_words below: icon drawings, numbers, format specifiers alone,
#     ALL-CAPS names such as function names, file and device names, the arguments of printf, strcmp
#     and other calls that never show their text, gadget sizing samples (MUIA_FixWidthTxt ...)).
# It also checks each row's translations keep the English's printf conversions (%s %d ...) in order,
# its '|' requester buttons, no translation that is another row's English (tr() twice), its leading MUI escape (\33c) and its '_' shortcut marker, and it
# flags a literal handed straight to a gadget (Label1("..."), QButton("..."), MUIA_ShortHelp ...)
# without tr() (unless its line says "lang: as is"). After adding strings, run this, translate, then tools/mklang.py.
import glob, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mklang

ROOT = mklang.ROOT
IGNORE = os.path.join(ROOT, 'lang', 'ignore.txt')

# calls whose string arguments are never shown as words
QUIET_CALLS = {'LOG', 'FTRACE', 'snprintf_log', 'printf', 'fprintf', 'sscanf', 'strcmp', 'strncmp', 'strstr', 'strchr', 'strrchr', 'strcspn', 'strspn',
               'Stricmp', 'Strnicmp', 'stricmp', 'strnicmp', 'fopen', 'Open', 'Lock', 'OpenLibrary', 'OpenDevice', 'FindPort',
               'MAKE_ID', 'qlogf', 'logf', 'dbg', 'OpenDiskFont', 'GetVar', 'SetVar', 'DeleteFile', 'Rename', 'CreateDir',
               'MatchPattern', 'ParsePattern', 'ParsePatternNoCase', 'MatchFirst', 'AddPart', 'strcasecmp', 'strncasecmp',
               'name_is', 'ext_is', 'has_ext', 'eq', 'starts', 'ends_with', 'fputs', 'qlog_line', 'splash_log'}
# gadget-making calls and tags that show their text as it is: a literal there needs tr()
SHOWN = re.compile(r'(?:\bask\("[^"]*", "%s", |\b(?:Label1|Label2|LLabel1|LLabel2|Label|LLabel|QLabel|QButton|SimpleButton|KeyButton|theme_bubble|CLabel)\s*\(\s*'
                   r'|MUIA_(?:ShortHelp|Text_Contents|Window_Title|Frame_Title|FrameTitle)\s*,\s*\(ULONG\)\s*)"([^"]+)"')
FMT = re.compile(r'%(?:-?[0-9]*(?:\.[0-9]+)?|\.\*|-?\*)(?:hh|h|ll|l)?[diouxXeEfgGcs%]')

def literals(path):
    """the string literals of a C file: (line, start offset, raw text), adjacent ones joined"""
    src = open(path, encoding='latin-1').read()
    out, i, n, line, cur = [], 0, len(src), 1, None
    while i < n:
        c = src[i]
        if c == '\n': line += 1; i += 1; continue
        if src.startswith('/*', i):
            j = src.find('*/', i + 2); line += src.count('\n', i, j); i = j + 2; continue
        if src.startswith('//', i): i = src.find('\n', i); continue
        if c == "'":
            j = i + 1
            while src[j] != "'": j += 2 if src[j] == '\\' else 1
            i = j + 1
            if cur: out.append(cur); cur = None
            continue
        if c == '"':
            j = i + 1
            while src[j] != '"': j += 2 if src[j] == '\\' else 1
            if cur: cur = (cur[0], cur[1], cur[2] + src[i + 1:j])
            else: cur = (line, i, src[i + 1:j])
            i = j + 1; continue
        if c in ' \t\r': i += 1; continue
        if c == '\\' and src.startswith('\\\n', i): i += 1; continue
        if cur: out.append(cur); cur = None
        if src.startswith('#include', i): i = src.find('\n', i); continue
        i += 1
    if cur: out.append(cur)
    return src, out

def enclosing_call(src, pos):
    """the name of the call whose parentheses hold position pos ('' if none on the way back)"""
    depth, i = 0, pos - 1
    lim = max(0, pos - 3000)
    while i > lim:
        c = src[i]
        if c == ')': depth += 1
        elif c == '(':
            if depth == 0:
                j = i - 1
                while j >= 0 and src[j] in ' \t': j -= 1
                k = j
                while k >= 0 and (src[k].isalnum() or src[k] == '_'): k -= 1
                return src[k + 1:j + 1]
            depth -= 1
        elif c in ';{}' and depth == 0: return ''
        i -= 1
    return ''

ICON = re.compile(r'^(?:[A-Za-z]{1,2}|-?\d+(?:\.\d+)?)(?: (?:[A-Za-z]{1,2}|-?\d+(?:\.\d+)?))*$')
def not_words(t):
    """true for text that isn't words for people to read"""
    s = FMT.sub(' ', t)
    if re.match(r'\t[a-z]+\t', t): return True                              # vbcc inline assembler ("\tjsr\t-108(a6)")
    if not re.search(r'[A-Za-z\300-\377]{2}', s): return True             # numbers, signs, conversions
    if re.fullmatch(r'(\$VER: )?Easel [0-9.]+( \(.*)?', t): return True                # the version
    if ICON.match(t) and re.search(r'\d', t): return True                  # an icon's drawing (theme.c)
    if re.fullmatch(r'[A-Z][A-Z0-9_.]*[A-Z0-9]?(\(\)?)?', t) and len(t) >= 3: return True   # FUNCTION names, IDs
    if ' ' not in t and re.fullmatch(r'[A-Za-z0-9_]+:\S+', t): return True           # PROGDIR:file, FONTS:...
    if ' ' not in t and re.search(r'\.[a-z]{2,9}$', t) and not t.endswith('...'): return True  # file names
    if ' ' not in t and re.fullmatch(r'[a-z0-9_/.\-]+', t) and ('_' in t or '/' in t): return True   # paths, ids
    return False

def read_ignore():
    ign = set()
    if os.path.exists(IGNORE):
        for line in open(IGNORE, encoding='utf-8'):
            line = line.rstrip('\n')
            if not line or line.startswith('# ') or line == '#': continue
            ign.add(mklang.unescape(line))
    return ign

def sources():
    files = sorted(glob.glob(os.path.join(ROOT, 'amiga', '*.c')) + glob.glob(os.path.join(ROOT, 'amiga', '*.h')))
    return [f for f in files if not f.endswith('lang_table.h') and not f.endswith('convert.c')]

STEP = re.compile(r'doc_begin\(\s*[A-Za-z_>\-.]+\s*,\s*"((?:[^"\\]|\\.)*)"')
def step_names():
    """the undo steps' names written in core/*.c: (file, line, text)"""
    out = []
    for path in sorted(glob.glob(os.path.join(ROOT, 'core', '*.c'))):
        src = open(path, encoding='latin-1').read()
        for m in STEP.finditer(src):
            out.append((os.path.relpath(path, ROOT), src.count('\n', 0, m.start()) + 1, mklang.unescape(m.group(1))))
    return out

# the core's lists of names the interface shows through tr(): (file, array, 'all' strings of its
# initializer, or the 'first' of each entry)
CORE_LISTS = [('core/adjust10.c', 'photo_filter_names', 'all'), ('core/adjust10.c', 'lev_names', 'all'),
              ('core/adjust10.c', 'cur_names', 'all'), ('core/adjust10.c', 'bw_names', 'all'),
              ('core/adjust10.c', 'hs_presets', 'first'), ('core/fx.c', 'fx_names', 'all'),
              ('core/toolopts.c', 'crop_overlay_names', 'all'), ('core/crop.c', 'crop_presets', 'first'),
              ('core/gallery.c', 'gallery_defs', 'all')] + \
             [('core/gallery.c', n, 'all') for n in ('tex_names', 'light_names', 'daub_names', 'dir_names', 'grain_names', 'glow_names')]
def core_list_names():
    """the names in CORE_LISTS: (file, line, text)"""
    out = []
    for rel, name, mode in CORE_LISTS:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path): continue
        src = open(path, encoding='latin-1').read()
        m = re.search(r'\b' + name + r'\s*\[[^]]*\]\s*=\s*\{', src)
        if not m: continue
        i, depth, start = m.end(), 1, m.end()
        while i < len(src) and depth:
            depth += {'{': 1, '}': -1}.get(src[i], 0); i += 1
        block = src[start:i - 1]
        if mode == 'all': found = [(mm.start(), mm.group(1)) for mm in re.finditer(r'"((?:[^"\\]|\\.)*)"', block)]
        else: found = [(mm.start(), mm.group(1)) for mm in re.finditer(r'\{\s*"((?:[^"\\]|\\.)*)"', block)]
        for pos, t in found:
            out.append((rel, src.count('\n', 0, start + pos) + 1, mklang.unescape(t)))
    return out

def check_row(texts):
    """problems with one row's translations"""
    en, probs = texts[0], []
    fe = FMT.findall(en)
    for k, t in enumerate(texts[1:], 1):
        lang = 'de fr es it'.split()[k - 1]
        if FMT.findall(t) != fe: probs.append('%s: conversions %s, not %s' % (lang, FMT.findall(t), fe))
        if en.count('|') != t.count('|'): probs.append("%s: %d '|', not %d" % (lang, t.count('|'), en.count('|')))
        if en.startswith('\x1b') and not t.startswith(en[:2]): probs.append('%s: should start with the same \\33 escape' % lang)
        if ('_' in en) != ('_' in t) and re.search(r'(^|\|)_', en): probs.append("%s: '_' shortcut marker" % lang)
        if not t.strip(): probs.append('%s: empty' % lang)
    return probs

def main():
    items, bad = mklang.read_rows()
    for b in bad: print('lang/strings.txt:' + b)
    rows = {v[0]: (n, v) for k, n, v in items if k == 'row'}
    ign = read_ignore()
    missing, used, unwrapped = {}, set(), []
    for path in sources():
        rel = os.path.relpath(path, ROOT)
        src, lits = literals(path)
        for line, pos, raw in lits:
            t = mklang.unescape(raw)
            if t in rows: used.add(t); continue
            if t in ign or not_words(t): continue
            if enclosing_call(src, pos) in QUIET_CALLS: continue
            if re.search(r'MUIA_(?:FixWidthTxt|FixHeightTxt|String_Accept|String_Reject|List_Format)\s*,\s*(?:\(ULONG\)\s*)?$', src[max(0, pos - 60):pos]): continue
            missing.setdefault(t, []).append('%s:%d' % (rel, line))
        for m in SHOWN.finditer(src):
            t = mklang.unescape(m.group(1))
            if 'lang: as is' in src[m.start():src.find('\n', m.start())]: continue
            if t in rows or (t not in ign and not not_words(t)):
                unwrapped.append('%s:%d: "%s" is shown without tr()' % (rel, src.count('\n', 0, m.start()) + 1, m.group(1)))
    for rel, line, t in step_names() + core_list_names():
        if t in rows: used.add(t); continue
        if t in ign: continue
        missing.setdefault(t, []).append('%s:%d' % (rel, line))
    probs = []
    for en, (n, v) in rows.items():
        for p in check_row(v): probs.append('lang/strings.txt:%d: %s' % (n, p))
        # theme_text() translates what it is given, and some callers hand it words already translated:
        # a translation that is another row's English must translate to itself there
        for k in range(1, mklang.NLANG):
            t = v[k]
            if t != en and t in rows and rows[t][1][k] != t:
                probs.append('lang/strings.txt:%d: %s "%s" is also the English of line %d (translated twice it becomes "%s")'
                             % (n, 'de fr es it'.split()[k - 1], t, rows[t][0], rows[t][1][k]))
    unused = [(n, en) for en, (n, v) in sorted(rows.items(), key=lambda x: x[1][0]) if en not in used]
    if '--unused' in sys.argv:
        for n, en in unused: print('lang/strings.txt:%d: unused: "%s"' % (n, en.replace('\n', '\\n')))
        return 1 if unused else 0
    if '--rows' in sys.argv:
        for t in missing: print('\t'.join([t.replace('\\', '\\\\').replace('\n', '\\n').replace('\t', '\\t')] * 5))
        return 1 if missing else 0
    for t, locs in sorted(missing.items(), key=lambda x: x[1][0]):
        print('%s: not translated: "%s"' % (locs[0], t.replace('\n', '\\n')))
    for u in unwrapped: print(u)
    for p in probs: print(p)
    for n, en in unused: print('lang/strings.txt:%d: unused (the source no longer has it): "%s"' % (n, en.replace('\n', '\\n')))
    print('langcheck: %d rows, %d used, %d strings missing, %d shown without tr(), %d unused rows, %d row problems'
          % (len(rows), len(used), len(missing), len(unwrapped), len(unused), len(probs)))
    if missing: print('langcheck: add a row to lang/strings.txt for each string not translated (python3 tools/langcheck.py --rows prints them\n'
                      '           ready to paste, English in every column), translate it, then run python3 tools/mklang.py')
    return 1 if missing or unwrapped or probs or bad or unused else 0

if __name__ == '__main__':
    sys.exit(main())
