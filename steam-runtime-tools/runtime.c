/*
 * Copyright © 2019 Collabora Ltd.
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

#include "steam-runtime-tools/enums.h"
#include "steam-runtime-tools/runtime.h"
#include "steam-runtime-tools/runtime-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#include <errno.h>
#include <linux/magic.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <unistd.h>

#include <glib/gstdio.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils.h"

/* See statfs(2) for a list of known filesystems and their identifying
 * numbers as found in f_type */

#ifndef CEPH_SUPER_MAGIC
#define CEPH_SUPER_MAGIC 0x00c36400
#endif

#ifndef CIFS_MAGIC_NUMBER
#define CIFS_MAGIC_NUMBER 0xff534d42
#endif

#ifndef ECRYPTFS_SUPER_MAGIC
#define ECRYPTFS_SUPER_MAGIC 0xf15f
#endif

#ifndef EXFAT_SUPER_MAGIC
#define EXFAT_SUPER_MAGIC 0x2011BAB0
#endif

#ifndef EXT_SUPER_MAGIC
#define EXT_SUPER_MAGIC 0x137d
#endif

#ifndef EXT2_OLD_SUPER_MAGIC
#define EXT2_OLD_SUPER_MAGIC 0xef51
#endif

#ifndef F2FS_SUPER_MAGIC
#define F2FS_SUPER_MAGIC 0xf2f52010
#endif

#ifndef FUSE_SUPER_MAGIC
#define FUSE_SUPER_MAGIC 0x65735546
#endif

#ifndef HFS_SUPER_MAGIC
#define HFS_SUPER_MAGIC 0x4244
#endif

#ifndef HOSTFS_SUPER_MAGIC
#define HOSTFS_SUPER_MAGIC 0x00c0ffee
#endif

#ifndef JFS_SUPER_MAGIC
#define JFS_SUPER_MAGIC 0x3153464a
#endif

#ifndef NTFS_SB_MAGIC
#define NTFS_SB_MAGIC 0x5346544e
#endif

#ifndef OVERLAYFS_SUPER_MAGIC
#define OVERLAYFS_SUPER_MAGIC 0x794c7630
#endif

#ifndef SMB2_MAGIC_NUMBER
#define SMB2_MAGIC_NUMBER 0xfe534d42
#endif

#ifndef UDF_SUPER_MAGIC
#define UDF_SUPER_MAGIC 0x15013346
#endif

#ifndef V9FS_MAGIC
#define V9FS_MAGIC 0x01021997
#endif

#ifndef XFS_SUPER_MAGIC
#define XFS_SUPER_MAGIC 0x58465342
#endif

/**
* SECTION:runtime
* @title: LD_LIBRARY_PATH-based Steam Runtime
* @short_description: Information about the Steam Runtime
* @include: steam-runtime-tools/steam-runtime-tools.h
*
* #SrtRuntimeIssues represents problems encountered with the Steam
* Runtime.
*/

static void
should_be_executable (SrtRuntimeIssues *issues,
                      const char *path,
                      const char *filename)
{
  gchar *full = g_build_filename (path, filename, NULL);

  if (!g_file_test (full, G_FILE_TEST_IS_EXECUTABLE))
    {
      g_debug ("%s is not executable", full);
      *issues |= SRT_RUNTIME_ISSUES_NOT_RUNTIME;
    }

  g_free (full);
}

static void
should_be_dir (SrtRuntimeIssues *issues,
               const char *path,
               const char *filename)
{
  gchar *full = g_build_filename (path, filename, NULL);

  if (!g_file_test (full, G_FILE_TEST_IS_DIR))
    {
      g_debug ("%s is not a regular file", full);
      *issues |= SRT_RUNTIME_ISSUES_NOT_RUNTIME;
    }

  g_free (full);
}

static void
should_be_stattable (SrtRuntimeIssues *issues,
                     const char *path,
                     const char *filename,
                     GStatBuf *buf)
{
  gchar *full = g_build_filename (path, filename, NULL);

  if (g_stat (full, buf) != 0)
    {
      g_debug ("stat %s: %s", full, g_strerror (errno));
      *issues |= SRT_RUNTIME_ISSUES_NOT_RUNTIME;
    }

  g_free (full);
}

