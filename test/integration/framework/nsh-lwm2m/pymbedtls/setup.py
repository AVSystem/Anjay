# -*- coding: utf-8 -*-
#
# Copyright 2017 AVSystem <avsystem@avsystem.com>
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


# boost_python library for python3 may have a different name depending on specific Linux distro.
# On Ubuntu, it's boost_python-pyXX, where XX is a specific Python major/minor version;
# for Arch it's just boost_python3. Check for both and select the one that's available in the system.
_boost_python_lib_names = ['boost_python3',
                           'boost_python-py%d%d' % (sys.version_info.major, sys.version_info.minor)]
for lib in _boost_python_lib_names:
    if library_exists(lib):
        BOOST_PYTHON_LIB = lib
        break
else:
    raise RuntimeError('Boost::Python3 library not found! Checked names: %s' % (', '.join(_boost_python_lib_names),))

extensions = [
    Extension('pymbedtls',
              sources=[os.path.join(SCRIPT_DIR, 'src/pymbedtls.cpp')],
              libraries=[BOOST_PYTHON_LIB, 'mbedtls', 'mbedcrypto'],
              extra_compile_args=['-std=c++11'])
]

setup(
    name='pymbedtls',
    version='0.1.0',
    description='''DTLS-PSK socket classes''',
    author='Marcin Radomski',
    author_email='m.radomski@avsystem.com',
    license='Apache License, Version 2.0',
    ext_modules=extensions
)
