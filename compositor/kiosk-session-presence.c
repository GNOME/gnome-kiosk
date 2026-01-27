#include "config.h"
#include "kiosk-session-presence.h"

#include <meta/display.h>
#include <meta/util.h>

#include "kiosk-compositor.h"

#define GSM_PRESENCE_BUS_NAME "org.gnome.SessionManager"
#define GSM_PRESENCE_OBJECT_PATH "/org/gnome/SessionManager/Presence"

struct _KioskSessionPresence
{
        GObject           parent;

        /* weak references */
        KioskCompositor  *compositor;
        MetaDisplay      *display;

        /* strong references */
        GsmPresence      *proxy;
        GCancellable     *cancellable;

        /* state */
        GsmPresenceStatus status;
};

enum
{
        PROP_COMPOSITOR = 1,
        PROP_STATUS,
        NUMBER_OF_PROPERTIES
};

static GParamSpec *kiosk_session_presence_properties[NUMBER_OF_PROPERTIES] = { NULL, };

G_DEFINE_ENUM_TYPE (GsmPresenceStatus, gsm_presence_status,
                    G_DEFINE_ENUM_VALUE (GSM_PRESENCE_STATUS_AVAILABLE, "available"),
                    G_DEFINE_ENUM_VALUE (GSM_PRESENCE_STATUS_INVISIBLE, "invisible"),
                    G_DEFINE_ENUM_VALUE (GSM_PRESENCE_STATUS_BUSY, "busy"),
                    G_DEFINE_ENUM_VALUE (GSM_PRESENCE_STATUS_IDLE, "idle"))

G_DEFINE_FINAL_TYPE (KioskSessionPresence, kiosk_session_presence, G_TYPE_OBJECT);

static const char *
gsm_presence_status_to_string (GsmPresenceStatus  status)
{
        g_autoptr (GEnumClass) enum_class = NULL;
        GEnumValue *enum_value;

        enum_class = g_type_class_ref (GSM_TYPE_PRESENCE_STATUS);
        enum_value = g_enum_get_value (enum_class, status);
        if (enum_value == NULL)
                return "unknown";

        return enum_value->value_nick;
}

static void
kiosk_session_presence_set_property (GObject      *object,
                                     guint         property_id,
                                     const GValue *value,
                                     GParamSpec   *param_spec)
{
        KioskSessionPresence *self = KIOSK_SESSION_PRESENCE (object);

        switch (property_id) {
        case PROP_COMPOSITOR:
                g_set_weak_pointer (&self->compositor, g_value_get_object (value));
                break;

        case PROP_STATUS: {
                GsmPresenceStatus status = g_value_get_enum (value);

                if (self->status == status)
                        break;

                self->status = status;
                g_object_notify_by_pspec (object, param_spec);
                break;
        }

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, param_spec);
                break;
        }
}

static void
kiosk_session_presence_get_property (GObject    *object,
                                     guint       property_id,
                                     GValue     *value,
                                     GParamSpec *param_spec)
{
        KioskSessionPresence *self = KIOSK_SESSION_PRESENCE (object);

        switch (property_id) {
        case PROP_STATUS:
                g_value_set_enum (value, self->status);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, param_spec);
                break;
        }
}

static void
kiosk_session_presence_constructed (GObject *object)
{
        KioskSessionPresence *self = KIOSK_SESSION_PRESENCE (object);

        G_OBJECT_CLASS (kiosk_session_presence_parent_class)->constructed (object);

        g_set_weak_pointer (&self->display,
                            meta_plugin_get_display (META_PLUGIN (self->compositor)));
}

static void
kiosk_session_presence_dispose (GObject *object)
{
        KioskSessionPresence *self = KIOSK_SESSION_PRESENCE (object);

        kiosk_session_presence_stop (self);

        g_clear_weak_pointer (&self->display);
        g_clear_weak_pointer (&self->compositor);

        G_OBJECT_CLASS (kiosk_session_presence_parent_class)->dispose (object);
}

