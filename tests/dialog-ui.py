#!/usr/bin/env python3
# Copyright 2020-2024 Collabora Ltd.
# SPDX-License-Identifier: MIT

import logging
import os
import subprocess
import sys

try:
    import typing
    typing      # noqa
except ImportError:
    pass

from testutils import (
    BaseTest,
    run_subprocess,
    test_main,
)


logger = logging.getLogger('test-dialog-ui')


STDERR_FILENO = 2


class TestDialogUi(BaseTest):
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

        if 'SRT_TEST_UNINSTALLED' in os.environ:
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
        else:
            self.skipTest('Not available as an installed-test')

    def test_check_features(self) -> None:
        for features, good in (
            ('', True),
            (' ', True),
            ('message', True),
            ('message progress', True),
            ('message message\tmessage\nmessage', True),
            ('bees', False),
            ('message progress bees', False),
        ):
            proc = subprocess.Popen(
                self.dialog_ui + [
                    '--check-features', features,
                ],
                stdout=STDERR_FILENO,
                stderr=STDERR_FILENO,
            )
            proc.wait()

            if good:
                self.assertEqual(proc.returncode, 0)
            else:
                self.assertEqual(proc.returncode, 255)

    def tearDown(self) -> None:
        super().tearDown()


if __name__ == '__main__':
    assert sys.version_info >= (3, 4), \
        'Python 3.4+ is required'

    test_main()

# vi: set sw=4 sts=4 et:
