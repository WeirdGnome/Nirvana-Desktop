# SPDX-License-Identifier: GPL-3.0-or-later
project('nirvana-desktop', ['c', 'cpp'],
  version: '3.0.0-alpha1',
  license: 'GPL-3.0-or-later AND LGPL-3.0-or-later',
  meson_version: '>= 0.60',
  default_options: [
    'c_std=c11',
    'cpp_std=c++20',
    'warning_level=2',       # -Wall -Wextra
    'buildtype=debugoptimized',
    'prefix=/opt/nirvana',
    'bindir=C',
    'libdir=Libs',
    'datadir=share',
  ])

cc = meson.get_compiler('c')
nirvana_home = get_option('prefix_nirvana')

add_project_arguments(
  '-DNIRVANA_VERSION="@0@"'.format(meson.project_version()),
  '-DNIRVANA_GETTEXT_PACKAGE="nirvana"',
  '-DNIRVANA_LOCALEDIR="@0@"'.format(get_option('prefix') / get_option('localedir')),
  '-DNIRVANA_HOME="@0@"'.format(nirvana_home),
  language: ['c', 'cpp'])
if get_option('nls')
  add_project_arguments('-DNIRVANA_ENABLE_NLS', language: ['c', 'cpp'])
endif

inc = include_directories('include')

# ------------------------------------------------------------------ libnirvana (LGPL-3.0)
zlib = dependency('zlib')
lzma = dependency('liblzma')
zstd = dependency('libzstd')
lz4 = dependency('liblz4')
archive = dependency('libarchive')

lib_src = files(
  'src/lib/assigns.c',
  'src/lib/launch.c',
  'src/lib/appimage.c',
  'src/lib/squashfs.c',
  'src/lib/iso9660.c',
)
lib_deps = [zlib, lzma, zstd, lz4, archive]
lib_cargs = []

dwarfs = dependency('dwarfs', required: get_option('dwarfs'),
                    modules: ['dwarfs::dwarfs_reader'], method: 'auto')
if dwarfs.found()
  lib_src += files('src/lib/appimage_dwarfs.cpp')
  lib_deps += dwarfs
  lib_cargs += '-DNIRVANA_WITH_DWARFS'
endif

libnirvana = shared_library('nirvana', lib_src,
  include_directories: inc,
  dependencies: lib_deps,
  c_args: lib_cargs,
  cpp_args: lib_cargs,
  version: '3.0.0',
  soversion: '3',
  gnu_symbol_visibility: 'default',
  install: true,
  install_rpath: get_option('prefix') / get_option('libdir'))

libnirvana_dep = declare_dependency(link_with: libnirvana, include_directories: inc)

install_headers('include/nirvana.h', 'include/nirvana_appimage.h', subdir: 'nirvana')

pkg = import('pkgconfig')
pkg.generate(libnirvana,
  name: 'libnirvana',
  description: 'Nirvana Desktop: assigns and AppImage metadata',
  subdirs: 'nirvana')

rpath = get_option('prefix') / get_option('libdir')

# ------------------------------------------------------------------ CLI (GPL-3.0)
assign_exe = executable('assign', 'src/cli/assign.c',
  dependencies: libnirvana_dep,
  install: true,
  install_rpath: rpath)

appimage_info_exe = executable('appimage-info', 'src/cli/appimage_info.c',
  dependencies: libnirvana_dep,
  install: true,
  install_rpath: rpath)

efl_ecore = dependency('ecore', required: get_option('desktop'))
efl_efreet = dependency('efreet', required: get_option('desktop'))
efl_efreet_mime = dependency('efreet-mime', required: get_option('desktop'))
efl_eldbus = dependency('eldbus', required: get_option('desktop'))
have_efl_cli = efl_ecore.found() and efl_efreet.found() and efl_efreet_mime.found() and efl_eldbus.found()

if have_efl_cli
  executable('wbrun', 'src/cli/wbrun.c',
    dependencies: [libnirvana_dep, efl_ecore, efl_efreet, efl_efreet_mime, efl_eldbus],
    install: true,
    install_rpath: rpath)
endif

# ------------------------------------------------------------------ the session's files
install_data('data/Startup-Sequence', install_dir: get_option('prefix') / 'S')
install_data('setup-env.sh', install_dir: get_option('prefix') / 'S', install_mode: 'rwxr-xr-x')

# ------------------------------------------------------------------ translations
if get_option('nls') and find_program('xgettext', required: false).found()
  subdir('po')
endif

# ------------------------------------------------------------------ tests
subdir('tests')

summary({
  'DwarFS AppImages': dwarfs.found(),
  'wbrun (EFL)': have_efl_cli,
  'NLS': get_option('nls'),
}, section: 'Features')
