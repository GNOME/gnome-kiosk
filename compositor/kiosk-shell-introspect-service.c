#include "config.h"
#include "kiosk-shell-introspect-service.h"

#include <stdlib.h>
#include <string.h>
#include <meta/display.h>
#include <meta/meta-context.h>
#include <meta/meta-backend.h>
#include <meta/meta-monitor-manager.h>
#include <meta/util.h>

#include "kiosk-compositor.h"
#include "kiosk-app.h"
#include "kiosk-app-system.h"
#include "kiosk-window-tracker.h"

#define KIOSK_SHELL_INTROSPECT_SERVICE_BUS_NAME "org.gnome.Shell.Introspect"
#define KIOSK_SHELL_INTROSPECT_SERVICE_OBJECT_PATH "/org/gnome/Shell/Introspect"
#define KIOSK_SHELL_INTROSPECT_SERVICE_SEAT "seat0"
#define KIOSK_SHELL_INTROSPECT_SERVICE_VERSION 3
#define KIOSK_SHELL_INTROSPECT_SERVICE_HAS_ANIMATIONS_ENABLED FALSE

struct _BusNameWatcher
{
        guint       watcher_id;
        const char *name;
        char       *name_owner;
};

static struct _BusNameWatcher allowed_app_list[] = {
        { 0, "org.freedesktop.impl.portal.desktop.gtk",   NULL },
        { 0, "org.freedesktop.impl.portal.desktop.gnome", NULL },
        { 0, NULL,                                        NULL },
};

struct _KioskShellIntrospectService
{
        KioskShellIntrospectDBusServiceSkeleton parent;

        /* weak references */
        KioskCompositor                        *compositor;
        KioskWindowTracker                     *tracker;
        MetaDisplay                            *display;
        MetaBackend                            *backend;
        MetaContext                            *context;
        MetaMonitorManager                     *monitor_manager;

        /* handles */
        guint                                   bus_id;
};

enum
{
        PROP_COMPOSITOR = 1,
        NUMBER_OF_PROPERTIES
};
static GParamSpec *kiosk_shell_introspect_service_properties[NUMBER_OF_PROPERTIES] = { NULL, };

static void kiosk_shell_introspect_dbus_service_interface_init (KioskShellIntrospectDBusServiceIface *interface);
static void cleanup_bus_watcher (KioskShellIntrospectService *self);
static void on_windows_changed (KioskWindowTracker *self,
                                gpointer            user_data);
static void on_focused_app_changed (KioskWindowTracker *self,
                                    GParamSpec         *param_spec,
                                    gpointer            user_data);
static void on_monitors_changed (MetaMonitorManager *monitor_manager,
                                 gpointer            user_data);

G_DEFINE_TYPE_WITH_CODE (KioskShellIntrospectService,
                         kiosk_shell_introspect_service,
                         KIOSK_TYPE_SHELL_INTROSPECT_DBUS_SERVICE_SKELETON,
                         G_IMPLEMENT_INTERFACE (KIOSK_TYPE_SHELL_INTROSPECT_DBUS_SERVICE,
                                                kiosk_shell_introspect_dbus_service_interface_init));

static void kiosk_shell_introspect_service_set_property (GObject      *object,
                                                         guint         property_id,
                                                         const GValue *value,
                                                         GParamSpec   *param_spec);
static void kiosk_shell_introspect_service_get_property (GObject    *object,
                                                         guint       property_id,
                                                         GValue     *value,
                                                         GParamSpec *param_spec);

static void kiosk_shell_introspect_service_constructed (GObject *object);
static void kiosk_shell_introspect_service_dispose (GObject *object);

