#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The assign command in a made-up home.
#
#     test_assign_cli.sh ASSIGN WORKDIR

set -u
ASSIGN=$1
W=$2/assign-cli-test
rm -rf "$W"
mkdir -p "$W/home/Work" "$W/run" "$W/a" "$W/b"
export HOME="$W/home" XDG_RUNTIME_DIR="$W/run" XDG_CONFIG_HOME="$W/home/.config"
fail=0
bad() { echo "FAIL: $*" >&2; fail=1; }

"$ASSIGN" --init --quiet || bad "init"
"$ASSIGN" | grep -q '^SYS: *  */  \[desktop\]$' || bad "list: $("$ASSIGN")"
"$ASSIGN" Work: | grep -q "$W/home/Work" || bad "a home folder's assign"

"$ASSIGN" Both: "$W/a;$W/b" --desktop --quiet || bad "set"
out=$("$ASSIGN" both:)
echo "$out" | grep -q "^Both: $W/a  \[desktop\]$" || bad "show: $out"
echo "$out" | grep -q "^ *+ $W/b$" || bad "second path: $out"

# relative and Nirvana paths become absolute ones
cd "$W" || exit 99
"$ASSIGN" Rel: a --quiet || bad "relative"
"$ASSIGN" Rel: | grep -q "^Rel: $W/a$" || bad "relative path: $("$ASSIGN" Rel:)"
"$ASSIGN" Nv: Home:Work --quiet || bad "Nirvana path"
"$ASSIGN" Nv: | grep -q "^Nv: $W/home/Work$" || bad "Nirvana path stored: $("$ASSIGN" Nv:)"
"$ASSIGN" Nv: "$W/b" --add --quiet || bad "add"
"$ASSIGN" Nv: | grep -q "+ $W/b" || bad "added path"

# a replaced assign keeps its desktop icon; --no-desktop takes it away
"$ASSIGN" Both: "$W/b" --quiet
"$ASSIGN" Both: | grep -q '\[desktop\]' || bad "desktop flag lost"
"$ASSIGN" Both: --no-desktop --quiet
"$ASSIGN" Both: | grep -q '\[desktop\]' && bad "desktop flag kept"

# --save: the Startup-Sequence
"$ASSIGN" Both: "$W/a" --desktop --save --quiet || bad "save"
grep -qx "assign Both: '$W/a' --desktop" "$HOME/.nirvana/Startup-Sequence" || bad "Startup-Sequence: $(cat "$HOME/.nirvana/Startup-Sequence")"
# the line runs: a fresh database made from it
rm -f "$W/run/nirvana-ram/assigns.db"
PATH="$(dirname "$ASSIGN"):$PATH" sh "$HOME/.nirvana/Startup-Sequence" || bad "the Startup-Sequence does not run"
"$ASSIGN" Both: | grep -q "^Both: $W/a  \[desktop\]$" || bad "the Startup-Sequence's assign"

"$ASSIGN" Both: REMOVE --save --quiet || bad "remove"
"$ASSIGN" Both: > /dev/null 2>&1
[ $? -eq 5 ] || bad "a removed assign is still there"
grep -q "Both:" "$HOME/.nirvana/Startup-Sequence" && bad "the line was not removed from the Startup-Sequence"

"$ASSIGN" NoColon /tmp > /dev/null 2>&1
[ $? -eq 1 ] || bad "a name without a colon"
"$ASSIGN" --bogus > /dev/null 2>&1
[ $? -eq 1 ] || bad "an unknown option"
"$ASSIGN" Missing: > /dev/null 2>&1
[ $? -eq 5 ] || bad "a missing assign"

[ $fail -eq 0 ] && echo ok
exit $fail
