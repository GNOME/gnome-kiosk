#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <glib/gi18n.h>

#include <meta/main.h>
#include <meta/meta-plugin.h>
#include <meta/prefs.h>

#include "kiosk-compositor.h"

static gboolean
print_version (const gchar    *option_name,
               const gchar    *value,
               gpointer        data,
               GError        **error)
{
        g_print ("Kiosk %s\n", VERSION);
        exit (0);
}

static GOptionEntry
kiosk_options[] = {
        {
                "version", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
                print_version,
                N_("Print version"),
                NULL
        },
        { NULL }
};

int
main (int argc, char **argv)
{
        g_autoptr (GOptionContext) option_context = NULL;
        g_autoptr (GError) error = NULL;
        int exit_code;

        bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        option_context = meta_get_option_context ();
        g_option_context_add_main_entries (option_context, kiosk_options, GETTEXT_PACKAGE);
        if (!g_option_context_parse (option_context, &argc, &argv, &error)) {
                g_printerr ("%s: %s\n", argv[0], error->message);
                exit (1);
        }

        meta_plugin_manager_set_plugin_type (KIOSK_TYPE_COMPOSITOR);

        meta_set_wm_name ("Kiosk");

        /* Prevent meta_init() from causing gtk to load the atk-bridge */
        g_setenv ("NO_AT_BRIDGE", "1", TRUE);
        meta_init ();
        g_unsetenv ("NO_AT_BRIDGE");

        exit_code = meta_run ();

        return exit_code;
}
