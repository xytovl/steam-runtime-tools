/* vi:set et sw=2 sts=2 cin cino=t0,f0,(0,{s,>2s,n-s,^-s,e-s:
 * Taken from Flatpak
 * Last updated: Flatpak 1.15.10
 *
 * Copyright © 1995-1998 Free Software Foundation, Inc.
 * Copyright © 2014-2019 Red Hat, Inc
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 */

#include "config.h"

#if 0
#include <glib/gi18n-lib.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>
#include <termios.h>

#include <glib.h>
#include <gio/gunixoutputstream.h>

#include "flatpak-error.h"
#include "flatpak-utils-base-private.h"
#include "flatpak-utils-private.h"
#include "libglnx.h"
#if 0
#include "valgrind-private.h"
#else
#define RUNNING_ON_VALGRIND 0
#endif

/* This is also here so the common code can report these errors to the lib */
static const GDBusErrorEntry flatpak_error_entries[] = {
  {FLATPAK_ERROR_ALREADY_INSTALLED,     "org.freedesktop.Flatpak.Error.AlreadyInstalled"},
  {FLATPAK_ERROR_NOT_INSTALLED,         "org.freedesktop.Flatpak.Error.NotInstalled"},
  {FLATPAK_ERROR_ONLY_PULLED,           "org.freedesktop.Flatpak.Error.OnlyPulled"}, /* Since: 1.0 */
  {FLATPAK_ERROR_DIFFERENT_REMOTE,      "org.freedesktop.Flatpak.Error.DifferentRemote"}, /* Since: 1.0 */
  {FLATPAK_ERROR_ABORTED,               "org.freedesktop.Flatpak.Error.Aborted"}, /* Since: 1.0 */
  {FLATPAK_ERROR_SKIPPED,               "org.freedesktop.Flatpak.Error.Skipped"}, /* Since: 1.0 */
  {FLATPAK_ERROR_NEED_NEW_FLATPAK,      "org.freedesktop.Flatpak.Error.NeedNewFlatpak"}, /* Since: 1.0 */
  {FLATPAK_ERROR_REMOTE_NOT_FOUND,      "org.freedesktop.Flatpak.Error.RemoteNotFound"}, /* Since: 1.0 */
  {FLATPAK_ERROR_RUNTIME_NOT_FOUND,     "org.freedesktop.Flatpak.Error.RuntimeNotFound"}, /* Since: 1.0 */
  {FLATPAK_ERROR_DOWNGRADE,             "org.freedesktop.Flatpak.Error.Downgrade"}, /* Since: 1.0 */
  {FLATPAK_ERROR_INVALID_REF,           "org.freedesktop.Flatpak.Error.InvalidRef"}, /* Since: 1.0.3 */
  {FLATPAK_ERROR_INVALID_DATA,          "org.freedesktop.Flatpak.Error.InvalidData"}, /* Since: 1.0.3 */
  {FLATPAK_ERROR_UNTRUSTED,             "org.freedesktop.Flatpak.Error.Untrusted"}, /* Since: 1.0.3 */
  {FLATPAK_ERROR_SETUP_FAILED,          "org.freedesktop.Flatpak.Error.SetupFailed"}, /* Since: 1.0.3 */
  {FLATPAK_ERROR_EXPORT_FAILED,         "org.freedesktop.Flatpak.Error.ExportFailed"}, /* Since: 1.0.3 */
  {FLATPAK_ERROR_REMOTE_USED,           "org.freedesktop.Flatpak.Error.RemoteUsed"}, /* Since: 1.0.3 */
  {FLATPAK_ERROR_RUNTIME_USED,          "org.freedesktop.Flatpak.Error.RuntimeUsed"}, /* Since: 1.0.3 */
  {FLATPAK_ERROR_INVALID_NAME,          "org.freedesktop.Flatpak.Error.InvalidName"}, /* Since: 1.0.3 */
  {FLATPAK_ERROR_OUT_OF_SPACE,          "org.freedesktop.Flatpak.Error.OutOfSpace"}, /* Since: 1.2.0 */
  {FLATPAK_ERROR_WRONG_USER,            "org.freedesktop.Flatpak.Error.WrongUser"}, /* Since: 1.2.0 */
  {FLATPAK_ERROR_NOT_CACHED,            "org.freedesktop.Flatpak.Error.NotCached"}, /* Since: 1.3.3 */
  {FLATPAK_ERROR_REF_NOT_FOUND,         "org.freedesktop.Flatpak.Error.RefNotFound"}, /* Since: 1.4.0 */
  {FLATPAK_ERROR_PERMISSION_DENIED,     "org.freedesktop.Flatpak.Error.PermissionDenied"}, /* Since: 1.5.1 */
  {FLATPAK_ERROR_AUTHENTICATION_FAILED, "org.freedesktop.Flatpak.Error.AuthenticationFailed"}, /* Since: 1.7.3 */
  {FLATPAK_ERROR_NOT_AUTHORIZED,        "org.freedesktop.Flatpak.Error.NotAuthorized"}, /* Since: 1.7.3 */
};

GQuark
flatpak_error_quark (void)
{
  static volatile gsize quark_volatile = 0;

  g_dbus_error_register_error_domain ("flatpak-error-quark",
                                      &quark_volatile,
                                      flatpak_error_entries,
                                      G_N_ELEMENTS (flatpak_error_entries));
  return (GQuark) quark_volatile;
}

gboolean
flatpak_fail_error (GError **error, FlatpakError code, const char *fmt, ...)
{
  if (error == NULL)
    return FALSE;

  va_list args;
  va_start (args, fmt);
  GError *new = g_error_new_valist (FLATPAK_ERROR, code, fmt, args);
  va_end (args);
  g_propagate_error (error, g_steal_pointer (&new));
  return FALSE;
}

GBytes *
flatpak_zlib_compress_bytes (GBytes *bytes,
                             int level,
                             GError **error)
{
  g_autoptr(GZlibCompressor) compressor = NULL;
  g_autoptr(GOutputStream) out = NULL;
  g_autoptr(GOutputStream) mem = NULL;

  mem = g_memory_output_stream_new_resizable ();

  compressor = g_zlib_compressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP, level);
  out = g_converter_output_stream_new (mem, G_CONVERTER (compressor));

  if (!g_output_stream_write_all (out, g_bytes_get_data (bytes, NULL), g_bytes_get_size (bytes),
                                  NULL, NULL, error))
    return NULL;

  if (!g_output_stream_close (out, NULL, error))
    return NULL;

  return g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (mem));
}

GBytes *
flatpak_zlib_decompress_bytes (GBytes *bytes,
                               GError **error)
{
  g_autoptr(GZlibDecompressor) decompressor = NULL;
  g_autoptr(GOutputStream) out = NULL;
  g_autoptr(GOutputStream) mem = NULL;

  mem = g_memory_output_stream_new_resizable ();

  decompressor = g_zlib_decompressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP);
  out = g_converter_output_stream_new (mem, G_CONVERTER (decompressor));

  if (!g_output_stream_write_all (out, g_bytes_get_data (bytes, NULL), g_bytes_get_size (bytes),
                                  NULL, NULL, error))
    return NULL;

  if (!g_output_stream_close (out, NULL, error))
    return NULL;

  return g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (mem));
}

GBytes *
flatpak_read_stream (GInputStream *in,
                     gboolean      null_terminate,
                     GError      **error)
{
  g_autoptr(GOutputStream) mem_stream = NULL;

  mem_stream = g_memory_output_stream_new_resizable ();
  if (g_output_stream_splice (mem_stream, in,
                              0, NULL, error) < 0)
    return NULL;

  if (null_terminate)
    {
      if (!g_output_stream_write (G_OUTPUT_STREAM (mem_stream), "\0", 1, NULL, error))
        return NULL;
    }

  if (!g_output_stream_close (G_OUTPUT_STREAM (mem_stream), NULL, error))
    return NULL;

  return g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (mem_stream));
}

gint
flatpak_strcmp0_ptr (gconstpointer a,
                     gconstpointer b)
{
  return g_strcmp0 (*(char * const *) a, *(char * const *) b);
}

/* Sometimes this is /var/run which is a symlink, causing weird issues when we pass
 * it as a path into the sandbox */
char *
flatpak_get_real_xdg_runtime_dir (void)
{
  return realpath (g_get_user_runtime_dir (), NULL);
}

/* Compares if str has a specific path prefix. This differs
   from a regular prefix in two ways. First of all there may
   be multiple slashes separating the path elements, and
   secondly, if a prefix is matched that has to be en entire
   path element. For instance /a/prefix matches /a/prefix/foo/bar,
   but not /a/prefixfoo/bar. */
gboolean
flatpak_has_path_prefix (const char *str,
                         const char *prefix)
{
  while (TRUE)
    {
      /* Skip consecutive slashes to reach next path
         element */
      while (*str == '/')
        str++;
      while (*prefix == '/')
        prefix++;

      /* No more prefix path elements? Done! */
      if (*prefix == 0)
        return TRUE;

      /* Compare path element */
      while (*prefix != 0 && *prefix != '/')
        {
          if (*str != *prefix)
            return FALSE;
          str++;
          prefix++;
        }

      /* Matched prefix path element,
         must be entire str path element */
      if (*str != '/' && *str != 0)
        return FALSE;
    }
}

