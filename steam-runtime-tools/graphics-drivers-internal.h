/*<private_header>*/
/*
 * Copyright © 2019-2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/graphics-internal.h"

typedef struct _SrtBaseGraphicsModule SrtBaseGraphicsModule;
typedef struct _SrtBaseGraphicsModuleClass SrtBaseGraphicsModuleClass;

#define SRT_TYPE_BASE_GRAPHICS_MODULE \
  (_srt_base_graphics_module_get_type ())
#define SRT_BASE_GRAPHICS_MODULE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_BASE_GRAPHICS_MODULE, SrtBaseGraphicsModule))
#define SRT_BASE_GRAPHICS_MODULE_CLASS(cls) \
  (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_BASE_GRAPHICS_MODULE, SrtBaseGraphicsModuleClass))
#define SRT_IS_BASE_GRAPHICS_MODULE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_BASE_GRAPHICS_MODULE))
#define SRT_IS_BASE_GRAPHICS_MODULE_CLASS(cls) \
  (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_BASE_GRAPHICS_MODULE))
#define SRT_BASE_GRAPHICS_MODULE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_BASE_GRAPHICS_MODULE, SrtBaseGraphicsModuleClass))
GType _srt_base_graphics_module_get_type (void);

struct _SrtBaseGraphicsModule
{
  GObject parent;
  GError *error;
  /* Either a filename, or a relative/absolute path in the sysroot */
  gchar *library_path;

  SrtLoadableIssues issues;
};

struct _SrtBaseGraphicsModuleClass
{
  GObjectClass parent_class;

  gchar *(*resolve_library_path) (SrtBaseGraphicsModule *self);
};

gchar *_srt_base_graphics_module_resolve_library_path (SrtBaseGraphicsModule *self);
gboolean _srt_base_graphics_module_check_error (SrtBaseGraphicsModule *self,
                                                GError **error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtBaseGraphicsModule, g_object_unref)
