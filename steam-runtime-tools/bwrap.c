/*
 * Copyright © 2014-2019 Red Hat, Inc
 * Copyright © 2017-2024 Collabora Ltd.
 * Copyright © 2017 Jonas Ådahl
 * Copyright © 2018 Erick555
 * Copyright © 2022 Julian Orth
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "steam-runtime-tools/bwrap-internal.h"
#include "steam-runtime-tools/log-internal.h"
#include "steam-runtime-tools/utils-internal.h"

/*
 * find_system_bwrap:
 *
 * Attempt to find a system copy of bubblewrap, either in the PATH
 * or in the libexecdir used by some version of Flatpak.
 *
 * Returns: (transfer full): The path to bwrap(1), or %NULL if not found
 */
static gchar *
find_system_bwrap (void)
{
  static const char * const flatpak_libexecdirs[] =
  {
    "/usr/local/libexec",
    "/usr/libexec",
    "/usr/lib/flatpak"
  };
  g_autofree gchar *candidate = NULL;
  gsize i;

  candidate = g_find_program_in_path ("bwrap");

  if (candidate != NULL)
    return g_steal_pointer (&candidate);

  for (i = 0; i < G_N_ELEMENTS (flatpak_libexecdirs); i++)
    {
      candidate = g_build_filename (flatpak_libexecdirs[i],
                                    "flatpak-bwrap", NULL);

      if (g_file_test (candidate, G_FILE_TEST_IS_EXECUTABLE))
        return g_steal_pointer (&candidate);
      else
        g_clear_pointer (&candidate, g_free);
    }

  return NULL;
}

/*
 * test_bwrap_executable:
 *
 * Test whether the given @bwrap_executable works.
 *
 * If a feature flag is present in @test_features, only return true
 * if @bwrap_executable works *and* has the desired features.
 * %SRT_BWRAP_FLAGS_HAS_PERMS is currently the only feature flag.
 * %SRT_BWRAP_FLAGS_SYSTEM and %SRT_BWRAP_FLAGS_SETUID are ignored here.
 *
 * Returns: %TRUE if all of the requested features are present
 */
static gboolean
test_bwrap_executable (SrtSubprocessRunner *runner,
                       const char *bwrap_executable,
                       SrtBwrapFlags test_features,
                       GError **error)
{
  g_autoptr(GPtrArray) argv = g_ptr_array_sized_new (10);
  g_autoptr(SrtCompletedSubprocess) completed = NULL;
  g_autoptr(GError) local_error = NULL;

  g_ptr_array_add (argv, (char *) bwrap_executable);

  if (test_features & SRT_BWRAP_FLAGS_HAS_PERMS)
    {
      g_ptr_array_add (argv, (char *) "--perms");
      g_ptr_array_add (argv, (char *) "0700");
      g_ptr_array_add (argv, (char *) "--dir");
      g_ptr_array_add (argv, (char *) "/");
    }

  g_ptr_array_add (argv, (char *) "--bind");
  g_ptr_array_add (argv, (char *) "/");
  g_ptr_array_add (argv, (char *) "/");

  g_ptr_array_add (argv, (char *) "true");
  g_ptr_array_add (argv, NULL);

  completed = _srt_subprocess_runner_run_sync (runner,
                                               SRT_HELPER_FLAGS_TIME_OUT,
                                               (const char * const *) argv->pdata,
                                               SRT_SUBPROCESS_OUTPUT_CAPTURE_DEBUG,
                                               SRT_SUBPROCESS_OUTPUT_CAPTURE_DEBUG,
                                               &local_error);

  if (completed == NULL)
    {
      g_debug ("Cannot run %s: %s",
               bwrap_executable, local_error->message);
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }
  else if (!_srt_completed_subprocess_check (completed, &local_error))
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }
  else
    {
      g_debug ("Successfully ran: %s --bind / / true", bwrap_executable);
      return TRUE;
    }
}

