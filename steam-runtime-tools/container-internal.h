/*<private_header>*/
/*
 * Copyright © 2021 Collabora Ltd.
 *
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

#pragma once

#include "steam-runtime-tools/container.h"
#include "steam-runtime-tools/os.h"
#include "steam-runtime-tools/resolve-in-sysroot-internal.h"
#include "steam-runtime-tools/subprocess-internal.h"

/*
 * _srt_container_info_new:
 * @type: Type of container
 * @flatpak_issues: Issues that reduce Steam functionality under Flatpak.
 *  This value is relevant only if @type is %SRT_CONTAINER_TYPE_FLATPAK.
 * @flatpak_version: (nullable): Flatpak container version, this value is
 *  relevant only if @type is %SRT_CONTAINER_TYPE_FLATPAK
 * @host_directory: (nullable) (type filename): Directory where host files can
 *  be found
 * @host_os_info: (nullable): Information about the OS of @host_directory,
 *  if available
 *
 * Inline convenience function to create a new SrtContainerInfo.
 * This is not part of the public API.
 *
 * Returns: (transfer full): A new #SrtContainerInfo
 */
static inline SrtContainerInfo *_srt_container_info_new (SrtContainerType type,
                                                         SrtFlatpakIssues flatpak_issues,
                                                         const gchar *flatpak_version,
                                                         const gchar *host_directory,
                                                         SrtOsInfo *host_os_info);

#ifndef __GTK_DOC_IGNORE__
static inline SrtContainerInfo *
_srt_container_info_new (SrtContainerType type,
                         SrtFlatpakIssues flatpak_issues,
                         const gchar *flatpak_version,
                         const gchar *host_directory,
                         SrtOsInfo *host_os_info)
{
  return g_object_new (SRT_TYPE_CONTAINER_INFO,
                       "type", type,
                       "flatpak-issues", flatpak_issues,
                       "flatpak-version", flatpak_version,
                       "host-directory", host_directory,
                       "host-os-info", host_os_info,
                       NULL);
}

static inline SrtContainerInfo *
_srt_container_info_new_empty (void)
{
  return _srt_container_info_new (SRT_CONTAINER_TYPE_UNKNOWN,
                                  SRT_FLATPAK_ISSUES_UNKNOWN,
                                  NULL, NULL, NULL);
}
#endif

/* See flatpak-metadata(5) */
#define FLATPAK_METADATA_GROUP_INSTANCE "Instance"
#define FLATPAK_METADATA_KEY_FLATPAK_VERSION "flatpak-version"

SrtContainerInfo *_srt_check_container (SrtSysroot *sysroot);

void _srt_container_info_check_issues (SrtContainerInfo *self,
                                       SrtSubprocessRunner *runner);
