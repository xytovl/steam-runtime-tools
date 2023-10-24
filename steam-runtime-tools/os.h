/*
 * Copyright © 2019-2023 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#if !defined(_SRT_IN_SINGLE_HEADER) && !defined(_SRT_COMPILATION)
#error "Do not include directly, use <steam-runtime-tools/steam-runtime-tools.h>"
#endif

#include <glib.h>
#include <glib-object.h>

#include <steam-runtime-tools/macros.h>
#include <steam-runtime-tools/types.h>

#define SRT_TYPE_OS_INFO srt_os_info_get_type ()
#define SRT_OS_INFO(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_OS_INFO, SrtOsInfo))
#define SRT_OS_INFO_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_OS_INFO, SrtOsInfoClass))
#define SRT_IS_OS_INFO(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_OS_INFO))
#define SRT_IS_OS_INFO_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_OS_INFO))
#define SRT_OS_INFO_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_OS_INFO, SrtOsInfoClass)

_SRT_PUBLIC
GType srt_os_info_get_type (void);

_SRT_PUBLIC
const char *srt_os_info_get_build_id (SrtOsInfo *self);
_SRT_PUBLIC
const char *srt_os_info_get_id (SrtOsInfo *self);
_SRT_PUBLIC
const char * const *srt_os_info_get_id_like (SrtOsInfo *self);
_SRT_PUBLIC
const char *srt_os_info_get_name (SrtOsInfo *self);
_SRT_PUBLIC
const char *srt_os_info_get_pretty_name (SrtOsInfo *self);
_SRT_PUBLIC
const char *srt_os_info_get_variant (SrtOsInfo *self);
_SRT_PUBLIC
const char *srt_os_info_get_variant_id (SrtOsInfo *self);
_SRT_PUBLIC
const char *srt_os_info_get_version_codename (SrtOsInfo *self);
_SRT_PUBLIC
const char *srt_os_info_get_version_id (SrtOsInfo *self);

_SRT_PUBLIC
GHashTable *srt_os_info_dup_fields (SrtOsInfo *self);
_SRT_PUBLIC
const char *srt_os_info_get_messages (SrtOsInfo *self);
_SRT_PUBLIC
const char *srt_os_info_get_source_path (SrtOsInfo *self);
_SRT_PUBLIC
const char *srt_os_info_get_source_path_resolved (SrtOsInfo *self);

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtOsInfo, g_object_unref)
#endif
