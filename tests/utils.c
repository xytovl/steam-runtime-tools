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

#include <steam-runtime-tools/steam-runtime-tools.h>

#include <dlfcn.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <syslog.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/input-device-internal.h"
#include "steam-runtime-tools/logger-internal.h"
#include "steam-runtime-tools/runtime-internal.h"
#include "steam-runtime-tools/steam-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "test-utils.h"

typedef void *AutoLibraryHandle;
G_DEFINE_AUTO_CLEANUP_FREE_FUNC (AutoLibraryHandle, dlclose, NULL);
typedef int (*type_of_sd_journal_stream_fd) (const char *, int, int);

static const char *argv0 = NULL;

typedef struct
{
  gchar *srcdir;
  gchar *builddir;
  gchar *logging_helper;
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

  f->srcdir = g_strdup (g_getenv ("G_TEST_SRCDIR"));
  f->builddir = g_strdup (g_getenv ("G_TEST_BUILDDIR"));

  if (f->srcdir == NULL)
    f->srcdir = g_path_get_dirname (argv0);

  if (f->builddir == NULL)
    f->builddir = g_path_get_dirname (argv0);

  f->logging_helper = g_build_filename (f->builddir, "logging-helper", NULL);
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  g_free (f->srcdir);
  g_free (f->builddir);
  g_free (f->logging_helper);
}

static void
test_avoid_gvfs (Fixture *f,
                 gconstpointer context)
{
  /* This doesn't actually call _srt_setenv_disable_gio_modules(),
   * because that's documented
   * to have to happen as early as possible in main(). Instead, we do that
   * in main() as documented, and in this function we just assert that
   * we did. */
  GVfs *vfs = g_vfs_get_default ();
  GVfs *local = g_vfs_get_local ();

  g_test_message ("Default VFS: %s at %p", G_OBJECT_TYPE_NAME (vfs), vfs);
  g_test_message ("Local VFS: %s at %p", G_OBJECT_TYPE_NAME (local), local);
  /* We compare by string equality to have a better message if this
   * assertion fails. We can't assert that the pointers are the same,
   * because GLib currently uses two instances of the same class. */
  g_assert_cmpstr (G_OBJECT_TYPE_NAME (vfs), ==,
                   G_OBJECT_TYPE_NAME (local));
  g_assert_cmpuint (G_OBJECT_TYPE (vfs), ==,
                    G_OBJECT_TYPE (local));
}

static void
test_bits_set (Fixture *f,
               gconstpointer context)
{
  g_assert_true (_srt_all_bits_set (0xff, 0x01 | 0x02 | 0x10));
  g_assert_false (_srt_all_bits_set (0x51, 0x01 | 0x02 | 0x10));
}

static void
test_compat_flags (Fixture *f,
                   gconstpointer context)
{
  static const struct
    {
      /* Length is arbitrary, expand as needed but leave room for NULL
       * termination */
      const char * const envp[3];
      SrtSteamCompatFlags expected;
    }
  tests[] =
    {
        {
            {
                "STEAM_COMPAT_FLAGS=search-cwd,search-cwd-first,reticulate-splines,fixme",
                NULL
            },
            (SRT_STEAM_COMPAT_FLAGS_SEARCH_CWD
             | SRT_STEAM_COMPAT_FLAGS_SEARCH_CWD_FIRST),
        },
        {
            { "STEAM_COMPAT_FLAGS=reticulate-splines,search-cwd", NULL },
            SRT_STEAM_COMPAT_FLAGS_SEARCH_CWD,
        },
        {
            { "STEAM_COMPAT_FLAGS=,,,,search-cwd-first,,,,", NULL },
            SRT_STEAM_COMPAT_FLAGS_SEARCH_CWD_FIRST,
        },
        {
            { "STEAM_COMPAT_FLAGS=runtime-sdl2", NULL },
            SRT_STEAM_COMPAT_FLAGS_RUNTIME_SDL2,
        },
        {
            { "STEAM_COMPAT_FLAGS=runtime-sdl3", NULL },
            SRT_STEAM_COMPAT_FLAGS_RUNTIME_SDL3,
        },
        {
            {
                "STEAM_COMPAT_TRACING=1",
                "STEAM_COMPAT_FLAGS=search-cwd",
                NULL
            },
            (SRT_STEAM_COMPAT_FLAGS_SEARCH_CWD
             | SRT_STEAM_COMPAT_FLAGS_SYSTEM_TRACING),
        },
        { { "STEAM_COMPAT_FLAGS=", NULL }, SRT_STEAM_COMPAT_FLAGS_NONE },
        { { "STEAM_COMPAT_TRACING=1", NULL }, SRT_STEAM_COMPAT_FLAGS_SYSTEM_TRACING },
        { { "STEAM_COMPAT_TRACING=", NULL }, SRT_STEAM_COMPAT_FLAGS_NONE },
        { { "STEAM_COMPAT_TRACING=0", NULL }, SRT_STEAM_COMPAT_FLAGS_NONE },
        { { "STEAM_COMPAT_RUNTIME_SDL2=1", NULL }, SRT_STEAM_COMPAT_FLAGS_RUNTIME_SDL2 },
        { { "STEAM_COMPAT_RUNTIME_SDL3=1", NULL }, SRT_STEAM_COMPAT_FLAGS_RUNTIME_SDL3 },
        { { NULL }, SRT_STEAM_COMPAT_FLAGS_NONE }
    };
  size_t i;

  g_assert_cmphex (_srt_steam_get_compat_flags (NULL), ==,
                   SRT_STEAM_COMPAT_FLAGS_NONE);

  for (i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      g_assert_cmpuint (g_strv_length ((gchar **) tests[i].envp),
                        <, G_N_ELEMENTS (tests[i].envp));
      g_assert_cmphex (_srt_steam_get_compat_flags (tests[i].envp),
                       ==, tests[i].expected);
    }
}

static void
test_describe_fd (Fixture *f,
                  gconstpointer context)
{
    {
      g_autofree gchar *desc = _srt_describe_fd (-1);

      g_assert_nonnull (desc);
      g_test_message ("Description of invalid fd: %s", desc);
    }

    {
      glnx_autofd int fd = open ("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
      g_autofree gchar *desc = _srt_describe_fd (fd);

      g_test_message ("Description of file: %s", desc);
      g_assert_cmpstr (desc, ==, "/");
    }

    {
      glnx_autofd int fd = open ("/dev/null", O_RDWR | O_CLOEXEC);
      g_autofree gchar *desc = _srt_describe_fd (fd);

      g_test_message ("Description of file: %s", desc);
      g_assert_cmpstr (desc, ==, "/dev/null");
    }

    {
      int socks[2];
      g_autofree gchar *desc = NULL;

      g_assert_no_errno (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, socks));
      desc = _srt_describe_fd (socks[0]);
      g_assert_nonnull (desc);
      g_test_message ("Description of half of a socketpair: %s", desc);
      g_assert_nonnull (strstr (desc, "AF_UNIX"));
      glnx_close_fd (&socks[0]);
      glnx_close_fd (&socks[1]);
    }

    {
      g_auto(SrtPipe) p = _SRT_PIPE_INIT;
      g_autoptr(GError) local_error = NULL;
      g_autofree gchar *desc = NULL;

      _srt_pipe_open (&p, &local_error);
      g_assert_no_error (local_error);
      desc = _srt_describe_fd (p.fds[0]);
      g_assert_nonnull (desc);
      g_test_message ("Description of half of a pipe: %s", desc);
    }

    {
      static union
        {
          struct sockaddr addr;
          struct sockaddr_un un;
        }
      journal_address =
        {
          .un =
            {
              .sun_family = AF_UNIX,
              .sun_path = "/run/systemd/journal/socket"
            }
        };
      glnx_autofd int fd = socket (AF_UNIX, SOCK_DGRAM, 0);

      g_assert_no_errno (fd);

      /* This will only work if systemd-journald happens to be running */
      if (connect (fd, &journal_address.addr, sizeof (journal_address)) == 0)
        {
          g_autofree gchar *desc = _srt_describe_fd (fd);

          g_assert_nonnull (desc);
          g_test_message ("Description of connected Unix socket: %s", desc);
          g_assert_nonnull (strstr (desc, "AF_UNIX"));
        }
    }

    {
      union
        {
          struct sockaddr addr;
          struct sockaddr_in in;
        }
      address =
        {
          .in = { .sin_family = AF_INET }
        };
      glnx_autofd int fd = socket (AF_INET, SOCK_STREAM, 0);

      g_assert_no_errno (fd);

      address.in.sin_port = htons (0);
      address.in.sin_addr.s_addr = htonl (INADDR_ANY);

      if (bind (fd, &address.addr, sizeof (address)) == 0)
        {
          g_autofree gchar *desc = _srt_describe_fd (fd);

          g_assert_nonnull (desc);
          g_test_message ("Description of bound IPv4 socket: %s", desc);
          g_assert_nonnull (strstr (desc, "0.0.0.0:"));
        }
    }

    {
      union
        {
          struct sockaddr addr;
          struct sockaddr_in in;
        }
      address =
        {
          .in = { .sin_family = AF_INET }
        };
      glnx_autofd int fd = socket (AF_INET, SOCK_STREAM, 0);

      g_assert_no_errno (fd);

      address.in.sin_port = htons (53);
      address.in.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

      /* This will only work if a local DNS resolver happens to be running */
      if (connect (fd, &address.addr, sizeof (address)) == 0)
        {
          g_autofree gchar *desc = _srt_describe_fd (fd);

          g_assert_nonnull (desc);
          g_test_message ("Description of connected IPv4 socket: %s", desc);
        }
    }

    {
      union
        {
          struct sockaddr addr;
          struct sockaddr_in6 in6;
        }
      address =
        {
          .in6 =
            {
              .sin6_family = AF_INET6,
              .sin6_flowinfo = 0,
              .sin6_scope_id = 0
            }
        };
      glnx_autofd int fd = socket (AF_INET6, SOCK_STREAM, 0);

      address.in6.sin6_port = htons (0);

      if (fd >= 0 && bind (fd, &address.addr, sizeof (address)) == 0)
        {
          g_autofree gchar *desc = _srt_describe_fd (fd);

          g_assert_nonnull (desc);
          g_test_message ("Description of bound IPv6 socket: %s", desc);
          g_assert_nonnull (strstr (desc, "[::]:"));
        }
    }
}

