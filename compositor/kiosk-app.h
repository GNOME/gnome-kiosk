#pragma once

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <meta/window.h>

typedef struct _KioskCompositor KioskCompositor;

G_BEGIN_DECLS

#define KIOSK_TYPE_APP (kiosk_app_get_type ())
G_DECLARE_FINAL_TYPE (KioskApp,
                      kiosk_app,
                      KIOSK,
                      APP,
                      GObject)

typedef enum
{
        KIOSK_APP_STATE_STOPPED,
        KIOSK_APP_STATE_RUNNING
} KioskAppState;

/* Window iterator */
typedef struct _KioskAppWindowIter
{
        GSList *current;
} KioskAppWindowIter;

void     kiosk_app_window_iter_init (KioskAppWindowIter *iter,
                                     KioskApp           *app);
gboolean kiosk_app_window_iter_next (KioskAppWindowIter *iter,
                                     MetaWindow        **window);

/* Process iterator */
typedef struct _KioskAppProcessIter
{
        KioskAppWindowIter window_iter;
        GHashTable        *seen_pids;
} KioskAppProcessIter;

void     kiosk_app_process_iter_init (KioskAppProcessIter *iter,
                                      KioskApp            *app);
gboolean kiosk_app_process_iter_next (KioskAppProcessIter *iter,
                                      pid_t               *pid);

const char *kiosk_app_get_id (KioskApp *app);
KioskAppState   kiosk_app_get_state (KioskApp *app);
int             kiosk_app_compare (KioskApp *app,
                                   KioskApp *other);
const char *kiosk_app_get_sandbox_id (KioskApp *app);
void            kiosk_app_add_window (KioskApp   *app,
                                      MetaWindow *window);
void            kiosk_app_remove_window (KioskApp   *app,
                                         MetaWindow *window);
KioskApp *kiosk_app_new_for_window (KioskCompositor *compositor,
                                    MetaWindow      *window);
KioskApp *kiosk_app_new (KioskCompositor *compositor,
                         GDesktopAppInfo *info);
G_END_DECLS
