/* vi:set et sw=2 sts=2 cin cino=t0,f0,(0,{s,>2s,n-s,^-s,e-s:
 * Taken from Flatpak
 * Last updated: Flatpak 1.15.10
 * Copyright © 2014-2019 Red Hat, Inc
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
 *
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 */

#include "config.h"

#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/vfs.h>
#include <sys/personality.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <grp.h>
#include <unistd.h>
#include <gio/gunixfdlist.h>

#if 0
#include <glib/gi18n-lib.h>
#endif

#include <gio/gio.h>
#include "libglnx.h"

#include "flatpak-exports-private.h"
#include "flatpak-metadata-private.h"
#include "flatpak-utils-base-private.h"
#include "flatpak-utils-private.h"
#include "flatpak-error.h"

/* pressure-vessel-specific */
#include "pressure-vessel/utils.h"

static const char * const abs_usrmerged_dirs[] =
{
  "/bin",
  "/lib",
  "/lib32",
  "/lib64",
  "/sbin",
  NULL
};
const char * const *flatpak_abs_usrmerged_dirs = abs_usrmerged_dirs;

/*
 * pressure-vessel-specific note:
 * This array is not used, but when merging from Flatpak, make sure any new
 * paths here are copied into pv_get_reserved_paths().
 */
#if 0
/* We don't want to export paths pointing into these, because they are readonly
   (so we can't create mountpoints there) and don't match what's on the host anyway.
   flatpak_abs_usrmerged_dirs get the same treatment without having to be listed
   here. */
static const char *dont_export_in[] = {
  "/.flatpak-info",
  "/app",
  "/dev",
  "/etc",
  "/proc",
  "/run/flatpak",
  "/run/host",
  "/usr",
  NULL
};
#endif

static char *
make_relative (const char *base, const char *path)
{
  GString *s = g_string_new ("");

  while (*base != 0)
    {
      while (*base == '/')
        base++;

      if (*base != 0)
        g_string_append (s, "../");

      while (*base != '/' && *base != 0)
        base++;
    }

  while (*path == '/')
    path++;

  g_string_append (s, path);

  return g_string_free (s, FALSE);
}

#define FAKE_MODE_DIR -1 /* Ensure a dir, either on tmpfs or mapped parent */
#define FAKE_MODE_TMPFS FLATPAK_FILESYSTEM_MODE_NONE
#define FAKE_MODE_SYMLINK G_MAXINT

static inline gboolean
is_export_mode (int mode)
{
  return ((mode >= FLATPAK_FILESYSTEM_MODE_NONE
           && mode <= FLATPAK_FILESYSTEM_MODE_LAST)
          || mode == FAKE_MODE_DIR
          || mode == FAKE_MODE_SYMLINK);
}

static inline const char *
export_mode_to_verb (int mode)
{
  switch (mode)
    {
      case FAKE_MODE_DIR:
        return "ensure existence of directory";

      case FAKE_MODE_SYMLINK:
        return "create symbolic link";

      default:
        break;
    }

  switch ((FlatpakFilesystemMode) mode)
    {
      case FLATPAK_FILESYSTEM_MODE_READ_ONLY:
        return "export read-only";

      case FLATPAK_FILESYSTEM_MODE_CREATE:
        return "create and export read/write";

      case FLATPAK_FILESYSTEM_MODE_READ_WRITE:
        return "export read/write";

      case FLATPAK_FILESYSTEM_MODE_NONE:
        return "replace with tmpfs";

      default:
        return "[use unknown/invalid mode?]";
    }
}

typedef struct
{
  char *path;
  gint  mode;
} ExportedPath;

struct _FlatpakExports
{
  GHashTable           *hash;
  FlatpakFilesystemMode host_etc;
  FlatpakFilesystemMode host_os;
  int                   host_fd;
  FlatpakExportsTestFlags test_flags;
};

/*
 * When populating /run/host, pretend @fd was the root of the host
 * filesystem.
 */
void
flatpak_exports_take_host_fd (FlatpakExports *exports,
                              int fd)
{
  glnx_close_fd (&exports->host_fd);

  if (fd >= 0)
    exports->host_fd = fd;
}

void
flatpak_exports_set_test_flags (FlatpakExports *exports,
                                FlatpakExportsTestFlags flags)
{
  exports->test_flags = flags;
}

static gboolean
flatpak_exports_stat_in_host (FlatpakExports *exports,
                              const char *abs_path,
                              struct stat *buf,
                              int flags,
                              GError **error)
{
  g_return_val_if_fail (abs_path[0] == '/', FALSE);

  if (exports->host_fd >= 0)
    {
      /* If abs_path is "/usr", then stat "usr" relative to host_fd.
       * As a special case, if abs_path is "/", stat host_fd itself,
       * due to the use of AT_EMPTY_PATH.
       *
       * This won't work if ${host_fd}/${abs_path} contains symlinks
       * that are absolute or otherwise escape from the mock root,
       * so be careful not to do that in unit tests. */
      return glnx_fstatat (exports->host_fd, &abs_path[1], buf,
                           AT_EMPTY_PATH | flags,
                           error);
    }

  return glnx_fstatat (AT_FDCWD, abs_path, buf, flags, error);
}

static gchar *
flatpak_exports_readlink_in_host (FlatpakExports *exports,
                                  const char *abs_path,
                                  GError **error)
{
  g_return_val_if_fail (abs_path[0] == '/', FALSE);

  /* Similar to flatpak_exports_stat_in_host, this assumes the mock root
   * doesn't contain symlinks that escape from the mock root. */
  if (exports->host_fd >= 0)
    return glnx_readlinkat_malloc (exports->host_fd, &abs_path[1],
                                   NULL, error);

  return glnx_readlinkat_malloc (AT_FDCWD, abs_path, NULL, error);
}

/* path must be absolute, but this is not checked because assertions
 * are not async-signal-safe. */
static int
flatpak_exports_open_in_host_async_signal_safe (FlatpakExports *exports,
                                                const char *abs_path,
                                                int flags)
{
  flags |= O_CLOEXEC;

  if (exports->host_fd >= 0)
    {
      /* Similar to flatpak_exports_stat_in_host, this assumes the mock root
       * doesn't contain symlinks that escape from the mock root. */
      return openat (exports->host_fd, &abs_path[1],
                     flags);
    }

  return openat (AT_FDCWD, abs_path, flags);
}

static int
flatpak_exports_open_in_host (FlatpakExports *exports,
                              const char *abs_path,
                              int flags)
{
  g_return_val_if_fail (abs_path[0] == '/', -1);
  return flatpak_exports_open_in_host_async_signal_safe (exports, abs_path, flags);
}

static char *
flatpak_exports_resolve_link_in_host (FlatpakExports *exports,
                                      const char *abs_path,
                                      GError **error)
{
  g_return_val_if_fail (abs_path[0] == '/', FALSE);

  if (exports->host_fd >= 0)
    {
      g_autofree char *fd_path = g_strdup_printf ("/proc/self/fd/%d/",
                                                  exports->host_fd);
      g_autofree char *real_path = g_strdup_printf ("%s%s", fd_path, &abs_path[1]);
      g_autofree char *resolved = flatpak_resolve_link (real_path, error);

      if (resolved == NULL)
        return NULL;

      if (!g_str_has_prefix (resolved, fd_path))
        return glnx_null_throw (error, "Symbolic link escapes from mock root");

      return g_strdup (resolved + strlen (fd_path) - 1);
    }

  return flatpak_resolve_link (abs_path, error);
}

static void
exported_path_free (ExportedPath *exported_path)
{
  g_free (exported_path->path);
  g_free (exported_path);
}

FlatpakExports *
flatpak_exports_new (void)
{
  FlatpakExports *exports = g_new0 (FlatpakExports, 1);

  exports->hash = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GFreeFunc) exported_path_free);
  exports->host_fd = -1;
  return exports;
}

void
flatpak_exports_free (FlatpakExports *exports)
{
  glnx_close_fd (&exports->host_fd);
  g_hash_table_destroy (exports->hash);
  g_free (exports);
}

/* Returns TRUE if the location of this export
   is not visible due to parents being exported */
static gboolean
path_parent_is_mapped (const char **keys,
                       guint        n_keys,
                       GHashTable  *hash_table,
                       const char  *path)
{
  guint i;
  gboolean is_mapped = FALSE;

  /* The keys are sorted so shorter (i.e. parents) are first */
  for (i = 0; i < n_keys; i++)
    {
      const char *mounted_path = keys[i];
      ExportedPath *ep = g_hash_table_lookup (hash_table, mounted_path);

      g_assert (is_export_mode (ep->mode));

      if (flatpak_has_path_prefix (path, mounted_path) &&
          (strcmp (path, mounted_path) != 0))
        {
          /* FAKE_MODE_DIR has same mapped value as parent */
          if (ep->mode == FAKE_MODE_DIR)
            continue;

          is_mapped = ep->mode != FAKE_MODE_TMPFS;
        }
    }

  return is_mapped;
}

