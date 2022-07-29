#pragma once

#include <glib-object.h>
#include <meta/meta-plugin.h>

#include "kiosk-backgrounds.h"
#include "kiosk-input-sources-manager.h"
#include "kiosk-service.h"

G_BEGIN_DECLS
#define KIOSK_TYPE_COMPOSITOR (kiosk_compositor_get_type ())

G_DECLARE_FINAL_TYPE (KioskCompositor, kiosk_compositor,
                      KIOSK, COMPOSITOR,
                      MetaPlugin);

KioskBackgrounds *kiosk_compositor_get_backgrounds (KioskCompositor *compositor);
KioskInputSourcesManager *kiosk_compositor_get_input_sources_manager (KioskCompositor *compositor);
KioskService *kiosk_compositor_get_service (KioskCompositor *compositor);
G_END_DECLS
