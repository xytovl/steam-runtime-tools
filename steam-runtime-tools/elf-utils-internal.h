/*<private_header>*/
/*
 * Copyright © 2019-2023 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <gelf.h>

#include <steam-runtime-tools/glib-backports-internal.h>

G_DEFINE_AUTOPTR_CLEANUP_FUNC(Elf, elf_end);

gboolean _srt_open_elf (int dfd,
                        const gchar *file_path,
                        int *fd,
                        Elf **elf,
                        GError **error);
