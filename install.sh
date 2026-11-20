#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Nirvana Desktop: build and install in one go.
#
#     ./install.sh             build, test, install to /opt/nirvana, set up the system and you
#     ./install.sh --deps      the same, the build's packages installed first (apt)
#     ./install.sh --no-dwarfs without DwarFS AppImages (libdwarfs is not fetched and built)
#     ./install.sh --no-tests  skip the tests
#     ./install.sh --uninstall remove what an install put in /opt/nirvana
#
# libdwarfs (for DwarFS AppImages) is not packaged by Devuan or Debian: its release is
# downloaded from GitHub, checked against its SHA-256 and built (a few minutes up to half
# an hour, once), then installed to /opt/nirvana/Libs. Run as yourself; root (sudo) is
# asked for only where it must be.

set -eu

DWARFS_VERSION=0.15.10
DWARFS_SHA256=036109bc210b5afa593a005f798918aacdd412a3d9dbf703bed2e70477454027
DWARFS_URL="https://github.com/mhx/dwarfs/releases/download/v$DWARFS_VERSION/dwarfs-$DWARFS_VERSION.tar.xz"

HERE=$(cd "$(dirname "$0")" && pwd)
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/nirvana-desktop"
PREFIX=${NIRVANA:-/opt/nirvana}
deps=0
tests=1
uninstall=0
dwarfs=1

for a in "$@"; do
    case "$a" in
        --deps) deps=1 ;;
        --no-tests) tests=0 ;;
        --no-dwarfs) dwarfs=0 ;;
        --uninstall) uninstall=1 ;;
        -h|--help) sed -n '3,15p' "$0" | sed 's/^#//; s/^ //'; exit 0 ;;
        *) echo "install.sh: unknown option $a" >&2; exit 1 ;;
    esac
done

say() { printf '\033[1m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[1;33m==> %s\033[0m\n' "$*" >&2; }

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
mkdir -p "$CACHE"
NPROC=$(nproc 2> /dev/null || echo 2)

# ---------------------------------------------------------------- where to build

# The sources: files dated in the future (a clock behind the one that packed them) make
# meson and ninja stop; they are given the present time - in a copy when the sources are
# not ours to change.
SRC="$HERE"
stamp=$(mktemp)
if [ -n "$(find "$HERE" -newer "$stamp" -print -quit 2> /dev/null)" ]; then
    warn "Some files are dated in the future (is the clock right?); they get today's date"
    if [ -w "$HERE" ] && find "$HERE" -newer "$stamp" -exec touch -c {} + 2> /dev/null; then
        :
    else
        SRC="$CACHE/src"
        rm -rf "$SRC"
        cp -R "$HERE" "$SRC"
        rm -rf "$SRC/build"
    fi
fi
rm -f "$stamp"

# The build beside the sources, or in the cache when they are not ours to write in.
BUILD="$SRC/build"
if [ ! -w "$SRC" ] || { [ -e "$BUILD" ] && [ ! -w "$BUILD" ]; }; then
    BUILD="$CACHE/build"
fi

DWARFS_SRC="$CACHE/dwarfs-$DWARFS_VERSION"
DWARFS_CMAKE="$PREFIX/Libs/cmake/dwarfs"

# ---------------------------------------------------------------- uninstall

if [ $uninstall -eq 1 ]; then
    found=0
    for b in "$HERE/build" "$CACHE/build"; do
        if [ -f "$b/meson-logs/install-log.txt" ]; then
            say "Removing the installed files"
            $SUDO ninja -C "$b" uninstall
            found=1
            break
        fi
    done
    [ $found -eq 1 ] || warn "No install log found; Nirvana's own files were not removed"
    if [ -f "$DWARFS_SRC/build/install_manifest.txt" ]; then
        say "Removing libdwarfs"
        tr '\n' '\0' < "$DWARFS_SRC/build/install_manifest.txt" | $SUDO xargs -0 rm -f
    fi
    [ -L /nirvana ] && $SUDO rm -f /nirvana
    $SUDO rm -f /etc/ld.so.conf.d/nirvana.conf /usr/share/wayland-sessions/nirvana.desktop \
        /usr/share/xsessions/nirvana-x11.desktop
    $SUDO ldconfig 2> /dev/null || true
    say "Done (your ~/.nirvana was left alone)"
    exit 0
fi

# ---------------------------------------------------------------- packages

if [ $deps -eq 1 ]; then
    say "Installing the build's packages"
    $SUDO apt-get install -y meson ninja-build pkg-config gcc g++ gettext \
        libarchive-dev zlib1g-dev liblzma-dev libzstd-dev liblz4-dev \
        libefl-all-dev libwayland-dev wayland-protocols squashfs-tools libarchive-tools \
        labwc curl xz-utils libgl1-mesa-dri libgbm1
    # the global menu's modules for GTK programs (run-time; not every release has both)
    $SUDO apt-get install -y appmenu-gtk3-module appmenu-gtk2-module ||
        $SUDO apt-get install -y appmenu-gtk3-module ||
        warn "appmenu-gtk3-module could not be installed: GTK programs keep their menus in their windows"
    if [ $dwarfs -eq 1 ]; then
        say "Installing the packages libdwarfs needs"
        $SUDO apt-get install -y cmake libboost-chrono-dev libboost-filesystem-dev \
            libboost-program-options-dev libfmt-dev libxxhash-dev libssl-dev libbrotli-dev \
            libparallel-hashmap-dev librange-v3-dev libutfcpp-dev libflac++-dev libmagic-dev \
            libacl1-dev nlohmann-json3-dev libhowardhinnant-date-dev ||
            warn "Some of libdwarfs' packages could not be installed"
    fi
