/*
 * Copyright © 2019-2020 Collabora Ltd.
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

#include "config.h"

#include <fcntl.h>
#include <unistd.h>

#include <gio/gio.h>
#include "libglnx.h"

#include "steam-runtime-tools/file-lock-internal.h"
#include "steam-runtime-tools/missing-internal.h"

/**
 * SrtFileLock:
 *
 * A read/write lock compatible with the locks taken out by
 * `bwrap --lock-file FILENAME` and Flatpak.
 */
struct _SrtFileLock
{
  int fd;
  gboolean is_ofd;
};

static gboolean
_srt_file_lock_acquire (int fd,
                        const char *path,
                        SrtFileLockFlags flags,
                        gboolean *is_ofd_out,
                        GError **error)
{
  int ofd;

  /* If PROCESS_ORIENTED, only do ofd == 0. If not, try 1 then 0. */
  for (ofd = (flags & SRT_FILE_LOCK_FLAGS_PROCESS_ORIENTED) ? 0 : 1;
       ofd >= 0;
       ofd--)
    {
      struct flock l = {
        .l_type = F_RDLCK,
        .l_whence = SEEK_SET,
        .l_start = 0,
        .l_len = 0
      };
      const char *type_str = "reading";
      int cmd;
      int saved_errno;

      /*
       * We want OFD locks because:
       *
       * - ordinary process-associated F_SETLK fnctl(2) locks are unlocked on
       *   fork(), but bwrap forks before calling into user code, so by the
       *   time we run our child process, it will have lost the lock
       * - flock(2) locks are orthogonal to fnctl(2) locks, so we can't take
       *   a lock that excludes the F_SETLK locks used by Flatpak/bwrap
       *
       * F_OFD_SETLK and F_SETLK are documented to conflict with each other,
       * so for example by holding an OFD read-lock, we can prevent other
       * processes from taking a process-associated write-lock, or vice versa.
       */
      if (ofd)
        {
          if (flags & SRT_FILE_LOCK_FLAGS_WAIT)
            cmd = F_OFD_SETLKW;
          else
            cmd = F_OFD_SETLK;
        }
      else
        {
          if (flags & SRT_FILE_LOCK_FLAGS_WAIT)
            cmd = F_SETLKW;
          else
            cmd = F_SETLK;
        }

      if (flags & SRT_FILE_LOCK_FLAGS_EXCLUSIVE)
        {
          l.l_type = F_WRLCK;
          type_str = "writing";
        }

      if (TEMP_FAILURE_RETRY (fcntl (fd, cmd, &l)) == 0)
        {
          if (is_ofd_out != NULL)
            *is_ofd_out = (ofd > 0);

          return TRUE;
        }

      saved_errno = errno;

      /* If the kernel doesn't support OFD locks, fall back to
       * process-oriented locks if allowed. */
      if (saved_errno == EINVAL &&
          ofd &&
          (flags & SRT_FILE_LOCK_FLAGS_REQUIRE_OFD) == 0)
        continue;

      if (saved_errno == EACCES || saved_errno == EAGAIN)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_BUSY,
                       "Unable to lock %s for %s: file is busy",
                       path, type_str);
          return FALSE;
        }

      return glnx_throw_errno_prefix (error, "Unable to lock %s for %s",
                                      path, type_str);
    }

  g_return_val_if_reached (FALSE);
}

/**
 * srt_file_lock_new:
 * @at_fd: If not `AT_FDCWD` or -1, look up @path relative to this
 *  directory fd instead of the current working directory, as per `openat(2)`
 * @path: File to lock
 * @flags: Flags affecting how we lock the file
 * @error: Used to raise an error on failure
 *
 * Take out a lock on a file.
 *
 * If %SRT_FILE_LOCK_FLAGS_EXCLUSIVE is in @flags, the lock is an exclusive
 * (write) lock, which can be held by at most one process at a time. This is
 * appropriate when about to modify or delete the locked resource.
 * Otherwise it is a shared (read) lock, which excludes exclusive locks
 * but does not exclude other shared locks. This is appropriate when using
 * but not modifying the locked resource.
 *
 * If %SRT_FILE_LOCK_FLAGS_WAIT is not in @flags, raise %G_IO_ERROR_BUSY
 * if the lock cannot be obtained immediately.
 *
 * Returns: (nullable): A lock (release and free with srt_file_lock_free())
 *  or %NULL.
 */
SrtFileLock *
srt_file_lock_new (int at_fd,
                   const gchar *path,
                   SrtFileLockFlags flags,
                   GError **error)
{
  glnx_autofd int fd = -1;
  int open_flags = O_CLOEXEC | O_NOCTTY;
  gboolean ofd;

  g_return_val_if_fail (path != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);
  g_return_val_if_fail ((flags & SRT_FILE_LOCK_FLAGS_PROCESS_ORIENTED) == 0 ||
                        (flags & SRT_FILE_LOCK_FLAGS_REQUIRE_OFD) == 0,
                        NULL);

  if (flags & SRT_FILE_LOCK_FLAGS_CREATE)
    open_flags |= O_RDWR | O_CREAT;
  else if (flags & SRT_FILE_LOCK_FLAGS_EXCLUSIVE)
    open_flags |= O_RDWR;
  else
    open_flags |= O_RDONLY;

  at_fd = glnx_dirfd_canonicalize (at_fd);
  fd = TEMP_FAILURE_RETRY (openat (at_fd, path, open_flags, 0644));

  if (fd < 0)
    {
      glnx_throw_errno_prefix (error, "openat(%s)", path);
      return NULL;
    }

  if (!_srt_file_lock_acquire (fd, path, flags, &ofd, error))
    return NULL;

  return srt_file_lock_new_take (g_steal_fd (&fd), ofd);
}

/**
 * srt_file_lock_new_take:
 * @fd: A file descriptor, already locked
 * @is_ofd: %TRUE if @fd is an open file descriptor lock
 *
 * Convert a simple file descriptor into a #SrtFileLock.
 *
 * Returns: (not nullable): A lock (release and free
 *  with srt_file_lock_free())
 */
SrtFileLock *
srt_file_lock_new_take (int fd,
                        gboolean is_ofd)
{
  SrtFileLock *self = NULL;

  g_return_val_if_fail (fd >= 0, NULL);
  g_return_val_if_fail (is_ofd == 0 || is_ofd == 1, NULL);

  self = g_slice_new0 (SrtFileLock);
  self->fd = g_steal_fd (&fd);
  self->is_ofd = is_ofd;
  return self;
}

void
srt_file_lock_free (SrtFileLock *self)
{
  glnx_autofd int fd = -1;

  g_return_if_fail (self != NULL);

  fd = g_steal_fd (&self->fd);
  g_slice_free (SrtFileLock, self);
  /* fd is closed by glnx_autofd if necessary, and that releases the lock */
}

int
srt_file_lock_steal_fd (SrtFileLock *self)
{
  g_return_val_if_fail (self != NULL, -1);
  return g_steal_fd (&self->fd);
}

gboolean
srt_file_lock_is_ofd (SrtFileLock *self)
{
  g_return_val_if_fail (self != NULL, FALSE);
  return self->is_ofd;
}
