#!/usr/bin/env python3
# Copyright 2020-2024 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

import logging
import os
import subprocess
import sys
import tempfile
import textwrap
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


logger = logging.getLogger('test-run-outside-ldlp')


LAUNCH_EX_FAILED = 125
LAUNCH_EX_USAGE = 125


class TestLogger(BaseTest):
    def setUp(self) -> None:
        super().setUp()

        if 'SRT_TEST_UNINSTALLED' in os.environ:
            self.run_outside_ldlp_path = os.path.join(
                self.top_builddir,
                'bin',
                'srt-run-outside-ldlp'
            )
            self.run_outside_ldlp = self.command_prefix + [
                'env',
                self.run_outside_ldlp_path,
            ]
        else:
            self.skipTest('Not available as an installed-test')

    def test_runs_command_outside_runtime(
        self,
        *,
        using_symlink = False
    ) -> None:
        BIN_NAME = 'test-bin'
        REAL_BIN_MESSAGE = 'inside real'

        with tempfile.TemporaryDirectory() as rt_dir, \
                tempfile.TemporaryDirectory() as skip_dir, \
                tempfile.TemporaryDirectory() as real_dir:
            if using_symlink:
                Path(rt_dir, BIN_NAME).symlink_to(self.run_outside_ldlp_path)

            # Make sure it skips the symlink to itself.
            Path(skip_dir, BIN_NAME).symlink_to(self.run_outside_ldlp_path)

            real_bin_path = Path(real_dir, BIN_NAME)
            real_bin_path.write_text(textwrap.dedent('''\
                #!/usr/bin/env sh
                if [ -n "$STEAM_RUNTIME" ]; then
                    echo "should not be set: STEAM_RUNTIME=$STEAM_RUNTIME" >&2
                    exit 1
                fi
                if [ -n "$LD_PRELOAD" ]; then
                    echo "should not be set: LD_PRELOAD=$LD_PRELOAD" >&2
                    exit 1
                fi
                echo "{}"
            '''.format(REAL_BIN_MESSAGE)))
            real_bin_path.chmod(0o755)

            process_env = {
                **os.environ,
                'PATH': ':'.join(
                    (rt_dir, skip_dir, real_dir, os.environ['PATH'])
                ),
                'LD_PRELOAD': os.path.join(rt_dir, 'does-not-exist'),
                'STEAM_RUNTIME': rt_dir,
            }

            if using_symlink:
                command = [*self.command_prefix, BIN_NAME]
            else:
                command = [*self.run_outside_ldlp, '-vv', BIN_NAME]

            result = subprocess.run(
                command,
                check=True,
                env=process_env,
                stdout=subprocess.PIPE,
                universal_newlines=True,
            )
            self.assertIn(REAL_BIN_MESSAGE, result.stdout)

    def test_runs_using_symlink(self) -> None:
        self.test_runs_command_outside_runtime(using_symlink=True)

    def test_avoids_searching_steam_runtime(self) -> None:
        BIN_NAME = 'test-bin'
        RT_BIN_MESSAGE = 'inside rt'
        REAL_BIN_MESSAGE = 'inside real'

        with tempfile.TemporaryDirectory() as rt_dir, \
                tempfile.TemporaryDirectory() as real_dir:
            rt_bin_path = Path(rt_dir, BIN_NAME)
            rt_bin_path.write_text(textwrap.dedent('''\
                #!/usr/bin/env sh
                echo "{}"
            '''.format(RT_BIN_MESSAGE)))
            rt_bin_path.chmod(0o755)

            real_bin_path = Path(real_dir, BIN_NAME)
            real_bin_path.write_text(textwrap.dedent('''\
                #!/usr/bin/env sh
                echo "{}"
            '''.format(REAL_BIN_MESSAGE)))
            real_bin_path.chmod(0o755)

            process_env = {
                **os.environ,
                'PATH': rt_dir + ':' + real_dir + ':' + os.environ['PATH'],
                'STEAM_RUNTIME': rt_dir,
            }

            # Make sure the setup is correct.
            result = subprocess.run(
                [BIN_NAME],
                check=True,
                env=process_env,
                stdout=subprocess.PIPE,
                universal_newlines=True,
            )
            self.assertIn(RT_BIN_MESSAGE, result.stdout)
            self.assertNotIn(REAL_BIN_MESSAGE, result.stdout)

            result = subprocess.run(
                [*self.run_outside_ldlp, '-vv', BIN_NAME],
                check=True,
                env=process_env,
                stdout=subprocess.PIPE,
                universal_newlines=True,
            )
            self.assertNotIn(RT_BIN_MESSAGE, result.stdout)
            self.assertIn(REAL_BIN_MESSAGE, result.stdout)

    def test_checks_for_nesting(self) -> None:
        BIN_NAME = 'test-bin'

        result = subprocess.run(
            [*self.run_outside_ldlp, 'env', *self.run_outside_ldlp, 'true'],
        )
        self.assertEqual(result.returncode, LAUNCH_EX_FAILED)

    def test_bad_names(self) -> None:
        for name in 'srt-test', 'steam-runtime-test':
            with tempfile.TemporaryDirectory() as dir:
                script_path = Path(dir, name)
                script_path.write_text('#!/bin/sh')
                script_path.chmod(0o755)

                env = {
                    **os.environ,
                    'PATH': dir + ':' + os.environ['PATH'],
                }

                # Make sure the setup is correct.
                subprocess.run([name], check=True, env=env)

                result = subprocess.run(
                    [*self.run_outside_ldlp, name],
                    env={
                        **os.environ,
                        'PATH': dir + ':' + os.environ['PATH'],
                    },
                )
                self.assertEqual(result.returncode, LAUNCH_EX_FAILED)


if __name__ == '__main__':
    assert sys.version_info >= (3, 5), \
        'Python 3.5+ is required'

    test_main()

# vi: set sw=4 sts=4 et:
