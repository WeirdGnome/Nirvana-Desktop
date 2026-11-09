#!/bin/sh
# LisaOS - instalator dla Devuana 6 "Excalibur" (baza: Debian 13 "trixie")
#
#   sudo ./instaluj.sh                  zależności, budowa, paczka .deb i instalacja
#   sudo ./instaluj.sh --logowanie      do tego ekran logowania LightDM
#   sudo ./instaluj.sh --xinitrc        ~/.xinitrc z "exec lisaos-session" (dla startx)
#        ./instaluj.sh --tylko-paczka   tylko zbuduj build-deb/lisaos_*.deb (bez instalacji)
#   sudo ./instaluj.sh --odinstaluj     usuń pakiet lisaos (ustawienia w ~ zostają)
#
# Inne opcje: --tak (bez pytań), --bez-x (nie instaluj serwera X), --pomoc.
#
# LisaOS instaluje się jako zwykły pakiet "lisaos" (pliki w /usr), więc później
# można go usunąć przez apt, a apt dociąga programy, z których korzysta
# (NetworkManager, udisks2, polkit, PulseAudio/PipeWire...).
set -eu

cd "$(dirname "$(readlink -f "$0")")"
SRC=$(pwd)
BUILD="$SRC/build-deb"

TAK=; LOGOWANIE=; XINITRC=; TYLKO_PACZKA=; ODINSTALUJ=; BEZ_X=
for a in "$@"; do
  case "$a" in
    --tak|-y) TAK=1 ;;
    --logowanie) LOGOWANIE=1 ;;
    --xinitrc) XINITRC=1 ;;
    --tylko-paczka) TYLKO_PACZKA=1 ;;
    --odinstaluj) ODINSTALUJ=1 ;;
    --bez-x) BEZ_X=1 ;;
    --pomoc|-h|--help) sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "Nieznana opcja: $a (zobacz --pomoc)" >&2; exit 2 ;;
  esac
done

if [ -t 1 ]; then B=$(printf '\033[1m'); R=$(printf '\033[0m'); C=$(printf '\033[31m'); else B=; R=; C=; fi
krok() { printf '\n%s==> %s%s\n' "$B" "$*" "$R"; }
blad() { printf '%sBłąd:%s %s\n' "$C" "$R" "$*" >&2; exit 1; }
pytaj() {   # pytaj "pytanie" -> 0 = tak
  [ -n "$TAK" ] && return 0
  printf '%s [T/n] ' "$1"
  read -r odp || odp=n
  case "$odp" in ''|t|T|tak|y|Y|yes) return 0 ;; *) return 1 ;; esac
}

# --- kto, gdzie ---------------------------------------------------------------
JA=$(id -u)
UZYTKOWNIK=${SUDO_USER:-}
[ "$UZYTKOWNIK" = root ] && UZYTKOWNIK=
# źródła w katalogu, do którego użytkownik nie pisze - budujemy jako root
if [ -n "$UZYTKOWNIK" ] && ! runuser -u "$UZYTKOWNIK" -- test -w "$SRC" 2>/dev/null; then UZYTKOWNIK_BUDUJE=; else UZYTKOWNIK_BUDUJE=$UZYTKOWNIK; fi
jako_uzytkownik() {   # budujemy jako zwykły użytkownik, żeby build-deb nie należał do roota
  if [ "$JA" = 0 ] && [ -n "$UZYTKOWNIK_BUDUJE" ]; then runuser -u "$UZYTKOWNIK_BUDUJE" -- "$@"; else "$@"; fi
}

if [ -n "$ODINSTALUJ" ]; then
  [ "$JA" = 0 ] || blad "uruchom przez sudo: sudo $0 --odinstaluj"
  krok "Usuwanie pakietu lisaos"
  apt-get remove -y lisaos
  echo "Ustawienia użytkowników (~/.config/lisaos, ~/Aplikacje) zostały na miejscu."
  exit 0
fi

[ -n "$TYLKO_PACZKA" ] || [ "$JA" = 0 ] || blad "instalacja wymaga uprawnień administratora: sudo $0"
command -v apt-get >/dev/null 2>&1 || blad "to nie jest system z apt (Devuan/Debian)"

ID=; VERSION_CODENAME=; PRETTY_NAME=nieznany
[ -r /etc/os-release ] && . /etc/os-release
echo "${B}LisaOS 0.1 - instalator${R}"
echo "System: $PRETTY_NAME"
if [ "$ID" != devuan ] || [ "$VERSION_CODENAME" != excalibur ]; then
  echo "Instalator jest przygotowany dla Devuana 6 \"Excalibur\" (Debian 13)."
  echo "Na innych wydaniach nazwy pakietów mogą się różnić."
  pytaj "Kontynuować mimo to?" || exit 1
fi

# --- zależności ---------------------------------------------------------------
# pakiet istnieje w repozytoriach?
jest() { apt-cache show "$1" >/dev/null 2>&1; }

BUDOWA="build-essential cmake git pkg-config gettext dpkg-dev file
  libx11-dev libxext-dev libxft-dev libxinerama-dev libxcursor-dev libxfixes-dev
  libxrender-dev libxss-dev libfontconfig-dev libdbus-1-dev
  libpng-dev libjpeg-dev zlib1g-dev"

if [ "$JA" = 0 ]; then
  krok "Odświeżanie listy pakietów"
  apt-get update
  krok "Narzędzia i biblioteki do budowania"
  # libvterm z systemu, jeśli jest (inaczej LisaOS ma własną kopię)
  extra=; jest libvterm-dev && extra=libvterm-dev
  # shellcheck disable=SC2086
  DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends $BUDOWA $extra
