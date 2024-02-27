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

#include "steam-runtime-tools/utils.h"
#include "steam-runtime-tools/utils-internal.h"

#include <dlfcn.h>
#include <errno.h>
#include <ftw.h>
#include <link.h>
#include <net/if.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef HAVE_SYS_AUXV_H
#include <sys/auxv.h>
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/resolve-in-sysroot-internal.h"

#ifdef HAVE_GETAUXVAL
#define getauxval_AT_SECURE() getauxval (AT_SECURE)
#else
/*
 * This implementation assumes that auxv entries are pointer-sized on
 * all architectures.
 *
 * Note that this implementation doesn't special-case AT_HWCAP and
 * AT_HWCAP like glibc does, so it is only suitable for other types
 * (but in practice we only need AT_SECURE here).
 */
static long
getauxval_AT_SECURE (void)
{
  uintptr_t buf[2] = { 0 /* type */, 0 /* value */ };
  FILE *auxv;
  gboolean found = FALSE;

  if ((auxv = fopen("/proc/self/auxv", "r")) == NULL)
    return 0;

  while ((fread (buf, sizeof (buf), 1, auxv)) == 1)
    {
      if (buf[0] == AT_SECURE)
        {
          found = TRUE;
          break;
        }
      else
        {
          buf[0] = buf[1] = 0;
        }
    }

  fclose(auxv);

  if (!found)
    errno = ENOENT;

  return (long) buf[1];
}
#endif

/* Return TRUE if setuid, setgid, setcap or otherwise running with
 * elevated privileges. "setuid" in the name is shorthand for this. */
static gboolean
check_for_setuid_once (void)
{
  errno = 0;

  /* If the kernel says we are running with elevated privileges,
   * believe it */
  if (getauxval_AT_SECURE ())
    return TRUE;

  /* If the kernel specifically told us we are not running with
   * elevated privileges, believe it (as opposed to the kernel not
   * having told us either way, which sets errno to ENOENT) */
  if (errno == 0)
    return FALSE;

  /* Otherwise resort to comparing (e)uid and (e)gid */
  if (geteuid () != getuid ())
    return TRUE;

  if (getegid () != getgid ())
    return TRUE;

  return FALSE;
}

static int is_setuid = -1;

/*
 * _srt_check_not_setuid:
 *
 * Check that the process containing this library is not setuid, setgid,
 * setcap or otherwise running with elevated privileges. The word
 * "setuid" in the function name is not completely accurate, but is used
 * as a shorthand term since it is the most common way for a process
 * to be more privileged than its parent.
 *
 * This library trusts environment variables and other aspects of the
 * execution environment, and is not designed to be used with elevated
 * privileges, so this should normally be done as a precondition check:
 *
 * |[<!-- language="C" -->
 * g_return_if_fail (_srt_check_not_setuid ());
 * // or in functions that return a value
 * g_return_val_if_fail (_srt_check_not_setuid (), SOME_ERROR_CONSTANT);
 * ]|
 *
 * Returns: %TRUE under normal circumstances
 */
G_GNUC_INTERNAL gboolean
_srt_check_not_setuid (void)
{
  if (is_setuid >= 0)
    return !is_setuid;

  is_setuid = check_for_setuid_once ();
  return !is_setuid;
}

#ifndef _SRT_MULTIARCH
#define MULTIARCH_LIBDIR "/lib/"
#else
#define MULTIARCH_LIBDIR \
  "/lib/" _SRT_MULTIARCH
#endif

#define RELOCATABLE_PKGLIBDIR \
  MULTIARCH_LIBDIR "/steam-runtime-tools-" _SRT_API_MAJOR
#define PKGLIBEXECDIR \
  "/libexec/steam-runtime-tools-" _SRT_API_MAJOR
#define INSTALLED_TESTS_PKGLIBEXECDIR \
  "/libexec/installed-tests/steam-runtime-tools-" _SRT_API_MAJOR

G_GNUC_INTERNAL const char *
_srt_find_myself (const char **exe_path_out,
                  const char **helpers_path_out,
                  GError **error)
{
  static gchar *saved_prefix = NULL;
  static gchar *saved_exe_path = NULL;
  static gchar *saved_helpers_path = NULL;
  Dl_info ignored;
  struct link_map *map = NULL;
  gchar *dir = NULL;
  g_autofree gchar *exe = NULL;

  g_return_val_if_fail (_srt_check_not_setuid (), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);
  g_return_val_if_fail (exe_path_out == NULL || *exe_path_out == NULL,
                        NULL);
  g_return_val_if_fail (helpers_path_out == NULL || *helpers_path_out == NULL,
                        NULL);

  if (saved_prefix != NULL
      && saved_exe_path != NULL
      && saved_helpers_path != NULL)
    goto out;

  if (dladdr1 (_srt_find_myself, &ignored, (void **) &map,
               RTLD_DL_LINKMAP) == 0 ||
      map == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Unable to locate shared library containing "
                   "_srt_find_myself()");
      goto out;
    }

  exe = realpath ("/proc/self/exe", NULL);

  if (exe == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Unable to locate main executable");
      goto out;
    }

  if (map->l_name == NULL || map->l_name[0] == '\0')
    {
      g_debug ("Found _srt_find_myself() in main executable %s", exe);
      dir = g_path_get_dirname (exe);
    }
  else
    {
      g_debug ("Found _srt_find_myself() in %s", map->l_name);
      dir = g_path_get_dirname (map->l_name);
    }

  if (g_str_has_suffix (dir, RELOCATABLE_PKGLIBDIR))
    dir[strlen (dir) - strlen (RELOCATABLE_PKGLIBDIR)] = '\0';
  else if (g_str_has_suffix (dir, MULTIARCH_LIBDIR))
    dir[strlen (dir) - strlen (MULTIARCH_LIBDIR)] = '\0';
  else if (g_str_has_suffix (dir, PKGLIBEXECDIR))
    dir[strlen (dir) - strlen (PKGLIBEXECDIR)] = '\0';
  else if (g_str_has_suffix (dir, INSTALLED_TESTS_PKGLIBEXECDIR))
    dir[strlen (dir) - strlen (INSTALLED_TESTS_PKGLIBEXECDIR)] = '\0';
  else if (g_str_has_suffix (dir, "/libexec"))
    dir[strlen (dir) - strlen ("/libexec")] = '\0';
  else if (g_str_has_suffix (dir, "/lib64"))
    dir[strlen (dir) - strlen ("/lib64")] = '\0';
  else if (g_str_has_suffix (dir, "/lib"))
    dir[strlen (dir) - strlen ("/lib")] = '\0';
  else if (g_str_has_suffix (dir, "/bin"))
    dir[strlen (dir) - strlen ("/bin")] = '\0';

  /* If the library was found in /lib/MULTIARCH, /lib64 or /lib on a
   * merged-/usr system, assume --prefix=/usr (/libexec doesn't
   * normally exist) */
  if (dir[0] == '\0')
    {
      g_free (dir);
      dir = g_strdup ("/usr");
    }

  saved_exe_path = g_steal_pointer (&exe);
  saved_prefix = g_steal_pointer (&dir);
  /* deliberate one-per-process leak */
  saved_helpers_path = g_build_filename (
      saved_prefix, "libexec", "steam-runtime-tools-" _SRT_API_MAJOR,
      NULL);

