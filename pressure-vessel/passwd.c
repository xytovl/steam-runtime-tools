/*
 * Copyright © 2020-2024 Collabora Ltd.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "passwd.h"

#include "steam-runtime-tools/log-internal.h"

/* Append @field to @buffer, replacing colons or newlines with '_'
 * to avoid possibly corrupting the passwd(5)/group(5) syntax. */
static void
append_field (GString *buffer,
              const char *field)
{
  gsize len = strlen (field);
  gsize old_len = buffer->len;
  gboolean warned = FALSE;
  gsize i;

  g_string_append (buffer, field);

  for (i = 0; i < len; i++)
    {
      switch (buffer->str[old_len + i])
        {
          case ':':
          case '\n':
            if (!warned)
              {
                _srt_log_warning ("Field \"%s\" cannot be represented in passwd(5)/group(5)",
                                  field);
                warned = TRUE;
              }

            buffer->str[old_len + i] = '_';
            break;

          default:
            break;
        }
    }

  g_assert (buffer->str[old_len + len] == '\0');
}

/*
 * Assume that the first line in @buffer is a single user in passwd(5)
 * syntax or a single group in group(5) syntax.
 *
 * Append all lines from @source/@path to @buffer, unless they would
 * duplicate the user/group that is already there.
 */
static gboolean
append_remaining_lines (GString *buffer,
                        SrtSysroot *source,
                        const char *path,
                        GError **error)
{
  g_autofree gchar *exclude_same_name = NULL;
  g_autofree gchar *content = NULL;
  const char *line;
  gsize line_num;

  if (buffer->len != 0)
    {
      const char *colon;

      colon = strchr (buffer->str, ':');
      g_assert (colon != NULL);
      /* username + ':' as a prefix to exclude matching lines, e.g. "gfreeman:" */
      exclude_same_name = g_strndup (buffer->str, (colon - buffer->str) + 1);
    }

  if (!_srt_sysroot_load (source, path, SRT_RESOLVE_FLAGS_READABLE,
                          NULL, &content, NULL, error))
    return FALSE;

  line = content;
  line_num = 0;

  while (TRUE)
    {
      const char *end_of_line;
      gsize len;

      end_of_line = strchr (line, '\n');

      if (end_of_line == NULL)
        len = strlen (line);
      else
        len = end_of_line - line;

      if (exclude_same_name != NULL && g_str_has_prefix (line, exclude_same_name))
        {
          g_debug ("Skipping %s:%zu \"%s...\" because it is our user/group",
                   path, line_num, exclude_same_name);
        }
      else if (len > 0)
        {
          g_string_append_len (buffer, line, len);
          g_string_append_c (buffer, '\n');
        }

      if (end_of_line == NULL)
        break;

      line = end_of_line + 1;
      line_num++;
    }

  return TRUE;
}

/* getpwuid(), but with mock output for unit-testing */
static const struct passwd *
getpwuid_wrapper (uid_t uid,
                  PvMockPasswdLookup *mock)
{
  if (G_LIKELY (mock == NULL))
    return getpwuid (uid);

  g_assert_cmpint (mock->uid, ==, uid);

  if (mock->lookup_errno != 0)
    errno = mock->lookup_errno;

  return mock->pwd;
}

/*
 * @source: A sysroot from which we can read /etc/passwd
 * @mock: (allow-none): A mock version of the real getpwuid() result,
 *  used during unit-testing
 *
 * Return contents for a passwd(5) that has at least our own uid.
 */
gchar *
pv_generate_etc_passwd (SrtSysroot *source,
                        PvMockPasswdLookup *mock)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GString) buffer = NULL;
  struct passwd fallback =
    {
      .pw_name = NULL,
      .pw_passwd = (char *) "x",
      .pw_uid = -1,
      .pw_gid = -1,
      .pw_gecos = NULL,
      .pw_dir = NULL,
      .pw_shell = (char *) "/bin/bash",
    };
  const struct passwd *pw = NULL;

  fallback.pw_name = (char *) g_get_user_name ();

  if (fallback.pw_name == NULL)
    fallback.pw_name = (char *) "user";

  fallback.pw_gecos = (char *) g_get_real_name ();

  if (fallback.pw_gecos == NULL)
    fallback.pw_gecos = fallback.pw_name;

  fallback.pw_uid = getuid ();
  fallback.pw_gid = getgid ();
  fallback.pw_dir = (char *) g_get_home_dir ();

  errno = 0;
  pw = getpwuid_wrapper (fallback.pw_uid, mock);

  if (pw == NULL)
    {
      int saved_errno = errno;

      _srt_log_warning ("Unable to resolve uid %d: %s",
                        fallback.pw_uid,
                        saved_errno == 0 ? "user not found" : g_strerror (errno));
      pw = &fallback;
    }

  g_assert (pw != NULL);
  buffer = g_string_new ("");
  append_field (buffer, pw->pw_name);
  g_string_append_printf (buffer, ":x:%d:%d:", pw->pw_uid, pw->pw_gid);
  append_field (buffer, pw->pw_gecos);
  g_string_append_c (buffer, ':');
  append_field (buffer, pw->pw_dir);
  /* We always behave as if the user's shell is bash, because we can rely
   * on that existing in the container, whereas an alternative shell like
   * /bin/zsh might not. */
  g_string_append (buffer, ":/bin/bash\n");

  if (!append_remaining_lines (buffer, source, "/etc/passwd", &local_error))
    {
      _srt_log_warning ("%s", local_error->message);
      g_clear_error (&local_error);
    }

  return g_string_free (g_steal_pointer (&buffer), FALSE);
}

/* getgrgid(), but with mock output for unit-testing */
static const struct group *
getgrgid_wrapper (gid_t gid,
                  PvMockPasswdLookup *mock)
{
  if (G_LIKELY (mock == NULL))
    return getgrgid (gid);

  g_assert_cmpint (mock->gid, ==, gid);

  if (mock->lookup_errno != 0)
    errno = mock->lookup_errno;

  return mock->grp;
}

/*
 * @source: A sysroot from which we can read /etc/passwd
 * @mock: (allow-none): A mock version of the real getgrgid() result,
 *  used during unit-testing
 *
 * Return contents for a group(5) that has at least our own primary gid.
 */
gchar *
pv_generate_etc_group (SrtSysroot *source,
                       PvMockPasswdLookup *mock)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GString) buffer = NULL;
  gid_t primary_gid = getgid ();
  const struct group *gr = NULL;

  errno = 0;
  gr = getgrgid_wrapper (primary_gid, mock);

  if (gr == NULL)
    {
      int saved_errno = errno;

      _srt_log_warning ("Unable to resolve gid %d: %s",
                        primary_gid,
                        saved_errno == 0 ? "group not found" : g_strerror (errno));
    }

  buffer = g_string_new ("");

  if (gr != NULL)
    {
      append_field (buffer, gr->gr_name);
      g_string_append_printf (buffer, ":x:%d:\n", gr->gr_gid);
    }

  if (!append_remaining_lines (buffer, source, "/etc/group", &local_error))
    {
      _srt_log_warning ("%s", local_error->message);
      g_clear_error (&local_error);
    }

  return g_string_free (g_steal_pointer (&buffer), FALSE);
}
