/*<private_header>*/
/*
 * Copyright © 2020 Collabora Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib.h>
#include <libglnx.h>

#include "steam-runtime-tools/glib-backports-internal.h"

typedef struct _SrtSysroot SrtSysroot;
typedef struct _SrtSysrootClass SrtSysrootClass;

typedef enum
{
  SRT_SYSROOT_MODE_NORMAL = 0,
  SRT_SYSROOT_MODE_DIRECT,
} SrtSysrootMode;

struct _SrtSysroot
{
  GObject parent;
  gchar *path;
  int fd;
  SrtSysrootMode mode;
};

struct _SrtSysrootClass
{
  /*< private >*/
  GObjectClass parent_class;
};

#define SRT_TYPE_SYSROOT (_srt_sysroot_get_type ())
#define SRT_SYSROOT(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_SYSROOT, SrtSysroot))
#define SRT_SYSROOT_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_SYSROOT, SrtSysrootClass))
#define SRT_IS_SYSROOT(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_SYSROOT))
#define SRT_IS_SYSROOT_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_SYSROOT))
#define SRT_SYSROOT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_SYSROOT, SrtSysrootClass)

GType _srt_sysroot_get_type (void);
SrtSysroot *_srt_sysroot_new_take (gchar *path,
                                   int fd);
SrtSysroot *_srt_sysroot_new (const char *path,
                              GError **error);
SrtSysroot *_srt_sysroot_new_direct (GError **error);
SrtSysroot *_srt_sysroot_new_real_root (GError **error);
SrtSysroot *_srt_sysroot_new_flatpak_host (GError **error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtSysroot, g_object_unref)

static inline const char *
_srt_sysroot_get_path (SrtSysroot *self)
{
  return self->path;
}

static inline gboolean
_srt_sysroot_is_direct (SrtSysroot *self)
{
  return (self->mode == SRT_SYSROOT_MODE_DIRECT);
}

static inline int
_srt_sysroot_get_fd (SrtSysroot *self)
{
  return self->fd;
}

/*
 * SrtResolveFlags:
 * @SRT_RESOLVE_FLAGS_MKDIR_P: Create the filename to be resolved and
 *  all of its ancestors as directories. If any already exist, they
 *  must be directories or symlinks to directories.
 * @SRT_RESOLVE_FLAGS_KEEP_FINAL_SYMLINK: If the last component of
 *  the path is a symlink, return a fd pointing to the symlink itself.
 * @SRT_RESOLVE_FLAGS_REJECT_SYMLINKS: If any component of
 *  the path is a symlink, fail with %G_IO_ERROR_TOO_MANY_LINKS.
 * @SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY: The last component of the path
 *  must be a directory or a symlink to a directory.
 * @SRT_RESOLVE_FLAGS_MUST_BE_REGULAR: The last component of the path
 *  must be a regular file or a symlink to a regular file.
 * @SRT_RESOLVE_FLAGS_READABLE: Open the last component of the path
 *  for reading, instead of just as `O_PATH`.
 *  With @SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY, it will be opened as
 *  if via opendir(). Otherwise, it will be opened as if via open(),
 *  with `O_RDONLY` and `O_NOCTTY`.
 * @SRT_RESOLVE_FLAGS_RETURN_ABSOLUTE: Prefix `/` to @real_path_out,
 *  making it an absolute path.
 * @SRT_RESOLVE_FLAGS_MUST_BE_EXECUTABLE: The last component of the path
 *  must be executable.
 * @SRT_RESOLVE_FLAGS_NONE: No special behaviour.
 *
 * Flags affecting how _srt_resolve_in_sysroot() behaves.
 */
typedef enum
{
  SRT_RESOLVE_FLAGS_MKDIR_P = (1 << 0),
  SRT_RESOLVE_FLAGS_KEEP_FINAL_SYMLINK = (1 << 1),
  SRT_RESOLVE_FLAGS_REJECT_SYMLINKS = (1 << 2),
  SRT_RESOLVE_FLAGS_READABLE = (1 << 3),
  SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY = (1 << 4),
  SRT_RESOLVE_FLAGS_MUST_BE_REGULAR = (1 << 5),
  SRT_RESOLVE_FLAGS_RETURN_ABSOLUTE = (1 << 6),
  SRT_RESOLVE_FLAGS_MUST_BE_EXECUTABLE = (1 << 7),
  SRT_RESOLVE_FLAGS_NONE = 0
} SrtResolveFlags;

int _srt_sysroot_open (SrtSysroot *sysroot,
                       const char *path,
                       SrtResolveFlags flags,
                       gchar **resolved,
                       GError **error) G_GNUC_WARN_UNUSED_RESULT;

gboolean _srt_sysroot_load (SrtSysroot *sysroot,
                            const char *path,
                            SrtResolveFlags flags,
                            gchar **resolved,
                            gchar **contents_out,
                            gsize *len_out,
                            GError **error);

gboolean _srt_sysroot_test (SrtSysroot *sysroot,
                            const char *path,
                            SrtResolveFlags flags,
                            GError **error);

G_GNUC_INTERNAL
int _srt_resolve_in_sysroot (int sysroot,
                             const char *descendant,
                             SrtResolveFlags flags,
                             gchar **real_path_out,
                             GError **error) G_GNUC_WARN_UNUSED_RESULT;
