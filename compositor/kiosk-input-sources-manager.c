#include "config.h"
#include "kiosk-input-sources-manager.h"

#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon.h>
#include <meta/display.h>
#include <meta/keybindings.h>
#include <meta/util.h>

#include <meta/meta-backend.h>
#include <meta/meta-plugin.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-languages.h>
#include <libgnome-desktop/gnome-xkb-info.h>

#include "org.freedesktop.locale1.h"
#include "kiosk-compositor.h"
#include "kiosk-dbus-utils.h"
#include "kiosk-gobject-utils.h"
#include "kiosk-input-engine-manager.h"
#include "kiosk-input-source-group.h"
#include "kiosk-x-keyboard-manager.h"

#define SD_LOCALE1_BUS_NAME "org.freedesktop.locale1"
#define SD_LOCALE1_OBJECT_PATH "/org/freedesktop/locale1"

#define KIOSK_INPUT_SOURCES_SCHEMA "org.gnome.desktop.input-sources"
#define KIOSK_INPUT_SOURCES_SETTING "sources"
#define KIOSK_INPUT_OPTIONS_SETTING "xkb-options"

#define KIOSK_INPUT_SOURCE_OBJECTS_PATH_PREFIX "/org/gnome/Kiosk/InputSources"
#define KIOSK_KEYBINDINGS_SCHEMA "org.gnome.desktop.wm.keybindings"
#define KIOSK_SWITCH_INPUT_SOURCES_KEYBINDING "switch-input-source"
#define KIOSK_SWITCH_INPUT_SOURCES_BACKWARD_KEYBINDING "switch-input-source-backward"

#define KIOSK_DBUS_INPUT_SOURCES_MANGER_INPUT_SOURCE_INTERFACE "org.gnome.Kiosk.InputSources.InputSource"

typedef enum
{
        KIOSK_INPUT_SOURCE_CONFIGURATION_SYSTEM,
        KIOSK_INPUT_SOURCE_CONFIGURATION_SESSION,
        KIOSK_INPUT_SOURCE_CONFIGURATION_OVERRIDE
} KioskInputSourceConfiguration;

struct _KioskInputSourcesManager
{
        GObject                       parent;

        /* weak references */
        KioskCompositor              *compositor;
        MetaDisplay                  *display;

        KioskDBusInputSourcesManager *dbus_service;
        GDBusObjectManagerServer     *dbus_object_manager;

        /* strong references */
        GCancellable                 *cancellable;
        KioskInputEngineManager      *input_engine_manager;
        KioskXKeyboardManager        *x_keyboard_manager;
        SdLocale1                    *locale_proxy;
        GnomeXkbInfo                 *xkb_info;
        GSettings                    *input_sources_settings;
        GSettings                    *key_binding_settings;
        GPtrArray                    *input_source_groups;

        /* state */
        ssize_t                       input_source_groups_index;
        KioskInputSourceConfiguration configuration_source;
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
static void kiosk_input_sources_manager_switch_to_next_input_source (KioskInputSourcesManager *self);
static void kiosk_input_sources_manager_switch_to_previous_input_source (KioskInputSourcesManager *self);
static void sync_dbus_service (KioskInputSourcesManager *self);

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

static KioskInputSourceGroup *
kiosk_input_sources_manager_get_selected_input_source_group (KioskInputSourcesManager *self)
{
        if (self->input_source_groups->len == 0) {
                return NULL;
        }

        return g_ptr_array_index (self->input_source_groups, self->input_source_groups_index);
}

static gboolean
activate_first_available_input_source_group (KioskInputSourcesManager *self)
{
        size_t i;

        for (i = 0; i < self->input_source_groups->len; i++) {
                KioskInputSourceGroup *input_source_group = g_ptr_array_index (self->input_source_groups, i);
                gboolean input_source_group_active;

                input_source_group_active = kiosk_input_source_group_activate (input_source_group);

                if (input_source_group_active) {
                        self->input_source_groups_index = i;
                        sync_dbus_service (self);
                        return TRUE;
                }
        }

        return FALSE;
}

static gboolean
activate_input_source_group_if_it_has_engine (KioskInputSourcesManager *self,
                                              KioskInputSourceGroup    *input_source_group,
                                              const char               *name)
{
        const char *input_engine_name = NULL;
        gboolean input_source_group_active;

        input_engine_name = kiosk_input_source_group_get_input_engine (input_source_group);

        if (g_strcmp0 (input_engine_name, name) != 0) {
                return FALSE;
        }

        input_source_group_active = kiosk_input_source_group_activate (input_source_group);

        if (input_source_group_active) {
                sync_dbus_service (self);
        }

        return input_source_group_active;
}

static gboolean
activate_input_source_group_with_engine (KioskInputSourcesManager *self,
                                         const char               *name)
{
        KioskInputSourceGroup *input_source_group;
        gboolean input_source_group_active = FALSE;
        size_t i;

        input_source_group = kiosk_input_sources_manager_get_selected_input_source_group (self);

        if (input_source_group != NULL) {
                input_source_group_active = activate_input_source_group_if_it_has_engine (self, input_source_group, name);
        }

        if (input_source_group_active) {
                return TRUE;
        }

        for (i = 0; i < self->input_source_groups->len; i++) {
                input_source_group = g_ptr_array_index (self->input_source_groups, i);

                input_source_group_active = activate_input_source_group_if_it_has_engine (self, input_source_group, name);

                if (input_source_group_active) {
                        self->input_source_groups_index = i;
                        return TRUE;
                }
        }

        return FALSE;
}

static gboolean
activate_input_source_group_if_it_has_layout (KioskInputSourcesManager *self,
                                              KioskInputSourceGroup    *input_source_group,
                                              const char               *name)
{
        const char *selected_layout = NULL;
        gboolean layout_selected, input_source_group_active = FALSE;

        selected_layout = kiosk_input_source_group_get_selected_layout (input_source_group);

        if (g_strcmp0 (selected_layout, name) == 0) {
                layout_selected = TRUE;
        } else {
                layout_selected = kiosk_input_source_group_switch_to_layout (input_source_group, name);
        }

        if (layout_selected) {
                input_source_group_active = kiosk_input_source_group_activate (input_source_group);
        }

        if (input_source_group_active) {
                sync_dbus_service (self);
        }

        return input_source_group_active;
}

static gboolean
activate_input_source_group_with_layout (KioskInputSourcesManager *self,
                                         const char               *name)
{
        KioskInputSourceGroup *input_source_group;
        gboolean input_source_group_active = FALSE;
        size_t i;

        input_source_group = kiosk_input_sources_manager_get_selected_input_source_group (self);

        if (input_source_group != NULL) {
                input_source_group_active = activate_input_source_group_if_it_has_layout (self, input_source_group, name);
        }

        if (input_source_group_active) {
                return TRUE;
        }

        for (i = 0; i < self->input_source_groups->len; i++) {
                input_source_group = g_ptr_array_index (self->input_source_groups, i);

                input_source_group_active = activate_input_source_group_if_it_has_layout (self, input_source_group, name);

                if (input_source_group_active) {
                        self->input_source_groups_index = i;
                        return TRUE;
                }
        }

        return FALSE;
}

static gboolean
activate_best_available_input_source_group (KioskInputSourcesManager *self,
                                            const char               *input_engine,
                                            const char               *selected_layout)
{
        gboolean input_source_group_active = FALSE;

        if (input_engine != NULL) {
                input_source_group_active = activate_input_source_group_with_engine (self, input_engine);
        } else if (selected_layout != NULL) {
                input_source_group_active = activate_input_source_group_with_layout (self, selected_layout);
        }

        if (!input_source_group_active) {
                input_source_group_active = activate_first_available_input_source_group (self);
        }

        return input_source_group_active;
}

static char *
get_dbus_object_path_name_for_input_source (KioskInputSourcesManager *self,
                                            const char               *type,
                                            const char               *name)
{
        g_autofree char *escaped_name = NULL;
        g_autofree char *base_name = NULL;
        char *object_path;

        escaped_name = kiosk_dbus_utils_escape_object_path (name, strlen (name));
        base_name = g_strdup_printf ("%s_%s", type, escaped_name);

        object_path = g_build_path ("/", KIOSK_INPUT_SOURCE_OBJECTS_PATH_PREFIX, base_name, NULL);

        return object_path;
}

static void
sync_selected_input_source_to_dbus_service (KioskInputSourcesManager *self)
{
        KioskInputSourceGroup *input_source_group;
        const char *backend_type;
        const char *backend_id;
        const char *input_engine_name;
        g_autofree char *selected_layout = NULL;
        g_autofree char *object_path_name = NULL;

        input_source_group = kiosk_input_sources_manager_get_selected_input_source_group (self);

        if (input_source_group == NULL) {
                return;
        }

        input_engine_name = kiosk_input_source_group_get_input_engine (input_source_group);

        if (input_engine_name != NULL) {
                backend_type = "ibus";
                backend_id = input_engine_name;
        } else {
                selected_layout = kiosk_input_source_group_get_selected_layout (input_source_group);

                if (selected_layout == NULL) {
                        return;
                }

                backend_type = "xkb";
                backend_id = selected_layout;
        }

        object_path_name = get_dbus_object_path_name_for_input_source (self, backend_type, backend_id);

        g_debug ("KioskInputSourceGroup: Setting SelectedInputSource D-Bus property to %s", object_path_name);

        kiosk_dbus_input_sources_manager_set_selected_input_source (self->dbus_service, object_path_name);
}

static void
export_input_source_object_to_dbus_service (KioskInputSourcesManager *self,
                                            const char               *object_path_name,
                                            const char               *backend_type,
                                            const char               *backend_id,
                                            const char               *short_name,
                                            const char               *full_name)
{
        g_autoptr (GDBusInterface) dbus_input_source = NULL;
        g_autoptr (GDBusObject) object_path = NULL;

        dbus_input_source = g_dbus_object_manager_get_interface (G_DBUS_OBJECT_MANAGER (self->dbus_object_manager),
                                                                 object_path_name,
                                                                 KIOSK_DBUS_INPUT_SOURCES_MANGER_INPUT_SOURCE_INTERFACE);

        if (dbus_input_source == NULL) {
                dbus_input_source = G_DBUS_INTERFACE (kiosk_dbus_input_source_skeleton_new ());
        }

        kiosk_dbus_input_source_set_backend_type (KIOSK_DBUS_INPUT_SOURCE (dbus_input_source), backend_type);
        kiosk_dbus_input_source_set_full_name (KIOSK_DBUS_INPUT_SOURCE (dbus_input_source), full_name);
        kiosk_dbus_input_source_set_short_name (KIOSK_DBUS_INPUT_SOURCE (dbus_input_source), short_name);
        kiosk_dbus_input_source_set_backend_id (KIOSK_DBUS_INPUT_SOURCE (dbus_input_source), backend_id);

        object_path = g_dbus_object_manager_get_object (G_DBUS_OBJECT_MANAGER (self->dbus_object_manager),
                                                        object_path_name);

        if (object_path == NULL) {
                object_path = G_DBUS_OBJECT (kiosk_dbus_object_skeleton_new (object_path_name));
                g_dbus_object_manager_server_export (self->dbus_object_manager, G_DBUS_OBJECT_SKELETON (object_path));
        }

        kiosk_dbus_object_skeleton_set_input_source (KIOSK_DBUS_OBJECT_SKELETON (object_path), KIOSK_DBUS_INPUT_SOURCE (dbus_input_source));
}

static GList *
prune_object_path_from_list (KioskInputSourcesManager *self,
                             const char               *name,
                             GList                    *list)
{
        GList *node;

        for (node = list; node != NULL; node = node->next) {
                GDBusObject *dbus_object = node->data;
                const char *candidate_name = g_dbus_object_get_object_path (dbus_object);

                if (g_strcmp0 (candidate_name, name) == 0) {
                        list = g_list_remove_link (list, node);
                        g_object_unref (dbus_object);
                        return list;
                }
        }

        return list;
}

static void
unexport_input_sources_from_dbus_service (KioskInputSourcesManager *self,
                                          GList                    *list)
{
        GList *node;

        for (node = list; node != NULL; node = node->next) {
                GDBusObject *dbus_object = node->data;
                g_autoptr (GDBusInterface) dbus_input_source = NULL;
                const char *name = g_dbus_object_get_object_path (dbus_object);

                dbus_input_source = g_dbus_object_get_interface (dbus_object, KIOSK_DBUS_INPUT_SOURCES_MANGER_INPUT_SOURCE_INTERFACE);
                if (dbus_input_source == NULL) {
                        continue;
                }

                g_dbus_object_manager_server_unexport (G_DBUS_OBJECT_MANAGER_SERVER (self->dbus_object_manager),
                                                       name);
        }
}

static void
sync_all_input_sources_to_dbus_service (KioskInputSourcesManager *self)
{
        GList *stale_dbus_objects;

        g_autoptr (GPtrArray) sorted_input_sources = NULL;
        g_autofree char *input_sources_string = NULL;
        size_t i;

        stale_dbus_objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (self->dbus_object_manager));

        sorted_input_sources = g_ptr_array_new_full (self->input_source_groups->len * 3, g_free);
        for (i = 0; i < self->input_source_groups->len; i++) {
                KioskInputSourceGroup *input_source_group = g_ptr_array_index (self->input_source_groups, i);
                const char *input_engine_name;
                const char *backend_type;
                const char *backend_id;
                g_auto (GStrv) layouts = NULL;
                size_t i;

                input_engine_name = kiosk_input_source_group_get_input_engine (input_source_group);

                if (input_engine_name != NULL) {
                        char *object_path_name = NULL;
                        g_autofree char *full_name = NULL;
                        g_autofree char *short_name = NULL;

                        backend_type = "ibus";
                        backend_id = input_engine_name;

                        kiosk_input_engine_manager_describe_engine (self->input_engine_manager, input_engine_name, &short_name, &full_name);

                        object_path_name = get_dbus_object_path_name_for_input_source (self, backend_type, backend_id);
                        stale_dbus_objects = prune_object_path_from_list (self,
                                                                          object_path_name,
                                                                          stale_dbus_objects);
                        export_input_source_object_to_dbus_service (self, object_path_name, backend_type, backend_id, short_name, full_name);
                        g_ptr_array_add (sorted_input_sources, object_path_name);
                        continue;
                }

                layouts = kiosk_input_source_group_get_layouts (input_source_group);

                backend_type = "xkb";
                for (i = 0; layouts[i] != NULL; i++) {
                        char *object_path_name = NULL;
                        const char *short_name = NULL;
                        const char *full_name = NULL;
                        gboolean layout_info_found;

                        backend_id = layouts[i];

                        layout_info_found = gnome_xkb_info_get_layout_info (self->xkb_info,
                                                                            backend_id,
                                                                            &full_name,
                                                                            &short_name,
                                                                            NULL /* xkb layout */,
                                                                            NULL /* xkb variant */);

                        if (!layout_info_found) {
                                continue;
                        }

                        object_path_name = get_dbus_object_path_name_for_input_source (self, backend_type, backend_id);
                        stale_dbus_objects = prune_object_path_from_list (self,
                                                                          object_path_name,
                                                                          stale_dbus_objects);

                        export_input_source_object_to_dbus_service (self, object_path_name, backend_type, backend_id, short_name, full_name);
                        g_ptr_array_add (sorted_input_sources, object_path_name);
                }
        }
        g_ptr_array_add (sorted_input_sources, NULL);
        unexport_input_sources_from_dbus_service (self, stale_dbus_objects);
        g_list_free_full (stale_dbus_objects, g_object_unref);

        input_sources_string = g_strjoinv ("','", (GStrv) sorted_input_sources->pdata);
        g_debug ("KioskInputSourcesManager: InputSources D-Bus property set to ['%s']", input_sources_string);
        kiosk_dbus_input_sources_manager_set_input_sources (self->dbus_service, (const char * const *) sorted_input_sources->pdata);
}

static void
sync_dbus_service_now (KioskInputSourcesManager *self)
{
        g_debug ("KioskInputSourcesManager: Synchronizing D-Bus service with internal state");

        sync_all_input_sources_to_dbus_service (self);
        sync_selected_input_source_to_dbus_service (self);
}

static void
sync_dbus_service (KioskInputSourcesManager *self)
{
        kiosk_gobject_utils_queue_defer_callback (G_OBJECT (self),
                                                  "[kiosk-input-sources-manager] on_deferred_dbus_service_sync",
                                                  self->cancellable,
                                                  KIOSK_OBJECT_CALLBACK (sync_dbus_service_now),
                                                  NULL);
}

static gboolean
kiosk_input_sources_manager_set_input_sources (KioskInputSourcesManager *self,
                                               GVariant                 *input_sources,
                                               const char * const       *options)
{
        KioskInputSourceGroup *old_input_source_group;
        g_autofree char *old_input_engine = NULL;
        g_autofree char *old_selected_layout = NULL;

        g_autoptr (GVariantIter) iter = NULL;
        g_autofree char *options_string = NULL;
        const char *backend_type = NULL, *backend_id = NULL;
        gboolean input_source_group_active;

        old_input_source_group = kiosk_input_sources_manager_get_selected_input_source_group (self);

        if (old_input_source_group != NULL) {
                old_input_engine = g_strdup (kiosk_input_source_group_get_input_engine (old_input_source_group));
                old_selected_layout = kiosk_input_source_group_get_selected_layout (old_input_source_group);
        }

        kiosk_input_sources_manager_clear_input_sources (self);

        options_string = g_strjoinv (",", (GStrv) options);

        g_variant_get (input_sources, "a(ss)", &iter);
        while (g_variant_iter_loop (iter, "(ss)", &backend_type, &backend_id)) {
                if (g_strcmp0 (backend_type, "xkb") == 0) {
                        g_debug ("KioskInputSourcesManager:         %s", backend_id);
                        kiosk_input_sources_manager_add_layout (self, backend_id, options_string);
                } else if (g_strcmp0 (backend_type, "ibus") == 0) {
                        g_debug ("KioskInputSourcesManager:         %s", backend_id);
                        kiosk_input_sources_manager_add_input_engine (self, backend_id, options_string);
                } else {
                        g_debug ("KioskInputSourcesManager: Unknown input source type '%s' for source '%s'", backend_type, backend_id);
                }
        }

        input_source_group_active = activate_best_available_input_source_group (self, old_input_engine, old_selected_layout);

        sync_dbus_service (self);

        return input_source_group_active;
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

static gboolean
on_dbus_service_handle_set_input_sources (KioskInputSourcesManager *self,
                                          GDBusMethodInvocation    *invocation,
                                          GVariant                 *input_sources,
                                          const char * const       *options)
{
        g_autoptr (GVariantIter) iter = NULL;
        g_autofree char *input_sources_string = NULL;
        g_autofree char *options_string = NULL;

        input_sources_string = g_variant_print (input_sources, FALSE);
        options_string = g_strjoinv (",", (GStrv) options);

        g_debug ("KioskService: Handling SetInputSources(%s, [%s]) call",
                 input_sources_string, options_string);

        kiosk_input_sources_manager_set_input_sources (self, input_sources, options);

        kiosk_dbus_input_sources_manager_complete_set_input_sources (self->dbus_service, invocation);

        return TRUE;
}

static gboolean
on_dbus_service_handle_set_input_sources_from_locales (KioskInputSourcesManager *self,
                                                       GDBusMethodInvocation    *invocation,
                                                       const char * const       *locales,
                                                       const char * const       *options)
{
        g_autofree char *locales_string = NULL;
        g_autofree char *options_string = NULL;

        locales_string = g_strjoinv (",", (GStrv) locales);
        options_string = g_strjoinv (",", (GStrv) options);

        g_debug ("KioskService: Handling SetInputSourcesFromLocales([%s], [%s]) call",
                 locales_string, options_string);

        kiosk_input_sources_manager_set_input_sources_from_locales (self, locales, options_string);
        kiosk_dbus_input_sources_manager_complete_set_input_sources_from_locales (self->dbus_service, invocation);

        return TRUE;
}

static gboolean
on_dbus_service_handle_set_input_sources_from_session_configuration (KioskInputSourcesManager *self,
                                                                     GDBusMethodInvocation    *invocation)
{
        g_debug ("KioskService: Handling SetInputSourcesFromSessionConfiguration() call");

        kiosk_input_sources_manager_set_input_sources_from_session_configuration (self);
        kiosk_dbus_input_sources_manager_complete_set_input_sources_from_session_configuration (self->dbus_service, invocation);

        return TRUE;
}

static gboolean
on_dbus_service_handle_select_input_source (KioskInputSourcesManager *self,
                                            GDBusMethodInvocation    *invocation,
                                            const char               *object_path)
{
        g_autoptr (GDBusInterface) dbus_input_source = NULL;

        g_debug ("KioskService: Handling SelectInputSource('%s') call", object_path);

        dbus_input_source = g_dbus_object_manager_get_interface (G_DBUS_OBJECT_MANAGER (self->dbus_object_manager),
                                                                 object_path,
                                                                 KIOSK_DBUS_INPUT_SOURCES_MANGER_INPUT_SOURCE_INTERFACE);
        if (dbus_input_source != NULL) {
                const char *source_type = NULL;
                const char *source_name = NULL;
                const char *input_engine = NULL;
                const char *layout_name = NULL;

                source_type = kiosk_dbus_input_source_get_backend_type (KIOSK_DBUS_INPUT_SOURCE (dbus_input_source));
                source_name = kiosk_dbus_input_source_get_backend_id (KIOSK_DBUS_INPUT_SOURCE (dbus_input_source));

                if (g_strcmp0 (source_type, "ibus") == 0) {
                        input_engine = source_name;
                } else if (g_strcmp0 (source_type, "xkb") == 0) {
                        layout_name = source_name;
                }

                activate_best_available_input_source_group (self, input_engine, layout_name);
        }

        kiosk_dbus_input_sources_manager_complete_select_input_source (self->dbus_service, invocation);

        return TRUE;
}

static gboolean
on_dbus_service_handle_select_next_input_source (KioskInputSourcesManager *self,
                                                 GDBusMethodInvocation    *invocation)
{
        g_debug ("KioskService: Handling SelectNextInputSource() call");

        kiosk_input_sources_manager_switch_to_next_input_source (self);
        kiosk_dbus_input_sources_manager_complete_select_next_input_source (self->dbus_service, invocation);

        return TRUE;
}

static gboolean
on_dbus_service_handle_select_previous_input_source (KioskInputSourcesManager *self,
                                                     GDBusMethodInvocation    *invocation)
{
        g_debug ("KioskService: Handling SelectPreviousInputSource() call");

        kiosk_input_sources_manager_switch_to_previous_input_source (self);
        kiosk_dbus_input_sources_manager_complete_select_previous_input_source (self->dbus_service, invocation);

        return TRUE;
}

KioskInputEngineManager *
kiosk_input_sources_manager_get_input_engine_manager (KioskInputSourcesManager *self)
{
        return self->input_engine_manager;
}

KioskXKeyboardManager *
kiosk_input_sources_manager_get_x_keyboard_manager (KioskInputSourcesManager *self)
{
        return self->x_keyboard_manager;
}

void
kiosk_input_sources_manager_clear_input_sources (KioskInputSourcesManager *self)
{
        g_debug ("KioskInputSourcesManager: Clearing selected keyboard mappings");

        g_ptr_array_set_size (self->input_source_groups, 0);
        self->input_source_groups_index = 0;
}

static void
kiosk_input_sources_manager_add_input_source_group (KioskInputSourcesManager *self,
                                                    KioskInputSourceGroup    *input_source_group)
{
        g_ptr_array_add (self->input_source_groups, g_object_ref (input_source_group));
}

static KioskInputSourceGroup *
kiosk_input_sources_manager_add_new_input_source_group (KioskInputSourcesManager *self,
                                                        const char               *options)
{
        g_autoptr (KioskInputSourceGroup) input_source_group = NULL;

        g_debug ("KioskInputSourcesManager: Adding new, empty keyboard mapping with options '%s'",
                 options);

        input_source_group = kiosk_input_source_group_new (self->compositor, self);
        kiosk_input_source_group_set_options (input_source_group, options);

        kiosk_input_sources_manager_add_input_source_group (self, input_source_group);

        return input_source_group;
}

static KioskInputSourceGroup *
kiosk_input_sources_manager_get_newest_input_source_group (KioskInputSourcesManager *self)
{
        if (self->input_source_groups->len == 0) {
                return NULL;
        }

        return g_ptr_array_index (self->input_source_groups, self->input_source_groups->len - 1);
}

void
kiosk_input_sources_manager_add_layout (KioskInputSourcesManager *self,
                                        const char               *id,
                                        const char               *options)
{
        KioskInputSourceGroup *input_source_group = NULL;
        const char *xkb_layout = NULL;
        const char *xkb_variant = NULL;
        gboolean layout_info_found;
        gboolean mapping_full;

        g_debug ("KioskInputSourcesManager: Adding layout '%s' to keyboard mapping", id);

        layout_info_found = gnome_xkb_info_get_layout_info (self->xkb_info,
                                                            id,
                                                            NULL /* display name */,
                                                            NULL /* short name */,
                                                            &xkb_layout,
                                                            &xkb_variant);

        if (!layout_info_found) {
                g_debug ("KioskInputSourcesManager: Layout not found");
                return;
        }

        input_source_group = kiosk_input_sources_manager_get_newest_input_source_group (self);

        if (input_source_group == NULL) {
                g_debug ("KioskInputSourcesManager: No keyboard mappings found, creating one");

                input_source_group = kiosk_input_sources_manager_add_new_input_source_group (self, options);
        }

        mapping_full = !kiosk_input_source_group_add_layout (input_source_group, xkb_layout, xkb_variant);

        if (mapping_full) {
                g_debug ("KioskInputSourcesManager: Keyboard mapping full, starting another one");

                input_source_group = kiosk_input_sources_manager_add_new_input_source_group (self, options);

                kiosk_input_source_group_add_layout (input_source_group, xkb_layout, xkb_variant);
        }
}

void
kiosk_input_sources_manager_add_input_engine (KioskInputSourcesManager *self,
                                              const char               *engine_name,
                                              const char               *options)
{
        KioskInputSourceGroup *input_source_group = NULL;

        g_debug ("KioskInputSourcesManager: Adding input engine '%s'", engine_name);

        input_source_group = kiosk_input_sources_manager_add_new_input_source_group (self, options);

        kiosk_input_source_group_set_input_engine (input_source_group, engine_name);
        kiosk_input_source_group_set_options (input_source_group, options);
}


gboolean
kiosk_input_sources_manager_set_input_sources_from_system_configuration (KioskInputSourcesManager *self)
{
        g_autofree char *localed_name_owner = NULL;

        const char *layouts_string = NULL;

        g_auto (GStrv) layouts = NULL;
        size_t number_of_layouts = 0;

        const char *variants_string = NULL;

        g_auto (GStrv) variants = NULL;
        size_t number_of_variants = 0;

        const char *options = NULL;
        size_t i, j;

        gboolean input_source_group_active;

        g_return_val_if_fail (KIOSK_IS_INPUT_SOURCES_MANAGER (self), FALSE);

        if (self->locale_proxy == NULL) {
                return FALSE;
        }

        localed_name_owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (self->locale_proxy));

        if (localed_name_owner == NULL) {
                return FALSE;
        }

        g_debug ("KioskInputSourcesManager: Setting keymap from system configuration");

        layouts_string = sd_locale1_get_x11_layout (self->locale_proxy);
        if (layouts_string == NULL) {
                g_debug ("KioskInputSourcesManager: No layouts defined");
                return FALSE;
        }
        g_debug ("KioskInputSourcesManager: System layout is '%s'", layouts_string);

        layouts = g_strsplit (layouts_string, ",", -1);
        number_of_layouts = g_strv_length (layouts);

        options = sd_locale1_get_x11_options (self->locale_proxy);
        g_debug ("KioskInputSourcesManager: System layout options are '%s'", options);

        variants_string = sd_locale1_get_x11_variant (self->locale_proxy);
        g_debug ("KioskInputSourcesManager: System layout variant is '%s'", variants_string);
        variants = g_strsplit (variants_string, ",", -1);
        number_of_variants = g_strv_length (variants);

        if (number_of_layouts < number_of_variants) {
                g_debug ("KioskInputSourcesManager: There is a layout variant mismatch");
                return FALSE;
        }

        kiosk_input_sources_manager_clear_input_sources (self);

        for (i = 0, j = 0; layouts[i] != NULL; i++) {
                char *id = NULL;
                const char *layout = layouts[i];
                const char *variant = "";

                if (variants[j] != NULL) {
                        variant = variants[j++];
                }

                if (variant[0] == '\0') {
                        id = g_strdup (layout);
                } else {
                        id = g_strdup_printf ("%s+%s", layout, variant);
                }

                kiosk_input_sources_manager_add_layout (self, id, options);
        }

        input_source_group_active = activate_first_available_input_source_group (self);

        if (!input_source_group_active) {
                const char * const *locales;

                locales = sd_locale1_get_locale (self->locale_proxy);
                input_source_group_active = kiosk_input_sources_manager_set_input_sources_from_locales (self, locales, options);
        }

        sync_dbus_service (self);
        self->configuration_source = KIOSK_INPUT_SOURCE_CONFIGURATION_SYSTEM;

        if (!input_source_group_active) {
                g_debug ("KioskInputSourcesManager: System has no valid configured input sources");
                return FALSE;
        }

        return TRUE;
}

static void
on_session_input_configuration_changed (KioskInputSourcesManager *self)
{
        g_debug ("KioskInputSourcesManager: Session input sources configuration changed");

        if (self->configuration_source == KIOSK_INPUT_SOURCE_CONFIGURATION_OVERRIDE) {
                g_debug ("KioskInputSourcesManager: Ignoring change, because keymap is overriden");
                return;
        }

        kiosk_input_sources_manager_set_input_sources_from_session_configuration (self);
}

static void
on_session_input_sources_setting_changed (KioskInputSourcesManager *self)
{
        kiosk_gobject_utils_queue_defer_callback (G_OBJECT (self),
                                                  "[kiosk-input-sources-manager] on_session_input_configuration_changed",
                                                  self->cancellable,
                                                  KIOSK_OBJECT_CALLBACK (on_session_input_configuration_changed),
                                                  NULL);
}

gboolean
kiosk_input_sources_manager_set_input_sources_from_session_configuration (KioskInputSourcesManager *self)
{
        g_autoptr (GVariant) input_sources = NULL;
        g_auto (GStrv) options = NULL;
        gboolean input_sources_active;

        g_return_val_if_fail (KIOSK_IS_INPUT_SOURCES_MANAGER (self), FALSE);

        g_debug ("KioskInputSourcesManager: Setting input sources from session configuration");

        self->configuration_source = KIOSK_INPUT_SOURCE_CONFIGURATION_SESSION;

        if (self->input_sources_settings == NULL) {
                self->input_sources_settings = g_settings_new (KIOSK_INPUT_SOURCES_SCHEMA);

                g_signal_connect_object (G_OBJECT (self->input_sources_settings),
                                         "changed::" KIOSK_INPUT_SOURCES_SETTING,
                                         G_CALLBACK (on_session_input_sources_setting_changed),
                                         self,
                                         G_CONNECT_SWAPPED);
                g_signal_connect_object (G_OBJECT (self->input_sources_settings),
                                         "changed::" KIOSK_INPUT_OPTIONS_SETTING,
                                         G_CALLBACK (on_session_input_sources_setting_changed),
                                         self,
                                         G_CONNECT_SWAPPED);
        }


        options = g_settings_get_strv (self->input_sources_settings, KIOSK_INPUT_OPTIONS_SETTING);

        input_sources = g_settings_get_value (self->input_sources_settings,
                                              KIOSK_INPUT_SOURCES_SETTING);

        input_sources_active = kiosk_input_sources_manager_set_input_sources (self, input_sources, (const char * const *) options);

        if (!input_sources_active) {
                g_debug ("KioskInputSourcesManager: Session has no valid configured input sources");
                self->configuration_source = KIOSK_INPUT_SOURCE_CONFIGURATION_SYSTEM;
                return kiosk_input_sources_manager_set_input_sources_from_system_configuration (self);
        }

        return TRUE;
}

gboolean
kiosk_input_sources_manager_set_input_sources_from_locales (KioskInputSourcesManager *self,
                                                            const char * const       *locales,
                                                            const char               *options)
{
        KioskInputSourceGroup *old_input_source_group;
        g_autofree char *old_selected_layout = NULL;
        g_autofree char *old_input_engine = NULL;
        g_autofree char *locales_string = NULL;
        gboolean input_source_group_active;

        g_return_val_if_fail (KIOSK_IS_INPUT_SOURCES_MANAGER (self), FALSE);
        g_return_val_if_fail (locales != NULL, FALSE);

        locales_string = g_strjoinv (",", (GStrv) locales);

        g_debug ("KioskInputSourcesManager: Setting keymap from locales '%s'",
                 locales_string);

        self->configuration_source = KIOSK_INPUT_SOURCE_CONFIGURATION_OVERRIDE;

        old_input_source_group = kiosk_input_sources_manager_get_selected_input_source_group (self);

        if (old_input_source_group != NULL) {
                old_selected_layout = kiosk_input_source_group_get_selected_layout (old_input_source_group);
                old_input_engine = g_strdup (kiosk_input_source_group_get_input_engine (old_input_source_group));
        }

        kiosk_input_sources_manager_clear_input_sources (self);

        for (int i = 0; locales[i] != NULL; i++) {
                const char *locale = locales[i];
                const char *backend_type, *backend_id;
                gboolean input_source_found;

                input_source_found = gnome_get_input_source_from_locale (locale,
                                                                         &backend_type,
                                                                         &backend_id);

                if (!input_source_found) {
                        g_debug ("KioskInputSourcesManager: Could not find keymap details from locale '%s'",
                                 locale);
                        continue;
                }

                if (g_strcmp0 (backend_type, "xkb") == 0) {
                        g_debug ("KioskInputSourcesManager: Found XKB input source '%s' for locale '%s'",
                                 backend_id, locale);

                        kiosk_input_sources_manager_add_layout (self, backend_id, options);
                } else if (g_strcmp0 (backend_type, "ibus") == 0) {
                        g_debug ("KioskInputSourcesManager: Found IBus input source '%s' for locale '%s'",
                                 backend_id, locale);

                        kiosk_input_sources_manager_add_input_engine (self, backend_id, options);
                } else {
                        g_debug ("KioskInputSourcesManager: Unknown input source type '%s' for source '%s'", backend_type, backend_id);
                }
        }

        input_source_group_active = activate_first_available_input_source_group (self);

        sync_dbus_service (self);

        if (!input_source_group_active) {
                g_debug ("KioskInputSourcesManager: Locales haves no valid associated keyboard mappings");
                return FALSE;
        }

        return TRUE;
}

static void
on_system_configuration_changed (KioskInputSourcesManager *self)
{
        g_autofree char *localed_owner = NULL;

        localed_owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (self->locale_proxy));

        if (localed_owner == NULL) {
                g_debug ("KioskInputSourcesManager: System locale daemon exited");
                return;
        }

        g_debug ("KioskInputSourcesManager: System locale configuration changed");

        if (self->configuration_source == KIOSK_INPUT_SOURCE_CONFIGURATION_OVERRIDE) {
                g_debug ("KioskInputSourcesManager: Ignoring change, because keymap is overriden");
                return;
        }

        if (self->configuration_source != KIOSK_INPUT_SOURCE_CONFIGURATION_SYSTEM) {
                g_debug ("KioskInputSourcesManager: Ignoring change, because configuration source is not system");
                return;
        }

        kiosk_input_sources_manager_set_input_sources_from_system_configuration (self);
}

static void
on_localed_property_notify (KioskInputSourcesManager *self)
{
        kiosk_gobject_utils_queue_defer_callback (G_OBJECT (self),
                                                  "[kiosk-input-sources-manager] on_system_configuration_changed",
                                                  self->cancellable,
                                                  KIOSK_OBJECT_CALLBACK (on_system_configuration_changed),
                                                  NULL);
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

                g_signal_connect_object (G_OBJECT (self->locale_proxy),
                                         "notify",
                                         G_CALLBACK (on_localed_property_notify),
                                         self,
                                         G_CONNECT_SWAPPED);
        }

        return TRUE;
}

static void
kiosk_input_sources_manager_activate_input_sources (KioskInputSourcesManager *self)
{
        KioskInputSourceGroup *input_source_group;
        gboolean input_source_group_active;

        input_source_group = kiosk_input_sources_manager_get_selected_input_source_group (self);

        if (input_source_group == NULL) {
                g_debug ("KioskInputSourcesManager: No available keyboard mappings");
                return;
        }

        input_source_group_active = kiosk_input_source_group_activate (input_source_group);

        if (input_source_group_active) {
                sync_dbus_service (self);
        }
}

static void
kiosk_input_sources_manager_cycle_input_sources_forward (KioskInputSourcesManager *self)
{
        g_debug ("KioskInputSourcesManager: Cycling input sources forward");

        self->input_source_groups_index++;

        if (self->input_source_groups_index >= self->input_source_groups->len) {
                KioskInputSourceGroup *input_source_group;

                self->input_source_groups_index -= self->input_source_groups->len;
                input_source_group = kiosk_input_sources_manager_get_selected_input_source_group (self);
                kiosk_input_source_group_switch_to_first_layout (input_source_group);
        }

        kiosk_input_sources_manager_activate_input_sources (self);
}

static void
kiosk_input_sources_manager_cycle_input_sources_backward (KioskInputSourcesManager *self)
{
        g_debug ("KioskInputSourcesManager: Cycling input sources backward");

        self->input_source_groups_index--;

        if (self->input_source_groups_index < 0) {
                KioskInputSourceGroup *input_source_group;

                self->input_source_groups_index += self->input_source_groups->len;
                input_source_group = kiosk_input_sources_manager_get_selected_input_source_group (self);
                kiosk_input_source_group_switch_to_last_layout (input_source_group);
        }

        kiosk_input_sources_manager_activate_input_sources (self);
}

static void
kiosk_input_sources_manager_switch_to_next_input_source (KioskInputSourcesManager *self)
{
        KioskInputSourceGroup *input_source_group = NULL;
        gboolean had_next_layout;

        g_debug ("KioskInputSourcesManager: Switching to next input sources");

        input_source_group = kiosk_input_sources_manager_get_selected_input_source_group (self);

        if (input_source_group == NULL) {
                g_debug ("KioskInputSourcesManager: No input sources available");
                return;
        }

        had_next_layout = kiosk_input_source_group_switch_to_next_layout (input_source_group);

        if (!had_next_layout) {
                kiosk_input_sources_manager_cycle_input_sources_forward (self);
        }

        sync_dbus_service (self);
}

static void
kiosk_input_sources_manager_switch_to_previous_input_source (KioskInputSourcesManager *self)
{
        KioskInputSourceGroup *input_source_group = NULL;
        gboolean had_previous_layout;

        g_debug ("KioskInputSourcesManager: Switching to next input sources");

        input_source_group = kiosk_input_sources_manager_get_selected_input_source_group (self);

        if (input_source_group == NULL) {
                g_debug ("KioskInputSourcesManager: No input sources available");
                return;
        }

        had_previous_layout = kiosk_input_source_group_switch_to_previous_layout (input_source_group);

        if (!had_previous_layout) {
                kiosk_input_sources_manager_cycle_input_sources_backward (self);
        }

        sync_dbus_service (self);
}

static void
on_switch_input_sources (MetaDisplay              *display,
                         MetaWindow               *window,
                         ClutterKeyEvent          *event,
                         MetaKeyBinding           *binding,
                         KioskInputSourcesManager *self)
{
        g_debug ("KioskInputSourcesManager: Keybinding pressed to change input source");

        if (meta_key_binding_is_reversed (binding)) {
                kiosk_input_sources_manager_switch_to_previous_input_source (self);
        } else {
                kiosk_input_sources_manager_switch_to_next_input_source (self);
        }
}

static gboolean
on_modifiers_switch_input_sources_cb (MetaDisplay              *display,
                                      KioskInputSourcesManager *self)
{
        g_debug ("KioskInputSourcesManager: ISO_Next_Group key combo pressed to change input source");

        kiosk_input_sources_manager_switch_to_next_input_source (self);

        return FALSE;
}

static void
kiosk_input_sources_manager_add_key_bindings (KioskInputSourcesManager *self)
{
        g_debug ("KioskInputSourcesManager: Adding key bindings for layout switching");

        self->key_binding_settings = g_settings_new (KIOSK_KEYBINDINGS_SCHEMA);
        meta_display_add_keybinding (self->display,
                                     KIOSK_SWITCH_INPUT_SOURCES_KEYBINDING,
                                     self->key_binding_settings,
                                     META_KEY_BINDING_NONE,
                                     (MetaKeyHandlerFunc)
                                     on_switch_input_sources,
                                     self,
                                     NULL);

        meta_display_add_keybinding (self->display,
                                     KIOSK_SWITCH_INPUT_SOURCES_BACKWARD_KEYBINDING,
                                     self->key_binding_settings,
                                     META_KEY_BINDING_IS_REVERSED,
                                     (MetaKeyHandlerFunc)
                                     on_switch_input_sources,
                                     self,
                                     NULL);
        g_signal_connect (self->display,
                          "modifiers-accelerator-activated",
                          G_CALLBACK (on_modifiers_switch_input_sources_cb),
                          self);
}

static void
kiosk_input_sources_manager_remove_key_bindings (KioskInputSourcesManager *self)
{
        g_debug ("KioskInputSourcesManager: Removing key bindings for layout switching");
        meta_display_remove_keybinding (self->display, KIOSK_SWITCH_INPUT_SOURCES_BACKWARD_KEYBINDING);
        meta_display_remove_keybinding (self->display, KIOSK_SWITCH_INPUT_SOURCES_KEYBINDING);
        g_signal_handlers_disconnect_by_func (self->display,
                                              G_CALLBACK (on_modifiers_switch_input_sources_cb),
                                              self);

        g_clear_object (&self->key_binding_settings);
}

static void
kiosk_input_sources_manager_maybe_activate_higher_priority_input_engine (KioskInputSourcesManager *self)
{
        size_t i;

        /* It's possible the user has an input engine configured to be used, but it wasn't ready
         * before. If so, now that it's ready, we should activate it.
         */
        for (i = 0; i < self->input_source_groups_index; i++) {
                KioskInputSourceGroup *input_source_group = g_ptr_array_index (self->input_source_groups, i);
                const char *input_engine_name = NULL;
                gboolean input_source_group_active;

                input_engine_name = kiosk_input_source_group_get_input_engine (input_source_group);

                if (input_engine_name == NULL) {
                        break;
                }

                input_source_group_active = kiosk_input_source_group_activate (input_source_group);

                if (input_source_group_active) {
                        sync_dbus_service (self);
                        return;
                }
        }

        g_debug ("KioskInputSourcesManager: No higher priority input engines found, reactivating existing input source");
        kiosk_input_sources_manager_activate_input_sources (self);
}

static void
on_input_engine_manager_is_loaded_changed (KioskInputSourcesManager *self)
{
        gboolean input_engine_manager_is_loaded;

        input_engine_manager_is_loaded = kiosk_input_engine_manager_is_loaded (self->input_engine_manager);

        if (!input_engine_manager_is_loaded) {
                g_debug ("KioskInputSourcesManager: Input engine manager unloaded, activating first available input source");

                activate_first_available_input_source_group (self);
                return;
        }

        g_debug ("KioskInputSourcesManager: Input engine manager loaded, reevaluating available input sources");
        kiosk_input_sources_manager_maybe_activate_higher_priority_input_engine (self);
}

static void
on_input_engine_manager_active_engine_changed (KioskInputSourcesManager *self)
{
        gboolean is_loaded;
        const char *active_input_engine;

        is_loaded = kiosk_input_engine_manager_is_loaded (self->input_engine_manager);

        if (!is_loaded) {
                g_debug ("KioskInputSourcesManager: Input engine changed while input engine manager unloaded. Ignoring...");
                return;
        }

        active_input_engine = kiosk_input_engine_manager_get_active_engine (self->input_engine_manager);

        if (active_input_engine == NULL) {
                g_debug ("KioskInputSourcesManager: Input engine deactivated, activating first available input source");
                activate_first_available_input_source_group (self);
                return;
        }

        activate_input_source_group_with_engine (self, active_input_engine);
}

static void
kiosk_input_sources_manager_start_input_engine_manager (KioskInputSourcesManager *self)
{
        self->input_engine_manager = kiosk_input_engine_manager_new (self);

        g_signal_connect_object (G_OBJECT (self->input_engine_manager),
                                 "notify::is-loaded",
                                 G_CALLBACK (on_input_engine_manager_is_loaded_changed),
                                 self,
                                 G_CONNECT_SWAPPED);

        g_signal_connect_object (G_OBJECT (self->input_engine_manager),
                                 "notify::active-engine",
                                 G_CALLBACK (on_input_engine_manager_active_engine_changed),
                                 self,
                                 G_CONNECT_SWAPPED);
}

#ifdef HAVE_X11
static void
process_x_keyboard_manager_selected_layout_change (KioskInputSourcesManager *self)
{
        const char *selected_layout;

        selected_layout = kiosk_x_keyboard_manager_get_selected_layout (self->x_keyboard_manager);

        if (selected_layout == NULL) {
                return;
        }

        g_debug ("KioskInputSourcesManager: X server changed active layout to %s", selected_layout);

        activate_input_source_group_with_layout (self, selected_layout);

        sync_dbus_service (self);
}

static void
on_x_keyboard_manager_selected_layout_changed (KioskInputSourcesManager *self)
{
        /* We defer processing the layout change for a bit, because often in practice there is more than
         * one layout change at the same time, and only the last one is the desired one
         */
        kiosk_gobject_utils_queue_defer_callback (G_OBJECT (self),
                                                  "[kiosk-input-sources-manager] process_x_keyboard_manager_selected_layout_change",
                                                  self->cancellable,
                                                  KIOSK_OBJECT_CALLBACK (process_x_keyboard_manager_selected_layout_change),
                                                  NULL);
}

static gboolean
layouts_match_selected_input_source_group (KioskInputSourcesManager *self,
                                           const char * const       *layouts,
                                           const char               *options)
{
        KioskInputSourceGroup *input_source_group;

        g_auto (GStrv) current_layouts = NULL;
        const char *input_source_group_options;
        const char *input_engine_name;

        input_source_group = kiosk_input_sources_manager_get_selected_input_source_group (self);

        if (input_source_group == NULL) {
                return FALSE;
        }

        input_engine_name = kiosk_input_source_group_get_input_engine (input_source_group);

        if (input_engine_name != NULL) {
                return FALSE;
        }

        current_layouts = kiosk_input_source_group_get_layouts (input_source_group);

        if (!g_strv_equal ((const char * const *) current_layouts, layouts)) {
                return FALSE;
        }

        input_source_group_options = kiosk_input_source_group_get_options (input_source_group);

        if (g_strcmp0 (input_source_group_options, options) != 0) {
                return FALSE;
        }

        return TRUE;
}

