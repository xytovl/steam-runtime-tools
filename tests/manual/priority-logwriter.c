/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

/*
 * priority-logwriter:
 *
 * A sample standalone implementation of an old-style GLogFunc and a
 * new-style GLogWriter that emit GLib diagnostic messages with a
 * level/priority prefix, ready to be parsed by
 * `srt-logger --parse-level-prefix` or `systemd-cat --level-prefix=1`.
 *
 * For example:
 *
 * ```
 * srt-logger -t example --parse-level-prefix -- ./priority-logwriter
 * ```
 */

/* This can optionally be defined to opt-in to g_debug(), g_warning()
 * etc. emitting new-style structured logging, which can carry additional
 * metadata. In a real program you would typically define this globally,
 * in the build system, rather than in a single source file */
#define G_LOG_USE_STRUCTURED

#include <errno.h>
#include <syslog.h>

#include <glib.h>
#include <glib-object.h>

static int
get_level_priority (GLogLevelFlags log_level)
{
  if (log_level & (G_LOG_FLAG_RECURSION
                   | G_LOG_FLAG_FATAL
                   | G_LOG_LEVEL_ERROR
                   | G_LOG_LEVEL_CRITICAL))
    return LOG_ERR;

  if (log_level & G_LOG_LEVEL_WARNING)
    return LOG_WARNING;

  if (log_level & G_LOG_LEVEL_MESSAGE)
    return LOG_NOTICE;

  if (log_level & G_LOG_LEVEL_INFO)
    return LOG_INFO;

  if (log_level & G_LOG_LEVEL_DEBUG)
    return LOG_DEBUG;

  return LOG_NOTICE;
}

static const char *
get_level_prefix (GLogLevelFlags log_level)
{
  if (log_level & (G_LOG_FLAG_RECURSION
                   | G_LOG_FLAG_FATAL
                   | G_LOG_LEVEL_ERROR))
    return "fatal error: ";

  if (log_level & G_LOG_LEVEL_CRITICAL)
    return "internal error: ";

  if (log_level & G_LOG_LEVEL_WARNING)
    return "warning: ";

  return "";
}

static void
priority_log_helper (const char *log_domain,
                     GLogLevelFlags log_level,
                     const char *message,
                     gssize message_len,
                     gboolean structured_logging)
{
  char priority_prefix[] = { '<', '0', '>', '\0' };
  GString *edited_message = g_string_new_len (message, message_len);
  gsize i;

  priority_prefix[1] += get_level_priority (log_level);

  for (i = 0; i < edited_message->len; i++)
    {
      if (edited_message->str[i] == '\n')
        {
          g_string_insert (edited_message, i + 1, priority_prefix);
          i += strlen (priority_prefix);
        }
    }

  g_printerr ("%s%s [%s] %s%s%s\n",
              priority_prefix,
              g_get_prgname (),
              log_domain != NULL ? log_domain : "main",
              structured_logging ? "" : "(old log API) ",
              get_level_prefix (log_level),
              edited_message->str);
  g_string_free (edited_message, TRUE);
}

/*
 * A GLogFunc used to handle old-style unstructured logging.
 */
static void
priority_logfunc (const char *log_domain,
                  GLogLevelFlags log_level,
                  const char *message,
                  void *user_data)
{
  priority_log_helper (log_domain, log_level, message, -1, FALSE);
}

/*
 * A GLogWriterFunc used to handle new-style structured logging.
 */
static GLogWriterOutput
priority_log_writer (GLogLevelFlags log_level,
                     const GLogField *fields,
                     gsize n_fields,
                     void *user_data)
{
  const char *log_domain = NULL;
  const char *message = NULL;
  gssize message_len = 0;
  gsize i;

  for (i = 0; i < n_fields; i++)
    {
      if (g_strcmp0 (fields[i].key, "MESSAGE") == 0)
        {
          message = fields[i].value;
          message_len = fields[i].length;
        }

      if (g_strcmp0 (fields[i].key, "GLIB_DOMAIN") == 0
          && fields[i].length < 0)
        log_domain = fields[i].value;
    }

  if (message != NULL)
    {
      priority_log_helper (log_domain, log_level, message, message_len, TRUE);
      return G_LOG_WRITER_HANDLED;
    }

  return G_LOG_WRITER_UNHANDLED;
}

int
main (int argc,
      const char **argv)
{
  gchar *errno_str;

  g_set_prgname ("priority-logwriter");
  g_log_set_default_handler (priority_logfunc, NULL);
  g_log_set_writer_func (priority_log_writer, NULL, NULL);

  g_critical ("this is a mockup of a critical warning");
  g_warning ("this is a mockup of an ordinary warning");
  g_message ("this is a mockup of a somewhat important message");
  g_message ("this message\ncontains\nmultiple\nlines");
  g_info ("this is a mockup of an informational message");
  g_debug ("this is a mockup of a debug message");

  g_log ("MyLib", G_LOG_LEVEL_CRITICAL,
         "this is a mockup of a critical warning from a library");
  g_log ("MyLib", G_LOG_LEVEL_WARNING,
         "this is a mockup of an ordinary warning from a library");
  g_log ("MyLib", G_LOG_LEVEL_MESSAGE,
         "this is a mockup of a somewhat important message from a library");
  g_log ("MyLib", G_LOG_LEVEL_MESSAGE,
         "this message\ncontains\nmultiple\nlines");
  g_log ("MyLib", G_LOG_LEVEL_INFO,
         "this is a mockup of an informational message from a library");
  g_log ("MyLib", G_LOG_LEVEL_DEBUG,
         "this is a mockup of a debug message from a library");

  /* A mockup of a library emitting a structured message with extra fields */
  errno_str = g_strdup_printf ("%d", EXDEV);
  g_log_structured ("MyLib", G_LOG_LEVEL_MESSAGE,
                    "MESSAGE_ID", "ce09319b7e2a430c8a12afa73f1e0a23",
                    "ERRNO", errno_str,
                    "CODE_FILE", __FILE__,
                    "CODE_LINE", G_STRINGIFY (__LINE__),
                    "CODE_FUNC", G_STRFUNC,
                    "MESSAGE", "Structured message: %s [errno %d]",
                    g_strerror (EXDEV), EXDEV);
  g_free (errno_str);

  return 0;
}
