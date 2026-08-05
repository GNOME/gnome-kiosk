#include "config.h"
#include "kiosk-backgrounds.h"

#include <stdlib.h>
#include <string.h>

#include <glycin.h>
#include <gio/gio.h>
#include <clutter/clutter.h>
#include <cogl/cogl-color.h>

#include <meta/display.h>
#include <meta/util.h>

#include <meta/meta-context.h>
#include <meta/meta-backend.h>
#include <meta/meta-plugin.h>
#include <meta/meta-monitor-manager.h>
#include <meta/meta-background-actor.h>
#include <meta/meta-background-content.h>
#include <meta/meta-background-group.h>
#include <meta/meta-background.h>

#include "kiosk-compositor.h"
#include "kiosk-gobject-utils.h"

#define KIOSK_BACKGROUNDS_SCHEMA "org.gnome.desktop.background"
#define KIOSK_BACKGROUNDS_PICTURE_OPTIONS_SETTING "picture-options"
#define KIOSK_BACKGROUNDS_PICTURE_URI_SETTING "picture-uri"
#define KIOSK_BACKGROUNDS_COLOR_SHADING_TYPE_SETTING "color-shading-type"
#define KIOSK_BACKGROUNDS_PRIMARY_COLOR_SETTING "primary-color"
#define KIOSK_BACKGROUNDS_SECONDARY_COLOR_SETTING "secondary-color"

/* Texture cache entry */
typedef struct
{
        CoglTexture       *texture;
        ClutterColorState *color_state;
} TextureCacheEntry;

static void
texture_cache_entry_free (TextureCacheEntry *entry)
{
        g_clear_object (&entry->texture);
        g_clear_object (&entry->color_state);
        g_free (entry);
}

struct _KioskBackgrounds
{
        MetaBackgroundGroup parent;

        /* weak references */
        KioskCompositor    *compositor;
        MetaDisplay        *display;
        ClutterActor       *window_group;
        MetaContext        *context;
        MetaBackend        *backend;
        MetaMonitorManager *monitor_manager;
        ClutterActor       *stage;

        /* strong references */
        GCancellable       *cancellable;
        GSettings          *settings;
        ClutterActor       *background_group;
        GHashTable         *texture_cache;       /* GFile -> TextureCacheEntry */
};

enum
{
        PROP_COMPOSITOR = 1,
        NUMBER_OF_PROPERTIES
};
static GParamSpec *kiosk_backgrounds_properties[NUMBER_OF_PROPERTIES] = { NULL, };

G_DEFINE_FINAL_TYPE (KioskBackgrounds, kiosk_backgrounds, G_TYPE_OBJECT);

static void kiosk_backgrounds_set_property (GObject      *object,
                                            guint         property_id,
                                            const GValue *value,
                                            GParamSpec   *param_spec);
static void kiosk_backgrounds_get_property (GObject    *object,
                                            guint       property_id,
                                            GValue     *value,
                                            GParamSpec *param_spec);

static void kiosk_backgrounds_constructed (GObject *object);
static void kiosk_backgrounds_dispose (GObject *object);

static void
kiosk_backgrounds_class_init (KioskBackgroundsClass *backgrounds_class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (backgrounds_class);

        object_class->constructed = kiosk_backgrounds_constructed;
        object_class->set_property = kiosk_backgrounds_set_property;
        object_class->get_property = kiosk_backgrounds_get_property;
        object_class->dispose = kiosk_backgrounds_dispose;

        kiosk_backgrounds_properties[PROP_COMPOSITOR] = g_param_spec_object ("compositor",
                                                                             NULL, NULL,
                                                                             KIOSK_TYPE_COMPOSITOR,
                                                                             G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_NAME);
        g_object_class_install_properties (object_class, NUMBER_OF_PROPERTIES, kiosk_backgrounds_properties);
}

