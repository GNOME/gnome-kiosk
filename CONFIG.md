# Configuration file

GNOME Kiosk takes a configuration file to specify the windows configuration at start-up.

The configuration file called `window-config.ini` is searched in multiple places on the
system. The first instance of the file found is used.

 * The base directory in which user-specific application configuration is stored
   `$XDG_CONFIG_HOME/gnome-kiosk/window-config.ini` (usually `$HOME/.config/gnome-kiosk/window-config.ini`)
 * The system-wide list of directories in which system-wide application data is stored `$XDG_DATA_DIRS`
   This list usually includes:
    - `/var/lib/flatpak/exports/share/gnome-kiosk/window-config.ini`
    - `/usr/local/share/gnome-kiosk/window-config.ini`
    - `/usr/share/gnome-kiosk/window-config.ini`

# Syntax

The configuration file is an "ini" style file with sections and keys/values.

There can be as many sections as desired.

The name of the sections does not matter, there is no special name of section,
each section gets evaluated.

There are two categories of keys, the "*match*" keys and the "*set*" keys.

The "*match*" keys are used to filter the windows before applying the
values from the "*set*" keys.

The "*match*" keys can take wildcards and patterns.

The following "*match*" keys as supported:

 * `match-title` (string) - Matches the window title
 * `match-class` (string) - Matches the window class
 * `match-sandboxed-app-id` (string) - Matches the sandboxed application id
 * `match-tag` (string)   - Matches the window tag

The following "*set*" keys are supported:

 * `set-fullscreen` (boolean) - Whether the window should be fullscreen
 * `set-x` (integer) - the X position
 * `set-y` (integer) - the Y position
 * `set-width` (integer) - the width
 * `set-height` (integer) - the height
 * `set-above` (boolean) - Whether the window should be placed on a layer above
 * `set-on-monitor` (string) - Place the window on the given monitor

Notes:

The name of the monitor to use for `set-on-monitor` is from the output
name as reported by `xrandr` on X11 or `wayland-info` on Wayland.

When `set-x`/`set-y` are used in with `set-on-monitor`, the actual location
is relative to the monitor.

# Example

```
  # Place all windows at (0,0) by default, not fullscreen
  [all]
  set-x=0
  set-y=0
  set-fullscreen=false
  # The following will place all windows on the same layer
  set-above=false

  # Make all Mozilla windows fullscreen on the laptop panel named "eDP-1"
  [mozilla]
  match-class=org.mozilla.*
  set-fullscreen=true
  set-on-monitor=eDP-1

  # All other windows will be set fullscreen automatically using the
  # existing GNOME Kiosk heuristic, as before.
```
