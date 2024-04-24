/*
 * Copyright © 2017-2020 Collabora Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "exports.h"

#include <ftw.h>

#include <steam-runtime-tools/utils-internal.h>

#include "flatpak-context-private.h"

/* nftw() doesn't have a user_data argument so we need to use a global
 * variable :-( */
static struct
{
  FlatpakExports *exports;
  const char *log_replace_prefix;
  const char *log_replace_with;
} export_targets_data;

/* If a symlink target matches one of these prefixes, it's assumed to be
 * intended to refer to a path inside the container, not a path on the host,
 * and therefore not exported.
 *
 * In fact we wouldn't export most of these anyway, because FlatpakExports
 * specifically excludes them - but it's confusing to get log messages saying
 * "Exporting foo because bar", "Unable to open path" for something that we
 * have no intention of exporting anyway. */
static const char * const exclude_prefixes[] =
{
  "/app/",
  "/bin/",
  "/dev/",
  "/etc/",
  "/lib", /* intentionally no trailing "/" to match lib64, etc. */
  "/overrides/",
  "/proc/",
  "/run/gfx/",
  "/run/host/",
  "/run/interpreter-host/",
  "/run/pressure-vessel/",
  "/sbin/",
  "/usr/",
  "/var/pressure-vessel/",
};

static int
export_targets_helper (const char *fpath,
                       const struct stat *sb,
                       int typeflag,
                       struct FTW *ftwbuf)
{
  g_autofree gchar *target = NULL;
  const char *after = NULL;
  size_t i;

  switch (typeflag)
    {
      case FTW_SL:
        target = glnx_readlinkat_malloc (-1, fpath, NULL, NULL);

        if (target[0] != '/')
          break;

        after = _srt_get_path_after (fpath, export_targets_data.log_replace_prefix);

        for (i = 0; i < G_N_ELEMENTS (exclude_prefixes); i++)
          {
            if (g_str_has_prefix (target, exclude_prefixes[i]))
              {
                if (after == NULL)
                  g_debug ("%s points to container-side path %s", fpath, target);
                else
                  g_debug ("%s/%s points to container-side path %s",
                           export_targets_data.log_replace_with, after, target);
                return 0;
              }
          }

        if (after == NULL)
          g_debug ("Exporting %s because %s points to it", target, fpath);
        else
          g_debug ("Exporting %s because %s/%s points to it",
                   target, export_targets_data.log_replace_with, after);

        pv_exports_expose_or_warn (export_targets_data.exports,
                                   FLATPAK_FILESYSTEM_MODE_READ_ONLY,
                                   target);
        break;

      default:
        break;
    }

  return 0;
}

/**
 * pv_export_symlink_targets:
 * @exports: The #FlatpakExports
 * @source: A copy of the overrides directory, for example
 *  `/tmp/tmp12345678/overrides`.
 * @log_as: Replace the @source with @log_as in debug messages,
 *  for example `${overrides}`.
 *
 * For every symbolic link in @source, if the target is absolute, mark
 * it to be exported in @exports.
 */
void
pv_export_symlink_targets (FlatpakExports *exports,
                           const char *source,
                           const char *log_as)
{
  g_return_if_fail (export_targets_data.exports == NULL);

  export_targets_data.exports = exports;
  export_targets_data.log_replace_prefix = source;
  export_targets_data.log_replace_with = log_as;
  nftw (source, export_targets_helper, 100, FTW_PHYS);
  export_targets_data.exports = NULL;
  export_targets_data.log_replace_prefix = NULL;
  export_targets_data.log_replace_with = NULL;
}

/* Originally from flatpak-context, last updated: Flatpak 1.14.6 */
static void
log_cannot_export_error (FlatpakFilesystemMode  mode,
                         const char            *path,
                         const GError          *error)
{
  GLogLevelFlags level = G_LOG_LEVEL_MESSAGE;

  /* By default we don't show a log message if the reason we are not sharing
   * something with the sandbox is simply "it doesn't exist" (or something
   * very close): otherwise it would be very noisy to launch apps that
   * opportunistically share things they might benefit from, like Steam
   * having access to $XDG_RUNTIME_DIR/app/com.discordapp.Discord if it
   * happens to exist. */
  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    level = G_LOG_LEVEL_INFO;
  /* Some callers specifically suppress warnings for particular errors
   * by setting this code. */
  else if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_FAILED_HANDLED))
    level = G_LOG_LEVEL_INFO;

  switch (mode)
    {
      case FLATPAK_FILESYSTEM_MODE_NONE:
        g_log (G_LOG_DOMAIN, level, _("Not replacing \"%s\" with tmpfs: %s"),
               path, error->message);
        break;

      case FLATPAK_FILESYSTEM_MODE_CREATE:
      case FLATPAK_FILESYSTEM_MODE_READ_ONLY:
      case FLATPAK_FILESYSTEM_MODE_READ_WRITE:
        g_log (G_LOG_DOMAIN, level,
               _("Not sharing \"%s\" with sandbox: %s"),
               path, error->message);
        break;

      default:
        g_warn_if_reached ();
    }
}

/*
 * pv_exports_expose_or_log:
 * @exports: The #FlatpakExports
 * @mode: A filesystem mode
 * @path: An absolute path
 *
 * Share @path with the container according to @mode.
 * If this is not possible (typically because the @path is in a reserved
 * location or doesn't exist), log a message, choosing the severity
 * automatically.
 *
 * If the @path on the host system has a symbolic link among its
 * ancestors, e.g. /home/user on systems with /home -> var/home,
 * mirror the symbolic links in the container and expose the directory's
 * real path.
 */
void
pv_exports_expose_or_log (FlatpakExports *exports,
                          FlatpakFilesystemMode mode,
                          const char *path)
{
  g_autoptr(GError) local_error = NULL;

  if (!flatpak_exports_add_path_expose (exports, mode, path, &local_error))
    log_cannot_export_error (mode, path, local_error);
}

/*
 * pv_exports_expose_or_warn:
 * @exports: The #FlatpakExports
 * @mode: A filesystem mode
 * @path: An absolute path
 *
 * Same as pv_exports_expose_or_log(), but always log a warning if the path
 * cannot be shared. Use this for paths that will break reasonable
 * expectations if not shared, such as the home directory.
 */
void
pv_exports_expose_or_warn (FlatpakExports *exports,
                           FlatpakFilesystemMode mode,
                           const char *path)
{
  g_autoptr(GError) local_error = NULL;

  if (!flatpak_exports_add_path_expose (exports, mode, path, &local_error))
    g_warning ("Unable to share \"%s\" with container: %s",
               path, local_error->message);
}

/*
 * pv_exports_expose_quietly:
 * @exports: The #FlatpakExports
 * @mode: A filesystem mode
 * @path: An absolute path
 *
 * Same as pv_exports_expose_or_log(), but never log a warning if the path
 * cannot be shared because it is reserved. Use this for paths that we
 * expect will often be reserved, such as subdirectories of the root directory.
 */
void
pv_exports_expose_quietly (FlatpakExports *exports,
                           FlatpakFilesystemMode mode,
                           const char *path)
{
  g_autoptr(GError) local_error = NULL;

  if (!flatpak_exports_add_path_expose (exports, mode, path, &local_error))
    {
      if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_MOUNTABLE_FILE))
        local_error->code = G_IO_ERROR_FAILED_HANDLED;

      log_cannot_export_error (mode, path, local_error);
      g_clear_error (&local_error);
    }
}

/*
 * pv_exports_mask_or_log:
 * @exports: The #FlatpakExports
 * @mode: A filesystem mode
 * @path: An absolute path
 *
 * Similar to pv_exports_expose_or_warn(), but instead of exposing the
 * @path from the real system, create a new, empty tmpfs in the same
 * place.
 *
 * If the @path on the host system has a symbolic link among its
 * ancestors, e.g. /home/user on systems with /home -> var/home,
 * mirror the symbolic links in the container and mask the directory's
 * real path.
 */
void
pv_exports_mask_or_log (FlatpakExports *exports,
                        const char *path)
{
  g_autoptr(GError) local_error = NULL;

  if (!flatpak_exports_add_path_tmpfs (exports, path, &local_error))
    log_cannot_export_error (FLATPAK_FILESYSTEM_MODE_NONE, path, local_error);
}

/*
 * pv_exports_ensure_dir_or_warn:
 * @exports: The #FlatpakExports
 * @path: An absolute path
 *
 * Similar to pv_exports_expose_or_warn(), but instead of exposing the
 * @path from the real system, create a new, empty @path that mimics the
 * real one.
 *
 * If the @path on the host system has a symbolic link among its
 * ancestors, e.g. /home/user on systems with /home -> var/home,
 * mirror the symbolic links in the container and create the directory's
 * real path.
 *
 * This function should only be called for a @path that is known to
 * exist on the host system, typically the home directory.
 */
void
pv_exports_ensure_dir_or_warn (FlatpakExports *exports,
                               const char *path)
{
  g_autoptr(GError) local_error = NULL;

  if (!flatpak_exports_add_path_dir (exports, path, &local_error))
    g_warning ("Unable to create \"%s\" inside container: %s",
               path, local_error->message);
}