static void
test_dir_iter (Fixture *f,
               gconstpointer context)
{
  g_auto(SrtDirIter) iter = SRT_DIR_ITER_CLEARED;
  g_autoptr(GError) error = NULL;
  g_autofree char *prev = NULL;
  struct dirent *dent;

  _srt_dir_iter_init_at (&iter, -1, "/",
                         SRT_DIR_ITER_FLAGS_NONE,
                         NULL,
                         &error);
  g_assert_no_error (error);
  _srt_dir_iter_clear (&iter);

  _srt_dir_iter_init_at (&iter, -1, "/",
                         SRT_DIR_ITER_FLAGS_ENSURE_DTYPE,
                         _srt_dirent_strcmp,
                         &error);
  g_assert_no_error (error);
  _srt_dir_iter_clear (&iter);

  g_test_message ("Iterating over '/' in arbitrary order");
  _srt_dir_iter_init_at (&iter, -1, "/",
                         SRT_DIR_ITER_FLAGS_ENSURE_DTYPE,
                         NULL,
                         &error);
  g_assert_no_error (error);

  while (TRUE)
    {
      gboolean ok = _srt_dir_iter_next_dent (&iter, &dent, NULL, &error);

      g_assert_no_error (error);
      g_assert_true (ok);

      if (dent == NULL)
        break;

      g_assert_cmpstr (dent->d_name, !=, ".");
      g_assert_cmpstr (dent->d_name, !=, "..");
      g_assert_cmpint (dent->d_type, !=, DT_UNKNOWN);
      g_test_message ("%u ino#%lld %s",
                      dent->d_type, (long long) dent->d_ino, dent->d_name);
    }

  g_test_message ("And again");
  _srt_dir_iter_rewind (&iter);

  while (TRUE)
    {
      gboolean ok = _srt_dir_iter_next_dent (&iter, &dent, NULL, &error);

      g_assert_no_error (error);
      g_assert_true (ok);

      if (dent == NULL)
        break;

      g_assert_cmpstr (dent->d_name, !=, ".");
      g_assert_cmpstr (dent->d_name, !=, "..");
      g_assert_cmpint (dent->d_type, !=, DT_UNKNOWN);
      g_test_message ("%u ino#%lld %s",
                      dent->d_type, (long long) dent->d_ino, dent->d_name);
    }

  _srt_dir_iter_clear (&iter);

  g_test_message ("Iterating over '/' in sorted order");
  _srt_dir_iter_init_at (&iter, -1, "/",
                         SRT_DIR_ITER_FLAGS_NONE,
                         _srt_dirent_strcmp,
                         &error);
  g_assert_no_error (error);

  while (TRUE)
    {
      gboolean ok = _srt_dir_iter_next_dent (&iter, &dent, NULL, &error);

      g_assert_no_error (error);
      g_assert_true (ok);

      if (dent == NULL)
        break;

      g_assert_cmpstr (dent->d_name, !=, ".");
      g_assert_cmpstr (dent->d_name, !=, "..");
      g_test_message ("ino#%lld %s",
                      (long long) dent->d_ino, dent->d_name);

      if (prev != NULL)
        {
          g_assert_cmpstr (dent->d_name, >, prev);
          g_clear_pointer (&prev, g_free);
        }

      prev = g_strdup (dent->d_name);
    }

  g_test_message ("And again");
  _srt_dir_iter_rewind (&iter);

  while (TRUE)
    {
      gboolean ok = _srt_dir_iter_next_dent (&iter, &dent, NULL, &error);

      g_assert_no_error (error);
      g_assert_true (ok);

      if (dent == NULL)
        break;

      g_assert_cmpstr (dent->d_name, !=, ".");
      g_assert_cmpstr (dent->d_name, !=, "..");
      g_test_message ("ino#%lld %s",
                      (long long) dent->d_ino, dent->d_name);
    }
}

static void
test_environ_get_boolean (Fixture *f,
                          gconstpointer context)
{
  static const char * const envp[] =
  {
    "EMPTY=",
    "ONE=1",
    "ZERO=0",
    "WRONG=whatever",
    NULL
  };
  g_autoptr(GError) local_error = NULL;
  gboolean value;
  gboolean ok;
  gboolean def;

  for (def = FALSE; def <= TRUE; def++)
    {
      /* NULL environment => indeterminate (don't touch *value) */
      value = def;
      ok = _srt_environ_get_boolean (NULL, "anything", &value, &local_error);
      g_test_message ("NULL environment: %s, default %d -> result %d",
                      ok ? "success" : "error", def, value);
      g_assert_no_error (local_error);
      g_assert_true (ok);
      g_assert_cmpint (value, ==, def);

      /* Unset => indeterminate (don't touch *value) */
      value = def;
      ok = _srt_environ_get_boolean (envp, "UNSET", &value, &local_error);
      g_test_message ("unset UNSET: %s, default %d -> result %d",
                      ok ? "success" : "error", def, value);
      g_assert_no_error (local_error);
      g_assert_true (ok);
      g_assert_cmpint (value, ==, def);

      /* Set to empty value => false */
      value = def;
      ok = _srt_environ_get_boolean (envp, "EMPTY", &value, &local_error);
      g_test_message ("EMPTY='': %s, default %d -> result %d",
                      ok ? "success" : "error", def, value);
      g_assert_no_error (local_error);
      g_assert_true (ok);
      g_assert_cmpint (value, ==, FALSE);

      /* 0 => false */
      value = def;
      ok = _srt_environ_get_boolean (envp, "ZERO", &value, &local_error);
      g_test_message ("ZERO=0: %s, default %d -> result %d",
                      ok ? "success" : "error", def, value);
      g_assert_no_error (local_error);
      g_assert_true (ok);
      g_assert_cmpint (value, ==, FALSE);

      /* 1 => true */
      value = def;
      ok = _srt_environ_get_boolean (envp, "ONE", &value, &local_error);
      g_test_message ("ONE=1: %s, default %d -> result %d",
                      ok ? "success" : "error", def, value);
      g_assert_no_error (local_error);
      g_assert_true (ok);
      g_assert_cmpint (value, ==, TRUE);

      /* Some other value => indeterminate, with error set */
      value = def;
      ok = _srt_environ_get_boolean (envp, "WRONG", &value, &local_error);
      g_test_message ("WRONG=whatever: %s, default %d -> result %d",
                      ok ? "success" : "error", def, value);
      g_assert_error (local_error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE);
      g_clear_error (&local_error);
      g_assert_false (ok);
      g_assert_cmpint (value, ==, def);

      value = def;
      ok = _srt_environ_get_boolean (envp, "WRONG", &value, NULL);
      g_test_message ("WRONG=whatever: %s, default %d -> result %d",
                      ok ? "success" : "error", def, value);
      g_assert_false (ok);
      g_assert_cmpint (value, ==, def);
    }
}

