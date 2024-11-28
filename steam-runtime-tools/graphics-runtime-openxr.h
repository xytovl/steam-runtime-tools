/*
 * Copyright © 2019-2022 Collabora Ltd.
 * Copyright © 2024 Patrick Nicolas <patricknicolas@laposte.net>.
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
#include <steam-runtime-tools/graphics.h>

typedef struct _SrtOpenxrRuntime SrtOpenxrRuntime;
typedef struct _SrtOpenxrRuntimeClass SrtOpenxrRuntimeClass;

#define SRT_TYPE_OPENXR_RUNTIME (srt_openxr_runtime_get_type ())
#define SRT_OPENXR_RUNTIME(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_OPENXR_RUNTIME, SrtOpenxrRuntime))
#define SRT_OPENXR_RUNTIME_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_OPENXR_RUNTIME, SrtOpenxrRuntimeClass))
#define SRT_IS_OPENXR_RUNTIME(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_OPENXR_RUNTIME))
#define SRT_IS_OPENXR_RUNTIME_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_OPENXR_RUNTIME))
#define SRT_OPENXR_RUNTIME_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_OPENXR_RUNTIME, SrtOpenxrRuntimeClass)

_SRT_PUBLIC
GType srt_openxr_runtime_get_type (void);

_SRT_PUBLIC
gboolean srt_openxr_runtime_check_error (SrtOpenxrRuntime *self,
                                         GError **error);
_SRT_PUBLIC
const gchar *srt_openxr_runtime_get_api_version (SrtOpenxrRuntime *self);
_SRT_PUBLIC
const gchar *srt_openxr_runtime_get_json_path (SrtOpenxrRuntime *self);
_SRT_PUBLIC
const gchar *srt_openxr_runtime_get_library_path (SrtOpenxrRuntime *self);
_SRT_PUBLIC
const gchar *srt_openxr_runtime_get_library_arch (SrtOpenxrRuntime *self);
_SRT_PUBLIC
SrtLoadableIssues srt_openxr_runtime_get_issues (SrtOpenxrRuntime *self);
_SRT_PUBLIC
gchar *srt_openxr_runtime_resolve_library_path (SrtOpenxrRuntime *self);
_SRT_PUBLIC
SrtOpenxrRuntime *srt_openxr_runtime_new_replace_library_path (SrtOpenxrRuntime *self,
                                                               const char *path);
_SRT_PUBLIC
gboolean srt_openxr_runtime_write_to_file (SrtOpenxrRuntime *self,
                                           const char *path,
                                           GError **error);

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtOpenxrRuntime, g_object_unref)
#endif
