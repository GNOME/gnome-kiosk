#pragma once

#include <glib-object.h>

typedef struct _KioskInputSourcesManager KioskInputSourcesManager;

G_BEGIN_DECLS

#define KIOSK_TYPE_INPUT_ENGINE_MANAGER (kiosk_input_engine_manager_get_type ())

G_DECLARE_FINAL_TYPE (KioskInputEngineManager,
                      kiosk_input_engine_manager,
                      KIOSK, INPUT_ENGINE_MANAGER,
                      GObject);

KioskInputEngineManager *kiosk_input_engine_manager_new (KioskInputSourcesManager *manager);
gboolean kiosk_input_engine_manager_is_loaded (KioskInputEngineManager *self);
const char *kiosk_input_engine_manager_get_active_engine (KioskInputEngineManager *self);

gboolean kiosk_input_engine_manager_find_layout_for_engine (KioskInputEngineManager *manager,
                                                            const char              *engine_name,
                                                            const char             **layout,
                                                            const char             **variant);
gboolean kiosk_input_engine_manager_describe_engine (KioskInputEngineManager *manager,
                                                     const char              *engine_name,
                                                     char                   **short_description,
                                                     char                   **full_description);
gboolean kiosk_input_engine_manager_activate_engine (KioskInputEngineManager *manager,
                                                     const char              *engine_name);

G_END_DECLS