typedef struct
{
  const char *name;
  SrtEscapeRuntimeFlags flags;
  /* Arbitrary size sufficient for our test data, increase as required */
  const char * const before[10];
  /* Assumed to be sorted in strcmp order */
  const char * const expected[10];
} EscapeSteamRuntimeTest;

static void
test_escape_steam_runtime (Fixture *f,
                           gconstpointer context)
{
  static const EscapeSteamRuntimeTest tests[] =
  {
      {
        .name = "with system variables, using host zenity",
        .before = {
          "STEAM_RUNTIME=/steam-runtime",
          "SYSTEM_PATH=/usr/local/bin:/usr/bin:/bin",
          "PATH=/usr/local/bin:/steam-runtime/amd64/bin:/usr/bin:/bin",
          "SYSTEM_LD_LIBRARY_PATH=/opt/lib",
          "LD_LIBRARY_PATH=/steam-runtime/lib/...:/opt/lib",
          "STEAM_ZENITY=/usr/bin/zenity",
          NULL
        },
        .expected = {
          "LD_LIBRARY_PATH=/opt/lib",
          "PATH=/usr/local/bin:/usr/bin:/bin",
          "STEAM_ZENITY=/usr/bin/zenity",
          "SYSTEM_LD_LIBRARY_PATH=/opt/lib",
          "SYSTEM_PATH=/usr/local/bin:/usr/bin:/bin",
          NULL
        },
      },
      {
        .name = "without system variables, using scout zenity",
        .before = {
          "STEAM_RUNTIME=/steam-runtime",
          "PATH=/usr/local/bin:/steam-runtime/amd64/bin:/usr/bin:/bin",
          "LD_LIBRARY_PATH=/steam-runtime/lib/...:/opt/lib",
          "STEAM_ZENITY=zenity",
          NULL
        },
        .expected = {
          "PATH=/usr/local/bin:/usr/bin:/bin",
          NULL
        },
      },
      {
        .name = "without system variables, explicitly using scout zenity",
        .before = {
          "STEAM_RUNTIME=/steam-runtime",
          "PATH=/usr/local/bin:/steam-runtime/amd64/bin:/usr/bin:/bin",
          "LD_LIBRARY_PATH=/steam-runtime/lib/...:/opt/lib",
          "STEAM_ZENITY=/steam-runtime/amd64/usr/bin/zenity",
          NULL
        },
        .expected = {
          "PATH=/usr/local/bin:/usr/bin:/bin",
          NULL
        },
      },
      {
        .name = "zenity explicitly disabled (like Steam Deck)",
        .before = {
          "STEAM_RUNTIME=/steam-runtime",
          "PATH=/usr/local/bin:/steam-runtime/amd64/bin:/usr/bin:/bin",
          "LD_LIBRARY_PATH=/steam-runtime/lib/...:/opt/lib",
          "STEAM_ZENITY=",
          NULL
        },
        .expected = {
          "PATH=/usr/local/bin:/usr/bin:/bin",
          "STEAM_ZENITY=",
          NULL
        },
      },
      {
        .name = "Steam Runtime path doesn't fully match PATH entries",
        .before = {
          "STEAM_RUNTIME=/steam-runtime",
          "PATH=/usr/local/bin:/steam-runtime-1/amd64/bin:/usr/bin:/bin",
          NULL
        },
        .expected = {
          "PATH=/usr/local/bin:/steam-runtime-1/amd64/bin:/usr/bin:/bin",
          NULL
        },
      },
      {
        .name = "not using Steam Runtime",
        .before = {
          "LD_LIBRARY_PATH=/whatever/lib/...:/opt/lib",
          "PATH=/usr/local/bin:/whatever/amd64/bin:/usr/bin:/bin",
          NULL
        },
        .expected = {
          "LD_LIBRARY_PATH=/whatever/lib/...:/opt/lib",
          "PATH=/usr/local/bin:/whatever/amd64/bin:/usr/bin:/bin",
          NULL
        },
      },
      {
        .name = "SYSTEM_PATH references Steam Runtime",
        .before = {
          "SYSTEM_PATH=/steam-runtime/bin:/usr/local/bin:/usr/bin:/bin",
          "STEAM_RUNTIME=/steam-runtime",
          NULL
        },
        .expected = {
          "PATH=/steam-runtime/bin:/usr/local/bin:/usr/bin:/bin",
          "SYSTEM_PATH=/steam-runtime/bin:/usr/local/bin:/usr/bin:/bin",
          NULL
        },
      },
      {
        .name = "SYSTEM_PATH references Steam Runtime but should be removed",
        .flags = SRT_ESCAPE_RUNTIME_FLAGS_CLEAN_PATH,
        .before = {
          "SYSTEM_PATH=/steam-runtime/bin:/usr/local/bin:/usr/bin:/bin",
          "STEAM_RUNTIME=/steam-runtime",
          NULL
        },
        .expected = {
          "PATH=/usr/local/bin:/usr/bin:/bin",
          "SYSTEM_PATH=/steam-runtime/bin:/usr/local/bin:/usr/bin:/bin",
          NULL
        },
      },
  };
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      g_auto(GStrv) env = _srt_strdupv (tests[i].before);
      gsize j;

      env = _srt_environ_escape_steam_runtime (env, tests[i].flags);

      g_test_message ("%s", tests[i].name);
      g_test_message ("Expected:");

      for (j = 0; tests[i].expected[j] != NULL; j++)
        g_test_message ("\t%s", tests[i].expected[j]);

      qsort (env, g_strv_length (env), sizeof (char *), _srt_indirect_strcmp0);
      g_test_message ("Got:");

      for (j = 0; env[j] != NULL; j++)
        g_test_message ("\t%s", env[j]);

      g_assert_cmpstrv (env, tests[i].expected);
    }
}

