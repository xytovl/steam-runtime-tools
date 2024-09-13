#!/usr/bin/env bash
# Copyright © 2024 Collabora Ltd
# SPDX-License-Identifier: MIT

set -eu

me="$0"
me="$(readlink -f "$0")"
here="${me%/*}"
me="${me##*/}"

[ -n "${G_TEST_SRCDIR-}" ] || G_TEST_SRCDIR="$here"
[ -n "${G_TEST_BUILDDIR-}" ] || G_TEST_BUILDDIR="$here"
# This is input into logger-0.bash, even if unexported.
# shellcheck disable=SC2034
SRT_LOGGER="${G_TEST_BUILDDIR}/../bin/srt-logger"

debug () {
    echo "logger-0: $*" >&2 || :
}

# We shellcheck this separately
# shellcheck source=/dev/null
if source "${G_TEST_SRCDIR}/../bin/logger-0.bash" "$@"; then
    debug "sourced logger-0.bash successfully"
else
    debug "sourced logger-0.bash unsuccessfully: $?"
fi

echo 'emitted from stdout'
echo 'EMITTED FROM STDERR' >&2
