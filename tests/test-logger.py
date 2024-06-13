#!/usr/bin/env python3
# Copyright 2020-2024 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

import enum
import io
import logging
import os
import re
import select
import subprocess
import sys
import tempfile
import time
from pathlib import Path


try:
    import typing
    typing      # placate pyflakes
except ImportError:
    pass

from testutils import (
    BaseTest,
    test_main,
)


logger = logging.getLogger('test-logger')


LAUNCH_EX_FAILED = 125
LAUNCH_EX_USAGE = 125
LAUNCH_EX_CANNOT_INVOKE = 126
LAUNCH_EX_NOT_FOUND = 127
STDOUT_FILENO = 1
STDERR_FILENO = 2


def wrap_process_io(process_io: 'typing.Optional[typing.IO[bytes]]') -> io.TextIOWrapper:
    assert process_io is not None
    return io.TextIOWrapper(process_io)


class TestLogger(BaseTest):
    def setUp(self) -> None:
        super().setUp()

        if 'SRT_TEST_UNINSTALLED' in os.environ:
            self.test_adverb = self.command_prefix + [
                'env',
                os.path.join(
                    self.top_builddir,
                    'tests',
                    'adverb'
                ),
            ]
            self.logger = self.command_prefix + [
                'env',
                os.path.join(
                    self.top_builddir,
                    'bin',
                    'srt-logger'
                ),
            ]
            self.logger_procsubst = self.command_prefix + [
                'env',
                'G_TEST_BUILDDIR=%s/tests' % self.top_builddir,
                'G_TEST_SRCDIR=%s/tests' % self.top_srcdir,
                os.path.join(
                    self.top_srcdir,
                    'tests',
                    'logger-procsubst.sh'
                ),
            ]
            self.logger_0 = self.command_prefix + [
                'env',
                'G_TEST_BUILDDIR=%s/tests' % self.top_builddir,
                'G_TEST_SRCDIR=%s/tests' % self.top_srcdir,
                os.path.join(
                    self.top_srcdir,
                    'tests',
                    'logger-0.sh'
                ),
            ]
            self.supervisor = self.command_prefix + [
                'env',
                os.path.join(
                    self.top_builddir,
                    'bin',
                    'steam-runtime-supervisor'
                ),
            ]
        else:
            self.skipTest('Not available as an installed-test')

    def test_background(self, cat=False, use_sh_syntax=False) -> None:
        args = []

        if use_sh_syntax:
            args.append('--sh-syntax')

        if cat:
            args.append('--')
            args.extend(self.test_adverb)
            args.append('--assert-no-children')
            args.append('--')
            args.append('cat')

        proc = subprocess.Popen(
            self.logger + [
                '--background',
                '--filename=',
            ] + args,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        stdin = proc.stdin
        assert stdin is not None

        stdout = proc.stdout
        assert stdout is not None

        stderr = proc.stderr
        assert stderr is not None

        # It daemonizes (but we can't easily wait for this if there's a
        # cat subprocess)
        if not cat:
            proc.wait()

        # We can wait for it to be ready
        with stdout:
            content = stdout.read()

            if use_sh_syntax:
                self.assertEqual(
                    content.split(b'\n')[-2:],
                    [b'SRT_LOGGER_READY=1', b''],
                )
            else:
                self.assertEqual(content, b'')

        with stdin:
            stdin.write(b'hello, world\n')

        with stderr:
            content = stderr.read()
            logger.info('stderr: %s', content.decode('utf-8', 'replace'))
            self.assertIn(b'hello, world\n', content)

        if cat:
            proc.wait()

        self.assertEqual(proc.returncode, 0)

    def test_background_cat(self) -> None:
        self.test_background(cat=True)

    def test_background_cat_sh_syntax(self) -> None:
        self.test_background(cat=True, use_sh_syntax=True)

    def test_background_sh_syntax(self) -> None:
        self.test_background(use_sh_syntax=True)

    def test_concurrent_logging(self, use_sh_syntax=False) -> None:
        '''
        If there is more than one writer for the same log file, rotation
        is disabled to avoid data loss
        '''
        with tempfile.TemporaryDirectory() as tmpdir:
            with open(str(Path(tmpdir, 'cat.txt')), 'w'):
                pass

            args = []

            if use_sh_syntax:
                args.append('--sh-syntax')

            lock_holder = subprocess.Popen(
                self.logger + [
                    '--filename=log.txt',
                    '--log-directory', tmpdir,
                ] + args,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=STDERR_FILENO,
            )

            proc = subprocess.Popen(
                self.logger + [
                    '--filename=log.txt',
                    '--log-directory', tmpdir,
                    '--rotate=50',
                ] + args,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=STDERR_FILENO,
            )

            # Wait for the first logger to be running, which it signals
            # by closing standard output.
            stdout = lock_holder.stdout
            assert stdout is not None

            with stdout:
                content = stdout.read()

                if use_sh_syntax:
                    self.assertEqual(
                        content.split(b'\n')[-2:],
                        [b'SRT_LOGGER_READY=1', b''],
                    )
                else:
                    # Without --sh-syntax there is no output at all,
                    # and in particular we don't see SRT_LOGGER_READY
                    self.assertEqual(content, b'')

            # Write enough output to the second logger to cause rotation.
            # It won't happen, because the lock_holder is holding a
            # separate shared lock.
            stdin = proc.stdin
            assert stdin is not None

            with stdin:
                for i in range(5):
                    stdin.write(b'.' * 20)
                    stdin.write(b'\n')
                    stdin.flush()

            stdout = proc.stdout
            assert stdout is not None

            with stdout:
                content = stdout.read()

                if use_sh_syntax:
                    self.assertEqual(
                        content.split(b'\n')[-2:],
                        [b'SRT_LOGGER_READY=1', b''],
                    )
                else:
                    self.assertEqual(content, b'')

            proc.wait()

            with open(str(Path(tmpdir, 'log.txt')), 'rb') as reader:
                lines = reader.read().splitlines(keepends=True)

                lines = [
                    line
                    for line in lines
                    if not line.startswith(b'srt-logger[')
                ]

                self.assertEqual(
                    lines,
                    [(b'.' * 20) + b'\n'] * 5,
                )

            self.assertFalse(Path(tmpdir, 'log.previous.txt').exists())

            stdin = lock_holder.stdin
            assert stdin is not None
            stdin.close()
            lock_holder.wait()

    def test_default_filename_from_argv0(self) -> None:
        '''
        Exercise default filename from COMMAND, and also default
        log directory from $SRT_LOG_DIR
        '''
        with tempfile.TemporaryDirectory() as tmpdir:
            with open(str(Path(tmpdir, 'cat.txt')), 'w'):
                pass

            proc = subprocess.Popen(
                [
                    'env',
                    'SRT_LOG_DIR=' + tmpdir,
                ] + self.logger + [
                    '--no-auto-terminal',
                    '--sh-syntax',
                    '--',
                    'cat',
                ],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=STDERR_FILENO,
            )

            stdin = proc.stdin
            assert stdin is not None

            with stdin:
                stdin.write(b'hello, world\n')
                stdin.flush()

            # Wait for the logger to be holding its lock on the log file,
            # which it signals by closing stdout.
            stdout = proc.stdout
            assert stdout is not None

            with stdout:
                content = stdout.read()
                self.assertEqual(
                    content.split(b'\n')[-2:],
                    [b'SRT_LOGGER_READY=1', b''],
                )

            proc.wait()

            # Take an exclusive lock on the log file to give the logger
            # time to exit too
            proc = subprocess.Popen(
                self.test_adverb + [
                    '--exclusive-lock', str(Path(tmpdir, 'cat.txt')),
                ],
                stdout=STDERR_FILENO,
                stderr=STDERR_FILENO,
            )
            proc.wait()

            with open(str(Path(tmpdir, 'cat.txt')), 'rb') as reader:
                content = reader.read()
                self.assertIn(b'hello, world\n', content)

    def test_default_filename_from_identifier(self) -> None:
        '''
        Exercise default filename from --identifier, and also
        default log directory from Steam
        '''
        with tempfile.TemporaryDirectory() as tmpdir:
            Path(
                tmpdir, '.local', 'share', 'Steam', 'my-logs'
            ).mkdir(parents=True)
            Path(tmpdir, '.steam').mkdir(parents=True)
            Path(tmpdir, '.steam', 'steam').symlink_to(
                '../.local/share/Steam'
            )

            proc = subprocess.Popen(
                [
                    'env',
                    'HOME=' + tmpdir,
                    'STEAM_CLIENT_LOG_FOLDER=my-logs',
                ] + self.logger + [
                    '--identifier=foo',
                    '--no-auto-terminal',
                ],
                stdin=subprocess.PIPE,
                stdout=STDERR_FILENO,
                stderr=STDERR_FILENO,
            )

            stdin = proc.stdin
            assert stdin is not None

            with stdin:
                stdin.write(b'hello, world\n')
                stdin.flush()

            proc.wait()

            with open(
                str(
                    Path(
                        tmpdir, '.local', 'share', 'Steam', 'my-logs',
                        'foo.txt',
                    ),
                ),
                'rb'
            ) as reader:
                content = reader.read()
                self.assertIn(b'hello, world\n', content)

    def test_exec_fallback_to_cat(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            proc = subprocess.Popen(
                self.logger + [
                    '--exec-fallback',
                    '--filename', 'filename',
                    '--log-directory', str(Path(tmpdir, 'nonexistent')),
                    '--sh-syntax',
                ],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

            stdin = proc.stdin
            assert stdin is not None

            with stdin:
                stdin.write(b'hello, world\n')
                stdin.flush()

            stdout = proc.stdout
            assert stdout is not None

            with stdout:
                content = stdout.read()
                # We failed to start logging, so we do not receive
                # "SRT_LOGGER_READY=1\n"
                self.assertEqual(b'', content)

            stderr = proc.stderr
            assert stderr is not None

            with stderr:
                content = stderr.read()
                logger.info('%s', content.decode('utf-8', 'replace'))
                self.assertIn(b'hello, world\n', content)

            proc.wait()

    def test_journal_fd(self) -> None:
        proc = subprocess.Popen(
            self.logger + ['--filename=', '--journal-fd=2'],
            stdin=subprocess.PIPE,
            stdout=STDERR_FILENO,
            stderr=subprocess.PIPE,
        )

        stdin = proc.stdin
        assert stdin is not None

        with stdin:
            stdin.write(b'Hello, world')
            stdin.flush()

        stderr = proc.stderr
        assert stderr is not None

        proc.wait()

        with stderr:
            content = stderr.read()
            logger.info('%s', content.decode('utf-8', 'replace'))
            self.assertIn(b'Hello, world', content)

    def test_not_buffered(self) -> None:
        '''\
        Messages to stderr or the terminal are not line-buffered.
        '''
        for args in (
            ['--no-auto-terminal'],
            # We use STDERR_FILENO as a mock terminal here.
            ['--no-auto-terminal', '--terminal-fd=%d' % STDERR_FILENO],
            [],
        ):
            proc = subprocess.Popen(
                self.logger + ['--filename='] + args,
                env={**os.environ, 'NO_COLOR': '1'},
                stdin=subprocess.PIPE,
                stdout=STDERR_FILENO,
                stderr=subprocess.PIPE,
            )

            stdin = proc.stdin
            assert stdin is not None

            stdin.write(b'Prompt> ')
            stdin.flush()

            stderr = proc.stderr
            assert stderr is not None

            content = b''

            while True:
                content += stderr.read(1)
                logger.info('%s', content.decode('utf-8', 'replace'))

                if b'Prompt> ' in content:
                    break

            with stdin:
                stdin.write(b'exit\r\n')
                stdin.flush()

            proc.wait()

            with stderr:
                content += stderr.read()
                logger.info('%s', content.decode('utf-8', 'replace'))
                self.assertIn(b'Prompt> ', content)
                self.assertIn(b'exit\r\n', content)

    def test_process_substitution(
        self,
        make_it_fail=False,
        script=[],
    ) -> None:
        '''\
        The logger can be attached to a shell script via process substitution.
        '''

        if not script:
            script = self.logger_procsubst

        with tempfile.TemporaryDirectory() as tmpdir:
            with open(str(Path(tmpdir, 'log.txt')), 'w'):
                pass

            if make_it_fail:
                args = ['-d', '/nonexistent']
            else:
                args = ['-d', tmpdir]

            # We use STDERR_FILENO as a mock terminal here.
            args.append('--filename=log.txt')
            args.append('--no-auto-terminal')
            args.append('--terminal-fd=%d' % STDERR_FILENO)
            args.append('--use-journal')

            proc = subprocess.Popen(
                script + args,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

            proc.wait()

            stdout = proc.stdout
            assert stdout is not None

            with stdout:
                content = stdout.read()

                logger.info('stdout: %s', content.decode('utf-8'))

                if make_it_fail:
                    self.assertEqual(content, b'emitted from stdout\n')

            stderr = proc.stderr
            assert stderr is not None

            with stderr:
                content = stderr.read()

                logger.info('stderr: %s', content.decode('utf-8'))

                if not make_it_fail:
                    self.assertIn(b'emitted from stdout', content)

                self.assertIn(b'EMITTED FROM STDERR', content)

            # Take an exclusive lock on the log file to give the logger
            # time to exit too
            proc = subprocess.Popen(
                self.test_adverb + [
                    '--exclusive-lock', str(Path(tmpdir, 'log.txt')),
                ],
                stdout=STDERR_FILENO,
                stderr=STDERR_FILENO,
            )
            proc.wait()

            if not make_it_fail:
                with open(str(Path(tmpdir, 'log.txt')), 'rb') as reader:
                    content = reader.read()
                    self.assertIn(b'emitted from stdout\n', content)
                    self.assertIn(b'EMITTED FROM STDERR\n', content)

    def test_process_substitution_fail(self) -> None:
        self.test_process_substitution(make_it_fail=True)

    def test_process_substitution_0(self) -> None:
        self.test_process_substitution(script=self.logger_0)

    def test_process_substitution_0_fail(self) -> None:
        self.test_process_substitution(
            make_it_fail=True,
            script=self.logger_0,
        )

    def test_sh_syntax(self) -> None:
        self.test_concurrent_logging(use_sh_syntax=True)

    def test_rotation(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            proc = subprocess.Popen(
                self.logger + [
                    '--filename=log.txt',
                    '--log-directory', tmpdir,
                    '--rotate=1K',
                    '--no-auto-terminal',
                ],
                stdin=subprocess.PIPE,
                stdout=STDERR_FILENO,
                stderr=STDERR_FILENO,
            )

            stdin = proc.stdin
            assert stdin is not None

            with stdin:
                stdin.write(b'first message\n')
                stdin.flush()

                for i in range(10):
                    stdin.write(b'.' * 110)
                    stdin.write(b'\n')
                    stdin.flush()

                stdin.write(b'middle message\n')
                stdin.flush()

                for i in range(10):
                    stdin.write(b'.' * 110)
                    stdin.write(b'\n')
                    stdin.flush()

                stdin.write(b'last message\n')
                stdin.flush()

            proc.wait()

            with open(str(Path(tmpdir, 'log.previous.txt')), 'rb') as reader:
                content = reader.read()
                self.assertIn(b'middle message\n', content)

            with open(str(Path(tmpdir, 'log.txt')), 'rb') as reader:
                content = reader.read()
                self.assertIn(b'last message\n', content)

    def test_rotation_no_suffix(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            proc = subprocess.Popen(
                self.logger + [
                    '--filename=log',
                    '--log-directory', tmpdir,
                    '--rotate=1K',
                    '--no-auto-terminal',
                ],
                stdin=subprocess.PIPE,
                stdout=STDERR_FILENO,
                stderr=STDERR_FILENO,
            )

            stdin = proc.stdin
            assert stdin is not None

            with stdin:
                stdin.write(b'first message\n')
                stdin.flush()

                for i in range(10):
                    stdin.write(b'.' * 110)
                    stdin.write(b'\n')
                    stdin.flush()

                stdin.write(b'middle message\n')
                stdin.flush()

                for i in range(10):
                    stdin.write(b'.' * 110)
                    stdin.write(b'\n')
                    stdin.flush()

                stdin.write(b'last message\n')
                stdin.flush()

            proc.wait()

            with open(str(Path(tmpdir, 'log.previous')), 'rb') as reader:
                content = reader.read()
                self.assertIn(b'middle message\n', content)

            with open(str(Path(tmpdir, 'log')), 'rb') as reader:
                content = reader.read()
                self.assertIn(b'last message\n', content)

    def test_stderr_only(self) -> None:
        proc = subprocess.Popen(
            self.logger + [
                '--filename=',
            ],
            stdin=subprocess.PIPE,
            stdout=STDERR_FILENO,
            stderr=subprocess.PIPE,
        )

        stdin = proc.stdin
        assert stdin is not None

        with stdin:
            stdin.write(b'hello, world\n')
            stdin.flush()

        stderr = proc.stderr
        assert stderr is not None

        proc.wait()

        with stderr:
            content = stderr.read()
            logger.info('%s', content.decode('utf-8', 'replace'))
            self.assertIn(b'hello, world\n', content)

    def test_stderr_only_cat(self) -> None:
        proc = subprocess.Popen(
            self.logger + [
                '--filename=',
                '--',
                'cat',
            ],
            stdin=subprocess.PIPE,
            stdout=STDERR_FILENO,
            stderr=subprocess.PIPE,
        )

        stdin = proc.stdin
        assert stdin is not None

        with stdin:
            stdin.write(b'hello, world\n')
            stdin.flush()

        stderr = proc.stderr
        assert stderr is not None

        proc.wait()

        with stderr:
            content = stderr.read()
            logger.info('%s', content.decode('utf-8', 'replace'))
            self.assertIn(b'hello, world\n', content)

    def test_supervisor(self, terminate=False) -> None:
        '''\
        srt-logger can be used to log the output of s-r-supervisor.

        steamrt/tasks#460
        '''

        if terminate:
            # When sh(1) exits, the supervisor will send SIGTERM to any
            # other subprocesses (in this case sleep 300) immediately,
            # then after waiting 1 second it will escalate to SIGKILL.
            supervisor_args = [
                '--terminate-timeout=1',
            ]
            # The main process leaks a child process so that there is
            # something for the supervisor to clean up.
            child_shell_script = (
                'sleep 300 & echo "Main process <<$$>> exiting"'
            )
            # Logger has to put itself in the background so that it will
            # not be a child of the supervisor, which would cause the
            # supervisor to send SIGTERM to the logger, resulting in the
            # loss of any messages that are still buffered in memory, and
            # then SIGPIPE or EPIPE if the supervisor or any surviving
            # supervised process tries to send additional log messages.
        else:
            # The supervisor will not terminate child processes, but will
            # just wait passively for them to exit.
            supervisor_args = []
            child_shell_script = 'echo "Main process <<$$>> exiting"'
            # Logger has to put itself in the background so that it will
            # not be a child of the supervisor, which would cause the
            # supervisor to wait for the logger to exit; but the logger
            # cannot exit until the supervisor has finished sending its
            # diagnostic messages to the supervisor, which is a deadlock.

        start_time = time.time()
        proc = subprocess.Popen(
            self.logger + [
                '--background',
                '--filename=',
                '--',
            ] + self.supervisor + [
                '--subreaper',
            ] + supervisor_args + [
                '--',
                'sh',
                '-c', child_shell_script,
            ],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        stdout = proc.stdout
        assert stdout is not None

        with stdout:
            content = stdout.read()
            logger.info('%s', content.decode('utf-8', 'replace'))
            self.assertEqual(content, b'')

        stderr = proc.stderr
        assert stderr is not None

        proc.wait()

        with stderr:
            content = stderr.read()
            logger.info('%s', content.decode('utf-8', 'replace'))
            self.assertIn(b'Main process <<', content)
            self.assertIn(b'>> exiting\n', content)

        end_time = time.time()
        # The whole test should take less than a second, but allow longer
        # to accommodate slow/heavily-loaded test systems.
        # It should certainly not take as long as the 5 minutes that it
        # will take for the sleep(1) process to exit without being killed.
        self.assertLess(end_time - start_time, 30)

    def test_supervisor_terminate(self) -> None:
        '''\
        srt-logger and s-r-supervisor --terminate-timeout can be combined.

        steamrt/tasks#460
        '''
        self.test_supervisor(terminate=True)

    def test_terminal(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            # Use a flat file as a mockup of a terminal to make it easier
            # to read output
            fake_tty = Path(tmpdir, 'fake-tty')

            with open(str(fake_tty), 'w'):
                pass

            proc = subprocess.Popen(
                [
                    'env',
                    'SRT_LOG_TERMINAL=' + str(fake_tty),
                ] + self.logger + [
                    '--filename=',
                    '--sh-syntax',
                    '--',
                    'cat',
                ],
                env={**os.environ, 'NO_COLOR': '1'},
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

            stdin = proc.stdin
            assert stdin is not None

            with stdin:
                stdin.write(b'hello, world\n')
                stdin.flush()

            # Wait for the logger to be running, which it signals
            # by closing standard output.
            stdout = proc.stdout
            assert stdout is not None

            with stdout:
                content = stdout.read()
                self.assertEqual(
                    content.split(b'\n')[-2:],
                    [b'SRT_LOGGER_READY=1', b''],
                )

            stderr = proc.stderr
            assert stderr is not None

            proc.wait()

            # Because we are not outputting to a file or the Journal,
            # output continues to go to stderr...
            with stderr:
                content = stderr.read()
                logger.info('%s', content.decode('utf-8', 'replace'))
                self.assertIn(b'hello, world\n', content)

            # ... but it also goes to the (fake) terminal
            with open(str(fake_tty)) as reader:
                self.assertIn('hello, world\n', reader.read())

            proc = subprocess.Popen(
                [
                    'env',
                    'SRT_LOG_TERMINAL=' + str(fake_tty),
                ] + self.logger + [
                    '--filename=log',
                    '--identifier', 'test-srt-logger',
                    '--log-directory', tmpdir,
                    '--sh-syntax',
                    '--use-journal',
                ],
                env={**os.environ, 'NO_COLOR': '1'},
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

            stdin = proc.stdin
            assert stdin is not None

            with stdin:
                stdin.write(b'hello, again\n')
                stdin.flush()

            # Wait for the logger to be running, which it signals
            # by closing standard output.
            stdout = proc.stdout
            assert stdout is not None

            with stdout:
                content = stdout.read()
                self.assertEqual(
                    content.split(b'\n')[-2:],
                    [b'SRT_LOGGER_READY=1', b''],
                )

            stderr = proc.stderr
            assert stderr is not None

            proc.wait()

            # Because we are writing to a log file, the message is not
            # copied to stderr this time
            with stderr:
                content = stderr.read()
                logger.info('%s', content.decode('utf-8', 'replace'))
                self.assertNotIn(b'hello, again\n', content)

            # The content from the second run was appended
            with open(str(fake_tty)) as reader:
                content = reader.read()
                self.assertIn('hello, world\n', content)
                self.assertIn('hello, again\n', content)

    def test_terminal_nested(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            fake_tty = Path(tmpdir, 'fake-tty')

            with open(str(fake_tty), 'w'):
                pass

            proc = subprocess.Popen(
                [
                    'env',
                    'SRT_LOG_TERMINAL=' + str(fake_tty),
                ] + self.logger + [
                    '--filename=one',
                    '--log-directory', tmpdir,
                    '--',
                ] + self.logger + [
                    '--filename=two',
                    '--log-directory', tmpdir,
                    '--',
                ] + self.logger + [
                    '--filename=three',
                    '--log-directory', tmpdir,
                ],
                env={**os.environ, 'NO_COLOR': '1'},
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

            stdin = proc.stdin
            assert stdin is not None

            with stdin:
                stdin.write(b'hello, world\n')
                stdin.flush()

            # Wait for the logger to be running, which it signals
            # by closing standard output.
            stdout = proc.stdout
            assert stdout is not None

            with stdout:
                self.assertEqual(stdout.read(), b'')

            stderr = proc.stderr
            assert stderr is not None

            proc.wait()

            with stderr:
                content = stderr.read()
                logger.info('stderr: %s', content.decode('utf-8', 'replace'))

            with open(str(Path(tmpdir, 'one'))) as reader:
                content = reader.read()
                logger.info('log 1: %s', content)
                self.assertNotIn('hello, world\n', content)

            with open(str(Path(tmpdir, 'two'))) as reader:
                content = reader.read()
                logger.info('log 2: %s', content)
                self.assertNotIn('hello, world\n', content)

            with open(str(Path(tmpdir, 'three'))) as reader:
                content = reader.read()
                logger.info('log 3: %s', content)
                self.assertIn('hello, world\n', content)

            with open(str(fake_tty)) as reader:
                content = reader.read()
                logger.info('tty output: %s', content)
                self.assertIn('hello, world\n', content)

    def test_level_prefixes(self) -> None:
        LEVEL_LINES = [
                'emergency message\n',
                'alert message\n',
                'crit message\n',
                'err message\n',
                'warning message\n',
                'notice message\n',
                'info message\n',
                'debug message\n',
        ]

        UNPREFIXED_LINE = 'default level message\n'

        INVALID_LINES = [
            '<invalid prefix message\n',
            '<5invalid prefix message\n',
        ]

        def prepend_level(level: int, line: str) -> str:
            return '<{}>{}'.format(level, line)

        with tempfile.TemporaryDirectory() as tmpdir:
            journal_path = Path(tmpdir, 'journal.txt')
            terminal_path = Path(tmpdir, 'terminal.txt')
            with journal_path.open('w') as journal, \
                    terminal_path.open('w') as terminal:
                proc = subprocess.Popen(
                    self.logger + [
                        '--parse-level-prefix',
                        '--default-level=notice',
                        '--file-level=debug',
                        '--journal-level=info',
                        '--terminal-level=err',
                        '--filename=log.txt',
                        '--log-directory', tmpdir,
                        '--journal-fd={}'.format(journal.fileno()),
                        '--terminal-fd={}'.format(terminal.fileno()),
                    ],
                    env={**os.environ, 'NO_COLOR': '1'},
                    pass_fds=(journal.fileno(), terminal.fileno(),),
                    stdin=subprocess.PIPE,
                )

            with wrap_process_io(proc.stdin) as stdin:
                for i, line in enumerate(LEVEL_LINES):
                    stdin.write(prepend_level(i, line))
                for line in INVALID_LINES:
                    stdin.write(line)
                stdin.write(UNPREFIXED_LINE)

            self.assertEqual(proc.wait(), 0)

            contents = Path(tmpdir, 'log.txt').read_text()
            for i, line in enumerate(LEVEL_LINES):
                self.assertIn(line, contents)
                self.assertNotIn(prepend_level(i, line), contents)
            self.assertIn(UNPREFIXED_LINE, contents)

            contents = journal_path.read_text()
            for i, line in enumerate(LEVEL_LINES[:7]):
                self.assertIn(prepend_level(i, line), contents)
            for line in LEVEL_LINES[7:]:
                self.assertNotIn(line, contents)
            for line in (*INVALID_LINES, UNPREFIXED_LINE):
                self.assertIn(prepend_level(5, line), contents)

            contents = terminal_path.read_text()
            for line in LEVEL_LINES[:4]:
                self.assertIn(line, contents)
            for line in (*LEVEL_LINES[4:], *INVALID_LINES, UNPREFIXED_LINE):
                self.assertNotIn(line, contents)

    def test_remaining_lines_level(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            journal_path = Path(tmpdir, 'journal.txt')

            with journal_path.open('w') as journal:
                proc = subprocess.Popen(
                    self.logger + [
                        '--parse-level-prefix',
                        '--default-level=notice',
                        '--journal-level=debug',
                        '--journal-fd={}'.format(journal.fileno()),
                    ],
                    pass_fds=(journal.fileno(),),
                    stdin=subprocess.PIPE,
                )

            with wrap_process_io(proc.stdin) as stdin:
                stdin.write('<6>foo\n')
                stdin.write('<remaining-lines-assume-level=5>\n')
                stdin.write('<2>bar\n')

            self.assertEqual(proc.wait(), 0)

            contents = journal_path.read_text()
            self.assertIn('<6>foo\n', contents)
            self.assertIn('<5><2>bar\n', contents)

    def test_level_prefix_buffering(self) -> None:
        # Use a pipe here instead of stderr, so it'll be easier to debug
        # srt-logger via printf.
        terminal_rd, terminal_wr = os.pipe()

        with tempfile.TemporaryDirectory() as tmpdir:
            journal_path = Path(tmpdir, 'journal.txt')
            with journal_path.open('w+') as journal, \
                    os.fdopen(terminal_wr, 'wb') as terminal:
                proc = subprocess.Popen(
                    self.logger + [
                        '--parse-level-prefix',
                        '--default-level=I',
                        '--journal-level=7',
                        '--terminal-level=notice',
                        '--journal-fd={}'.format(journal.fileno()),
                        '--terminal-fd={}'.format(terminal.fileno()),
                    ],
                    bufsize=0,
                    env={**os.environ, 'NO_COLOR': '1'},
                    pass_fds=(journal.fileno(), terminal.fileno()),
                    stdin=subprocess.PIPE,
                )

            assert proc.stdin is not None
            with proc.stdin as stdin, \
                    os.fdopen(terminal_rd, 'rb', buffering=0) as terminal:
                for b in b'<4>w':
                    stdin.write(bytes([b]))
                    time.sleep(0.05)
                stdin.write(b'arning')

                buffer = b''
                while b'warning' not in buffer:
                    buffer += terminal.read(10)
                    logging.info('buffer=%s', buffer)

                stdin.write(b'\n')
                buffer = b''
                while b'\n' not in buffer:
                    buffer += terminal.read(10)
                    logging.info('buffer=%s', buffer)

                stdin.write(b'unprefixed notice\n')
                stdin.write(b'<7>journal only debug\n')

                stdin.write(b'<remaining-lines-')
                stdin.write(b'assume-level=5>\n')

                stdin.write(b'<7>actually a notice\n')
                stdin.close()

                rest = terminal.read()
                self.assertIn(b'<7>actually a notice', rest)
                self.assertNotIn(b'unprefixed notice', rest)
                self.assertNotIn(b'journal only debug', rest)

            self.assertEqual(proc.wait(), 0)

            contents = journal_path.read_text()
            self.assertIn('<4>warning\n', contents)
            self.assertIn('<6>unprefixed notice\n', contents)
            self.assertIn('<7>journal only debug\n', contents)
            self.assertIn('<5><7>actually a notice\n', contents)


    def test_level_colors(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            journal_path = Path(tmpdir, 'journal.txt')
            terminal_path = Path(tmpdir, 'terminal.txt')
            with journal_path.open('w') as journal, \
                    terminal_path.open('w') as terminal:
                proc = subprocess.Popen(
                    self.logger + [
                        '--parse-level-prefix',
                        '--terminal-level=notice',
                        '--filename=log.txt',
                        '--log-directory', tmpdir,
                        '--journal-fd={}'.format(journal.fileno()),
                        '--terminal-fd={}'.format(terminal.fileno()),
                    ],
                    env={**os.environ, 'NO_COLOR': ''},
                    pass_fds=(journal.fileno(), terminal.fileno(),),
                    stdin=subprocess.PIPE,
                )

            with wrap_process_io(proc.stdin) as stdin:
                stdin.write('<0>a message\n')

            self.assertEqual(proc.wait(), 0)

            contents = terminal_path.read_text()
            # Ensure that the line has some color applied to it that is reset
            # afterwards.
            self.assertRegex(contents, '\033\\[[^0].*a message.*\033\\[0m')

            contents = journal_path.read_text()
            self.assertNotIn('\033', contents)

            contents = Path(tmpdir, 'log.txt').read_text()
            self.assertNotIn('\033', contents)

    def test_wrong_options(self) -> None:
        for option in (
            '--an-unknown-option',
            '--filename=not/allowed',
            '--filename=.not-allowed',
            '--default-level=invalid',
        ):
            proc = subprocess.Popen(
                self.logger + [
                    option,
                    '--',
                    'true',
                ],
                stdout=STDERR_FILENO,
                stderr=STDERR_FILENO,
            )
            proc.wait()
            self.assertEqual(proc.returncode, LAUNCH_EX_FAILED)

    def tearDown(self) -> None:
        super().tearDown()


if __name__ == '__main__':
    assert sys.version_info >= (3, 5), \
        'Python 3.5+ is required'

    test_main()

# vi: set sw=4 sts=4 et:
