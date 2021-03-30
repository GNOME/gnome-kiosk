#!/usr/bin/env python3

import os
import shutil
import subprocess
import sys

destdir = os.environ.get('DESTDIR', '/')
prefix = os.environ.get('MESON_INSTALL_PREFIX', '/usr/local')
datadir = os.path.join(destdir + prefix, 'share')

wayland_sessions_dir = os.path.join(datadir, 'wayland-sessions')
if not os.path.exists(wayland_sessions_dir):
    os.makedirs(wayland_sessions_dir)

source_file = os.path.join(datadir, 'xsessions', 'org.gnome.Kiosk.SearchApp.Session.desktop')
destination_file = os.path.join(wayland_sessions_dir, 'org.gnome.Kiosk.SearchApp.Session.desktop')
shutil.copyfile(source_file, destination_file)

# Packaging tools define DESTDIR and this isn't needed for them
if 'DESTDIR' not in os.environ:
    print('Updating icon cache...')
    icon_cache_dir = os.path.join(datadir, 'icons', 'hicolor')
    if not os.path.exists(icon_cache_dir):
        os.makedirs(icon_cache_dir)
    subprocess.call(['gtk-update-icon-cache', '-qtf', icon_cache_dir])

    print('Updating desktop database...')
    desktop_database_dir = os.path.join(datadir, 'applications')
    if not os.path.exists(desktop_database_dir):
        os.makedirs(desktop_database_dir)
    subprocess.call(['update-desktop-database', '-q', desktop_database_dir])

    print('Compiling GSettings schemas...')
    schemas_dir = os.path.join(datadir, 'glib-2.0', 'schemas')
    if not os.path.exists(schemas_dir):
        os.makedirs(schemas_dir)
    subprocess.call(['glib-compile-schemas', schemas_dir])
