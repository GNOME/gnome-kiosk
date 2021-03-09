#pragma once

#include <glib-object.h>

typedef struct _KioskCompositor KioskCompositor;

G_BEGIN_DECLS

#define KIOSK_TYPE_INPUT_SOURCES_MANAGER (kiosk_input_sources_manager_get_type ())

G_DECLARE_FINAL_TYPE (KioskInputSourcesManager,
                      kiosk_input_sources_manager,
                      KIOSK, INPUT_SOURCES_MANAGER,
                      GObject)

KioskInputSourcesManager *kiosk_input_sources_manager_new (KioskCompositor *compositor);

G_END_DECLS
