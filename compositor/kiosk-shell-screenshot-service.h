#pragma once

#include <glib-object.h>

#include "org.gnome.Shell.Screenshot.h"

typedef struct _KioskCompositor KioskCompositor;

G_BEGIN_DECLS

#define KIOSK_TYPE_SHELL_SCREENSHOT_SERVICE (kiosk_shell_screenshot_service_get_type ())

G_DECLARE_FINAL_TYPE (KioskShellScreenshotService,
                      kiosk_shell_screenshot_service,
                      KIOSK, SHELL_SCREENSHOT_SERVICE,
                      KioskShellScreenshotDBusServiceSkeleton);

KioskShellScreenshotService *kiosk_shell_screenshot_service_new (KioskCompositor *compositor);
gboolean kiosk_shell_screenshot_service_start (KioskShellScreenshotService *service,
                                               GError                     **error);
void kiosk_shell_screenshot_service_stop (KioskShellScreenshotService *service);

G_END_DECLS
