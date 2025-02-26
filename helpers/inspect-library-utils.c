/*
 * Copyright © 2019-2023 Collabora Ltd.
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

#include <argz.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <link.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <assert.h>
#include <unistd.h>

#include "inspect-library-utils.h"

/*
 * Print a bytestring to stdout, escaping backslashes and control
 * characters in octal. The result can be parsed with g_strcompress().
 */
void
print_strescape (const char *bytestring)
{
  const unsigned char *p;

  for (p = (const unsigned char *) bytestring; *p != '\0'; p++)
    {
      if (*p < ' ' || *p >= 0x7f || *p == '\\')
        printf ("\\%03o", *p);
      else
        putc (*p, stdout);
    }
}

void
print_json_string_content (const char *s)
{
  const unsigned char *p;

  for (p = (const unsigned char *) s; *p != '\0'; p++)
    {
      if (*p == '"' || *p == '\\' || *p <= 0x1F || *p >= 0x80)
        printf ("\\u%04x", *p);
      else
        printf ("%c", *p);
    }
}

/*
 * Print an array element as line based
 */
void
print_array_entry (const char *entry,
                   const char *name)
{
  assert (entry != NULL);
  assert (name != NULL);

  fprintf (stdout, "%s=", name);
  print_strescape (entry);
  putc ('\n', stdout);
}

/*
 * Print an array in stdout as line based
 */
void
print_argz (const char *name,
            const char *argz_values,
            size_t argz_n)
{
  const char *entry = 0;

  while ((entry = argz_next (argz_values, argz_n, entry)))
    print_array_entry (entry, name);
}
