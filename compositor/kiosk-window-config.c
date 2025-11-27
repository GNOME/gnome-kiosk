#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "kiosk-compositor.h"
#include "kiosk-window-config.h"
#include "kiosk-monitor-constraint.h"
#include "kiosk-area-constraint.h"

#include <meta/display.h>
#include <meta/meta-backend.h>
#include <meta/meta-context.h>
#include <meta/meta-monitor-manager.h>
#include <meta/meta-external-constraint.h>

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
        GObject             parent;

        /* Weak references */
        KioskCompositor    *compositor;
        MetaDisplay        *display;
        MetaContext        *context;
        MetaBackend        *backend;
        MetaMonitorManager *monitor_manager;

        /* Strong references */
        GKeyFile           *config_key_file;
        GFileMonitor       *config_file_monitor;
        gchar              *user_config_file_path;

        /* <MetaWindow * window, const char *output_name> */
        GHashTable         *windows_on_monitors;
        /* <MetaWindow * window, KioskMonitorConstraint *> */
        GHashTable         *locked_monitors;
        /* <MetaWindow * window, KioskAreaConstraint *> */
        GHashTable         *locked_areas;
};

enum
{
        PROP_0,
        PROP_COMPOSITOR,
        N_PROPS
};
static GParamSpec *props[N_PROPS] = { NULL, };

typedef enum
{
        MONITOR_NOT_SET = 0,
        MONITOR_NOT_FOUND,
        MONITOR_FOUND,
} KioskWindowConfigMonitor;

G_DEFINE_TYPE (KioskWindowConfig, kiosk_window_config, G_TYPE_OBJECT)

static gboolean
kiosk_window_config_wants_window_fullscreen (KioskWindowConfig *self,
                                             MetaWindow        *window);

static void
kiosk_window_config_update_window (KioskWindowConfig *kiosk_window_config,
                                   MetaWindow        *window,
                                   MetaWindowConfig  *window_config);

static void
kiosk_window_config_on_window_created (MetaDisplay *display,
                                       MetaWindow  *window,
                                       gpointer     user_data);

static void
kiosk_window_config_on_monitors_changed (MetaMonitorManager *monitor_manager,
                                         gpointer            user_data);

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
        g_set_weak_pointer (&self->backend, meta_context_get_backend (self->context));
        g_set_weak_pointer (&self->monitor_manager, meta_backend_get_monitor_manager (self->backend));

        self->windows_on_monitors = g_hash_table_new_full (NULL, NULL, NULL, g_free);
        self->locked_monitors = g_hash_table_new_full (NULL, NULL, NULL, g_object_unref);
        self->locked_areas = g_hash_table_new_full (NULL, NULL, NULL, g_object_unref);

        g_signal_connect (self->display,
                          "window-created",
                          G_CALLBACK (kiosk_window_config_on_window_created),
                          self);

        /* Setup file monitoring after configuration is loaded */
        kiosk_window_config_setup_file_monitoring (self);

        g_signal_connect (self->monitor_manager,
                          "monitors-changed",
                          G_CALLBACK (kiosk_window_config_on_monitors_changed),
                          self);

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
        g_clear_weak_pointer (&self->backend);
        g_clear_weak_pointer (&self->monitor_manager);

        G_OBJECT_CLASS (kiosk_window_config_parent_class)->dispose (object);
}

