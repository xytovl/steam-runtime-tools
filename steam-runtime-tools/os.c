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

#include "steam-runtime-tools/os-internal.h"

#include <string.h>

#include "steam-runtime-tools/glib-backports-internal.h"

#include <glib.h>

#include "steam-runtime-tools/utils.h"
#include "steam-runtime-tools/utils-internal.h"

G_GNUC_INTERNAL void
_srt_os_release_init (SrtOsRelease *self)
{
  self->build_id = NULL;
  self->id = NULL;
  self->id_like = NULL;
  self->name = NULL;
  self->pretty_name = NULL;
  self->variant = NULL;
  self->variant_id = NULL;
  self->version_codename = NULL;
  self->version_id = NULL;
  self->populated = FALSE;
}

static const struct
{
  const char *path;
  gboolean only_in_run_host;
}
os_release_paths[] =
{
  { "/etc/os-release", FALSE },
  { "/usr/lib/os-release", FALSE },
  /* https://github.com/flatpak/flatpak/pull/3733 */
  { "/os-release", TRUE }
};

static gboolean
do_line (GHashTable *fields,
         const char *path,
         gchar *line,
         gchar **message_out,
         GError **error)
{
  g_autofree gchar *unquoted = NULL;
  char *equals;

  /* Modify line in-place to strip leading and trailing whitespace */
  g_strstrip (line);

  if (line[0] == '\0' || line[0] == '#')
    return TRUE;

  g_debug ("%s: %s", path, line);

  equals = strchr (line, '=');

  if (equals == NULL)
    return glnx_throw (error, "Unable to parse line \"%s\" in %s: no \"=\" found",
                       line, path);

  unquoted = g_shell_unquote (equals + 1, error);

  if (unquoted == NULL)
    return glnx_prefix_error (error, "Unable to parse line \"%s\" in %s",
                              line, path);

  *equals = '\0';

  if (g_hash_table_contains (fields, line))
    {
      g_autofree gchar *message = NULL;

      message = g_strdup_printf ("%s appears more than once in %s, will use last instance",
                                 line, path);
      g_debug ("%s", message);

      if (message_out != NULL)
        *message_out = g_steal_pointer (&message);
    }

  g_hash_table_replace (fields, g_strdup (line), g_steal_pointer (&unquoted));
  return TRUE;
}

