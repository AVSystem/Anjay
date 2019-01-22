#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import re
import subprocess
import sys
from argparse import ArgumentParser
from io import StringIO

EXPECTED_COPYRIGHT_HEADER = 'Copyright 2017-2019 AVSystem <avsystem@avsystem.com>'

EXPECTED_LICENSE_LINES = [
    'Licensed under the Apache License, Version 2.0 (the "License");',
    'you may not use this file except in compliance with the License.',
    'You may obtain a copy of the License at',
    '',
    '    http://www.apache.org/licenses/LICENSE-2.0',
    '',
    'Unless required by applicable law or agreed to in writing, software',
    'distributed under the License is distributed on an "AS IS" BASIS,',
    'WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.',
    'See the License for the specific language governing permissions and',
    'limitations under the License.'
]

IGNORE_PATTERNS = list(map(re.compile, [
    '^\.git',
    '^examples/',
    '^test/fuzz/test_cases/',
    '^doc/sphinx/[Mm]ake',
    '^doc/sphinx/source/conf\.py\.in$',
    '^Doxyfile\.in$',
    '^LICENSE$',
    '^NOTICE$',
    '^README\.md$',
    '^README\.Windows\.md$',
    '^valgrind_test\.supp$',
    '^\.clang-format$',
    '\.png$',
    '\.svg$'
]))


def show_license():
    print(EXPECTED_COPYRIGHT_HEADER)
    print('<more copyright lines may follow>')
    print('')
    for line in EXPECTED_LICENSE_LINES:
        print(line)


def show_ignores():
    for pattern in IGNORE_PATTERNS:
        print(pattern.pattern)


def is_ignored(filename):
    for pattern in IGNORE_PATTERNS:
        if pattern.search(filename) is not None:
            return True
    return False


def get_file_list(origin_commit):
    output = subprocess.run(['git', 'diff', '--name-only', origin_commit],
                            stdout=subprocess.PIPE, universal_newlines=True, check=True)
    with StringIO(output.stdout) as f:
        return [os.path.normpath(line.strip()) for line in f]


def check_license(filename):
    prefix = None
    expected_line = 0
    with open(filename, mode='r', encoding='utf-8', errors='surrogateescape') as f:
        for line in f:
            trimmed = line.strip()
            if prefix is None:
                found = trimmed.split(EXPECTED_COPYRIGHT_HEADER)
                if len(found) > 1:
                    prefix = found[0]
            elif trimmed == (prefix + EXPECTED_LICENSE_LINES[expected_line]).strip():
                expected_line += 1
                if expected_line == len(EXPECTED_LICENSE_LINES):
                    return True
            elif expected_line > 0:
                break
    return False


def _main():
    parser = ArgumentParser(description='Check which files in the project do not have '
                                        'the proper Apache 2.0 license header.')
    parser.add_argument('-l', '--show-license',
                        help='Displays the expected license header.',
                        action='store_true')
    parser.add_argument('-i', '--show-ignores',
                        help='Displays the currently hard-coded patterns for ignored '
                             'file names.',
                        action='store_true')
    parser.add_argument('-n', '--no-ignores',
                        help='Checks even files that are normally ignored.',
                        action='store_true')
    parser.add_argument('-d', '--diff',
                        help='Only check files that were changed since the commit '
                             'passed as argument. By default, checks all files',
                        # semi-well known SHA for git's empty tree
                        # see http://stackoverflow.com/q/9765453 for details
                        # might also use `git hash-object -t tree /dev/null`
                        default='4b825dc642cb6eb9a060e54bf8d69288fbee4904')
    parser.add_argument('-r', '--root',
                        help='Project root directory. Must be a git working tree.',
                        default='.')

    args = parser.parse_args()

    if args.show_license:
        show_license()
        return 0

    if args.show_ignores:
        show_ignores()
        return 0

    result = 0
    os.chdir(args.root)
    for f in get_file_list(args.diff):
        if (os.path.isfile(f)
                and not os.path.islink(f)
                and (args.no_ignores or not is_ignored(f))
                and not check_license(f)):
            print(f)
            result = 1

    return result


if __name__ == '__main__':
    sys.exit(_main())
