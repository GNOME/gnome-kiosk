#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <glib-unix.h>
#include <glib/gi18n.h>

#include <meta/meta-context.h>
#include <meta/meta-plugin.h>
#include <meta/prefs.h>

#include "kiosk-compositor.h"

static char **argv_ignored = NULL;

static void
command_exited_cb (GPid      command_pid,
                   int       status,
                   gpointer  user_data)
{
        MetaContext *context = user_data;
        GError *error;

        g_spawn_close_pid (command_pid);

        if (status) {
                error = g_error_new (G_IO_ERROR, G_IO_ERROR_FAILED,
                                     "The command exited with a nonzero status: %d\n",
                                     status);

                meta_context_terminate_with_error (context, error);
        } else {
                meta_context_terminate (context);
        }
}

static int
start_command (MetaContext *context)
{
        GPid command_pid;
        g_autoptr (GError) error = NULL;
        g_auto (GStrv) command_argv = NULL;

        if (argv_ignored) {
                command_argv = g_steal_pointer (&argv_ignored);

                if (!g_spawn_async (NULL, command_argv, NULL,
                                    G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
                                    NULL, NULL, &command_pid, &error)) {
                        g_printerr ("Failed to run the command: %s\n", error->message);

                        return EXIT_FAILURE;
                }

                g_child_watch_add (command_pid, command_exited_cb, context);
        }

        return EXIT_SUCCESS;
}

static gboolean
print_version (const gchar *option_name,
               const gchar *value,
               gpointer     data,
               GError     **error)
{
        g_print ("Kiosk %s\n", VERSION);
        exit (0);
}

static GOptionEntry
        kiosk_options[] = {
        {
                "version", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
                print_version,
                N_ ("Print version"),
                NULL
        },
        {
                G_OPTION_REMAINING,
                .arg = G_OPTION_ARG_STRING_ARRAY,
                &argv_ignored,
                .arg_description = "[[--] COMMAND [ARGUMENT…]]"
        },
        { NULL }
};

static void
set_working_directory (void)
{
        const char *working_directory;
        int result;

        working_directory = g_get_home_dir ();

        if (working_directory == NULL)
                working_directory = "/";

        result = chdir (working_directory);

        if (result != 0) {
                g_warning ("Could not change working directory to '%s': %m",
                           working_directory);
        }
}

static void
set_dconf_profile (void)
{
        setenv ("DCONF_PROFILE", "gnomekiosk", TRUE);
}

static gboolean
on_termination_signal (MetaContext *context)
{
        meta_context_terminate (context);

        return G_SOURCE_REMOVE;
}

int
main (int    argc,
      char **argv)
{
        g_autoptr (MetaContext) context = NULL;
        g_autoptr (GError) error = NULL;

        bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        signal (SIGPIPE, SIG_IGN);

        set_working_directory ();
        set_dconf_profile ();

        context = meta_create_context ("Kiosk");
        meta_context_add_option_entries (context, kiosk_options, GETTEXT_PACKAGE);
        if (!meta_context_configure (context, &argc, &argv, &error)) {
                g_printerr ("%s: Configuration failed: %s\n", argv[0], error->message);
                exit (1);
        }

        meta_context_set_plugin_gtype (context, KIOSK_TYPE_COMPOSITOR);

        if (!meta_context_setup (context, &error)) {
                g_printerr ("%s: Setup failed: %s\n", argv[0], error->message);
                exit (1);
        }

        if (!meta_context_start (context, &error)) {
                g_printerr ("%s: Failed to start: %s\n", argv[0], error->message);
                exit (1);
        }

        g_unix_signal_add (SIGTERM, (GSourceFunc) on_termination_signal, context);

        if (argv_ignored) {
                if (start_command (context) == EXIT_FAILURE)
                        return EXIT_FAILURE;
        }

        if (!meta_context_run_main_loop (context, &error)) {
                g_printerr ("%s: Quit unexpectedly: %s\n", argv[0], error->message);
                exit (1);
        }

        return 0;
}
