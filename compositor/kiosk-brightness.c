#include "config.h"
#include "kiosk-brightness.h"

#include <float.h>
#include <math.h>

#include <meta/display.h>
#include <meta/meta-backend.h>
#include <meta/meta-context.h>
#include <meta/meta-monitor-manager.h>
#include <meta/meta-backlight.h>
#include <meta/meta-monitor.h>
#include <meta/util.h>

#include "kiosk-compositor.h"

#define KIOSK_BRIGHTNESS_BUS_NAME "org.gnome.Shell.Brightness"
#define KIOSK_BRIGHTNESS_OBJECT_PATH "/org/gnome/Shell/Brightness"

/* Dimming reduces brightness to this fraction of normal */
#define DIMMING_BRIGHTNESS_FRACTION 0.3

struct _KioskBrightness
{
        KioskShellBrightnessDBusServiceSkeleton parent;

        /* weak references */
        KioskCompositor                        *compositor;
        MetaDisplay                            *display;
        MetaContext                            *context;
        MetaBackend                            *backend;
        MetaMonitorManager                     *monitor_manager;

        /* handles */
        guint                                   bus_id;

        /* state */
        gboolean                                dimming_enabled;
        double                                  auto_brightness_target;
        gboolean                                has_backlight;
};

enum
{
        PROP_COMPOSITOR = 1,
        NUMBER_OF_PROPERTIES
};

static GParamSpec *kiosk_brightness_properties[NUMBER_OF_PROPERTIES] = { NULL, };

static void kiosk_brightness_dbus_interface_init (KioskShellBrightnessDBusServiceIface *interface);

G_DEFINE_FINAL_TYPE_WITH_CODE (KioskBrightness,
                               kiosk_brightness,
                               KIOSK_TYPE_SHELL_BRIGHTNESS_DBUS_SERVICE_SKELETON,
                               G_IMPLEMENT_INTERFACE (KIOSK_TYPE_SHELL_BRIGHTNESS_DBUS_SERVICE,
                                                      kiosk_brightness_dbus_interface_init));

