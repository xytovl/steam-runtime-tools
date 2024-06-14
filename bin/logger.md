---
title: srt-logger
section: 1
...

<!-- This document:
Copyright © 2024 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

srt-logger - record logs

# SYNOPSIS

**srt-logger**
[*OPTIONS*]
[**--**]
[*COMMAND* [*ARGUMENTS...*]]

# DESCRIPTION

If run without a *COMMAND*, **srt-logger** reads from standard input
and writes to a log file.
Depending on configuration and execution environment, messages might also
be copied to the terminal and/or the systemd Journal.

If run with a *COMMAND*, **srt-logger** runs the *COMMAND* with its
standard output and standard error connected to a pipe, with a new
instance of **srt-logger** reading from the pipe and logging as above.

The logger holds a shared **F_OFD_SETLK** lock on the log file, which is
temporarily upgraded to an exclusive lock during log rotation.
If running on an older kernel that does not support open file description
locks, then the logger holds a shared **F_SETLK** lock instead, and log
rotation is disabled.
These locks are compatible with those used by **bwrap**(1), **flatpak**(1),
**steam-runtime-supervisor**(1) and **pressure-vessel-adverb**(1).
It is unspecified whether they exclude the **flock**(2) locks used
by util-linux **flock**(1) or not, so using those locks on lock
files used by **steam-runtime-tools** should be avoided.

The name of this tool intentionally does not include **steam** so that
it will not be killed by commands like **pkill steam**, allowing any
final log messages during shutdown to be recorded.

# OPTIONS

**--background**
:   Run a new **srt-logger** process in the background (daemonized),
    instead of having it become a child of the *COMMAND* or the parent
    process.
    This can be useful when combined with **steam-runtime-supervisor**,
    because it is better for the **srt-logger** not to be killed during
    process termination (if it was, then any buffered log output would
    be lost).

**--exec-fallback**
:   If unable to set up logging, run the *COMMAND* anyway.
    If there is no *COMMAND*, and **--background** was not used,
    mirror messages received on standard input
    to standard error by running **cat**(1).
    This option should be used in situations where fault-tolerance is
    more important than logging.

**--filename** *FILENAME*
:   Write to the given *FILENAME* in the **--log-directory**,
    with rotation when it becomes too large.

    An empty *FILENAME* disables logging to flat files.

    If no **--filename** is given, the default is to append **.txt**
    to the **--identifier**.

**--identifier** *STRING*, **-t** *STRING*
:   When writing to the systemd Journal, log with the given *STRING*
    as an identifier.

    An empty *STRING* disables logging to the systemd Journal,
    unless a **--journal-fd** is also given.

    If no **--identifier** is given, the default is to remove the
    extension from the **--filename**.

    If no **--filename** is given either, when run with a *COMMAND*,
    the default is the basename of the first word of the *COMMAND*,
    similar to **systemd-cat**(1).

**--journal-fd** *FD*
:   Receive an inherited file descriptor instead of opening a new connection
    to the Journal.

**--log-directory** *PATH*
:   Interpret the **--filename** as being in *PATH*.
    If `$SRT_LOG_DIR` is set, the default is that directory,
    interpreted as being absolute or relative to the current working
    directory in the usual way.
    Otherwise, if `$STEAM_CLIENT_LOG_FOLDER` is set, the default is
    that directory interpreted as relative to `~/.steam/steam` (unusually,
    this is done even if it starts with `/`).
    Otherwise the default is `~/.steam/steam/logs`.
    The log directory must already exist during **srt-logger** startup.

**--log-fd** *FD*
:   Receive an inherited file descriptor for the log file instead of
    opening the log file again.
    It is an error to provide a **--log-fd** that does not point to the
    **--filename** in the **--log-directory**.
    If this is done, it is likely to lead to unintended results and
    potentially loss of log messages during log rotation.

**--no-auto-terminal**
:   Don't copy logged messages to the terminal, if any.
    If standard error is a terminal, the default is to copy logged messages
    to that terminal, and also provide it to subprocesses via
    `$SRT_LOG_TERMINAL`.
    Otherwise, if `$SRT_LOG_TERMINAL` is set, the default is to copy logged
    messages to that terminal.

**--sh-syntax**
:   Write machine-readable output to standard output.
    The output format is line-oriented, can be parsed by **eval**(1posix),
    and on success the last line will be exactly **SRT_LOGGER_READY=1**
    followed by a newline.
    When this option is used, if standard output is closed without any
    output, that indicates that the logger failed to initialize.
    If this happens, diagnostic messages will be written to standard error
    as usual.
    This is mainly used to implement **--background**, but can also be used
    elsewhere.

    If a line before the last starts with **SRT_LOGGER_PID=**, then it
    is a shell-style assignment indicating the process ID of the process
    that is responsible for logging (which might be a daemonized background
    process).
    The associated value might be quoted, and will need to be unquoted
    according to **sh**(1) rules before use,
    for example with GLib's **g_shell_unquote** or Python's **shlex.split**.

    If a line before the last starts with **export** or **unset** followed
    by a space, then it indicates an environment variable that should be
    set or unset for processes that are writing their output to this
    logger, to ensure that any **srt-logger** child processes direct their
    log output to appropriate destinations.
    The associated value for an **export** might be quoted, as above.

    Other output may be produced in future versions of this tool.

