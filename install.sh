#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Nirvana Desktop: built, made into packages and installed with them.
#
#     ./install.sh             build, test, make the packages, install them, set you up
#     ./install.sh --deps      the same, the build's packages installed first (apt or dnf)
#     ./install.sh --no-dwarfs without DwarFS AppImages (libdwarfs is not fetched and built)
#     ./install.sh --no-tests  skip the tests
#     ./install.sh --no-install  only make the packages
#     ./install.sh --uninstall remove the packages (and what older installs left)
#
# Nothing is copied into the system by hand: everything goes into packages - .rpm on
# Rocky Linux 10, AlmaLinux 10 and their kin (dnf), .deb on Debian, Devuan, Ubuntu (apt)
# - which the package manager installs, upgrades and removes. They are kept in
# ~/.cache/nirvana-desktop/packages:
#   nirvana-desktop     the desktop, its commands, themes and sessions (/opt/nirvana)
#   nirvana-libdwarfs   libdwarfs, for DwarFS AppImages (no system packages it)
#   nirvana-efl         EFL, the toolkit - where the system has none (Rocky Linux 10)
#   nirvana-labwc       labwc with wlroots, the compositor - where the system has none
# Their sources are fetched, checked against their SHA-256 and built once (EFL takes
# from a few minutes to an hour). Run as yourself; root (sudo) is asked for only to
# install packages.

set -eu
umask 022

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

HOMEPAGE=https://github.com/WeirdGnome/Nirvana-Desktop
MAINTAINER="Nirvana Desktop <106549403+WeirdGnome@users.noreply.github.com>"

HERE=$(cd "$(dirname "$0")" && pwd)
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/nirvana-desktop"
PKGDIR="$CACHE/packages"
PREFIX=${NIRVANA:-/opt/nirvana}
EFL_PREFIX="$PREFIX/Libs/efl"
LABWC_PREFIX="$PREFIX/Libs/labwc"
RELEASE=$(date +%Y%m%d%H%M)
deps=0
tests=1
uninstall=0
dwarfs=1
install=1

for a in "$@"; do
    case "$a" in
        --deps) deps=1 ;;
        --no-tests) tests=0 ;;
        --no-dwarfs) dwarfs=0 ;;
        --no-install) install=0 ;;
        --uninstall) uninstall=1 ;;
        -h|--help) sed -n '3,23p' "$0" | sed 's/^#//; s/^ //'; exit 0 ;;
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
    echo "install.sh: sudo is needed to install the packages" >&2
    exit 1
fi
mkdir -p "$CACHE" "$PKGDIR"
NPROC=$(nproc 2> /dev/null || echo 2)

# ---------------------------------------------------------------- the system

OS_ID=unknown
OS_LIKE=""
OS_VERSION=""
if [ -r /etc/os-release ]; then
    # shellcheck disable=SC1091
    OS_ID=$(. /etc/os-release && echo "${ID:-unknown}")
    # shellcheck disable=SC1091
    OS_LIKE=$(. /etc/os-release && echo "${ID_LIKE:-}")
    # shellcheck disable=SC1091
    OS_VERSION=$(. /etc/os-release && echo "${VERSION_ID:-}")
fi
# (NIRVANA_PKG=rpm or deb: that kind of package made, whatever the system)
case "${NIRVANA_PKG:-} $OS_ID $OS_LIKE " in
    "rpm "*) FAMILY=rpm ;;
    "deb "*) FAMILY=deb ;;
    *" rhel "*|*" centos "*|*" fedora "*|*" rocky "*|*" almalinux "*) FAMILY=rpm ;;
    *" debian "*|*" ubuntu "*|*" devuan "*) FAMILY=deb ;;
    *) if command -v dnf > /dev/null; then FAMILY=rpm
       elif command -v apt-get > /dev/null; then FAMILY=deb
       else die "Neither dnf nor apt here: install.sh makes .rpm or .deb packages only"; fi ;;
esac

# the helpers built here, found by the builds that follow
export PKG_CONFIG_PATH="$EFL_PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export PATH="$EFL_PREFIX/bin:$PATH"

# ---------------------------------------------------------------- packages: installed

# the version of an installed package (without the release), or nothing
pkg_installed_version() {
    if [ $FAMILY = rpm ]; then
        if rpm -q "$1" > /dev/null 2>&1; then rpm -q --qf '%{VERSION}' "$1"; fi
    else
        v=$(dpkg-query -W -f '${db:Status-Abbrev} ${Version}' "$1" 2> /dev/null || true)
        case "$v" in ii*) v=${v##* }; echo "${v%%+*}" | tr '~' '-' ;; esac
    fi
}

