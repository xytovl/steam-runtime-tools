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

static void
child_setup_cb (gpointer user_data)
{
  g_fdwalk_set_cloexec (3);
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
test_bwrap_executable (const char *bwrap_executable,
                       SrtBwrapFlags test_features,
                       GError **error)
{
  g_autoptr(GPtrArray) argv = g_ptr_array_sized_new (10);
  int wait_status;
  g_autofree gchar *child_stdout = NULL;
  g_autofree gchar *child_stderr = NULL;
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

  /* We use LEAVE_DESCRIPTORS_OPEN and set CLOEXEC in the child_setup,
   * to work around a deadlock in GLib < 2.60 */
  if (!g_spawn_sync (NULL,  /* cwd */
                     (gchar **) argv->pdata,
                     NULL,  /* environ */
                     G_SPAWN_LEAVE_DESCRIPTORS_OPEN,
                     child_setup_cb, NULL,
                     &child_stdout,
                     &child_stderr,
                     &wait_status,
                     &local_error))
    {
      g_debug ("Cannot run %s: %s",
               bwrap_executable, local_error->message);
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }
  else if (wait_status != 0)
    {
      const char *diag_prefix = "";
      const char *diag_stderr = "";

      g_debug ("Cannot run %s: wait status %d",
               bwrap_executable, wait_status);

      if (child_stdout != NULL && child_stdout[0] != '\0')
        g_debug ("Output:\n%s", child_stdout);

      if (child_stderr != NULL && child_stderr[0] != '\0')
        {
          g_debug ("Diagnostic output:\n%s", child_stderr);
          g_strchomp (child_stderr);
          diag_prefix = ": ";
          diag_stderr = child_stderr;
        }

      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Cannot run %s: wait status %d%s%s",
                   bwrap_executable, wait_status, diag_prefix, diag_stderr);
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
check_bwrap (const char *pkglibexecdir,
             gboolean skip_testing,
             SrtBwrapFlags *flags_out,
             GError **error)
{
  g_autofree gchar *local_bwrap = NULL;
  g_autofree gchar *system_bwrap = NULL;
  const char *tmp;

  g_return_val_if_fail (pkglibexecdir != NULL, NULL);

  tmp = g_getenv ("PRESSURE_VESSEL_BWRAP");

  if (tmp == NULL)
    tmp = g_getenv ("BWRAP");

  if (tmp != NULL)
    {
      /* If the user specified an environment variable, then we don't
       * try anything else. */
      g_info ("Using bubblewrap from environment: %s", tmp);

      if (!skip_testing
          && !test_bwrap_executable (tmp, SRT_BWRAP_FLAGS_NONE, error))
        return NULL;

      return g_strdup (tmp);
    }

  local_bwrap = g_build_filename (pkglibexecdir, "srt-bwrap", NULL);

  /* If our local copy works, use it. If not, keep relatively quiet
   * about it for now - we might need to use a setuid system copy, for
   * example on Debian 10, RHEL 7, Arch linux-hardened kernel. */
  if (skip_testing
      || test_bwrap_executable (local_bwrap, SRT_BWRAP_FLAGS_NONE, NULL))
    return g_steal_pointer (&local_bwrap);

  g_assert (!skip_testing);
  system_bwrap = find_system_bwrap ();

  /* Try the system copy */
  if (system_bwrap != NULL
      && test_bwrap_executable (system_bwrap, SRT_BWRAP_FLAGS_NONE, NULL))
    {
      if (flags_out != NULL)
        *flags_out |= SRT_BWRAP_FLAGS_SYSTEM;

      return g_steal_pointer (&system_bwrap);
    }

  /* If there was no system copy, try the local copy again. We expect
   * this to fail, and are really just doing this to populate @error -
   * but if it somehow works, great, I suppose? */
  if (test_bwrap_executable (local_bwrap, SRT_BWRAP_FLAGS_NONE, error))
    {
      g_warning ("Local bwrap executable didn't work first time but "
                 "worked second time?");
      return g_steal_pointer (&local_bwrap);
    }

  return NULL;
}

/*
 * _srt_check_bwrap:
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
_srt_check_bwrap (const char *pkglibexecdir,
                  gboolean skip_testing,
                  SrtBwrapFlags *flags_out,
                  GError **error)
{
  SrtBwrapFlags flags = SRT_BWRAP_FLAGS_NONE;
  g_autofree gchar *bwrap = check_bwrap (pkglibexecdir,
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

  if (test_bwrap_executable (bwrap, SRT_BWRAP_FLAGS_HAS_PERMS, NULL))
    flags |= SRT_BWRAP_FLAGS_HAS_PERMS;

  if (flags_out != NULL)
    *flags_out = flags;

  return g_steal_pointer (&bwrap);
}
