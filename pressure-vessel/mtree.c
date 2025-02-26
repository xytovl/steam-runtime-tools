/*
 * Copyright © 2021 Collabora Ltd.
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

#include "mtree.h"

#include <ftw.h>

#include "steam-runtime-tools/profiling-internal.h"
#include "steam-runtime-tools/resolve-in-sysroot-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "flatpak-utils-base-private.h"

#include <gio/gunixinputstream.h>

#include "enumtypes.h"
#include "utils.h"

/* Enabling debug logging for this is rather too verbose, so only
 * enable it when actively debugging this module */
#if 0
#define trace(...) g_debug (__VA_ARGS__)
#else
#define trace(...) do { } while (0)
#endif

static gboolean
is_token (const char *token,
          const char *equals,
          const char *expected)
{
  if (equals == NULL)
    {
      return strcmp (token, expected) == 0;
    }
  else
    {
      g_assert (equals >= token);
      g_assert (*equals == '=');
      return strncmp (token, expected, equals - token) == 0;
    }
}

static gboolean
require_value (const char *token,
               const char *equals,
               GError **error)
{
  if (equals == NULL)
    return glnx_throw (error, "%s requires a value", token);

  return TRUE;
}

static gboolean
forbid_value (const char *token,
              const char *equals,
              GError **error)
{
  if (equals != NULL)
    return glnx_throw (error, "%s does not take a value", token);

  return TRUE;
}

static gboolean
pv_mtree_entry_parse_internal (const char *line,
                               PvMtreeEntry *entry,
                               const char *filename,
                               guint line_number,
                               GError **error)
{
  PvMtreeEntry blank = PV_MTREE_ENTRY_BLANK;
  g_auto(GStrv) tokens = NULL;
  gsize i;

  *entry = blank;

  if (line[0] == '\0' || line[0] == '#')
    return TRUE;

  if (line[0] == '/')
    return glnx_throw (error, "Special commands not supported");

  if (line[0] != '.' || (line[1] != ' ' && line[1] != '/' && line[1] != '\0'))
    return glnx_throw (error,
                       "Filenames not relative to top level not supported");

  if (g_str_has_suffix (line, "\\"))
    return glnx_throw (error, "Continuation lines not supported");

  for (i = 0; line[i] != '\0'; i++)
    {
      if (line[i] == '\\')
        {
          if (line[i + 1] >= '0' && line[i + 1] <= '9')
            continue;

          switch (line[i + 1])
            {
              /* g_strcompress() documents these to work */
              case 'b':
              case 'f':
              case 'n':
              case 'r':
              case 't':
              case 'v':
              case '"':
              case '\\':
                i += 1;
                continue;

              /* \M, \^, \a, \s, \E, \x, \$, escaped whitespace and escaped
               * newline are not supported here */
              default:
                return glnx_throw (error,
                                   "Unsupported backslash escape: \"\\%c\"",
                                   line[i + 1]);
            }
        }
    }

  tokens = g_strsplit_set (line, " \t", -1);

  if (tokens == NULL)
    return glnx_throw (error, "Line is empty");

  entry->name = g_strcompress (tokens[0]);

  for (i = 1; tokens[i] != NULL; i++)
    {
      static const char * const ignored[] =
      {
        "cksum",
        "device",
        "flags",
        "gid",
        "gname",
        "inode",
        "md5",
        "md5digest",
        "nlink",
        "resdevice",
        "ripemd160digest",
        "rmd160",
        "rmd160digest",
        "sha1",
        "sha1digest",
        "sha384",
        "sha384digest",
        "sha512",
        "sha512digest",
        "uid",
        "uname",
      };
      const char *equals;
      char *endptr;
      gsize j;

      equals = strchr (tokens[i], '=');

      for (j = 0; j < G_N_ELEMENTS (ignored); j++)
        {
          if (is_token (tokens[i], equals, ignored[j]))
            break;
        }

      if (j < G_N_ELEMENTS (ignored))
        continue;

#define REQUIRE_VALUE \
      G_STMT_START \
        { \
          if (!require_value (tokens[i], equals, error)) \
            return FALSE; \
        } \
      G_STMT_END

#define FORBID_VALUE(token) \
      G_STMT_START \
        { \
          if (!forbid_value ((token), equals, error)) \
            return FALSE; \
        } \
      G_STMT_END

      if (is_token (tokens[i], equals, "link"))
        {
          REQUIRE_VALUE;
          entry->link = g_strcompress (equals + 1);
          continue;
        }

      if (is_token (tokens[i], equals, "contents")
          || is_token (tokens[i], equals, "content"))
        {
          REQUIRE_VALUE;
          entry->contents = g_strcompress (equals + 1);
          continue;
        }

      if (is_token (tokens[i], equals, "sha256")
          || is_token (tokens[i], equals, "sha256digest"))
        {
          REQUIRE_VALUE;

          if (entry->sha256 == NULL)
            entry->sha256 = g_strdup (equals + 1);
          else if (strcmp (entry->sha256, equals + 1) != 0)
            return glnx_throw (error,
                               "sha256 and sha256digest not consistent");

          continue;
        }

      if (is_token (tokens[i], equals, "mode"))
        {
          gint64 value;

          REQUIRE_VALUE;
          value = g_ascii_strtoll (equals + 1, &endptr, 8);

          if (equals[1] == '\0' || *endptr != '\0')
            return glnx_throw (error, "Invalid mode %s", equals + 1);

          entry->mode = value & 07777;
          continue;
        }

      if (is_token (tokens[i], equals, "size"))
        {
          gint64 value;

          REQUIRE_VALUE;
          value = g_ascii_strtoll (equals + 1, &endptr, 10);

          if (equals[1] == '\0' || *endptr != '\0')
            return glnx_throw (error, "Invalid size %s", equals + 1);

          entry->size = value;
          continue;
        }

      if (is_token (tokens[i], equals, "time"))
        {
          guint64 value;
          guint64 ns = 0;

          REQUIRE_VALUE;
          value = g_ascii_strtoull (equals + 1, &endptr, 10);

          if (equals[1] == '\0' || (*endptr != '\0' && *endptr != '.'))
            return glnx_throw (error, "Invalid time %s", equals + 1);

          /* This is silly, but time=1.234 has historically meant
           * 1 second + 234 nanoseconds, or what normal people would
           * write as 1.000000234, so parsing it as a float is incorrect
           * (for example mtree-netbsd in Debian still prints it
           * like that).
           *
           * time=1.0 is unambiguous, and so is time=1.123456789
           * with exactly 9 digits. */
          if (*endptr == '.' && strcmp (endptr, ".0") != 0)
            {
              const char *dot = endptr;

              ns = g_ascii_strtoull (dot + 1, &endptr, 10);

              if (dot[1] == '\0' || *endptr != '\0' || ns > 999999999)
                return glnx_throw (error, "Invalid nanoseconds count %s",
                                   dot + 1);

              /* If necessary this could become just a warning, but for
               * now require it to be unambiguous - libarchive and
               * FreeBSD mtree show this unambiguous format. */
              if (endptr != dot + 10)
                return glnx_throw (error,
                                   "Ambiguous nanoseconds count %s, "
                                   "should have exactly 9 digits",
                                   dot + 1);
            }

          /* We store it as a GTimeSpan which is "only" microsecond
           * precision. */
          entry->mtime_usec = (value * G_TIME_SPAN_SECOND) + (ns / 1000);
          continue;
        }

      if (is_token (tokens[i], equals, "type"))
        {
          int value;

          REQUIRE_VALUE;

          if (srt_enum_from_nick (PV_TYPE_MTREE_ENTRY_KIND, equals + 1,
                                  &value, NULL))
            entry->kind = value;
          else
            entry->kind = PV_MTREE_ENTRY_KIND_UNKNOWN;

          continue;
        }

      if (is_token (tokens[i], equals, "ignore"))
        {
          FORBID_VALUE ("ignore");
          entry->entry_flags |= PV_MTREE_ENTRY_FLAGS_IGNORE_BELOW;
          continue;
        }

      if (is_token (tokens[i], equals, "nochange"))
        {
          FORBID_VALUE ("nochange");
          entry->entry_flags |= PV_MTREE_ENTRY_FLAGS_NO_CHANGE;
          continue;
        }

      if (is_token (tokens[i], equals, "optional"))
        {
          FORBID_VALUE ("optional");
          entry->entry_flags |= PV_MTREE_ENTRY_FLAGS_OPTIONAL;
          continue;
        }

      g_warning ("%s:%u: Unknown mtree keyword %s",
                 filename, line_number, tokens[i]);
    }

  if (entry->kind == PV_MTREE_ENTRY_KIND_UNKNOWN)
    return glnx_throw (error, "Unknown mtree entry type");

  if (entry->link != NULL && entry->kind != PV_MTREE_ENTRY_KIND_LINK)
    return glnx_throw (error, "Non-symlink cannot have a symlink target");

  if (entry->link == NULL && entry->kind == PV_MTREE_ENTRY_KIND_LINK)
    return glnx_throw (error, "Symlink must have a symlink target");

  return TRUE;
}

