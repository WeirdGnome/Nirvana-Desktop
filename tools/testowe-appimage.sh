#!/bin/sh
# Buduje trzy małe, testowe AppImage (typ 2) do sprawdzania Ramblera:
#   Zegar-2.1-x86_64.AppImage  (xclock, ikona PNG, zapisuje raport z piaskownicy)
#   Kalkulator_v1.0.AppImage   (xcalc, ikona PNG)
#   Oczy-0.9.AppImage          (xeyes, ikona SVG)
# Wymaga: gcc, mksquashfs (squashfs-tools), convert (ImageMagick), x11-apps.
# Użycie: tools/testowe-appimage.sh KATALOG_DOCELOWY
set -e
OUT="${1:-.}"
B="$(mktemp -d)"
trap 'rm -rf "$B"' EXIT

printf '#include <stdio.h>\nint main(void){puts("runtime testowy Nirvana Desktop");return 0;}\n' > "$B/runtime.c"
gcc -O2 -o "$B/runtime" "$B/runtime.c"

mk() { # plik program typ_ikony kolor litera dodatkowe_polecenia
  d="$B/$2.AppDir"; mkdir -p "$d"
  if [ "$3" = png ]; then
    convert -size 64x64 xc:none -fill "$4" -draw "roundrectangle 4,4 60,60 12,12" \
      -fill white -pointsize 36 -gravity center -annotate 0 "$5" "$d/ikona.png"
    ln -s ikona.png "$d/.DirIcon"
  else
    printf '<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64"><circle cx="32" cy="32" r="28" fill="%s" stroke="black" stroke-width="2"/><circle cx="22" cy="28" r="7" fill="white"/><circle cx="42" cy="28" r="7" fill="white"/><circle cx="23" cy="29" r="3"/><circle cx="43" cy="29" r="3"/></svg>' "$4" > "$d/ikona.svg"
    ln -s ikona.svg "$d/.DirIcon"
  fi
  printf '[Desktop Entry]\nName=%s\nExec=AppRun\nIcon=ikona\nType=Application\n' "$2" > "$d/app.desktop"
  printf '#!/bin/sh\n%s\nexec %s\n' "$6" "$2" > "$d/AppRun"
  chmod +x "$d/AppRun"
  mksquashfs "$d" "$B/$2.sqfs" -root-owned -noappend -quiet >/dev/null
  cat "$B/runtime" "$B/$2.sqfs" > "$OUT/$1"
  chmod +x "$OUT/$1"
  echo "$OUT/$1"
}

mk "Zegar-2.1-x86_64.AppImage" xclock png "#2E7D32" Z \
  '{ echo "NIRVANA_SANDBOX=$NIRVANA_SANDBOX"; echo "/home widzi: $(ls /home | tr "\\n" " ")"; touch /usr/proba 2>/dev/null && echo "zapis do /usr: TAK" || echo "zapis do /usr: zablokowany"; touch "$HOME/proba-zapisu" && echo "zapis do Nirvana HD: TAK"; echo "PID w piaskownicy: $$"; } > "$HOME/piaskownica.txt"'
mk "Kalkulator_v1.0.AppImage" xcalc png "#C62828" "÷" ''
mk "Oczy-0.9.AppImage" xeyes svg "#F9A825" "" ''
