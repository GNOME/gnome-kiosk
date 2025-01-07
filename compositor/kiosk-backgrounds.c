#include "config.h"
#include "kiosk-backgrounds.h"

#include <stdlib.h>
#include <string.h>

#include <clutter/clutter.h>
#include <cogl/cogl-color.h>

#include <meta/display.h>
#include <meta/util.h>

#include <meta/meta-context.h>
#include <meta/meta-backend.h>
#include <meta/meta-plugin.h>
#include <meta/meta-monitor-manager.h>
#include <meta/meta-background-actor.h>
#include <meta/meta-background-content.h>
#include <meta/meta-background-group.h>
#include <meta/meta-background-image.h>
#include <meta/meta-background.h>

#include "kiosk-compositor.h"
#include "kiosk-gobject-utils.h"

#define KIOSK_BACKGROUNDS_SCHEMA "org.gnome.desktop.background"
#define KIOSK_BACKGROUNDS_PICTURE_OPTIONS_SETTING "picture-options"
#define KIOSK_BACKGROUNDS_PICTURE_URI_SETTING "picture-uri"
#define KIOSK_BACKGROUNDS_COLOR_SHADING_TYPE_SETTING "color-shading-type"
#define KIOSK_BACKGROUNDS_PRIMARY_COLOR_SETTING "primary-color"
#define KIOSK_BACKGROUNDS_SECONDARY_COLOR_SETTING "secondary-color"

struct _KioskBackgrounds
{
        MetaBackgroundGroup       parent;

        /* weak references */
        KioskCompositor          *compositor;
        MetaDisplay              *display;
        ClutterActor             *window_group;
        MetaContext              *context;
        MetaBackend              *backend;
        MetaMonitorManager       *monitor_manager;
        ClutterActor             *stage;

        MetaBackgroundImageCache *image_cache;

        /* strong references */
        GCancellable             *cancellable;
        GSettings                *settings;
        ClutterActor             *background_group;
};

enum
{
        PROP_COMPOSITOR = 1,
        NUMBER_OF_PROPERTIES
};
static GParamSpec *kiosk_backgrounds_properties[NUMBER_OF_PROPERTIES] = { NULL, };

G_DEFINE_TYPE (KioskBackgrounds, kiosk_backgrounds, G_TYPE_OBJECT)

static void kiosk_backgrounds_set_property (GObject      *object,
                                            guint         property_id,
                                            const GValue *value,
                                            GParamSpec   *param_spec);
static void kiosk_backgrounds_get_property (GObject    *object,
                                            guint       property_id,
                                            GValue     *value,
                                            GParamSpec *param_spec);

static void kiosk_backgrounds_constructed (GObject *object);
static void kiosk_backgrounds_dispose (GObject *object);

static void
kiosk_backgrounds_class_init (KioskBackgroundsClass *backgrounds_class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (backgrounds_class);

        object_class->constructed = kiosk_backgrounds_constructed;
        object_class->set_property = kiosk_backgrounds_set_property;
        object_class->get_property = kiosk_backgrounds_get_property;
        object_class->dispose = kiosk_backgrounds_dispose;

        kiosk_backgrounds_properties[PROP_COMPOSITOR] = g_param_spec_object ("compositor",
                                                                             "compositor",
                                                                             "compositor",
                                                                             KIOSK_TYPE_COMPOSITOR,
                                                                             G_PARAM_CONSTRUCT_ONLY
                                                                             | G_PARAM_WRITABLE
                                                                             | G_PARAM_STATIC_NAME
                                                                             | G_PARAM_STATIC_NICK
                                                                             | G_PARAM_STATIC_BLURB);
        g_object_class_install_properties (object_class, NUMBER_OF_PROPERTIES, kiosk_backgrounds_properties);
}

static void
set_background_color_from_settings (KioskBackgrounds *self,
                                    MetaBackground   *background)
{
        GDesktopBackgroundShading color_shading_type;
        g_autofree char *primary_color_as_string = NULL;
        g_autofree char *secondary_color_as_string = NULL;
        CoglColor primary_color = { 0 };
        CoglColor secondary_color = { 0 };

        color_shading_type = g_settings_get_enum (self->settings, KIOSK_BACKGROUNDS_COLOR_SHADING_TYPE_SETTING);
        primary_color_as_string = g_settings_get_string (self->settings, KIOSK_BACKGROUNDS_PRIMARY_COLOR_SETTING);
        cogl_color_from_string (&primary_color, primary_color_as_string);

        switch (color_shading_type) {
        case G_DESKTOP_BACKGROUND_SHADING_SOLID:
                meta_background_set_color (background, &primary_color);
                break;

        case G_DESKTOP_BACKGROUND_SHADING_VERTICAL:
        case G_DESKTOP_BACKGROUND_SHADING_HORIZONTAL:
                secondary_color_as_string = g_settings_get_string (self->settings, KIOSK_BACKGROUNDS_SECONDARY_COLOR_SETTING);
                cogl_color_from_string (&secondary_color, secondary_color_as_string);
                meta_background_set_gradient (background, color_shading_type, &primary_color, &secondary_color);
                break;
        }
}

