#include "config.h"

#include "kiosk-compositor.h"
#include "kiosk-app.h"
#include "kiosk-app-system.h"
#include "kiosk-window-tracker.h"

#include <stdlib.h>
#include <string.h>

#include <meta/display.h>
#ifdef HAVE_X11_CLIENT
#include <meta/meta-x11-group.h>
#endif /* HAVE_X11_CLIENT */

#include <glib-object.h>

/* This code is a simplified and expunged version based on GNOME Shell
 * implementation of ShellWindowTracker.
 */

/**
 * SECTION:kiosk-window-tracker
 * @short_description: Associate windows with applications
 *
 * Maintains a mapping from windows to applications (.desktop file ids).
 */

struct _KioskWindowTracker
{
        GObject          parent;

        /* weak references */
        KioskCompositor *compositor;
        KioskAppSystem  *app_system;

        KioskApp        *focused_app;

        /* <MetaWindow * window, KioskApp *app> */
        GHashTable      *window_to_app;
};

G_DEFINE_TYPE (KioskWindowTracker, kiosk_window_tracker, G_TYPE_OBJECT);

enum
{
        PROP_0,
        PROP_APP_SYSTEM,
        PROP_COMPOSITOR,
        PROP_FOCUSED_APP,
        N_PROPS
};

static GParamSpec *props[N_PROPS] = { NULL, };

