/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#if !defined(_SRT_IN_SINGLE_HEADER) && !defined(_SRT_COMPILATION)
#error "Do not include directly, use <steam-runtime-tools/steam-runtime-tools.h>"
#endif

#include <steam-runtime-tools/macros.h>

/**
 * SrtBwrapIssues:
 * @SRT_BWRAP_ISSUES_UNKNOWN: An internal error occurred while checking
 *  whether `bwrap(1)` can work, or an unknown issue flag was encountered
 *  in a JSON report
 * @SRT_BWRAP_ISSUES_CANNOT_RUN: The diagnostic tool was unable
 *  to run `bwrap(1)`, either a copy included with the Steam Runtime or
 *  a copy provided by the host system.
 * @SRT_BWRAP_ISSUES_SETUID: The first working version of
 *  `bwrap(1)` that was found is setuid root, which comes with some
 *  limitations.
 * @SRT_BWRAP_ISSUES_SYSTEM: The first working version of
 *  `bwrap(1)` that was found is from the operating system and might not
 *  have all of the features required by Steam.
 * @SRT_BWRAP_ISSUES_NO_UNPRIVILEGED_USERNS_CLONE: The sysctl
 *  `/proc/sys/kernel/unprivileged_userns_clone` added by kernel patches
 *  in several distributions was set to `0`, preventing unprivileged processes
 *  from creating new user namespaces.
 * @SRT_BWRAP_ISSUES_MAX_USER_NAMESPACES_ZERO: The sysctl
 *  `/proc/sys/user/max_user_namespaces` was set to `0`, preventing
 *  unprivileged processes from creating new user namespaces.
 * @SRT_BWRAP_ISSUES_NONE: None of the above
 *
 * Flags describing problems with `bwrap(1)` and its ability to create
 * new user namespaces.
 */
typedef enum
{
  SRT_BWRAP_ISSUES_UNKNOWN = (1 << 0),
  SRT_BWRAP_ISSUES_CANNOT_RUN = (1 << 1),
  SRT_BWRAP_ISSUES_SETUID = (1 << 2),
  SRT_BWRAP_ISSUES_SYSTEM = (1 << 3),
  SRT_BWRAP_ISSUES_NO_UNPRIVILEGED_USERNS_CLONE = (1 << 4),
  SRT_BWRAP_ISSUES_MAX_USER_NAMESPACES_ZERO = (1 << 5),
  SRT_BWRAP_ISSUES_NONE = 0
} SrtBwrapIssues;
