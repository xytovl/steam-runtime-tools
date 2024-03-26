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

#pragma once

#include <glib.h>
#include <glib-object.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "libglnx.h"

/**
 * SrtFileLockFlags:
 * @SRT_FILE_LOCK_FLAGS_CREATE: If the lock file doesn't exist, create it
 * @SRT_FILE_LOCK_FLAGS_WAIT: If another process holds an incompatible lock,
 *  wait for it to be released; by default srt_file_lock_new()
 *  raises %G_IO_ERROR_BUSY immediately
 * @SRT_FILE_LOCK_FLAGS_EXCLUSIVE: Take an exclusive (write) lock instead
 *  of the default shared (read) lock
 * @SRT_FILE_LOCK_FLAGS_REQUIRE_OFD: Require an open file descriptor lock,
 *  which is not released on fork(). By default srt_file_lock_new() tries
 *  an OFD lock first, then falls back to process-oriented locks if the
 *  kernel is older than Linux 3.15.
 * @SRT_FILE_LOCK_FLAGS_PROCESS_ORIENTED: Require a process-oriented lock,
 *  which is released on fork(). By default srt_file_lock_new() uses
 *  an OFD lock if available.
 * @SRT_FILE_LOCK_FLAGS_VERBOSE: If the lock cannot be acquired immediately,
 *  log a message before waiting for it and another message when it is
 *  acquired. Currently ignored if not also using %SRT_FILE_LOCK_FLAGS_WAIT.
 * @SRT_FILE_LOCK_FLAGS_NONE: None of the above
 *
 * Flags affecting how we take a lock on a runtime directory.
 */
typedef enum
{
  SRT_FILE_LOCK_FLAGS_CREATE = (1 << 0),
  SRT_FILE_LOCK_FLAGS_WAIT = (1 << 1),
  SRT_FILE_LOCK_FLAGS_EXCLUSIVE = (1 << 2),
  SRT_FILE_LOCK_FLAGS_REQUIRE_OFD = (1 << 3),
  SRT_FILE_LOCK_FLAGS_PROCESS_ORIENTED = (1 << 4),
  SRT_FILE_LOCK_FLAGS_VERBOSE = (1 << 5),
  SRT_FILE_LOCK_FLAGS_NONE = 0
} SrtFileLockFlags;

typedef struct _SrtFileLock SrtFileLock;

SrtFileLock *srt_file_lock_new (int at_fd,
                                const gchar *path,
                                SrtFileLockFlags flags,
                                GError **error);
SrtFileLock *srt_file_lock_new_take (int fd,
                                     gboolean is_ofd);
void srt_file_lock_free (SrtFileLock *self);
int srt_file_lock_steal_fd (SrtFileLock *self);
gboolean srt_file_lock_is_ofd (SrtFileLock *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtFileLock, srt_file_lock_free)
