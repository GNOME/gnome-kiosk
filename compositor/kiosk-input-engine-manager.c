#include "config.h"
#include "kiosk-input-engine-manager.h"

#include <stdlib.h>
#include <string.h>

#include <ibus.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-languages.h>

#include <meta/display.h>
#include <meta/util.h>

#include <meta/meta-backend.h>

#include "org.gnome.SessionManager.h"

#include "kiosk-gobject-utils.h"
#include "kiosk-input-sources-manager.h"

#define DEFAULT_INPUT_ENGINE_NAME "xkb:us::eng"
#define DEFAULT_LAYOUT_NAME "us"

struct _KioskInputEngineManager
{
        GObject                   parent;

        /* weak references */
        KioskInputSourcesManager *input_sources_manager;

        /* strong references */
        GCancellable             *cancellable;
        IBusBus                  *bus;
        GHashTable               *engines;
        char                     *active_engine;

        /* state */
        guint32                   is_loaded : 1;
};

enum
{
        PROP_INPUT_SOURCES_MANAGER = 1,
        PROP_IS_LOADED,
        PROP_ACTIVE_ENGINE,
        NUMBER_OF_PROPERTIES
};

static GParamSpec *kiosk_input_engine_manager_properties[NUMBER_OF_PROPERTIES] = { NULL, };

G_DEFINE_TYPE (KioskInputEngineManager, kiosk_input_engine_manager, G_TYPE_OBJECT)

static void kiosk_input_engine_manager_set_property (GObject      *object,
                                                     guint         property_id,
                                                     const GValue *value,
                                                     GParamSpec   *param_spec);
static void kiosk_input_engine_manager_get_property (GObject    *object,
                                                     guint       property_id,
                                                     GValue     *value,
                                                     GParamSpec *param_spec);

static void kiosk_input_engine_manager_constructed (GObject *object);
static void kiosk_input_engine_manager_dispose (GObject *object);

KioskInputEngineManager *
kiosk_input_engine_manager_new (KioskInputSourcesManager *input_sources_manager)
{
        GObject *object;

        object = g_object_new (KIOSK_TYPE_INPUT_ENGINE_MANAGER,
                               "input-sources-manager", input_sources_manager,
                               NULL);

        return KIOSK_INPUT_ENGINE_MANAGER (object);
}

static void
kiosk_input_engine_manager_class_init (KioskInputEngineManagerClass *input_engine_manager_class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (input_engine_manager_class);

        object_class->constructed = kiosk_input_engine_manager_constructed;
        object_class->set_property = kiosk_input_engine_manager_set_property;
        object_class->get_property = kiosk_input_engine_manager_get_property;
        object_class->dispose = kiosk_input_engine_manager_dispose;

        kiosk_input_engine_manager_properties[PROP_INPUT_SOURCES_MANAGER] = g_param_spec_object ("input-sources-manager",
                                                                                                 "input-sources-manager",
                                                                                                 "input-sources-manager",
                                                                                                 KIOSK_TYPE_INPUT_SOURCES_MANAGER,
                                                                                                 G_PARAM_CONSTRUCT_ONLY
                                                                                                 | G_PARAM_WRITABLE
                                                                                                 | G_PARAM_STATIC_NAME
                                                                                                 | G_PARAM_STATIC_NICK
                                                                                                 | G_PARAM_STATIC_BLURB);
        kiosk_input_engine_manager_properties[PROP_IS_LOADED] = g_param_spec_boolean ("is-loaded",
                                                                                      "is-loaded",
                                                                                      "is-loaded",
                                                                                      FALSE,
                                                                                      G_PARAM_READABLE
                                                                                      | G_PARAM_STATIC_NAME
                                                                                      | G_PARAM_STATIC_NICK
                                                                                      | G_PARAM_STATIC_BLURB);

        kiosk_input_engine_manager_properties[PROP_ACTIVE_ENGINE] = g_param_spec_string ("active-engine",
                                                                                         "active-engine",
                                                                                         "active-engine",
                                                                                         NULL,
                                                                                         G_PARAM_READABLE
                                                                                         | G_PARAM_STATIC_NAME
                                                                                         | G_PARAM_STATIC_NICK
                                                                                         | G_PARAM_STATIC_BLURB);

        g_object_class_install_properties (object_class, NUMBER_OF_PROPERTIES, kiosk_input_engine_manager_properties);
}

