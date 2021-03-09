#ifndef KIOSK_DBUS_UTILS_H
#define KIOSK_DBUS_UTILS_H

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

void kiosk_dbus_utils_register_error_domain (GQuark error_domain,
                                             GType  error_enum);

G_END_DECLS
#endif