static void
kiosk_window_config_finalize (GObject *object)
{
        KioskWindowConfig *self = KIOSK_WINDOW_CONFIG (object);

        g_clear_pointer (&self->config_key_file, g_key_file_free);
        g_clear_pointer (&self->user_config_file_path, g_free);
        g_clear_pointer (&self->windows_on_monitors, g_hash_table_unref);
        g_clear_pointer (&self->locked_monitors, g_hash_table_unref);
        g_clear_pointer (&self->locked_areas, g_hash_table_unref);

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
        g_auto (GStrv) sections = NULL;
        gsize length;
        gboolean key_found = FALSE;
        gsize i;

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
        g_auto (GStrv) sections = NULL;
        gsize length;
        gboolean key_found = FALSE;
        gsize i;

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

static const char *
kiosk_window_config_get_connector_for_window (KioskWindowConfig *self,
                                              MetaWindow        *window)
{
        char *output_name = NULL;

        if (!kiosk_window_config_get_string_for_window (self,
                                                        window,
                                                        "set-on-monitor",
                                                        &output_name))
                return NULL;

        return output_name;
}

static KioskWindowConfigMonitor
kiosk_window_config_wants_window_on_monitor (KioskWindowConfig *self,
                                             MetaWindow        *window,
                                             int               *monitor)
{
        const char *output_name;
        int m;

        output_name = g_hash_table_lookup (self->windows_on_monitors, window);
        if (!output_name)
                return MONITOR_NOT_SET;

        m = meta_monitor_manager_get_monitor_for_connector (self->monitor_manager,
                                                            output_name);
        if (m < 0) {
                g_warning ("Could not find monitor named \"%s\"", output_name);
                return MONITOR_NOT_FOUND;
        }

        *monitor = m;

        return MONITOR_FOUND;
}

static gboolean
kiosk_window_config_should_lock_window_on_monitor (KioskWindowConfig *self,
                                                   MetaWindow        *window)
{
        gboolean lock_on_monitor = FALSE;

        if (kiosk_window_config_get_boolean_for_window (self,
                                                        window,
                                                        "lock-on-monitor",
                                                        &lock_on_monitor)) {
                return lock_on_monitor;
        }

        return FALSE;
}

static gboolean
kiosk_window_config_wants_window_locked_on_monitor (KioskWindowConfig *self,
                                                    MetaWindow        *window)
{
        return g_hash_table_contains (self->locked_monitors, window);
}

static gboolean
kiosk_window_config_wants_window_locked_on_monitor_area (KioskWindowConfig *self,
                                                         MetaWindow        *window)
{
        return g_hash_table_contains (self->locked_areas, window);
}

static gboolean
kiosk_window_config_parse_lock_area (const char   *area_string,
                                     MtkRectangle *area)
{
        int x, y, width, height;
        int parsed;
        g_autofree char *str = NULL;

        if (!area_string || !area)
                return FALSE;

        /* Strip leading and trailing whitespace */
        str = g_strstrip (g_strdup (area_string));
        parsed = sscanf (str, " %d , %d %d x %d", &x, &y, &width, &height);

        if (parsed != 4) {
                g_warning ("KioskWindowConfig: Invalid lock-on-monitor-area format '%s', expected 'x,y WxH'",
                           area_string);
                return FALSE;
        }

        if (width <= 0 || height <= 0) {
                g_warning ("KioskWindowConfig: Invalid lock-on-monitor-area dimensions '%s', width and height must be > 0",
                           area_string);
                return FALSE;
        }

        /* Store as MtkRectangle */
        area->x = x;
        area->y = y;
        area->width = width;
        area->height = height;

        g_debug ("KioskWindowConfig: Parsed lock area: %d,%d %dx%d",
                 area->x, area->y, area->width, area->height);

        return TRUE;
}

static gboolean
kiosk_window_config_should_lock_window_on_monitor_area (KioskWindowConfig *self,
                                                        MetaWindow        *window,
                                                        MtkRectangle      *area)
{
        g_autofree gchar *area_string = NULL;

        if (!kiosk_window_config_get_string_for_window (self,
                                                        window,
                                                        "lock-on-monitor-area",
                                                        &area_string))
                return FALSE;

        return kiosk_window_config_parse_lock_area (area_string, area);
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
        long unsigned int i;

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
        g_auto (GStrv) sections = NULL;
        gsize length;
        gsize i;

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

const char *
kiosk_window_config_lookup_window_output_name (KioskWindowConfig *self,
                                               MetaWindow        *window)
{
        return g_hash_table_lookup (self->windows_on_monitors, window);
}

static void
kiosk_window_config_on_window_configure_initial (KioskWindowConfig *self,
                                                 MetaWindow        *window,
                                                 MetaWindowConfig  *window_config)
{
        gboolean fullscreen;

        g_debug ("KioskWindowConfig: configure window: %s", meta_window_get_description (window));

        fullscreen = kiosk_window_config_wants_window_fullscreen (self, window);
        meta_window_config_set_is_fullscreen (window_config, fullscreen);
        kiosk_window_config_update_window (self,
                                           window,
                                           window_config);
}

static void
kiosk_window_config_show_window (MetaWindow *window)
{
        if (!meta_window_is_mapped_inhibited (window)) {
                g_debug ("KioskWindowConfig: Window %s is not hidden",
                         meta_window_get_description (window));
                return;
        }

        g_debug ("KioskWindowConfig: Showing window: %s", meta_window_get_description (window));
        meta_window_uninhibit_mapped (window);
}

static void
kiosk_window_config_hide_window (MetaWindow *window)
{
        if (meta_window_is_mapped_inhibited (window)) {
                g_debug ("KioskWindowConfig: Window %s is already hidden",
                         meta_window_get_description (window));
                return;
        }

        g_debug ("KioskWindowConfig: Hiding window: %s", meta_window_get_description (window));
        meta_window_inhibit_mapped (window);
}

static void
kiosk_window_config_update_window_on_monitor (KioskWindowConfig *self,
                                              MetaWindow        *window)
{
        KioskWindowConfigMonitor monitor_status;
        int monitor_index;

        monitor_status =
                kiosk_window_config_wants_window_on_monitor (self, window, &monitor_index);

        if (monitor_status == MONITOR_NOT_SET) {
                g_debug ("KioskWindowConfig: Window %s is not set on a monitor",
                         meta_window_get_description (window));
                return;
        }

        if (monitor_status == MONITOR_FOUND) {
                g_debug ("KioskWindowConfig: Moving window %s to monitor %i",
                         meta_window_get_description (window), monitor_index);
                meta_window_move_to_monitor (window, monitor_index);
        }

        if (kiosk_window_config_wants_window_locked_on_monitor (self, window) ||
            kiosk_window_config_wants_window_locked_on_monitor_area (self, window)) {
                if (monitor_status == MONITOR_FOUND) {
                        kiosk_window_config_show_window (window);
                } else if (monitor_status == MONITOR_NOT_FOUND) {
                        kiosk_window_config_hide_window (window);
                }
        }
}

static void
kiosk_window_config_on_monitors_changed (MetaMonitorManager *monitor_manager,
                                         gpointer            user_data)
{
        KioskWindowConfig *self = KIOSK_WINDOW_CONFIG (user_data);
        g_autoptr (GHashTable) windows = NULL;
        GHashTableIter iter;
        gpointer window;

        g_debug ("KioskWindowConfig: Monitors changed");

        /* Use a temporary set to avoid duplicates, hence calling the update function more
         * than once on the same window.
         */
        windows = g_hash_table_new (NULL, NULL);

        g_hash_table_iter_init (&iter, self->locked_monitors);
        while (g_hash_table_iter_next (&iter, &window, NULL)) {
                g_hash_table_add (windows, window);
        }

        g_hash_table_iter_init (&iter, self->locked_areas);
        while (g_hash_table_iter_next (&iter, &window, NULL)) {
                g_hash_table_add (windows, window);
        }

        g_hash_table_iter_init (&iter, windows);
        while (g_hash_table_iter_next (&iter, &window, NULL)) {
                kiosk_window_config_update_window_on_monitor (self, window);
        }
}

static void
kiosk_window_config_on_window_configure (MetaWindow       *window,
                                         MetaWindowConfig *window_config,
                                         gpointer          user_data)
{
        KioskWindowConfig *self = KIOSK_WINDOW_CONFIG (user_data);

        if (meta_window_config_get_is_initial (window_config))
                kiosk_window_config_on_window_configure_initial (self, window, window_config);
        else
                g_debug ("KioskWindowConfig: Ignoring configure for window: %s",
                         meta_window_get_description (window));
}

static void
kiosk_window_config_on_window_unmanaged (MetaWindow *window,
                                         gpointer    user_data)
{
        KioskWindowConfig *self = KIOSK_WINDOW_CONFIG (user_data);
        KioskMonitorConstraint *monitor_constraint;
        KioskAreaConstraint *area_constraint;

        g_signal_handlers_disconnect_by_func (window,
                                              G_CALLBACK (kiosk_window_config_on_window_configure),
                                              self);

        g_signal_handlers_disconnect_by_func (window,
                                              G_CALLBACK (kiosk_window_config_on_window_unmanaged),
                                              self);

        g_hash_table_remove (self->windows_on_monitors, window);

        monitor_constraint = g_hash_table_lookup (self->locked_monitors, window);
        if (monitor_constraint) {
                meta_window_remove_external_constraint (window, META_EXTERNAL_CONSTRAINT (monitor_constraint));
                g_hash_table_remove (self->locked_monitors, window);
        }

        area_constraint = g_hash_table_lookup (self->locked_areas, window);
        if (area_constraint) {
                meta_window_remove_external_constraint (window, META_EXTERNAL_CONSTRAINT (area_constraint));
                g_hash_table_remove (self->locked_areas, window);
        }
}

static void
kiosk_window_config_on_window_created (MetaDisplay *display,
                                       MetaWindow  *window,
                                       gpointer     user_data)
{
        KioskWindowConfig *self = KIOSK_WINDOW_CONFIG (user_data);
        const char *output_name;
        MtkRectangle lock_area;
        gboolean lock_on_monitor;
        gboolean lock_on_monitor_area;

        g_signal_connect (window,
                          "configure",
                          G_CALLBACK (kiosk_window_config_on_window_configure),
                          self);

        g_signal_connect (window,
                          "unmanaged",
                          G_CALLBACK (kiosk_window_config_on_window_unmanaged),
                          self);

        output_name = kiosk_window_config_get_connector_for_window (self, window);
        if (output_name) {
                g_debug ("KioskWindowConfig: Window %s is set on monitor %s",
                         meta_window_get_description (window), output_name);
                g_hash_table_insert (self->windows_on_monitors, window, g_strdup (output_name));
        }

        lock_on_monitor = kiosk_window_config_should_lock_window_on_monitor (self, window);
        if (lock_on_monitor) {
                KioskMonitorConstraint *constraint;

                g_debug ("KioskWindowConfig: Window %s is locked on monitor",
                         meta_window_get_description (window));
                constraint = kiosk_monitor_constraint_new (self->compositor);
                g_hash_table_insert (self->locked_monitors, window, constraint);
                meta_window_add_external_constraint (window, META_EXTERNAL_CONSTRAINT (constraint));
        }

        lock_on_monitor_area = kiosk_window_config_should_lock_window_on_monitor_area (self, window, &lock_area);
        if (lock_on_monitor_area) {
                KioskAreaConstraint *constraint;

                g_debug ("KioskWindowConfig: Window %s is locked on monitor %s with area %d,%d %dx%d",
                         meta_window_get_description (window), output_name,
                         lock_area.x, lock_area.y,
                         lock_area.width, lock_area.height);
                constraint = kiosk_area_constraint_new (self->compositor, &lock_area);
                g_hash_table_insert (self->locked_areas, window, constraint);
                meta_window_add_external_constraint (window, META_EXTERNAL_CONSTRAINT (constraint));
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

        if (kiosk_window_config_wants_window_on_monitor (kiosk_window_config, window, &monitor) == MONITOR_FOUND) {
                g_debug ("KioskWindowConfig: Moving window to monitor");
                kiosk_window_config_update_window_on_monitor (kiosk_window_config, window);
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