/* Returns end of matching path prefix, or NULL if no match */
const char *
flatpak_path_match_prefix (const char *pattern,
                           const char *string)
{
  char c, test;
  const char *tmp;

  while (*pattern == '/')
    pattern++;

  while (*string == '/')
    string++;

  while (TRUE)
    {
      switch (c = *pattern++)
        {
        case 0:
          if (*string == '/' || *string == 0)
            return string;
          return NULL;

        case '?':
          if (*string == '/' || *string == 0)
            return NULL;
          string++;
          break;

        case '*':
          c = *pattern;

          while (c == '*')
            c = *++pattern;

          /* special case * at end */
          if (c == 0)
            {
              tmp = strchr (string, '/');
              if (tmp != NULL)
                return tmp;
              return string + strlen (string);
            }
          else if (c == '/')
            {
              string = strchr (string, '/');
              if (string == NULL)
                return NULL;
              break;
            }

          while ((test = *string) != 0)
            {
              tmp = flatpak_path_match_prefix (pattern, string);
              if (tmp != NULL)
                return tmp;
              if (test == '/')
                break;
              string++;
            }
          return NULL;

        default:
          if (c != *string)
            return NULL;
          string++;
          break;
        }
    }
  return NULL; /* Should not be reached */
}

static const char *
flatpak_get_kernel_arch (void)
{
  static struct utsname buf;
  static const char *arch = NULL;
  char *m;

  if (arch != NULL)
    return arch;

  if (uname (&buf))
    {
      arch = "unknown";
      return arch;
    }

  /* By default, just pass on machine, good enough for most arches */
  arch = buf.machine;

  /* Override for some arches */

  m = buf.machine;
  /* i?86 */
  if (strlen (m) == 4 && m[0] == 'i' && m[2] == '8'  && m[3] == '6')
    {
      arch = "i386";
    }
  else if (g_str_has_prefix (m, "arm"))
    {
      if (g_str_has_suffix (m, "b"))
        arch = "armeb";
      else
        arch = "arm";
    }
  else if (strcmp (m, "mips") == 0)
    {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      arch = "mipsel";
#endif
    }
  else if (strcmp (m, "mips64") == 0)
    {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      arch = "mips64el";
#endif
    }

  return arch;
}

/* This maps the kernel-reported uname to a single string representing
 * the cpu family, in the sense that all members of this family would
 * be able to understand and link to a binary file with such cpu
 * opcodes. That doesn't necessarily mean that all members of the
 * family can run all opcodes, for instance for modern 32bit intel we
 * report "i386", even though they support instructions that the
 * original i386 cpu cannot run. Still, such an executable would
 * at least try to execute a 386, whereas an arm binary would not.
 */
const char *
flatpak_get_arch (void)
{
  /* Avoid using uname on multiarch machines, because uname reports the kernels
   * arch, and that may be different from userspace. If e.g. the kernel is 64bit and
   * the userspace is 32bit we want to use 32bit by default. So, we take the current build
   * arch as the default. */
#if defined(__i386__)
  return "i386";
#elif defined(__x86_64__)
  return "x86_64";
#elif defined(__aarch64__)
  return "aarch64";
#elif defined(__arm__)
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  return "arm";
#else
  return "armeb";
#endif
#else
  return flatpak_get_kernel_arch ();
#endif
}

gboolean
flatpak_is_linux32_arch (const char *arch)
{
  const char *kernel_arch = flatpak_get_kernel_arch ();

  if (strcmp (kernel_arch, "x86_64") == 0 &&
      strcmp (arch, "i386") == 0)
    return TRUE;

  if (strcmp (kernel_arch, "aarch64") == 0 &&
      strcmp (arch, "arm") == 0)
    return TRUE;

  return FALSE;
}

static struct
{
  const char *kernel_arch;
  const char *compat_arch;
} compat_arches[] = {
  { "x86_64", "i386" },
  { "aarch64", "arm" },
};

const char *
flatpak_get_compat_arch (const char *kernel_arch)
{
  int i;

  /* Also add all other arches that are compatible with the kernel arch */
  for (i = 0; i < G_N_ELEMENTS (compat_arches); i++)
    {
      if (strcmp (compat_arches[i].kernel_arch, kernel_arch) == 0)
        return compat_arches[i].compat_arch;
    }

  return NULL;
}

const char *
flatpak_get_compat_arch_reverse (const char *compat_arch)
{
  int i;

  /* Also add all other arches that are compatible with the kernel arch */
  for (i = 0; i < G_N_ELEMENTS (compat_arches); i++)
    {
      if (strcmp (compat_arches[i].compat_arch, compat_arch) == 0)
        return compat_arches[i].kernel_arch;
    }

  return NULL;
}

/* Get all compatible arches for this host in order of priority */
const char **
flatpak_get_arches (void)
{
  static gsize arches = 0;

  if (g_once_init_enter (&arches))
    {
      gsize new_arches = 0;
      const char *main_arch = flatpak_get_arch ();
      const char *kernel_arch = flatpak_get_kernel_arch ();
      const char *compat_arch;
      GPtrArray *array = g_ptr_array_new ();

      /* This is the userspace arch, i.e. the one flatpak itself was
         build for. It's always first. */
      g_ptr_array_add (array, (char *) main_arch);

      compat_arch = flatpak_get_compat_arch (kernel_arch);
      if (g_strcmp0 (compat_arch, main_arch) != 0)
        g_ptr_array_add (array, (char *) compat_arch);

      g_ptr_array_add (array, NULL);
      new_arches = (gsize) g_ptr_array_free (array, FALSE);

      g_once_init_leave (&arches, new_arches);
    }

  return (const char **) arches;
}

const char **
flatpak_get_gl_drivers (void)
{
  static gsize drivers = 0;

  if (g_once_init_enter (&drivers))
    {
      gsize new_drivers;
      char **new_drivers_c = 0;
      const char *env = g_getenv ("FLATPAK_GL_DRIVERS");
      if (env != NULL && *env != 0)
        new_drivers_c = g_strsplit (env, ":", -1);
      else
        {
          g_autofree char *nvidia_version = NULL;
          char *dot;
          GPtrArray *array = g_ptr_array_new ();

          if (g_file_get_contents ("/sys/module/nvidia/version",
                                   &nvidia_version, NULL, NULL))
            {
              g_strstrip (nvidia_version);
              /* Convert dots to dashes */
              while ((dot = strchr (nvidia_version, '.')) != NULL)
                *dot = '-';
              g_ptr_array_add (array, g_strconcat ("nvidia-", nvidia_version, NULL));
            }

          g_ptr_array_add (array, (char *) "default");
          g_ptr_array_add (array, (char *) "host");

          g_ptr_array_add (array, NULL);
          new_drivers_c = (char **) g_ptr_array_free (array, FALSE);
        }

      new_drivers = (gsize) new_drivers_c;
      g_once_init_leave (&drivers, new_drivers);
    }

  return (const char **) drivers;
}

static gboolean
flatpak_get_have_intel_gpu (void)
{
  static int have_intel = -1;

  if (have_intel == -1)
    have_intel = g_file_test ("/sys/module/i915", G_FILE_TEST_EXISTS);

  return have_intel;
}

static GHashTable *
load_kernel_module_list (void)
{
  GHashTable *modules = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  g_autofree char *modules_data = NULL;
  g_autoptr(GError) error = NULL;
  char *start, *end;

  if (!g_file_get_contents ("/proc/modules", &modules_data, NULL, &error))
    {
      g_info ("Failed to read /proc/modules: %s", error->message);
      return modules;
    }

  /* /proc/modules is a table of modules.
   * Columns are split by spaces and rows by newlines.
   * The first column is the name. */
  start = modules_data;
  while (TRUE)
    {
      end = strchr (start, ' ');
      if (end == NULL)
        break;

      g_hash_table_add (modules, g_strndup (start, (end - start)));

      start = strchr (end, '\n');
      if (start == NULL)
        break;

      start++;
    }

  return modules;
}

static gboolean
flatpak_get_have_kernel_module (const char *module_name)
{
  static GHashTable *kernel_modules = NULL;

  if (g_once_init_enter (&kernel_modules))
    g_once_init_leave (&kernel_modules, load_kernel_module_list ());

  return g_hash_table_contains (kernel_modules, module_name);
}