static void
test_evdev_bits (Fixture *f,
                 gconstpointer context)
{
  unsigned long words[] = { 0x00020001, 0x00080005 };

#ifdef __i386__
  g_assert_cmpuint (BITS_PER_LONG, ==, 32);
  g_assert_cmpuint (LONGS_FOR_BITS (1), ==, 1);
  g_assert_cmpuint (LONGS_FOR_BITS (32), ==, 1);
  g_assert_cmpuint (LONGS_FOR_BITS (33), ==, 2);
  g_assert_cmpuint (CHOOSE_BIT (0), ==, 0);
  g_assert_cmpuint (CHOOSE_BIT (31), ==, 31);
  g_assert_cmpuint (CHOOSE_BIT (32), ==, 0);
  g_assert_cmpuint (CHOOSE_BIT (33), ==, 1);
  g_assert_cmpuint (CHOOSE_BIT (63), ==, 31);
  g_assert_cmpuint (CHOOSE_BIT (64), ==, 0);
  g_assert_cmpuint (CHOOSE_LONG (0), ==, 0);
  g_assert_cmpuint (CHOOSE_LONG (31), ==, 0);
  g_assert_cmpuint (CHOOSE_LONG (32), ==, 1);
  g_assert_cmpuint (CHOOSE_LONG (33), ==, 1);
  g_assert_cmpuint (CHOOSE_LONG (63), ==, 1);
  g_assert_cmpuint (CHOOSE_LONG (64), ==, 2);
#elif defined(__LP64__)
  g_assert_cmpuint (BITS_PER_LONG, ==, 64);
  g_assert_cmpuint (LONGS_FOR_BITS (1), ==, 1);
  g_assert_cmpuint (LONGS_FOR_BITS (64), ==, 1);
  g_assert_cmpuint (LONGS_FOR_BITS (65), ==, 2);
  g_assert_cmpuint (CHOOSE_BIT (0), ==, 0);
  g_assert_cmpuint (CHOOSE_BIT (63), ==, 63);
  g_assert_cmpuint (CHOOSE_BIT (64), ==, 0);
  g_assert_cmpuint (CHOOSE_BIT (65), ==, 1);
  g_assert_cmpuint (CHOOSE_BIT (127), ==, 63);
  g_assert_cmpuint (CHOOSE_BIT (128), ==, 0);
  g_assert_cmpuint (CHOOSE_LONG (0), ==, 0);
  g_assert_cmpuint (CHOOSE_LONG (63), ==, 0);
  g_assert_cmpuint (CHOOSE_LONG (64), ==, 1);
  g_assert_cmpuint (CHOOSE_LONG (65), ==, 1);
  g_assert_cmpuint (CHOOSE_LONG (127), ==, 1);
  g_assert_cmpuint (CHOOSE_LONG (128), ==, 2);
#endif

  /* Among bits 0 to 15, only bit 0 (0x1) is set */
  g_assert_cmpuint (test_bit_checked (0, words, G_N_ELEMENTS (words)), ==, 1);
  g_assert_cmpuint (test_bit_checked (1, words, G_N_ELEMENTS (words)), ==, 0);
  g_assert_cmpuint (test_bit_checked (15, words, G_N_ELEMENTS (words)), ==, 0);

  /* Among bits 16 to 31, only bit 17 (0x2 << 16) is set */
  g_assert_cmpuint (test_bit_checked (16, words, G_N_ELEMENTS (words)), ==, 0);
  g_assert_cmpuint (test_bit_checked (17, words, G_N_ELEMENTS (words)), ==, 1);
  g_assert_cmpuint (test_bit_checked (18, words, G_N_ELEMENTS (words)), ==, 0);
  g_assert_cmpuint (test_bit_checked (31, words, G_N_ELEMENTS (words)), ==, 0);

#ifdef __i386__
  /* Among bits 32 to 63, only bits 32 (0x1 << 32), 34 (0x4 << 32)
   * and 51 (0x8 << 48) are set, and they don't count as set unless we
   * allow ourselves to look that far */
  g_assert_cmpuint (test_bit_checked (32, words, 1), ==, 0);
  g_assert_cmpuint (test_bit_checked (32, words, G_N_ELEMENTS (words)), ==, 1);
  g_assert_cmpuint (test_bit_checked (33, words, G_N_ELEMENTS (words)), ==, 0);
  g_assert_cmpuint (test_bit_checked (34, words, 1), ==, 0);
  g_assert_cmpuint (test_bit_checked (34, words, G_N_ELEMENTS (words)), ==, 1);
  g_assert_cmpuint (test_bit_checked (35, words, G_N_ELEMENTS (words)), ==, 0);
  g_assert_cmpuint (test_bit_checked (50, words, G_N_ELEMENTS (words)), ==, 0);
  g_assert_cmpuint (test_bit_checked (51, words, G_N_ELEMENTS (words)), ==, 1);
  g_assert_cmpuint (test_bit_checked (52, words, G_N_ELEMENTS (words)), ==, 0);
#elif defined(__LP64__)
  /* Among bits 64 to 127, only bits 64 (0x1 << 64), 66 (0x4 << 64)
   * and 83 (0x8 << 80) are set, and they don't count as set unless we
   * allow ourselves to look that far */
  g_assert_cmpuint (test_bit_checked (64, words, 1), ==, 0);
  g_assert_cmpuint (test_bit_checked (64, words, G_N_ELEMENTS (words)), ==, 1);
  g_assert_cmpuint (test_bit_checked (65, words, G_N_ELEMENTS (words)), ==, 0);
  g_assert_cmpuint (test_bit_checked (66, words, 1), ==, 0);
  g_assert_cmpuint (test_bit_checked (66, words, G_N_ELEMENTS (words)), ==, 1);
  g_assert_cmpuint (test_bit_checked (67, words, G_N_ELEMENTS (words)), ==, 0);
  g_assert_cmpuint (test_bit_checked (82, words, G_N_ELEMENTS (words)), ==, 0);
  g_assert_cmpuint (test_bit_checked (83, words, G_N_ELEMENTS (words)), ==, 1);
  g_assert_cmpuint (test_bit_checked (84, words, G_N_ELEMENTS (words)), ==, 0);
#endif
}

typedef struct
{
  const char *name;
  mode_t mode;
} File;

typedef struct
{
  const char *name;
  const char *target;
} Symlink;

typedef struct
{
  const char *path;
  SrtResolveFlags test;
  gboolean expected_result;
} InSysrootTest;

static void
test_file_in_sysroot (Fixture *f,
                      gconstpointer context)
{
  static const char * const prepare_dirs[] =
  {
    "dir1/dir2/dir3",
  };

  static const File prepare_files[] =
  {
    { "dir1/file1", 0600 },
    { "dir1/dir2/file2", 0600 },
    { "dir1/exec1", 0700 },
  };

  static const Symlink prepare_symlinks[] =
  {
    { "dir1/dir2/symlink_to_dir3", "dir3" },
    { "dir1/dir2/symlink_to_file2", "file2" },
    { "dir1/dir2/sym_to_sym_to_file2", "symlink_to_file2" },
    { "dir1/abs_symlink_to_run", "/run" },
  };

  static const InSysrootTest tests[] =
  {
    { "dir1", SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY, TRUE },
    { "dir1", SRT_RESOLVE_FLAGS_NONE, TRUE },
    { "/dir1", SRT_RESOLVE_FLAGS_NONE, TRUE },
    { "dir1/dir2", SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY, TRUE },
    /* These gets solved in sysroot, following symlinks too */
    { "dir1/dir2/symlink_to_dir3", SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY, TRUE },
    { "dir1/dir2/sym_to_sym_to_file2", SRT_RESOLVE_FLAGS_MUST_BE_REGULAR, TRUE },
    { "dir1/abs_symlink_to_run", SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY, FALSE },
    { "dir1/missing", SRT_RESOLVE_FLAGS_NONE, FALSE },
    { "dir1/file1", SRT_RESOLVE_FLAGS_MUST_BE_REGULAR, TRUE },
    {
      "dir1/file1",
      (SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY
       | SRT_RESOLVE_FLAGS_MUST_BE_EXECUTABLE),
      FALSE
    },
    { "dir1/exec1", SRT_RESOLVE_FLAGS_MUST_BE_REGULAR, TRUE },
    { "dir1/exec1", SRT_RESOLVE_FLAGS_MUST_BE_EXECUTABLE, TRUE },
  };

  g_autoptr(SrtSysroot) sysroot = NULL;
  g_autoptr(GError) error = NULL;
  g_auto(GLnxTmpDir) tmpdir = { FALSE };
  gsize i;

  glnx_mkdtemp ("test-XXXXXX", 0700, &tmpdir, &error);
  g_assert_no_error (error);

  for (i = 0; i < G_N_ELEMENTS (prepare_dirs); i++)
    {
      const char *it = prepare_dirs[i];

      glnx_shutil_mkdir_p_at (tmpdir.fd, it, 0700, NULL, &error);
      g_assert_no_error (error);
    }

  for (i = 0; i < G_N_ELEMENTS (prepare_files); i++)
    {
      const File *it = &prepare_files[i];

      glnx_autofd int fd = openat (tmpdir.fd, it->name, O_WRONLY|O_CREAT, it->mode);
      if (fd == -1)
        g_error ("openat %s: %s", it->name, g_strerror (errno));
    }

  for (i = 0; i < G_N_ELEMENTS (prepare_symlinks); i++)
    {
      const Symlink *it = &prepare_symlinks[i];

      if (symlinkat (it->target, tmpdir.fd, it->name) != 0)
        g_error ("symlinkat %s: %s", it->name, g_strerror (errno));
    }

  sysroot = _srt_sysroot_new (tmpdir.path, &error);
  g_assert_no_error (error);

  for (i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      const InSysrootTest *it = &tests[i];
      gboolean ok;

      ok = _srt_sysroot_test (sysroot, it->path, it->test, &error);

      if (it->expected_result)
        {
          g_assert_no_error (error);
          g_assert_true (ok);
        }
      else
        {
          g_assert_nonnull (error);
          g_assert_false (ok);
        }

      g_clear_error (&error);
    }
}

static void
test_get_path_after (Fixture *f,
                     gconstpointer context)
{
  static const struct
  {
    const char *str;
    const char *prefix;
    const char *expected;
  } tests[] =
  {
    { "/run/host/usr", "/run/host", "usr" },
    { "/run/host/usr", "/run/host/", "usr" },
    { "/run/host", "/run/host", "" },
    { "////run///host////usr", "//run//host", "usr" },
    { "////run///host////usr", "//run//host////", "usr" },
    { "/run/hostage", "/run/host", NULL },
    /* Any number of leading slashes is ignored, even zero */
    { "foo/bar", "/foo", "bar" },
    { "/foo/bar", "foo", "bar" },
  };
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      const char *str = tests[i].str;
      const char *prefix = tests[i].prefix;
      const char *expected = tests[i].expected;

      if (expected == NULL)
        g_test_message ("%s should not have path prefix %s",
                        str, prefix);
      else
        g_test_message ("%s should have path prefix %s followed by %s",
                        str, prefix, expected);

      g_assert_cmpstr (_srt_get_path_after (str, prefix), ==, expected);
    }
}

/*
 * Test _srt_filter_gameoverlayrenderer function.
 */
