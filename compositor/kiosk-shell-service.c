#include "config.h"
#include "kiosk-shell-service.h"

#include <stdlib.h>
#include <string.h>
#include <meta/display.h>
#include <meta/util.h>

#include "kiosk-compositor.h"

#define KIOSK_SHELL_SERVICE_BUS_NAME "org.gnome.Shell"
#define KIOSK_SHELL_SERVICE_OBJECT_PATH "/org/gnome/Shell"

struct _KioskShellService
{
        KioskShellDBusServiceSkeleton parent;

        /* weak references */
        KioskCompositor              *compositor;
        MetaDisplay                  *display;

        /* strong references */
        GHashTable                   *client_bus_watch_ids;
        GHashTable                   *grabbed_accelerators;

        /* handles */
        guint                         bus_id;
};

enum
{
        PROP_COMPOSITOR = 1,
        NUMBER_OF_PROPERTIES
};
static GParamSpec *kiosk_shell_service_properties[NUMBER_OF_PROPERTIES] = { NULL, };

static void kiosk_shell_dbus_service_interface_init (KioskShellDBusServiceIface *interface);

G_DEFINE_TYPE_WITH_CODE (KioskShellService,
                         kiosk_shell_service,
                         KIOSK_TYPE_SHELL_DBUS_SERVICE_SKELETON,
                         G_IMPLEMENT_INTERFACE (KIOSK_TYPE_SHELL_DBUS_SERVICE,
                                                kiosk_shell_dbus_service_interface_init));

static void kiosk_shell_service_set_property (GObject      *object,
                                              guint         property_id,
                                              const GValue *value,
                                              GParamSpec   *param_spec);
static void kiosk_shell_service_get_property (GObject    *object,
                                              guint       property_id,
                                              GValue     *value,
                                              GParamSpec *param_spec);

static void kiosk_shell_service_constructed (GObject *object);
static void kiosk_shell_service_dispose (GObject *object);

static void
kiosk_shell_service_class_init (KioskShellServiceClass *shell_service_class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (shell_service_class);

        object_class->constructed = kiosk_shell_service_constructed;
        object_class->set_property = kiosk_shell_service_set_property;
        object_class->get_property = kiosk_shell_service_get_property;
        object_class->dispose = kiosk_shell_service_dispose;

        kiosk_shell_service_properties[PROP_COMPOSITOR] = g_param_spec_object ("compositor",
                                                                               "compositor",
                                                                               "compositor",
                                                                               KIOSK_TYPE_COMPOSITOR,
                                                                               G_PARAM_CONSTRUCT_ONLY
                                                                               | G_PARAM_WRITABLE
                                                                               | G_PARAM_STATIC_NAME
                                                                               | G_PARAM_STATIC_NICK
                                                                               | G_PARAM_STATIC_BLURB);
        g_object_class_install_properties (object_class, NUMBER_OF_PROPERTIES, kiosk_shell_service_properties);
}

static void
kiosk_shell_service_set_property (GObject      *object,
                                  guint         property_id,
                                  const GValue *value,
                                  GParamSpec   *param_spec)
{
        KioskShellService *self = KIOSK_SHELL_SERVICE (object);

        switch (property_id) {
        case PROP_COMPOSITOR:
                g_set_weak_pointer (&self->compositor, g_value_get_object (value));
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, param_spec);
                break;
        }
}

static void
kiosk_shell_service_get_property (GObject    *object,
                                  guint       property_id,
                                  GValue     *value,
                                  GParamSpec *param_spec)
{
        switch (property_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, param_spec);
                break;
        }
}

static void
kiosk_shell_service_dispose (GObject *object)
{
        KioskShellService *self = KIOSK_SHELL_SERVICE (object);

        kiosk_shell_service_stop (self);

        g_clear_pointer (&self->client_bus_watch_ids, g_hash_table_unref);
        g_clear_pointer (&self->grabbed_accelerators, g_hash_table_unref);

        g_clear_weak_pointer (&self->display);
        g_clear_weak_pointer (&self->compositor);

        G_OBJECT_CLASS (kiosk_shell_service_parent_class)->dispose (object);
}

static void
kiosk_shell_service_constructed (GObject *object)
{
        KioskShellService *self = KIOSK_SHELL_SERVICE (object);

        G_OBJECT_CLASS (kiosk_shell_service_parent_class)->constructed (object);

        g_set_weak_pointer (&self->display, meta_plugin_get_display (META_PLUGIN (self->compositor)));
}

