#!/usr/bin/env python3
# Copyright 2020-2024 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

import logging
import os
import shlex
import signal
import subprocess
import sys
import tempfile


try:
    import typing
    typing      # placate pyflakes
except ImportError:
    pass

from testutils import (
    BaseTest,
    run_subprocess,
    test_main,
)


logger = logging.getLogger('test-supervisor')


LAUNCH_EX_FAILED = 125
LAUNCH_EX_USAGE = 125
LAUNCH_EX_CANNOT_INVOKE = 126
LAUNCH_EX_NOT_FOUND = 127
STDIN_FILENO = 0
STDOUT_FILENO = 1
STDERR_FILENO = 2


class TestSupervisor(BaseTest):
    def run_subprocess(
        self,
        args,           # type: typing.Union[typing.List[str], str]
        check=False,
        input=None,     # type: typing.Optional[bytes]
        timeout=None,   # type: typing.Optional[int]
        **kwargs        # type: typing.Any
    ):
        logger.info('Running: %r', args)
        return run_subprocess(
            args, check=check, input=input, timeout=timeout, **kwargs
        )

    def setUp(self) -> None:
        super().setUp()
        self.uname = os.uname()

        if 'SRT_TEST_UNINSTALLED' in os.environ:
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

    def test_enoent(self) -> None:
        proc = subprocess.Popen(
            self.supervisor + ['--', '/nonexistent'],
            stdout=STDERR_FILENO,
            stderr=STDERR_FILENO,
        )
        proc.wait()
        self.assertEqual(proc.returncode, LAUNCH_EX_NOT_FOUND)

    def test_enoexec(self) -> None:
        proc = subprocess.Popen(
            self.supervisor + ['--', '/dev/null'],
            stdout=STDERR_FILENO,
            stderr=STDERR_FILENO,
        )
        proc.wait()
        self.assertEqual(proc.returncode, LAUNCH_EX_CANNOT_INVOKE)

    def test_env(self) -> None:
        '''
        Exercise --env, --unset-env, --inherit-env[-matching], --verbose
        '''
        proc = subprocess.Popen(
            [
                'env',
                '-u', 'FOO',
                'BAR=wrong',
                'UNSET=wrong',
                'INHERIT=inherit',
                'WILDCARD=wildcard',
            ] + self.supervisor + [
                '--env=FOO=',
                '--env=BAR=bar',
                '--env=INHERIT=wrong',
                '--env=WILDCARD=wrong',
                '--inherit-env=INHERIT',
                '--inherit-env-matching=WILD*',
                '--unset-env=UNSET',
                '--verbose',
                '--',
                'sh', '-euc',
                '''
                echo FOO="${FOO-unset}"
                echo BAR="${BAR-unset}"
                echo INHERIT="${INHERIT-unset}"
                echo WILDCARD="${WILDCARD-unset}"
                echo UNSET="${UNSET-unset}"
                ''',
            ],
            stdout=subprocess.PIPE,
            stderr=STDERR_FILENO,
        )

        try:
            stdout = proc.stdout
            assert stdout is not None

            with stdout:
                self.assertEqual(
                    stdout.read().decode('utf-8'),
                    'FOO=\n'
                    'BAR=bar\n'
                    'INHERIT=inherit\n'
                    'WILDCARD=wildcard\n'
                    'UNSET=unset\n'
                )
        finally:
            proc.wait()
            self.assertEqual(proc.returncode, 0)

    def test_env_fd(self) -> None:
        '''Exercise --clear-env, --env-fd, -vv'''
        proc = subprocess.Popen(
            [
                'env',
                'FOO=wrong',
                'BAR=wrong',
                'UNSET=wrong',
                'INHERIT=inherit',
                'WILDCARD=wildcard',
            ] + self.supervisor + [
                '--clear-env',
                '--env-fd=%d' % STDIN_FILENO,
                '--inherit-env=INHERIT',
                '--inherit-env-matching=WILD*',
                '-v',
                '-v',
                '--',
                '/bin/sh', '-euc',
                '''
                echo FOO="${FOO-unset}"
                echo BAR="${BAR-unset}"
                echo INHERIT="${INHERIT-unset}"
                echo WILDCARD="${WILDCARD-unset}"
                echo UNSET="${UNSET-unset}"
                ''',
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=STDERR_FILENO,
        )

        try:
            stdin = proc.stdin
            assert stdin is not None

            with stdin:
                stdin.write(b'FOO=\0')
                stdin.write(b'BAR=bar\0')

            stdout = proc.stdout
            assert stdout is not None

            with stdout:
                self.assertEqual(
                    stdout.read().decode('utf-8'),
                    'FOO=\n'
                    'BAR=bar\n'
                    # --clear-env makes --inherit-env effectively equivalent
                    # to --unset-env, as documented
                    'INHERIT=unset\n'
                    'WILDCARD=unset\n'
                    'UNSET=unset\n'
                )
        finally:
            proc.wait()
            self.assertEqual(proc.returncode, 0)

    def test_fd_assign(self) -> None:
        for target in (STDOUT_FILENO, 9):
            read_end, write_end = os.pipe2(os.O_CLOEXEC)

            proc = subprocess.Popen(
                self.supervisor + [
                    '--assign-fd=%d=%d' % (target, write_end),
                    '--',
                    'sh', '-euc', 'echo hello >&%d' % target,
                ],
                pass_fds=[write_end],
                stdout=STDERR_FILENO,
                stderr=STDERR_FILENO,
            )

            try:
                os.close(write_end)

                with os.fdopen(read_end, 'rb') as reader:
                    self.assertEqual(reader.read(), b'hello\n')
            finally:
                proc.wait()
                self.assertEqual(proc.returncode, 0)

    def test_fd_hold(self) -> None:
        lock_fd, lock_name = tempfile.mkstemp()

        proc = subprocess.Popen(
            self.supervisor + [
                '--lock-fd=%d' % lock_fd,
                '--',
                'sh', '-euc',
                # The echo is to assert that the --lock-fd is not inherited
                # by the process being supervised
                'echo this will fail >&%d || true; cat' % lock_fd,
            ],
            pass_fds=[lock_fd],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=STDERR_FILENO,
        )

        try:
            os.close(lock_fd)

            # Until we let it terminate by closing stdin, the supervisor
            # process is still holding the lock open
            path = os.readlink('/proc/%d/fd/%d' % (proc.pid, lock_fd))
            self.assertEqual(
                os.path.realpath(path),
                os.path.realpath(lock_name),
            )

            stdin = proc.stdin
            assert stdin is not None
            stdin.write(b'from stdin')
            stdin.close()

            stdout = proc.stdout
            assert stdout is not None
            self.assertEqual(stdout.read(), b'from stdin')
            stdout.close()
        finally:
            proc.wait()
            self.assertEqual(proc.returncode, 0)

        with open(lock_name, 'rb') as reader:
            # The "echo hello" didn't write to the lock file
            self.assertEqual(reader.read(), b'')

        os.unlink(lock_name)

    def test_fd_passthrough_explicit(self) -> None:
        read_end, write_end = os.pipe2(os.O_CLOEXEC)
        read_end2, write_end2 = os.pipe2(os.O_CLOEXEC)

        proc = subprocess.Popen(
            self.supervisor + [
                '--close-fds',
                '--pass-fd=%d' % write_end,
                '--',
                'sh', '-euc',
                'echo cannot >&%d || true; echo hello >&%d' % (
                    write_end2, write_end,
                ),
            ],
            pass_fds=[write_end, write_end2],
            stdout=STDERR_FILENO,
            stderr=STDERR_FILENO,
        )

        try:
            os.close(write_end)
            os.close(write_end2)

            with os.fdopen(read_end, 'rb') as reader:
                self.assertEqual(reader.read(), b'hello\n')

            # write_end2 was not passed through the supervisor to the
            # shell, so 'cannot' was not written successfully
            with os.fdopen(read_end2, 'rb') as reader:
                self.assertEqual(reader.read(), b'')
        finally:
            proc.wait()
            self.assertEqual(proc.returncode, 0)

    def test_fd_passthrough_implicit(self) -> None:
        read_end, write_end = os.pipe2(os.O_CLOEXEC)

        proc = subprocess.Popen(
            self.supervisor + [
                '--',
                'sh', '-euc',
                'echo hello >&%d' % write_end,
            ],
            pass_fds=[write_end],
            stdout=STDERR_FILENO,
            stderr=STDERR_FILENO,
        )

        try:
            os.close(write_end)

            with os.fdopen(read_end, 'rb') as reader:
                self.assertEqual(reader.read(), b'hello\n')
        finally:
            proc.wait()
            self.assertEqual(proc.returncode, 0)

    def test_lock_file(self, verbose=False) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            read_end, write_end = os.pipe2(os.O_CLOEXEC)

            proc = subprocess.Popen(
                self.supervisor + [
                    '--lock-create',
                    '--lock-exclusive',
                    '--lock-file=%s/.ref' % tmpdir,
                    '--',
                    'sh', '-euc',
                    'exec >/dev/null; echo hello > %s/.ref' % (
                        shlex.quote(tmpdir),
                    ),
                ],
                stdin=read_end,
                stdout=subprocess.PIPE,
                stderr=STDERR_FILENO,
            )

            try:
                os.close(read_end)

                # By the time sh runs, pv-adverb should have taken the lock.
                # The sh process signals that it is running by closing stdout.
                stdout = proc.stdout
                assert stdout is not None
                self.assertEqual(stdout.read(), b'')
                stdout.close()

                extra_options = []

                if verbose:
                    extra_options.append('--lock-verbose')

                proc2 = subprocess.Popen(
                    self.supervisor + extra_options + [
                        '--lock-create',
                        '--lock-wait',
                        '--lock-exclusive',
                        '--lock-file=%s/.ref' % tmpdir,
                        '--',
                        'sh', '-euc',
                        'cat %s/.ref' % shlex.quote(tmpdir),
                    ],
                    stdout=subprocess.PIPE,
                    stderr=STDERR_FILENO,
                )

                # proc2 doesn't exit until proc releases the lock,
                # by which time .ref contains "hello\n"

                os.close(write_end)
                proc2.wait()
                self.assertEqual(proc2.returncode, 0)
                stdout = proc2.stdout
                assert stdout is not None
                self.assertEqual(stdout.read(), b'hello\n')
                stdout.close()
            finally:
                proc.wait()
                self.assertEqual(proc.returncode, 0)

    def test_lock_file_verbose(self) -> None:
        self.test_lock_file(verbose=True)

    def test_stdio_passthrough(self) -> None:
        proc = subprocess.Popen(
            self.supervisor + [
                '--',
                'sh', '-euc',
                '''
                if [ "$(cat)" != "hello, world!" ]; then
                    exit 1
                fi

                echo $$
                exec >/dev/null
                exec sleep infinity
                ''',
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=2,
        )
        pid = 0

        try:
            stdin = proc.stdin
            assert stdin is not None
            stdin.write(b'hello, world!')
            stdin.close()

            stdout = proc.stdout
            assert stdout is not None
            pid = int(stdout.read().decode('ascii').strip())
            stdout.close()
        finally:
            if pid:
                os.kill(pid, signal.SIGTERM)
            else:
                proc.terminate()

            self.assertIn(
                proc.wait(),
                (128 + signal.SIGTERM, -signal.SIGTERM),
            )

    def test_wrong_options(self) -> None:
        for option in (
            '--an-unknown-option',
            '--env=FOO',
            '--env==bar',
            '--env-fd=-1',
            '--env-fd=23',
            '--lock-fd=-1',
            '--lock-fd=23',
            '--lock-fd=nope',
            '--pass-fd=-1',
            '--pass-fd=23',
            '--pass-fd=nope',
            '--assign-fd=-1',
            '--assign-fd=nope',
            '--assign-fd=2',
            '--assign-fd=2=-1',
            '--assign-fd=2=23',
        ):
            proc = subprocess.Popen(
                self.supervisor + [
                    option,
                    '--',
                    'sh', '-euc', 'exit 42',
                ],
                stdout=STDERR_FILENO,
                stderr=STDERR_FILENO,
            )
            proc.wait()
            self.assertEqual(LAUNCH_EX_USAGE, LAUNCH_EX_FAILED)
            self.assertEqual(proc.returncode, LAUNCH_EX_FAILED)

    def tearDown(self) -> None:
        super().tearDown()


if __name__ == '__main__':
    assert sys.version_info >= (3, 4), \
        'Python 3.4+ is required'

    test_main()

# vi: set sw=4 sts=4 et:
