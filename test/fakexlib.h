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

#pragma once

#include <stdint.h>

extern uint32_t behavior_flags;

#define bit(x_) (1UL << x_)

enum lib_behaviors {
	FLAG_RESET = 0,
	FLAG_XOPEN_DISPLAY_FAIL = bit(0),
	FLAG_RR_EXT_FAIL = bit(1),
	FLAG_RR_VERSION_FAIL = bit(2),
	FLAG_RR_RESOURCES_FAIL = bit(3),
	FLAG_RR_GET_OUTPUT_FAIL = bit(4),
	FLAG_RR_OUTPUT_NAME_WAYLAND = bit(5),
};

#define FLAG(f_) (behavior_flags |= (f_))
#define RESET_FLAGS() (behavior_flags = FLAG_RESET)