pkg_install() {
    say "Installing $(basename "$1")"
    if [ $FAMILY = rpm ]; then
        $SUDO dnf install -y "$1"
    else
        $SUDO apt-get install -y --allow-downgrades "$1"
    fi
}

pkg_remove() {
    list=""
    for p in "$@"; do
        [ -n "$(pkg_installed_version "$p")" ] && list="$list $p"
    done
    [ -n "$list" ] || return 0
    say "Removing the packages$list"
    # shellcheck disable=SC2086
    if [ $FAMILY = rpm ]; then
        $SUDO dnf remove -y $list
    else
        $SUDO apt-get remove -y $list
    fi
}

# ---------------------------------------------------------------- packages: made

# ELF files of a directory
elf_files() {
    find "$1" -type f -size +64c | while IFS= read -r f; do
        [ "$(head -c 4 "$f" | od -An -c | tr -d ' ')" = '177ELF' ] && printf '%s\n' "$f"
    done
}

# make_package NAME VERSION STAGE "SUMMARY" "LICENSE" [DEPENDENCY...]
# A package of what was installed under STAGE; prints the package's path.
make_package() {
    name=$1 ver=$2 stage=$3 summary=$4 license=$5
    shift 5
    if command -v strip > /dev/null; then
        elf_files "$stage" | while IFS= read -r f; do strip --strip-unneeded "$f" 2> /dev/null || true; done
    fi
    if [ $FAMILY = rpm ]; then
        make_rpm "$name" "$(echo "$ver" | tr '-' '~')" "$stage" "$summary" "$license" "$@"
    else
        make_deb "$name" "$(echo "$ver" | tr '-' '~')" "$stage" "$summary" "$license" "$@"
    fi
}

