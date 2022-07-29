/* SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
 * NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <assert.h>
#include <stdio.h>

#include "fakexlib.h"

extern int xisxwayland(int argc, char **argv __attribute__((unused)));

static char *no_args[1] = {"xisxwayland"};

static void
test_no_dpy(void)
{
	RESET_FLAGS();
	FLAG(FLAG_XOPEN_DISPLAY_FAIL);
	assert(xisxwayland(1, no_args) == 3);
}

static void
test_rr_extension_fail(void)
{
	RESET_FLAGS();
	FLAG(FLAG_RR_EXT_FAIL);
	assert(xisxwayland(1, no_args) == 1);
}

static void
test_rr_version_fail(void)
{
	RESET_FLAGS();
	FLAG(FLAG_RR_VERSION_FAIL);
	assert(xisxwayland(1, no_args) == 1);
}

static void
test_rr_resources_fail(void)
{
	RESET_FLAGS();
	FLAG(FLAG_RR_RESOURCES_FAIL);
	assert(xisxwayland(1, no_args) == 3);
}

static void
test_rr_getoutput_fail(void)
{
	RESET_FLAGS();
	FLAG(FLAG_RR_GET_OUTPUT_FAIL);
	assert(xisxwayland(1, no_args) == 3);
}

static void
test_xwayland(void)
{
	RESET_FLAGS();
	FLAG(FLAG_RR_OUTPUT_NAME_WAYLAND);
	assert(xisxwayland(1, no_args) == 0);
}

static void
test_not_xwayland(void)
{
	RESET_FLAGS();
	assert(xisxwayland(1, no_args) == 1);
}

static void
test_xwayland_ext(void)
{
	RESET_FLAGS();
	FLAG(FLAG_XWAYLAND_EXTENSION);
	/* just to make sure we don't pick up the fallback */
	FLAG(FLAG_RR_EXT_FAIL);

	assert(xisxwayland(1, no_args) == 0);
}

int main(void) {
	test_no_dpy();
	test_rr_extension_fail();
	test_rr_version_fail();
	test_rr_resources_fail();
	test_rr_getoutput_fail();

	test_xwayland();
	test_not_xwayland();
	test_xwayland_ext();

	return 0;
}
