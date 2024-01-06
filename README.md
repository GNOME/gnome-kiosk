# GNOME Kiosk
## Mutter based compositor for kiosks
GNOME Kiosk provides a desktop environment suitable for fixed purpose, or
single application deployments like wall displays and point-of-sale systems.

It provides a very minimal wayland display server and compositor and Xorg
compositor and window manager. It automatically starts applications fullscreen.

Notably, GNOME Kiosk features no panels, dashes, or docks that could distract
from the application using it as a platform.

## Sample application
In order to demonstrate how GNOME Kiosk functions, there is one provided sample
application. It is a search appliance that shows https://www.google.com in a
full screen Mozilla Firefox window.

The search appliance ships with three parts:

1. A GNOME Display Manager session file that gets installed in
   `/usr/share/xsessions` and `/usr/share/wayland-sessions`.
   This file is responsible for telling the display manager how to start the
   session. It informs the display manager to start GNOME Session in a special,
   custom mode, designed specifically for setting up the kiosk environment.
1. A GNOME Session session description file that gets installed in
   `/usr/share/gnome-session/sessions`. This file is responsible for telling
   GNOME Session which components to start for the kiosk environment. The two
   components the session description file describes are GNOME Kiosk and
   Mozilla Firefox.
1. A custom application desktop file for firefox to run it in its kiosk mode pointed
   at https://www.google.com

## Keyboard layout switching
GNOME Kiosk will automatically set up the list of available keyboard layouts based
on GSettings and failing that, localed configuration.

Layout switching can be performed using the standard GNOME keybindings, which default
to Super-Space an Shift-Super-Space, and may be reconfigured using GSettings.

Because a central design ideal behind GNOME Kiosk is that it offers no persistent UI
elements, there is no builtin way aside from the above mentioned keybindings to change
keyboard layouts.

Deployments that require an on screen keyboard layout indicator need to implement it
themselves as part of their fullscreen application. GNOME Kiosk provides D-Bus APIs
to set, list, and switch between keyboard layouts.

A sample application is provided to show how these APIs function.

## Future
GNOME Kiosk is still in early development. Use cases and new ideas are actively
being considered. Some future improvements that may occur:

### On-screen keyboard
It's clear that not all target deployments where GNOME Kiosk is used will have keyboards.
It's likely that in the future GNOME Kiosk will ship with an optional on screen keyboard.

### Better input method support
At the moment GNOME Kiosk has limited support for extended methods (such as Chinese input),
using IBus. The functionality relies on IBus's own UI for selecting input candidates.

It's possible that at some point GNOME Kiosk will add some dedicated UI for input methods to
use, instead of relying on IBus's UI.

### Screen locking
One use case for GNOME Kiosk is to show a non-interactive display most of the time, but
provide a way for someone to unlock the display and interact with the application.

At the moment, it's up to the application to lock itself down, but in the future, GNOME
Kiosk may provide a transparent screen lock option, to assist the application.

## Contributing

To contribute, open merge requests at https://gitlab.gnome.org/GNOME/gnome-kiosk.

## License
GNOME Kiosk is distributed under the terms of the GNU General Public License,
version 2 or later. See the [COPYING][license] file for details.

[license]: COPYING

NOTE: Much of the original code is copyright Red Hat, Inc., but there is no copyright assignment and individual contributions are subject to the copyright of their contributors.