static void
might_be_stattable (const char *path,
                    const char *filename,
                    GStatBuf *buf)
{
  GStatBuf zeroed_stat = {};
  gchar *full = g_build_filename (path, filename, NULL);

  if (g_stat (full, buf) != 0)
    {
      g_debug ("stat %s: %s", full, g_strerror (errno));
      *buf = zeroed_stat;
    }

  g_free (full);
}

static gboolean
same_stat (GStatBuf *left,
           GStatBuf *right)
{
  return left->st_dev == right->st_dev && left->st_ino == right->st_ino;
}

static SrtRuntimeIssues
_srt_runtime_check_filesystem (const char *path)
{
  SrtRuntimeIssues issues = SRT_RUNTIME_ISSUES_NONE;
  int result;
  struct statfs filesystem = {};

  /* LSB tells us that statfs() is deprecated, but until glibc 2.39 it
   * was the only way to get the filesystem type. */
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  result = statfs (path, &filesystem);
  G_GNUC_END_IGNORE_DEPRECATIONS

  if (result < 0)
    {
      g_info ("Unable to determine filesystem of %s: %s",
              path, g_strerror (errno));
      return SRT_RUNTIME_ISSUES_ON_UNKNOWN_FILESYSTEM;
    }

  switch (filesystem.f_type)
    {
      case BTRFS_SUPER_MAGIC:
      case EXT_SUPER_MAGIC:
      case EXT2_OLD_SUPER_MAGIC:
      case EXT2_SUPER_MAGIC:
      /* EXT3_SUPER_MAGIC: same as EXT2 */
      /* EXT4_SUPER_MAGIC: same as EXT2 */
      case F2FS_SUPER_MAGIC:
      case JFS_SUPER_MAGIC:
      case REISERFS_SUPER_MAGIC:
      case TMPFS_MAGIC:
      case XFS_SUPER_MAGIC:
        g_debug ("%s is on a Unix filesystem, f_type=0x%08lx",
                 path, (unsigned long) filesystem.f_type);
        break;

      case EXFAT_SUPER_MAGIC:
      case MSDOS_SUPER_MAGIC:
      case HFS_SUPER_MAGIC:
      case NTFS_SB_MAGIC:
      case UDF_SUPER_MAGIC:
        issues |= SRT_RUNTIME_ISSUES_ON_NON_UNIX_FILESYSTEM;
        g_debug ("%s is on a non-Unix filesystem, f_type=0x%08lx",
                 path, (unsigned long) filesystem.f_type);
        break;

      case CIFS_MAGIC_NUMBER:
      case SMB_SUPER_MAGIC:
      case SMB2_MAGIC_NUMBER:
        issues |= SRT_RUNTIME_ISSUES_ON_NETWORK_FILESYSTEM;
        issues |= SRT_RUNTIME_ISSUES_ON_NON_UNIX_FILESYSTEM;
        g_debug ("%s is on a non-Unix network filesystem, f_type=0x%08lx",
                 path, (unsigned long) filesystem.f_type);
        break;

      case FUSE_SUPER_MAGIC:
        /* We don't know which specific FUSE filesystem. */
        issues |= SRT_RUNTIME_ISSUES_ON_UNKNOWN_FILESYSTEM;
        g_debug ("%s is on a FUSE filesystem", path);
        break;

      case ECRYPTFS_SUPER_MAGIC:
      case HOSTFS_SUPER_MAGIC:
      case OVERLAYFS_SUPER_MAGIC:
        /* We don't know what the backing filesystems are, and
         * overlayfs can itself cause issues. */
        issues |= SRT_RUNTIME_ISSUES_ON_UNKNOWN_FILESYSTEM;
        g_debug ("%s is on an overlay/stacking filesystem, f_type=0x%08lx",
                 path, (unsigned long) filesystem.f_type);
        break;

      case CEPH_SUPER_MAGIC:
      case NFS_SUPER_MAGIC:
      case V9FS_MAGIC:
        issues |= SRT_RUNTIME_ISSUES_ON_NETWORK_FILESYSTEM;
        g_debug ("%s is on a network filesystem, f_type=0x%08lx",
                 path, (unsigned long) filesystem.f_type);
        break;

      default:
        issues |= SRT_RUNTIME_ISSUES_ON_UNKNOWN_FILESYSTEM;
        g_debug ("%s is on an unknown filesystem, f_type=0x%08lx",
                 path, (unsigned long) filesystem.f_type);
        break;
    }

  return issues;
}