static void
stop_watching_client (KioskShellService *self,
                      const char        *client_unique_name)
{
        guint bus_watch_id;

        bus_watch_id = GPOINTER_TO_UINT (g_hash_table_lookup (self->client_bus_watch_ids,
                                                              client_unique_name));
        if (bus_watch_id == 0) {
                return;
        }

        g_debug ("KioskShellService: No longer watching client %s", client_unique_name);

        g_bus_unwatch_name (bus_watch_id);
        g_hash_table_remove (self->client_bus_watch_ids, client_unique_name);
}

static void
stop_watching_clients (KioskShellService *self)
{
        GHashTableIter iter;
        gpointer key, value;

        g_debug ("KioskShellService: Dropping all client watches");

        g_hash_table_iter_init (&iter, self->client_bus_watch_ids);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                const char *client_unique_name = key;
                guint bus_watch_id = GPOINTER_TO_UINT (value);

                g_debug ("KioskShellService: No longer watching client %s", client_unique_name);
                g_bus_unwatch_name (bus_watch_id);
        }

        g_hash_table_remove_all (self->client_bus_watch_ids);
}

static void
on_client_vanished (GDBusConnection   *connection,
                    const char        *client_unique_name,
                    KioskShellService *self)
{
        GHashTableIter iter;
        gpointer key, value;

        g_debug ("KioskShellService: Client %s vanished", client_unique_name);

        g_hash_table_iter_init (&iter, self->grabbed_accelerators);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                guint action_id = GPOINTER_TO_UINT (key);
                const char *unique_name = value;

                if (g_strcmp0 (client_unique_name, unique_name) != 0) {
                        continue;
                }

                g_debug ("KioskShellService: Ungrabbing accelerator with id %d",
                         action_id);
        }

        stop_watching_client (self, client_unique_name);
}

static void
watch_client (KioskShellService *self,
              const char        *client_unique_name)
{
        guint bus_watch_id;

        if (g_hash_table_contains (self->client_bus_watch_ids,
                                   client_unique_name)) {
                return;
        }

        g_debug ("KioskShellService: Watching client %s", client_unique_name);

        bus_watch_id = g_bus_watch_name (G_BUS_TYPE_SESSION,
                                         client_unique_name,
                                         G_BUS_NAME_WATCHER_FLAGS_NONE,
                                         (GBusNameAppearedCallback) NULL,
                                         (GBusNameVanishedCallback)
                                         on_client_vanished,
                                         self,
                                         NULL);

        g_hash_table_insert (self->client_bus_watch_ids,
                             g_strdup (client_unique_name),
                             GUINT_TO_POINTER (bus_watch_id));
}

static guint
grab_accelerator_for_client (KioskShellService *self,
                             const char        *accelerator,
                             guint              mode_flags,
                             guint              grab_flags,
                             const char        *client_unique_name)
{
        guint action_id;

        g_debug ("KioskShellService: Grabbing accelerator '%s' with flags %x for client %s",
                 accelerator, grab_flags, client_unique_name);

        action_id = meta_display_grab_accelerator (self->display, accelerator, grab_flags);

        if (action_id == 0) {
                g_debug ("KioskShellService: Grabbing failed");
                return action_id;
        }

        watch_client (self, client_unique_name);
        g_hash_table_insert (self->grabbed_accelerators,
                             GUINT_TO_POINTER (action_id),
                             g_strdup (client_unique_name));

        return action_id;
}

static gboolean
ungrab_accelerator_for_client (KioskShellService *self,
                               guint              action_id,
                               const char        *client_unique_name)
{
        const char *grabbing_client;
        gboolean ungrab_succeeded;

        g_debug ("KioskShellService: Ungrabbing accelerator with id '%d' for client %s",
                 action_id, client_unique_name);

        grabbing_client = g_hash_table_lookup (self->grabbed_accelerators,
                                               GUINT_TO_POINTER (action_id));

        if (g_strcmp0 (grabbing_client, client_unique_name) != 0) {
                g_debug ("KioskShellService: Client %s does not have grab on accelerator with id '%d'", client_unique_name, action_id);
                return FALSE;
        }

        ungrab_succeeded = meta_display_ungrab_accelerator (self->display, action_id);

        if (ungrab_succeeded) {
                g_debug ("KioskShellService: Ungrab succeeded");
                g_hash_table_remove (self->grabbed_accelerators,
                                     GUINT_TO_POINTER (action_id));
        } else {
                g_debug ("KioskShellService: Ungrab failed");
        }

        return ungrab_succeeded;
}

