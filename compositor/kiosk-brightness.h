#pragma once

#include <glib-object.h>

#include "org.gnome.Shell.Brightness.h"

typedef struct _KioskCompositor KioskCompositor;

G_BEGIN_DECLS

#define KIOSK_TYPE_BRIGHTNESS (kiosk_brightness_get_type ())

G_DECLARE_FINAL_TYPE (KioskBrightness,
                      kiosk_brightness,
                      KIOSK, BRIGHTNESS,
                      KioskShellBrightnessDBusServiceSkeleton);

KioskBrightness *kiosk_brightness_new (KioskCompositor *compositor);
gboolean kiosk_brightness_start (KioskBrightness *service,
                                 GError         **error);
void kiosk_brightness_stop (KioskBrightness *service);

G_END_DECLS
