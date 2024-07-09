/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <locale.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <libglnx.h>

#include <steam-runtime-tools/log-internal.h>

#include "logger-mkfifo.h"

static gboolean
create_fifo (const char *parent,
             gchar **fifo_out,
             GError **error)
{
  g_autofree gchar *fifo = g_build_filename (parent, "fifo", NULL);

  if (mkfifo (fifo, 0700) < 0)
    return glnx_throw_errno_prefix (error, "Unable to create fifo \"%s\"", fifo);

  g_debug ("Created fifo: %s", fifo);
  *fifo_out = g_steal_pointer (&fifo);
  return TRUE;
}

static gboolean
write_output (int fd,
              const char *fifo,
              GError **error)
{
  if (glnx_loop_write (fd, fifo, strlen (fifo)) < 0)
    return glnx_throw_errno_prefix (error, "Unable to write filename");

  if (glnx_loop_write (fd, "\n", 1) < 0)
    return glnx_throw_errno_prefix (error, "Unable to write newline");

  g_debug ("Wrote fifo to stdout");
  return TRUE;
}

static void
cleanup (const char *dir)
{
  g_autoptr(GError) local_error = NULL;

  if (dir == NULL)
    return;

  g_debug ("Removing \"%s\"", dir);

  if (!glnx_shutil_rm_rf_at (AT_FDCWD, dir, NULL, &local_error))
    {
      _srt_log_warning ("%s", local_error->message);
      g_clear_error (&local_error);
    }
}

static gboolean
try_various_paths (gchar **fifo_out,
                   GError **error)
{
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *in_preferred_temp_dir = NULL;
  char in_tmp_template[] = "/tmp/srt-fifo.XXXXXX";
  const char *dir;
  const char *runtime_dir;

  runtime_dir = g_get_user_runtime_dir ();

  if (runtime_dir != NULL
      && g_file_test (runtime_dir, G_FILE_TEST_IS_DIR))
    {
      g_autofree gchar *template = NULL;

      g_debug ("Trying XDG_RUNTIME_DIR \"%s\"", runtime_dir);

      template = g_build_filename (runtime_dir, "srt-fifo.XXXXXX", NULL);
      g_debug ("Template: \"%s\"", template);
      dir = g_mkdtemp (template);

      if (dir != NULL)
        {
          if (create_fifo (dir, fifo_out, &local_error))
            return TRUE;
        }
      else
        {
          glnx_throw_errno_prefix (&local_error, "g_mkdtemp");
        }

      _srt_log_warning ("%s", local_error->message);
      g_clear_error (&local_error);
      cleanup (dir);
    }

  g_debug ("Trying preferred temp directory");
  in_preferred_temp_dir = g_dir_make_tmp ("srt-fifo.XXXXXX", &local_error);

  if (in_preferred_temp_dir != NULL
      && create_fifo (in_preferred_temp_dir, fifo_out, &local_error))
    return TRUE;

  _srt_log_warning ("%s", local_error->message);
  g_clear_error (&local_error);
  cleanup (in_preferred_temp_dir);

  g_debug ("Trying /tmp");
  dir = g_mkdtemp (in_tmp_template);

  if (dir != NULL)
    {
      if (create_fifo (dir, fifo_out, &local_error))
        return TRUE;
    }
  else
    {
      glnx_throw_errno_prefix (&local_error, "g_mkdtemp");
    }

  _srt_log_warning ("%s", local_error->message);
  g_clear_error (&local_error);
  cleanup (dir);

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
               "Unable to create a fifo in $XDG_RUNTIME_DIR, $TMPDIR or /tmp");
  return FALSE;
}

gboolean
srt_logger_mkfifo (int original_stdout,
                   int argc,
                   char **argv,
                   GError **error)
{
  g_autofree gchar *fifo = NULL;

  if (argc != 1)
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_UNKNOWN_OPTION,
                   "srt-logger --mkfifo takes no other arguments");
      return FALSE;
    }

  if (!try_various_paths (&fifo, error))
    return FALSE;

  if (!write_output (original_stdout, fifo, error))
    {
      g_autofree gchar *dir = g_path_get_dirname (fifo);

      if (unlink (fifo) < 0)
        _srt_log_warning ("unlink \"%s\": %s", fifo, g_strerror (errno));

      if (rmdir (dir) < 0)
        _srt_log_warning ("rmdir \"%s\": %s", dir, g_strerror (errno));
    }

  return TRUE;
}
