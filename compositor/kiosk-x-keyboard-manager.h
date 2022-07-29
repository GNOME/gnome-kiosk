#pragma once

#include <glib-object.h>
#include <xkbcommon/xkbcommon.h>

typedef struct _KioskCompositor KioskCompositor;

G_BEGIN_DECLS

#define KIOSK_TYPE_X_KEYBOARD_MANAGER (kiosk_x_keyboard_manager_get_type ())

G_DECLARE_FINAL_TYPE (KioskXKeyboardManager,
                      kiosk_x_keyboard_manager,
                      KIOSK, X_KEYBOARD_MANAGER,
                      GObject);

KioskXKeyboardManager *kiosk_x_keyboard_manager_new (KioskCompositor *compositor);

const char * const *kiosk_x_keyboard_manager_get_layouts (KioskXKeyboardManager *manager);
const char *kiosk_x_keyboard_manager_get_selected_layout (KioskXKeyboardManager *manager);
const char *kiosk_x_keyboard_manager_get_options (KioskXKeyboardManager *manager);
gboolean kiosk_x_keyboard_manager_keymap_is_active (KioskXKeyboardManager *manager,
                                                    const char * const    *layouts,
                                                    const char * const    *variants,
                                                    const char            *options);
gboolean kiosk_x_keyboard_manager_layout_group_is_locked (KioskXKeyboardManager *manager,
                                                          xkb_layout_index_t     layout_index);

G_END_DECLS
