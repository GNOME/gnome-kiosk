#include "config.h"
#include "kiosk-screensaver.h"

#include <clutter/clutter.h>
#include <cogl/cogl-color.h>

#include <meta/display.h>
#include <meta/meta-context.h>
#include <meta/meta-backend.h>
#include <meta/meta-plugin.h>
#include <meta/meta-monitor-manager.h>

#include "kiosk-compositor.h"

struct _KioskScreensaver
{
        GObject             parent;

        /* weak references */
        KioskCompositor    *compositor;
        MetaDisplay        *display;
        MetaContext        *context;
        MetaBackend        *backend;
        MetaMonitorManager *monitor_manager;
        ClutterActor       *stage;

        /* strong references */
        ClutterActor       *screensaver_group;
        ClutterGrab        *stage_grab;

        gboolean            active;
        gboolean            locked;
        gint64              activate_time;
};

enum
{
        PROP_COMPOSITOR = 1,
        NUMBER_OF_PROPERTIES
};
static GParamSpec *kiosk_screensaver_properties[NUMBER_OF_PROPERTIES] = { NULL, };

enum
{
        STATUS_CHANGED,
        NUMBER_OF_SIGNALS
};
static guint kiosk_screensaver_signals[NUMBER_OF_SIGNALS] = { 0, };

G_DEFINE_FINAL_TYPE (KioskScreensaver, kiosk_screensaver, G_TYPE_OBJECT);

