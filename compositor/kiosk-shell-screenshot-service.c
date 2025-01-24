#include "config.h"
#include "kiosk-shell-screenshot-service.h"

#include <stdlib.h>
#include <string.h>
#include <meta/display.h>
#include <meta/meta-context.h>

#include "kiosk-compositor.h"
#include "kiosk-screenshot.h"

#define KIOSK_SHELL_SCREENSHOT_SERVICE_BUS_NAME "org.gnome.Shell.Screenshot"
#define KIOSK_SHELL_SCREENSHOT_SERVICE_OBJECT_PATH "/org/gnome/Shell/Screenshot"

struct _KioskShellScreenshotService
{
        KioskShellScreenshotDBusServiceSkeleton parent;

        /* weak references */
        KioskCompositor                        *compositor;
        MetaDisplay                            *display;
        MetaContext                            *context;

        /* strong references */
        GCancellable                           *cancellable;
        KioskScreenshot                        *screenshot;

        /* handles */
        guint                                   bus_id;
};

struct KioskShellScreenshotCompletion
{
        KioskShellScreenshotDBusService *service;
        GDBusMethodInvocation           *invocation;

        gpointer                         data;
};

enum
{
        PROP_COMPOSITOR = 1,
        NUMBER_OF_PROPERTIES
};

static GParamSpec *kiosk_shell_screenshot_service_properties[NUMBER_OF_PROPERTIES] = { NULL, };

static void kiosk_shell_screenshot_dbus_service_interface_init (KioskShellScreenshotDBusServiceIface *interface);

G_DEFINE_TYPE_WITH_CODE (KioskShellScreenshotService,
                         kiosk_shell_screenshot_service,
                         KIOSK_TYPE_SHELL_SCREENSHOT_DBUS_SERVICE_SKELETON,
                         G_IMPLEMENT_INTERFACE (KIOSK_TYPE_SHELL_SCREENSHOT_DBUS_SERVICE,
                                                kiosk_shell_screenshot_dbus_service_interface_init));

static void kiosk_shell_screenshot_service_set_property (GObject      *object,
                                                         guint         property_id,
                                                         const GValue *value,
                                                         GParamSpec   *param_spec);

static void kiosk_shell_screenshot_service_constructed (GObject *object);
static void kiosk_shell_screenshot_service_dispose (GObject *object);

static void
kiosk_shell_screenshot_service_class_init (KioskShellScreenshotServiceClass *shell_service_class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (shell_service_class);

        object_class->constructed = kiosk_shell_screenshot_service_constructed;
        object_class->set_property = kiosk_shell_screenshot_service_set_property;
        object_class->dispose = kiosk_shell_screenshot_service_dispose;

        kiosk_shell_screenshot_service_properties[PROP_COMPOSITOR] =
                g_param_spec_object ("compositor",
                                     NULL,
                                     NULL,
                                     KIOSK_TYPE_COMPOSITOR,
                                     G_PARAM_CONSTRUCT_ONLY
                                     | G_PARAM_WRITABLE
                                     | G_PARAM_STATIC_NAME);
        g_object_class_install_properties (object_class,
                                           NUMBER_OF_PROPERTIES,
                                           kiosk_shell_screenshot_service_properties);
}

