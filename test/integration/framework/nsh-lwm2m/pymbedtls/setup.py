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

import os
import sys
import distutils.sysconfig
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


# Remove the "-Wstrict-prototypes" compiler option, which isn't valid for C++.
# Taken from https://stackoverflow.com/a/29634231
cfg_vars = distutils.sysconfig.get_config_vars()
for key, value in cfg_vars.items():
    if type(value) == str:
        cfg_vars[key] = value.replace("-Wstrict-prototypes", "")

library_dirs = []
include_dirs = [os.path.join(SCRIPT_DIR, 'src/pybind11/include/')]

if 'MBEDTLS_ROOT_DIR' in os.environ:
    root = os.getenv('MBEDTLS_ROOT_DIR')
    library_dirs += [ os.path.join(root, 'lib') ]
    include_dirs += [ os.path.join(root, 'include') ]

extensions = [
    Extension('pymbedtls',
              sources=[os.path.join(SCRIPT_DIR, 'src/pymbedtls.cpp'),
                       os.path.join(SCRIPT_DIR, 'src/socket.cpp'),
                       os.path.join(SCRIPT_DIR, 'src/common.cpp'),
                       os.path.join(SCRIPT_DIR, 'src/context.cpp'),
                       os.path.join(SCRIPT_DIR, 'src/security.cpp')],
              library_dirs=library_dirs,
              libraries=['mbedtls', 'mbedcrypto', 'mbedx509'],
              include_dirs=include_dirs,
              extra_compile_args=['-std=c++1y', '-isystem', '/usr/local/include'])
]

setup(
    name='pymbedtls',
    version='0.3.0',
    description='''DTLS-PSK socket classes''',
    author='AVSystem',
    author_email='avsystem@avsystem.com',
    license='Commercial',
    ext_modules=extensions
)
