#!/usr/bin/env python3
# Copyright 2020-2024 Collabora Ltd.
# SPDX-License-Identifier: MIT

import logging
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path[:0] = [str(Path(__file__).resolve().parent.parent)]

from testutils import (
    BaseTest,
    test_main,
)


logger = logging.getLogger('test-dialog-ui')


STDERR_FILENO = 2


class TestDialogUi(BaseTest):
    def setUp(self) -> None:
        super().setUp()

        self.dialog_ui = self.command_prefix + [
            'env',
            '-u', 'LD_AUDIT',
            '-u', 'LD_PRELOAD',
            os.path.join(
                self.top_builddir,
                'bin',
                'steam-runtime-dialog-ui',
            ),
        ]

    def test_error(self) -> None:
        proc = subprocess.Popen(
            self.dialog_ui + [
                '--error',
                '--text=Press any key or mouse or gamepad button',
                '--title=Mockup of an error',
            ],
            stdout=STDERR_FILENO,
            stderr=STDERR_FILENO,
        )
        proc.wait()
        self.assertEqual(proc.returncode, 0)

    def test_info(self) -> None:
        proc = subprocess.Popen(
            self.dialog_ui + [
                '--info',
                '--text=Press any key or mouse or gamepad button',
            ],
            stdout=STDERR_FILENO,
            stderr=STDERR_FILENO,
        )
        proc.wait()
        self.assertEqual(proc.returncode, 0)

    def test_warning(self) -> None:
        proc = subprocess.Popen(
            self.dialog_ui + [
                '--warning',
                '--text=Press any key or mouse or gamepad button',
            ],
            stdout=STDERR_FILENO,
            stderr=STDERR_FILENO,
        )
        proc.wait()
        self.assertEqual(proc.returncode, 0)

    def test_progress(self) -> None:
        proc = subprocess.Popen(
            self.dialog_ui + [
                '--auto-close',
                '--progress',
                '--no-cancel',
                '--text=Reticulating splines...',
                '--title=Please wait',
            ],
            stdin=subprocess.PIPE,
            stdout=STDERR_FILENO,
            stderr=STDERR_FILENO,
        )

        stdin = proc.stdin
        assert stdin is not None

        with stdin:
            time.sleep(1)
            stdin.write(b'33.3\n')
            stdin.flush()
            time.sleep(1)
            stdin.write(b'#Answering questions about the universe...\n')
            stdin.write(b'42\n')
            stdin.flush()
            time.sleep(1)
            stdin.write(b'#Not sure how long this is going to take...\n')
            stdin.write(b'pulsate:true\n')
            stdin.flush()
            time.sleep(1)
            stdin.write(b'#Waiting for Godot...\n')
            stdin.write(b'pulsate:false\n')
            stdin.flush()
            time.sleep(1)
            stdin.write(b'80\n')
            stdin.flush()
            time.sleep(1)

        proc.wait()
        self.assertEqual(proc.returncode, 0)

    def test_progress_cancellable(self) -> None:
        proc = subprocess.Popen(
            [
                'env',
                'STEAM_RUNTIME_DIALOG_FULLSCREEN=1',
            ] + self.dialog_ui + [
                '--progress',
                '--pulsate',
                '--text=You can press a key or button to exit early',
                '--title=Please wait',
            ],
            stdin=subprocess.PIPE,
            stdout=STDERR_FILENO,
            stderr=STDERR_FILENO,
        )

        stdin = proc.stdin
        assert stdin is not None

        try:
            with stdin:
                time.sleep(5)
                stdin.write(b'pulsate:false\n')
                stdin.write(b'100\n')
                stdin.write(
                    b'#Press any key or mouse or gamepad button to exit\n'
                )
                stdin.flush()
        except BrokenPipeError:
            pass

        proc.wait()
        self.assertEqual(proc.returncode, 0)

    def tearDown(self) -> None:
        super().tearDown()


if __name__ == '__main__':
    assert sys.version_info >= (3, 5), \
        'Python 3.5+ is required'

    if 'SRT_TEST_TOP_BUILDDIR' not in os.environ:
        raise SystemExit(
            'Usage: SRT_TEST_TOP_BUILDDIR=_build/host '
            './tests/manual/dialog-ui.py'
        )

    test_main()

# vi: set sw=4 sts=4 et:
