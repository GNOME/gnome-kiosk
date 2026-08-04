# Configure the kiosk menu script
kiosk_menu_config = configuration_data()
kiosk_menu_config.set('LOCALEDIR', localedir)
kiosk_menu_config.set('GETTEXT_PACKAGE', meson.project_name())
kiosk_menu_config.set('PACKAGE', meson.project_name())
kiosk_menu_config.set('DATADIR', datadir)

kiosk_menu_script = configure_file(
  input: 'gnome-kiosk-menu.py.in',
  output: 'gnome-kiosk-menu',
  configuration: kiosk_menu_config,
  install: true,
  install_dir: bindir,
  install_mode: 'rwxr-xr-x'
)

# Install configuration file
install_data('kiosk-menu.conf',
  install_dir: join_paths(datadir, meson.project_name())
)

# Install CSS file
install_data('kiosk-menu.css',
  install_dir: join_paths(datadir, meson.project_name())
)

# Desktop file
desktop_file = configure_file(
  input: 'desktop/org.gnome.Kiosk.Menu.desktop.in.in',
  output: 'org.gnome.Kiosk.Menu.desktop.in',
  configuration: desktop_config_data
)

i18n.merge_file(
  input: desktop_file,
  output: 'org.gnome.Kiosk.Menu.desktop',
  po_dir: po_dir,
  install: true,
  install_dir: desktop_data_dir,
  type: 'desktop'
)

# Systemd service file
configure_file(
  input: 'systemd/org.gnome.Kiosk.Menu.service.in',
  output: '@BASENAME@',
  configuration: systemd_service_config_data,
  install_dir: systemd_user_unit_dir
)

# Session target configuration
kiosk_menu_systemd_target_dir = join_paths(systemd_user_unit_dir, 'gnome-session@gnome-kiosk-menu.target.d')
install_data('systemd/session.conf',
  install_dir: kiosk_menu_systemd_target_dir
)

# Session file
install_data('gnome-session/gnome-kiosk-menu.session',
  install_dir: session_dir,
)

# Wayland session file
i18n.merge_file(
  input: 'wayland-sessions/gnome-kiosk-menu-wayland.desktop.in',
  output: '@BASENAME@',
  po_dir: po_dir,
  install: true,
  install_dir: wayland_sessions_dir,
  type: 'desktop'
)
