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
