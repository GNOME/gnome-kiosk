session_config_data = configuration_data()
session_config_data.set('required_components', 'org.gnome.Kiosk;org.gnome.Kiosk.SearchApp;')

session_file = configure_file(
        input: 'org.gnome.Kiosk.SearchApp.session.desktop.in.in',
        output: 'org.gnome.Kiosk.SearchApp.session.desktop.in',
        configuration: session_config_data
)
i18n.merge_file(
        input: session_file,
        output: 'org.gnome.Kiosk.SearchApp.session',
        po_dir: po_dir,
        install: true,
        install_dir: session_dir,
        type: 'desktop'
)

search_app_desktop_file = configure_file(
        input: 'org.gnome.Kiosk.SearchApp.desktop.in.in',
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

if mutter_have_x11
        subdir ('xsessions')
endif

subdir ('wayland-sessions')

configure_file(
       input: 'systemd/org.gnome.Kiosk.SearchApp.service.in',
       output: '@BASENAME@',
       configuration: systemd_service_config_data,
       install_dir: systemd_user_unit_dir
)

kiosk_search_appliance_systemd_target_dir = join_paths(systemd_user_unit_dir, 'gnome-session@org.gnome.Kiosk.SearchApp.target.d')
install_data('systemd/session.conf',
        install_dir: kiosk_search_appliance_systemd_target_dir
)
