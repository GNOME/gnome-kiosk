#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "kiosk-compositor.h"
#include "kiosk-window-config.h"

#include <meta/display.h>
#include <meta/meta-backend.h>
#include <meta/meta-context.h>
#include <meta/meta-monitor-manager.h>

#include <glib-object.h>
#include <glib.h>
#include <gio/gio.h>

#define KIOSK_WINDOW_CONFIG_DIR      "gnome-kiosk"
#define KIOSK_WINDOW_CONFIG_FILENAME "window-config.ini"
#define KIOSK_WINDOW_CONFIG_GET_KEY_VALUE(f) ((KioskWindowConfigGetKeyValue) (f))

typedef gpointer (*KioskWindowConfigGetKeyValue) (GKeyFile   *key_file,
                                                  const char *section_name,
                                                  const char *key_name,
                                                  GError    **error);

struct _KioskWindowConfig
{
        GObject          parent;

        /* Weak references */
        KioskCompositor *compositor;
        MetaDisplay     *display;
        MetaContext     *context;

        /* Strong references */
        GKeyFile        *config_key_file;
        GFileMonitor    *config_file_monitor;
        gchar           *user_config_file_path;
};

enum
{
        PROP_0,
        PROP_COMPOSITOR,
        N_PROPS
};
static GParamSpec *props[N_PROPS] = { NULL, };

G_DEFINE_TYPE (KioskWindowConfig, kiosk_window_config, G_TYPE_OBJECT)

static gboolean
kiosk_window_config_wants_window_fullscreen (KioskWindowConfig *self,
                                             MetaWindow        *window);

static void
kiosk_window_config_update_window (KioskWindowConfig *kiosk_window_config,
                                   MetaWindow        *window,
                                   MetaWindowConfig  *window_config);

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

        if (kiosk_window_config_try_load_file (kiosk_window_config, filename)) {
                /* Store the user config file path for monitoring */
                g_set_str (&kiosk_window_config->user_config_file_path, filename);
                goto out;
        }

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
kiosk_window_config_reload (KioskWindowConfig *kiosk_window_config)
{
        g_debug ("KioskWindowConfig: Reloading configuration");

        /* Save the old key file */
        g_key_file_free (kiosk_window_config->config_key_file);
        kiosk_window_config->config_key_file = g_key_file_new ();

        /* Reload the configuration */
        if (!kiosk_window_config_load (kiosk_window_config)) {
                g_warning ("KioskWindowConfig: Failed to load the new configuration");
                return;
        }

        g_debug ("KioskWindowConfig: New configuration loaded successfully");
}

static void
kiosk_window_config_on_file_changed (GFileMonitor      *monitor,
                                     GFile             *file,
                                     GFile             *other_file,
                                     GFileMonitorEvent  event_type,
                                     gpointer           user_data)
{
        KioskWindowConfig *self = KIOSK_WINDOW_CONFIG (user_data);

        /* Only handle changes, not deletions or other events */
        if (event_type != G_FILE_MONITOR_EVENT_CHANGED &&
            event_type != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT) {
                return;
        }

        g_debug ("KioskWindowConfig: Configuration file changed, reloading");
        kiosk_window_config_reload (self);
}

static void
kiosk_window_config_on_window_configure (MetaWindow       *window,
                                         MetaWindowConfig *window_config,
                                         gpointer          user_data)
{
        KioskWindowConfig *self = KIOSK_WINDOW_CONFIG (user_data);
        gboolean fullscreen;

        if (!meta_window_config_get_is_initial (window_config)) {
                g_debug ("KioskWindowConfig: Ignoring configure for window: %s",
                         meta_window_get_description (window));
                return;
        }

        g_debug ("KioskWindowConfig: configure window: %s", meta_window_get_description (window));

        fullscreen = kiosk_window_config_wants_window_fullscreen (self, window);
        meta_window_config_set_is_fullscreen (window_config, fullscreen);
        kiosk_window_config_update_window (self,
                                           window,
                                           window_config);
}

static void
kiosk_window_config_on_window_unmanaged (MetaWindow *window,
                                         gpointer    user_data)
{
        KioskWindowConfig *self = KIOSK_WINDOW_CONFIG (user_data);

        g_signal_handlers_disconnect_by_func (window,
                                              G_CALLBACK (kiosk_window_config_on_window_configure),
                                              self);

        g_signal_handlers_disconnect_by_func (window,
                                              G_CALLBACK (kiosk_window_config_on_window_unmanaged),
                                              self);
}

static void
kiosk_window_config_on_window_created (MetaDisplay *display,
                                       MetaWindow  *window,
                                       gpointer     user_data)
{
        KioskWindowConfig *self = KIOSK_WINDOW_CONFIG (user_data);

        g_signal_connect (window,
                          "configure",
                          G_CALLBACK (kiosk_window_config_on_window_configure),
                          self);

        g_signal_connect (window,
                          "unmanaged",
                          G_CALLBACK (kiosk_window_config_on_window_unmanaged),
                          self);
}

