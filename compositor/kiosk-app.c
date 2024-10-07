#include "config.h"

#include <string.h>

#include "kiosk-compositor.h"
#include "kiosk-enum-types.h"

#include <meta/display.h>
#include <meta/meta-context.h>
#include <meta/meta-workspace-manager.h>

#include "kiosk-app.h"

/* This code is a simplified and expunged version based on GNOME Shell
 * implementation of ShellApp.
 */

typedef struct
{
        /* weak references */
        MetaDisplay *display;

        /* Signal connection to dirty window sort list on workspace changes */
        gulong       workspace_switch_id;

        GSList      *windows;

        guint        number_of_interesting_windows;

        /* Whether or not we need to resort the windows; this is done on demand */
        guint        windows_are_unsorted : 1;
} KioskAppRunningState;

/**
 * SECTION:kiosk-app
 * @short_description: Object representing an application
 *
 * This object wraps a #GDesktopAppInfo, providing methods and signals
 * primarily useful for running applications.
 */
struct _KioskApp
{
        GObject               parent;

        /* weak references */
        KioskCompositor      *compositor;
        MetaDisplay          *display;

        int                   started_on_workspace;

        KioskAppState         state;

        GDesktopAppInfo      *info;

        KioskAppRunningState *running_state;

        char                 *window_id_string;
};

enum
{
        PROP_0,
        PROP_COMPOSITOR,
        PROP_STATE,
        PROP_ID,
        PROP_APP_INFO,
        N_PROPS
};

static GParamSpec *props[N_PROPS] = { NULL, };

enum
{
        WINDOWS_CHANGED,
        LAST_SIGNAL
};

static guint kiosk_app_signals[LAST_SIGNAL] = { 0 };

static void create_running_state (KioskApp *app);
static void unref_running_state (KioskAppRunningState *state);

G_DEFINE_TYPE (KioskApp, kiosk_app, G_TYPE_OBJECT)

const char *
kiosk_app_get_id (KioskApp * app){
        if (app->info)
                return g_app_info_get_id (G_APP_INFO (app->info));
        return app->window_id_string;
}

static MetaWorkspace *
get_active_workspace (MetaDisplay *display)
{
        MetaWorkspaceManager *workspace_manager =
                meta_display_get_workspace_manager (display);

        return meta_workspace_manager_get_active_workspace (workspace_manager);
}

KioskAppState
kiosk_app_get_state (KioskApp *app)
{
        return app->state;
}

typedef struct
{
        KioskApp      *app;
        MetaWorkspace *active_workspace;
} CompareWindowsData;

static int
kiosk_app_compare_windows (gconstpointer  a,
                           gconstpointer  b,
                           gpointer       datap)
{
        MetaWindow *win_a = (gpointer) a;
        MetaWindow *win_b = (gpointer) b;
        CompareWindowsData *data = datap;
        gboolean ws_a, ws_b;
        gboolean vis_a, vis_b;

        ws_a = meta_window_get_workspace (win_a) == data->active_workspace;
        ws_b = meta_window_get_workspace (win_b) == data->active_workspace;

        if (ws_a && !ws_b)
                return -1;
        else if (!ws_a && ws_b)
                return 1;

        vis_a = meta_window_showing_on_its_workspace (win_a);
        vis_b = meta_window_showing_on_its_workspace (win_b);

        if (vis_a && !vis_b)
                return -1;
        else if (!vis_a && vis_b)
                return 1;

        return meta_window_get_user_time (win_b) - meta_window_get_user_time (win_a);
}

void
kiosk_app_window_iter_init (KioskAppWindowIter *iter,
                            KioskApp           *app)
{
        if (app->running_state == NULL) {
                iter->current = NULL;
        } else {
                if (app->running_state->windows_are_unsorted) {
                        CompareWindowsData data;
                        data.app = app;
                        data.active_workspace = get_active_workspace (app->display);
                        app->running_state->windows =
                                g_slist_sort_with_data (app->running_state->windows,
                                                        kiosk_app_compare_windows,
                                                        &data);
                        app->running_state->windows_are_unsorted = FALSE;
                }
                iter->current = app->running_state->windows;
        }
}

gboolean
kiosk_app_window_iter_next (KioskAppWindowIter *iter,
                            MetaWindow        **window)
{
        while (iter->current) {
                MetaWindow *w = iter->current->data;
                iter->current = iter->current->next;

                if (!meta_window_is_override_redirect (w)) {
                        *window = w;
                        return TRUE;
                }
        }
        return FALSE;
}

void
kiosk_app_process_iter_init (KioskAppProcessIter *iter,
                             KioskApp            *app)
{
        kiosk_app_window_iter_init (&iter->window_iter, app);
        iter->seen_pids = g_hash_table_new (g_direct_hash, g_direct_equal);
}