out:
  if (exe_path_out != NULL)
    *exe_path_out = saved_exe_path;

  if (helpers_path_out != NULL)
    *helpers_path_out = saved_helpers_path;

  g_free (dir);
  return saved_prefix;
}

/**
 * _srt_filter_gameoverlayrenderer:
 * @input: The environment variable value that needs to be filtered.
 *  Usually retrieved with g_environ_getenv() or _srt_environ_getenv()
 *
 * Filter the @input paths list from every path containing `gameoverlayrenderer.so`
 *
 * Returns: A newly-allocated string containing all the paths from @input
 *  except for the ones with `gameoverlayrenderer.so`.
 *  Free with g_free ().
 */
gchar *
_srt_filter_gameoverlayrenderer (const gchar *input)
{
  gchar **entries;
  gchar **entry;
  gchar *ret = NULL;
  GPtrArray *filtered;

  g_return_val_if_fail (input != NULL, NULL);

  entries = g_strsplit (input, ":", 0);
  filtered = g_ptr_array_new ();

  for (entry = entries; entry != NULL && *entry != NULL; entry++)
    {
      if (!g_str_has_suffix (*entry, "/gameoverlayrenderer.so"))
        g_ptr_array_add (filtered, *entry);
    }

  g_ptr_array_add (filtered, NULL);
  ret = g_strjoinv (":", (gchar **) filtered->pdata);

  g_ptr_array_free (filtered, TRUE);
  g_strfreev (entries);

  return ret;
}

/**
 * _srt_filter_gameoverlayrenderer_from_envp:
 * @envp: (array zero-terminated=1) (not nullable):
 *
 * Filter every path containing `gameoverlayrenderer.so` from the environment
 * variable `LD_PRELOAD` in the provided @envp.
 *
 * Returns (array zero-terminated=1) (transfer full): A newly-allocated array of
 *  strings containing all the values from @envp, but with
 *  `gameoverlayrenderer.so` filtered out. Free with g_strfreev().
 */
gchar **
_srt_filter_gameoverlayrenderer_from_envp (const char * const *envp)
{
  GStrv filtered_environ = NULL;
  const gchar *ld_preload;
  g_autofree gchar *filtered_preload = NULL;

  g_return_val_if_fail (envp != NULL, NULL);

  filtered_environ = _srt_strdupv (envp);
  ld_preload = g_environ_getenv (filtered_environ, "LD_PRELOAD");
  if (ld_preload != NULL)
    {
      filtered_preload = _srt_filter_gameoverlayrenderer (ld_preload);
      filtered_environ = g_environ_setenv (filtered_environ, "LD_PRELOAD",
                                           filtered_preload, TRUE);
    }

  return filtered_environ;
}

/**
 * srt_enum_value_to_nick
 * @enum_type: The type of the enumeration.
 * @value: The enumeration value to stringify.
 *
 * Get the #GEnumValue.value-nick of a given enumeration value.
 * For example, `srt_enum_value_to_nick (SRT_TYPE_WINDOW_SYSTEM, SRT_WINDOW_SYSTEM_EGL_X11)`
 * returns `"egl-x11"`.
 *
 * Returns: (transfer none): A string representation
 *  of the given enumeration value.
 */
const char *
srt_enum_value_to_nick (GType enum_type,
                        int value)
{
  GEnumClass *class;
  GEnumValue *enum_value;
  const char *result;

  g_return_val_if_fail (G_TYPE_IS_ENUM (enum_type), NULL);

  class = g_type_class_ref (enum_type);
  enum_value = g_enum_get_value (class, value);

  if (enum_value != NULL)
    result = enum_value->value_nick;
  else
    result = NULL;

  g_type_class_unref (class);
  return result;
}

/**
 * srt_enum_from_nick:
 * @enum_type: The type of the enumeration
 * @nick: The nickname to look up
 * @value_out: (not nullable): Used to return the enumeration that has been
 *  found from the provided @nick
 * @error: Used to raise an error on failure
 *
 * Get the enumeration from a given #GEnumValue.value-nick.
 * For example:
 * `srt_enum_from_nick (SRT_TYPE_GRAPHICS_LIBRARY_VENDOR, "glvnd", (gint *) &library_vendor, NULL)`
 * will set `library_vendor` to SRT_GRAPHICS_LIBRARY_VENDOR_GLVND.
 *
 * Returns: %TRUE if no errors have been found.
 */
gboolean
srt_enum_from_nick (GType enum_type,
                    const gchar *nick,
                    gint *value_out,
                    GError **error)
{
  GEnumClass *class;
  GEnumValue *enum_value;
  gboolean result = TRUE;

  g_return_val_if_fail (G_TYPE_IS_ENUM (enum_type), FALSE);
  g_return_val_if_fail (nick != NULL, FALSE);
  g_return_val_if_fail (value_out != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  class = g_type_class_ref (enum_type);

  enum_value = g_enum_get_value_by_nick (class, nick);
  if (enum_value)
    {
      *value_out = enum_value->value;
    }
  else
    {
      if (error != NULL)
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "\"%s\" is not a known member of %s",
                     nick, g_type_name (enum_type));
      result = FALSE;
    }

  g_type_class_unref (class);
  return result;
}

/**
 * srt_add_flag_from_nick:
 * @flags_type: The type of the flag
 * @nick: The nickname to look up
 * @value_out: (not nullable) (inout): The flag, from the provided @nick,
 *  will be added to @value_out
 * @error: Used to raise an error on failure
 *
 * Get the flag from a given #GEnumValue.value-nick.
 * For example:
 * `srt_add_flag_from_nick (SRT_TYPE_STEAM_ISSUES, "cannot-find", &issues, error)`
 * will add SRT_STEAM_ISSUES_CANNOT_FIND to `issues`.
 *
 * Returns: %TRUE if no errors have been found.
 */
gboolean
srt_add_flag_from_nick (GType flags_type,
                        const gchar *nick,
                        guint *value_out,
                        GError **error)
{
  GFlagsClass *class;
  GFlagsValue *flags_value;
  gboolean result = TRUE;

  g_return_val_if_fail (G_TYPE_IS_FLAGS (flags_type), FALSE);
  g_return_val_if_fail (nick != NULL, FALSE);
  g_return_val_if_fail (value_out != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  class = g_type_class_ref (flags_type);

  flags_value = g_flags_get_value_by_nick (class, nick);
  if (flags_value)
    {
      *value_out |= flags_value->value;
    }
  else
    {
      if (error != NULL)
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "\"%s\" is not a known member of %s",
                     nick, g_type_name (flags_type));
      result = FALSE;
    }

  g_type_class_unref (class);
  return result;
}