/*
 * _srt_runtime_check_ldlp:
 * @bin32: (nullable): The absolute path to `ubuntu12_32`
 * @expected_version: (nullable): The expected version number of the
 *  Steam Runtime
 * @envp: (not nullable): The list of environment variables to use
 * @version_out: (optional) (type utf8) (out): The actual version number
 * @path_out: (optional) (type filename) (out): The absolute path of the
 *  Steam Runtime
 *
 * Check that the current process is running in a LD_LIBRARY_PATH
 * Steam Runtime environment, returning any issue flags that indicate
 * problems with it.
 */
static SrtRuntimeIssues
_srt_runtime_check_ldlp (const char *bin32,
                         const char *expected_version,
                         const char * const *envp,
                         gchar **version_out,
                         gchar **path_out)
{
  SrtRuntimeIssues issues = SRT_RUNTIME_ISSUES_NONE;
  GStatBuf zeroed_stat = {};
  GStatBuf expected_stat, actual_stat;
  GStatBuf pinned_libs_32;
  GStatBuf pinned_libs_64;
  GStatBuf lib_i386_linux_gnu;
  GStatBuf lib_x86_64_linux_gnu;
  GStatBuf usr_lib_i386_linux_gnu;
  GStatBuf usr_lib_x86_64_linux_gnu;
  GStatBuf amd64_bin;
  GStatBuf i386_bin;
  const gchar *env = NULL;
  gchar *contents = NULL;
  gchar *expected_path = NULL;
  gchar *path = NULL;
  gchar *version = NULL;
  gchar *version_txt = NULL;
  gsize len = 0;
  GError *error = NULL;

  g_return_val_if_fail (version_out == NULL || *version_out == NULL,
                        SRT_RUNTIME_ISSUES_UNKNOWN);
  g_return_val_if_fail (path_out == NULL || *path_out == NULL,
                        SRT_RUNTIME_ISSUES_UNKNOWN);
  g_return_val_if_fail (_srt_check_not_setuid (), SRT_RUNTIME_ISSUES_UNKNOWN);

  env = _srt_environ_getenv (envp, "STEAM_RUNTIME");

  if (bin32 != NULL)
    expected_path = g_build_filename (bin32, "steam-runtime", NULL);

  if (g_strcmp0 (env, "0") == 0)
    {
      issues |= SRT_RUNTIME_ISSUES_DISABLED;
      goto out;
    }

  if (env == NULL || env[0] != '/')
    {
      issues |= SRT_RUNTIME_ISSUES_NOT_IN_ENVIRONMENT;
    }
  else if (g_stat (env, &actual_stat) != 0)
    {
      g_debug ("stat %s: %s", env, g_strerror (errno));
      issues |= SRT_RUNTIME_ISSUES_NOT_IN_ENVIRONMENT;
      actual_stat = zeroed_stat;
    }

  if (issues & SRT_RUNTIME_ISSUES_NOT_IN_ENVIRONMENT)
    {
      /* Try to recover by using the expected path */
      if (expected_path != NULL)
        {
          path = g_strdup (expected_path);

          if (g_stat (path, &actual_stat) != 0)
            actual_stat = zeroed_stat;
        }
    }
  else
    {
      g_assert (env != NULL);
      g_assert (env[0] == '/');
      path = g_strdup (env);
    }

  /* If we haven't found it yet, there is nothing else we can check */
  if (path == NULL)
    goto out;

  issues |= _srt_runtime_check_filesystem (path);

  if (expected_path != NULL && strcmp (path, expected_path) != 0)
    {
      if (g_stat (expected_path, &expected_stat) == 0)
        {
          if (!same_stat (&expected_stat, &actual_stat))
            {
              g_debug ("%s and %s are different inodes", path, expected_path);
              issues |= SRT_RUNTIME_ISSUES_UNEXPECTED_LOCATION;
            }
        }
      else
        {
          g_debug ("stat %s: %s", expected_path, g_strerror (errno));
          /* If the expected location doesn't exist then logically
           * the actual Steam Runtime in use can't be in the expected
           * location... */
          issues |= SRT_RUNTIME_ISSUES_UNEXPECTED_LOCATION;
        }
    }

  version_txt = g_build_filename (path, "version.txt", NULL);

  if (g_file_get_contents (version_txt, &contents, &len, &error))
    {
      const char *underscore = strrchr (contents, '_');

      /* Remove trailing \n if any */
      if (len > 0 && contents[len - 1] == '\n')
        contents[--len] = '\0';

      if (len != strlen (contents) ||
          strchr (contents, '\n') != NULL ||
          underscore == NULL)
        {
          g_debug ("Corrupt runtime: contents of %s should be in the "
                   "format NAME_VERSION",
                   version_txt);
          issues |= SRT_RUNTIME_ISSUES_NOT_RUNTIME;
        }
      else if (!g_str_has_prefix (contents, "steam-runtime_"))
        {
          g_debug ("Unofficial Steam Runtime build %s", contents);
          issues |= SRT_RUNTIME_ISSUES_UNOFFICIAL;
        }

      if (underscore != NULL)
        {
          version = g_strdup (underscore + 1);

          if (version[0] == '\0')
            {
              g_debug ("Corrupt runtime: contents of %s is missing the expected "
                       "runtime version number",
                       version_txt);
              issues |= SRT_RUNTIME_ISSUES_NOT_RUNTIME;
            }

          if (expected_version != NULL &&
              strcmp (expected_version, version) != 0)
            {
              g_debug ("Expected Steam Runtime v%s, got v%s",
                       expected_version, version);
              issues |= SRT_RUNTIME_ISSUES_UNEXPECTED_VERSION;
            }
        }

      g_clear_pointer (&contents, g_free);
    }
  else
    {
      issues |= SRT_RUNTIME_ISSUES_NOT_RUNTIME;
      g_debug ("Unable to read %s: %s", version_txt, error->message);
      g_clear_error (&error);
    }

  should_be_dir (&issues, path, "scripts");
  should_be_executable (&issues, path, "run.sh");
  should_be_executable (&issues, path, "setup.sh");
  should_be_stattable (&issues, path, "amd64/lib/x86_64-linux-gnu",
                       &lib_x86_64_linux_gnu);
  should_be_stattable (&issues, path, "amd64/usr/lib/x86_64-linux-gnu",
                       &usr_lib_x86_64_linux_gnu);
  should_be_stattable (&issues, path, "i386/lib/i386-linux-gnu",
                       &lib_i386_linux_gnu);
  should_be_stattable (&issues, path, "i386/usr/lib/i386-linux-gnu",
                       &usr_lib_i386_linux_gnu);
  might_be_stattable (path, "pinned_libs_32", &pinned_libs_32);
  might_be_stattable (path, "pinned_libs_64", &pinned_libs_64);
  might_be_stattable (path, "amd64/usr/bin", &amd64_bin);
  might_be_stattable (path, "i386/usr/bin", &i386_bin);

  env = _srt_environ_getenv (envp, "STEAM_RUNTIME_PREFER_HOST_LIBRARIES");

  if (g_strcmp0 (env, "0") == 0)
    issues |= SRT_RUNTIME_ISSUES_NOT_USING_NEWER_HOST_LIBRARIES;

  env = _srt_environ_getenv (envp, "LD_LIBRARY_PATH");

  if (env == NULL)
    {
      issues |= SRT_RUNTIME_ISSUES_NOT_IN_LD_PATH;
    }
  else
    {
      gchar **entries = g_strsplit (env, ":", 0);
      gchar **entry;
      gboolean saw_lib_i386_linux_gnu = FALSE;
      gboolean saw_lib_x86_64_linux_gnu = FALSE;
      gboolean saw_usr_lib_i386_linux_gnu = FALSE;
      gboolean saw_usr_lib_x86_64_linux_gnu = FALSE;
      gboolean saw_pinned_libs_32 = FALSE;
      gboolean saw_pinned_libs_64 = FALSE;

      for (entry = entries; entry != NULL && *entry != NULL; entry++)
        {
          /* Scripts that manipulate LD_LIBRARY_PATH have a habit of
           * adding empty entries */
          if (*entry[0] == '\0')
            continue;

          /* We compare by stat(), because the entries in the
           * LD_LIBRARY_PATH might not have been canonicalized by
           * chasing symlinks, replacing "/.." or "//", etc. */
          if (g_stat (*entry, &actual_stat) == 0)
            {
              if (same_stat (&actual_stat, &lib_i386_linux_gnu))
                saw_lib_i386_linux_gnu = TRUE;

              /* Don't use "else if": it would be legitimate for
               * usr/lib/i386-linux-gnu and lib/i386-linux-gnu
               * to be symlinks to the same place, in which case
               * seeing one counts as seeing both. */
              if (same_stat (&actual_stat, &usr_lib_i386_linux_gnu))
                saw_usr_lib_i386_linux_gnu = TRUE;

              if (same_stat (&actual_stat, &lib_x86_64_linux_gnu))
                saw_lib_x86_64_linux_gnu = TRUE;

              if (same_stat (&actual_stat, &usr_lib_x86_64_linux_gnu))
                saw_usr_lib_x86_64_linux_gnu = TRUE;

              /* The pinned libraries only count if they are before the
               * corresponding Steam Runtime directories */
              if (!saw_lib_i386_linux_gnu &&
                  !saw_usr_lib_i386_linux_gnu &&
                  pinned_libs_32.st_mode != 0 &&
                  same_stat (&actual_stat, &pinned_libs_32))
                saw_pinned_libs_32 = TRUE;

              if (!saw_lib_x86_64_linux_gnu &&
                  !saw_usr_lib_x86_64_linux_gnu &&
                  pinned_libs_64.st_mode != 0 &&
                  same_stat (&actual_stat, &pinned_libs_64))
                saw_pinned_libs_64 = TRUE;
            }
          else
            {
              g_debug ("stat LD_LIBRARY_PATH entry %s: %s",
                       *entry, g_strerror (errno));
            }
        }

      if (!saw_lib_x86_64_linux_gnu || !saw_usr_lib_x86_64_linux_gnu)
        {
          g_debug ("STEAM_RUNTIME/amd64/[usr/]lib/x86_64-linux-gnu missing "
                   "from LD_LIBRARY_PATH");
          issues |= SRT_RUNTIME_ISSUES_NOT_IN_LD_PATH;
        }

      if (!saw_lib_i386_linux_gnu || !saw_usr_lib_i386_linux_gnu)
        {
          g_debug ("STEAM_RUNTIME/i386/[usr/]lib/i386-linux-gnu missing "
                   "from LD_LIBRARY_PATH");
          issues |= SRT_RUNTIME_ISSUES_NOT_IN_LD_PATH;
        }

      if (!saw_pinned_libs_64 || !saw_pinned_libs_32)
        {
          g_debug ("Pinned libraries missing from LD_LIBRARY_PATH");
          issues |= SRT_RUNTIME_ISSUES_NOT_USING_NEWER_HOST_LIBRARIES;
        }

      g_strfreev (entries);
    }

  env = _srt_environ_getenv (envp, "PATH");

  if (env == NULL)
    {
      issues |= SRT_RUNTIME_ISSUES_NOT_IN_PATH;
    }
  else
    {
      gchar **entries = g_strsplit (env, ":", 0);
      gchar **entry;
      gboolean saw_amd64_bin = FALSE;
      gboolean saw_i386_bin = FALSE;

      for (entry = entries; entry != NULL && *entry != NULL; entry++)
        {
          /* Scripts that manipulate PATH have a habit of adding empty
           * entries */
          if (*entry[0] == '\0')
            continue;

          /* We compare by stat(), because the entries in the
           * PATH might not have been canonicalized by
           * chasing symlinks, replacing "/.." or "//", etc. */
          if (g_stat (*entry, &actual_stat) == 0)
            {
              if (amd64_bin.st_mode != 0 &&
                  same_stat (&actual_stat, &amd64_bin))
                saw_amd64_bin = TRUE;

              if (i386_bin.st_mode != 0 &&
                  same_stat (&actual_stat, &i386_bin))
                saw_i386_bin = TRUE;

            }
          else
            {
              g_debug ("stat PATH entry %s: %s",
                       *entry, g_strerror (errno));
            }
        }

      if (!saw_amd64_bin && !saw_i386_bin)
        {
          g_debug ("Neither STEAM_RUNTIME/amd64/usr/bin nor STEAM_RUNTIME/i386/usr/bin "
                   "are available in PATH");
          issues |= SRT_RUNTIME_ISSUES_NOT_IN_PATH;
        }

      g_strfreev (entries);
    }

out:
  if (path_out != NULL)
    *path_out = g_steal_pointer (&path);

  if (version_out != NULL)
    *version_out = g_steal_pointer (&version);

  g_free (contents);
  g_free (expected_path);
  g_free (path);
  g_free (version);
  g_free (version_txt);
  g_clear_error (&error);
  return issues;
}