gboolean
pv_mtree_entry_parse (const char *line,
                      PvMtreeEntry *entry,
                      const char *filename,
                      guint line_number,
                      GError **error)
{
  if (!pv_mtree_entry_parse_internal (line, entry,
                                      filename, line_number, error))
    {
      g_prefix_error (error, "%s: %u: ", filename, line_number);
      return FALSE;
    }

  return TRUE;
}

static gboolean
maybe_chmod (const PvMtreeEntry *entry,
             int parent_fd,
             const char *base,
             int fd,
             const char *sysroot,
             PvMtreeApplyFlags flags,
             GLogLevelFlags *chmod_plusx_warning_level,
             GLogLevelFlags *chmod_minusx_warning_level,
             GError **error)
{
  g_autofree gchar *permissions = NULL;
  int adjusted_mode;
  int saved_errno;
  struct stat stat_buf = {};

  if (entry->entry_flags & PV_MTREE_ENTRY_FLAGS_NO_CHANGE)
    return TRUE;

  if (entry->kind == PV_MTREE_ENTRY_KIND_DIR
      || (entry->mode >= 0 && entry->mode & 0111))
    adjusted_mode = 0755;
  else
    adjusted_mode = 0644;

  if (TEMP_FAILURE_RETRY (fchmod (fd, adjusted_mode)) == 0)
    return TRUE;

  saved_errno = errno;

  if (fstat (fd, &stat_buf) == 0)
    {
      permissions = pv_stat_describe_permissions (&stat_buf);
    }
  else
    {
      permissions = g_strdup_printf ("(unknown: %s)", g_strerror (errno));
    }

  if (saved_errno == EPERM
      && (flags & PV_MTREE_APPLY_FLAGS_CHMOD_MAY_FAIL) != 0)
    {
      /* Use faccessat() instead of reading stat_buf.st_mode,
       * in case we are not the file owner or the filesystem
       * bypasses normal POSIX permissions */
      if ((adjusted_mode & 0111) != 0)
        {
          /* We want it to be executable */
          if (faccessat (parent_fd, base, R_OK|X_OK, 0) == 0)
            {
              g_log (G_LOG_DOMAIN, *chmod_plusx_warning_level,
                     "Cannot chmod directory/executable \"%s\" in \"%s\" "
                     "from %s to 0%o (%s): assuming R_OK|X_OK is close enough",
                     entry->name, sysroot,
                     permissions, adjusted_mode, g_strerror (saved_errno));
              *chmod_plusx_warning_level = G_LOG_LEVEL_INFO;
              return TRUE;
            }
        }
      else
        {
          /* We don't want it to be executable, but hopefully it's not
           * fatally bad if it is accidentally executable (as files on
           * NTFS or FAT often will be) */
          if (faccessat (parent_fd, base, R_OK, 0) == 0)
            {
              g_log (G_LOG_DOMAIN, *chmod_minusx_warning_level,
                     "Cannot chmod non-executable file \"%s\" in \"%s\" "
                     "from %s to 0%o (%s): assuming R_OK is close enough",
                     entry->name, sysroot,
                     permissions, adjusted_mode, g_strerror (saved_errno));
              *chmod_minusx_warning_level = G_LOG_LEVEL_INFO;
              return TRUE;
            }
        }
    }

  errno = saved_errno;
  return glnx_throw_errno_prefix (error,
                                  "Unable to change mode of \"%s\" in \"%s\" "
                                  "from %s to 0%o: fchmod",
                                  entry->name, sysroot,
                                  permissions, adjusted_mode);
}