static gboolean
kiosk_shell_service_handle_grab_accelerator (KioskShellDBusService *object,
                                             GDBusMethodInvocation *invocation,
                                             const char            *accelerator,
                                             guint                  mode_flags,
                                             guint                  grab_flags)
{
        KioskShellService *self = KIOSK_SHELL_SERVICE (object);
        const char *client_unique_name;
        guint action_id;

        g_debug ("KioskShellService: Handling GrabAccelerator(%s, %x, %x) call",
                 accelerator, mode_flags, grab_flags);

        client_unique_name = g_dbus_method_invocation_get_sender (invocation);
        action_id = grab_accelerator_for_client (self, accelerator, mode_flags, grab_flags, client_unique_name);

        kiosk_shell_dbus_service_complete_grab_accelerator (KIOSK_SHELL_DBUS_SERVICE (self),
                                                            invocation,
                                                            action_id);

        return TRUE;
}

static gboolean
kiosk_shell_service_handle_grab_accelerators (KioskShellDBusService *object,
                                              GDBusMethodInvocation *invocation,
                                              GVariant              *accelerators)
{
        KioskShellService *self = KIOSK_SHELL_SERVICE (object);
        g_autoptr (GVariantIter) iter = NULL;
        GVariantBuilder builder;
        const char *client_unique_name;
        const char *accelerator;
        guint mode_flags;
        guint grab_flags;

        g_debug ("KioskShellService: Handling GrabAccelerators() call");

        client_unique_name = g_dbus_method_invocation_get_sender (invocation);

        g_variant_builder_init (&builder, G_VARIANT_TYPE ("au"));

        g_variant_get (accelerators, "a(suu)", &iter);
        while (g_variant_iter_loop (iter, "(suu)", &accelerator, &mode_flags, &grab_flags)) {
                guint action_id;

                action_id = grab_accelerator_for_client (self, accelerator, mode_flags, grab_flags, client_unique_name);
                g_variant_builder_add (&builder, "u", action_id);
        }

        kiosk_shell_dbus_service_complete_grab_accelerators (KIOSK_SHELL_DBUS_SERVICE (self),
                                                             invocation,
                                                             g_variant_builder_end (&builder));

        return TRUE;
}

static gboolean
kiosk_shell_service_handle_ungrab_accelerator (KioskShellDBusService *object,
                                               GDBusMethodInvocation *invocation,
                                               guint                  action_id)
{
        KioskShellService *self = KIOSK_SHELL_SERVICE (object);
        const char *client_unique_name;
        gboolean ungrab_succeeded;

        g_debug ("KioskShellService: Handling UngrabAccelerator(%d) call",
                 action_id);

        client_unique_name = g_dbus_method_invocation_get_sender (invocation);

        ungrab_succeeded = ungrab_accelerator_for_client (self, action_id, client_unique_name);

        kiosk_shell_dbus_service_complete_ungrab_accelerator (KIOSK_SHELL_DBUS_SERVICE (self),
                                                              invocation,
                                                              ungrab_succeeded);
        return TRUE;
}

static gboolean
kiosk_shell_service_handle_ungrab_accelerators (KioskShellDBusService *object,
                                                GDBusMethodInvocation *invocation,
                                                GVariant              *action_ids)
{
        KioskShellService *self = KIOSK_SHELL_SERVICE (object);
        const char *client_unique_name;
        guint action_id;

        g_autoptr (GVariantIter) iter = NULL;
        gboolean ungrab_succeeded = TRUE;

        g_debug ("KioskShellService: Handling UngrabAccelerators() call");

        client_unique_name = g_dbus_method_invocation_get_sender (invocation);

        g_variant_get (action_ids, "au", &iter);
        while (g_variant_iter_loop (iter, "u", &action_id)) {
                ungrab_succeeded &= ungrab_accelerator_for_client (self, action_id, client_unique_name);
        }

        kiosk_shell_dbus_service_complete_ungrab_accelerator (KIOSK_SHELL_DBUS_SERVICE (self),
                                                              invocation,
                                                              ungrab_succeeded);

        return TRUE;
}