/*
 * _srt_runtime_check_container:
 * @self: information about the runtime
 * @os_info: information about the container OS
 *
 * Return information about a Steam Runtime container (pressure-vessel,
 * Docker or any other container) if we are running inside one.
 *
 * Returns: %TRUE if we are running in a container Steam Runtime
 *  environment
 */
static gboolean
_srt_runtime_check_container (SrtRuntime *self,
                              SrtOsInfo *os_info)
{
  if (g_strcmp0 (srt_os_info_get_id (os_info), "steamrt") != 0)
    return FALSE;

  _srt_runtime_clear_outputs (self);
  self->path = g_strdup ("/");
  self->version = g_strdup (srt_os_info_get_build_id (os_info));
  /* We don't use "/" here because in practice, that will often be a tmpfs.
   * As currently implemented in pressure-vessel, /usr comes from the
   * Steam library directory, so we can use that as our oracle. */
  self->issues |= _srt_runtime_check_filesystem ("/usr");

  if (self->expected_version != NULL
      && g_strcmp0 (self->expected_version, self->version) != 0)
    self->issues |= SRT_RUNTIME_ISSUES_UNEXPECTED_VERSION;

  if (self->version == NULL)
    {
      self->issues |= SRT_RUNTIME_ISSUES_NOT_RUNTIME;
    }
  else
    {
      const char *p;

      for (p = self->version; *p != '\0'; p++)
        {
          if (!g_ascii_isdigit (*p) && *p != '.')
            self->issues |= SRT_RUNTIME_ISSUES_UNOFFICIAL;
        }
    }

  return TRUE;
}