static const char *
flatpak_get_gtk_theme (void)
{
  static char *gtk_theme;

  if (g_once_init_enter (&gtk_theme))
    {
      /* The schema may not be installed so check first */
      GSettingsSchemaSource *source = g_settings_schema_source_get_default ();
      g_autoptr(GSettingsSchema) schema = NULL;

      if (source == NULL)
        g_once_init_leave (&gtk_theme, g_strdup (""));
      else
        {
          schema = g_settings_schema_source_lookup (source,
                                                    "org.gnome.desktop.interface", TRUE);

          if (schema == NULL)
            g_once_init_leave (&gtk_theme, g_strdup (""));
          else
            {
              /* GSettings is used to store the theme if you use Wayland or GNOME.
               * TODO: Check XSettings Net/ThemeName for other desktops.
               * We don't care about any other method (like settings.ini) because they
               *   aren't passed through the sandbox anyway. */
              g_autoptr(GSettings) settings = g_settings_new ("org.gnome.desktop.interface");
              g_once_init_leave (&gtk_theme, g_settings_get_string (settings, "gtk-theme"));
            }
        }
    }

  return (const char *) gtk_theme;
}

#if 0
/* Not used in pressure-vessel, use _srt_check_bwrap() instead */
const char *
flatpak_get_bwrap (void)
{
  const char *e = g_getenv ("FLATPAK_BWRAP");

  if (e != NULL)
    return e;
  return HELPER;
}

gboolean
flatpak_bwrap_is_unprivileged (void)
{
  g_autofree char *path = g_find_program_in_path (flatpak_get_bwrap ());
  struct stat st;

  /* Various features are supported only if bwrap exists and is not setuid */
  return
    path != NULL &&
    stat (path, &st) == 0 &&
    (st.st_mode & S_ISUID) == 0;
}
#endif

static char *
line_get_word (char **line)
{
  char *word = NULL;

  while (g_ascii_isspace (**line))
    (*line)++;

  if (**line == 0)
    return NULL;

  word = *line;

  while (**line && !g_ascii_isspace (**line))
    (*line)++;

  if (**line)
    {
      **line = 0;
      (*line)++;
    }

  return word;
}

char *
flatpak_filter_glob_to_regexp (const char  *glob,
                               gboolean     runtime_only,
                               GError     **error)
{
  g_autoptr(GString) regexp = g_string_new ("");
  int parts = 1;
  gboolean empty_part;

  if (g_str_has_prefix (glob, "app/"))
    {
      if (runtime_only)
        {
          flatpak_fail_error (error, FLATPAK_ERROR_INVALID_DATA, _("Glob can't match apps"));
          return NULL;
        }
      else
        {
          glob += strlen ("app/");
          g_string_append (regexp, "app/");
        }
    }
  else if (g_str_has_prefix (glob, "runtime/"))
    {
      glob += strlen ("runtime/");
      g_string_append (regexp, "runtime/");
    }
  else
    {
      if (runtime_only)
        g_string_append (regexp, "runtime/");
      else
        g_string_append (regexp, "(app|runtime)/");
    }

  /* We really need an id part, the rest is optional */
  if (*glob == 0)
    {
      flatpak_fail_error (error, FLATPAK_ERROR_INVALID_DATA, _("Empty glob"));
      return NULL;
    }

  empty_part = TRUE;
  while (*glob != 0)
    {
      char c = *glob;
      glob++;

      if (c == '/')
        {
          if (empty_part)
            g_string_append (regexp, "[.\\-_a-zA-Z0-9]*");
          empty_part = TRUE;
          parts++;
          g_string_append (regexp, "/");
          if (parts > 3)
            {
              flatpak_fail_error (error, FLATPAK_ERROR_INVALID_DATA, _("Too many segments in glob"));
              return NULL;
            }
        }
      else if (c == '*')
        {
          empty_part = FALSE;
         g_string_append (regexp, "[.\\-_a-zA-Z0-9]*");
        }
      else if (c == '.')
        {
          empty_part = FALSE;
          g_string_append (regexp, "\\.");
        }
      else if (g_ascii_isalnum (c) || c == '-' || c == '_')
        {
          empty_part = FALSE;
          g_string_append_c (regexp, c);
        }
      else
        {
          flatpak_fail_error (error, FLATPAK_ERROR_INVALID_DATA, _("Invalid glob character '%c'"), c);
          return NULL;
        }
    }

  while (parts < 3)
    {
      parts++;
      g_string_append (regexp, "/[.\\-_a-zA-Z0-9]*");
    }

  return g_string_free (g_steal_pointer (&regexp), FALSE);
}

gboolean
flatpak_parse_filters (const char *data,
                       GRegex **allow_refs_out,
                       GRegex **deny_refs_out,
                       GError **error)
{
  g_auto(GStrv) lines = NULL;
  int i;
  g_autoptr(GString) allow_regexp = g_string_new ("^(");
  g_autoptr(GString) deny_regexp = g_string_new ("^(");
  gboolean has_allow = FALSE;
  gboolean has_deny = FALSE;
  g_autoptr(GRegex) allow_refs = NULL;
  g_autoptr(GRegex) deny_refs = NULL;

  lines = g_strsplit (data, "\n", -1);
  for (i = 0; lines[i] != NULL; i++)
    {
      char *line = lines[i];
      char *comment, *command;

      /* Ignore shell-style comments */
      comment = strchr (line, '#');
      if (comment != NULL)
        *comment = 0;

      command = line_get_word (&line);
      /* Ignore empty lines */
      if (command == NULL)
        continue;

      if (strcmp (command, "allow") == 0 || strcmp (command, "deny") == 0)
        {
          char *glob, *next;
          g_autofree char *ref_regexp = NULL;
          GString *command_regexp;
          gboolean *has_type = NULL;

          glob = line_get_word (&line);
          if (glob == NULL)
            return flatpak_fail_error (error, FLATPAK_ERROR_INVALID_DATA, _("Missing glob on line %d"), i + 1);

          next = line_get_word (&line);
          if (next != NULL)
            return flatpak_fail_error (error, FLATPAK_ERROR_INVALID_DATA, _("Trailing text on line %d"), i + 1);

          ref_regexp = flatpak_filter_glob_to_regexp (glob, FALSE, error);
          if (ref_regexp == NULL)
            return glnx_prefix_error (error, _("on line %d"), i + 1);

          if (strcmp (command, "allow") == 0)
            {
              command_regexp = allow_regexp;
              has_type = &has_allow;
            }
          else
            {
              command_regexp = deny_regexp;
              has_type = &has_deny;
            }

          if (*has_type)
            g_string_append (command_regexp, "|");
          else
            *has_type = TRUE;

          g_string_append (command_regexp, ref_regexp);
        }
      else
        {
          return flatpak_fail_error (error, FLATPAK_ERROR_INVALID_DATA, _("Unexpected word '%s' on line %d"), command, i + 1);
        }
    }

  g_string_append (allow_regexp, ")$");
  g_string_append (deny_regexp, ")$");

  if (allow_regexp)
    {
      allow_refs = g_regex_new (allow_regexp->str, G_REGEX_DOLLAR_ENDONLY|G_REGEX_RAW|G_REGEX_OPTIMIZE, G_REGEX_MATCH_ANCHORED, error);
      if (allow_refs == NULL)
        return FALSE;
    }

  if (deny_regexp)
    {
      deny_refs = g_regex_new (deny_regexp->str, G_REGEX_DOLLAR_ENDONLY|G_REGEX_RAW|G_REGEX_OPTIMIZE, G_REGEX_MATCH_ANCHORED, error);
      if (deny_refs == NULL)
        return FALSE;
    }

  *allow_refs_out = g_steal_pointer (&allow_refs);
  *deny_refs_out = g_steal_pointer (&deny_refs);

  return TRUE;
}

gboolean
flatpak_filters_allow_ref (GRegex *allow_refs,
                           GRegex *deny_refs,
                           const char *ref)
{
  if (deny_refs == NULL)
    return TRUE; /* All refs are allowed by default */

  if (!g_regex_match (deny_refs, ref, G_REGEX_MATCH_ANCHORED, NULL))
    return TRUE; /* Not denied */

  if (allow_refs &&  g_regex_match (allow_refs, ref, G_REGEX_MATCH_ANCHORED, NULL))
    return TRUE; /* Explicitly allowed */

  return FALSE;
}

static gboolean
remove_dangling_symlinks (int           parent_fd,
                          const char   *name,
                          GCancellable *cancellable,
                          GError      **error)
{
  gboolean ret = FALSE;
  struct dirent *dent;
  g_auto(GLnxDirFdIterator) iter = { 0 };

  if (!glnx_dirfd_iterator_init_at (parent_fd, name, FALSE, &iter, error))
    goto out;

  while (TRUE)
    {
      if (!glnx_dirfd_iterator_next_dent_ensure_dtype (&iter, &dent, cancellable, error))
        goto out;

      if (dent == NULL)
        break;

      if (dent->d_type == DT_DIR)
        {
          if (!remove_dangling_symlinks (iter.fd, dent->d_name, cancellable, error))
            goto out;
        }
      else if (dent->d_type == DT_LNK)
        {
          struct stat stbuf;
          if (fstatat (iter.fd, dent->d_name, &stbuf, 0) != 0 && errno == ENOENT)
            {
              if (unlinkat (iter.fd, dent->d_name, 0) != 0)
                {
                  glnx_set_error_from_errno (error);
                  goto out;
                }
            }
        }
    }

  ret = TRUE;
out:

  return ret;
}

