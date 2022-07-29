#pragma once

#include <glib-object.h>
#include <meta/meta-background-group.h>

typedef struct _KioskCompositor KioskCompositor;

G_BEGIN_DECLS

#define KIOSK_TYPE_BACKGROUNDS (kiosk_backgrounds_get_type ())

G_DECLARE_FINAL_TYPE (KioskBackgrounds,
                      kiosk_backgrounds,
                      KIOSK, BACKGROUNDS,
                      MetaBackgroundGroup);

KioskBackgrounds *kiosk_backgrounds_new (KioskCompositor *compositor);

G_END_DECLS
