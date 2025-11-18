#include "config.h"
#include "kiosk-screensaver-service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <meta/display.h>
#include <meta/util.h>

#include "kiosk-compositor.h"
#include "kiosk-screensaver.h"
#include "kiosk-session-presence.h"

#define KIOSK_SCREENSAVER_SERVICE_BUS_NAME "org.gnome.ScreenSaver"
#define KIOSK_SCREENSAVER_SERVICE_OBJECT_PATH "/org/gnome/ScreenSaver"

#define GNOME_DESKTOP_SCREENSAVER_SCHEMA "org.gnome.desktop.screensaver"
#define GNOME_DESKTOP_SCREENSAVER_LOCK_ENABLED "lock-enabled"
#define GNOME_DESKTOP_SCREENSAVER_LOCK_DELAY "lock-delay"

struct _KioskScreenSaverService
{
        KioskScreenSaverSkeleton parent;

        /* weak references */
        KioskCompositor         *compositor;
        MetaDisplay             *display;
        KioskSessionPresence    *session_presence;

        /* strong references */
        KioskScreensaver        *screensaver;
        GSettings               *screensaver_settings;

        /* handles */
        guint                    bus_id;
        guint                    lock_timeout_id;
};

enum
{
        PROP_COMPOSITOR = 1,
        NUMBER_OF_PROPERTIES
};
static GParamSpec *kiosk_screensaver_service_properties[NUMBER_OF_PROPERTIES] = { NULL, };

static void kiosk_screensaver_dbus_interface_init (KioskScreenSaverIface *interface);

G_DEFINE_FINAL_TYPE_WITH_CODE (KioskScreenSaverService,
                               kiosk_screensaver_service,
                               KIOSK_TYPE_SCREEN_SAVER_SKELETON,
                               G_IMPLEMENT_INTERFACE (KIOSK_TYPE_SCREEN_SAVER,
                                                      kiosk_screensaver_dbus_interface_init));

static void
kiosk_screensaver_service_set_property (GObject      *object,
                                        guint         property_id,
                                        const GValue *value,
                                        GParamSpec   *param_spec)
{
        KioskScreenSaverService *self = KIOSK_SCREENSAVER_SERVICE (object);

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
kiosk_screensaver_service_get_property (GObject    *object,
                                        guint       property_id,
                                        GValue     *value,
                                        GParamSpec *param_spec)
{
        KioskScreenSaverService *self = KIOSK_SCREENSAVER_SERVICE (object);

        switch (property_id) {
        case PROP_COMPOSITOR:
                g_value_set_object (value, self->compositor);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, param_spec);
                break;
        }
}

static void
cancel_lock_timeout (KioskScreenSaverService *self)
{
        g_clear_handle_id (&self->lock_timeout_id, g_source_remove);
}

static void
on_lock_timeout (gpointer  user_data)
{
        KioskScreenSaverService *self = user_data;

        g_debug ("KioskScreenSaverService: Lock timeout, locking");

        self->lock_timeout_id = 0;

        if (kiosk_screensaver_get_active (self->screensaver) &&
            !kiosk_screensaver_get_locked (self->screensaver)) {
                kiosk_screensaver_lock (self->screensaver);
        }
}

static void
start_lock_timeout (KioskScreenSaverService *self)
{
        gboolean lock_enabled;
        guint lock_delay;

        cancel_lock_timeout (self);

        lock_enabled = g_settings_get_boolean (self->screensaver_settings,
                                               GNOME_DESKTOP_SCREENSAVER_LOCK_ENABLED);
        if (!lock_enabled) {
                g_debug ("KioskScreenSaverService: Lock not enabled, skipping lock timeout");
                return;
        }

        lock_delay = g_settings_get_uint (self->screensaver_settings,
                                          GNOME_DESKTOP_SCREENSAVER_LOCK_DELAY);

        g_debug ("KioskScreenSaverService: Starting lock timeout after %u seconds", lock_delay);

        self->lock_timeout_id =
                g_timeout_add_seconds_once (lock_delay,
                                            on_lock_timeout,
                                            self);
}

static void
on_screensaver_status_changed (KioskScreensaver        *screensaver,
                               gboolean                 active,
                               KioskScreenSaverService *self)
{
        g_debug ("KioskScreenSaverService: Screensaver status changed to %s", active ? "active" : "inactive");

        kiosk_screen_saver_emit_active_changed (KIOSK_SCREEN_SAVER (self), active);

        if (active)
                start_lock_timeout (self);
        else
                cancel_lock_timeout (self);
}

