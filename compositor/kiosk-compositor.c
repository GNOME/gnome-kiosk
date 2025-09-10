#include "config.h"
#include "kiosk-compositor.h"

#include <stdlib.h>
#include <string.h>

#include <glib-object.h>

#ifdef HAVE_X11
#include <X11/Xlib.h>
#endif

#include <clutter/clutter.h>
#include <meta/common.h>
#include <meta/display.h>
#include <meta/keybindings.h>
#include <meta/meta-backend.h>
#include <meta/meta-context.h>
#include <meta/util.h>
#include <meta/meta-window-config.h>
#include <meta/meta-window-group.h>

#include <systemd/sd-daemon.h>

#include "kiosk-backgrounds.h"
#include "kiosk-gobject-utils.h"
#include "kiosk-input-sources-manager.h"
#include "kiosk-automount-manager.h"
#include "kiosk-service.h"
#include "kiosk-app-system.h"
#include "kiosk-window-tracker.h"
#include "kiosk-shell-service.h"
#include "kiosk-shell-introspect-service.h"
#include "kiosk-shell-screenshot-service.h"
#include "kiosk-window-config.h"

#include "org.gnome.DisplayManager.Manager.h"

#define GNOME_DESKTOP_INTERFACE_SCHEMA "org.gnome.desktop.interface"
#define GNOME_DESKTOP_ENABLE_ANIMATIONS "enable-animations"

struct _KioskCompositor
{
        MetaPlugin                   parent;

        /* weak references */
        MetaDisplay                 *display;
        MetaContext                 *context;
        ClutterBackend              *backend;
        ClutterActor                *stage;

        /* strong references */
        GCancellable                *cancellable;
        KioskBackgrounds            *backgrounds;
        KioskInputSourcesManager    *input_sources_manager;
        KioskAutomountManager       *automount_manager;
        KioskService                *service;
        KioskAppSystem              *app_system;
        KioskWindowTracker          *tracker;
        KioskShellIntrospectService *introspect_service;
        KioskShellScreenshotService *screenshot_service;
        KioskWindowConfig           *kiosk_window_config;
        KioskShellService           *shell_service;
        GSettings                   *interface_settings;
};

enum
{
        X_SERVER_EVENT,
        NUMBER_OF_SIGNALS
};

static guint signals[NUMBER_OF_SIGNALS] = { 0, };

G_DEFINE_TYPE (KioskCompositor, kiosk_compositor, META_TYPE_PLUGIN)

static void kiosk_compositor_dispose (GObject *object);

static void
kiosk_compositor_dispose (GObject *object)
{
        KioskCompositor *self = KIOSK_COMPOSITOR (object);

        if (self->cancellable != NULL) {
                g_cancellable_cancel (self->cancellable);
                g_clear_object (&self->cancellable);
        }

        g_clear_object (&self->backgrounds);
        g_clear_object (&self->automount_manager);
        g_clear_object (&self->input_sources_manager);
        g_clear_object (&self->app_system);
        g_clear_object (&self->tracker);
        g_clear_object (&self->kiosk_window_config);
        g_clear_object (&self->introspect_service);
        g_clear_object (&self->screenshot_service);
        g_clear_object (&self->shell_service);
        g_clear_object (&self->interface_settings);
        g_clear_object (&self->service);

        g_clear_weak_pointer (&self->display);
        g_clear_weak_pointer (&self->context);
        g_clear_weak_pointer (&self->backend);
        g_clear_weak_pointer (&self->stage);

        G_OBJECT_CLASS (kiosk_compositor_parent_class)->dispose (object);
}

static void
register_with_display_manager (KioskCompositor *self)
{
        g_autoptr (GDBusConnection) system_bus = NULL;
        g_autoptr (GdmManager) display_manager = NULL;
        GVariantBuilder builder;
        g_autoptr (GError) error = NULL;
        g_autoptr (GVariant) reply = NULL;

        system_bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM,
                                     self->cancellable,
                                     &error);
        if (error != NULL) {
                g_debug ("KioskCompositor: Could not contact system bus: %s",
                         error->message);
                return;
        }

        display_manager = gdm_manager_proxy_new_sync (system_bus,
                                                      G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
                                                      G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                      "org.gnome.DisplayManager",
                                                      "/org/gnome/DisplayManager/Manager",
                                                      self->cancellable,
                                                      &error);

        if (error != NULL) {
                g_debug ("KioskCompositor: Could not contact display manager: %s",
                         error->message);
                return;
        }

        g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));

        gdm_manager_call_register_display_sync (display_manager,
                                                g_variant_builder_end (&builder),
                                                self->cancellable,
                                                &error);

        if (error != NULL) {
                g_debug ("KioskCompositor: Could not register with display manager: %s",
                         error->message);
                return;
        }
}

static void
register_with_systemd (KioskCompositor *self)
{
        sd_notify (TRUE, "READY=1");
}

static void
register_session (KioskCompositor *self)
{
        meta_context_notify_ready (self->context);

        register_with_display_manager (self);

        register_with_systemd (self);
}

static void
on_builtin_keybinding_triggered (MetaDisplay     *display,
                                 MetaWindow      *window,
                                 ClutterKeyEvent *event,
                                 MetaKeyBinding  *binding,
                                 KioskCompositor *self)
{
        g_debug ("KioskCompositor: Ignoring '%s' request",
                 meta_key_binding_get_name (binding));
}

static void
neuter_builtin_keybindings (KioskCompositor *self)
{
        const char *builtin_keybindings[] = {
                "switch-to-workspace-1",
                "switch-to-workspace-2",
                "switch-to-workspace-3",
                "switch-to-workspace-4",
                "switch-to-workspace-5",
                "switch-to-workspace-6",
                "switch-to-workspace-7",
                "switch-to-workspace-8",
                "switch-to-workspace-9",
                "switch-to-workspace-10",
                "switch-to-workspace-11",
                "switch-to-workspace-12",
                "switch-to-workspace-left",
                "switch-to-workspace-right",
                "switch-to-workspace-up",
                "switch-to-workspace-down",
                "switch-to-workspace-last",
                "panel-main-menu",
                "panel-run-dialog",
                "set-spew-mark",
                "switch-monitor",
                "rotate-monitor",
                "restore-shortcuts",
                "activate-window-menu",
                "toggle-above",
                "toggle-shaded",
                "minimize",
                "toggle-on-all-workspaces",
                "move-to-workspace-1",
                "move-to-workspace-2",
                "move-to-workspace-3",
                "move-to-workspace-4",
                "move-to-workspace-5",
                "move-to-workspace-6",
                "move-to-workspace-7",
                "move-to-workspace-8",
                "move-to-workspace-9",
                "move-to-workspace-10",
                "move-to-workspace-11",
                "move-to-workspace-12",
                "move-to-workspace-last",
                "move-to-workspace-left",
                "move-to-workspace-right",
                "move-to-workspace-up",
                "move-to-workspace-down",
                NULL
        };
        size_t i;

        g_debug ("KioskCompositor: Neutering builtin keybindings");

        for (i = 0; builtin_keybindings[i] != NULL; i++) {
                meta_keybindings_set_custom_handler (builtin_keybindings[i],
                                                     (MetaKeyHandlerFunc)
                                                     on_builtin_keybinding_triggered,
                                                     self,
                                                     NULL);
        }
}

