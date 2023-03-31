project('xisxwayland', 'c',
        version: '2',
        license: 'MIT/Expat',
        default_options: ['c_std=gnu99', 'warning_level=2'],
        meson_version: '>= 0.41.0')

pkgconfig = import('pkgconfig')
dep_xlib = dependency('x11')
dep_xrandr = dependency('xrandr')

executable('xisxwayland',
           'xisxwayland.c',
           'main.c',
           dependencies: [dep_xrandr, dep_xlib],
           install: true)
install_man('xisxwayland.1')

t = executable('test-xisxwayland',
               'test/test-xisxwayland.c',
               'test/fakexlib.h',
               'test/fakexlib.c',
               'xisxwayland.c',
               install: false)
test('test-xiswayland', t)
