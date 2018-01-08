# -*- coding: utf-8 -*-
#
# Copyright 2017-2018 AVSystem <avsystem@avsystem.com>
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
import sys
from distutils.core import setup
from distutils.extension import Extension

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))


def library_exists(lib_name):
    import subprocess
    import tempfile

    with tempfile.NamedTemporaryFile(suffix='.cpp') as f:
        f.write(b'int main() { return 0; }')
        f.flush()
        result = subprocess.call(('c++ -shared -o /dev/null %s -l%s' % (f.name, lib_name)).split(),
                                 stdout=open(os.devnull, 'w'),
                                 stderr=open(os.devnull, 'w'))

    return result == 0


extensions = [
    Extension('pymbedtls',
              sources=[os.path.join(SCRIPT_DIR, 'src/pymbedtls.cpp')],
              libraries=['mbedtls', 'mbedcrypto'],
              include_dirs=[os.path.join(SCRIPT_DIR, 'src/pybind11/include/')],
              extra_compile_args=['-std=c++11', '-isystem', '/usr/local/include'])
]

setup(
    name='pymbedtls',
    version='0.2.0',
    description='''DTLS-PSK socket classes''',
    author='Marcin Radomski',
    author_email='m.radomski@avsystem.com',
    license='Apache License, Version 2.0',
    ext_modules=extensions
)
