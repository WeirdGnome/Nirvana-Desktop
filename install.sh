#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Nirvana Desktop: build and install in one go.
#
#     ./install.sh             build, test, install to /opt/nirvana, set up the system and you
#     ./install.sh --deps      the same, the build's packages installed first (apt or dnf)
#     ./install.sh --no-dwarfs without DwarFS AppImages (libdwarfs is not fetched and built)
#     ./install.sh --no-tests  skip the tests
#     ./install.sh --uninstall remove what an install put in /opt/nirvana
#
# Systems: Debian, Devuan, Ubuntu (apt); Rocky Linux 10, AlmaLinux 10 and the like (dnf,
# with EPEL and CRB, which --deps turns on). What a system does not package is fetched,
# checked against its SHA-256 and built once, then installed to /opt/nirvana/Libs:
#   libdwarfs (DwarFS AppImages) - everywhere;
#   EFL (the desktop's toolkit) and labwc (the Wayland compositor) - where they are not
#   packaged (Rocky Linux 10); EFL takes from a quarter to an hour to build.
# Run as yourself; root (sudo) is asked for only where it must be.

set -eu

DWARFS_VERSION=0.15.10
DWARFS_SHA256=036109bc210b5afa593a005f798918aacdd412a3d9dbf703bed2e70477454027
DWARFS_URLS="https://github.com/mhx/dwarfs/releases/download/v$DWARFS_VERSION/dwarfs-$DWARFS_VERSION.tar.xz"

EFL_VERSION=1.28.1
EFL_SHA256=84cf6145f9cc82bfff690005be24392c8f3c52f8e00ff04d8eea371429c09424
EFL_URLS="https://download.enlightenment.org/rel/libs/efl/efl-$EFL_VERSION.tar.xz
http://archive.ubuntu.com/ubuntu/pool/universe/e/efl/efl_$EFL_VERSION.orig.tar.xz"

LABWC_VERSION=0.9.3
LABWC_SHA256=deda3c37098b03eccbe4cc82662efde96570981910f1ae910f18156cbbaa231e
LABWC_URLS="http://archive.ubuntu.com/ubuntu/pool/universe/l/labwc/labwc_$LABWC_VERSION.orig.tar.xz"

WLROOTS_VERSION=0.19.2
WLROOTS_SHA256=804bd3eab10f03556628c4e977351b0b1b17bc34084342dfaff5086c54ce1fa0
WLROOTS_URLS="http://archive.ubuntu.com/ubuntu/pool/universe/w/wlroots/wlroots_$WLROOTS_VERSION.orig.tar.bz2"

HERE=$(cd "$(dirname "$0")" && pwd)
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/nirvana-desktop"
PREFIX=${NIRVANA:-/opt/nirvana}
EFL_PREFIX="$PREFIX/Libs/efl"
LABWC_PREFIX="$PREFIX/Libs/labwc"
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
        -h|--help) sed -n '3,18p' "$0" | sed 's/^#//; s/^ //'; exit 0 ;;
        *) echo "install.sh: unknown option $a" >&2; exit 1 ;;
    esac
done

say() { printf '\033[1m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[1;33m==> %s\033[0m\n' "$*" >&2; }
die() { printf '\033[1;31m==> %s\033[0m\n' "$*" >&2; exit 1; }

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

# ---------------------------------------------------------------- the system

# apt (Debian and its kin) or dnf (Rocky Linux and its kin)
OS_ID=unknown
OS_LIKE=""
OS_VERSION=""
if [ -r /etc/os-release ]; then
    OS_ID=$(. /etc/os-release && echo "${ID:-unknown}")
    OS_LIKE=$(. /etc/os-release && echo "${ID_LIKE:-}")
    OS_VERSION=$(. /etc/os-release && echo "${VERSION_ID:-}")
fi
case " $OS_ID $OS_LIKE " in
    *" rhel "*|*" centos "*|*" fedora "*|*" rocky "*|*" almalinux "*) FAMILY=rpm ;;
    *" debian "*|*" ubuntu "*|*" devuan "*) FAMILY=deb ;;
    *) if command -v dnf > /dev/null; then FAMILY=rpm; elif command -v apt-get > /dev/null; then FAMILY=deb; else FAMILY=other; fi ;;
esac

# what was built here and must be found by the build and at run time
export PKG_CONFIG_PATH="$EFL_PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export PATH="$EFL_PREFIX/bin:$PATH"

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
EFL_SRC="$CACHE/efl-$EFL_VERSION"
LABWC_SRC="$CACHE/labwc-$LABWC_VERSION"
LDCONF=/etc/ld.so.conf.d/nirvana-built.conf

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
    # (EFL and labwc built here live in their own directories)
    if [ -d "$LABWC_PREFIX" ]; then
        say "Removing labwc"
        [ -L "$PREFIX/C/labwc" ] && $SUDO rm -f "$PREFIX/C/labwc"
        $SUDO rm -rf "$LABWC_PREFIX"
    fi
    if [ -d "$EFL_PREFIX" ]; then
        say "Removing EFL"
        $SUDO rm -rf "$EFL_PREFIX"
    fi
    [ -L /nirvana ] && $SUDO rm -f /nirvana
    $SUDO rm -f /etc/ld.so.conf.d/nirvana.conf "$LDCONF" \
        /usr/share/wayland-sessions/nirvana.desktop /usr/share/xsessions/nirvana-x11.desktop
    $SUDO ldconfig 2> /dev/null || true
    say "Done (your ~/.nirvana was left alone)"
    exit 0
fi

# ---------------------------------------------------------------- packages

# dnf: the packages that are there installed, the others left out (they are named after)
dnf_install() {
    if dnf --version 2> /dev/null | grep -qi dnf5; then
        $SUDO dnf install -y --skip-unavailable "$@" || true
    else
        $SUDO dnf install -y --setopt=strict=0 "$@" || true
    fi
    missing=""
    for p in "$@"; do
        rpm -q --whatprovides "$p" > /dev/null 2>&1 || missing="$missing $p"
    done
    [ -z "$missing" ] || warn "Not installed (not in the enabled repositories):$missing"
}

deps_deb() {
    say "Installing the build's packages"
    $SUDO apt-get install -y meson ninja-build pkg-config gcc g++ gettext git \
        libarchive-dev zlib1g-dev liblzma-dev libzstd-dev liblz4-dev \
        libefl-all-dev libwayland-dev wayland-protocols squashfs-tools libarchive-tools \
        labwc curl xz-utils bzip2 libgl1-mesa-dri libgbm1 dbus
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
}

deps_rpm() {
    # EPEL (luajit, some of libdwarfs' libraries) and CRB (the -devel packages)
    say "Turning on the EPEL and CRB repositories"
    if ! rpm -q epel-release > /dev/null 2>&1; then
        $SUDO dnf install -y epel-release ||
            $SUDO dnf install -y "https://dl.fedoraproject.org/pub/epel/epel-release-latest-${OS_VERSION%%.*}.noarch.rpm" ||
            warn "EPEL could not be turned on: luajit and libdwarfs' libraries will be missing"
    fi
    $SUDO dnf install -y dnf-plugins-core > /dev/null 2>&1 || true
    if command -v crb > /dev/null; then
        $SUDO crb enable || true
    else
        $SUDO dnf config-manager --set-enabled crb 2> /dev/null ||
            $SUDO dnf config-manager setopt crb.enabled=1 2> /dev/null ||
            warn "The CRB repository could not be turned on"
    fi

    say "Installing the build's packages"
    dnf_install meson ninja-build pkgconf-pkg-config gcc gcc-c++ gettext gettext-devel git \
        curl xz bzip2 tar findutils python3 \
        libarchive-devel zlib-devel xz-devel libzstd-devel lz4-devel \
        wayland-devel wayland-protocols-devel squashfs-tools bsdtar dbus-daemon

    say "Installing the packages EFL needs"
    dnf_install openssl-devel libjpeg-turbo-devel libpng-devel libtiff-devel giflib-devel \
        libwebp-devel freetype-devel fontconfig-devel fribidi-devel harfbuzz-devel \
        luajit-devel dbus-devel systemd-devel libmount-devel libunwind-devel glib2-devel \
        libX11-devel libXcursor-devel libXcomposite-devel libXdamage-devel libXext-devel \
        libXfixes-devel libXinerama-devel libXrandr-devel libXrender-devel libXtst-devel \
        libXScrnSaver-devel libXi-devel libxcb-devel libxkbcommon-devel libxkbcommon-x11-devel \
        libglvnd-devel mesa-libEGL-devel mesa-libGLES-devel mesa-libgbm-devel libdrm-devel \
        libinput-devel

    say "Installing the packages labwc needs"
    dnf_install libxml2-devel cairo-devel pango-devel pixman-devel hwdata-devel \
        libdisplay-info-devel libseat-devel lcms2-devel xcb-util-wm-devel \
        xcb-util-renderutil-devel xcb-util-devel xorg-x11-server-Xwayland \
        xorg-x11-server-Xwayland-devel mesa-dri-drivers mesa-libgbm

    # the global menu's module for GTK programs (run-time)
    dnf_install appmenu-gtk3-module ||
        true

    if [ $dwarfs -eq 1 ]; then
        say "Installing the packages libdwarfs needs"
        dnf_install cmake boost-devel fmt-devel xxhash-devel brotli-devel \
            parallel-hashmap-devel range-v3-devel utf8cpp-devel flac-devel file-devel \
            libacl-devel json-devel date-devel
    fi
}