static void
kiosk_shell_screenshot_service_set_property (GObject      *object,
                                             guint         property_id,
                                             const GValue *value,
                                             GParamSpec   *param_spec)
{
        KioskShellScreenshotService *self = KIOSK_SHELL_SCREENSHOT_SERVICE (object);

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
kiosk_shell_screenshot_service_dispose (GObject *object)
{
        KioskShellScreenshotService *self = KIOSK_SHELL_SCREENSHOT_SERVICE (object);

        kiosk_shell_screenshot_service_stop (self);

        g_clear_object (&self->screenshot);
        g_clear_weak_pointer (&self->context);
        g_clear_weak_pointer (&self->display);
        g_clear_weak_pointer (&self->compositor);

        G_OBJECT_CLASS (kiosk_shell_screenshot_service_parent_class)->dispose (object);
}

static void
kiosk_shell_screenshot_service_constructed (GObject *object)
{
        KioskShellScreenshotService *self = KIOSK_SHELL_SCREENSHOT_SERVICE (object);

        self->screenshot = kiosk_screenshot_new (self->compositor);

        g_set_weak_pointer (&self->display, meta_plugin_get_display (META_PLUGIN (self->compositor)));
        g_set_weak_pointer (&self->context, meta_display_get_context (self->display));

        G_OBJECT_CLASS (kiosk_shell_screenshot_service_parent_class)->constructed (object);
}

static gboolean
kiosk_shell_screenshot_check_access (KioskShellScreenshotService *self,
                                     const char                  *client_unique_name)
{
        GValue value = G_VALUE_INIT;
        gboolean unsafe_mode;

        g_object_get_property (G_OBJECT (self->context), "unsafe-mode", &value);
        unsafe_mode = g_value_get_boolean (&value);
        g_debug ("KioskShellScreenshotService: unsafe-mode is %s",
                 unsafe_mode ? "TRUE" : "FALSE");

        return unsafe_mode;
}

static void
completion_dispose (struct KioskShellScreenshotCompletion *completion)
{
        g_object_unref (completion->service);
        g_object_unref (completion->invocation);
        g_free (completion->data);

        g_free (completion);
}

static struct KioskShellScreenshotCompletion *
completion_new (KioskShellScreenshotDBusService *service,
                GDBusMethodInvocation           *invocation,
                const char                      *filename)
{
        struct KioskShellScreenshotCompletion *completion;

        completion = g_new0 (struct KioskShellScreenshotCompletion, 1);
        completion->service = g_object_ref (service);
        completion->invocation = g_object_ref (invocation);
        completion->data = g_strdup (filename);

        return completion;
}

static gboolean
kiosk_shell_screenshot_service_handle_flash_area (KioskShellScreenshotDBusService *object,
                                                  GDBusMethodInvocation           *invocation,
                                                  gint                             arg_x,
                                                  gint                             arg_y,
                                                  gint                             arg_width,
                                                  gint                             arg_height)
{
        KioskShellScreenshotService *self = KIOSK_SHELL_SCREENSHOT_SERVICE (object);
        const char *client_unique_name = g_dbus_method_invocation_get_sender (invocation);

        g_debug ("KioskShellScreenshotService: Handling FlashArea(x=%i, y=%i, w=%i, h=%i) from %s",
                 arg_x, arg_y, arg_width, arg_height, client_unique_name);

        if (!kiosk_shell_screenshot_check_access (self, client_unique_name)) {
                g_dbus_method_invocation_return_error (invocation,
                                                       G_DBUS_ERROR,
                                                       G_DBUS_ERROR_ACCESS_DENIED,
                                                       "Permission denied");
                return G_DBUS_METHOD_INVOCATION_HANDLED;
        }

        g_dbus_method_invocation_return_error (invocation,
                                               G_DBUS_ERROR,
                                               G_DBUS_ERROR_NOT_SUPPORTED,
                                               "FlashArea is not supported");

        return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
kiosk_shell_screenshot_service_handle_interactive_screenshot (KioskShellScreenshotDBusService *object,
                                                              GDBusMethodInvocation           *invocation)
{
        KioskShellScreenshotService *self = KIOSK_SHELL_SCREENSHOT_SERVICE (object);
        const char *client_unique_name = g_dbus_method_invocation_get_sender (invocation);

        g_debug ("KioskShellScreenshotService: Handling InteractiveScreenshot() from %s",
                 client_unique_name);

        if (!kiosk_shell_screenshot_check_access (self, client_unique_name)) {
                g_dbus_method_invocation_return_error (invocation,
                                                       G_DBUS_ERROR,
                                                       G_DBUS_ERROR_ACCESS_DENIED,
                                                       "Permission denied");
                return G_DBUS_METHOD_INVOCATION_HANDLED;
        }

        g_dbus_method_invocation_return_error (invocation,
                                               G_DBUS_ERROR,
                                               G_DBUS_ERROR_NOT_SUPPORTED,
                                               "InteractiveScreenshot is not supported");

        return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static void
screenshot_ready_callback (GObject      *source_object,
                           GAsyncResult *result,
                           gpointer      data)
{
        struct KioskShellScreenshotCompletion *completion = data;
        KioskShellScreenshotService *self = KIOSK_SHELL_SCREENSHOT_SERVICE (completion->service);
        g_autoptr (GError) error = NULL;
        gboolean success = TRUE;

        kiosk_screenshot_screenshot_finish (self->screenshot,
                                            result,
                                            NULL,
                                            &error);

        if (error) {
                g_warning ("Screenshot failed: %s", error->message);
                success = FALSE;
        }

        kiosk_shell_screenshot_dbus_service_complete_screenshot (completion->service,
                                                                 completion->invocation,
                                                                 success,
                                                                 completion->data);

        completion_dispose (completion);
}

static gboolean
kiosk_shell_screenshot_service_handle_screenshot (KioskShellScreenshotDBusService *object,
                                                  GDBusMethodInvocation           *invocation,
                                                  gboolean                         arg_include_cursor,
                                                  gboolean                         arg_flash,
                                                  const gchar                     *arg_filename)
{
        KioskShellScreenshotService *self = KIOSK_SHELL_SCREENSHOT_SERVICE (object);
        const char *client_unique_name = g_dbus_method_invocation_get_sender (invocation);
        struct KioskShellScreenshotCompletion *completion;
        g_autoptr (GFile) file = NULL;
        g_autoptr (GFileOutputStream) stream = NULL;
        g_autoptr (GError) error = NULL;
        g_autoptr (GAsyncResult) result = NULL;

        g_debug ("KioskShellScreenshotService: Handling Screenshot(cursor=%i, flash=%i, file='%s') from %s",
                 arg_include_cursor, arg_flash, arg_filename, client_unique_name);

        if (!kiosk_shell_screenshot_check_access (self, client_unique_name)) {
                g_dbus_method_invocation_return_error (invocation,
                                                       G_DBUS_ERROR,
                                                       G_DBUS_ERROR_ACCESS_DENIED,
                                                       "Permission denied");
                return G_DBUS_METHOD_INVOCATION_HANDLED;
        }

        file = g_file_new_for_path (arg_filename);
        stream = g_file_create (file, G_FILE_CREATE_NONE, NULL, &error);
        if (error) {
                g_dbus_method_invocation_return_error (invocation,
                                                       G_DBUS_ERROR,
                                                       G_DBUS_ERROR_FAILED,
                                                       "Error creating file: %s",
                                                       error->message);

                return G_DBUS_METHOD_INVOCATION_HANDLED;
        }

        completion = completion_new (object, invocation, arg_filename);
        kiosk_screenshot_screenshot (self->screenshot,
                                     arg_include_cursor,
                                     G_OUTPUT_STREAM (stream),
                                     screenshot_ready_callback,
                                     completion);

        return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static void
screenshot_area_ready_callback (GObject      *source_object,
                                GAsyncResult *result,
                                gpointer      data)
{
        struct KioskShellScreenshotCompletion *completion = data;
        KioskShellScreenshotService *self = KIOSK_SHELL_SCREENSHOT_SERVICE (completion->service);
        g_autoptr (GError) error = NULL;
        gboolean success = TRUE;

        kiosk_screenshot_screenshot_area_finish (self->screenshot,
                                                 result,
                                                 NULL,
                                                 &error);

        if (error) {
                g_warning ("Screenshot area failed: %s", error->message);
                success = FALSE;
        }

        kiosk_shell_screenshot_dbus_service_complete_screenshot_area (completion->service,
                                                                      completion->invocation,
                                                                      success,
                                                                      completion->data);

        completion_dispose (completion);
}

static gboolean
kiosk_shell_screenshot_service_handle_screenshot_area (KioskShellScreenshotDBusService *object,
                                                       GDBusMethodInvocation           *invocation,
                                                       gint                             arg_x,
                                                       gint                             arg_y,
                                                       gint                             arg_width,
                                                       gint                             arg_height,
                                                       gboolean                         arg_flash,
                                                       const gchar                     *arg_filename)
{
        KioskShellScreenshotService *self = KIOSK_SHELL_SCREENSHOT_SERVICE (object);
        const char *client_unique_name = g_dbus_method_invocation_get_sender (invocation);
        struct KioskShellScreenshotCompletion *completion;
        g_autoptr (GFile) file = NULL;
        g_autoptr (GFileOutputStream) stream = NULL;
        g_autoptr (GError) error = NULL;
        g_autoptr (GAsyncResult) result = NULL;

        g_debug ("KioskShellScreenshotService: Handling ScreenshotArea(x=%i, y=%i, w=%i, h=%i, flash=%i, file='%s') from %s",
                 arg_x, arg_y, arg_width, arg_height, arg_flash, arg_filename, client_unique_name);

        if (!kiosk_shell_screenshot_check_access (self, client_unique_name)) {
                g_dbus_method_invocation_return_error (invocation,
                                                       G_DBUS_ERROR,
                                                       G_DBUS_ERROR_ACCESS_DENIED,
                                                       "Permission denied");
                return G_DBUS_METHOD_INVOCATION_HANDLED;
        }

        file = g_file_new_for_path (arg_filename);
        stream = g_file_create (file, G_FILE_CREATE_NONE, NULL, &error);
        if (error) {
                g_dbus_method_invocation_return_error (invocation,
                                                       G_DBUS_ERROR,
                                                       G_DBUS_ERROR_FAILED,
                                                       "Error creating file: %s",
                                                       error->message);

                return G_DBUS_METHOD_INVOCATION_HANDLED;
        }

        completion = completion_new (object, invocation, arg_filename);
        kiosk_screenshot_screenshot_area (self->screenshot,
                                          arg_x,
                                          arg_y,
                                          arg_width,
                                          arg_height,
                                          G_OUTPUT_STREAM (stream),
                                          screenshot_area_ready_callback,
                                          completion);

        return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static void
screenshot_window_ready_callback (GObject      *source_object,
                                  GAsyncResult *result,
                                  gpointer      data)
{
        struct KioskShellScreenshotCompletion *completion = data;
        KioskShellScreenshotService *self = KIOSK_SHELL_SCREENSHOT_SERVICE (completion->service);
        g_autoptr (GError) error = NULL;
        gboolean success = TRUE;

        kiosk_screenshot_screenshot_window_finish (self->screenshot,
                                                   result,
                                                   NULL,
                                                   &error);

        if (error) {
                g_warning ("Screenshot window failed: %s", error->message);
                success = FALSE;
        }

        kiosk_shell_screenshot_dbus_service_complete_screenshot_window (completion->service,
                                                                        completion->invocation,
                                                                        success,
                                                                        completion->data);

        completion_dispose (completion);
}

static gboolean
kiosk_shell_screenshot_service_handle_screenshot_window (KioskShellScreenshotDBusService *object,
                                                         GDBusMethodInvocation           *invocation,
                                                         gboolean                         arg_include_frame,
                                                         gboolean                         arg_include_cursor,
                                                         gboolean                         arg_flash,
                                                         const gchar                     *arg_filename)
{
        KioskShellScreenshotService *self = KIOSK_SHELL_SCREENSHOT_SERVICE (object);
        const char *client_unique_name = g_dbus_method_invocation_get_sender (invocation);
        struct KioskShellScreenshotCompletion *completion;
        g_autoptr (GFile) file = NULL;
        g_autoptr (GFileOutputStream) stream = NULL;
        g_autoptr (GError) error = NULL;
        g_autoptr (GAsyncResult) result = NULL;

        g_debug ("KioskShellScreenshotService: Handling ScreenshotWindow(frame=%i, cursor=%i, flash=%i, file='%s') from %s",
                 arg_include_frame, arg_include_cursor, arg_flash, arg_filename, client_unique_name);

        if (!kiosk_shell_screenshot_check_access (self, client_unique_name)) {
                g_dbus_method_invocation_return_error (invocation,
                                                       G_DBUS_ERROR,
                                                       G_DBUS_ERROR_ACCESS_DENIED,
                                                       "Permission denied");
                return G_DBUS_METHOD_INVOCATION_HANDLED;
        }

        file = g_file_new_for_path (arg_filename);
        stream = g_file_create (file, G_FILE_CREATE_NONE, NULL, &error);
        if (error) {
                g_dbus_method_invocation_return_error (invocation,
                                                       G_DBUS_ERROR,
                                                       G_DBUS_ERROR_FAILED,
                                                       "Error creating file: %s",
                                                       error->message);

                return G_DBUS_METHOD_INVOCATION_HANDLED;
        }

        completion = completion_new (object, invocation, arg_filename);
        kiosk_screenshot_screenshot_window (self->screenshot,
                                            arg_include_frame,
                                            arg_include_cursor,
                                            G_OUTPUT_STREAM (stream),
                                            screenshot_window_ready_callback,
                                            completion);

        return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
kiosk_shell_screenshot_service_handle_select_area (KioskShellScreenshotDBusService *object,
                                                   GDBusMethodInvocation           *invocation)
{
        KioskShellScreenshotService *self = KIOSK_SHELL_SCREENSHOT_SERVICE (object);
        const char *client_unique_name = g_dbus_method_invocation_get_sender (invocation);

        g_debug ("KioskShellScreenshotService: Handling SelectArea() from %s",
                 client_unique_name);

        if (!kiosk_shell_screenshot_check_access (self, client_unique_name)) {
                g_dbus_method_invocation_return_error (invocation,
                                                       G_DBUS_ERROR,
                                                       G_DBUS_ERROR_ACCESS_DENIED,
                                                       "Permission denied");
                return G_DBUS_METHOD_INVOCATION_HANDLED;
        }

        g_dbus_method_invocation_return_error (invocation,
                                               G_DBUS_ERROR,
                                               G_DBUS_ERROR_NOT_SUPPORTED,
                                               "SelectArea is not supported");

        return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static void
kiosk_shell_screenshot_dbus_service_interface_init (KioskShellScreenshotDBusServiceIface *interface)
{
        interface->handle_flash_area =
                kiosk_shell_screenshot_service_handle_flash_area;
        interface->handle_interactive_screenshot =
                kiosk_shell_screenshot_service_handle_interactive_screenshot;
        interface->handle_screenshot =
                kiosk_shell_screenshot_service_handle_screenshot;
        interface->handle_screenshot_area =
                kiosk_shell_screenshot_service_handle_screenshot_area;
        interface->handle_screenshot_window =
                kiosk_shell_screenshot_service_handle_screenshot_window;
        interface->handle_select_area =
                kiosk_shell_screenshot_service_handle_select_area;
}

static void
kiosk_shell_screenshot_service_init (KioskShellScreenshotService *self)
{
        g_debug ("KioskShellScreenshotService: Initializing");
}

KioskShellScreenshotService *
kiosk_shell_screenshot_service_new (KioskCompositor *compositor)
{
        GObject *object;

        object = g_object_new (KIOSK_TYPE_SHELL_SCREENSHOT_SERVICE,
                               "compositor", compositor,
                               NULL);

        return KIOSK_SHELL_SCREENSHOT_SERVICE (object);
}

static void
on_user_bus_acquired (GDBusConnection             *connection,
                      const char                  *unique_name,
                      KioskShellScreenshotService *self)
{
        g_autoptr (GError) error = NULL;

        g_debug ("KioskShellScreenshotService: Connected to user bus");

        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
                                          connection,
                                          KIOSK_SHELL_SCREENSHOT_SERVICE_OBJECT_PATH,
                                          &error);

        if (error != NULL) {
                g_debug ("KioskShellScreenshotService: Could not export interface skeleton: %s",
                         error->message);
                g_clear_error (&error);
        }
}

static void
on_bus_name_acquired (GDBusConnection             *connection,
                      const char                  *name,
                      KioskShellScreenshotService *self)
{
        if (g_strcmp0 (name, KIOSK_SHELL_SCREENSHOT_SERVICE_BUS_NAME) != 0) {
                return;
        }

        g_debug ("KioskShellScreenshotService: Acquired name %s", name);
}

static void
on_bus_name_lost (GDBusConnection             *connection,
                  const char                  *name,
                  KioskShellScreenshotService *self)
{
        if (g_strcmp0 (name, KIOSK_SHELL_SCREENSHOT_SERVICE_BUS_NAME) != 0) {
                return;
        }

        g_debug ("KioskShellScreenshotService: Lost name %s", name);
}

gboolean
kiosk_shell_screenshot_service_start (KioskShellScreenshotService *self,
                                      GError                     **error)
{
        g_return_val_if_fail (KIOSK_IS_SHELL_SCREENSHOT_SERVICE (self), FALSE);

        g_debug ("KioskShellScreenshotService: Starting");
        self->bus_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                       KIOSK_SHELL_SCREENSHOT_SERVICE_BUS_NAME,
                                       G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                       (GBusAcquiredCallback) on_user_bus_acquired,
                                       (GBusNameAcquiredCallback) on_bus_name_acquired,
                                       (GBusNameVanishedCallback) on_bus_name_lost,
                                       self,
                                       NULL);

        return TRUE;
}

void
kiosk_shell_screenshot_service_stop (KioskShellScreenshotService *self)
{
        g_return_if_fail (KIOSK_IS_SHELL_SCREENSHOT_SERVICE (self));

        g_debug ("KioskShellScreenshotService: Stopping");

        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self));
        g_clear_handle_id (&self->bus_id, g_bus_unown_name);
}
