..
   Copyright 2017-2021 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

Fuzzing avs_coap
================

``tests/fuzz`` directory contains a few tests intended for use with
`AFL <http://lcamtuf.coredump.cx/afl/>`_ fuzzer.

These tests work by basically calling public APIs in random order with random
data until something crashes. Every such test reads the sequence of API calls
and its arguments from stdin, so that crashes can be reproduced later.

Basic usage
-----------

- configure ``avs_coap`` with ``afl-gcc`` and compile everything. When
  ``afl-gcc`` is detected as the compiler, ``make`` will build fuzz tests as
  well::

      ./devconfig -DCMAKE_C_COMPILER=$HOME/tools/afl/latest/afl-gcc \
                  -DCMAKE_C_FLAGS='--coverage'
      make -j

- start ``afl-fuzz``, passing it a directory with initial set of tests (there
  are some under ``tests/fuzz``), the directory to store findings in, and the
  application to be fuzzed::

      afl-fuzz -i ./tests/fuzz/input/coap_async_api_udp \
               -o fuzz-out \
               ./tests/fuzz/avs_coap_async_api_udp

- when AFL finds an input that causes a crash, it is put into ``crashes``
  subdirectory. To reproduce the crash, compile the application with a regular
  compiler (*not* ``afl-gcc``) - out-of-source builds are helpful here; while
  fuzz tests binaries are not normally compiled if the compiler is not
  ``afl-gcc``, you can manually build it by running e.g.
  ``make avs_coap_async_api_udp``. Pass the input to stdin of the built binary.
  Use ``VERBOSE`` environment variable to enable detailed logging if necessary::

      VERBOSE=1 ./tests/fuzz/avs_coap_async_api_udp < fuzz-out/crashes/id:000000,sig:06,src:000003,op:havoc,rep:64

  .. note::

     AFL can find pretty crazy cases. Using `RR <https://rr-project.org/>`_ is
     often extremely helpful when figuring out what exactly happened.

Setting up initial test cases
-----------------------------

Test inputs are binary data, which can be hard to figure out. For that reason,
``tests/fuzz/input/*.hex`` directories contain annotated versions of input
tests. These kind-of-human-readable inputs can be converted into binary form
using ``tests/fuzz/input/hex-to-fuzz-inputs.sh`` script:

    ./hex-to-fuzz-inputs.py < input.hex > input.binary

The script strips all "line comments" starting with #. You may use them freely
in annotated input files.

Fuzz coverage reports
---------------------

To figure out what parts of the code did AFL reach, use
`afl-cov <https://github.com/mrash/afl-cov>`_. You will need to pass
``--coverage`` to ``CMAKE_C_FLAGS`` when configuring ``avs_coap`` to make this
work.

Example usage::

    afl-cov --afl-fuzzing-dir ./fuzz-out --coverage-cmd "./tests/fuzz/avs_coap_async_api_udp < AFL_FILE" --code-dir .
    # after this finishes, open ./fuzz-out/cov/web/ in a web browser
