#!/usr/bin/env python3
# Copyright 2020 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

import gzip
import hashlib
import os
import subprocess
import sys
import tempfile
import typing
from pathlib import Path


from testutils import (
    BaseTest,
    test_main,
)


def sha256(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


class TestPvVerify(BaseTest):
    def setUp(self) -> None:
        super().setUp()
        os.environ['G_MESSAGES_DEBUG'] = 'all'
        self.pv_verify = Path(self.G_TEST_BUILDDIR) / (
            '../../pressure-vessel/pv-verify'
        )

    def assert_completed_success(
        self,
        completed: subprocess.CompletedProcess
    ) -> None:
        stdout = completed.stdout
        stderr = completed.stderr
        self.assertEqual(stdout, '')
        assert stderr is not None

        if completed.returncode != 0:
            print('# Verification failed unexpectedly:')

            for line in stderr.splitlines():
                print('#\t{!r}'.format(line))

            self.fail()

    def assert_verify_fails(
        self,
        tree: Path,
    ) -> str:
        completed = subprocess.run(
            [
                str(self.pv_verify),
                '--',
                str(tree),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,
        )
        stdout = completed.stdout
        stderr = completed.stderr
        self.assertEqual(stdout, '')
        assert stderr is not None
        self.assertTrue(stderr)
        self.assertEqual(completed.returncode, 1)
        self.assertRegex(
            stderr,
            r'Verifying ".*" with ".*mtree.txt.gz" failed',
        )

        print('# Verification failed as expected:')

        for line in stderr.splitlines():
            print('#\t{!r}'.format(line))

        return stderr

    def write_mtree(
        self,
        tree: Path,
        manifest_name: str,
        lines: typing.List[str]
    ) -> None:
        writer = gzip.open(str(tree / manifest_name), 'wt')

        for line in lines:
            writer.write(line)
            writer.write('\n')

        writer.write('./mtree.txt.gz type=file optional\n')
        writer.close()

    def test_good(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as tree:
            (Path(tree) / 'bin').mkdir(parents=True)
            (Path(tree) / 'bin/env').symlink_to('../usr/bin/env')
            (Path(tree) / 'lib/x86_64-linux-gnu').mkdir(parents=True)
            (Path(tree) / 'usr/bin').mkdir(parents=True)
            (Path(tree) / 'usr/bin/env').touch()
            (Path(tree) / 'var/tmp').mkdir(parents=True)
            self.write_mtree(
                Path(tree), 'mtree.txt.gz',
                [
                    './bin type=dir',
                    './lib type=dir',
                    './lib/x86_64-linux-gnu type=dir',
                    './usr type=dir',
                    './usr/bin type=dir',
                    './usr/bin/env type=file size=0',
                    './bin/env type=link link=../usr/bin/env',
                    './maybe type=file optional',
                    './var type=dir ignore',
                ]
            )

            completed = subprocess.run(
                [
                    str(self.pv_verify),
                    '--',
                    tree,
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                universal_newlines=True,
            )
            self.assert_completed_success(completed)

    def test_good_runtime(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as tree:
            (Path(tree) / 'files/bin').mkdir(parents=True)
            (Path(tree) / 'files/bin/env').symlink_to('../usr/bin/env')
            self.write_mtree(
                Path(tree), 'usr-mtree.txt.gz',
                [
                    './bin type=dir',
                    './bin/env type=link link=../usr/bin/env',
                    './empty type=file size=0',
                ]
            )

            completed = subprocess.run(
                [
                    str(self.pv_verify),
                    '--minimized-runtime',
                    '--',
                    str(Path(tree) / 'files'),
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                universal_newlines=True,
            )
            self.assert_completed_success(completed)

    def test_file_should_be_symlink(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as tree:
            (Path(tree) / 'should-be-symlink').touch()
            self.write_mtree(
                Path(tree), 'mtree.txt.gz',
                [
                    './should-be-symlink type=link link=bin/env',
                ]
            )

            stderr = self.assert_verify_fails(tree)
            self.assertRegex(
                stderr,
                (r'"should-be-symlink" in ".*" is not a symlink to '
                 r'"bin/env":'),
            )

    def test_should_be_executable(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as tree:
            (Path(tree) / 'file').touch()
            (Path(tree) / 'file').chmod(0o640)
            self.write_mtree(
                Path(tree), 'mtree.txt.gz',
                [
                    './file type=file mode=0711',
                ]
            )

            stderr = self.assert_verify_fails(tree)
            self.assertRegex(
                stderr,
                r'"file" in ".*" should be executable, not mode 0640',
            )

    def test_symlink_should_be_file(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as tree:
            (Path(tree) / 'should-be-file').symlink_to('/nonexistent')
            self.write_mtree(
                Path(tree), 'mtree.txt.gz',
                [
                    './should-be-file type=file',
                ]
            )

            stderr = self.assert_verify_fails(tree)
            self.assertRegex(
                stderr,
                r'Unable to open regular file "should-be-file" in ".*":',
            )

    def test_unexpected_dir(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as tree:
            (Path(tree) / 'rogue').mkdir()
            self.write_mtree(Path(tree), 'mtree.txt.gz', [])

            stderr = self.assert_verify_fails(tree)
            self.assertRegex(
                stderr,
                r'directory "rogue" in ".*" not found in manifest',
            )

    def test_unexpected_file(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as tree:
            (Path(tree) / 'rogue').touch()
            self.write_mtree(Path(tree), 'mtree.txt.gz', [])

            stderr = self.assert_verify_fails(tree)
            self.assertRegex(
                stderr,
                r'regular file "rogue" in ".*" not found in manifest',
            )

    def test_unexpected_symlink(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as tree:
            (Path(tree) / 'rogue').symlink_to('/')
            self.write_mtree(Path(tree), 'mtree.txt.gz', [])

            stderr = self.assert_verify_fails(tree)
            self.assertRegex(
                stderr,
                r'symbolic link "rogue" in ".*" not found in manifest',
            )

    def test_wrong_content(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as tree:
            (Path(tree) / 'file').write_text('a')
            self.write_mtree(
                Path(tree), 'mtree.txt.gz',
                [
                    './file type=file size=1 sha256={}'.format(sha256(b'b')),
                ]
            )

            stderr = self.assert_verify_fails(tree)
            self.assertRegex(
                stderr,
                r'"file" in ".*" did not have expected contents',
            )

    def test_wrong_size(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as tree:
            (Path(tree) / 'file').touch()
            self.write_mtree(
                Path(tree), 'mtree.txt.gz',
                [
                    './file type=file size=1',
                ]
            )

            stderr = self.assert_verify_fails(tree)
            self.assertRegex(
                stderr,
                r'"file" in ".*" should have size 1, not 0',
            )

    def test_wrong_symlink(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as tree:
            (Path(tree) / 'bin').mkdir(parents=True)
            (Path(tree) / 'bin/env').symlink_to('/nonexistent')
            self.write_mtree(
                Path(tree), 'mtree.txt.gz',
                [
                    './bin type=dir',
                    './bin/env type=link link=../usr/bin/env',
                ]
            )

            stderr = self.assert_verify_fails(tree)
            self.assertRegex(
                stderr,
                (r'"bin/env" in ".*" points to "/nonexistent", '
                 r'expected "../usr/bin/env"'),
            )

    def tearDown(self) -> None:
        super().tearDown()


if __name__ == '__main__':
    assert sys.version_info >= (3, 5), \
        'Python 3.5+ is required (configure with -Dpython=python3.5 ' \
        'if necessary)'

    test_main()

# vi: set sw=4 sts=4 et:
