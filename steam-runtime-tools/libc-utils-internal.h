/*<private_header>*/
/*
 * Utilities for programs that can't use GLib for whatever reason.
 *
 * Copyright 2019-2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>

void oom (void) __attribute__((noreturn));

void *steal_pointer (void *pp);
int steal_fd (int *fdp);
void clear_with_free (void *pp);
void clear_with_fclose (void *pp);

/*
 * autofclose:
 *
 * A type attribute marking a `FILE *` variable to be closed with `fclose()`
 * when it goes out of scope: `autofclose FILE *fh = fopen (...);`.
 */
#define autofclose __attribute__((__cleanup__(clear_with_fclose)))

/*
 * autofree:
 *
 * A type attribute marking a pointer variable to be freed with `free()`
 * when it goes out of scope: `autofree char *str = strdup (...);`.
 */
#define autofree __attribute__((__cleanup__(clear_with_free)))


/*
 * N_ELEMENTS:
 * @arr: An array (not a pointer! it must be an array)
 *
 * Returns: The number of items in @arr
 */
#define N_ELEMENTS(arr) (sizeof (arr) / sizeof (arr[0]))

/*
 * xasprintf:
 * ...: The same arguments as for `asprintf()`
 *
 * Try to call `asprintf()`. If it fails, print an error and crash.
 */
#define xasprintf(...) \
do { \
    if (asprintf (__VA_ARGS__) < 0) \
      oom (); \
} while (0)

/*
 * xcalloc:
 * @n: Number of items
 * @size: Size of each item
 *
 * Try to call `calloc()`. If it fails, print an error and crash.
 */
static inline void *
xcalloc (size_t n, size_t size)
{
  void *ret = calloc (n, size);

  if (ret == NULL)
    oom ();

  return ret;
}

/*
 * new0:
 * @type: A type, typically a struct
 *
 * Try to allocate one @type with `calloc()`. If it fails, crash.
 *
 * Returns: A newly-allocated, zero-filled `(type *)`
 */
#define new0(type) ((type *) xcalloc (1, sizeof (type)))