**--rotate** *BYTES*
:   If the **--filename** would exceed *BYTES*, rename it to a different
    filename (for example `log.txt` becomes `log.previous.txt`)
    and start a new log.

    If log rotation fails or another process is holding a lock on the
    log file, then rotation is disabled and *BYTES* may be exceeded.

    *BYTES* may be suffixed with
    `K`, `KiB`, `M` or `MiB` for powers of 1024,
    or `kB` or `MB` for powers of 1000.

    The default is 8 MiB. **--rotate=0** or environment variable
    **SRT_LOG_ROTATION=0** can be used to disable rotation.

**--terminal-fd** *FD*
:   Receive an inherited file descriptor for the terminal instead of
    opening the terminal device again.

**--use-journal**
:   Write messages to the systemd Journal if possible, even if no
    **--journal-fd** was given.

**--verbose**, **-v**
:   Be more verbose. If used twice, debug messages are shown.

**--version**
:   Print version information in YAML format.

# ENVIRONMENT

`SRT_LOG`
:   A sequence of tokens separated by colons, spaces or commas
    affecting how diagnostic output from **srt-logger** itself is recorded.
    In particular, `SRT_LOG=journal` has the same effect as
    `SRT_LOG_TO_JOURNAL=1`.

`SRT_LOG_DIR`
:   An absolute or relative path to be used instead of the default
    log directory.

`SRT_LOG_ROTATION` (`0` or `1`)
:   If set to `0`, log rotation is disabled and **--rotate** is ignored.

`SRT_LOG_TERMINAL`
:   If set to the absolute path of a terminal device such as `/dev/pts/0`,
    **srt-logger** will attempt to copy all log messages to that
    terminal device.
    If set to the empty string, the effect is the same as
    **--no-auto-terminal**.
    If output to the terminal is enabled, **srt-logger** also sets this
    environment variable for the *COMMAND*.

`SRT_LOG_TO_JOURNAL`, `SRT_LOGGER_USE_JOURNAL`
:   If either is set to `1`, log to the systemd Journal if available.
    As well as redirecting diagnostic output from **srt-logger** itself,
    this has an effect similar to adding
    **--use-journal** to all **srt-logger** invocations.
    If output to the Journal is enabled, **srt-logger** also sets
    **SRT_LOGGER_USE_JOURNAL=1** for the *COMMAND*,
    and may set **SRT_LOG_TO_JOURNAL=0** in order to ensure that child
    processes that are part of the steam-runtime-tools suite do not write
    directly to the Journal, which would cause them to bypass the
    **srt-logger**.

`STEAM_CLIENT_LOG_FOLDER`
:   A path relative to `~/.steam/steam` to be used as a default log
    directory if `$SRT_LOG_DIR` is unset.
    The default is `logs`.
    Note that unusually, this is interpreted as a relative path, even if
    it starts with `/` (this is consistent with the behaviour of the
    Steam client).

# OUTPUT

If a **--filename** could be determined, then log messages from standard
input or the *COMMAND* are written to that file.

If there was a **--journal-fd**, **--use-journal** was given, or standard
error is the systemd Journal, then the same log messages are copied to the
systemd Journal.

If neither of those log destinations are available, then log messages
are written to standard error.

Additionally, if a terminal has been selected for logging, the same
log messages are copied to that terminal.

# EXIT STATUS

The exit status is similar to **env**(1):

0
:   The *COMMAND* exited successfully with status 0.

125
:   Invalid arguments were given, or **srt-logger** failed to start.

126, 127
:   The *COMMAND* was not found or could not be launched.
    These errors are not currently distinguished.

255
:   The *COMMAND* was launched, but its exit status could not be
    determined. This happens if the wait-status was neither
    normal exit nor termination by a signal.

Any value
:   The *COMMAND* exited unsuccessfully with the status indicated.

128 + *n*
:   The *COMMAND* was killed by signal *n*.
    (This is the same encoding used by **bash**(1), **bwrap**(1) and
    **env**(1).)

# NOTES

If the **srt-logger** is to be combined with **steam-runtime-supervisor**(1)
or another subreaper that will wait for all of its descendant processes to
exit, then the **srt-logger** should normally be run "outside" the control
of the subreaper, using the **--background** option. This avoids situations
that can cause a deadlock or a loss of log messages.

This is particularly important when using **steam-runtime-supervisor**(1)
with the **--terminate-timeout** option, or a similar subreaper that will
terminate all of its child processes.

# EXAMPLES

Run vulkaninfo and send its output to vulkaninfo.txt and the systemd Journal:

    ~/.steam/root/ubuntu12_32/steam-runtime/run.sh \
    srt-logger --use-journal -t vulkaninfo -- vulkaninfo

The best way to combine **srt-logger** with **steam-runtime-supervisor**(1)
is to run the **srt-logger** "outside" the **steam-runtime-supervisor**
and use **--background**:

    ~/.steam/root/ubuntu12_32/steam-runtime/run.sh \
    srt-logger --background --use-journal -t supervised-game -- \
    steam-runtime-supervisor --subreaper --terminate-timeout=5 -- \
    ./your-game

<!-- vim:set sw=4 sts=4 et: -->
