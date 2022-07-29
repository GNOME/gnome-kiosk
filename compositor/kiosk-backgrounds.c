#include "config.h"
#include "kiosk-backgrounds.h"

#include <stdlib.h>
#include <string.h>

#include <clutter/clutter.h>

#include <meta/display.h>
#include <meta/util.h>

#include <meta/meta-plugin.h>
#include <meta/meta-monitor-manager.h>
#include <meta/meta-background-actor.h>
#include <meta/meta-background-content.h>
#include <meta/meta-background-group.h>
#include <meta/meta-background-image.h>
#include <meta/meta-background.h>

#include "kiosk-compositor.h"

struct _KioskBackgrounds
{
        MetaBackgroundGroup parent;

        /* weak references */
        KioskCompositor    *compositor;
        MetaDisplay        *display;
        ClutterActor       *window_group;
        MetaMonitorManager *monitor_manager;
        ClutterActor       *stage;

        /* strong references */
        ClutterActor       *background_group;
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
create_background_for_monitor (KioskBackgrounds *self,
                               int               monitor_index)
{
        MetaRectangle geometry;
        ClutterActor *background_actor = NULL;
        MetaBackgroundContent *background_content;
        g_autoptr (MetaBackground) background = NULL;
        ClutterColor color;

        g_debug ("KioskBackgrounds: Creating background for monitor %d", monitor_index);

        meta_display_get_monitor_geometry (self->display, monitor_index, &geometry);

        background_actor = meta_background_actor_new (self->display, monitor_index);

        clutter_actor_set_position (background_actor, geometry.x, geometry.y);
        clutter_actor_set_size (background_actor, geometry.width, geometry.height);

        clutter_color_init (&color, 50, 50, 50, 255);

        background = meta_background_new (self->display);
        meta_background_set_color (background, &color);

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

        g_clear_weak_pointer (&self->stage);
        g_clear_weak_pointer (&self->display);
        g_clear_weak_pointer (&self->window_group);
        g_clear_weak_pointer (&self->monitor_manager);
        g_clear_weak_pointer (&self->compositor);

        g_clear_object (&self->background_group);

        G_OBJECT_CLASS (kiosk_backgrounds_parent_class)->dispose (object);
}

static void
kiosk_backgrounds_constructed (GObject *object)
{
        KioskBackgrounds *self = KIOSK_BACKGROUNDS (object);

        G_OBJECT_CLASS (kiosk_backgrounds_parent_class)->constructed (object);

        g_set_weak_pointer (&self->display, meta_plugin_get_display (META_PLUGIN (self->compositor)));
        g_set_weak_pointer (&self->stage, meta_get_stage_for_display (self->display));
        g_set_weak_pointer (&self->window_group, meta_get_window_group_for_display (self->display));
        g_set_weak_pointer (&self->monitor_manager, meta_monitor_manager_get ());

        self->background_group = meta_background_group_new ();
        clutter_actor_insert_child_below (self->window_group, self->background_group, NULL);

        g_signal_connect_object (G_OBJECT (self->monitor_manager),
                                 "monitors-changed",
                                 G_CALLBACK (reinitialize_backgrounds),
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
