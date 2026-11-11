#!/usr/bin/env python3
"""fill.py LANG - writes the translations from tools/i18n/LANG.py into po/LANG.po
(after tools/i18n/pot.py). Prints texts that have no translation."""
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
        e.msgstr_plural = {i: f for i, f in enumerate(t)}
    else:
        e.msgstr = t
    if 'fuzzy' in e.flags: e.flags.remove('fuzzy')
po.save(path)
for x in missing: print("MISSING:", repr(x))
print(f"{path}: {len(po.translated_entries())}/{len(po)}")