static void
kiosk_compositor_start (MetaPlugin *plugin)
{
        KioskCompositor *self = KIOSK_COMPOSITOR (plugin);
        g_autoptr (GError) error = NULL;
        MetaDisplay *display = meta_plugin_get_display (META_PLUGIN (self));
        MetaCompositor *compositor = meta_display_get_compositor (display);

        g_set_weak_pointer (&self->display, display);
        g_set_weak_pointer (&self->context, meta_display_get_context (self->display));
        g_set_weak_pointer (&self->backend, clutter_get_default_backend ());
        g_set_weak_pointer (&self->stage, CLUTTER_ACTOR (meta_compositor_get_stage (compositor)));

        clutter_actor_show (self->stage);

        self->cancellable = g_cancellable_new ();

        self->service = kiosk_service_new (self);
        kiosk_service_start (self->service, &error);

        if (error != NULL) {
                g_debug ("KioskCompositor: Could not start D-Bus service: %s", error->message);
                g_clear_error (&error);
        }

        neuter_builtin_keybindings (self);

        self->backgrounds = kiosk_backgrounds_new (self);
        self->automount_manager = kiosk_automount_manager_new (self);
        self->input_sources_manager = kiosk_input_sources_manager_new (self);
        self->app_system = kiosk_app_system_new (self);
        self->tracker = kiosk_window_tracker_new (self, self->app_system);
        self->kiosk_window_config = kiosk_window_config_new (self);
        self->introspect_service = kiosk_shell_introspect_service_new (self);
        kiosk_shell_introspect_service_start (self->introspect_service, &error);
        self->screenshot_service = kiosk_shell_screenshot_service_new (self);
        kiosk_shell_screenshot_service_start (self->screenshot_service, &error);
        self->shell_service = kiosk_shell_service_new (self);
        kiosk_shell_service_start (self->shell_service, &error);

        if (error != NULL) {
                g_debug ("KioskCompositor: Could not start D-Bus service: %s", error->message);
                g_clear_error (&error);
        }

        self->interface_settings = g_settings_new (GNOME_DESKTOP_INTERFACE_SCHEMA);

        kiosk_gobject_utils_queue_immediate_callback (G_OBJECT (self),
                                                      "[kiosk-compositor] register_session",
                                                      self->cancellable,
                                                      KIOSK_OBJECT_CALLBACK (register_session),
                                                      NULL);
}

static void
kiosk_compositor_minimize (MetaPlugin      *plugin,
                           MetaWindowActor *actor)
{
        meta_plugin_minimize_completed (plugin, actor);
}

static void
kiosk_compositor_unminimize (MetaPlugin      *plugin,
                             MetaWindowActor *actor)
{
        meta_plugin_unminimize_completed (plugin, actor);
}

static void
kiosk_compositor_size_changed (MetaPlugin      *plugin,
                               MetaWindowActor *actor)
{
        g_assert (META_PLUGIN_CLASS (kiosk_compositor_parent_class)->size_changed == NULL);
}

static void
kiosk_compositor_size_change (MetaPlugin      *plugin,
                              MetaWindowActor *actor,
                              MetaSizeChange   which_change,
                              MtkRectangle    *old_frame_rect,
                              MtkRectangle    *old_buffer_rect)
{
        g_assert (META_PLUGIN_CLASS (kiosk_compositor_parent_class)->size_change == NULL);
}

static void
on_faded_in (KioskCompositor   *self,
             ClutterTransition *transition)
{
        MetaWindowActor *actor = g_object_get_data (G_OBJECT (transition), "actor");

        meta_plugin_map_completed (META_PLUGIN (self), actor);
}

static void
kiosk_compositor_map_immediately (KioskCompositor *self,
                                  MetaWindowActor *actor)
{
        g_debug ("KioskCompositor: Animations disabled, mapping window immediately");
        clutter_actor_set_opacity (CLUTTER_ACTOR (actor), 255);
        meta_plugin_map_completed (META_PLUGIN (self), actor);
}

static void
kiosk_compositor_map_with_fade_in (KioskCompositor *self,
                                   MetaWindowActor *actor,
                                   MetaWindow      *window)
{
        ClutterTransition *fade_in_transition;
        int easing_duration;

        if (meta_window_is_fullscreen (window)) {
                g_debug ("KioskCompositor: Mapping window that does need to be fullscreened");
                easing_duration = 3000;
        } else {
                g_debug ("KioskCompositor: Mapping window that does not need to be fullscreened");
                easing_duration = 500;
        }

        clutter_actor_set_opacity (CLUTTER_ACTOR (actor), 0);

        clutter_actor_save_easing_state (CLUTTER_ACTOR (actor));
        clutter_actor_set_easing_duration (CLUTTER_ACTOR (actor), easing_duration);
        clutter_actor_set_easing_mode (CLUTTER_ACTOR (actor), CLUTTER_EASE_IN_OUT_QUINT);
        clutter_actor_set_opacity (CLUTTER_ACTOR (actor), 255);
        fade_in_transition = clutter_actor_get_transition (CLUTTER_ACTOR (actor), "opacity");
        clutter_actor_restore_easing_state (CLUTTER_ACTOR (actor));

        g_object_set_data (G_OBJECT (fade_in_transition), "actor", actor);

        g_signal_connect_object (G_OBJECT (fade_in_transition),
                                 "completed",
                                 G_CALLBACK (on_faded_in),
                                 self,
                                 G_CONNECT_SWAPPED);
}

