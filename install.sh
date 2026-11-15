#!/bin/sh
# Rambler for X11 - the installer. It builds a Debian package and installs it.
#
#   ./install.sh                 builds rambler-x11_VERSION_ARCH.deb and installs it;
#                                files of an earlier installation that is not from the
#                                package (make install, an older install.sh) are removed
#   ./install.sh --package-only  only builds the package (into this directory)
#   ./install.sh --no-deps       does not install the packages needed for building
#   ./install.sh --uninstall     removes the package and any files installed without it
#
# The package puts the programs into /usr/bin and the session into /usr/share/xsessions,
# so "Rambler" appears among the sessions of the display manager.
#
# Licensed under the AROS Public License, version 1.1.
set -e

DEPS=1
PACKAGE_ONLY=0
UNINSTALL=0
while [ $# -gt 0 ]; do
    case "$1" in
        --package-only) PACKAGE_ONLY=1; shift ;;
        --no-deps) DEPS=0; shift ;;
        --uninstall) UNINSTALL=1; shift ;;
        -h|--help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done

SRC=$(cd "$(dirname "$0")" && pwd)
BUILD="$SRC/build"
PKGNAME=rambler-x11
VERSION=$(head -n1 "$SRC/VERSION")
SUDO=""
[ "$(id -u)" -ne 0 ] && command -v sudo >/dev/null 2>&1 && SUDO=sudo

say() { printf '\033[1m==> %s\033[0m\n' "$*"; }

if ! command -v dpkg-deb >/dev/null 2>&1; then
    echo "This installer builds a Debian package and needs dpkg (Debian, Devuan, Ubuntu ...)." >&2
    exit 1
fi
ARCH=$(dpkg --print-architecture)
DEB="$SRC/${PKGNAME}_${VERSION}_${ARCH}.deb"

# --- files of an installation that is not from the package -----------------------------------
# The places an earlier install.sh (cmake --install into /usr/local) or a manual build put
# them; a file is removed only if no installed package owns it.
remove_unpackaged() {
    FOUND=""
    for f in /usr/local/bin/rambler /usr/local/bin/rambler-wm /usr/local/bin/rambler-session \
             /usr/bin/rambler /usr/bin/rambler-wm /usr/bin/rambler-session \
             /usr/share/xsessions/rambler.desktop /usr/local/share/xsessions/rambler.desktop \
             /usr/local/share/doc/rambler-x11/LICENSE /usr/local/share/doc/rambler-x11/README.md \
             /usr/local/bin/wanderer /usr/local/bin/wanderer-wm /usr/local/bin/wanderer-session \
             /usr/bin/wanderer /usr/bin/wanderer-wm /usr/bin/wanderer-session \
             /usr/share/xsessions/wanderer.desktop /usr/local/share/xsessions/wanderer.desktop \
             /usr/local/share/doc/wanderer-x11/LICENSE /usr/local/share/doc/wanderer-x11/README.md; do
        [ -e "$f" ] || continue
        dpkg -S "$f" >/dev/null 2>&1 && continue   # belongs to a package: left alone
        FOUND="$FOUND $f"
    done
    # files listed by an earlier cmake --install of this source tree
    for m in "$BUILD"/install_manifest*.txt; do
        [ -f "$m" ] || continue
        while IFS= read -r f; do
            [ -n "$f" ] && [ -e "$f" ] || continue
            dpkg -S "$f" >/dev/null 2>&1 && continue
            case " $FOUND " in *" $f "*) ;; *) FOUND="$FOUND $f" ;; esac
        done < "$m"
        rm -f "$m"
    done
    if [ -n "$FOUND" ]; then
        say "Removing an installation that is not from the package"
        for f in $FOUND; do $SUDO rm -f "$f" && echo "  removed $f"; done
        $SUDO rmdir /usr/local/share/doc/rambler-x11 /usr/local/share/doc/wanderer-x11 2>/dev/null || true
    fi
}

# the package of the earlier name (wanderer-x11): the new one replaces it
remove_old_package() {
    if dpkg -s wanderer-x11 >/dev/null 2>&1; then
        say "Removing the package of the earlier name, wanderer-x11"
        $SUDO apt-get remove -y wanderer-x11 2>/dev/null || $SUDO dpkg -r wanderer-x11
    fi
}

if [ "$UNINSTALL" -eq 1 ]; then
    if dpkg -s "$PKGNAME" >/dev/null 2>&1; then
        say "Removing the package $PKGNAME"
        $SUDO apt-get remove -y "$PKGNAME" 2>/dev/null || $SUDO dpkg -r "$PKGNAME"
    fi
    remove_unpackaged
    say "Done. Rambler is no longer installed."
    exit 0