gboolean
flatpak_remove_dangling_symlinks (GFile        *dir,
                                  GCancellable *cancellable,
                                  GError      **error)
{
  gboolean ret = FALSE;

  /* The fd is closed by this call */
  if (!remove_dangling_symlinks (AT_FDCWD, flatpak_file_get_path_cached (dir),
                                 cancellable, error))
    goto out;

  ret = TRUE;

out:
  return ret;
}

/* This atomically replaces a symlink with a new value, removing the
 * existing symlink target, if it exstis and is different from
 * @target. This is atomic in the sense that we're guaranteed to
 * remove any existing symlink target (once), independent of how many
 * processes do the same operation in parallele. However, it is still
 * possible that we remove the old and then fail to create the new
 * symlink for some reason, ending up with neither the old or the new
 * target. That is fine if the reason for the symlink is keeping a
 * cache though.
 */
gboolean
flatpak_switch_symlink_and_remove (const char *symlink_path,
                                   const char *target,
                                   GError    **error)
{
  g_autofree char *symlink_dir = g_path_get_dirname (symlink_path);
  int try;

  for (try = 0; try < 100; try++)
    {
      g_autofree char *tmp_path = NULL;
      int fd;

      /* Try to atomically create the symlink */
      if (TEMP_FAILURE_RETRY (symlink (target, symlink_path)) == 0)
        return TRUE;

      if (errno != EEXIST)
        {
          /* Unexpected failure, bail */
          glnx_set_error_from_errno (error);
          return FALSE;
        }

      /* The symlink existed, move it to a temporary name atomically, and remove target
         if that succeeded. */
      tmp_path = g_build_filename (symlink_dir, ".switched-symlink-XXXXXX", NULL);

      fd = g_mkstemp_full (tmp_path, O_RDWR, 0644);
      if (fd == -1)
        {
          glnx_set_error_from_errno (error);
          return FALSE;
        }
      close (fd);

      if (TEMP_FAILURE_RETRY (rename (symlink_path, tmp_path)) == 0)
        {
          /* The move succeeded, now we can remove the old target */
          g_autofree char *old_target = flatpak_readlink (tmp_path, error);
          if (old_target == NULL)
            return FALSE;
          if (strcmp (old_target, target) != 0) /* Don't remove old file if its the same as the new one */
            {
              g_autofree char *old_target_path = g_build_filename (symlink_dir, old_target, NULL);
              unlink (old_target_path);
            }
        }
      else if (errno != ENOENT)
        {
          glnx_set_error_from_errno (error);
          unlink (tmp_path);
          return -1;
        }
      unlink (tmp_path);

      /* An old target was removed, try again */
    }

  return flatpak_fail (error, "flatpak_switch_symlink_and_remove looped too many times");
}

gboolean
flatpak_argument_needs_quoting (const char *arg)
{
  if (*arg == '\0')
    return TRUE;

  while (*arg != 0)
    {
      char c = *arg;
      if (!g_ascii_isalnum (c) &&
          !(c == '-' || c == '/' || c == '~' ||
            c == ':' || c == '.' || c == '_' ||
            c == '=' || c == '@'))
        return TRUE;
      arg++;
    }
  return FALSE;
}

char *
flatpak_quote_argv (const char *argv[],
                    gssize      len)
{
  GString *res = g_string_new ("");
  int i;

  if (len == -1)
    len = g_strv_length ((char **) argv);

  for (i = 0; i < len; i++)
    {
      if (i != 0)
        g_string_append_c (res, ' ');

      if (flatpak_argument_needs_quoting (argv[i]))
        {
          g_autofree char *quoted = g_shell_quote (argv[i]);
          g_string_append (res, quoted);
        }
      else
        g_string_append (res, argv[i]);
    }

  return g_string_free (res, FALSE);
}

/* This is useful, because it handles escaped characters in uris, and ? arguments at the end of the uri */
gboolean
flatpak_file_arg_has_suffix (const char *arg, const char *suffix)
{
  g_autoptr(GFile) file = g_file_new_for_commandline_arg (arg);
  g_autofree char *basename = g_file_get_basename (file);

  return g_str_has_suffix (basename, suffix);
}

GFile *
flatpak_build_file_va (GFile  *base,
                       va_list args)
{
  g_autoptr(GFile) res = g_object_ref (base);
  const gchar *arg;

  while ((arg = va_arg (args, const gchar *)))
    {
      g_autoptr(GFile) child = g_file_resolve_relative_path (res, arg);
      g_set_object (&res, child);
    }

  return g_steal_pointer (&res);
}

GFile *
flatpak_build_file (GFile *base, ...)
{
  GFile *res;
  va_list args;

  va_start (args, base);
  res = flatpak_build_file_va (base, args);
  va_end (args);

  return res;
}

const char *
flatpak_file_get_path_cached (GFile *file)
{
  const char *path;
  static GQuark _file_path_quark = 0;

  if (G_UNLIKELY (_file_path_quark == 0))
    _file_path_quark = g_quark_from_static_string ("flatpak-file-path");

  do
    {
      path = g_object_get_qdata ((GObject *) file, _file_path_quark);
      if (path == NULL)
        {
          g_autofree char *new_path = NULL;
          new_path = g_file_get_path (file);
          if (new_path == NULL)
            return NULL;

          if (g_object_replace_qdata ((GObject *) file, _file_path_quark,
                                      NULL, new_path, g_free, NULL))
            path = g_steal_pointer (&new_path);
        }
    }
  while (path == NULL);

  return path;
}

gboolean
flatpak_openat_noatime (int           dfd,
                        const char   *name,
                        int          *ret_fd,
                        GCancellable *cancellable,
                        GError      **error)
{
  int fd;
  int flags = O_RDONLY | O_CLOEXEC;

#ifdef O_NOATIME
  do
    fd = openat (dfd, name, flags | O_NOATIME, 0);
  while (G_UNLIKELY (fd == -1 && errno == EINTR));
  /* Only the owner or superuser may use O_NOATIME; so we may get
   * EPERM.  EINVAL may happen if the kernel is really old...
   */
  if (fd == -1 && (errno == EPERM || errno == EINVAL))
#endif
  do
    fd = openat (dfd, name, flags, 0);
  while (G_UNLIKELY (fd == -1 && errno == EINTR));

  if (fd == -1)
    {
      glnx_set_error_from_errno (error);
      return FALSE;
    }
  else
    {
      *ret_fd = fd;
      return TRUE;
    }
}

gboolean
flatpak_cp_a (GFile         *src,
              GFile         *dest,
              FlatpakCpFlags flags,
              GCancellable  *cancellable,
              GError       **error)
{
  gboolean ret = FALSE;
  GFileEnumerator *enumerator = NULL;
  GFileInfo *src_info = NULL;
  GFile *dest_child = NULL;
  int dest_dfd = -1;
  gboolean merge = (flags & FLATPAK_CP_FLAGS_MERGE) != 0;
  gboolean no_chown = (flags & FLATPAK_CP_FLAGS_NO_CHOWN) != 0;
  gboolean move = (flags & FLATPAK_CP_FLAGS_MOVE) != 0;
  g_autoptr(GFileInfo) child_info = NULL;
  GError *temp_error = NULL;
  int r;

  enumerator = g_file_enumerate_children (src, "standard::type,standard::name,unix::uid,unix::gid,unix::mode",
                                          G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                          cancellable, error);
  if (!enumerator)
    goto out;

  src_info = g_file_query_info (src, "standard::name,unix::mode,unix::uid,unix::gid," \
                                     "time::modified,time::modified-usec,time::access,time::access-usec",
                                G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                cancellable, error);
  if (!src_info)
    goto out;

  do
    r = mkdir (flatpak_file_get_path_cached (dest), 0755);
  while (G_UNLIKELY (r == -1 && errno == EINTR));
  if (r == -1 &&
      (!merge || errno != EEXIST))
    {
      glnx_set_error_from_errno (error);
      goto out;
    }

  if (!glnx_opendirat (AT_FDCWD, flatpak_file_get_path_cached (dest), TRUE,
                       &dest_dfd, error))
    goto out;

  if (!no_chown)
    {
      do
        r = fchown (dest_dfd,
                    g_file_info_get_attribute_uint32 (src_info, "unix::uid"),
                    g_file_info_get_attribute_uint32 (src_info, "unix::gid"));
      while (G_UNLIKELY (r == -1 && errno == EINTR));
      if (r == -1)
        {
          glnx_set_error_from_errno (error);
          goto out;
        }
    }

  do
    r = fchmod (dest_dfd, g_file_info_get_attribute_uint32 (src_info, "unix::mode"));
  while (G_UNLIKELY (r == -1 && errno == EINTR));

  if (dest_dfd != -1)
    {
      (void) close (dest_dfd);
      dest_dfd = -1;
    }

  while ((child_info = g_file_enumerator_next_file (enumerator, cancellable, &temp_error)))
    {
      const char *name = g_file_info_get_name (child_info);
      g_autoptr(GFile) src_child = g_file_get_child (src, name);

      if (dest_child)
        g_object_unref (dest_child);
      dest_child = g_file_get_child (dest, name);

      if (g_file_info_get_file_type (child_info) == G_FILE_TYPE_DIRECTORY)
        {
          if (!flatpak_cp_a (src_child, dest_child, flags,
                             cancellable, error))
            goto out;
        }
      else
        {
          (void) unlink (flatpak_file_get_path_cached (dest_child));
          GFileCopyFlags copyflags = G_FILE_COPY_OVERWRITE | G_FILE_COPY_NOFOLLOW_SYMLINKS;
          if (!no_chown)
            copyflags |= G_FILE_COPY_ALL_METADATA;
          if (move)
            {
              if (!g_file_move (src_child, dest_child, copyflags,
                                cancellable, NULL, NULL, error))
                goto out;
            }
          else
            {
              if (!g_file_copy (src_child, dest_child, copyflags,
                                cancellable, NULL, NULL, error))
                goto out;
            }
        }

      g_clear_object (&child_info);
    }

  if (temp_error != NULL)
    {
      g_propagate_error (error, temp_error);
      goto out;
    }

  if (move &&
      !g_file_delete (src, NULL, error))
    goto out;

  ret = TRUE;
out:
  if (dest_dfd != -1)
    (void) close (dest_dfd);
  g_clear_object (&src_info);
  g_clear_object (&enumerator);
  g_clear_object (&dest_child);
  return ret;
}

static gboolean
_flatpak_canonicalize_permissions (int         parent_dfd,
                                   const char *rel_path,
                                   gboolean    toplevel,
                                   int         uid,
                                   int         gid,
                                   GError    **error)
{
  struct stat stbuf;
  gboolean res = TRUE;

  /* Note, in order to not leave non-canonical things around in case
   * of error, this continues after errors, but returns the first
   * error. */

  if (TEMP_FAILURE_RETRY (fstatat (parent_dfd, rel_path, &stbuf, AT_SYMLINK_NOFOLLOW)) != 0)
    {
      glnx_set_error_from_errno (error);
      return FALSE;
    }

  if ((uid != -1 && uid != stbuf.st_uid) || (gid != -1 && gid != stbuf.st_gid))
    {
      if (TEMP_FAILURE_RETRY (fchownat (parent_dfd, rel_path, uid, gid, AT_SYMLINK_NOFOLLOW)) != 0)
        {
          glnx_set_error_from_errno (error);
          return FALSE;
        }

      /* Re-read st_mode for new owner */
      if (TEMP_FAILURE_RETRY (fstatat (parent_dfd, rel_path, &stbuf, AT_SYMLINK_NOFOLLOW)) != 0)
        {
          glnx_set_error_from_errno (error);
          return FALSE;
        }
    }

  if (S_ISDIR (stbuf.st_mode))
    {
      g_auto(GLnxDirFdIterator) dfd_iter = { 0, };

      /* For the toplevel we set to 0700 so we can modify it, but not
         expose any non-canonical files to any other user, then we set
         it to 0755 afterwards. */
      if (fchmodat (parent_dfd, rel_path, toplevel ? 0700 : 0755, 0) != 0)
        {
          glnx_set_error_from_errno (error);
          error = NULL;
          res = FALSE;
        }

      if (glnx_dirfd_iterator_init_at (parent_dfd, rel_path, FALSE, &dfd_iter, NULL))
        {
          while (TRUE)
            {
              struct dirent *dent;

              if (!glnx_dirfd_iterator_next_dent (&dfd_iter, &dent, NULL, NULL) || dent == NULL)
                break;

              if (!_flatpak_canonicalize_permissions (dfd_iter.fd, dent->d_name, FALSE, uid, gid, error))
                {
                  error = NULL;
                  res = FALSE;
                }
            }
        }

      if (toplevel &&
          fchmodat (parent_dfd, rel_path, 0755, 0) != 0)
        {
          glnx_set_error_from_errno (error);
          error = NULL;
          res = FALSE;
        }

      return res;
    }
  else if (S_ISREG (stbuf.st_mode))
    {
      mode_t mode;

      /* If use can execute, make executable by all */
      if (stbuf.st_mode & S_IXUSR)
        mode = 0755;
      else /* otherwise executable by none */
        mode = 0644;

      if (fchmodat (parent_dfd, rel_path, mode, 0) != 0)
        {
          glnx_set_error_from_errno (error);
          res = FALSE;
        }
    }
  else if (S_ISLNK (stbuf.st_mode))
    {
      /* symlinks have no permissions */
    }
  else
    {
      /* some weird non-canonical type, lets delete it */
      if (unlinkat (parent_dfd, rel_path, 0) != 0)
        {
          glnx_set_error_from_errno (error);
          res = FALSE;
        }
    }

  return res;
}

/* Canonicalizes files to the same permissions as bare-user-only checkouts */
gboolean
flatpak_canonicalize_permissions (int         parent_dfd,
                                  const char *rel_path,
                                  int         uid,
                                  int         gid,
                                  GError    **error)
{
  return _flatpak_canonicalize_permissions (parent_dfd, rel_path, TRUE, uid, gid, error);
}

/* Make a directory, and its parent. Don't error if it already exists.
 * If you want a failure mode with EEXIST, use g_file_make_directory_with_parents. */
gboolean
flatpak_mkdir_p (GFile        *dir,
                 GCancellable *cancellable,
                 GError      **error)
{
  return glnx_shutil_mkdir_p_at (AT_FDCWD,
                                 flatpak_file_get_path_cached (dir),
                                 0777,
                                 cancellable,
                                 error);
}

gboolean
flatpak_rm_rf (GFile        *dir,
               GCancellable *cancellable,
               GError      **error)
{
  return glnx_shutil_rm_rf_at (AT_FDCWD,
                               flatpak_file_get_path_cached (dir),
                               cancellable, error);
}

gboolean
flatpak_file_rename (GFile        *from,
                     GFile        *to,
                     GCancellable *cancellable,
                     GError      **error)
{
  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    return FALSE;

  if (rename (flatpak_file_get_path_cached (from),
              flatpak_file_get_path_cached (to)) < 0)
    {
      glnx_set_error_from_errno (error);
      return FALSE;
    }

  return TRUE;
}

/* If memfd_create() is available, generate a sealed memfd with contents of
 * @str. Otherwise use an O_TMPFILE @tmpf in anonymous mode, write @str to
 * @tmpf, and lseek() back to the start. See also similar uses in e.g.
 * rpm-ostree for running dracut.
 */
gboolean
flatpak_buffer_to_sealed_memfd_or_tmpfile (GLnxTmpfile *tmpf,
                                           const char  *name,
                                           const char  *str,
                                           size_t       len,
                                           GError     **error)
{
  if (len == -1)
    len = strlen (str);
  glnx_autofd int memfd = memfd_create (name, MFD_CLOEXEC | MFD_ALLOW_SEALING);
  int fd; /* Unowned */
  if (memfd != -1)
    {
      fd = memfd;
    }
  else
    {
      /* We use an anonymous fd (i.e. O_EXCL) since we don't want
       * the target container to potentially be able to re-link it.
       */
      if (!G_IN_SET (errno, ENOSYS, EOPNOTSUPP))
        return glnx_throw_errno_prefix (error, "memfd_create");
      if (!glnx_open_anonymous_tmpfile (O_RDWR | O_CLOEXEC, tmpf, error))
        return FALSE;
      fd = tmpf->fd;
    }
  if (ftruncate (fd, len) < 0)
    return glnx_throw_errno_prefix (error, "ftruncate");
  if (glnx_loop_write (fd, str, len) < 0)
    return glnx_throw_errno_prefix (error, "write");
  if (lseek (fd, 0, SEEK_SET) < 0)
    return glnx_throw_errno_prefix (error, "lseek");
  if (memfd != -1)
    {
      /* Valgrind doesn't currently handle G_ADD_SEALS, so lets not seal when debugging... */
      if ((!RUNNING_ON_VALGRIND) &&
          fcntl (memfd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL) < 0)
        return glnx_throw_errno_prefix (error, "fcntl(F_ADD_SEALS)");
      /* The other values can stay default */
      tmpf->fd = g_steal_fd (&memfd);
      tmpf->initialized = TRUE;
    }
  return TRUE;
}

gboolean
flatpak_open_in_tmpdir_at (int             tmpdir_fd,
                           int             mode,
                           char           *tmpl,
                           GOutputStream **out_stream,
                           GCancellable   *cancellable,
                           GError        **error)
{
  const int max_attempts = 128;
  int i;
  int fd;

  /* 128 attempts seems reasonable... */
  for (i = 0; i < max_attempts; i++)
    {
      glnx_gen_temp_name (tmpl);

      do
        fd = openat (tmpdir_fd, tmpl, O_WRONLY | O_CREAT | O_EXCL, mode);
      while (fd == -1 && errno == EINTR);
      if (fd < 0 && errno != EEXIST)
        {
          glnx_set_error_from_errno (error);
          return FALSE;
        }
      else if (fd != -1)
        break;
    }
  if (i == max_attempts)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Exhausted attempts to open temporary file");
      return FALSE;
    }

  if (out_stream)
    *out_stream = g_unix_output_stream_new (fd, TRUE);
  else
    (void) close (fd);

  return TRUE;
}

