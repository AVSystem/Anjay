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
import re
import sys

INCLUDE_WHITELIST = {
    r'assert\.h',
    # r'complex\.h',
    r'ctype\.h',
    r'errno\.h',
    # r'fenv\.h',
    # r'float\.h',
    r'inttypes\.h',
    # r'iso646\.h',
    r'limits\.h',
    # r'locale\.h',
    r'math\.h',
    # r'setjmp\.h',
    # r'signal\.h',
    r'stdarg\.h',
    r'stdbool\.h',
    r'stddef\.h',
    r'stdint\.h',
    r'stdio\.h',
    r'stdlib\.h',
    r'string\.h',
    # r'tgmath\.h',
    r'time\.h',
    # r'wchar\.h',
    # r'wctype\.h',
    r'anjay_config_log\.h',
    r'anjay_config\.h',
    r'avsystem/commons/[^.]*\.h',
    r'avsystem/coap/[^.]*\.h',
    r'anjay/[^.]*\.h',
    r'anjay_modules/[^.]*\.h'
}

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('Error: you must provide absolute path to the file to check')
        sys.exit(1)

    filename = sys.argv[1]

    if any(w in filename for w in ('/tests/', '/modules/at_sms/')):
        sys.exit(0)

    with open(filename, 'r') as fp:
        contents = fp.readlines()

    # verify that #include <...> directives only include files from the whitelist above
    # (prevent including forbidden dependencies such as POSIX)
    for line in contents:
        m = re.match(r'^\s*#\s*include\s*<([^>]*)>', line)
        if m and not any(re.match(pattern, m.group(1)) for pattern in INCLUDE_WHITELIST):
            raise ValueError('Invalid include: %s\n' % (m.group(0),))