static void
kiosk_input_engine_manager_set_is_loaded (KioskInputEngineManager *self,
                                          gboolean                 is_loaded)
{
        if (self->is_loaded == is_loaded) {
                return;
        }

        if (is_loaded) {
                g_debug ("KioskInputEngineManager: Loaded");
        } else {
                g_debug ("KioskInputEngineManager: Unloaded");
        }

        self->is_loaded = is_loaded;
        g_object_notify (G_OBJECT (self), "is-loaded");
}

gboolean
kiosk_input_engine_manager_is_loaded (KioskInputEngineManager *self)
{
        g_return_val_if_fail (G_IS_OBJECT (self), FALSE);

        return self->is_loaded;
}

static void
kiosk_input_engine_manager_set_active_engine (KioskInputEngineManager *self,
                                              const char              *active_engine)
{
        if (g_strcmp0 (self->active_engine, active_engine) == 0) {
                return;
        }

        if (active_engine == NULL) {
                g_debug ("KioskInputEngineManager: There is now no active input engine");
        } else {
                g_debug ("KioskInputEngineManager: Active input engine is now '%s'", active_engine);
        }

        g_free (self->active_engine);
        self->active_engine = g_strdup (active_engine);

        g_object_notify (G_OBJECT (self), "active-engine");
}

const char *
kiosk_input_engine_manager_get_active_engine (KioskInputEngineManager *self)
{
        return self->active_engine;
}

static void
kiosk_input_engine_manager_set_property (GObject      *object,
                                         guint         property_id,
                                         const GValue *value,
                                         GParamSpec   *param_spec)
{
        KioskInputEngineManager *self = KIOSK_INPUT_ENGINE_MANAGER (object);

        switch (property_id) {
        case PROP_INPUT_SOURCES_MANAGER:
                g_set_weak_pointer (&self->input_sources_manager, g_value_get_object (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, param_spec);
                break;
        }
}

static void
kiosk_input_engine_manager_get_property (GObject    *object,
                                         guint       property_id,
                                         GValue     *value,
                                         GParamSpec *param_spec)
{
        KioskInputEngineManager *self = KIOSK_INPUT_ENGINE_MANAGER (object);

        switch (property_id) {
        case PROP_IS_LOADED:
                g_value_set_boolean (value, self->is_loaded);
                break;
        case PROP_ACTIVE_ENGINE:
                g_value_set_string (value, self->active_engine);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, param_spec);
                break;
        }
}

static gboolean
start_ibus (KioskInputEngineManager *self)
{
        g_autoptr (GSubprocessLauncher) launcher = NULL;
        g_autoptr (GError) error = NULL;
        const char *display;

        g_debug ("KioskInputEngineManager: Starting IBus daemon");
        launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);

        display = g_getenv ("GNOME_SETUP_DISPLAY");

        if (display != NULL) {
                g_subprocess_launcher_setenv (launcher, "DISPLAY", display, TRUE);
        }

        if (meta_is_wayland_compositor ()) {
                g_subprocess_launcher_spawn (launcher, &error, "ibus-daemon", NULL);
        } else {
                g_subprocess_launcher_spawn (launcher, &error, "ibus-daemon", "--xim", NULL);
        }

        if (error != NULL) {
                g_debug ("KioskInputEngineManager: Could not start IBus daemon: %s",
                         error->message);
                return FALSE;
        }

        return TRUE;
}

static void
fetch_available_engines (KioskInputEngineManager *self)
{
        g_autoptr (GList) engines_list = NULL;
        GList *node;

        g_debug ("KioskInputEngineManager: Fetching available IBus engines");

        engines_list = ibus_bus_list_engines (self->bus);

        for (node = engines_list; node != NULL; node = node->next) {
                IBusEngineDesc *engine_description = node->data;
                const char *name = ibus_engine_desc_get_name (engine_description);

                g_hash_table_insert (self->engines,
                                     g_strdup (name),
                                     g_object_ref (engine_description));
        }

        g_debug ("KioskInputEngineManager: Found %d engines.", g_list_length (engines_list));
}

static void
on_active_ibus_engine_changed (KioskInputEngineManager *self,
                               const char              *name)
{
        g_debug ("KioskInputEngineManager: global engine changed");

        kiosk_input_engine_manager_set_active_engine (self, name);
}