enum
{
        TRACKED_WINDOWS_CHANGED,
        LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void kiosk_window_tracker_dispose (GObject *object);
static void kiosk_window_tracker_finalize (GObject *object);
static void set_focused_app (KioskWindowTracker *tracker,
                             KioskApp           *new_focused_app);
static void on_focused_window_changed (MetaDisplay        *display,
                                       GParamSpec         *spec,
                                       KioskWindowTracker *tracker);

static void track_window (KioskWindowTracker *tracker,
                          MetaWindow         *window);
static void disassociate_window (KioskWindowTracker *tracker,
                                 MetaWindow         *window);

static void
kiosk_window_tracker_set_property (GObject      *gobject,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
        KioskWindowTracker *tracker = KIOSK_WINDOW_TRACKER (gobject);

        switch (prop_id) {
        case PROP_APP_SYSTEM:
                g_set_weak_pointer (&tracker->app_system,
                                    g_value_get_object (value));
                break;
        case PROP_COMPOSITOR:
                g_set_weak_pointer (&tracker->compositor,
                                    g_value_get_object (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
                break;
        }
}

static void
kiosk_window_tracker_get_property (GObject    *gobject,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
        KioskWindowTracker *tracker = KIOSK_WINDOW_TRACKER (gobject);

        switch (prop_id) {
        case PROP_FOCUSED_APP:
                g_value_set_object (value, tracker->focused_app);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id,
                                                   pspec);
                break;
        }
}

static void
kiosk_window_tracker_class_init (KioskWindowTrackerClass *klass)
{
        GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

        gobject_class->set_property = kiosk_window_tracker_set_property;
        gobject_class->get_property = kiosk_window_tracker_get_property;
        gobject_class->dispose = kiosk_window_tracker_dispose;
        gobject_class->finalize = kiosk_window_tracker_finalize;

        signals[TRACKED_WINDOWS_CHANGED] =
                g_signal_new ("tracked-windows-changed",
                              KIOSK_TYPE_WINDOW_TRACKER,
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL,
                              NULL,
                              NULL,
                              G_TYPE_NONE,
                              0);

        props[PROP_APP_SYSTEM] = g_param_spec_object ("app-system",
                                                      "App System",
                                                      "Application system",
                                                      KIOSK_TYPE_APP_SYSTEM,
                                                      G_PARAM_CONSTRUCT_ONLY
                                                      | G_PARAM_WRITABLE
                                                      | G_PARAM_STATIC_NAME
                                                      | G_PARAM_STATIC_NICK
                                                      | G_PARAM_STATIC_BLURB);
        props[PROP_COMPOSITOR] = g_param_spec_object ("compositor",
                                                      "compositor",
                                                      "compositor",
                                                      KIOSK_TYPE_COMPOSITOR,
                                                      G_PARAM_CONSTRUCT_ONLY
                                                      | G_PARAM_WRITABLE
                                                      | G_PARAM_STATIC_NAME
                                                      | G_PARAM_STATIC_NICK
                                                      | G_PARAM_STATIC_BLURB);
        props[PROP_FOCUSED_APP] = g_param_spec_object ("focused-app",
                                                       "Focused App",
                                                       "Focused application",
                                                       KIOSK_TYPE_APP,
                                                       G_PARAM_READABLE
                                                       | G_PARAM_STATIC_STRINGS);

        g_object_class_install_properties (gobject_class, N_PROPS, props);
}

static gboolean
check_app_id_prefix (KioskApp   *app,
                     const char *prefix)
{
        if (prefix == NULL)
                return TRUE;

        return g_str_has_prefix (kiosk_app_get_id (app), prefix);
}

static KioskApp *
get_app_from_window_wmclass (KioskWindowTracker *tracker,
                             MetaWindow         *window)
{
        KioskApp *app;
        const char *wm_class;
        const char *wm_instance;
        const char *sandbox_id;
        g_autofree char *app_prefix = NULL;

        sandbox_id = meta_window_get_sandboxed_app_id (window);
        if (sandbox_id)
                app_prefix = g_strdup_printf ("%s.", sandbox_id);

        /* First try a match from WM_CLASS (instance part) to .desktop */
        wm_instance = meta_window_get_wm_class_instance (window);
        app = kiosk_app_system_lookup_desktop_wmclass (tracker->app_system, wm_instance);
        if (app != NULL && check_app_id_prefix (app, app_prefix))
                return g_object_ref (app);

        /* Then try a match from WM_CLASS to .desktop */
        wm_class = meta_window_get_wm_class (window);
        app = kiosk_app_system_lookup_desktop_wmclass (tracker->app_system, wm_class);
        if (app != NULL && check_app_id_prefix (app, app_prefix))
                return g_object_ref (app);

        return NULL;
}

static KioskApp *
get_app_from_id (KioskWindowTracker *tracker,
                 MetaWindow         *window,
                 const char         *id)
{
        KioskApp *app;
        g_autofree char *desktop_file = NULL;

        g_return_val_if_fail (id != NULL, NULL);

        desktop_file = g_strconcat (id, ".desktop", NULL);
        app = kiosk_app_system_lookup_app (tracker->app_system, desktop_file);
        if (app)
                return g_object_ref (app);

        return NULL;
}

static KioskApp *
get_app_from_gapplication_id (KioskWindowTracker *tracker,
                              MetaWindow         *window)
{
        const char *id;

        id = meta_window_get_gtk_application_id (window);
        if (!id)
                return NULL;

        return get_app_from_id (tracker, window, id);
}

static KioskApp *
get_app_from_sandboxed_app_id (KioskWindowTracker *tracker,
                               MetaWindow         *window)
{
        const char *id;

        id = meta_window_get_sandboxed_app_id (window);
        if (!id)
                return NULL;

        return get_app_from_id (tracker, window, id);
}

static KioskApp *
get_app_from_window_group (KioskWindowTracker *tracker,
                           MetaWindow         *window)
{
#ifdef HAVE_X11_CLIENT
        KioskApp *result;
        GSList *group_windows;
        MetaGroup *group;
        GSList *iter;

        if (meta_window_get_client_type (window) != META_WINDOW_CLIENT_TYPE_X11)
                return NULL;

        group = meta_window_x11_get_group (window);
        if (group == NULL)
                return NULL;

        group_windows = meta_group_list_windows (group);

        result = NULL;
        /* Try finding a window in the group of type NORMAL; if we
         * succeed, use that as our source. */
        for (iter = group_windows; iter; iter = iter->next) {
                MetaWindow *group_window = iter->data;

                if (meta_window_get_window_type (group_window) != META_WINDOW_NORMAL)
                        continue;

                result = g_hash_table_lookup (tracker->window_to_app,
                                              group_window);
                if (result)
                        break;
        }

        g_slist_free (group_windows);

        if (result)
                g_object_ref (result);

        return result;
#else
        return NULL;
#endif
}

static KioskApp *
kiosk_window_tracker_get_app_from_pid (KioskWindowTracker *tracker,
                                       int                 pid)
{
        KioskAppSystemAppIter app_iter;
        KioskApp *app;
        KioskApp *result = NULL;

        kiosk_app_system_app_iter_init (&app_iter, tracker->app_system);

        while (kiosk_app_system_app_iter_next (&app_iter, &app)) {
                KioskAppProcessIter pid_iter;
                pid_t app_pid;

                kiosk_app_process_iter_init (&pid_iter, app);

                while (kiosk_app_process_iter_next (&pid_iter, &app_pid)) {
                        if (app_pid == pid) {
                                result = app;
                                break;
                        }
                }

                if (result != NULL)
                        break;
        }

        return result;
}

static KioskApp *
get_app_from_window_pid (KioskWindowTracker *tracker,
                         MetaWindow         *window)
{
        KioskApp *result;
        pid_t pid;

        if (meta_window_is_remote (window))
                return NULL;

        pid = meta_window_get_pid (window);

        if (pid < 1)
                return NULL;

        result = kiosk_window_tracker_get_app_from_pid (tracker, pid);
        if (result != NULL)
                g_object_ref (result);

        return result;
}

static KioskApp *
get_app_for_window (KioskWindowTracker *tracker,
                    MetaWindow         *window)
{
        KioskApp *result = NULL;
        MetaWindow *transient_for;

        transient_for = meta_window_get_transient_for (window);
        if (transient_for != NULL)
                return get_app_for_window (tracker, transient_for);

        /* First, we check whether we already know about this window,
         * if so, just return that.
         */
        if (meta_window_get_window_type (window) == META_WINDOW_NORMAL
            || meta_window_is_remote (window)) {
                result = g_hash_table_lookup (tracker->window_to_app, window);
                if (result != NULL) {
                        g_object_ref (result);
                        return result;
                }
        }

        if (meta_window_is_remote (window))
                return kiosk_app_new_for_window (tracker->compositor, window);

        /* Check if the app's WM_CLASS specifies an app; this is
         * canonical if it does.
         */
        result = get_app_from_window_wmclass (tracker, window);
        if (result != NULL)
                return result;

        /* Check if the window was opened from within a sandbox; if this
         * is the case, a corresponding .desktop file is guaranteed to match;
         */
        result = get_app_from_sandboxed_app_id (tracker, window);
        if (result != NULL)
                return result;

        /* Check if the window has a GApplication ID attached; this is
         * canonical if it does
         */
        result = get_app_from_gapplication_id (tracker, window);
        if (result != NULL)
                return result;

        result = get_app_from_window_pid (tracker, window);
        if (result != NULL)
                return result;

        result = get_app_from_window_group (tracker, window);
        /* Our last resort - we create a fake app from the window */
        if (result == NULL)
                result = kiosk_app_new_for_window (tracker->compositor, window);

        return result;
}

static KioskApp *
kiosk_window_tracker_get_window_app (KioskWindowTracker *tracker,
                                     MetaWindow         *window)
{
        KioskApp *app;

        app = g_hash_table_lookup (tracker->window_to_app, window);
        if (app)
                g_object_ref (app);

        return app;
}

static void
update_focused_app (KioskWindowTracker *self)
{
        MetaWindow *new_focus_win;
        KioskApp *new_focused_app = NULL;
        MetaDisplay *display;

        display = meta_plugin_get_display (META_PLUGIN (self->compositor));
        new_focus_win = meta_display_get_focus_window (display);

        g_debug ("KioskWindowTracker: Update focus window to 0x%lx",
                 new_focus_win ? meta_window_get_id (new_focus_win) : 0);

        /* we only consider an app focused if the focus window can be clearly
         * associated with a running app; this is the case if the focus window
         * or one of its parents is visible in the taskbar, e.g.
         *   - 'nautilus' should appear focused when its about dialog has focus
         *   - 'nautilus' should not appear focused when the DESKTOP has focus
         */
        while (new_focus_win && meta_window_is_skip_taskbar (new_focus_win)) {
                new_focus_win = meta_window_get_transient_for (new_focus_win);
        }

        if (new_focus_win)
                new_focused_app = kiosk_window_tracker_get_window_app (self, new_focus_win);

        set_focused_app (self, new_focused_app);

        g_clear_object (&new_focused_app);
}

static void
tracked_window_changed (KioskWindowTracker *self,
                        MetaWindow         *window)
{
        /* It's simplest to just treat this as a remove + add. */
        disassociate_window (self, window);
        track_window (self, window);
        /* Also just recalculate the focused app, in case it was the focused
         * window that changed */
        update_focused_app (self);
}

static void
on_wm_class_changed (MetaWindow *window,
                     GParamSpec *pspec,
                     gpointer    user_data)
{
        KioskWindowTracker *self = KIOSK_WINDOW_TRACKER (user_data);
        tracked_window_changed (self, window);
}

static void
on_title_changed (MetaWindow *window,
                  GParamSpec *pspec,
                  gpointer    user_data)
{
        KioskWindowTracker *self = KIOSK_WINDOW_TRACKER (user_data);
        g_signal_emit (self, signals[TRACKED_WINDOWS_CHANGED], 0);
}

static void
on_gtk_application_id_changed (MetaWindow *window,
                               GParamSpec *pspec,
                               gpointer    user_data)
{
        KioskWindowTracker *self = KIOSK_WINDOW_TRACKER (user_data);
        tracked_window_changed (self, window);
}

static void
on_window_unmanaged (MetaWindow *window,
                     gpointer    user_data)
{
        disassociate_window (KIOSK_WINDOW_TRACKER (user_data), window);
}

static void
on_app_state_changed (KioskApp   *app,
                      GParamSpec *pspec,
                      gpointer    user_data)
{
        KioskWindowTracker *self = KIOSK_WINDOW_TRACKER (user_data);
        kiosk_app_system_notify_app_state_changed (self->app_system, app);
}

static void
track_window (KioskWindowTracker *self,
              MetaWindow         *window)
{
        KioskApp *app;

        app = get_app_for_window (self, window);
        if (!app)
                return;

        /* At this point we've stored the association from window -> application */
        g_hash_table_insert (self->window_to_app, window, app);

        g_signal_connect (window, "notify::wm-class",
                          G_CALLBACK (on_wm_class_changed),
                          self);
        g_signal_connect (window, "notify::title",
                          G_CALLBACK (on_title_changed),
                          self);
        g_signal_connect (window, "notify::gtk-application-id",
                          G_CALLBACK (on_gtk_application_id_changed),
                          self);
        g_signal_connect (window, "unmanaged",
                          G_CALLBACK (on_window_unmanaged),
                          self);
        g_signal_connect (app, "notify::state",
                          G_CALLBACK (on_app_state_changed),
                          self);

        kiosk_app_add_window (app, window);

        g_signal_emit (self, signals[TRACKED_WINDOWS_CHANGED], 0);
}

static void
on_window_created (MetaDisplay *display,
                   MetaWindow  *window,
                   gpointer     user_data)
{
        track_window (KIOSK_WINDOW_TRACKER (user_data), window);
}

static void
disassociate_window (KioskWindowTracker *self,
                     MetaWindow         *window)
{
        KioskApp *app;

        app = g_hash_table_lookup (self->window_to_app, window);
        if (!app)
                return;

        g_object_ref (app);

        g_hash_table_remove (self->window_to_app, window);

        kiosk_app_remove_window (app, window);
        g_signal_handlers_disconnect_by_func (window,
                                              G_CALLBACK
                                                      (on_wm_class_changed),
                                              self);
        g_signal_handlers_disconnect_by_func (window,
                                              G_CALLBACK (on_title_changed),
                                              self);
        g_signal_handlers_disconnect_by_func (window,
                                              G_CALLBACK
                                                      (on_gtk_application_id_changed),
                                              self);
        g_signal_handlers_disconnect_by_func (window,
                                              G_CALLBACK
                                                      (on_window_unmanaged),
                                              self);

        g_signal_emit (self, signals[TRACKED_WINDOWS_CHANGED], 0);

        g_object_unref (app);
}

static void
load_initial_windows (KioskWindowTracker *tracker)
{
        MetaDisplay *display;
        g_autoptr (GList) windows = NULL;
        GList *l;

        display = meta_plugin_get_display (META_PLUGIN (tracker->compositor));
        windows = meta_display_list_all_windows (display);
        for (l = windows; l; l = l->next) {
                track_window (tracker, l->data);
        }
}

static void
init_window_tracking (KioskWindowTracker *self)
{
        MetaDisplay *display;

        display = meta_plugin_get_display (META_PLUGIN (self->compositor));
        g_signal_connect_object (display, "notify::focus-window",
                                 G_CALLBACK (on_focused_window_changed),
                                 self,
                                 G_CONNECT_DEFAULT);
        g_signal_connect_object (display, "window-created",
                                 G_CALLBACK (on_window_created),
                                 self,
                                 G_CONNECT_DEFAULT);
}

static void
kiosk_window_tracker_init (KioskWindowTracker *self)
{
        self->window_to_app = g_hash_table_new_full (g_direct_hash,
                                                     g_direct_equal,
                                                     NULL,
                                                     (GDestroyNotify) g_object_unref);
}

static void
kiosk_window_tracker_dispose (GObject *object)
{
        KioskWindowTracker *self = KIOSK_WINDOW_TRACKER (object);

        g_clear_weak_pointer (&self->app_system);
        g_clear_weak_pointer (&self->compositor);

        G_OBJECT_CLASS (kiosk_window_tracker_parent_class)->dispose (object);
}

static void
kiosk_window_tracker_finalize (GObject *object)
{
        KioskWindowTracker *self = KIOSK_WINDOW_TRACKER (object);

        g_hash_table_destroy (self->window_to_app);

        G_OBJECT_CLASS (kiosk_window_tracker_parent_class)->finalize (object);
}

static void
set_focused_app (KioskWindowTracker *tracker,
                 KioskApp           *new_focused_app)
{
        g_debug ("KioskWindowTracker: Update focus App to '%s'",
                 new_focused_app ? kiosk_app_get_id (new_focused_app) : "None");

        if (new_focused_app == tracker->focused_app)
                return;

        if (tracker->focused_app != NULL)
                g_object_unref (tracker->focused_app);

        tracker->focused_app = new_focused_app;

        if (tracker->focused_app != NULL)
                g_object_ref (tracker->focused_app);

        g_object_notify_by_pspec (G_OBJECT (tracker), props[PROP_FOCUSED_APP]);
}

KioskApp *
kiosk_window_tracker_get_focused_app (KioskWindowTracker *tracker)
{
        if (tracker->focused_app)
                g_object_ref (tracker->focused_app);
        return tracker->focused_app;
}

static void
on_focused_window_changed (MetaDisplay        *display,
                           GParamSpec         *spec,
                           KioskWindowTracker *tracker)
{
        update_focused_app (tracker);
}

KioskWindowTracker *
kiosk_window_tracker_new (KioskCompositor *compositor,
                          KioskAppSystem  *app_system)
{
        KioskWindowTracker *tracker;

        tracker = g_object_new (KIOSK_TYPE_WINDOW_TRACKER,
                                "compositor", compositor,
                                "app_system", app_system,
                                NULL);

        load_initial_windows (tracker);
        init_window_tracking (tracker);

        return tracker;
}
