#pragma once

#include <glib-object.h>

#include "org.gnome.ScreenSaver.h"

typedef struct _KioskCompositor KioskCompositor;

G_BEGIN_DECLS

#define KIOSK_TYPE_SCREENSAVER_SERVICE (kiosk_screensaver_service_get_type ())

G_DECLARE_FINAL_TYPE (KioskScreenSaverService,
                      kiosk_screensaver_service,
                      KIOSK, SCREENSAVER_SERVICE,
                      KioskScreenSaverSkeleton);

KioskScreenSaverService *kiosk_screensaver_service_new (KioskCompositor *compositor);
gboolean kiosk_screensaver_service_start (KioskScreenSaverService *service,
                                          GError                 **error);
void kiosk_screensaver_service_stop (KioskScreenSaverService *service);

G_END_DECLS