gboolean
kiosk_app_process_iter_next (KioskAppProcessIter *iter,
                             pid_t               *pid_out)
{
        MetaWindow *window;
        while (kiosk_app_window_iter_next (&iter->window_iter, &window)) {
                pid_t pid = meta_window_get_pid (window);
                if (pid < 1)
                        continue;
                if (g_hash_table_add (iter->seen_pids, GINT_TO_POINTER (pid))) {
                        *pid_out = pid;
                        return TRUE;
                }
                /* pid already seen */
        }
        return FALSE;
}

static int
kiosk_app_get_last_user_time (KioskApp *app)
{
        KioskAppWindowIter iter;
        MetaWindow *window;
        guint32 last_user_time = 0;

        if (app->running_state != NULL) {
                kiosk_app_window_iter_init (&iter, app);
                while (kiosk_app_window_iter_next (&iter, &window)) {
                        last_user_time = MAX (last_user_time, meta_window_get_user_time (window));
                }
        }

        return (int) last_user_time;
}

static gboolean
kiosk_app_is_minimized (KioskApp *app)
{
        KioskAppWindowIter iter;
        MetaWindow *window;

        if (app->running_state == NULL)
                return FALSE;

        kiosk_app_window_iter_init (&iter, app);
        while (kiosk_app_window_iter_next (&iter, &window)) {
                if (meta_window_showing_on_its_workspace (window))
                        return FALSE;
        }

        return TRUE;
}

static gboolean
kiosk_app_has_windows (KioskApp *app)
{
        KioskAppWindowIter iter;
        MetaWindow *window;

        if (app->running_state == NULL)
                return FALSE;

        kiosk_app_window_iter_init (&iter, app);
        return kiosk_app_window_iter_next (&iter, &window);
}

/**
 * kiosk_app_compare:
 * @app:
 * @other: A #KioskApp
 *
 * Compare one #KioskApp instance to another, in the following way:
 *   - Running applications sort before not-running applications.
 *   - If one of them has non-minimized windows and the other does not,
 *     the one with visible windows is first.
 *   - Finally, the application which the user interacted with most recently
 *     compares earlier.
 */
int
kiosk_app_compare (KioskApp *app,
                   KioskApp *other)
{
        gboolean min_app, min_other;

        if (app->state != other->state) {
                if (app->state == KIOSK_APP_STATE_RUNNING)
                        return -1;
                return 1;
        }

        min_app = kiosk_app_is_minimized (app);
        min_other = kiosk_app_is_minimized (other);

        if (min_app != min_other) {
                if (min_other)
                        return -1;
                return 1;
        }

        if (app->state == KIOSK_APP_STATE_RUNNING) {
                if (kiosk_app_has_windows (app) && !kiosk_app_has_windows (other))
                        return -1;
                else if (!kiosk_app_has_windows (app) && kiosk_app_has_windows (other))
                        return 1;

                return kiosk_app_get_last_user_time (other) -
                       kiosk_app_get_last_user_time (app);
        }

        return 0;
}

static void
kiosk_app_set_app_info (KioskApp        *app,
                        GDesktopAppInfo *info)
{
        g_set_object (&app->info, info);
}

static void
kiosk_app_state_transition (KioskApp      *app,
                            KioskAppState  state)
{
        if (app->state == state)
                return;

        g_debug ("KioskApp: App '%s' state %i", kiosk_app_get_id (app), state);

        app->state = state;
        g_object_notify_by_pspec (G_OBJECT (app), props[PROP_STATE]);
}

static void
kiosk_app_on_user_time_changed (MetaWindow *window,
                                GParamSpec *pspec,
                                KioskApp   *app)
{
        g_assert (app->running_state != NULL);

        /* Ideally we don't want to emit windows-changed if the sort order
         * isn't actually changing. This check catches most of those.
         */
        if (window != app->running_state->windows->data) {
                app->running_state->windows_are_unsorted = TRUE;
                g_signal_emit (app, kiosk_app_signals[WINDOWS_CHANGED], 0);
        }
}

static void
kiosk_app_sync_running_state (KioskApp *app)
{
        g_return_if_fail (app->running_state != NULL);

        if (app->running_state->number_of_interesting_windows == 0)
                kiosk_app_state_transition (app, KIOSK_APP_STATE_STOPPED);
        else
                kiosk_app_state_transition (app, KIOSK_APP_STATE_RUNNING);
}

static void
kiosk_app_on_skip_taskbar_changed (MetaWindow *window,
                                   GParamSpec *pspec,
                                   KioskApp   *app)
{
        g_assert (app->running_state != NULL);

        /* we rely on MetaWindow:skip-taskbar only being notified
         * when it actually changes; when that assumption breaks,
         * we'll have to track the "interesting" windows themselves
         */
        if (meta_window_is_skip_taskbar (window))
                app->running_state->number_of_interesting_windows--;
        else
                app->running_state->number_of_interesting_windows++;

        kiosk_app_sync_running_state (app);
}

