#!/bin/sh
# Nirvana Desktop - installer for Devuan 6 "Excalibur" (based on Debian 13 "trixie")
#
#   sudo ./install.sh                  dependencies, build, .deb package and installation
#   sudo ./install.sh --login-screen   also install the LightDM login screen
#   sudo ./install.sh --xinitrc        write ~/.xinitrc with "exec nirvana-session" (for startx)
#        ./install.sh --package-only   only build build-deb/nirvana_*.deb (no installation)
#   sudo ./install.sh --uninstall      remove the nirvana package (settings in ~ are kept)
#
# Other options: --yes (no questions), --no-x (don't install the X server), --help.
#
# Nirvana Desktop is installed as an ordinary package "nirvana" (files in /usr), so it
# can be removed with apt later, and apt pulls in the programs it uses
# (NetworkManager, udisks2, polkit, PulseAudio/PipeWire...).
set -eu

cd "$(dirname "$(readlink -f "$0")")"
SRC=$(pwd)
BUILD="$SRC/build-deb"
VERSION=$(head -n 1 "$SRC/VERSION" 2>/dev/null || echo "?")

YES=; LOGIN=; XINITRC=; PACKAGE_ONLY=; UNINSTALL=; NO_X=
for a in "$@"; do
  case "$a" in
    --yes|-y) YES=1 ;;
    --login-screen) LOGIN=1 ;;
    --xinitrc) XINITRC=1 ;;
    --package-only) PACKAGE_ONLY=1 ;;
    --uninstall) UNINSTALL=1 ;;
    --no-x) NO_X=1 ;;
    -h|--help) sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "Unknown option: $a (see --help)" >&2; exit 2 ;;
  esac
done

if [ -t 1 ]; then B=$(printf '\033[1m'); R=$(printf '\033[0m'); C=$(printf '\033[31m'); else B=; R=; C=; fi
step() { printf '\n%s==> %s%s\n' "$B" "$*" "$R"; }
die() { printf '%sError:%s %s\n' "$C" "$R" "$*" >&2; exit 1; }
ask() {   # ask "question" -> 0 = yes
  [ -n "$YES" ] && return 0
  printf '%s [Y/n] ' "$1"
  read -r answer || answer=n
  case "$answer" in ''|y|Y|yes|Yes) return 0 ;; *) return 1 ;; esac
}

# --- who and where -----------------------------------------------------------
ME=$(id -u)
USER_NAME=${SUDO_USER:-}
[ "$USER_NAME" = root ] && USER_NAME=
# sources in a directory the user cannot write to: build as root
if [ -n "$USER_NAME" ] && ! runuser -u "$USER_NAME" -- test -w "$SRC" 2>/dev/null; then BUILD_USER=; else BUILD_USER=$USER_NAME; fi
as_user() {   # build as the normal user, so build-deb is not owned by root
  if [ "$ME" = 0 ] && [ -n "$BUILD_USER" ]; then runuser -u "$BUILD_USER" -- "$@"; else "$@"; fi
}

if [ -n "$UNINSTALL" ]; then
  [ "$ME" = 0 ] || die "run with sudo: sudo $0 --uninstall"
  step "Removing the nirvana package"
  apt-get remove -y nirvana
  echo "User settings (~/.config/nirvana, ~/Applications) were left in place."
  exit 0
fi

[ -n "$PACKAGE_ONLY" ] || [ "$ME" = 0 ] || die "installation needs administrator rights: sudo $0"
command -v apt-get >/dev/null 2>&1 || die "this is not an apt-based system (Devuan/Debian)"

ID=; VERSION_CODENAME=; PRETTY_NAME=unknown
[ -r /etc/os-release ] && . /etc/os-release
echo "${B}Nirvana Desktop $VERSION - installer${R}"
echo "System: $PRETTY_NAME"
if [ "$ID" != devuan ] || [ "$VERSION_CODENAME" != excalibur ]; then
  echo "This installer is made for Devuan 6 \"Excalibur\" (Debian 13)."
  echo "On other releases package names may differ."
  ask "Continue anyway?" || exit 1
fi

# --- dependencies ------------------------------------------------------------
# does the package exist in the repositories?
available() { apt-cache show "$1" >/dev/null 2>&1; }

BUILD_DEPS="build-essential cmake git pkg-config gettext dpkg-dev file
  libx11-dev libxext-dev libxft-dev libxinerama-dev libxcursor-dev libxfixes-dev
  libxrender-dev libxss-dev libfontconfig-dev libdbus-1-dev
  libpng-dev libjpeg-dev zlib1g-dev
  libgl-dev libxcomposite-dev libxdamage-dev libcups2-dev liblightdm-gobject-dev
  libglib2.0-dev libpolkit-agent-1-dev"

if [ "$ME" = 0 ]; then
  step "Updating the package lists"
  apt-get update
  step "Build tools and libraries"
  # the system libvterm if available (otherwise Nirvana Desktop uses its own copy)
  extra=; available libvterm-dev && extra=libvterm-dev
  # shellcheck disable=SC2086
  DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends $BUILD_DEPS $extra
else
  for p in cmake g++ msgfmt dpkg-shlibdeps git pkg-config; do
    command -v "$p" >/dev/null 2>&1 || die "$p is missing - run with sudo or install: $BUILD_DEPS"
  done
fi

