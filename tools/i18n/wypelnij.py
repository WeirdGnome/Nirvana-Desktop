#!/usr/bin/env python3
"""wypelnij.py JĘZYK - wpisuje tłumaczenia z tools/i18n/JĘZYK.py do po/JĘZYK.po
(po tools/i18n/pot.py). Wypisuje teksty bez tłumaczenia."""
import os, sys, importlib.util
import polib
lang = sys.argv[1]
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("t", os.path.join(here, lang + ".py"))
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
path = os.path.join(here, "..", "..", "po", lang + ".po")
po = polib.pofile(path)
missing = []
for e in po:
    t = m.T.get(e.msgid)
    if t is None: missing.append(e.msgid); continue
    if e.msgid_plural:
        if isinstance(t, str): t = (t, t)
        e.msgstr_plural = {0: t[0], 1: t[1]}
    else:
        e.msgstr = t
    if 'fuzzy' in e.flags: e.flags.remove('fuzzy')
po.save(path)
for x in missing: print("BRAK:", repr(x))
print(f"{path}: {len(po.translated_entries())}/{len(po)}")
