#include "config.h"
#include "kiosk-gobject-utils.h"

#define COALESCE_INTERVAL 250 /* milliseconds */

static void
on_task_wait_complete (GObject *self,
                       GTask   *task)
{
        KioskObjectCallback callback;
        gpointer user_data;
        gboolean completed;
        g_autofree char *data_key = NULL;

        g_debug ("KioskGObjectUtils: Executing queued task '%s'", g_task_get_name (task));

        callback = g_object_get_data (G_OBJECT (task), "callback");
        user_data = g_object_get_data (G_OBJECT (task), "user-data");

        completed = g_task_propagate_boolean (task, NULL);

        if (completed) {
                callback (self, user_data);
        }

        data_key = g_strdup_printf ("kiosk-gobject-utils-%p-%p-task",
                                    callback, user_data);

        g_object_set_data (G_OBJECT (self), data_key, NULL);
}

static gboolean
on_called_back (GTask *task)
{
        if (!g_task_return_error_if_cancelled (task)) {
                g_task_return_boolean (task, TRUE);
        }

        return G_SOURCE_REMOVE;
}

static void
kiosk_gobject_utils_queue_callback (GObject             *self,
                                    const char          *name,
                                    int                  timeout,
                                    GCancellable        *cancellable,
                                    KioskObjectCallback  callback,
                                    gpointer             user_data)
{
        g_autofree char *data_key = NULL;
        g_autoptr (GSource) source = NULL;
        GTask *task;

        g_return_if_fail (G_IS_OBJECT (self));
        g_return_if_fail (callback != NULL);

        data_key = g_strdup_printf ("kiosk-gobject-utils-%p-%p-task",
                                    callback, user_data);

        task = g_object_get_data (G_OBJECT (self), data_key);

        if (task != NULL) {
                return;
        }

        if (timeout <= 0)
                source = g_idle_source_new ();
        else
                source = g_timeout_source_new (timeout);

        task = g_task_new (self,
                           cancellable,
                           (GAsyncReadyCallback) on_task_wait_complete,
                           NULL);

        if (name != NULL) {
                g_task_set_name (task, name);

                if (timeout > 0)
                        g_debug ("KioskGObjectUtils: Deferring task '%s' for %dms", name, timeout);
                else
                        g_debug ("KioskGObjectUtils: Queuing task '%s' to run on next iteration of event loop", name);
        }

        g_task_attach_source (task, source, G_SOURCE_FUNC (on_called_back));

        g_object_set_data (G_OBJECT (task), "callback", callback);
        g_object_set_data (G_OBJECT (task), "user-data", user_data);

        g_object_set_data_full (G_OBJECT (self),
                                data_key,
                                task,
                                (GDestroyNotify)
                                g_object_unref);
}

void
kiosk_gobject_utils_queue_defer_callback (GObject             *self,
                                          const char          *name,
                                          GCancellable        *cancellable,
                                          KioskObjectCallback  callback,
                                          gpointer             user_data)
{
        kiosk_gobject_utils_queue_callback (self, name, COALESCE_INTERVAL, cancellable, callback, user_data);
}

void
kiosk_gobject_utils_queue_immediate_callback (GObject             *self,
                                              const char          *name,
                                              GCancellable        *cancellable,
                                              KioskObjectCallback  callback,
                                              gpointer             user_data)
{
        kiosk_gobject_utils_queue_callback (self, name, 0, cancellable, callback, user_data);
}
