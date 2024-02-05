/*
 * Copyright © 2019-2024 Collabora Ltd.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <sys/wait.h>

#include <glib.h>

int _srt_wait_status_to_exit_status (int wait_status);

gboolean _srt_wait_for_child_processes (pid_t main_process,
                                        int *wait_status_out,
                                        GError **error);

gboolean _srt_subreaper_terminate_all_child_processes (GTimeSpan wait_period,
                                                       GTimeSpan grace_period,
                                                       GError **error);