# --- build and package -------------------------------------------------------
step "Building Nirvana Desktop (the first time this also downloads FLTK 1.4 from GitHub)"
FLTK_OPT=
[ -n "${FLTK_DIR:-}" ] && FLTK_OPT="-DFLTK_DIR=$FLTK_DIR"
if [ -d "$BUILD" ] && [ "$ME" = 0 ] && [ -n "$BUILD_USER" ]; then chown -R "$BUILD_USER" "$BUILD"; fi
# shellcheck disable=SC2086
as_user cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr $FLTK_OPT
as_user cmake --build "$BUILD" -j "$(nproc 2>/dev/null || echo 2)"

step "Creating the .deb package"
rm -f "$BUILD"/nirvana_*.deb
# UTF-8: otherwise cpack cannot pack files with non-ASCII names (wallpapers)
(cd "$BUILD" && LC_ALL=C.UTF-8 as_user cpack -G DEB >/dev/null)
DEB=$(ls "$BUILD"/nirvana_*.deb 2>/dev/null | head -n 1)
[ -n "$DEB" ] || die "cpack did not create the package"
echo "Package: $DEB"
dpkg-deb -f "$DEB" Depends | sed 's/^/  Depends: /'

if [ -n "$PACKAGE_ONLY" ]; then
  echo
  echo "To install the package:  sudo apt install --reinstall $DEB"
  exit 0
fi

# --- installation ------------------------------------------------------------
step "Installing the nirvana package and the programs it uses"
# apt with a local file resolves dependencies; Recommends (NetworkManager,
# udisks2, polkit, xsecurelock...) are installed by default, missing ones skipped
# --reinstall: also when the same version is already installed (rebuilt package);
# --allow-downgrades: the package from this folder wins even over a newer number
# apt reads local packages as the "_apt" user, who cannot enter a home folder
# (otherwise: "N: Download is performed unsandboxed as root") - copy it out first
APT_DIR=$(mktemp -d /tmp/nirvana-install.XXXXXX)
chmod 755 "$APT_DIR"
cp "$DEB" "$APT_DIR/" && chmod 644 "$APT_DIR/$(basename "$DEB")"
DEBIAN_FRONTEND=noninteractive apt-get install -y --reinstall --allow-downgrades "$APT_DIR/$(basename "$DEB")"
rm -rf "$APT_DIR"

if [ -z "$NO_X" ] && ! command -v Xorg >/dev/null 2>&1; then
  step "X server"
  DEBIAN_FRONTEND=noninteractive apt-get install -y xserver-xorg xinit
fi

# sound: on Devuan PipeWire starts from XDG autostart (nirvana-session runs it)
if ! dpkg -s pipewire-pulse >/dev/null 2>&1 && ! dpkg -s pulseaudio >/dev/null 2>&1; then
  if available pipewire-pulse && ask "No sound server found. Install PipeWire (pipewire-pulse, wireplumber)?"; then
    DEBIAN_FRONTEND=noninteractive apt-get install -y pipewire-pulse wireplumber
  fi
fi

# NetworkManager only manages interfaces that are not in /etc/network/interfaces
if [ -f /etc/network/interfaces ] && grep -Eq '^[[:space:]]*(auto|allow-hotplug|iface)[[:space:]]+(e|w)' /etc/network/interfaces; then
  echo
  echo "Note: network adapters are configured in /etc/network/interfaces, so"
  echo "NetworkManager (and the Nirvana Desktop network menu) will not manage them. To hand"
  echo "them over to NetworkManager, keep only 'lo' there and run: sudo service network-manager restart"
fi

if [ -n "$LOGIN" ]; then
  step "Nirvana-DM login screen (LightDM)"
  DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends lightdm
  mkdir -p /etc/lightdm/lightdm.conf.d
  if [ -x /usr/sbin/nirvana-dm-greeter ]; then
    printf '[Seat:*]\ngreeter-session=nirvana-dm\nuser-session=nirvana\n' > /etc/lightdm/lightdm.conf.d/50-nirvana.conf
    echo "LightDM shows Nirvana-DM and starts Nirvana Desktop (/etc/lightdm/lightdm.conf.d/50-nirvana.conf)."
  else   # built without liblightdm-gobject-dev: another greeter
    DEBIAN_FRONTEND=noninteractive apt-get install -y lightdm-gtk-greeter
    printf '[Seat:*]\nuser-session=nirvana\n' > /etc/lightdm/lightdm.conf.d/50-nirvana.conf
    echo "LightDM offers the Nirvana Desktop session (Nirvana-DM was not built)."
  fi
fi

if [ -n "$XINITRC" ] && [ -n "$USER_NAME" ]; then
  HOME_DIR=$(getent passwd "$USER_NAME" | cut -d: -f6)
  if [ -f "$HOME_DIR/.xinitrc" ]; then
    cp "$HOME_DIR/.xinitrc" "$HOME_DIR/.xinitrc.before-nirvana"
    echo "The previous ~/.xinitrc was saved as ~/.xinitrc.before-nirvana"
  fi
  printf '#!/bin/sh\nexec /usr/bin/nirvana-session\n' > "$HOME_DIR/.xinitrc"
  chown "$USER_NAME" "$HOME_DIR/.xinitrc"
  echo "$HOME_DIR/.xinitrc starts Nirvana Desktop - just type startx."
fi

step "Done"
echo "Nirvana Desktop is installed ($(dpkg-query -W -f '${Version}' nirvana))."
dm=
for d in lightdm sddm gdm3 slim lxdm xdm wdm; do [ -x "/etc/init.d/$d" ] && dm=$d; done
if [ -n "$dm" ]; then
  echo "Log out and choose the \"Nirvana Desktop\" session on the login screen ($dm)."
else
  echo "Start it from the console:  startx /usr/bin/nirvana-session"
  echo "(or install a login screen: sudo $0 --login-screen)"
fi
echo "To remove it:               sudo apt remove nirvana"