static void
filter_gameoverlayrenderer (Fixture *f,
                            gconstpointer context)
{
  const char *ld_preload1 = "/home/me/.local/share/Steam/ubuntu12_32/gameoverlayrenderer.so:"
                            "/home/me/.local/share/Steam/ubuntu12_64/gameoverlayrenderer.so";

  const char *ld_preload2 = ":/home/me/my/lib.so:"
                            "/home/me/.local/share/Steam/ubuntu12_32/gameoverlayrenderer.so:"
                            "/home/me/.local/share/Steam/ubuntu12_64/gameoverlayrenderer.so:"
                            "/home/me/my/second.lib.so:";

  const char *ld_preload3 = "/home/me/my/lib.so:"
                            "/home/me/my/second.lib.so";

  gchar *filtered_preload = NULL;

  filtered_preload = _srt_filter_gameoverlayrenderer (ld_preload1);
  g_assert_cmpstr (filtered_preload, ==, "");
  g_free (filtered_preload);

  filtered_preload = _srt_filter_gameoverlayrenderer (ld_preload2);
  g_assert_cmpstr (filtered_preload, ==,
                   ":/home/me/my/lib.so:/home/me/my/second.lib.so:");
  g_free (filtered_preload);

  filtered_preload = _srt_filter_gameoverlayrenderer (ld_preload3);
  g_assert_cmpstr (filtered_preload, ==,
                   "/home/me/my/lib.so:/home/me/my/second.lib.so");
  g_free (filtered_preload);
}

static void
test_gstring_replace (Fixture *f,
                      gconstpointer context)
{
  static const struct
  {
    const char *string;
    const char *original;
    const char *replacement;
    const char *expected;
  }
  tests[] =
  {
    { "/usr/$LIB/libMangoHud.so", "$LIB", "lib32", "/usr/lib32/libMangoHud.so" },
    { "food for foals", "o", "", "fd fr fals" },
    { "aaa", "a", "aaa", "aaaaaaaaa" },
    { "aaa", "a", "", "" },
    { "aaa", "aa", "bb", "bba" },
  };
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      g_autoptr(GString) buffer = g_string_new (tests[i].string);

      g_string_replace (buffer, tests[i].original, tests[i].replacement, 0);
      g_assert_cmpstr (buffer->str, ==, tests[i].expected);
      g_assert_cmpuint (buffer->len, ==, strlen (tests[i].expected));
      g_assert_cmpuint (buffer->allocated_len, >=, strlen (tests[i].expected) + 1);
    }
}

static void
test_hash_iter (Fixture *f,
                gconstpointer context)
{
  g_autoptr(GHashTable) table = g_hash_table_new_full (g_str_hash,
                                                       g_str_equal,
                                                       g_free,
                                                       g_free);
  g_auto(SrtHashTableIter) iter = SRT_HASH_TABLE_ITER_CLEARED;
  const char *prev = NULL;
  const char *k;
  const char *v;

  g_hash_table_replace (table, g_strdup ("1"), g_strdup ("one"));
  g_hash_table_replace (table, g_strdup ("2"), g_strdup ("two"));
  g_hash_table_replace (table, g_strdup ("3"), g_strdup ("three"));

  _srt_hash_table_iter_init (&iter, table);
  _srt_hash_table_iter_clear (&iter);

  _srt_hash_table_iter_init_sorted (&iter, table, NULL);
  _srt_hash_table_iter_clear (&iter);

  _srt_hash_table_iter_init_sorted (&iter, table, _srt_generic_strcmp0);
  _srt_hash_table_iter_clear (&iter);

  g_test_message ("Iterating in arbitrary order");
  _srt_hash_table_iter_init (&iter, table);

  while (_srt_hash_table_iter_next (&iter, &k, &v))
    g_test_message ("%s -> %s", k, v);

  _srt_hash_table_iter_clear (&iter);

  g_test_message ("Iterating in arbitrary order, keys only");
  _srt_hash_table_iter_init_sorted (&iter, table, NULL);

  while (_srt_hash_table_iter_next (&iter, &k, NULL))
    g_test_message ("%s -> (value)", k);

  _srt_hash_table_iter_clear (&iter);

  g_test_message ("Iterating in arbitrary order, values only");
  _srt_hash_table_iter_init_sorted (&iter, table, NULL);

  while (_srt_hash_table_iter_next (&iter, NULL, &v))
    g_test_message ("(key) -> %s", v);

  _srt_hash_table_iter_clear (&iter);

  g_test_message ("Iterating in sorted order");
  prev = NULL;
  _srt_hash_table_iter_init_sorted (&iter, table, _srt_generic_strcmp0);

  while (_srt_hash_table_iter_next (&iter, &k, &v))
    {
      g_test_message ("%s -> %s", k, v);

      if (prev != NULL)
        g_assert_cmpstr (k, >, prev);

      prev = k;
    }

  _srt_hash_table_iter_clear (&iter);

  g_test_message ("Iterating in sorted order, keys only");
  prev = NULL;
  _srt_hash_table_iter_init_sorted (&iter, table, _srt_generic_strcmp0);

  while (_srt_hash_table_iter_next (&iter, &k, NULL))
    {
      g_test_message ("%s -> (value)", k);

      if (prev != NULL)
        g_assert_cmpstr (k, >, prev);

      prev = k;
    }

  _srt_hash_table_iter_clear (&iter);

  g_test_message ("Iterating in sorted order, values only");
  _srt_hash_table_iter_init_sorted (&iter, table, _srt_generic_strcmp0);

  while (_srt_hash_table_iter_next (&iter, NULL, &v))
    g_test_message ("(key) -> %s", v);
}

static void
test_is_identifier (Fixture *f,
                    gconstpointer context)
{
  g_assert_false (_srt_is_identifier (""));
  g_assert_true (_srt_is_identifier ("_"));
  g_assert_true (_srt_is_identifier ("a"));
  g_assert_true (_srt_is_identifier ("A"));
  g_assert_false (_srt_is_identifier ("9"));
  g_assert_true (_srt_is_identifier ("if"));
  g_assert_false (_srt_is_identifier ("0install"));
  g_assert_true (_srt_is_identifier ("PATH"));
  g_assert_true (_srt_is_identifier ("SDL_JOYSTICK_HIDAPI_PS4"));
  g_assert_true (_srt_is_identifier ("__GLX_VENDOR_LIBRARY_NAME"));
}

typedef enum
{
  LOGGING_TEST_BASIC = 0,
  LOGGING_TEST_FLAGS,
  LOGGING_TEST_FLAGS_OLD,
  LOGGING_TEST_TO_JOURNAL,
  LOGGING_TEST_TO_JOURNAL_OLD,
  LOGGING_TEST_NOT_TO_JOURNAL,
  LOGGING_TEST_DIFFABLE,
  LOGGING_TEST_DIFFABLE_PID,
  LOGGING_TEST_NO_AUTO_JOURNAL,
  LOGGING_TEST_AUTO_JOURNAL,
  N_LOGGING_TESTS
} LoggingTest;

typedef struct
{
  gboolean close_stdin;
  gboolean close_stdout;
  gboolean close_stderr;
} ChildSetupData;

static void
child_setup (gpointer user_data)
{
  ChildSetupData *data = user_data;

  if (data == NULL)
    _exit (1);

  if (data->close_stdin)
    close (STDIN_FILENO);

  if (data->close_stdout)
    close (STDOUT_FILENO);

  if (data->close_stderr)
    close (STDERR_FILENO);
}

