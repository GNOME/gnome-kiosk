#pragma once

#include <gio/gio.h>
#include <clutter/clutter.h>
#include <meta/window.h>

#include "kiosk-app.h"

typedef struct _KioskCompositor KioskCompositor;

#define KIOSK_TYPE_APP_SYSTEM (kiosk_app_system_get_type ())
G_DECLARE_FINAL_TYPE (KioskAppSystem, kiosk_app_system, KIOSK, APP_SYSTEM, GObject)

/* App iterator */
typedef struct _KioskAppSystemAppIter
{
        GHashTableIter hash_iter;
} KioskAppSystemAppIter;

void     kiosk_app_system_app_iter_init (KioskAppSystemAppIter *iter,
                                         KioskAppSystem        *system);
gboolean kiosk_app_system_app_iter_next (KioskAppSystemAppIter *iter,
                                         KioskApp             **app);

KioskApp *kiosk_app_system_lookup_app (KioskAppSystem *system,
                                       const char     *id);
KioskApp *kiosk_app_system_lookup_desktop_wmclass (KioskAppSystem *system,
                                                   const char     *wmclass);
void            kiosk_app_system_notify_app_state_changed (KioskAppSystem *system,
                                                           KioskApp       *app);
KioskAppSystem *kiosk_app_system_new (KioskCompositor *compositor);
