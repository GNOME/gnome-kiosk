project('gnome-kiosk', 'c',
        version: '44.0'
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

mutter_api_version = 12
libmutter_name = 'libmutter-@0@'.format(mutter_api_version)
libmutter_cogl_name = 'mutter-cogl-@0@'.format(mutter_api_version)
libmutter_cogl_pango_name = 'mutter-cogl-pango-@0@'.format(mutter_api_version)
libmutter_clutter_name = 'mutter-clutter-@0@'.format(mutter_api_version)

mutter_dependency = dependency(libmutter_name)
mutter_libdir = mutter_dependency.get_pkgconfig_variable('typelibdir')

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

compositor_dependencies = []
compositor_dependencies += c_compiler.find_library('m')
compositor_dependencies += dependency('gio-2.0')
compositor_dependencies += dependency('glib-2.0')
compositor_dependencies += dependency('gnome-desktop-4')
compositor_dependencies += dependency('gobject-2.0')
compositor_dependencies += dependency('ibus-1.0')
compositor_dependencies += dependency(libmutter_cogl_name)
compositor_dependencies += dependency(libmutter_cogl_pango_name)
compositor_dependencies += dependency(libmutter_clutter_name)
compositor_dependencies += mutter_dependency
compositor_dependencies += systemd_dependency

compositor_sources = []
compositor_sources += 'compositor/kiosk-backgrounds.c'
compositor_sources += 'compositor/kiosk-compositor.c'
compositor_sources += 'compositor/kiosk-dbus-utils.c'
compositor_sources += 'compositor/kiosk-gobject-utils.c'
compositor_sources += 'compositor/kiosk-input-sources-manager.c'
compositor_sources += 'compositor/kiosk-input-engine-manager.c'
compositor_sources += 'compositor/kiosk-input-source-group.c'
compositor_sources += 'compositor/kiosk-service.c'
compositor_sources += 'compositor/kiosk-shell-service.c'
compositor_sources += 'compositor/kiosk-x-keyboard-manager.c'
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
systemd_service_files += 'compositor/data/systemd/org.gnome.Kiosk@x11.service.in'

foreach service_file : systemd_service_files
        configure_file(
                input: service_file,
                output: '@BASENAME@',
                configuration: systemd_service_config_data,
                install_dir: systemd_user_unit_dir
        )
endforeach

install_data(
        'compositor/data/systemd/org.gnome.Kiosk.target',
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

install_data('kiosk-script/gnome-kiosk-script',
        install_dir: bindir,
	install_mode: 'rwxr-xr-x'
)

desktop_file = configure_file(
        input: 'kiosk-script/desktop/org.gnome.Kiosk.Script.desktop.in.in',
        output: 'org.gnome.Kiosk.Script.desktop.in',
        configuration: desktop_config_data
)

i18n.merge_file(
        input: desktop_file,
        output: 'org.gnome.Kiosk.Script.desktop',
        po_dir: po_dir,
        install: true,
        install_dir: desktop_data_dir,
        type: 'desktop'
)

configure_file(
       input: 'kiosk-script/systemd/org.gnome.Kiosk.Script.service.in',
       output: '@BASENAME@',
       configuration: systemd_service_config_data,
       install_dir: systemd_user_unit_dir
)

kiosk_script_systemd_target_dir = join_paths(systemd_user_unit_dir, 'gnome-session@gnome-kiosk-script.target.d')
install_data('kiosk-script/systemd/session.conf',
        install_dir: kiosk_script_systemd_target_dir
)

install_data('kiosk-script/gnome-session/gnome-kiosk-script.session',
        install_dir: session_dir,
)

i18n.merge_file(
        input: 'kiosk-script/xsessions/gnome-kiosk-script-xorg.desktop.in',
        output: '@BASENAME@',
        po_dir: po_dir,
        install: true,
        install_dir: xsessions_dir,
        type: 'desktop'
)

i18n.merge_file(
        input: 'kiosk-script/wayland-sessions/gnome-kiosk-script-wayland.desktop.in',
        output: '@BASENAME@',
        po_dir: po_dir,
        install: true,
        install_dir: wayland_sessions_dir,
        type: 'desktop'
)

session_config_data = configuration_data()
session_config_data.set('required_components', 'org.gnome.Kiosk;org.gnome.Kiosk.SearchApp;')

session_file = configure_file(
        input: 'search-app/org.gnome.Kiosk.SearchApp.session.desktop.in.in',
        output: 'org.gnome.Kiosk.SearchApp.session.desktop.in',
        configuration: session_config_data
)

subdir('input-selector')

i18n.merge_file(
        input: session_file,
        output: 'org.gnome.Kiosk.SearchApp.session',
        po_dir: po_dir,
        install: true,
        install_dir: session_dir,
        type: 'desktop'
)

i18n.merge_file(
        input: 'search-app/org.gnome.Kiosk.SearchApp.Session.desktop.in',
        output: 'org.gnome.Kiosk.SearchApp.Session.desktop',
        po_dir: po_dir,
        install: true,
        install_dir: xsessions_dir,
        type: 'desktop'
)

search_app_desktop_file = configure_file(
        input: 'search-app/org.gnome.Kiosk.SearchApp.desktop.in.in',
        output: 'org.gnome.Kiosk.SearchApp.desktop.in',
        configuration: desktop_config_data
)

i18n.merge_file(
        input: search_app_desktop_file,
        output: 'org.gnome.Kiosk.SearchApp.desktop',
        po_dir: po_dir,
        install: true,
        install_dir: desktop_data_dir,
        type: 'desktop'
)

test('check-format', find_program('scripts/check-format.sh'))