static void
kiosk_shell_dbus_service_interface_init (KioskShellDBusServiceIface *interface)
{
        interface->handle_grab_accelerator = kiosk_shell_service_handle_grab_accelerator;
        interface->handle_grab_accelerators = kiosk_shell_service_handle_grab_accelerators;
        interface->handle_ungrab_accelerator = kiosk_shell_service_handle_ungrab_accelerator;
        interface->handle_ungrab_accelerators = kiosk_shell_service_handle_ungrab_accelerators;
}

static void
kiosk_shell_service_init (KioskShellService *self)
{
        g_debug ("KioskShellService: Initializing");
        self->grabbed_accelerators = g_hash_table_new_full (NULL, NULL, NULL, g_free);
        self->client_bus_watch_ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

KioskShellService *
kiosk_shell_service_new (KioskCompositor *compositor)
{
        GObject *object;

        object = g_object_new (KIOSK_TYPE_SHELL_SERVICE,
                               "compositor", compositor,
                               NULL);

        return KIOSK_SHELL_SERVICE (object);
}

static void
on_user_bus_acquired (GDBusConnection   *connection,
                      const char        *unique_name,
                      KioskShellService *self)
{
        g_autoptr (GError) error = NULL;

        g_debug ("KioskShellService: Connected to user bus");

        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
                                          connection,
                                          KIOSK_SHELL_SERVICE_OBJECT_PATH,
                                          &error);

        if (error != NULL) {
                g_debug ("KioskShellService: Could not export interface skeleton: %s",
                         error->message);
                g_clear_error (&error);
        }
}

static void
on_bus_name_acquired (GDBusConnection   *connection,
                      const char        *name,
                      KioskShellService *self)
{
        if (g_strcmp0 (name, KIOSK_SHELL_SERVICE_BUS_NAME) != 0) {
                return;
        }

        g_debug ("KioskShellService: Acquired name %s", name);
}

static void
on_bus_name_lost (GDBusConnection   *connection,
                  const char        *name,
                  KioskShellService *self)
{
        if (g_strcmp0 (name, KIOSK_SHELL_SERVICE_BUS_NAME) != 0) {
                return;
        }

        g_debug ("KioskShellService: Lost name %s", name);
}

static void
on_accelerator_activated (KioskShellService  *self,
                          guint               action_id,
                          ClutterInputDevice *device,
                          guint32             timestamp)
{
        GVariantBuilder builder;
        const char *grabbing_client;
        const char *device_node;

        g_debug ("KioskShellService: Accelerator with id '%d' activated",
                 action_id);

        grabbing_client = g_hash_table_lookup (self->grabbed_accelerators,
                                               GUINT_TO_POINTER (action_id));

        if (grabbing_client == NULL) {
                g_debug ("KioskShellService: No grabbing client, so ignoring");
                return;
        }

        g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
        g_variant_builder_add (&builder, "{sv}", "timestamp", g_variant_new_uint32 (timestamp));

        device_node = clutter_input_device_get_device_node (device);

        if (device_node != NULL) {
                g_variant_builder_add (&builder, "{sv}", "device-node", g_variant_new_string (device_node));
        }

        kiosk_shell_dbus_service_emit_accelerator_activated (KIOSK_SHELL_DBUS_SERVICE (self),
                                                             action_id,
                                                             g_variant_builder_end (&builder));
}

gboolean
kiosk_shell_service_start (KioskShellService *self,
                           GError           **error)
{
        g_return_val_if_fail (KIOSK_IS_SHELL_SERVICE (self), FALSE);

        g_debug ("KioskShellService: Starting");
        self->bus_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                       KIOSK_SHELL_SERVICE_BUS_NAME,
                                       G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                       (GBusAcquiredCallback) on_user_bus_acquired,
                                       (GBusNameAcquiredCallback) on_bus_name_acquired,
                                       (GBusNameVanishedCallback) on_bus_name_lost,
                                       self,
                                       NULL);

        g_signal_connect_swapped (self->display,
                                  "accelerator-activated",
                                  G_CALLBACK (on_accelerator_activated),
                                  self);
        return TRUE;
}

void
kiosk_shell_service_stop (KioskShellService *self)
{
        g_return_if_fail (KIOSK_IS_SHELL_SERVICE (self));

        g_debug ("KioskShellService: Stopping");

        g_signal_handlers_disconnect_by_func (self->display, on_accelerator_activated, self);

        stop_watching_clients (self);
        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self));
        g_clear_handle_id (&self->bus_id, g_bus_unown_name);
}