static CoglPixelFormat
gly_memory_format_to_cogl (GlyMemoryFormat  format)
{
        switch ((guint) format) {
        case GLY_MEMORY_B8G8R8A8_PREMULTIPLIED:
                return COGL_PIXEL_FORMAT_BGRA_8888_PRE;
        case GLY_MEMORY_A8R8G8B8_PREMULTIPLIED:
                return COGL_PIXEL_FORMAT_ARGB_8888_PRE;
        case GLY_MEMORY_R8G8B8A8_PREMULTIPLIED:
                return COGL_PIXEL_FORMAT_RGBA_8888_PRE;
        case GLY_MEMORY_B8G8R8A8:
                return COGL_PIXEL_FORMAT_BGRA_8888;
        case GLY_MEMORY_A8R8G8B8:
                return COGL_PIXEL_FORMAT_ARGB_8888;
        case GLY_MEMORY_R8G8B8A8:
                return COGL_PIXEL_FORMAT_RGBA_8888;
        case GLY_MEMORY_A8B8G8R8:
                return COGL_PIXEL_FORMAT_ABGR_8888;
        case GLY_MEMORY_R8G8B8:
                return COGL_PIXEL_FORMAT_RGB_888;
        case GLY_MEMORY_B8G8R8:
                return COGL_PIXEL_FORMAT_BGR_888;
        case GLY_MEMORY_R16G16B16A16_PREMULTIPLIED:
                return COGL_PIXEL_FORMAT_RGBA_16161616_PRE;
        case GLY_MEMORY_R16G16B16A16:
                return COGL_PIXEL_FORMAT_RGBA_16161616;
        case GLY_MEMORY_R16G16B16A16_FLOAT:
                return COGL_PIXEL_FORMAT_RGBA_FP_16161616;
        case GLY_MEMORY_R32G32B32A32_FLOAT_PREMULTIPLIED:
                return COGL_PIXEL_FORMAT_RGBA_FP_32323232_PRE;
        case GLY_MEMORY_R32G32B32A32_FLOAT:
                return COGL_PIXEL_FORMAT_RGBA_FP_32323232;
        default:
                g_assert_not_reached ();
        }
}

static GlyMemoryFormatSelection
glycin_supported_memory_formats (void)
{
        return GLY_MEMORY_SELECTION_B8G8R8A8_PREMULTIPLIED |
               GLY_MEMORY_SELECTION_A8R8G8B8_PREMULTIPLIED |
               GLY_MEMORY_SELECTION_R8G8B8A8_PREMULTIPLIED |
               GLY_MEMORY_SELECTION_B8G8R8A8 |
               GLY_MEMORY_SELECTION_A8R8G8B8 |
               GLY_MEMORY_SELECTION_R8G8B8A8 |
               GLY_MEMORY_SELECTION_A8B8G8R8 |
               GLY_MEMORY_SELECTION_R8G8B8 |
               GLY_MEMORY_SELECTION_B8G8R8 |
               GLY_MEMORY_SELECTION_R16G16B16A16_PREMULTIPLIED |
               GLY_MEMORY_SELECTION_R16G16B16A16 |
               GLY_MEMORY_SELECTION_R16G16B16A16_FLOAT |
               GLY_MEMORY_SELECTION_R32G32B32A32_FLOAT_PREMULTIPLIED |
               GLY_MEMORY_SELECTION_R32G32B32A32_FLOAT;
}

static void
gly_cicp_to_clutter (const GlyCicp *gly_cicp,
                     ClutterCicp   *clutter_cicp)
{
        clutter_cicp->primaries = (ClutterCicpPrimaries) gly_cicp->color_primaries;
        clutter_cicp->transfer = (ClutterCicpTransfer) gly_cicp->transfer_characteristics;
        clutter_cicp->matrix_coefficients = gly_cicp->matrix_coefficients;
        clutter_cicp->video_full_range_flag = gly_cicp->video_full_range_flag;
}

static void
set_background_color_from_settings (KioskBackgrounds *self,
                                    MetaBackground   *background)
{
        GDesktopBackgroundShading color_shading_type;
        g_autofree char *primary_color_as_string = NULL;
        g_autofree char *secondary_color_as_string = NULL;
        CoglColor primary_color = { 0 };
        CoglColor secondary_color = { 0 };

        color_shading_type = g_settings_get_enum (self->settings, KIOSK_BACKGROUNDS_COLOR_SHADING_TYPE_SETTING);
        primary_color_as_string = g_settings_get_string (self->settings, KIOSK_BACKGROUNDS_PRIMARY_COLOR_SETTING);
        cogl_color_from_string (&primary_color, primary_color_as_string);

        switch (color_shading_type) {
        case G_DESKTOP_BACKGROUND_SHADING_SOLID:
                meta_background_set_color (background, &primary_color);
                break;

        case G_DESKTOP_BACKGROUND_SHADING_VERTICAL:
        case G_DESKTOP_BACKGROUND_SHADING_HORIZONTAL:
                secondary_color_as_string = g_settings_get_string (self->settings, KIOSK_BACKGROUNDS_SECONDARY_COLOR_SETTING);
                cogl_color_from_string (&secondary_color, secondary_color_as_string);
                meta_background_set_gradient (background, color_shading_type, &primary_color, &secondary_color);
                break;
        }
}

