#pragma once

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

char *kiosk_dbus_utils_escape_object_path (const char *data,
                                           gsize       length);
G_END_DECLS
