/* -*- c-basic-offset: 8; c-ts-mode-indent-offset: 8; indent-tabs-mode: nil; -*- */
/* kiosk-automount-manager.c
 *
 * Copyright 2023 Mohammed Sadiq
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include "kiosk-compositor.h"
#include "kiosk-gobject-utils.h"
#include "kiosk-automount-manager.h"

#define KIOSK_MEDIA_HANDLING_SCHEMA "org.gnome.desktop.media-handling"
#define KIOSK_MEDIA_AUTOMOUNT_SETTING "automount"

struct _KioskAutomountManager
{
        GObject          parent;

        /* weak references */
        KioskCompositor *compositor;

        /* strong references */
        GVolumeMonitor  *volume_monitor;
        GSettings       *media_handling_settings;
        GCancellable    *cancellable;

        /* signal ids */
        gulong           automount_id;
};

enum
{
        PROP_COMPOSITOR = 1,
        NUMBER_OF_PROPERTIES
};
static GParamSpec *kiosk_automount_manager_properties[NUMBER_OF_PROPERTIES] = { NULL, };

G_DEFINE_TYPE (KioskAutomountManager, kiosk_automount_manager, G_TYPE_OBJECT)

static void kiosk_automount_manager_set_property (GObject      *object,
                                                  guint         property_id,
                                                  const GValue *value,
                                                  GParamSpec   *param_spec);

static void kiosk_automount_manager_constructed (GObject *object);
static void kiosk_automount_manager_dispose (GObject *object);

KioskAutomountManager *
kiosk_automount_manager_new (KioskCompositor *compositor)
{
        GObject *object;

        object = g_object_new (KIOSK_TYPE_AUTOMOUNT_MANAGER,
                               "compositor", compositor,
                               NULL);

        return KIOSK_AUTOMOUNT_MANAGER (object);
}

static void
kiosk_automount_manager_class_init (KioskAutomountManagerClass *automount_manager_class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (automount_manager_class);

        object_class->constructed = kiosk_automount_manager_constructed;
        object_class->set_property = kiosk_automount_manager_set_property;
        object_class->dispose = kiosk_automount_manager_dispose;

        kiosk_automount_manager_properties[PROP_COMPOSITOR] = g_param_spec_object ("compositor",
                                                                                   "compositor",
                                                                                   "compositor",
                                                                                   KIOSK_TYPE_COMPOSITOR,
                                                                                   G_PARAM_CONSTRUCT_ONLY
                                                                                   | G_PARAM_WRITABLE
                                                                                   | G_PARAM_STATIC_NAME
                                                                                   | G_PARAM_STATIC_NICK
                                                                                   | G_PARAM_STATIC_BLURB);
        g_object_class_install_properties (object_class, NUMBER_OF_PROPERTIES, kiosk_automount_manager_properties);
}

static void
on_volume_added (KioskAutomountManager *self,
                 GVolume               *volume)
{
        g_autoptr (GMount) mount = NULL;

        g_assert (KIOSK_IS_AUTOMOUNT_MANAGER (self));
        g_assert (volume);
        g_assert (G_IS_VOLUME (volume));

        mount = g_volume_get_mount (volume);
        if (mount)
                return;

        if (g_volume_should_automount (volume) &&
            g_volume_can_mount (volume)) {
                g_volume_mount (volume,
                                G_MOUNT_MOUNT_NONE,
                                NULL,
                                self->cancellable,
                                NULL,
                                NULL);
        }

        g_object_unref (volume);
}

static void
on_volume_monitor_changed (KioskAutomountManager *self,
                           GVolume               *volume)
{
        g_assert (KIOSK_IS_AUTOMOUNT_MANAGER (self));
        g_assert (G_IS_VOLUME (volume));

        kiosk_gobject_utils_queue_defer_callback (G_OBJECT (self),
                                                  "[kiosk-automount-manager] on_volume_added",
                                                  self->cancellable,
                                                  KIOSK_OBJECT_CALLBACK (on_volume_added),
                                                  g_object_ref (volume));
}

static void
on_media_automount_changed (KioskAutomountManager *self)
{
        GList *volumes;

        g_assert (KIOSK_IS_AUTOMOUNT_MANAGER (self));

        g_clear_signal_handler (&self->automount_id, self->volume_monitor);

        if (!g_settings_get_boolean (self->media_handling_settings,
                                     KIOSK_MEDIA_AUTOMOUNT_SETTING))
                return;

        self->automount_id = g_signal_connect_object (self->volume_monitor,
                                                      "volume-added",
                                                      G_CALLBACK (on_volume_monitor_changed),
                                                      self,
                                                      G_CONNECT_SWAPPED);
        volumes = g_volume_monitor_get_volumes (self->volume_monitor);

        for (GList *volume = volumes; volume && volume->data; volume = volume->next) {
                on_volume_monitor_changed (self, volume->data);
        }

        g_list_free_full (volumes, g_object_unref);
}

static void
on_media_automount_setting_changed (KioskAutomountManager *self)
{
        g_assert (KIOSK_IS_AUTOMOUNT_MANAGER (self));

        kiosk_gobject_utils_queue_defer_callback (G_OBJECT (self),
                                                  "[kiosk-automount-manager] on_media_automount_changed",
                                                  self->cancellable,
                                                  KIOSK_OBJECT_CALLBACK (on_media_automount_changed),
                                                  NULL);
}

static void
kiosk_automount_manager_handle_automount (KioskAutomountManager *self)
{
        g_signal_connect_object (self->media_handling_settings,
                                 "changed::" KIOSK_MEDIA_AUTOMOUNT_SETTING,
                                 G_CALLBACK (on_media_automount_setting_changed),
                                 self,
                                 G_CONNECT_SWAPPED);
        on_media_automount_setting_changed (self);
}

static void
kiosk_automount_manager_set_property (GObject      *object,
                                      guint         property_id,
                                      const GValue *value,
                                      GParamSpec   *param_spec)
{
        KioskAutomountManager *self = KIOSK_AUTOMOUNT_MANAGER (object);

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
kiosk_automount_manager_constructed (GObject *object)
{
        KioskAutomountManager *self = KIOSK_AUTOMOUNT_MANAGER (object);

        g_debug ("KioskAutomountManager: Initializing");

        G_OBJECT_CLASS (kiosk_automount_manager_parent_class)->constructed (object);

        self->cancellable = g_cancellable_new ();
        self->volume_monitor = g_volume_monitor_get ();
        self->media_handling_settings = g_settings_new (KIOSK_MEDIA_HANDLING_SCHEMA);
        kiosk_automount_manager_handle_automount (self);
}

static void
kiosk_automount_manager_init (KioskAutomountManager *self)
{
}

static void
kiosk_automount_manager_dispose (GObject *object)
{
        KioskAutomountManager *self = KIOSK_AUTOMOUNT_MANAGER (object);

        if (self->cancellable != NULL) {
                g_cancellable_cancel (self->cancellable);
                g_clear_object (&self->cancellable);
        }

        g_clear_signal_handler (&self->automount_id, self->volume_monitor);
        g_clear_object (&self->volume_monitor);
        g_clear_object (&self->media_handling_settings);

        g_clear_weak_pointer (&self->compositor);

        G_OBJECT_CLASS (kiosk_automount_manager_parent_class)->dispose (object);
}