static gboolean
path_is_mapped (const char **keys,
                guint        n_keys,
                GHashTable  *hash_table,
                const char  *path,
                gboolean    *is_readonly_out)
{
  guint i;
  gboolean is_mapped = FALSE;
  gboolean is_readonly = FALSE;

  /* The keys are sorted so shorter (i.e. parents) are first */
  for (i = 0; i < n_keys; i++)
    {
      const char *mounted_path = keys[i];
      ExportedPath *ep = g_hash_table_lookup (hash_table, mounted_path);

      g_assert (is_export_mode (ep->mode));

      if (flatpak_has_path_prefix (path, mounted_path))
        {
          /* FAKE_MODE_DIR has same mapped value as parent */
          if (ep->mode == FAKE_MODE_DIR)
            continue;

          if (ep->mode == FAKE_MODE_SYMLINK)
            is_mapped = strcmp (path, mounted_path) == 0;
          else
            is_mapped = ep->mode != FAKE_MODE_TMPFS;

          if (is_mapped)
            is_readonly = ep->mode == FLATPAK_FILESYSTEM_MODE_READ_ONLY;
          else
            is_readonly = FALSE;
        }
    }

  *is_readonly_out = is_readonly;
  return is_mapped;
}

static gint
compare_eps (const ExportedPath *a,
             const ExportedPath *b)
{
  return g_strcmp0 (a->path, b->path);
}

/* This differs from g_file_test (path, G_FILE_TEST_IS_DIR) which
   returns true if the path is a symlink to a dir */
static gboolean
path_is_dir (FlatpakExports *exports,
             const char *path)
{
  struct stat s;

  if (!flatpak_exports_stat_in_host (exports, path, &s, AT_SYMLINK_NOFOLLOW, NULL))
    return FALSE;

  return S_ISDIR (s.st_mode);
}

static gboolean
path_is_symlink (FlatpakExports *exports,
                 const char *path)
{
  struct stat s;

  if (!flatpak_exports_stat_in_host (exports, path, &s, AT_SYMLINK_NOFOLLOW, NULL))
    return FALSE;

  return S_ISLNK (s.st_mode);
}

/*
 * @name: A file or directory below /etc
 * @test: How we test whether it is suitable
 *
 * The paths in /etc that are required if we want to make use of the
 * host /usr (and /lib, and so on).
 */
typedef struct
{
  const char *name;
  int ifmt;
} LibsNeedEtc;

static const LibsNeedEtc libs_need_etc[] =
{
  /* glibc */
  { "ld.so.cache", S_IFREG },
  /* Used for executables and a few libraries on e.g. Debian */
  { "alternatives", S_IFDIR }
};