static void _srt_constructor (void) __attribute__((__constructor__));
static void
_srt_constructor (void)
{
#if !GLIB_CHECK_VERSION(2, 36, 0)
  g_type_init ();
#endif
  g_return_if_fail (_srt_check_not_setuid ());
}

/*
 * _srt_child_setup_unblock_signals:
 * @ignored: Ignored, for compatibility with #GSpawnChildSetupFunc
 *
 * A child-setup function that unblocks all signals, and resets all signals
 * to their default dispositions.
 *
 * In particular, this can be used to work around versions of `timeout(1)`
 * that do not do configure `SIGCHLD` to make sure they receive it
 * (GNU coreutils >= 8.27, < 8.29 as seen in Ubuntu 18.04).
 *
 * This function is async-signal-safe.
 */
void
_srt_child_setup_unblock_signals (gpointer ignored)
{
  struct sigaction action = { .sa_handler = SIG_DFL };
  sigset_t new_set;
  int sig;

  /* We ignore errors and don't even g_debug(), to avoid being
   * async-signal-unsafe */
  sigemptyset (&new_set);
  (void) pthread_sigmask (SIG_SETMASK, &new_set, NULL);

  for (sig = 1; sig < NSIG; sig++)
    {
      if (sig != SIGKILL && sig != SIGSTOP)
        (void) sigaction (sig, &action, NULL);
    }
}

/*
 * _srt_unblock_signals:
 *
 * Unblock all signals, and reset all signals to their default dispositions.
 *
 * This function is not async-signal-safe.
 *
 * This function manipulates process-global state, and should be called
 * from `main()`, after logging has been initialized, but before creating
 * a second thread, intentionally blocking or ignoring any signals, or
 * setting any non-default signal handlers, either directly or via GLib.
 * In particular, it cannot safely be called after creating a subprocess,
 * child watch or GDBus connection.
 */
void
_srt_unblock_signals (void)
{
  struct sigaction old_action = { .sa_handler = SIG_DFL };
  struct sigaction new_action = { .sa_handler = SIG_DFL };
  sigset_t old_set;
  sigset_t new_set;
  int sig;
  int saved_errno;

  sigemptyset (&old_set);
  sigfillset (&new_set);

  /* This returns an errno code instead of setting errno */
  saved_errno = pthread_sigmask (SIG_UNBLOCK, &new_set, &old_set);

  if (saved_errno != 0)
    {
      g_warning ("Unable to unblock signals: %s", g_strerror (saved_errno));
    }
  else
    {
      for (sig = 1; sig < NSIG; sig++)
        {
          /* sigismember returns -1 for non-signals, which we ignore */
          if (sigismember (&new_set, sig) == 1 &&
              sigismember (&old_set, sig) == 1)
            g_debug ("Unblocked signal %d (%s)", sig, g_strsignal (sig));
        }
    }

  for (sig = 1; sig < NSIG; sig++)
    {
      if (sig == SIGKILL || sig == SIGSTOP)
        continue;

#if defined(__SANITIZE_ADDRESS__) || _srt_compiler_has_feature(address_sanitizer)
      /* AddressSanitizer sets handlers for these signals during startup.
       * Leave those in place, and don't show a warning for them. */
      if ((sig == SIGBUS || sig == SIGFPE || sig == SIGSEGV)
          && sigaction (sig, NULL, &old_action) == 0
          && old_action.sa_handler != SIG_IGN)
        continue;
#endif

      if (sigaction (sig, &new_action, &old_action) != 0)
        {
          saved_errno = errno;

          /* EINVAL is returned for the signals reserved by glibc */
          if (saved_errno != EINVAL)
            g_warning ("Unable to reset handler for signal %d (%s): %s",
                       sig, g_strsignal (sig), g_strerror (saved_errno));
        }
      else if (old_action.sa_handler == SIG_IGN)
        {
          g_debug ("Reset signal %d (%s) from SIG_IGN to SIG_DFL",
                   sig, g_strsignal (sig));
        }
      else if (old_action.sa_handler != SIG_DFL)
        {
          /* This should not happen, because _srt_unblock_signals() should
           * only be called early enough in process startup that there are
           * no non-default signal handlers yet */
          g_warning ("Reset signal %d (%s) from handler %p to SIG_DFL",
                     sig, g_strsignal (sig), old_action.sa_handler);
        }
    }
}

/*
 * _srt_indirect_strcmp0:
 * @left: A non-%NULL pointer to a (possibly %NULL) `const char *`
 * @right: A non-%NULL pointer to a (possibly %NULL) `const char *`
 *
 * A #GCompareFunc to sort pointers to strings in lexicographic
 * (g_strcmp0()) order.
 *
 * Returns: An integer < 0 if left < right, > 0 if left > right,
 *  or 0 if left == right or if they are not comparable
 */
int
_srt_indirect_strcmp0 (gconstpointer left,
                       gconstpointer right)
{
  const gchar * const *l = left;
  const gchar * const *r = right;

  g_return_val_if_fail (l != NULL, 0);
  g_return_val_if_fail (r != NULL, 0);
  return g_strcmp0 (*l, *r);
}

static gint
ftw_remove (const gchar *path,
            const struct stat *sb,
            gint typeflags,
            struct FTW *ftwbuf)
{
  if (remove (path) < 0)
    {
      g_debug ("Unable to remove %s: %s", path, g_strerror (errno));
      return -1;
    }

  return 0;
}

/**
 * _srt_rm_rf:
 * @directory: (type filename): The directory to remove.
 *
 * Recursively delete @directory within the same file system and
 * without following symbolic links.
 *
 * Returns: %TRUE if the removal was successful
 */
gboolean
_srt_rm_rf (const char *directory)
{
  g_return_val_if_fail (directory != NULL, FALSE);

  if (nftw (directory, ftw_remove, 10, FTW_DEPTH|FTW_MOUNT|FTW_PHYS) < 0)
    return FALSE;

  return TRUE;
}

/*
 * _srt_divert_stdout_to_stderr:
 * @error: Used to raise an error on failure
 *
 * Duplicate file descriptors so that functions that would write to
 * `stdout` instead write to a copy of the original `stderr`. Return
 * a file handle that can be used to print structured output to the
 * original `stdout`.
 *
 * Returns: (transfer full): A libc file handle for the original `stdout`,
 *  or %NULL on error. Free with `fclose()`.
 */
