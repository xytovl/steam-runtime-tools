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

#include <stdlib.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx.h"

#include "tests/test-utils.h"
#include "flatpak-utils-private.h"
#include "mtree.h"
#include "utils.h"

typedef struct
{
  TestsOpenFdSet old_fds;
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

  f->old_fds = tests_check_fd_leaks_enter ();
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  tests_check_fd_leaks_leave (f->old_fds);
}

static void
test_first_key (Fixture *f,
                gconstpointer context)
{
  g_autoptr(GHashTable) table = g_hash_table_new (g_str_hash, g_str_equal);
  gpointer k;

  k = pv_hash_table_get_first_key (table, NULL);
  g_assert_null (k);

  g_hash_table_add (table, (char *) "hello");
  k = pv_hash_table_get_first_key (table, NULL);
  g_assert_cmpstr (k, ==, "hello");
  k = pv_hash_table_get_first_key (table, _srt_generic_strcmp0);
  g_assert_cmpstr (k, ==, "hello");

  g_hash_table_add (table, (char *) "world");
  k = pv_hash_table_get_first_key (table, NULL);

  if (g_strcmp0 (k, "hello") != 0)
    g_assert_cmpstr (k, ==, "world");

  k = pv_hash_table_get_first_key (table, _srt_generic_strcmp0);
  g_assert_cmpstr (k, ==, "hello");
}

typedef struct
{
  gsize n;
  int digits;
} DecimalDigitsData;

static void
test_count_decimal_digits (Fixture *f,
                           gconstpointer context)
{
  static const DecimalDigitsData tests[] =
  {
      { 0, 1 },
      { 1, 1 },
      { 9, 1 },
      { 10, 2 },
      { 99, 2 },
      { 100, 3 },
      { 1000000000U, 10 },
      { 4294967295U, 10 },
#ifdef __LP64__
      { 10000000000ULL, 11 },
      { 10000000000000000000ULL, 20 },
      { 18446744073709551615ULL, 20 },
#endif
  };
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (tests); i++)
    g_assert_cmpint (pv_count_decimal_digits (tests[i].n), ==, tests[i].digits);
}

typedef struct
{
  const gchar *sub_dir;
  const gchar *file;
  const gchar *multiarch_tuple;
  int seq;
  int digits; /* Defaults to 1 */
  const gchar *expected_path;
} FilepathData;

typedef struct
{
  FilepathData filepath_data[10];
} GenerateFilepathTest;

