# Configure the accessibility panel script with proper locale directory
accessibility_panel_app_id = 'org.gnome.Kiosk.AccessibilityPanel'

accessibility_panel_config = configuration_data()
accessibility_panel_config.set('LOCALEDIR', localedir)
accessibility_panel_config.set('GETTEXT_PACKAGE', meson.project_name())
accessibility_panel_config.set('A11Y_PANEL_APP_ID', accessibility_panel_app_id)

accessibility_panel_script = configure_file(
  input: 'accessibility-panel.py.in',
  output: 'gnome-kiosk-accessibility-panel',
  configuration: accessibility_panel_config,
  install: true,
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x'
)

# Desktop file for the accessibility panel with translation support
accessibility_panel_desktop_file_input = accessibility_panel_app_id + '.desktop.in'
accessibility_panel_desktop_file = i18n.merge_file(
  input: accessibility_panel_desktop_file_input,
  output: '@BASENAME@',
  type: 'desktop',
  po_dir: po_dir,
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications')
)
