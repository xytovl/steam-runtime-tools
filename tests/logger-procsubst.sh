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

debug () {
    echo "logger-procsubst: $*" >&2 || :
}

redirect_log () {
    local SRT_LOGGER_PID
    local SRT_LOGGER_READY
    local fifo
    local from_stdout
    local to_stdin

    fifo=$("$G_TEST_BUILDDIR/../bin/srt-logger" --mkfifo)

    exec {to_stdin}> >(
        # blocks until parent opens the fifo for reading, then allows
        # the srt-logger to proceed
        exec > "$fifo"

        exec "$G_TEST_BUILDDIR/../bin/srt-logger" --background --sh-syntax "$@"
    )

    # blocks until child opens the fifo for writing, then allows the
    # parent to proceed
    exec {from_stdout}< "$fifo"

    # the temporary directory no longer needs to exist after both sides
    # have opened it
    rm -f "$fifo"
    rmdir "${fifo%/*}"

    debug "Reading from child stdout $from_stdout"
    output="$(cat <&${from_stdout})"
    debug 'EOF reached'

    # for debugging, would not be done in production
    printf '%s\n' "$output" | sed -e 's/^/logger-procsubst: srt-logger stdout: /g' >&2

    if [ "${output%SRT_LOGGER_READY=1}" != "$output" ] && eval "$output"; then
        debug "SRT_LOG_TERMINAL set to ${SRT_LOG_TERMINAL-(unset)}"
        debug "SRT_LOGGER_USE_JOURNAL set to ${SRT_LOGGER_USE_JOURNAL-(unset)}"
        debug "Logger $SRT_LOGGER_PID ready, sending subsequent output to pipe $to_stdin"
        exec >&${to_stdin}
        debug 'Sending subsequent stderr to pipe too'
        exec 2>&1
    else
        debug 'Unable to set up log redirection, not redirecting'
    fi

    debug 'Closing duplicate pipe from stdout'
    exec {from_stdout}<&-
    debug 'Closing duplicate pipe to stdin'
    exec {to_stdin}>&-
}

redirect_log "$@"
echo 'emitted from stdout'
echo 'EMITTED FROM STDERR' >&2