static void
on_background_image_loaded (KioskBackgrounds    *self,
                            MetaBackgroundImage *background_image)
{
        MetaBackground *background;
        GDesktopBackgroundStyle background_style;
        GFile *picture_file = NULL;

        g_signal_handlers_disconnect_by_func (G_OBJECT (background_image),
                                              on_background_image_loaded,
                                              self);

        background = g_object_get_data (G_OBJECT (background_image), "kiosk-background");

        picture_file = g_object_get_data (G_OBJECT (background), "picture-file");
        background_style = g_settings_get_enum (self->settings, KIOSK_BACKGROUNDS_PICTURE_OPTIONS_SETTING);

        meta_background_set_file (background, picture_file, background_style);
        set_background_color_from_settings (self, background);

        g_object_set_data (G_OBJECT (background), "picture-file", NULL);

        background = NULL;
        g_object_set_data (G_OBJECT (background_image), "kiosk-background", NULL);

        g_object_unref (background_image);
}

static void
set_background_file_from_settings (KioskBackgrounds *self,
                                   MetaBackground   *background)
{
        g_autofree char *uri = NULL;
        g_autoptr (GFile) picture_file = NULL;
        MetaBackgroundImage *background_image;

        uri = g_settings_get_string (self->settings, KIOSK_BACKGROUNDS_PICTURE_URI_SETTING);
        picture_file = g_file_new_for_commandline_arg (uri);

        /* We explicitly prime the file in the cache so we can defer setting the background color
         * until the image is fully loaded and avoid flicker at startup.
         */
        background_image = meta_background_image_cache_load (self->image_cache, picture_file);
        g_object_set_data_full (G_OBJECT (background_image),
                                "kiosk-background",
                                g_object_ref (background),
                                g_object_unref);
        g_object_set_data_full (G_OBJECT (background),
                                "picture-file",
                                g_steal_pointer (&picture_file),
                                g_object_unref);

        if (meta_background_image_is_loaded (background_image)) {
                kiosk_gobject_utils_queue_immediate_callback (G_OBJECT (self),
                                                              "[kiosk-backgrounds] on_background_image_loaded",
                                                              self->cancellable,
                                                              KIOSK_OBJECT_CALLBACK (on_background_image_loaded),
                                                              background_image);
        } else {
                g_signal_connect_object (G_OBJECT (background_image),
                                         "loaded",
                                         G_CALLBACK (on_background_image_loaded),
                                         self,
                                         G_CONNECT_SWAPPED);
        }
}

static void
create_background_for_monitor (KioskBackgrounds *self,
                               int               monitor_index)
{
        g_autoptr (MetaBackground) background = NULL;
        GDesktopBackgroundStyle background_style;
        MtkRectangle geometry;
        ClutterActor *background_actor = NULL;
        MetaBackgroundContent *background_content;

        g_debug ("KioskBackgrounds: Creating background for monitor %d", monitor_index);

        background = meta_background_new (self->display);
        background_style = g_settings_get_enum (self->settings, KIOSK_BACKGROUNDS_PICTURE_OPTIONS_SETTING);

        if (background_style == G_DESKTOP_BACKGROUND_STYLE_NONE) {
                set_background_color_from_settings (self, background);
        } else {
                set_background_file_from_settings (self, background);
        }

        background_actor = meta_background_actor_new (self->display, monitor_index);

        meta_display_get_monitor_geometry (self->display, monitor_index, &geometry);

        clutter_actor_set_position (background_actor, geometry.x, geometry.y);
        clutter_actor_set_size (background_actor, geometry.width, geometry.height);

        background_content = META_BACKGROUND_CONTENT (clutter_actor_get_content (background_actor));
        meta_background_content_set_background (background_content, background);

        clutter_actor_add_child (self->background_group, background_actor);
        clutter_actor_show (background_actor);
}

static void
reinitialize_backgrounds (KioskBackgrounds *self)
{
        int i, number_of_monitors;

        g_debug ("KioskBackgrounds: Recreating backgrounds");

        clutter_actor_destroy_all_children (self->background_group);

        number_of_monitors = meta_display_get_n_monitors (self->display);
        for (i = 0; i < number_of_monitors; i++) {
                create_background_for_monitor (self, i);
        }

        g_debug ("KioskBackgrounds: Finished recreating backgrounds");
}