make_rpm() {
    name=$1 ver=$2 stage=$3 summary=$4 license=$5
    shift 5
    command -v rpmbuild > /dev/null || die "rpmbuild is missing (sudo dnf install rpm-build)"
    top="$CACHE/rpmbuild"
    rm -rf "$top"
    mkdir -p "$top/SPECS" "$top/BUILD" "$top/BUILDROOT"
    spec="$top/SPECS/$name.spec"
    {
        echo "Name: $name"
        echo "Version: $ver"
        echo "Release: $RELEASE"
        echo "Summary: $summary"
        echo "License: $license"
        echo "URL: $HOMEPAGE"
        for d in "$@"; do echo "Requires: $d"; done
        echo "%global debug_package %{nil}"
        echo "%global _build_id_links none"
        echo "%global __os_install_post %{nil}"
        echo "%global __arch_install_post %{nil}"
        echo "%define _rpmfilename %%{NAME}-%%{VERSION}-%%{RELEASE}.%%{ARCH}.rpm"
        echo "%description"
        echo "$summary (built by Nirvana Desktop's install.sh)."
        echo "%install"
        echo "cp -a '$stage'/. %{buildroot}/"
        echo "%post -p /sbin/ldconfig"
        echo "%postun -p /sbin/ldconfig"
        echo "%files"
        # every file; the directories of Nirvana's own (/opt/nirvana...), not the system's
        (cd "$stage" && find . -mindepth 1 ! -type d -printf '"/%P"\n')
        (cd "$stage" && find . -mindepth 1 -type d -printf '/%P\n') |
            while IFS= read -r d; do
                case "$d" in "$PREFIX"|"$PREFIX"/*) printf '%%dir "%s"\n' "$d" ;; esac
            done
    } > "$spec"
    rpmbuild -bb --quiet --define "_topdir $top" --define "_rpmdir $PKGDIR" \
        --define "_build_name_fmt %%{NAME}-%%{VERSION}-%%{RELEASE}.%%{ARCH}.rpm" "$spec" >&2 ||
        die "The package $name could not be made"
    ls -t "$PKGDIR/$name-$ver-$RELEASE".*.rpm | head -n 1
}

make_deb() {
    name=$1 ver=$2 stage=$3 summary=$4 license=$5
    shift 5
    command -v dpkg-deb > /dev/null || die "dpkg-deb is missing"
    arch=$(dpkg --print-architecture)
    # the libraries the programs use: their packages (dpkg-shlibdeps), and the ones given
    depends=""
    if command -v dpkg-shlibdeps > /dev/null; then
        work=$(mktemp -d)
        mkdir "$work/debian"
        printf 'Source: %s\n\nPackage: %s\nArchitecture: any\n' "$name" "$name" > "$work/debian/control"
        libdirs=$(find "$stage" -name '*.so*' -type f -printf '-l%h\n' | sort -u | tr '\n' ' ')
        files=$(elf_files "$stage" | tr '\n' ' ')
        if [ -n "$files" ]; then
            # shellcheck disable=SC2086
            depends=$(cd "$work" && dpkg-shlibdeps -O --ignore-missing-info $libdirs $files 2> /dev/null |
                sed -n 's/^shlibs:Depends=//p')
        fi
        rm -rf "$work"
    else
        warn "dpkg-shlibdeps is missing (package dpkg-dev): $name names only Nirvana's own packages"
    fi
    for d in "$@"; do depends="${depends:+$depends, }$d"; done
    mkdir -p "$stage/DEBIAN"
    {
        echo "Package: $name"
        echo "Version: $ver+$RELEASE"
        echo "Architecture: $arch"
        echo "Maintainer: $MAINTAINER"
        echo "Section: x11"
        echo "Priority: optional"
        echo "Homepage: $HOMEPAGE"
        [ -n "$depends" ] && echo "Depends: $depends"
        echo "Description: $summary"
        echo " Built by Nirvana Desktop's install.sh. Licence: $license."
    } > "$stage/DEBIAN/control"
    printf '#!/bin/sh\nset -e\nldconfig\n' > "$stage/DEBIAN/postinst"
    printf '#!/bin/sh\nset -e\nldconfig\n' > "$stage/DEBIAN/postrm"
    chmod 755 "$stage/DEBIAN/postinst" "$stage/DEBIAN/postrm"
    out="$PKGDIR/${name}_$ver+${RELEASE}_$arch.deb"
    dpkg-deb --root-owner-group -Zxz --build "$stage" "$out" > /dev/null ||
        die "The package $name could not be made"
    rm -rf "$stage/DEBIAN"
    echo "$out"
}

# a clean directory to install a build into
new_stage() {
    rm -rf "$CACHE/stage-$1"
    mkdir -p "$CACHE/stage-$1"
    echo "$CACHE/stage-$1"
}

# ---------------------------------------------------------------- older installs

# install.sh 3.0.0-alpha4 and before copied the files in without packages; they are
# taken out before the packages come in
remove_unpackaged() {
    [ -z "$(pkg_installed_version nirvana-desktop)" ] || return 0
    for b in "$HERE/build" "$CACHE/build"; do
        if [ -f "$b/meson-logs/install-log.txt" ] &&
            [ -e "$(sed -n '2p' "$b/meson-logs/install-log.txt")" ]; then
            say "Removing the files of an install without packages"
            $SUDO ninja -C "$b" uninstall > /dev/null 2>&1 || true
            rm -f "$b/meson-logs/install-log.txt"
            break
        fi
    done
    m="$CACHE/dwarfs-$DWARFS_VERSION/build/install_manifest.txt"
    if [ -f "$m" ] && [ -z "$(pkg_installed_version nirvana-libdwarfs)" ]; then
        tr '\n' '\0' < "$m" | $SUDO xargs -0 rm -f
        $SUDO rm -f "$PREFIX/Libs/cmake/dwarfs/nirvana-built-version"
    fi
    [ -L /nirvana ] && $SUDO rm -f /nirvana
    $SUDO rm -f /etc/ld.so.conf.d/nirvana.conf /etc/ld.so.conf.d/nirvana-built.conf \
        /usr/share/wayland-sessions/nirvana.desktop /usr/share/xsessions/nirvana-x11.desktop
}

# ---------------------------------------------------------------- uninstall

if [ $uninstall -eq 1 ]; then
    pkg_remove nirvana-desktop nirvana-labwc nirvana-efl nirvana-libdwarfs
    remove_unpackaged
    $SUDO ldconfig 2> /dev/null || true
    say "Done (your ~/.nirvana was left alone)"
    exit 0
fi

# ---------------------------------------------------------------- the build's packages

# dnf: the packages that are there installed, the others named
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
    $SUDO apt-get install -y meson ninja-build pkg-config gcc g++ gettext git dpkg-dev \
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
        rpm-build binutils curl xz bzip2 tar findutils python3 \
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
    dnf_install appmenu-gtk3-module

    if [ $dwarfs -eq 1 ]; then
        say "Installing the packages libdwarfs needs"
        dnf_install cmake boost-devel fmt-devel xxhash-devel brotli-devel \
            parallel-hashmap-devel range-v3-devel utf8cpp-devel flac-devel file-devel \
            libacl-devel json-devel date-devel
    fi
}

if [ $deps -eq 1 ]; then
    if [ $FAMILY = rpm ]; then deps_rpm; else deps_deb; fi
fi

for tool in meson ninja pkg-config cc; do
    if ! command -v $tool > /dev/null; then
        echo "install.sh: $tool is missing (./install.sh --deps installs what is needed)" >&2
        exit 1
    fi
done

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

# get_source NAME FILE SHA256 "URLS": a source's tarball in the cache, checked; the first
# address whose download matches is used. A tarball put in the cache by hand (without a
# network) is used when it matches.
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

[ $install -eq 1 ] && remove_unpackaged

# ---------------------------------------------------------------- nirvana-efl

# Where the system has no EFL of 1.26 or later (Rocky Linux 10 has none).
build_efl() {
    need_tools xz sha256sum || return 1
    get_source EFL "efl-$EFL_VERSION.tar.xz" "$EFL_SHA256" "$EFL_URLS" || return 1
    src="$CACHE/efl-$EFL_VERSION"
    if [ ! -f "$src/meson.build" ]; then
        rm -rf "$src"
        tar -xJf "$CACHE/efl-$EFL_VERSION.tar.xz" -C "$CACHE"
    fi
    lua=lua
    pkg-config --exists luajit && lua=luajit
    if [ ! -f "$src/build/build.ninja" ]; then
        say "Configuring EFL $EFL_VERSION"
        meson setup "$src/build" "$src" --prefix="$EFL_PREFIX" --libdir=lib \
            --buildtype=release -Dlua-interpreter=$lua \
            -Dx11=true -Dwl=true -Dopengl=es-egl -Ddrm=false -Dfb=false -Dphysics=false \
            -Daudio=false -Dpulseaudio=false -Dgstreamer=false -Davahi=false -Dv4l2=false \
            -Dedje-sound-and-video=false -Dxpresent=false \
            -Dbuild-tests=false -Dbuild-examples=false \
            -Decore-imf-loaders-disabler=ibus,scim \
            -Devas-loaders-disabler=gst,pdf,ps,raw,rsvg,xcf,json,avif,heif,jxl,jp2k ||
            { rm -rf "$src/build"; return 1; }
    fi
    say "Building EFL $EFL_VERSION (once; from a few minutes to an hour)"
    ninja -C "$src/build" -j "$NPROC" || return 1
    stage=$(new_stage efl)
    meson install -C "$src/build" --destdir "$stage" --no-rebuild > /dev/null || return 1
    # (only what is under its own directory: no D-Bus or systemd entries in the system's)
    rm -rf "${stage:?}/usr" "${stage:?}/etc"
    mkdir -p "$stage/etc/ld.so.conf.d" "$stage$EFL_PREFIX/share/licenses/efl"
    echo "$EFL_PREFIX/lib" > "$stage/etc/ld.so.conf.d/nirvana-efl.conf"
    cp -R "$src/COPYING" "$src/licenses" "$stage$EFL_PREFIX/share/licenses/efl/" 2> /dev/null || true
    pkg=$(make_package nirvana-efl "$EFL_VERSION" "$stage" \
        "EFL, the Enlightenment Foundation Libraries, for Nirvana Desktop" \
        "BSD-2-Clause AND LGPL-2.1-only AND GPL-2.0-only")
    rm -rf "$stage"
    pkg_install "$pkg"
}

if [ "$(pkg_installed_version nirvana-efl)" = "$EFL_VERSION" ]; then
    say "nirvana-efl $EFL_VERSION is installed already"
elif pkg-config --atleast-version=1.26 elementary 2> /dev/null; then
    :
else
    [ $install -eq 1 ] || die "EFL is needed for the build: it must be installed (no --no-install)"
    [ $FAMILY = deb ] && warn "No EFL packages found (./install.sh --deps installs them); building EFL"
    build_efl || die "EFL could not be built (the reason is above); the desktop needs it"
fi

# ---------------------------------------------------------------- nirvana-labwc

# Where the system has no labwc (Rocky Linux 10): labwc with wlroots inside it; what
# wlroots needs and the system lacks (a newer Wayland, libseat...) meson fetches with git.
build_labwc() {
    need_tools bzip2 xz sha256sum git || return 1
    get_source labwc "labwc_$LABWC_VERSION.orig.tar.xz" "$LABWC_SHA256" "$LABWC_URLS" || return 1
    get_source wlroots "wlroots_$WLROOTS_VERSION.orig.tar.bz2" "$WLROOTS_SHA256" "$WLROOTS_URLS" || return 1
    src="$CACHE/labwc-$LABWC_VERSION"
    if [ ! -f "$src/build/build.ninja" ]; then
        rm -rf "$src"
        mkdir -p "$src/subprojects/wlroots"
        tar -xJf "$CACHE/labwc_$LABWC_VERSION.orig.tar.xz" -C "$src" --strip-components=1
        tar -xjf "$CACHE/wlroots_$WLROOTS_VERSION.orig.tar.bz2" -C "$src/subprojects/wlroots" \
            --strip-components=1
        # (wlroots' own fallbacks made known to the build)
        for w in "$src"/subprojects/wlroots/subprojects/*.wrap; do
            [ -e "$src/subprojects/$(basename "$w")" ] || cp "$w" "$src/subprojects/"
        done
        say "Configuring labwc $LABWC_VERSION with wlroots $WLROOTS_VERSION"
        meson setup "$src/build" "$src" --prefix="$LABWC_PREFIX" --libdir=lib \
            --buildtype=release --default-library=static --wrap-mode=default \
            -Dman-pages=disabled -Dsvg=disabled -Dicon=disabled -Dnls=disabled \
            -Dwlroots:renderers=gles2 -Dwlroots:backends=drm,libinput \
            -Dwlroots:xwayland=auto -Dwlroots:libliftoff=disabled ||
            { rm -rf "$src/build"; return 1; }
    fi
    say "Building labwc (once)"
    ninja -C "$src/build" -j "$NPROC" || return 1
    stage=$(new_stage labwc)
    meson install -C "$src/build" --destdir "$stage" --no-rebuild > /dev/null || return 1
    # (the static libraries and headers of what it was built with are not needed)
    rm -rf "${stage:?}$LABWC_PREFIX/include" "${stage:?}$LABWC_PREFIX/lib" "${stage:?}/usr" "${stage:?}/etc"
    mkdir -p "$stage$PREFIX/C" "$stage$LABWC_PREFIX/share/licenses/labwc"
    ln -s "$LABWC_PREFIX/bin/labwc" "$stage$PREFIX/C/labwc"
    cp "$src/LICENSE" "$stage$LABWC_PREFIX/share/licenses/labwc/LICENSE.labwc" 2> /dev/null || true
    cp "$src/subprojects/wlroots/LICENSE" "$stage$LABWC_PREFIX/share/licenses/labwc/LICENSE.wlroots" 2> /dev/null || true
    pkg=$(make_package nirvana-labwc "$LABWC_VERSION" "$stage" \
        "labwc, a Wayland window-stacking compositor, for Nirvana Desktop" \
        "GPL-2.0-only AND MIT")
    rm -rf "$stage"
    if [ $install -eq 1 ]; then pkg_install "$pkg"; fi
}

LABWC_DEP=""
if [ "$(pkg_installed_version nirvana-labwc)" = "$LABWC_VERSION" ]; then
    say "nirvana-labwc $LABWC_VERSION is installed already"
    LABWC_DEP=nirvana-labwc
elif command -v labwc > /dev/null && [ "$(command -v labwc)" != "$PREFIX/C/labwc" ]; then
    LABWC_DEP=labwc
elif build_labwc; then
    LABWC_DEP=nirvana-labwc
else
    warn "labwc could not be built: no Wayland session (the reason is above)"
fi

# ---------------------------------------------------------------- nirvana-libdwarfs

DWARFS_CMAKE="$PREFIX/Libs/cmake/dwarfs"
build_dwarfs() {
    need_tools cmake c++ xz sha256sum || return 1
    get_source libdwarfs "dwarfs-$DWARFS_VERSION.tar.xz" "$DWARFS_SHA256" "$DWARFS_URLS" || return 1
    src="$CACHE/dwarfs-$DWARFS_VERSION"
    if [ ! -f "$src/CMakeLists.txt" ]; then
        rm -rf "$src"
        tar -xJf "$CACHE/dwarfs-$DWARFS_VERSION.tar.xz" -C "$CACHE"
    fi
    say "Building libdwarfs $DWARFS_VERSION (once; this takes a while)"
    cmake -S "$src" -B "$src/build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=Libs \
        -DCMAKE_INSTALL_INCLUDEDIR=include -DCMAKE_INSTALL_RPATH="$PREFIX/Libs" \
        -DDWARFS_CMAKE_INSTALL_DIR=Libs/cmake/dwarfs \
        -DWITH_LIBDWARFS=ON -DWITH_TOOLS=OFF -DWITH_FUSE_DRIVER=OFF -DWITH_TESTS=OFF \
        -DWITH_DESKTOP_INTEGRATION=OFF -DWITH_MAN_OPTION=OFF -DENABLE_PERFMON=OFF || return 1
    ninja -C "$src/build" -j "$NPROC" || return 1
    stage=$(new_stage dwarfs)
    DESTDIR="$stage" cmake --install "$src/build" > /dev/null || return 1
    mkdir -p "$stage$PREFIX/Libs/licenses/dwarfs"
    cp "$src"/LICENSE* "$stage$PREFIX/Libs/licenses/dwarfs/" 2> /dev/null || true
    pkg=$(make_package nirvana-libdwarfs "$DWARFS_VERSION" "$stage" \
        "libdwarfs, for DwarFS AppImages in Nirvana Desktop" "MIT AND GPL-3.0-or-later AND Apache-2.0")
    rm -rf "$stage"
    pkg_install "$pkg"
}

dwarfs_opts="-Ddwarfs=disabled"
DWARFS_DEP=""
if [ $dwarfs -eq 1 ]; then
    if [ "$(pkg_installed_version nirvana-libdwarfs)" = "$DWARFS_VERSION" ]; then
        say "nirvana-libdwarfs $DWARFS_VERSION is installed already"
        dwarfs_opts="-Ddwarfs=enabled -Dcmake_prefix_path=$DWARFS_CMAKE"
        DWARFS_DEP=nirvana-libdwarfs
    elif [ $install -eq 0 ]; then
        warn "libdwarfs must be installed to build with it (no --no-install): built without"
    elif build_dwarfs; then
        dwarfs_opts="-Ddwarfs=enabled -Dcmake_prefix_path=$DWARFS_CMAKE"
        DWARFS_DEP=nirvana-libdwarfs
    else
        warn "libdwarfs could not be built: Nirvana is built without DwarFS AppImages"
        warn "(the other AppImages - SquashFS, ISO 9660 - work as before)"
    fi
fi

# ---------------------------------------------------------------- nirvana-desktop

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

say "Making the package"
VERSION=$(meson introspect --projectinfo "$BUILD" | sed -n 's/.*"version": *"\([^"]*\)".*/\1/p')
stage=$(new_stage desktop)
meson install -C "$BUILD" --destdir "$stage" --no-rebuild > /dev/null
NIRVANA="$PREFIX" sh "$SRC/setup-env.sh" --system --destdir "$stage" > /dev/null
mkdir -p "$stage$PREFIX/S/Licenses"
cp "$SRC/COPYING" "$SRC/COPYING.LESSER" "$stage$PREFIX/S/Licenses/"
set --
[ -n "$(pkg_installed_version nirvana-efl)" ] && set -- "$@" nirvana-efl
[ -n "$LABWC_DEP" ] && set -- "$@" "$LABWC_DEP"
[ -n "$DWARFS_DEP" ] && set -- "$@" "$DWARFS_DEP"
PKG=$(make_package nirvana-desktop "$VERSION" "$stage" \
    "Nirvana Desktop, a spatial desktop with a screen bar and a global menu" \
    "GPL-3.0-or-later AND LGPL-3.0-or-later" "$@")
rm -rf "$stage"

if [ $install -eq 0 ]; then
    say "Done: the packages are in $PKGDIR"
    ls -1 "$PKGDIR"
    exit 0
fi
pkg_install "$PKG"

say "Setting up your account"
NIRVANA="$PREFIX" sh "$SRC/setup-env.sh" --user

say "Done"
echo
echo "  Packages: $PKGDIR"
echo "  (remove: ./install.sh --uninstall, or the package manager)"
echo
echo "  Choose \"Nirvana Desktop\" in your login manager, or run"
echo "  $PREFIX/C/nirvana-session from a text console."
echo
echo "  The commands are in $PREFIX/C (assign, wbrun, appimage-info)."