typedef struct
{
        KioskBackgrounds       *backgrounds;
        MetaBackground         *background;
        GDesktopBackgroundStyle style;
        GFile                  *file;
} LoadTextureData;

static void
load_texture_data_free (LoadTextureData *data)
{
        g_object_unref (data->backgrounds);
        g_object_unref (data->background);
        g_object_unref (data->file);
        g_free (data);
}

static void
load_texture_thread (GTask        *task,
                     gpointer      source_object,
                     gpointer      task_data,
                     GCancellable *cancellable)
{
        LoadTextureData *data = task_data;
        g_autoptr (GFileInputStream) stream = NULL;
        g_autoptr (GlyLoader) loader = NULL;
        g_autoptr (GlyImage) image = NULL;
        GlyFrame *frame;
        GError *error = NULL;

        stream = g_file_read (data->file, cancellable, &error);
        if (stream == NULL) {
                g_task_return_error (task, error);
                return;
        }

        loader = gly_loader_new_for_stream (G_INPUT_STREAM (stream));
        gly_loader_set_accepted_memory_formats (loader, glycin_supported_memory_formats ());

        image = gly_loader_load (loader, &error);
        if (!image) {
                g_task_return_error (task, error);
                return;
        }

        frame = gly_image_next_frame (image, &error);
        if (!frame) {
                g_task_return_error (task, error);
                return;
        }

        g_task_return_pointer (task, frame, (GDestroyNotify) g_object_unref);
}

static void
on_texture_loaded (GObject      *source_object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
        GTask *task = G_TASK (result);
        LoadTextureData *data = g_task_get_task_data (task);
        KioskBackgrounds *self = data->backgrounds;
        g_autoptr (GError) error = NULL;
        g_autoptr (GError) local_error = NULL;
        g_autoptr (GlyFrame) frame = NULL;
        ClutterContext *clutter_context = clutter_actor_get_context (self->stage);
        ClutterBackend *clutter_backend = clutter_context_get_backend (clutter_context);
        CoglContext *ctx = clutter_backend_get_cogl_context (clutter_backend);
        CoglTexture *texture = NULL;
        int width, height, row_stride;
        GlyMemoryFormat format;
        g_autoptr (GlyCicp) cicp = NULL;
        GBytes *bytes;
        const guint8 *data_ptr;
        TextureCacheEntry *entry;

        frame = g_task_propagate_pointer (task, &error);
        if (frame == NULL) {
                g_autofree char *uri = g_file_get_uri (data->file);
                g_warning ("Failed to load background '%s': %s", uri, error->message);
                return;
        }

        width = gly_frame_get_width (frame);
        height = gly_frame_get_height (frame);
        row_stride = gly_frame_get_stride (frame);
        bytes = gly_frame_get_buf_bytes (frame);
        format = gly_frame_get_memory_format (frame);
        cicp = gly_frame_get_color_cicp (frame);
        data_ptr = g_bytes_get_data (bytes, NULL);

        /* Create texture */
        texture = COGL_TEXTURE (cogl_texture_2d_new_with_size (ctx, width, height));
        cogl_texture_set_components (texture,
                                     gly_memory_format_has_alpha (format)
                                       ? COGL_TEXTURE_COMPONENTS_RGBA
                                       : COGL_TEXTURE_COMPONENTS_RGB);

        /* Try to allocate, if it fails (texture too large), use sliced */
        if (!cogl_texture_allocate (texture, &local_error)) {
                g_clear_error (&local_error);
                g_object_unref (texture);
                texture = COGL_TEXTURE (cogl_texture_2d_sliced_new_with_size (ctx, width, height, COGL_TEXTURE_MAX_WASTE));
                cogl_texture_set_components (texture,
                                             gly_memory_format_has_alpha (format)
                                               ? COGL_TEXTURE_COMPONENTS_RGBA
                                               : COGL_TEXTURE_COMPONENTS_RGB);
        }

        /* Set texture data */
        if (!cogl_texture_set_data (texture,
                                    gly_memory_format_to_cogl (format),
                                    row_stride,
                                    data_ptr, 0,
                                    &local_error)) {
                g_warning ("Failed to set texture data for background: %s", local_error->message);
                g_clear_object (&texture);
                return;
        }

        /* Cache the texture */
        entry = g_new0 (TextureCacheEntry, 1);
        entry->texture = texture;

        if (cicp) {
                ClutterCicp clutter_cicp;

                gly_cicp_to_clutter (cicp, &clutter_cicp);

                entry->color_state =
                        clutter_color_state_params_new_from_cicp (clutter_context,
                                                                  &clutter_cicp,
                                                                  &local_error);
                if (local_error)
                        g_warning ("Failed to create color state from CICP data: %s", local_error->message);
        } else {
                entry->color_state = NULL;
        }

        g_hash_table_insert (self->texture_cache, g_object_ref (data->file), entry);

        /* Set the texture on the background */
        meta_background_set_texture (data->background, texture, data->style, entry->color_state);
}

static void
set_background_file_from_settings (KioskBackgrounds        *self,
                                   MetaBackground          *background,
                                   GDesktopBackgroundStyle  background_style)
{
        g_autofree char *uri = NULL;
        g_autoptr (GFile) picture_file = NULL;
        TextureCacheEntry *entry;
        LoadTextureData *data;
        GTask *task;

        uri = g_settings_get_string (self->settings, KIOSK_BACKGROUNDS_PICTURE_URI_SETTING);
        picture_file = g_file_new_for_commandline_arg (uri);

        /* Check cache first */
        entry = g_hash_table_lookup (self->texture_cache, picture_file);
        if (entry != NULL) {
                meta_background_set_texture (background, entry->texture, background_style, entry->color_state);
                return;
        }

        /* Load asynchronously */
        data = g_new0 (LoadTextureData, 1);
        data->backgrounds = g_object_ref (self);
        data->background = g_object_ref (background);
        data->style = background_style;
        data->file = g_object_ref (picture_file);

        task = g_task_new (self, self->cancellable, on_texture_loaded, NULL);
        g_task_set_task_data (task, data, (GDestroyNotify) load_texture_data_free);
        g_task_run_in_thread (task, load_texture_thread);
        g_object_unref (task);
}

static void
create_background_for_monitor (KioskBackgrounds *self,
                               int               monitor_index)
{
        g_autoptr (MetaBackground) background = NULL;
        GDesktopBackgroundStyle background_style;
        MtkRectangle geometry;
        ClutterActor *background_actor = NULL;
        MetaBackgroundContent *background_content;

        g_debug ("KioskBackgrounds: Creating background for monitor %d", monitor_index);

        background = meta_background_new (self->display);
        background_style = g_settings_get_enum (self->settings, KIOSK_BACKGROUNDS_PICTURE_OPTIONS_SETTING);

        if (background_style == G_DESKTOP_BACKGROUND_STYLE_NONE) {
                set_background_color_from_settings (self, background);
        } else {
                set_background_file_from_settings (self, background, background_style);
        }

        background_actor = meta_background_actor_new (self->display, monitor_index);

        meta_display_get_monitor_geometry (self->display, monitor_index, &geometry);

        clutter_actor_set_position (background_actor, geometry.x, geometry.y);
        clutter_actor_set_size (background_actor, geometry.width, geometry.height);

        background_content = META_BACKGROUND_CONTENT (clutter_actor_get_content (background_actor));
        meta_background_content_set_background (background_content, background);

        clutter_actor_add_child (self->background_group, background_actor);
        clutter_actor_show (background_actor);
}

static void
reinitialize_backgrounds (KioskBackgrounds *self)
{
        int i, number_of_monitors;

        g_debug ("KioskBackgrounds: Recreating backgrounds");

        clutter_actor_destroy_all_children (self->background_group);

        number_of_monitors = meta_display_get_n_monitors (self->display);
        for (i = 0; i < number_of_monitors; i++) {
                create_background_for_monitor (self, i);
        }

        g_debug ("KioskBackgrounds: Finished recreating backgrounds");
}

static void
kiosk_backgrounds_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *param_spec)
{
        KioskBackgrounds *self = KIOSK_BACKGROUNDS (object);

        switch (property_id) {
        case PROP_COMPOSITOR:
                g_set_weak_pointer (&self->compositor, g_value_get_object (value));
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, param_spec);
                break;
        }
}

static void
kiosk_backgrounds_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *param_spec)
{
        switch (property_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, param_spec);
                break;
        }
}

