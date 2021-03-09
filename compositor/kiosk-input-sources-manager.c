#include "config.h"
#include "kiosk-input-sources-manager.h"

#include <stdlib.h>
#include <string.h>

#include <meta/display.h>
#include <meta/util.h>

#include <meta/meta-backend.h>
#include <meta/meta-plugin.h>

#include "org.freedesktop.locale1.h"
#include "kiosk-compositor.h"

#define SD_LOCALE1_BUS_NAME "org.freedesktop.locale1"
#define SD_LOCALE1_OBJECT_PATH "/org/freedesktop/locale1"

struct _KioskInputSourcesManager
{
        GObject parent;

        /* weak references */
        KioskCompositor *compositor;

        /* strong references */
        GCancellable *cancellable;
        SdLocale1 *locale_proxy;
};

enum
{
  PROP_COMPOSITOR = 1,
  NUMBER_OF_PROPERTIES
};
static GParamSpec *kiosk_input_sources_manager_properties[NUMBER_OF_PROPERTIES] = { NULL, };

G_DEFINE_TYPE (KioskInputSourcesManager, kiosk_input_sources_manager, G_TYPE_OBJECT)

static void kiosk_input_sources_manager_set_property (GObject      *object,
                                                     guint         property_id,
                                                     const GValue *value,
                                                     GParamSpec   *param_spec);
static void kiosk_input_sources_manager_get_property (GObject    *object,
                                                     guint       property_id,
                                                     GValue     *value,
                                                     GParamSpec *param_spec);

static void kiosk_input_sources_manager_constructed (GObject *object);
static void kiosk_input_sources_manager_dispose (GObject *object);

KioskInputSourcesManager *
kiosk_input_sources_manager_new (KioskCompositor *compositor)
{
        GObject *object;

        object = g_object_new (KIOSK_TYPE_INPUT_SOURCES_MANAGER,
                               "compositor", compositor,
                               NULL);

        return KIOSK_INPUT_SOURCES_MANAGER (object);
}

static void
kiosk_input_sources_manager_class_init (KioskInputSourcesManagerClass *input_sources_manager_class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (input_sources_manager_class);

        object_class->constructed = kiosk_input_sources_manager_constructed;
        object_class->set_property = kiosk_input_sources_manager_set_property;
        object_class->get_property = kiosk_input_sources_manager_get_property;
        object_class->dispose = kiosk_input_sources_manager_dispose;

        kiosk_input_sources_manager_properties[PROP_COMPOSITOR] = g_param_spec_object ("compositor",
                                                                                      "compositor",
                                                                                      "compositor",
                                                                                      KIOSK_TYPE_COMPOSITOR,
                                                                                      G_PARAM_CONSTRUCT_ONLY
                                                                                      | G_PARAM_WRITABLE
                                                                                      | G_PARAM_STATIC_NAME
                                                                                      | G_PARAM_STATIC_NICK
                                                                                      | G_PARAM_STATIC_BLURB);
        g_object_class_install_properties (object_class, NUMBER_OF_PROPERTIES, kiosk_input_sources_manager_properties);
}

static void
kiosk_input_sources_manager_set_property (GObject      *object,
                                         guint         property_id,
                                         const GValue *value,
                                         GParamSpec   *param_spec)
{
        KioskInputSourcesManager *self = KIOSK_INPUT_SOURCES_MANAGER (object);

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
kiosk_input_sources_manager_get_property (GObject    *object,
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
set_keymap_from_system_configuration (KioskInputSourcesManager *self)
{
        const char *layout = NULL;
        const char *options = NULL;
        const char *variant = NULL;

        if (self->locale_proxy == NULL) {
                return;
        }

        g_debug ("KiosInputSourcesManager: Setting keymap from system configuration");

        layout = sd_locale1_get_x11_layout (self->locale_proxy);
        g_debug ("KioskInputSourcesManager: System layout is '%s'", layout);

        options = sd_locale1_get_x11_options (self->locale_proxy);
        g_debug ("KioskInputSourcesManager: System layout options are '%s'", options);

        variant = sd_locale1_get_x11_variant (self->locale_proxy);
        g_debug ("KioskInputSourcesManager: System layout variant is '%s'", variant);

        meta_backend_set_keymap (meta_get_backend (), layout, options, variant);
}

static gboolean
kiosk_input_sources_manager_connect_to_localed (KioskInputSourcesManager *self)
{
        g_autoptr (GDBusConnection) system_bus = NULL;
        g_autoptr (GError) error = NULL;

        g_debug ("KioskInputSourcesManager: Connecting to localed");

        system_bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM,
                                     self->cancellable,
                                     &error);

        self->locale_proxy = sd_locale1_proxy_new_sync (system_bus,
                                                        G_DBUS_PROXY_FLAGS_NONE,
                                                        SD_LOCALE1_BUS_NAME,
                                                        SD_LOCALE1_OBJECT_PATH,
                                                        self->cancellable,
                                                        &error);

        if (error != NULL) {
                g_debug ("KioskInputSourcesManager: Could not connect to localed: %s",
                         error->message);
                return FALSE;
        } else {
                g_debug ("KioskInputSourcesManager: Connected to localed");
        }

        return TRUE;
}

static void
kiosk_input_sources_manager_constructed (GObject *object)
{
        KioskInputSourcesManager *self = KIOSK_INPUT_SOURCES_MANAGER (object);

        g_debug ("KioskInputSourcesManager: Initializing");

        self->cancellable = g_cancellable_new ();

        kiosk_input_sources_manager_connect_to_localed (self);
        set_keymap_from_system_configuration (self);
}

static void
kiosk_input_sources_manager_init (KioskInputSourcesManager *self)
{
}

static void
kiosk_input_sources_manager_dispose (GObject *object)
{
        KioskInputSourcesManager *self = KIOSK_INPUT_SOURCES_MANAGER (object);

        if (self->cancellable != NULL) {
                g_cancellable_cancel (self->cancellable);
                g_clear_object (&self->cancellable);
        }

        g_clear_object (&self->locale_proxy);

        g_clear_weak_pointer (&self->compositor);

        G_OBJECT_CLASS (kiosk_input_sources_manager_parent_class)->dispose (object);
}
