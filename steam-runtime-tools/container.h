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

#if !defined(_SRT_IN_SINGLE_HEADER) && !defined(_SRT_COMPILATION)
#error "Do not include directly, use <steam-runtime-tools/steam-runtime-tools.h>"
#endif

#include <glib.h>
#include <glib-object.h>

#include <steam-runtime-tools/macros.h>
#include <steam-runtime-tools/types.h>

typedef struct _SrtContainerInfo SrtContainerInfo;
typedef struct _SrtContainerInfoClass SrtContainerInfoClass;

#define SRT_TYPE_CONTAINER_INFO srt_container_info_get_type ()
#define SRT_CONTAINER_INFO(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_CONTAINER_INFO, SrtContainerInfo))
#define SRT_CONTAINER_INFO_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_CONTAINER_INFO, SrtContainerInfoClass))
#define SRT_IS_CONTAINER_INFO(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_CONTAINER_INFO))
#define SRT_IS_CONTAINER_INFO_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_CONTAINER_INFO))
#define SRT_CONTAINER_INFO_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_CONTAINER_INFO, SrtContainerInfoClass)
_SRT_PUBLIC
GType srt_container_info_get_type (void);

/**
 * SrtContainerType:
 * @SRT_CONTAINER_TYPE_UNKNOWN: Unknown container type
 * @SRT_CONTAINER_TYPE_NONE: No container detected
 * @SRT_CONTAINER_TYPE_FLATPAK: Running in a Flatpak app (see flatpak.org)
 * @SRT_CONTAINER_TYPE_PRESSURE_VESSEL: Running in a Steam Runtime container
 *  using pressure-vessel
 * @SRT_CONTAINER_TYPE_DOCKER: Running in a Docker container (see docker.com)
 * @SRT_CONTAINER_TYPE_PODMAN: Running in a Podman container (see podman.io)
 * @SRT_CONTAINER_TYPE_SNAP: Running in a Snap app (see snapcraft.io)
 *
 * A type of container.
 */
typedef enum
{
  SRT_CONTAINER_TYPE_NONE = 0,
  SRT_CONTAINER_TYPE_FLATPAK,
  SRT_CONTAINER_TYPE_PRESSURE_VESSEL,
  SRT_CONTAINER_TYPE_DOCKER,
  SRT_CONTAINER_TYPE_PODMAN,
  SRT_CONTAINER_TYPE_SNAP,
  SRT_CONTAINER_TYPE_UNKNOWN = -1
} SrtContainerType;

/**
 * SrtFlatpakIssues:
 * @SRT_FLATPAK_ISSUES_UNKNOWN: An internal error occurred while checking
 *  Flatpak sandbox capabilities, or an unknown issue flag was encountered
 *  in a JSON report, or no Flatpak container was detected
 * @SRT_FLATPAK_ISSUES_TOO_OLD: The version of Flatpak is too old for
 *  full functionality.
 * @SRT_FLATPAK_ISSUES_SUBSANDBOX_NOT_CHECKED: The diagnostic tool was
 *  unable to check for the ability to create a Flatpak subsandbox.
 * @SRT_FLATPAK_ISSUES_SUBSANDBOX_UNAVAILABLE: The diagnostic tool was unable
 *  to create a Flatpak subsandbox by using the equivalent of `flatpak-spawn(1)`.
 * @SRT_FLATPAK_ISSUES_SUBSANDBOX_TIMED_OUT: A timeout was encountered
 *  while trying to launch a Flatpak subsandbox.
 * @SRT_FLATPAK_ISSUES_SUBSANDBOX_LIMITED_BY_SETUID_BWRAP: The ability
 *  to create a Flatpak subsandbox was limited by a setuid `bwrap(1)`
 *  on the host system.
 * @SRT_FLATPAK_ISSUES_SUBSANDBOX_DID_NOT_INHERIT_DISPLAY: The ability
 *  to create a Flatpak subsandbox was limited by misconfiguration of
 *  the D-Bus or systemd activation environment.
 * @SRT_FLATPAK_ISSUES_SUBSANDBOX_OUTPUT_CORRUPTED: The Flatpak subsandbox
 *  adds unwanted text on standard output.
 * @SRT_FLATPAK_ISSUES_NONE: None of the above
 *
 * Flags describing problems with the Flatpak sandboxing framework and its
 * ability to create new "sub-sandboxes" for the Steam Linux Runtime.
 */
typedef enum
{
  SRT_FLATPAK_ISSUES_UNKNOWN = (1 << 0),
  SRT_FLATPAK_ISSUES_TOO_OLD = (1 << 1),
  SRT_FLATPAK_ISSUES_SUBSANDBOX_NOT_CHECKED = (1 << 2),
  SRT_FLATPAK_ISSUES_SUBSANDBOX_UNAVAILABLE = (1 << 3),
  SRT_FLATPAK_ISSUES_SUBSANDBOX_TIMED_OUT = (1 << 4),
  SRT_FLATPAK_ISSUES_SUBSANDBOX_LIMITED_BY_SETUID_BWRAP = (1 << 5),
  SRT_FLATPAK_ISSUES_SUBSANDBOX_DID_NOT_INHERIT_DISPLAY = (1 << 6),
  SRT_FLATPAK_ISSUES_SUBSANDBOX_OUTPUT_CORRUPTED = (1 << 7),
  SRT_FLATPAK_ISSUES_NONE = 0
} SrtFlatpakIssues;

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtContainerInfo, g_object_unref)
#endif

_SRT_PUBLIC
SrtContainerType srt_container_info_get_container_type (SrtContainerInfo *self);
_SRT_PUBLIC
const gchar *srt_container_info_get_container_host_directory (SrtContainerInfo *self);
_SRT_PUBLIC
SrtOsInfo *srt_container_info_get_container_host_os_info (SrtContainerInfo *self);
_SRT_PUBLIC
SrtFlatpakIssues srt_container_info_get_flatpak_issues (SrtContainerInfo *self);
_SRT_PUBLIC
const gchar *srt_container_info_get_flatpak_version (SrtContainerInfo *self);
