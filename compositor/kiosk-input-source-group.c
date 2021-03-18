#include "config.h"
#include "kiosk-input-source-group.h"

#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon.h>

#include <meta/meta-backend.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-languages.h>
#include <libgnome-desktop/gnome-xkb-info.h>

#include "kiosk-gobject-utils.h"
#include "kiosk-input-sources-manager.h"

#define KIOSK_INPUT_SOURCE_GROUP_MAX_LAYOUTS 3

struct _KioskInputSourceGroup
{
        GObject parent;

        /* weak references */
        KioskInputSourcesManager *input_sources_manager;

        /* strong references */
        GPtrArray *layouts;
        GPtrArray *variants;
        char *options;

        /* state */
        xkb_layout_index_t layout_index;
};

enum
{
  PROP_INPUT_SOURCES_MANAGER = 1,
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
kiosk_input_source_group_new (KioskInputSourcesManager *input_sources_manager)
{
        GObject *object;

        object = g_object_new (KIOSK_TYPE_INPUT_SOURCE_GROUP,
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
        if (number_of_layouts >= KIOSK_INPUT_SOURCE_GROUP_MAX_LAYOUTS)
                return FALSE;

        add_layout (self, layout, variant);

        return TRUE;
}

void
kiosk_input_source_group_set_options (KioskInputSourceGroup *self,
                                      const char            *options)
{
        g_free (self->options);
        self->options = g_strdup (options);
}

gboolean
kiosk_input_source_group_activate (KioskInputSourceGroup *self)
{
        size_t number_of_layouts;
        g_autofree char *layouts = NULL;
        g_autofree char *variants = NULL;

        number_of_layouts = kiosk_input_source_group_get_number_of_layouts (self);

        if (number_of_layouts == 0) {
                return FALSE;
        }

        layouts = g_strjoinv (",", (GStrv) self->layouts->pdata);
        variants = g_strjoinv (",", (GStrv) self->variants->pdata);

        g_debug ("KioskInputSourceGroup: Setting keyboard mapping to [%s] (%s) [%s]",
                 layouts, variants, self->options);

        meta_backend_set_keymap (meta_get_backend (), layouts, variants, self->options);
        meta_backend_lock_layout_group (meta_get_backend (), self->layout_index);

        return TRUE;
}

static ssize_t
get_index_of_layout (KioskInputSourceGroup *self,
                     const char            *layout_name)
{
        g_auto (GStrv) layouts;
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

        meta_backend_lock_layout_group (meta_get_backend (), self->layout_index);

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
        meta_backend_lock_layout_group (meta_get_backend (), self->layout_index);
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
        meta_backend_lock_layout_group (meta_get_backend (), self->layout_index);
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

        meta_backend_lock_layout_group (meta_get_backend (), self->layout_index);

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

        meta_backend_lock_layout_group (meta_get_backend (), self->layout_index);

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
        G_OBJECT_CLASS (kiosk_input_source_group_parent_class)->constructed (object);
}

static void
kiosk_input_source_group_dispose (GObject *object)
{
        KioskInputSourceGroup *self = KIOSK_INPUT_SOURCE_GROUP (object);

        g_debug ("KioskInputSourceGroup: Disposing");

        g_clear_pointer (&self->options, g_free);

        g_clear_pointer (&self->variants, g_ptr_array_unref);
        g_clear_pointer (&self->layouts, g_ptr_array_unref);

        g_clear_weak_pointer (&self->input_sources_manager);

        G_OBJECT_CLASS (kiosk_input_source_group_parent_class)->dispose (object);
}
