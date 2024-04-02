/*
 * Copyright © 2019-2022 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "steam-runtime-tools/graphics-drivers-dri.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/graphics.h"
#include "steam-runtime-tools/graphics-drivers-internal.h"
#include "steam-runtime-tools/graphics-internal.h"

/**
 * SECTION:graphics-drivers-dri
 * @title: Mesa DRI graphics driver enumeration
 * @short_description: Get information about DRI drivers
 * @include: steam-runtime-tools/steam-runtime-tools.h
 *
 * #SrtDriDriver is an opaque object representing the metadata describing
 * a Mesa DRI driver.
 * This is a reference-counted object: use g_object_ref() and
 * g_object_unref() to manage its lifecycle.
 */

/**
 * SrtDriDriver:
 *
 * Opaque object representing a Mesa DRI driver.
 */

struct _SrtDriDriver
{
  /*< private >*/
  SrtBaseGraphicsModule parent;
  gboolean is_extra;
};

struct _SrtDriDriverClass
{
  /*< private >*/
  SrtBaseGraphicsModuleClass parent_class;
};

enum
{
  DRI_DRIVER_PROP_0,
  DRI_DRIVER_PROP_IS_EXTRA,
  N_DRI_DRIVER_PROPERTIES
};

G_DEFINE_TYPE (SrtDriDriver, srt_dri_driver, SRT_TYPE_BASE_GRAPHICS_MODULE)

static void
srt_dri_driver_init (SrtDriDriver *self)
{
}

static void
srt_dri_driver_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  SrtDriDriver *self = SRT_DRI_DRIVER (object);

  switch (prop_id)
    {
      case DRI_DRIVER_PROP_IS_EXTRA:
        g_value_set_boolean (value, self->is_extra);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_dri_driver_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
  SrtDriDriver *self = SRT_DRI_DRIVER (object);

  switch (prop_id)
    {
      case DRI_DRIVER_PROP_IS_EXTRA:
        self->is_extra = g_value_get_boolean (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static GParamSpec *dri_driver_properties[N_DRI_DRIVER_PROPERTIES] = { NULL };

static void
srt_dri_driver_class_init (SrtDriDriverClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_dri_driver_get_property;
  object_class->set_property = srt_dri_driver_set_property;

  dri_driver_properties[DRI_DRIVER_PROP_IS_EXTRA] =
    g_param_spec_boolean ("is-extra", "Is extra?",
                          "TRUE if the driver is located in an unusual path",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_DRI_DRIVER_PROPERTIES,
                                     dri_driver_properties);
}

/*
 * srt_dri_driver_new:
 * @library_path: (transfer none): the path to the library
 * @is_extra: if the DRI driver is in an unusual path
 *
 * Returns: (transfer full): a new DRI driver
 */
SrtDriDriver *
srt_dri_driver_new (const gchar *library_path,
                    gboolean is_extra)
{
  g_return_val_if_fail (library_path != NULL, NULL);

  return g_object_new (SRT_TYPE_DRI_DRIVER,
                       "library-path", library_path,
                       "is-extra", is_extra,
                       NULL);
}

/**
 * srt_dri_driver_get_library_path:
 * @self: The DRI driver
 *
 * Return the library path for this DRI driver.
 *
 * Returns: (type filename) (transfer none): #SrtDriDriver:library-path
 */
const gchar *
srt_dri_driver_get_library_path (SrtDriDriver *self)
{
  g_return_val_if_fail (SRT_IS_DRI_DRIVER (self), NULL);
  return SRT_BASE_GRAPHICS_MODULE (self)->library_path;
}

/**
 * srt_dri_driver_is_extra:
 * @self: The DRI driver
 *
 * Return a gboolean that indicates if the DRI is in an unusual position.
 *
 * Returns: %TRUE if the DRI driver is in an unusual position.
 */
gboolean
srt_dri_driver_is_extra (SrtDriDriver *self)
{
  g_return_val_if_fail (SRT_IS_DRI_DRIVER (self), FALSE);
  return self->is_extra;
}

/**
 * srt_dri_driver_resolve_library_path:
 * @self: The DRI driver
 *
 * Return the absolute path for this DRI driver.
 * If srt_dri_driver_get_library_path() is already an absolute path, a copy
 * of the same value will be returned.
 *
 * Returns: (type filename) (transfer full): A copy of
 *  #SrtDriDriver:resolved-library-path. Free with g_free().
 */
gchar *
srt_dri_driver_resolve_library_path (SrtDriDriver *self)
{
  g_return_val_if_fail (SRT_IS_DRI_DRIVER (self), NULL);
  return _srt_base_graphics_module_resolve_library_path (SRT_BASE_GRAPHICS_MODULE (self));
}
