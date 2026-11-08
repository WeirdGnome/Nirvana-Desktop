#!/usr/bin/env python3
"""Import schematów kolorów z Redmond97 SE (Sliver X, GPL-3.0) do formatu LisaOS.

Użycie:
    tools/importuj-redmond97.py KATALOG_REDMOND97_SE schematy/redmond97-se

Z presetów (builder/presets/theme_*.conf) bierzemy wyłącznie wartości kolorów
i nazwę; skrypty i grafiki Redmond97 SE nie są kopiowane.
"""
import pathlib
import re
import sys

MAP = {  # klucz Redmond97 -> klucz LisaOS
    "bgcolor": "okno",
    "fgcolor": "tekst",
    "basecolor": "tresc",
    "basefg": "tekst_tresci",
    "selectedbg": "zaznaczenie",
    "selectedtext": "tekst_zaznaczenia",
    "border": "obramowanie",
    "activetitle": "pasek",
    "activetitle1": "pasek2",
    "activetitletext": "tekst_paska",
    "inactivetitle": "pasek_nieaktywny",
    "inactivetitle1": "pasek_nieaktywny2",
    "inactivetitletext": "tekst_paska_nieaktywnego",
}


def norm(v: str) -> str:
    v = v.strip().strip('"')
    if re.fullmatch(r"#[0-9a-fA-F]{3}", v):
        v = "#" + "".join(c * 2 for c in v[1:])
    m = re.fullmatch(r"(\d+),(\d+),(\d+)", v)
    if m:
        v = "#%02x%02x%02x" % tuple(int(x) for x in m.groups())
    return v.upper() if v.startswith("#") else v


def main() -> None:
    src = pathlib.Path(sys.argv[1])
    dst = pathlib.Path(sys.argv[2])
    dst.mkdir(parents=True, exist_ok=True)
    presets = sorted(src.glob("**/builder/presets/theme_*.conf"))
    for p in presets:
        vals = {}
        name = p.stem.removeprefix("theme_")
        for line in p.read_text(errors="replace").splitlines():
            m = re.match(r'\s*([A-Za-z_0-9]+)\s*=\s*(?:"([^"]*)"|([^\s#]*))', line)
            if not m:
                continue
            key, val = m.group(1), (m.group(2) or m.group(3) or "").strip()
            if key == "Theme_name" and val:
                name = val
            if key in MAP and val:
                v = norm(val)
                if re.fullmatch(r"#[0-9A-F]{6}", v):   # pomijamy błędne wpisy
                    vals[MAP[key]] = v
        if "okno" not in vals:
            continue
        slug = p.stem.removeprefix("theme_")
        out = [
            "# Schemat kolorów LisaOS",
            "# Kolory: Redmond97 SE (Sliver X), https://codeberg.org/Sliver_X/Redmond97-SE",
            "# Licencja źródła: GNU GPL 3.0",
            f"nazwa={name}",
            "zrodlo=Redmond97 SE",
            "styl_paska=" + ("gradient" if vals.get("pasek2", vals.get("pasek")) != vals.get("pasek") else "kolor"),
        ]
        for k in MAP.values():
            if k in vals:
                out.append(f"{k}={vals[k]}")
        (dst / f"{slug}.conf").write_text("\n".join(out) + "\n")
        print(f"{slug}: {name}")


if __name__ == "__main__":
    main()