gboolean
flatpak_bytes_save (GFile        *dest,
                    GBytes       *bytes,
                    GCancellable *cancellable,
                    GError      **error)
{
  g_autoptr(GOutputStream) out = NULL;

  out = (GOutputStream *) g_file_replace (dest, NULL, FALSE,
                                          G_FILE_CREATE_REPLACE_DESTINATION,
                                          cancellable, error);
  if (out == NULL)
    return FALSE;

  if (!g_output_stream_write_all (out,
                                  g_bytes_get_data (bytes, NULL),
                                  g_bytes_get_size (bytes),
                                  NULL,
                                  cancellable,
                                  error))
    return FALSE;

  if (!g_output_stream_close (out, cancellable, error))
    return FALSE;

  return TRUE;
}

gboolean
flatpak_variant_save (GFile        *dest,
                      GVariant     *variant,
                      GCancellable *cancellable,
                      GError      **error)
{
  g_autoptr(GOutputStream) out = NULL;
  gsize bytes_written;

  out = (GOutputStream *) g_file_replace (dest, NULL, FALSE,
                                          G_FILE_CREATE_REPLACE_DESTINATION,
                                          cancellable, error);
  if (out == NULL)
    return FALSE;

  if (!g_output_stream_write_all (out,
                                  g_variant_get_data (variant),
                                  g_variant_get_size (variant),
                                  &bytes_written,
                                  cancellable,
                                  error))
    return FALSE;

  if (!g_output_stream_close (out, cancellable, error))
    return FALSE;

  return TRUE;
}

char *
flatpak_keyfile_get_string_non_empty (GKeyFile   *keyfile,
                                      const char *group,
                                      const char *key)
{
  g_autofree char *value = NULL;

  value = g_key_file_get_string (keyfile, group, key, NULL);
  if (value != NULL && *value == '\0')
    g_clear_pointer (&value, g_free);

  return g_steal_pointer (&value);
}

gboolean
flatpak_extension_matches_reason (const char *extension_id,
                                  const char *reasons,
                                  gboolean    default_value)
{
  const char *extension_basename;
  g_auto(GStrv) reason_list = NULL;
  size_t i;

  if (reasons == NULL || *reasons == 0)
    return default_value;

  extension_basename = strrchr (extension_id, '.');
  if (extension_basename == NULL)
    return FALSE;
  extension_basename += 1;

  reason_list = g_strsplit (reasons, ";", -1);

  for (i = 0; reason_list[i]; ++i)
    {
      const char *reason = reason_list[i];

      if (strcmp (reason, "active-gl-driver") == 0)
        {
          /* handled below */
          const char **gl_drivers = flatpak_get_gl_drivers ();
          size_t j;

          for (j = 0; gl_drivers[j]; j++)
            {
              if (strcmp (gl_drivers[j], extension_basename) == 0)
                return TRUE;
            }
        }
      else if (strcmp (reason, "active-gtk-theme") == 0)
        {
          const char *gtk_theme = flatpak_get_gtk_theme ();
          if (strcmp (gtk_theme, extension_basename) == 0)
            return TRUE;
        }
      else if (strcmp (reason, "have-intel-gpu") == 0)
        {
          /* Used for Intel VAAPI driver extension */
          if (flatpak_get_have_intel_gpu ())
            return TRUE;
        }
      else if (g_str_has_prefix (reason, "have-kernel-module-"))
        {
          const char *module_name = reason + strlen ("have-kernel-module-");

          if (flatpak_get_have_kernel_module (module_name))
            return TRUE;
        }
      else if (g_str_has_prefix (reason, "on-xdg-desktop-"))
        {
          const char *desktop_name = reason + strlen ("on-xdg-desktop-");
          const char *current_desktop_var = g_getenv ("XDG_CURRENT_DESKTOP");
          g_auto(GStrv) current_desktop_names = NULL;
          size_t j;

          if (!current_desktop_var)
            continue;

          current_desktop_names = g_strsplit (current_desktop_var, ":", -1);

          for (j = 0; current_desktop_names[j]; ++j)
            {
              if (g_ascii_strcasecmp (desktop_name, current_desktop_names[j]) == 0)
                return TRUE;
            }
        }
    }

  return FALSE;
}

void
flatpak_parse_extension_with_tag (const char *extension,
                                  char      **name,
                                  char      **tag)
{
  const char *tag_chr = strchr (extension, '@');

  if (tag_chr)
    {
      if (name != NULL)
        *name = g_strndup (extension, tag_chr - extension);

      /* Everything after the @ */
      if (tag != NULL)
        *tag = g_strdup (tag_chr + 1);

      return;
    }

  if (name != NULL)
    *name = g_strdup (extension);

  if (tag != NULL)
    *tag = NULL;
}

/* This allocates and locks a subdir of the tmp dir, using an existing
 * one with the same prefix if it is not in use already. */
gboolean
flatpak_allocate_tmpdir (int           tmpdir_dfd,
                         const char   *tmpdir_relpath,
                         const char   *tmpdir_prefix,
                         char        **tmpdir_name_out,
                         int          *tmpdir_fd_out,
                         GLnxLockFile *file_lock_out,
                         gboolean     *reusing_dir_out,
                         GCancellable *cancellable,
                         GError      **error)
{
  gboolean reusing_dir = FALSE;
  g_autofree char *tmpdir_name = NULL;
  glnx_autofd int tmpdir_fd = -1;
  g_auto(GLnxDirFdIterator) dfd_iter = { 0, };

  /* Look for existing tmpdir (with same prefix) to reuse */
  if (!glnx_dirfd_iterator_init_at (tmpdir_dfd, tmpdir_relpath ? tmpdir_relpath : ".", FALSE, &dfd_iter, error))
    return FALSE;

  while (tmpdir_name == NULL)
    {
      struct dirent *dent;
      glnx_autofd int existing_tmpdir_fd = -1;
      g_autoptr(GError) local_error = NULL;
      g_autofree char *lock_name = NULL;

      if (!glnx_dirfd_iterator_next_dent (&dfd_iter, &dent, cancellable, error))
        return FALSE;

      if (dent == NULL)
        break;

      if (!g_str_has_prefix (dent->d_name, tmpdir_prefix))
        continue;

      /* Quickly skip non-dirs, if unknown we ignore ENOTDIR when opening instead */
      if (dent->d_type != DT_UNKNOWN &&
          dent->d_type != DT_DIR)
        continue;

      if (!glnx_opendirat (dfd_iter.fd, dent->d_name, FALSE,
                           &existing_tmpdir_fd, &local_error))
        {
          if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY))
            {
              continue;
            }
          else
            {
              g_propagate_error (error, g_steal_pointer (&local_error));
              return FALSE;
            }
        }

      lock_name = g_strconcat (dent->d_name, "-lock", NULL);

      /* We put the lock outside the dir, so we can hold the lock
       * until the directory is fully removed */
      if (!glnx_make_lock_file (dfd_iter.fd, lock_name, LOCK_EX | LOCK_NB,
                                file_lock_out, &local_error))
        {
          if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
            {
              continue;
            }
          else
            {
              g_propagate_error (error, g_steal_pointer (&local_error));
              return FALSE;
            }
        }

      /* Touch the reused directory so that we don't accidentally
       *   remove it due to being old when cleaning up the tmpdir
       */
      (void) futimens (existing_tmpdir_fd, NULL);

      /* We found an existing tmpdir which we managed to lock */
      tmpdir_name = g_strdup (dent->d_name);
      tmpdir_fd = g_steal_fd (&existing_tmpdir_fd);
      reusing_dir = TRUE;
    }

  while (tmpdir_name == NULL)
    {
      g_autofree char *tmpdir_name_template = g_strconcat (tmpdir_prefix, "XXXXXX", NULL);
      g_autoptr(GError) local_error = NULL;
      g_autofree char *lock_name = NULL;
      g_auto(GLnxTmpDir) new_tmpdir = { 0, };
      /* No existing tmpdir found, create a new */

      if (!glnx_mkdtempat (dfd_iter.fd, tmpdir_name_template, 0777,
                           &new_tmpdir, error))
        return FALSE;

      lock_name = g_strconcat (new_tmpdir.path, "-lock", NULL);

      /* Note, at this point we can race with another process that picks up this
       * new directory. If that happens we need to retry, making a new directory. */
      if (!glnx_make_lock_file (dfd_iter.fd, lock_name, LOCK_EX | LOCK_NB,
                                file_lock_out, &local_error))
        {
          if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
            {
              glnx_tmpdir_unset (&new_tmpdir); /* Don't delete */
              continue;
            }
          else
            {
              g_propagate_error (error, g_steal_pointer (&local_error));
              return FALSE;
            }
        }

      tmpdir_name = g_strdup (new_tmpdir.path);
      tmpdir_fd = dup (new_tmpdir.fd);
      glnx_tmpdir_unset (&new_tmpdir); /* Don't delete */
    }

  if (tmpdir_name_out)
    *tmpdir_name_out = g_steal_pointer (&tmpdir_name);

  if (tmpdir_fd_out)
    *tmpdir_fd_out = g_steal_fd (&tmpdir_fd);

  if (reusing_dir_out)
    *reusing_dir_out = reusing_dir;

  return TRUE;
}

