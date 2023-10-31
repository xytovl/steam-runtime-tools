/*<private_header>*/
/*
 * Copyright © 2023 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <steam-runtime-tools/macros.h>
#include <steam-runtime-tools/glib-backports-internal.h>

#include "steam-runtime-tools/system-info.h"

typedef struct _SrtSubprocessRunner SrtSubprocessRunner;
typedef struct _SrtSubprocessRunnerClass SrtSubprocessRunnerClass;

#define SRT_TYPE_SUBPROCESS_RUNNER (_srt_subprocess_runner_get_type ())
#define SRT_SUBPROCESS_RUNNER(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), SRT_TYPE_SUBPROCESS_RUNNER, SrtSubprocessRunner))
#define SRT_IS_SUBPROCESS_RUNNER(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), SRT_TYPE_SUBPROCESS_RUNNER))
#define SRT_SUBPROCESS_RUNNER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), SRT_TYPE_SUBPROCESS_RUNNER, SrtSubprocessRunnerClass))
#define SRT_SUBPROCESS_RUNNER_CLASS(c) (G_TYPE_CHECK_CLASS_CAST ((c), SRT_TYPE_SUBPROCESS_RUNNER, SrtSubprocessRunnerClass))
#define SRT_IS_SUBPROCESS_RUNNER_CLASS(c) (G_TYPE_CHECK_CLASS_TYPE ((c), SRT_TYPE_SUBPROCESS_RUNNER))

GType _srt_subprocess_runner_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtSubprocessRunner, g_object_unref)

static inline SrtSubprocessRunner *
_srt_subprocess_runner_new (void)
{
  return g_object_new (SRT_TYPE_SUBPROCESS_RUNNER,
                       NULL);
}

static inline SrtSubprocessRunner *
_srt_subprocess_runner_new_full (const char * const *envp,
                                 const char *helpers_path,
                                 SrtTestFlags flags)
{
  return g_object_new (SRT_TYPE_SUBPROCESS_RUNNER,
                       "environ", envp,
                       "helpers-path", helpers_path,
                       "test-flags", flags,
                       NULL);
}

const char * const *_srt_subprocess_runner_get_environ (SrtSubprocessRunner *self);
const char *_srt_subprocess_runner_getenv (SrtSubprocessRunner *self, const char *var);
const char *_srt_subprocess_runner_get_helpers_path (SrtSubprocessRunner *self);
SrtTestFlags _srt_subprocess_runner_get_test_flags (SrtSubprocessRunner *self);

typedef enum
{
  SRT_HELPER_FLAGS_SEARCH_PATH = (1 << 0),
  SRT_HELPER_FLAGS_TIME_OUT = (1 << 1),
  SRT_HELPER_FLAGS_TIME_OUT_SOONER = (1 << 2),
  SRT_HELPER_FLAGS_KEEP_GAMEOVERLAYRENDERER = (1 << 3),
  SRT_HELPER_FLAGS_LIBGL_VERBOSE = (1 << 4),
  SRT_HELPER_FLAGS_STDOUT_SILENCE = (1 << 5),
  SRT_HELPER_FLAGS_NONE = 0
} SrtHelperFlags;

GPtrArray *_srt_subprocess_runner_get_helper (SrtSubprocessRunner *self,
                                              const char *multiarch,
                                              const char *base,
                                              SrtHelperFlags flags,
                                              GError **error);
gboolean _srt_subprocess_runner_spawn_sync (SrtSubprocessRunner *self,
                                            SrtHelperFlags flags,
                                            const char * const *argv,
                                            gchar **stdout_out,
                                            gchar **stderr_out,
                                            gint *wait_status_out,
                                            GError **error);
