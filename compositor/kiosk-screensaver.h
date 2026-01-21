#pragma once

#include <glib-object.h>

typedef struct _KioskCompositor KioskCompositor;

G_BEGIN_DECLS

#define KIOSK_TYPE_SCREENSAVER (kiosk_screensaver_get_type ())

G_DECLARE_FINAL_TYPE (KioskScreensaver,
                      kiosk_screensaver,
                      KIOSK, SCREENSAVER,
                      GObject);

KioskScreensaver *kiosk_screensaver_new (KioskCompositor *compositor);
void     kiosk_screensaver_activate (KioskScreensaver *self);
void     kiosk_screensaver_deactivate (KioskScreensaver *self);
void     kiosk_screensaver_lock (KioskScreensaver *self);
gboolean kiosk_screensaver_get_active (KioskScreensaver *self);
gboolean kiosk_screensaver_get_locked (KioskScreensaver *self);
guint32  kiosk_screensaver_get_active_time (KioskScreensaver *self);

G_END_DECLS
