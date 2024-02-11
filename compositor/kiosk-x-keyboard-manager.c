#include "config.h"
#include "kiosk-x-keyboard-manager.h"

#include <stdlib.h>
#include <string.h>

#include <meta/display.h>
#include <meta/util.h>

#include <meta/meta-backend.h>
#include <meta/meta-context.h>
#include <meta/meta-x11-display.h>

#include <X11/Xatom.h>
#include <X11/XKBlib.h>

#include "kiosk-compositor.h"
#include "kiosk-gobject-utils.h"

struct _KioskXKeyboardManager
{
        GObject          parent;

        /* weak references */
        KioskCompositor *compositor;
        MetaBackend     *backend;
        MetaDisplay     *display;
        MetaContext     *context;
        Display         *x_server_display;

        /* strong references */
        GCancellable    *cancellable;
        GBytes          *xkb_rules_names_data;
        char           **layouts;
        char            *options;

        /* state */
        Window           x_server_root_window;
        Atom             xkb_rules_names_atom;
        int              xkb_event_base;

        size_t           layout_index;
        ssize_t          pending_layout_index;

        /* flags */
        guint32          xkb_rules_names_data_changed : 1;
};

enum
{
        PROP_COMPOSITOR = 1,
        PROP_LAYOUTS,
        PROP_SELECTED_LAYOUT,
        NUMBER_OF_PROPERTIES
};

static GParamSpec *kiosk_x_keyboard_manager_properties[NUMBER_OF_PROPERTIES] = { NULL, };

G_DEFINE_TYPE (KioskXKeyboardManager, kiosk_x_keyboard_manager, G_TYPE_OBJECT)

static void kiosk_x_keyboard_manager_set_property (GObject      *object,
                                                   guint         property_id,
                                                   const GValue *value,
                                                   GParamSpec   *param_spec);
static void kiosk_x_keyboard_manager_get_property (GObject    *object,
                                                   guint       property_id,
                                                   GValue     *value,
                                                   GParamSpec *param_spec);

static void kiosk_x_keyboard_manager_constructed (GObject *object);
static void kiosk_x_keyboard_manager_dispose (GObject *object);

KioskXKeyboardManager *
kiosk_x_keyboard_manager_new (KioskCompositor *compositor)
{
        GObject *object;

        object = g_object_new (KIOSK_TYPE_X_KEYBOARD_MANAGER,
                               "compositor", compositor,
                               NULL);

        return KIOSK_X_KEYBOARD_MANAGER (object);
}

static void
kiosk_x_keyboard_manager_class_init (KioskXKeyboardManagerClass *x_keyboard_manager_class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (x_keyboard_manager_class);

        object_class->constructed = kiosk_x_keyboard_manager_constructed;
        object_class->set_property = kiosk_x_keyboard_manager_set_property;
        object_class->get_property = kiosk_x_keyboard_manager_get_property;
        object_class->dispose = kiosk_x_keyboard_manager_dispose;

        kiosk_x_keyboard_manager_properties[PROP_COMPOSITOR] = g_param_spec_object ("compositor",
                                                                                    "compositor",
                                                                                    "compositor",
                                                                                    KIOSK_TYPE_COMPOSITOR,
                                                                                    G_PARAM_CONSTRUCT_ONLY
                                                                                    | G_PARAM_WRITABLE
                                                                                    | G_PARAM_STATIC_NAME
                                                                                    | G_PARAM_STATIC_NICK
                                                                                    | G_PARAM_STATIC_BLURB);
        kiosk_x_keyboard_manager_properties[PROP_SELECTED_LAYOUT] = g_param_spec_string ("selected-layout",
                                                                                         "selected-layout",
                                                                                         "selected-layout",
                                                                                         NULL,
                                                                                         G_PARAM_READABLE
                                                                                         | G_PARAM_STATIC_NAME
                                                                                         | G_PARAM_STATIC_NICK
                                                                                         | G_PARAM_STATIC_BLURB);

        kiosk_x_keyboard_manager_properties[PROP_LAYOUTS] = g_param_spec_boxed ("layouts",
                                                                                "layouts",
                                                                                "layouts",
                                                                                G_TYPE_STRV,
                                                                                G_PARAM_READABLE
                                                                                | G_PARAM_STATIC_NAME
                                                                                | G_PARAM_STATIC_NICK
                                                                                | G_PARAM_STATIC_BLURB);

        g_object_class_install_properties (object_class, NUMBER_OF_PROPERTIES, kiosk_x_keyboard_manager_properties);
}

static void
kiosk_x_keyboard_manager_set_property (GObject      *object,
                                       guint         property_id,
                                       const GValue *value,
                                       GParamSpec   *param_spec)
{
        KioskXKeyboardManager *self = KIOSK_X_KEYBOARD_MANAGER (object);

        switch (property_id) {
        case PROP_COMPOSITOR:
                g_set_weak_pointer (&self->compositor, g_value_get_object (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, param_spec);
                break;
        }
}

static void
kiosk_x_keyboard_manager_get_property (GObject    *object,
                                       guint       property_id,
                                       GValue     *value,
                                       GParamSpec *param_spec)
{
        KioskXKeyboardManager *self = KIOSK_X_KEYBOARD_MANAGER (object);

        switch (property_id) {
        case PROP_SELECTED_LAYOUT:
                g_value_set_string (value, self->layouts[self->layout_index]);
                break;
        case PROP_LAYOUTS:
                g_value_set_boxed (value, self->layouts);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, param_spec);
                break;
        }
}

static char **
qualify_layouts_with_variants (KioskXKeyboardManager *self,
                               const char * const    *layouts,
                               const char * const    *variants)
{
        size_t number_of_layouts = 0;
        size_t number_of_variants = 0;
        char **fully_qualified_layouts = NULL;

        size_t i, j;

        g_return_val_if_fail (KIOSK_IS_X_KEYBOARD_MANAGER (self), FALSE);

        number_of_layouts = g_strv_length ((GStrv) layouts);
        number_of_variants = g_strv_length ((GStrv) variants);

        if (number_of_layouts < number_of_variants) {
                g_debug ("KioskXKeyboardManager: There is a layout variant mismatch");
                return NULL;
        }

        fully_qualified_layouts = g_new0 (char *, number_of_layouts + 1);

        for (i = 0, j = 0; layouts[i] != NULL; i++) {
                const char *layout = layouts[i];
                const char *variant = "";

                if (variants[j] != NULL) {
                        variant = variants[j++];
                }

                if (variant[0] == '\0') {
                        fully_qualified_layouts[i] = g_strdup (layout);
                } else {
                        fully_qualified_layouts[i] = g_strdup_printf ("%s+%s", layout, variant);
                }
        }

        return fully_qualified_layouts;
}

static void
kiosk_x_keyboard_manager_set_layout_index (KioskXKeyboardManager *self,
                                           size_t                 layout_index)
{
        size_t number_of_layouts;

        if (self->layout_index == layout_index) {
                return;
        }

        g_debug ("KioskXKeyboardManager: X server is using layout with index %ld",
                 layout_index);

        number_of_layouts = g_strv_length (self->layouts);

        if (layout_index >= number_of_layouts) {
                layout_index = 0;
        }

        self->layout_index = layout_index;
        g_object_notify (G_OBJECT (self), "selected-layout");
}

static gboolean
kiosk_x_keyboard_manager_read_current_layout_index (KioskXKeyboardManager *self)
{
        XkbStateRec xkb_state = { 0 };
        int status;

        status = XkbGetState (self->x_server_display, XkbUseCoreKbd, &xkb_state);

        if (status != Success) {
                g_debug ("KioskXKeyboardManager: Could not read current layout index");
                return FALSE;
        }

        kiosk_x_keyboard_manager_set_layout_index (self, xkb_state.locked_group);
        return FALSE;
}