static gint
string_length_compare_func (gconstpointer a,
                            gconstpointer b)
{
  return strlen (*(char * const *) a) - strlen (*(char * const *) b);
}

/* Sort a string array by decreasing length */
char **
flatpak_strv_sort_by_length (const char * const *strv)
{
  GPtrArray *array;
  int i;

  if (strv == NULL)
    return NULL;

  /* Combine both */
  array = g_ptr_array_new ();

  for (i = 0; strv[i] != NULL; i++)
    g_ptr_array_add (array, g_strdup (strv[i]));

  g_ptr_array_sort (array, string_length_compare_func);

  g_ptr_array_add (array, NULL);
  return (char **) g_ptr_array_free (array, FALSE);
}

char **
flatpak_strv_merge (char   **strv1,
                    char   **strv2)
{
  GPtrArray *array;
  int i;

  /* Maybe either (or both) is unspecified */
  if (strv1 == NULL)
    return g_strdupv (strv2);
  if (strv2 == NULL)
    return g_strdupv (strv1);

  /* Combine both */
  array = g_ptr_array_new ();

  for (i = 0; strv1[i] != NULL; i++)
    {
      if (!flatpak_g_ptr_array_contains_string (array, strv1[i]))
        g_ptr_array_add (array, g_strdup (strv1[i]));
    }

  for (i = 0; strv2[i] != NULL; i++)
    {
      if (!flatpak_g_ptr_array_contains_string (array, strv2[i]))
        g_ptr_array_add (array, g_strdup (strv2[i]));
    }

  g_ptr_array_add (array, NULL);
  return (char **) g_ptr_array_free (array, FALSE);
}

/* In this NULL means don't care about these paths, while
   an empty array means match anything */
char **
flatpak_subpaths_merge (char **subpaths1,
                        char **subpaths2)
{
  char **res;

  if (subpaths1 != NULL && subpaths1[0] == NULL)
    return g_strdupv (subpaths1);
  if (subpaths2 != NULL && subpaths2[0] == NULL)
    return g_strdupv (subpaths2);

  res = flatpak_strv_merge (subpaths1, subpaths2);
  if (res)
    qsort (res, g_strv_length (res), sizeof (const char *), flatpak_strcmp0_ptr);

  return res;
}

gboolean
flatpak_g_ptr_array_contains_string (GPtrArray *array, const char *str)
{
  int i;

  for (i = 0; i < array->len; i++)
    {
      if (strcmp (g_ptr_array_index (array, i), str) == 0)
        return TRUE;
    }
  return FALSE;
}

#if 0
gboolean
flatpak_check_required_version (const char *ref,
                                GKeyFile   *metakey,
                                GError    **error)
{
  g_auto(GStrv) required_versions = NULL;
  const char *group;
  int max_required_major = 0, max_required_minor = 0;
  const char *max_required_version = "0.0";
  int i;

  if (g_str_has_prefix (ref, "app/"))
    group = "Application";
  else
    group = "Runtime";

  /* We handle handle multiple version requirements here. Each requirement must
   * be in the form major.minor.micro, and if the flatpak version matches the
   * major.minor part, t must be equal or later in the micro. If the major.minor part
   * doesn't exactly match any of the specified requirements it must be larger
   * than the maximum specified requirement.
   *
   * For example, specifying
   *   required-flatpak=1.6.2;1.4.2;1.0.2;
   * would allow flatpak versions:
   *  1.7.0, 1.6.2, 1.6.3, 1.4.2, 1.4.3, 1.0.2, 1.0.3
   * but not:
   *  1.6.1, 1.4.1 or 1.2.100.
   *
   * The goal here is to be able to specify a version (like 1.6.2 above) where a feature
   * was introduced, but also allow backports of said feature to earlier version series.
   *
   * Earlier versions that only support specifying one version will only look at the first
   * element in the list, so put the largest version first.
   */
  required_versions = g_key_file_get_string_list (metakey, group, "required-flatpak", NULL, NULL);
  if (required_versions == 0 || required_versions[0] == NULL)
    return TRUE;

  for (i = 0; required_versions[i] != NULL; i++)
    {
      int required_major, required_minor, required_micro;
      const char *required_version = required_versions[i];

      if (sscanf (required_version, "%d.%d.%d", &required_major, &required_minor, &required_micro) != 3)
        return flatpak_fail_error (error, FLATPAK_ERROR_INVALID_DATA,
                                   _("Invalid require-flatpak argument %s"), required_version);
      else
        {
          /* If flatpak is in the same major.minor series as the requirement, do a micro check */
          if (required_major == PACKAGE_MAJOR_VERSION && required_minor == PACKAGE_MINOR_VERSION)
            {
              if (required_micro <= PACKAGE_MICRO_VERSION)
                return TRUE;
              else
                return flatpak_fail_error (error, FLATPAK_ERROR_NEED_NEW_FLATPAK,
                                           _("%s needs a later flatpak version (%s)"),
                                           ref, required_version);
            }

          /* Otherwise, keep track of the largest major.minor that is required */
          if ((required_major > max_required_major) ||
              (required_major == max_required_major &&
               required_minor > max_required_minor))
            {
              max_required_major = required_major;
              max_required_minor = required_minor;
              max_required_version = required_version;
            }
        }
    }

  if (max_required_major > PACKAGE_MAJOR_VERSION ||
      (max_required_major == PACKAGE_MAJOR_VERSION && max_required_minor > PACKAGE_MINOR_VERSION))
    return flatpak_fail_error (error, FLATPAK_ERROR_NEED_NEW_FLATPAK,
                               _("%s needs a later flatpak version (%s)"),
                               ref, max_required_version);

  return TRUE;
}
#endif

static int
dist (const char *s, int ls, const char *t, int lt, int i, int j, int *d)
{
  int x, y;

  if (d[i * (lt + 1) + j] >= 0)
    return d[i * (lt + 1) + j];

  if (i == ls)
    x = lt - j;
  else if (j == lt)
    x = ls - i;
  else if (s[i] == t[j])
    x = dist (s, ls, t, lt, i + 1, j + 1, d);
  else
    {
      x = dist (s, ls, t, lt, i + 1, j + 1, d);
      y = dist (s, ls, t, lt, i, j + 1, d);
      if (y < x)
        x = y;
      y = dist (s, ls, t, lt, i + 1, j, d);
      if (y < x)
        x = y;
      x++;
    }

  d[i * (lt + 1) + j] = x;

  return x;
}

int
flatpak_levenshtein_distance (const char *s,
                              gssize ls,
                              const char *t,
                              gssize lt)
{
  int i, j;
  int *d;

  if (ls < 0)
    ls = strlen (s);

  if (lt < 0)
    lt = strlen (t);

  d = alloca (sizeof (int) * (ls + 1) * (lt + 1));

  for (i = 0; i <= ls; i++)
    for (j = 0; j <= lt; j++)
      d[i * (lt + 1) + j] = -1;

  return dist (s, ls, t, lt, 0, 0, d);
}

/* Convert an app id to a dconf path in the obvious way.
 */
char *
flatpak_dconf_path_for_app_id (const char *app_id)
{
  GString *s;
  const char *p;

  s = g_string_new ("");

  g_string_append_c (s, '/');
  for (p = app_id; *p; p++)
    {
      if (*p == '.')
        g_string_append_c (s, '/');
      else
        g_string_append_c (s, *p);
    }
  g_string_append_c (s, '/');

  return g_string_free (s, FALSE);
}

/* Check if two dconf paths are 'similar enough', which
 * for now is defined as equal except case differences
 * and -/_
 */
