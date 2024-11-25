#!/usr/bin/env python3

import os
import shutil

destdir = os.environ.get('DESTDIR', '/')
prefix = os.environ.get('MESON_INSTALL_PREFIX', '/usr/local')
datadir = os.path.join(destdir + prefix, 'share')

wayland_sessions_dir = os.path.join(datadir, 'wayland-sessions')
if not os.path.exists(wayland_sessions_dir):
    os.makedirs(wayland_sessions_dir)

source_file = os.path.join(datadir, 'xsessions', 'org.gnome.Kiosk.SearchApp.Session.desktop')
destination_file = os.path.join(wayland_sessions_dir, 'org.gnome.Kiosk.SearchApp.Session.desktop')
shutil.copyfile(source_file, destination_file)
