#include "config.h"
#include "kiosk-service.h"

#include <stdlib.h>
#include <string.h>
#include <meta/display.h>
#include <meta/util.h>

#include "kiosk-compositor.h"

#define KIOSK_SERVICE_BUS_NAME "org.gnome.Kiosk"
#define KIOSK_SERVICE_OBJECT_PATH "/org/gnome/Kiosk"

#define KIOSK_SERVICE_INPUT_SOURCES_OBJECTS_PATH_PREFIX KIOSK_SERVICE_OBJECT_PATH "/InputSources"
#define KIOSK_SERVICE_INPUT_SOURCES_MANAGER_OBJECT_PATH KIOSK_SERVICE_INPUT_SOURCES_OBJECTS_PATH_PREFIX "/Manager"

struct _KioskService
{
        GObject                               parent;

        /* weak references */
        KioskCompositor                      *compositor;

        /* strong references */
        KioskDBusServiceSkeleton             *service_skeleton;

        KioskDBusInputSourcesManagerSkeleton *input_sources_manager_skeleton;
        GDBusObjectManagerServer             *input_sources_object_manager;

        /* handles */
        guint                                 bus_id;
};

enum
{
        PROP_COMPOSITOR = 1,
        NUMBER_OF_PROPERTIES
};
static GParamSpec *kiosk_service_properties[NUMBER_OF_PROPERTIES] = { NULL, };

G_DEFINE_TYPE (KioskService, kiosk_service, G_TYPE_OBJECT);

static void kiosk_service_set_property (GObject      *object,
                                        guint         property_id,
                                        const GValue *value,
                                        GParamSpec   *param_spec);
static void kiosk_service_get_property (GObject    *object,
                                        guint       property_id,
                                        GValue     *value,
                                        GParamSpec *param_spec);

static void kiosk_service_constructed (GObject *object);
static void kiosk_service_dispose (GObject *object);

KioskService *
kiosk_service_new (KioskCompositor *compositor)
{
        GObject *object;

        object = g_object_new (KIOSK_TYPE_SERVICE,
                               "compositor", compositor,
                               NULL);

        return KIOSK_SERVICE (object);
}

static void
kiosk_service_class_init (KioskServiceClass *service_class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (service_class);

        object_class->constructed = kiosk_service_constructed;
        object_class->set_property = kiosk_service_set_property;
        object_class->get_property = kiosk_service_get_property;
        object_class->dispose = kiosk_service_dispose;

        kiosk_service_properties[PROP_COMPOSITOR] = g_param_spec_object ("compositor",
                                                                         "compositor",
                                                                         "compositor",
                                                                         KIOSK_TYPE_COMPOSITOR,
                                                                         G_PARAM_CONSTRUCT_ONLY
                                                                         | G_PARAM_WRITABLE
                                                                         | G_PARAM_STATIC_NAME
                                                                         | G_PARAM_STATIC_NICK
                                                                         | G_PARAM_STATIC_BLURB);
        g_object_class_install_properties (object_class, NUMBER_OF_PROPERTIES, kiosk_service_properties);
}

static void
kiosk_service_set_property (GObject      *object,
                            guint         property_id,
                            const GValue *value,
                            GParamSpec   *param_spec)
{
        KioskService *self = KIOSK_SERVICE (object);

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
kiosk_service_get_property (GObject    *object,
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
kiosk_service_constructed (GObject *object)
{
        G_OBJECT_CLASS (kiosk_service_parent_class)->constructed (object);
}

static void
kiosk_service_init (KioskService *self)
{
        g_debug ("KioskService: Initializing");
        self->service_skeleton = KIOSK_DBUS_SERVICE_SKELETON (kiosk_dbus_service_skeleton_new ());

        self->input_sources_manager_skeleton = KIOSK_DBUS_INPUT_SOURCES_MANAGER_SKELETON (kiosk_dbus_input_sources_manager_skeleton_new ());
        self->input_sources_object_manager = g_dbus_object_manager_server_new (KIOSK_SERVICE_INPUT_SOURCES_OBJECTS_PATH_PREFIX);
}

static void
export_service (KioskService    *self,
                GDBusConnection *connection)
{
        g_autoptr (GError) error = NULL;

        g_debug ("KioskService: Exporting service over user bus");

        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self->service_skeleton),
                                          connection, KIOSK_SERVICE_OBJECT_PATH, &error);

        if (error != NULL) {
                g_debug ("KioskService: Could not export service over user bus: %s", error->message);
                g_clear_error (&error);
        }
}