else
  for p in cmake g++ msgfmt dpkg-shlibdeps git pkg-config; do
    command -v "$p" >/dev/null 2>&1 || blad "brak $p - uruchom przez sudo albo doinstaluj: $BUDOWA"
  done
fi

# --- budowa i paczka ----------------------------------------------------------
krok "Budowanie LisaOS (pierwszy raz pobiera też FLTK 1.4 z GitHuba)"
FLTK_OPT=
[ -n "${FLTK_DIR:-}" ] && FLTK_OPT="-DFLTK_DIR=$FLTK_DIR"
if [ -d "$BUILD" ] && [ "$JA" = 0 ] && [ -n "$UZYTKOWNIK_BUDUJE" ]; then chown -R "$UZYTKOWNIK_BUDUJE" "$BUILD"; fi
# shellcheck disable=SC2086
jako_uzytkownik cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr $FLTK_OPT
jako_uzytkownik cmake --build "$BUILD" -j "$(nproc 2>/dev/null || echo 2)"

krok "Tworzenie paczki .deb"
rm -f "$BUILD"/lisaos_*.deb
# UTF-8: inaczej cpack nie spakuje plików z polskimi literami w nazwie (tapety)
(cd "$BUILD" && LC_ALL=C.UTF-8 jako_uzytkownik cpack -G DEB >/dev/null)
DEB=$(ls "$BUILD"/lisaos_*.deb 2>/dev/null | head -n 1)
[ -n "$DEB" ] || blad "cpack nie utworzył paczki"
echo "Paczka: $DEB"
dpkg-deb -f "$DEB" Depends | sed 's/^/  Depends: /'

if [ -n "$TYLKO_PACZKA" ]; then
  echo
  echo "Instalacja paczki:  sudo apt install $DEB"
  exit 0
fi

# --- instalacja ---------------------------------------------------------------
krok "Instalowanie pakietu lisaos i programów, z których korzysta"
# apt z lokalnym plikiem rozwiązuje zależności; Recommends (NetworkManager,
# udisks2, polkit, xsecurelock...) instaluje domyślnie, brakujące pomija
DEBIAN_FRONTEND=noninteractive apt-get install -y "$DEB"

if [ -z "$BEZ_X" ] && ! command -v Xorg >/dev/null 2>&1; then
  krok "Serwer X"
  DEBIAN_FRONTEND=noninteractive apt-get install -y xserver-xorg xinit
fi

# dźwięk: PipeWire na Devuanie startuje z autostartu XDG (uruchamia go lisaos-session)
if ! dpkg -s pipewire-pulse >/dev/null 2>&1 && ! dpkg -s pulseaudio >/dev/null 2>&1; then
  if jest pipewire-pulse && pytaj "Brak serwera dźwięku. Zainstalować PipeWire (pipewire-pulse, wireplumber)?"; then
    DEBIAN_FRONTEND=noninteractive apt-get install -y pipewire-pulse wireplumber
  fi
fi

# NetworkManager zarządza tylko interfejsami spoza /etc/network/interfaces
if [ -f /etc/network/interfaces ] && grep -Eq '^[[:space:]]*(auto|allow-hotplug|iface)[[:space:]]+(e|w)' /etc/network/interfaces; then
  echo
  echo "Uwaga: karty sieciowe są skonfigurowane w /etc/network/interfaces, więc"
  echo "NetworkManager (i menu sieci LisaOS) ich nie obsłuży. Żeby oddać je"
  echo "NetworkManagerowi, zostaw tam tylko 'lo' i uruchom: sudo service network-manager restart"
fi

if [ -n "$LOGOWANIE" ]; then
  krok "Ekran logowania LightDM"
  DEBIAN_FRONTEND=noninteractive apt-get install -y lightdm lightdm-gtk-greeter
  mkdir -p /etc/lightdm/lightdm.conf.d
  printf '[Seat:*]\nuser-session=lisaos\n' > /etc/lightdm/lightdm.conf.d/50-lisaos.conf
  echo "LightDM proponuje sesję LisaOS (/etc/lightdm/lightdm.conf.d/50-lisaos.conf)."
fi

if [ -n "$XINITRC" ] && [ -n "$UZYTKOWNIK" ]; then
  DOM=$(getent passwd "$UZYTKOWNIK" | cut -d: -f6)
  if [ -f "$DOM/.xinitrc" ]; then
    cp "$DOM/.xinitrc" "$DOM/.xinitrc.przed-lisaos"
    echo "Poprzedni ~/.xinitrc zapisany jako ~/.xinitrc.przed-lisaos"
  fi
  printf '#!/bin/sh\nexec /usr/bin/lisaos-session\n' > "$DOM/.xinitrc"
  chown "$UZYTKOWNIK" "$DOM/.xinitrc"
  echo "$DOM/.xinitrc uruchamia LisaOS - wystarczy wpisać startx."
fi

krok "Gotowe"
echo "LisaOS jest zainstalowany ($(dpkg-query -W -f '${Version}' lisaos))."
dm=
for d in lightdm sddm gdm3 slim lxdm xdm wdm; do [ -x "/etc/init.d/$d" ] && dm=$d; done
if [ -n "$dm" ]; then
  echo "Wyloguj się i wybierz sesję \"LisaOS\" na ekranie logowania ($dm)."
else
  echo "Uruchom z konsoli:  startx /usr/bin/lisaos-session"
  echo "(albo zainstaluj ekran logowania: sudo $0 --logowanie)"
fi
echo "Usunięcie:          sudo apt remove lisaos"
