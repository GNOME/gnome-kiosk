#pragma once

#include <glib-object.h>

#include "org.gnome.SessionManager.Presence.h"

typedef struct _KioskCompositor KioskCompositor;

G_BEGIN_DECLS

#define KIOSK_TYPE_SESSION_PRESENCE (kiosk_session_presence_get_type ())

G_DECLARE_FINAL_TYPE (KioskSessionPresence,
                      kiosk_session_presence,
                      KIOSK, SESSION_PRESENCE,
                      GObject);

/* Presence status values (from gnome-session) */
typedef enum
{
        GSM_PRESENCE_STATUS_AVAILABLE = 0,
        GSM_PRESENCE_STATUS_INVISIBLE,
        GSM_PRESENCE_STATUS_BUSY,
        GSM_PRESENCE_STATUS_IDLE,
} GsmPresenceStatus;

#define GSM_TYPE_PRESENCE_STATUS (gsm_presence_status_get_type ())
GType gsm_presence_status_get_type (void);

KioskSessionPresence *kiosk_session_presence_new (KioskCompositor *compositor);
gboolean kiosk_session_presence_start (KioskSessionPresence *self,
                                       GError              **error);
void kiosk_session_presence_stop (KioskSessionPresence *self);
GsmPresenceStatus kiosk_session_presence_get_status (KioskSessionPresence *self);

G_END_DECLS
