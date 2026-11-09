#!/usr/bin/env python3
"""opakuj.py PLIK [LINIA:KOL ...] [-n LINIA:KOL ...] [-p LINIA:KOL ...] [-_ LINIA:KOL ...]
Opakowuje literały w _("..."); po -n w N_("..."), po -p w P_("...") (ścieżka menu),
po -_ znów w _("...").
Pozycje LINIA:KOL jak z literaly.py. Pomija literały już opakowane."""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from literaly import literals

f = sys.argv[1]
mode, want = '_', {}
for a in sys.argv[2:]:
    if a in ('-n', '-p', '-_'): mode = {'-n': 'N_', '-p': 'P_', '-_': '_'}[a]; continue
    ln, col = a.split(':'); want[(int(ln), int(col))] = mode
src = open(f, encoding='utf-8').read()
lits = literals(src)
edits = []
for (ln, col, a, b, lit) in lits:
    m = want.pop((ln, col), None)
    if not m: continue
    before = src[max(0, a - 3):a]
    if before.endswith('_('): continue   # _( N_( P_(
    edits.append((a, b, m))
for a, b, m in sorted(edits, reverse=True):
    src = src[:a] + m + '(' + src[a:b] + ')' + src[b:]
open(f, 'w', encoding='utf-8').write(src)
if want: print("NIE ZNALEZIONO:", sorted(want), file=sys.stderr)
print(f"{f}: opakowano {len(edits)}")
