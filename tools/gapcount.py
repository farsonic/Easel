#!/usr/bin/env python3
# Counts the Have / Partial / Missing / Excluded rows of docs/photocraft-gap.md, section by section
# (sections 1-20 are rated; 21 is the Amiga legacy extras), for the coverage summary in section 23.
# Usage: python3 tools/gapcount.py docs/photocraft-gap.md
import re,sys
L=open(sys.argv[1]).read().split('\n')
sec=None; cnt={}; other={}
for l in L:
    m=re.match(r'## (\d+)\. ',l)
    if m: sec=int(m.group(1)); continue
    if l.startswith('## '): sec=None
    if sec is None or sec>21 or not l.startswith('|') or l.startswith('|---'): continue
    cells=[c.strip() for c in l.strip('|').split('|')]
    if len(cells)<2: continue
    c=cells[1]
    m=re.match(r'\*\*(Have|Partial|Missing|Excluded|Left out)\b',c) or re.match(r'(Have|Partial|Missing|Excluded|Left out)\b',c)
    if not m:
        other.setdefault(sec,set()).add(c[:30]); continue
    k={'Left out':'Excluded'}.get(m.group(1),m.group(1))
    cnt.setdefault(sec,{'Have':0,'Partial':0,'Missing':0,'Excluded':0})[k]+=1
T={'Have':0,'Partial':0,'Missing':0,'Excluded':0}
for s in sorted(cnt):
    d=cnt[s]; r=d['Have']+d['Partial']+d['Missing']
    print(s,d,r,'%.0f%%'%(100*(d['Have']+d['Partial']/2)/r if r else 0))
    if s<=20:
        for k in T: T[k]+=d[k]
r=T['Have']+T['Partial']+T['Missing']
print('ALL',T,r,'%.1f%%'%(100*(T['Have']+T['Partial']/2)/r))
for s in other: print('unrated',s,list(other[s])[:6])
