/*
 * Copyright © 2023 Collabora Ltd.
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

#include <stdlib.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx.h"

#include "tests/test-utils.h"

#include "graphics-provider.h"

typedef struct
{
  TestsOpenFdSet old_fds;
  gchar *tmpdir;
  int tmpdir_fd;
} Fixture;

typedef struct
{
  int unused;
} Config;

static void
setup (Fixture *f,
       gconstpointer context)
{
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *linuxbrew = NULL;
  g_autofree gchar *linuxbrew_exe = NULL;
  g_autofree gchar *linuxbrew_home = NULL;
  g_autofree gchar *linuxbrew_home_exe = NULL;
  g_autofree gchar *bin = NULL;
  g_autofree gchar *bin_exe = NULL;
  g_autofree gchar *local_bin = NULL;
  g_autofree gchar *local_bin_not_exe = NULL;
  const gchar *executable = "ldconfig";

  f->old_fds = tests_check_fd_leaks_enter ();
  f->tmpdir = g_dir_make_tmp ("pressure-vessel-tests.XXXXXX", &local_error);
  g_assert_no_error (local_error);
  glnx_opendirat (AT_FDCWD, f->tmpdir, TRUE, &f->tmpdir_fd, &local_error);
  g_assert_no_error (local_error);

  linuxbrew = g_build_filename (f->tmpdir, ".linuxbrew", "bin", NULL);
  g_assert_no_errno (g_mkdir_with_parents (linuxbrew, 0755));
  linuxbrew_exe = g_build_filename (".linuxbrew", "bin", executable, NULL);
  glnx_file_replace_contents_with_perms_at (f->tmpdir_fd, linuxbrew_exe,
                                            (const guint8 *) "", 0,
                                            (mode_t) 755, (uid_t) -1, (gid_t) -1,
                                            0, NULL, &local_error);
  g_assert_no_error (local_error);

  linuxbrew_home = g_build_filename (f->tmpdir, "home", "linuxbrew", ".local", "bin", NULL);
  g_assert_no_errno (g_mkdir_with_parents (linuxbrew_home, 0755));
  linuxbrew_home_exe = g_build_filename ("home", "linuxbrew", ".local", "bin", executable, NULL);
  glnx_file_replace_contents_with_perms_at (f->tmpdir_fd, linuxbrew_home_exe,
                                            (const guint8 *) "", 0,
                                            (mode_t) 755, (uid_t) -1, (gid_t) -1,
                                            0, NULL, &local_error);
  g_assert_no_error (local_error);

  bin = g_build_filename (f->tmpdir, "bin", NULL);
  g_assert_no_errno (g_mkdir_with_parents (bin, 0755));
  bin_exe = g_build_filename ("bin", executable, NULL);
  glnx_file_replace_contents_with_perms_at (f->tmpdir_fd, bin_exe,
                                            (const guint8 *) "", 0,
                                            (mode_t) 755, (uid_t) -1, (gid_t) -1,
                                            0, NULL, &local_error);
  g_assert_no_error (local_error);

  local_bin = g_build_filename (f->tmpdir, "home", "user", ".local", "bin", NULL);
  g_assert_no_errno (g_mkdir_with_parents (local_bin, 0755));
  local_bin_not_exe = g_build_filename ("home", "user", ".local", "bin", executable, NULL);
  /* Create an ldconfig that is unexpectedly not an executable */
  glnx_file_replace_contents_with_perms_at (f->tmpdir_fd, local_bin_not_exe,
                                            (const guint8 *) "", 0,
                                            (mode_t) 644, (uid_t) -1, (gid_t) -1,
                                            0, NULL, &local_error);
  g_assert_no_error (local_error);
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  g_autoptr(GError) local_error = NULL;

  glnx_close_fd (&f->tmpdir_fd);

  if (f->tmpdir != NULL)
    {
      glnx_shutil_rm_rf_at (-1, f->tmpdir, NULL, &local_error);
      g_assert_no_error (local_error);
    }

  g_clear_pointer (&f->tmpdir, g_free);

  tests_check_fd_leaks_leave (f->old_fds);
}

typedef struct
{
  const gchar *description;
  const gchar *path_value;
  const gchar *search_result;
  const gchar *search_result_suffix;
} GraphicsProviderTest;

static const GraphicsProviderTest graphics_provider_test[] =
{
  {
    .description = "`ldconfig` should be available in `/bin`",
    .path_value = "/usr/bin:/bin:/usr/sbin:/sbin",
    .search_result = "/bin/ldconfig",
  },

  {
    .description = "`.linuxbrew` is expected to be skipped",
    .path_value = "/.linuxbrew/bin:/usr/bin:/bin:/usr/sbin:/sbin",
    .search_result = "/bin/ldconfig",
  },

  {
    .description = "If the user is called `linuxbrew` we shouldn't skip it",
    .path_value = "/.linuxbrew/bin:/home/linuxbrew/.local/bin::/bin",
    .search_result = "/home/linuxbrew/.local/bin/ldconfig",
  },

  {
    .description = "If `/home/user/.local/bin/ldconfig` is not an executable, it should be skipped",
    .path_value = "/home/user/.local/bin:/home/linuxbrew/.local/bin",
    .search_result = "/home/linuxbrew/.local/bin/ldconfig",
  },

  {
    .description = "`ldconfig` is expected to be found in the hardcoded paths",
    .path_value = "/.linuxbrew/bin:/usr/sbin",
    .search_result = "/bin/ldconfig",
  },

  {
    .description = "Search in the common bin dirs when PATH is unset",
    .search_result = "/bin/ldconfig",
  },
};

static void
test_graphics_provider_search (Fixture *f,
                               gconstpointer context)
{
  g_autoptr(PvGraphicsProvider) graphics_provider = NULL;
  g_autoptr(GError) error = NULL;
  gsize i;

  /* Assume the provider path in current ns is the tmpdir. In this way we have
   * a controlled environment that we can edit for our tests. */
  graphics_provider = pv_graphics_provider_new (f->tmpdir, "/run/host", TRUE, &error);
  g_assert_no_error (error);
  g_assert_nonnull (graphics_provider);

  for (i = 0; i < G_N_ELEMENTS (graphics_provider_test); i++)
    {
      const GraphicsProviderTest *test = &graphics_provider_test[i];
      g_autofree gchar *program_path = NULL;

      g_test_message ("%s", test->description);

      program_path = pv_graphics_provider_search_in_path_and_bin (graphics_provider, test->path_value, "ldconfig");

      if (test->search_result_suffix != NULL)
        g_assert_true (g_str_has_suffix (program_path, test->search_result_suffix));
      else
        g_assert_cmpstr (program_path, ==, test->search_result);
    }
}

int
main (int argc,
      char **argv)
{
  _srt_setenv_disable_gio_modules ();

  _srt_tests_init (&argc, &argv, NULL);

  g_test_add ("/graphics-provider-search", Fixture, NULL,
              setup, test_graphics_provider_search, teardown);

  return g_test_run ();
}