static void
kiosk_backgrounds_dispose (GObject *object)
{
        KioskBackgrounds *self = KIOSK_BACKGROUNDS (object);
        if (self->cancellable != NULL) {
                g_cancellable_cancel (self->cancellable);
                g_clear_object (&self->cancellable);
        }

        g_clear_object (&self->background_group);
        g_clear_object (&self->settings);
        g_clear_pointer (&self->texture_cache, g_hash_table_unref);

        g_clear_weak_pointer (&self->context);
        g_clear_weak_pointer (&self->backend);
        g_clear_weak_pointer (&self->stage);
        g_clear_weak_pointer (&self->display);
        g_clear_weak_pointer (&self->window_group);
        g_clear_weak_pointer (&self->monitor_manager);
        g_clear_weak_pointer (&self->compositor);

        G_OBJECT_CLASS (kiosk_backgrounds_parent_class)->dispose (object);
}

static void
on_settings_changed (KioskBackgrounds *self)
{
        kiosk_gobject_utils_queue_defer_callback (G_OBJECT (self),
                                                  "[kiosk-backgrounds] on_backgrounds_settings_changed",
                                                  self->cancellable,
                                                  KIOSK_OBJECT_CALLBACK (reinitialize_backgrounds),
                                                  NULL);
}

static void
kiosk_backgrounds_constructed (GObject *object)
{
        KioskBackgrounds *self = KIOSK_BACKGROUNDS (object);
        MetaDisplay *display = meta_plugin_get_display (META_PLUGIN (self->compositor));
        MetaCompositor *compositor = meta_display_get_compositor (display);

        G_OBJECT_CLASS (kiosk_backgrounds_parent_class)->constructed (object);

        g_set_weak_pointer (&self->display, display);
        g_set_weak_pointer (&self->context, meta_display_get_context (self->display));
        g_set_weak_pointer (&self->backend, meta_context_get_backend (self->context));
        g_set_weak_pointer (&self->stage, CLUTTER_ACTOR (meta_compositor_get_stage (compositor)));
        g_set_weak_pointer (&self->window_group, meta_compositor_get_window_group (compositor));
        g_set_weak_pointer (&self->monitor_manager, meta_backend_get_monitor_manager (self->backend));

        self->cancellable = g_cancellable_new ();

        /* Initialize texture cache */
        self->texture_cache = g_hash_table_new_full (g_file_hash,
                                                     (GEqualFunc) g_file_equal,
                                                     g_object_unref,
                                                     (GDestroyNotify) texture_cache_entry_free);

        self->background_group = meta_background_group_new ();
        clutter_actor_insert_child_below (self->window_group, self->background_group, NULL);

        g_signal_connect_object (G_OBJECT (self->monitor_manager),
                                 "monitors-changed",
                                 G_CALLBACK (reinitialize_backgrounds),
                                 self,
                                 G_CONNECT_SWAPPED);

        self->settings = g_settings_new (KIOSK_BACKGROUNDS_SCHEMA);

        g_signal_connect_object (G_OBJECT (self->settings),
                                 "changed::" KIOSK_BACKGROUNDS_PICTURE_OPTIONS_SETTING,
                                 G_CALLBACK (on_settings_changed),
                                 self,
                                 G_CONNECT_SWAPPED);
        g_signal_connect_object (G_OBJECT (self->settings),
                                 "changed::" KIOSK_BACKGROUNDS_PICTURE_URI_SETTING,
                                 G_CALLBACK (on_settings_changed),
                                 self,
                                 G_CONNECT_SWAPPED);
        g_signal_connect_object (G_OBJECT (self->settings),
                                 "changed::" KIOSK_BACKGROUNDS_COLOR_SHADING_TYPE_SETTING,
                                 G_CALLBACK (on_settings_changed),
                                 self,
                                 G_CONNECT_SWAPPED);
        g_signal_connect_object (G_OBJECT (self->settings),
                                 "changed::" KIOSK_BACKGROUNDS_PRIMARY_COLOR_SETTING,
                                 G_CALLBACK (on_settings_changed),
                                 self,
                                 G_CONNECT_SWAPPED);

        g_signal_connect_object (G_OBJECT (self->settings),
                                 "changed::" KIOSK_BACKGROUNDS_SECONDARY_COLOR_SETTING,
                                 G_CALLBACK (on_settings_changed),
                                 self,
                                 G_CONNECT_SWAPPED);

        reinitialize_backgrounds (self);
}

static void
kiosk_backgrounds_init (KioskBackgrounds *self)
{
        g_debug ("KioskBackgrounds: Initializing");
}

KioskBackgrounds *
kiosk_backgrounds_new (KioskCompositor *compositor)
{
        GObject *object;

        object = g_object_new (KIOSK_TYPE_BACKGROUNDS,
                               "compositor", compositor,
                               NULL);

        return KIOSK_BACKGROUNDS (object);
}
