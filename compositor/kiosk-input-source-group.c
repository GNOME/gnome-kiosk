#include "config.h"
#include "kiosk-input-source-group.h"

#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon.h>

#include <meta/meta-context.h>
#include <meta/meta-backend.h>

#include <meta/display.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-languages.h>
#include <libgnome-desktop/gnome-xkb-info.h>

#include "kiosk-gobject-utils.h"
#include "kiosk-input-sources-manager.h"
#include "kiosk-compositor.h"

#define KIOSK_INPUT_SOURCE_GROUP_MAX_LAYOUTS 3
#define KIOSK_INPUT_SOURCE_KEYBOARD_MODEL "pc105+inet"

struct _KioskInputSourceGroup
{
        GObject                   parent;

        /* weak references */
        KioskCompositor          *compositor;
        KioskInputSourcesManager *input_sources_manager;
        KioskInputEngineManager  *input_engine_manager;
#ifdef HAVE_X11
        KioskXKeyboardManager    *x_keyboard_manager;
#endif
        MetaDisplay              *display;
        MetaContext              *context;
        MetaBackend              *backend;

        /* strong references */
        char                     *input_engine_name;
        GPtrArray                *layouts;
        GPtrArray                *variants;
        char                     *options;

        /* state */
        xkb_layout_index_t        layout_index;
};
enum
{
        PROP_COMPOSITOR = 1,
        PROP_INPUT_SOURCES_MANAGER,
        NUMBER_OF_PROPERTIES
};

static GParamSpec *kiosk_input_source_group_properties[NUMBER_OF_PROPERTIES] = { NULL, };

G_DEFINE_TYPE (KioskInputSourceGroup, kiosk_input_source_group, G_TYPE_OBJECT)

static void kiosk_input_source_group_set_property (GObject      *object,
                                                   guint         property_id,
                                                   const GValue *value,
                                                   GParamSpec   *param_spec);
static void kiosk_input_source_group_get_property (GObject    *object,
                                                   guint       property_id,
                                                   GValue     *value,
                                                   GParamSpec *param_spec);

static void kiosk_input_source_group_constructed (GObject *object);
static void kiosk_input_source_group_dispose (GObject *object);

KioskInputSourceGroup *
kiosk_input_source_group_new (KioskCompositor          *compositor,
                              KioskInputSourcesManager *input_sources_manager)
{
        GObject *object;

        object = g_object_new (KIOSK_TYPE_INPUT_SOURCE_GROUP,
                               "compositor", compositor,
                               "input-sources-manager", input_sources_manager,
                               NULL);

        return KIOSK_INPUT_SOURCE_GROUP (object);
}

static size_t
kiosk_input_source_group_get_number_of_layouts (KioskInputSourceGroup *self)
{
        return self->layouts->len - 1;
}

char *
kiosk_input_source_group_get_selected_layout (KioskInputSourceGroup *self)
{
        size_t number_of_layouts;
        const char *layout, *variant;

        number_of_layouts = kiosk_input_source_group_get_number_of_layouts (self);

        if (number_of_layouts == 0) {
                return NULL;
        }

        layout = g_ptr_array_index (self->layouts, self->layout_index);
        variant = g_ptr_array_index (self->variants, self->layout_index);

        return g_strdup_printf ("%s%s%s",
                                layout,
                                variant != NULL && variant[0] != '\0'? "+" : "",
                                variant != NULL && variant[0] != '\0'? variant : "");
}

char **
kiosk_input_source_group_get_layouts (KioskInputSourceGroup *self)
{
        g_autoptr (GPtrArray) array = NULL;
        size_t i, number_of_layouts;

        number_of_layouts = kiosk_input_source_group_get_number_of_layouts (self);

        array = g_ptr_array_sized_new (number_of_layouts);

        if (number_of_layouts == 0) {
                goto out;
        }

        for (i = 0; i < number_of_layouts; i++) {
                const char *layout, *variant;

                layout = g_ptr_array_index (self->layouts, i);
                variant = g_ptr_array_index (self->variants, i);

                g_ptr_array_add (array, g_strdup_printf ("%s%s%s", layout,
                                                         variant != NULL && variant[0] != '\0'? "+" : "",
                                                         variant != NULL && variant[0] != '\0'? variant : ""));
        }

out:
        g_ptr_array_add (array, NULL);

        return (char **) g_ptr_array_steal (array, NULL);
}

static void
add_layout (KioskInputSourceGroup *self,
            const char            *layout,
            const char            *variant)
{
        size_t number_of_layouts;

        g_debug ("KioskInputSourceGroup: Adding layout '%s%s%s' to mapping",
                 layout,
                 variant != NULL && variant[0] != '\0'? "+" : "",
                 variant != NULL && variant[0] != '\0'? variant : "");

        number_of_layouts = kiosk_input_source_group_get_number_of_layouts (self);

        /* Drop terminating NULL */
        g_ptr_array_remove_index (self->layouts, number_of_layouts);
        g_ptr_array_remove_index (self->variants, number_of_layouts);

        g_ptr_array_add (self->layouts, g_strdup (layout));
        g_ptr_array_add (self->variants, g_strdup (variant));

        /* Add back terminating NULL */
        g_ptr_array_add (self->layouts, NULL);
        g_ptr_array_add (self->variants, NULL);
}

gboolean
kiosk_input_source_group_add_layout (KioskInputSourceGroup *self,
                                     const char            *layout,
                                     const char            *variant)
{
        size_t number_of_layouts;

        number_of_layouts = kiosk_input_source_group_get_number_of_layouts (self);
        if (number_of_layouts >= KIOSK_INPUT_SOURCE_GROUP_MAX_LAYOUTS) {
                return FALSE;
        }

        if (self->input_engine_name != NULL) {
                return FALSE;
        }

        add_layout (self, layout, variant);

        return TRUE;
}

static void
kiosk_input_source_group_ensure_layout_for_input_engine (KioskInputSourceGroup *self)
{
        const char *layout = NULL;
        const char *variant = NULL;
        size_t number_of_layouts;
        gboolean layout_found;

        if (self->input_engine_name == NULL) {
                return;
        }

        number_of_layouts = kiosk_input_source_group_get_number_of_layouts (self);

        if (number_of_layouts == 1) {
                return;
        }

        g_ptr_array_set_size (self->layouts, 0);
        g_ptr_array_set_size (self->variants, 0);

        g_ptr_array_add (self->layouts, NULL);
        g_ptr_array_add (self->variants, NULL);

        layout_found = kiosk_input_engine_manager_find_layout_for_engine (self->input_engine_manager,
                                                                          self->input_engine_name,
                                                                          &layout,
                                                                          &variant);

        if (layout_found) {
                add_layout (self, layout, variant);
        }
}

gboolean
kiosk_input_source_group_set_input_engine (KioskInputSourceGroup *self,
                                           const char            *engine_name)
{
        g_debug ("KioskInputSourceGroup: Setting input engine to '%s'", engine_name);

        g_free (self->input_engine_name);
        self->input_engine_name = g_strdup (engine_name);

        g_ptr_array_set_size (self->layouts, 0);
        g_ptr_array_set_size (self->variants, 0);

        g_ptr_array_add (self->layouts, NULL);
        g_ptr_array_add (self->variants, NULL);

        return TRUE;
}

const char *
kiosk_input_source_group_get_input_engine (KioskInputSourceGroup *self)
{
        return self->input_engine_name;
}

void
kiosk_input_source_group_set_options (KioskInputSourceGroup *self,
                                      const char            *options)
{
        g_free (self->options);
        self->options = g_strdup (options);
}

const char *
kiosk_input_source_group_get_options (KioskInputSourceGroup *self)
{
        return self->options;
}

static void
set_keymap_cb (GObject      *source_object,
               GAsyncResult *result,
               gpointer      user_data)
{
        MetaBackend *backend = META_BACKEND (source_object);
        g_autoptr (GError) error = NULL;

        meta_backend_set_keymap_finish (backend, result, &error);
        if (error)
                g_warning ("Failed to set keymap: %s", error->message);
}

static void
set_keymap_layout_group_cb (GObject      *source_object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
        MetaBackend *backend = META_BACKEND (source_object);
        g_autoptr (GError) error = NULL;

        meta_backend_set_keymap_layout_group_finish (backend, result, &error);
        if (error)
                g_warning ("Failed to set keymap layout group: %s", error->message);
}

