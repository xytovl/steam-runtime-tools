#!/bin/sh
#
# SPDX-License-Identifier: LGPL-2.1+
#
# Copyright © 2018 Collabora Ltd
#
# This package is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This package is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this package.  If not, see
# <http://www.gnu.org/licenses/>.

set -e
set -u

if ! command -v shellcheck >/dev/null 2>&1; then
    echo "1..0 # SKIP shellcheck not available"
    exit 0
fi

if [ -z "${TOP_SRCDIR-}" ]; then
    me="$(readlink -f "$0")"
    srcdir="${me%/*}"
    TOP_SRCDIR="${srcdir%/*}"
fi

cd "$TOP_SRCDIR"

n=0
for shell_script in \
        pressure-vessel-wrap \
        t/shellcheck.t \
        ; do
    n=$((n + 1))
    if shellcheck "$shell_script"; then
        echo "ok $n - $shell_script"
    else
        echo "not ok $n # TODO - $shell_script"
    fi
done

echo "1..$n"

# vim:set sw=4 sts=4 et ft=sh:
