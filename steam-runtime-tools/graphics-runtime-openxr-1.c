/*
 * Copyright © 2019-2022 Collabora Ltd.
 * Copyright © 2024 Patrick Nicolas <patricknicolas@laposte.net>.
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

#include "graphics-drivers-json-based-internal.h"

/**
 * SECTION:graphics-runtime-openxr_1
 * @title: OpenXR 1 runtime enumeration
 * @short_description: Get information about the system's OpenXR 1 runtime 
 * @include: steam-runtime-tools/steam-runtime-tools.h
 *
 * #SrtOpenxr1Runtime is an opaque object representing the metadata describing
 * an OpenXR 1 runtime.
 * This is a reference-counted object: use g_object_ref() and
 * g_object_unref() to manage its lifecycle.
 */

/**
 * SrtOpenxr1Runtime:
 *
 * Opaque object representing an OpenXR 1 runtime.
 */

struct _SrtOpenxr1RuntimeClass
{
  /*< private >*/
  SrtBaseJsonGraphicsModuleClass parent_class;
};

enum
{
  OPENXR_1_RUNTIME_PROP_0,
  OPENXR_1_RUNTIME_PROP_LIBRARY_ARCH,
  N_OPENXR_1_RUNTIME_PROPERTIES
};

G_DEFINE_TYPE (SrtOpenxr1Runtime, srt_openxr_1_runtime, SRT_TYPE_BASE_JSON_GRAPHICS_MODULE)

static void
srt_openxr_1_runtime_init (SrtOpenxr1Runtime *self)
{
}

static void
srt_openxr_1_runtime_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  SrtOpenxr1Runtime *self = SRT_OPENXR_1_RUNTIME (object);

  switch (prop_id)
    {
      case OPENXR_1_RUNTIME_PROP_LIBRARY_ARCH:
        g_value_set_string (value, self->parent.library_arch);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_openxr_1_runtime_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
  SrtOpenxr1Runtime *self = SRT_OPENXR_1_RUNTIME (object);

  switch (prop_id)
    {
      case OPENXR_1_RUNTIME_PROP_LIBRARY_ARCH:
        g_return_if_fail (self->parent.library_arch == NULL);
        self->parent.library_arch = g_value_dup_string (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_openxr_1_runtime_constructed (GObject *object)
{
  SrtBaseGraphicsModule *base = SRT_BASE_GRAPHICS_MODULE (object);

  G_OBJECT_CLASS (srt_openxr_1_runtime_parent_class)->constructed (object);

  if (base->error != NULL)
    {
      g_return_if_fail (base->library_path == NULL);
    }
  else
    {
      g_return_if_fail (base->library_path != NULL);
    }
}

static GParamSpec *openxr_1_runtime_properties[N_OPENXR_1_RUNTIME_PROPERTIES] = { NULL };

static void
srt_openxr_1_runtime_class_init (SrtOpenxr1RuntimeClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_openxr_1_runtime_get_property;
  object_class->set_property = srt_openxr_1_runtime_set_property;
  object_class->constructed = srt_openxr_1_runtime_constructed;

  openxr_1_runtime_properties[OPENXR_1_RUNTIME_PROP_LIBRARY_ARCH] =
    g_param_spec_string ("library-arch", "Library architecture",
                         "Architecture of the library implementing this runtime. "
                         "The values allowed by the specification are \"32\" "
                         "and \"64\", but other values are possible.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_OPENXR_1_RUNTIME_PROPERTIES,
                                     openxr_1_runtime_properties);
}

/*
 * srt_openxr_1_runtime_new:
 * @json_path: (transfer none): the absolute path to the JSON file
 * @library_path: (transfer none): the path to the library
 * @library_arch: (transfer none) (nullable): the architecture of
 *  @library_path
 * @issues: problems with this runtime 
 *
 * Returns: (transfer full): a new runtime
 */
SrtOpenxr1Runtime *
srt_openxr_1_runtime_new (const gchar *json_path,
                          const gchar *library_path,
                          const gchar *library_arch,
                          SrtLoadableIssues issues)
{
  g_return_val_if_fail (json_path != NULL, NULL);
  g_return_val_if_fail (library_path != NULL, NULL);

  return g_object_new (SRT_TYPE_OPENXR_1_RUNTIME,
                       "json-path", json_path,
                       "library-path", library_path,
                       "library-arch", library_arch,
                       "issues", issues,
                       NULL);
}

/*
 * srt_openxr_1_runtime_new_error:
 * @issues: problems with this runtime
 * @error: (transfer none): Error that occurred when loading the runtime
 *
 * Returns: (transfer full): a new runtime
 */
SrtOpenxr1Runtime *
srt_openxr_1_runtime_new_error (const gchar *json_path,
                                SrtLoadableIssues issues,
                                const GError *error)
{
  return (SrtOpenxr1Runtime *) _srt_base_json_graphics_module_new_error (SRT_TYPE_OPENXR_1_RUNTIME,
                                                                         json_path,
                                                                         issues,
                                                                         error);
}

/**
 * srt_openxr_1_runtime_check_error:
 * @self: The runtime
 * @error: Used to return details if the runtime manifest could not be loaded
 *
 * Check whether we failed to load the JSON manifest describing this OpenXR 1 runtime.
 * Note that this does not actually `dlopen()` the runtime itself.
 *
 * Returns: %TRUE if the JSON was loaded successfully
 */
gboolean
srt_openxr_1_runtime_check_error (SrtOpenxr1Runtime *self,
                                  GError **error)
{
  g_return_val_if_fail (SRT_IS_OPENXR_1_RUNTIME (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  return _srt_base_graphics_module_check_error (SRT_BASE_GRAPHICS_MODULE (self), error);
}

/**
 * srt_openxr_1_runtime_get_json_path:
 * @self: The runtime
 *
 * Return the absolute path to the JSON file representing this runtime.
 *
 * Returns: (type filename) (transfer none): #SrtOpenxr1Runtime:json-path
 */
const gchar *
srt_openxr_1_runtime_get_json_path (SrtOpenxr1Runtime *self)
{
  g_return_val_if_fail (SRT_IS_OPENXR_1_RUNTIME (self), NULL);
  return self->parent.json_path;
}

/**
 * srt_openxr_1_runtime_get_library_path:
 * @self: The runtime
 *
 * Return the library path for this runtime. It is either an absolute path,
 * a path relative to srt_openxr_1_runtime_get_json_path() containing at least one
 * directory separator (slash), or a basename to be loaded from the
 * shared library search path.
 *
 * If the JSON description for this runtime could not be loaded, return %NULL
 * instead.
 *
 * Returns: (type filename) (transfer none) (nullable): #SrtOpenxr1Runtime:library-path
 */
const gchar *
srt_openxr_1_runtime_get_library_path (SrtOpenxr1Runtime *self)
{
  g_return_val_if_fail (SRT_IS_OPENXR_1_RUNTIME (self), NULL);
  return SRT_BASE_GRAPHICS_MODULE (self)->library_path;
}

/**
 * srt_openxr_1_runtime_get_library_arch:
 * @self: The runtime
 *
 * Return a string that describes the architecture of this runtime.
 * The values allowed by the OpenXR specification are `x86_64`, `i686`
 * `aarch64`... full list:
 * https://registry.khronos.org/OpenXR/specs/1.1/loader.html#architecture-identifiers
 *
 * This is an optional field, so if it was not available in the JSON,
 * or if the manifest could not be loaded, %NULL will be returned.
 *
 * Returns: (type filename) (transfer none) (nullable): #SrtOpenxr1Runtime:library-arch
 */
const gchar *
srt_openxr_1_runtime_get_library_arch (SrtOpenxr1Runtime *self)
{
  g_return_val_if_fail (SRT_IS_OPENXR_1_RUNTIME (self), NULL);
  return self->parent.library_arch;
}

/**
 * srt_openxr_1_runtime_get_issues:
 * @self: The runtime
 *
 * Return the problems found when parsing and loading @self.
 *
 * Returns: A bitfield containing problems, or %SRT_LOADABLE_ISSUES_NONE
 *  if no problems were found
 */
SrtLoadableIssues
srt_openxr_1_runtime_get_issues (SrtOpenxr1Runtime *self)
{
  g_return_val_if_fail (SRT_IS_OPENXR_1_RUNTIME (self), SRT_LOADABLE_ISSUES_UNKNOWN);
  return SRT_BASE_GRAPHICS_MODULE (self)->issues;
}

/**
 * srt_openxr_1_runtime_resolve_library_path:
 * @self: A runtime
 *
 * Return the path that can be passed to `dlopen()` for this runtime.
 *
 * If srt_openxr_1_runtime_get_library_path() is a relative path, return the
 * absolute path that is the result of interpreting it relative to
 * srt_openxr_1_runtime_get_json_path(). Otherwise return a copy of
 * srt_openxr_1_runtime_get_library_path().
 *
 * The result is either the basename of a shared library (to be found
 * relative to some directory listed in `$LD_LIBRARY_PATH`, `/etc/ld.so.conf`,
 * `/etc/ld.so.conf.d` or the hard-coded library search path), or an
 * absolute path.
 *
 * Returns: (transfer full) (type filename) (nullable): A copy
 *  of #SrtOpenxr1Runtime:resolved-library-path. Free with g_free().
 */
gchar *
srt_openxr_1_runtime_resolve_library_path (SrtOpenxr1Runtime *self)
{
  g_return_val_if_fail (SRT_IS_OPENXR_1_RUNTIME (self), NULL);
  return _srt_base_graphics_module_resolve_library_path (SRT_BASE_GRAPHICS_MODULE (self));
}

/**
 * srt_openxr_1_runtime_write_to_file:
 * @self: A runtime
 * @path: (type filename): A filename
 * @error: Used to describe the error on failure
 *
 * Serialize @self to the given JSON file.
 *
 * Returns: %TRUE on success
 */
gboolean
srt_openxr_1_runtime_write_to_file (SrtOpenxr1Runtime *self,
                                    const char *path,
                                    GError **error)
{
  g_return_val_if_fail (SRT_IS_OPENXR_1_RUNTIME (self), FALSE);
  g_return_val_if_fail (path != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  return _srt_base_json_graphics_module_write_to_file (&self->parent,
                                                       path,
                                                       SRT_TYPE_OPENXR_1_RUNTIME,
                                                       error);
}

/**
 * srt_openxr_1_runtime_new_replace_library_path:
 * @self: A runtime
 * @path: (type filename) (transfer none): A path
 *
 * Return a copy of @self with the srt_openxr_1_runtime_get_library_path()
 * changed to @path. For example, this is useful when setting up a
 * container where the underlying shared object will be made available
 * at a different absolute path.
 *
 * If @self is in an error state, this returns a new reference to @self.
 *
 * Note that @self issues are copied to the new #SrtOpenxr1Runtime copy, including
 * the eventual %SRT_LOADABLE_ISSUES_DUPLICATED.
 *
 * Returns: (transfer full): A new reference to a #SrtOpenxr1Runtime. Free with
 *  g_object_unref().
 */
SrtOpenxr1Runtime *
srt_openxr_1_runtime_new_replace_library_path (SrtOpenxr1Runtime *self,
                                               const char *path)
{
  SrtBaseGraphicsModule *base;
  g_return_val_if_fail (SRT_IS_OPENXR_1_RUNTIME (self), NULL);

  base = SRT_BASE_GRAPHICS_MODULE (self);

  if (base->error != NULL)
    return g_object_ref (self);

  return srt_openxr_1_runtime_new (self->parent.json_path,
                                   path,
                                   self->parent.library_arch,
                                   base->issues);
}

static void
openxr_1_runtime_load_json_cb (SrtSysroot *sysroot,
                               const char *filename,
                         void *user_data)
{
  g_autofree gchar * absolute_filename = realpath(filename, NULL);
  load_icd_from_json (SRT_TYPE_OPENXR_1_RUNTIME, sysroot, absolute_filename, user_data);
}

/*
 * Return the ${sysconfdir} that we assume the OpenXR 1 loader has.
 * See get_glvnd_sysconfdir().
 */
static const char *
get_openxr_1_sysconfdir (void)
{
  return "/etc";
}

/* Reference:
 * https://registry.khronos.org/OpenXR/specs/1.1/loader.html#runtime-discovery
 */
gchar **
_srt_graphics_get_openxr_1_search_paths (const char * const *envp,
                                         const char *suffix)
{
  GPtrArray *search_paths = g_ptr_array_new_with_free_func (g_free);
  g_auto(GStrv) dirs = NULL;
  const char *home;
  const gchar *value;
  gsize i;

  home = _srt_environ_getenv (envp, "HOME");

  if (home == NULL)
    home = g_get_home_dir ();

  /* 1. $XDG_CONFIG_HOME or $HOME/.config */
  value = _srt_environ_getenv (envp, "XDG_CONFIG_HOME");

  if (value != NULL)
    g_ptr_array_add (search_paths, g_build_filename (value, suffix, NULL));
  else if (home != NULL)
    g_ptr_array_add (search_paths, g_build_filename (home, ".config", suffix, NULL));

  /* 1a. $XDG_CONFIG_DIRS or /etc/xdg */
  value = _srt_environ_getenv (envp, "XDG_CONFIG_DIRS");

  /* Constant and non-configurable fallback, as per
   * https://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html */
  if (value == NULL)
    value = "/etc/xdg";

  dirs = g_strsplit (value, G_SEARCHPATH_SEPARATOR_S, -1);
  for (i = 0; dirs[i] != NULL; i++)
    g_ptr_array_add (search_paths, g_build_filename (dirs[i], suffix, NULL));

  g_clear_pointer (&dirs, g_strfreev);

  /* 2. SYSCONFDIR */
  value = get_openxr_1_sysconfdir ();
  g_ptr_array_add (search_paths, g_build_filename (value, suffix, NULL));

  g_ptr_array_add (search_paths, NULL);

  return (GStrv) g_ptr_array_free (search_paths, FALSE);
}

/*
 * _srt_load_openxr_1_runtimes:
 * @sysroot: (not nullable): The root directory, usually `/`
 * @runner: The execution environment
 * @multiarch_tuples: (nullable): If not %NULL, duplicated
 *  OpenXR 1 runtimes are searched by their absolute path, obtained using
 *  "inspect-library" in the provided multiarch tuples, instead of just their
 *  resolved library path.
 * @check_flags: Whether to check for problems
 *
 * Implementation of srt_system_info_list_openxr_1_runtimes().
 *
 * Returns: (transfer full) (element-type SrtOpenxr1Runtime): A list of runtimes,
 *  most-important first
 */
GList *
_srt_load_openxr_1_runtimes (SrtSysroot *sysroot,
                             SrtSubprocessRunner *runner,
                             const char * const *multiarch_tuples,
                             SrtCheckFlags check_flags)
{
  const char * const *envp;
  const gchar *value;
  /* To avoid O(n**2) performance, we build this list in reverse order,
   * then reverse it at the end. */
  GList *ret = NULL;

  g_return_val_if_fail (_srt_check_not_setuid (), NULL);
  g_return_val_if_fail (SRT_IS_SYSROOT (sysroot), NULL);
  g_return_val_if_fail (SRT_IS_SUBPROCESS_RUNNER (runner), NULL);

  /* Reference:
   * https://registry.khronos.org/OpenXR/specs/1.1/loader.html#overriding-the-default-runtime-usage
   */
  envp = _srt_subprocess_runner_get_environ (runner);
  value = _srt_environ_getenv (envp, "XR_RUNTIME_JSON");

  if (value != NULL)
    {
      g_debug ("OpenXR 1 runtime overridden to: %s", value);

      load_icd_from_json (SRT_TYPE_OPENXR_1_RUNTIME, sysroot, value, &ret);
    }
  else
    {
      g_auto(GStrv) search_paths = NULL;

      search_paths = _srt_graphics_get_openxr_1_search_paths (envp,
                                                              _SRT_GRAPHICS_OPENXR_1_RUNTIME_SUFFIX);

      g_debug ("Using normal OpenXR 1 manifest search path");
      load_json_dirs (sysroot, search_paths, NULL, READDIR_ORDER,
                      openxr_1_runtime_load_json_cb, &ret);
    }

  if (!(check_flags & SRT_CHECK_FLAGS_SKIP_SLOW_CHECKS))
    _srt_loadable_flag_duplicates (SRT_TYPE_OPENXR_1_RUNTIME, runner,
                                   multiarch_tuples, ret);

  return g_list_reverse (ret);
}