static void
on_workspace_switched (MetaWorkspaceManager *workspace_manager,
                       int                   from,
                       int                   to,
                       MetaMotionDirection   direction,
                       gpointer              data)
{
        KioskApp *app = KIOSK_APP (data);

        g_assert (app->running_state != NULL);

        app->running_state->windows_are_unsorted = TRUE;

        g_signal_emit (app, kiosk_app_signals[WINDOWS_CHANGED], 0);
}

void
kiosk_app_add_window (KioskApp   *app,
                      MetaWindow *window)
{
        if (app->running_state
            && g_slist_find (app->running_state->windows, window))
                return;

        g_debug ("KioskApp: App '%s' add window 0x%lx",
                 kiosk_app_get_id (app), meta_window_get_id (window));

        g_object_freeze_notify (G_OBJECT (app));

        if (!app->running_state)
                create_running_state (app);

        app->running_state->windows_are_unsorted = TRUE;
        app->running_state->windows =
                g_slist_prepend (app->running_state->windows,
                                 g_object_ref (window));
        g_signal_connect_object (window, "notify::user-time",
                                 G_CALLBACK (kiosk_app_on_user_time_changed),
                                 app, 0);
        g_signal_connect_object (window, "notify::skip-taskbar",
                                 G_CALLBACK (kiosk_app_on_skip_taskbar_changed),
                                 app, 0);

        if (!meta_window_is_skip_taskbar (window))
                app->running_state->number_of_interesting_windows++;
        kiosk_app_sync_running_state (app);

        if (app->started_on_workspace >= 0
            && !meta_window_is_on_all_workspaces (window)) {
                meta_window_change_workspace_by_index (window,
                                                       app->started_on_workspace,
                                                       FALSE);
        }
        app->started_on_workspace = -1;

        g_object_thaw_notify (G_OBJECT (app));

        g_signal_emit (app, kiosk_app_signals[WINDOWS_CHANGED], 0);
}

void
kiosk_app_remove_window (KioskApp   *app,
                         MetaWindow *window)
{
        g_assert (app->running_state != NULL);

        if (!g_slist_find (app->running_state->windows, window))
                return;

        g_debug ("KioskApp: App '%s' remove window 0x%lx",
                 kiosk_app_get_id (app), meta_window_get_id (window));

        app->running_state->windows =
                g_slist_remove (app->running_state->windows, window);

        if (!meta_window_is_skip_taskbar (window))
                app->running_state->number_of_interesting_windows--;
        kiosk_app_sync_running_state (app);

        if (app->running_state->windows == NULL)
                g_clear_pointer (&app->running_state, unref_running_state);

        g_signal_handlers_disconnect_by_func (window,
                                              G_CALLBACK (kiosk_app_on_user_time_changed),
                                              app);
        g_signal_handlers_disconnect_by_func (window,
                                              G_CALLBACK (kiosk_app_on_skip_taskbar_changed),
                                              app);

        g_object_unref (window);

        g_signal_emit (app, kiosk_app_signals[WINDOWS_CHANGED], 0);
}

KioskApp *
kiosk_app_new_for_window (KioskCompositor *compositor,
                          MetaWindow      *window)
{
        KioskApp *app;

        app = g_object_new (KIOSK_TYPE_APP, "compositor", compositor, NULL);

        app->window_id_string = g_strdup_printf ("window:%d",
                                                 meta_window_get_stable_sequence (window));

        return app;
}

KioskApp *
kiosk_app_new (KioskCompositor *compositor,
               GDesktopAppInfo *info)
{
        KioskApp *app;

        app = g_object_new (KIOSK_TYPE_APP,
                            "compositor", compositor, "app-info", info, NULL);

        return app;
}

const char *
kiosk_app_get_sandbox_id (KioskApp *app)
{
        KioskAppWindowIter iter;
        MetaWindow *window;

        kiosk_app_window_iter_init (&iter, app);
        while (kiosk_app_window_iter_next (&iter, &window)) {
                const char *sandbox_id = meta_window_get_sandboxed_app_id (window);

                if (sandbox_id)
                        return sandbox_id;
        }

        return NULL;
}

static void
create_running_state (KioskApp *app)
{
        MetaWorkspaceManager *workspace_manager =
                meta_display_get_workspace_manager (app->display);

        g_assert (app->running_state == NULL);

        app->running_state = g_new0 (KioskAppRunningState, 1);
        app->running_state->workspace_switch_id =
                g_signal_connect (workspace_manager, "workspace-switched",
                                  G_CALLBACK (on_workspace_switched), app);
        g_set_weak_pointer (&app->running_state->display, app->display);
}

