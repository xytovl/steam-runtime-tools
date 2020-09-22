/*
 * Copyright © 2019-2020 Collabora Ltd.
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

#include "tests/test-utils.h"

#include <glib.h>
#include <glib-object.h>

#include "libglnx/libglnx.h"

#include "steam-runtime-tools/glib-compat.h"
#include <steam-runtime-tools/steam-runtime-tools.h>
#include "steam-runtime-tools/utils-internal.h"

static gchar *fake_home_parent = NULL;

/**
 * _srt_global_setup_private_xdg_dirs:
 *
 * Setup a fake home directory and, following the [XDG standard]
 * (https://specifications.freedesktop.org/mime-apps-spec/mime-apps-spec-1.0.html)
 * mask every XDG used in the MIME lookup to avoid altering the tests if there
 * were already other user defined MIME lists.
 *
 * Call this function one time before launching the tests because changing the
 * environment variables is not thread safe.
 *
 * Returns: (type filename) (transfer full): Absolute path to the newly created
 *  fake home directory.
 */
gchar *
_srt_global_setup_private_xdg_dirs (void)
{
  GError *error = NULL;
  gchar *fake_home_path = NULL;
  gchar *xdg_data_home = NULL;

  g_return_val_if_fail (fake_home_parent == NULL, NULL);

  /* Create a directory that we control, and then put the fake home
   * directory inside it, so we can delete and recreate the fake home
   * directory without being vulnerable to symlink attacks. */
  fake_home_parent = g_dir_make_tmp ("fake-home-XXXXXX", &error);
  g_assert_no_error (error);
  fake_home_path = g_build_filename (fake_home_parent, "home", NULL);

  xdg_data_home = g_build_filename (fake_home_path, ".local", "share", NULL);

  g_setenv ("XDG_CONFIG_HOME", xdg_data_home, TRUE);
  g_setenv ("XDG_CONFIG_DIRS", xdg_data_home, TRUE);
  g_setenv ("XDG_DATA_HOME", xdg_data_home, TRUE);
  g_setenv ("XDG_DATA_DIRS", xdg_data_home, TRUE);

  g_free (xdg_data_home);

  return fake_home_path;
}

/**
 * _srt_global_teardown_private_xdg_dirs:
 *
 * Teardown the previously created temporary directory.
 *
 * Returns: %TRUE if no errors occurred removing the temporary directory.
 */
gboolean
_srt_global_teardown_private_xdg_dirs (void)
{
  gboolean result;
  g_return_val_if_fail (fake_home_parent != NULL, FALSE);

  result = _srt_rm_rf (fake_home_parent);
  g_free (fake_home_parent);
  fake_home_parent = NULL;

  return result;
}

TestsOpenFdSet
tests_check_fd_leaks_enter (void)
{
  g_autoptr(GHashTable) ret = NULL;
  g_auto(GLnxDirFdIterator) iter = { FALSE };
  g_autoptr(GError) error = NULL;

  ret = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  glnx_dirfd_iterator_init_at (AT_FDCWD, "/proc/self/fd", TRUE,
                               &iter, &error);
  g_assert_no_error (error);

  while (TRUE)
    {
      g_autofree gchar *target = NULL;
      struct dirent *dent;
      gint64 which;
      char *endptr;

      glnx_dirfd_iterator_next_dent (&iter, &dent, NULL, &error);
      g_assert_no_error (error);

      if (dent == NULL)
        break;

      if (g_str_equal (dent->d_name, ".") || g_str_equal (dent->d_name, ".."))
        continue;

      which = g_ascii_strtoll (dent->d_name, &endptr, 10);

      if (endptr == NULL || *endptr != '\0')
        {
          g_warning ("Found unexpected entry \"%s\" in /proc/self/fd",
                     dent->d_name);
          continue;
        }

      if (which == (gint64) iter.fd)
        continue;

      /* ignore error, just let it be NULL */
      target = glnx_readlinkat_malloc (iter.fd, dent->d_name, NULL, NULL);
      g_hash_table_replace (ret, g_strdup (dent->d_name),
                            g_steal_pointer (&target));
    }

  return g_steal_pointer (&ret);
}

void
tests_check_fd_leaks_leave (TestsOpenFdSet fds)
{
  g_auto(GLnxDirFdIterator) iter = { FALSE };
  g_autoptr(GError) error = NULL;

  glnx_dirfd_iterator_init_at (AT_FDCWD, "/proc/self/fd", TRUE,
                               &iter, &error);
  g_assert_no_error (error);

  while (TRUE)
    {
      g_autofree gchar *target = NULL;
      gpointer val = NULL;
      gint64 which;
      struct dirent *dent;
      char *endptr;

      glnx_dirfd_iterator_next_dent (&iter, &dent, NULL, &error);
      g_assert_no_error (error);

      if (dent == NULL)
        break;

      if (g_str_equal (dent->d_name, ".") || g_str_equal (dent->d_name, ".."))
        continue;

      which = g_ascii_strtoll (dent->d_name, &endptr, 10);

      if (endptr == NULL || *endptr != '\0')
        {
          g_warning ("Found unexpected entry \"%s\" in /proc/self/fd",
                     dent->d_name);
          continue;
        }

      if (which == (gint64) iter.fd)
        continue;

      /* ignore error, just let it be NULL */
      target = glnx_readlinkat_malloc (iter.fd, dent->d_name, NULL, NULL);

      if (g_hash_table_lookup_extended (fds, dent->d_name, NULL, &val))
        {
          g_assert_cmpstr (target, ==, val);
        }
      else
        {
          g_error ("fd %s \"%s\" was leaked",
                   dent->d_name, target == NULL ? "(null)" : target);
        }
    }

  g_hash_table_unref (fds);
}
