#!/usr/bin/env python3
"""Imports the color schemes of Redmond97 SE (Sliver X, GPL-3.0) into the Nirvana Desktop format.

Usage:
    tools/import-redmond97.py REDMOND97_SE_DIR schemes/redmond97-se

Only the color values and the name are taken from the presets (builder/presets/theme_*.conf);
the Redmond97 SE scripts and graphics are not copied.
"""
import pathlib
import re
import sys

MAP = {  # Redmond97 key -> Nirvana Desktop key
    "bgcolor": "window",
    "fgcolor": "text",
    "basecolor": "content",
    "basefg": "content_text",
    "selectedbg": "selection",
    "selectedtext": "selection_text",
    "border": "border",
    "activetitle": "title",
    "activetitle1": "title2",
    "activetitletext": "title_text",
    "inactivetitle": "title_inactive",
    "inactivetitle1": "title_inactive2",
    "inactivetitletext": "title_inactive_text",
}


def norm(v: str) -> str:
    v = v.strip().strip('"')
    if re.fullmatch(r"#[0-9a-fA-F]{3}", v):
        v = "#" + "".join(c * 2 for c in v[1:])
    m = re.fullmatch(r"(\d+),(\d+),(\d+)", v)
    if m:
        v = "#%02x%02x%02x" % tuple(int(x) for x in m.groups())
    return v.upper() if v.startswith("#") else v


# Preset names that are someone else's trademarks get neutral names (slug, name).
RENAMES = {"windows-95": ("periwinkle", "Periwinkle"), "more-windows": ("many-panes", "Many Panes")}


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
                if re.fullmatch(r"#[0-9A-F]{6}", v):   # skip invalid entries
                    vals[MAP[key]] = v
        if "window" not in vals:
            continue
        slug = p.stem.removeprefix("theme_")
        if slug in RENAMES:
            slug, name = RENAMES[slug]
        out = [
            "# Nirvana Desktop color scheme",
            "# Colors: Redmond97 SE (Sliver X), https://codeberg.org/Sliver_X/Redmond97-SE",
            "# Source license: GNU GPL 3.0",
            f"name={name}",
            "source=Redmond97 SE",
            "title_style=" + ("gradient" if vals.get("title2", vals.get("title")) != vals.get("title") else "solid"),
        ]
        for k in MAP.values():
            if k in vals:
                out.append(f"{k}={vals[k]}")
        (dst / f"{slug}.conf").write_text("\n".join(out) + "\n")
        print(f"{slug}: {name}")


if __name__ == "__main__":
    main()
