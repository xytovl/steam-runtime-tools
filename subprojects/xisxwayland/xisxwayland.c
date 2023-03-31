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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#define EXIT_IS_XWAYLAND	0
#define EXIT_NOT_XWAYLAND	1
#define EXIT_INVALID_USAGE	2
#define EXIT_ERROR		3

static void usage(void)
{
	fprintf(stderr,
		"Usage: xisxwayland [-v|--verbose]\n"
		"\n"
		"Options:\n"
		"  -v, --verbose ... verbose output\n"
		"\n"
		"Exit status:\n"
		"  0 ... the X server is Xwayland\n"
		"  1 ... the X server is not Xwayland\n"
		"  2 ... invalid usage\n"
		"  3 ... failed to connect to the X server\n");
	exit(EXIT_INVALID_USAGE);
}

int xisxwayland(int argc, char **argv)
{
	Display *dpy = NULL;
	XRRScreenResources *resources = NULL;
	XRROutputInfo *output = NULL;
	int rc = EXIT_ERROR;
	bool verbose = false;
	int opcode, event, error;

	if (argc > 1) {
		if (argc == 2)
			verbose = strcmp(argv[1], "-v") == 0 ||
				  strcmp(argv[1], "--verbose") == 0;
		if (!verbose)
			usage();
	}

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "Failed to connect to X server\n");
		goto out;
	}
	/* For new-enough servers, the XWAYLAND extension is the reliable
	 * way to check for Xwayland.
	 */
	if (XQueryExtension(dpy, "XWAYLAND", &opcode, &event, &error)) {
		rc = EXIT_IS_XWAYLAND;
		goto out;
	}

	/* There is no definitive way of checking for an XWayland server,
	 * but the two working methods are:
	 * - RandR output names in Xwayland are XWAYLAND0, XWAYLAND1, etc.
	 * - XI devices are xwayland-pointer:10, xwayland-keyboard:11
	 * Let's go with the XRandR check here because it's slightly less
	 * code to write.
	 */

	int event_base, error_base, major, minor;
	if (!XRRQueryExtension(dpy, &event_base, &error_base) ||
	    !XRRQueryVersion(dpy, &major, &minor)) {
		/* e.g. Xnest, but definitely not Xwayland */
		rc = EXIT_NOT_XWAYLAND;
		goto out;
	}

	resources = XRRGetScreenResourcesCurrent(dpy, DefaultRootWindow(dpy));
	if (!resources) {
		fprintf(stderr, "Failed to get screen resources\n");
		goto out;
	}

	output = XRRGetOutputInfo(dpy, resources, resources->outputs[0]);
	if (!output) {
		fprintf(stderr, "Failed to get output info\n");
		goto out;
	}

	if (strncmp(output->name, "XWAYLAND", 8) == 0)
		rc = EXIT_IS_XWAYLAND;

	XRRFreeOutputInfo(output);

	if (rc == EXIT_IS_XWAYLAND)
		goto out;

	rc = EXIT_NOT_XWAYLAND;
out:
	if (resources)
		XRRFreeScreenResources(resources);
	if (dpy)
		XCloseDisplay(dpy);

	if (verbose) {
		switch (rc) {
		case EXIT_IS_XWAYLAND:
			printf("Xwayland: YES\n");
			break;
		case EXIT_NOT_XWAYLAND:
			printf("Xwayland: NO\n");
			break;
		default:
			break;
		}
	}

	return rc;
}
