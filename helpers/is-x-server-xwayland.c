/*
 * Copyright © 2023 Collabora Ltd.
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

extern int xisxwayland (int argc, char **argv);

/* Silence the missing 'noreturn' attribute from xisxwayland subproject */
static void usage (void) __attribute__((__noreturn__));

#include <errno.h>
#include <getopt.h>

#include "subprojects/xisxwayland/xisxwayland.c"

enum
{
  OPTION_HELP = 1,
  OPTION_VERBOSE,
  OPTION_VERSION,
};

struct option long_options[] =
{
  { "help", no_argument, NULL, OPTION_HELP },
  { "verbose", no_argument, NULL, OPTION_VERBOSE },
  { "version", no_argument, NULL, OPTION_VERSION },
  { NULL, 0, NULL, 0 }
};


static void usage_and_exit (int code) __attribute__((__noreturn__));

/*
 * Print usage information and exit with status @code.
 */
static void
usage_and_exit (int code)
{
  FILE *fp;

  if (code == 0)
    fp = stdout;
  else
    fp = stderr;

  fprintf (fp, "Usage: %s [OPTIONS]\n",
           program_invocation_short_name);
  fprintf (fp, "Options:\n");
  fprintf (fp, "--help\t\tShow this help and exit\n");
  fprintf (fp, "--verbose\tBe more verbose\n");
  fprintf (fp, "--version\tShow version and exit\n");
  fprintf (fp, "\nExit status:\n");
  fprintf (fp, "0\t\tThe X server is Xwayland\n");
  fprintf (fp, "1\t\tThe X server is not Xwayland\n");
  fprintf (fp, "2\t\tInvalid usage\n");
  fprintf (fp, "3\t\tFailed to connect to the X server\n");
  exit (code);
}

int main (int argc,
          char** argv)
{
  int opt;

  while ((opt = getopt_long (argc, argv, "", long_options, NULL)) != -1)
    {
      switch (opt)
        {
          case OPTION_HELP:
            usage_and_exit (0);
            break;

          case OPTION_VERBOSE:
            /* Passing verbose down to xisxwayland() */
            break;

          case OPTION_VERSION:
            /* Output version number as YAML for machine-readability,
             * inspired by `ostree --version` and `docker version` */
            printf (
                "%s:\n"
                " Package: steam-runtime-tools\n"
                " Version: %s\n",
                argv[0], VERSION);
            return 0;

          case '?':
          default:
            usage_and_exit (1);
            break;  /* not reached */
        }
    }

  return xisxwayland (argc, argv);
}