static void
kiosk_window_config_setup_file_monitoring (KioskWindowConfig *self)
{
        g_autoptr (GFile) config_file = NULL;
        g_autoptr (GError) error = NULL;

        /* Only monitor the user config file if it exists */
        if (!self->user_config_file_path)
                return;

        config_file = g_file_new_for_path (self->user_config_file_path);
        self->config_file_monitor = g_file_monitor_file (config_file,
                                                         G_FILE_MONITOR_NONE,
                                                         NULL,
                                                         &error);

        if (!self->config_file_monitor) {
                g_warning ("KioskWindowConfig: Failed to monitor config file %s: %s",
                           self->user_config_file_path,
                           error ? error->message : "Unknown error");
                return;
        }

        g_signal_connect (self->config_file_monitor,
                          "changed",
                          G_CALLBACK (kiosk_window_config_on_file_changed),
                          self);
}

static void
kiosk_window_config_constructed (GObject *object)
{
        KioskWindowConfig *self = KIOSK_WINDOW_CONFIG (object);

        g_set_weak_pointer (&self->display, meta_plugin_get_display (META_PLUGIN (self->compositor)));
        g_set_weak_pointer (&self->context, meta_display_get_context (self->display));

        g_signal_connect (self->display,
                          "window-created",
                          G_CALLBACK (kiosk_window_config_on_window_created),
                          self);

        /* Setup file monitoring after configuration is loaded */
        kiosk_window_config_setup_file_monitoring (self);

        G_OBJECT_CLASS (kiosk_window_config_parent_class)->constructed (object);
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

        g_signal_handlers_disconnect_by_func (self->display,
                                              G_CALLBACK (kiosk_window_config_on_window_created),
                                              self);

        if (self->config_file_monitor) {
                g_signal_handlers_disconnect_by_func (self->config_file_monitor,
                                                      G_CALLBACK (kiosk_window_config_on_file_changed),
                                                      self);
                g_clear_object (&self->config_file_monitor);
        }

        g_clear_weak_pointer (&self->compositor);
        g_clear_weak_pointer (&self->display);
        g_clear_weak_pointer (&self->context);

        G_OBJECT_CLASS (kiosk_window_config_parent_class)->dispose (object);
}

static void
kiosk_window_config_finalize (GObject *object)
{
        KioskWindowConfig *self = KIOSK_WINDOW_CONFIG (object);

        g_clear_pointer (&self->config_key_file, g_key_file_free);
        g_clear_pointer (&self->user_config_file_path, g_free);

        G_OBJECT_CLASS (kiosk_window_config_parent_class)->finalize (object);
}

static void
kiosk_window_config_set_property (GObject      *object,
                                  guint         property_id,
                                  const GValue *value,
                                  GParamSpec   *param_spec)
{
        KioskWindowConfig *self = KIOSK_WINDOW_CONFIG (object);

        switch (property_id) {
        case PROP_COMPOSITOR:
                self->compositor = g_value_get_object (value);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id,
                                                   param_spec);
                break;
        }
}

static void
kiosk_window_config_get_property (GObject    *object,
                                  guint       property_id,
                                  GValue     *value,
                                  GParamSpec *param_spec)
{
        KioskWindowConfig *self = KIOSK_WINDOW_CONFIG (object);

        switch (property_id) {
        case PROP_COMPOSITOR:
                g_value_set_object (value, self->compositor);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id,
                                                   param_spec);
                break;
        }
}

