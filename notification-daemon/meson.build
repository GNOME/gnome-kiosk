# Configure the notification daemon script
notification_daemon_app_id = 'org.gnome.Kiosk.NotificationDaemon'
libexecdir = join_paths(prefix, get_option('libexecdir'))

notification_daemon_config = configuration_data()
notification_daemon_config.set('LOCALEDIR', localedir)
notification_daemon_config.set('GETTEXT_PACKAGE', meson.project_name())
notification_daemon_config.set('PACKAGE', meson.project_name())
notification_daemon_config.set('NOTIFICATION_DAEMON_APP_ID', notification_daemon_app_id)
notification_daemon_config.set('DATADIR', datadir)

notification_daemon_script = configure_file(
  input: 'notification-daemon.py.in',
  output: 'gnome-kiosk-notification-daemon',
  configuration: notification_daemon_config,
  install: true,
  install_dir: get_option('libexecdir'),
  install_mode: 'rwxr-xr-x'
)

# Install CSS file
install_data(
  'notification-daemon.css',
  install_dir: join_paths(datadir, meson.project_name())
)

# D-Bus and systemd service files
notification_daemon_service_config = configuration_data()
notification_daemon_service_config.set('libexecdir', libexecdir)
notification_daemon_service_config.set('bindir', bindir)

# D-Bus service files for bus activation
# org.freedesktop.Notifications
configure_file(
  input: 'org.freedesktop.Notifications.service.in',
  output: 'org.freedesktop.Notifications.service',
  configuration: notification_daemon_service_config,
  install: true,
  install_dir: join_paths(datadir, 'dbus-1', 'services')
)

# org.gtk.Notifications
configure_file(
  input: 'org.gtk.Notifications.service.in',
  output: 'org.gtk.Notifications.service',
  configuration: notification_daemon_service_config,
  install: true,
  install_dir: join_paths(datadir, 'dbus-1', 'services')
)

# Systemd user service file
configure_file(
  input: 'gnome-kiosk-notification-daemon.service.in',
  output: 'gnome-kiosk-notification-daemon.service',
  configuration: notification_daemon_service_config,
  install: true,
  install_dir: systemd_user_unit_dir
)
