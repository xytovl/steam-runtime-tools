/*
 * Copyright © 2019-2023 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <libglnx.h>
#include <steam-runtime-tools/glib-backports-internal.h>
#include <steam-runtime-tools/steam-runtime-tools.h>

#include "steam-runtime-tools/os-internal.h"
#include "steam-runtime-tools/utils-internal.h"

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

static void
test_empty (Fixture *f,
            gconstpointer context)
{
  SrtOsRelease os_release;

  _srt_os_release_init (&os_release);

  g_assert_cmpint (os_release.populated, ==, FALSE);
  g_assert_cmpstr (os_release.build_id, ==, NULL);
  g_assert_cmpstr (os_release.id, ==, NULL);
  g_assert_cmpstr (os_release.id_like, ==, NULL);
  g_assert_cmpstr (os_release.name, ==, NULL);
  g_assert_cmpstr (os_release.pretty_name, ==, NULL);
  g_assert_cmpstr (os_release.variant, ==, NULL);
  g_assert_cmpstr (os_release.variant_id, ==, NULL);
  g_assert_cmpstr (os_release.version_codename, ==, NULL);
  g_assert_cmpstr (os_release.version_id, ==, NULL);

  _srt_os_release_clear (&os_release);
}

typedef struct
{
  const char *test_name;
  const char *data;
  gssize len;
  const char *build_id;
  const char *id;
  const char *id_like;
  const char *name;
  const char *pretty_name;
  const char *variant;
  const char *variant_id;
  const char *version_codename;
  const char *version_id;
  /* Arbitrary length, increase as necessary */
  const char * const expect_messages[4];
} DataTest;

static const DataTest from_data[] =
{
    {
      "empty",
      "",
      0,
    },
    {
      "small",
      (
        "NAME=\"This OS\"\n"
        "ID=this\n"
      ),
      -1,
      .id = "this",
      .name = "This OS",
    },
    {
      "complete",
      (
        "NAME=\"My OS\"\n"
        "ID=myos\n"
        "ID_LIKE=fedora\n"
        "VERSION_ID=32\n"
        "VERSION_CODENAME=stoat\n"
        "PRETTY_NAME='My OS v32'\n"
        "BUILD_ID=666\n"
        "VARIANT=Best\n"
        "VARIANT_ID=best\n"
        "FOO="
      ),
      -1,
      .build_id = "666",
      .id = "myos",
      .id_like = "fedora",
      .name = "My OS",
      .pretty_name = "My OS v32",
      .variant = "Best",
      .variant_id = "best",
      .version_codename = "stoat",
      .version_id = "32",
    },
    {
      "scout special-cases",
      (
        "NAME='Steam Runtime'\n"
        "ID=steamrt\n"
        "ID_LIKE=ubuntu\n"
        "VERSION_ID=1\n"
        "PRETTY_NAME=\"Steam Runtime 1 'scout'\"\n"
        "BUILD_ID=0.20231017.0\n"
        "VARIANT=Platform\n"
        "VARIANT_ID=platform\n"
      ),
      -1,
      .build_id = "0.20231017.0",
      .id = "steamrt",
      /* Special-cased in code */
      .id_like = "ubuntu debian",
      .name = "Steam Runtime",
      .pretty_name = "Steam Runtime 1 'scout'",
      .variant = "Platform",
      .variant_id = "platform",
      /* Special-cased in code */
      .version_codename = "scout",
      .version_id = "1",
    },
    {
      "unterminated",
      (
        "NAME=foo\n"
        "ID=ignore-this\n"
      ),
      9,  /* == strlen ("NAME=foo\n") */
      .name = "foo",
    },
    {
      "unterminated 2",
      (
        "NAME=foo"
        "ID=ignore-this\n"
      ),
      8,  /* == strlen ("NAME=foo") */
      .name = "foo",
    },
    {
      "duplicates",
      (
        "NAME=foo\n"
        "NAME=bar\n"
      ),
      -1,
      .name = "bar",
      .expect_messages =
      {
        "NAME appears more than once in <data>, will use last instance",
      },
    },
    {
      "incorrect",
      (
        "FOO\n"
        "BAR='\n"
      ),
      -1,
      .expect_messages =
      {
        "Unable to parse line \"FOO\" in <data>: no \"=\" found",
        "Unable to parse line \"BAR='\" in <data>: ...",
      },
    },
};

static void
test_from_data (Fixture *f,
                gconstpointer context)
{
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (from_data); i++)
    {
      const DataTest *test = &from_data[i];
      gsize len;
      SrtOsRelease os_release;
      g_autoptr(GString) messages = g_string_new ("");

      _srt_os_release_init (&os_release);

      g_test_message ("%s...", test->test_name);

      if (test->len >= 0)
        len = (gsize) test->len;
      else
        len = strlen (test->data);

      _srt_os_release_populate_from_data (&os_release, "<data>", test->data, len,
                                          messages);

      g_assert_cmpstr (os_release.build_id, ==, test->build_id);
      g_assert_cmpstr (os_release.id, ==, test->id);
      g_assert_cmpstr (os_release.id_like, ==, test->id_like);
      g_assert_cmpstr (os_release.name, ==, test->name);
      g_assert_cmpstr (os_release.pretty_name, ==, test->pretty_name);
      g_assert_cmpstr (os_release.variant, ==, test->variant);
      g_assert_cmpstr (os_release.variant_id, ==, test->variant_id);
      g_assert_cmpstr (os_release.version_codename, ==, test->version_codename);
      g_assert_cmpstr (os_release.version_id, ==, test->version_id);

      if (test->expect_messages[0] == NULL)
        {
          g_assert_cmpstr (messages->str, ==, "");
        }
      else
        {
          g_auto(GStrv) lines = NULL;
          gsize j;

          g_assert_cmpstr (messages->str, !=, "");
          lines = g_strsplit (messages->str, "\n", -1);

          for (j = 0; lines[j] != NULL; j++)
            g_test_message ("Diagnostic message: %s", lines[j]);

          for (j = 0; test->expect_messages[j] != NULL; j++)
            {
              g_assert_nonnull (lines[j]);

              if (g_str_has_suffix (test->expect_messages[j], "..."))
                {
                  gsize cmp_len = strlen (test->expect_messages[j]) - 3;
                  g_autofree gchar *expected = g_strndup (test->expect_messages[j], cmp_len);
                  g_autofree gchar *prefix = g_strndup (lines[j], cmp_len);
                  g_assert_cmpstr (prefix, ==, expected);
                }
              else
                {
                  g_assert_cmpstr (lines[j], ==, test->expect_messages[j]);
                }
            }

          /* messages ends with a newline, which g_strsplit() turns
           * into a blank line */
          g_assert_cmpstr (lines[j], ==, "");
          g_assert_cmpstr (lines[j + 1], ==, NULL);
        }

      _srt_os_release_clear (&os_release);
    }
}

int
main (int argc,
      char **argv)
{
  _srt_tests_init (&argc, &argv, NULL);
  g_test_add ("/os/empty", Fixture, NULL,
              setup, test_empty, teardown);
  g_test_add ("/os/from-data", Fixture, NULL,
              setup, test_from_data, teardown);

  return g_test_run ();
}
