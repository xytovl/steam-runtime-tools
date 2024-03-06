/*<private_header>*/
/*
 * Copyright © 2014-2019 Red Hat, Inc
 * Copyright © 2017-2024 Collabora Ltd.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "steam-runtime-tools/glib-backports-internal.h"

#include "steam-runtime-tools/bwrap.h"
#include "steam-runtime-tools/resolve-in-sysroot-internal.h"
#include "steam-runtime-tools/subprocess-internal.h"

typedef enum
{
  SRT_BWRAP_FLAGS_SYSTEM = (1 << 0),
  SRT_BWRAP_FLAGS_SETUID = (1 << 1),
  SRT_BWRAP_FLAGS_HAS_PERMS = (1 << 2),
  SRT_BWRAP_FLAGS_NONE = 0
} SrtBwrapFlags;

gchar *_srt_check_bwrap (SrtSubprocessRunner *runner,
                         const char *pkglibexecdir,
                         gboolean skip_testing,
                         SrtBwrapFlags *flags_out,
                         GError **error);

SrtBwrapIssues _srt_check_bwrap_issues (SrtSysroot *sysroot,
                                        SrtSubprocessRunner *runner,
                                        const char *pkglibexecdir,
                                        gchar **bwrap_out,
                                        gchar **message_out);
