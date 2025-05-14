install_data('gnome-kiosk-script',
        install_dir: bindir,
	install_mode: 'rwxr-xr-x'
)

desktop_file = configure_file(
        input: 'desktop/org.gnome.Kiosk.Script.desktop.in.in',
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
       input: 'systemd/org.gnome.Kiosk.Script.service.in',
       output: '@BASENAME@',
       configuration: systemd_service_config_data,
       install_dir: systemd_user_unit_dir
)

kiosk_script_systemd_target_dir = join_paths(systemd_user_unit_dir, 'gnome-session@gnome-kiosk-script.target.d')
install_data('systemd/session.conf',
        install_dir: kiosk_script_systemd_target_dir
)

install_data('gnome-session/gnome-kiosk-script.session',
        install_dir: session_dir,
)

if mutter_have_x11
        i18n.merge_file(
                input: 'xsessions/gnome-kiosk-script-xorg.desktop.in',
                output: '@BASENAME@',
                po_dir: po_dir,
                install: true,
                install_dir: xsessions_dir,
                type: 'desktop'
        )
endif

i18n.merge_file(
        input: 'wayland-sessions/gnome-kiosk-script-wayland.desktop.in',
        output: '@BASENAME@',
        po_dir: po_dir,
        install: true,
        install_dir: wayland_sessions_dir,
        type: 'desktop'
)
