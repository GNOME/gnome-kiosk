#include "config.h"

#include "kiosk-area-constraint.h"
#include "kiosk-compositor.h"
#include "kiosk-window-config.h"

#include <meta/meta-external-constraint.h>

struct _KioskAreaConstraint
{
        GObject            parent;

        /* Weak references */
        KioskCompositor   *compositor;
        KioskWindowConfig *config;

        MtkRectangle       area;
};

enum
{
        PROP_0,
        PROP_COMPOSITOR,
        PROP_AREA,
        N_PROPS
};
static GParamSpec *props[N_PROPS] = { NULL, };

static gboolean
kiosk_area_constraint_constrain (MetaExternalConstraint     *constraint,
                                 MetaWindow                 *window,
                                 MetaExternalConstraintInfo *info);

static void kiosk_area_constraint_iface_init (MetaExternalConstraintInterface *iface);

G_DEFINE_TYPE_WITH_CODE (KioskAreaConstraint, kiosk_area_constraint, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (META_TYPE_EXTERNAL_CONSTRAINT,
                                                kiosk_area_constraint_iface_init))

static void
kiosk_area_constraint_dispose (GObject *object)
{
        KioskAreaConstraint *self = KIOSK_AREA_CONSTRAINT (object);

        g_clear_weak_pointer (&self->compositor);
        g_clear_weak_pointer (&self->config);

        G_OBJECT_CLASS (kiosk_area_constraint_parent_class)->dispose (object);
}

static void
kiosk_area_constraint_iface_init (MetaExternalConstraintInterface *iface)
{
        iface->constrain = kiosk_area_constraint_constrain;
}

static void
kiosk_area_constraint_set_property (GObject      *object,
                                    guint         property_id,
                                    const GValue *value,
                                    GParamSpec   *param_spec)
{
        KioskAreaConstraint *self = KIOSK_AREA_CONSTRAINT (object);
        MtkRectangle *area;

        switch (property_id) {
        case PROP_COMPOSITOR:
                g_set_weak_pointer (&self->compositor, g_value_get_object (value));
                break;
        case PROP_AREA:
                area = g_value_get_boxed (value);
                if (area)
                        self->area = *area;
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id,
                                                   param_spec);
                break;
        }
}

static void
kiosk_area_constraint_get_property (GObject    *object,
                                    guint       property_id,
                                    GValue     *value,
                                    GParamSpec *param_spec)
{
        KioskAreaConstraint *self = KIOSK_AREA_CONSTRAINT (object);

        switch (property_id) {
        case PROP_COMPOSITOR:
                g_value_set_object (value, self->compositor);
                break;
        case PROP_AREA:
                g_value_set_boxed (value, &self->area);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id,
                                                   param_spec);
                break;
        }
}

static void
kiosk_area_constraint_constructed (GObject *object)
{
        KioskAreaConstraint *self = KIOSK_AREA_CONSTRAINT (object);

        G_OBJECT_CLASS (kiosk_area_constraint_parent_class)->constructed (object);

        g_set_weak_pointer (&self->config, kiosk_compositor_get_window_config (self->compositor));
}

static void
kiosk_area_constraint_class_init (KioskAreaConstraintClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->set_property = kiosk_area_constraint_set_property;
        object_class->get_property = kiosk_area_constraint_get_property;
        object_class->constructed = kiosk_area_constraint_constructed;
        object_class->dispose = kiosk_area_constraint_dispose;

        props[PROP_COMPOSITOR] = g_param_spec_object ("compositor",
                                                      NULL,
                                                      NULL,
                                                      KIOSK_TYPE_COMPOSITOR,
                                                      G_PARAM_CONSTRUCT_ONLY
                                                      | G_PARAM_READWRITE
                                                      | G_PARAM_STATIC_NAME
                                                      | G_PARAM_STATIC_NICK
                                                      | G_PARAM_STATIC_BLURB);

        props[PROP_AREA] = g_param_spec_boxed ("area",
                                               NULL,
                                               NULL,
                                               MTK_TYPE_RECTANGLE,
                                               G_PARAM_CONSTRUCT_ONLY
                                               | G_PARAM_READWRITE
                                               | G_PARAM_STATIC_NAME
                                               | G_PARAM_STATIC_NICK
                                               | G_PARAM_STATIC_BLURB);

        g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
kiosk_area_constraint_init (KioskAreaConstraint *self)
{
}

static gboolean
kiosk_area_constraint_constrain (MetaExternalConstraint     *constraint,
                                 MetaWindow                 *window,
                                 MetaExternalConstraintInfo *info)
{
        return TRUE;
}

KioskAreaConstraint *
kiosk_area_constraint_new (KioskCompositor *compositor,
                           MtkRectangle    *area)
{
        return g_object_new (KIOSK_TYPE_AREA_CONSTRAINT,
                             "compositor", compositor,
                             "area", area,
                             NULL);
}