static void
test_generate_unique_filepath (Fixture *f,
                               gconstpointer context)
{
#define ICD_SUBDIR "share/vulkan/icd.d"
#define EXPLICIT_LAYER_SUBDIR "share/vulkan/explicit_layer.d"

  static const GenerateFilepathTest tests[] =
  {
    {
      .filepath_data =
      {
        {
          .sub_dir = ICD_SUBDIR,
          .file = "radeon_icd.json",
          .seq = 0,
        },
        {
          .sub_dir = ICD_SUBDIR,
          .file = "lvp_icd.json",
          .multiarch_tuple = "x86_64",
          .seq = 1,
        },
        {
          .sub_dir = ICD_SUBDIR,
          .file = "radeon_icd.json",
          .seq = 2,
          .expected_path = "share/vulkan/icd.d/2/radeon_icd.json",
        },
        {
          .sub_dir = ICD_SUBDIR,
          .file = "lvp_icd.json",
          .multiarch_tuple = "x86_64",
          .seq = 3,
          .expected_path = "share/vulkan/icd.d/3-x86_64/lvp_icd.json",
        },
        {
          .sub_dir = ICD_SUBDIR,
          .file = "lvp_icd.json",
          .multiarch_tuple = "i686",
          .seq = 3,
          .expected_path = "share/vulkan/icd.d/3-i686/lvp_icd.json",
        },
      }
    },

    {
      .filepath_data =
      {
        {
          .sub_dir = ICD_SUBDIR,
          .file = "radeon_icd.x86_64.json",
          .seq = 0,
        },
        {
          .sub_dir = ICD_SUBDIR,
          .file = "lvp_icd.json",
          .seq = 1,
        },
        {
          .sub_dir = ICD_SUBDIR,
          .file = "my_custom.json",
          .seq = 2,
        },
        {
          /* Use a different sub directory. There shouldn't be any conflicts,
           * even if the filename is the same. */
          .sub_dir = EXPLICIT_LAYER_SUBDIR,
          .file = "my_custom.json",
          .seq = 3,
        },
        {
          .sub_dir = EXPLICIT_LAYER_SUBDIR,
          .file = "my_custom.json",
          .seq = 4,
          .digits = 2,
          .expected_path = "share/vulkan/explicit_layer.d/04/my_custom.json",
        },
        {
          .sub_dir = EXPLICIT_LAYER_SUBDIR,
          .file = "my_custom.json",
          .multiarch_tuple = "x86_64",
          .seq = 5,
          .digits = 2,
          .expected_path = "share/vulkan/explicit_layer.d/05-x86_64/my_custom.json",
        },
      }
    },
  };

  gsize i;
  gsize j;

  for (i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      g_autoptr(GHashTable) files_set = NULL;

      files_set = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

      for (j = 0; tests[i].filepath_data[j].file != NULL; j++)
        {
          g_autofree gchar *unique_path = NULL;
          const FilepathData data = tests[i].filepath_data[j];

          unique_path = pv_generate_unique_filepath (data.sub_dir,
                                                     data.digits == 0 ? 1 : data.digits,
                                                     data.seq,
                                                     data.file,
                                                     data.multiarch_tuple,
                                                     files_set);

          if (data.expected_path == NULL)
            {
              /* This is the case where there isn't a potential conflict */
              g_autofree gchar *expected = g_build_filename (data.sub_dir, data.file, NULL);
              g_assert_cmpstr (unique_path, ==, expected);
            }
          else
            {
              g_assert_cmpstr (unique_path, ==, data.expected_path);
            }

          g_assert_true (g_hash_table_contains (files_set, unique_path));
          g_assert_cmpint (g_hash_table_size (files_set), ==, j + 1);
        }

    }
}

static void
test_run_sync (Fixture *f,
               gconstpointer context)
{
  const gchar * argv[] =
  {
    "printf", "hello\\n", NULL, NULL,
  };
  g_autofree gchar *output = NULL;
  g_autoptr(GError) error = NULL;
  g_auto(GStrv) envp = g_get_environ ();
  int exit_status = -1;

  pv_run_sync (argv, NULL, NULL, &output, &error);
  g_assert_cmpstr (output, ==, "hello");
  g_assert_no_error (error);
  g_clear_pointer (&output, g_free);

  pv_run_sync (argv, NULL, &exit_status, NULL, &error);
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_no_error (error);

  argv[1] = "hello\\nworld";    /* deliberately no trailing newline */
  pv_run_sync (argv, NULL, &exit_status, &output, &error);
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_no_error (error);
  g_assert_cmpstr (output, ==, "hello\nworld");
  g_clear_pointer (&output, g_free);

  argv[0] = "/nonexistent/doesnotexist";
  pv_run_sync (argv, NULL, &exit_status, &output, &error);
  g_assert_cmpint (exit_status, ==, -1);
  g_assert_nonnull (error);

  if (error->domain == G_SPAWN_ERROR
      && error->code == G_SPAWN_ERROR_NOENT)
    {
      /* OK: specific failure could be reported */
    }
  else if (error->domain == G_SPAWN_ERROR
           && error->code == G_SPAWN_ERROR_FAILED)
    {
      /* less specific, but also OK */
    }
  else
    {
      g_error ("Expected pv_capture_output() with nonexistent executable "
               "to fail, got %s #%d",
               g_quark_to_string (error->domain), error->code);
    }

  g_assert_cmpstr (output, ==, NULL);
  g_clear_error (&error);

  argv[0] = "false";
  argv[1] = NULL;
  pv_run_sync (argv, NULL, &exit_status, &output, &error);
  g_assert_cmpint (exit_status, ==, 1);
  g_assert_error (error, G_SPAWN_EXIT_ERROR, 1);
  g_assert_cmpstr (output, ==, NULL);
  g_clear_error (&error);

  argv[0] = "sh";
  argv[1] = "-euc";
  argv[2] = "echo \"$PATH\"";
  pv_run_sync (argv, NULL, &exit_status, &output, &error);
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_no_error (error);
  g_assert_cmpstr (output, ==, g_getenv ("PATH"));
  g_clear_pointer (&output, g_free);

  envp = g_environ_setenv (envp, "FOO", "bar", TRUE);
  argv[0] = "sh";
  argv[1] = "-euc";
  argv[2] = "echo \"${FOO-unset}\"";
  pv_run_sync (argv, (const char * const *) envp, &exit_status, &output, &error);
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_no_error (error);
  g_assert_cmpstr (output, ==, "bar");
  g_clear_pointer (&output, g_free);

  envp = g_environ_unsetenv (envp, "FOO");
  argv[0] = "sh";
  argv[1] = "-euc";
  argv[2] = "echo \"${FOO-unset}\"";
  pv_run_sync (argv, (const char * const *) envp, &exit_status, &output, &error);
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_no_error (error);
  g_assert_cmpstr (output, ==, "unset");
  g_clear_pointer (&output, g_free);
}

