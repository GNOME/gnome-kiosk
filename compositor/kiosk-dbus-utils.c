#include "config.h"
#include "kiosk-dbus-utils.h"

#include <string.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

char *
kiosk_dbus_utils_escape_object_path (const char *data,
                                     gsize       length)
{
        const char *p;
        GString *string;

        g_return_val_if_fail (data != NULL, NULL);

        string = g_string_sized_new ((length + 1) * 6);

        for (p = data; *p != '\0'; p++) {
                guchar character;

                character = (guchar) * p;

                if (((character >= ((guchar) 'a')) &&
                     (character <= ((guchar) 'z'))) ||
                    ((character >= ((guchar) 'A')) &&
                     (character <= ((guchar) 'Z'))) ||
                    ((character >= ((guchar) '0')) && (character <= ((guchar) '9')))) {
                        g_string_append_c (string, (char) character);
                        continue;
                }

                g_string_append_printf (string, "_%x_", character);
        }

        return g_string_free (string, FALSE);
}
