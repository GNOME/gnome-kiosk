#pragma once

#include <glib-object.h>
#include <meta/meta-external-constraint.h>

G_BEGIN_DECLS

typedef struct _KioskCompositor KioskCompositor;

#define KIOSK_TYPE_MONITOR_CONSTRAINT (kiosk_monitor_constraint_get_type ())
G_DECLARE_FINAL_TYPE (KioskMonitorConstraint, kiosk_monitor_constraint,
                      KIOSK, MONITOR_CONSTRAINT, GObject)

KioskMonitorConstraint *kiosk_monitor_constraint_new (KioskCompositor * compositor);

G_END_DECLS
