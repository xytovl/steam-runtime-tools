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
test_clear_pointer (Fixture *f,
                    gconstpointer context)
{
  FILE *devnull = NULL;

  g_assert_no_errno ((devnull = fopen ("/dev/null", "r")) ? 0 : -1);
  clear_pointer (&devnull, fclose);
  g_assert_null (devnull);
  clear_pointer (&devnull, fclose);
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
  autofree char *pointer = xstrdup ("hello");
  autofree char *stolen = NULL;

  stolen = steal_pointer (&pointer);
  g_assert_null (pointer);
  g_assert_cmpstr (stolen, ==, "hello");
}

static void
test_strcmp0 (Fixture *f,
              gconstpointer context)
{
  g_assert_cmpint (strcmp0 (NULL, NULL), ==, 0);
  g_assert_cmpint (strcmp0 ("one", "one"), ==, 0);
  g_assert_cmpint (strcmp0 (NULL, "one"), <, 0);
  g_assert_cmpint (strcmp0 ("one", "two"), <, 0);
  g_assert_cmpint (strcmp0 ("one", NULL), >, 0);
  g_assert_cmpint (strcmp0 ("two", "one"), >, 0);
}

static void
test_str_has_prefix (Fixture *f,
                     gconstpointer context)
{
  g_assert_true (str_has_prefix ("", ""));
  g_assert_true (str_has_prefix ("bees", ""));
  g_assert_true (str_has_prefix ("bees", "be"));
  g_assert_true (str_has_prefix ("bees", "bees"));
  g_assert_false (str_has_prefix ("be", "bees"));
  g_assert_false (str_has_prefix ("beer", "bees"));
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

static void
test_xrealloc (Fixture *f,
               gconstpointer context)
{
  autofree int *ints = xrealloc (NULL, 2 * sizeof (int));

  ints[0] = -1;
  ints[1] = 1;
  ints = xrealloc (ints, 4 * sizeof (int));
  g_assert_cmpint (ints[0], ==, -1);
  g_assert_cmpint (ints[1], ==, 1);
  ints[2] = ints[3] = 42;
}

int
main (int argc,
      char **argv)
{
  unblock_signals_single_threaded ();
  _srt_tests_init (&argc, &argv, NULL);

  g_test_add ("/libc-utils/autofclose",
              Fixture, NULL, setup, test_autofclose, teardown);
  g_test_add ("/libc-utils/clear-pointer",
              Fixture, NULL, setup, test_clear_pointer, teardown);
  g_test_add ("/libc-utils/n-elements",
              Fixture, NULL, setup, test_n_elements, teardown);
  g_test_add ("/libc-utils/new0",
              Fixture, NULL, setup, test_new0, teardown);
  g_test_add ("/libc-utils/steal-fd",
              Fixture, NULL, setup, test_steal_fd, teardown);
  g_test_add ("/libc-utils/steal-pointer",
              Fixture, NULL, setup, test_steal_pointer, teardown);
  g_test_add ("/libc-utils/strcmp0",
              Fixture, NULL, setup, test_strcmp0, teardown);
  g_test_add ("/libc-utils/str-has-prefix",
              Fixture, NULL, setup, test_str_has_prefix, teardown);
  g_test_add ("/libc-utils/xasprintf",
              Fixture, NULL, setup, test_xasprintf, teardown);
  g_test_add ("/libc-utils/xcalloc",
              Fixture, NULL, setup, test_xcalloc, teardown);
  g_test_add ("/libc-utils/xrealloc",
              Fixture, NULL, setup, test_xrealloc, teardown);

  return g_test_run ();
}