/*
 * _srt_runtime_check_execution_environment:
 * @self: Information about the runtime. Fields other than @expected_version
 *  will be overwritten.
 * @env: A copy of `environ`, or a mock environment
 * @os_info: Information about the running OS release
 * @bin32: Path to `~/.steam/root/ubuntu12_32`
 *
 * Check that the current process is running in a LD_LIBRARY_PATH
 * or container Steam Runtime environment, setting fields in @self
 * as appropriate.
 */
void
_srt_runtime_check_execution_environment (SrtRuntime *self,
                                          const char * const *env,
                                          SrtOsInfo *os_info,
                                          const char *bin32)
{
  const char *runtime;

  g_return_if_fail (self != NULL);
  g_return_if_fail (env != NULL);
  g_return_if_fail (os_info != NULL);

  _srt_runtime_clear_outputs (self);
  runtime = _srt_environ_getenv (env, "STEAM_RUNTIME");

  /* If we are currently running in a LD_LIBRARY_PATH runtime, check that
   * it is as expected */
  if (runtime != NULL && runtime[0] == '/' && runtime[1] != '\0')
    {
      self->issues = _srt_runtime_check_ldlp (bin32, self->expected_version, env,
                                              &self->version, &self->path);
      return;
    }

  /* Or, if we are currently running in a container runtime (for example
   * pressure-vessel Platform or Docker SDK), check that it is as expected */
  if (_srt_runtime_check_container (self, os_info))
    return;

  /* If we are not currently running in a container runtime, check that
   * the default LD_LIBRARY_PATH runtime in
   * ~/.steam/root/ubuntu12_32/steam-runtime is as expected */
  self->issues = _srt_runtime_check_ldlp (bin32, self->expected_version, env,
                                          &self->version, &self->path);
}