gboolean
kiosk_input_source_group_activate (KioskInputSourceGroup *self)
{
        size_t number_of_layouts;
        g_autofree char *layouts = NULL;
        g_autofree char *variants = NULL;
        gboolean keymap_already_set = FALSE;
        gboolean layout_group_already_locked = FALSE;

        g_debug ("KioskInputSourceGroup: Activating input source");

        if (self->input_engine_name != NULL) {
                kiosk_input_source_group_ensure_layout_for_input_engine (self);
        }

        number_of_layouts = kiosk_input_source_group_get_number_of_layouts (self);

        if (number_of_layouts == 0) {
                return FALSE;
        }

        layouts = g_strjoinv (",", (GStrv) self->layouts->pdata);
        variants = g_strjoinv (",", (GStrv) self->variants->pdata);

        if (self->input_engine_name != NULL) {
                gboolean activated;

                activated = kiosk_input_engine_manager_activate_engine (self->input_engine_manager, self->input_engine_name);

                if (!activated) {
                        g_debug ("KioskInputSourceGroup: Could not activate input engine '%s'", self->input_engine_name);
                        return FALSE;
                }
        } else {
                kiosk_input_engine_manager_activate_engine (self->input_engine_manager, NULL);
        }

#ifdef HAVE_X11
        if (self->x_keyboard_manager != NULL) {
                keymap_already_set = kiosk_x_keyboard_manager_keymap_is_active (self->x_keyboard_manager, (const char * const *) self->layouts->pdata, (const char * const *) self->variants->pdata, self->options);
                layout_group_already_locked = kiosk_x_keyboard_manager_layout_group_is_locked (self->x_keyboard_manager, self->layout_index);
        }
#endif

        if (!keymap_already_set) {
                g_debug ("KioskInputSourceGroup: Setting keyboard mapping to [%s] (%s) [%s]",
                         layouts, variants, self->options);

                meta_backend_set_keymap_async (self->backend,
                                               layouts,
                                               variants,
                                               self->options,
                                               KIOSK_INPUT_SOURCE_KEYBOARD_MODEL,
                                               NULL,
                                               set_keymap_cb,
                                               NULL);
        }

        if (!layout_group_already_locked) {
                g_debug ("KioskInputSourceGroup: Locking layout to index %d", self->layout_index);
                meta_backend_set_keymap_layout_group_async (self->backend,
                                                            self->layout_index,
                                                            NULL,
                                                            set_keymap_layout_group_cb,
                                                            NULL);
        }

        if (keymap_already_set && layout_group_already_locked) {
                g_debug ("KioskInputSourceGroup: Input source already active");
        }

        return TRUE;
}

static ssize_t
get_index_of_layout (KioskInputSourceGroup *self,
                     const char            *layout_name)
{
        g_auto (GStrv) layouts = NULL;
        size_t i;

        layouts = kiosk_input_source_group_get_layouts (self);
        for (i = 0; layouts[i] != NULL; i++) {
                if (g_strcmp0 (layout_name, layouts[i]) == 0) {
                        return (ssize_t) i;
                }
        }

        return -1;
}

gboolean
kiosk_input_source_group_only_has_layouts (KioskInputSourceGroup *self,
                                           const char * const    *layouts_to_check)
{
        g_auto (GStrv) layouts = NULL;

        layouts = kiosk_input_source_group_get_layouts (self);

        return g_strv_equal (layouts_to_check, (const char * const *) layouts);
}

gboolean
kiosk_input_source_group_switch_to_layout (KioskInputSourceGroup *self,
                                           const char            *layout_name)
{
        g_autofree char *active_layout = NULL;
        ssize_t layout_index;

        layout_index = get_index_of_layout (self, layout_name);

        if (layout_index < 0) {
                return FALSE;
        }

        g_debug ("KioskInputSourceGroup: Switching to layout %s", layout_name);

        active_layout = kiosk_input_source_group_get_selected_layout (self);

        self->layout_index = layout_index;

        g_debug ("KioskInputSourceGroup: Switching from layout '%s' to next layout '%s'",
                 active_layout, layout_name);

        meta_backend_set_keymap_layout_group_async (self->backend,
                                                    self->layout_index,
                                                    NULL,
                                                    set_keymap_layout_group_cb,
                                                    NULL);

        return TRUE;
}

void
kiosk_input_source_group_switch_to_first_layout (KioskInputSourceGroup *self)
{
        size_t number_of_layouts;
        g_autofree char *layout_to_activate = NULL;

        g_debug ("KioskInputSourceGroup: Switching mapping to first layout");

        number_of_layouts = kiosk_input_source_group_get_number_of_layouts (self);

        if (number_of_layouts == 0) {
                g_debug ("KioskInputSourceGroup: Mapping has no layouts");
                return;
        }

        self->layout_index = 0;
        layout_to_activate = kiosk_input_source_group_get_selected_layout (self);

        g_debug ("KioskInputSourceGroup: First layout is '%s'", layout_to_activate);
        meta_backend_set_keymap_layout_group_async (self->backend,
                                                    self->layout_index,
                                                    NULL,
                                                    set_keymap_layout_group_cb,
                                                    NULL);
}

void
kiosk_input_source_group_switch_to_last_layout (KioskInputSourceGroup *self)
{
        size_t number_of_layouts;
        g_autofree char *layout_to_activate = NULL;

        g_debug ("KioskInputSourceGroup: Switching mapping to last layout");

        number_of_layouts = kiosk_input_source_group_get_number_of_layouts (self);

        if (number_of_layouts == 0) {
                g_debug ("KioskInputSourceGroup: Mapping has no layouts");
                return;
        }

        self->layout_index = number_of_layouts - 1;
        layout_to_activate = kiosk_input_source_group_get_selected_layout (self);

        g_debug ("KioskInputSourceGroup: Last layout is '%s'", layout_to_activate);
        meta_backend_set_keymap_layout_group_async (self->backend,
                                                    self->layout_index,
                                                    NULL,
                                                    set_keymap_layout_group_cb,
                                                    NULL);
}

gboolean
kiosk_input_source_group_switch_to_next_layout (KioskInputSourceGroup *self)
{
        size_t number_of_layouts, last_layout_index;
        g_autofree char *active_layout = NULL;
        g_autofree char *layout_to_activate = NULL;

        g_debug ("KioskInputSourceGroup: Switching mapping forward one layout");

        number_of_layouts = kiosk_input_source_group_get_number_of_layouts (self);

        if (number_of_layouts == 0) {
                g_debug ("KioskInputSourceGroup: Mapping has no layouts");
                return FALSE;
        }

        last_layout_index = number_of_layouts - 1;

        if (self->layout_index + 1 > last_layout_index) {
                g_debug ("KioskInputSourceGroup: Mapping is at last layout");
                return FALSE;
        }

        active_layout = kiosk_input_source_group_get_selected_layout (self);

        self->layout_index++;

        layout_to_activate = kiosk_input_source_group_get_selected_layout (self);

        g_debug ("KioskInputSourceGroup: Switching from layout '%s' to next layout '%s'",
                 active_layout, layout_to_activate);

        meta_backend_set_keymap_layout_group_async (self->backend,
                                                    self->layout_index,
                                                    NULL,
                                                    set_keymap_layout_group_cb,
                                                    NULL);

        return TRUE;
}

gboolean
kiosk_input_source_group_switch_to_previous_layout (KioskInputSourceGroup *self)
{
        size_t number_of_layouts;
        g_autofree char *active_layout = NULL;
        g_autofree char *layout_to_activate = NULL;

        g_debug ("KioskInputSourceGroup: Switching mapping backward one layout");

        number_of_layouts = kiosk_input_source_group_get_number_of_layouts (self);

        if (number_of_layouts == 0) {
                g_debug ("KioskInputSourceGroup: Mapping has no layouts");
                return FALSE;
        }

        if (self->layout_index == 0) {
                g_debug ("KioskInputSourceGroup: Mapping is at first layout");
                return FALSE;
        }

        active_layout = kiosk_input_source_group_get_selected_layout (self);

        self->layout_index--;

        layout_to_activate = kiosk_input_source_group_get_selected_layout (self);

        g_debug ("KioskInputSourceGroup: Switching from layout '%s' to previous layout '%s'",
                 active_layout, layout_to_activate);

        meta_backend_set_keymap_layout_group_async (self->backend,
                                                    self->layout_index,
                                                    NULL,
                                                    set_keymap_layout_group_cb,
                                                    NULL);

        return TRUE;
}