static void
test_delete_dangling_symlink (Fixture *f,
                              gconstpointer context)
{
  g_autoptr(GError) error = NULL;
  g_auto(GLnxTmpDir) tmpdir = { FALSE };
  struct stat stat_buf;

  glnx_mkdtemp ("test-XXXXXX", 0700, &tmpdir, &error);
  g_assert_no_error (error);

  glnx_file_replace_contents_at (tmpdir.fd, "exists", (const guint8 *) "",
                                 0, 0, NULL, &error);
  g_assert_no_error (error);

  g_assert_no_errno (mkdirat (tmpdir.fd, "subdir", 0755));
  g_assert_no_errno (symlinkat ("exists", tmpdir.fd, "target-exists"));
  g_assert_no_errno (symlinkat ("does-not-exist", tmpdir.fd,
                                "target-does-not-exist"));
  g_assert_no_errno (symlinkat ("/etc/ssl/private/nope", tmpdir.fd,
                                "cannot-stat-target"));

  pv_delete_dangling_symlink (tmpdir.fd, tmpdir.path, "cannot-stat-target");
  pv_delete_dangling_symlink (tmpdir.fd, tmpdir.path, "does-not-exist");
  pv_delete_dangling_symlink (tmpdir.fd, tmpdir.path, "exists");
  pv_delete_dangling_symlink (tmpdir.fd, tmpdir.path, "subdir");
  pv_delete_dangling_symlink (tmpdir.fd, tmpdir.path, "target-does-not-exist");
  pv_delete_dangling_symlink (tmpdir.fd, tmpdir.path, "target-exists");

  /* We cannot tell whether ./cannot-stat-target is dangling or not
   * (assuming we're not root) so we give it the benefit of the doubt
   * and do not delete it */
  if (G_LIKELY (stat ("/etc/ssl/private/nope", &stat_buf) < 0
      && errno == EACCES))
    {
      g_assert_no_errno (fstatat (tmpdir.fd, "cannot-stat-target", &stat_buf,
                                  AT_SYMLINK_NOFOLLOW));
    }

  /* ./does-not-exist never existed */
  g_assert_cmpint (fstatat (tmpdir.fd, "does-not-exist", &stat_buf,
                            AT_SYMLINK_NOFOLLOW) == 0 ? 0 : errno,
                   ==, ENOENT);

  /* ./exists is not a symlink and so was not deleted */
  g_assert_no_errno (fstatat (tmpdir.fd, "exists", &stat_buf,
                              AT_SYMLINK_NOFOLLOW));

  /* ./subdir is not a symlink and so was not deleted */
  g_assert_no_errno (fstatat (tmpdir.fd, "subdir", &stat_buf,
                              AT_SYMLINK_NOFOLLOW));

  /* ./target-does-not-exist is a dangling symlink and so was deleted */
  g_assert_cmpint (fstatat (tmpdir.fd, "target-does-not-exist", &stat_buf,
                            AT_SYMLINK_NOFOLLOW) == 0 ? 0 : errno,
                   ==, ENOENT);

  /* ./target-exists is a non-dangling symlink and so was not deleted */
  g_assert_no_errno (fstatat (tmpdir.fd, "target-exists", &stat_buf,
                              AT_SYMLINK_NOFOLLOW));
}

