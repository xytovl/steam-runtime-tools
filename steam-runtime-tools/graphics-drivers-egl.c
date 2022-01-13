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

#include "steam-runtime-tools/graphics.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/graphics-drivers-json-based-internal.h"
#include "steam-runtime-tools/graphics-internal.h"

/**
 * SECTION:graphics-drivers-egl
 * @title: EGL graphics driver enumeration
 * @short_description: Get information about the system's EGL drivers
 * @include: steam-runtime-tools/steam-runtime-tools.h
 *
 * #SrtEglIcd is an opaque object representing the metadata describing
 * an EGL ICD.
 * This is a reference-counted object: use g_object_ref() and
 * g_object_unref() to manage its lifecycle.
 */

/**
 * SrtEglIcd:
 *
 * Opaque object representing an EGL ICD.
 */

struct _SrtEglIcd
{
  /*< private >*/
  GObject parent;
  SrtLoadable icd;
};

struct _SrtEglIcdClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum
{
  EGL_ICD_PROP_0,
  EGL_ICD_PROP_ERROR,
  EGL_ICD_PROP_ISSUES,
  EGL_ICD_PROP_JSON_PATH,
  EGL_ICD_PROP_LIBRARY_PATH,
  EGL_ICD_PROP_RESOLVED_LIBRARY_PATH,
  N_EGL_ICD_PROPERTIES
};

G_DEFINE_TYPE (SrtEglIcd, srt_egl_icd, G_TYPE_OBJECT)

static void
srt_egl_icd_init (SrtEglIcd *self)
{
}

static void
srt_egl_icd_get_property (GObject *object,
                          guint prop_id,
                          GValue *value,
                          GParamSpec *pspec)
{
  SrtEglIcd *self = SRT_EGL_ICD (object);

  switch (prop_id)
    {
      case EGL_ICD_PROP_ERROR:
        g_value_set_boxed (value, self->icd.error);
        break;

      case EGL_ICD_PROP_ISSUES:
        g_value_set_flags (value, self->icd.issues);
        break;

      case EGL_ICD_PROP_JSON_PATH:
        g_value_set_string (value, self->icd.json_path);
        break;

      case EGL_ICD_PROP_LIBRARY_PATH:
        g_value_set_string (value, self->icd.library_path);
        break;

      case EGL_ICD_PROP_RESOLVED_LIBRARY_PATH:
        g_value_take_string (value, srt_egl_icd_resolve_library_path (self));
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_egl_icd_set_property (GObject *object,
                          guint prop_id,
                          const GValue *value,
                          GParamSpec *pspec)
{
  SrtEglIcd *self = SRT_EGL_ICD (object);
  const char *tmp;

  switch (prop_id)
    {
      case EGL_ICD_PROP_ERROR:
        g_return_if_fail (self->icd.error == NULL);
        self->icd.error = g_value_dup_boxed (value);
        break;

      case EGL_ICD_PROP_ISSUES:
        g_return_if_fail (self->icd.issues == 0);
        self->icd.issues = g_value_get_flags (value);
        break;

      case EGL_ICD_PROP_JSON_PATH:
        g_return_if_fail (self->icd.json_path == NULL);
        tmp = g_value_get_string (value);
        self->icd.json_path = g_canonicalize_filename (tmp, NULL);
        break;

      case EGL_ICD_PROP_LIBRARY_PATH:
        g_return_if_fail (self->icd.library_path == NULL);
        self->icd.library_path = g_value_dup_string (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_egl_icd_constructed (GObject *object)
{
  SrtEglIcd *self = SRT_EGL_ICD (object);

  g_return_if_fail (self->icd.json_path != NULL);
  g_return_if_fail (g_path_is_absolute (self->icd.json_path));
  g_return_if_fail (self->icd.api_version == NULL);

  if (self->icd.error != NULL)
    g_return_if_fail (self->icd.library_path == NULL);
  else
    g_return_if_fail (self->icd.library_path != NULL);
}

static void
srt_egl_icd_finalize (GObject *object)
{
  SrtEglIcd *self = SRT_EGL_ICD (object);

  srt_loadable_clear (&self->icd);

  G_OBJECT_CLASS (srt_egl_icd_parent_class)->finalize (object);
}

static GParamSpec *egl_icd_properties[N_EGL_ICD_PROPERTIES] = { NULL };

static void
srt_egl_icd_class_init (SrtEglIcdClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_egl_icd_get_property;
  object_class->set_property = srt_egl_icd_set_property;
  object_class->constructed = srt_egl_icd_constructed;
  object_class->finalize = srt_egl_icd_finalize;

  egl_icd_properties[EGL_ICD_PROP_ERROR] =
    g_param_spec_boxed ("error", "Error",
                        "GError describing how this ICD failed to load, or NULL",
                        G_TYPE_ERROR,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  egl_icd_properties[EGL_ICD_PROP_ISSUES] =
    g_param_spec_flags ("issues", "Issues", "Problems with this ICD",
                        SRT_TYPE_LOADABLE_ISSUES, SRT_LOADABLE_ISSUES_NONE,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  egl_icd_properties[EGL_ICD_PROP_JSON_PATH] =
    g_param_spec_string ("json-path", "JSON path",
                         "Absolute path to JSON file describing this ICD. "
                         "If examining a sysroot, this path is set as though "
                         "the sysroot was the root directory. "
                         "When constructing the object, a relative path can "
                         "be given: it will be converted to an absolute path.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  egl_icd_properties[EGL_ICD_PROP_LIBRARY_PATH] =
    g_param_spec_string ("library-path", "Library path",
                         "Library implementing this ICD, expressed as a "
                         "basename to be searched for in the default "
                         "library search path (e.g. libEGL_mesa.so.0), "
                         "a relative path containing '/' to be resolved "
                         "relative to #SrtEglIcd:json-path (e.g. "
                         "./libEGL_myvendor.so), or an absolute path "
                         "as though the sysroot (if any) was the root "
                         "(e.g. /opt/EGL/libEGL_myvendor.so)",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  egl_icd_properties[EGL_ICD_PROP_RESOLVED_LIBRARY_PATH] =
    g_param_spec_string ("resolved-library-path", "Resolved library path",
                         "Library implementing this ICD, expressed as a "
                         "basename to be searched for in the default "
                         "library search path (e.g. libEGL_mesa.so.0) "
                         "or an absolute path as though the sysroot (if any) "
                         "was the root "
                         "(e.g. /opt/EGL/libEGL_myvendor.so)",
                         NULL,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_EGL_ICD_PROPERTIES,
                                     egl_icd_properties);
}

/*
 * srt_egl_icd_new:
 * @json_path: (transfer none): the absolute path to the JSON file
 * @library_path: (transfer none): the path to the library
 * @issues: problems with this ICD
 *
 * Returns: (transfer full): a new ICD
 */
SrtEglIcd *
srt_egl_icd_new (const gchar *json_path,
                 const gchar *library_path,
                 SrtLoadableIssues issues)
{
  g_return_val_if_fail (json_path != NULL, NULL);
  g_return_val_if_fail (library_path != NULL, NULL);

  return g_object_new (SRT_TYPE_EGL_ICD,
                       "json-path", json_path,
                       "library-path", library_path,
                       "issues", issues,
                       NULL);
}

/*
 * srt_egl_icd_new_error:
 * @issues: problems with this ICD
 * @error: (transfer none): Error that occurred when loading the ICD
 *
 * Returns: (transfer full): a new ICD
 */
SrtEglIcd *
srt_egl_icd_new_error (const gchar *json_path,
                       SrtLoadableIssues issues,
                       const GError *error)
{
  g_return_val_if_fail (json_path != NULL, NULL);
  g_return_val_if_fail (error != NULL, NULL);

  return g_object_new (SRT_TYPE_EGL_ICD,
                       "error", error,
                       "json-path", json_path,
                       "issues", issues,
                       NULL);
}

/**
 * srt_egl_icd_check_error:
 * @self: The ICD
 * @error: Used to return #SrtEglIcd:error if the ICD description could
 *  not be loaded
 *
 * Check whether we failed to load the JSON describing this EGL ICD.
 * Note that this does not actually `dlopen()` the ICD itself.
 *
 * Returns: %TRUE if the JSON was loaded successfully
 */
gboolean
srt_egl_icd_check_error (SrtEglIcd *self,
                         GError **error)
{
  g_return_val_if_fail (SRT_IS_EGL_ICD (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  return srt_loadable_check_error (&self->icd, error);
}

/**
 * srt_egl_icd_get_json_path:
 * @self: The ICD
 *
 * Return the absolute path to the JSON file representing this ICD.
 *
 * Returns: (type filename) (transfer none): #SrtEglIcd:json-path
 */
const gchar *
srt_egl_icd_get_json_path (SrtEglIcd *self)
{
  g_return_val_if_fail (SRT_IS_EGL_ICD (self), NULL);
  return self->icd.json_path;
}

/**
 * srt_egl_icd_get_library_path:
 * @self: The ICD
 *
 * Return the library path for this ICD. It is either an absolute path,
 * a path relative to srt_egl_icd_get_json_path() containing at least one
 * directory separator (slash), or a basename to be loaded from the
 * shared library search path.
 *
 * If the JSON description for this ICD could not be loaded, return %NULL
 * instead.
 *
 * Returns: (type filename) (transfer none) (nullable): #SrtEglIcd:library-path
 */
const gchar *
srt_egl_icd_get_library_path (SrtEglIcd *self)
{
  g_return_val_if_fail (SRT_IS_EGL_ICD (self), NULL);
  return self->icd.library_path;
}

/**
 * srt_egl_icd_get_issues:
 * @self: The ICD
 *
 * Return the problems found when parsing and loading @self.
 *
 * Returns: A bitfield containing problems, or %SRT_LOADABLE_ISSUES_NONE
 *  if no problems were found
 */
SrtLoadableIssues
srt_egl_icd_get_issues (SrtEglIcd *self)
{
  g_return_val_if_fail (SRT_IS_EGL_ICD (self), SRT_LOADABLE_ISSUES_UNKNOWN);
  return self->icd.issues;
}

/*
 * @self: The ICD
 * @is_duplicated: if %TRUE, this ICD is a duplicated of another ICD
 *
 * @self issues is adjusted accordingly to the @is_duplicated value.
 */
void
_srt_egl_icd_set_is_duplicated (SrtEglIcd *self,
                                gboolean is_duplicated)
{
  g_return_if_fail (SRT_IS_EGL_ICD (self));
  if (is_duplicated)
    self->icd.issues |= SRT_LOADABLE_ISSUES_DUPLICATED;
  else
    self->icd.issues &= ~SRT_LOADABLE_ISSUES_DUPLICATED;
}

/*
 * egl_icd_load_json:
 * @sysroot: (not nullable): The root directory, usually `/`
 * @filename: The filename of the metadata
 * @list: (element-type SrtEglIcd) (inout): Prepend the
 *  resulting #SrtEglIcd to this list
 *
 * Load a single ICD metadata file.
 */
static void
egl_icd_load_json (const char *sysroot,
                   const char *filename,
                   GList **list)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *canon = NULL;
  g_autofree gchar *in_sysroot = NULL;
  g_autofree gchar *library_path = NULL;
  SrtLoadableIssues issues = SRT_LOADABLE_ISSUES_NONE;

  g_return_if_fail (sysroot != NULL);
  g_return_if_fail (list != NULL);

  if (!g_path_is_absolute (filename))
    {
      canon = g_canonicalize_filename (filename, NULL);
      filename = canon;
    }

  in_sysroot = g_build_filename (sysroot, filename, NULL);

  if (load_json (SRT_TYPE_EGL_ICD, in_sysroot,
                 NULL, &library_path, &issues, &error))
    {
      g_assert (library_path != NULL);
      g_assert (error == NULL);
      *list = g_list_prepend (*list,
                              srt_egl_icd_new (filename, library_path, issues));
    }
  else
    {
      g_assert (library_path == NULL);
      g_assert (error != NULL);
      *list = g_list_prepend (*list,
                              srt_egl_icd_new_error (filename, issues, error));
    }
}

/**
 * srt_egl_icd_resolve_library_path:
 * @self: An ICD
 *
 * Return the path that can be passed to `dlopen()` for this ICD.
 *
 * If srt_egl_icd_get_library_path() is a relative path, return the
 * absolute path that is the result of interpreting it relative to
 * an appropriate location (the exact interpretation is subject to change,
 * depending on upstream decisions).
 *
 * Otherwise return a copy of srt_egl_icd_get_library_path().
 *
 * The result is either the basename of a shared library (to be found
 * relative to some directory listed in `$LD_LIBRARY_PATH`, `/etc/ld.so.conf`,
 * `/etc/ld.so.conf.d` or the hard-coded library search path), or an
 * absolute path.
 *
 * Returns: (transfer full) (type filename) (nullable): A copy
 *  of #SrtEglIcd:resolved-library-path. Free with g_free().
 */
gchar *
srt_egl_icd_resolve_library_path (SrtEglIcd *self)
{
  g_return_val_if_fail (SRT_IS_EGL_ICD (self), NULL);
  return srt_loadable_resolve_library_path (&self->icd);
}

/**
 * srt_egl_icd_new_replace_library_path:
 * @self: An ICD
 * @path: (type filename) (transfer none): A path
 *
 * Return a copy of @self with the srt_egl_icd_get_library_path()
 * changed to @path. For example, this is useful when setting up a
 * container where the underlying shared object will be made available
 * at a different absolute path.
 *
 * If @self is in an error state, this returns a new reference to @self.
 *
 * Returns: (transfer full): A new reference to a #SrtEglIcd. Free with
 *  g_object_unref().
 */
SrtEglIcd *
srt_egl_icd_new_replace_library_path (SrtEglIcd *self,
                                      const char *path)
{
  g_return_val_if_fail (SRT_IS_EGL_ICD (self), NULL);

  if (self->icd.error != NULL)
    return g_object_ref (self);

  return srt_egl_icd_new (self->icd.json_path, path, self->icd.issues);
}

/**
 * srt_egl_icd_write_to_file:
 * @self: An ICD
 * @path: (type filename): A filename
 * @error: Used to describe the error on failure
 *
 * Serialize @self to the given JSON file.
 *
 * Returns: %TRUE on success
 */
gboolean
srt_egl_icd_write_to_file (SrtEglIcd *self,
                           const char *path,
                           GError **error)
{
  g_return_val_if_fail (SRT_IS_EGL_ICD (self), FALSE);
  g_return_val_if_fail (path != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  return srt_loadable_write_to_file (&self->icd, path, SRT_TYPE_EGL_ICD, error);
}

static void
egl_icd_load_json_cb (const char *sysroot,
                      const char *filename,
                      void *user_data)
{
  egl_icd_load_json (sysroot, filename, user_data);
}

#define EGL_VENDOR_SUFFIX "glvnd/egl_vendor.d"

/*
 * Return the ${sysconfdir} that we assume GLVND has.
 *
 * steam-runtime-tools is typically installed in the Steam Runtime,
 * which is not part of the operating system, so we cannot assume
 * that our own prefix is the same as GLVND. Assume a conventional
 * OS-wide installation of GLVND.
 */
static const char *
get_glvnd_sysconfdir (void)
{
  return "/etc";
}

/*
 * Return the ${datadir} that we assume GLVND has. See above.
 */
static const char *
get_glvnd_datadir (void)
{
  return "/usr/share";
}

/*
 * _srt_load_egl_icds:
 * @helpers_path: (nullable): An optional path to find "inspect-library"
 *  helper, PATH is used if %NULL
 * @sysroot: (not nullable): The root directory, usually `/`
 * @envp: (array zero-terminated=1) (not nullable): Behave as though `environ`
 *  was this array
 * @multiarch_tuples: (nullable): If not %NULL, and a Flatpak environment
 *  is detected, assume a freedesktop-sdk-based runtime and look for
 *  GL extensions for these multiarch tuples. Also if not %NULL, duplicated
 *  EGL ICDs are searched by their absolute path, obtained using
 *  "inspect-library" in the provided multiarch tuples, instead of just their
 *  resolved library path.
 * @check_flags: Whether to check for problems
 *
 * Implementation of srt_system_info_list_egl_icds().
 *
 * Returns: (transfer full) (element-type SrtEglIcd): A list of ICDs,
 *  most-important first
 */
GList *
_srt_load_egl_icds (const char *helpers_path,
                    const char *sysroot,
                    gchar **envp,
                    const char * const *multiarch_tuples,
                    SrtCheckFlags check_flags)
{
  const gchar *value;
  gsize i;
  /* To avoid O(n**2) performance, we build this list in reverse order,
   * then reverse it at the end. */
  GList *ret = NULL;

  g_return_val_if_fail (sysroot != NULL, NULL);
  g_return_val_if_fail (_srt_check_not_setuid (), NULL);
  g_return_val_if_fail (envp != NULL, NULL);

  /* See
   * https://github.com/NVIDIA/libglvnd/blob/master/src/EGL/icd_enumeration.md
   * for details of the search order. */

  value = g_environ_getenv (envp, "__EGL_VENDOR_LIBRARY_FILENAMES");

  if (value != NULL)
    {
      g_auto(GStrv) filenames = g_strsplit (value, G_SEARCHPATH_SEPARATOR_S, -1);

      for (i = 0; filenames[i] != NULL; i++)
        egl_icd_load_json (sysroot, filenames[i], &ret);
    }
  else
    {
      g_autofree gchar *flatpak_info = NULL;

      value = g_environ_getenv (envp, "__EGL_VENDOR_LIBRARY_DIRS");

      flatpak_info = g_build_filename (sysroot, ".flatpak-info", NULL);

      if (value != NULL)
        {
          g_auto(GStrv) dirs = g_strsplit (value, G_SEARCHPATH_SEPARATOR_S, -1);

          load_json_dirs (sysroot, dirs, NULL, _srt_indirect_strcmp0,
                          egl_icd_load_json_cb, &ret);
        }
      else if (g_file_test (flatpak_info, G_FILE_TEST_EXISTS)
               && multiarch_tuples != NULL)
        {
          g_debug ("Flatpak detected: assuming freedesktop-based runtime");

          for (i = 0; multiarch_tuples[i] != NULL; i++)
            {
              g_autofree gchar *tmp = NULL;

              /* freedesktop-sdk reconfigures the EGL loader to look here. */
              tmp = g_strdup_printf ("/usr/lib/%s/GL/" EGL_VENDOR_SUFFIX,
                                     multiarch_tuples[i]);
              load_json_dir (sysroot, tmp, NULL, _srt_indirect_strcmp0,
                             egl_icd_load_json_cb, &ret);
            }
        }
      else
        {
          load_json_dir (sysroot, get_glvnd_sysconfdir (), EGL_VENDOR_SUFFIX,
                         _srt_indirect_strcmp0, egl_icd_load_json_cb, &ret);
          load_json_dir (sysroot, get_glvnd_datadir (), EGL_VENDOR_SUFFIX,
                         _srt_indirect_strcmp0, egl_icd_load_json_cb, &ret);
        }
    }

  if (!(check_flags & SRT_CHECK_FLAGS_SKIP_SLOW_CHECKS))
    _srt_loadable_flag_duplicates (SRT_TYPE_EGL_ICD, envp, helpers_path,
                                   multiarch_tuples, ret);

  return g_list_reverse (ret);
}