static void
test_logging (Fixture *f,
              gconstpointer context)
{
  int i;
  g_auto(AutoLibraryHandle) handle = NULL;
  type_of_sd_journal_stream_fd sym = NULL;
  glnx_autofd int journal_fd = -1;

  handle = dlopen ("libsystemd.so.0", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);

  if (handle != NULL)
    {
      sym = (type_of_sd_journal_stream_fd) dlsym (handle, "sd_journal_stream_fd");

      if (sym != NULL)
        journal_fd = sym ("srt-utils-test", LOG_DEBUG, FALSE);
    }

  for (i = LOGGING_TEST_BASIC; i < N_LOGGING_TESTS; i++)
    {
      g_autoptr(GError) error = NULL;
      g_auto(GStrv) envp = g_get_environ ();
      GSpawnFlags spawn_flags = G_SPAWN_DEFAULT;
      ChildSetupData child_setup_data = {};
      g_autofree gchar *out = NULL;
      g_autofree gchar *err = NULL;
      int wait_status = -1;
      const char *argv[10] = { NULL };
      const char *title;
      int argc = 0;

      envp = g_environ_unsetenv (envp, "SRT_LOG");
      envp = g_environ_unsetenv (envp, "G_MESSAGES_DEBUG");
      envp = g_environ_unsetenv (envp, "SRT_LOG_TO_JOURNAL");
      envp = g_environ_unsetenv (envp, "PRESSURE_VESSEL_LOG_INFO");
      envp = g_environ_unsetenv (envp, "PRESSURE_VESSEL_LOG_WITH_TIMESTAMP");
      argv[argc++] = f->logging_helper;

      switch (i)
        {
          case LOGGING_TEST_BASIC:
          default:
            title = "Basic logging test";
            break;

          case LOGGING_TEST_FLAGS:
            envp = g_environ_setenv (envp, "SRT_LOG",
                                     "debug,info,timestamp,timing,journal",
                                     TRUE);
            argv[argc++] = "--divert-stdout";
            argv[argc++] = "--keep-prgname";
            child_setup_data.close_stdin = TRUE;
            title = "Various flags set";
            break;

          case LOGGING_TEST_FLAGS_OLD:
            envp = g_environ_setenv (envp, "PRESSURE_VESSEL_LOG_INFO", "1", TRUE);
            envp = g_environ_setenv (envp, "PRESSURE_VESSEL_LOG_WITH_TIMESTAMP", "1", TRUE);
            argv[argc++] = "--allow-journal";
            title = "Old environment variables set";
            break;

          case LOGGING_TEST_TO_JOURNAL:
            envp = g_environ_setenv (envp, "SRT_LOG", "journal", TRUE);
            argv[argc++] = "--allow-journal";
            argv[argc++] = "--divert-stdout";
            title = "Diverting to Journal";
            break;

          case LOGGING_TEST_TO_JOURNAL_OLD:
            envp = g_environ_setenv (envp, "SRT_LOG_TO_JOURNAL", "1", TRUE);
            argv[argc++] = "--allow-journal";
            title = "Diverting to Journal (old environment variable)";
            break;

          case LOGGING_TEST_NOT_TO_JOURNAL:
            envp = g_environ_setenv (envp, "SRT_LOG", "journal", TRUE);
            envp = g_environ_setenv (envp, "SRT_LOG_TO_JOURNAL", "0", TRUE);
            argv[argc++] = "--allow-journal";
            title = "Not diverting to Journal because SRT_LOG_TO_JOURNAL=0";
            break;

          case LOGGING_TEST_DIFFABLE:
            envp = g_environ_setenv (envp, "SRT_LOG", "diffable", TRUE);
            title = "Diffable";
            break;

          case LOGGING_TEST_DIFFABLE_PID:
            envp = g_environ_setenv (envp, "SRT_LOG", "diffable,pid", TRUE);
            title = "Diffable overridden by pid";
            break;

          case LOGGING_TEST_NO_AUTO_JOURNAL:
            title = "Don't automatically redirect to Journal";
            child_setup_data.close_stdout = TRUE;
            spawn_flags |= G_SPAWN_STDERR_TO_DEV_NULL;
            break;

          case LOGGING_TEST_AUTO_JOURNAL:
            argv[argc++] = "--allow-journal";
            title = "Automatically redirect to Journal";
            child_setup_data.close_stderr = TRUE;
            spawn_flags |= G_SPAWN_STDOUT_TO_DEV_NULL;
            break;
        }

      g_test_message ("Starting test: %s", title);
      argv[argc++] = title;
      argv[argc++] = NULL;
      g_assert_cmpint (argc, <=, G_N_ELEMENTS (argv));

      g_spawn_sync (NULL, (gchar **) argv, envp, spawn_flags,
                    child_setup, &child_setup_data,
                    (spawn_flags & G_SPAWN_STDOUT_TO_DEV_NULL) ? NULL : &out,
                    (spawn_flags & G_SPAWN_STDERR_TO_DEV_NULL) ? NULL : &err,
                    &wait_status, &error);
      g_assert_no_error (error);

      if (out != NULL)
        g_test_message ("stdout: '''%s%s'''", out[0] == '\0' ? "" : "\\\n", out);

      if (err != NULL)
        g_test_message ("stderr: '''%s%s'''", err[0] == '\0' ? "" : "\\\n", err);

      switch (i)
        {
          case LOGGING_TEST_BASIC:
            g_assert_nonnull (strstr (err, "srt-tests-logging-helper["));
            g_assert_nonnull (strstr (err, "]: N: Basic logging test"));
            g_assert_nonnull (strstr (err, "stderr while running"));
            g_assert_nonnull (strstr (err, "]: N: notice message"));
            g_assert_nonnull (strstr (err, "original stderr"));

            /* We didn't divert stderr */
            g_assert_cmpstr (out, ==, "stdout while running\noriginal stdout\n");
            break;

          case LOGGING_TEST_FLAGS:
            /* We're using timestamps and didn't reset the g_set_prgname() */
            g_assert_nonnull (strstr (err, ": logging-helper["));
            /* We enabled profiling */
            g_assert_nonnull (strstr (err, "]: N: Enabled profiling"));
            g_assert_nonnull (strstr (err, "]: N: Various flags set"));
            /* We enabled debug and info messages */
            g_assert_nonnull (strstr (err, "]: D: debug message"));
            g_assert_nonnull (strstr (err, "]: I: info message"));
            g_assert_nonnull (strstr (err, "]: N: notice message"));
            /* SRT_LOG=journal didn't take effect because we didn't pass in
             * the OPTIONALLY_JOURNAL flag */

            /* We diverted stdout away and back */
            g_assert_nonnull (strstr (err, "stdout while running"));
            g_assert_cmpstr (out, ==, "original stdout\n");
            break;

          case LOGGING_TEST_FLAGS_OLD:
            /* We're using timestamps */
            g_assert_nonnull (strstr (err, ": srt-tests-logging-helper["));
            g_assert_nonnull (strstr (err, "]: N: Old environment variables set"));
            /* We enabled info messages */
            g_assert_nonnull (strstr (err, "]: I: info message"));
            g_assert_nonnull (strstr (err, "]: N: notice message"));
            break;

          case LOGGING_TEST_TO_JOURNAL:
            /* SRT_LOG=journal sends logging to the Journal if possible.
             * If the Journal isn't available, it'll fall back to stderr. */
            if (journal_fd >= 0)
              g_assert_null (strstr (err, "notice message"));

            /* DIVERT_STDOUT|JOURNAL redirects stdout to the Journal. */
            if (journal_fd >= 0)
              g_assert_null (strstr (err, "stdout while running"));

            g_assert_cmpstr (out, ==, "original stdout\n");
            break;

          case LOGGING_TEST_TO_JOURNAL_OLD:
            /* SRT_LOG_TO_JOURNAL=1 sends logging to the Journal if possible.
             * If the Journal isn't available, it'll fall back to stderr. */
            if (journal_fd >= 0)
              g_assert_null (strstr (err, "notice message"));

            /* JOURNAL without DIVERT_STDOUT doesn't redirect stdout. */
            g_assert_cmpstr (out, ==, "stdout while running\noriginal stdout\n");
            break;

          case LOGGING_TEST_NOT_TO_JOURNAL:
            /* SRT_LOG_TO_JOURNAL=0 "wins" vs. SRT_LOG=journal */
            g_assert_nonnull (strstr (err, "notice message"));
            g_assert_cmpstr (out, ==, "stdout while running\noriginal stdout\n");
            break;

          case LOGGING_TEST_DIFFABLE:
            /* SRT_LOG=diffable suppresses process IDs... */
            g_assert_nonnull (strstr (err, "srt-tests-logging-helper[0]: N: Diffable"));
            break;

          case LOGGING_TEST_DIFFABLE_PID:
            /* ... unless you specifically ask for them */
            g_assert_null (strstr (err, "[0]"));
            break;

          case LOGGING_TEST_AUTO_JOURNAL:
          case LOGGING_TEST_NO_AUTO_JOURNAL:
          default:
            /* We can't make any useful assertions here because we're not
             * capturing the output, so these have to be a manual test.
             * You should see "N: Automatically redirect to Journal"
             * in the Journal. You should not see
             * "N: Don't automatically redirect to Journal". */
            break;
        }
    }
}

