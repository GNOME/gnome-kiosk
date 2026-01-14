#pragma once

#include <glib-object.h>
#include <meta/meta-external-constraint.h>
#include <mtk/mtk-rectangle.h>

G_BEGIN_DECLS

typedef struct _KioskCompositor KioskCompositor;

#define KIOSK_TYPE_AREA_CONSTRAINT (kiosk_area_constraint_get_type ())
G_DECLARE_FINAL_TYPE (KioskAreaConstraint, kiosk_area_constraint,
                      KIOSK, AREA_CONSTRAINT, GObject)

KioskAreaConstraint *kiosk_area_constraint_new (KioskCompositor * compositor,
                                                MtkRectangle * area);

G_END_DECLS
