#pragma once

#include <glib-object.h>
#include <glib.h>
#include <meta/window.h>

#include "kiosk-app.h"
#include "kiosk-app-system.h"

typedef struct _KioskCompositor KioskCompositor;

G_BEGIN_DECLS

#define KIOSK_TYPE_WINDOW_TRACKER (kiosk_window_tracker_get_type ())
G_DECLARE_FINAL_TYPE (KioskWindowTracker, kiosk_window_tracker,
                      KIOSK, WINDOW_TRACKER, GObject)

KioskApp *kiosk_window_tracker_get_focused_app (KioskWindowTracker * tracker);
KioskWindowTracker *kiosk_window_tracker_new (KioskCompositor *compositor,
                                              KioskAppSystem  *app_system);

G_END_DECLS
