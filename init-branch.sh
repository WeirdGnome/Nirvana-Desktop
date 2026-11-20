#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# How the NirvanaNG branch was made: an orphan branch (no history, no files of the other
# branches), pushed to GitHub. Nothing else in the repository - branches, tags, history -
# is touched. Run in a clone of the repository; it stops if the branch exists already.

set -eu
BRANCH=NirvanaNG

if git rev-parse --verify --quiet "$BRANCH" > /dev/null ||
   git ls-remote --exit-code --heads origin "$BRANCH" > /dev/null 2>&1; then
    echo "init-branch.sh: $BRANCH exists already" >&2
    exit 1
fi
if [ -n "$(git status --porcelain)" ]; then
    echo "init-branch.sh: commit or stash your changes first" >&2
    exit 1
fi

git checkout --orphan "$BRANCH"
git rm -rqf .
git clean -fdxq
printf 'build/\n*.edj\n*.o\n*.so\n' > .gitignore
git add .gitignore
git commit -m "Initial clean slate for NirvanaNG (Nirvana Desktop v3.0)"
git push -u origin "$BRANCH"