static void
unref_running_state (KioskAppRunningState *state)
{
        MetaWorkspaceManager *workspace_manager;

        workspace_manager = meta_display_get_workspace_manager (state->display);
        g_clear_signal_handler (&state->workspace_switch_id,
                                workspace_manager);
        g_clear_weak_pointer (&state->display);

        g_free (state);
}

static void
kiosk_app_get_property (GObject    *gobject,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
        KioskApp *app = KIOSK_APP (gobject);

        switch (prop_id) {
        case PROP_STATE:
                g_value_set_enum (value, app->state);
                break;
        case PROP_ID:
                g_value_set_string (value, kiosk_app_get_id (app));
                break;
        case PROP_APP_INFO:
                if (app->info)
                        g_value_set_object (value, app->info);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id,
                                                   pspec);
                break;
        }
}

static void
kiosk_app_set_property (GObject      *gobject,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
        KioskApp *app = KIOSK_APP (gobject);

        switch (prop_id) {
        case PROP_COMPOSITOR:
                g_set_weak_pointer (&app->compositor,
                                    g_value_get_object (value));
                break;
        case PROP_APP_INFO:
                kiosk_app_set_app_info (app, g_value_get_object (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
                break;
        }
}

static void
kiosk_app_init (KioskApp *self)
{
        self->state = KIOSK_APP_STATE_STOPPED;
        self->started_on_workspace = -1;
}

static void
kiosk_app_constructed (GObject *object)
{
        KioskApp *app = KIOSK_APP (object);

        G_OBJECT_CLASS (kiosk_app_parent_class)->constructed (object);

        g_set_weak_pointer (&app->display,
                            meta_plugin_get_display (META_PLUGIN
                                                             (app->compositor)));
}

static void
kiosk_app_dispose (GObject *object)
{
        KioskApp *app = KIOSK_APP (object);

        g_clear_object (&app->info);

        while (app->running_state) {
                kiosk_app_remove_window (app, app->running_state->windows->data);
        }

        g_assert (app->running_state == NULL);

        g_clear_weak_pointer (&app->display);
        g_clear_weak_pointer (&app->compositor);

        G_OBJECT_CLASS (kiosk_app_parent_class)->dispose (object);
}

static void
kiosk_app_finalize (GObject *object)
{
        KioskApp *app = KIOSK_APP (object);

        g_free (app->window_id_string);

        G_OBJECT_CLASS (kiosk_app_parent_class)->finalize (object);
}

static void
kiosk_app_class_init (KioskAppClass *klass)
{
        GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

        gobject_class->constructed = kiosk_app_constructed;
        gobject_class->get_property = kiosk_app_get_property;
        gobject_class->set_property = kiosk_app_set_property;
        gobject_class->dispose = kiosk_app_dispose;
        gobject_class->finalize = kiosk_app_finalize;

        kiosk_app_signals[WINDOWS_CHANGED] = g_signal_new ("windows-changed",
                                                           KIOSK_TYPE_APP,
                                                           G_SIGNAL_RUN_LAST,
                                                           0, NULL, NULL,
                                                           NULL, G_TYPE_NONE,
                                                           0);

        props[PROP_COMPOSITOR] = g_param_spec_object ("compositor",
                                                      "compositor",
                                                      "compositor",
                                                      KIOSK_TYPE_COMPOSITOR,
                                                      G_PARAM_CONSTRUCT_ONLY
                                                      | G_PARAM_WRITABLE
                                                      | G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_NICK |
                                                      G_PARAM_STATIC_BLURB);
        /**
         * KioskApp:state:
         *
         * The high-level state of the application, effectively whether it's
         * running or not, or transitioning between those states.
         */
        props[PROP_STATE] =
                g_param_spec_enum ("state",
                                   "State",
                                   "Application state",
                                   KIOSK_TYPE_APP_STATE,
                                   KIOSK_APP_STATE_STOPPED,
                                   G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

        /**
         * KioskApp:id:
         *
         * The id of this application (a desktop filename, or a special string
         * like window:0xabcd1234)
         */
        props[PROP_ID] =
                g_param_spec_string ("id",
                                     "Application id",
                                     "The desktop file id of this KioskApp",
                                     NULL,
                                     G_PARAM_READABLE |
                                     G_PARAM_STATIC_STRINGS);

        /**
         * KioskApp:app-info:
         *
         * The #GDesktopAppInfo associated with this KioskApp, if any.
         */
        props[PROP_APP_INFO] =
                g_param_spec_object ("app-info",
                                     "DesktopAppInfo",
                                     "The DesktopAppInfo associated with this app",
                                     G_TYPE_DESKTOP_APP_INFO,
                                     G_PARAM_READWRITE |
                                     G_PARAM_CONSTRUCT_ONLY |
                                     G_PARAM_STATIC_STRINGS);

        g_object_class_install_properties (gobject_class, N_PROPS, props);
}