static void
on_x_keyboard_manager_layouts_changed (KioskInputSourcesManager *self)
{
        const char * const *new_layouts;
        const char *selected_layout;
        const char *options;
        gboolean layouts_match;
        size_t i;

        new_layouts = kiosk_x_keyboard_manager_get_layouts (self->x_keyboard_manager);
        options = kiosk_x_keyboard_manager_get_options (self->x_keyboard_manager);
        layouts_match = layouts_match_selected_input_source_group (self, new_layouts, options);

        if (layouts_match) {
                return;
        }

        g_debug ("KioskInputSorcesManager: X server keyboard layouts changed");

        self->configuration_source = KIOSK_INPUT_SOURCE_CONFIGURATION_OVERRIDE;
        kiosk_input_sources_manager_clear_input_sources (self);

        for (i = 0; new_layouts[i] != NULL; i++) {
                kiosk_input_sources_manager_add_layout (self, new_layouts[i], options);
        }

        selected_layout = kiosk_x_keyboard_manager_get_selected_layout (self->x_keyboard_manager);

        if (selected_layout != NULL) {
                activate_best_available_input_source_group (self, NULL, selected_layout);
        }
}

static void
kiosk_input_source_manager_start_x_keyboard_manager (KioskInputSourcesManager *self)
{
        g_debug ("KioskInputSourcesManager: Starting X Keyboard Manager");
        self->x_keyboard_manager = kiosk_x_keyboard_manager_new (self->compositor);

        g_signal_connect_object (G_OBJECT (self->x_keyboard_manager),
                                 "notify::selected-layout",
                                 G_CALLBACK (on_x_keyboard_manager_selected_layout_changed),
                                 self,
                                 G_CONNECT_SWAPPED);
        g_signal_connect_object (G_OBJECT (self->x_keyboard_manager),
                                 "notify::layouts",
                                 G_CALLBACK (on_x_keyboard_manager_layouts_changed),
                                 self,
                                 G_CONNECT_SWAPPED);
}
#endif /* HAVE_X11 */

static void
kiosk_input_sources_manager_handle_dbus_service (KioskInputSourcesManager *self)
{
        KioskService *service;

        service = kiosk_compositor_get_service (self->compositor);

        g_set_weak_pointer (&self->dbus_service, KIOSK_DBUS_INPUT_SOURCES_MANAGER (kiosk_service_get_input_sources_manager_skeleton (service)));
        g_set_weak_pointer (&self->dbus_object_manager, kiosk_service_get_input_sources_object_manager (service));

        g_signal_connect_object (G_OBJECT (self->dbus_service),
                                 "handle-set-input-sources",
                                 G_CALLBACK (on_dbus_service_handle_set_input_sources),
                                 self,
                                 G_CONNECT_SWAPPED);
        g_signal_connect_object (G_OBJECT (self->dbus_service),
                                 "handle-set-input-sources-from-locales",
                                 G_CALLBACK (on_dbus_service_handle_set_input_sources_from_locales),
                                 self,
                                 G_CONNECT_SWAPPED);
        g_signal_connect_object (G_OBJECT (self->dbus_service),
                                 "handle-set-input-sources-from-session-configuration",
                                 G_CALLBACK (on_dbus_service_handle_set_input_sources_from_session_configuration),
                                 self,
                                 G_CONNECT_SWAPPED);
        g_signal_connect_object (G_OBJECT (self->dbus_service),
                                 "handle-select-input-source",
                                 G_CALLBACK (on_dbus_service_handle_select_input_source),
                                 self,
                                 G_CONNECT_SWAPPED);
        g_signal_connect_object (G_OBJECT (self->dbus_service),
                                 "handle-select-next-input-source",
                                 G_CALLBACK (on_dbus_service_handle_select_next_input_source),
                                 self,
                                 G_CONNECT_SWAPPED);
        g_signal_connect_object (G_OBJECT (self->dbus_service),
                                 "handle-select-previous-input-source",
                                 G_CALLBACK (on_dbus_service_handle_select_previous_input_source),
                                 self,
                                 G_CONNECT_SWAPPED);
}

static void
kiosk_input_sources_manager_constructed (GObject *object)
{
        KioskInputSourcesManager *self = KIOSK_INPUT_SOURCES_MANAGER (object);

        g_debug ("KioskInputSourcesManager: Initializing");

        G_OBJECT_CLASS (kiosk_input_sources_manager_parent_class)->constructed (object);

        g_set_weak_pointer (&self->display, meta_plugin_get_display (META_PLUGIN (self->compositor)));

        self->cancellable = g_cancellable_new ();

        self->xkb_info = gnome_xkb_info_new ();
        self->input_source_groups = g_ptr_array_new_full (1, g_object_unref);

        kiosk_input_sources_manager_handle_dbus_service (self);

        kiosk_input_sources_manager_start_input_engine_manager (self);

        kiosk_input_sources_manager_connect_to_localed (self);

        kiosk_input_sources_manager_add_key_bindings (self);

        kiosk_input_sources_manager_set_input_sources_from_session_configuration (self);

        /* We start the X keyboard manager after we've already loaded and locked in
         * GSettings etc, so the session settings take precedence over xorg.conf
         */
#ifdef HAVE_X11
        if (!meta_is_wayland_compositor ()) {
                g_debug ("KioskInputSourcesManager: Will start X keyboard manager shortly");
                kiosk_gobject_utils_queue_defer_callback (G_OBJECT (self),
                                                          "[kiosk-input-sources-manager] kiosk_input_source_manager_start_x_keyboard_manager",
                                                          self->cancellable,
                                                          KIOSK_OBJECT_CALLBACK (kiosk_input_source_manager_start_x_keyboard_manager),
                                                          NULL);
        } else {
                g_debug ("KioskInputSourcesManager: Won't start X keyboard manager on wayland");
        }
#endif
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

        kiosk_input_sources_manager_clear_input_sources (self);

        g_clear_object (&self->input_engine_manager);
        g_clear_object (&self->x_keyboard_manager);

        g_clear_object (&self->xkb_info);

        g_clear_object (&self->locale_proxy);

        kiosk_input_sources_manager_remove_key_bindings (self);
        g_clear_weak_pointer (&self->dbus_service);
        g_clear_weak_pointer (&self->dbus_object_manager);
        g_clear_weak_pointer (&self->display);
        g_clear_weak_pointer (&self->compositor);

        G_OBJECT_CLASS (kiosk_input_sources_manager_parent_class)->dispose (object);
}
