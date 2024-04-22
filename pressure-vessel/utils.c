/*
 * Contains code taken from Flatpak.
 *
 * Copyright © 2014-2019 Red Hat, Inc
 * Copyright © 2017-2019 Collabora Ltd.
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

#include "utils.h"

#include <ftw.h>
#include <grp.h>
#include <pwd.h>
#include <sys/types.h>

#include <glib.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "libglnx.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/profiling-internal.h"
#include "steam-runtime-tools/resolve-in-sysroot-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "flatpak-bwrap-private.h"
#include "flatpak-utils-base-private.h"
#include "flatpak-utils-private.h"

/*
 * pv_get_reserved_paths:
 *
 * Return reserved directories above or below which user-specified "exports"
 * are not allowed.
 *
 * For example, `/var/cache/ldconfig/ld.so.cache` needs to be controlled
 * by the runtime, so any attempt to export `/var/cache/ldconfig`,
 * `/var/cache` or `/var` needs to be ignored.
 *
 * Returns: (array zero-terminated=1) (transfer none): A NULL-terminated
 *  array of absolute paths
 */
const char * const *
pv_get_reserved_paths (void)
{
  static const char * const paths[] =
  {
    /* Reserved by Flatpak-derived code */
    "/.flatpak-info",
    /* Reserved by Flatpak-derived code */
    "/app",
    /* Conceptually part of /usr */
    "/bin",
    /* /dev is managed separately */
    "/dev",
    /* /etc is merged from the host, gfx provider and runtime */
    "/etc",
    /* Used by pv-runtime */
    "/overrides",
    /* Conceptually part of /usr */
    "/lib",
    "/lib32",
    "/lib64",
    /* Managed separately */
    "/proc",
    /* Reserved by Flatpak-derived code */
    "/run/flatpak",
    /* Used to mount the graphics provider if not the host */
    "/run/gfx",
    /* Used to mount the host filesystem */
    "/run/host",
    /* Used to mount the real host filesystem under FEX-Emu */
    "/run/interpreter-host",
    /* Used when running in a Flatpak subsandbox */
    "/run/parent",
    /* Sockets, /run/pressure-vessel/pv-from-host, etc. */
    "/run/pressure-vessel",
    /* Conceptually part of /usr */
    "/sbin",
    /* Used to mount the runtime */
    "/usr",
    /* Used to mount parts of the graphics stack provider */
    "/var/pressure-vessel",
    /* We need to manage this for compatibility with ClearLinux ld.so */
    "/var/cache/ldconfig",
    NULL
  };

  return paths;
}

PvWorkaroundFlags
pv_get_workarounds (SrtBwrapFlags bwrap_flags,
                    const char * const *envp)
{
  static const struct
  {
    PvWorkaroundFlags flag;
    /* Arbitrary length, extend as necessary */
    const char * const names[2];
  } workarounds[] =
  {
    { PV_WORKAROUND_FLAGS_ALL, { "all" } },
    { PV_WORKAROUND_FLAGS_BWRAP_NO_PERMS, { "bwrap-no-perms", "old-bwrap" } },
    { PV_WORKAROUND_FLAGS_STEAMSNAP_356, { "steam-snap#356", "steamsnap356" } },
    { PV_WORKAROUND_FLAGS_STEAMSNAP_359, { "steam-snap#359", "steamsnap359" } },
    { PV_WORKAROUND_FLAGS_STEAMSNAP_369, { "steam-snap#369", "steamsnap369" } },
    { PV_WORKAROUND_FLAGS_STEAMSNAP_370, { "steam-snap#370", "steamsnap370" } },
    { PV_WORKAROUND_FLAGS_BWRAP_SETUID, { "bwrap-setuid" } },
  };
  PvWorkaroundFlags flags = PV_WORKAROUND_FLAGS_NONE;
  const char *value;

  value = _srt_environ_getenv (envp, "PRESSURE_VESSEL_WORKAROUNDS");

  if (!(bwrap_flags & SRT_BWRAP_FLAGS_HAS_PERMS))
    flags |= PV_WORKAROUND_FLAGS_BWRAP_NO_PERMS;

  if (bwrap_flags & SRT_BWRAP_FLAGS_SETUID)
    flags |= PV_WORKAROUND_FLAGS_BWRAP_SETUID;

  if (_srt_environ_getenv (envp, "SNAP") != NULL
      && _srt_environ_getenv (envp, "SNAP_NAME") != NULL
      && _srt_environ_getenv (envp, "SNAP_REVISION") != NULL)
    flags |= PV_WORKAROUND_FLAGS_SNAP;

  if (value != NULL)
    {
      g_auto(GStrv) tokens = NULL;
      size_t i, j, k;

      g_info ("Workarounds overridden by environment: %s", value);

      tokens = g_strsplit_set (value, " \t,", 0);

      for (i = 0; tokens[i] != NULL; i++)
        {
          const char *token = tokens[i];
          gboolean found = FALSE;
          gboolean negative = FALSE;

          if (g_str_equal (tokens[i], "none"))
            {
              negative = TRUE;
              token = "all";
            }
          else if (token[0] == '+')
            {
              token++;
            }
          else if (token[0] == '-' || token[0] == '!')
            {
              negative = TRUE;
              token++;
            }

          for (j = 0; j < G_N_ELEMENTS (workarounds); j++)
            {
              for (k = 0; k < G_N_ELEMENTS (workarounds[j].names); k++)
                {
                  if (g_strcmp0 (workarounds[j].names[k], token) == 0)
                    {
                      found = TRUE;

                      if (negative)
                        flags &= ~workarounds[j].flag;
                      else
                        flags |= workarounds[j].flag;
                    }
                }
            }

          if (!found)
            g_warning ("Workaround token not understood: %s", tokens[i]);
        }
    }

  if (flags == 0)
    {
      g_debug ("No workarounds enabled");
    }
  else
    {
      size_t i;

      g_info ("Workarounds enabled: 0x%x", flags);

      for (i = 0; i < G_N_ELEMENTS (workarounds); i++)
        {
          if (_srt_all_bits_set (flags, workarounds[i].flag))
            g_info ("- %s", workarounds[i].names[0]);
        }
    }

  return flags;
}

