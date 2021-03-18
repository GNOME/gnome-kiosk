#pragma once

#include <glib-object.h>

typedef struct _KioskInputSourcesManager KioskInputSourcesManager;

G_BEGIN_DECLS

#define KIOSK_TYPE_INPUT_SOURCE_GROUP (kiosk_input_source_group_get_type ())

G_DECLARE_FINAL_TYPE (KioskInputSourceGroup,
                      kiosk_input_source_group,
                      KIOSK, INPUT_SOURCE_GROUP,
                      GObject)

KioskInputSourceGroup *kiosk_input_source_group_new (KioskInputSourcesManager *manager);
gboolean kiosk_input_source_group_add_layout (KioskInputSourceGroup *input_sources,
                                        const char       *layout,
                                        const char       *variant);
char *kiosk_input_source_group_get_selected_layout (KioskInputSourceGroup *input_sources);
char **kiosk_input_source_group_get_layouts (KioskInputSourceGroup *input_sources);

void kiosk_input_source_group_set_options (KioskInputSourceGroup *input_sources,
                                     const char       *options);

gboolean kiosk_input_source_group_activate (KioskInputSourceGroup *input_sources);

gboolean kiosk_input_source_group_switch_to_layout (KioskInputSourceGroup *input_sources,
                                                    const char            *layout_name);
void kiosk_input_source_group_switch_to_first_layout (KioskInputSourceGroup *input_sources);
void kiosk_input_source_group_switch_to_last_layout (KioskInputSourceGroup *input_sources);
gboolean kiosk_input_source_group_switch_to_next_layout (KioskInputSourceGroup *input_sources);
gboolean kiosk_input_source_group_switch_to_previous_layout (KioskInputSourceGroup *input_sources);
G_END_DECLS
