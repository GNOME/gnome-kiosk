/* -*- c-basic-offset: 8; c-ts-mode-indent-offset: 8; indent-tabs-mode: nil; -*- */
/* kiosk-automount-manager.h
 *
 * Copyright 2023 Mohammed Sadiq
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <glib-object.h>

typedef struct _KioskCompositor KioskCompositor;

G_BEGIN_DECLS

#define KIOSK_TYPE_AUTOMOUNT_MANAGER (kiosk_automount_manager_get_type ())

G_DECLARE_FINAL_TYPE (KioskAutomountManager,
                      kiosk_automount_manager,
                      KIOSK, AUTOMOUNT_MANAGER,
                      GObject);

KioskAutomountManager *kiosk_automount_manager_new (KioskCompositor *compositor);

G_END_DECLS