static void
on_active_ibus_engine_fetched (IBusBus                 *bus,
                               GAsyncResult            *result,
                               KioskInputEngineManager *self)
{
        g_autoptr (GError) error = NULL;

        ibus_bus_get_global_engine_async_finish (bus, result, &error);

        if (error != NULL) {
                g_debug ("KioskInputEngineManager: Could not fetch engine: %s",
                         error->message);
                kiosk_input_engine_manager_set_active_engine (self, NULL);
        } else {
                g_debug ("KioskInputEngineManager: Done fetching global engine");
        }

        kiosk_input_engine_manager_set_is_loaded (self, TRUE);
}

static void
fetch_active_ibus_engine (KioskInputEngineManager *self)
{
        g_debug ("KioskInputEngineManager: Fetching active IBus global engine...");
        ibus_bus_get_global_engine_async (self->bus,
                                          -1,
                                          self->cancellable,
                                          (GAsyncReadyCallback)
                                          on_active_ibus_engine_fetched,
                                          self);
}

static void
on_ibus_bus_connected (KioskInputEngineManager *self)
{
        g_debug ("KioskInputEngineManager: Connected to IBus");
        fetch_available_engines (self);

        kiosk_gobject_utils_queue_defer_callback (G_OBJECT (self),
                                                  "[kiosk-input-engine-manager] fetch_active_ibus_engine",
                                                  self->cancellable,
                                                  KIOSK_OBJECT_CALLBACK (fetch_active_ibus_engine),
                                                  NULL);
}

static void
on_ibus_bus_disconnected (KioskInputEngineManager *self)
{
        g_debug ("KioskInputEngineManager: Disconnected from IBus");
        g_hash_table_remove_all (self->engines);
        kiosk_input_engine_manager_set_is_loaded (self, FALSE);

        kiosk_input_engine_manager_set_active_engine (self, NULL);
}

static gboolean
kiosk_input_engine_manager_connect_to_ibus (KioskInputEngineManager *self)
{
        gboolean input_engine_started;

        g_debug ("KioskInputEngineManager: Connecting to IBus");

        input_engine_started = start_ibus (self);

        if (!input_engine_started) {
                return FALSE;
        }

        ibus_init ();

        self->engines = g_hash_table_new_full (g_str_hash,
                                               g_str_equal,
                                               (GDestroyNotify) g_free,
                                               (GDestroyNotify) g_object_unref);
        self->bus = ibus_bus_new ();
        g_signal_connect_object (G_OBJECT (self->bus),
                                 "connected",
                                 G_CALLBACK (on_ibus_bus_connected),
                                 self,
                                 G_CONNECT_SWAPPED);
        g_signal_connect_object (G_OBJECT (self->bus),
                                 "disconnected",
                                 G_CALLBACK (on_ibus_bus_disconnected),
                                 self,
                                 G_CONNECT_SWAPPED);
        g_signal_connect_object (G_OBJECT (self->bus),
                                 "global-engine-changed",
                                 G_CALLBACK (on_active_ibus_engine_changed),
                                 self,
                                 G_CONNECT_SWAPPED);
        ibus_bus_set_watch_ibus_signal (self->bus, TRUE);

        return self->bus != NULL;
}

gboolean
kiosk_input_engine_manager_find_layout_for_engine (KioskInputEngineManager *self,
                                                   const char              *engine_name,
                                                   const char             **layout,
                                                   const char             **variant)
{
        IBusEngineDesc *engine_description;

        g_return_val_if_fail (G_IS_OBJECT (self), FALSE);

        g_debug ("KioskInputEngineManager: Fetching input engine '%s'", engine_name);

        engine_description = g_hash_table_lookup (self->engines, engine_name);

        if (engine_description == NULL) {
                g_debug ("KioskInputEngineManager: Could not find input engine");
                return FALSE;
        }

        *layout = ibus_engine_desc_get_layout (engine_description);

        if (g_strcmp0 (*layout, "default") == 0) {
                *layout = DEFAULT_LAYOUT_NAME;
        }

        *variant = ibus_engine_desc_get_layout_variant (engine_description);

        return TRUE;
}

