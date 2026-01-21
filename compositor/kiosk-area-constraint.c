#include "config.h"

#include "kiosk-area-constraint.h"
#include "kiosk-compositor.h"
#include "kiosk-window-config.h"

#include <meta/display.h>
#include <meta/meta-backend.h>
#include <meta/meta-context.h>
#include <meta/meta-monitor-manager.h>
#include <meta/meta-external-constraint.h>

struct _KioskAreaConstraint
{
        GObject             parent;

        /* Weak references */
        KioskCompositor    *compositor;
        KioskWindowConfig  *config;
        MetaDisplay        *display;
        MetaContext        *context;
        MetaBackend        *backend;
        MetaMonitorManager *monitor_manager;

        MtkRectangle        area;
        gboolean            is_absolute;
};

enum
{
        PROP_0,
        PROP_COMPOSITOR,
        PROP_AREA,
        PROP_IS_ABSOLUTE,
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
        g_clear_weak_pointer (&self->display);
        g_clear_weak_pointer (&self->context);
        g_clear_weak_pointer (&self->backend);
        g_clear_weak_pointer (&self->monitor_manager);

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
        case PROP_IS_ABSOLUTE:
                self->is_absolute = g_value_get_boolean (value);
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
        case PROP_IS_ABSOLUTE:
                g_value_set_boolean (value, self->is_absolute);
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
        g_set_weak_pointer (&self->display, meta_plugin_get_display (META_PLUGIN (self->compositor)));
        g_set_weak_pointer (&self->context, meta_display_get_context (self->display));
        g_set_weak_pointer (&self->backend, meta_context_get_backend (self->context));
        g_set_weak_pointer (&self->monitor_manager, meta_backend_get_monitor_manager (self->backend));
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

        props[PROP_IS_ABSOLUTE] = g_param_spec_boolean ("is-absolute",
                                                        NULL,
                                                        NULL,
                                                        FALSE,
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

static void
kiosk_area_constraint_constrain_to_rectangle (MtkRectangle                *rect,
                                              MtkRectangle                *area,
                                              MetaExternalConstraintFlags  flags)
{
        if (!mtk_rectangle_contains_rect (area, rect)) {
                g_debug ("KioskAreaConstraint: rectangle (%i,%i) [%ix%i] is outside the constraint area (%i,%i) [%ix%i]",
                         rect->x, rect->y, rect->width, rect->height,
                         area->x, area->y, area->width, area->height);

                /* Constrain position to stay within area */
                if (flags & META_EXTERNAL_CONSTRAINT_FLAGS_MOVE) {
                        rect->x = CLAMP (rect->x,
                                         area->x,
                                         area->x + MAX (0, area->width - rect->width));
                        rect->y = CLAMP (rect->y,
                                         area->y,
                                         area->y + MAX (0, area->height - rect->height));
                        g_debug ("KioskAreaConstraint: Constraining position to (%i,%i)",
                                 rect->x, rect->y);
                }

                /* Constrain size to fit within area (unless it's a pure move) */
                if (flags != META_EXTERNAL_CONSTRAINT_FLAGS_MOVE) {
                        mtk_rectangle_intersect (area, rect, rect);
                        g_debug ("KioskAreaConstraint: Constraining size to (%i,%i) [%ix%i]",
                                 rect->x, rect->y, rect->width, rect->height);
                }
        }
}

static gboolean
kiosk_area_constraint_get_constraint_area (KioskAreaConstraint *self,
                                           MetaWindow          *window,
                                           MtkRectangle        *constraint_area)
{
        const char *output_name;
        int monitor_index;
        MtkRectangle monitor_geometry = { 0, };

        /* For absolute areas, use the area directly */
        if (self->is_absolute) {
                *constraint_area = self->area;
                return TRUE;
        }

        /* For monitor-relative areas, convert to absolute coordinates */
        output_name = kiosk_window_config_lookup_window_output_name (self->config, window);
        if (!output_name) {
                g_debug ("KioskAreaConstraint: Window %s has no monitor set",
                         meta_window_get_description (window));
                return FALSE;
        }

        monitor_index = meta_monitor_manager_get_monitor_for_connector (self->monitor_manager,
                                                                        output_name);
        if (monitor_index < 0) {
                g_debug ("KioskAreaConstraint: Could not find monitor named \"%s\"", output_name);
                return FALSE;
        }

        /* Convert relative area to absolute coordinates */
        meta_display_get_monitor_geometry (self->display, monitor_index, &monitor_geometry);
        constraint_area->x = self->area.x + monitor_geometry.x;
        constraint_area->y = self->area.y + monitor_geometry.y;
        constraint_area->width = self->area.width;
        constraint_area->height = self->area.height;

        /* Clip to monitor bounds */
        mtk_rectangle_intersect (&monitor_geometry, constraint_area, constraint_area);

        return TRUE;
}

static gboolean
kiosk_area_constraint_constrain (MetaExternalConstraint     *constraint,
                                 MetaWindow                 *window,
                                 MetaExternalConstraintInfo *info)
{
        KioskAreaConstraint *self = KIOSK_AREA_CONSTRAINT (constraint);
        MtkRectangle constraint_area = { 0, };

        if (!self->config)
                return TRUE;

        g_debug ("KioskAreaConstraint: Constraining window %s on %s area",
                 meta_window_get_description (window),
                 self->is_absolute ? "absolute" : "monitor-relative");

        if (!kiosk_area_constraint_get_constraint_area (self, window, &constraint_area))
                return TRUE;

        if (mtk_rectangle_is_empty (&constraint_area)) {
                g_debug ("KioskAreaConstraint: Resulting area for window %s is empty, ignore",
                         meta_window_get_description (window));
                return TRUE;
        }

        g_debug ("KioskAreaConstraint: Window %s is constrained on area (%i,%i) [%ix%i]",
                 meta_window_get_description (window),
                 constraint_area.x, constraint_area.y, constraint_area.width, constraint_area.height);

        kiosk_area_constraint_constrain_to_rectangle (info->new_rect, &constraint_area, info->flags);

        return TRUE;
}

KioskAreaConstraint *
kiosk_area_constraint_new (KioskCompositor *compositor,
                           MtkRectangle    *area,
                           gboolean         is_absolute)
{
        return g_object_new (KIOSK_TYPE_AREA_CONSTRAINT,
                             "compositor", compositor,
                             "area", area,
                             "is-absolute", is_absolute,
                             NULL);
}
