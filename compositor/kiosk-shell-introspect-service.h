#pragma once

#include <glib-object.h>

#include "org.gnome.Shell.Introspect.h"

typedef struct _KioskCompositor KioskCompositor;

G_BEGIN_DECLS

#define KIOSK_TYPE_SHELL_INTROSPECT_SERVICE (kiosk_shell_introspect_service_get_type ())

G_DECLARE_FINAL_TYPE (KioskShellIntrospectService,
                      kiosk_shell_introspect_service,
                      KIOSK, SHELL_INTROSPECT_SERVICE,
                      KioskShellIntrospectDBusServiceSkeleton);

KioskShellIntrospectService *kiosk_shell_introspect_service_new (KioskCompositor *compositor);
gboolean kiosk_shell_introspect_service_start (KioskShellIntrospectService *service,
                                               GError                     **error);
void kiosk_shell_introspect_service_stop (KioskShellIntrospectService *service);

G_END_DECLS
