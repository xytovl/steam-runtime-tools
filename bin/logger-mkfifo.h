/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <glib.h>

gboolean srt_logger_mkfifo (int original_stdout,
                            int argc,
                            char **argv,
                            GError **error);
