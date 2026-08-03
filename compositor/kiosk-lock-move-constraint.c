#include "config.h"

#include "kiosk-lock-move-constraint.h"

#include <meta/meta-external-constraint.h>

struct _KioskLockMoveConstraint
{
        GObject parent;
};

static void kiosk_lock_move_constraint_iface_init (MetaExternalConstraintInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (KioskLockMoveConstraint, kiosk_lock_move_constraint, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (META_TYPE_EXTERNAL_CONSTRAINT,
                                                      kiosk_lock_move_constraint_iface_init));

static gboolean
kiosk_lock_move_constraint_constrain (MetaExternalConstraint     *constraint,
                                      MetaWindow                 *window,
                                      MetaExternalConstraintInfo *info)
{
        MtkRectangle current_rect;
        MtkRectangle *new_rect = info->new_rect;

        if (!(info->flags & META_EXTERNAL_CONSTRAINT_FLAGS_MOVE) &&
            !(info->flags & META_EXTERNAL_CONSTRAINT_FLAGS_RESIZE))
                return TRUE;

        meta_window_get_frame_rect (window, &current_rect);
        g_debug ("KioskLockMoveConstraint: Locking move for window %s at (%i,%i)",
                 meta_window_get_description (window),
                 current_rect.x, current_rect.y);

        if (info->flags & META_EXTERNAL_CONSTRAINT_FLAGS_RESIZE) {
                if (new_rect->x != current_rect.x) {
                        new_rect->x = current_rect.x;
                        new_rect->width = current_rect.width;
                }

                if (new_rect->y != current_rect.y) {
                        new_rect->y = current_rect.y;
                        new_rect->height = current_rect.height;
                }
        }

        if (info->flags & META_EXTERNAL_CONSTRAINT_FLAGS_MOVE) {
                new_rect->x = current_rect.x;
                new_rect->y = current_rect.y;
        }

        return TRUE;
}

static void
kiosk_lock_move_constraint_iface_init (MetaExternalConstraintInterface *iface)
{
        iface->constrain = kiosk_lock_move_constraint_constrain;
}

static void
kiosk_lock_move_constraint_class_init (KioskLockMoveConstraintClass *klass)
{
}

static void
kiosk_lock_move_constraint_init (KioskLockMoveConstraint *self)
{
}

KioskLockMoveConstraint *
kiosk_lock_move_constraint_new (void)
{
        return g_object_new (KIOSK_TYPE_LOCK_MOVE_CONSTRAINT, NULL);
}
