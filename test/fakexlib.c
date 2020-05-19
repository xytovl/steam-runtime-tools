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
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include "fakexlib.h"

uint32_t behavior_flags;

Display*
XOpenDisplay(const char *name)
{
	assert(name == NULL);

	if (behavior_flags & FLAG_XOPEN_DISPLAY_FAIL)
		return NULL;

	/* Thanks to the DefaultScreenOfDisplay() macro we need to emulate the
	 * display struct initialized correctly enough to get past the segfaults
	 * otherwise */
	_XPrivDisplay dpy = calloc(1, sizeof(*dpy));
	dpy->default_screen = 0;
	dpy->screens = calloc(1, sizeof(Screen));
	dpy->screens[0].root = 10;
	return (Display*)dpy;
}

int
XCloseDisplay(Display *dpy)
{
	assert(dpy);
	free(((_XPrivDisplay)dpy)->screens);
	free(dpy);
	return 0;
}

int XRRQueryExtension(Display *dpy, int *event_base, int *error_base)
{
	assert(dpy);
	assert(event_base);
	assert(error_base);

	if (behavior_flags & FLAG_RR_EXT_FAIL)
		return 0;

	*event_base = 10;
	*error_base = 20;

	return 1;
}

int
XRRQueryVersion(Display *dpy, int *major, int *minor)
{
	assert(dpy);
	assert(major);
	assert(minor);

	if (behavior_flags & FLAG_RR_VERSION_FAIL)
		return 0;

	*major = 1;
	*minor = 2;

	return 1;
}

void
XRRFreeScreenResources(XRRScreenResources *resources)
{
	assert(resources);
	free(resources->outputs);
	free(resources);
}

void
XRRFreeOutputInfo(XRROutputInfo *output)
{
	assert(output);
	free(output->name);
	free(output);
}

XRRScreenResources *
XRRGetScreenResourcesCurrent(Display *dpy, Window win)
{
	assert(dpy);
	assert(win != 0);

	if (behavior_flags & FLAG_RR_RESOURCES_FAIL)
		return NULL;

	XRRScreenResources *res = calloc(1, sizeof(*res));
	res->outputs = calloc(1, sizeof(*res->outputs));
	res->outputs[0] = 1234;

	return res;
}

XRROutputInfo *
XRRGetOutputInfo(Display *dpy, XRRScreenResources *resources, RROutput output)
{
	assert(dpy);
	assert(output);
	assert(resources);

	if (behavior_flags & FLAG_RR_GET_OUTPUT_FAIL)
		return NULL;

	XRROutputInfo *out= calloc(1, sizeof(*out));

	if (behavior_flags & FLAG_RR_OUTPUT_NAME_WAYLAND)
		out->name = strdup("XWAYLAND0");
	else
		out->name = strdup("DP0");
	return out;
}
