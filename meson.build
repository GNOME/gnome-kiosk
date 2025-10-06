project('gnome-kiosk', 'c',
        version: '49.0',
        meson_version : '>= 0.59',
)
add_project_arguments('-D_GNU_SOURCE',
        language: 'c'
)

c_compiler = meson.get_compiler('c')

gnome = import('gnome')
i18n = import('i18n')

prefix = get_option('prefix')
datadir = join_paths(prefix, get_option('datadir'))
bindir = join_paths(prefix, get_option('bindir'))
localedir = join_paths(datadir, 'locale')
desktop_data_dir = join_paths(datadir, 'applications')
session_dir = join_paths(datadir, 'gnome-session', 'sessions')
xsessions_dir = join_paths(datadir, 'xsessions')
wayland_sessions_dir = join_paths(datadir, 'wayland-sessions')

po_dir = join_paths(meson.current_source_dir(), 'po')

mutter_api_version = 17
libmutter_name = 'libmutter-@0@'.format(mutter_api_version)
libmutter_cogl_name = 'mutter-cogl-@0@'.format(mutter_api_version)
libmutter_clutter_name = 'mutter-clutter-@0@'.format(mutter_api_version)

mutter_dependency = dependency(libmutter_name)
mutter_libdir = mutter_dependency.get_pkgconfig_variable('typelibdir')
mutter_have_x11 = mutter_dependency.get_variable('have_x11') == 'true'
mutter_have_x11_client = mutter_dependency.get_variable('have_x11_client') == 'true'

config_data = configuration_data()
config_data.set_quoted('GETTEXT_PACKAGE', meson.project_name())
config_data.set_quoted('VERSION', meson.project_version())
config_data.set_quoted('LOCALEDIR', localedir)
config_data.set('HAVE_X11', mutter_have_x11)
config_data.set('HAVE_X11_CLIENT', mutter_have_x11_client)

config_h = configure_file(
        input: 'config.h.meson',
        output: 'config.h',
        configuration: config_data
)

systemd_user_unit_dir = dependency('systemd').get_pkgconfig_variable('systemduserunitdir',
                                                                     define_variable: ['prefix', prefix])
systemd_dependency = dependency('libsystemd')

dbus_proxies = []
dbus_proxies += {
        'prefix':   'org.gnome.DisplayManager',
        'namespace': 'Gdm',
        'interface': 'Manager',
}

dbus_proxies += {
        'prefix':   'org.freedesktop',
        'namespace': 'Sd',
        'interface': 'locale1',
}

dbus_proxies += {
        'prefix':   'org.gnome',
        'namespace': 'Gsm',
        'interface': 'SessionManager',
}

dbus_interface_sources_map = {}
foreach dbus_proxy : dbus_proxies
        dbus_interface = dbus_proxy['prefix'] + '.' + dbus_proxy['interface']
        dbus_interface_file = join_paths('dbus-interfaces', dbus_interface + '.xml')
        sources = gnome.gdbus_codegen(dbus_interface, dbus_interface_file,
                namespace: dbus_proxy['namespace'],
                interface_prefix: dbus_proxy['prefix'],
        )
        dbus_interface_sources_map += { dbus_interface: sources }
endforeach

dbus_interface = 'org.gnome.Kiosk'
dbus_interface_file = join_paths('dbus-interfaces', dbus_interface + '.xml')
sources = gnome.gdbus_codegen(dbus_interface, dbus_interface_file,
        namespace: 'KioskDBus',
        interface_prefix: 'org.gnome.Kiosk',
        object_manager: true,
        annotations: [
                [ dbus_interface, 'org.gtk.GDBus.C.Name', 'Service' ],
                [ dbus_interface + '.InputSources', 'org.gtk.GDBus.C.Name', 'InputSourcesManager' ],
                [ dbus_interface + '.InputSources.InputSource', 'org.gtk.GDBus.C.Name', 'InputSource' ],
        ]
)
dbus_interface_sources_map += { dbus_interface: sources }

dbus_interface = 'org.gnome.Shell'
dbus_interface_file = join_paths('dbus-interfaces', dbus_interface + '.xml')
sources = gnome.gdbus_codegen(dbus_interface, dbus_interface_file,
        namespace: 'Kiosk',
        interface_prefix: 'org.gnome',
        annotations: [
                [ dbus_interface, 'org.gtk.GDBus.C.Name', 'ShellDBusService' ]
        ]
)
dbus_interface_sources_map += { dbus_interface: sources }

dbus_interface = 'org.gnome.Shell.Introspect'
dbus_interface_file = join_paths('dbus-interfaces', dbus_interface + '.xml')
sources = gnome.gdbus_codegen(dbus_interface, dbus_interface_file,
        namespace: 'Kiosk',
        interface_prefix: 'org.gnome',
        annotations: [
                [ dbus_interface, 'org.gtk.GDBus.C.Name', 'ShellIntrospectDBusService' ]
        ]
)
dbus_interface_sources_map += { dbus_interface: sources }

dbus_interface = 'org.gnome.Shell.Screenshot'
dbus_interface_file = join_paths('dbus-interfaces', dbus_interface + '.xml')
sources = gnome.gdbus_codegen(dbus_interface, dbus_interface_file,
        namespace: 'Kiosk',
        interface_prefix: 'org.gnome',
        annotations: [
                [ dbus_interface, 'org.gtk.GDBus.C.Name', 'ShellScreenshotDBusService' ]
        ]
)
dbus_interface_sources_map += { dbus_interface: sources }

