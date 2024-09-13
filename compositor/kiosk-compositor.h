#pragma once

#include <glib-object.h>
#include <meta/meta-plugin.h>

#include "kiosk-backgrounds.h"
#include "kiosk-input-sources-manager.h"
#include "kiosk-service.h"
#include "kiosk-app-system.h"
#include "kiosk-window-tracker.h"

G_BEGIN_DECLS
#define KIOSK_TYPE_COMPOSITOR (kiosk_compositor_get_type ())

G_DECLARE_FINAL_TYPE (KioskCompositor, kiosk_compositor,
                      KIOSK, COMPOSITOR,
                      MetaPlugin);

KioskBackgrounds *kiosk_compositor_get_backgrounds (KioskCompositor *compositor);
KioskInputSourcesManager *kiosk_compositor_get_input_sources_manager (KioskCompositor *compositor);
KioskService *kiosk_compositor_get_service (KioskCompositor *compositor);
KioskAppSystem *kiosk_compositor_get_app_system (KioskCompositor *compositor);
KioskWindowTracker *kiosk_compositor_get_window_tracker (KioskCompositor *compositor);

G_END_DECLS
