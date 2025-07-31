# GNOME Kiosk Accessibility Panel

A GTK4-based utility application for controlling accessibility settings through an intuitive interface with toggle switches.

## Features

The accessibility panel provides easy access to the following accessibility settings:

- **Screen Reader**: Announces screen content and controls
- **High Contrast**: Increases contrast for better visibility
- **Large Text**: Increases text size to 125%
- **Visual Alert**: Flash screen instead of playing alert sounds
- **Sticky Keys**: Treat modifier keys as toggle switches
- **Slow Keys**: Add delay between key press and acceptance
- **Bounce Keys**: Ignore rapid key presses
- **Mouse Keys**: Control mouse cursor with numeric keypad

## Usage

Launch the accessibility panel from the applications menu or by running:

```bash
gnome-kiosk-accessibility-panel
```

Toggle any accessibility feature by clicking on the corresponding switch. The settings are applied immediately and persist across sessions.

## Implementation

The application is implemented in Python using:
- GTK4 for the user interface
- GSettings for managing system accessibility preferences
- Appropriate GLib/GObject bindings

## Controlled GSettings

The application manages the following GSettings keys:

| Feature | Schema | Key | Type |
|---------|--------|-----|------|
| Screen Reader | org.gnome.desktop.a11y.applications | screen-reader-enabled | boolean |
| High Contrast | org.gnome.desktop.a11y.interface | high-contrast | boolean |
| Large Text | org.gnome.desktop.interface | text-scaling-factor | float (1.25/1.0) |
| Visual Alert | org.gnome.desktop.wm.preferences | visual-bell | boolean |
| Sticky Keys | org.gnome.desktop.a11y.keyboard | stickykeys-enable | boolean |
| Slow Keys | org.gnome.desktop.a11y.keyboard | slowkeys-enable | boolean |
| Bounce Keys | org.gnome.desktop.a11y.keyboard | bouncekeys-enable | boolean |
| Mouse Keys | org.gnome.desktop.a11y.keyboard | mousekeys-enable | boolean |