if [ $deps -eq 1 ]; then
    case $FAMILY in
        deb) deps_deb ;;
        rpm) deps_rpm ;;
        *) warn "No apt or dnf here: install the build's packages yourself (see README.md)" ;;
    esac
fi

for tool in meson ninja pkg-config cc; do
    if ! command -v $tool > /dev/null; then
        echo "install.sh: $tool is missing (./install.sh --deps installs what is needed)" >&2
        exit 1
    fi
done

# ---------------------------------------------------------------- downloads

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

# A source's tarball in the cache, checked: get_source NAME FILE SHA256 "URLS"; the first
# address whose download matches the checksum is used. A tarball put in the cache by hand
# (without a network) is used when it matches.
get_source() {
    file="$CACHE/$2"
    if [ -f "$file" ] && echo "$3  $file" | sha256sum -c --quiet - 2> /dev/null; then
        return 0
    fi
    rm -f "$file"
    for url in $4; do
        say "Downloading $1 ($url)"
        if fetch "$url" "$file.part" && echo "$3  $file.part" | sha256sum -c --quiet - 2> /dev/null; then
            mv "$file.part" "$file"
            return 0
        fi
        warn "That download failed or does not match its checksum"
        rm -f "$file.part"
    done
    warn "$1 could not be fetched; you can put $2 in $CACHE yourself"
    return 1
}

need_tools() {
    for tool in "$@"; do
        if ! command -v "$tool" > /dev/null; then
            warn "$tool is missing (./install.sh --deps installs it)"
            return 1
        fi
    done
}

ldconf_add() {
    if ! grep -qx "$1" "$LDCONF" 2> /dev/null; then
        echo "$1" | $SUDO tee -a "$LDCONF" > /dev/null
    fi
    $SUDO ldconfig
}

# ---------------------------------------------------------------- EFL

# Built when the system has no EFL of 1.26 or later (Rocky Linux 10 has none).
build_efl() {
    need_tools xz sha256sum || return 1
    get_source EFL "efl-$EFL_VERSION.tar.xz" "$EFL_SHA256" "$EFL_URLS" || return 1
    if [ ! -f "$EFL_SRC/meson.build" ]; then
        rm -rf "$EFL_SRC"
        tar -xJf "$CACHE/efl-$EFL_VERSION.tar.xz" -C "$CACHE"
    fi
    lua=lua
    pkg-config --exists luajit && lua=luajit
    say "Building EFL $EFL_VERSION (once; this takes from a quarter to an hour)"
    rm -rf "$EFL_SRC/build"
    meson setup "$EFL_SRC/build" "$EFL_SRC" --prefix="$EFL_PREFIX" --libdir=lib \
        --buildtype=release -Dlua-interpreter=$lua \
        -Dx11=true -Dwl=true -Dopengl=es-egl -Ddrm=false -Dfb=false -Dphysics=false \
        -Daudio=false -Dpulseaudio=false -Dgstreamer=false -Davahi=false -Dv4l2=false \
        -Dedje-sound-and-video=false -Dxpresent=false \
        -Dbuild-tests=false -Dbuild-examples=false \
        -Decore-imf-loaders-disabler=ibus,scim \
        -Devas-loaders-disabler=gst,pdf,ps,raw,rsvg,xcf,json,avif,heif,jxl,jp2k || return 1
    ninja -C "$EFL_SRC/build" -j "$NPROC" || return 1
    say "Installing EFL to $EFL_PREFIX"
    $SUDO ninja -C "$EFL_SRC/build" install || return 1
    ldconf_add "$EFL_PREFIX/lib"
    $SUDO sh -c "echo $EFL_VERSION > '$EFL_PREFIX/nirvana-built-version'"
}

if pkg-config --atleast-version=1.26 elementary 2> /dev/null &&
    [ ! -f "$EFL_PREFIX/nirvana-built-version" ]; then
    :
elif [ "$(cat "$EFL_PREFIX/nirvana-built-version" 2> /dev/null)" = "$EFL_VERSION" ]; then
    say "EFL $EFL_VERSION is built already"
    ldconf_add "$EFL_PREFIX/lib"
