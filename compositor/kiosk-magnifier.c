#include "config.h"
#include "kiosk-magnifier.h"

#include <clutter/clutter.h>

#include <meta/display.h>
#include <meta/meta-context.h>
#include <meta/meta-backend.h>
#include <meta/meta-cursor-tracker.h>

#include "kiosk-compositor.h"

#define KIOSK_A11Y_APPLICATIONS_SCHEMA "org.gnome.desktop.a11y.applications"
#define KIOSK_A11Y_MAGNIFIER_ENABLED "screen-magnifier-enabled"

#define KIOSK_A11Y_MAGNIFIER_SCHEMA "org.gnome.desktop.a11y.magnifier"
#define KIOSK_A11Y_MAGNIFIER_MAG_FACTOR "mag-factor"

struct _KioskMagnifier
{
        GObject            parent;

        /* weak references */
        KioskCompositor   *compositor;
        MetaDisplay       *display;
        MetaContext       *context;
        MetaBackend       *backend;
        MetaCursorTracker *cursor_tracker;
        ClutterActor      *stage;

        /* strong references */
        GSettings         *a11y_applications_settings;
        GSettings         *a11y_magnifier_settings;

        /* state */
        gboolean           enabled;
        double             mag_factor;
        gulong             cursor_moved_handler_id;
};

enum
{
        PROP_COMPOSITOR = 1,
        NUMBER_OF_PROPERTIES
};
static GParamSpec *kiosk_magnifier_properties[NUMBER_OF_PROPERTIES] = { NULL, };

G_DEFINE_TYPE (KioskMagnifier, kiosk_magnifier, G_TYPE_OBJECT)

static void kiosk_magnifier_set_property (GObject      *object,
                                          guint         property_id,
                                          const GValue *value,
                                          GParamSpec   *param_spec);
static void kiosk_magnifier_get_property (GObject    *object,
                                          guint       property_id,
                                          GValue     *value,
                                          GParamSpec *param_spec);

static void kiosk_magnifier_constructed (GObject *object);
static void kiosk_magnifier_dispose (GObject *object);

static void
kiosk_magnifier_apply_zoom (KioskMagnifier *self)
{
        graphene_matrix_t modelview;
        graphene_matrix_t transform;
        graphene_point_t cursor_position;
        float factor = (float) self->mag_factor;

        meta_cursor_tracker_get_pointer (self->cursor_tracker, &cursor_position, NULL);

        graphene_matrix_init_identity (&modelview);
        graphene_matrix_scale (&modelview, factor, factor, 1.0);

        graphene_matrix_init_translate (&transform,
                                        &GRAPHENE_POINT3D_INIT (cursor_position.x * (1.0f - factor),
                                                                cursor_position.y * (1.0f - factor),
                                                                0.0f));
        graphene_matrix_multiply (&modelview, &transform, &modelview);

        clutter_actor_set_child_transform (self->stage, &modelview);
}

static void
kiosk_magnifier_reset_zoom (KioskMagnifier *self)
{
        graphene_matrix_t identity;

        g_debug ("KioskMagnifier: Resetting zoom to default");

        graphene_matrix_init_identity (&identity);
        clutter_actor_set_child_transform (self->stage, &identity);
}

static void
on_cursor_moved (KioskMagnifier *self)
{
        kiosk_magnifier_apply_zoom (self);
}

static void
kiosk_magnifier_connect_cursor_tracking (KioskMagnifier *self)
{
        if (self->cursor_moved_handler_id != 0)
                return;

        g_debug ("KioskMagnifier: Connecting to cursor tracking");

        self->cursor_moved_handler_id =
                g_signal_connect_object (G_OBJECT (self->cursor_tracker),
                                         "position-invalidated",
                                         G_CALLBACK (on_cursor_moved),
                                         self,
                                         G_CONNECT_SWAPPED);

        kiosk_magnifier_apply_zoom (self);
}

static void
kiosk_magnifier_disconnect_cursor_tracking (KioskMagnifier *self)
{
        g_debug ("KioskMagnifier: Disconnecting from cursor tracking");

        g_clear_signal_handler (&self->cursor_moved_handler_id, self->cursor_tracker);
}

static void
kiosk_magnifier_update_state (KioskMagnifier *self)
{
        gboolean was_enabled = self->enabled;

        self->enabled = g_settings_get_boolean (self->a11y_applications_settings,
                                                KIOSK_A11Y_MAGNIFIER_ENABLED);
        self->mag_factor = g_settings_get_double (self->a11y_magnifier_settings,
                                                  KIOSK_A11Y_MAGNIFIER_MAG_FACTOR);

        g_debug ("KioskMagnifier: Magnifier enabled: %s, factor: %f",
                 self->enabled ? "yes" : "no", self->mag_factor);

        if (self->enabled)
                kiosk_magnifier_connect_cursor_tracking (self);
        else
                kiosk_magnifier_disconnect_cursor_tracking (self);

        if (was_enabled && !self->enabled)
                kiosk_magnifier_reset_zoom (self);
}

