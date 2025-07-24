#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "kiosk-window-config.h"

#define KIOSK_WINDOW_CONFIG_DIR      "gnome-kiosk"
#define KIOSK_WINDOW_CONFIG_FILENAME "window-config.ini"
#define KIOSK_WINDOW_CONFIG_GET_KEY_VALUE(f) ((KioskWindowConfigGetKeyValue) (f))

typedef gpointer (*KioskWindowConfigGetKeyValue) (GKeyFile   *key_file,
                                                  const char *section_name,
                                                  const char *key_name,
                                                  GError    **error);

struct _KioskWindowConfig
{
        GObject   parent;

        GKeyFile *config_key_file;
};

G_DEFINE_TYPE (KioskWindowConfig, kiosk_window_config, G_TYPE_OBJECT)

static gboolean
kiosk_window_config_try_load_file (KioskWindowConfig *kiosk_window_config,
                                   char              *filename)
{
        g_autoptr (GError) error = NULL;

        if (!g_key_file_load_from_file (kiosk_window_config->config_key_file,
                                        filename,
                                        G_KEY_FILE_NONE,
                                        &error)) {
                g_debug ("KioskWindowConfig: Error loading key file %s: %s",
                         filename, error->message);

                return FALSE;
        }

        return TRUE;
}

static gboolean
kiosk_window_config_load (KioskWindowConfig *kiosk_window_config)
{
        const char * const *xdg_data_dirs;
        g_autofree gchar *filename = NULL;
        int i;

        /* Try user config first */
        filename = g_build_filename (g_get_user_config_dir (),
                                     KIOSK_WINDOW_CONFIG_DIR,
                                     KIOSK_WINDOW_CONFIG_FILENAME, NULL);

        if (kiosk_window_config_try_load_file (kiosk_window_config, filename))
                goto out;

        /* Then system config */
        xdg_data_dirs = g_get_system_data_dirs ();
        for (i = 0; xdg_data_dirs[i]; i++) {
                filename = g_build_filename (xdg_data_dirs[i],
                                             KIOSK_WINDOW_CONFIG_DIR,
                                             KIOSK_WINDOW_CONFIG_FILENAME, NULL);

                if (kiosk_window_config_try_load_file (kiosk_window_config, filename))
                        goto out;
        }

        g_debug ("KioskWindowConfig: No configuration file found");

        return FALSE;
out:
        g_debug ("KioskWindowConfig: Loading key file %s", filename);

        return TRUE;
}

static void
kiosk_window_config_init (KioskWindowConfig *self)
{
        self->config_key_file = g_key_file_new ();
        kiosk_window_config_load (self);
}

static void
kiosk_window_config_dispose (GObject *object)
{
        KioskWindowConfig *self = KIOSK_WINDOW_CONFIG (object);

        g_clear_pointer (&self->config_key_file, g_key_file_free);

        G_OBJECT_CLASS (kiosk_window_config_parent_class)->dispose (object);
}

static void
kiosk_window_config_class_init (KioskWindowConfigClass *klass)
{
        GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

        gobject_class->dispose = kiosk_window_config_dispose;
}

#define KIOSK_WINDOW_CONFIG_CHECK_FUNC_VALUE(self, section, key, func, value) \
        G_STMT_START { \
                g_autoptr (GError) error = NULL; \
                if (!g_key_file_has_key (self->config_key_file, section, key, NULL)) { \
                        g_debug ("KioskWindowConfig: No key '%s' in section [%s]", \
                                 key, section); \
                        return FALSE; \
                } \
                *value = func (self->config_key_file, section, key, &error); \
                if (error) { \
                        g_debug ("KioskWindowConfig: Error with key '%s' in section [%s]: %s", \
                                 key, section, error->message); \
                        return FALSE; \
                } \
                return TRUE; \
        } G_STMT_END

static gboolean
kiosk_window_config_check_for_string_value (KioskWindowConfig *kiosk_window_config,
                                            const char        *section_name,
                                            const char        *key_name,
                                            char             **value)
{
        KIOSK_WINDOW_CONFIG_CHECK_FUNC_VALUE (kiosk_window_config,
                                              section_name,
                                              key_name,
                                              g_key_file_get_string,
                                              value);
}