static void
kiosk_shell_introspect_service_class_init (KioskShellIntrospectServiceClass *shell_service_class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (shell_service_class);

        object_class->constructed = kiosk_shell_introspect_service_constructed;
        object_class->set_property = kiosk_shell_introspect_service_set_property;
        object_class->get_property = kiosk_shell_introspect_service_get_property;
        object_class->dispose = kiosk_shell_introspect_service_dispose;

        kiosk_shell_introspect_service_properties[PROP_COMPOSITOR] =
                g_param_spec_object ("compositor",
                                     "compositor",
                                     "compositor",
                                     KIOSK_TYPE_COMPOSITOR,
                                     G_PARAM_CONSTRUCT_ONLY
                                     | G_PARAM_WRITABLE
                                     | G_PARAM_STATIC_NAME
                                     | G_PARAM_STATIC_NICK
                                     | G_PARAM_STATIC_BLURB);
        g_object_class_install_properties (object_class,
                                           NUMBER_OF_PROPERTIES,
                                           kiosk_shell_introspect_service_properties);
}

static void
kiosk_shell_introspect_service_set_property (GObject      *object,
                                             guint         property_id,
                                             const GValue *value,
                                             GParamSpec   *param_spec)
{
        KioskShellIntrospectService *self = KIOSK_SHELL_INTROSPECT_SERVICE (object);

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
kiosk_shell_introspect_service_get_property (GObject    *object,
                                             guint       property_id,
                                             GValue     *value,
                                             GParamSpec *param_spec)
{
        switch (property_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, param_spec);
                break;
        }
}

static void
kiosk_shell_introspect_service_dispose (GObject *object)
{
        KioskShellIntrospectService *self = KIOSK_SHELL_INTROSPECT_SERVICE (object);

        g_signal_handlers_disconnect_by_func (self->tracker,
                                              G_CALLBACK (on_windows_changed),
                                              self);
        g_signal_handlers_disconnect_by_func (self->tracker,
                                              G_CALLBACK (on_focused_app_changed),
                                              self);
        g_signal_handlers_disconnect_by_func (self->monitor_manager,
                                              G_CALLBACK (on_monitors_changed),
                                              self);

        kiosk_shell_introspect_service_stop (self);

        g_clear_weak_pointer (&self->monitor_manager);
        g_clear_weak_pointer (&self->backend);
        g_clear_weak_pointer (&self->context);
        g_clear_weak_pointer (&self->display);
        g_clear_weak_pointer (&self->tracker);
        g_clear_weak_pointer (&self->compositor);

        cleanup_bus_watcher (self);

        G_OBJECT_CLASS (kiosk_shell_introspect_service_parent_class)->dispose (object);
}

static void
kiosk_shell_introspect_service_constructed (GObject *object)
{
        KioskShellIntrospectService *self = KIOSK_SHELL_INTROSPECT_SERVICE (object);

        G_OBJECT_CLASS (kiosk_shell_introspect_service_parent_class)->constructed (object);

        g_set_weak_pointer (&self->display, meta_plugin_get_display (META_PLUGIN (self->compositor)));
        g_set_weak_pointer (&self->context, meta_display_get_context (self->display));
        g_set_weak_pointer (&self->backend, meta_context_get_backend (self->context));
        g_set_weak_pointer (&self->monitor_manager, meta_backend_get_monitor_manager (self->backend));
}

static gboolean
kiosk_shell_introspect_check_access (KioskShellIntrospectService *self,
                                     const char                  *client_unique_name)
{
        GValue value = G_VALUE_INIT;
        gboolean unsafe_mode;
        int i;

        for (i = 0; allowed_app_list[i].name; i++) {
                if (g_strcmp0 (client_unique_name, allowed_app_list[i].name_owner) == 0) {
                        g_debug ("KioskShellIntrospectService: '%s' has access granted",
                                 allowed_app_list[i].name);
                        return TRUE;
                }
        }

        g_object_get_property (G_OBJECT (self->context), "unsafe-mode", &value);
        unsafe_mode = g_value_get_boolean (&value);
        g_debug ("KioskShellIntrospectService: unsafe-mode is %s",
                 unsafe_mode ? "TRUE" : "FALSE");

        return unsafe_mode;
}

