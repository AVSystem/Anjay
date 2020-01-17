# -*- coding: utf-8 -*-
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

import hashlib
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, '../../..'))


class FileDirtinessChecker:
    def __init__(self, md5hashes_file):
        """
        @param md5hashes_file: str
                - absolute path to a MD5 hashes file. Each line should match
                  following format:

                  MD5HASH <whitespace> FILEPATH_RELATIVE_TO_PROJECT_ROOT
        """
        try:
            with open(md5hashes_file) as f:
                lines = f.readlines()
        except OSError:
            print('***')
            print('*** could not read hash file: %s' % (md5hashes_file,))
            print('***')
            lines = []

        tuples = [line.split(None, 1) for line in lines]
        if any(len(tpl) != 2 for tpl in tuples):
            print('***')
            print('*** malformed hash file: %s' % (md5hashes_file,))
            print('*** expected format:')
            print('*** MD5HASH <whitespace> FILEPATH_RELATIVE_TO_PROJECT_ROOT')
            print('***')
            self.file_to_md5 = {}
        else:
            self.file_to_md5 = dict((tpl[1].rstrip('\n'), tpl[0]) for tpl in tuples)

    def is_file_dirty(self, filepath):
        """
        Compares file MD5 against pre-computed hashes stored in a file
        to determine whether FILEPATH was changed.

        @param filepath: str
                - file path to check for modifications, relative to project root.

        @returns bool
                - True if the was changed (loaded hash file does not contain
                  a hash for FILEPATH or it's different than expected)
        """
        if filepath not in self.file_to_md5:
            return True

        with open(os.path.join(PROJECT_ROOT, filepath), 'rb') as f:
            actual = hashlib.md5(f.read()).hexdigest().lower()
            expected = self.file_to_md5.get(filepath, '').lower()
            return actual != expected