static void
kiosk_screensaver_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *param_spec)
{
        KioskScreensaver *self = KIOSK_SCREENSAVER (object);

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
kiosk_screensaver_get_property (GObject    *object,
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
create_screensaver_for_monitor (KioskScreensaver *self,
                                int               monitor_index)
{
        ClutterActor *screensaver_actor;
        CoglColor black;
        MtkRectangle geometry;

        g_debug ("KioskScreensaver: Creating screensaver for monitor %d", monitor_index);

        cogl_color_init_from_4f (&black, 0.0f, 0.0f, 0.0f, 1.0f);

        screensaver_actor = clutter_actor_new ();
        clutter_actor_set_background_color (screensaver_actor, &black);
        clutter_actor_set_reactive (screensaver_actor, TRUE);

        meta_display_get_monitor_geometry (self->display, monitor_index, &geometry);

        clutter_actor_set_position (screensaver_actor, geometry.x, geometry.y);
        clutter_actor_set_size (screensaver_actor, geometry.width, geometry.height);

        clutter_actor_add_child (self->screensaver_group, screensaver_actor);
}

static gboolean
is_unlock_event (ClutterEvent *event)
{
        ClutterEventType event_type;
        guint keyval;

        event_type = clutter_event_type (event);

        /* Allow button clicks to unlock */
        if (event_type == CLUTTER_BUTTON_PRESS)
                return TRUE;

        /* Allow touch events to unlock */
        if (event_type == CLUTTER_TOUCH_BEGIN)
                return TRUE;

        /* Allow specific keys to unlock */
        if (event_type != CLUTTER_KEY_PRESS)
                return FALSE;

        keyval = clutter_event_get_key_symbol (event);
        if (keyval == CLUTTER_KEY_Return ||
            keyval == CLUTTER_KEY_KP_Enter ||
            keyval == CLUTTER_KEY_space ||
            keyval == CLUTTER_KEY_Escape)
                return TRUE;

        return FALSE;
}

static gboolean
on_input_event (ClutterActor *actor,
                ClutterEvent *event,
                gpointer      user_data)
{
        KioskScreensaver *self = KIOSK_SCREENSAVER (user_data);

        g_debug ("KioskScreensaver: Input event received");

        if (!self->locked && !self->active)
                return CLUTTER_EVENT_PROPAGATE;

        /* When locked, only deactivate on specific key press or button click */
        if (self->locked && !is_unlock_event (event))
                return CLUTTER_EVENT_STOP;

        g_debug ("KioskScreensaver: Deactivating on input event");

        kiosk_screensaver_deactivate (self);

        return CLUTTER_EVENT_STOP;
}

static void
reinitialize_screensavers (KioskScreensaver *self)
{
        g_autoptr (GList) old_children = NULL;
        GList *l;
        int i, number_of_monitors;

        g_debug ("KioskScreensaver: Recreating screensavers");

        old_children = clutter_actor_get_children (self->screensaver_group);

        number_of_monitors = meta_display_get_n_monitors (self->display);
        for (i = 0; i < number_of_monitors; i++) {
                create_screensaver_for_monitor (self, i);
        }

        for (l = old_children; l != NULL; l = l->next) {
                clutter_actor_destroy (CLUTTER_ACTOR (l->data));
        }

        if (self->stage_grab == NULL) {
                g_debug ("KioskScreensaver: Grabbing stage");
                self->stage_grab = clutter_stage_grab (CLUTTER_STAGE (self->stage),
                                                       self->screensaver_group);
        }

        g_debug ("KioskScreensaver: Finished recreating screensavers");
}

static void
kiosk_screensaver_show_now (KioskScreensaver *self)
{
        g_debug ("KioskScreensaver: Showing screensaver");

        clutter_actor_set_opacity (self->screensaver_group, 255);
        clutter_actor_show (self->screensaver_group);
        g_signal_emit (self, kiosk_screensaver_signals[STATUS_CHANGED], 0, TRUE);
}

static void
on_fade_in_complete (ClutterTransition *transition,
                     KioskScreensaver  *self)
{
        g_debug ("KioskScreensaver: Fade in complete");

        kiosk_screensaver_show_now (self);
}

static void
kiosk_screensaver_show (KioskScreensaver *self)
{
        ClutterTransition *fade_in_transition;

        self->active = TRUE;
        self->activate_time = g_get_monotonic_time ();
        clutter_actor_set_child_above_sibling (self->stage, self->screensaver_group, NULL);

        g_signal_connect (self->screensaver_group, "button-press-event",
                          G_CALLBACK (on_input_event), self);
        g_signal_connect (self->screensaver_group, "key-press-event",
                          G_CALLBACK (on_input_event), self);
        g_signal_connect (self->screensaver_group, "motion-event",
                          G_CALLBACK (on_input_event), self);
        g_signal_connect (self->screensaver_group, "touch-event",
                          G_CALLBACK (on_input_event), self);

        g_signal_connect_object (G_OBJECT (self->monitor_manager),
                                 "monitors-changed",
                                 G_CALLBACK (reinitialize_screensavers),
                                 self,
                                 G_CONNECT_SWAPPED);

        reinitialize_screensavers (self);

        if (!kiosk_compositor_are_animations_enabled (self->compositor)) {
                /* Show immediately without fade */
                kiosk_screensaver_show_now (self);
                return;
        }

        /* Fade in */
        clutter_actor_set_opacity (self->screensaver_group, 0);
        clutter_actor_show (self->screensaver_group);
        clutter_actor_save_easing_state (self->screensaver_group);
        clutter_actor_set_easing_duration (self->screensaver_group, 500);
        clutter_actor_set_easing_mode (self->screensaver_group, CLUTTER_EASE_IN_OUT_QUAD);
        clutter_actor_set_opacity (self->screensaver_group, 255);
        fade_in_transition = clutter_actor_get_transition (self->screensaver_group, "opacity");
        clutter_actor_restore_easing_state (self->screensaver_group);

        g_signal_connect (fade_in_transition,
                          "completed",
                          G_CALLBACK (on_fade_in_complete),
                          self);
}

static void
kiosk_screensaver_hide_now (KioskScreensaver *self)
{
        g_debug ("KioskScreensaver: Hiding screensaver");

        clutter_actor_hide (self->screensaver_group);
        clutter_actor_destroy_all_children (self->screensaver_group);
        g_signal_emit (self, kiosk_screensaver_signals[STATUS_CHANGED], 0, FALSE);
}

static void
on_fade_out_complete (ClutterTransition *transition,
                      KioskScreensaver  *self)
{
        g_debug ("KioskScreensaver: Fade out complete");

        kiosk_screensaver_hide_now (self);
}

static void
kiosk_screensaver_hide (KioskScreensaver *self)
{
        ClutterTransition *fade_out_transition;

        self->active = FALSE;
        self->locked = FALSE;

        g_clear_pointer (&self->stage_grab, clutter_grab_dismiss);

        g_signal_handlers_disconnect_by_func (self->screensaver_group,
                                              G_CALLBACK (on_input_event), self);

        g_signal_handlers_disconnect_by_func (self->monitor_manager,
                                              G_CALLBACK (reinitialize_screensavers), self);

        if (!kiosk_compositor_are_animations_enabled (self->compositor)) {
                /* Hide immediately without fade */
                kiosk_screensaver_hide_now (self);
                return;
        }

        /* Fade out */
        clutter_actor_save_easing_state (self->screensaver_group);
        clutter_actor_set_easing_duration (self->screensaver_group, 250);
        clutter_actor_set_easing_mode (self->screensaver_group, CLUTTER_EASE_IN_OUT_QUAD);
        clutter_actor_set_opacity (self->screensaver_group, 0);
        fade_out_transition = clutter_actor_get_transition (self->screensaver_group, "opacity");
        clutter_actor_restore_easing_state (self->screensaver_group);

        g_signal_connect (fade_out_transition,
                          "completed",
                          G_CALLBACK (on_fade_out_complete),
                          self);
}

static void
kiosk_screensaver_constructed (GObject *object)
{
        KioskScreensaver *self = KIOSK_SCREENSAVER (object);
        MetaDisplay *display = meta_plugin_get_display (META_PLUGIN (self->compositor));
        MetaCompositor *compositor = meta_display_get_compositor (display);

        G_OBJECT_CLASS (kiosk_screensaver_parent_class)->constructed (object);

        g_set_weak_pointer (&self->display, display);
        g_set_weak_pointer (&self->context, meta_display_get_context (self->display));
        g_set_weak_pointer (&self->backend, meta_context_get_backend (self->context));
        g_set_weak_pointer (&self->stage, CLUTTER_ACTOR (meta_compositor_get_stage (compositor)));
        g_set_weak_pointer (&self->monitor_manager, meta_backend_get_monitor_manager (self->backend));

        self->screensaver_group = clutter_actor_new ();
        g_object_ref_sink (self->screensaver_group);
        clutter_actor_set_reactive (self->screensaver_group, TRUE);
        clutter_actor_add_child (self->stage, self->screensaver_group);
        clutter_actor_hide (self->screensaver_group);
}

static void
kiosk_screensaver_dispose (GObject *object)
{
        KioskScreensaver *self = KIOSK_SCREENSAVER (object);

        kiosk_screensaver_hide (self);

        g_clear_object (&self->screensaver_group);

        g_clear_weak_pointer (&self->stage);
        g_clear_weak_pointer (&self->context);
        g_clear_weak_pointer (&self->backend);
        g_clear_weak_pointer (&self->display);
        g_clear_weak_pointer (&self->monitor_manager);
        g_clear_weak_pointer (&self->compositor);

        G_OBJECT_CLASS (kiosk_screensaver_parent_class)->dispose (object);
}

static void
kiosk_screensaver_class_init (KioskScreensaverClass *screensaver_class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (screensaver_class);

        object_class->constructed = kiosk_screensaver_constructed;
        object_class->set_property = kiosk_screensaver_set_property;
        object_class->get_property = kiosk_screensaver_get_property;
        object_class->dispose = kiosk_screensaver_dispose;

        kiosk_screensaver_properties[PROP_COMPOSITOR] = g_param_spec_object ("compositor",
                                                                             NULL, NULL,
                                                                             KIOSK_TYPE_COMPOSITOR,
                                                                             G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_NAME);
        g_object_class_install_properties (object_class, NUMBER_OF_PROPERTIES, kiosk_screensaver_properties);

        kiosk_screensaver_signals[STATUS_CHANGED] =
                g_signal_new ("status-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL, NULL,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_BOOLEAN);
}

static void
kiosk_screensaver_init (KioskScreensaver *self)
{
        g_debug ("KioskScreensaver: Initializing");
}

KioskScreensaver *
kiosk_screensaver_new (KioskCompositor *compositor)
{
        GObject *object;

        object = g_object_new (KIOSK_TYPE_SCREENSAVER,
                               "compositor", compositor,
                               NULL);

        return KIOSK_SCREENSAVER (object);
}

void
kiosk_screensaver_activate (KioskScreensaver *self)
{
        g_return_if_fail (KIOSK_IS_SCREENSAVER (self));
        g_return_if_fail (!self->active);

        g_debug ("KioskScreensaver: Activating screensaver");

        kiosk_screensaver_show (self);
}

void
kiosk_screensaver_deactivate (KioskScreensaver *self)
{
        g_return_if_fail (KIOSK_IS_SCREENSAVER (self));
        g_return_if_fail (self->active);

        g_debug ("KioskScreensaver: Deactivating screensaver");

        kiosk_screensaver_hide (self);
}

void
kiosk_screensaver_lock (KioskScreensaver *self)
{
        g_return_if_fail (KIOSK_IS_SCREENSAVER (self));
        g_return_if_fail (!self->locked);

        g_debug ("KioskScreensaver: Locking screensaver");

        self->locked = TRUE;
        if (!self->active)
                kiosk_screensaver_show (self);
}

gboolean
kiosk_screensaver_get_active (KioskScreensaver *self)
{
        g_return_val_if_fail (KIOSK_IS_SCREENSAVER (self), FALSE);

        return self->active;
}

gboolean
kiosk_screensaver_get_locked (KioskScreensaver *self)
{
        g_return_val_if_fail (KIOSK_IS_SCREENSAVER (self), FALSE);

        return self->locked;
}

guint32
kiosk_screensaver_get_active_time (KioskScreensaver *self)
{
        gint64 now;
        gint64 delta;

        g_return_val_if_fail (KIOSK_IS_SCREENSAVER (self), 0);

        if (!self->active)
                return 0;

        now = g_get_monotonic_time ();
        delta = now - self->activate_time;

        /* Convert from microseconds to seconds */
        return (guint32) (delta / G_USEC_PER_SEC);
}