void
_srt_os_release_populate_from_data (SrtOsRelease *self,
                                    const char *path,
                                    const char *contents,
                                    gsize len,
                                    GString *messages)
{
  g_autoptr(GHashTable) fields = NULL;
  gsize j;
  gsize beginning_of_line;

  g_return_if_fail (!self->populated);
  g_return_if_fail (self->build_id == NULL);
  g_return_if_fail (self->id == NULL);
  g_return_if_fail (self->name == NULL);
  g_return_if_fail (self->pretty_name == NULL);
  g_return_if_fail (self->variant == NULL);
  g_return_if_fail (self->variant_id == NULL);
  g_return_if_fail (self->version_codename == NULL);
  g_return_if_fail (self->version_id == NULL);

  fields = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  beginning_of_line = 0;

  for (j = 0; j < len; j++)
    {
      if (contents[j] == '\n')
        {
          g_autoptr(GError) local_error = NULL;
          g_autofree gchar *line = g_strndup (&contents[beginning_of_line],
                                              j - beginning_of_line);
          g_autofree gchar *message = NULL;

          beginning_of_line = j + 1;

          if (!do_line (fields, path, line, &message, &local_error))
            {
              g_string_append (messages, local_error->message);
              g_string_append_c (messages, '\n');
              g_debug ("%s", local_error->message);
              continue;
            }

          if (message != NULL)
            {
              g_string_append (messages, message);
              g_string_append_c (messages, '\n');
            }
        }
    }

  /* Collect a possible partial line */
  if (beginning_of_line < len)
    {
      g_autofree gchar *line = g_strndup (&contents[beginning_of_line],
                                          len - beginning_of_line);
      g_autofree gchar *message = NULL;
      g_autoptr(GError) local_error = NULL;

      if (!do_line (fields, path, line, &message, &local_error))
        {
          g_string_append (messages, local_error->message);
          g_string_append_c (messages, '\n');
          g_debug ("%s", local_error->message);
        }

      if (message != NULL)
        {
          g_string_append (messages, message);
          g_string_append_c (messages, '\n');
        }
    }

  /* Special case for the Steam Runtime: Flatpak-style scout images have
   * historically not had a VERSION_CODENAME in os-release(5), but
   * we know that version 1 is scout, so let's add it. */
  if (!g_hash_table_contains (fields, "VERSION_CODENAME")
      && g_strcmp0 (g_hash_table_lookup (fields, "ID"), "steamrt") == 0
      && g_strcmp0 (g_hash_table_lookup (fields, "VERSION_ID"), "1") == 0)
    g_hash_table_replace (fields, g_strdup ("VERSION_CODENAME"), g_strdup ("scout"));

  /* Special case for the Steam Runtime: we got this wrong in the past. */
  if (g_strcmp0 (g_hash_table_lookup (fields, "ID_LIKE"), "ubuntu") == 0)
    g_hash_table_replace (fields, g_strdup ("ID_LIKE"), g_strdup ("ubuntu debian"));

  self->build_id = g_strdup (g_hash_table_lookup (fields, "BUILD_ID"));
  self->id = g_strdup (g_hash_table_lookup (fields, "ID"));
  self->id_like = g_strdup (g_hash_table_lookup (fields, "ID_LIKE"));
  self->name = g_strdup (g_hash_table_lookup (fields, "NAME"));
  self->pretty_name = g_strdup (g_hash_table_lookup (fields, "PRETTY_NAME"));
  self->variant = g_strdup (g_hash_table_lookup (fields, "VARIANT"));
  self->variant_id = g_strdup (g_hash_table_lookup (fields, "VARIANT_ID"));
  self->version_codename = g_strdup (g_hash_table_lookup (fields, "VERSION_CODENAME"));
  self->version_id = g_strdup (g_hash_table_lookup (fields, "VERSION_ID"));

  self->populated = TRUE;
}

G_GNUC_INTERNAL void
_srt_os_release_populate (SrtOsRelease *self,
                          SrtSysroot *sysroot,
                          GString *messages)
{
  gsize i;

  g_return_if_fail (_srt_check_not_setuid ());
  g_return_if_fail (!self->populated);
  g_return_if_fail (SRT_IS_SYSROOT (sysroot));

  for (i = 0; i < G_N_ELEMENTS (os_release_paths); i++)
    {
      g_autoptr(GError) local_error = NULL;
      const char *path = os_release_paths[i].path;
      gboolean only_in_run_host = os_release_paths[i].only_in_run_host;
      g_autofree gchar *contents = NULL;
      gsize len;

      if (only_in_run_host
          && !g_str_has_suffix (sysroot->path, "/run/host"))
        continue;

      if (!_srt_sysroot_load (sysroot, path,
                              SRT_RESOLVE_FLAGS_NONE,
                              NULL, &contents, &len, &local_error))
        {
          if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            {
              g_string_append (messages, local_error->message);
              g_string_append_c (messages, '\n');
            }

          g_debug ("%s", local_error->message);
          continue;
        }

      _srt_os_release_populate_from_data (self, path, contents, len, messages);
      return;
    }

  g_string_append (messages, "os-release(5) not found\n");
  self->populated = TRUE;
}

G_GNUC_INTERNAL void
_srt_os_release_clear (SrtOsRelease *self)
{
  g_clear_pointer (&self->build_id, g_free);
  g_clear_pointer (&self->id, g_free);
  g_clear_pointer (&self->id_like, g_free);
  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->pretty_name, g_free);
  g_clear_pointer (&self->variant, g_free);
  g_clear_pointer (&self->variant_id, g_free);
  g_clear_pointer (&self->version_codename, g_free);
  g_clear_pointer (&self->version_id, g_free);
  self->populated = FALSE;
}
