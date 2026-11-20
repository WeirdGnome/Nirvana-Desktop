<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Changelog

*[Polski](CHANGELOG.pl.md)*

## 3.0.0-alpha1 — unreleased (NirvanaNG, stage 1)

A new start on the `NirvanaNG` branch: new code, Meson, C11.

### Added
- `libnirvana` (LGPL-3.0-or-later):
  - the assigns database in `$XDG_RUNTIME_DIR/nirvana-ram/assigns.db` with atomic,
    locked changes; default assigns (SYS:, Home:, RAM:, Nirvana:, Themes:, Tools:, the
    XDG user directories, the folders of $HOME); multi-assigns; making, replacing,
    adding, removing, reordering, renaming and deleting; `nirvana_to_posix()` and
    `posix_to_nirvana()` (longest prefix); persistence in `~/.nirvana/Startup-Sequence`;
    the running desktop told with SIGUSR1;
  - an AppImage reader that runs nothing: the ELF payload offset (ELF64 and ELF32), an
    own SquashFS 4.0 reader (gzip, lzma, xz, lz4, zstd), ISO 9660 through libarchive,
    DwarFS through libdwarfs (optional); the icon (.DirIcon, else the .desktop file's
    Icon) and the .desktop keys;
  - the launch environment for the global menu, the default program of a MIME type
    (mimeapps.list), detached children.
- `assign`, `wbrun` (EFL: Efreet, Eldbus) and `appimage-info` (GPL-3.0-or-later).
- `setup-env.sh` and the `Startup-Sequence` template.
- Tests: the assigns, the `assign` command, AppImages packed with every compressor and as
  ISO, broken images.