static gboolean
kiosk_x_keyboard_manager_read_xkb_rules_names_data (KioskXKeyboardManager *self)
{
        g_autoptr (GBytes) new_xkb_rules_names_data = NULL;
        g_autoptr (GVariant) input_source_group = NULL;
        size_t number_of_layouts = 0;
        g_autofree char *layouts_string = NULL;
        g_autofree char *variants_string = NULL;
        g_autofree char *options = NULL;
        g_auto (GStrv) layouts = NULL;
        g_auto (GStrv) variants = NULL;
        g_auto (GStrv) qualified_layouts = NULL;
        int status;
        Atom returned_type = 0;
        int returned_format = 0;
        gulong number_of_bytes_read = 0;
        gulong number_of_bytes_unread = 0;
        guchar *property_values;
        size_t i;
        enum
        {
                RULES_NAME = 0,
                MODEL,
                LAYOUTS,
                VARIANTS,
                OPTIONS
        } property_value_index;

        self->xkb_rules_names_data_changed = TRUE;

        g_debug ("KioskXKeyboardManager: Reading active keyboard layouts from X server");

        status = XGetWindowProperty (self->x_server_display,
                                     self->x_server_root_window,
                                     self->xkb_rules_names_atom,
                                     0, 1024, FALSE, XA_STRING,
                                     &returned_type,
                                     &returned_format,
                                     &number_of_bytes_read,
                                     &number_of_bytes_unread,
                                     &property_values);

        if (status != Success) {
                g_debug ("KioskXKeyboardManager: Could not read active keyboard layouts from X server");
                return FALSE;
        }

        if (returned_type != XA_STRING ||
            returned_format != 8 ||
            number_of_bytes_unread != 0) {
                g_debug ("KioskXKeyboardManager: Active keyboard layouts propery from X server is corrupted");
                return FALSE;
        }

        new_xkb_rules_names_data = g_bytes_new_with_free_func (property_values,
                                                               number_of_bytes_read,
                                                               (GDestroyNotify) XFree,
                                                               NULL);

        property_value_index = 0;
        for (i = 0; i < number_of_bytes_read; i++) {
                g_autofree char *value = g_strdup ((char *) property_values + i);
                size_t value_length = strlen (value);

                switch (property_value_index) {
                case RULES_NAME:
                case MODEL:
                        break;
                case LAYOUTS:
                        layouts_string = g_steal_pointer (&value);
                        g_debug ("KioskXKeyboardManager: Read layouts '%s'", layouts_string);
                        break;
                case VARIANTS:
                        variants_string = g_steal_pointer (&value);
                        g_debug ("KioskXKeyboardManager: Read variants '%s'", variants_string);
                        break;
                case OPTIONS:
                        options = g_steal_pointer (&value);
                        g_debug ("KioskXKeyboardManager: Read options '%s'", options);
                        break;
                }

                i += value_length;
                property_value_index++;
        }

        if (self->xkb_rules_names_data != NULL && g_bytes_equal (self->xkb_rules_names_data, new_xkb_rules_names_data)) {
                g_debug ("KioskXKeyboardManager: XKB rules names data is unchanged");
                return FALSE;
        }

        g_clear_pointer (&self->xkb_rules_names_data, g_bytes_unref);
        self->xkb_rules_names_data = g_steal_pointer (&new_xkb_rules_names_data);

        layouts = g_strsplit (layouts_string, ",", -1);
        variants = g_strsplit (variants_string, ",", -1);

        qualified_layouts = qualify_layouts_with_variants (self, (const char * const *) layouts, (const char * const *) variants);

        if (qualified_layouts == NULL) {
                g_debug ("KioskXKeyboardManager: Unable to qualify layouts with variants");
                return FALSE;
        }

        number_of_layouts = g_strv_length (qualified_layouts);

        if (number_of_layouts == 0) {
                g_debug ("KioskXKeyboardManager: No layouts found");
                return FALSE;
        }

        g_clear_pointer (&self->layouts, g_strfreev);
        self->layouts = g_steal_pointer (&qualified_layouts);
        self->options = g_steal_pointer (&options);

        g_object_freeze_notify (G_OBJECT (self));
        g_object_notify (G_OBJECT (self), "layouts");
        kiosk_x_keyboard_manager_read_current_layout_index (self);
        g_object_thaw_notify (G_OBJECT (self));

        return TRUE;
}

static void
monitor_x_server_display_for_changes (KioskXKeyboardManager *self)
{
        int major = XkbMajorVersion;
        int minor = XkbMinorVersion;
        XWindowAttributes attributes;

        XGetWindowAttributes (self->x_server_display, self->x_server_root_window, &attributes);

        if (!(attributes.your_event_mask & PropertyChangeMask)) {
                XSelectInput (self->x_server_display,
                              self->x_server_root_window,
                              attributes.your_event_mask | PropertyChangeMask);
        }

        XkbQueryExtension (self->x_server_display, NULL, &self->xkb_event_base, NULL, &major, &minor);
        self->xkb_rules_names_atom = XInternAtom (self->x_server_display, "_XKB_RULES_NAMES", False);
}

static void
kiosk_x_keyboard_manager_handle_x_server_property_notify (KioskXKeyboardManager *self,
                                                          XPropertyEvent        *x_server_event)
{
        if (x_server_event->window != self->x_server_root_window) {
                return;
        }

        if (x_server_event->atom != self->xkb_rules_names_atom) {
                return;
        }

        g_debug ("KioskXKeyboardManager: XKB rules names property changed in X server");
        kiosk_x_keyboard_manager_read_xkb_rules_names_data (self);
}

static void
kiosk_x_keyboard_manager_handle_xkb_event (KioskXKeyboardManager *self,
                                           XkbEvent              *x_server_event)
{
        size_t layout_index;

        layout_index = XkbStateGroup (&x_server_event->state);
        switch (x_server_event->any.xkb_type) {
        case XkbStateNotify:
                if (!(x_server_event->state.changed & XkbGroupStateMask)) {
                        return;
                }

                /* Mutter immediately reverts all layout changes coming from
                 * the outside, so we hide the event from it.
                 */
                x_server_event->state.changed &= ~XkbGroupLockMask;

                if (self->xkb_rules_names_data_changed) {
                        g_debug ("KioskXKeyboardManager: Ignoring spurious group change following layout change");
                        self->xkb_rules_names_data_changed = FALSE;
                        return;
                }
                g_debug ("KioskXKeyboardManager: Approving keyboard group change to group %lu", layout_index);
                kiosk_x_keyboard_manager_set_layout_index (self, layout_index);
                break;
        }
}