static gboolean
kiosk_window_config_check_for_integer_value (KioskWindowConfig *kiosk_window_config,
                                             const char        *section_name,
                                             const char        *key_name,
                                             int               *value)
{
        KIOSK_WINDOW_CONFIG_CHECK_FUNC_VALUE (kiosk_window_config,
                                              section_name,
                                              key_name,
                                              g_key_file_get_integer,
                                              value);
}

static gboolean
kiosk_window_config_check_for_boolean_value (KioskWindowConfig *kiosk_window_config,
                                             const char        *section_name,
                                             const char        *key_name,
                                             gboolean          *value)
{
        KIOSK_WINDOW_CONFIG_CHECK_FUNC_VALUE (kiosk_window_config,
                                              section_name,
                                              key_name,
                                              g_key_file_get_boolean,
                                              value);
}

static void
kiosk_window_config_apply_config (KioskWindowConfig *kiosk_window_config,
                                  MetaWindowConfig  *window_config,
                                  const char        *section_name)
{
        MtkRectangle rect;
        int new_x, new_y, new_width, new_height;
        gboolean fullscreen;

        if (kiosk_window_config_check_for_boolean_value (kiosk_window_config,
                                                         section_name,
                                                         "set-fullscreen",
                                                         &fullscreen)) {
                g_debug ("KioskWindowConfig: Using 'set-fullscreen=%s' from section [%s]",
                         fullscreen ? "TRUE" : "FALSE", section_name);
                meta_window_config_set_is_fullscreen (window_config, fullscreen);
        }

        rect = meta_window_config_get_rect (window_config);

        if (kiosk_window_config_check_for_integer_value (kiosk_window_config,
                                                         section_name,
                                                         "set-x",
                                                         &new_x)) {
                g_debug ("KioskWindowConfig: Using 'set-x=%i' from section [%s]",
                         new_x, section_name);
                rect.x = new_x;
        }

        if (kiosk_window_config_check_for_integer_value (kiosk_window_config,
                                                         section_name,
                                                         "set-y",
                                                         &new_y)) {
                g_debug ("KioskWindowConfig: Using 'set-y=%i' from section [%s]",
                         new_y, section_name);
                rect.y = new_y;
        }

        if (kiosk_window_config_check_for_integer_value (kiosk_window_config,
                                                         section_name,
                                                         "set-width",
                                                         &new_width)) {
                g_debug ("KioskWindowConfig: Using 'set-width=%i' from section [%s]",
                         new_width, section_name);
                rect.width = new_width;
        }

        if (kiosk_window_config_check_for_integer_value (kiosk_window_config,
                                                         section_name,
                                                         "set-height",
                                                         &new_height)) {
                g_debug ("KioskWindowConfig: Using 'set-height=%i' from section [%s]",
                         new_height, section_name);
                rect.height = new_height;
        }

        meta_window_config_set_rect (window_config, rect);
}

static gboolean
kiosk_window_config_match_string_key (KioskWindowConfig *kiosk_window_config,
                                      const char        *section_name,
                                      const char        *key_name,
                                      const char        *value)
{
        g_autofree gchar *key_value = NULL;
        g_autoptr (GError) error = NULL;
        gboolean is_a_match = TRUE;

        /* Keys are used to filter out, no key means we have a match */
        if (!kiosk_window_config_check_for_string_value (kiosk_window_config,
                                                         section_name,
                                                         key_name,
                                                         &key_value))
                return TRUE;

        is_a_match = g_pattern_match_simple (key_value, value);
        g_debug ("KioskWindowConfig: Value '%s' %s key '%s=%s' from section [%s]",
                 value,
                 is_a_match ? "matches" : "does not match",
                 key_name,
                 key_value,
                 section_name);

        return is_a_match;
}

