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

#pragma once

#define _SRT_IN_SINGLE_HEADER

#include <steam-runtime-tools/architecture.h>
#include <steam-runtime-tools/bwrap.h>
#include <steam-runtime-tools/container.h>
#include <steam-runtime-tools/cpu-feature.h>
#include <steam-runtime-tools/desktop-entry.h>
#include <steam-runtime-tools/display.h>
#include <steam-runtime-tools/enums.h>
#include <steam-runtime-tools/graphics.h>
#include <steam-runtime-tools/graphics-drivers-dri.h>
#include <steam-runtime-tools/graphics-drivers-egl.h>
#include <steam-runtime-tools/graphics-drivers-glx.h>
#include <steam-runtime-tools/graphics-drivers-vaapi.h>
#include <steam-runtime-tools/graphics-drivers-vdpau.h>
#include <steam-runtime-tools/graphics-drivers-vulkan.h>
#include <steam-runtime-tools/graphics-runtime-openxr.h>
#include <steam-runtime-tools/input-device.h>
#include <steam-runtime-tools/library.h>
#include <steam-runtime-tools/locale.h>
#include <steam-runtime-tools/macros.h>
#include <steam-runtime-tools/os.h>
#include <steam-runtime-tools/runtime.h>
#include <steam-runtime-tools/steam.h>
#include <steam-runtime-tools/system-info.h>
#include <steam-runtime-tools/utils.h>
#include <steam-runtime-tools/xdg-portal.h>

#undef _SRT_IN_SINGLE_HEADER
