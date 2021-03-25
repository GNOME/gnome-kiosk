#include "config.h"
#include "kiosk-input-sources-manager.h"

#include <stdlib.h>
#include <string.h>

#include <meta/display.h>
#include <meta/util.h>

#include <meta/meta-backend.h>
#include <meta/meta-plugin.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-languages.h>
#include <libgnome-desktop/gnome-xkb-info.h>

#include "org.freedesktop.locale1.h"
#include "kiosk-compositor.h"
#include "kiosk-gobject-utils.h"
#include "kiosk-input-source-group.h"

#define SD_LOCALE1_BUS_NAME "org.freedesktop.locale1"
#define SD_LOCALE1_OBJECT_PATH "/org/freedesktop/locale1"

#define KIOSK_INPUT_SOURCE_GROUP_SCHEMA "org.gnome.desktop.input-sources"
#define KIOSK_INPUT_SOURCE_GROUP_SETTING "sources"
#define KIOSK_INPUT_OPTIONS_SETTING "xkb-options"

struct _KioskInputSourcesManager
{
        GObject parent;

        /* weak references */
        KioskCompositor *compositor;

        /* strong references */
        GCancellable *cancellable;
        SdLocale1 *locale_proxy;
        GnomeXkbInfo *xkb_info;
        GSettings *input_sources_settings;

        GPtrArray *input_source_groups;
        ssize_t input_source_groups_index;

        /* flags */
        guint32 overriding_configuration : 1;
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
        gboolean layout_selected, input_source_group_active;

        selected_layout = kiosk_input_source_group_get_selected_layout (input_source_group);

        if (g_strcmp0 (selected_layout, name) == 0) {
                layout_selected = TRUE;
        } else {
                layout_selected = kiosk_input_source_group_switch_to_layout (input_source_group, name);
        }

        if (layout_selected) {
                input_source_group_active = kiosk_input_source_group_activate (input_source_group);
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
                                            const char               *selected_layout)
{
        gboolean input_source_group_active = FALSE;

        if (selected_layout != NULL) {
                input_source_group_active = activate_input_source_group_with_layout (self, selected_layout);
        }

        if (!input_source_group_active) {
                input_source_group_active = activate_first_available_input_source_group (self);
        }

        return input_source_group_active;
}

static gboolean
kiosk_input_sources_manager_set_input_sources (KioskInputSourcesManager *self,
                                               GVariant                 *input_sources,
                                               const char * const       *options)
{
        KioskInputSourceGroup *old_input_source_group;
        g_autofree char *old_selected_layout = NULL;
        g_autoptr (GVariantIter) iter = NULL;
        g_autofree char *options_string = NULL;
        const char *backend_type = NULL, *backend_id = NULL;
        gboolean input_source_group_active;

        old_input_source_group = kiosk_input_sources_manager_get_selected_input_source_group (self);

        if (old_input_source_group != NULL) {
                old_selected_layout = kiosk_input_source_group_get_selected_layout (old_input_source_group);
        }

        kiosk_input_sources_manager_clear_input_sources (self);

        options_string = g_strjoinv (",", (GStrv) options);

        g_variant_get (input_sources, "a(ss)", &iter);
        while (g_variant_iter_loop (iter, "(ss)", &backend_type, &backend_id)) {
                if (g_strcmp0 (backend_type, "xkb") == 0) {
                        g_debug ("KioskInputSourcesManager:         %s", backend_id);
                        kiosk_input_sources_manager_add_layout (self, backend_id, options_string);
                } else {
                        g_debug ("KioskInputSourcesManager: Unknown input source type '%s' for source '%s'", backend_type, backend_id);
                }
        }

        input_source_group_active = activate_best_available_input_source_group (self, old_selected_layout);

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

        input_source_group = kiosk_input_source_group_new (self);
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

gboolean
kiosk_input_sources_manager_set_input_sources_from_system_configuration (KioskInputSourcesManager *self)
{
        KioskInputSourceGroup *old_input_source_group;
        g_autofree char *old_selected_layout = NULL;

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

        g_debug ("KioskInputSourcesManager: Setting keymap from system configuration");

        layouts_string = sd_locale1_get_x11_layout (self->locale_proxy);
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

        old_input_source_group = kiosk_input_sources_manager_get_selected_input_source_group (self);

        if (old_input_source_group != NULL) {
                old_selected_layout = kiosk_input_source_group_get_selected_layout (old_input_source_group);
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

        input_source_group_active = activate_best_available_input_source_group (self, old_selected_layout);

        if (!input_source_group_active) {
                const char * const *locales;

                locales = sd_locale1_get_locale (self->locale_proxy);
                input_source_group_active = kiosk_input_sources_manager_set_input_sources_from_locales (self, locales, options);
        }

        self->overriding_configuration = FALSE;

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

        if (self->overriding_configuration) {
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
        g_autoptr (GVariant) input_source_group = NULL;
        g_auto (GStrv) options = NULL;
        gboolean input_source_group_active;

        g_return_val_if_fail (KIOSK_IS_INPUT_SOURCES_MANAGER (self), FALSE);

        g_debug ("KioskInputSourcesManager: Setting input sources from session configuration");

        self->overriding_configuration = FALSE;

        if (self->input_sources_settings == NULL) {
                self->input_sources_settings = g_settings_new (KIOSK_INPUT_SOURCE_GROUP_SCHEMA);

                g_signal_connect_object (G_OBJECT (self->input_sources_settings),
                                         "changed::" KIOSK_INPUT_SOURCE_GROUP_SETTING,
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

        input_source_group = g_settings_get_value (self->input_sources_settings,
                                              KIOSK_INPUT_SOURCE_GROUP_SETTING);

        input_source_group_active = kiosk_input_sources_manager_set_input_sources (self, input_source_group, (const char * const *) options);

        if (!input_source_group_active) {
                g_debug ("KioskInputSourcesManager: Session has no valid configured input sources");
                return kiosk_input_sources_manager_set_input_sources_from_system_configuration (self);
        }

        return TRUE;
}

gboolean
kiosk_input_sources_manager_set_input_sources_from_locales (KioskInputSourcesManager *self,
                                                            const char * const       *locales,
                                                            const char               *options)
{
        g_autofree char *locales_string = NULL;
        gboolean input_source_group_active;

        g_return_val_if_fail (KIOSK_IS_INPUT_SOURCES_MANAGER (self), FALSE);
        g_return_val_if_fail (locales != NULL, FALSE);

        locales_string = g_strjoinv (",", (GStrv) locales);

        g_debug ("KioskInputSourcesManager: Setting keymap from locales '%s'",
                 locales_string);

        self->overriding_configuration = TRUE;

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
                        g_debug ("KioskInputSourcesManager: Found input source '%s' for locale '%s'",
                                 backend_id, locale);

                        kiosk_input_sources_manager_add_layout (self, backend_id, options);
                } else {
                        g_debug ("KioskInputSourcesManager: Unknown input source type '%s' for source '%s'", backend_type, backend_id);
                }
        }

        input_source_group_active = activate_first_available_input_source_group (self);

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

        if (self->overriding_configuration) {
                g_debug ("KioskInputSourcesManager: Ignoring change, because keymap is overriden");
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
kiosk_input_sources_manager_constructed (GObject *object)
{
        KioskInputSourcesManager *self = KIOSK_INPUT_SOURCES_MANAGER (object);

        g_debug ("KioskInputSourcesManager: Initializing");

        self->cancellable = g_cancellable_new ();

        self->xkb_info = gnome_xkb_info_new ();
        self->input_source_groups = g_ptr_array_new_full (1, g_object_unref);

        kiosk_input_sources_manager_connect_to_localed (self);

        kiosk_input_sources_manager_set_input_sources_from_session_configuration (self);
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

        g_clear_object (&self->xkb_info);
        g_clear_object (&self->locale_proxy);

        g_clear_weak_pointer (&self->compositor);

        G_OBJECT_CLASS (kiosk_input_sources_manager_parent_class)->dispose (object);
}
