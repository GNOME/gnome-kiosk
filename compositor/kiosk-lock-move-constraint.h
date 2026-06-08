#pragma once

#include <glib-object.h>
#include <meta/meta-external-constraint.h>

G_BEGIN_DECLS

#define KIOSK_TYPE_LOCK_MOVE_CONSTRAINT (kiosk_lock_move_constraint_get_type ())
G_DECLARE_FINAL_TYPE (KioskLockMoveConstraint, kiosk_lock_move_constraint,
                      KIOSK, LOCK_MOVE_CONSTRAINT, GObject);

KioskLockMoveConstraint *kiosk_lock_move_constraint_new (void);

G_END_DECLS