static void
export_input_sources_manager (KioskService    *self,
                              GDBusConnection *connection)
{
        g_autoptr (GDBusObjectSkeleton) object = NULL;

        g_debug ("KioskService: Exporting input sources manager over bus");

        object = g_dbus_object_skeleton_new (KIOSK_SERVICE_INPUT_SOURCES_MANAGER_OBJECT_PATH);
        g_dbus_object_skeleton_add_interface (object, G_DBUS_INTERFACE_SKELETON (self->input_sources_manager_skeleton));
        g_dbus_object_manager_server_export (G_DBUS_OBJECT_MANAGER_SERVER (self->input_sources_object_manager), G_DBUS_OBJECT_SKELETON (object));

        g_dbus_object_manager_server_set_connection (G_DBUS_OBJECT_MANAGER_SERVER (self->input_sources_object_manager), connection);
}

static void
on_user_bus_acquired (GDBusConnection *connection,
                      const char      *unique_name,
                      KioskService    *self)
{
        g_debug ("KioskService: Connected to user bus");

        export_service (self, connection);
        export_input_sources_manager (self, connection);
}

static void
on_bus_name_acquired (GDBusConnection *connection,
                      const char      *name,
                      KioskService    *self)
{
        if (g_strcmp0 (name, KIOSK_SERVICE_BUS_NAME) != 0) {
                return;
        }

        g_debug ("KioskService: Acquired name %s", name);
}

static void
on_bus_name_lost (GDBusConnection *connection,
                  const char      *name,
                  KioskService    *self)
{
        if (g_strcmp0 (name, KIOSK_SERVICE_BUS_NAME) != 0) {
                return;
        }

        g_debug ("KioskService: Lost name %s", name);

        /* If the name got stolen from us, assume we're getting
         * replaced and terminate immediately.
         *
         * If we've just lost our name as part of getting stopped, that's non-fatal.
         */
        if (self->bus_id != 0) {
                g_debug ("KioskService: Terminating");
                raise (SIGTERM);
        }
}

gboolean
kiosk_service_start (KioskService *self,
                     GError      **error)
{
        g_return_val_if_fail (KIOSK_IS_SERVICE (self), FALSE);

        g_debug ("KioskService: Starting");

        self->bus_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                       KIOSK_SERVICE_BUS_NAME,
                                       G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                                       G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                       (GBusAcquiredCallback) on_user_bus_acquired,
                                       (GBusNameAcquiredCallback) on_bus_name_acquired,
                                       (GBusNameVanishedCallback) on_bus_name_lost,
                                       self,
                                       NULL);

        return TRUE;
}

void
kiosk_service_stop (KioskService *self)
{
        g_return_if_fail (KIOSK_IS_SERVICE (self));

        g_debug ("KioskService: Stopping");

        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self->service_skeleton));
        g_dbus_object_manager_server_unexport (G_DBUS_OBJECT_MANAGER_SERVER (self->input_sources_manager_skeleton),
                                               KIOSK_SERVICE_INPUT_SOURCES_MANAGER_OBJECT_PATH);

        g_clear_handle_id (&self->bus_id, g_bus_unown_name);
}

KioskDBusInputSourcesManagerSkeleton *
kiosk_service_get_input_sources_manager_skeleton (KioskService *self)
{
        return self->input_sources_manager_skeleton;
}

GDBusObjectManagerServer *
kiosk_service_get_input_sources_object_manager (KioskService *self)
{
        return self->input_sources_object_manager;
}

static void
kiosk_service_dispose (GObject *object)
{
        KioskService *self = KIOSK_SERVICE (object);

        g_debug ("KioskService: Disposing");

        kiosk_service_stop (self);

        g_clear_object (&self->input_sources_manager_skeleton);
        g_clear_object (&self->input_sources_object_manager);
        g_clear_weak_pointer (&self->compositor);

        G_OBJECT_CLASS (kiosk_service_parent_class)->dispose (object);
}