#define VALUE_OR_EMPTY(v) (v ? v : "")
static gboolean
kiosk_window_config_match_window (KioskWindowConfig *kiosk_window_config,
                                  MetaWindow        *window,
                                  const char        *section_name)
{
        const char *match_value;

        g_debug ("KioskWindowConfig: Checking section [%s]", section_name);

        match_value = VALUE_OR_EMPTY (meta_window_get_title (window));
        if (!kiosk_window_config_match_string_key (kiosk_window_config,
                                                   section_name,
                                                   "match-title",
                                                   match_value))
                return FALSE;

        match_value = VALUE_OR_EMPTY (meta_window_get_wm_class (window));
        if (!kiosk_window_config_match_string_key (kiosk_window_config,
                                                   section_name,
                                                   "match-class",
                                                   match_value))
                return FALSE;

        match_value = VALUE_OR_EMPTY (meta_window_get_sandboxed_app_id (window));
        if (!kiosk_window_config_match_string_key (kiosk_window_config,
                                                   section_name,
                                                   "match-sandboxed-app-id",
                                                   match_value))
                return FALSE;

        match_value = VALUE_OR_EMPTY (meta_window_get_tag (window));
        if (!kiosk_window_config_match_string_key (kiosk_window_config,
                                                   section_name,
                                                   "match-tag",
                                                   match_value))
                return FALSE;

        return TRUE;
}
#undef VALUE_OR_EMPTY

gboolean
kiosk_window_config_get_boolean_for_window (KioskWindowConfig *kiosk_window_config,
                                            MetaWindow        *window,
                                            const char        *key_name,
                                            gboolean          *value)
{
        g_auto (GStrv) sections;
        gsize length;
        gboolean key_found = FALSE;
        int i;

        sections = g_key_file_get_groups (kiosk_window_config->config_key_file, &length);
        for (i = 0; i < length; i++) {
                if (!kiosk_window_config_match_window (kiosk_window_config,
                                                       window,
                                                       sections[i]))
                        continue;

                if (kiosk_window_config_check_for_boolean_value (kiosk_window_config,
                                                                 sections[i],
                                                                 key_name,
                                                                 value)) {
                        g_debug ("KioskWindowConfig: Using '%s=%s' from section [%s]",
                                 key_name, *value ? "TRUE" : "FALSE", sections[i]);

                        key_found = TRUE;
                }
        }

        return key_found;
}

gboolean
kiosk_window_config_get_string_for_window (KioskWindowConfig *kiosk_window_config,
                                           MetaWindow        *window,
                                           const char        *key_name,
                                           char             **value)
{
        g_auto (GStrv) sections;
        gsize length;
        gboolean key_found = FALSE;
        int i;

        sections = g_key_file_get_groups (kiosk_window_config->config_key_file, &length);
        for (i = 0; i < length; i++) {
                if (!kiosk_window_config_match_window (kiosk_window_config,
                                                       window,
                                                       sections[i]))
                        continue;

                if (kiosk_window_config_check_for_string_value (kiosk_window_config,
                                                                sections[i],
                                                                key_name,
                                                                value)) {
                        g_debug ("KioskWindowConfig: Using '%s=%s' from section [%s]",
                                 key_name, *value, sections[i]);

                        key_found = TRUE;
                }
        }

        return key_found;
}

void
kiosk_window_config_update_window (KioskWindowConfig *kiosk_window_config,
                                   MetaWindow        *window,
                                   MetaWindowConfig  *window_config)
{
        g_auto (GStrv) sections;
        gsize length;
        int i;

        sections = g_key_file_get_groups (kiosk_window_config->config_key_file, &length);
        for (i = 0; i < length; i++) {
                if (!kiosk_window_config_match_window (kiosk_window_config,
                                                       window,
                                                       sections[i]))
                        continue;

                kiosk_window_config_apply_config (kiosk_window_config,
                                                  window_config,
                                                  sections[i]);
        }
}

KioskWindowConfig *
kiosk_window_config_new (void)
{
        KioskWindowConfig *kiosk_window_config;

        kiosk_window_config = g_object_new (KIOSK_TYPE_WINDOW_CONFIG,
                                            NULL);

        return kiosk_window_config;
}
