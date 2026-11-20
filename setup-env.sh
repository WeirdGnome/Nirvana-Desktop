#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Nirvana Desktop: the system's and the user's directories set up.
#
#     sudo ./setup-env.sh --system      /opt/nirvana/{C,Libs,Themes,S}, the /nirvana link
#     ./setup-env.sh --user             ~/.nirvana, the Startup-Sequence, GTK's settings
#     ./setup-env.sh                    both (the system part only when run as root)
#
# Nothing is overwritten that is already there (a Startup-Sequence you changed is kept);
# run again any time.

set -eu

NIRVANA=${NIRVANA:-/opt/nirvana}
HERE=$(cd "$(dirname "$0")" && pwd)

do_system=0
do_user=0
case "${1:-}" in
    --system) do_system=1 ;;
    --user) do_user=1 ;;
    "") do_user=1; [ "$(id -u)" -eq 0 ] && do_system=1 ;;
    -h|--help) sed -n '3,12p' "$0"; exit 0 ;;
    *) echo "setup-env.sh: unknown option $1" >&2; exit 1 ;;
esac

say() { printf '%s\n' "$*"; }

if [ $do_system -eq 1 ]; then
    if [ "$(id -u)" -ne 0 ]; then
        echo "setup-env.sh: --system needs root" >&2
        exit 1
    fi
    for d in C Libs Themes S Locale; do
        mkdir -p "$NIRVANA/$d"
    done
    chmod 755 "$NIRVANA" "$NIRVANA"/*
    if [ ! -e /nirvana ]; then
        ln -s "$NIRVANA" /nirvana
        say "made /nirvana -> $NIRVANA"
    elif [ -L /nirvana ]; then
        say "/nirvana is there already ($(readlink /nirvana))"
    else
        say "warning: /nirvana exists and is not a link; left alone" >&2
    fi
    # the libraries of Nirvana found by the dynamic linker
    if [ -d /etc/ld.so.conf.d ] && [ ! -e /etc/ld.so.conf.d/nirvana.conf ]; then
        echo "$NIRVANA/Libs" > /etc/ld.so.conf.d/nirvana.conf
        ldconfig 2>/dev/null || true
    fi
    if [ ! -e "$NIRVANA/S/Startup-Sequence" ] && [ -f "$HERE/data/Startup-Sequence" ]; then
        install -m 755 "$HERE/data/Startup-Sequence" "$NIRVANA/S/Startup-Sequence"
    fi
    # the sessions in the display managers' lists (made again on every run: an update
    # gets the current entries)
    if [ -f "$NIRVANA/S/nirvana.desktop" ]; then
        mkdir -p /usr/share/wayland-sessions
        sed "s|/opt/nirvana|$NIRVANA|g" "$NIRVANA/S/nirvana.desktop" > /usr/share/wayland-sessions/nirvana.desktop
        chmod 644 /usr/share/wayland-sessions/nirvana.desktop
        say "made /usr/share/wayland-sessions/nirvana.desktop"
    else
        say "warning: $NIRVANA/S/nirvana.desktop is missing (is Nirvana installed?)" >&2
    fi
    if [ -f "$NIRVANA/S/nirvana-x11.desktop" ]; then
        mkdir -p /usr/share/xsessions
        sed "s|/opt/nirvana|$NIRVANA|g" "$NIRVANA/S/nirvana-x11.desktop" > /usr/share/xsessions/nirvana-x11.desktop
        chmod 644 /usr/share/xsessions/nirvana-x11.desktop
        say "made /usr/share/xsessions/nirvana-x11.desktop"
    fi
    say "system part ready in $NIRVANA"
fi

if [ $do_user -eq 1 ]; then
    U="$HOME/.nirvana"
    mkdir -p "$U/Themes" "$U/Prefs" "$U/Tools"
    if [ ! -e "$U/Startup-Sequence" ]; then
        for t in "$NIRVANA/S/Startup-Sequence" "$HERE/data/Startup-Sequence"; do
            if [ -f "$t" ]; then
                install -m 755 "$t" "$U/Startup-Sequence"
                say "made $U/Startup-Sequence"
                break
            fi
        done
    fi

    # GTK 3: menus shown by the shell (the global menu), not in the windows
    G="${XDG_CONFIG_HOME:-$HOME/.config}/gtk-3.0"
    mkdir -p "$G"
    S="$G/settings.ini"
    if [ ! -f "$S" ]; then
        printf '[Settings]\ngtk-shell-shows-menubar=1\n' > "$S"
        say "made $S"
    elif ! grep -q '^gtk-shell-shows-menubar' "$S"; then
        if grep -q '^\[Settings\]' "$S"; then
            sed -i 's/^\[Settings\]$/[Settings]\ngtk-shell-shows-menubar=1/' "$S"
        else
            printf '\n[Settings]\ngtk-shell-shows-menubar=1\n' >> "$S"
        fi
        say "set gtk-shell-shows-menubar=1 in $S"
    fi
    # your own labwc settings: the depth gadgets need one mouse binding there
    L="${XDG_CONFIG_HOME:-$HOME/.config}/labwc/rc.xml"
    if [ -f "$L" ] && ! grep -q 'button="Task"' "$L"; then
        say "note: for the depth gadgets under labwc, add this to the <mouse> section of $L:"
        say '  <context name="Client"><mousebind button="Task" action="Press"><action name="Lower"/></mousebind></context>'
    fi
    say "user part ready in $U"
fi