fi

# --- the packages needed for building --------------------------------------------------------
if [ "$DEPS" -eq 1 ] && command -v apt-get >/dev/null 2>&1; then
    PKGS="build-essential cmake git pkg-config dpkg-dev libx11-dev libxft-dev libxext-dev \
libxinerama-dev libxcursor-dev libxfixes-dev libxrender-dev libfontconfig1-dev libfreetype-dev"
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
say "Building $PKGNAME $VERSION (FLTK 1.4 is downloaded and built in if the system has none)"
cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      -DRAMBLER_XSESSIONS_DIR=/usr/share/xsessions
cmake --build "$BUILD" -j"$(nproc 2>/dev/null || echo 2)" --target rambler rambler-wm

# --- the package ------------------------------------------------------------------------------
say "Making the package"
ROOT="$BUILD/pkgroot"
rm -rf "$ROOT"
DESTDIR="$ROOT" cmake --install "$BUILD" --component rambler >/dev/null
rm -f "$BUILD"/install_manifest*.txt   # (a DESTDIR install is no installation)
strip --strip-unneeded "$ROOT/usr/bin/rambler" "$ROOT/usr/bin/rambler-wm" 2>/dev/null || true

# the libraries the programs need: their packages (dpkg-shlibdeps)
DEPENDS=""
if command -v dpkg-shlibdeps >/dev/null 2>&1; then
    mkdir -p "$BUILD/shlibs/debian"
    echo "Source: $PKGNAME" > "$BUILD/shlibs/debian/control"
    DEPENDS=$(cd "$BUILD/shlibs" && dpkg-shlibdeps --ignore-missing-info -O "$ROOT/usr/bin/rambler" "$ROOT/usr/bin/rambler-wm" 2>/dev/null \
              | sed -n 's/^shlibs:Depends=//p')
fi
SIZE=$(du -sk "$ROOT" | cut -f1)
mkdir -p "$ROOT/DEBIAN"
cat > "$ROOT/DEBIAN/control" <<EOF
Package: $PKGNAME
Version: $VERSION
Architecture: $ARCH
Maintainer: Nirvana Desktop contributors <106549403+WeirdGnome@users.noreply.github.com>
Installed-Size: $SIZE
Depends: ${DEPENDS:+$DEPENDS, }xdg-utils
Recommends: dbus-x11, x11-xserver-utils, x11-utils, xterm, network-manager, squashfs-tools
Suggests: x-terminal-emulator
Section: x11
Priority: optional
Homepage: https://github.com/WeirdGnome/Nirvana-Desktop
Conflicts: wanderer-x11
Replaces: wanderer-x11
Description: Rambler for X11 - a port of the AROS desktop
 Rambler is Wanderer, the desktop and file manager of the AROS Research
 Operating System, ported to Linux and X11, with its own window manager (screens that
 are dragged down by their title bar, window frames with gadgets, menus in
 the screen's title bar) and a session for the display manager.
 .
 Licensed under the AROS Public License 1.1.
EOF
find "$ROOT" -type d -exec chmod 755 {} +
dpkg-deb --root-owner-group --build "$ROOT" "$DEB" >/dev/null
echo "  $DEB"

if [ "$PACKAGE_ONLY" -eq 1 ]; then
    say "Done. Install it with:  sudo apt install $DEB"
    exit 0
fi

# --- installing -------------------------------------------------------------------------------
remove_old_package
remove_unpackaged
say "Installing the package"
if command -v apt-get >/dev/null 2>&1; then
    # apt reads the package as its own user (_apt), who cannot see into a home directory:
    # a copy in a world-readable temporary directory (no "unsandboxed download" notice)
    TMPDEB="$(mktemp -d /tmp/rambler-x11.XXXXXX)"
    cp "$DEB" "$TMPDEB/"
    chmod 755 "$TMPDEB"; chmod 644 "$TMPDEB/$(basename "$DEB")"
    $SUDO apt-get install -y --reinstall "$TMPDEB/$(basename "$DEB")"
    rm -rf "$TMPDEB"
else
    $SUDO dpkg -i "$DEB"
fi

say "Done."
echo "Log out and choose \"Rambler\" from the sessions of the login screen."
echo "To remove it:  ./install.sh --uninstall   (or: sudo apt remove $PKGNAME)"