static void
test_envp_cmp (Fixture *f,
               gconstpointer context)
{
  static const char * const unsorted[] =
  {
    "SAME_NAME=2",
    "EARLY_NAME=a",
    "SAME_NAME=222",
    "Z_LATE_NAME=b",
    "SUFFIX_ADDED=23",
    "SAME_NAME=1",
    "SAME_NAME=",
    "SUFFIX=42",
    "SAME_NAME=3",
    "SAME_NAME",
  };
  static const char * const sorted[] =
  {
    "EARLY_NAME=a",
    "SAME_NAME",
    "SAME_NAME=",
    "SAME_NAME=1",
    "SAME_NAME=2",
    "SAME_NAME=222",
    "SAME_NAME=3",
    "SUFFIX=42",
    "SUFFIX_ADDED=23",
    "Z_LATE_NAME=b",
  };
  G_GNUC_UNUSED const Config *config = context;
  const char **sort_this = NULL;
  gsize i, j;

  G_STATIC_ASSERT (G_N_ELEMENTS (sorted) == G_N_ELEMENTS (unsorted));

  for (i = 0; i < G_N_ELEMENTS (sorted); i++)
    {
      g_autofree gchar *copy = g_strdup (sorted[i]);

      g_test_message ("%s == %s", copy, sorted[i]);
      g_assert_cmpint (flatpak_envp_cmp (&copy, &sorted[i]), ==, 0);
      g_assert_cmpint (flatpak_envp_cmp (&sorted[i], &copy), ==, 0);

      for (j = i + 1; j < G_N_ELEMENTS (sorted); j++)
        {
          g_test_message ("%s < %s", sorted[i], sorted[j]);
          g_assert_cmpint (flatpak_envp_cmp (&sorted[i], &sorted[j]), <, 0);
          g_assert_cmpint (flatpak_envp_cmp (&sorted[j], &sorted[i]), >, 0);
        }
    }

  sort_this = g_new0 (const char *, G_N_ELEMENTS (unsorted));

  for (i = 0; i < G_N_ELEMENTS (unsorted); i++)
    sort_this[i] = unsorted[i];

  qsort (sort_this, G_N_ELEMENTS (unsorted), sizeof (char *), flatpak_envp_cmp);

  for (i = 0; i < G_N_ELEMENTS (sorted); i++)
    g_assert_cmpstr (sorted[i], ==, sort_this[i]);

  g_free (sort_this);
}

