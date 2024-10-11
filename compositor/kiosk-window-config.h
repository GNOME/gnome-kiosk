#pragma once

#include <glib-object.h>
#include <glib.h>

#include <meta/window.h>
#include <meta/meta-window-config.h>

G_BEGIN_DECLS

#define KIOSK_TYPE_WINDOW_CONFIG (kiosk_window_config_get_type ())
G_DECLARE_FINAL_TYPE (KioskWindowConfig, kiosk_window_config,
                      KIOSK, WINDOW_CONFIG, GObject)

KioskWindowConfig *kiosk_window_config_new (void);

void kiosk_window_config_update_window (KioskWindowConfig *kiosk_window_config,
                                        MetaWindow        *window,
                                        MetaWindowConfig  *window_config);

G_END_DECLS
