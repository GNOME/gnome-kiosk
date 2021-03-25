#pragma once

#include <glib-object.h>

#include "kiosk-input-source-group.h"

typedef struct _KioskCompositor KioskCompositor;

G_BEGIN_DECLS

#define KIOSK_TYPE_INPUT_SOURCES_MANAGER (kiosk_input_sources_manager_get_type ())

G_DECLARE_FINAL_TYPE (KioskInputSourcesManager,
                      kiosk_input_sources_manager,
                      KIOSK, INPUT_SOURCES_MANAGER,
                      GObject)

KioskInputSourcesManager *kiosk_input_sources_manager_new (KioskCompositor *compositor);

void kiosk_input_sources_manager_clear_input_sources (KioskInputSourcesManager *self);
gboolean kiosk_input_sources_manager_set_input_sources_from_locales (KioskInputSourcesManager *self,
                                                                     const char * const       *locales,
                                                                     const char               *options);
gboolean kiosk_input_sources_manager_set_input_sources_from_session_configuration (KioskInputSourcesManager *manager);

void kiosk_input_sources_manager_add_layout (KioskInputSourcesManager *self,
                                             const char               *layout,
                                             const char               *options);

G_END_DECLS
