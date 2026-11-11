#!/bin/sh
# Builds three small test AppImages (type 2) for checking Rambler:
#   Clock-2.1-x86_64.AppImage   (xclock, PNG icon, writes a sandbox report)
#   Calculator_v1.0.AppImage    (xcalc, PNG icon)
#   Eyes-0.9.AppImage           (xeyes, SVG icon)
# Needs: gcc, mksquashfs (squashfs-tools), convert (ImageMagick), x11-apps.
# Usage: tools/test-appimages.sh TARGET_DIR
set -e
OUT="${1:-.}"
B="$(mktemp -d)"
trap 'rm -rf "$B"' EXIT

printf '#include <stdio.h>\nint main(void){puts("Nirvana Desktop test runtime");return 0;}\n' > "$B/runtime.c"
gcc -O2 -o "$B/runtime" "$B/runtime.c"

mk() { # file program icon_type color letter extra_commands
  d="$B/$2.AppDir"; mkdir -p "$d"
  if [ "$3" = png ]; then
    convert -size 64x64 xc:none -fill "$4" -draw "roundrectangle 4,4 60,60 12,12" \
      -fill white -pointsize 36 -gravity center -annotate 0 "$5" "$d/icon.png"
    ln -s icon.png "$d/.DirIcon"
  else
    printf '<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64"><circle cx="32" cy="32" r="28" fill="%s" stroke="black" stroke-width="2"/><circle cx="22" cy="28" r="7" fill="white"/><circle cx="42" cy="28" r="7" fill="white"/><circle cx="23" cy="29" r="3"/><circle cx="43" cy="29" r="3"/></svg>' "$4" > "$d/icon.svg"
    ln -s icon.svg "$d/.DirIcon"
  fi
  printf '[Desktop Entry]\nName=%s\nExec=AppRun\nIcon=icon\nType=Application\n' "$2" > "$d/app.desktop"
  printf '#!/bin/sh\n%s\nexec %s\n' "$6" "$2" > "$d/AppRun"
  chmod +x "$d/AppRun"
  mksquashfs "$d" "$B/$2.sqfs" -root-owned -noappend -quiet >/dev/null
  cat "$B/runtime" "$B/$2.sqfs" > "$OUT/$1"
  chmod +x "$OUT/$1"
  echo "$OUT/$1"
}

mk "Clock-2.1-x86_64.AppImage" xclock png "#2E7D32" C \
  '{ echo "NIRVANA_SANDBOX=$NIRVANA_SANDBOX"; echo "/home shows: $(ls /home | tr "\\n" " ")"; touch /usr/write-test 2>/dev/null && echo "writing to /usr: YES" || echo "writing to /usr: blocked"; touch "$HOME/write-test" && echo "writing to Nirvana HD: YES"; echo "PID in the sandbox: $$"; } > "$HOME/sandbox.txt"'
mk "Calculator_v1.0.AppImage" xcalc png "#C62828" "÷" ''
mk "Eyes-0.9.AppImage" xeyes svg "#F9A825" "" ''