static void
on_session_presence_status_changed (GObject                 *object,
                                    GParamSpec              *pspec,
                                    KioskScreenSaverService *self)
{
        KioskSessionPresence *session_presence = KIOSK_SESSION_PRESENCE (object);
        GsmPresenceStatus status;
        gboolean is_active;
        gboolean is_locked;

        status = kiosk_session_presence_get_status (session_presence);

        g_debug ("KioskScreenSaverService: Session presence status changed to %d", status);

        is_active = kiosk_screensaver_get_active (self->screensaver);
        is_locked = kiosk_screensaver_get_locked (self->screensaver);

        if (status == GSM_PRESENCE_STATUS_IDLE) {
                if (!is_active) {
                        g_debug ("KioskScreenSaverService: Activating screensaver: idle");
                        kiosk_screensaver_activate (self->screensaver);
                }
        } else {
                if (is_active && !is_locked) {
                        g_debug ("KioskScreenSaverService: Deactivating screensaver: not idle");
                        kiosk_screensaver_deactivate (self->screensaver);
                }
        }
}

static void
kiosk_screensaver_service_constructed (GObject *object)
{
        KioskScreenSaverService *self = KIOSK_SCREENSAVER_SERVICE (object);

        G_OBJECT_CLASS (kiosk_screensaver_service_parent_class)->constructed (object);

        g_set_weak_pointer (&self->display, meta_plugin_get_display (META_PLUGIN (self->compositor)));
        g_set_weak_pointer (&self->session_presence,
                            kiosk_compositor_get_session_presence (self->compositor));

        self->screensaver = kiosk_screensaver_new (self->compositor);
        self->screensaver_settings = g_settings_new (GNOME_DESKTOP_SCREENSAVER_SCHEMA);

        g_signal_connect (self->screensaver,
                          "status-changed",
                          G_CALLBACK (on_screensaver_status_changed),
                          self);

        g_signal_connect (self->session_presence,
                          "notify::status",
                          G_CALLBACK (on_session_presence_status_changed),
                          self);
}

static void
kiosk_screensaver_service_dispose (GObject *object)
{
        KioskScreenSaverService *self = KIOSK_SCREENSAVER_SERVICE (object);

        kiosk_screensaver_service_stop (self);

        cancel_lock_timeout (self);

        g_signal_handlers_disconnect_by_func (self->session_presence,
                                              on_session_presence_status_changed,
                                              self);

        g_signal_handlers_disconnect_by_func (self->screensaver,
                                              on_screensaver_status_changed,
                                              self);

        g_clear_object (&self->screensaver_settings);
        g_clear_object (&self->screensaver);

        g_clear_weak_pointer (&self->session_presence);
        g_clear_weak_pointer (&self->compositor);
        g_clear_weak_pointer (&self->display);

        G_OBJECT_CLASS (kiosk_screensaver_service_parent_class)->dispose (object);
}

static gboolean
kiosk_screensaver_service_handle_lock (KioskScreenSaver      *interface,
                                       GDBusMethodInvocation *invocation)
{
        KioskScreenSaverService *self = KIOSK_SCREENSAVER_SERVICE (interface);

        g_debug ("KioskScreenSaverService: Lock requested");

        kiosk_screensaver_lock (self->screensaver);

        kiosk_screen_saver_complete_lock (interface, invocation);

        return TRUE;
}

static gboolean
kiosk_screensaver_service_handle_get_active (KioskScreenSaver      *interface,
                                             GDBusMethodInvocation *invocation)
{
        KioskScreenSaverService *self = KIOSK_SCREENSAVER_SERVICE (interface);
        gboolean active;

        g_debug ("KioskScreenSaverService: GetActive requested");

        active = kiosk_screensaver_get_active (self->screensaver);

        kiosk_screen_saver_complete_get_active (interface, invocation, active);

        return TRUE;
}

static gboolean
kiosk_screensaver_service_handle_set_active (KioskScreenSaver      *interface,
                                             GDBusMethodInvocation *invocation,
                                             gboolean               active)
{
        KioskScreenSaverService *self = KIOSK_SCREENSAVER_SERVICE (interface);

        g_debug ("KioskScreenSaverService: SetActive: %s", active ? "TRUE" : "FALSE");

        if (active)
                kiosk_screensaver_activate (self->screensaver);
        else
                kiosk_screensaver_deactivate (self->screensaver);

        kiosk_screen_saver_complete_set_active (interface, invocation);

        return TRUE;
}