/*
 * check_bwrap:
 * @runner: A subprocess execution environment
 * @pkglibexecdir: Path to libexec/steam-runtime-tools-0
 * @skip_testing: If true, do not test the bwrap executable, but instead
 *  assume that it will work
 * @flags_out: (out): Used to return %SRT_BWRAP_FLAGS_SYSTEM if appropriate
 *
 * Attempt to find a working bwrap executable in the environment,
 * @pkglibexecdir or a system location. Return %NULL with @error set
 * if none can be found.
 *
 * Flags other than %SRT_BWRAP_FLAGS_SYSTEM are not checked here.
 *
 * Returns: (transfer full): Path to bwrap(1), or %NULL if not found
 *  or non-functional
 */
static gchar *
check_bwrap (SrtSubprocessRunner *runner,
             const char *pkglibexecdir,
             gboolean skip_testing,
             SrtBwrapFlags *flags_out,
             GError **error)
{
  g_autofree gchar *local_bwrap = NULL;
  g_autofree gchar *system_bwrap = NULL;
  const char *tmp;

  g_return_val_if_fail (pkglibexecdir != NULL, NULL);

  tmp = _srt_subprocess_runner_getenv (runner, "PRESSURE_VESSEL_BWRAP");

  if (tmp == NULL)
    tmp = _srt_subprocess_runner_getenv (runner, "BWRAP");

  if (tmp != NULL)
    {
      /* If the user specified an environment variable, then we don't
       * try anything else. */
      g_info ("Using bubblewrap from environment: %s", tmp);

      if (!skip_testing
          && !test_bwrap_executable (runner, tmp, SRT_BWRAP_FLAGS_NONE, error))
        return NULL;

      return g_strdup (tmp);
    }

  local_bwrap = g_build_filename (pkglibexecdir, "srt-bwrap", NULL);

  /* If our local copy works, use it. If not, keep relatively quiet
   * about it for now - we might need to use a setuid system copy, for
   * example on Debian 10, RHEL 7, Arch linux-hardened kernel. */
  if (skip_testing
      || test_bwrap_executable (runner, local_bwrap, SRT_BWRAP_FLAGS_NONE, NULL))
    return g_steal_pointer (&local_bwrap);

  g_assert (!skip_testing);
  system_bwrap = find_system_bwrap ();

  /* Try the system copy */
  if (system_bwrap != NULL
      && test_bwrap_executable (runner, system_bwrap, SRT_BWRAP_FLAGS_NONE, NULL))
    {
      if (flags_out != NULL)
        *flags_out |= SRT_BWRAP_FLAGS_SYSTEM;

      return g_steal_pointer (&system_bwrap);
    }

  /* If there was no system copy, try the local copy again. We expect
   * this to fail, and are really just doing this to populate @error -
   * but if it somehow works, great, I suppose? */
  if (test_bwrap_executable (runner, local_bwrap, SRT_BWRAP_FLAGS_NONE, error))
    {
      g_warning ("Local bwrap executable didn't work first time but "
                 "worked second time?");
      return g_steal_pointer (&local_bwrap);
    }

  return NULL;
}

/*
 * _srt_check_bwrap:
 * @runner: A subprocess execution environment
 * @pkglibexecdir: Path to libexec/steam-runtime-tools-0
 * @skip_testing: If true, do not test the bwrap executable, but instead
 *  assume that it will work
 * @flags_out: (out): Properties of the returned executable
 *
 * Attempt to find a working bwrap executable in the environment,
 * @pkglibexecdir or a system location. Log messages via _srt_log_failure()
 * if none can be found.
 *
 * Returns: (transfer full): Path to bwrap(1), or %NULL if not found
 */
