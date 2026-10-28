#!/usr/bin/env python3
"""name_anon.py - give dav1d's anonymous structs and unions names, for compilers without C11's
anonymous members (vbcc). clang's JSON AST of every translation unit shows each anonymous record
and each member access that passes through one. The record gets a name after its closing brace
("u", "s", "u2", ...) and the access gets that name spelt out (a.x becomes a.s.x).

usage: name_anon.py av1-dir   (rewrites the sources in place; run it once on fresh dav1d sources)
"""
import json, os, subprocess, sys
from collections import defaultdict

ROOT = os.path.abspath(sys.argv[1])
TUS = [f"src/{f}.c" for f in "cdf cpu ctx data decode dequant_tables getbits intra_edge itx_1d lf_mask lib log mem msac obu pal picture qm ref refmvs scan tables thread_task warpmv wedge".split()]
TMPL = "cdef_apply cdef fg_apply filmgrain ipred_prepare ipred itx lf_apply loopfilter looprestoration lr_apply mc recon".split()
JOBS = [(t, []) for t in TUS] + [(f"src/{t}_tmpl.c", [f"-DBITDEPTH={b}"]) for t in TMPL for b in (8, 16)]

records = {}                 # (file, begin offset) -> dict(end=(file, off), kind, parent, name)
fields = {}                  # implicit anonymous FieldDecl id -> record key
member_edits = {}            # (file, offset) -> prefix text to insert
conflicts = []

class Loc:
    """clang's JSON omits a location's file when it is the same as the one printed before"""
    def __init__(self): self.file = None
    def fix(self, d):
        if not isinstance(d, dict): return None
        if 'spellingLoc' in d:
            s = self.fix(d['spellingLoc']); self.fix(d['expansionLoc']); return s
        if 'offset' not in d: return None
        if 'file' in d: self.file = d['file']
        return (self.file, d['offset'])

def run(tu, extra):
    args = ['clang', '-std=c11', '-fsyntax-only', '-include', 'config.h', '-Icompat', '-I.', '-Iinclude', '-Isrc'] + extra + ['-Xclang', '-ast-dump=json', tu]
    out = subprocess.run(args, cwd=ROOT, capture_output=True, check=True).stdout
    ast = json.loads(out)
    loc = Loc()
    ids = {}                 # node id -> MemberExpr info, for this TU

    def walk(n, parent_rec):
        k = n.get('kind')
        # locations in printed order: loc, then range begin/end
        l = loc.fix(n.get('loc'))
        r = n.get('range') or {}
        b = loc.fix(r.get('begin')); e = loc.fix(r.get('end'))
        me = None
        if k == 'RecordDecl' and not n.get('name') and n.get('completeDefinition') and b:
            if b not in records:
                records[b] = dict(end=e, kind=n.get('tagUsed'), parent=parent_rec, name=None)
            parent_rec = b
        elif k == 'RecordDecl' and n.get('completeDefinition') and b:
            parent_rec = ('named', b)
        if k == 'FieldDecl' and n.get('isImplicit') and not n.get('name') and l:
            fields[n['id']] = l
        if k == 'MemberExpr':
            me = dict(name=n.get('name'), ref=n.get('referencedMemberDecl'), end=e)
        kids = n.get('inner', [])
        res = [walk(c, parent_rec) for c in kids]
        if k == 'MemberExpr':
            # the chain of anonymous members under this one
            chain = []
            base = res[0] if res else None
            while base and base.get('anon'):
                chain.append(base['ref']); base = base.get('base')
            info = dict(anon=not n.get('name'), ref=n.get('referencedMemberDecl'), base=res[0] if res else None)
            if n.get('name') and chain and e:
                member_edits.setdefault(e, set()).add(tuple(chain))
            return info
        return None

    walk(ast, None)

def main():
    for tu, extra in JOBS:
        run(tu, extra)
        print('parsed', tu, *extra, file=sys.stderr)
    # names: per parent, "s"/"u" then "s2"/"u2"...
    anon = set(fields.values())
    for key in list(records):
        if key not in anon: del records[key]       # an unnamed type, not an anonymous member
    used = defaultdict(set)
    for key in sorted(records, key=lambda k: (k[0], k[1])):
        rec = records[key]
        base = 'u' if rec['kind'] == 'union' else 's'
        name, i = base, 1
        while name in used[rec['parent']]:
            i += 1; name = f"{base}{i}"
        used[rec['parent']].add(name)
        rec['name'] = name
    edits = defaultdict(list)          # file -> [(offset, text)]
    for key, rec in records.items():
        f, off = rec['end']            # the closing brace: the name goes after it
        edits[f].append((off + 1, ' ' + rec['name']))
    for (f, off), chains in member_edits.items():
        texts = set()
        for chain in chains:
            texts.add(''.join(records[fields[i]]['name'] + '.' for i in reversed(chain)))
        if len(texts) != 1:
            conflicts.append((f, off, texts)); continue
        edits[f].append((off, texts.pop()))
    for f, es in edits.items():
        path = f if os.path.isabs(f) else os.path.join(ROOT, f)
        if not os.path.abspath(path).startswith(ROOT) or not os.path.exists(path):
            print('NOT APPLIED (outside the sources, or made by ## pasting):', path, sorted(set(es)), file=sys.stderr); continue
        s = open(path, 'rb').read()
        for off, text in sorted(set(es), reverse=True):
            s = s[:off] + text.encode() + s[off:]
        open(path, 'wb').write(s)
        print(f'{path}: {len(set(es))} edits', file=sys.stderr)
    for c in conflicts: print('CONFLICT', c, file=sys.stderr)
    return 1 if conflicts else 0

if __name__ == '__main__':
    sys.exit(main())