static gboolean
kiosk_screensaver_service_handle_get_active_time (KioskScreenSaver      *interface,
                                                  GDBusMethodInvocation *invocation)
{
        KioskScreenSaverService *self = KIOSK_SCREENSAVER_SERVICE (interface);
        guint32 active_time;

        g_debug ("KioskScreenSaverService: GetActiveTime");

        active_time = kiosk_screensaver_get_active_time (self->screensaver);

        kiosk_screen_saver_complete_get_active_time (interface, invocation, active_time);

        return TRUE;
}

static void
kiosk_screensaver_dbus_interface_init (KioskScreenSaverIface *interface)
{
        interface->handle_lock = kiosk_screensaver_service_handle_lock;
        interface->handle_get_active = kiosk_screensaver_service_handle_get_active;
        interface->handle_set_active = kiosk_screensaver_service_handle_set_active;
        interface->handle_get_active_time = kiosk_screensaver_service_handle_get_active_time;
}

static void
kiosk_screensaver_service_class_init (KioskScreenSaverServiceClass *screensaver_service_class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (screensaver_service_class);

        object_class->constructed = kiosk_screensaver_service_constructed;
        object_class->set_property = kiosk_screensaver_service_set_property;
        object_class->get_property = kiosk_screensaver_service_get_property;
        object_class->dispose = kiosk_screensaver_service_dispose;

        kiosk_screensaver_service_properties[PROP_COMPOSITOR] = g_param_spec_object ("compositor",
                                                                                     NULL, NULL,
                                                                                     KIOSK_TYPE_COMPOSITOR,
                                                                                     G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_NAME);
        g_object_class_install_properties (object_class, NUMBER_OF_PROPERTIES, kiosk_screensaver_service_properties);
}

static void
kiosk_screensaver_service_init (KioskScreenSaverService *screensaver_service)
{
        g_debug ("KioskScreenSaverService: Initializing");
}

KioskScreenSaverService *
kiosk_screensaver_service_new (KioskCompositor *compositor)
{
        GObject *object;

        object = g_object_new (KIOSK_TYPE_SCREENSAVER_SERVICE,
                               "compositor", compositor,
                               NULL);

        return KIOSK_SCREENSAVER_SERVICE (object);
}

static void
on_user_bus_acquired (GDBusConnection         *connection,
                      const char              *unique_name,
                      KioskScreenSaverService *self)
{
        g_autoptr (GError) error = NULL;

        g_debug ("KioskScreenSaverService: Connected to user bus");

        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
                                          connection,
                                          KIOSK_SCREENSAVER_SERVICE_OBJECT_PATH,
                                          &error);

        if (error != NULL) {
                g_debug ("KioskScreenSaverService: Could not export interface skeleton: %s",
                         error->message);
        }
}

static void
on_bus_name_acquired (GDBusConnection         *connection,
                      const char              *name,
                      KioskScreenSaverService *self)
{
        g_debug ("KioskScreenSaverService: Acquired name %s", name);
}

static void
on_bus_name_lost (GDBusConnection         *connection,
                  const char              *name,
                  KioskScreenSaverService *self)
{
        g_debug ("KioskScreenSaverService: Lost name %s", name);
}

gboolean
kiosk_screensaver_service_start (KioskScreenSaverService *self,
                                 GError                 **error)
{
        g_return_val_if_fail (KIOSK_IS_SCREENSAVER_SERVICE (self), FALSE);

        g_debug ("KioskScreenSaverService: Starting");

        self->bus_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                       KIOSK_SCREENSAVER_SERVICE_BUS_NAME,
                                       G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                       (GBusAcquiredCallback) on_user_bus_acquired,
                                       (GBusNameAcquiredCallback) on_bus_name_acquired,
                                       (GBusNameVanishedCallback) on_bus_name_lost,
                                       self,
                                       NULL);

        return TRUE;
}

void
kiosk_screensaver_service_stop (KioskScreenSaverService *self)
{
        g_return_if_fail (KIOSK_IS_SCREENSAVER_SERVICE (self));

        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self));
        g_clear_handle_id (&self->bus_id, g_bus_unown_name);
}