static void
kiosk_shell_introspect_add_running_app (KioskWindowTracker *tracker,
                                        KioskApp           *app,
                                        GVariantBuilder    *app_builder)
{
        GVariantBuilder app_properties_builder;
        const char *sandbox_id;
        const char *app_id;

        app_id = kiosk_app_get_id (app);
        g_debug ("KioskShellIntrospectService: adding running app '%s'", app_id);

        g_variant_builder_init (&app_properties_builder, G_VARIANT_TYPE_VARDICT);

        if (app == kiosk_window_tracker_get_focused_app (tracker)) {
                GVariant *children[1] = { 0 };

                children[0] = g_variant_new_string (KIOSK_SHELL_INTROSPECT_SERVICE_SEAT);
                g_variant_builder_add (&app_properties_builder,
                                       "{sv}",
                                       "active-on-seats",
                                       g_variant_new_array (G_VARIANT_TYPE_STRING,
                                                            children, 1));
        }

        sandbox_id = kiosk_app_get_sandbox_id (app);
        if (sandbox_id) {
                g_variant_builder_add (&app_properties_builder,
                                       "{sv}",
                                       "sandboxed-app-id",
                                       g_variant_new_string (sandbox_id));
        }

        g_variant_builder_add (app_builder,
                               "{sa{sv}}",
                               app_id,
                               &app_properties_builder);
}

