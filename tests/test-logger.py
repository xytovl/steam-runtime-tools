#!/usr/bin/env python3
# Copyright 2020-2024 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

import logging
import os
import subprocess
import sys
import tempfile
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
        else:
            self.skipTest('Not available as an installed-test')

    def test_concurrent_logging(self) -> None:
        '''
        If there is more than one writer for the same log file, rotation
        is disabled to avoid data loss
        '''
        with tempfile.TemporaryDirectory() as tmpdir:
            with open(str(Path(tmpdir, 'cat.txt')), 'w'):
                pass

            lock_holder = subprocess.Popen(
                self.logger + [
                    '--filename=log.txt',
                    '--log-directory', tmpdir,
                ],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=STDERR_FILENO,
            )

            proc = subprocess.Popen(
                self.logger + [
                    '--filename=log.txt',
                    '--log-directory', tmpdir,
                    '--rotate=50',
                ],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=STDERR_FILENO,
            )

            # Wait for the first logger to be running, which it signals
            # by closing standard output.
            stdout = lock_holder.stdout
            assert stdout is not None

            with stdout:
                stdout.read()

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
                self.assertEqual(stdout.read(), b'')

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
                self.assertEqual(b'', content)

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
                self.assertEqual(b'', content)

            stderr = proc.stderr
            assert stderr is not None

            with stderr:
                content = stderr.read()
                logger.info('%s', content.decode('utf-8', 'replace'))
                self.assertIn(b'hello, world\n', content)

            proc.wait()

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
                    '--',
                    'cat',
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

            # Wait for the logger to be running, which it signals
            # by closing standard output.
            stdout = proc.stdout
            assert stdout is not None

            with stdout:
                stdout.read()

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
                    '--use-journal',
                ],
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
                stdout.read()

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
                stdout.read()

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

    def test_wrong_options(self) -> None:
        for option in (
            '--an-unknown-option',
            '--filename=not/allowed',
            '--filename=.not-allowed',
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