static void
kiosk_input_source_group_class_init (KioskInputSourceGroupClass *input_sources_class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (input_sources_class);

        object_class->constructed = kiosk_input_source_group_constructed;
        object_class->set_property = kiosk_input_source_group_set_property;
        object_class->get_property = kiosk_input_source_group_get_property;
        object_class->dispose = kiosk_input_source_group_dispose;

        kiosk_input_source_group_properties[PROP_COMPOSITOR] = g_param_spec_object ("compositor",
                                                                                    "compositor",
                                                                                    "compositor",
                                                                                    KIOSK_TYPE_COMPOSITOR,
                                                                                    G_PARAM_CONSTRUCT_ONLY
                                                                                    | G_PARAM_WRITABLE
                                                                                    | G_PARAM_STATIC_NAME
                                                                                    | G_PARAM_STATIC_NICK
                                                                                    | G_PARAM_STATIC_BLURB);
        kiosk_input_source_group_properties[PROP_INPUT_SOURCES_MANAGER] = g_param_spec_object ("input-sources-manager",
                                                                                               "input-sources-manager",
                                                                                               "input-sources-manager",
                                                                                               KIOSK_TYPE_INPUT_SOURCES_MANAGER,
                                                                                               G_PARAM_CONSTRUCT_ONLY
                                                                                               | G_PARAM_WRITABLE
                                                                                               | G_PARAM_STATIC_NAME
                                                                                               | G_PARAM_STATIC_NICK
                                                                                               | G_PARAM_STATIC_BLURB);

        g_object_class_install_properties (object_class, NUMBER_OF_PROPERTIES, kiosk_input_source_group_properties);
}

static void
kiosk_input_source_group_set_property (GObject      *object,
                                       guint         property_id,
                                       const GValue *value,
                                       GParamSpec   *param_spec)
{
        KioskInputSourceGroup *self = KIOSK_INPUT_SOURCE_GROUP (object);

        switch (property_id) {
        case PROP_COMPOSITOR:
                g_set_weak_pointer (&self->compositor, g_value_get_object (value));
                break;
        case PROP_INPUT_SOURCES_MANAGER:
                g_set_weak_pointer (&self->input_sources_manager, g_value_get_object (value));
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, param_spec);
                break;
        }
}

static void
kiosk_input_source_group_get_property (GObject    *object,
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
kiosk_input_source_group_init (KioskInputSourceGroup *self)
{
        g_debug ("KioskInputSourceGroup: Initializing");

        self->layouts = g_ptr_array_new_full (KIOSK_INPUT_SOURCE_GROUP_MAX_LAYOUTS + 1, g_free);
        self->variants = g_ptr_array_new_full (KIOSK_INPUT_SOURCE_GROUP_MAX_LAYOUTS + 1, g_free);

        g_ptr_array_add (self->layouts, NULL);
        g_ptr_array_add (self->variants, NULL);
}

static void
kiosk_input_source_group_constructed (GObject *object)
{
        KioskInputSourceGroup *self = KIOSK_INPUT_SOURCE_GROUP (object);

        G_OBJECT_CLASS (kiosk_input_source_group_parent_class)->constructed (object);

        g_set_weak_pointer (&self->input_engine_manager, kiosk_input_sources_manager_get_input_engine_manager (self->input_sources_manager));
#ifdef HAVE_X11
        g_set_weak_pointer (&self->x_keyboard_manager, kiosk_input_sources_manager_get_x_keyboard_manager (self->input_sources_manager));
#endif
        g_set_weak_pointer (&self->display, meta_plugin_get_display (META_PLUGIN (self->compositor)));
        g_set_weak_pointer (&self->context, meta_display_get_context (self->display));
        g_set_weak_pointer (&self->backend, meta_context_get_backend (self->context));
}

static void
kiosk_input_source_group_dispose (GObject *object)
{
        KioskInputSourceGroup *self = KIOSK_INPUT_SOURCE_GROUP (object);

        g_debug ("KioskInputSourceGroup: Disposing");

        g_clear_pointer (&self->options, g_free);

        g_clear_pointer (&self->variants, g_ptr_array_unref);
        g_clear_pointer (&self->layouts, g_ptr_array_unref);

        g_clear_weak_pointer (&self->backend);
        g_clear_weak_pointer (&self->context);
        g_clear_weak_pointer (&self->display);
#ifdef HAVE_X11
        g_clear_weak_pointer (&self->x_keyboard_manager);
#endif
        g_clear_weak_pointer (&self->input_engine_manager);
        g_clear_weak_pointer (&self->input_sources_manager);

        G_OBJECT_CLASS (kiosk_input_source_group_parent_class)->dispose (object);
}
