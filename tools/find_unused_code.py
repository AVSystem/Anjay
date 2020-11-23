#!/usr/bin/env python3
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

import argparse
import collections
import logging
import os
import re
import shutil
import subprocess
import sys
import tempfile

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))

Violation = collections.namedtuple('Violation', ['file', 'object', 'symbol'])


class Ignores(collections.namedtuple('Ignores',
                                     ['ignore_files', 'ignore_objects', 'ignore_symbols'])):
    def is_ignored(self, violation):
        return any(re.search(ignore_file, violation.file) is not None for ignore_file in
                   self.ignore_files) or any(
            re.search(ignore_object, violation.object) is not None for ignore_object in
            self.ignore_objects) or any(
            re.search(ignore_symbol, violation.symbol) is not None for ignore_symbol in
            self.ignore_symbols)


def filter_out_static_symbols(violations):
    files = {v.file for v in violations}
    files = [(f if os.path.isabs(f) else os.path.join('demo', f)) for f in files]

    static_symbols = set()
    if len(files) > 0:
        for entry in subprocess.run(['nm', '--portability'] + files, universal_newlines=True,
                                    stdout=subprocess.PIPE, check=True).stdout.splitlines():
            columns = entry.split()
            # second column of nm output specifies symbol type
            # it's supposed to be a single letter, see man nm for details
            # lowercase generally means the symbol has internal linkage
            if len(columns) >= 2 and re.search(r'[a-z]', columns[1]) is not None:
                static_symbols.add(columns[0])

    return {v for v in violations if v.symbol not in static_symbols}


def find_unused_code(jobs, ignores):
    DEVCONFIG_OUT_FNAME = 'devconfig.out'
    MAKE_OUT_FNAME = 'make.out'
    UNUSED_SECTIONS_FNAME = 'unused-sections'

    logging.info('configuring: %s/%s', os.getcwd(), DEVCONFIG_OUT_FNAME)
    with open(DEVCONFIG_OUT_FNAME, 'w') as out:
        subprocess.run([os.path.join(PROJECT_ROOT, 'devconfig'), '--without-memcheck',
                        '-DCMAKE_C_FLAGS=-ffunction-sections -fdata-sections',
                        '-DCMAKE_EXE_LINKER_FLAGS=-Wl,--gc-sections -Wl,--print-gc-sections',
                        '-DWITH_STATIC_ANALYSIS=OFF'],
                       stdout=out, stderr=out, check=True)

    with open('CMakeCache.txt', 'r') as f:
        cmake_binary_candidates = [line for line in f.readlines() if line.startswith('CMAKE_COMMAND')]
        assert len(cmake_binary_candidates) == 1
        cmake_binary = cmake_binary_candidates[0].split('=')[-1].strip()

    logging.info('compiling: %s/%s', os.getcwd(), MAKE_OUT_FNAME)
    with open(MAKE_OUT_FNAME, 'w') as out:
        subprocess.run([cmake_binary, '--build', '.', '--', '-j%d' % (jobs,)], stdout=out, stderr=out,
                       check=True)

    # examples of lines we're looking for in ld output:
    #
    ### GNU ld 2.26.1 on Ubuntu 16.04
    # /usr/bin/ld: Removing unused section '.rodata.AVS_NET_EPROTO' in file '../output/lib/libavs_net.a(net_impl.c.o)'
    # /usr/bin/ld: Removing unused section '.data' in file '/usr/lib/gcc/x86_64-linux-gnu/5/crtbegin.o'
    #
    ### GNU ld 2.31.1 on Ubuntu 18.10 (lowercase r in "removing")
    # /usr/bin/ld: removing unused section '.rodata.AVS_NET_EPROTO' in file '../output/lib/libavs_net.a(net_impl.c.o)'
    # /usr/bin/ld: removing unused section '.data' in file '/usr/lib/gcc/x86_64-linux-gnu/5/crtbegin.o'
    #
    violations = set()
    with open(MAKE_OUT_FNAME, 'r') as f:
        for line in f:
            match = re.search(r'removing unused section (.*) in file (.*)$', line.strip(),
                              re.IGNORECASE)
            if match is None:
                continue

            symbol = match.group(1).strip().strip("'")
            # drop .$SECTION. prefix
            symbol = re.sub(r'^\.(data|rodata|text)\.(rel\.ro\.local\.)?', '', symbol, 1)

            file_obj = match.group(2).strip().strip("'")
            match = re.fullmatch(r'(.*)\((.*)\)', file_obj)
            if match is not None:
                violations.add(Violation(file=match.group(1), object=match.group(2), symbol=symbol))
            else:
                violations.add(Violation(file=file_obj, object=file_obj, symbol=symbol))

    violations = {v for v in violations if not ignores.is_ignored(v)}
    violations = filter_out_static_symbols(violations)

    if len(violations) == 0:
        return 0

    logging.error('unused symbols found:')
    # Column formatting, adapted from https://stackoverflow.com/a/12065663
    widths = [max(map(len, col)) for col in zip(*violations)]
    with open(UNUSED_SECTIONS_FNAME, 'w') as f:
        for violation in sorted(violations):
            line = '  '.join((val.ljust(width) for val, width in zip(violation, widths)))
            print(line, file=f)
            logging.error(line)

    return 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Find unused exported symbols in the codebase")
    parser.add_argument('-j', '--jobs', type=int, help='run build in N parallel jobs',
                        default=os.cpu_count())
    parser.add_argument('-s', '--ignore-symbol', action='append',
                        help='do not report unused symbols matching REGEX')
    parser.add_argument('-o', '--ignore-object', action='append',
                        help='do not report unused symbols from object files matching REGEX')
    parser.add_argument('-f', '--ignore-file', action='append',
                        help='do not report unused symbols from files matching REGEX')
    parser.add_argument('--preserve-tmpdir', action='store_true',
                        help='do not delete temporary directory after finishing')

    args = parser.parse_args()
    logging.getLogger().setLevel(logging.DEBUG)
    for file in args.ignore_file:
        logging.debug('ignoring file: %s', file)
    for obj in args.ignore_object:
        logging.debug('ignoring object file: %s', obj)
    for sym in args.ignore_symbol:
        logging.debug('ignoring symbol: %s', sym)

    tmpdir = tempfile.mkdtemp()
    try:
        os.chdir(tmpdir)
        sys.exit(find_unused_code(args.jobs, Ignores(args.ignore_file, args.ignore_object,
                                                     args.ignore_symbol)))
    finally:
        if not args.preserve_tmpdir:
            shutil.rmtree(tmpdir)
