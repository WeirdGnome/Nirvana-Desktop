#!/usr/bin/env python3
"""wrap.py FILE [LINE:COL ...] [-n LINE:COL ...] [-p LINE:COL ...] [-_ LINE:COL ...]
Wraps literals in _("..."); after -n in N_("..."), after -p in P_("...") (menu path),
after -_ in _("...") again.
LINE:COL positions as printed by literals.py. Skips literals that are already wrapped."""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from literals import literals

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
