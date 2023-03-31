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

#include "steam-runtime-tools/glib-backports-internal.h"

#include <steam-runtime-tools/steam-runtime-tools.h>

#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "steam-runtime-tools/display-internal.h"
#include "test-utils.h"

typedef struct
{
  int unused;
} Fixture;

typedef struct
{
  int unused;
} Config;

static void
setup (Fixture *f,
       gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;
}

/*
 * Test basic functionality of the SrtDisplayInfo object.
 */
static void
test_object (Fixture *f,
             gconstpointer context)
{
  g_autoptr(SrtDisplayInfo) display = NULL;
  gboolean wayland_session;
  SrtDisplayWaylandIssues wayland_issues;
  g_auto(GStrv) environment_variables = NULL;

  const gchar *expected_variables[] =
  {
    "DISPLAY=:0",
    "WAYLAND_DISPLAY=wayland-0",
    "XDG_SESSION_TYPE=wayland",
    NULL,
  };

  display = _srt_display_info_new ((GStrv) expected_variables,
                                   FALSE,
                                   SRT_DISPLAY_WAYLAND_ISSUES_MISSING_SOCKET);

  g_assert_true (g_strv_equal (srt_display_info_get_environment_list (display), expected_variables));
  g_assert_false (srt_display_info_is_wayland_session (display));
  g_assert_cmpint (srt_display_info_get_wayland_issues (display), ==,
                   SRT_DISPLAY_WAYLAND_ISSUES_MISSING_SOCKET);

  g_object_get (display,
                "display-environ", &environment_variables,
                "wayland-session", &wayland_session,
                "wayland-issues", &wayland_issues,
                NULL);

  g_assert_true (g_strv_equal ((const gchar *const *) environment_variables, expected_variables));
  g_assert_false (wayland_session);
  g_assert_cmpint (wayland_issues, ==,
                   SRT_DISPLAY_WAYLAND_ISSUES_MISSING_SOCKET);
}

/*
 * Ensure SrtDisplayInfo keeps the expected environment variables
 */
static void
test_display_environment (Fixture *f,
                          gconstpointer context)
{
  g_autoptr(SrtSystemInfo) info = NULL;
  g_autoptr(SrtDisplayInfo) display = NULL;
  g_auto(GStrv) env = NULL;
  const gchar *const *display_env_vars = NULL;

  info = srt_system_info_new (NULL);

  env = g_get_environ ();
  env = g_environ_setenv (env, "DISPLAY", ":0", TRUE);
  env = g_environ_setenv (env, "DISPLAY_MINE", ":1", TRUE);
  env = g_environ_setenv (env, "WHAT_DISPLAY", ":)", TRUE);
  env = g_environ_setenv (env, "GDK_BACKEND", "x11", TRUE);

  srt_system_info_set_environ (info, env);

  display = srt_system_info_check_display (info);

  display_env_vars = srt_display_info_get_environment_list (display);

  /* We expect to only have the environment variables relevant to the display */
  g_assert_nonnull (display_env_vars);
  g_assert_cmpstr (g_environ_getenv ((gchar **) display_env_vars, "DISPLAY"), ==, ":0");
  g_assert_cmpstr (g_environ_getenv ((gchar **) display_env_vars, "DISPLAY_MINE"), ==, NULL);
  g_assert_cmpstr (g_environ_getenv ((gchar **) display_env_vars, "WHAT_DISPLAY"), ==, NULL);
  g_assert_cmpstr (g_environ_getenv ((gchar **) display_env_vars, "GDK_BACKEND"), ==, "x11");
}

static void
test_display_wayland_issues (Fixture *f,
                             gconstpointer context)
{
  g_autoptr(SrtSystemInfo) info = NULL;
  g_autoptr(SrtDisplayInfo) display = NULL;
  g_auto(GStrv) env = NULL;
  g_autofree gchar *temp = NULL;
  g_autofree gchar *wayland_0 = NULL;
  g_autofree gchar *wayland_1 = NULL;
  g_autoptr(GError) error = NULL;
  SrtDisplayWaylandIssues wayland_issues;

  info = srt_system_info_new (NULL);

  temp = g_dir_make_tmp (NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (temp);

  wayland_0 = g_build_filename (temp, "wayland-0", NULL);
  wayland_1 = g_build_filename (temp, "wayland-1", NULL);
  g_file_set_contents (wayland_0, "", -1, &error);
  g_assert_no_error (error);
  g_file_set_contents (wayland_1, "", -1, &error);
  g_assert_no_error (error);

  /* Test the situation where WAYLAND_DISPLAY is unset and we expect to
   * fallback to the default wayland-0 */
  env = g_get_environ ();
  env = g_environ_unsetenv (env, "WAYLAND_DISPLAY");
  env = g_environ_setenv (env, "XDG_RUNTIME_DIR", temp, TRUE);
  srt_system_info_set_environ (info, env);

  display = srt_system_info_check_display (info);
  wayland_issues = srt_display_info_get_wayland_issues (display);
  g_assert_cmpint (wayland_issues, ==, SRT_DISPLAY_WAYLAND_ISSUES_NONE);
  g_assert_true (srt_display_info_is_wayland_session (display));
  g_clear_pointer (&display, g_object_unref);

  /* Test the explicitly set WAYLAND_DISPLAY */
  env = g_environ_setenv (env, "WAYLAND_DISPLAY", "wayland-1", TRUE);
  srt_system_info_set_environ (info, env);

  display = srt_system_info_check_display (info);
  wayland_issues = srt_display_info_get_wayland_issues (display);
  g_assert_cmpint (wayland_issues, ==, SRT_DISPLAY_WAYLAND_ISSUES_NONE);
  g_assert_true (srt_display_info_is_wayland_session (display));
  g_clear_pointer (&display, g_object_unref);

  /* Test WAYLAND_DISPLAY that points to a missing file */
  env = g_environ_setenv (env, "WAYLAND_DISPLAY", "wayland-missing", TRUE);
  srt_system_info_set_environ (info, env);

  display = srt_system_info_check_display (info);
  wayland_issues = srt_display_info_get_wayland_issues (display);
  g_assert_cmpint (wayland_issues, ==, SRT_DISPLAY_WAYLAND_ISSUES_MISSING_SOCKET);
  g_assert_false (srt_display_info_is_wayland_session (display));
  g_clear_pointer (&display, g_object_unref);

  /* Recent versions of Wayland can also use absolute paths */
  env = g_environ_setenv (env, "WAYLAND_DISPLAY", wayland_0, TRUE);
  env = g_environ_unsetenv (env, "XDG_RUNTIME_DIR");
  srt_system_info_set_environ (info, env);

  display = srt_system_info_check_display (info);
  wayland_issues = srt_display_info_get_wayland_issues (display);
  g_assert_cmpint (wayland_issues, ==, SRT_DISPLAY_WAYLAND_ISSUES_NONE);
  g_assert_true (srt_display_info_is_wayland_session (display));

  glnx_shutil_rm_rf_at (-1, temp, NULL, &error);
  g_assert_no_error (error);
}

int
main (int argc,
      char **argv)
{
  _srt_tests_init (&argc, &argv, NULL);

  g_test_add ("/display/object", Fixture, NULL,
              setup, test_object, teardown);
  g_test_add ("/display/environment", Fixture, NULL,
              setup, test_display_environment, teardown);
  g_test_add ("/display/wayland_issues", Fixture, NULL,
              setup, test_display_wayland_issues, teardown);

  return g_test_run ();
}
