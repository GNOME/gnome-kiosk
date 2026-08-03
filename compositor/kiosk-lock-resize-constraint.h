#pragma once

#include <glib-object.h>
#include <meta/meta-external-constraint.h>

G_BEGIN_DECLS

#define KIOSK_TYPE_LOCK_RESIZE_CONSTRAINT (kiosk_lock_resize_constraint_get_type ())
G_DECLARE_FINAL_TYPE (KioskLockResizeConstraint, kiosk_lock_resize_constraint,
                      KIOSK, LOCK_RESIZE_CONSTRAINT, GObject);

KioskLockResizeConstraint *kiosk_lock_resize_constraint_new (void);

G_END_DECLS