gchar *
_srt_check_bwrap (SrtSubprocessRunner *runner,
                  const char *pkglibexecdir,
                  gboolean skip_testing,
                  SrtBwrapFlags *flags_out,
                  GError **error)
{
  SrtBwrapFlags flags = SRT_BWRAP_FLAGS_NONE;
  g_autofree gchar *bwrap = check_bwrap (runner,
                                         pkglibexecdir,
                                         skip_testing,
                                         &flags,
                                         error);
  struct stat statbuf;

  if (bwrap == NULL)
    return NULL;

  if (stat (bwrap, &statbuf) < 0)
    {
      g_info ("stat(%s): %s", bwrap, g_strerror (errno));
    }
  else if (statbuf.st_mode & S_ISUID)
    {
      g_info ("Using setuid bubblewrap executable %s (permissions: %o)",
              bwrap, _srt_stat_get_permissions (&statbuf));
      flags |= SRT_BWRAP_FLAGS_SETUID;
    }

  if (test_bwrap_executable (runner, bwrap, SRT_BWRAP_FLAGS_HAS_PERMS, NULL))
    flags |= SRT_BWRAP_FLAGS_HAS_PERMS;

  if (flags_out != NULL)
    *flags_out = flags;

  return g_steal_pointer (&bwrap);
}

/*
 * _srt_check_bwrap_issues:
 * @sysroot: A system root used to look up kernel parameters
 * @runner: A subprocess execution environment
 * @pkglibexecdir: Directory containing srt-bwrap
 * @bwrap_out: (out): Path to a bwrap executable, if found
 * @message: (out): A diagnostic message if appropriate
 */
SrtBwrapIssues
_srt_check_bwrap_issues (SrtSysroot *sysroot,
                         SrtSubprocessRunner *runner,
                         const char *pkglibexecdir,
                         gchar **bwrap_out,
                         gchar **message_out)
{
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *bwrap = NULL;
  SrtBwrapIssues issues = SRT_BWRAP_ISSUES_NONE;
  SrtBwrapFlags flags = SRT_BWRAP_FLAGS_NONE;

  if (message_out != NULL)
    *message_out = NULL;

  if (bwrap_out != NULL)
    *bwrap_out = NULL;

  bwrap = _srt_check_bwrap (runner, pkglibexecdir, FALSE, &flags, &local_error);

  if (bwrap != NULL)
    {
      if (bwrap_out != NULL)
        *bwrap_out = g_steal_pointer (&bwrap);

      if (flags & SRT_BWRAP_FLAGS_SETUID)
        issues |= SRT_BWRAP_ISSUES_SETUID;

      if (flags & SRT_BWRAP_FLAGS_SYSTEM)
        issues |= SRT_BWRAP_ISSUES_SYSTEM;
    }
  else
    {
      if (message_out != NULL)
        *message_out = g_strdup (local_error->message);

      issues |= SRT_BWRAP_ISSUES_CANNOT_RUN;
    }

  /* As a minor optimization, if our bundled bwrap works, don't go looking
   * at why it might not work. */
  if (issues != SRT_BWRAP_ISSUES_NONE)
    {
      g_autofree gchar *contents = NULL;
      gsize len = 0;

      if (_srt_sysroot_load (sysroot, "/proc/sys/kernel/unprivileged_userns_clone",
                             SRT_RESOLVE_FLAGS_NONE, NULL,
                             &contents, &len, NULL))
        {
          if (g_strcmp0 (contents, "0\n") == 0)
            issues |= SRT_BWRAP_ISSUES_NO_UNPRIVILEGED_USERNS_CLONE;

          g_clear_pointer (&contents, g_free);
        }

      if (_srt_sysroot_load (sysroot, "/proc/sys/user/max_user_namespaces",
                             SRT_RESOLVE_FLAGS_NONE, NULL,
                             &contents, &len, NULL))
        {
          if (g_strcmp0 (contents, "0\n") == 0)
            issues |= SRT_BWRAP_ISSUES_MAX_USER_NAMESPACES_ZERO;

          g_clear_pointer (&contents, g_free);
        }
    }

  return issues;
}