gboolean
flatpak_dconf_path_is_similar (const char *path1,
                               const char *path2)
{
  int i1, i2;
  int num_components = -1;

  for (i1 = i2 = 0; path1[i1] != '\0'; i1++, i2++)
    {
      if (path2[i2] == '\0')
        break;

      if (isupper(path2[i2]) &&
          (path1[i1] == '-' || path1[i1] == '_'))
        {
          i1++;
          if (path1[i1] == '\0')
            break;
        }

      if (isupper(path1[i1]) &&
          (path2[i2] == '-' || path2[i2] == '_'))
        {
          i2++;
          if (path2[i2] == '\0')
            break;
        }

      if (tolower (path1[i1]) == tolower (path2[i2]))
        {
          if (path1[i1] == '/')
            num_components++;
          continue;
        }

      if ((path1[i1] == '-' || path1[i1] == '_') &&
          (path2[i2] == '-' || path2[i2] == '_'))
        continue;

      break;
    }

  /* Skip over any versioning if we have at least a TLD and
   * domain name, so 2 components */
  /* We need at least TLD, and domain name, so 2 components */
  if (num_components >= 2)
    {
      while (isdigit (path1[i1]))
        i1++;
      while (isdigit (path2[i2]))
        i2++;
    }

  if (path1[i1] != path2[i2])
    return FALSE;

  /* Both strings finished? */
  if (path1[i1] == '\0')
    return TRUE;

  /* Maybe a trailing slash in both strings */
  if (path1[i1] == '/')
    {
      i1++;
      i2++;
    }

  if (path1[i1] != path2[i2])
    return FALSE;

  return (path1[i1] == '\0');
}

GStrv
flatpak_parse_env_block (const char  *data,
                         gsize        length,
                         GError     **error)
{
  g_autoptr(GPtrArray) env_vars = g_ptr_array_new_with_free_func (g_free);
  const char *p = data;
  gsize remaining = length;

  /* env_block might not be \0-terminated */
  while (remaining > 0)
    {
      size_t len = strnlen (p, remaining);
      const char *equals;

      g_assert (len <= remaining);

      equals = memchr (p, '=', len);

      if (equals == NULL || equals == p)
        return glnx_null_throw (error,
                                "Environment variable must be in the form VARIABLE=VALUE, not %.*s", (int) len, p);

      g_ptr_array_add (env_vars,
                       g_strndup (p, len));

      p += len;
      remaining -= len;

      if (remaining > 0)
        {
          g_assert (*p == '\0');
          p += 1;
          remaining -= 1;
        }
    }

  g_ptr_array_add (env_vars, NULL);

  return (GStrv) g_ptr_array_free (g_steal_pointer (&env_vars), FALSE);
}

/**
 * flatpak_envp_cmp:
 * @p1: a `const char * const *`
 * @p2: a `const char * const *`
 *
 * Compare two environment variables, given as pointers to pointers
 * to the actual `KEY=value` string.
 *
 * In particular this is suitable for sorting a #GStrv using `qsort`.
 *
 * Returns: negative, 0 or positive if `*p1` compares before, equal to
 *  or after `*p2`
 */
int
flatpak_envp_cmp (const void *p1,
                  const void *p2)
{
  const char * const * s1 = p1;
  const char * const * s2 = p2;
  size_t l1 = strlen (*s1);
  size_t l2 = strlen (*s2);
  size_t min;
  const char *tmp;
  int ret;

  tmp = strchr (*s1, '=');

  if (tmp != NULL)
    l1 = tmp - *s1;

  tmp = strchr (*s2, '=');

  if (tmp != NULL)
    l2 = tmp - *s2;

  min = MIN (l1, l2);
  ret = strncmp (*s1, *s2, min);

  /* If they differ before the first '=' (if any) in either s1 or s2,
   * then they are certainly different */
  if (ret != 0)
    return ret;

  ret = strcmp (*s1, *s2);

  /* If they do not differ at all, then they are equal */
  if (ret == 0)
    return ret;

  /* FOO < FOO=..., and FOO < FOOBAR */
  if ((*s1)[min] == '\0')
    return -1;

  /* FOO=... > FOO, and FOOBAR > FOO */
  if ((*s2)[min] == '\0')
    return 1;

  /* FOO= < FOOBAR */
  if ((*s1)[min] == '=' && (*s2)[min] != '=')
    return -1;

  /* FOOBAR > FOO= */
  if ((*s2)[min] == '=' && (*s1)[min] != '=')
    return 1;

  /* Fall back to plain string comparison */
  return ret;
}

/*
 * Return %TRUE if @s consists of one or more digits.
 * This is the same as Python bytes.isdigit().
 */
gboolean
flatpak_str_is_integer (const char *s)
{
  if (s == NULL || *s == '\0')
    return FALSE;

  for (; *s != '\0'; s++)
    {
      if (!g_ascii_isdigit (*s))
        return FALSE;
    }

  return TRUE;
}

gboolean
flatpak_uri_equal (const char *uri1,
                   const char *uri2)
{
  g_autofree char *uri1_norm = NULL;
  g_autofree char *uri2_norm = NULL;
  gsize uri1_len = strlen (uri1);
  gsize uri2_len = strlen (uri2);

  /* URIs handled by libostree are equivalent with or without a trailing slash,
   * but this isn't otherwise guaranteed to be the case.
   */
  if (g_str_has_prefix (uri1, "oci+") || g_str_has_prefix (uri2, "oci+"))
    return g_strcmp0 (uri1, uri2) == 0;

  if (g_str_has_suffix (uri1, "/"))
    uri1_norm = g_strndup (uri1, uri1_len - 1);
  else
    uri1_norm = g_strdup (uri1);

  if (g_str_has_suffix (uri2, "/"))
    uri2_norm = g_strndup (uri2, uri2_len - 1);
  else
    uri2_norm = g_strdup (uri2);

  return g_strcmp0 (uri1_norm, uri2_norm) == 0;
}

static gboolean
is_char_safe (gunichar c)
{
  return g_unichar_isgraph (c) || c == ' ';
}

static gboolean
should_hex_escape (gunichar           c,
                   FlatpakEscapeFlags flags)
{
  if ((flags & FLATPAK_ESCAPE_ALLOW_NEWLINES) && c == '\n')
    return FALSE;

  return !is_char_safe (c);
}

static void
append_hex_escaped_character (GString *result,
                              gunichar c)
{
  if (c <= 0xFF)
    g_string_append_printf (result, "\\x%02X", c);
  else if (c <= 0xFFFF)
    g_string_append_printf (result, "\\u%04X", c);
  else
    g_string_append_printf (result, "\\U%08X", c);
}

static char *
escape_character (gunichar c)
{
  g_autoptr(GString) res = g_string_new ("");
  append_hex_escaped_character (res, c);
  return g_string_free (g_steal_pointer (&res), FALSE);
}

char *
flatpak_escape_string (const char        *s,
                       FlatpakEscapeFlags flags)
{
  g_autoptr(GString) res = g_string_new ("");
  gboolean did_escape = FALSE;

  while (*s)
    {
      gunichar c = g_utf8_get_char_validated (s, -1);
      if (c == (gunichar)-2 || c == (gunichar)-1)
        {
          /* Need to convert to unsigned first, to avoid negative chars becoming
             huge gunichars. */
          append_hex_escaped_character (res, (unsigned char)*s++);
          did_escape = TRUE;
          continue;
        }
      else if (should_hex_escape (c, flags))
        {
          append_hex_escaped_character (res, c);
          did_escape = TRUE;
        }
      else if (c == '\\' || (!(flags & FLATPAK_ESCAPE_DO_NOT_QUOTE) && c == '\''))
        {
          g_string_append_printf (res, "\\%c", (char) c);
          did_escape = TRUE;
        }
      else
        g_string_append_unichar (res, c);

      s = g_utf8_find_next_char (s, NULL);
    }

  if (did_escape && !(flags & FLATPAK_ESCAPE_DO_NOT_QUOTE))
    {
      g_string_prepend_c (res, '\'');
      g_string_append_c (res, '\'');
    }

  return g_string_free (g_steal_pointer (&res), FALSE);
}

gboolean
flatpak_validate_path_characters (const char *path,
                                  GError    **error)
{
  while (*path)
    {
      gunichar c = g_utf8_get_char_validated (path, -1);
      if (c == (gunichar)-1 || c == (gunichar)-2)
        {
          /* Need to convert to unsigned first, to avoid negative chars becoming
             huge gunichars. */
          g_autofree char *escaped_char = escape_character ((unsigned char)*path);
          g_autofree char *escaped = flatpak_escape_string (path, FLATPAK_ESCAPE_DEFAULT);
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                       "Non-UTF8 byte %s in path %s", escaped_char, escaped);
          return FALSE;
        }
      else if (!is_char_safe (c))
        {
          g_autofree char *escaped_char = escape_character (c);
          g_autofree char *escaped = flatpak_escape_string (path, FLATPAK_ESCAPE_DEFAULT);
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                       "Non-graphical character %s in path %s", escaped_char, escaped);
          return FALSE;
        }

      path = g_utf8_find_next_char (path, NULL);
    }

  return TRUE;
}

gboolean
running_under_sudo (void)
{
  const char *sudo_command_env = g_getenv ("SUDO_COMMAND");
  g_auto(GStrv) split_command = NULL;

  if (!sudo_command_env)
    return FALSE;

  /* SUDO_COMMAND could be a value like `/usr/bin/flatpak run foo` */
  split_command = g_strsplit (sudo_command_env, " ", 2);
  if (g_str_has_suffix (split_command[0], "flatpak"))
    return TRUE;

  return FALSE;
}