static void
on_magnifier_enabled_changed (KioskMagnifier *self)
{
        g_debug ("KioskMagnifier: Magnifier enabled setting changed");

        kiosk_magnifier_update_state (self);
}

static void
on_mag_factor_changed (KioskMagnifier *self)
{
        g_debug ("KioskMagnifier: Magnification factor setting changed");

        self->mag_factor = g_settings_get_double (self->a11y_magnifier_settings,
                                                  KIOSK_A11Y_MAGNIFIER_MAG_FACTOR);

        if (self->enabled)
                kiosk_magnifier_apply_zoom (self);
}

static void
kiosk_magnifier_class_init (KioskMagnifierClass *magnifier_class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (magnifier_class);

        object_class->constructed = kiosk_magnifier_constructed;
        object_class->set_property = kiosk_magnifier_set_property;
        object_class->get_property = kiosk_magnifier_get_property;
        object_class->dispose = kiosk_magnifier_dispose;

        kiosk_magnifier_properties[PROP_COMPOSITOR] = g_param_spec_object ("compositor",
                                                                           NULL,
                                                                           NULL,
                                                                           KIOSK_TYPE_COMPOSITOR,
                                                                           G_PARAM_CONSTRUCT_ONLY
                                                                           | G_PARAM_WRITABLE
                                                                           | G_PARAM_STATIC_NAME
                                                                           | G_PARAM_STATIC_NICK
                                                                           | G_PARAM_STATIC_BLURB);
        g_object_class_install_properties (object_class, NUMBER_OF_PROPERTIES, kiosk_magnifier_properties);
}

static void
kiosk_magnifier_set_property (GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *param_spec)
{
        KioskMagnifier *self = KIOSK_MAGNIFIER (object);

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
kiosk_magnifier_get_property (GObject       *object,
                              guint          property_id,
                              GValue *value  G_GNUC_UNUSED,
                              GParamSpec    *param_spec)
{
        switch (property_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, param_spec);
                break;
        }
}

static void
kiosk_magnifier_dispose (GObject *object)
{
        KioskMagnifier *self = KIOSK_MAGNIFIER (object);

        kiosk_magnifier_disconnect_cursor_tracking (self);

        g_clear_object (&self->a11y_applications_settings);
        g_clear_object (&self->a11y_magnifier_settings);

        g_clear_weak_pointer (&self->context);
        g_clear_weak_pointer (&self->backend);
        g_clear_weak_pointer (&self->stage);
        g_clear_weak_pointer (&self->display);
        g_clear_weak_pointer (&self->cursor_tracker);
        g_clear_weak_pointer (&self->compositor);

        G_OBJECT_CLASS (kiosk_magnifier_parent_class)->dispose (object);
}

static void
kiosk_magnifier_constructed (GObject *object)
{
        KioskMagnifier *self = KIOSK_MAGNIFIER (object);
        MetaDisplay *display = meta_plugin_get_display (META_PLUGIN (self->compositor));
        MetaCompositor *compositor = meta_display_get_compositor (display);

        G_OBJECT_CLASS (kiosk_magnifier_parent_class)->constructed (object);

        g_set_weak_pointer (&self->display, display);
        g_set_weak_pointer (&self->context, meta_display_get_context (self->display));
        g_set_weak_pointer (&self->backend, meta_context_get_backend (self->context));
        g_set_weak_pointer (&self->stage, CLUTTER_ACTOR (meta_compositor_get_stage (compositor)));
        g_set_weak_pointer (&self->cursor_tracker, meta_backend_get_cursor_tracker (self->backend));

        self->a11y_applications_settings = g_settings_new (KIOSK_A11Y_APPLICATIONS_SCHEMA);
        self->a11y_magnifier_settings = g_settings_new (KIOSK_A11Y_MAGNIFIER_SCHEMA);

        g_signal_connect_object (G_OBJECT (self->a11y_applications_settings),
                                 "changed::" KIOSK_A11Y_MAGNIFIER_ENABLED,
                                 G_CALLBACK (on_magnifier_enabled_changed),
                                 self,
                                 G_CONNECT_SWAPPED);

        g_signal_connect_object (G_OBJECT (self->a11y_magnifier_settings),
                                 "changed::" KIOSK_A11Y_MAGNIFIER_MAG_FACTOR,
                                 G_CALLBACK (on_mag_factor_changed),
                                 self,
                                 G_CONNECT_SWAPPED);

        kiosk_magnifier_update_state (self);
}

static void
kiosk_magnifier_init (KioskMagnifier *self)
{
        g_debug ("KioskMagnifier: Initializing");

        self->enabled = FALSE;
        self->mag_factor = 1.0;
        self->cursor_moved_handler_id = 0;
}

KioskMagnifier *
kiosk_magnifier_new (KioskCompositor *compositor)
{
        GObject *object;

        object = g_object_new (KIOSK_TYPE_MAGNIFIER,
                               "compositor", compositor,
                               NULL);

        return KIOSK_MAGNIFIER (object);
}
