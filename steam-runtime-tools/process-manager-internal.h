/*
 * Copyright © 2019-2024 Collabora Ltd.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <sys/wait.h>

#include <glib.h>
#include "libglnx.h"

#include "steam-runtime-tools/file-lock-internal.h"

int _srt_wait_status_to_exit_status (int wait_status);

gboolean _srt_wait_for_child_processes (pid_t main_process,
                                        int *wait_status_out,
                                        GError **error);

gboolean _srt_subreaper_terminate_all_child_processes (GTimeSpan wait_period,
                                                       GTimeSpan grace_period,
                                                       GError **error);

gboolean _srt_process_manager_init_single_threaded (GError **error);

typedef struct
{
  /* File descriptors to assign to other file descriptors, for example
   * { .target = 2, .source = 1 } is equivalent to 2>&1 in the shell */
  GArray *assign_fds;
  /* Exceptions to close_all_fds */
  GArray *pass_fds;
  /* Array of SrtFileLock */
  GPtrArray *locks;

  /* If greater than 0, wait this many microseconds after the main child
   * process has exited before terminating remaining child processes.
   * Must be non-negative. */
  GTimeSpan terminate_wait_usec;
  /* If greater than 0, after @terminate_wait_usec plus this many
   * microseconds, use `SIGKILL` instead of `SIGTERM`. If 0, proceed
   * directly to sending `SIGKILL`. */
  GTimeSpan terminate_grace_usec;

  /* If true, all fds not mentioned in @pass_fds or @assign_fds will be
   * closed, except for stdin, stdout and stderr. */
  unsigned close_fds : 1;
  /* If true, log the arguments and environment before launching the
   * child process. */
  unsigned dump_parameters : 1;
  /* If true, send SIGTERM to the process manager when its parent exits.
   * This will be forwarded to the child if @forward_signals is also true. */
  unsigned exit_with_parent : 1;
  /* If true, forward SIGTERM and similar signals from the process manager
   * to the main child process. */
  unsigned forward_signals : 1;
  /* If true, wait for all descendant processes to exit.
   * Must be true if using @terminate_wait_usec or @terminate_grace_usec. */
  unsigned subreaper : 1;
} SrtProcessManagerOptions;

#define SRT_PROCESS_MANAGER_OPTIONS_INIT \
{ \
  .terminate_grace_usec = -1, \
}

void _srt_process_manager_options_take_fd_assignment (SrtProcessManagerOptions *self,
                                                      int target,
                                                      int source);
void _srt_process_manager_options_take_original_stdout_stderr (SrtProcessManagerOptions *self,
                                                               int original_stdout,
                                                               int original_stderr);
gboolean _srt_process_manager_options_assign_fd_cli (SrtProcessManagerOptions *self,
                                                     const char *name,
                                                     const char *value,
                                                     GError **error);
gboolean _srt_process_manager_options_lock_fd_cli (SrtProcessManagerOptions *self,
                                                   const char *name,
                                                   const char *value,
                                                   GError **error);
gboolean _srt_process_manager_options_pass_fd_cli (SrtProcessManagerOptions *self,
                                                   const char *name,
                                                   const char *value,
                                                   GError **error);
void _srt_process_manager_options_take_lock (SrtProcessManagerOptions *self,
                                             SrtFileLock *lock);

GOptionGroup *_srt_process_manager_options_get_option_group (SrtProcessManagerOptions *self,
                                                             GError **error);

void _srt_process_manager_options_clear (SrtProcessManagerOptions *self);
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (SrtProcessManagerOptions, _srt_process_manager_options_clear)

typedef struct _SrtProcessManager SrtProcessManager;
typedef struct _SrtProcessManagerClass SrtProcessManagerClass;

#define SRT_TYPE_PROCESS_MANAGER \
  (_srt_process_manager_get_type ())
#define SRT_PROCESS_MANAGER(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), SRT_TYPE_PROCESS_MANAGER, SrtProcessManager))
#define SRT_IS_PROCESS_MANAGER(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), SRT_TYPE_PROCESS_MANAGER))
#define SRT_PROCESS_MANAGER_GET_CLASS(o) \
  (G_TYPE_INSTANCE_GET_CLASS ((o), SRT_TYPE_PROCESS_MANAGER, SrtProcessManagerClass))
#define SRT_PROCESS_MANAGER_CLASS(c) \
  (G_TYPE_CHECK_CLASS_CAST ((c), SRT_TYPE_PROCESS_MANAGER, SrtProcessManagerClass))
#define SRT_IS_PROCESS_MANAGER_CLASS(c) \
  (G_TYPE_CHECK_CLASS_TYPE ((c), SRT_TYPE_PROCESS_MANAGER))

GType _srt_process_manager_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtProcessManager, g_object_unref)

SrtProcessManager *_srt_process_manager_new (SrtProcessManagerOptions *options,
                                             GError **error);
gboolean _srt_process_manager_run (SrtProcessManager *self,
                                   const char * const *argv,
                                   const char * const *envp,
                                   GError **error);
int _srt_process_manager_get_exit_status (SrtProcessManager *self);
