#include "config.h"
#include "kiosk-compositor.h"

#include <stdlib.h>
#include <string.h>

#include <glib-object.h>

#include <clutter/clutter.h>
#include <clutter/x11/clutter-x11.h>
#include <meta/display.h>
#include <meta/main.h>
#include <meta/util.h>

#include "kiosk-backgrounds.h"
#include "kiosk-input-sources-manager.h"

#include "org.gnome.DisplayManager.Manager.h"

struct _KioskCompositor
{
        MetaPlugin parent;

        /* weak references */
        MetaDisplay *display;
        ClutterBackend *backend;
        ClutterActor *stage;

        /* strong references */
        GCancellable *cancellable;
        KioskBackgrounds *backgrounds;
        KioskInputSourcesManager *input_sources_manager;
};

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

        g_clear_weak_pointer (&self->stage);
        g_clear_weak_pointer (&self->display);
        g_clear_weak_pointer (&self->backend);

        g_clear_object (&self->backgrounds);

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
register_session (KioskCompositor *self)
{
        meta_register_with_session ();

        register_with_display_manager (self);
}

static void
kiosk_compositor_start (MetaPlugin *plugin)
{
        KioskCompositor *self = KIOSK_COMPOSITOR (plugin);
        g_autoptr (GError) error = NULL;

        g_set_weak_pointer (&self->display, meta_plugin_get_display (META_PLUGIN (self)));
        g_set_weak_pointer (&self->backend, clutter_get_default_backend ());
        g_set_weak_pointer (&self->stage, meta_get_stage_for_display (self->display));

        clutter_actor_show (self->stage);

        self->cancellable = g_cancellable_new ();
        self->backgrounds = kiosk_backgrounds_new (self);
        self->input_sources_manager = kiosk_input_sources_manager_new (self);

        register_session (self);
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
                              MetaRectangle   *old_frame_rect,
                              MetaRectangle   *old_buffer_rect)
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
kiosk_compositor_map (MetaPlugin      *plugin,
                      MetaWindowActor *actor)
{
        KioskCompositor *self = KIOSK_COMPOSITOR (plugin);
        MetaWindow *window;
        ClutterTransition *fade_in_transition;
        int easing_duration;

        window = meta_window_actor_get_meta_window (actor);

        if (meta_window_allows_resize (window)) {
                meta_window_make_fullscreen (window);
                easing_duration = 3000;
        } else {
                if (!meta_window_is_override_redirect (window)) {
                        meta_window_make_above (window);
                }
                easing_duration = 500;
        }

        clutter_actor_show (self->stage);
        clutter_actor_show (CLUTTER_ACTOR (actor));

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
kiosk_compositor_show_tile_preview (MetaPlugin    *plugin,
                                    MetaWindow    *window,
                                    MetaRectangle *tile_rect,
                                    int            tile_monitor)
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
kiosk_compositor_show_window_menu_for_rect (MetaPlugin *plugin,
                                            MetaWindow *window,
                                            MetaWindowMenuType menu,
                                            MetaRectangle *rect)
{
        g_assert (META_PLUGIN_CLASS (kiosk_compositor_parent_class)->show_window_menu_for_rect == NULL);
}

static gboolean
kiosk_compositor_xevent_filter (MetaPlugin *plugin,
                                XEvent     *xev)
{
        return FALSE;
}

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

static const MetaPluginInfo info = {
        .name = "GNOME Kiosk",
        .version = VERSION,
        .author = "Various",
        .license = "GPLv2+",
        .description = "Provides Kiosk compositor plugin for mutter"
};

static const MetaPluginInfo *
kiosk_compositor_plugin_info (MetaPlugin *plugin)
{

        return &info;
}

static MetaCloseDialog *
kiosk_compositor_create_close_dialog (MetaPlugin *plugin,
                                      MetaWindow *window)
{
        return NULL;
}

static MetaInhibitShortcutsDialog *
kiosk_compositor_create_inhibit_shortcuts_dialog (MetaPlugin *plugin,
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

        plugin_class->xevent_filter = kiosk_compositor_xevent_filter;
        plugin_class->keybinding_filter = kiosk_compositor_keybinding_filter;

        plugin_class->confirm_display_change = kiosk_compositor_confirm_display_change;

        plugin_class->plugin_info = kiosk_compositor_plugin_info;

        plugin_class->create_close_dialog = kiosk_compositor_create_close_dialog;
        plugin_class->create_inhibit_shortcuts_dialog = kiosk_compositor_create_inhibit_shortcuts_dialog;

        plugin_class->locate_pointer = kiosk_compositor_locate_pointer;
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
