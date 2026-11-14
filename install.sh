#!/bin/sh
# Wanderer for X11 - the installer.
#
#   ./install.sh                 builds and installs into /usr/local, adds "Wanderer" to the
#                                sessions of the display manager (/usr/share/xsessions)
#   ./install.sh --prefix DIR    installs into DIR
#   ./install.sh --no-deps       does not install the packages needed for building
#   ./install.sh --uninstall     removes what an earlier installation put in place
#
# Licensed under the AROS Public License, version 1.1.
set -e

PREFIX=/usr/local
DEPS=1
UNINSTALL=0
while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) PREFIX="$2"; shift 2 ;;
        --prefix=*) PREFIX="${1#--prefix=}"; shift ;;
        --no-deps) DEPS=0; shift ;;
        --uninstall) UNINSTALL=1; shift ;;
        -h|--help) sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done

SRC=$(cd "$(dirname "$0")" && pwd)
BUILD="$SRC/build"
SUDO=""
[ "$(id -u)" -ne 0 ] && command -v sudo >/dev/null 2>&1 && SUDO=sudo

say() { printf '\033[1m==> %s\033[0m\n' "$*"; }

if [ "$UNINSTALL" -eq 1 ]; then
    if [ ! -f "$BUILD/install_manifest_wanderer.txt" ]; then
        echo "No installation found (build/install_manifest_wanderer.txt is missing)." >&2
        exit 1
    fi
    say "Removing the installed files"
    while IFS= read -r f; do
        [ -n "$f" ] && $SUDO rm -f "$f" && echo "  removed $f"
    done < "$BUILD/install_manifest_wanderer.txt"
    say "Done. Wanderer is no longer among the sessions."
    exit 0
fi

# --- the packages needed for building (Debian, Devuan, Ubuntu and relatives) ------------------
if [ "$DEPS" -eq 1 ] && command -v apt-get >/dev/null 2>&1; then
    PKGS="build-essential cmake git pkg-config libx11-dev libxft-dev libxext-dev libxinerama-dev \
libxcursor-dev libxfixes-dev libxrender-dev libfontconfig1-dev libfreetype-dev x11-utils xdg-utils"
    MISSING=""
    for p in $PKGS; do
        dpkg -s "$p" >/dev/null 2>&1 || MISSING="$MISSING $p"
    done
    if [ -n "$MISSING" ]; then
        say "Installing the packages needed for building:$MISSING"
        $SUDO apt-get update
        $SUDO apt-get install -y $MISSING
    fi
fi

# --- building ---------------------------------------------------------------------------------
say "Building (FLTK 1.4 is downloaded and built with it if the system has none)"
cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build "$BUILD" -j"$(nproc 2>/dev/null || echo 2)" --target wanderer wanderer-wm

# --- installing -------------------------------------------------------------------------------
say "Installing into $PREFIX"
$SUDO cmake --install "$BUILD" --component wanderer

# the display manager starts the session by the name in the .desktop file: it has to be found
# in the PATH the display manager gives the session
if [ "$PREFIX" != /usr ] && [ "$PREFIX" != /usr/local ]; then
    echo "Note: $PREFIX/bin has to be in the session's PATH for the display manager to start Wanderer."
fi

say "Done."
echo "Log out and choose \"Wanderer\" from the sessions of the login screen."
echo "To try it inside a running X session without logging out (another window manager must not run):"
echo "    wanderer-wm & wanderer --backdrop"