void
flatpak_exports_append_bwrap_args (FlatpakExports *exports,
                                   FlatpakBwrap   *bwrap)
{
  guint n_keys;
  g_autofree const char **keys = (const char **) g_hash_table_get_keys_as_array (exports->hash, &n_keys);
  g_autoptr(GList) eps = NULL;
  GList *l;
  struct stat buf;

  eps = g_hash_table_get_values (exports->hash);
  eps = g_list_sort (eps, (GCompareFunc) compare_eps);

  qsort (keys, n_keys, sizeof (char *), flatpak_strcmp0_ptr);

  g_debug ("Converting FlatpakExports to bwrap arguments...");

  for (l = eps; l != NULL; l = l->next)
    {
      ExportedPath *ep = l->data;
      const char *path = ep->path;

      g_assert (is_export_mode (ep->mode));

      if (ep->mode == FAKE_MODE_SYMLINK)
        {
          g_debug ("\"%s\" is meant to be a symlink", path);

          if (path_parent_is_mapped (keys, n_keys, exports->hash, path))
            {
              g_debug ("Not creating \"%s\" as symlink because its parent is "
                       "already mapped", path);
            }
          else
            {
              g_autofree char *resolved = flatpak_exports_resolve_link_in_host (exports,
                                                                                path,
                                                                                 NULL);
              if (resolved)
                {
                  g_autofree char *parent = g_path_get_dirname (path);
                  g_autofree char *relative = make_relative (parent, resolved);

                  g_debug ("Resolved \"%s\" to \"%s\" in host", path, resolved);
                  g_debug ("Creating \"%s\" -> \"%s\" in sandbox", path, relative);
                  flatpak_bwrap_add_args (bwrap, "--symlink", relative, path,  NULL);
                }
              else
                {
                  g_debug ("Unable to resolve \"%s\" in host, skipping", path);
                }
            }
        }
      else if (ep->mode == FAKE_MODE_TMPFS)
        {
          g_debug ("\"%s\" is meant to be a tmpfs or empty directory", path);

          /* Mount a tmpfs to hide the subdirectory, but only if there
             is a pre-existing dir we can mount the path on. */
          if (path_is_dir (exports, path))
            {
              if (!path_parent_is_mapped (keys, n_keys, exports->hash, path))
                /* If the parent is not mapped, it will be a tmpfs, no need to mount another one */
                {
                  g_debug ("Parent of \"%s\" is not mapped, creating empty directory", path);
                  flatpak_bwrap_add_args (bwrap, "--dir", path, NULL);
                }
              else
                {
                  g_debug ("Parent of \"%s\" is mapped, creating tmpfs to shadow it", path);
                  flatpak_bwrap_add_args (bwrap, "--tmpfs", path, NULL);
                }
            }
          else
            {
              g_debug ("Not a directory, skipping: \"%s\"", path);
            }
        }
      else if (ep->mode == FAKE_MODE_DIR)
        {
          g_debug ("\"%s\" is meant to be a directory", path);

          if (path_is_dir (exports, path))
            {
              g_debug ("Ensuring \"%s\" is created as a directory", path);
              flatpak_bwrap_add_args (bwrap, "--dir", path, NULL);
            }
          else
            {
              g_debug ("Not a directory, skipping: \"%s\"", path);
            }
        }
      else
        {
          g_debug ("\"%s\" is meant to be shared (ro or rw) with the container",
                   path);
          flatpak_bwrap_add_args (bwrap,
                                  (ep->mode == FLATPAK_FILESYSTEM_MODE_READ_ONLY) ? "--ro-bind" : "--bind",
                                  path, path, NULL);
        }
    }

  g_assert (exports->host_os >= FLATPAK_FILESYSTEM_MODE_NONE);
  g_assert (exports->host_os <= FLATPAK_FILESYSTEM_MODE_LAST);

  if (exports->host_os != FLATPAK_FILESYSTEM_MODE_NONE)
    {
      const char *os_bind_mode = "--bind";
      int i;

      if (exports->host_os == FLATPAK_FILESYSTEM_MODE_READ_ONLY)
        os_bind_mode = "--ro-bind";

      if (flatpak_exports_stat_in_host (exports, "/usr", &buf, 0, NULL) &&
          S_ISDIR (buf.st_mode))
        flatpak_bwrap_add_args (bwrap,
                                os_bind_mode, "/usr", "/run/host/usr", NULL);

      /* /usr/local points to ../var/usrlocal on ostree systems,
	 so bind-mount that too. */
      if (flatpak_exports_stat_in_host (exports, "/var/usrlocal", &buf, 0, NULL) &&
	    S_ISDIR (buf.st_mode))
        flatpak_bwrap_add_args (bwrap,
                                os_bind_mode, "/var/usrlocal", "/run/host/var/usrlocal", NULL);

      for (i = 0; flatpak_abs_usrmerged_dirs[i] != NULL; i++)
        {
          const char *subdir = flatpak_abs_usrmerged_dirs[i];
          g_autofree char *target = NULL;
          g_autofree char *run_host_subdir = NULL;

          g_assert (subdir[0] == '/');
          /* e.g. /run/host/lib32 */
          run_host_subdir = g_strconcat ("/run/host", subdir, NULL);
          target = flatpak_exports_readlink_in_host (exports, subdir, NULL);

          if (target != NULL &&
              g_str_has_prefix (target, "usr/"))
            {
              /* e.g. /lib32 is a relative symlink to usr/lib32, or
               * on Arch Linux, /lib64 is a relative symlink to usr/lib;
               * keep it relative */
              flatpak_bwrap_add_args (bwrap,
                                      "--symlink", target, run_host_subdir,
                                      NULL);
            }
          else if (target != NULL &&
                   g_str_has_prefix (target, "/usr/"))
            {
              /* e.g. /lib32 is an absolute symlink to /usr/lib32; make
               * it a relative symlink to usr/lib32 instead by skipping
               * the '/' */
              flatpak_bwrap_add_args (bwrap,
                                      "--symlink", target + 1, run_host_subdir,
                                      NULL);
            }
          else if (flatpak_exports_stat_in_host (exports, subdir, &buf, 0, NULL) &&
                   S_ISDIR (buf.st_mode))
            {
              /* e.g. /lib32 is a symlink to /opt/compat/ia32/lib,
               * or is a plain directory because the host OS has not
               * undergone the /usr merge; bind-mount the directory instead */
              flatpak_bwrap_add_args (bwrap,
                                      os_bind_mode, subdir, run_host_subdir,
                                      NULL);
            }
        }

      if (exports->host_etc == FLATPAK_FILESYSTEM_MODE_NONE)
        {
          /* We are exposing the host /usr (and friends) but not the
           * host /etc. Additionally expose just enough of /etc to make
           * things that want to read /usr work as expected.
           *
           * (If exports->host_etc is nonzero, we'll do this as part of
           * /etc instead.) */

          for (i = 0; i < G_N_ELEMENTS (libs_need_etc); i++)
            {
              const LibsNeedEtc *item = &libs_need_etc[i];
              g_autofree gchar *host_path = g_strconcat ("/etc/", item->name, NULL);

              if (flatpak_exports_stat_in_host (exports, host_path,
                                                &buf, 0, NULL) &&
                  (buf.st_mode & S_IFMT) == item->ifmt)
                {
                  g_autofree gchar *run_host_path = g_strconcat ("/run/host/etc/", item->name, NULL);

                  flatpak_bwrap_add_args (bwrap,
                                          os_bind_mode, host_path, run_host_path,
                                          NULL);
                }
            }
        }
    }

  g_assert (exports->host_etc >= FLATPAK_FILESYSTEM_MODE_NONE);
  g_assert (exports->host_etc <= FLATPAK_FILESYSTEM_MODE_LAST);

  if (exports->host_etc != FLATPAK_FILESYSTEM_MODE_NONE)
    {
      const char *etc_bind_mode = "--bind";

      if (exports->host_etc == FLATPAK_FILESYSTEM_MODE_READ_ONLY)
        etc_bind_mode = "--ro-bind";

      if (flatpak_exports_stat_in_host (exports, "/etc", &buf, 0, NULL) &&
          S_ISDIR (buf.st_mode))
        flatpak_bwrap_add_args (bwrap,
                                etc_bind_mode, "/etc", "/run/host/etc", NULL);
    }

  /* As per the os-release specification https://www.freedesktop.org/software/systemd/man/os-release.html
   * always read-only bind-mount /etc/os-release if it exists, or /usr/lib/os-release as a fallback from
   * the host into the application's /run/host */
  if (flatpak_exports_stat_in_host (exports, "/etc/os-release", &buf, 0, NULL))
    flatpak_bwrap_add_args (bwrap, "--ro-bind", "/etc/os-release", "/run/host/os-release", NULL);
  else if (flatpak_exports_stat_in_host (exports, "/usr/lib/os-release", &buf, 0, NULL))
    flatpak_bwrap_add_args (bwrap, "--ro-bind", "/usr/lib/os-release", "/run/host/os-release", NULL);
}

/* Returns FLATPAK_FILESYSTEM_MODE_NONE if not visible */
FlatpakFilesystemMode
flatpak_exports_path_get_mode (FlatpakExports *exports,
                               const char     *path)
{
  guint n_keys;
  g_autofree const char **keys = (const char **) g_hash_table_get_keys_as_array (exports->hash, &n_keys);
  g_autofree char *canonical = NULL;
  gboolean is_readonly = FALSE;
  g_auto(GStrv) parts = NULL;
  int i;
  g_autoptr(GString) path_builder = g_string_new ("");
  struct stat st;

  qsort (keys, n_keys, sizeof (char *), flatpak_strcmp0_ptr);

  /* Syntactic canonicalization only, no need to use host_fd */
  path = canonical = flatpak_canonicalize_filename (path);

  parts = g_strsplit (path + 1, "/", -1);

  /* A path is visible in the sandbox if no parent
   * path element that is mapped in the sandbox is
   * a symlink, and the final element is mapped.
   * If any parent is a symlink we resolve that and
   * continue with that instead.
   */
  for (i = 0; parts[i] != NULL; i++)
    {
      g_string_append (path_builder, "/");
      g_string_append (path_builder, parts[i]);

      if (path_is_mapped (keys, n_keys, exports->hash, path_builder->str, &is_readonly))
        {
          g_autoptr(GError) stat_error = NULL;

          if (!flatpak_exports_stat_in_host (exports, path_builder->str, &st, AT_SYMLINK_NOFOLLOW, &stat_error))
            {
              if (g_error_matches (stat_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) &&
                  parts[i + 1] == NULL &&
                  !is_readonly)
                {
                  /* Last element was mapped but isn't there, this is
                   * OK (used for the save case) if we the parent is
                   * mapped and writable, as the app can then create
                   * the file here.
                   */
                  break;
                }

              return FLATPAK_FILESYSTEM_MODE_NONE;
            }

          if (S_ISLNK (st.st_mode))
            {
              g_autofree char *resolved = flatpak_exports_resolve_link_in_host (exports,
                                                                                path_builder->str,
                                                                                NULL);
              g_autoptr(GString) path2_builder = NULL;
              int j;

              if (resolved == NULL)
                return FLATPAK_FILESYSTEM_MODE_NONE;

              path2_builder = g_string_new (resolved);

              for (j = i + 1; parts[j] != NULL; j++)
                {
                  g_string_append (path2_builder, "/");
                  g_string_append (path2_builder, parts[j]);
                }

              return flatpak_exports_path_get_mode (exports, path2_builder->str);
            }
        }
      else if (parts[i + 1] == NULL)
        return FLATPAK_FILESYSTEM_MODE_NONE; /* Last part was not mapped */
    }

  if (is_readonly)
    return FLATPAK_FILESYSTEM_MODE_READ_ONLY;

  return FLATPAK_FILESYSTEM_MODE_READ_WRITE;
}

gboolean
flatpak_exports_path_is_visible (FlatpakExports *exports,
                                 const char     *path)
{
  return flatpak_exports_path_get_mode (exports, path) > FLATPAK_FILESYSTEM_MODE_NONE;
}

static gboolean
never_export_as_symlink (const char *path)
{
  /* Don't export {/var,}/tmp as a symlink even if it is on the host, because
     that will fail with the pre-existing directory we created for it,
     and anyway, it being a symlink is not useful in the sandbox */
  if (strcmp (path, "/tmp") == 0 || strcmp (path, "/var/tmp") == 0)
    return TRUE;

  return FALSE;
}

static void
do_export_path (FlatpakExports *exports,
                const char     *path,
                gint            mode)
{
  ExportedPath *old_ep = g_hash_table_lookup (exports->hash, path);
  ExportedPath *ep;

  g_return_if_fail (is_export_mode (mode));

  ep = g_new0 (ExportedPath, 1);
  ep->path = g_strdup (path);

  if (old_ep != NULL)
    {
      if (old_ep->mode < mode)
        {
          g_debug ("Increasing export mode from \"%s\" to \"%s\": %s",
                   export_mode_to_verb (old_ep->mode),
                   export_mode_to_verb (mode),
                   path);
          ep->mode = mode;
        }
      else
        {
          g_debug ("Not changing export mode from \"%s\" to \"%s\": %s",
                   export_mode_to_verb (old_ep->mode),
                   export_mode_to_verb (mode),
                   path);
          ep->mode = old_ep->mode;
        }
    }
  else
    {
      g_debug ("Will %s: %s", export_mode_to_verb (mode), path);
      ep->mode = mode;
    }

  g_hash_table_replace (exports->hash, ep->path, ep);
}

/* AUTOFS mounts are tricky, as using them as a source in a bind mount
 * causes the mount to trigger, which can take a long time (or forever)
 * waiting for a device or network mount. We try to open the directory
 * but time out after a while, ignoring the mount. Unfortunately we
 * have to mess with forks and stuff to be able to handle the timeout.
 */
static gboolean
check_if_autofs_works (FlatpakExports *exports,
                       const char *path)
{
  int selfpipe[2];
  struct timeval timeout;
  pid_t pid;
  fd_set rfds;
  int res;
  int wstatus;

  g_return_val_if_fail (path[0] == '/', FALSE);

  if (pipe2 (selfpipe, O_CLOEXEC) == -1)
    return FALSE;

  fcntl (selfpipe[0], F_SETFL, fcntl (selfpipe[0], F_GETFL) | O_NONBLOCK);
  fcntl (selfpipe[1], F_SETFL, fcntl (selfpipe[1], F_GETFL) | O_NONBLOCK);

  pid = fork ();
  if (pid == -1)
    {
      close (selfpipe[0]);
      close (selfpipe[1]);
      return FALSE;
    }

  if (pid == 0)
    {
      /* Note: open, close and _exit are signal-async-safe, so it is ok to call in the child after fork */

      close (selfpipe[0]); /* Close unused read end */

      int dir_fd = flatpak_exports_open_in_host_async_signal_safe (exports,
                                                                   path,
                                                                   O_RDONLY | O_NONBLOCK | O_DIRECTORY);
      _exit (dir_fd == -1 ? 1 : 0);
    }

  /* Parent */
  close (selfpipe[1]);  /* Close unused write end */

  /* 200 msec timeout*/
  timeout.tv_sec = 0;
  timeout.tv_usec = 200 * 1000;

  FD_ZERO (&rfds);
  FD_SET (selfpipe[0], &rfds);
  res = select (selfpipe[0] + 1, &rfds, NULL, NULL, &timeout);

  close (selfpipe[0]);

  if (res == -1 /* Error */ || res == 0) /* Timeout */
    {
      /* Kill, but then waitpid to avoid zombie */
      kill (pid, SIGKILL);
    }

  if (waitpid (pid, &wstatus, 0) != pid)
    return FALSE;

  if (res == -1 /* Error */ || res == 0) /* Timeout */
    return FALSE;

  if (!WIFEXITED (wstatus) || WEXITSTATUS (wstatus) != 0)
    return FALSE;

  if (G_UNLIKELY (exports->test_flags & FLATPAK_EXPORTS_TEST_FLAGS_AUTOFS))
    {
      if (strcmp (path, "/broken-autofs") == 0)
        return FALSE;
    }

  return TRUE;
}

/* We use level to avoid infinite recursion.
 *
 * Note that some of the errors produced by this function are "real errors"
 * and should show up as a user-visible warning, but others are relatively
 * uninteresting, and in general none are actually fatal: we prefer to
 * continue with fewer paths exposed rather than failing to run. */
static gboolean
_exports_path_expose (FlatpakExports *exports,
                      int             mode,
                      const char     *path,
                      int             level,
                      GError        **error)
{
  g_autofree char *canonical = NULL;
  const char * const *dont_export_in;
  struct stat st;
  struct statfs stfs;
  char *slash;
  int i;
  glnx_autofd int o_path_fd = -1;

  g_return_val_if_fail (is_export_mode (mode), FALSE);

  g_debug ("Trying to %s: %s", export_mode_to_verb (mode), path);

  if (level > 40) /* 40 is the current kernel ELOOP check */
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_TOO_MANY_LINKS,
                   "%s", g_strerror (ELOOP));
      return FALSE;
    }

  if (!g_path_is_absolute (path))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
                   _("An absolute path is required"));
      return FALSE;
    }

  /* Check if it exists at all */
  o_path_fd = flatpak_exports_open_in_host (exports, path, O_PATH | O_NOFOLLOW);

  if (o_path_fd == -1)
    {
      int saved_errno = errno;

      /* Intentionally using G_IO_ERROR_NOT_FOUND even if errno is
       * something different, so callers can suppress the warning in this
       * relatively likely and uninteresting case: we don't particularly
       * care whether this is happening as a result of ENOENT or EACCES
       * or any other reason. */
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   _("Unable to open path \"%s\": %s"),
                   path, g_strerror (saved_errno));
      return FALSE;
    }

  if (fstat (o_path_fd, &st) != 0)
    return glnx_throw (error,
                       _("Unable to get file type of \"%s\": %s"),
                       path, g_strerror (errno));

  /* Don't expose weird things */
  if (!(S_ISDIR (st.st_mode) ||
        S_ISREG (st.st_mode) ||
        S_ISLNK (st.st_mode) ||
        S_ISSOCK (st.st_mode)))
    return glnx_throw (error,
                       _("File \"%s\" has unsupported type 0o%o"),
                       path, st.st_mode & S_IFMT);

  /* O_PATH + fstatfs is the magic that we need to statfs without automounting the target */
  if (fstatfs (o_path_fd, &stfs) != 0)
    return glnx_throw (error,
                       _("Unable to get filesystem information for \"%s\": %s"),
                       path, g_strerror (errno));

  if (stfs.f_type == AUTOFS_SUPER_MAGIC ||
      (G_UNLIKELY (exports->test_flags & FLATPAK_EXPORTS_TEST_FLAGS_AUTOFS) &&
       S_ISDIR (st.st_mode)))
    {
      if (!check_if_autofs_works (exports, path))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK,
                       _("Ignoring blocking autofs path \"%s\""), path);
          return FALSE;
        }
    }

  /* Syntactic canonicalization only, no need to use host_fd */
  path = canonical = flatpak_canonicalize_filename (path);

  /* pressure-vessel-specific: Use a longer list of reserved paths
   * than the one in Flatpak */
  dont_export_in = pv_get_reserved_paths ();

  for (i = 0; dont_export_in[i] != NULL; i++)
    {
      /* Don't expose files in non-mounted dirs like /app or /usr, as
         they are not the same as on the host, and we generally can't
         create the parents for them anyway */
      if (flatpak_has_path_prefix (path, dont_export_in[i]))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_MOUNTABLE_FILE,
                       _("Path \"%s\" is reserved by the container framework"),
                       dont_export_in[i]);
          return FALSE;
        }

      /* Also don't expose directories that are a parent of a directory
       * that is "owned" by the sandboxing framework. For example, because
       * Flatpak controls /run/host and /run/flatpak, we cannot allow
       * --filesystem=/run, which would prevent us from creating the
       * contents of /run/host and /run/flatpak. */
      if (flatpak_has_path_prefix (dont_export_in[i], path))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_MOUNTABLE_FILE,
                       _("Path \"%s\" is reserved by the container framework"),
                       dont_export_in[i]);
          return FALSE;
        }
    }

  for (i = 0; flatpak_abs_usrmerged_dirs[i] != NULL; i++)
    {
      /* Same as /usr, but for the directories that get merged into /usr.
       * Keep the translatable string here the same as the one above */
      if (flatpak_has_path_prefix (path, flatpak_abs_usrmerged_dirs[i]))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_MOUNTABLE_FILE,
                       _("Path \"%s\" is reserved by the container framework"),
                       flatpak_abs_usrmerged_dirs[i]);
          return FALSE;
        }
    }

  /* Handle any symlinks prior to the target itself. This includes path itself,
     because we expose the target of the symlink. */
  slash = canonical;
  do
    {
      slash = strchr (slash + 1, '/');
      if (slash)
        *slash = 0;

      if (!path_is_symlink (exports, path))
        {
          g_debug ("%s is not a symlink", path);
        }
      else if (never_export_as_symlink (path))
        {
          g_debug ("%s is a symlink, but we avoid exporting it as such", path);
        }
      else
        {
          g_autoptr(GError) local_error = NULL;
          g_autofree char *resolved = flatpak_exports_resolve_link_in_host (exports, path, &local_error);
          g_autofree char *new_target = NULL;

          if (resolved)
            {
              g_debug ("%s is a symlink, resolved to %s", path, resolved);

              if (slash)
                new_target = g_build_filename (resolved, slash + 1, NULL);
              else
                new_target = g_strdup (resolved);

              g_debug ("Trying to export the target instead: %s", new_target);

              if (_exports_path_expose (exports, mode, new_target, level + 1, &local_error))
                {
                  do_export_path (exports, path, FAKE_MODE_SYMLINK);
                  return TRUE;
                }

              g_debug ("Could not export target %s, so ignoring %s",
                       new_target, path);
              g_propagate_error (error, g_steal_pointer (&local_error));
              return FALSE;
            }
          else
            {
              g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                           _("Unable to resolve symbolic link \"%s\": %s"),
                           path, local_error->message);
              return FALSE;
            }
        }

      if (slash)
        *slash = '/';
    }
  while (slash != NULL);

  do_export_path (exports, path, mode);
  return TRUE;
}

gboolean
flatpak_exports_add_path_expose (FlatpakExports         *exports,
                                 FlatpakFilesystemMode   mode,
                                 const char             *path,
                                 GError                **error)
{
  g_return_val_if_fail (mode > FLATPAK_FILESYSTEM_MODE_NONE, FALSE);
  g_return_val_if_fail (mode <= FLATPAK_FILESYSTEM_MODE_LAST, FALSE);
  return _exports_path_expose (exports, mode, path, 0, error);
}

gboolean
flatpak_exports_add_path_tmpfs (FlatpakExports  *exports,
                                const char      *path,
                                GError         **error)
{
  return _exports_path_expose (exports, FAKE_MODE_TMPFS, path, 0, error);
}

gboolean
flatpak_exports_add_path_expose_or_hide (FlatpakExports        *exports,
                                         FlatpakFilesystemMode  mode,
                                         const char            *path,
                                         GError               **error)
{
  g_return_val_if_fail (mode >= FLATPAK_FILESYSTEM_MODE_NONE, FALSE);
  g_return_val_if_fail (mode <= FLATPAK_FILESYSTEM_MODE_LAST, FALSE);

  if (mode == FLATPAK_FILESYSTEM_MODE_NONE)
    return flatpak_exports_add_path_tmpfs (exports, path, error);
  else
    return flatpak_exports_add_path_expose (exports, mode, path, error);
}

gboolean
flatpak_exports_add_path_dir (FlatpakExports  *exports,
                              const char      *path,
                              GError         **error)
{
  return _exports_path_expose (exports, FAKE_MODE_DIR, path, 0, error);
}

void
flatpak_exports_add_host_etc_expose (FlatpakExports       *exports,
                                     FlatpakFilesystemMode mode)
{
  g_return_if_fail (mode > FLATPAK_FILESYSTEM_MODE_NONE);
  g_return_if_fail (mode <= FLATPAK_FILESYSTEM_MODE_LAST);

  exports->host_etc = mode;
}

void
flatpak_exports_add_host_os_expose (FlatpakExports       *exports,
                                    FlatpakFilesystemMode mode)
{
  g_return_if_fail (mode > FLATPAK_FILESYSTEM_MODE_NONE);
  g_return_if_fail (mode <= FLATPAK_FILESYSTEM_MODE_LAST);

  exports->host_os = mode;
}