static void
on_notify_status (GObject    *object,
                  GParamSpec *pspec,
                  gpointer    user_data)
{
        KioskSessionPresence *self = KIOSK_SESSION_PRESENCE (object);

        g_debug ("KioskSessionPresence: status=%s",
                 gsm_presence_status_to_string (self->status));
}

static void
on_proxy_ready (GObject              *source_object,
                GAsyncResult         *result,
                KioskSessionPresence *self)
{
        g_autoptr (GError) error = NULL;

        self->proxy = gsm_presence_proxy_new_for_bus_finish (result, &error);

        if (error != NULL) {
                g_debug ("KioskSessionPresence: Failed to create proxy: %s",
                         error->message);
                return;
        }

        g_debug ("KioskSessionPresence: Proxy created successfully");

        g_object_bind_property (self->proxy, "status",
                                self, "status",
                                G_BINDING_SYNC_CREATE);
}

static void
kiosk_session_presence_class_init (KioskSessionPresenceClass *presence_class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (presence_class);

        object_class->constructed = kiosk_session_presence_constructed;
        object_class->set_property = kiosk_session_presence_set_property;
        object_class->get_property = kiosk_session_presence_get_property;
        object_class->dispose = kiosk_session_presence_dispose;

        kiosk_session_presence_properties[PROP_COMPOSITOR] =
                g_param_spec_object ("compositor",
                                     NULL,
                                     NULL,
                                     KIOSK_TYPE_COMPOSITOR,
                                     G_PARAM_CONSTRUCT_ONLY
                                     | G_PARAM_WRITABLE
                                     | G_PARAM_STATIC_NAME);
        kiosk_session_presence_properties[PROP_STATUS] =
                g_param_spec_enum ("status",
                                   NULL,
                                   NULL,
                                   GSM_TYPE_PRESENCE_STATUS,
                                   GSM_PRESENCE_STATUS_AVAILABLE,
                                   G_PARAM_READWRITE
                                   | G_PARAM_EXPLICIT_NOTIFY
                                   | G_PARAM_STATIC_NAME);
        g_object_class_install_properties (object_class,
                                           NUMBER_OF_PROPERTIES,
                                           kiosk_session_presence_properties);
}

static void
kiosk_session_presence_init (KioskSessionPresence *self)
{
        g_debug ("KioskSessionPresence: Initializing");

        g_signal_connect (self, "notify::status",
                          G_CALLBACK (on_notify_status), NULL);
}

KioskSessionPresence *
kiosk_session_presence_new (KioskCompositor *compositor)
{
        GObject *object;

        object = g_object_new (KIOSK_TYPE_SESSION_PRESENCE,
                               "compositor", compositor,
                               NULL);

        return KIOSK_SESSION_PRESENCE (object);
}

gboolean
kiosk_session_presence_start (KioskSessionPresence *self,
                              GError              **error)
{
        g_return_val_if_fail (KIOSK_IS_SESSION_PRESENCE (self), FALSE);

        g_debug ("KioskSessionPresence: Starting");

        self->cancellable = g_cancellable_new ();

        gsm_presence_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                                        G_DBUS_PROXY_FLAGS_NONE,
                                        GSM_PRESENCE_BUS_NAME,
                                        GSM_PRESENCE_OBJECT_PATH,
                                        self->cancellable,
                                        (GAsyncReadyCallback) on_proxy_ready,
                                        self);

        return TRUE;
}

void
kiosk_session_presence_stop (KioskSessionPresence *self)
{
        g_return_if_fail (KIOSK_IS_SESSION_PRESENCE (self));

        g_debug ("KioskSessionPresence: Stopping");

        if (self->cancellable != NULL) {
                g_cancellable_cancel (self->cancellable);
                g_clear_object (&self->cancellable);
        }

        g_clear_object (&self->proxy);
}

GsmPresenceStatus
kiosk_session_presence_get_status (KioskSessionPresence *self)
{
        g_return_val_if_fail (KIOSK_IS_SESSION_PRESENCE (self), GSM_PRESENCE_STATUS_AVAILABLE);

        return self->status;
}