else
    [ $FAMILY = deb ] && warn "No EFL packages found (./install.sh --deps installs them); building EFL"
    build_efl || die "EFL could not be built (the reason is above); the desktop needs it"
fi

# ---------------------------------------------------------------- labwc

# Built when the system has none (Rocky Linux 10), with wlroots inside it; what wlroots
# needs and the system lacks (a newer Wayland, libseat...) meson fetches with git.
build_labwc() {
    need_tools bzip2 xz sha256sum git || return 1
    get_source labwc "labwc_$LABWC_VERSION.orig.tar.xz" "$LABWC_SHA256" "$LABWC_URLS" || return 1
    get_source wlroots "wlroots_$WLROOTS_VERSION.orig.tar.bz2" "$WLROOTS_SHA256" "$WLROOTS_URLS" || return 1
    rm -rf "$LABWC_SRC"
    mkdir -p "$LABWC_SRC"
    tar -xJf "$CACHE/labwc_$LABWC_VERSION.orig.tar.xz" -C "$LABWC_SRC" --strip-components=1
    mkdir -p "$LABWC_SRC/subprojects/wlroots"
    tar -xjf "$CACHE/wlroots_$WLROOTS_VERSION.orig.tar.bz2" -C "$LABWC_SRC/subprojects/wlroots" \
        --strip-components=1
    # (wlroots' own fallbacks made known to the build)
    for w in "$LABWC_SRC"/subprojects/wlroots/subprojects/*.wrap; do
        [ -e "$LABWC_SRC/subprojects/$(basename "$w")" ] || cp "$w" "$LABWC_SRC/subprojects/"
    done
    say "Building labwc $LABWC_VERSION with wlroots $WLROOTS_VERSION (once)"
    meson setup "$LABWC_SRC/build" "$LABWC_SRC" --prefix="$LABWC_PREFIX" --libdir=lib \
        --buildtype=release --default-library=static --wrap-mode=default \
        -Dman-pages=disabled -Dsvg=disabled -Dicon=disabled -Dnls=disabled \
        -Dwlroots:renderers=gles2 -Dwlroots:backends=drm,libinput \
        -Dwlroots:xwayland=auto -Dwlroots:libliftoff=disabled || return 1
    ninja -C "$LABWC_SRC/build" -j "$NPROC" || return 1
    say "Installing labwc to $LABWC_PREFIX"
    $SUDO meson install -C "$LABWC_SRC/build" --no-rebuild || return 1
    $SUDO mkdir -p "$PREFIX/C"
    $SUDO ln -sf "$LABWC_PREFIX/bin/labwc" "$PREFIX/C/labwc"
    $SUDO sh -c "echo $LABWC_VERSION > '$LABWC_PREFIX/nirvana-built-version'"
}

if [ "$(cat "$LABWC_PREFIX/nirvana-built-version" 2> /dev/null)" = "$LABWC_VERSION" ]; then
    say "labwc $LABWC_VERSION is built already"
elif ! command -v labwc > /dev/null; then
    build_labwc || warn "labwc could not be built: no Wayland session (the reason is above)"
fi

# ---------------------------------------------------------------- libdwarfs

build_dwarfs() {
    need_tools cmake c++ xz sha256sum || return 1
    get_source libdwarfs "dwarfs-$DWARFS_VERSION.tar.xz" "$DWARFS_SHA256" "$DWARFS_URLS" || return 1
    if [ ! -f "$DWARFS_SRC/CMakeLists.txt" ]; then
        rm -rf "$DWARFS_SRC"
        tar -xJf "$CACHE/dwarfs-$DWARFS_VERSION.tar.xz" -C "$CACHE"
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
# (SELinux: the new files get their labels)
if command -v restorecon > /dev/null; then
    $SUDO restorecon -R "$PREFIX" /usr/share/wayland-sessions 2> /dev/null || true
fi

say "Setting up your account"
NIRVANA="$PREFIX" sh "$SRC/setup-env.sh" --user

say "Done"
echo
echo "  Wayland: choose \"Nirvana Desktop\" in your login manager, or run"
echo "           $PREFIX/C/nirvana-session"
if [ -f /usr/share/xsessions/nirvana-x11.desktop ]; then
    echo "  X11:     choose \"Nirvana Desktop (X11)\", or put this in ~/.xinitrc:"
    echo "           exec $PREFIX/C/nirvana-session --x11"
fi
echo
echo "  The commands are in $PREFIX/C (assign, wbrun, appimage-info)."