typedef gboolean (*PvMtreeForeachFunc) (PvMtreeEntry *entry,
                                        PvMtreeApplyFlags flags,
                                        const char *mtree,
                                        guint line_number,
                                        void *user_data,
                                        GError **error);

typedef void (*PvMtreeForeachErrorFunc) (PvMtreeEntry *entry,
                                         PvMtreeApplyFlags flags,
                                         const char *mtree,
                                         guint line_number,
                                         const GError *error,
                                         void *user_data);

static gboolean
pv_mtree_foreach (const char *mtree,
                  PvMtreeApplyFlags flags,
                  PvMtreeForeachFunc callback,
                  void *user_data,
                  PvMtreeForeachErrorFunc on_error_cb,
                  void *on_error_data,
                  GError **error)
{
  glnx_autofd int mtree_fd = -1;
  g_autoptr(GInputStream) istream = NULL;
  g_autoptr(GDataInputStream) reader = NULL;
  guint line_number = 0;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (mtree != NULL, FALSE);
  g_return_val_if_fail (callback != NULL, FALSE);

  if (!glnx_openat_rdonly (AT_FDCWD, mtree, TRUE, &mtree_fd, error))
    return FALSE;

  istream = g_unix_input_stream_new (g_steal_fd (&mtree_fd), TRUE);

  if (flags & PV_MTREE_APPLY_FLAGS_GZIP)
    {
      g_autoptr(GInputStream) filter = NULL;
      g_autoptr(GZlibDecompressor) decompressor = NULL;

      decompressor = g_zlib_decompressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP);
      filter = g_converter_input_stream_new (istream, G_CONVERTER (decompressor));
      g_clear_object (&istream);
      istream = g_object_ref (filter);
    }

  reader = g_data_input_stream_new (istream);
  g_data_input_stream_set_newline_type (reader, G_DATA_STREAM_NEWLINE_TYPE_LF);

  while (TRUE)
    {
      g_autofree gchar *line = NULL;
      g_autoptr(GError) local_error = NULL;
      g_auto(PvMtreeEntry) entry = PV_MTREE_ENTRY_BLANK;

      line = g_data_input_stream_read_line (reader, NULL, NULL, &local_error);

      if (line == NULL)
        {
          if (local_error != NULL)
            {
              g_propagate_prefixed_error (error, g_steal_pointer (&local_error),
                                          "While reading a line from %s: ",
                                          mtree);
              return FALSE;
            }
          else
            {
              /* End of file, not an error */
              break;
            }
        }

      g_strstrip (line);
      line_number++;

      trace ("line %u: %s", line_number, line);

      if (!pv_mtree_entry_parse (line, &entry, mtree, line_number, error))
        return FALSE;

      if (entry.name == NULL || strcmp (entry.name, ".") == 0)
        continue;

      trace ("mtree entry: %s", entry.name);

      if (!callback (&entry, flags, mtree, line_number, user_data, &local_error))
        {
          if (on_error_cb != NULL)
            {
              on_error_cb (&entry, flags, mtree, line_number, local_error, on_error_data);
              g_clear_error (&local_error);
            }
          else
            {
              g_propagate_error (error, g_steal_pointer (&local_error));
              return FALSE;
            }
        }
    }

  return TRUE;
}

typedef struct
{
  const char *sysroot;
  int sysroot_fd;
  const char *source_files;
  int source_files_fd;
  GLogLevelFlags chmod_plusx_warning_level;
  GLogLevelFlags chmod_minusx_warning_level;
  GLogLevelFlags set_mtime_warning_level;
} ForeachApplyState;

