/*
 * Copyright © 2019-2023 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "steam-runtime-tools/elf-utils-internal.h"

#include <elf.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils.h"

/**
 * _srt_open_elf:
 * @dfd: A directory file descriptor, `AT_FDCWD` or -1
 * @file_path: (type filename): Non-empty path to a library relative to @dfd
 * @fd: (out): Used to return a file descriptor of the opened library
 * @elf: (out): Used to return an initialized Elf of the library
 * @error: Used to raise an error on failure
 *
 * Returns: %TRUE if the Elf has been opened correctly
 */
gboolean
_srt_open_elf (int dfd,
               const gchar *file_path,
               int *fd,
               Elf **elf,
               GError **error)
{
  glnx_autofd int file_fd = -1;
  g_autoptr(Elf) local_elf = NULL;

  g_return_val_if_fail (file_path != NULL, FALSE);
  g_return_val_if_fail (file_path[0] != '\0', FALSE);
  g_return_val_if_fail (fd != NULL, FALSE);
  g_return_val_if_fail (elf != NULL, FALSE);
  g_return_val_if_fail (*elf == NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  dfd = glnx_dirfd_canonicalize (dfd);

  if (elf_version (EV_CURRENT) == EV_NONE)
    return glnx_throw (error, "elf_version(EV_CURRENT): %s",
                       elf_errmsg (elf_errno ()));

  if ((file_fd = openat (dfd, file_path, O_RDONLY | O_CLOEXEC, 0)) < 0)
    return glnx_throw_errno_prefix (error, "Error reading \"%s\"", file_path);

  if ((local_elf = elf_begin (file_fd, ELF_C_READ, NULL)) == NULL)
    return glnx_throw (error, "Error reading library \"%s\": %s",
                       file_path, elf_errmsg (elf_errno ()));

  *fd = g_steal_fd (&file_fd);
  *elf = g_steal_pointer (&local_elf);

  return TRUE;
}
