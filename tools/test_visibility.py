#!/usr/bin/env python3
#
# Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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
import sys
import re
import collections
from enum import Enum

FileType = Enum('FileType', 'PUBLIC_HEADER PRIVATE_HEADER PRIVATE_SOURCE IGNORED')

def classify_file(filename) -> FileType:
    if '/tests/' in filename:
        return FileType.IGNORED

    if filename.endswith('.h'):
        if '/include_public/' in filename:
            return FileType.PUBLIC_HEADER
        else:
            return FileType.PRIVATE_HEADER
    else:
        return FileType.PRIVATE_SOURCE

def make_replacement(pattern, replacement='', flags=re.MULTILINE):
    return (pattern, replacement, flags)

replacements = [
    # joins lines on trailing backslash
    make_replacement(r'\\\s*\n', replacement=' '),
    # removes leading spaces
    make_replacement(r'^\s*'),
    # removes block comments (incl. multiline)
    make_replacement(r'/\*.*?\*/', flags=re.MULTILINE|re.DOTALL),
    # removes line comments
    make_replacement(r'//.*$'),
    # removes preprocessor directives
    make_replacement(r'^#.*$'),
    # removes trailing spaces
    make_replacement(r'\s*$'),
    # removes extern function declarations
    make_replacement(r'^extern .*\);$'),
    # removes empty lines
    make_replacement(r'^\s*$')
]

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('Error: you must provide absolute path to the file to check')
        sys.exit(1)

    filename = sys.argv[1]
    filetype = classify_file(filename)

    if filetype == FileType.IGNORED:
        sys.exit(0)

    with open(filename, 'r') as fp:
        contents = fp.read()

    for pattern, replacement, flags in replacements:
        contents = re.sub(pattern, replacement, contents, flags=flags).strip()

    lines = contents.split()
    valid = False
    if filetype == FileType.PRIVATE_HEADER:
        valid = all((lines[0] == 'VISIBILITY_PRIVATE_HEADER_BEGIN',
                     lines[-1] == 'VISIBILITY_PRIVATE_HEADER_END'))
    elif filetype == FileType.PUBLIC_HEADER:
        valid = len(lines) == 0 or all((re.match(r'^VISIBILITY', lines[0]) is None,
                                        re.match(r'^VISIBILITY', lines[-1]) is None))
    elif filetype == FileType.PRIVATE_SOURCE:
        valid = lines[0] == 'VISIBILITY_SOURCE_BEGIN'

    sys.exit(0 if valid else 1)
