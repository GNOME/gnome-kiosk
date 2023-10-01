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

static char *
dashed_string_to_studly_caps (const char *dashed_string)
{
        char *studly_string;
        size_t studly_string_length;
        size_t i;

        i = 0;

        studly_string = g_strdup (dashed_string);
        studly_string_length = strlen (studly_string);

        studly_string[i] = g_ascii_toupper (studly_string[i]);
        i++;

        while (i < studly_string_length) {
                if (studly_string[i] == '-' || studly_string[i] == '_') {
                        memmove (studly_string + i, studly_string + i + 1, studly_string_length - i - 1);
                        studly_string_length--;
                        if (g_ascii_isalpha (studly_string[i])) {
                                studly_string[i] = g_ascii_toupper (studly_string[i]);
                        }
                }
                i++;
        }
        studly_string[studly_string_length] = '\0';

        return studly_string;
}

static char *
dashed_string_to_dbus_error_string (const char *dashed_string,
                                    const char *old_prefix,
                                    const char *new_prefix,
                                    const char *suffix)
{
        char *studly_suffix;
        char *dbus_error_string;
        size_t dbus_error_string_length;
        size_t i;

        i = 0;

        if (g_str_has_prefix (dashed_string, old_prefix) &&
            (dashed_string[strlen (old_prefix)] == '-' ||
             dashed_string[strlen (old_prefix)] == '_')) {
                dashed_string += strlen (old_prefix) + 1;
        }

        studly_suffix = dashed_string_to_studly_caps (suffix);
        dbus_error_string = g_strdup_printf ("%s.%s.%s",
                                             new_prefix,
                                             dashed_string,
                                             studly_suffix);
        g_free (studly_suffix);
        i += strlen (new_prefix) + 1;

        dbus_error_string_length = strlen (dbus_error_string);

        dbus_error_string[i] = g_ascii_toupper (dbus_error_string[i]);
        i++;

        while (i < dbus_error_string_length) {
                if (dbus_error_string[i] == '_' || dbus_error_string[i] == '-') {
                        dbus_error_string[i] = '.';

                        if (g_ascii_isalpha (dbus_error_string[i + 1])) {
                                dbus_error_string[i + 1] = g_ascii_toupper (dbus_error_string[i + 1]);
                        }
                }

                i++;
        }

        return dbus_error_string;
}

void
kiosk_dbus_utils_register_error_domain (GQuark  error_domain,
                                        GType   error_enum)
{
        const char *error_domain_string;
        g_autofree char *type_name = NULL;
        GType type;
        g_autoptr (GTypeClass) type_class = NULL;
        const GEnumClass *enum_class;
        guint i;

        error_domain_string = g_quark_to_string (error_domain);
        type_name = dashed_string_to_studly_caps (error_domain_string);
        type = g_type_from_name (type_name);
        type_class = g_type_class_ref (type);

        if (type_class == NULL) {
                g_warning ("kiosk-dbus-utils: Could not identify type %s", type_name);
                return;
        }

        enum_class = G_ENUM_CLASS (type_class);

        for (i = 0; i < enum_class->n_values; i++) {
                g_autofree char *dbus_error_string = NULL;

                dbus_error_string = dashed_string_to_dbus_error_string (error_domain_string,
                                                                        "kiosk",
                                                                        "org.gnome",
                                                                        enum_class->values[i].
                                                                        value_nick);

                g_debug ("kiosk-dbus-utils: Registering dbus error %s", dbus_error_string);
                g_dbus_error_register_error (error_domain,
                                             enum_class->values[i].value, dbus_error_string);
        }
}
