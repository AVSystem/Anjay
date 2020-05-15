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

import re
import argparse
import string
import sys
import collections


TOC_START_MARKER = '<!-- toc -->'
TOC_END_MARKER = '<!-- /toc -->'


Header = collections.namedtuple('Header', ['level', 'title'])


def extract_toc_headers(content):
    headers = []

    for line in content.split('\n'):
        # double hash is a dirty hack to skip comment lines in code blocks
        m = re.fullmatch(r'(##+)(.*)', line)
        if m is not None:
            headers.append(Header(level=len(m.group(1)),
                                  title=m.group(2).strip()))
        elif line.strip() == TOC_END_MARKER:
            # headers preceding TOC are not included in the table
            headers = []

    return headers


def strip_links(text):
    return re.sub(r'\[(.*?)]\(.*?\)', r'\1', text)


def anchor_from_title(title):
    # Should be fine, as it follows: https://github.com/jch/html-pipeline/blob/master/lib/html/pipeline/toc_filter.rb#L42
    title = title.lower()
    title = ''.join(c for c in title if c not in string.punctuation)
    return title.replace(' ', '-')


def make_toc_from_headers(headers):
    min_level = min(h.level for h in headers)
    toc_string = '\n'

    for header in headers:
        indent = '  ' * (header.level - min_level)
        linkless_header = strip_links(header.title)
        toc_string += '%s* [%s](#%s)\n' % (indent, linkless_header, anchor_from_title(linkless_header))

    return toc_string


parser = argparse.ArgumentParser(formatter_class=argparse.RawDescriptionHelpFormatter,
                                 description='''
Checks or generates a table of contents for a Markdown file. TOC contents are
inserted between %s and %s markers in the parsed file.

If markers are not present, TOC is not inserted.

Note: top-level headers (ones starting with a single #) are not included in the TOC.
'''.strip() % (TOC_START_MARKER, TOC_END_MARKER))

parser.add_argument('files', metavar='FILE', nargs='+',
                    help='Files to update table of contents in.')
parser.add_argument('--update', '-u', action='store_true',
                    help='Update TOC in specified files.')
parser.add_argument('--check', '-c', action='store_true',
                    help='Set non-zero exit code if TOC needs updating in any '
                         'of specified files.')

cmdline_args = parser.parse_args()
has_changes = False

for filename in cmdline_args.files:
    with open(filename) as f:
        content = f.read()

    toc = make_toc_from_headers(extract_toc_headers(content))
    new_content = re.sub(re.escape(TOC_START_MARKER) + '.*' + re.escape(TOC_END_MARKER),
                         '%s\n%s\n%s' % (TOC_START_MARKER, toc, TOC_END_MARKER),
                         content,
                         flags=re.DOTALL)

    if content == new_content:
        print('%s: content not changed' % (filename,))
    else:
        print('%s: changed, new TOC:\n%s' % (filename, toc))
        has_changes = True

        if cmdline_args.update:
            with open(filename, 'w') as f:
                f.write(new_content)

if cmdline_args.check:
    sys.exit(1 if has_changes else 0)