static char *
remove_runtime_from_path (const char *steam_runtime,
                          const char *path)
{
  g_autoptr(GString) buf = g_string_new ("");
  g_auto(GStrv) bits = g_strsplit (path, ":", -1);
  char **p;

  for (p = bits; *p != NULL; p++)
    {
      if (_srt_get_path_after (*p, steam_runtime) == NULL)
        {
          if (buf->len > 0)
            g_string_append_c (buf, ':');

          g_string_append (buf, *p);
        }
    }

  return g_string_free (g_steal_pointer (&buf), FALSE);
}

/*
 * _srt_environ_escape_steam_runtime:
 * @env: (array zero-terminated=1) (element-type filename) (transfer full):
 *  The original environment
 * @flags: Flags to modify the escape behavior
 *
 * Returns: (array zero-terminated=1) (element-type filename) (transfer full):
 *  The new environment
 */
GStrv
_srt_environ_escape_steam_runtime (GStrv env,
                                   SrtEscapeRuntimeFlags flags)
{
  const char *path;
  const char *steam_runtime = g_environ_getenv (env, "STEAM_RUNTIME");
  const char *system_ldlp;
  const char *system_path;
  const char *zenity;

  if (steam_runtime == NULL || steam_runtime[0] != '/')
    return env;

  system_ldlp = g_environ_getenv (env, "SYSTEM_LD_LIBRARY_PATH");

  /* Restore the system LD_LIBRARY_PATH, or unset it */
  if (system_ldlp != NULL)
    env = g_environ_setenv (env, "LD_LIBRARY_PATH", system_ldlp, TRUE);
  else
    env = g_environ_unsetenv (env, "LD_LIBRARY_PATH");

  path = g_environ_getenv (env, "PATH");
  system_path = g_environ_getenv (env, "SYSTEM_PATH");

  /* Restore the system PATH if we can, or edit out whatever items in it
   * start with the Steam Runtime directory. */
  if (system_path != NULL)
    {
      g_autofree char *cleaned_path = NULL;
      if (flags & SRT_ESCAPE_RUNTIME_FLAGS_CLEAN_PATH)
        cleaned_path = remove_runtime_from_path (steam_runtime, system_path);

      env = g_environ_setenv (env, "PATH", cleaned_path ?: system_path, TRUE);
    }
  else if (path != NULL)
    {
      g_autofree char *cleaned_path = remove_runtime_from_path (steam_runtime,
                                                                path);
      env = g_environ_setenv (env, "PATH", cleaned_path, TRUE);
    }

  zenity = g_environ_getenv (env, "STEAM_ZENITY");

  if (zenity != NULL &&
      (g_strcmp0 (zenity, "zenity") == 0
       || _srt_get_path_after (zenity, steam_runtime) != NULL))
    env = g_environ_unsetenv (env, "STEAM_ZENITY");

  env = g_environ_unsetenv (env, "STEAM_RUNTIME");
  return env;
}