FILE *
_srt_divert_stdout_to_stderr (GError **error)
{
  g_autoptr(FILE) original_stdout = NULL;
  glnx_autofd int original_stdout_fd = -1;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* Duplicate the original stdout so that we still have a way to write
   * machine-readable output. */
  original_stdout_fd = dup (STDOUT_FILENO);

  if (original_stdout_fd < 0)
    return glnx_null_throw_errno_prefix (error,
                                         "Unable to duplicate fd %d",
                                         STDOUT_FILENO);

  if (_srt_fd_set_close_on_exec (original_stdout_fd) < 0)
    return glnx_null_throw_errno_prefix (error,
                                         "Unable to set flags for new fd");

  /* If something like g_debug writes to stdout, make it come out of
   * our original stderr. */
  if (dup2 (STDERR_FILENO, STDOUT_FILENO) != STDOUT_FILENO)
    return glnx_null_throw_errno_prefix (error,
                                         "Unable to make fd %d a copy of fd %d",
                                         STDOUT_FILENO, STDERR_FILENO);

  original_stdout = fdopen (original_stdout_fd, "w");

  if (original_stdout == NULL)
    return glnx_null_throw_errno_prefix (error,
                                         "Unable to create a stdio wrapper for fd %d",
                                         original_stdout_fd);
  else
    original_stdout_fd = -1;    /* ownership taken, do not close */

  return g_steal_pointer (&original_stdout);
}

/*
 * Return a pointer to the environment block, without copying.
 * In the unlikely event that `environ == NULL`, return a pointer to
 * an empty #GStrv.
 */
const char * const *
_srt_peek_environ_nonnull (void)
{
  static const char * const no_strings[] = { NULL };

  g_return_val_if_fail (_srt_check_not_setuid (), no_strings);

  if (environ != NULL)
    return (const char * const *) environ;
  else
    return no_strings;
}

/*
 * Globally disable GIO modules.
 *
 * This function modifies the environment, therefore:
 *
 * - it must be called from main() before starting any threads
 * - you must save a copy of the original environment first if you intend
 *   for subprocesses to receive the original, unmodified environment
 *
 * To be effective, it must also be called before any use of GIO APIs.
 */
void
_srt_setenv_disable_gio_modules (void)
{
  g_setenv ("GIO_USE_VFS", "local", TRUE);
  g_setenv ("GIO_MODULE_DIR", "/nonexistent", TRUE);
}

/*
 * Return TRUE if @str points to one or more decimal digits, followed
 * by nul termination. This is the same as Python bytes.isdigit().
 */
gboolean
_srt_str_is_integer (const char *str)
{
  const char *p;

  g_return_val_if_fail (str != NULL, FALSE);

  if (*str == '\0')
    return FALSE;

  for (p = str;
       *p != '\0';
       p++)
    {
      if (!g_ascii_isdigit (*p))
        break;
    }

  return (*p == '\0');
}

/*
 * _srt_fstatat_is_same_file:
 * @afd: a file descriptor, `AT_FDCWD` or -1
 * @a: a path
 * @bfd: a file descriptor, `AT_FDCWD` or -1
 * @b: a path
 *
 * Returns: %TRUE if a (relative to afd) and b (relative to bfd)
 *  are names for the same inode.
 */
gboolean
_srt_fstatat_is_same_file (int afd,
                           const char *a,
                           int bfd,
                           const char *b)
{
  struct stat a_buffer, b_buffer;

  g_return_val_if_fail (a != NULL, FALSE);
  g_return_val_if_fail (b != NULL, FALSE);

  afd = glnx_dirfd_canonicalize (afd);
  bfd = glnx_dirfd_canonicalize (bfd);

  if (afd == bfd && strcmp (a, b) == 0)
    return TRUE;

  return (fstatat (afd, a, &a_buffer, AT_EMPTY_PATH) == 0
          && fstatat (bfd, b, &b_buffer, AT_EMPTY_PATH) == 0
          && _srt_is_same_stat (&a_buffer, &b_buffer));
}

/*
 * GHashFunc for struct stat.
 */
guint
_srt_struct_stat_devino_hash (gconstpointer p)
{
  const struct stat *s = p;

  return (guint) (s->st_dev ^ s->st_ino);
}

/*
 * GEqualFunc for struct stat, comparing for equality by device number
 * and inode number.
 */
gboolean
_srt_struct_stat_devino_equal (gconstpointer p1,
                               gconstpointer p2)
{
  return _srt_is_same_stat (p1, p2);
}

/*
 * _srt_steam_command_via_pipe:
 * @arguments: (not nullable) (element-type utf8) (transfer none): An array
 *  of command-line arguments that need to be passed to the Steam pipe
 * @n_arguments: Number of elements of @arguments to use, or negative if
 *  @arguments is %NULL-terminated
 * @error: Used to raise an error on failure
 *
 * If @n_arguments is positive it's the caller's responsibility to ensure that
 * @arguments has at least @n_arguments elements.
 * If @n_arguments is negative, @arguments must be %NULL-terminated.
 *
 * Returns: %TRUE if the provided @arguments were successfully passed to the
 *  running Steam client instance
 */
