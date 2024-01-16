/*
 * Copyright © 2019-2022 Collabora Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include "per-arch-dirs.h"

#include <glib/gstdio.h>

#include "steam-runtime-tools/system-info.h"
#include "steam-runtime-tools/utils-internal.h"

static gchar *
get_libdl_lib_or_mock (SrtSystemInfo *system_info,
                       gsize abi,
                       GError **error)
{
  if (g_getenv ("PRESSURE_VESSEL_TEST_STANDARDIZE_PLATFORM") != NULL)
    {
      /* For unit tests, pretend it's an unsupported multilib setup,
       * so we fall through to ${PLATFORM} */
      return glnx_null_throw (error, "Pretending multilib is unsupported for unit test");
    }
  else
    {
      return srt_system_info_dup_libdl_lib (system_info,
                                            pv_multiarch_details[abi].tuple,
                                            error);
    }
}

static gchar *
get_libdl_platform_or_mock (SrtSystemInfo *system_info,
                            gsize abi,
                            GError **error)
{
  if (g_getenv ("PRESSURE_VESSEL_TEST_STANDARDIZE_PLATFORM") != NULL)
    {
      /* In unit tests it isn't straightforward to find the real
       * ${PLATFORM}, so we use a predictable mock implementation:
       * for x86 we use whichever platform happens to be listed first
       * and for all the other cases we simply use "mock". */
#if defined(__i386__) || defined(__x86_64__)
      return g_strdup (pv_multiarch_details[abi].platforms[0]);
#else
      return g_strdup("mock");
#endif
    }
  else
    {
      return srt_system_info_dup_libdl_platform (system_info,
                                                 pv_multiarch_details[abi].tuple,
                                                 error);
    }
}

/*
 * PvPerArchDirsScheme:
 * @PV_PER_ARCH_DIRS_SCHEME_MULTIARCH: Debian-style multiarch.
 *  `${LIB}` expands to `lib/x86_64-linux-gnu` or similar.
 * @PV_PER_ARCH_DIRS_SCHEME_UBUNTU_1204: An early implementation of multiarch.
 *  `${LIB}` expands to `x86_64-linux-gnu` or similar.
 * @PV_PER_ARCH_DIRS_SCHEME_FHS: FHS library directories, as used in Red Hat.
 *  `${LIB}` expands to `lib` on i386 and `lib64` on x86_64.
 * @PV_PER_ARCH_DIRS_SCHEME_ARCH: Arch Linux's variant of FHS library
 *  directories. `${LIB}` expands to `lib32` on i386 and `lib` on x86_64.
 *  Does not exist on non-x86: use %PV_PER_ARCH_DIRS_SCHEME_FHS instead.
 * @PV_PER_ARCH_DIRS_SCHEME_PLATFORM: `${PLATFORM}` expands to a
 *  known/supported platform alias.
 * @PV_PER_ARCH_DIRS_SCHEME_NONE: None of the above.
 */
typedef enum
{
  PV_PER_ARCH_DIRS_SCHEME_NONE = 0,
  PV_PER_ARCH_DIRS_SCHEME_MULTIARCH,
  PV_PER_ARCH_DIRS_SCHEME_UBUNTU_1204,
  PV_PER_ARCH_DIRS_SCHEME_FHS,
#if defined(__i386__) || defined(__x86_64__)
  PV_PER_ARCH_DIRS_SCHEME_ARCH,
#endif
  PV_PER_ARCH_DIRS_SCHEME_PLATFORM,
} PvPerArchDirsScheme;

#if defined(__i386__) || defined(__x86_64__)
  static const char * const multiarch_libs[] = { "lib/x86_64-linux-gnu", "lib/i386-linux-gnu" };
  static const char * const fhs_libs[] = { "lib64", "lib" };
  static const char * const arch_libs[] = { "lib", "lib32" };
#elif defined(__aarch64__)
  static const char * const multiarch_libs[] = { "lib/aarch64-linux-gnu" };
  static const char * const fhs_libs[] = { "lib" };
#elif defined(_SRT_MULTIARCH)
  static const char * const multiarch_libs[] = { "lib/" _SRT_MULTIARCH };
  static const char * const fhs_libs[] = { "lib" };
#else
#error Architecture not supported by pressure-vessel
#endif