static gboolean
pv_mtree_foreach_apply_cb (PvMtreeEntry *entry,
                           PvMtreeApplyFlags flags,
                           const char *mtree,
                           guint line_number,
                           void *user_data,
                           GError **error)
{
  ForeachApplyState *state = user_data;
  g_autofree gchar *parent = NULL;
  const char *base;
  glnx_autofd int parent_fd = -1;
  glnx_autofd int fd = -1;

  parent = g_path_get_dirname (entry->name);
  base = glnx_basename (entry->name);

  trace ("Creating %s in %s", parent, state->sysroot);
  parent_fd = _srt_resolve_in_sysroot (state->sysroot_fd, parent,
                                       SRT_RESOLVE_FLAGS_MKDIR_P,
                                       NULL, error);

  if (parent_fd < 0)
    return glnx_prefix_error (error,
                              "Unable to create parent directory for \"%s\" in \"%s\"",
                              entry->name, state->sysroot);

  switch (entry->kind)
    {
      case PV_MTREE_ENTRY_KIND_FILE:
        if (entry->size == 0)
          {
            /* For empty files, we can create it from nothing. */
            fd = TEMP_FAILURE_RETRY (openat (parent_fd, base,
                                             (O_RDWR | O_CLOEXEC | O_NOCTTY
                                              | O_NOFOLLOW | O_CREAT
                                              | O_TRUNC),
                                             0644));

            if (fd < 0)
              return glnx_throw_errno_prefix (error,
                                              "Unable to open \"%s\" in \"%s\"",
                                              entry->name, state->sysroot);
          }
        else if (state->source_files_fd >= 0)
          {
            const char *source = entry->contents;

            if (source == NULL)
              source = entry->name;

            /* If it already exists, assume it's correct */
            if (glnx_openat_rdonly (parent_fd, base, FALSE, &fd, NULL))
              {
                trace ("\"%s\" already exists in \"%s\"",
                       entry->name, state->sysroot);
              }
            /* If we can create a hard link, that's also fine */
            else if (TEMP_FAILURE_RETRY (linkat (state->source_files_fd, source,
                                                 parent_fd, base, 0)) == 0)
              {
                trace ("Created hard link \"%s\" in \"%s\"",
                       entry->name, state->sysroot);
              }
            /* Or if we can copy it, that's fine too */
            else
              {
                int link_errno = errno;

                g_debug ("Could not create hard link \"%s\" from \"%s/%s\" into \"%s\": %s",
                         entry->name, state->source_files, source, state->sysroot,
                         g_strerror (link_errno));

                if (!glnx_file_copy_at (state->source_files_fd, source, NULL,
                                        parent_fd, base,
                                        (GLNX_FILE_COPY_OVERWRITE
                                         | GLNX_FILE_COPY_NOCHOWN
                                         | GLNX_FILE_COPY_NOXATTRS),
                                        NULL, error))
                  return glnx_prefix_error (error,
                                            "Could not create copy \"%s\" from \"%s/%s\" into \"%s\"",
                                            entry->name, state->source_files,
                                            source, state->sysroot);

                if ((flags & PV_MTREE_APPLY_FLAGS_EXPECT_HARD_LINKS) != 0)
                  {
                    g_warning ("Unable to create hard link \"%s/%s\" to "
                               "\"%s/%s\": %s",
                               state->sysroot, entry->name, state->source_files, source,
                               g_strerror (link_errno));
                    g_warning ("Falling back to copying, but this will "
                               "take more time and disk space.");
                    g_warning ("For best results, \"%s\" and \"%s\" "
                               "should both be on the same "
                               "fully-featured Linux filesystem.",
                               state->source_files, state->sysroot);
                    /* Only warn once per tree copied */
                    flags &= ~PV_MTREE_APPLY_FLAGS_EXPECT_HARD_LINKS;
                  }
              }
          }

        /* For other regular files we just assert that it already exists
         * (and is not a symlink). */
        if (fd < 0
            && !(entry->entry_flags & PV_MTREE_ENTRY_FLAGS_OPTIONAL)
            && !glnx_openat_rdonly (parent_fd, base, FALSE, &fd, error))
          return glnx_prefix_error (error,
                                    "Unable to open \"%s\" in \"%s\"",
                                    entry->name, state->sysroot);

        break;

      case PV_MTREE_ENTRY_KIND_DIR:
        /* Create directories on-demand */
        if (!glnx_ensure_dir (parent_fd, base, 0755, error))
          return glnx_prefix_error (error,
                                    "Unable to create directory \"%s\" in \"%s\"",
                                    entry->name, state->sysroot);

        /* Assert that it is in fact a directory */
        if (!glnx_opendirat (parent_fd, base, FALSE, &fd, error))
          return glnx_prefix_error (error,
                                    "Unable to open directory \"%s\" in \"%s\"",
                                    entry->name, state->sysroot);

        break;

      case PV_MTREE_ENTRY_KIND_LINK:
          {
            g_autofree char *target = NULL;
            /* Create symlinks on-demand. To be idempotent, don't delete
             * an existing symlink. */
            target = glnx_readlinkat_malloc (parent_fd, base,
                                             NULL, NULL);

            if (target == NULL && symlinkat (entry->link, parent_fd, base) != 0)
              return glnx_throw_errno_prefix (error,
                                              "Unable to create symlink \"%s\" in \"%s\"",
                                              entry->name, state->sysroot);
          }
        break;

      case PV_MTREE_ENTRY_KIND_BLOCK:
      case PV_MTREE_ENTRY_KIND_CHAR:
      case PV_MTREE_ENTRY_KIND_FIFO:
      case PV_MTREE_ENTRY_KIND_SOCKET:
      case PV_MTREE_ENTRY_KIND_UNKNOWN:
      default:
        return glnx_throw (error,
                           "%s:%u: Special file not supported",
                           mtree, line_number);
    }

  if (fd >= 0 &&
      !maybe_chmod (entry, parent_fd, base, fd, state->sysroot, flags,
                    &state->chmod_plusx_warning_level,
                    &state->chmod_minusx_warning_level,
                    error))
    return FALSE;

  if (entry->mtime_usec >= 0
      && fd >= 0
      && !(entry->entry_flags & PV_MTREE_ENTRY_FLAGS_NO_CHANGE)
      && entry->kind == PV_MTREE_ENTRY_KIND_FILE)
    {
      struct timespec times[2] =
      {
        { .tv_sec = 0, .tv_nsec = UTIME_OMIT },   /* atime */
        {
          .tv_sec = entry->mtime_usec / G_TIME_SPAN_SECOND,
          .tv_nsec = (entry->mtime_usec % G_TIME_SPAN_SECOND) * 1000
        }   /* mtime */
      };

      if (futimens (fd, times) != 0)
        {
          g_log (G_LOG_DOMAIN, state->set_mtime_warning_level,
                 "Unable to set mtime of \"%s\" in \"%s\": %s",
                 entry->name, state->sysroot, g_strerror (errno));
          state->set_mtime_warning_level = G_LOG_LEVEL_INFO;
        }
    }

  return TRUE;
}