gboolean
_srt_steam_command_via_pipe (const char * const *arguments,
                             gssize n_arguments,
                             GError **error)
{
  glnx_autofd int fd = -1;
  int ofd_flags;
  g_autofree gchar *steampipe = NULL;
  g_autoptr(GString) args_string = g_string_new ("");
  gsize length;
  gsize i;

  g_return_val_if_fail (arguments != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (n_arguments >= 0)
    length = n_arguments;
  else
    length = g_strv_length ((gchar **) arguments);

  steampipe = g_build_filename (g_get_home_dir (), ".steam", "steam.pipe", NULL);

  fd = open (steampipe, O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);

  if (fd < 0 && (errno == ENOENT || errno == ENXIO))
    return glnx_throw_errno_prefix (error, "Steam is not running");
  else if (fd < 0)
    return glnx_throw_errno_prefix (error, "An error occurred trying to open the Steam pipe");

  ofd_flags = fcntl (fd, F_GETFL, 0);

  /* Remove O_NONBLOCK to block if we write more than the pipe-buffer space */
  if (fcntl (fd, F_SETFL, ofd_flags & ~O_NONBLOCK) != 0)
    return glnx_throw_errno_prefix (error, "Unable to set flags on the steam pipe fd");

  /* We hardcode the canonical steam installation path, instead of actually
   * searching where Steam has been installed, because apparently this
   * information is not used for anything in particular and Steam just
   * discards it */
  g_string_append (args_string, "'~/.steam/root/ubuntu12_32/steam'");

  for (i = 0; i < length; i++)
    {
      g_autofree gchar *quoted = NULL;

      quoted = g_shell_quote (arguments[i]);

      g_string_append (args_string, " ");
      g_string_append (args_string, quoted);
    }

  g_string_append (args_string, "\n");

  if (glnx_loop_write (fd, args_string->str, args_string->len) < 0)
    return glnx_throw_errno_prefix (error,
                                    "An error occurred trying to write to the Steam pipe");

  return TRUE;
}

typedef struct
{
  const char *from;
  const char *to;
} CommonReplacements;

/*
 * _srt_list_directory_content:
 * @working_dir_fd: File descriptor to the current working directory
 * @working_dir_path: (not nullable) (type filename): Working directory in
 *  the sysroot
 * @sub_directory: (nullable) (type filename): If %NULL, the @working_dir_path
 *  itself will be opened
 * @common_replacements: (nullable): If not %NULL, perform these replacements
 *  to the beginning of the targets paths for symlinks that have been found
 * @level: Current level of recursion
 * @result: (not nullable): The elements that @directory contains are appended
 *  to this array
 * @messages: (not nullable): Human-readable debug information are appended
 *  to this array
 */
static void
_srt_list_directory_content (int working_dir_fd,
                             const gchar *working_dir_path,
                             const gchar *sub_directory,
                             const CommonReplacements *common_replacements,
                             int level,
                             GPtrArray *result,
                             GPtrArray *messages)
{
  g_autofree gchar *full_working_path = NULL;
  g_auto(GLnxDirFdIterator) iter = { FALSE };
  g_autoptr(GError) error = NULL;
  gsize i;

  g_return_if_fail (working_dir_path != NULL);
  g_return_if_fail (result != NULL);
  g_return_if_fail (messages != NULL);

  if (sub_directory != NULL)
    full_working_path = g_build_filename (working_dir_path, sub_directory, NULL);
  else
    full_working_path = g_strdup (working_dir_path);

  /* Arbitrary limit. If we reach this level of recursion it's a sign that
   * something went wrong and it's better to bail out. */
  if (level > 9)
    {
      g_ptr_array_add (messages, g_strdup_printf ("%s/... (too much recursion, not shown)",
                                                  full_working_path));
      return;
    }

  if (!glnx_dirfd_iterator_init_at (working_dir_fd,
                                   sub_directory != NULL ? sub_directory : ".",
                                   FALSE,
                                   &iter,
                                   &error))
    {
      glnx_prefix_error (&error,
                         "An error occurred trying to initialize an iterator for \"%s\"",
                         full_working_path);
      g_debug ("%s", error->message);
      g_ptr_array_add (messages, g_strdup_printf ("%s %d: %s",
                                                  g_quark_to_string (error->domain),
                                                  error->code, error->message));
      return;
    }

  while (error == NULL)
    {
      struct dirent *dent;
      g_autofree gchar *full_name = NULL;

      if (!glnx_dirfd_iterator_next_dent_ensure_dtype (&iter, &dent, NULL, &error))
        {
          glnx_prefix_error (&error, "An error occurred trying to initerate through \"%s\"",
                             full_working_path);
          g_debug ("%s", error->message);
          g_ptr_array_add (messages, g_strdup_printf ("%s %d: %s",
                                                      g_quark_to_string (error->domain),
                                                      error->code, error->message));
          return;
        }

      if (dent == NULL)
        break;

      full_name = g_build_filename (full_working_path, dent->d_name, NULL);

      if (dent->d_type == DT_LNK)
        {
          g_autofree gchar *target = NULL;

          target = glnx_readlinkat_malloc (iter.fd, dent->d_name, NULL, &error);
          if (target == NULL)
            {
              glnx_prefix_error (&error, "An error occurred trying to read the symlink \"%s\"",
                                 full_name);
              g_debug ("%s", error->message);
              g_ptr_array_add (messages, g_strdup_printf ("%s %d: %s",
                                                          g_quark_to_string (error->domain),
                                                          error->code, error->message));
              g_clear_error (&error);
              target = g_strdup ("(unknown)");
            }

          for (i = 0; common_replacements != NULL && common_replacements[i].to != NULL; i++)
            {
              if (common_replacements[i].from == NULL)
                continue;

              const gchar *after = _srt_get_path_after (target, common_replacements[i].from);

              if (after != NULL)
                {
                  g_autofree gchar *new_target = NULL;
                  new_target = g_build_filename (common_replacements[i].to, after, NULL);
                  g_clear_pointer (&target, g_free);
                  target = g_steal_pointer (&new_target);
                  break;
                }
            }

          g_ptr_array_add (result, g_strdup_printf ("%s -> %s", full_name, target));
        }
      else if (dent->d_type == DT_DIR)
        {
          g_ptr_array_add (result, g_strdup_printf ("%s/", full_name));

          _srt_list_directory_content (iter.fd, full_working_path, dent->d_name,
                                       common_replacements, level + 1, result, messages);
        }
      else
        {
          g_ptr_array_add (result, g_steal_pointer (&full_name));
        }

    }
}

/*
 * _srt_recursive_list_content:
 * @sysroot: (not nullable) (type filename): A path used as the root
 * @sysroot_fd: A file descriptor opened on @sysroot, or negative to
 *  reopen it
 * @directory: (not nullable) (type filename): A path below the root directory,
 *  either absolute or relative (to the root)
 * @directory_fd: If non-negative, assumed to be open on @directory
 * @envp: (array zero-terminated=1) (not nullable): Behave as though `environ`
 *  was this array
 * @messages_out: (optional) (out) (array zero-terminated=1) (transfer full):
 *  If not %NULL, used to return a %NULL-terminated array of diagnostic
 *  messages. Free with g_strfreev().
 *
 * Returns: (array zero-terminated=1) (transfer full) (nullable): A
 *  %NULL-terminated array of files, symbolic links and directories, that
 *  are present in the provided @directory. Free with g_strfreev().
 */
gchar **
_srt_recursive_list_content (const gchar *sysroot,
                             int sysroot_fd,
                             const gchar *directory,
                             int directory_fd,
                             const char * const *envp,
                             gchar ***messages_out)
{
  g_autoptr(GPtrArray) content = NULL;
  g_autoptr(GPtrArray) messages = NULL;
  glnx_autofd int local_sysroot_fd = -1;
  glnx_autofd int top_fd = -1;
  g_autoptr(GError) error = NULL;
  const gchar *steam_runtime = NULL;

  g_return_val_if_fail (sysroot != NULL, NULL);
  g_return_val_if_fail (directory != NULL, NULL);
  g_return_val_if_fail (envp != NULL, NULL);
  g_return_val_if_fail (messages_out == NULL || *messages_out == NULL, NULL);

  steam_runtime = _srt_environ_getenv (envp, "STEAM_RUNTIME");
  /* If STEAM_RUNTIME is just the root directory we don't want to replace
   * every leading '/' with $STEAM_RUNTIME */
  if (g_strcmp0 (steam_runtime, "/") == 0)
    steam_runtime = NULL;

  const CommonReplacements common_replacements[] =
  {
    { steam_runtime, "$STEAM_RUNTIME" },
    { _srt_environ_getenv (envp, "HOME"), "$HOME" },
    { NULL, NULL },
  };

  content = g_ptr_array_new_with_free_func (g_free);
  messages = g_ptr_array_new_with_free_func (g_free);

  if (sysroot_fd < 0)
    {
      if (!glnx_opendirat (-1, sysroot, FALSE, &local_sysroot_fd, &error))
        {
          glnx_prefix_error (&error, "An error occurred trying to open sysroot \"%s\"",
                             sysroot);
          g_debug ("%s", error->message);
          g_ptr_array_add (messages, g_strdup_printf ("%s %d: %s",
                                                      g_quark_to_string (error->domain),
                                                      error->code, error->message));
          goto out;
        }
      sysroot_fd = local_sysroot_fd;
    }

  if (directory_fd < 0)
    {
      top_fd = _srt_resolve_in_sysroot (sysroot_fd,
                                        directory,
                                        (SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY
                                         | SRT_RESOLVE_FLAGS_READABLE),
                                        NULL,
                                        &error);

      if (top_fd < 0)
        {
          glnx_prefix_error (&error, "An error occurred trying to resolve \"%s\" in sysroot",
                             directory);
          g_debug ("%s", error->message);
          g_ptr_array_add (messages, g_strdup_printf ("%s %d: %s",
                                                      g_quark_to_string (error->domain),
                                                      error->code, error->message));
          goto out;
        }

      directory_fd = top_fd;
    }

  _srt_list_directory_content (directory_fd, directory, NULL, common_replacements, 0,
                               content, messages);

  g_ptr_array_sort (content, _srt_indirect_strcmp0);

out:
  g_ptr_array_add (content, NULL);

  if (messages_out != NULL && messages->len > 0)
    {
      g_ptr_array_add (messages, NULL);
      *messages_out = (GStrv) g_ptr_array_free (g_steal_pointer (&messages), FALSE);
    }

  return (GStrv) g_ptr_array_free (g_steal_pointer (&content), FALSE);
}

/*
 * @str: A path
 * @prefix: A possible prefix
 *
 * The same as flatpak_has_path_prefix(), but instead of a boolean,
 * return the part of @str after @prefix (non-%NULL but possibly empty)
 * if @str has prefix @prefix, or %NULL if it does not.
 *
 * Returns: (nullable) (transfer none): the part of @str after @prefix,
 *  or %NULL if @str is not below @prefix
 */
const char *
_srt_get_path_after (const char *str,
                     const char *prefix)
{
  while (TRUE)
    {
      /* Skip consecutive slashes to reach next path
         element */
      while (*str == '/')
        str++;
      while (*prefix == '/')
        prefix++;

      /* No more prefix path elements? Done! */
      if (*prefix == 0)
        return str;

      /* Compare path element */
      while (*prefix != 0 && *prefix != '/')
        {
          if (*str != *prefix)
            return NULL;
          str++;
          prefix++;
        }

      /* Matched prefix path element,
         must be entire str path element */
      if (*str != '/' && *str != 0)
        return NULL;
    }
}

/*
 * _srt_set_compatible_resource_limits:
 * @pid: A process ID, or 0 to act on the current process
 *
 * Attempt to set resource limits (see `getrlimit(3)`) for the given
 * process to have reasonable values that are compatible with the
 * maximum number of programs and libraries.
 *
 * `RLIMIT_NOFILE`, the limit for file descriptor numbers that can be
 * returned by a successful `open(2)`, `pipe(2)`, `dup(2)`, etc.,
 * is set to 1024 or to the hard limit, whichever is lower, to avoid
 * incompatibility with two classes of programs:
 *
 * - programs that call `select(2)`, which cannot represent file
 *   descriptors higher than 1023 in the `fd_set` bitmask;
 * - programs that allocate an amount of memory or perform a number
 *   of operations proportional to `RLIMIT_NOFILE`, such as some
 *   Java interpreters
 *
 * See <http://0pointer.net/blog/file-descriptor-limits.html> for more
 * information on `RLIMIT_NOFILE` best practices.
 *
 * Other resource limits are not currently altered.
 *
 * This function is technically not async-signal-safe
 * (see `signal-safety(7)`), but in practice can probably be called
 * safely after `fork()` on glibc systems.
 *
 * Returns: 0 on success, or a negative errno value such as `-EINVAL`
 *  on failure, with errno set.
 */
int
_srt_set_compatible_resource_limits (pid_t pid)
{
  struct rlimit rlim = { 0, 0 };
  int ret;

  ret = prlimit (pid, RLIMIT_NOFILE, NULL, &rlim);

  if (ret < 0)
    return -errno;

  if (rlim.rlim_cur != FD_SETSIZE
      && (rlim.rlim_max >= FD_SETSIZE
          || rlim.rlim_max == RLIM_INFINITY))
    {
      rlim.rlim_cur = FD_SETSIZE;
      ret = prlimit (pid, RLIMIT_NOFILE, &rlim, NULL);

      if (ret < 0)
        return -errno;
    }

  return 0;
}

/*
 * _srt_find_executable:
 * @error: Used to raise an error on failure
 *
 * Find the current executable.
 *
 * Returns: (transfer full): The path to the executable, or %NULL on failure
 */
gchar *
_srt_find_executable (GError **error)
{
  g_autofree gchar *target = NULL;

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  target = glnx_readlinkat_malloc (-1, "/proc/self/exe", NULL, error);

  if (target == NULL)
    return glnx_prefix_error_null (error, "Unable to resolve /proc/self/exe");

  return g_steal_pointer (&target);
}

/*
 * _srt_find_executable_dir:
 * @error: Used to raise an error on failure
 *
 * Find the directory containing this executable.
 *
 * Returns: (transfer full): The directory containing the current executable,
 *  or %NULL on failure
 */
gchar *
_srt_find_executable_dir (GError **error)
{
  g_autofree gchar *target = _srt_find_executable (error);

  return g_path_get_dirname (target);
}

gboolean
_srt_boolean_environment (const gchar *name,
                          gboolean def)
{
  const gchar *value = g_getenv (name);

  if (g_strcmp0 (value, "1") == 0)
    return TRUE;

  if (g_strcmp0 (value, "") == 0 || g_strcmp0 (value, "0") == 0)
    return FALSE;

  if (value != NULL)
    g_warning ("Unrecognised value \"%s\" for $%s", value, name);

  return def;
}

/*
 * _srt_async_signal_safe_error:
 * @prgname: The program name, like g_get_prgname()
 * @message: A human-readable message
 * @exit_status: Call `_exit` with this status
 *
 * Exit with a fatal error, like g_error(), but async-signal-safe
 * (see signal-safety(7)).
 */
void
_srt_async_signal_safe_error (const char *prgname,
                              const char *message,
                              int exit_status)
{
  if ((prgname != NULL && write (STDERR_FILENO, prgname, strlen (prgname)) < 0)
      || (prgname != NULL && write (STDERR_FILENO, ": ", 2) < 0)
      || write (STDERR_FILENO, message, strlen (message)) < 0
      || write (STDERR_FILENO, "\n", 1) < 0)
    {
      /* Ignore - there's nothing we can do about it anyway - but
       * suppress -Wunused-result. */
    }

  _exit (exit_status);
}

/**
 * _srt_get_current_dirs:
 * @cwd_p: (out) (transfer full) (optional): Used to return the
 *  current physical working directory, equivalent to `$(pwd -P)`
 *  in a shell
 * @cwd_l: (out) (transfer full) (optional): Used to return the
 *  current logical working directory, equivalent to `$(pwd -L)`
 *  in a shell
 *
 * Return the physical and/or logical working directory.
 */
void
_srt_get_current_dirs (gchar **cwd_p,
                       gchar **cwd_l)
{
  g_autofree gchar *cwd = NULL;
  const gchar *pwd;

  g_return_if_fail (cwd_p == NULL || *cwd_p == NULL);
  g_return_if_fail (cwd_l == NULL || *cwd_l == NULL);

  cwd = g_get_current_dir ();

  if (cwd_p != NULL)
    *cwd_p = g_canonicalize_filename (cwd, NULL);

  if (cwd_l != NULL)
    {
      pwd = g_getenv ("PWD");

      if (pwd != NULL && _srt_is_same_file (pwd, cwd))
        *cwd_l = g_strdup (pwd);
      else
        *cwd_l = g_strdup (cwd);
    }
}

#define PROC_SYS_KERNEL_RANDOM_UUID "/proc/sys/kernel/random/uuid"

/**
 * _srt_get_random_uuid:
 * @error: Used to raise an error on failure
 *
 * Return a random UUID (RFC 4122 version 4) as a string.
 * It is a 128-bit quantity, with 122 bits of entropy, and 6 fixed bits
 * indicating the "variant" (type, 0b10) and "version" (subtype, 0b0100).
 *
 * Returns: (transfer full): A random UUID, or %NULL on error
 */
gchar *
_srt_get_random_uuid (GError **error)
{
  g_autofree gchar *contents = NULL;

  if (!g_file_get_contents (PROC_SYS_KERNEL_RANDOM_UUID,
                            &contents, NULL, error))
    return NULL;

  g_strchomp (contents);    /* delete trailing newline */

  /* Format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx */
  if (strlen (contents) != 36)
    return glnx_null_throw (error, "%s not in expected format",
                            PROC_SYS_KERNEL_RANDOM_UUID);

  return g_steal_pointer (&contents);
}

/**
 * _srt_get_steam_app_id:
 *
 * Attempt to determine the Steam app-ID of the current process.
 *
 * Returns: either @cli_override or a global environment variable
 */
const char *
_srt_get_steam_app_id (void)
{
  const char *value;

  if ((value = g_getenv ("STEAM_COMPAT_APP_ID")) != NULL)
    return value;

  if ((value = g_getenv ("SteamAppId")) != NULL)
    return value;

  return NULL;
}

/*
 * _srt_hash_table_iter_init:
 * @iter: An iterator
 * @table: A hash table
 *
 * Same as g_hash_table_iter_init().
 */
void
_srt_hash_table_iter_init (SrtHashTableIter *iter,
                           GHashTable *table)
{
  SrtHashTableIter zero = SRT_HASH_TABLE_ITER_CLEARED;

  *iter = zero;
  g_hash_table_iter_init (&iter->real_iter, table);
}

/*
 * Adapter to compare two pointers to items with a #GCompareFunc, in a
 * container that compares pointers to pointers to the items themselves.
 * For example, this can be used to pass g_strcmp0() into g_qsort_with_data().
 */
static int
indirect_cmp (const void *a,
              const void *b,
              void *user_data)
{
  const void * const *ap = a;
  const void * const *bp = b;
  GCompareFunc cmp = user_data;

  return cmp (*ap, *bp);
}

/*
 * _srt_hash_table_iter_init_sorted:
 * @iter: An iterator
 * @table: A hash table
 * @cmp: Function to compare two items, or %NULL to iterate in
 *  arbitrary order
 *
 * Same as g_hash_table_iter_init(), but if @cmp is non-%NULL, then
 * iteration will be done in a sorted order.
 * It is a programming error to modify @table during this iteration.
 */
void
_srt_hash_table_iter_init_sorted (SrtHashTableIter *iter,
                                  GHashTable *table,
                                  GCompareFunc cmp)
{
  _srt_hash_table_iter_init (iter, table);
  iter->table = g_hash_table_ref (table);

  if (cmp != NULL)
    {
      iter->sorted_keys = g_hash_table_get_keys_as_array (table, &iter->sorted_n);

      if (iter->sorted_n > 0)
        g_qsort_with_data (iter->sorted_keys, iter->sorted_n,
                           sizeof (gpointer), indirect_cmp, cmp);

      iter->sorted_next = 0;
    }
}

/*
 * _srt_hash_table_iter_next:
 * @iter: An iterator
 * @key_p: A pointer to gpointer used to return the key, or %NULL to ignore
 * @value_p: A pointer to gpointer used to return the value, or %NULL to ignore
 *
 * Same as g_hash_table_iter_next(), but return items sorted by key
 * if @iter was initialized with a sort order.
 */
gboolean
_srt_hash_table_iter_next (SrtHashTableIter *iter,
                           gpointer key_p,
                           gpointer value_p)
{
  gpointer *key_out = key_p;
  gpointer *value_out = value_p;

  if (iter->sorted_keys == NULL)
    return g_hash_table_iter_next (&iter->real_iter, key_out, value_out);

  if (iter->sorted_next < iter->sorted_n)
    {
      gpointer k = iter->sorted_keys[iter->sorted_next++];

      if (key_out != NULL)
        *key_out = k;

      if (value_out != NULL)
        *value_out = g_hash_table_lookup (iter->table, k);

      return TRUE;
    }

  if (key_out != NULL)
    *key_out = NULL;

  if (value_out != NULL)
    *value_out = NULL;

  return FALSE;
}

/*
 * copy_dirent:
 * @other: Another struct dirent
 *
 * Returns: a copy of @other. Free with g_free().
 */
static struct dirent *
copy_dirent (const struct dirent *other)
{
  struct dirent *self;
  size_t len;

  /* We can't just use sizeof (struct dirent) for the length to copy,
   * because filenames are allowed to be longer than NAME_MAX bytes. */
  len = G_STRUCT_OFFSET (struct dirent, d_name) + strlen (other->d_name) + 1;

  if (len < other->d_reclen)
    len = other->d_reclen;

  if (len < sizeof (struct dirent))
    len = sizeof (struct dirent);

  self = g_malloc0 (len);
  memcpy (self, other, len);
  return self;
}

/*
 * _srt_dirent_strcmp:
 *
 * A #SrtDirentCompareFunc that compares lexicographically.
 * In the `C` locale, this is effectively the same as GNU `alphasort`.
 * In other locales, it ignores the locale settings and continues to
 * provide lexicographic order.
 */
int
_srt_dirent_strcmp (const struct dirent **a,
                    const struct dirent **b)
{
  return strcmp ((*a)->d_name, (*b)->d_name);
}

/*
 * _srt_dir_iter_next_dent:
 * @self: A directory iterator
 * @out_dent: (out) (nullable) (not optional) (transfer none): Used to
 *  emit the next entry, or %NULL at end of iteration
 * @cancellable: Cancellation indicator
 * @error: Error indicator
 *
 * If there are more entries in @self, set @out_dent to the next entry,
 * advance the iterator and return %TRUE.
 *
 * At the end of iteration, set @out_dent to %NULL and return %TRUE.
 *
 * If the #SrtDirIter has a non-%NULL #SrtDirentCompareFunc, then
 * directory entries will be read into a cache at the beginning of
 * iteration so that they can be emitted in the specified order,
 * and subsequent iteration will iterate through the cache.
 *
 * If the #SrtDirIterFlags include %SRT_DIR_ITER_FLAGS_ENSURE_DTYPE,
 * then all directory entries will have the `d_type` field set.
 *
 * Returns: %FALSE on error, %TRUE on success
 */
gboolean
_srt_dir_iter_next_dent (SrtDirIter *self,
                         struct dirent **out_dent,
                         GCancellable *cancellable,
                         GError **error)
{
  gboolean (*next_dent) (GLnxDirFdIterator *, struct dirent **,
                         GCancellable *, GError **);

  g_return_val_if_fail (out_dent != NULL, FALSE);

  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    return FALSE;

  if (self->flags & SRT_DIR_ITER_FLAGS_ENSURE_DTYPE)
    next_dent = glnx_dirfd_iterator_next_dent_ensure_dtype;
  else
    next_dent = glnx_dirfd_iterator_next_dent;

  if (self->cmp != NULL)
    {
      if (self->members == NULL)
        {
          struct dirent *member;

          /* arbitrarily guess 8 members per directory */
          self->members = g_ptr_array_new_full (8, g_free);
          g_assert (self->next_member == 0);

          do
            {
              if (!next_dent (&self->real_iter, &member, cancellable, error))
                return FALSE;

              if (member != NULL)
                g_ptr_array_add (self->members, copy_dirent (member));
            }
          while (member != NULL);

          g_ptr_array_sort (self->members,
                            (GCompareFunc) (GCallback) self->cmp);
        }

      if (self->next_member >= self->members->len)
        *out_dent = NULL;
      else
        *out_dent = g_ptr_array_index (self->members, self->next_member++);

      return TRUE;
    }

  return next_dent (&self->real_iter, out_dent, cancellable, error);
}

typedef union
{
  struct sockaddr addr;
  struct sockaddr_storage storage;
  struct sockaddr_in in;
  struct sockaddr_in6 in6;
  struct sockaddr_un un;
} any_sockaddr;

static void
string_append_escaped_len (GString *str,
                           const char *bytes,
                           size_t len)
{
  size_t i;

  for (i = 0; i < len; i++)
    {
      char c = bytes[i];

      if (c >= ' ' && c < 0x7f && c != '"' && c != '\\')
        g_string_append_c (str, c);
      else
        g_string_append_printf (str, "\\%03o", (unsigned char) c);
    }
}

static gboolean
string_append_sockaddr (GString *str,
                        const any_sockaddr *addr,
                        size_t addr_len)
{
  size_t path_size;
  char host[1024];

  switch (addr->storage.ss_family)
    {
      case AF_UNIX:
        if (addr_len <= offsetof (struct sockaddr_un, sun_path))
          {
            /* unnamed AF_UNIX socket */
            g_string_append (str, "AF_UNIX");
            return TRUE;
          }

        path_size = addr_len - offsetof (struct sockaddr_un, sun_path);
        g_string_append (str, "AF_UNIX \"");
        string_append_escaped_len (str, addr->un.sun_path, path_size);
        g_string_append_c (str, '"');
        return TRUE;

      case AF_INET:
      case AF_INET6:
        if (getnameinfo (&addr->addr, addr_len, host, sizeof (host), NULL, 0,
                         NI_NUMERICHOST) == 0)
          {
            if (addr->storage.ss_family == AF_INET6)
              g_string_append_c (str, '[');

            g_string_append (str, host);

            if (addr->storage.ss_family == AF_INET6)
              {
                g_string_append_printf (str, "]:%d",
                                        ntohs (addr->in6.sin6_port));
              }
            else
              {
                g_assert (addr->storage.ss_family == AF_INET);
                g_string_append_printf (str, ":%d", ntohs (addr->in.sin_port));
              }

            return TRUE;
          }

        return FALSE;

      default:
        return FALSE;
    }
}

/*
 * Return some sort of human-readable description of @fd, suitable for
 * diagnostic use.
 *
 * If @fd is a regular file, the result will usually be its absolute path.
 *
 * If @fd is a socket, the result will usually show the local and peer
 * addresses.
 *
 * If @fd is not a valid file descriptor or its target cannot be discovered,
 * the result may be an error message, for example
 * "Bad file descriptor" or "readlinkat: No such file or directory".
 *
 * Returns: (type utf8): A diagnostic string. Free with g_free().
 */
gchar *
_srt_describe_fd (int fd)
{
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *proc_self_fd_n = g_strdup_printf ("/proc/self/fd/%d", fd);
  g_autofree gchar *target = NULL;
  any_sockaddr addr = {};
  socklen_t addr_len = sizeof (addr);

  target = glnx_readlinkat_malloc (-1, proc_self_fd_n, NULL, &local_error);

  if (getsockname (fd, &addr.addr, &addr_len) == 0)
    {
      /* fd is a socket */
      g_autoptr(GString) ret = g_string_new (target);
      size_t position;

      position = ret->len;

      if (string_append_sockaddr (ret, &addr, addr_len))
        g_string_insert (ret, position, ": ");

      position = ret->len;
      memset (&addr, 0, sizeof (addr));
      addr_len = sizeof (addr);

      if (getpeername (fd, &addr.addr, &addr_len) == 0
          && string_append_sockaddr (ret, &addr, addr_len))
        g_string_insert (ret, position, " -> ");

      return g_string_free (g_steal_pointer (&ret), FALSE);
    }
  else if (errno == EBADF)
    {
      /* fd is not a valid file descriptor at all */
      return g_strdup (g_strerror (EBADF));
    }
  /* else fd is valid, but not a socket: maybe a regular file, or
   * some VFS object like a pipe */

  if (target == NULL)
    return g_strdup (local_error->message);

  return g_strescape (target, NULL);
}
