#include "kiosk-input-selector-application.h"

#include <stdlib.h>
#include <string.h>

#include <glib-object.h>
#include <gtk/gtk.h>

#include "org.gnome.Kiosk.h"

struct _KioskInputSelectorApplication
{
        GtkApplication            parent;

        /* weak references */
        GtkWidget                *window;
        GtkWidget                *input_sources_menu_button;
        GMenu                    *input_sources_menu;

        /* strong references */
        GCancellable             *cancellable;
        KioskInputSourcesManager *input_sources_manager;
        GDBusObjectManager       *object_manager;
};

G_DEFINE_TYPE (KioskInputSelectorApplication, kiosk_input_selector_application, GTK_TYPE_APPLICATION)

KioskInputSelectorApplication *
kiosk_input_selector_application_new (void){
        GObject *object;
        guint flags = G_APPLICATION_NON_UNIQUE
                      | G_APPLICATION_HANDLES_COMMAND_LINE;

        object = g_object_new (KIOSK_TYPE_INPUT_SELECTOR_APPLICATION,
                               "application-id", "org.gnome.Kiosk.InputSelector",
                               "flags", flags,
                               NULL);

        return KIOSK_INPUT_SELECTOR_APPLICATION (object);
}

static void
on_activate_switch_action (KioskInputSelectorApplication *self,
                           GVariant                      *parameter)
{
        const char *object_path;

        g_variant_get (parameter, "&o", &object_path);
        g_print ("activated source %s\n", object_path);

        kiosk_input_sources_manager_call_select_input_source_sync (self->input_sources_manager, object_path, NULL, NULL);
}

static void
set_menu_label_from_selected_input_source (KioskInputSelectorApplication *self)
{
        const char *object_path;
        g_autoptr (GDBusObject) object = NULL;
        g_autoptr (KioskInputSource) input_source = NULL;

        object_path = kiosk_input_sources_manager_get_selected_input_source (self->input_sources_manager);
        object = g_dbus_object_manager_get_object (G_DBUS_OBJECT_MANAGER (self->object_manager), object_path);
        input_source = kiosk_object_get_input_source (KIOSK_OBJECT (object));

        gtk_menu_button_set_label (GTK_MENU_BUTTON (self->input_sources_menu_button), kiosk_input_source_get_short_name (input_source));

        g_debug ("KioskInputSelectorApplication: Marking input source %s ('%s', '%s') as selected",
                 object_path,
                 kiosk_input_source_get_backend_type (input_source),
                 kiosk_input_source_get_backend_id (input_source));
}

static void
populate_input_sources_menu_with_input_source_manager (KioskInputSelectorApplication *self)
{
        const char * const *object_paths;
        size_t i;

        gtk_menu_button_popdown (GTK_MENU_BUTTON (self->input_sources_menu_button));
        g_menu_remove_all (self->input_sources_menu);
        object_paths = kiosk_input_sources_manager_get_input_sources (self->input_sources_manager);
        for (i = 0; object_paths[i] != NULL; i++) {
                const char *object_path = object_paths[i];
                g_autoptr (GDBusObject) object = NULL;
                g_autoptr (KioskInputSource) input_source = NULL;
                g_autofree char *action_id = NULL;

                object = g_dbus_object_manager_get_object (G_DBUS_OBJECT_MANAGER (self->object_manager), object_path);
                input_source = kiosk_object_get_input_source (KIOSK_OBJECT (object));

                g_debug ("KioskInputSelectorApplication: %s ('%s', '%s')",
                         object_path,
                         kiosk_input_source_get_backend_type (input_source),
                         kiosk_input_source_get_backend_id (input_source));

                action_id = g_action_print_detailed_name ("win.switch-input-source",
                                                          g_variant_new ("o", object_path));

                g_menu_append (self->input_sources_menu,
                               kiosk_input_source_get_full_name (input_source),
                               action_id);
        }
}

static void
synchronize_input_sources_menu_with_input_source_manager (KioskInputSelectorApplication *self)
{
        g_debug ("KioskInputSelectorApplication: Synchronizing menu with compositor state");
        set_menu_label_from_selected_input_source (self);
        populate_input_sources_menu_with_input_source_manager (self);
}

static void
create_switch_input_source_action (KioskInputSelectorApplication *self)
{
        g_autoptr (GSimpleAction) switch_action = NULL;

        switch_action = g_simple_action_new ("switch-input-source", G_VARIANT_TYPE ("o"));
        g_signal_connect_object (G_OBJECT (switch_action),
                                 "activate",
                                 G_CALLBACK (on_activate_switch_action),
                                 self,
                                 G_CONNECT_SWAPPED);

        g_action_map_add_action (G_ACTION_MAP (self->window), G_ACTION (switch_action));
}

