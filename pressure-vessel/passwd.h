/*
 * Copyright © 2020-2024 Collabora Ltd.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <sys/types.h>
#include <grp.h>
#include <pwd.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/resolve-in-sysroot-internal.h"

/* Mock data to be used during unit-testing */
typedef struct
{
  uid_t uid;
  gid_t gid;
  const struct passwd *pwd;
  const struct group *grp;
  int lookup_errno;
} PvMockPasswdLookup;

gchar *pv_generate_etc_passwd (SrtSysroot *source,
                               PvMockPasswdLookup *mock);
gchar *pv_generate_etc_group (SrtSysroot *source,
                              PvMockPasswdLookup *mock);