static void
on_x_server_event (KioskXKeyboardManager *self,
                   XEvent                *x_server_event)
{
        if (self->x_server_display == NULL) {
                self->x_server_display = x_server_event->xany.display;
                self->x_server_root_window = DefaultRootWindow (self->x_server_display);
                monitor_x_server_display_for_changes (self);
        }

        switch (x_server_event->type) {
        case PropertyNotify:
                kiosk_x_keyboard_manager_handle_x_server_property_notify (self, &x_server_event->xproperty);
                break;
        default:
                if (x_server_event->type == self->xkb_event_base) {
                        kiosk_x_keyboard_manager_handle_xkb_event (self, (XkbEvent *) x_server_event);
                }
                break;
        }
}

const char * const *
kiosk_x_keyboard_manager_get_layouts (KioskXKeyboardManager *self)
{
        g_return_val_if_fail (KIOSK_IS_X_KEYBOARD_MANAGER (self), NULL);

        return (const char * const *) self->layouts;
}

const char *
kiosk_x_keyboard_manager_get_selected_layout (KioskXKeyboardManager *self)
{
        g_return_val_if_fail (KIOSK_IS_X_KEYBOARD_MANAGER (self), NULL);

        if (self->layouts == NULL) {
                return NULL;
        }

        g_debug ("KioskXKeyboardManager: Selected layout is '%s'", self->layouts[self->layout_index]);

        return self->layouts[self->layout_index];
}

const char *
kiosk_x_keyboard_manager_get_options (KioskXKeyboardManager *self)
{
        g_return_val_if_fail (KIOSK_IS_X_KEYBOARD_MANAGER (self), NULL);

        return self->options;
}

gboolean
kiosk_x_keyboard_manager_keymap_is_active (KioskXKeyboardManager *self,
                                           const char * const    *layouts,
                                           const char * const    *variants,
                                           const char            *options)
{
        g_auto (GStrv) qualified_layouts = NULL;

        if (g_strcmp0 (options, self->options) != 0) {
                return FALSE;
        }

        qualified_layouts = qualify_layouts_with_variants (self, layouts, variants);

        if (qualified_layouts == NULL) {
                return FALSE;
        }

        if (!g_strv_equal ((const char * const *) qualified_layouts, (const char * const *) self->layouts)) {
                return FALSE;
        }

        return TRUE;
}

gboolean
kiosk_x_keyboard_manager_layout_group_is_locked (KioskXKeyboardManager *self,
                                                 xkb_layout_index_t     layout_index)
{
        return self->layout_index == layout_index;
}

static void
kiosk_x_keyboard_manager_init (KioskXKeyboardManager *self)
{
}

static void
kiosk_x_keyboard_manager_constructed (GObject *object)
{
        KioskXKeyboardManager *self = KIOSK_X_KEYBOARD_MANAGER (object);

        g_debug ("KioskXKeyboardManager: Initializing");

        G_OBJECT_CLASS (kiosk_x_keyboard_manager_parent_class)->constructed (object);

        self->cancellable = g_cancellable_new ();

        g_set_weak_pointer (&self->display, meta_plugin_get_display (META_PLUGIN (self->compositor)));
        g_set_weak_pointer (&self->context, meta_display_get_context (self->display));
        g_set_weak_pointer (&self->backend, meta_context_get_backend (self->context));

        self->pending_layout_index = -1;

        g_signal_connect_object (G_OBJECT (self->compositor),
                                 "x-server-event",
                                 G_CALLBACK (on_x_server_event),
                                 self,
                                 G_CONNECT_SWAPPED);
}

static void
kiosk_x_keyboard_manager_dispose (GObject *object)
{
        KioskXKeyboardManager *self = KIOSK_X_KEYBOARD_MANAGER (object);

        g_debug ("KioskXKeyboardManager: Disposing");

        if (self->cancellable != NULL) {
                g_cancellable_cancel (self->cancellable);
                g_clear_object (&self->cancellable);
        }

        g_clear_pointer (&self->xkb_rules_names_data, g_bytes_unref);
        g_clear_pointer (&self->layouts, g_strfreev);
        g_clear_pointer (&self->options, g_free);

        g_clear_weak_pointer (&self->display);
        g_clear_weak_pointer (&self->context);
        g_clear_weak_pointer (&self->backend);
        g_clear_weak_pointer (&self->compositor);

        G_OBJECT_CLASS (kiosk_x_keyboard_manager_parent_class)->dispose (object);
}
