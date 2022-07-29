#pragma once

#include <glib-object.h>

#include "org.gnome.Shell.h"

typedef struct _KioskCompositor KioskCompositor;

G_BEGIN_DECLS

#define KIOSK_TYPE_SHELL_SERVICE (kiosk_shell_service_get_type ())

G_DECLARE_FINAL_TYPE (KioskShellService,
                      kiosk_shell_service,
                      KIOSK, SHELL_SERVICE,
                      KioskShellDBusServiceSkeleton);

KioskShellService *kiosk_shell_service_new (KioskCompositor *compositor);
gboolean kiosk_shell_service_start (KioskShellService *service,
                                    GError           **error);
void kiosk_shell_service_stop (KioskShellService *service);

G_END_DECLS