static void
kiosk_window_config_class_init (KioskWindowConfigClass *klass)
{
        GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

        gobject_class->constructed = kiosk_window_config_constructed;
        gobject_class->set_property = kiosk_window_config_set_property;
        gobject_class->get_property = kiosk_window_config_get_property;
        gobject_class->dispose = kiosk_window_config_dispose;
        gobject_class->finalize = kiosk_window_config_finalize;

        props[PROP_COMPOSITOR] = g_param_spec_object ("compositor",
                                                      NULL,
                                                      NULL,
                                                      KIOSK_TYPE_COMPOSITOR,
                                                      G_PARAM_CONSTRUCT_ONLY
                                                      | G_PARAM_WRITABLE
                                                      | G_PARAM_STATIC_NAME
                                                      | G_PARAM_STATIC_NICK
                                                      | G_PARAM_STATIC_BLURB);

        g_object_class_install_properties (gobject_class, N_PROPS, props);
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

static gboolean
kiosk_window_config_wants_window_fullscreen (KioskWindowConfig *self,
                                             MetaWindow        *window)
{
        MetaWindowType window_type;

        g_autoptr (GList) windows = NULL;
        GList *node;

        if (!meta_window_allows_resize (window)) {
                g_debug ("KioskWindowConfig: Window does not allow resizes");
                return FALSE;
        }

        if (meta_window_is_override_redirect (window)) {
                g_debug ("KioskWindowConfig: Window is override redirect");
                return FALSE;
        }

        window_type = meta_window_get_window_type (window);

        if (window_type != META_WINDOW_NORMAL) {
                g_debug ("KioskWindowConfig: Window is not normal");
                return FALSE;
        }

        windows = meta_display_get_tab_list (self->display, META_TAB_LIST_NORMAL_ALL, NULL);

        for (node = windows; node != NULL; node = node->next) {
                MetaWindow *existing_window = node->data;

                if (meta_window_is_monitor_sized (existing_window)) {
                        return FALSE;
                }
        }

        return TRUE;
}

static gboolean
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

static gboolean
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

static gboolean
kiosk_window_config_wants_window_above (KioskWindowConfig *self,
                                        MetaWindow        *window)
{
        gboolean set_above;

        if (kiosk_window_config_get_boolean_for_window (self,
                                                        window,
                                                        "set-above",
                                                        &set_above))
                return set_above;

        /* If not specified in the config, use the heuristics */
        if (meta_window_is_screen_sized (window)) {
                return FALSE;
        }

        if (meta_window_is_monitor_sized (window)) {
                return FALSE;
        }

        return TRUE;
}

static gboolean
kiosk_window_config_wants_window_on_monitor (KioskWindowConfig *self,
                                             MetaWindow        *window,
                                             int               *monitor)
{
        g_autofree gchar *output_name = NULL;
        MetaBackend *backend;
        MetaMonitorManager *monitor_manager;
        int m;

        if (!kiosk_window_config_get_string_for_window (self,
                                                        window,
                                                        "set-on-monitor",
                                                        &output_name))
                return FALSE;

        backend = meta_context_get_backend (self->context);
        monitor_manager = meta_backend_get_monitor_manager (backend);
        m = meta_monitor_manager_get_monitor_for_connector (monitor_manager,
                                                            output_name);
        if (m < 0) {
                g_warning ("Could not find monitor named \"%s\"", output_name);
                return FALSE;
        }

        *monitor = m;

        return TRUE;
}

static gboolean
kiosk_window_config_wants_window_type (KioskWindowConfig *self,
                                       MetaWindow        *window,
                                       MetaWindowType    *window_type)
{
        g_autofree gchar *type_name = NULL;
        struct window_types_name
        {
                const char    *name;
                MetaWindowType type;
        } window_types_name[] = {
                { "desktop", META_WINDOW_DESKTOP      },
                { "dock",    META_WINDOW_DOCK         },
                { "splash",  META_WINDOW_SPLASHSCREEN },
        };
        int i;

        if (!kiosk_window_config_get_string_for_window (self,
                                                        window,
                                                        "set-window-type",
                                                        &type_name))
                return FALSE;

        for (i = 0; i < G_N_ELEMENTS (window_types_name); i++) {
                if (g_ascii_strcasecmp (type_name, window_types_name[i].name) == 0) {
                        g_debug ("KioskWindowConfig: Using window type: %s", window_types_name[i].name);
                        *window_type = window_types_name[i].type;
                        return TRUE;
                }
        }

        g_warning ("KioskWindowConfig: Unsupported window type: %s", type_name);
        return FALSE;
}

static void
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

void
kiosk_window_config_apply_initial_config (KioskWindowConfig *kiosk_window_config,
                                          MetaWindow        *window)
{
        int monitor;
        MetaWindowType window_type;

        if (!meta_window_is_fullscreen (window)) {
                if (kiosk_window_config_wants_window_above (kiosk_window_config, window)) {
                        g_debug ("KioskWindowConfig: Setting window above");
                        meta_window_make_above (window);
                }
        }

        if (kiosk_window_config_wants_window_on_monitor (kiosk_window_config, window, &monitor)) {
                g_debug ("KioskWindowConfig: Moving window to monitor %i", monitor);
                meta_window_move_to_monitor (window, monitor);
        }

        if (kiosk_window_config_wants_window_type (kiosk_window_config, window, &window_type)) {
                g_debug ("KioskWindowConfig: Setting window type 0x%x", window_type);
                meta_window_set_type (window, window_type);
        }
}

KioskWindowConfig *
kiosk_window_config_new (KioskCompositor *compositor)
{
        KioskWindowConfig *kiosk_window_config;

        kiosk_window_config = g_object_new (KIOSK_TYPE_WINDOW_CONFIG,
                                            "compositor", compositor,
                                            NULL);

        return kiosk_window_config;
}
