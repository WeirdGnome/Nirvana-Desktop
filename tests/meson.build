# SPDX-License-Identifier: GPL-3.0-or-later
test_assigns = executable('test_assigns', 'test_assigns.c', dependencies: libnirvana_dep)
test('assigns', test_assigns, env: {'TMPDIR': meson.current_build_dir()})

test_paths = executable('test_paths', 'test_paths.c',
  include_directories: [inc, include_directories('../src/lib')],
  link_with: libnirvana)
test('paths', test_paths)

mksquashfs = find_program('mksquashfs', required: false)
bsdtar = find_program('bsdtar', required: false)
sh = find_program('sh')
if mksquashfs.found()
  test('appimage', sh,
    args: [files('test_appimage.sh'), appimage_info_exe.full_path(), meson.current_build_dir()],
    env: {'BSDTAR': bsdtar.found() ? bsdtar.full_path() : ''},
    depends: appimage_info_exe,
    timeout: 120)
endif
test('assign-cli', sh,
  args: [files('test_assign_cli.sh'), assign_exe.full_path(), meson.current_build_dir()],
  depends: assign_exe)
