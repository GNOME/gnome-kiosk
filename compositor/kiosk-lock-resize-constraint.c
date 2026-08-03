#include "config.h"

#include "kiosk-lock-resize-constraint.h"

#include <meta/meta-external-constraint.h>

struct _KioskLockResizeConstraint
{
        GObject parent;
};

static void kiosk_lock_resize_constraint_iface_init (MetaExternalConstraintInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (KioskLockResizeConstraint, kiosk_lock_resize_constraint, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (META_TYPE_EXTERNAL_CONSTRAINT,
                                                      kiosk_lock_resize_constraint_iface_init));

static gboolean
kiosk_lock_resize_constraint_constrain (MetaExternalConstraint     *constraint,
                                        MetaWindow                 *window,
                                        MetaExternalConstraintInfo *info)
{
        MtkRectangle current_rect;
        MtkRectangle *new_rect = info->new_rect;

        if (!(info->flags & META_EXTERNAL_CONSTRAINT_FLAGS_RESIZE))
                return TRUE;

        meta_window_get_frame_rect (window, &current_rect);
        new_rect->width = current_rect.width;
        new_rect->height = current_rect.height;

        g_debug ("KioskLockResizeConstraint: Locking resize for window %s to [%ix%i]",
                 meta_window_get_description (window),
                 current_rect.width, current_rect.height);

        return TRUE;
}

static void
kiosk_lock_resize_constraint_iface_init (MetaExternalConstraintInterface *iface)
{
        iface->constrain = kiosk_lock_resize_constraint_constrain;
}

static void
kiosk_lock_resize_constraint_class_init (KioskLockResizeConstraintClass *klass)
{
}

static void
kiosk_lock_resize_constraint_init (KioskLockResizeConstraint *self)
{
}

KioskLockResizeConstraint *
kiosk_lock_resize_constraint_new (void)
{
        return g_object_new (KIOSK_TYPE_LOCK_RESIZE_CONSTRAINT, NULL);
}
