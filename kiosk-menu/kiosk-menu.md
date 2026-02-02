# GNOME Kiosk Menu

The Kiosk Menu is a sample application for GNOME Kiosk that provides a simple
fullscreen launcher with a configurable popup menu.

The Kiosk Menu is to demonstrate how to possibly implement a root menu using
GNOME Kiosk features and configurability, yet it is not meant to be used in
production.

In other words, this is just a demo application.

## Overview

When running, the Kiosk Menu displays a fullscreen window showing the desktop
wallpaper (as configured in `org.gnome.desktop.background`). Clicking anywhere
on the screen opens a popup menu at the cursor location, allowing the user to
launch configured applications.

This application is intended as a starting point for building custom kiosk
solutions where users need access to a limited set of applications from a
simple menu interface.

## Session

The Kiosk Menu includes a Wayland session that can be selected from the
display manager (GDM). The session is called "Kiosk Menu Session" and will
start GNOME Kiosk with the Kiosk Menu application.

When the Kiosk Menu application exits, the session terminates.

## Debugging

To run GNOME Kiosk with the Kiosk Menu (for development or testing purpose),
start `gnome-kiosk` using devkit and pass the menu as the session client,
for example:

```
gnome-kiosk --wayland --devkit -- gnome-service-client -t gnome-kiosk-menu -- gnome-kiosk-menu
```

## Configuration File

The menu is configured through a simple text file that lists the applications
to display.

### File Locations

The configuration file (`kiosk-menu.conf`) and the CSS stylesheet
(`kiosk-menu.css`) are searched in the following locations (in order of
priority):

1. `~/.config/gnome-kiosk/` (user configuration)
2. `$XDG_CONFIG_DIRS/gnome-kiosk/` (typically `/etc/xdg/gnome-kiosk/`)
3. `$PREFIX/share/gnome-kiosk/` (system default)

### File Format

The configuration file uses a simple line-based format:

- **Comments**: Lines starting with `#` are comments and are ignored.
- **Empty lines**: Blank lines are ignored.
- **Applications**: Each line contains a `.desktop` file ID (the `.desktop`
  extension is optional).
- **Separators**: Use `--` on a line by itself to add a visual separator
  in the menu.
- **Exit entry**: Use `exit` on a line by itself to add an Exit menu item
  that quits the application and terminates the session.

### Example Configuration

```
# System settings
gnome-background-panel.desktop
gnome-display-panel.desktop
gnome-network-panel.desktop
--
# Applications
org.gnome.Ptyxis.desktop
org.gnome.Calculator.desktop
org.mozilla.firefox.desktop
--
# Exit option
exit
```

This configuration creates a menu with:
- Three system settings panels
- A separator
- Three applications
- Another separator
- An Exit option

### Notes

- Applications are identified by their `.desktop` file ID, which is typically
  the filename of the desktop entry file (e.g., `org.gnome.Calculator.desktop`).
- If a `.desktop` file cannot be found, the entry is skipped and a warning is
  printed to the console.
- The Exit entry is optional. If not included in the configuration, there will
  be no way to exit the application from the menu (the session can still be
  terminated by other means).
- The menu displays application names and icons as defined in their respective
  `.desktop` files.

## Building

The Kiosk Menu is an optional component. To build it, enable the `kiosk-menu`
option when configuring the build:

```
meson setup build -Dkiosk-menu=true
```

## Dependencies

- Python 3
- GTK 4
- GLib/GIO
