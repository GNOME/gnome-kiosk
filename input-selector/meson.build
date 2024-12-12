input_selector_dependencies = []
input_selector_dependencies += dependency('gio-2.0')
input_selector_dependencies += dependency('gio-unix-2.0')
input_selector_dependencies += dependency('glib-2.0')
input_selector_dependencies += dependency('gobject-2.0')
input_selector_dependencies += dependency('gtk4')

input_selector_sources = []
input_selector_sources += 'kiosk-input-selector-application.c'
input_selector_sources += 'main.c'

dbus_interface = 'org.gnome.Kiosk'
dbus_interface_file = join_paths('../dbus-interfaces', dbus_interface + '.xml')
sources = gnome.gdbus_codegen(dbus_interface, dbus_interface_file,
        namespace: 'Kiosk',
        interface_prefix: 'org.gnome.Kiosk',
        object_manager: true,
        annotations: [
                [ dbus_interface, 'org.gtk.GDBus.C.Name', 'Service' ],
                [ dbus_interface + '.InputSources', 'org.gtk.GDBus.C.Name', 'InputSourcesManager' ],
                [ dbus_interface + '.InputSources.InputSource', 'org.gtk.GDBus.C.Name', 'InputSource' ],
        ]
)

input_selector_sources += sources

resources = gnome.compile_resources('kiosk-input-selector-resources',
                                    'kiosk-input-selector.gresource.xml',
                                    source_dir: '.')
input_selector_sources += resources

executable('gnome-kiosk-input-selector', input_selector_sources,
        dependencies: input_selector_dependencies,
        install: false
)