void
pv_search_path_append (GString *search_path,
                       const gchar *item)
{
  g_return_if_fail (search_path != NULL);

  if (item == NULL || item[0] == '\0')
    return;

  if (search_path->len != 0)
    g_string_append (search_path, ":");

  g_string_append (search_path, item);
}

gboolean
pv_run_sync (const char * const * argv,
             const char * const * envp,
             int *exit_status_out,
             char **output_out,
             GError **error)
{
  gsize len;
  gint wait_status;
  g_autofree gchar *output = NULL;
  g_autofree gchar *errors = NULL;
  gsize i;
  g_autoptr(GString) command = g_string_new ("");

  g_return_val_if_fail (argv != NULL, FALSE);
  g_return_val_if_fail (argv[0] != NULL, FALSE);
  g_return_val_if_fail (output_out == NULL || *output_out == NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (exit_status_out != NULL)
    *exit_status_out = -1;

  for (i = 0; argv[i] != NULL; i++)
    {
      g_autofree gchar *quoted = g_shell_quote (argv[i]);

      g_string_append_printf (command, " %s", quoted);
    }

  g_debug ("run:%s", command->str);

  /* We use LEAVE_DESCRIPTORS_OPEN to work around a deadlock in older GLib,
   * and to avoid wasting a lot of time closing fds if the rlimit for
   * maximum open file descriptors is high. Because we're waiting for the
   * subprocess to finish anyway, it doesn't really matter that any fds
   * that are not close-on-execute will get leaked into the child. */
  if (!g_spawn_sync (NULL,  /* cwd */
                     (char **) argv,
                     (char **) envp,
                     (G_SPAWN_SEARCH_PATH |
                      G_SPAWN_LEAVE_DESCRIPTORS_OPEN),
                     NULL, NULL,
                     &output,
                     &errors,
                     &wait_status,
                     error))
    return FALSE;

  g_printerr ("%s", errors);

  if (exit_status_out != NULL)
    {
      if (WIFEXITED (wait_status))
        *exit_status_out = WEXITSTATUS (wait_status);
    }

  len = strlen (output);

  /* Emulate shell $() */
  if (len > 0 && output[len - 1] == '\n')
    output[len - 1] = '\0';

  g_debug ("-> %s", output);

  if (!g_spawn_check_wait_status (wait_status, error))
    return FALSE;

  if (output_out != NULL)
    *output_out = g_steal_pointer (&output);

  return TRUE;
}

/*
 * Returns: (transfer none): The first key in @table in @cmp order,
 *  or an arbitrary key if @cmp is %NULL, or %NULL if @table is empty.
 */
gpointer
pv_hash_table_get_first_key (GHashTable *table,
                             GCompareFunc cmp)
{
  GHashTableIter iter;
  gpointer key = NULL;

  if (cmp != NULL)
    {
      GList *keys = g_list_sort (g_hash_table_get_keys (table), cmp);

      if (keys != NULL)
        key = keys->data;
      else
        key = NULL;

      g_list_free (keys);
      return key;
    }

  g_hash_table_iter_init (&iter, table);
  if (g_hash_table_iter_next (&iter, &key, NULL))
    return key;
  else
    return NULL;
}

/**
 * pv_current_namespace_path_to_host_path:
 * @current_env_path: a path in the current environment
 *
 * Returns: (transfer full): The @current_env_path converted to the host
 *  system, or a copy of @current_env_path if we are not in a Flatpak
 *  environment or it's unknown how to convert the given path.
 */
gchar *
pv_current_namespace_path_to_host_path (const gchar *current_env_path)
{
  gchar *path_on_host = NULL;

  g_return_val_if_fail (g_path_is_absolute (current_env_path),
                        g_strdup (current_env_path));

  if (g_file_test ("/.flatpak-info", G_FILE_TEST_IS_REGULAR))
    {
      struct stat via_current_env_stat;
      struct stat via_persist_stat;
      const gchar *home = g_getenv ("HOME");
      const gchar *after = NULL;

      if (home == NULL)
        home = g_get_home_dir ();

      if (home != NULL)
        after = _srt_get_path_after (current_env_path, home);

      /* If we are inside a Flatpak container, usually, the home
       * folder is '${HOME}/.var/app/${FLATPAK_ID}' on the host system */
      if (after != NULL)
        {
          path_on_host = g_build_filename (home,
                                           ".var",
                                           "app",
                                           g_getenv ("FLATPAK_ID"),
                                           after,
                                           NULL);

          if (lstat (path_on_host, &via_persist_stat) < 0)
            {
              /* The file doesn't exist in ~/.var/app, so assume it was
               * exposed via --filesystem */
              g_clear_pointer (&path_on_host, g_free);
            }
          else if (lstat (current_env_path, &via_current_env_stat) == 0
                   && (via_persist_stat.st_dev != via_current_env_stat.st_dev
                       || via_persist_stat.st_ino != via_current_env_stat.st_ino))
            {
              /* The file exists in ~/.var/app, but is not the same there -
              * presumably a different version was mounted over the top via
              * --filesystem */
              g_clear_pointer (&path_on_host, g_free);
            }
        }

      after = _srt_get_path_after (current_env_path, "/run/host");

      /* In a Flatpak container, usually, '/run/host' is the root of the
       * host system */
      if (after != NULL && path_on_host == NULL)
        path_on_host = g_build_filename ("/", after, NULL);
    }
  /* Either we are not in a Flatpak container or it's not obvious how the
   * container to host translation should happen. Just keep the same path. */
  if (path_on_host == NULL)
    path_on_host = g_strdup (current_env_path);

  return path_on_host;
}

/**
 * pv_delete_dangling_symlink:
 * @dirfd: An open file descriptor for a directory
 * @debug_path: Path to directory represented by @dirfd, used in debug messages
 * @name: A filename in @dirfd that is thought to be a symbolic link
 *
 * If @name exists in @dirfd and is a symbolic link whose target does not
 * exist, delete it.
 */
void
pv_delete_dangling_symlink (int dirfd,
                            const char *debug_path,
                            const char *name)
{
  struct stat stat_buf, lstat_buf;

  g_return_if_fail (dirfd >= 0);
  g_return_if_fail (name != NULL);
  g_return_if_fail (strcmp (name, "") != 0);
  g_return_if_fail (strcmp (name, ".") != 0);
  g_return_if_fail (strcmp (name, "..") != 0);

  if (fstatat (dirfd, name, &lstat_buf, AT_SYMLINK_NOFOLLOW) == 0)
    {
      if (!S_ISLNK (lstat_buf.st_mode))
        {
          g_debug ("Ignoring %s/%s: not a symlink",
                   debug_path, name);
        }
      else if (fstatat (dirfd, name, &stat_buf, 0) == 0)
        {
          g_debug ("Ignoring %s/%s: symlink target still exists",
                   debug_path, name);
        }
      else if (errno != ENOENT)
        {
          int saved_errno = errno;

          g_debug ("Ignoring %s/%s: fstatat(!NOFOLLOW): %s",
                   debug_path, name, g_strerror (saved_errno));
        }
      else
        {
          g_debug ("Target of %s/%s no longer exists, deleting it",
                   debug_path, name);

          if (unlinkat (dirfd, name, 0) != 0)
            {
              int saved_errno = errno;

              g_debug ("Could not delete %s/%s: unlinkat: %s",
                       debug_path, name, g_strerror (saved_errno));
            }
        }
    }
  else if (errno == ENOENT)
    {
      /* Silently ignore: symlink doesn't exist so we don't need to
       * delete it */
    }
  else
    {
      int saved_errno = errno;

      g_debug ("Ignoring %s/%s: fstatat(NOFOLLOW): %s",
               debug_path, name, g_strerror (saved_errno));
    }
}

/*
 * Return the number of decimal digits in n.
 */
int
pv_count_decimal_digits (gsize n)
{
  gsize next_power_of_10 = 10;
  int required = 1;

  while (G_UNLIKELY (n >= next_power_of_10))
    {
      required += 1;

      if (G_UNLIKELY (next_power_of_10 > G_MAXSIZE / 10))
        return required;

      next_power_of_10 *= 10;
    }

  return required;
}

/*
 * pv_generate_unique_filepath:
 * @sub_dir: `share/vulkan/icd.d`, `share/glvnd/egl_vendor.d` or similar
 * @digits: Number of digits to pad length of numeric prefix if used
 * @seq: Sequence number of files, used to make unique filenames
 * @file: basename of the file
 * @multiarch_tuple: (nullable): Multiarch tuple of the @file, used
 *  to make unique filenames, or %NULL if architecture-independent
 * @files_set: (element-type filename ignored): A map
 *  `{ owned string => itself }` representing the set
 *  of files for which it has already been generated a unique path. Used
 *  internally to notice when to use unique sub directories.
 *
 * Generate a file path for @file, under @sub_dir, and store it in
 * @files_set. If "@sub_dir/@file" was already present in @files_set,
 * a unique subdirectory based on @seq and @multiarch_tuple will be used.
 *
 * Returns: (transfer full) (type filename): The unique relative path for @file
 *  in @sub_dir
 */
gchar *
pv_generate_unique_filepath (const gchar *sub_dir,
                             int digits,
                             gsize seq,
                             const gchar *file,
                             const gchar *multiarch_tuple,
                             GHashTable *files_set)
{
  g_autofree gchar *relative_to_overrides = NULL;

  relative_to_overrides = g_build_filename (sub_dir, file, NULL);

  /* If we already have a file with this name in this directory, we create
   * a unique sub directory to avoid conflicts. */
  if (g_hash_table_contains (files_set, relative_to_overrides))
    {
      g_autofree gchar *dedup_dir_basename = NULL;

      g_clear_pointer (&relative_to_overrides, g_free);

      if (multiarch_tuple != NULL)
        dedup_dir_basename = g_strdup_printf ("%.*" G_GSIZE_FORMAT "-%s", digits,
                                              seq, multiarch_tuple);
      else
        dedup_dir_basename = g_strdup_printf ("%.*" G_GSIZE_FORMAT, digits, seq);

      relative_to_overrides = g_build_filename (sub_dir, dedup_dir_basename,
                                                file, NULL);
    }

  g_hash_table_add (files_set, g_strdup (relative_to_overrides));

  return g_steal_pointer (&relative_to_overrides);
}

static gboolean
we_are_in_group (gid_t gid)
{
  /* Arbitrarily guess we might be in 16 groups */
  int size = 16;
  g_autofree gid_t *gids = NULL;
  int res;
  int i;

  if (gid == getegid ())
    return TRUE;

  while (1)
    {
      gids = g_realloc_n (gids, size, sizeof (gid_t));
      res = getgroups (size, gids);

      if (res >= 0 || errno != EINVAL)
        break;

      /* If gids was not large enough, try to find out how much space
       * is actually needed */
      size = getgroups (0, gids);

      if (size < 0)
        break;
    }

  for (i = 0; i < res; i++)
    {
      if (gid == gids[i])
        return TRUE;
    }

  return FALSE;
}

gchar *
pv_stat_describe_permissions (const struct stat *stat_buf)
{
  g_autoptr(GString) buf = g_string_new ("");

  g_string_append_printf (buf, "0%o", _srt_stat_get_permissions (stat_buf));

  if (stat_buf->st_uid != geteuid () || stat_buf->st_gid != getegid ())
    {
      /* Arbitrary size: if user/group information doesn't fit in this,
       * we fall back to using the numeric ID. */
      char temp[1024];
      struct passwd pwd = {};
      struct passwd *pwd_out;
      struct group grp = {};
      struct group *grp_out;

      g_string_append (buf, " (owner: ");

      if (stat_buf->st_uid == geteuid ())
        g_string_append (buf, "current user");
      else if (getpwuid_r (stat_buf->st_uid, &pwd, temp, sizeof (temp),
                           &pwd_out) == 0
               && pwd_out == &pwd)
        g_string_append_printf (buf, "\"%s\"", pwd.pw_name);
      else
        g_string_append_printf (buf, "ID %ld", (long) stat_buf->st_uid);

      g_string_append (buf, ", group: ");

      if (stat_buf->st_gid == getegid ())
        g_string_append (buf, "primary group");
      else if (getgrgid_r (stat_buf->st_gid, &grp, temp, sizeof (temp),
                           &grp_out) == 0
               && grp_out == &grp)
        g_string_append_printf (buf, "\"%s\"", grp.gr_name);
      else
        g_string_append_printf (buf, "ID %ld", (long) stat_buf->st_gid);

      if (!we_are_in_group (stat_buf->st_gid))
        g_string_append (buf, ", non-member");

      g_string_append (buf, ")");
    }

  return g_string_free (g_steal_pointer (&buf), FALSE);
}