/*
 * pv_mtree_apply:
 * @mtree: (type filename): Path to a mtree(5) manifest
 * @sysroot: (type filename): A directory
 * @sysroot_fd: A fd opened on @sysroot
 * @source_files: (optional): A directory from which files will be
 *  hard-linked or copied when populating @sysroot. The `content`
 *  or filename in @mtree is taken to be relative to @source_files.
 * @flags: Flags affecting how this is done
 *
 * Make the container root filesystem @sysroot conform to @mtree.
 *
 * @mtree must contain a subset of BSD mtree(5) syntax:
 *
 * - one entry per line
 * - no device nodes, fifos, sockets or other special devices
 * - strings are escaped using octal (for example \040 for space)
 * - filenames other than "." start with "./"
 *
 * For regular files, we assert that the file exists, set its mtime,
 * and set its permissions to either 0644 or 0755.
 *
 * For directories, we create the directory with 0755 permissions.
 *
 * For symbolic links, we create the symbolic link if it does not
 * already exist.
 *
 * A suitable mtree file can be created from a tarball or the filesystem
 * with `bsdtar(1)` from the `libarchive-tools` Debian package:
 *
 * |[
 * bsdtar -cf - \
 *     --format=mtree \
 *     --options "!all,type,link,mode,size,time" \
 *     @- < foo.tar.gz
 * bsdtar -cf - \
 *     --format=mtree \
 *     --options "!all,type,link,mode,size,time" \
 *     -C files/ .
 * ]|
 *
 * A suitable mtree file can also be created by `mtree(8)` from the
 * `netbsd-mtree` Debian package if the filenames happen to be ASCII
 * (although this implementation does not support all escaped non-ASCII
 * filenames produced by `netbsd-mtree`):
 *
 * |[
 * mtree -p files -c | mtree -C
 * ]|
 *
 * Because hard links are used whenever possible, the permissions or
 * modification time of a source file in @source_files might be modified
 * to conform to the @mtree.
 *
 * Returns: %TRUE on success
 */
gboolean
pv_mtree_apply (const char *mtree,
                const char *sysroot,
                int sysroot_fd,
                const char *source_files,
                PvMtreeApplyFlags flags,
                GError **error)
{
  g_autoptr(SrtProfilingTimer) timer = NULL;
  glnx_autofd int source_files_fd = -1;
  /* We only emit a warning for the first file we were unable to chmod +x,
   * and the first file we were unable to chmod -x, per mtree applied;
   * the second and subsequent file in each class are demoted to INFO. */
  ForeachApplyState state =
  {
    .sysroot = sysroot,
    .sysroot_fd = sysroot_fd,
    .source_files = source_files,
    .source_files_fd = -1,
    .chmod_plusx_warning_level = G_LOG_LEVEL_WARNING,
    .chmod_minusx_warning_level = G_LOG_LEVEL_WARNING,
    .set_mtime_warning_level = G_LOG_LEVEL_WARNING,
  };

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (mtree != NULL, FALSE);
  g_return_val_if_fail (sysroot != NULL, FALSE);
  g_return_val_if_fail (sysroot_fd >= 0, FALSE);

  timer = _srt_profiling_start ("Apply %s to %s", mtree, sysroot);

  if (source_files != NULL)
    {
      if (!glnx_opendirat (AT_FDCWD, source_files, FALSE, &source_files_fd,
                           error))
        return FALSE;

      state.source_files_fd = source_files_fd;
    }

  g_info ("Applying \"%s\" to \"%s\"...", mtree, sysroot);

  return pv_mtree_foreach (mtree, flags,
                           pv_mtree_foreach_apply_cb, &state,
                           NULL, NULL,
                           error);
}

typedef struct
{
  GHashTable *names;
  GPtrArray *runtimes;
  const char *sysroot;
  int sysroot_fd;
  gboolean failed;
} ForeachVerifyState;