static gboolean
kiosk_shell_introspect_service_handle_get_running_applications (KioskShellIntrospectDBusService *object,
                                                                GDBusMethodInvocation           *invocation)
{
        KioskShellIntrospectService *self = KIOSK_SHELL_INTROSPECT_SERVICE (object);
        const char *client_unique_name = g_dbus_method_invocation_get_sender (invocation);
        GVariantBuilder app_builder;
        KioskAppSystem *app_system;
        KioskWindowTracker *tracker;
        KioskAppSystemAppIter app_iter;
        KioskApp *app;

        g_debug ("KioskShellIntrospectService: Handling GetRunningApplications() from %s",
                 client_unique_name);

        if (!kiosk_shell_introspect_check_access (self, client_unique_name)) {
                g_dbus_method_invocation_return_error (invocation,
                                                       G_DBUS_ERROR,
                                                       G_DBUS_ERROR_ACCESS_DENIED,
                                                       "Permission denied");
                return G_DBUS_METHOD_INVOCATION_HANDLED;
        }

        app_system = kiosk_compositor_get_app_system (self->compositor);
        tracker = kiosk_compositor_get_window_tracker (self->compositor);

        g_variant_builder_init (&app_builder, G_VARIANT_TYPE ("a{sa{sv}}"));

        kiosk_app_system_app_iter_init (&app_iter, app_system);

        while (kiosk_app_system_app_iter_next (&app_iter, &app)) {
                kiosk_shell_introspect_add_running_app (tracker, app, &app_builder);
        }

        kiosk_shell_introspect_dbus_service_complete_get_running_applications (
                KIOSK_SHELL_INTROSPECT_DBUS_SERVICE (self),
                invocation,
                g_variant_builder_end (&app_builder));

        return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
kiosk_shell_introspect_is_eligible_window (MetaWindow *window)
{
        MetaWindowType window_type;

        window_type = meta_window_get_window_type (window);

        switch (window_type) {
        case META_WINDOW_NORMAL:
        case META_WINDOW_DIALOG:
        case META_WINDOW_MODAL_DIALOG:
        case META_WINDOW_UTILITY:
                return TRUE;
        default:
                return FALSE;
        }
}

static void
kiosk_shell_introspect_add_window_properties (KioskApp        *app,
                                              MetaWindow      *window,
                                              GVariantBuilder *window_properties_builder)
{
        const char *app_id;
        const char *sandbox_id;
        const char *wm_class;
        const char *title;
        MetaWindowClientType client_type;
        gboolean is_hidden;
        gboolean has_focus;
        MtkRectangle frame_rect;

        app_id = kiosk_app_get_id (app);
        g_debug ("KioskShellIntrospectService: adding properties for app '%s'", app_id);
        g_variant_builder_add (window_properties_builder,
                               "{sv}",
                               "app-id",
                               g_variant_new_string (app_id));

        client_type = meta_window_get_client_type (window);
        g_variant_builder_add (window_properties_builder,
                               "{sv}",
                               "client-type",
                               g_variant_new ("u", client_type));

        is_hidden = meta_window_is_hidden (window);
        g_variant_builder_add (window_properties_builder,
                               "{sv}",
                               "is-hidden",
                               g_variant_new_boolean (is_hidden));

        has_focus = meta_window_has_focus (window);
        g_variant_builder_add (window_properties_builder,
                               "{sv}",
                               "has-focus",
                               g_variant_new_boolean (has_focus));

        meta_window_get_frame_rect (window, &frame_rect);
        g_variant_builder_add (window_properties_builder,
                               "{sv}",
                               "width",
                               g_variant_new ("u", frame_rect.width));
        g_variant_builder_add (window_properties_builder,
                               "{sv}",
                               "height",
                               g_variant_new ("u", frame_rect.height));

        title = meta_window_get_title (window);
        if (title) {
                g_variant_builder_add (window_properties_builder,
                                       "{sv}",
                                       "title",
                                       g_variant_new_string (title));
        }

        wm_class = meta_window_get_wm_class (window);
        if (wm_class) {
                g_variant_builder_add (window_properties_builder,
                                       "{sv}",
                                       "wm-class",
                                       g_variant_new_string (wm_class));
        }

        sandbox_id = kiosk_app_get_sandbox_id (app);
        if (sandbox_id) {
                g_variant_builder_add (window_properties_builder,
                                       "{sv}",
                                       "sandboxed-app-id",
                                       g_variant_new_string (sandbox_id));
        }
}

static void
kiosk_shell_introspect_add_windows_from_app (KioskApp        *app,
                                             GVariantBuilder *window_builder)
{
        GVariantBuilder window_properties_builder;
        const char *app_id;
        KioskAppWindowIter window_iter;
        MetaWindow *window;

        app_id = kiosk_app_get_id (app);
        g_debug ("KioskShellIntrospectService: adding windows for app '%s'", app_id);

        kiosk_app_window_iter_init (&window_iter, app);

        while (kiosk_app_window_iter_next (&window_iter, &window)) {
                if (!kiosk_shell_introspect_is_eligible_window (window))
                        continue;

                g_variant_builder_init (&window_properties_builder, G_VARIANT_TYPE_VARDICT);
                kiosk_shell_introspect_add_window_properties (app, window, &window_properties_builder);
                g_variant_builder_add (window_builder,
                                       "{ta{sv}}",
                                       meta_window_get_id (window),
                                       &window_properties_builder);
        }
}

static gboolean
kiosk_shell_introspect_service_handle_get_windows (KioskShellIntrospectDBusService *object,
                                                   GDBusMethodInvocation           *invocation)
{
        KioskShellIntrospectService *self = KIOSK_SHELL_INTROSPECT_SERVICE (object);
        const char *client_unique_name = g_dbus_method_invocation_get_sender (invocation);
        GVariantBuilder window_builder;
        KioskAppSystem *app_system;
        KioskAppSystemAppIter app_iter;
        KioskApp *app;

        g_debug ("KioskShellIntrospectService: Handling GetWindows() from %s",
                 client_unique_name);

        if (!kiosk_shell_introspect_check_access (self, client_unique_name)) {
                g_dbus_method_invocation_return_error (invocation,
                                                       G_DBUS_ERROR,
                                                       G_DBUS_ERROR_ACCESS_DENIED,
                                                       "Permission denied");
                return G_DBUS_METHOD_INVOCATION_HANDLED;
        }

        app_system = kiosk_compositor_get_app_system (self->compositor);

        g_variant_builder_init (&window_builder, G_VARIANT_TYPE ("a{ta{sv}}"));

        kiosk_app_system_app_iter_init (&app_iter, app_system);

        while (kiosk_app_system_app_iter_next (&app_iter, &app)) {
                kiosk_shell_introspect_add_windows_from_app (app, &window_builder);
        }

        kiosk_shell_introspect_dbus_service_complete_get_windows (
                KIOSK_SHELL_INTROSPECT_DBUS_SERVICE (self),
                invocation,
                g_variant_builder_end (&window_builder));

        return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static void
kiosk_shell_introspect_dbus_service_interface_init (KioskShellIntrospectDBusServiceIface *interface)
{
        interface->handle_get_running_applications =
                kiosk_shell_introspect_service_handle_get_running_applications;
        interface->handle_get_windows =
                kiosk_shell_introspect_service_handle_get_windows;
}

static void
kiosk_shell_introspect_service_init (KioskShellIntrospectService *self)
{
        g_debug ("KioskShellIntrospectService: Initializing");
}

KioskShellIntrospectService *
kiosk_shell_introspect_service_new (KioskCompositor *compositor)
{
        GObject *object;

        object = g_object_new (KIOSK_TYPE_SHELL_INTROSPECT_SERVICE,
                               "compositor", compositor,
                               NULL);

        return KIOSK_SHELL_INTROSPECT_SERVICE (object);
}

static void
on_user_bus_acquired (GDBusConnection             *connection,
                      const char                  *unique_name,
                      KioskShellIntrospectService *self)
{
        g_autoptr (GError) error = NULL;

        g_debug ("KioskShellIntrospectService: Connected to user bus");

        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
                                          connection,
                                          KIOSK_SHELL_INTROSPECT_SERVICE_OBJECT_PATH,
                                          &error);

        if (error != NULL) {
                g_debug ("KioskShellIntrospectService: Could not export interface skeleton: %s",
                         error->message);
                g_clear_error (&error);
        }
}

static void
on_bus_name_acquired (GDBusConnection             *connection,
                      const char                  *name,
                      KioskShellIntrospectService *self)
{
        if (g_strcmp0 (name, KIOSK_SHELL_INTROSPECT_SERVICE_BUS_NAME) != 0) {
                return;
        }

        g_debug ("KioskShellIntrospectService: Acquired name %s", name);
}

static void
on_bus_name_lost (GDBusConnection             *connection,
                  const char                  *name,
                  KioskShellIntrospectService *self)
{
        if (g_strcmp0 (name, KIOSK_SHELL_INTROSPECT_SERVICE_BUS_NAME) != 0) {
                return;
        }

        g_debug ("KioskShellIntrospectService: Lost name %s", name);
}

static void
on_windows_changed (KioskWindowTracker *tracker,
                    gpointer            user_data)
{
        KioskShellIntrospectService *self = KIOSK_SHELL_INTROSPECT_SERVICE (user_data);

        g_debug ("KioskShellIntrospectService: windows changed");
        kiosk_shell_introspect_dbus_service_emit_windows_changed (
                KIOSK_SHELL_INTROSPECT_DBUS_SERVICE (self));
}

static void
on_focused_app_changed (KioskWindowTracker *tracker,
                        GParamSpec         *param_spec,
                        gpointer            user_data)
{
        KioskShellIntrospectService *self = KIOSK_SHELL_INTROSPECT_SERVICE (user_data);

        g_debug ("KioskShellIntrospectService: focus app changed");
        kiosk_shell_introspect_dbus_service_emit_running_applications_changed (
                KIOSK_SHELL_INTROSPECT_DBUS_SERVICE (self));
}

static void
kiosk_shell_introspect_service_update_screen_size (KioskShellIntrospectService *self)
{
        int width, height;

        meta_display_get_size (self->display, &width, &height);
        kiosk_shell_introspect_dbus_service_set_screen_size (
                KIOSK_SHELL_INTROSPECT_DBUS_SERVICE (self),
                g_variant_new ("(ii)", width, height));
}

static void
on_monitors_changed (MetaMonitorManager *monitor_manager,
                     gpointer            user_data)
{
        KioskShellIntrospectService *self = KIOSK_SHELL_INTROSPECT_SERVICE (user_data);

        g_debug ("KioskShellIntrospectService: monitors changed");
        kiosk_shell_introspect_service_update_screen_size (self);
}

static void
on_name_appeared (GDBusConnection *connection,
                  const gchar     *name,
                  const gchar     *name_owner,
                  gpointer         user_data)
{
        int i;

        g_debug ("KioskShellIntrospectService: Name '%s' appeared, owner '%s'", name, name_owner);

        for (i = 0; allowed_app_list[i].name; i++) {
                if (g_strcmp0 (name, allowed_app_list[i].name) == 0) {
                        allowed_app_list[i].name_owner = g_strdup (name_owner);
                        break;
                }
        }
}

static void
on_name_vanished (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
        int i;

        g_debug ("KioskShellIntrospectService: Name '%s' vanished", name);

        for (i = 0; allowed_app_list[i].name; i++) {
                if (g_strcmp0 (name, allowed_app_list[i].name) == 0) {
                        g_clear_pointer (&allowed_app_list[i].name_owner, g_free);
                        break;
                }
        }
}

static void
cleanup_bus_watcher (KioskShellIntrospectService *self)
{
        int i;

        for (i = 0; allowed_app_list[i].name; i++) {
                if (allowed_app_list[i].watcher_id)
                        g_bus_unwatch_name (allowed_app_list[i].watcher_id);
                allowed_app_list[i].watcher_id = 0;
                g_clear_pointer (&allowed_app_list[i].name_owner, g_free);
        }
}

static void
setup_bus_watcher (KioskShellIntrospectService *self)
{
        int i;

        for (i = 0; allowed_app_list[i].name; i++) {
                allowed_app_list[i].watcher_id =
                        g_bus_watch_name (G_BUS_TYPE_SESSION,
                                          allowed_app_list[i].name,
                                          G_BUS_NAME_WATCHER_FLAGS_NONE,
                                          on_name_appeared,
                                          on_name_vanished,
                                          self,
                                          NULL);
        }
}

gboolean
kiosk_shell_introspect_service_start (KioskShellIntrospectService *self,
                                      GError                     **error)
{
        g_return_val_if_fail (KIOSK_IS_SHELL_INTROSPECT_SERVICE (self), FALSE);

        g_debug ("KioskShellIntrospectService: Starting");
        self->bus_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                       KIOSK_SHELL_INTROSPECT_SERVICE_BUS_NAME,
                                       G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                       (GBusAcquiredCallback) on_user_bus_acquired,
                                       (GBusNameAcquiredCallback) on_bus_name_acquired,
                                       (GBusNameVanishedCallback) on_bus_name_lost,
                                       self,
                                       NULL);

        setup_bus_watcher (self);

        g_set_weak_pointer (&self->tracker,
                            kiosk_compositor_get_window_tracker (self->compositor));

        g_signal_connect (self->tracker, "tracked-windows-changed",
                          G_CALLBACK (on_windows_changed),
                          self);
        g_signal_connect (self->tracker, "notify::focused-app",
                          G_CALLBACK (on_focused_app_changed),
                          self);
        g_signal_connect (self->monitor_manager,
                          "monitors-changed",
                          G_CALLBACK (on_monitors_changed),
                          self);

        kiosk_shell_introspect_dbus_service_set_animations_enabled (
                KIOSK_SHELL_INTROSPECT_DBUS_SERVICE (self),
                KIOSK_SHELL_INTROSPECT_SERVICE_HAS_ANIMATIONS_ENABLED);
        kiosk_shell_introspect_dbus_service_set_version (
                KIOSK_SHELL_INTROSPECT_DBUS_SERVICE (self),
                KIOSK_SHELL_INTROSPECT_SERVICE_VERSION);
        kiosk_shell_introspect_service_update_screen_size (self);

        return TRUE;
}

void
kiosk_shell_introspect_service_stop (KioskShellIntrospectService *self)
{
        g_return_if_fail (KIOSK_IS_SHELL_INTROSPECT_SERVICE (self));

        g_debug ("KioskShellIntrospectService: Stopping");

        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self));
        g_clear_handle_id (&self->bus_id, g_bus_unown_name);
}
