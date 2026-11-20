#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Nirvana Desktop: build and install in one go.
#
#     ./install.sh            build, test, install to /opt/nirvana, set up the system and you
#     ./install.sh --deps     the same, the build's packages installed first (apt)
#     ./install.sh --no-tests skip the tests
#     ./install.sh --uninstall remove what an install put in /opt/nirvana
#
# Run it as yourself; it asks for root (sudo) only where it must.

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
# the build goes beside the sources; when they are not yours to write in (unpacked as
# root, say), into ~/.cache instead
BUILD="$HERE/build"
if [ ! -w "$HERE" ] || { [ -e "$BUILD" ] && [ ! -w "$BUILD" ]; }; then
    BUILD="${XDG_CACHE_HOME:-$HOME/.cache}/nirvana-desktop/build"
fi
PREFIX=${NIRVANA:-/opt/nirvana}
deps=0
tests=1
uninstall=0

for a in "$@"; do
    case "$a" in
        --deps) deps=1 ;;
        --no-tests) tests=0 ;;
        --uninstall) uninstall=1 ;;
        -h|--help) sed -n '3,11p' "$0" | sed 's/^#//; s/^ //'; exit 0 ;;
        *) echo "install.sh: unknown option $a" >&2; exit 1 ;;
    esac
done

say() { printf '\033[1m==> %s\033[0m\n' "$*"; }

if [ "$(id -u)" -eq 0 ]; then
    echo "install.sh: run it as your own user (it uses sudo where needed), so that your" >&2
    echo "            ~/.nirvana is set up for you, not for root." >&2
    exit 1
elif command -v sudo > /dev/null; then
    SUDO=sudo
else
    echo "install.sh: sudo is needed to install to $PREFIX" >&2
    exit 1
fi

if [ $uninstall -eq 1 ]; then
    if [ -f "$BUILD/meson-logs/install-log.txt" ]; then
        say "Removing the installed files"
        $SUDO ninja -C "$BUILD" uninstall
    else
        echo "install.sh: no install log in $BUILD; nothing removed" >&2
    fi
    [ -L /nirvana ] && $SUDO rm -f /nirvana
    $SUDO rm -f /etc/ld.so.conf.d/nirvana.conf /usr/share/wayland-sessions/nirvana.desktop
    $SUDO ldconfig 2> /dev/null || true
    say "Done (your ~/.nirvana was left alone)"
    exit 0
fi

if [ $deps -eq 1 ]; then
    say "Installing the build's packages"
    $SUDO apt-get install -y meson ninja-build pkg-config gcc g++ gettext \
        libarchive-dev zlib1g-dev liblzma-dev libzstd-dev liblz4-dev \
        libefl-all-dev libwayland-dev wayland-protocols squashfs-tools libarchive-tools \
        labwc
fi

for tool in meson ninja pkg-config cc; do
    if ! command -v $tool > /dev/null; then
        echo "install.sh: $tool is missing (./install.sh --deps installs what is needed)" >&2
        exit 1
    fi
done

mkdir -p "$(dirname "$BUILD")"
say "Configuring (prefix $PREFIX, build in $BUILD)"
if [ -d "$BUILD" ]; then
    meson setup --reconfigure "$BUILD" "$HERE" -Dprefix="$PREFIX"
else
    meson setup "$BUILD" "$HERE" -Dprefix="$PREFIX"
fi

say "Building"
ninja -C "$BUILD"

if [ $tests -eq 1 ]; then
    say "Testing"
    meson test -C "$BUILD" --print-errorlogs
fi

say "Installing to $PREFIX"
$SUDO ninja -C "$BUILD" install

say "Setting up the system"
$SUDO env NIRVANA="$PREFIX" sh "$HERE/setup-env.sh" --system

say "Setting up your account"
NIRVANA="$PREFIX" sh "$HERE/setup-env.sh" --user

say "Done"
cat <<EOF

  Wayland: choose "Nirvana Desktop" in your login manager, or run
           $PREFIX/C/nirvana-session
  X11:     put this in ~/.xinitrc:  exec $PREFIX/C/nirvana-session --x11

  The commands are in $PREFIX/C (assign, wbrun, appimage-info).
EOF