static void
kiosk_backgrounds_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *param_spec)
{
        KioskBackgrounds *self = KIOSK_BACKGROUNDS (object);

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
kiosk_backgrounds_get_property (GObject    *object,
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
kiosk_backgrounds_dispose (GObject *object)
{
        KioskBackgrounds *self = KIOSK_BACKGROUNDS (object);
        if (self->cancellable != NULL) {
                g_cancellable_cancel (self->cancellable);
                g_clear_object (&self->cancellable);
        }

        g_clear_object (&self->background_group);
        g_clear_object (&self->settings);

        g_clear_weak_pointer (&self->context);
        g_clear_weak_pointer (&self->backend);
        g_clear_weak_pointer (&self->stage);
        g_clear_weak_pointer (&self->display);
        g_clear_weak_pointer (&self->window_group);
        g_clear_weak_pointer (&self->monitor_manager);
        g_clear_weak_pointer (&self->compositor);
        g_clear_weak_pointer (&self->image_cache);

        G_OBJECT_CLASS (kiosk_backgrounds_parent_class)->dispose (object);
}

static void
on_settings_changed (KioskBackgrounds *self)
{
        kiosk_gobject_utils_queue_defer_callback (G_OBJECT (self),
                                                  "[kiosk-backgrounds] on_backgrounds_settings_changed",
                                                  self->cancellable,
                                                  KIOSK_OBJECT_CALLBACK (reinitialize_backgrounds),
                                                  NULL);
}

static void
kiosk_backgrounds_constructed (GObject *object)
{
        KioskBackgrounds *self = KIOSK_BACKGROUNDS (object);
        MetaDisplay *display = meta_plugin_get_display (META_PLUGIN (self->compositor));
        MetaCompositor *compositor = meta_display_get_compositor (display);

        G_OBJECT_CLASS (kiosk_backgrounds_parent_class)->constructed (object);

        g_set_weak_pointer (&self->display, display);
        g_set_weak_pointer (&self->context, meta_display_get_context (self->display));
        g_set_weak_pointer (&self->backend, meta_context_get_backend (self->context));
        g_set_weak_pointer (&self->stage, CLUTTER_ACTOR (meta_compositor_get_stage (compositor)));
        g_set_weak_pointer (&self->window_group, meta_compositor_get_window_group (compositor));
        g_set_weak_pointer (&self->monitor_manager, meta_backend_get_monitor_manager (self->backend));
        g_set_weak_pointer (&self->image_cache, meta_background_image_cache_get_default ());

        self->cancellable = g_cancellable_new ();

        self->background_group = meta_background_group_new ();
        clutter_actor_insert_child_below (self->window_group, self->background_group, NULL);

        g_signal_connect_object (G_OBJECT (self->monitor_manager),
                                 "monitors-changed",
                                 G_CALLBACK (reinitialize_backgrounds),
                                 self,
                                 G_CONNECT_SWAPPED);

        self->settings = g_settings_new (KIOSK_BACKGROUNDS_SCHEMA);

        g_signal_connect_object (G_OBJECT (self->settings),
                                 "changed::" KIOSK_BACKGROUNDS_PICTURE_OPTIONS_SETTING,
                                 G_CALLBACK (on_settings_changed),
                                 self,
                                 G_CONNECT_SWAPPED);
        g_signal_connect_object (G_OBJECT (self->settings),
                                 "changed::" KIOSK_BACKGROUNDS_PICTURE_URI_SETTING,
                                 G_CALLBACK (on_settings_changed),
                                 self,
                                 G_CONNECT_SWAPPED);
        g_signal_connect_object (G_OBJECT (self->settings),
                                 "changed::" KIOSK_BACKGROUNDS_COLOR_SHADING_TYPE_SETTING,
                                 G_CALLBACK (on_settings_changed),
                                 self,
                                 G_CONNECT_SWAPPED);
        g_signal_connect_object (G_OBJECT (self->settings),
                                 "changed::" KIOSK_BACKGROUNDS_PRIMARY_COLOR_SETTING,
                                 G_CALLBACK (on_settings_changed),
                                 self,
                                 G_CONNECT_SWAPPED);

        g_signal_connect_object (G_OBJECT (self->settings),
                                 "changed::" KIOSK_BACKGROUNDS_SECONDARY_COLOR_SETTING,
                                 G_CALLBACK (on_settings_changed),
                                 self,
                                 G_CONNECT_SWAPPED);

        reinitialize_backgrounds (self);
}

static void
kiosk_backgrounds_init (KioskBackgrounds *self)
{
        g_debug ("KioskBackgrounds: Initializing");
}

KioskBackgrounds *
kiosk_backgrounds_new (KioskCompositor *compositor)
{
        GObject *object;

        object = g_object_new (KIOSK_TYPE_BACKGROUNDS,
                               "compositor", compositor,
                               NULL);

        return KIOSK_BACKGROUNDS (object);
}
