# GNOME desktop configuration

GNOME Kiosk is built on mutter, therefore the same GNOME Desktop settings
which apply to mutter will also apply to GNOME Kiosk.

## Disabling animations

```sh
gsettings set org.gnome.desktop.interface enable-animations false
```

# Accessibility settings

## Enabling High Contrast

```sh
gsettings set org.gnome.desktop.a11y.interface high-contrast true
gsettings set org.gnome.desktop.a11y.interface show-status-shapes true
```

## Large text

```sh
gsettings set org.gnome.desktop.interface text-scaling-factor 1.25
```

## Visual alerts

```sh
gsettings set org.gnome.desktop.wm.preferences visual-bell true
```

## Sticky Keys

```sh
gsettings set org.gnome.desktop.a11y.keyboard stickykeys-enable true
```

## Slow Keys

```sh
gsettings set org.gnome.desktop.a11y.keyboard slowkeys-enable true
```

## Bounce Keys

```sh
gsettings set org.gnome.desktop.a11y.keyboard bouncekeys-enable true
```

## Mouse keys

```sh
gsettings set org.gnome.desktop.a11y.keyboard mousekeys-enable true
```

## Screen Magnifier

Enable screen magnification:

```sh
gsettings set org.gnome.desktop.a11y.applications screen-magnifier-enabled true
```

The magnification factor can be adjusted with:

```sh
gsettings set org.gnome.desktop.a11y.magnifier mag-factor 2.0
```

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

## Syntax

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
 * `match-window-type` (string) - Matches the window type (`normal`, `dialog`, `menu`).

The following "*set*" keys are supported:

 * `set-fullscreen` (boolean) - Whether the window should be fullscreen
 * `set-x` (integer) - the X position
 * `set-y` (integer) - the Y position
 * `set-width` (integer) - the width
 * `set-height` (integer) - the height
 * `set-above` (boolean) - Whether the window should be placed on a layer above
 * `set-on-monitor` (string) - Place the window on the given monitor
 * `lock-on-monitor` (boolean) - Lock the window on the monitor
 * `lock-on-monitor-area` (string) - Lock the window within a specific area on the monitor (format: "x,y WxH")
 * `lock-on-area` (string) - Lock the window within a specific area using absolute coordinates (format: "x,y WxH")
 * `set-window-type` (string) - Change the window type
 * `lock-move` (boolean) - Prevent the user from moving the window
 * `lock-resize` (boolean) - Prevent the user from resizing the window
 * `set-strut` (string) - Reserve a screen area as a strut based on the window geometry (format: "side"); only applies to `dock` windows

Notes:

The name of the monitor to use for `set-on-monitor` is from the output
name as reported by `wayland-info` on Wayland.

Only a subset of window types are supported with `set-window-type`, namely:
 * 'desktop': This is intended for implementing desktop windows, usually a fullscreen window that can contain icons, menus, etc.
 * 'dock': This is intended for dock windows or panels. Such windows will be placed above the others.
 * 'splash': This typically for windows shown at startup.

When `set-x`/`set-y` are used in with `set-on-monitor`, the actual location
is relative to the monitor.

The `lock-on-monitor` option, when set to `true`, locks the window to the monitor
specified by `set-on-monitor`.<br>
The window will be hidden if the monitor is removed and shown again when the monitor
is reconnected.

The `lock-on-monitor-area` option constrains a window to stay within a specific
rectangular area on the monitor specified by `set-on-monitor`.<br>
This option only applies to windows that have `set-on-monitor` configured.<br>
The window will be hidden if the monitor is removed and shown again when the monitor
is reconnected, just like with `lock-on-monitor`.<br>
The area is defined in the format "x,y WxH" where:

 * `x,y` are the coordinates of the top-left corner of the area, relative to the monitor
 * `W` is the width of the area
 * `H` is the height of the area
 * The coordinates of the areas are relative to the monitor's top-left corner
 * Width and height must be positive values (> 0)

The `lock-on-area` option constrains a window to stay within a specific rectangular
area using absolute screen coordinates.<br>
Unlike `lock-on-monitor-area`, this option does not require `set-on-monitor` and the
window will not be hidden when monitors are added or removed.<br>
The area is defined in the format "x,y WxH" where:

 * `x,y` are the absolute coordinates of the top-left corner of the area
 * `W` is the width of the area
 * `H` is the height of the area
 * Width and height must be positive values (> 0)

The `lock-move` option, when set to `true`, prevents the user from moving
the window by dragging it or through other interactive move operations.

Resizing a window may also imply a move when resized from the top, the left,
or from the top-left corner. In that case, if `lock-move` is set, the resize
will be prevented as well along the affected axis.

The `lock-resize` option, when set to `true`, prevents the user from resizing
the window interactively.

These options can be combined with `lock-on-area` and `lock-on-monitor-area`:
the area constraints keep the window within bounds, while `lock-move` prevents the
user from changing the window position.

The `set-strut` option reserves a rectangular area of the screen as a strut,
reducing the available work area for other windows.<br>
The strut is associated with the matching window and removed automatically when
that window is closed.<br>
It only applies to windows of type `dock` (for example when using
`set-window-type=dock`) and is ignored for other window types.<br>
The value is a side name (`top`, `bottom`, `left`, or `right`). The strut
rectangle is the intersection of the window with each monitor it overlaps:

 * The strut covers the window's footprint on that monitor
 * If a window spans two monitors, struts are created on both
 * The strut is not applied when its thickness exceeds 75% of the monitor
   width (for left/right) or height (for top/bottom), to avoid reserving
   almost the entire work area
 * The strut rectangle is recomputed automatically when the window moves or is
   resized, and when the screen layout changes

## Example

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

  # Lock a specific window within a 800x600 area starting at (100,100)
  # on monitor "HDMI-1", relative to the monitor's location
  [restricted-app]
  match-class=RestrictedApp
  set-on-monitor=HDMI-1
  lock-on-monitor-area=100,100 800x600

  # Lock a window within a 640x480 area at absolute position (200,150)
  # This does not depend on any specific monitor
  [fixed-position-app]
  match-class=FixedApp
  lock-on-area=200,150 640x480

  # Fix a window at a specific size and position, then prevent user move/resize
  [kiosk-app]
  match-class=KioskApp
  set-x=100
  set-y=100
  set-width=800
  set-height=600
  lock-move=true
  lock-resize=true

  # Set the window type to match the window tag name for the supported types
  [desktop]
  match-tag=desktop
  set-window-type=desktop
  set-fullscreen=true

  [dock]
  match-tag=dock
  set-window-type=dock
  set-fullscreen=false
  set-strut=top

  [splash]
  match-tag=splash
  set-window-type=splash
  set-fullscreen=true

  # All other windows will be set fullscreen automatically using the
  # existing GNOME Kiosk heuristic, as before.
```
