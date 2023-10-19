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
  g_autoptr(SrtOsInfo) info = NULL;
  g_autoptr(GHashTable) fields = NULL;
  g_autoptr(GHashTable) fields_property = NULL;
  g_autofree gchar *messages_property = NULL;
  g_autofree gchar *path_property = NULL;

  info = _srt_os_info_new (NULL, NULL, NULL);

  fields = srt_os_info_dup_fields (info);
  g_assert_nonnull (fields);
  g_assert_cmpuint (g_hash_table_size (fields), ==, 0);
  g_assert_cmpstr (srt_os_info_get_build_id (info), ==, NULL);
  g_assert_cmpstr (srt_os_info_get_id (info), ==, NULL);
  g_assert_null (srt_os_info_get_id_like (info));
  g_assert_cmpstr (srt_os_info_get_name (info), ==, NULL);
  g_assert_cmpstr (srt_os_info_get_pretty_name (info), ==, NULL);
  g_assert_cmpstr (srt_os_info_get_variant (info), ==, NULL);
  g_assert_cmpstr (srt_os_info_get_variant_id (info), ==, NULL);
  g_assert_cmpstr (srt_os_info_get_version_codename (info), ==, NULL);
  g_assert_cmpstr (srt_os_info_get_version_id (info), ==, NULL);
  g_assert_cmpstr (srt_os_info_get_messages (info), ==, NULL);
  g_assert_cmpstr (srt_os_info_get_source_path (info), ==, NULL);

  g_object_get (info,
                "fields", &fields_property,
                "messages", &messages_property,
                "source-path", &path_property,
                NULL);
  g_assert_nonnull (fields_property);
  g_assert_cmpuint (g_hash_table_size (fields_property), ==, 0);
  /* Each dup_fields() call returns a deep copy, to ensure that @info
   * remains immutable after construction */
  g_assert_true (fields_property != fields);
  g_assert_cmpstr (messages_property, ==, NULL);
  g_assert_cmpstr (path_property, ==, NULL);
}

typedef struct
{
  const char *test_name;
  const char *data;
  gssize len;
  const char *source_path;
  const char *previous_messages;
  const char *build_id;
  const char *id;
  const char *name;
  const char *pretty_name;
  const char *variant;
  const char *variant_id;
  const char *version_codename;
  const char *version_id;
  const char *foo;
  /* Arbitrary length, increase as necessary */
  const char * const id_like[3];
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
      .source_path = "/etc/os-release",
      .previous_messages = "Failed to reticulate splines",
      .build_id = "666",
      .id = "myos",
      .id_like = { "fedora", NULL },
      .name = "My OS",
      .pretty_name = "My OS v32",
      .variant = "Best",
      .variant_id = "best",
      .version_codename = "stoat",
      .version_id = "32",
      .foo = "",
      .expect_messages =
      {
        "Failed to reticulate splines",
        NULL,
      },
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
      .id_like = { "ubuntu", "debian", NULL },
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
      .previous_messages = "Already had some\ndiagnostic messages",
      .name = "bar",
      .expect_messages =
      {
        "Already had some",
        "diagnostic messages",
        "NAME appears more than once in <data>, will use last instance",
        NULL
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
      const char *source_path = "<data>";
      g_autoptr(SrtOsInfo) info = NULL;
      g_autoptr(GHashTable) fields = NULL;
      g_autoptr(GHashTable) fields_property = NULL;
      g_autofree gchar *messages_property = NULL;
      g_autofree gchar *path_property = NULL;
      GHashTableIter iter;
      gpointer k, v;
      gsize len;

      g_test_message ("%s...", test->test_name);

      if (test->source_path != NULL)
        source_path = test->source_path;

      if (test->len >= 0)
        len = (gsize) test->len;
      else
        len = strlen (test->data);

      info = _srt_os_info_new_from_data (source_path,
                                         test->data, len,
                                         test->previous_messages);

      g_assert_cmpstr (srt_os_info_get_source_path (info), ==, source_path);
      g_assert_cmpstr (srt_os_info_get_build_id (info), ==, test->build_id);
      g_assert_cmpstr (srt_os_info_get_id (info), ==, test->id);

      if (test->id_like[0] == NULL)
        g_assert_null (srt_os_info_get_id_like (info));
      else
        g_assert_cmpstrv (srt_os_info_get_id_like (info), test->id_like);

      g_assert_cmpstr (srt_os_info_get_name (info), ==, test->name);
      g_assert_cmpstr (srt_os_info_get_pretty_name (info), ==, test->pretty_name);
      g_assert_cmpstr (srt_os_info_get_variant (info), ==, test->variant);
      g_assert_cmpstr (srt_os_info_get_variant_id (info), ==, test->variant_id);
      g_assert_cmpstr (srt_os_info_get_version_codename (info), ==, test->version_codename);
      g_assert_cmpstr (srt_os_info_get_version_id (info), ==, test->version_id);

      fields = srt_os_info_dup_fields (info);
      g_assert_nonnull (fields);
      g_assert_cmpstr (g_hash_table_lookup (fields, "BUILD_ID"), ==, test->build_id);
      g_assert_cmpstr (g_hash_table_lookup (fields, "ID"), ==, test->id);
      g_assert_cmpstr (g_hash_table_lookup (fields, "NAME"), ==, test->name);
      g_assert_cmpstr (g_hash_table_lookup (fields, "PRETTY_NAME"), ==, test->pretty_name);
      g_assert_cmpstr (g_hash_table_lookup (fields, "VARIANT"), ==, test->variant);
      g_assert_cmpstr (g_hash_table_lookup (fields, "VARIANT_ID"), ==, test->variant_id);
      g_assert_cmpstr (g_hash_table_lookup (fields, "VERSION_CODENAME"), ==, test->version_codename);
      g_assert_cmpstr (g_hash_table_lookup (fields, "VERSION_ID"), ==, test->version_id);
      g_assert_cmpstr (g_hash_table_lookup (fields, "FOO"), ==, test->foo);

      if (test->expect_messages[0] == NULL)
        {
          g_assert_cmpstr (srt_os_info_get_messages (info), ==, NULL);
        }
      else
        {
          g_auto(GStrv) lines = NULL;
          gsize j;

          g_assert_cmpstr (srt_os_info_get_messages (info), !=, NULL);
          g_assert_cmpstr (srt_os_info_get_messages (info), !=, "");
          lines = g_strsplit (srt_os_info_get_messages (info), "\n", -1);

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

          /* get_messages() ends with a newline, which g_strsplit() turns
           * into a blank line */
          g_assert_cmpstr (lines[j], ==, "");
          g_assert_cmpstr (lines[j + 1], ==, NULL);
        }

      g_object_get (info,
                    "fields", &fields_property,
                    "messages", &messages_property,
                    "source-path", &path_property,
                    NULL);
      g_assert_nonnull (fields_property);
      g_assert_cmpuint (g_hash_table_size (fields_property), ==,
                        g_hash_table_size (fields));
      /* Each dup_fields() call returns a deep copy, to ensure that @info
       * remains immutable after construction */
      g_assert_true (fields_property != fields);

      g_hash_table_iter_init (&iter, fields);

      while (g_hash_table_iter_next (&iter, &k, &v))
        {
          const char *value = g_hash_table_lookup (fields_property, k);

          g_test_message ("%s=%s", (const char *) k, (const char *) v);
          g_assert_cmpstr (v, ==, value);
        }

      g_assert_cmpstr (messages_property, ==, srt_os_info_get_messages (info));
      g_assert_cmpstr (path_property, ==, source_path);
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
