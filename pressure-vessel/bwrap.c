/*
 * Copyright © 2017-2019 Collabora Ltd.
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

#include <ftw.h>

#include <gio/gio.h>

#include "bwrap.h"
#include "flatpak-utils-private.h"
#include "runtime.h"
#include "utils.h"

#include "steam-runtime-tools/resolve-in-sysroot-internal.h"
#include "steam-runtime-tools/log-internal.h"
#include "steam-runtime-tools/utils-internal.h"

/**
 * pv_bwrap_run_sync:
 * @bwrap: A #FlatpakBwrap on which flatpak_bwrap_finish() has been called
 * @exit_status_out: (out) (optional): Used to return the exit status,
 *  or -1 if it could not be launched or was killed by a signal
 * @error: Used to raise an error on failure
 *
 * Try to run a command. It inherits pressure-vessel's own file
 * descriptors.
 *
 * Returns: %TRUE if the subprocess runs to completion
 */
gboolean
pv_bwrap_run_sync (FlatpakBwrap *bwrap,
                   int *exit_status_out,
                   GError **error)
{
  g_return_val_if_fail (bwrap != NULL, FALSE);
  g_return_val_if_fail (bwrap->argv->len >= 2, FALSE);
  g_return_val_if_fail (pv_bwrap_was_finished (bwrap), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return pv_run_sync ((const char * const *) bwrap->argv->pdata,
                      (const char * const *) bwrap->envp,
                      exit_status_out, NULL, error);
}

/**
 * pv_bwrap_execve:
 * @bwrap: A #FlatpakBwrap on which flatpak_bwrap_finish() has been called
 * @inherit_fds (array length=n_inherit_fds): Allow these fds to be
 *  inherited across execve(), but without seeking to the beginning
 * @n_inherit_fds: Number of entries in @inherit_fds
 * @error: Used to raise an error on failure
 *
 * Attempt to replace the current process with the given bwrap command.
 * If unable to do so, raise an error.
 *
 * Returns: %FALSE
 */
gboolean
pv_bwrap_execve (FlatpakBwrap *bwrap,
                 int *inherit_fds,
                 size_t n_inherit_fds,
                 GError **error)
{
  int saved_errno;
  size_t i;

  g_return_val_if_fail (bwrap != NULL, FALSE);
  g_return_val_if_fail (bwrap->argv->len >= 2, FALSE);
  g_return_val_if_fail (pv_bwrap_was_finished (bwrap), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_debug ("Replacing self with %s...",
           glnx_basename (g_ptr_array_index (bwrap->argv, 0)));

  if (bwrap->fds != NULL && bwrap->fds->len > 0)
    flatpak_bwrap_child_setup_cb (bwrap->fds);

  for (i = 0; i < n_inherit_fds; i++)
    {
      int fd = inherit_fds[i];

      if (_srt_fd_unset_close_on_exec (fd) < 0)
        g_warning ("Unable to clear close-on-exec flag of fd %d: %s",
                   fd, g_strerror (errno));
    }

  fflush (stdout);
  fflush (stderr);

  execve (bwrap->argv->pdata[0],
          (char * const *) bwrap->argv->pdata,
          bwrap->envp);
  saved_errno = errno;

  /* If we are still here then execve failed */
  g_set_error (error,
               G_IO_ERROR,
               g_io_error_from_errno (saved_errno),
               "Error replacing self with bwrap: %s",
               g_strerror (saved_errno));
  return FALSE;
}

/**
 * pv_bwrap_bind_usr:
 * @bwrap: The #FlatpakBwrap
 * @provider_in_host_namespace: The directory we will
 *  use to provide the container's `/usr`, `/lib*` etc.,
 *  in the form of an absolute path that can be resolved
 *  on the host system
 * @provider_fd: The same directory, this time
 *  in the form of a file descriptor
 *  in the namespace where pressure-vessel-wrap is running.
 *  For example, if we run in a Flatpak container and we intend
 *  to mount the host system's `/usr`, then
 *  @provider_in_host_namespace would be `/`
 *  but @provider_fd would be the result of opening
 *  `/run/host`.
 * @provider_in_container_namespace: Absolute path of the location
 *  at which we will mount @provider_in_host_namespace in the
 *  final container
 * @error: Used to raise an error on failure
 *
 * Append arguments to @bwrap that will bind-mount `/usr` and associated
 * directories from @host_path into @mount_point.
 *
 * If @host_path contains a `usr` directory, it is assumed to be a
 * system root. Its `usr` directory is mounted on `${mount_point}/usr`
 * in the container. Its `lib*`, `bin` and `sbin` directories are
 * created as symbolic links in @mount_point, or mounted on subdirectories
 * of @mount_point, as appropriate.
 *
 * If @host_path does not contain a `usr` directory, it is assumed to be
 * a merged `/usr`. It is mounted on `${mount_point}/usr`, and `lib*`,
 * `bin` and `sbin` symbolic links are created in @mount_point.
 *
 * To make this useful, the caller will probably also have to bind-mount
 * `etc`, or at least `etc/alternatives` and `etc/ld.so.cache`. However,
 * these are not handled here.
 *
 * Returns: %TRUE on success
 */
gboolean
pv_bwrap_bind_usr (FlatpakBwrap *bwrap,
                   const char *provider_in_host_namespace,
                   int provider_fd,
                   const char *provider_in_container_namespace,
                   GError **error)
{
  g_autofree gchar *usr = NULL;
  glnx_autofd int usr_fd = -1;
  g_autofree gchar *dest = NULL;
  gboolean host_path_is_usr = FALSE;
  g_auto(SrtDirIter) iter = SRT_DIR_ITER_CLEARED;
  const gchar *member = NULL;

  g_return_val_if_fail (bwrap != NULL, FALSE);
  g_return_val_if_fail (!pv_bwrap_was_finished (bwrap), FALSE);
  g_return_val_if_fail (provider_in_host_namespace != NULL, FALSE);
  g_return_val_if_fail (provider_in_host_namespace[0] == '/', FALSE);
  g_return_val_if_fail (provider_fd >= 0, FALSE);
  g_return_val_if_fail (provider_in_container_namespace != NULL, FALSE);
  g_return_val_if_fail (provider_in_container_namespace[0] == '/', FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  usr = g_build_filename (provider_in_host_namespace, "usr", NULL);
  usr_fd = _srt_resolve_in_sysroot (provider_fd, "usr",
                                    SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY,
                                    NULL, NULL);
  dest = g_build_filename (provider_in_container_namespace, "usr", NULL);

  if (usr_fd >= 0)
    {
      flatpak_bwrap_add_args (bwrap,
                              "--ro-bind", usr, dest,
                              NULL);
    }
  else
    {
      /* /usr is not a directory; host_path is assumed to be a merged /usr */
      host_path_is_usr = TRUE;
      flatpak_bwrap_add_args (bwrap,
                              "--ro-bind", provider_in_host_namespace, dest,
                              NULL);
    }

  g_clear_pointer (&dest, g_free);

  if (!_srt_dir_iter_init_at (&iter, provider_fd, ".",
                              SRT_DIR_ITER_FLAGS_FOLLOW,
                              _srt_dirent_strcmp,
                              error))
    return FALSE;

  while (TRUE)
    {
      struct dirent *dent;

      if (!_srt_dir_iter_next_dent (&iter, &dent, NULL, error))
        return FALSE;

      if (dent == NULL)
        break;

      member = dent->d_name;

      if ((g_str_has_prefix (member, "lib")
           && !g_str_equal (member, "libexec"))
          || g_str_equal (member, "bin")
          || g_str_equal (member, "sbin")
          || g_str_equal (member, ".ref"))
        {
          dest = g_build_filename (provider_in_container_namespace, member, NULL);

          if (host_path_is_usr)
            {
              g_autofree gchar *target = g_build_filename ("usr",
                                                           member, NULL);

              flatpak_bwrap_add_args (bwrap,
                                      "--symlink", target, dest,
                                      NULL);
            }
          else
            {
              g_autofree gchar *target = glnx_readlinkat_malloc (provider_fd,
                                                                 member,
                                                                 NULL, NULL);

              if (target != NULL)
                {
                  flatpak_bwrap_add_args (bwrap,
                                          "--symlink", target, dest,
                                          NULL);
                }
              else
                {
                  g_autofree gchar *path_in_host = g_build_filename (provider_in_host_namespace,
                                                                     member, NULL);


                  flatpak_bwrap_add_args (bwrap,
                                          "--ro-bind", path_in_host, dest,
                                          NULL);
                }
            }

          g_clear_pointer (&dest, g_free);
        }
    }

  return TRUE;
}

/* nftw() doesn't have a user_data argument so we need to use a global
 * variable :-( */
static struct
{
  FlatpakBwrap *bwrap;
  const char *source;
  const char *dest;
} nftw_data;

static int
copy_tree_helper (const char *fpath,
                  const struct stat *sb,
                  int typeflag,
                  struct FTW *ftwbuf)
{
  const char *path_in_container;
  g_autofree gchar *target = NULL;
  gsize prefix_len;
  int fd = -1;
  GError *error = NULL;

  g_return_val_if_fail (g_str_has_prefix (fpath, nftw_data.source), 1);
  g_return_val_if_fail (g_str_has_suffix (nftw_data.source, nftw_data.dest), 1);

  prefix_len = strlen (nftw_data.source) - strlen (nftw_data.dest);
  path_in_container = fpath + prefix_len;

  switch (typeflag)
    {
      case FTW_D:
        flatpak_bwrap_add_args (nftw_data.bwrap,
                                "--dir", path_in_container,
                                NULL);
        break;

      case FTW_SL:
        target = glnx_readlinkat_malloc (-1, fpath, NULL, NULL);
        flatpak_bwrap_add_args (nftw_data.bwrap,
                                "--symlink", target, path_in_container,
                                NULL);
        break;

      case FTW_F:
        if (!glnx_openat_rdonly (AT_FDCWD, fpath, FALSE, &fd, &error))
          {
            g_warning ("Unable to copy file into container: %s",
                       error->message);
            g_clear_error (&error);
          }

        flatpak_bwrap_add_args_data_fd (nftw_data.bwrap,
                                        "--ro-bind-data", g_steal_fd (&fd),
                                        path_in_container);
        break;

      default:
        g_warning ("Don't know how to handle ftw type flag %d at %s",
                   typeflag, fpath);
    }

  return 0;
}

/**
 * pv_bwrap_copy_tree:
 * @bwrap: The #FlatpakBwrap
 * @source: A copy of the desired @dest in a temporary directory,
 *  for example `/tmp/tmp12345678/overrides/lib`. The path must end
 *  with @dest.
 * @dest: The destination path in the container, which must be absolute.
 *
 * For every file, directory or symbolic link in @source, add a
 * corresponding read-only file, directory or symbolic link via the bwrap
 * command-line, so that the files, directories and symbolic links in the
 * container will persist even after @source has been deleted.
 */
void
pv_bwrap_copy_tree (FlatpakBwrap *bwrap,
                    const char *source,
                    const char *dest)
{
  g_return_if_fail (nftw_data.bwrap == NULL);
  g_return_if_fail (dest[0] == '/');
  g_return_if_fail (g_str_has_suffix (source, dest));

  nftw_data.bwrap = bwrap;
  nftw_data.source = source;
  nftw_data.dest = dest;
  nftw (source, copy_tree_helper, 100, FTW_PHYS);
  nftw_data.bwrap = NULL;
  nftw_data.source = NULL;
  nftw_data.dest = NULL;
}

/**
 * pv_bwrap_add_api_filesystems:
 * @bwrap: The #FlatpakBwrap
 * @sysfs_mode: Mode for /sys
 *
 * Make basic API filesystems available.
 */
void
pv_bwrap_add_api_filesystems (FlatpakBwrap *bwrap,
                              FlatpakFilesystemMode sysfs_mode)
{
  g_autofree char *link = NULL;

  g_return_if_fail (sysfs_mode >= FLATPAK_FILESYSTEM_MODE_READ_ONLY);

  flatpak_bwrap_add_args (bwrap,
                          "--dev-bind", "/dev", "/dev",
                          "--proc", "/proc",
                          NULL);

  if (sysfs_mode >= FLATPAK_FILESYSTEM_MODE_READ_WRITE)
    flatpak_bwrap_add_args (bwrap,
                            "--bind", "/sys", "/sys",
                            NULL);
  else
    flatpak_bwrap_add_args (bwrap,
                            "--ro-bind", "/sys", "/sys",
                            NULL);

  link = glnx_readlinkat_malloc (AT_FDCWD, "/dev/shm", NULL, NULL);

  if (g_strcmp0 (link, "/run/shm") == 0)
    {
      if (g_file_test ("/proc/self/root/run/shm", G_FILE_TEST_IS_DIR))
        flatpak_bwrap_add_args (bwrap,
                                "--bind", "/run/shm", "/run/shm",
                                NULL);
      else
        flatpak_bwrap_add_args (bwrap,
                                "--dir", "/run/shm",
                                NULL);
    }
  else if (link != NULL)
    {
      g_warning ("Unexpected /dev/shm symlink %s", link);
    }
}

FlatpakBwrap *
pv_bwrap_copy (FlatpakBwrap *bwrap)
{
  FlatpakBwrap *ret;

  g_return_val_if_fail (bwrap != NULL, NULL);
  g_return_val_if_fail (!pv_bwrap_was_finished (bwrap), NULL);
  /* bwrap can't own any fds, because if it did,
   * flatpak_bwrap_append_bwrap() would steal them. */
  g_return_val_if_fail (bwrap->fds == NULL || bwrap->fds->len == 0, NULL);

  ret = flatpak_bwrap_new (flatpak_bwrap_empty_env);
  flatpak_bwrap_append_bwrap (ret, bwrap);
  return ret;
}

/*
 * Return @bwrap's @envp, while resetting @bwrap's @envp to an empty
 * environment block.
 */
GStrv
pv_bwrap_steal_envp (FlatpakBwrap *bwrap)
{
  GStrv envp = g_steal_pointer (&bwrap->envp);

  bwrap->envp = g_strdupv (flatpak_bwrap_empty_env);
  return envp;
}

/*
 * pv_bwrap_append_adjusted_exports:
 * @to: The exported files and directories of @from will be adjusted and
  appended to this FlatpakBwrap
 * @from: Arguments produced by flatpak_exports_append_bwrap_args(),
 *  not including an executable name (the 0'th argument must be
 *  `--bind` or similar)
 * @home: The home directory
 * @interpreter_root (nullable): Path to the interpreter root, or %NULL if
 *  there isn't one
 * @error: Used to return an error on failure
 *
 * Adjust arguments in @from to cope with potentially running in a
 * container or interpreter and append them to @to.
 * This function will steal the fds of @from.
 */
gboolean
pv_bwrap_append_adjusted_exports (FlatpakBwrap *to,
                                  FlatpakBwrap *from,
                                  const char *home,
                                  SrtSysroot *interpreter_root,
                                  PvBwrapFlags bwrap_flags,
                                  GError **error)
{
  g_autofree int *fds = NULL;
  /* Bypass FEX-Emu transparent rewrite by using
   * "/proc/self/root" as the root path. */
  g_autoptr(SrtSysroot) root = NULL;
  gsize n_fds;
  gsize i;

  g_return_val_if_fail (to != NULL, FALSE);
  g_return_val_if_fail (from != NULL, FALSE);
  g_return_val_if_fail (home != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  fds = flatpak_bwrap_steal_fds (from, &n_fds);
  for (i = 0; i < n_fds; i++)
    flatpak_bwrap_add_fd (to, fds[i]);

  if (interpreter_root != NULL)
    {
      root = _srt_sysroot_new_real_root (error);

      if (root == NULL)
        return FALSE;

      /* Both of these are using fd-relative I/O, not naive path-based I/O */
      g_assert (!_srt_sysroot_is_direct (interpreter_root));
      g_assert (!_srt_sysroot_is_direct (root));
    }

  g_debug ("Exported directories:");

  for (i = 0; i < from->argv->len;)
    {
      const char *opt = from->argv->pdata[i];

      g_assert (opt != NULL);

      if (g_str_equal (opt, "--bind-data") ||
          g_str_equal (opt, "--chmod") ||
          g_str_equal (opt, "--ro-bind-data") ||
          g_str_equal (opt, "--file") ||
          g_str_equal (opt, "--symlink"))
        {
          g_assert (i + 3 <= from->argv->len);
          /* pdata[i + 1] is the target, fd or permissions: unchanged. */
          /* pdata[i + 2] is a path in the final container: unchanged. */
          g_debug ("%s %s %s",
                   opt,
                   (const char *) from->argv->pdata[i + 1],
                   (const char *) from->argv->pdata[i + 2]);

          flatpak_bwrap_add_args (to, opt, from->argv->pdata[i + 1],
                                  from->argv->pdata[i + 2], NULL);

          i += 3;
        }
      else if (g_str_equal (opt, "--dev") ||
               g_str_equal (opt, "--dir") ||
               g_str_equal (opt, "--mqueue") ||
               g_str_equal (opt, "--proc") ||
               g_str_equal (opt, "--remount-ro") ||
               g_str_equal (opt, "--tmpfs"))
        {
          g_assert (i + 2 <= from->argv->len);
          /* pdata[i + 1] is a path in the final container, or a non-path:
           * unchanged. */
          g_debug ("%s %s",
                   opt,
                   (const char *) from->argv->pdata[i + 1]);

          flatpak_bwrap_add_args (to, opt, from->argv->pdata[i + 1], NULL);

          i += 2;
        }
      else if (g_str_equal (opt, "--bind") ||
               g_str_equal (opt, "--bind-try") ||
               g_str_equal (opt, "--dev-bind") ||
               g_str_equal (opt, "--dev-bind-try") ||
               g_str_equal (opt, "--ro-bind") ||
               g_str_equal (opt, "--ro-bind-try"))
        {
          g_assert (i + 3 <= from->argv->len);
          const char *from_src = from->argv->pdata[i + 1];
          const char *from_dest = from->argv->pdata[i + 2];
          gboolean skip_real_root = FALSE;

          /* If we're using FEX-Emu or similar, Flatpak code might think it
           * has found a particular file either because it's in the rootfs,
           * or because it's in the real root filesystem.
           * If it exists in FEX rootfs, we add an additional mount entry
           * where the source is from the FEX rootfs and the destination is
           * prefixed with the pressure-vessel interpreter root location.
           *
           * An exception to this is that if the destination path is one
           * that we don't want to mount into the interpreter root (usually
           * /run/host) then we mount it into the real root, and avoid
           * mounting a version from the real root (if any) at the same
           * location. */
          if (interpreter_root != NULL
              && _srt_sysroot_test (interpreter_root, from_src,
                                    SRT_RESOLVE_FLAGS_NONE, NULL))
            {
              g_autofree gchar *inter_src = g_build_filename (interpreter_root->path,
                                                              from_src, NULL);
              g_autofree gchar *inter_dest = NULL;

              if (pv_runtime_path_belongs_in_interpreter_root (NULL, from_dest))
                {
                  inter_dest = g_build_filename (PV_RUNTIME_PATH_INTERPRETER_ROOT,
                                                 from_dest, NULL);
                }
              else
                {
                  inter_dest = g_strdup (from_dest);
                  skip_real_root = TRUE;
                }

              g_debug ("Adjusted \"%s\" to \"%s\" and \"%s\" to \"%s\" for the interpreter root",
                       from_src, inter_src, from_dest, inter_dest);
              g_debug ("%s %s %s", opt, inter_src, inter_dest);
              flatpak_bwrap_add_args (to, opt, inter_src, inter_dest, NULL);
            }

          if ((interpreter_root == NULL
               || _srt_sysroot_test (root, from_src,
                                     SRT_RESOLVE_FLAGS_NONE, NULL))
              && !skip_real_root)
            {
              g_autofree gchar *src = NULL;
              /* Paths in the home directory might need adjusting.
               * Paths outside the home directory do not: if they're part of
               * /run/host, they've been adjusted already by
               * flatpak_exports_take_host_fd(), and if not, they appear in
               * the container with the same path as on the host. */
              if (flatpak_has_path_prefix (from_src, home))
                {
                  src = pv_current_namespace_path_to_host_path (from_src);

                  if (!g_str_equal (from_src, src))
                    g_debug ("Adjusted \"%s\" to \"%s\" to be reachable from host",
                             from_src, src);
                }
              else
                {
                  src = g_strdup (from_src);
                }

              g_debug ("%s %s %s", opt, src, from_dest);
              flatpak_bwrap_add_args (to, opt, src, from_dest, NULL);
            }

          i += 3;
        }
      else if (g_str_equal (opt, "--perms"))
        {
          g_assert (i + 2 <= from->argv->len);
          const char *perms = from->argv->pdata[i + 1];
          /* pdata[i + 1] is a non-path: unchanged. */
          g_debug ("%s %s",
                   opt,
                   (const char *) from->argv->pdata[i + 1]);

          /* A system copy of bubblewrap older than 0.5.0
           * (Debian 11 or older) won't support --perms. Fall back to
           * creating mount-points with the default permissions if
           * necessary. */
          if (bwrap_flags & PV_BWRAP_FLAGS_HAS_PERMS)
            flatpak_bwrap_add_args (to, opt, perms, NULL);
          else
            g_debug ("Ignoring \"--perms %s\" because bwrap is too old",
                     perms);

          i += 2;
        }
      else
        {
          g_return_val_if_reached (FALSE);
        }
    }

  return TRUE;
}

/* List of variables that are stripped down from the environment when
 * using the secure-execution mode.
 * List taken from glibc sysdeps/generic/unsecvars.h */
static const char* unsecure_environment_variables[] = {
  "GCONV_PATH",
  "GETCONF_DIR",
  "GLIBC_TUNABLES",
  "HOSTALIASES",
  "LD_AUDIT",
  "LD_DEBUG",
  "LD_DEBUG_OUTPUT",
  "LD_DYNAMIC_WEAK",
  "LD_HWCAP_MASK",
  "LD_LIBRARY_PATH",
  "LD_ORIGIN_PATH",
  "LD_PRELOAD",
  "LD_PROFILE",
  "LD_SHOW_AUXV",
  "LD_USE_LOAD_BIAS",
  "LOCALDOMAIN",
  "LOCPATH",
  "MALLOC_TRACE",
  "NIS_PATH",
  "NLSPATH",
  "RESOLV_HOST_CONF",
  "RES_OPTIONS",
  "TMPDIR",
  "TZDIR",
  NULL,
};

/*
 * Populate @flatpak_subsandbox with environment variables from @container_env.
 * They'll be passed via pv-launch `--env`/`--unset-env`.
 */
void
pv_bwrap_container_env_to_subsandbox_argv (FlatpakBwrap *flatpak_subsandbox,
                                           PvEnviron *container_env)
{
  g_autoptr(GList) vars = NULL;
  const GList *iter;

  g_return_if_fail (flatpak_subsandbox != NULL);
  g_return_if_fail (container_env != NULL);

  vars = pv_environ_get_vars (container_env);

  for (iter = vars; iter != NULL; iter = iter->next)
    {
      const char *var = iter->data;
      const char *val = pv_environ_getenv (container_env, var);

      if (val != NULL)
        flatpak_bwrap_add_arg_printf (flatpak_subsandbox,
                                      "--env=%s=%s",
                                      var, val);
      else
        flatpak_bwrap_add_args (flatpak_subsandbox,
                                "--unset-env", var,
                                NULL);
    }
}

/*
 * Populate @bwrap with environment variables from @container_env.
 * They'll be passed via bubblewrap `--setenv`/`--unsetenv`.
 */
void
pv_bwrap_container_env_to_bwrap_argv (FlatpakBwrap *bwrap,
                                      PvEnviron *container_env)
{
  g_autoptr(GList) vars = NULL;
  const GList *iter;

  g_return_if_fail (bwrap != NULL);
  g_return_if_fail (container_env != NULL);

  vars = pv_environ_get_vars (container_env);

  for (iter = vars; iter != NULL; iter = iter->next)
    {
      const char *var = iter->data;
      const char *val = pv_environ_getenv (container_env, var);

      if (val != NULL)
        flatpak_bwrap_add_args (bwrap,
                                "--setenv", var, val,
                                NULL);
      else
        flatpak_bwrap_add_args (bwrap,
                                "--unsetenv", var,
                                NULL);
    }
}

/*
 * Populate @bwrap with environment variables from @container_env.
 */
void
pv_bwrap_container_env_to_envp (FlatpakBwrap *bwrap,
                                PvEnviron *container_env)
{
  g_autoptr(GList) vars = NULL;
  const GList *iter;

  g_return_if_fail (bwrap != NULL);
  g_return_if_fail (container_env != NULL);

  vars = pv_environ_get_vars (container_env);

  for (iter = vars; iter != NULL; iter = iter->next)
    {
      const char *var = iter->data;
      const char *val = pv_environ_getenv (container_env, var);

      if (val != NULL)
        flatpak_bwrap_set_env (bwrap, var, val, TRUE);
      else
        flatpak_bwrap_unset_env (bwrap, var);
    }
}

/*
 * For each variable in @container_env that would be filtered out by
 * a setuid bubblewrap, add it to @bwrap via `--setenv`.
 */
void
pv_bwrap_filtered_container_env_to_bwrap_argv (FlatpakBwrap *bwrap,
                                               PvEnviron *container_env)
{
  gsize i;

  g_return_if_fail (bwrap != NULL);
  g_return_if_fail (container_env != NULL);

  for (i = 0; unsecure_environment_variables[i] != NULL; i++)
    {
      const char *var = unsecure_environment_variables[i];
      const char *val = pv_environ_getenv (container_env, var);

      if (val != NULL)
        flatpak_bwrap_add_args (bwrap, "--setenv", var, val, NULL);
    }
}
