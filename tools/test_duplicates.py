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

import collections
import re
import sys


def find_closing_bracket(s, start_index, start='(', end=')'):
    def find_closing_quote(s, quote_char, start_index):
        i = start_index
        while i < len(s):
            if s[i] == quote_char:
                return i
            elif s[i] == '\\':
                i += 1
            i += 1
        return None

    par_depth = 0
    i = start_index
    while i < len(s):
        if s[i] == start:
            par_depth += 1
        elif s[i] == end:
            if par_depth == 0:
                return i
            else:
                par_depth -= 1
        elif s[i] == '"' or s[i] == '\'':
            i = find_closing_quote(s, s[i], i + 1)
            if i is None: break
        i += 1
    return None


def strip_function_like_macros(s, replace_with):
    r = re.compile("[A-Z][a-zA-Z0-9_]*\s*\(")
    while True:
        m = r.search(s)
        if m is None: break
        closing = find_closing_bracket(s, m.end())
        if closing is None: break
        s = s[:m.start()] + replace_with + s[closing + 1:]
    return s


def strip_gcc_attributes(s):
    r = re.compile("__attribute__\s*\(")
    while True:
        m = r.search(s)
        if m is None: break
        closing = find_closing_bracket(s, m.end())
        if closing is None: break
        s = s[:m.start()] + s[closing + 1:]
    return s


def strip_curly_blocks(s, replace_with):
    while True:
        index = s.find('{')
        if index < 0: break
        closing = find_closing_bracket(s, index + 1, '{', '}')
        if closing is None: break
        s = s[:index] + replace_with + s[closing + 1:]
    return s


def extract_function_name(decl):
    beg = 0
    while beg < len(decl):
        opening_paren = decl.index('(', beg)
        closing_paren = find_closing_bracket(decl, opening_paren + 1)
        if closing_paren is None:
            break
        elif closing_paren == len(decl) - 1:
            match = re.search('[a-z_][a-zA-Z0-9_]*$', decl[:opening_paren])
            if match:
                return match.group()
        beg = closing_paren + 1
    return None


def extract_function_names(filename):
    with open(filename, 'r') as f:
        contents = f.read()
    contents = re.sub('\\\s*\n', ' ', contents)  # join lines on trailing backslash
    contents = re.sub('/\*.*?\*/', '', contents, flags=re.DOTALL)  # remove block comments
    contents = re.sub('//.*$', '', contents, flags=re.MULTILINE)  # remove line comments
    contents = re.sub('^\s*#.*$', '', contents, flags=re.MULTILINE)  # remove preprocessor directives
    contents = strip_function_like_macros(contents, 'MACRO')
    contents = strip_gcc_attributes(contents)
    contents = re.sub('extern\s*"C"\s*\{', '', contents)  # remove extern "C" qualifiers
    contents = strip_curly_blocks(contents, ';')
    contents = re.sub('\s+', ' ', contents)  # replace all whitespace (including newlines with single spaces
    declarations = contents.split(';')
    declarations = (item.strip() for item in declarations if 'typedef ' not in item)
    declarations = filter(lambda item: item.endswith(')'), declarations)
    declarations = (extract_function_name(decl) for decl in declarations)
    return filter(lambda item: item is not None, declarations)


function_files = collections.defaultdict(lambda: [])
for file in sys.argv[1:]:
    for function in extract_function_names(file):
        function_files[function].append(file)

result = 0
for function in function_files:
    if len(function_files[function]) > 1:
        result = -1
        sys.stderr.write('Function {} declared {} times:\n'.format(function, len(function_files[function])))
        for file in function_files[function]:
            sys.stderr.write('  > ' + file + '\n')
sys.exit(result)
