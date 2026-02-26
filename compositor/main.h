#pragma once

#include <glib.h>

G_BEGIN_DECLS

gboolean is_vt_switch_enabled (void);
gboolean are_animations_forced (void);
gboolean is_no_cursor_enabled (void);

G_END_DECLS