static void
test_mtree_entry_parse (Fixture *f,
                        gconstpointer context)
{
  static const struct
  {
    const char *line;
    const char *name;
    PvMtreeEntry expected;
    gboolean error;
    const char *link;
    const char *sha256;
  } tests[] =
  {
    { "#mtree",
      NULL,
      { .size = -1, .mtime_usec = -1, .mode = -1,
        .kind = PV_MTREE_ENTRY_KIND_UNKNOWN },
    },
    { "",
      NULL,
      { .size = -1, .mtime_usec = -1, .mode = -1,
        .kind = PV_MTREE_ENTRY_KIND_UNKNOWN },
    },
    { ". type=dir ignore",
      ".",
      { .size = -1, .mtime_usec = -1, .mode = -1,
        .kind = PV_MTREE_ENTRY_KIND_DIR,
        .entry_flags = PV_MTREE_ENTRY_FLAGS_IGNORE_BELOW },
    },
    { "./foo type=file sha256=ffff mode=0640 size=42 time=1597415889.500000000",
      "./foo",
      { .size = 42,
        .mtime_usec = 1597415889 * G_TIME_SPAN_SECOND + (G_TIME_SPAN_SECOND / 2),
        .mode = 0640,
        .kind = PV_MTREE_ENTRY_KIND_FILE },
      .sha256 = "ffff",
    },
    { "./foo type=file sha256digest=ffff mode=4755 time=1234567890 optional",
      "./foo",
      { .size = -1, .mtime_usec = (1234567890 * G_TIME_SPAN_SECOND),
        .mode = 04755, .kind = PV_MTREE_ENTRY_KIND_FILE,
        .entry_flags = PV_MTREE_ENTRY_FLAGS_OPTIONAL },
      .sha256 = "ffff",
    },
    { "./foo type=file sha256=ffff sha256digest=ffff time=1234567890.0",
      "./foo",
      { .size = -1, .mtime_usec = (1234567890 * G_TIME_SPAN_SECOND), .mode = -1,
        .kind = PV_MTREE_ENTRY_KIND_FILE },
      .sha256 = "ffff",
    },
    { "./symlink type=link link=/dev/null nochange",
      "./symlink",
      { .size = -1, .mtime_usec = -1, .mode = -1,
        .kind = PV_MTREE_ENTRY_KIND_LINK,
        .entry_flags = PV_MTREE_ENTRY_FLAGS_NO_CHANGE },
      .link = "/dev/null",
    },
    { "./silly-name/\\001\\123\\n\\r type=link link=\\\"\\\\\\b",
      "./silly-name/\001\123\n\r",
      { .size = -1, .mtime_usec = -1, .mode = -1,
        .kind = PV_MTREE_ENTRY_KIND_LINK },
      .link = "\"\\\b",
    },
    { ("./ignore cksum=123 device=456 contents=./ignore flags=123 gid=123 "
       "gname=users ignore inode=123 md5=ffff md5digest=ffff nlink=1 "
       "nochange optional resdevice=123 "
       "ripemd160digest=ffff rmd160=ffff rmd160digest=ffff "
       "sha1=ffff sha1digest=ffff "
       "sha384=ffff sha384digest=ffff "
       "sha512=ffff sha512digest=ffff "
       "uid=0 uname=root type=dir"),
      "./ignore",
      { .size = -1, .mtime_usec = -1, .mode = -1,
        .kind = PV_MTREE_ENTRY_KIND_DIR,
        .entry_flags = (PV_MTREE_ENTRY_FLAGS_IGNORE_BELOW
                        | PV_MTREE_ENTRY_FLAGS_NO_CHANGE
                        | PV_MTREE_ENTRY_FLAGS_OPTIONAL) },
    },
    { "./foo type=file sha256=ffff sha256digest=eeee", .error = TRUE },
    { "./foo type=file mode=1a", .error = TRUE },
    { "/set type=dir", .error = TRUE },
    { "../escape type=dir", .error = TRUE },
    { "relative type=dir", .error = TRUE },
    { "./foo link", .error = TRUE },
    { "./foo type=bar", .error = TRUE },
    { "./continuation type=dir \\", .error = TRUE },
    { "./alert type=link link=\\a", .error = TRUE },
    { "./hex type=link link=\\x12", .error = TRUE },
    { "./symlink type=file link=/dev/null", .error = TRUE },
    { "./symlink type=link", .error = TRUE },
    { "      ", .error = TRUE },
    { "./not-time type=file type=1a", .error = TRUE },
    { "./not-time type=file type=1.2a", .error = TRUE },
    { "./ambiguous-time type=file type=1.234", .error = TRUE },
  };
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      const char *line = tests[i].line;
      PvMtreeEntry expected = tests[i].expected;
      g_auto(PvMtreeEntry) got = PV_MTREE_ENTRY_BLANK;
      g_autoptr(GError) error = NULL;

      g_test_message ("%s", line);
      pv_mtree_entry_parse (line, &got, "test.mtree", 1, &error);

      if (tests[i].error)
        {
          g_assert_nonnull (error);
          g_test_message ("-> %s", error->message);
        }
      else
        {
          g_assert_no_error (error);
          g_test_message ("-> OK");
          g_assert_cmpstr (got.name, ==, tests[i].name);
          g_assert_cmpstr (got.link, ==, tests[i].link);
          g_assert_cmpstr (got.sha256, ==, tests[i].sha256);
          g_assert_cmpint (got.size, ==, expected.size);
          g_assert_cmpint (got.mtime_usec, ==, expected.mtime_usec);
          g_assert_cmpint (got.mode, ==, expected.mode);
          g_assert_cmpint (got.kind, ==, expected.kind);
          g_assert_cmphex (got.entry_flags, ==, expected.entry_flags);
        }
    }
}