G_STATIC_ASSERT (G_N_ELEMENTS (multiarch_libs) == PV_N_SUPPORTED_ARCHITECTURES);
G_STATIC_ASSERT (G_N_ELEMENTS (fhs_libs) == PV_N_SUPPORTED_ARCHITECTURES);
#if defined(__i386__) || defined(__x86_64__)
G_STATIC_ASSERT (G_N_ELEMENTS (arch_libs) == PV_N_SUPPORTED_ARCHITECTURES);
#endif

static gboolean
pv_per_arch_dirs_supports_scheme (SrtSystemInfo *system_info,
                                  PvPerArchDirsScheme scheme)
{
  gsize abi;

  for (abi = 0; abi < PV_N_SUPPORTED_ARCHITECTURES; abi++)
    {
      g_autofree gchar *libdl_string = NULL;
      const char *multiarch_tuple = pv_multiarch_details[abi].tuple;

      switch (scheme)
        {
          case PV_PER_ARCH_DIRS_SCHEME_MULTIARCH:
          case PV_PER_ARCH_DIRS_SCHEME_UBUNTU_1204:
          case PV_PER_ARCH_DIRS_SCHEME_FHS:
#if defined(__i386__) || defined(__x86_64__)
          case PV_PER_ARCH_DIRS_SCHEME_ARCH:
#endif
            libdl_string = get_libdl_lib_or_mock (system_info, abi, NULL);
            break;

          case PV_PER_ARCH_DIRS_SCHEME_PLATFORM:
            libdl_string = get_libdl_platform_or_mock (system_info, abi, NULL);
            break;

          case PV_PER_ARCH_DIRS_SCHEME_NONE:
          default:
            g_return_val_if_reached (FALSE);
        }

      switch (scheme)
        {
          case PV_PER_ARCH_DIRS_SCHEME_MULTIARCH:
            g_return_val_if_fail (g_str_has_prefix (multiarch_libs[abi], "lib/"), FALSE);
            g_return_val_if_fail (g_str_equal (multiarch_libs[abi] + 4, multiarch_tuple), FALSE);

            if (libdl_string == NULL
                || !g_str_equal (libdl_string, multiarch_libs[abi]))
              return FALSE;

            break;

          case PV_PER_ARCH_DIRS_SCHEME_UBUNTU_1204:
            if (libdl_string == NULL
                || !g_str_equal (libdl_string, multiarch_tuple))
              return FALSE;

            break;

          case PV_PER_ARCH_DIRS_SCHEME_FHS:
            if (libdl_string == NULL
                || !g_str_equal (libdl_string, fhs_libs[abi]))
              return FALSE;

            break;

#if defined(__i386__) || defined(__x86_64__)
          case PV_PER_ARCH_DIRS_SCHEME_ARCH:
            if (libdl_string == NULL
                || !g_str_equal (libdl_string, arch_libs[abi]))
              return FALSE;

            break;
#endif

          case PV_PER_ARCH_DIRS_SCHEME_PLATFORM:
            if (libdl_string == NULL)
              return FALSE;

            break;

          case PV_PER_ARCH_DIRS_SCHEME_NONE:
          default:
            g_return_val_if_reached (FALSE);
        }
    }

  return TRUE;
}

void
pv_per_arch_dirs_free (PvPerArchDirs *self)
{
  gsize abi;

  if (self->root_path != NULL)
    _srt_rm_rf (self->root_path);

  g_clear_pointer (&self->root_path, g_free);
  g_clear_pointer (&self->libdl_token_path, g_free);

  for (abi = 0; abi < G_N_ELEMENTS (self->abi_paths); abi++)
    g_clear_pointer (&self->abi_paths[abi], g_free);

  g_free (self);
}

PvPerArchDirs *
pv_per_arch_dirs_new (GError **error)
{
  g_autoptr(PvPerArchDirs) self = g_new0 (PvPerArchDirs, 1);
  g_autoptr(SrtSystemInfo) info = NULL;
  gsize abi;

  info = srt_system_info_new (NULL);

  self->root_path = g_dir_make_tmp ("pressure-vessel-libs-XXXXXX", error);
  if (self->root_path == NULL)
    return glnx_prefix_error_null (error,
                                   "Cannot create temporary directory for platform specific libraries");

  if (pv_per_arch_dirs_supports_scheme (info, PV_PER_ARCH_DIRS_SCHEME_MULTIARCH))
    {
      self->libdl_token_path = g_build_filename (self->root_path, "${LIB}", NULL);

      for (abi = 0; abi < PV_N_SUPPORTED_ARCHITECTURES; abi++)
        self->abi_paths[abi] = g_build_filename (self->root_path, multiarch_libs[abi], NULL);
    }
  else if (pv_per_arch_dirs_supports_scheme (info, PV_PER_ARCH_DIRS_SCHEME_UBUNTU_1204))
    {
      self->libdl_token_path = g_build_filename (self->root_path, "lib/${LIB}", NULL);

      for (abi = 0; abi < PV_N_SUPPORTED_ARCHITECTURES; abi++)
        self->abi_paths[abi] = g_build_filename (self->root_path, multiarch_libs[abi], NULL);
    }
  else if (pv_per_arch_dirs_supports_scheme (info, PV_PER_ARCH_DIRS_SCHEME_FHS))
    {
      self->libdl_token_path = g_build_filename (self->root_path, "${LIB}", NULL);

      for (abi = 0; abi < PV_N_SUPPORTED_ARCHITECTURES; abi++)
        self->abi_paths[abi] = g_build_filename (self->root_path, fhs_libs[abi], NULL);
    }
#if defined(__i386__) || defined(__x86_64__)
  else if (pv_per_arch_dirs_supports_scheme (info, PV_PER_ARCH_DIRS_SCHEME_ARCH))
    {
      self->libdl_token_path = g_build_filename (self->root_path, "${LIB}", NULL);

      for (abi = 0; abi < PV_N_SUPPORTED_ARCHITECTURES; abi++)
        self->abi_paths[abi] = g_build_filename (self->root_path, arch_libs[abi], NULL);
    }
#endif
  else
    {
      self->libdl_token_path = g_build_filename (self->root_path,
                                                 "${PLATFORM}", NULL);

      for (abi = 0; abi < PV_N_SUPPORTED_ARCHITECTURES; abi++)
        {
          g_autofree gchar *libdl_platform = get_libdl_platform_or_mock (info, abi, error);

          if (!libdl_platform)
            return glnx_prefix_error_null (error,
                                           "Unknown expansion of the dl string token $PLATFORM");

          self->abi_paths[abi] = g_build_filename (self->root_path, libdl_platform, NULL);
        }
    }

  for (abi = 0; abi < PV_N_SUPPORTED_ARCHITECTURES; abi++)
    {
      const char *abi_path = self->abi_paths[abi];

      if (g_mkdir_with_parents (abi_path, 0700) != 0)
        return glnx_null_throw_errno_prefix (error, "Unable to create \"%s\"", abi_path);
    }

  return g_steal_pointer (&self);
}

gboolean
pv_adverb_set_up_overrides (FlatpakBwrap *wrapped_command,
                            PvPerArchDirs *lib_temp_dirs,
                            const char *overrides,
                            GError **error)
{
  g_autofree gchar *value = NULL;
  gsize abi;

  g_return_val_if_fail (wrapped_command != NULL, FALSE);

  if (lib_temp_dirs == NULL)
    return glnx_throw (error, "Unable to set up VDPAU driver search path");

  for (abi = 0; abi < PV_N_SUPPORTED_ARCHITECTURES; abi++)
    {
      const char *multiarch_tuple = pv_multiarch_details[abi].tuple;
      gchar *abi_path = g_build_filename (lib_temp_dirs->abi_paths[abi], "vdpau", NULL);
      gchar *target = g_build_filename (overrides, multiarch_tuple, "vdpau", NULL);

      if (!g_file_test (target, G_FILE_TEST_IS_DIR))
        continue;

      g_debug ("Creating \"%s\" -> \"%s\"", abi_path, target);

      if (symlink (target, abi_path) != 0)
        return glnx_throw_errno_prefix (error, "Cannot create symlink \"%s\"", abi_path);
    }

  value = g_build_filename (lib_temp_dirs->libdl_token_path, "vdpau", NULL);
  g_debug ("Setting VDPAU_DRIVER_PATH=\"%s\"", value);
  flatpak_bwrap_set_env (wrapped_command, "VDPAU_DRIVER_PATH", value, TRUE);
  return TRUE;
}