static void
kiosk_compositor_map (MetaPlugin      *plugin,
                      MetaWindowActor *actor)
{
        KioskCompositor *self = KIOSK_COMPOSITOR (plugin);
        MetaWindow *window;
        gboolean animations_enabled;

        window = meta_window_actor_get_meta_window (actor);

        kiosk_window_config_apply_initial_config (self->kiosk_window_config, window);

        clutter_actor_show (self->stage);
        clutter_actor_show (CLUTTER_ACTOR (actor));

        animations_enabled = g_settings_get_boolean (self->interface_settings,
                                                     GNOME_DESKTOP_ENABLE_ANIMATIONS);

        if (animations_enabled) {
                kiosk_compositor_map_with_fade_in (self, actor, window);
        } else {
                kiosk_compositor_map_immediately (self, actor);
        }
}

static void
kiosk_compositor_destroy (MetaPlugin      *plugin,
                          MetaWindowActor *actor)
{
        KioskCompositor *self = KIOSK_COMPOSITOR (plugin);

        clutter_actor_hide (CLUTTER_ACTOR (actor));

        meta_plugin_destroy_completed (META_PLUGIN (self), actor);
}

static void
kiosk_compositor_switch_workspace (MetaPlugin          *plugin,
                                   gint                 from,
                                   gint                 to,
                                   MetaMotionDirection  direction)
{
        KioskCompositor *self = KIOSK_COMPOSITOR (plugin);

        meta_plugin_switch_workspace_completed (META_PLUGIN (self));
}

static void
kiosk_compositor_kill_window_effects (MetaPlugin      *plugin,
                                      MetaWindowActor *actor)
{
}

static void
kiosk_compositor_kill_switch_workspace (MetaPlugin *plugin)
{
}

static void
kiosk_compositor_show_tile_preview (MetaPlugin   *plugin,
                                    MetaWindow   *window,
                                    MtkRectangle *tile_rect,
                                    int           tile_monitor)
{
        g_assert (META_PLUGIN_CLASS (kiosk_compositor_parent_class)->show_tile_preview == NULL);
}

static void
kiosk_compositor_hide_tile_preview (MetaPlugin *plugin)
{
        g_assert (META_PLUGIN_CLASS (kiosk_compositor_parent_class)->hide_tile_preview == NULL);
}

static void
kiosk_compositor_show_window_menu (MetaPlugin         *plugin,
                                   MetaWindow         *window,
                                   MetaWindowMenuType  menu,
                                   int                 x,
                                   int                 y)
{
        g_assert (META_PLUGIN_CLASS (kiosk_compositor_parent_class)->show_window_menu == NULL);
}

static void
kiosk_compositor_show_window_menu_for_rect (MetaPlugin         *plugin,
                                            MetaWindow         *window,
                                            MetaWindowMenuType  menu,
                                            MtkRectangle       *rect)
{
        g_assert (META_PLUGIN_CLASS (kiosk_compositor_parent_class)->show_window_menu_for_rect == NULL);
}

#ifdef HAVE_X11
static gboolean
kiosk_compositor_xevent_filter (MetaPlugin *plugin,
                                XEvent     *x_server_event)
{
        KioskCompositor *self = KIOSK_COMPOSITOR (plugin);

        g_signal_emit (G_OBJECT (self), signals[X_SERVER_EVENT], 0, x_server_event);
        return FALSE;
}
#endif /* HAVE_X11 */

