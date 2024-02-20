/*
 * Copyright 2019-2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "libc-utils-internal.h"

#include <err.h>

/*
 * steal_pointer:
 * @pp: A pointer to some pointer type, for example a `FILE **`
 *
 * Set `*pp` to %NULL and return its previous value.
 * This is conceptually the same as GLib g_steal_pointer().
 *
 * Returns: The original value of `*pp`, possibly %NULL
 */
void *
steal_pointer (void *pp)
{
  typedef void *__attribute__((may_alias)) voidp_alias;
  voidp_alias *pointer_to_pointer = pp;
  void *ret = *pointer_to_pointer;
  *pointer_to_pointer = NULL;
  return ret;
}

/*
 * steal_fd:
 * @fdp: A pointer to a file descriptor
 *
 * Set `*fdp` to -1 and return its previous value.
 *
 * Returns: The original value of `*fdp`, possibly negative
 */
int
steal_fd (int *fdp)
{
  int fd = *fdp;
  *fdp = -1;
  return fd;
}

/*
 * clear_with_free:
 * @pp: A pointer to some pointer allocated with `malloc()` or compatible
 *
 * If `*pp` is not %NULL, free the pointer that it points to.
 * Set `*pp` to %NULL.
 */
void
clear_with_free (void *pp)
{
  free (steal_pointer (pp));
}

/*
 * clear_with_fclose:
 * @pp: A `FILE **`
 *
 * If `*pp` is not %NULL, close the `FILE` that it points to.
 * Set `*pp` to %NULL.
 */
void
clear_with_fclose (void *pp)
{
  FILE *fh = steal_pointer (pp);

  if (fh != NULL)
    fclose (fh);
}

/*
 * oom:
 *
 * Crash with an "out of memory" error.
 */
void
oom (void)
{
  warnx ("Out of memory");
  abort ();
}
