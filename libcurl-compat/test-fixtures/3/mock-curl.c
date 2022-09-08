/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 *
 * A stand-in for a host OS copy of libcurl with the ABI historically
 * used in Debian.
 *
 * To make the behaviour of this mock library consistent with how libcurl
 * will actually behave, we need to import at least one symbol from glibc;
 * see ../scout/mock-curl.c for details.
 */

#include <unistd.h>

__attribute__((__visibility__("default"))) const char *curl_version (void);

const char *
curl_version (void)
{
  (void) access ("3", F_OK);
  return "mockup of libcurl from host OS with CURL_*_3 symbols";
}
