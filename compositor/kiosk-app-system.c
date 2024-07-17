#include "config.h"

#include "kiosk-compositor.h"
#include "kiosk-app.h"
#include "kiosk-app-system.h"
#include <string.h>

#include <gio/gio.h>
#include <glib/gi18n.h>

/* This code is a simplified and expunged version based on GNOME Shell
 * implementation of ShellAppSystem.
 */

/* Vendor prefixes are something that can be prepended to a .desktop
 * file name. Undo this.
 */
static const char *const vendor_prefixes[] = {
        "gnome-",
        "fedora-",
        "mozilla-",
        "debian-",
        NULL
};

enum
{
        PROP_0,
        PROP_COMPOSITOR,
        N_PROPS
};

static GParamSpec *props[N_PROPS] = { NULL, };

enum
{
        APP_STATE_CHANGED,
        LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

struct _KioskAppSystem
{
        GObject          parent;

        /* weak references */
        KioskCompositor *compositor;

        GHashTable      *running_apps;
        GHashTable      *id_to_app;
};

static void kiosk_app_system_finalize (GObject *object);

G_DEFINE_TYPE (KioskAppSystem, kiosk_app_system, G_TYPE_OBJECT);

static void
kiosk_app_system_set_property (GObject      *gobject,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
        KioskAppSystem *self = KIOSK_APP_SYSTEM (gobject);

        switch (prop_id) {
        case PROP_COMPOSITOR:
                g_set_weak_pointer (&self->compositor,
                                    g_value_get_object (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
                break;
        }
}

static void
kiosk_app_system_dispose (GObject *object)
{
        KioskAppSystem *self = KIOSK_APP_SYSTEM (object);

        g_clear_weak_pointer (&self->compositor);

        G_OBJECT_CLASS (kiosk_app_system_parent_class)->dispose (object);
}

static void
kiosk_app_system_class_init (KioskAppSystemClass *klass)
{
        GObjectClass *gobject_class = (GObjectClass *) klass;

        gobject_class->set_property = kiosk_app_system_set_property;
        gobject_class->finalize = kiosk_app_system_finalize;
        gobject_class->dispose = kiosk_app_system_dispose;

        signals[APP_STATE_CHANGED] = g_signal_new ("app-state-changed",
                                                   KIOSK_TYPE_APP_SYSTEM,
                                                   G_SIGNAL_RUN_LAST,
                                                   0,
                                                   NULL,
                                                   NULL,
                                                   NULL,
                                                   G_TYPE_NONE, 1,
                                                   KIOSK_TYPE_APP);

        props[PROP_COMPOSITOR] = g_param_spec_object ("compositor",
                                                      "compositor",
                                                      "compositor",
                                                      KIOSK_TYPE_COMPOSITOR,
                                                      G_PARAM_CONSTRUCT_ONLY
                                                      | G_PARAM_WRITABLE
                                                      | G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_NICK |
                                                      G_PARAM_STATIC_BLURB);
        g_object_class_install_properties (gobject_class, N_PROPS, props);
}

static void
kiosk_app_system_init (KioskAppSystem *self)
{
        self->running_apps = g_hash_table_new_full (NULL,
                                                    NULL,
                                                    (GDestroyNotify) g_object_unref,
                                                    NULL);
        self->id_to_app = g_hash_table_new_full (g_str_hash,
                                                 g_str_equal,
                                                 NULL,
                                                 (GDestroyNotify) g_object_unref);
}

static void
kiosk_app_system_finalize (GObject *object)
{
        KioskAppSystem *self = KIOSK_APP_SYSTEM (object);

        g_hash_table_destroy (self->running_apps);
        g_hash_table_destroy (self->id_to_app);

        G_OBJECT_CLASS (kiosk_app_system_parent_class)->finalize (object);
}

/**
 * kiosk_app_system_lookup_app:
 *
 * Find a #KioskApp corresponding to an id.
 *
 * Return value: (transfer none): The #KioskApp for id, or %NULL if none
 */
KioskApp *
kiosk_app_system_lookup_app (KioskAppSystem *self,
                             const char     *id)
{
        KioskApp *app;
        GDesktopAppInfo *info;

        app = g_hash_table_lookup (self->id_to_app, id);
        if (app)
                return app;

        info = g_desktop_app_info_new (id);
        if (!info)
                return NULL;

        app = kiosk_app_new (self->compositor, info);
        g_hash_table_insert (self->id_to_app, (char *) kiosk_app_get_id (app), app);

        return app;
}

static KioskApp *
kiosk_app_system_lookup_heuristic_basename (KioskAppSystem *self,
                                            const char     *name)
{
        KioskApp *result;
        const char *const *prefix;

        result = kiosk_app_system_lookup_app (self, name);
        if (result != NULL)
                return result;

        for (prefix = vendor_prefixes; *prefix != NULL; prefix++) {
                char *tmpid = g_strconcat (*prefix, name, NULL);
                result = kiosk_app_system_lookup_app (self, tmpid);
                g_free (tmpid);
                if (result != NULL)
                        return result;
        }

        return NULL;
}

/**
 * kiosk_app_system_lookup_desktop_wmclass:
 * @system: a #KioskAppSystem
 * @wmclass: (nullable): A WM_CLASS value
 *
 * Find a valid application whose .desktop file, without the extension
 * and properly canonicalized, matches @wmclass.
 *
 * Returns: (transfer none): A #KioskApp for @wmclass
 */
KioskApp *
kiosk_app_system_lookup_desktop_wmclass (KioskAppSystem *self,
                                         const char     *wmclass)
{
        char *canonicalized;
        char *desktop_file;
        KioskApp *app;

        if (wmclass == NULL)
                return NULL;

        /* First try without changing the case (this handles
         * org.example.Foo.Bar.desktop applications)
         *
         * Note that is slightly wrong in that Gtk+ would set
         * the WM_CLASS to Org.example.Foo.Bar, but it also
         * sets the instance part to org.example.Foo.Bar, so we're ok
         */
        desktop_file = g_strconcat (wmclass, ".desktop", NULL);
        app = kiosk_app_system_lookup_heuristic_basename (self,
                                                          desktop_file);
        g_free (desktop_file);

        if (app)
                return app;

        canonicalized = g_ascii_strdown (wmclass, -1);

        /* This handles "Fedora Eclipse", probably others.
         * Note g_strdelimit is modify-in-place. */
        g_strdelimit (canonicalized, " ", '-');

        desktop_file = g_strconcat (canonicalized, ".desktop", NULL);

        app = kiosk_app_system_lookup_heuristic_basename (self,
                                                          desktop_file);

        g_free (canonicalized);
        g_free (desktop_file);

        return app;
}

void
kiosk_app_system_notify_app_state_changed (KioskAppSystem *self,
                                           KioskApp       *app)
{
        KioskAppState state = kiosk_app_get_state (app);

        switch (state) {
        case KIOSK_APP_STATE_RUNNING:
                g_hash_table_insert (self->running_apps,
                                     g_object_ref (app),
                                     NULL);
                break;
        case KIOSK_APP_STATE_STOPPED:
                g_hash_table_remove (self->running_apps, app);
                break;
        default:
                g_warn_if_reached ();
                break;
        }
        g_signal_emit (self, signals[APP_STATE_CHANGED], 0, app);
}

void
kiosk_app_system_app_iter_init (KioskAppSystemAppIter *iter,
                                KioskAppSystem        *system)
{
        g_hash_table_iter_init (&iter->hash_iter, system->running_apps);
}

gboolean
kiosk_app_system_app_iter_next (KioskAppSystemAppIter *iter,
                                KioskApp             **app)
{
        gpointer key, value;
        if (g_hash_table_iter_next (&iter->hash_iter, &key, &value)) {
                *app = KIOSK_APP (key);
                return TRUE;
        }
        return FALSE;
}

KioskAppSystem *
kiosk_app_system_new (KioskCompositor *compositor)
{
        KioskAppSystem *self;

        self = g_object_new (KIOSK_TYPE_APP_SYSTEM,
                             "compositor", compositor, NULL);

        return self;
}
