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

i18n.merge_file(
        input: 'org.gnome.Kiosk.SearchApp.Session.desktop.in',
        output: 'org.gnome.Kiosk.SearchApp.Session.desktop',
        po_dir: po_dir,
        install: true,
        install_dir: xsessions_dir,
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
