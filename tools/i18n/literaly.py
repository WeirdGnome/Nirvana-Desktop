#!/usr/bin/env python3
"""Wyszukuje literały napisowe w kodzie C++ (bez komentarzy, #include, R"(...)").
Wypisuje: plik:linia:kolumna<TAB>literał<TAB>kontekst."""
import re, sys

def literals(src):
    i, n, line, col0 = 0, len(src), 1, 0
    out = []
    while i < n:
        c = src[i]
        if c == '\n':
            line += 1; col0 = i + 1; i += 1; continue
        if src.startswith('//', i):
            j = src.find('\n', i); i = n if j < 0 else j; continue
        if src.startswith('/*', i):
            j = src.find('*/', i + 2); seg = src[i:j + 2]; line += seg.count('\n'); i = j + 2
            if '\n' in seg: col0 = i - (len(seg) - seg.rfind('\n') - 1)
            continue
        if c == '#' and src[col0:i].strip() == '':
            j = src.find('\n', i)
            if src.startswith('#include', i) or src.startswith('#pragma', i): i = j; continue
        if c == 'R' and src.startswith('R"', i) and not (src[i-1].isalnum() or src[i-1] == '_'):
            m = re.match(r'R"([^(]*)\(', src[i:]); end = ')' + m.group(1) + '"'
            j = src.find(end, i); seg = src[i:j + len(end)]; line += seg.count('\n'); i = j + len(end); continue
        if c == "'":
            j = i + 1
            while src[j] != "'": j += 2 if src[j] == '\\' else 1
            i = j + 1; continue
        if c == '"':
            j = i + 1
            while src[j] != '"': j += 2 if src[j] == '\\' else 1
            out.append((line, i - col0, i, j + 1, src[i:j + 1])); i = j + 1; continue
        i += 1
    return out

if __name__ == '__main__':
    for f in sys.argv[1:]:
        src = open(f, encoding='utf-8').read()
        lines = src.split('\n')
        for (ln, col, a, b, lit) in literals(src):
            print(f"{f}:{ln}:{col}\t{lit}\t{lines[ln-1].strip()[:110]}")