compositor_dependencies = []
compositor_dependencies += c_compiler.find_library('m')
compositor_dependencies += dependency('gio-2.0')
compositor_dependencies += dependency('glib-2.0')
compositor_dependencies += dependency('gnome-desktop-4')
compositor_dependencies += dependency('gobject-2.0')
compositor_dependencies += dependency('ibus-1.0')
compositor_dependencies += dependency('gdk-pixbuf-2.0')
compositor_dependencies += dependency(libmutter_cogl_name)
compositor_dependencies += dependency(libmutter_clutter_name)
compositor_dependencies += mutter_dependency
compositor_dependencies += systemd_dependency

compositor_headers = []
compositor_headers += 'compositor/kiosk-app.h'
compositor_headers += 'compositor/kiosk-app-system.h'
compositor_headers += 'compositor/kiosk-window-tracker.h'
kiosk_enums = gnome.mkenums_simple('kiosk-enum-types',
  sources: compositor_headers
)

compositor_sources = []
compositor_sources += kiosk_enums
compositor_sources += 'compositor/kiosk-backgrounds.c'
compositor_sources += 'compositor/kiosk-compositor.c'
compositor_sources += 'compositor/kiosk-app.c'
compositor_sources += 'compositor/kiosk-app-system.c'
compositor_sources += 'compositor/kiosk-window-tracker.c'
compositor_sources += 'compositor/kiosk-dbus-utils.c'
compositor_sources += 'compositor/kiosk-gobject-utils.c'
compositor_sources += 'compositor/kiosk-automount-manager.c'
compositor_sources += 'compositor/kiosk-input-sources-manager.c'
compositor_sources += 'compositor/kiosk-input-engine-manager.c'
compositor_sources += 'compositor/kiosk-input-source-group.c'
compositor_sources += 'compositor/kiosk-service.c'
compositor_sources += 'compositor/kiosk-shell-service.c'
compositor_sources += 'compositor/kiosk-shell-introspect-service.c'
compositor_sources += 'compositor/kiosk-shell-screenshot-service.c'
compositor_sources += 'compositor/kiosk-window-config.c'
compositor_sources += 'compositor/kiosk-screenshot.c'

if mutter_have_x11
        compositor_sources += 'compositor/kiosk-x-keyboard-manager.c'
endif
compositor_sources += 'compositor/main.c'

foreach dbus_interface, sources: dbus_interface_sources_map
        compositor_sources += sources
endforeach

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

i18n.merge_file(
        input: desktop_file,
        output: 'org.gnome.Kiosk.desktop',
        po_dir: po_dir,
        install: true,
        install_dir: desktop_data_dir,
        type: 'desktop'
)

systemd_service_config_data = configuration_data()
systemd_service_config_data.set('bindir', bindir)

systemd_service_files = []
systemd_service_files += 'compositor/data/systemd/org.gnome.Kiosk@wayland.service.in'
if mutter_have_x11
        systemd_service_files += 'compositor/data/systemd/org.gnome.Kiosk@x11.service.in'
endif

foreach service_file : systemd_service_files
        configure_file(
                input: service_file,
                output: '@BASENAME@',
                configuration: systemd_service_config_data,
                install_dir: systemd_user_unit_dir
        )
endforeach

targetconf = configuration_data()

sessions_wants = ['org.gnome.Kiosk@wayland.service']
if mutter_have_x11
        sessions_wants += ['org.gnome.Kiosk@x11.service']
endif
targetconf.set('SUPPORTED_SESSIONS', ' '.join(sessions_wants))
target = configure_file(
        input: 'compositor/data/systemd/org.gnome.Kiosk.target.in',
        output: 'org.gnome.Kiosk.target',
        configuration: targetconf,
        install_dir: systemd_user_unit_dir
)

systemd_service_config_data = configuration_data()
systemd_service_config_data.set('bindir', bindir)

dconf_config_data = configuration_data()
dconf_config_data.set('PACKAGE', meson.project_name())
dconf_config_data.set('DATADIR', datadir)

dconf_profile_dir = join_paths(datadir, 'dconf/profile')
dconf = configure_file(
        input: 'compositor/data/dconf/gnomekiosk.in',
        output: '@BASENAME@.configured',
        configuration: dconf_config_data,
)
install_data(dconf,
        install_dir: dconf_profile_dir,
        rename: 'gnomekiosk'
)

dconf_dir = join_paths(datadir, meson.project_name())
dconf_defaults_input_dir = join_paths (meson.current_source_dir(), 'compositor/data/dconf/defaults')
dconf_defaults = custom_target('gnomekiosk.dconf.compiled',
        output: 'gnomekiosk.dconf.compiled',
        input:  'compositor/data/dconf/defaults/gnomekiosk.dconf',
        command: [
                find_program('dconf'),
                'compile',
                '@OUTPUT@',
                dconf_defaults_input_dir
        ],
        install: true,
        install_dir: dconf_dir
)

if get_option('kiosk-script')
        subdir ('kiosk-script')
endif


if get_option('input-selector')
        subdir('input-selector')
endif

if get_option ('search-app')
        subdir ('search-app')
endif

test('check-format', find_program('scripts/check-format.sh'))

if get_option('accessibility-panel')
        subdir('accessibility-panel')
endif

gnome.post_install(
  gtk_update_icon_cache: true,
  glib_compile_schemas: true,
  update_desktop_database: true,
)
