#pragma once

#include <glib-object.h>

typedef struct _KioskCompositor KioskCompositor;

G_BEGIN_DECLS

#define KIOSK_TYPE_MAGNIFIER (kiosk_magnifier_get_type ())

G_DECLARE_FINAL_TYPE (KioskMagnifier,
                      kiosk_magnifier,
                      KIOSK, MAGNIFIER,
                      GObject);

KioskMagnifier *kiosk_magnifier_new (KioskCompositor *compositor);

G_END_DECLS
