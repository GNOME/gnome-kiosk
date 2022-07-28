#pragma once

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef void (* KioskObjectCallback) (GObject  *self,
                                      gpointer  user_data);
#define KIOSK_OBJECT_CALLBACK(_callback) ((KioskObjectCallback) _callback)

void kiosk_gobject_utils_queue_defer_callback (GObject             *self,
                                               const char          *name,
                                               GCancellable        *cancellable,
                                               KioskObjectCallback  callback,
                                               gpointer             user_data);
void kiosk_gobject_utils_queue_immediate_callback (GObject             *self,
                                                   const char          *name,
                                                   GCancellable        *cancellable,
                                                   KioskObjectCallback  callback,
                                                   gpointer             user_data);

G_END_DECLS