static void
test_search_path_append (Fixture *f,
                         gconstpointer context)
{
  g_autoptr(GString) str = g_string_new ("");

  pv_search_path_append (str, NULL);
  g_assert_cmpstr (str->str, ==, "");

  pv_search_path_append (str, "");
  g_assert_cmpstr (str->str, ==, "");

  pv_search_path_append (str, "/bin");
  g_assert_cmpstr (str->str, ==, "/bin");

  pv_search_path_append (str, NULL);
  g_assert_cmpstr (str->str, ==, "/bin");

  pv_search_path_append (str, "");
  g_assert_cmpstr (str->str, ==, "/bin");

  pv_search_path_append (str, "/usr/bin");
  g_assert_cmpstr (str->str, ==, "/bin:/usr/bin");

  /* Duplicates are not removed */
  pv_search_path_append (str, "/usr/bin");
  g_assert_cmpstr (str->str, ==, "/bin:/usr/bin:/usr/bin");
}

static struct stat stats_to_describe[] =
{
  { .st_mode = 0104750, .st_uid = 0, .st_gid = 0 },
  { .st_mode = 0100644, .st_uid = 1, .st_gid = 4 },   /* daemon:adm on Debian */
  { .st_mode = 0100644, .st_uid = 100, .st_gid = 100 },   /* :users on Debian */
  { .st_mode = 0100644, .st_uid = 1000, .st_gid = 100 },
  { .st_mode = 0100644, .st_uid = 1000, .st_gid = 1000 },
  { .st_mode = 0100755, .st_uid = 65534, .st_gid = 65534 },
  { .st_mode = 0100755, .st_uid = -1, .st_gid = -1 },
  /* -2 is a placeholder which we replace with the current uid/gid below.
   * -1 is not a valid uid/gid, so we will show it numerically. */
  { .st_mode = 0100755, .st_uid = -2, .st_gid = -1 },
  { .st_mode = 0100755, .st_uid = -1, .st_gid = -2 },
  { .st_mode = 0100755, .st_uid = -2, .st_gid = -2 },
};

static void
test_stat_describe_permissions (Fixture *f,
                                gconstpointer context)
{
  size_t i;

  /* This is mostly a manual test: we assume the current user is
   * reasonably likely to be uid 1000 and a member of Debian's
   * adm or users groups for the purposes of exercising all code paths. */
  for (i = 0; i < G_N_ELEMENTS (stats_to_describe); i++)
    {
      g_autofree char *description = NULL;

      if (stats_to_describe[i].st_uid == (uid_t) -2)
        stats_to_describe[i].st_uid = geteuid ();

      if (stats_to_describe[i].st_gid == (gid_t) -2)
        stats_to_describe[i].st_gid = getegid ();

      description = pv_stat_describe_permissions (&stats_to_describe[i]);
      g_assert_nonnull (description);
      g_test_message ("%s", description);
    }
}