static gboolean
kiosk_compositor_keybinding_filter (MetaPlugin     *plugin,
                                    MetaKeyBinding *binding)
{
        return FALSE;
}

static void
kiosk_compositor_confirm_display_change (MetaPlugin *plugin)
{
        KioskCompositor *self = KIOSK_COMPOSITOR (plugin);

        meta_plugin_complete_display_change (META_PLUGIN (self), TRUE);
}

static MetaCloseDialog *
kiosk_compositor_create_close_dialog (MetaPlugin *plugin,
                                      MetaWindow *window)
{
        return NULL;
}

static void
kiosk_compositor_locate_pointer (MetaPlugin *plugin)
{
}

static void
kiosk_compositor_class_init (KioskCompositorClass *compositor_class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (compositor_class);
        MetaPluginClass *plugin_class = META_PLUGIN_CLASS (compositor_class);

        object_class->dispose = kiosk_compositor_dispose;

        plugin_class->start = kiosk_compositor_start;
        plugin_class->map = kiosk_compositor_map;
        plugin_class->minimize = kiosk_compositor_minimize;
        plugin_class->unminimize = kiosk_compositor_unminimize;
        plugin_class->size_changed = kiosk_compositor_size_changed;
        plugin_class->size_change = kiosk_compositor_size_change;
        plugin_class->destroy = kiosk_compositor_destroy;

        plugin_class->switch_workspace = kiosk_compositor_switch_workspace;

        plugin_class->kill_window_effects = kiosk_compositor_kill_window_effects;
        plugin_class->kill_switch_workspace = kiosk_compositor_kill_switch_workspace;

        plugin_class->show_tile_preview = kiosk_compositor_show_tile_preview;
        plugin_class->hide_tile_preview = kiosk_compositor_hide_tile_preview;
        plugin_class->show_window_menu = kiosk_compositor_show_window_menu;
        plugin_class->show_window_menu_for_rect = kiosk_compositor_show_window_menu_for_rect;

#ifdef HAVE_X11
        plugin_class->xevent_filter = kiosk_compositor_xevent_filter;
#endif /* HAVE_X11 */
        plugin_class->keybinding_filter = kiosk_compositor_keybinding_filter;

        plugin_class->confirm_display_change = kiosk_compositor_confirm_display_change;

        plugin_class->create_close_dialog = kiosk_compositor_create_close_dialog;

        plugin_class->locate_pointer = kiosk_compositor_locate_pointer;

        signals [X_SERVER_EVENT] =
                g_signal_new ("x-server-event",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__POINTER,
                              G_TYPE_NONE,
                              1, G_TYPE_POINTER);
}

static void
kiosk_compositor_init (KioskCompositor *compositor)
{
        g_debug ("KioskCompositor: Initializing");
}

KioskBackgrounds *
kiosk_compositor_get_backgrounds (KioskCompositor *self)
{
        g_return_val_if_fail (KIOSK_IS_COMPOSITOR (self), NULL);

        return KIOSK_BACKGROUNDS (self->backgrounds);
}

KioskInputSourcesManager *
kiosk_compositor_get_input_sources_manager (KioskCompositor *self)
{
        g_return_val_if_fail (KIOSK_IS_COMPOSITOR (self), NULL);

        return KIOSK_INPUT_SOURCES_MANAGER (self->input_sources_manager);
}

KioskService *
kiosk_compositor_get_service (KioskCompositor *self)
{
        g_return_val_if_fail (KIOSK_IS_COMPOSITOR (self), NULL);

        return KIOSK_SERVICE (self->service);
}

KioskAppSystem *
kiosk_compositor_get_app_system (KioskCompositor *self)
{
        g_return_val_if_fail (KIOSK_IS_COMPOSITOR (self), NULL);

        return KIOSK_APP_SYSTEM (self->app_system);
}

KioskWindowTracker *
kiosk_compositor_get_window_tracker (KioskCompositor *self)
{
        g_return_val_if_fail (KIOSK_IS_COMPOSITOR (self), NULL);

        return KIOSK_WINDOW_TRACKER (self->tracker);
}
