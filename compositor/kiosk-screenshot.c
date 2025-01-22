#include "config.h"
#include "kiosk-compositor.h"
#include "kiosk-screenshot.h"
#include "kiosk-gobject-utils.h"

#include <stdlib.h>
#include <string.h>

#include <cogl/cogl.h>

#include <meta/display.h>
#include <meta/util.h>

#include <meta/meta-backend.h>
#include <meta/meta-context.h>
#include <meta/meta-cursor-tracker.h>
#include <meta/meta-plugin.h>
#include <meta/meta-monitor-manager.h>
#include <meta/meta-background-actor.h>
#include <meta/meta-background-content.h>
#include <meta/meta-background-group.h>
#include <meta/meta-background-image.h>
#include <meta/meta-background.h>

/* This code is a largely based on GNOME Shell implementation of ShellScreenshot */

typedef enum _KioskScreenshotFlag
{
        KIOSK_SCREENSHOT_FLAG_NONE,
        KIOSK_SCREENSHOT_FLAG_INCLUDE_CURSOR,
} KioskScreenshotFlag;

typedef enum _KioskScreenshotMode
{
        KIOSK_SCREENSHOT_SCREEN,
        KIOSK_SCREENSHOT_WINDOW,
        KIOSK_SCREENSHOT_AREA,
} KioskScreenshotMode;

enum
{
        SCREENSHOT_TAKEN,

        LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

typedef struct _KioskScreenshot KioskScreenshot;

struct _KioskScreenshot
{
        GObject             parent_instance;

        /* weak references */
        KioskCompositor    *compositor;
        MetaDisplay        *display;
        MetaContext        *context;
        MetaBackend        *backend;
        ClutterActor       *stage;

        /* strong references */
        GOutputStream      *stream;
        KioskScreenshotFlag flags;
        KioskScreenshotMode mode;

        GDateTime          *datetime;

        cairo_surface_t    *image;
        MtkRectangle        screenshot_area;

        gboolean            include_frame;

        float               scale;
        ClutterContent     *cursor_content;
        graphene_point_t    cursor_point;
        float               cursor_scale;
};

enum
{
        PROP_COMPOSITOR = 1,
        NUMBER_OF_PROPERTIES
};
static GParamSpec *kiosk_screenshot_properties[NUMBER_OF_PROPERTIES] = { NULL, };

G_DEFINE_TYPE (KioskScreenshot, kiosk_screenshot, G_TYPE_OBJECT);

static void
kiosk_screenshot_dispose (GObject *object)
{
        KioskScreenshot *self = KIOSK_SCREENSHOT (object);

        g_clear_weak_pointer (&self->context);
        g_clear_weak_pointer (&self->backend);
        g_clear_weak_pointer (&self->stage);
        g_clear_weak_pointer (&self->display);
        g_clear_weak_pointer (&self->compositor);

        G_OBJECT_CLASS (kiosk_screenshot_parent_class)->dispose (object);
}

static void
kiosk_screenshot_constructed (GObject *object)
{
        KioskScreenshot *self = KIOSK_SCREENSHOT (object);
        MetaDisplay *display = meta_plugin_get_display (META_PLUGIN (self->compositor));
        MetaCompositor *compositor = meta_display_get_compositor (display);

        G_OBJECT_CLASS (kiosk_screenshot_parent_class)->constructed (object);

        g_set_weak_pointer (&self->display, display);
        g_set_weak_pointer (&self->context, meta_display_get_context (self->display));
        g_set_weak_pointer (&self->backend, meta_context_get_backend (self->context));
        g_set_weak_pointer (&self->stage, CLUTTER_ACTOR (meta_compositor_get_stage (compositor)));
}

static void
kiosk_screenshot_set_property (GObject      *object,
                               guint         property_id,
                               const GValue *value,
                               GParamSpec   *param_spec)
{
        KioskScreenshot *self = KIOSK_SCREENSHOT (object);

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
kiosk_screenshot_get_property (GObject    *object,
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
kiosk_screenshot_class_init (KioskScreenshotClass *screenshot_class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (screenshot_class);

        object_class->constructed = kiosk_screenshot_constructed;
        object_class->set_property = kiosk_screenshot_set_property;
        object_class->get_property = kiosk_screenshot_get_property;
        object_class->dispose = kiosk_screenshot_dispose;

        kiosk_screenshot_properties[PROP_COMPOSITOR] = g_param_spec_object ("compositor",
                                                                            "compositor",
                                                                            "compositor",
                                                                            KIOSK_TYPE_COMPOSITOR,
                                                                            G_PARAM_CONSTRUCT_ONLY
                                                                            | G_PARAM_WRITABLE
                                                                            | G_PARAM_STATIC_NAME
                                                                            | G_PARAM_STATIC_NICK
                                                                            | G_PARAM_STATIC_BLURB);
        g_object_class_install_properties (object_class, NUMBER_OF_PROPERTIES, kiosk_screenshot_properties);

        signals[SCREENSHOT_TAKEN] =
                g_signal_new ("screenshot-taken",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL, NULL,
                              G_TYPE_NONE,
                              1,
                              MTK_TYPE_RECTANGLE);
}

static void
kiosk_screenshot_init (KioskScreenshot *screenshot)
{
        g_debug ("KiosScreenshot: Initializing");
}

static void
on_screenshot_written (GObject      *source,
                       GAsyncResult *task,
                       gpointer      user_data)
{
        KioskScreenshot *screenshot = KIOSK_SCREENSHOT (source);
        GTask *result = user_data;

        g_task_return_boolean (result, g_task_propagate_boolean (G_TASK (task), NULL));
        g_object_unref (result);

        g_clear_pointer (&screenshot->image, cairo_surface_destroy);
        g_clear_object (&screenshot->stream);
        g_clear_pointer (&screenshot->datetime, g_date_time_unref);
}

static cairo_format_t
util_cairo_format_for_content (cairo_content_t  content)
{
        switch (content) {
        case CAIRO_CONTENT_COLOR:
                return CAIRO_FORMAT_RGB24;
        case CAIRO_CONTENT_ALPHA:
                return CAIRO_FORMAT_A8;
        case CAIRO_CONTENT_COLOR_ALPHA:
        default:
                return CAIRO_FORMAT_ARGB32;
        }
}

static cairo_surface_t *
util_cairo_surface_coerce_to_image (cairo_surface_t *surface,
                                    cairo_content_t  content,
                                    int              src_x,
                                    int              src_y,
                                    int              width,
                                    int              height)
{
        cairo_surface_t *copy;
        cairo_t *cr;

        copy = cairo_image_surface_create (util_cairo_format_for_content (content),
                                           width,
                                           height);

        cr = cairo_create (copy);
        cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_surface (cr, surface, -src_x, -src_y);
        cairo_paint (cr);
        cairo_destroy (cr);

        return copy;
}

static void
convert_alpha (guchar *dest_data,
               int     dest_stride,
               guchar *src_data,
               int     src_stride,
               int     src_x,
               int     src_y,
               int     width,
               int     height)
{
        int x, y;

        src_data += src_stride * src_y + src_x * 4;

        for (y = 0; y < height; y++) {
                uint32_t *src = (guint32 *) src_data;

                for (x = 0; x < width; x++) {
                        unsigned int alpha = src[x] >> 24;

                        if (alpha == 0) {
                                dest_data[x * 4 + 0] = 0;
                                dest_data[x * 4 + 1] = 0;
                                dest_data[x * 4 + 2] = 0;
                        } else {
                                dest_data[x * 4 + 0] = (((src[x] & 0xff0000) >> 16) * 255 + alpha / 2) / alpha;
                                dest_data[x * 4 + 1] = (((src[x] & 0x00ff00) >> 8) * 255 + alpha / 2) / alpha;
                                dest_data[x * 4 + 2] = (((src[x] & 0x0000ff) >> 0) * 255 + alpha / 2) / alpha;
                        }
                        dest_data[x * 4 + 3] = alpha;
                }

                src_data += src_stride;
                dest_data += dest_stride;
        }
}

static void
convert_no_alpha (guchar *dest_data,
                  int     dest_stride,
                  guchar *src_data,
                  int     src_stride,
                  int     src_x,
                  int     src_y,
                  int     width,
                  int     height)
{
        int x, y;

        src_data += src_stride * src_y + src_x * 4;

        for (y = 0; y < height; y++) {
                uint32_t *src = (uint32_t *) src_data;

                for (x = 0; x < width; x++) {
                        dest_data[x * 3 + 0] = src[x] >> 16;
                        dest_data[x * 3 + 1] = src[x] >> 8;
                        dest_data[x * 3 + 2] = src[x];
                }

                src_data += src_stride;
                dest_data += dest_stride;
        }
}

static GdkPixbuf *
util_pixbuf_from_surface (cairo_surface_t *surface,
                          gint             src_x,
                          gint             src_y,
                          gint             width,
                          gint             height)
{
        cairo_content_t content;
        GdkPixbuf *dest;

        /* General sanity checks */
        g_return_val_if_fail (surface != NULL, NULL);
        g_return_val_if_fail (width > 0 && height > 0, NULL);

        content = cairo_surface_get_content (surface) | CAIRO_CONTENT_COLOR;
        dest = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
                               !!(content & CAIRO_CONTENT_ALPHA),
                               8,
                               width, height);

        if (cairo_surface_get_type (surface) == CAIRO_SURFACE_TYPE_IMAGE &&
            cairo_image_surface_get_format (surface) == util_cairo_format_for_content (content)) {
                surface = cairo_surface_reference (surface);
        } else {
                surface = util_cairo_surface_coerce_to_image (surface, content,
                                                              src_x, src_y,
                                                              width, height);
                src_x = 0;
                src_y = 0;
        }
        cairo_surface_flush (surface);
        if (cairo_surface_status (surface) || dest == NULL) {
                cairo_surface_destroy (surface);
                g_clear_object (&dest);
                return NULL;
        }

        if (gdk_pixbuf_get_has_alpha (dest)) {
                convert_alpha (gdk_pixbuf_get_pixels (dest),
                               gdk_pixbuf_get_rowstride (dest),
                               cairo_image_surface_get_data (surface),
                               cairo_image_surface_get_stride (surface),
                               src_x, src_y,
                               width, height);
        } else {
                convert_no_alpha (gdk_pixbuf_get_pixels (dest),
                                  gdk_pixbuf_get_rowstride (dest),
                                  cairo_image_surface_get_data (surface),
                                  cairo_image_surface_get_stride (surface),
                                  src_x, src_y,
                                  width, height);
        }

        cairo_surface_destroy (surface);

        return dest;
}

static void
write_screenshot_thread (GTask        *result,
                         gpointer      object,
                         gpointer      task_data,
                         GCancellable *cancellable)
{
        KioskScreenshot *screenshot = KIOSK_SCREENSHOT (object);
        g_autoptr (GOutputStream) stream = NULL;
        g_autoptr (GdkPixbuf) pixbuf = NULL;
        g_autofree char *creation_time = NULL;
        GError *error = NULL;

        g_assert (screenshot != NULL);

        stream = g_object_ref (screenshot->stream);

        pixbuf = util_pixbuf_from_surface (screenshot->image,
                                           0, 0,
                                           cairo_image_surface_get_width (screenshot->image),
                                           cairo_image_surface_get_height (screenshot->image));
        creation_time = g_date_time_format (screenshot->datetime, "%c");

        if (!creation_time)
                creation_time = g_date_time_format (screenshot->datetime, "%FT%T%z");

        gdk_pixbuf_save_to_stream (pixbuf, stream, "png", NULL, &error,
                                   "tEXt::Software", "gnome-screenshot",
                                   "tEXt::Creation Time", creation_time,
                                   NULL);

        if (error)
                g_task_return_error (result, error);
        else
                g_task_return_boolean (result, TRUE);
}

static void
do_grab_screenshot (KioskScreenshot     *screenshot,
                    int                  x,
                    int                  y,
                    int                  width,
                    int                  height,
                    KioskScreenshotFlag  flags)
{
        MtkRectangle screenshot_rect = { x, y, width, height };
        int image_width;
        int image_height;
        float scale;
        cairo_surface_t *image;
        ClutterPaintFlag paint_flags = CLUTTER_PAINT_FLAG_NONE;
        g_autoptr (GError) error = NULL;

        clutter_stage_get_capture_final_size (CLUTTER_STAGE (screenshot->stage),
                                              &screenshot_rect,
                                              &image_width,
                                              &image_height,
                                              &scale);
        image = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                            image_width, image_height);

        if (flags & KIOSK_SCREENSHOT_FLAG_INCLUDE_CURSOR)
                paint_flags |= CLUTTER_PAINT_FLAG_FORCE_CURSORS;
        else
                paint_flags |= CLUTTER_PAINT_FLAG_NO_CURSORS;
        if (!clutter_stage_paint_to_buffer (CLUTTER_STAGE (screenshot->stage),
                                            &screenshot_rect, scale,
                                            cairo_image_surface_get_data (image),
                                            cairo_image_surface_get_stride (image),
                                            COGL_PIXEL_FORMAT_CAIRO_ARGB32_COMPAT,
                                            paint_flags,
                                            &error)) {
                cairo_surface_destroy (image);
                g_warning ("Failed to take screenshot: %s", error->message);
                return;
        }

        screenshot->image = image;

        screenshot->datetime = g_date_time_new_now_local ();
}

static void
draw_cursor_image (KioskScreenshot *screenshot,
                   cairo_surface_t *surface,
                   MtkRectangle     area)
{
        CoglTexture *texture;
        int width, height;
        int stride;
        guint8 *data;
        MetaCursorTracker *tracker;
        cairo_surface_t *cursor_surface;
        cairo_t *cr;
        int x, y;
        int xhot, yhot;
        double xscale, yscale;
        graphene_point_t point;

        tracker = meta_backend_get_cursor_tracker (screenshot->backend);
        texture = meta_cursor_tracker_get_sprite (tracker);

        if (!texture)
                return;

        meta_cursor_tracker_get_pointer (tracker, &point, NULL);
        x = point.x;
        y = point.y;

        if (!mtk_rectangle_contains_point (&area, point.x, point.y))
                return;

        meta_cursor_tracker_get_hot (tracker, &xhot, &yhot);
        width = cogl_texture_get_width (texture);
        height = cogl_texture_get_height (texture);
        stride = 4 * width;
        data = g_new (guint8, stride * height);
        cogl_texture_get_data (texture, COGL_PIXEL_FORMAT_CAIRO_ARGB32_COMPAT, stride, data);

        /* FIXME: cairo-gl? */
        cursor_surface = cairo_image_surface_create_for_data (data,
                                                              CAIRO_FORMAT_ARGB32,
                                                              width, height,
                                                              stride);

        cairo_surface_get_device_scale (surface, &xscale, &yscale);

        if (xscale != 1.0 || yscale != 1.0) {
                int monitor;
                float monitor_scale;
                MtkRectangle cursor_rect = {
                        .x = x, .y = y, .width = width, .height = height
                };

                monitor = meta_display_get_monitor_index_for_rect (screenshot->display,
                                                                   &cursor_rect);
                monitor_scale = meta_display_get_monitor_scale (screenshot->display,
                                                                monitor);

                cairo_surface_set_device_scale (cursor_surface, monitor_scale, monitor_scale);
        }

        cr = cairo_create (surface);
        cairo_set_source_surface (cr,
                                  cursor_surface,
                                  x - xhot - area.x,
                                  y - yhot - area.y);
        cairo_paint (cr);

        cairo_destroy (cr);
        cairo_surface_destroy (cursor_surface);
        g_free (data);
}

static void
grab_screenshot (KioskScreenshot     *screenshot,
                 KioskScreenshotFlag  flags,
                 GTask               *result)
{
        int width, height;
        GTask *task;

        meta_display_get_size (screenshot->display, &width, &height);

        do_grab_screenshot (screenshot,
                            0, 0, width, height,
                            flags);

        screenshot->screenshot_area.x = 0;
        screenshot->screenshot_area.y = 0;
        screenshot->screenshot_area.width = width;
        screenshot->screenshot_area.height = height;

        task = g_task_new (screenshot, NULL, on_screenshot_written, result);
        g_task_run_in_thread (task, write_screenshot_thread);
        g_object_unref (task);
}

static void
grab_window_screenshot (KioskScreenshot     *screenshot,
                        KioskScreenshotFlag  flags,
                        GTask               *result)
{
        GTask *task;
        MetaWindow *window = meta_display_get_focus_window (screenshot->display);
        ClutterActor *window_actor;
        gfloat actor_x, actor_y;
        MtkRectangle rect;

        window_actor = CLUTTER_ACTOR (meta_window_get_compositor_private (window));
        clutter_actor_get_position (window_actor, &actor_x, &actor_y);

        meta_window_get_frame_rect (window, &rect);

        if (!screenshot->include_frame)
                meta_window_frame_rect_to_client_rect (window, &rect, &rect);

        screenshot->screenshot_area = rect;

        screenshot->image = meta_window_actor_get_image (META_WINDOW_ACTOR (window_actor),
                                                         NULL);

        if (!screenshot->image) {
                g_task_report_new_error (screenshot, on_screenshot_written, result, NULL,
                                         G_IO_ERROR, G_IO_ERROR_FAILED,
                                         "Capturing window failed");
                return;
        }

        screenshot->datetime = g_date_time_new_now_local ();

        if (flags & KIOSK_SCREENSHOT_FLAG_INCLUDE_CURSOR) {
                if (meta_window_get_client_type (window) == META_WINDOW_CLIENT_TYPE_WAYLAND) {
                        float resource_scale;
                        resource_scale = clutter_actor_get_resource_scale (window_actor);

                        cairo_surface_set_device_scale (screenshot->image, resource_scale, resource_scale);
                }

                draw_cursor_image (screenshot,
                                   screenshot->image,
                                   screenshot->screenshot_area);
        }

        g_signal_emit (screenshot, signals[SCREENSHOT_TAKEN], 0, &rect);

        task = g_task_new (screenshot, NULL, on_screenshot_written, result);
        g_task_run_in_thread (task, write_screenshot_thread);
        g_object_unref (task);
}

static gboolean
finish_screenshot (KioskScreenshot *screenshot,
                   GAsyncResult    *result,
                   MtkRectangle   **area,
                   GError         **error)
{
        if (!g_task_propagate_boolean (G_TASK (result), error))
                return FALSE;

        if (area)
                *area = &screenshot->screenshot_area;

        return TRUE;
}

static void
on_after_paint (ClutterStage     *stage,
                ClutterStageView *view,
                ClutterFrame     *frame,
                GTask            *result)
{
        KioskScreenshot *screenshot = g_task_get_task_data (result);
        MetaCompositor *compositor = meta_display_get_compositor (screenshot->display);
        GTask *task;

        g_signal_handlers_disconnect_by_func (stage, on_after_paint, result);

        if (screenshot->mode == KIOSK_SCREENSHOT_AREA) {
                do_grab_screenshot (screenshot,
                                    screenshot->screenshot_area.x,
                                    screenshot->screenshot_area.y,
                                    screenshot->screenshot_area.width,
                                    screenshot->screenshot_area.height,
                                    screenshot->flags);

                task = g_task_new (screenshot, NULL, on_screenshot_written, result);
                g_task_run_in_thread (task, write_screenshot_thread);
        } else {
                grab_screenshot (screenshot, screenshot->flags, result);
        }

        g_signal_emit (screenshot, signals[SCREENSHOT_TAKEN], 0,
                       (MtkRectangle *) &screenshot->screenshot_area);

        meta_compositor_enable_unredirect (compositor);
}

/**
 * kiosk_screenshot_screenshot:
 * @screenshot: the #KioskScreenshot
 * @include_cursor: Whether to include the cursor or not
 * @stream: The stream for the screenshot
 * @callback: (scope async): function to call returning success or failure
 * of the async grabbing
 * @user_data: the data to pass to callback function
 *
 * Takes a screenshot of the whole screen
 * in @stream as png image.
 *
 */
void
kiosk_screenshot_screenshot (KioskScreenshot     *screenshot,
                             gboolean             include_cursor,
                             GOutputStream       *stream,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
        GTask *result;
        KioskScreenshotFlag flags;

        g_return_if_fail (KIOSK_IS_SCREENSHOT (screenshot));
        g_return_if_fail (G_IS_OUTPUT_STREAM (stream));

        if (screenshot->stream != NULL) {
                if (callback) {
                        g_task_report_new_error (screenshot,
                                                 callback,
                                                 user_data,
                                                 kiosk_screenshot_screenshot,
                                                 G_IO_ERROR,
                                                 G_IO_ERROR_PENDING,
                                                 "Only one screenshot operation at a time "
                                                 "is permitted");
                }
                return;
        }

        result = g_task_new (screenshot, NULL, callback, user_data);
        g_task_set_source_tag (result, kiosk_screenshot_screenshot);
        g_task_set_task_data (result, screenshot, NULL);

        screenshot->stream = g_object_ref (stream);

        flags = KIOSK_SCREENSHOT_FLAG_NONE;
        if (include_cursor)
                flags |= KIOSK_SCREENSHOT_FLAG_INCLUDE_CURSOR;

        if (meta_is_wayland_compositor ()) {
                grab_screenshot (screenshot, flags, result);

                g_signal_emit (screenshot, signals[SCREENSHOT_TAKEN], 0,
                               (MtkRectangle *) &screenshot->screenshot_area);
        } else {
                MetaCompositor *compositor = meta_display_get_compositor (screenshot->display);

                meta_compositor_disable_unredirect (compositor);
                clutter_actor_queue_redraw (CLUTTER_ACTOR (screenshot->stage));
                screenshot->flags = flags;
                screenshot->mode = KIOSK_SCREENSHOT_SCREEN;
                g_signal_connect (screenshot->stage, "after-paint",
                                  G_CALLBACK (on_after_paint), result);
        }
}

/**
 * kiosk_screenshot_screenshot_finish:
 * @screenshot: the #KioskScreenshot
 * @result: the #GAsyncResult that was provided to the callback
 * @area: (out) (transfer none): the area that was grabbed in screen coordinates
 * @error: #GError for error reporting
 *
 * Finish the asynchronous operation started by kiosk_screenshot_screenshot()
 * and obtain its result.
 *
 * Returns: whether the operation was successful
 *
 */
gboolean
kiosk_screenshot_screenshot_finish (KioskScreenshot *screenshot,
                                    GAsyncResult    *result,
                                    MtkRectangle   **area,
                                    GError         **error)
{
        g_return_val_if_fail (KIOSK_IS_SCREENSHOT (screenshot), FALSE);
        g_return_val_if_fail (G_IS_TASK (result), FALSE);
        g_return_val_if_fail (g_async_result_is_tagged (result,
                                                        kiosk_screenshot_screenshot),
                              FALSE);
        return finish_screenshot (screenshot, result, area, error);
}

/**
 * kiosk_screenshot_screenshot_area:
 * @screenshot: the #KioskScreenshot
 * @x: The X coordinate of the area
 * @y: The Y coordinate of the area
 * @width: The width of the area
 * @height: The height of the area
 * @stream: The stream for the screenshot
 * @callback: (scope async): function to call returning success or failure
 * of the async grabbing
 * @user_data: the data to pass to callback function
 *
 * Takes a screenshot of the passed in area and saves it
 * in @stream as png image.
 *
 */
void
kiosk_screenshot_screenshot_area (KioskScreenshot     *screenshot,
                                  int                  x,
                                  int                  y,
                                  int                  width,
                                  int                  height,
                                  GOutputStream       *stream,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
        GTask *result;
        g_autoptr (GTask) task = NULL;

        g_return_if_fail (KIOSK_IS_SCREENSHOT (screenshot));
        g_return_if_fail (G_IS_OUTPUT_STREAM (stream));

        if (screenshot->stream != NULL) {
                if (callback) {
                        g_task_report_new_error (screenshot,
                                                 callback,
                                                 NULL,
                                                 kiosk_screenshot_screenshot_area,
                                                 G_IO_ERROR,
                                                 G_IO_ERROR_PENDING,
                                                 "Only one screenshot operation at a time "
                                                 "is permitted");
                }
                return;
        }

        result = g_task_new (screenshot, NULL, callback, user_data);
        g_task_set_source_tag (result, kiosk_screenshot_screenshot_area);
        g_task_set_task_data (result, screenshot, NULL);

        screenshot->stream = g_object_ref (stream);
        screenshot->screenshot_area.x = x;
        screenshot->screenshot_area.y = y;
        screenshot->screenshot_area.width = width;
        screenshot->screenshot_area.height = height;


        if (meta_is_wayland_compositor ()) {
                do_grab_screenshot (screenshot,
                                    screenshot->screenshot_area.x,
                                    screenshot->screenshot_area.y,
                                    screenshot->screenshot_area.width,
                                    screenshot->screenshot_area.height,
                                    KIOSK_SCREENSHOT_FLAG_NONE);

                g_signal_emit (screenshot, signals[SCREENSHOT_TAKEN], 0,
                               (MtkRectangle *) &screenshot->screenshot_area);

                task = g_task_new (screenshot, NULL, on_screenshot_written, result);
                g_task_run_in_thread (task, write_screenshot_thread);
        } else {
                MetaCompositor *compositor = meta_display_get_compositor (screenshot->display);

                meta_compositor_disable_unredirect (compositor);
                clutter_actor_queue_redraw (CLUTTER_ACTOR (screenshot->stage));
                screenshot->flags = KIOSK_SCREENSHOT_FLAG_NONE;
                screenshot->mode = KIOSK_SCREENSHOT_AREA;
                g_signal_connect (screenshot->stage, "after-paint",
                                  G_CALLBACK (on_after_paint), result);
        }
}

/**
 * kiosk_screenshot_screenshot_area_finish:
 * @screenshot: the #KioskScreenshot
 * @result: the #GAsyncResult that was provided to the callback
 * @area: (out) (transfer none): the area that was grabbed in screen coordinates
 * @error: #GError for error reporting
 *
 * Finish the asynchronous operation started by kiosk_screenshot_screenshot_area()
 * and obtain its result.
 *
 * Returns: whether the operation was successful
 *
 */
gboolean
kiosk_screenshot_screenshot_area_finish (KioskScreenshot *screenshot,
                                         GAsyncResult    *result,
                                         MtkRectangle   **area,
                                         GError         **error)
{
        g_return_val_if_fail (KIOSK_IS_SCREENSHOT (screenshot), FALSE);
        g_return_val_if_fail (G_IS_TASK (result), FALSE);
        g_return_val_if_fail (g_async_result_is_tagged (result,
                                                        kiosk_screenshot_screenshot_area),
                              FALSE);
        return finish_screenshot (screenshot, result, area, error);
}

/**
 * kiosk_screenshot_screenshot_window:
 * @screenshot: the #KioskScreenshot
 * @include_frame: Whether to include the frame or not
 * @include_cursor: Whether to include the cursor or not
 * @stream: The stream for the screenshot
 * @callback: (scope async): function to call returning success or failure
 * of the async grabbing
 * @user_data: the data to pass to callback function
 *
 * Takes a screenshot of the focused window (optionally omitting the frame)
 * in @stream as png image.
 *
 */
void
kiosk_screenshot_screenshot_window (KioskScreenshot     *screenshot,
                                    gboolean             include_frame,
                                    gboolean             include_cursor,
                                    GOutputStream       *stream,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
        MetaWindow *window;
        GTask *result;

        g_return_if_fail (KIOSK_IS_SCREENSHOT (screenshot));
        g_return_if_fail (G_IS_OUTPUT_STREAM (stream));

        window = meta_display_get_focus_window (screenshot->display);

        if (screenshot->stream != NULL || !window) {
                if (callback) {
                        g_task_report_new_error (screenshot,
                                                 callback,
                                                 NULL,
                                                 kiosk_screenshot_screenshot_window,
                                                 G_IO_ERROR,
                                                 G_IO_ERROR_PENDING,
                                                 "Only one screenshot operation at a time "
                                                 "is permitted");
                }
                return;
        }

        result = g_task_new (screenshot, NULL, callback, user_data);
        g_task_set_source_tag (result, kiosk_screenshot_screenshot_window);

        screenshot->stream = g_object_ref (stream);
        screenshot->include_frame = include_frame;

        grab_window_screenshot (screenshot, include_cursor, result);
}

/**
 * kiosk_screenshot_screenshot_window_finish:
 * @screenshot: the #KioskScreenshot
 * @result: the #GAsyncResult that was provided to the callback
 * @area: (out) (transfer none): the area that was grabbed in screen coordinates
 * @error: #GError for error reporting
 *
 * Finish the asynchronous operation started by kiosk_screenshot_screenshot_window()
 * and obtain its result.
 *
 * Returns: whether the operation was successful
 *
 */
gboolean
kiosk_screenshot_screenshot_window_finish (KioskScreenshot *screenshot,
                                           GAsyncResult    *result,
                                           MtkRectangle   **area,
                                           GError         **error)
{
        g_return_val_if_fail (KIOSK_IS_SCREENSHOT (screenshot), FALSE);
        g_return_val_if_fail (G_IS_TASK (result), FALSE);
        g_return_val_if_fail (g_async_result_is_tagged (result,
                                                        kiosk_screenshot_screenshot_window),
                              FALSE);
        return finish_screenshot (screenshot, result, area, error);
}

KioskScreenshot *
kiosk_screenshot_new (KioskCompositor *compositor)
{
        GObject *object;

        object = g_object_new (KIOSK_TYPE_SCREENSHOT,
                               "compositor", compositor,
                               NULL);

        return KIOSK_SCREENSHOT (object);
}