static void
test_workarounds (Fixture *f,
                  gconstpointer context)
{
  static const char * const all_env[] =
  {
    "PRESSURE_VESSEL_WORKAROUNDS=all", NULL
  };
  static const char * const none_env[] =
  {
    "PRESSURE_VESSEL_WORKAROUNDS=none", NULL
  };
  static const char * const minus_all_env[] =
  {
    "PRESSURE_VESSEL_WORKAROUNDS=-all", NULL
  };
  static const char * const snap_env[] =
  {
    "SNAP=steam", "SNAP_NAME=steam", "SNAP_REVISION=1", NULL
  };
  static const char * const config_env[] =
  {
    "PRESSURE_VESSEL_WORKAROUNDS=old-bwrap,steam-snap#356", NULL
  };
  static const char * const order_env[] =
  {
    "PRESSURE_VESSEL_WORKAROUNDS=steam-snap#356 +steamsnap369 -steamsnap356", NULL
  };

  g_assert_cmpint (pv_get_workarounds (SRT_BWRAP_FLAGS_HAS_PERMS, NULL),
                   ==, PV_WORKAROUND_FLAGS_NONE);
  g_assert_cmpint (pv_get_workarounds (SRT_BWRAP_FLAGS_NONE, NULL),
                   ==, PV_WORKAROUND_FLAGS_BWRAP_NO_PERMS);
  g_assert_cmpint (pv_get_workarounds (SRT_BWRAP_FLAGS_NONE, none_env),
                   ==, PV_WORKAROUND_FLAGS_NONE);
  g_assert_cmpint (pv_get_workarounds (SRT_BWRAP_FLAGS_NONE, minus_all_env),
                   ==, PV_WORKAROUND_FLAGS_NONE);
  g_assert_cmpint (pv_get_workarounds (SRT_BWRAP_FLAGS_HAS_PERMS, all_env),
                   ==, PV_WORKAROUND_FLAGS_ALL);
  g_assert_cmpint (pv_get_workarounds (SRT_BWRAP_FLAGS_HAS_PERMS, snap_env),
                   ==, (PV_WORKAROUND_FLAGS_STEAMSNAP_397));
  g_assert_cmpint (pv_get_workarounds (SRT_BWRAP_FLAGS_HAS_PERMS, config_env),
                   ==, (PV_WORKAROUND_FLAGS_BWRAP_NO_PERMS
                        | PV_WORKAROUND_FLAGS_STEAMSNAP_356));
  g_assert_cmpint (pv_get_workarounds (SRT_BWRAP_FLAGS_HAS_PERMS, order_env),
                   ==, (PV_WORKAROUND_FLAGS_STEAMSNAP_369));
}

int
main (int argc,
      char **argv)
{
  _srt_setenv_disable_gio_modules ();

  _srt_tests_init (&argc, &argv, NULL);
  g_test_add ("/count-decimal-digits", Fixture, NULL,
              setup, test_count_decimal_digits, teardown);
  g_test_add ("/first-key", Fixture, NULL,
              setup, test_first_key, teardown);
  g_test_add ("/generate-unique-filepath", Fixture, NULL,
              setup, test_generate_unique_filepath, teardown);
  g_test_add ("/run-sync", Fixture, NULL,
              setup, test_run_sync, teardown);
  g_test_add ("/delete-dangling-symlink", Fixture, NULL,
              setup, test_delete_dangling_symlink, teardown);
  g_test_add ("/envp-cmp", Fixture, NULL, setup, test_envp_cmp, teardown);
  g_test_add ("/mtree-entry-parse", Fixture, NULL,
              setup, test_mtree_entry_parse, teardown);
  g_test_add ("/search-path-append", Fixture, NULL,
              setup, test_search_path_append, teardown);
  g_test_add ("/stat-describe-permissions", Fixture, NULL,
              setup, test_stat_describe_permissions, teardown);
  g_test_add ("/workarounds", Fixture, NULL,
              setup, test_workarounds, teardown);

  return g_test_run ();
}