fi

for tool in meson ninja pkg-config cc; do
    if ! command -v $tool > /dev/null; then
        echo "install.sh: $tool is missing (./install.sh --deps installs what is needed)" >&2
        exit 1
    fi
done

# ---------------------------------------------------------------- libdwarfs

fetch() {
    if command -v curl > /dev/null; then
        curl -fL --retry 3 -o "$2" "$1"
    elif command -v wget > /dev/null; then
        wget -O "$2" "$1"
    else
        echo "neither curl nor wget is installed" >&2
        return 1
    fi
}

build_dwarfs() {
    for tool in cmake c++ xz sha256sum; do
        if ! command -v $tool > /dev/null; then
            warn "$tool is missing: no DwarFS AppImages (./install.sh --deps installs it)"
            return 1
        fi
    done
    tarball="$CACHE/dwarfs-$DWARFS_VERSION.tar.xz"
    if [ ! -f "$tarball" ] || ! echo "$DWARFS_SHA256  $tarball" | sha256sum -c --quiet - 2> /dev/null; then
        say "Downloading libdwarfs $DWARFS_VERSION"
        rm -f "$tarball"
        fetch "$DWARFS_URL" "$tarball.part" || return 1
        if ! echo "$DWARFS_SHA256  $tarball.part" | sha256sum -c --quiet -; then
            warn "The download of libdwarfs does not match its checksum; it is not used"
            rm -f "$tarball.part"
            return 1
        fi
        mv "$tarball.part" "$tarball"
    fi
    if [ ! -f "$DWARFS_SRC/CMakeLists.txt" ]; then
        rm -rf "$DWARFS_SRC"
        tar -xJf "$tarball" -C "$CACHE"
    fi
    say "Building libdwarfs $DWARFS_VERSION (once; this takes a while)"
    cmake -S "$DWARFS_SRC" -B "$DWARFS_SRC/build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=Libs \
        -DCMAKE_INSTALL_INCLUDEDIR=include -DCMAKE_INSTALL_RPATH="$PREFIX/Libs" \
        -DDWARFS_CMAKE_INSTALL_DIR=Libs/cmake/dwarfs \
        -DWITH_LIBDWARFS=ON -DWITH_TOOLS=OFF -DWITH_FUSE_DRIVER=OFF -DWITH_TESTS=OFF \
        -DWITH_DESKTOP_INTEGRATION=OFF -DWITH_MAN_OPTION=OFF -DENABLE_PERFMON=OFF || return 1
    ninja -C "$DWARFS_SRC/build" -j "$NPROC" || return 1
    say "Installing libdwarfs to $PREFIX/Libs"
    $SUDO cmake --install "$DWARFS_SRC/build" || return 1
    $SUDO sh -c "echo $DWARFS_VERSION > '$DWARFS_CMAKE/nirvana-built-version'"
}

dwarfs_opts="-Ddwarfs=disabled"
if [ $dwarfs -eq 1 ]; then
    if [ "$(cat "$DWARFS_CMAKE/nirvana-built-version" 2> /dev/null)" = "$DWARFS_VERSION" ]; then
        say "libdwarfs $DWARFS_VERSION is installed already"
        dwarfs_opts="-Ddwarfs=enabled -Dcmake_prefix_path=$DWARFS_CMAKE"
    elif build_dwarfs; then
        dwarfs_opts="-Ddwarfs=enabled -Dcmake_prefix_path=$DWARFS_CMAKE"
    else
        warn "libdwarfs could not be built: Nirvana is built without DwarFS AppImages"
        warn "(the other AppImages - SquashFS, ISO 9660 - work as before)"
    fi
fi

# ---------------------------------------------------------------- Nirvana

say "Configuring (prefix $PREFIX, build in $BUILD)"
if [ -f "$BUILD/meson-private/coredata.dat" ]; then
    # shellcheck disable=SC2086
    meson setup --reconfigure "$BUILD" "$SRC" -Dprefix="$PREFIX" $dwarfs_opts
else
    rm -rf "$BUILD"
    # shellcheck disable=SC2086
    meson setup "$BUILD" "$SRC" -Dprefix="$PREFIX" $dwarfs_opts
fi

say "Building"
ninja -C "$BUILD" -j "$NPROC"

if [ $tests -eq 1 ]; then
    say "Testing"
    meson test -C "$BUILD" --print-errorlogs
fi

say "Installing to $PREFIX"
$SUDO ninja -C "$BUILD" install

say "Setting up the system"
$SUDO env NIRVANA="$PREFIX" sh "$SRC/setup-env.sh" --system

say "Setting up your account"
NIRVANA="$PREFIX" sh "$SRC/setup-env.sh" --user

say "Done"
cat <<EOF

  Wayland: choose "Nirvana Desktop" in your login manager, or run
           $PREFIX/C/nirvana-session
  X11:     put this in ~/.xinitrc:  exec $PREFIX/C/nirvana-session --x11

  The commands are in $PREFIX/C (assign, wbrun, appimage-info).
EOF
