/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "steam-runtime-tools/libc-utils-internal.h"

#include "steam-runtime-tools/glib-backports-internal.h"
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
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
}

static void
test_autofclose (Fixture *f,
                 gconstpointer context)
{
  autofclose FILE *devnull = NULL;

  g_assert_no_errno ((devnull = fopen ("/dev/null", "r")) ? 0 : -1);
  g_assert_nonnull (devnull);
}

static void
test_n_elements (Fixture *f,
                 gconstpointer context)
{
  G_GNUC_UNUSED const int numbers[] = { 1, 2, 3, 4 };

  G_STATIC_ASSERT (N_ELEMENTS (numbers) == 4);
}

static void
test_new0 (Fixture *f,
           gconstpointer context)
{
  autofree int *data = new0 (int);

  g_assert_cmpint (data[0], ==, 0);
}

static void
test_steal_fd (Fixture *f,
               gconstpointer context)
{
  glnx_autofd int fd = -1;
  glnx_autofd int stolen = -1;

  g_assert_no_errno ((fd = open ("/dev/null", O_CLOEXEC | O_RDONLY)));
  stolen = steal_fd (&fd);
  g_assert_cmpint (stolen, >=, 0);
  g_assert_cmpint (fd, ==, -1);
}

static void
test_steal_pointer (Fixture *f,
                    gconstpointer context)
{
  autofree char *pointer = g_strdup ("hello");
  autofree char *stolen = NULL;

  stolen = steal_pointer (&pointer);
  g_assert_null (pointer);
  g_assert_cmpstr (stolen, ==, "hello");
}

static void
test_xasprintf (Fixture *f,
                gconstpointer context)
{
  autofree char *hello = NULL;

  xasprintf (&hello, "%s%s", "he", "llo");
  g_assert_cmpstr (hello, ==, "hello");
}

static void
test_xcalloc (Fixture *f,
              gconstpointer context)
{
  autofree int *ints = xcalloc (2, sizeof (int));

  g_assert_cmpint (ints[0], ==, 0);
  g_assert_cmpint (ints[1], ==, 0);
}

int
main (int argc,
      char **argv)
{
  _srt_tests_init (&argc, &argv, NULL);

  g_test_add ("/libc-utils/autofclose",
              Fixture, NULL, setup, test_autofclose, teardown);
  g_test_add ("/libc-utils/n-elements",
              Fixture, NULL, setup, test_n_elements, teardown);
  g_test_add ("/libc-utils/new0",
              Fixture, NULL, setup, test_new0, teardown);
  g_test_add ("/libc-utils/steal-fd",
              Fixture, NULL, setup, test_steal_fd, teardown);
  g_test_add ("/libc-utils/steal-pointer",
              Fixture, NULL, setup, test_steal_pointer, teardown);
  g_test_add ("/libc-utils/xasprintf",
              Fixture, NULL, setup, test_xasprintf, teardown);
  g_test_add ("/libc-utils/xcalloc",
              Fixture, NULL, setup, test_xcalloc, teardown);

  return g_test_run ();
}