static gboolean
connect_to_input_source_manager (KioskInputSelectorApplication *self)
{
        g_autoptr (GDBusObject) manager_object = NULL;

        self->object_manager = kiosk_object_manager_client_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                                             G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                                                                             "org.gnome.Kiosk",
                                                                             "/org/gnome/Kiosk/InputSources",
                                                                             self->cancellable,
                                                                             NULL);

        manager_object = g_dbus_object_manager_get_object (G_DBUS_OBJECT_MANAGER (self->object_manager), "/org/gnome/Kiosk/InputSources/Manager");
        if (!manager_object) {
                g_critical ("Failed to connect to the input sources manager! Are you running GNOME Kiosk?");
                return FALSE;
        }

        self->input_sources_manager = kiosk_object_get_input_sources_manager (KIOSK_OBJECT (manager_object));

        g_signal_connect_object (G_OBJECT (self->input_sources_manager),
                                 "notify::selected-input-source",
                                 G_CALLBACK (set_menu_label_from_selected_input_source),
                                 self,
                                 G_CONNECT_SWAPPED);
        g_signal_connect_object (G_OBJECT (self->input_sources_manager),
                                 "notify::input-sources",
                                 G_CALLBACK (populate_input_sources_menu_with_input_source_manager),
                                 self,
                                 G_CONNECT_SWAPPED);

        return TRUE;
}

static void
activate (KioskInputSelectorApplication *self)
{
        g_debug ("KioskInputSelectorApplication: Activating");

        gtk_application_add_window (GTK_APPLICATION (self),
                                    GTK_WINDOW (self->window));

        create_switch_input_source_action (self);
        if (!connect_to_input_source_manager (self))
                return;
        synchronize_input_sources_menu_with_input_source_manager (self);
}

static void
kiosk_input_selector_application_activate (GApplication *application)
{
        KioskInputSelectorApplication *self = KIOSK_INPUT_SELECTOR_APPLICATION (application);

        activate (self);

        G_APPLICATION_CLASS (kiosk_input_selector_application_parent_class)->activate (application);
}

static int
kiosk_input_selector_application_command_line (GApplication            *application,
                                               GApplicationCommandLine *command_line)
{
        KioskInputSelectorApplication *self = KIOSK_INPUT_SELECTOR_APPLICATION (application);

        GList *windows;
        GtkWidget *window;

        g_debug ("KioskInputSelectorApplication: Processing command line");
        G_APPLICATION_CLASS (kiosk_input_selector_application_parent_class)->command_line (application, command_line);
        activate (self);

        windows = gtk_application_get_windows (GTK_APPLICATION (self));
        window = GTK_WIDGET (g_list_first (windows)->data);

        gtk_widget_set_visible (GTK_WIDGET (window), TRUE);

        return 0;
}

static void
kiosk_input_selector_application_startup (GApplication *application)
{
        KioskInputSelectorApplication *self = KIOSK_INPUT_SELECTOR_APPLICATION (application);

        g_autoptr (GtkBuilder) builder = NULL;

        g_debug ("KioskInputSelectorApplication: Startup");

        G_APPLICATION_CLASS (kiosk_input_selector_application_parent_class)->startup (application);

        builder = gtk_builder_new_from_resource ("/ui/kiosk-input-selector-application.ui");

        g_set_weak_pointer (&self->window, GTK_WIDGET (gtk_builder_get_object (builder, "window")));
        g_set_weak_pointer (&self->input_sources_menu_button,
                            GTK_WIDGET (gtk_builder_get_object (builder, "input-sources-menu-button")));
        g_set_weak_pointer (&self->input_sources_menu,
                            G_MENU (gtk_builder_get_object (builder, "input-sources-menu")));
}

static void
kiosk_input_selector_application_init (KioskInputSelectorApplication *application)
{
        KioskInputSelectorApplication *self = KIOSK_INPUT_SELECTOR_APPLICATION (application);

        g_debug ("KioskInputSelectorApplication: Initializing");
        self->cancellable = g_cancellable_new ();
}

static void
kiosk_input_selector_application_dispose (GObject *object)
{
        KioskInputSelectorApplication *self = KIOSK_INPUT_SELECTOR_APPLICATION (object);

        if (self->cancellable != NULL) {
                g_cancellable_cancel (self->cancellable);
                g_clear_object (&self->cancellable);
        }

        g_clear_weak_pointer (&self->window);
        g_clear_weak_pointer (&self->input_sources_menu_button);
        g_clear_weak_pointer (&self->input_sources_menu);

        g_clear_object (&self->object_manager);
        g_clear_object (&self->input_sources_manager);
        G_OBJECT_CLASS (kiosk_input_selector_application_parent_class)->dispose (object);
}

static void
kiosk_input_selector_application_class_init (KioskInputSelectorApplicationClass *input_selector_application_class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (input_selector_application_class);
        GApplicationClass *application_class = G_APPLICATION_CLASS (input_selector_application_class);

        object_class->dispose = kiosk_input_selector_application_dispose;

        application_class->activate = kiosk_input_selector_application_activate;
        application_class->command_line = kiosk_input_selector_application_command_line;
        application_class->startup = kiosk_input_selector_application_startup;
}
