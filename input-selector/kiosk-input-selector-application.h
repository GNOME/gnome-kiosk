#pragma once

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS
#define KIOSK_TYPE_INPUT_SELECTOR_APPLICATION (kiosk_input_selector_application_get_type ())

G_DECLARE_FINAL_TYPE (KioskInputSelectorApplication, kiosk_input_selector_application,
                      KIOSK, INPUT_SELECTOR_APPLICATION,
                      GtkApplication)

KioskInputSelectorApplication *kiosk_input_selector_application_new (void);

G_END_DECLS
