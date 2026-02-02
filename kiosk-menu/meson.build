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
