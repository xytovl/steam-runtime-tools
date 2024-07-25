# Copyright 2024 Collabora Ltd.
# SPDX-License-Identifier: MIT

foreach program : [
  'priority-logwriter',
]
  executable(
    program,
    program + '.c',
    dependencies : [
      glib,
    ],
    include_directories : project_include_dirs,
    install : get_option('installed_tests'),
    install_dir : tests_dir,
  )
endforeach
