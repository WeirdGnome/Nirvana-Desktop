# SPDX-License-Identifier: GPL-3.0-or-later
# The Edje themes compiled into Themes: (the .edj files).
themes = ['ambient']
foreach t : themes
  custom_target(t + '.edj',
    input: t + '.edc',
    output: t + '.edj',
    depend_files: files('images' / t / 'grad_bar.png'),
    command: [edje_cc, '-id', meson.current_source_dir() / 'images' / t,
              '@INPUT@', '@OUTPUT@'],
    build_by_default: true,
    install: true,
    install_dir: get_option('prefix') / 'Themes')
endforeach
