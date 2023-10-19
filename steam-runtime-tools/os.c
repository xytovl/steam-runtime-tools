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

static void
do_line (GHashTable *fields,
         const char *path,
         gchar *line)
{
  g_autofree gchar *unquoted = NULL;
  char *equals;
  GError *local_error = NULL;

  /* Modify line in-place to strip leading and trailing whitespace */
  g_strstrip (line);

  if (line[0] == '\0' || line[0] == '#')
    return;

  g_debug ("%s: %s", path, line);

  equals = strchr (line, '=');

  if (equals == NULL)
    {
      g_debug ("Unable to parse line \"%s\" in %s: no \"=\" found",
               line, path);
      return;
    }

  unquoted = g_shell_unquote (equals + 1, &local_error);

  if (unquoted == NULL)
    {
      g_debug ("Unable to parse line \"%s\" in %s: %s",
               line, path, local_error->message);
      g_clear_error (&local_error);
      return;
    }

  *equals = '\0';

  if (g_hash_table_contains (fields, line))
    {
      g_debug ("%s appears more than once in %s, will use last instance",
               line, path);
    }

  g_hash_table_replace (fields, g_strdup (line), g_steal_pointer (&unquoted));
}

void
_srt_os_release_populate_from_data (SrtOsRelease *self,
                                    const char *path,
                                    const char *contents,
                                    gsize len)
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
          g_autofree gchar *line = g_strndup (&contents[beginning_of_line],
                                              j - beginning_of_line);

          beginning_of_line = j + 1;

          do_line (fields, path, line);
        }
    }

  /* Collect a possible partial line */
  if (beginning_of_line < len)
    {
      g_autofree gchar *line = g_strndup (&contents[beginning_of_line],
                                          len - beginning_of_line);

      do_line (fields, path, line);
    }

  self->build_id = g_strdup (g_hash_table_lookup (fields, "BUILD_ID"));
  self->id = g_strdup (g_hash_table_lookup (fields, "ID"));
  self->id_like = g_strdup (g_hash_table_lookup (fields, "ID_LIKE"));
  self->name = g_strdup (g_hash_table_lookup (fields, "NAME"));
  self->pretty_name = g_strdup (g_hash_table_lookup (fields, "PRETTY_NAME"));
  self->variant = g_strdup (g_hash_table_lookup (fields, "VARIANT"));
  self->variant_id = g_strdup (g_hash_table_lookup (fields, "VARIANT_ID"));
  self->version_codename = g_strdup (g_hash_table_lookup (fields, "VERSION_CODENAME"));
  self->version_id = g_strdup (g_hash_table_lookup (fields, "VERSION_ID"));

  /* Special case for the Steam Runtime: Flatpak-style scout images have
   * historically not had a VERSION_CODENAME in os-release(5), but
   * we know that version 1 is scout, so let's add it. */
  if (self->version_codename == NULL
      && g_strcmp0 (self->id, "steamrt") == 0
      && g_strcmp0 (self->version_id, "1") == 0)
    self->version_codename = g_strdup ("scout");

  /* Special case for the Steam Runtime: we got this wrong in the past. */
  if (g_strcmp0 (self->id_like, "ubuntu") == 0)
    {
      g_free (self->id_like);
      self->id_like = g_strdup ("ubuntu debian");
    }

  self->populated = TRUE;
}

G_GNUC_INTERNAL void
_srt_os_release_populate (SrtOsRelease *self,
                          SrtSysroot *sysroot)
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
          g_debug ("%s", local_error->message);
          continue;
        }

      _srt_os_release_populate_from_data (self, path, contents, len);
      return;
    }

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
