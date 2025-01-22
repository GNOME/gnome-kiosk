#pragma once

#include <glib-object.h>
#include <gio/gio.h>
#include <mtk/mtk.h>
#include <clutter/clutter.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

typedef struct _KioskCompositor KioskCompositor;

/**
 * KioskScreenshot:
 *
 * Grabs screenshots of areas and/or windows
 *
 * The #KioskScreenshot object is used to take screenshots of screen
 * areas or windows and write them out as png files.
 *
 */
#define KIOSK_TYPE_SCREENSHOT (kiosk_screenshot_get_type ())

G_DECLARE_FINAL_TYPE (KioskScreenshot, kiosk_screenshot,
                      KIOSK, SCREENSHOT, GObject)

KioskScreenshot *kiosk_screenshot_new (KioskCompositor * compositor);

void    kiosk_screenshot_screenshot_area (KioskScreenshot     *screenshot,
                                          int                  x,
                                          int                  y,
                                          int                  width,
                                          int                  height,
                                          GOutputStream       *stream,
                                          GAsyncReadyCallback  callback,
                                          gpointer             user_data);
gboolean kiosk_screenshot_screenshot_area_finish (KioskScreenshot *screenshot,
                                                  GAsyncResult    *result,
                                                  MtkRectangle   **area,
                                                  GError         **error);

void    kiosk_screenshot_screenshot_window (KioskScreenshot     *screenshot,
                                            gboolean             include_frame,
                                            gboolean             include_cursor,
                                            GOutputStream       *stream,
                                            GAsyncReadyCallback  callback,
                                            gpointer             user_data);
gboolean kiosk_screenshot_screenshot_window_finish (KioskScreenshot *screenshot,
                                                    GAsyncResult    *result,
                                                    MtkRectangle   **area,
                                                    GError         **error);

void    kiosk_screenshot_screenshot (KioskScreenshot     *screenshot,
                                     gboolean             include_cursor,
                                     GOutputStream       *stream,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data);
gboolean kiosk_screenshot_screenshot_finish (KioskScreenshot *screenshot,
                                             GAsyncResult    *result,
                                             MtkRectangle   **area,
                                             GError         **error);
