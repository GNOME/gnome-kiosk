#pragma once

#include <glib-object.h>
#include "org.gnome.Kiosk.h"

typedef struct _KioskCompositor KioskCompositor;

G_BEGIN_DECLS

#define KIOSK_TYPE_SERVICE (kiosk_service_get_type ())

G_DECLARE_FINAL_TYPE (KioskService,
                      kiosk_service,
                      KIOSK, SERVICE,
                      GObject);

KioskService *kiosk_service_new (KioskCompositor *compositor);

gboolean kiosk_service_start (KioskService *self,
                              GError      **error);
void kiosk_service_stop (KioskService *self);

KioskDBusInputSourcesManagerSkeleton *kiosk_service_get_input_sources_manager_skeleton (KioskService *self);
GDBusObjectManagerServer *kiosk_service_get_input_sources_object_manager (KioskService *self);

G_END_DECLS