gboolean
kiosk_input_engine_manager_describe_engine (KioskInputEngineManager *self,
                                            const char              *engine_name,
                                            char                   **short_description,
                                            char                   **full_description)
{
        IBusEngineDesc *engine_description;
        const char *locale;

        g_return_val_if_fail (G_IS_OBJECT (self), FALSE);

        g_debug ("KioskInputEngineManager: Fetching input engine '%s'", engine_name);

        engine_description = g_hash_table_lookup (self->engines, engine_name);

        if (engine_description == NULL) {
                g_debug ("KioskInputEngineManager: Could not find input engine");
                return FALSE;
        }

        locale = ibus_engine_desc_get_language (engine_description);
        if (full_description != NULL) {
                const char *language_name, *engine_long_name, *text_domain;

                language_name = ibus_get_language_name (locale);
                text_domain = ibus_engine_desc_get_textdomain (engine_description);
                engine_long_name = ibus_engine_desc_get_longname (engine_description);

                *full_description = g_strdup_printf ("%s (%s)",
                                                     language_name,
                                                     g_dgettext (text_domain, engine_long_name));
        }

        if (short_description != NULL) {
                const char *symbol;

                symbol = ibus_engine_desc_get_symbol (engine_description);

                if (symbol == NULL || symbol[0] == '\0') {
                        char *language_code = NULL;
                        gboolean locale_parsed;

                        locale_parsed = gnome_parse_locale (locale, &language_code, NULL, NULL, NULL);

                        if (!locale_parsed || strlen (language_code) > 3) {
                                *short_description = g_strdup ("⌨");
                        } else {
                                *short_description = language_code;
                        }
                } else {
                        *short_description = g_strdup (symbol);
                }
        }

        return TRUE;
}

gboolean
kiosk_input_engine_manager_activate_engine (KioskInputEngineManager *self,
                                            const char              *engine_name)
{
        IBusEngineDesc *engine_description;

        g_return_val_if_fail (G_IS_OBJECT (self), FALSE);

        if (!self->is_loaded) {
                return engine_name == NULL;
        }

        if (engine_name == NULL) {
                engine_name = DEFAULT_INPUT_ENGINE_NAME;
        }

        g_debug ("KioskInputEngineManager: Activating input engine %s", engine_name);
        engine_description = g_hash_table_lookup (self->engines, engine_name);

        if (engine_description == NULL) {
                g_debug ("KioskInputEngineManager: Could not find input engine");
                return FALSE;
        }

        return ibus_bus_set_global_engine (self->bus, engine_name);
}

static void
tell_session_manager_about_ibus (KioskInputEngineManager *self)
{
        g_autoptr (GDBusConnection) user_bus = NULL;
        g_autoptr (GsmSessionManager) session_manager = NULL;
        g_autoptr (GError) error = NULL;
        g_autoptr (GVariant) reply = NULL;

        g_debug ("KioskInputEngineManager: Telling session manager about IBus");

        user_bus = g_bus_get_sync (G_BUS_TYPE_SESSION,
                                   self->cancellable,
                                   &error);
        if (error != NULL) {
                g_debug ("KioskInputEngineManager: Could not contact user bus: %s",
                         error->message);
                return;
        }

        session_manager = gsm_session_manager_proxy_new_sync (user_bus,
                                                              G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
                                                              G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                              "org.gnome.SessionManager",
                                                              "/org/gnome/SessionManager",
                                                              self->cancellable,
                                                              &error);

        if (error != NULL) {
                g_debug ("KioskInputEngineManager: Could not contact session manager: %s",
                         error->message);
                return;
        }

        gsm_session_manager_call_setenv_sync (session_manager,
                                              "GTK_IM_MODULE",
                                              "ibus",
                                              self->cancellable,
                                              &error);

        if (error != NULL) {
                g_debug ("KioskInputEngineManager: Could not tell session manager about IBus: %s",
                         error->message);
                return;
        }
}

static void
kiosk_input_engine_manager_init (KioskInputEngineManager *self)
{
        gboolean connected_to_ibus;

        g_debug ("KioskInputEngineManager: Initializing");

        self->cancellable = g_cancellable_new ();

        connected_to_ibus = kiosk_input_engine_manager_connect_to_ibus (self);

        if (connected_to_ibus) {
                tell_session_manager_about_ibus (self);
        }
}

static void
kiosk_input_engine_manager_constructed (GObject *object)
{
        G_OBJECT_CLASS (kiosk_input_engine_manager_parent_class)->constructed (object);
}

static void
kiosk_input_engine_manager_dispose (GObject *object)
{
        KioskInputEngineManager *self = KIOSK_INPUT_ENGINE_MANAGER (object);

        g_debug ("KioskInputEngineManager: Disposing");

        if (self->cancellable != NULL) {
                g_cancellable_cancel (self->cancellable);
                g_clear_object (&self->cancellable);
        }

        g_clear_weak_pointer (&self->input_sources_manager);

        G_OBJECT_CLASS (kiosk_input_engine_manager_parent_class)->dispose (object);
}