static gboolean
pv_mtree_foreach_verify_cb (PvMtreeEntry *entry,
                            PvMtreeApplyFlags flags,
                            const char *mtree,
                            guint line_number,
                            void *user_data,
                            GError **error)
{
  g_autofree gchar *parent = NULL;
  g_autoptr(GError) local_error = NULL;
  glnx_autofd int parent_fd = -1;
  glnx_autofd int fd = -1;
  ForeachVerifyState *state = user_data;
  const char *base;
  const char *name;
  struct stat stat_buf;

  name = entry->name;

  if ((flags & PV_MTREE_APPLY_FLAGS_MINIMIZED_RUNTIME)
      && entry->contents != NULL)
    name = entry->contents;

  if (g_str_has_prefix (name, "./"))
    name += 2;

  g_hash_table_replace (state->names, g_strdup (name),
                        GINT_TO_POINTER (entry->entry_flags));

  if (flags & PV_MTREE_APPLY_FLAGS_MINIMIZED_RUNTIME)
    {
      if (entry->contents != NULL)
        {
          g_autofree gchar *ancestor = g_strdup (name);
          char *slash;

          while (TRUE)
            {
              slash = strrchr (ancestor, '/');

              if (slash == NULL)
                break;

              *slash = '\0';

              if (!g_hash_table_lookup_extended (state->names, ancestor,
                                                 NULL, NULL))
                g_hash_table_insert (state->names, g_strdup (ancestor),
                                     GINT_TO_POINTER (PV_MTREE_ENTRY_FLAGS_OPTIONAL));
            }
        }

      switch (entry->kind)
        {
          case PV_MTREE_ENTRY_KIND_FILE:
            /* Empty files are defined entirely by their metadata */
            if (entry->size == 0)
              return TRUE;

            break;

          case PV_MTREE_ENTRY_KIND_DIR:
          case PV_MTREE_ENTRY_KIND_LINK:
            /* These are defined entirely by their metadata */
            return TRUE;

          case PV_MTREE_ENTRY_KIND_BLOCK:
          case PV_MTREE_ENTRY_KIND_CHAR:
          case PV_MTREE_ENTRY_KIND_FIFO:
          case PV_MTREE_ENTRY_KIND_SOCKET:
          case PV_MTREE_ENTRY_KIND_UNKNOWN:
          default:
            /* Do nothing: handled in more detail later */
            break;
        }
    }

  parent = g_path_get_dirname (name);
  base = glnx_basename (name);

  trace ("Verifying %s in %s", parent, state->sysroot);
  parent_fd = _srt_resolve_in_sysroot (state->sysroot_fd, parent,
                                       SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY,
                                       NULL, &local_error);

  if (parent_fd < 0)
    return glnx_prefix_error (error,
                              "Unable to open parent directory for \"%s\" in \"%s\"",
                              name, state->sysroot);

  switch (entry->kind)
    {
      case PV_MTREE_ENTRY_KIND_FILE:
        if (!glnx_openat_rdonly (parent_fd, base, FALSE, &fd, &local_error))
          {
            if ((entry->entry_flags & PV_MTREE_ENTRY_FLAGS_OPTIONAL)
                && g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
              return TRUE;

            g_propagate_prefixed_error (error, g_steal_pointer (&local_error),
                                        "Unable to open regular file \"%s\" in \"%s\": ",
                                        name, state->sysroot);
            return FALSE;
          }
        break;

      case PV_MTREE_ENTRY_KIND_DIR:
        if (!glnx_opendirat (parent_fd, base, FALSE, &fd, &local_error))
          {
            if ((entry->entry_flags & PV_MTREE_ENTRY_FLAGS_OPTIONAL)
                && g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
              return TRUE;

            g_propagate_prefixed_error (error, g_steal_pointer (&local_error),
                                        "Unable to open directory \"%s\" in \"%s\": ",
                                        name, state->sysroot);
            return FALSE;
          }
        break;

      case PV_MTREE_ENTRY_KIND_LINK:
          {
            g_autofree char *target = NULL;

            target = glnx_readlinkat_malloc (parent_fd, base, NULL, &local_error);

            if (target == NULL)
              {
                if ((entry->entry_flags & PV_MTREE_ENTRY_FLAGS_OPTIONAL)
                    && g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
                  return TRUE;

                g_propagate_prefixed_error (error, g_steal_pointer (&local_error),
                                            "\"%s\" in \"%s\" is not a symlink to \"%s\": ",
                                            name, state->sysroot, entry->link);
                return FALSE;
              }

            if (!g_str_equal (target, entry->link))
              {
                return glnx_throw (error,
                                   "\"%s\" in \"%s\" points to \"%s\", expected \"%s\"",
                                   name, state->sysroot, target, entry->link);
              }

            return TRUE;
          }
        break;

      case PV_MTREE_ENTRY_KIND_BLOCK:
      case PV_MTREE_ENTRY_KIND_CHAR:
      case PV_MTREE_ENTRY_KIND_FIFO:
      case PV_MTREE_ENTRY_KIND_SOCKET:
      case PV_MTREE_ENTRY_KIND_UNKNOWN:
      default:
        return glnx_throw (error,
                           "%s:%u: Special file not supported",
                           mtree, line_number);
    }

  if (fd < 0)
    return TRUE;

  if (fstat (fd, &stat_buf) < 0)
    return glnx_throw_errno_prefix (error,
                                    "Unable to get file information for \"%s\" in \"%s\"",
                                    name, state->sysroot);

  switch (entry->kind)
    {
      case PV_MTREE_ENTRY_KIND_FILE:
        if (!S_ISREG (stat_buf.st_mode))
          return glnx_throw (error,
                             "\"%s\" in \"%s\" should be a regular file, not type 0o%o",
                             name, state->sysroot, stat_buf.st_mode & S_IFMT);

        if (entry->size >= 0 && entry->size != stat_buf.st_size)
          return glnx_throw (error,
                             "\"%s\" in \"%s\" should have size %" G_GINT64_FORMAT
                             ", not %" G_GINT64_FORMAT,
                             name, state->sysroot, entry->size,
                             (gint64) stat_buf.st_size);

        if (entry->sha256 != NULL)
          {
            g_autoptr(GChecksum) hasher = g_checksum_new (G_CHECKSUM_SHA256);
            char buf[4096];
            ssize_t n = 0;

            do
              {
                n = TEMP_FAILURE_RETRY (read (fd, buf, sizeof (buf)));

                if (n < 0)
                  return glnx_throw_errno_prefix (error,
                                                  "Unable to read \"%s\" in \"%s\"",
                                                  name, state->sysroot);

                if (n > 0)
                  g_checksum_update (hasher, (void *) buf, n);
              }
            while (n > 0);

            if (!g_str_equal (g_checksum_get_string (hasher), entry->sha256))
              return glnx_throw (error,
                                 "\"%s\" in \"%s\" did not have expected contents",
                                 name, state->sysroot);
          }

        break;

      case PV_MTREE_ENTRY_KIND_DIR:
        if (!S_ISDIR (stat_buf.st_mode))
          return glnx_throw (error,
                             "\"%s\" in \"%s\" should be a directory, not type 0o%o",
                             name, state->sysroot, stat_buf.st_mode & S_IFMT);
        break;

      case PV_MTREE_ENTRY_KIND_LINK:
      case PV_MTREE_ENTRY_KIND_BLOCK:
      case PV_MTREE_ENTRY_KIND_CHAR:
      case PV_MTREE_ENTRY_KIND_FIFO:
      case PV_MTREE_ENTRY_KIND_SOCKET:
      case PV_MTREE_ENTRY_KIND_UNKNOWN:
      default:
        g_return_val_if_reached (FALSE);
    }

  /* We do a simplified permissions check, because Steampipe doesn't preserve
   * permissions anyway */
  if ((stat_buf.st_mode & 0111) == 0
      && (entry->kind == PV_MTREE_ENTRY_KIND_DIR
          || (entry->mode >= 0 && (entry->mode & 0111) != 0)))
    return glnx_throw (error,
                       "\"%s\" in \"%s\" should be executable, not mode 0%o",
                       name, state->sysroot, stat_buf.st_mode & 07777);

  if (!(flags & PV_MTREE_APPLY_FLAGS_MINIMIZED_RUNTIME)
      && g_str_equal (base, "usr-mtree.txt.gz"))
    g_ptr_array_add (state->runtimes, g_strdup (parent));

  return TRUE;
}

static void
pv_mtree_foreach_verify_error_cb (PvMtreeEntry *entry,
                                  PvMtreeApplyFlags flags,
                                  const char *mtree,
                                  guint line_number,
                                  const GError *error,
                                  void *user_data)
{
  ForeachVerifyState *state = user_data;

  g_warning ("%s", error->message);
  state->failed = TRUE;
}

/* nftw() doesn't have a user_data argument so we need to use a global variable */
static ForeachVerifyState verify_state =
{
  .sysroot = NULL,
  .sysroot_fd = -1,
  .names = NULL,
  .failed = FALSE
};

static int
pv_mtree_verify_nftw_cb (const char *fpath,
                         const struct stat *sb,
                         int typeflag,
                         struct FTW *ftwbuf)
{
  ForeachVerifyState *state = &verify_state;
  size_t len;
  const char *suffix;
  gpointer value;

  if (!g_str_has_prefix (fpath, state->sysroot))
    {
      g_critical ("\"%s\" should have started with \"%s\"",
                  fpath, state->sysroot);
      state->failed = TRUE;
      return FTW_CONTINUE;
    }

  len = strlen (state->sysroot);

  if (fpath[len] == '\0')
    return FTW_CONTINUE;

  g_return_val_if_fail (fpath[len] == '/', 1);
  suffix = &fpath[len + 1];

  while (suffix[0] == '/')
    suffix++;

  /* If sysroot was /path/to/source and fpath was /path/to/source/foo/bar,
   * then suffix is now foo/bar. */

  if (g_hash_table_lookup_extended (state->names, suffix, NULL, &value))
    {
      PvMtreeEntryFlags entry_flags = GPOINTER_TO_INT (value);

      trace ("Found \"%s\" in real directory hierarchy", suffix);

      if (typeflag == FTW_D
          && (entry_flags & PV_MTREE_ENTRY_FLAGS_IGNORE_BELOW))
        {
          trace ("Ignoring contents of \"%s\" due to ignore flag", suffix);
          return FTW_SKIP_SUBTREE;
        }
    }
  else
    {
      const char *label;

      switch (typeflag)
        {
          case FTW_D:
          case FTW_DP:
          case FTW_DNR:
            label = "directory";
            break;

          case FTW_F:
            label = "regular file";
            break;

          case FTW_SL:
          case FTW_SLN:
            label = "symbolic link";
            break;

          case FTW_NS:
          default:
            label = "filesystem object";
            break;
        }

      g_warning ("%s \"%s\" in \"%s\" not found in manifest",
                 label, suffix, state->sysroot);
      state->failed = TRUE;

      if (typeflag == FTW_D)
        return FTW_SKIP_SUBTREE;
    }

  return FTW_CONTINUE;
}

/*
 * pv_mtree_verify:
 * @mtree: (type filename): Path to a mtree(5) manifest
 * @sysroot: (type filename): A directory
 * @sysroot_fd: A fd opened on @sysroot
 * @source_files: (optional): A directory from which files will be
 *  hard-linked or copied when populating @sysroot. The `content`
 *  or filename in @mtree is taken to be relative to @source_files.
 * @flags: Flags affecting how this is done
 *
 * Check that the container root filesystem @sysroot conforms to @mtree.
 *
 * @mtree must contain a subset of BSD mtree(5) syntax,
 * as for pv_mtree_apply().
 *
 * For regular files, we check the type, size and sha256, and if the mode
 * has any executable bits set, we check that the file is executable.
 * Other modes and the modification time are not currently checked.
 * Other checksums are not currently supported.
 *
 * For directories, we check the type and that the directory is executable.
 *
 * For symbolic links, we check the type and target.
 *
 * Other file types are not currently supported.
 *
 * The `ignore` and `optional` flags are also supported.
 *
 * If a directory contains both `files` and `usr-mtree.txt.gz`,
 * we verify that `files` contains all of the content necessary to
 * reconstitute the tree described by `usr-mtree.txt.gz`.
 *
 * Returns: %TRUE on success
 */
gboolean
pv_mtree_verify (const char *mtree,
                 const char *sysroot,
                 int sysroot_fd,
                 PvMtreeApplyFlags flags,
                 GError **error)
{
  g_autoptr(SrtProfilingTimer) timer = NULL;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GHashTable) names = NULL;
  g_autoptr(GPtrArray) runtimes = NULL;
  g_autofree gchar *canonicalized_sysroot = NULL;
  int res = -1;
  gsize i;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (mtree != NULL, FALSE);
  g_return_val_if_fail (sysroot != NULL, FALSE);
  g_return_val_if_fail (sysroot_fd >= 0, FALSE);
  /* Can't run concurrently */
  g_return_val_if_fail (verify_state.sysroot == NULL, FALSE);

  timer = _srt_profiling_start ("Verify %s against %s", sysroot, mtree);
  g_info ("Verifying \"%s\" against \"%s\"...", sysroot, mtree);

  names = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  if (!(flags & PV_MTREE_APPLY_FLAGS_MINIMIZED_RUNTIME))
    runtimes = g_ptr_array_new_full (1, g_free);

  canonicalized_sysroot = flatpak_canonicalize_filename (sysroot);
  verify_state.sysroot = canonicalized_sysroot;
  verify_state.sysroot_fd = sysroot_fd;
  verify_state.names = names;
  verify_state.runtimes = runtimes;
  verify_state.failed = FALSE;

  if (pv_mtree_foreach (mtree, flags,
                        pv_mtree_foreach_verify_cb, &verify_state,
                        pv_mtree_foreach_verify_error_cb, &verify_state,
                        &local_error))
    {
      res = nftw (verify_state.sysroot, pv_mtree_verify_nftw_cb, 100,
                  FTW_PHYS | FTW_ACTIONRETVAL);

      if (verify_state.failed)
        res = -1;
    }

  verify_state.sysroot = NULL;
  verify_state.sysroot_fd = -1;
  verify_state.names = NULL;
  verify_state.runtimes = NULL;
  verify_state.failed = FALSE;

  if (runtimes != NULL)
    {
      for (i = 0; i < runtimes->len; i++)
        {
          const char *runtime = g_ptr_array_index (runtimes, i);
          g_autofree gchar *runtime_mtree = NULL;
          g_autofree gchar *runtime_files_rel = NULL;
          g_autofree gchar *runtime_files = NULL;
          glnx_autofd int runtime_fd = -1;
          g_autoptr(GError) runtime_error = NULL;

          runtime_mtree = g_build_filename (sysroot, runtime, "usr-mtree.txt.gz", NULL);
          runtime_files_rel = g_build_filename (runtime, "files", NULL);
          runtime_files = g_build_filename (sysroot, runtime_files_rel, NULL);
          runtime_fd = _srt_resolve_in_sysroot (sysroot_fd, runtime_files_rel,
                                                SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY,
                                                NULL, &runtime_error);

          if (runtime_fd < 0
              || !pv_mtree_verify (runtime_mtree, runtime_files, runtime_fd,
                                   flags | PV_MTREE_APPLY_FLAGS_MINIMIZED_RUNTIME,
                                   &runtime_error))
            {
              g_assert (runtime_error != NULL);
              g_warning ("%s", runtime_error->message);
              res = -1;
              continue;
            }
        }
    }

  if (res != 0)
    {
      if (local_error != NULL)
        g_propagate_error (error, g_steal_pointer (&local_error));
      else
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "Verifying \"%s\" with \"%s\" failed", sysroot, mtree);

      return FALSE;
    }

  g_message ("Verified \"%s\" against \"%s\" successfully", sysroot, mtree);
  return TRUE;
}

/*
 * Free the contents of @entry, but not @entry itself.
 */
void
pv_mtree_entry_clear (PvMtreeEntry *entry)
{
  g_clear_pointer (&entry->name, g_free);
  g_clear_pointer (&entry->contents, g_free);
  g_clear_pointer (&entry->link, g_free);
  g_clear_pointer (&entry->sha256, g_free);
}