static void
kiosk_brightness_set_property (GObject      *object,
                               guint         property_id,
                               const GValue *value,
                               GParamSpec   *param_spec)
{
        KioskBrightness *self = KIOSK_BRIGHTNESS (object);

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
kiosk_brightness_get_property (GObject    *object,
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
kiosk_brightness_update_has_brightness_control (KioskBrightness *self)
{
        GList *monitors;
        GList *l;

        self->has_backlight = FALSE;

        if (self->monitor_manager == NULL)
                goto out;

        monitors = meta_monitor_manager_get_monitors (self->monitor_manager);

        for (l = monitors; l != NULL; l = l->next) {
                MetaMonitor *monitor = META_MONITOR (l->data);

                if (!meta_monitor_is_active (monitor))
                        continue;

                if (meta_monitor_get_backlight (monitor) != NULL) {
                        self->has_backlight = TRUE;
                        break;
                }
        }

out:
        g_debug ("KioskBrightness: HasBrightnessControl = %s",
                 self->has_backlight ? "TRUE" : "FALSE");

        kiosk_shell_brightness_dbus_service_set_has_brightness_control (
                KIOSK_SHELL_BRIGHTNESS_DBUS_SERVICE (self), self->has_backlight);
}

static void
kiosk_brightness_apply_brightness (KioskBrightness *self)
{
        GList *monitors;
        GList *l;
        double effective_target;

        if (!self->has_backlight)
                return;

        if (self->monitor_manager == NULL)
                return;

        /* Calculate effective brightness based on auto_brightness_target and dimming */
        effective_target = self->auto_brightness_target;

        if (self->dimming_enabled)
                effective_target *= DIMMING_BRIGHTNESS_FRACTION;

        monitors = meta_monitor_manager_get_monitors (self->monitor_manager);

        for (l = monitors; l != NULL; l = l->next) {
                MetaMonitor *monitor = META_MONITOR (l->data);
                MetaBacklight *backlight;
                int brightness_min, brightness_max;
                int target_brightness;

                if (!meta_monitor_is_active (monitor))
                        continue;

                backlight = meta_monitor_get_backlight (monitor);
                if (backlight == NULL)
                        continue;

                meta_backlight_get_brightness_info (backlight,
                                                    &brightness_min,
                                                    &brightness_max);

                /* Map [0, 1] to [brightness_min, brightness_max] */
                target_brightness = (int) (brightness_min +
                                           effective_target * (brightness_max - brightness_min));

                /* Clamp to valid range */
                target_brightness = CLAMP (target_brightness, brightness_min, brightness_max);

                g_debug ("KioskBrightness: Setting brightness to %d on %s (range: %d-%d, target: %f, dimming: %s)",
                         target_brightness,
                         meta_monitor_get_connector (monitor),
                         brightness_min, brightness_max,
                         effective_target, self->dimming_enabled ? "enabled" : "disabled");

                meta_backlight_set_brightness (backlight, target_brightness);
        }
}

static void
on_monitors_changed (MetaMonitorManager *monitor_manager,
                     KioskBrightness    *self)
{
        g_debug ("KioskBrightness: Monitors changed, updating brightness control status");
        kiosk_brightness_update_has_brightness_control (self);
}

static void
kiosk_brightness_constructed (GObject *object)
{
        KioskBrightness *self = KIOSK_BRIGHTNESS (object);

        G_OBJECT_CLASS (kiosk_brightness_parent_class)->constructed (object);

        g_set_weak_pointer (&self->display,
                            meta_plugin_get_display (META_PLUGIN (self->compositor)));
        g_set_weak_pointer (&self->context,
                            meta_display_get_context (self->display));
        g_set_weak_pointer (&self->backend,
                            meta_context_get_backend (self->context));
        g_set_weak_pointer (&self->monitor_manager,
                            meta_backend_get_monitor_manager (self->backend));

        /* Listen for monitor changes */
        g_signal_connect (self->monitor_manager,
                          "monitors-changed",
                          G_CALLBACK (on_monitors_changed),
                          self);

        /* Initialize HasBrightnessControl property */
        kiosk_brightness_update_has_brightness_control (self);
}

static void
kiosk_brightness_dispose (GObject *object)
{
        KioskBrightness *self = KIOSK_BRIGHTNESS (object);

        kiosk_brightness_stop (self);

        g_signal_handlers_disconnect_by_func (self->monitor_manager,
                                              on_monitors_changed,
                                              self);

        g_clear_weak_pointer (&self->monitor_manager);
        g_clear_weak_pointer (&self->backend);
        g_clear_weak_pointer (&self->context);
        g_clear_weak_pointer (&self->display);
        g_clear_weak_pointer (&self->compositor);

        G_OBJECT_CLASS (kiosk_brightness_parent_class)->dispose (object);
}

static gboolean
kiosk_brightness_handle_set_dimming (KioskShellBrightnessDBusService *object,
                                     GDBusMethodInvocation           *invocation,
                                     gboolean                         enable)
{
        KioskBrightness *self = KIOSK_BRIGHTNESS (object);

        g_debug ("KioskBrightness: SetDimming(%s) requested",
                 enable ? "TRUE" : "FALSE");

        if (self->dimming_enabled != enable) {
                self->dimming_enabled = enable;
                kiosk_brightness_apply_brightness (self);
        }

        kiosk_shell_brightness_dbus_service_complete_set_dimming (object, invocation);

        return TRUE;
}

static gboolean
kiosk_brightness_handle_set_auto_brightness_target (KioskShellBrightnessDBusService *object,
                                                    GDBusMethodInvocation           *invocation,
                                                    gdouble                          target)
{
        KioskBrightness *self = KIOSK_BRIGHTNESS (object);

        g_debug ("KioskBrightness: SetAutoBrightnessTarget(%f) requested", target);

        if (isnan (target) || isinf (target) || target < 0.0 || target > 1.0) {
                g_dbus_method_invocation_return_error (invocation,
                                                       G_DBUS_ERROR,
                                                       G_DBUS_ERROR_INVALID_ARGS,
                                                       "Brightness target must be a finite value between 0.0 and 1.0");
                return TRUE;
        }

        if (!G_APPROX_VALUE (self->auto_brightness_target, target, FLT_EPSILON)) {
                self->auto_brightness_target = target;
                kiosk_brightness_apply_brightness (self);
        }

        kiosk_shell_brightness_dbus_service_complete_set_auto_brightness_target (object, invocation);

        return TRUE;
}

static void
kiosk_brightness_dbus_interface_init (KioskShellBrightnessDBusServiceIface *interface)
{
        interface->handle_set_dimming = kiosk_brightness_handle_set_dimming;
        interface->handle_set_auto_brightness_target = kiosk_brightness_handle_set_auto_brightness_target;
}

static void
kiosk_brightness_class_init (KioskBrightnessClass *brightness_class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (brightness_class);

        object_class->constructed = kiosk_brightness_constructed;
        object_class->set_property = kiosk_brightness_set_property;
        object_class->get_property = kiosk_brightness_get_property;
        object_class->dispose = kiosk_brightness_dispose;

        kiosk_brightness_properties[PROP_COMPOSITOR] =
                g_param_spec_object ("compositor",
                                     NULL,
                                     NULL,
                                     KIOSK_TYPE_COMPOSITOR,
                                     G_PARAM_CONSTRUCT_ONLY
                                     | G_PARAM_WRITABLE
                                     | G_PARAM_STATIC_NAME);
        g_object_class_install_properties (object_class,
                                           NUMBER_OF_PROPERTIES,
                                           kiosk_brightness_properties);
}

static void
kiosk_brightness_init (KioskBrightness *self)
{
        g_debug ("KioskBrightness: Initializing");

        self->auto_brightness_target = 1.0;
}

KioskBrightness *
kiosk_brightness_new (KioskCompositor *compositor)
{
        GObject *object;

        object = g_object_new (KIOSK_TYPE_BRIGHTNESS,
                               "compositor", compositor,
                               NULL);

        return KIOSK_BRIGHTNESS (object);
}

static void
on_user_bus_acquired (GDBusConnection *connection,
                      const char      *unique_name,
                      KioskBrightness *self)
{
        g_autoptr (GError) error = NULL;

        g_debug ("KioskBrightness: Connected to user bus");

        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
                                          connection,
                                          KIOSK_BRIGHTNESS_OBJECT_PATH,
                                          &error);

        if (error != NULL) {
                g_debug ("KioskBrightness: Could not export interface skeleton: %s",
                         error->message);
        }
}

static void
on_bus_name_acquired (GDBusConnection *connection,
                      const char      *name,
                      KioskBrightness *self)
{
        g_debug ("KioskBrightness: Acquired name %s", name);
}

static void
on_bus_name_lost (GDBusConnection *connection,
                  const char      *name,
                  KioskBrightness *self)
{
        g_debug ("KioskBrightness: Lost name %s", name);
}

gboolean
kiosk_brightness_start (KioskBrightness *self,
                        GError         **error)
{
        g_return_val_if_fail (KIOSK_IS_BRIGHTNESS (self), FALSE);

        g_debug ("KioskBrightness: Starting");
        self->bus_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                       KIOSK_BRIGHTNESS_BUS_NAME,
                                       G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                       (GBusAcquiredCallback) on_user_bus_acquired,
                                       (GBusNameAcquiredCallback) on_bus_name_acquired,
                                       (GBusNameVanishedCallback) on_bus_name_lost,
                                       self,
                                       NULL);

        return TRUE;
}

void
kiosk_brightness_stop (KioskBrightness *self)
{
        g_return_if_fail (KIOSK_IS_BRIGHTNESS (self));

        g_debug ("KioskBrightness: Stopping");

        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self));
        g_clear_handle_id (&self->bus_id, g_bus_unown_name);
}