static void
test_recursive_list (Fixture *f,
                     gconstpointer context)
{
  g_auto(GStrv) listing = NULL;
  const char * const *const_listing;
  g_autofree gchar *target = NULL;
  gsize i;

  if (G_LIKELY (!g_file_test ("/nonexistent", G_FILE_TEST_EXISTS)))
    {
      listing = _srt_recursive_list_content ("/", -1, "/nonexistent", -1,
                                             _srt_peek_environ_nonnull (), NULL);
      g_assert_nonnull (listing);
      g_assert_null (listing[0]);
      g_strfreev (g_steal_pointer (&listing));
    }
  else
    {
      /* Assume this is an OS bug, but if it somehow happens on real systems
       * we can reduce this to a g_test_skip(). */
      g_warning ("/nonexistent exists! Check your system");
    }

  if (G_LIKELY (g_file_test ("/dev/null", G_FILE_TEST_EXISTS)))
    {
      listing = _srt_recursive_list_content ("/", -1, "/dev", -1,
                                             _srt_peek_environ_nonnull (), NULL);
      g_assert_nonnull (listing);
      g_assert_nonnull (listing[0]);
      const_listing = (const char * const *) listing;

      for (i = 0; listing[i] != NULL; i++)
        g_test_message ("%s", listing[i]);

      g_assert_true (g_strv_contains (const_listing, "/dev/null"));

      if (G_LIKELY (g_file_test ("/dev/pts", G_FILE_TEST_IS_DIR)
                    && !g_file_test ("/dev/pts", G_FILE_TEST_IS_SYMLINK)))
        {
          g_assert_true (g_strv_contains (const_listing, "/dev/pts/"));
        }
      else
        {
          /* This could conceivably be false in some containers.
           * Mark the test as skipped but intentionally don't early-return
           * here: we can still check for /dev/stderr. */
          g_test_skip ("/dev/pts doesn't exist or isn't a directory");
        }

      target = glnx_readlinkat_malloc (AT_FDCWD, "/dev/stderr", NULL, NULL);

      if (G_LIKELY (target != NULL))
        {
          g_autofree gchar *expected = g_strdup_printf ("/dev/stderr -> %s", target);

          g_assert_true (g_strv_contains (const_listing, expected));
        }
      else
        {
          /* This could conceivably be false in some containers.
           * Again, intentionally not early-returning here. */
          g_test_skip ("/dev/stderr isn't a symlink");
        }
    }
  else
    {
      g_warning ("/dev/null doesn't exist! Check your system");
    }
}

G_STATIC_ASSERT (FD_SETSIZE == 1024);

static void
test_rlimit (Fixture *f,
             gconstpointer context)
{
  struct rlimit original;
  struct rlimit adjusted;

  if (getrlimit (RLIMIT_NOFILE, &original) < 0)
    {
      int saved_errno = errno;

      g_test_skip_printf ("getrlimit: %s", g_strerror (saved_errno));
      return;
    }

  if (original.rlim_max < 2048)
    {
      g_test_skip ("RLIMIT_NOFILE rlim_max is too small");
      return;
    }

  adjusted = original;
  adjusted.rlim_cur = 2048;
  g_assert_no_errno (setrlimit (RLIMIT_NOFILE, &adjusted));
  g_assert_cmpint (_srt_set_compatible_resource_limits (0), ==, 0);
  g_assert_no_errno (getrlimit (RLIMIT_NOFILE, &adjusted));
  g_assert_cmpint (adjusted.rlim_cur, ==, 1024);
  g_assert_cmpint (adjusted.rlim_max, ==, original.rlim_max);

  adjusted = original;
  adjusted.rlim_cur = 512;
  g_assert_no_errno (setrlimit (RLIMIT_NOFILE, &adjusted));
  g_assert_cmpint (_srt_set_compatible_resource_limits (getpid ()), ==, 0);
  g_assert_no_errno (getrlimit (RLIMIT_NOFILE, &adjusted));
  g_assert_cmpint (adjusted.rlim_cur, ==, 1024);
  g_assert_cmpint (adjusted.rlim_max, ==, original.rlim_max);

  adjusted = original;
  adjusted.rlim_cur = 1024;
  g_assert_no_errno (setrlimit (RLIMIT_NOFILE, &adjusted));
  g_assert_cmpint (_srt_set_compatible_resource_limits (0), ==, 0);
  g_assert_no_errno (getrlimit (RLIMIT_NOFILE, &adjusted));
  g_assert_cmpint (adjusted.rlim_cur, ==, 1024);
  g_assert_cmpint (adjusted.rlim_max, ==, original.rlim_max);
}

static void
test_same_file (Fixture *f,
                gconstpointer context)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *temp = NULL;
  g_autofree gchar *hard_link_from = NULL;
  g_autofree gchar *hard_link_to = NULL;
  g_autofree gchar *symlink_to_dev_null = NULL;

  g_assert_true (_srt_is_same_file ("/dev/null", "/dev/null"));
  g_assert_true (_srt_is_same_file ("/nonexistent", "/nonexistent"));
  g_assert_false (_srt_is_same_file ("/dev/null", "/dev/zero"));
  g_assert_false (_srt_is_same_file ("/dev/null", "/nonexistent"));
  g_assert_false (_srt_is_same_file ("/nonexistent", "/dev/null"));
  g_assert_false (_srt_is_same_file ("/nonexistent", "/nonexistent/also"));

  temp = g_dir_make_tmp (NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (temp);

  hard_link_from = g_build_filename (temp, "hard-link-from", NULL);
  hard_link_to = g_build_filename (temp, "hard-link-to", NULL);
  symlink_to_dev_null = g_build_filename (temp, "symlink", NULL);

  g_file_set_contents (hard_link_from, "hello", -1, &error);
  g_assert_no_error (error);

  if (link (hard_link_from, hard_link_to) != 0)
    g_error ("Could not create hard link \"%s\" -> /dev/null: %s",
             symlink_to_dev_null, g_strerror (errno));

  g_assert_true (_srt_is_same_file (hard_link_from, hard_link_to));
  g_assert_false (_srt_is_same_file (hard_link_from, "/dev/null"));

  if (symlink ("/dev/null", symlink_to_dev_null) != 0)
    g_error ("Could not create symlink \"%s\" -> /dev/null: %s",
             symlink_to_dev_null, g_strerror (errno));

  g_assert_true (_srt_is_same_file (symlink_to_dev_null, "/dev/null"));
  g_assert_false (_srt_is_same_file (symlink_to_dev_null, "/dev/zero"));

  glnx_shutil_rm_rf_at (-1, temp, NULL, &error);
  g_assert_no_error (error);
}

static void
test_str_is_integer (Fixture *f,
                     gconstpointer context)
{
  g_assert_false (_srt_str_is_integer (""));
  g_assert_false (_srt_str_is_integer ("no"));
  g_assert_true (_srt_str_is_integer ("1"));
  g_assert_true (_srt_str_is_integer ("123456789012345678901234567890"));
  g_assert_false (_srt_str_is_integer ("1.23"));
  g_assert_false (_srt_str_is_integer ("x23"));
  g_assert_false (_srt_str_is_integer ("23a"));
}

static const struct
{
  const char *str;
  ssize_t len;
  const char *suffix;
  gboolean expected;
} string_ends[] =
{
  { "", 0, "", TRUE },
  { "bar", -1, "bar", TRUE },
  { "foobar", -1, "bar", TRUE },
  { "foobar", -1, "BAR", FALSE },
  { "foo\0bar", 7, "ar", TRUE },
  { "foo\0bar", 7, "aa", FALSE },
};

static void
test_string_ends_with (Fixture *f,
                       gconstpointer context)
{
  size_t i;

  for (i = 0; i < G_N_ELEMENTS (string_ends); i++)
    {
      g_autoptr(GString) str = NULL;
      gboolean result;

      str = g_string_new_len (string_ends[i].str, string_ends[i].len);
      result = _srt_string_ends_with (str, string_ends[i].suffix);
      g_test_message ("#%zu \"%s\" ends with \"%s\": %c, expected: %c",
                      i,
                      string_ends[i].len < 0 ? string_ends[i].str : "<not null-terminated>",
                      string_ends[i].suffix,
                      result ? 'y' : 'n',
                      string_ends[i].expected ? 'y' : 'n');
      g_assert_cmpint (result, ==, string_ends[i].expected);
    }
}

static void
test_string_read_fd_until_eof (Fixture *f,
                               gconstpointer context)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GString) buf = NULL;
  g_auto(SrtPipe) p = _SRT_PIPE_INIT;
  glnx_autofd int unconnected_socket = -1;
  gboolean result;

  _srt_pipe_open (&p, &error);
  g_assert_no_error (error);

  g_assert_no_errno (glnx_loop_write (p.fds[_SRT_PIPE_END_WRITE], "bar\0baz", 7));
  glnx_close_fd (&p.fds[_SRT_PIPE_END_WRITE]);

  buf = g_string_new ("foo");
  result = _srt_string_read_fd_until_eof (buf, p.fds[_SRT_PIPE_END_READ], &error);
  g_assert_no_error (error);
  g_assert_true (result);
  g_assert_cmpuint (buf->len, ==, strlen ("foobar0baz"));
  g_assert_cmpstr (buf->str, ==, "foobar");
  g_assert_cmpstr (buf->str + strlen ("foobar0"), ==, "baz");

  result = _srt_string_read_fd_until_eof (buf, p.fds[_SRT_PIPE_END_READ], &error);
  g_assert_no_error (error);
  g_assert_true (result);
  g_assert_cmpuint (buf->len, ==, strlen ("foobar0baz"));

  unconnected_socket = socket (AF_INET, SOCK_STREAM, 0);
  result = _srt_string_read_fd_until_eof (buf, unconnected_socket, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED);
  g_assert_false (result);
  g_assert_cmpuint (buf->len, ==, strlen ("foobar0baz"));
  g_clear_error (&error);

  if (g_test_undefined ())
    {
      result = _srt_string_read_fd_until_eof (buf, -1, &error);
      g_assert_error (error, G_IO_ERROR, g_io_error_from_errno (EBADF));
      g_assert_false (result);
      g_assert_cmpuint (buf->len, ==, strlen ("foobar0baz"));
      g_clear_error (&error);
    }
}

