#!/usr/bin/env python3
"""Builds po/nirvana.pot from the code and updates po/*.po (msgmerge).

  _("…")  N_("…")  trn(n, "one", "other")             - xgettext
  P_("Window/Sort/by Name")                            - menu paths: each part separately
Run from the top directory: tools/i18n/pot.py"""
import glob, os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(__file__))
from literaly import literals

root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.chdir(root)
src = sorted(f for d in ("alice", "audio", "denise", "insight", "net", "platinum", "power", "shell")
             for f in glob.glob(d + "/*.cxx"))
pot = "po/nirvana.pot"
subprocess.run(["xgettext", "--from-code=UTF-8", "--language=C++", "-k_", "-kN_", "-ktr", "-ktrn:2,3",
                "--add-comments=TRANSLATORS:", "--package-name=Nirvana Desktop", "--package-version=0.1",
                "-o", pot] + src, check=True)

def unescape(lit):
    return bytes(lit[1:-1], "utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8")

text = open(pot, encoding="utf-8").read()
have = set(re.findall(r'^msgid "(.*)"$', text, re.M))
extra = {}
for f in src:
    s = open(f, encoding="utf-8").read()
    for (ln, col, a, b, lit) in literals(s):
        if not s[max(0, a - 3):a].endswith("P_("): continue
        for seg in unescape(lit).split("/"):
            if seg and seg != "logo" and seg not in have:
                extra.setdefault(seg, []).append(f"{f}:{ln}")
with open(pot, "a", encoding="utf-8") as out:
    for seg, refs in extra.items():
        out.write(f"\n#: {' '.join(refs)}\nmsgid \"{seg}\"\nmsgstr \"\"\n")
subprocess.run(["msguniq", "-o", pot, pot], check=True)
for po in glob.glob("po/*.po"):
    subprocess.run(["msgmerge", "-q", "--update", "--backup=none", "--no-fuzzy-matching", po, pot], check=True)
    subprocess.run(["msgattrib", "--no-obsolete", "-o", po, po], check=True)
n = len(re.findall(r'^msgid ', open(pot, encoding="utf-8").read(), re.M)) - 1
print(f"{pot}: {n} tekstów")
