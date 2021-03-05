project('gnome-kiosk', 'c',
        version: '40.alpha'
)
add_project_arguments('-D_GNU_SOURCE',
                      language: 'c'
)

c_compiler = meson.get_compiler('c')

i18n = import('i18n')

prefix = get_option('prefix')
datadir = join_paths(prefix, get_option('datadir'))
bindir = join_paths(prefix, get_option('bindir'))
localedir = join_paths(datadir, 'locale')
desktop_data_dir = join_paths(datadir, 'applications')
session_dir = join_paths(datadir, 'gnome-session', 'sessions')
xsessions_dir = join_paths(datadir, 'xsessions')

po_dir = join_paths(meson.current_source_dir(), 'po')

config_data = configuration_data()
config_data.set_quoted('GETTEXT_PACKAGE', meson.project_name())
config_data.set_quoted('VERSION', meson.project_version())
config_data.set_quoted('LOCALEDIR', localedir)

config_h = configure_file(
        input: 'config.h.meson',
        output: 'config.h',
        configuration: config_data
)

meson.add_install_script ('meson/postinstall.py')

mutter_dependency = dependency('libmutter-8')
mutter_libdir = mutter_dependency.get_pkgconfig_variable('typelibdir')

compositor_dependencies = []
compositor_dependencies += c_compiler.find_library('m')
compositor_dependencies += dependency('gio-2.0')
compositor_dependencies += dependency('glib-2.0')
compositor_dependencies += dependency('gobject-2.0')
compositor_dependencies += dependency('mutter-cogl-8')
compositor_dependencies += dependency('mutter-cogl-pango-8')
compositor_dependencies += dependency('mutter-clutter-8')
compositor_dependencies += mutter_dependency

compositor_sources = []
compositor_sources += 'compositor/kiosk-backgrounds.c'
compositor_sources += 'compositor/kiosk-compositor.c'
compositor_sources += 'compositor/main.c'

executable('gnome-kiosk', compositor_sources,
        dependencies: compositor_dependencies,
        build_rpath: mutter_libdir,
        install_rpath: mutter_libdir,
        install: true
)

desktop_config_data = configuration_data()
desktop_config_data.set('bindir', bindir)

desktop_file = configure_file(
        input: 'compositor/data/org.gnome.Kiosk.desktop.in.in',
        output: 'org.gnome.Kiosk.desktop.in',
        configuration: desktop_config_data
)

i18n.merge_file('desktop',
        input: desktop_file,
        output: 'org.gnome.Kiosk.desktop',
        po_dir: po_dir,
        install: true,
        install_dir: desktop_data_dir,
        type: 'desktop'
)