struct
{
  const char *input;
  int expected;
} syslog_level_tests[] =
{
  { "emerg", LOG_EMERG },
  { "EmErGeNcY", LOG_EMERG },
  { "ALERT", LOG_ALERT },
  { "crit", LOG_CRIT },
  { "critical", LOG_CRIT },
  { "err", LOG_ERR },
  { "error", LOG_ERR },
  { "e", LOG_ERR },
  { "warning", LOG_WARNING },
  { "warn", LOG_WARNING },
  { "W", LOG_WARNING },
  { "notice", LOG_NOTICE },
  { "n", LOG_NOTICE },
  { "info", LOG_INFO },
  { "i", LOG_INFO },
  { "debug", LOG_DEBUG },
  { "d", LOG_DEBUG },
  { "-1", -1 },
  { "0", LOG_EMERG },
  { "1", LOG_ALERT },
  { "2", LOG_CRIT },
  { "3", LOG_ERR },
  { "4", LOG_WARNING },
  { "5", LOG_NOTICE },
  { "6", LOG_INFO },
  { "7", LOG_DEBUG },
  { "8", -1 },
  { "9", -1 },
  { "666", -1 },
  { "errata", -1 },
  { "", -1 },
};

static void
test_syslog_level_parse (Fixture *f,
                         gconstpointer context)
{
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (syslog_level_tests); i++)
    {
      g_autoptr(GError) local_error = NULL;
      const char *input = syslog_level_tests[i].input;
      int expected = syslog_level_tests[i].expected;
      int actual = -2;
      gboolean ok;

      ok = _srt_syslog_level_parse (input, &actual, &local_error);

      if (ok)
        g_test_message ("parse syslog level \"%s\" => %d", input, actual);
      else
        g_test_message ("parse syslog level \"%s\" => failed: %s",
                        input, local_error->message);

      if (expected < 0)
        {
          g_assert_nonnull (local_error);
          g_assert_cmpint (actual, ==, -2);   /* untouched */
          g_assert_false (ok);
        }
      else
        {
          g_assert_no_error (local_error);
          g_assert_cmpint (actual, ==, expected);
          g_assert_true (ok);
        }
    }
}

static const char uevent[] =
"DRIVER=lenovo\n"
"HID_ID=0003:000017EF:00006009\n"
"HID_NAME=Lite-On Technology Corp. ThinkPad USB Keyboard with TrackPoint\n"
"HID_PHYS=usb-0000:00:14.0-2/input0\n"
"HID_UNIQ=\n"
"MODALIAS=hid:b0003g0000v000017EFp00006009\n";

static struct
{
  const char *key;
  const char *value;
} uevent_parsed[] =
{
  { "DRIVER", "lenovo" },
  { "HID_ID", "0003:000017EF:00006009" },
  { "HID_NAME", "Lite-On Technology Corp. ThinkPad USB Keyboard with TrackPoint" },
  { "HID_PHYS", "usb-0000:00:14.0-2/input0" },
  { "HID_UNIQ", "" },
  { "MODALIAS", "hid:b0003g0000v000017EFp00006009" }
};

static const char no_newline[] = "DRIVER=lenovo";

static void
test_uevent_field (Fixture *f,
                   gconstpointer context)
{
  gsize i;

  g_assert_false (_srt_input_device_uevent_field_equals (no_newline, "DRIVER", ""));
  g_assert_false (_srt_input_device_uevent_field_equals (no_newline, "DRIVER", "lenov"));
  g_assert_true (_srt_input_device_uevent_field_equals (no_newline, "DRIVER", "lenovo"));
  g_assert_false (_srt_input_device_uevent_field_equals (no_newline, "DRIVER", "lenovoo"));

  g_assert_false (_srt_input_device_uevent_field_equals (uevent, "DRIVER", "lenov"));
  g_assert_false (_srt_input_device_uevent_field_equals (uevent, "DRIVER", "lenovoo"));
  g_assert_false (_srt_input_device_uevent_field_equals (uevent, "HID_ID", "0003:000017EF:0000600"));
  g_assert_false (_srt_input_device_uevent_field_equals (uevent, "HID_ID", "0003:000017EF:000060099"));
  g_assert_false (_srt_input_device_uevent_field_equals (uevent, "HID_UNIQ", "x"));
  g_assert_false (_srt_input_device_uevent_field_equals (uevent, "MODALIAS", "nope"));
  g_assert_false (_srt_input_device_uevent_field_equals (uevent, "NOPE", ""));
  g_assert_false (_srt_input_device_uevent_field_equals (uevent, "NOPE", "nope"));

  for (i = 0; i < G_N_ELEMENTS (uevent_parsed); i++)
    {
      g_autofree gchar *v = NULL;

      v = _srt_input_device_uevent_field (uevent, uevent_parsed[i].key);
      g_assert_cmpstr (v, ==, uevent_parsed[i].value);
      g_assert_true (_srt_input_device_uevent_field_equals (uevent,
                                                            uevent_parsed[i].key,
                                                            uevent_parsed[i].value));
    }

  g_assert_null (_srt_input_device_uevent_field (uevent, "NOPE"));
}

int
main (int argc,
      char **argv)
{
  argv0 = argv[0];
  _srt_setenv_disable_gio_modules ();

  _srt_tests_init (&argc, &argv, NULL);
  g_test_add ("/utils/avoid-gvfs", Fixture, NULL, setup, test_avoid_gvfs, teardown);
  g_test_add ("/utils/bits-set", Fixture, NULL,
              setup, test_bits_set, teardown);
  g_test_add ("/utils/compat-flags", Fixture, NULL,
              setup, test_compat_flags, teardown);
  g_test_add ("/utils/describe-fd", Fixture, NULL,
              setup, test_describe_fd, teardown);
  g_test_add ("/utils/dir-iter", Fixture, NULL,
              setup, test_dir_iter, teardown);
  g_test_add ("/utils/escape_steam_runtime", Fixture, NULL,
              setup, test_escape_steam_runtime, teardown);
  g_test_add ("/utils/environ_get_boolean", Fixture, NULL,
              setup, test_environ_get_boolean, teardown);
  g_test_add ("/utils/evdev-bits", Fixture, NULL,
              setup, test_evdev_bits, teardown);
  g_test_add ("/utils/test-file-in-sysroot", Fixture, NULL,
              setup, test_file_in_sysroot, teardown);
  g_test_add ("/utils/filter_gameoverlayrenderer", Fixture, NULL, setup,
              filter_gameoverlayrenderer, teardown);
  g_test_add ("/utils/get-path-after", Fixture, NULL,
              setup, test_get_path_after, teardown);
  g_test_add ("/utils/gstring-replace", Fixture, NULL,
              setup, test_gstring_replace, teardown);
  g_test_add ("/utils/hash-iter", Fixture, NULL,
              setup, test_hash_iter, teardown);
  g_test_add ("/utils/is-identifier", Fixture, NULL,
              setup, test_is_identifier, teardown);
  g_test_add ("/utils/logging", Fixture, NULL,
              setup, test_logging, teardown);
  g_test_add ("/utils/recursive_list", Fixture, NULL,
              setup, test_recursive_list, teardown);
  g_test_add ("/utils/rlimit", Fixture, NULL,
              setup, test_rlimit, teardown);
  g_test_add ("/utils/same-file", Fixture, NULL,
              setup, test_same_file, teardown);
  g_test_add ("/utils/str_is_integer", Fixture, NULL,
              setup, test_str_is_integer, teardown);
  g_test_add ("/utils/string_ends_with", Fixture, NULL,
              setup, test_string_ends_with, teardown);
  g_test_add ("/utils/string_read_fd_until_eof", Fixture, NULL,
              setup, test_string_read_fd_until_eof, teardown);
  g_test_add ("/utils/syslog_level_parse", Fixture, NULL,
              setup, test_syslog_level_parse, teardown);
  g_test_add ("/utils/uevent-field", Fixture, NULL,
              setup, test_uevent_field, teardown);

  return g_test_run ();
}
