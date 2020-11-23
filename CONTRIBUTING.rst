..
   Copyright 2017-2020 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

Contributing to Anjay
=====================

Thank you for considering contributing to Anjay!

Please take a moment to review this document in order to make the contribution process easy and effective for everyone involved.

Following these guidelines helps to communicate that you respect the time of the developers managing and developing this open source project. In return, they should reciprocate that respect in addressing your issue, assessing changes, and helping you finalize your pull requests.


Reporting issues
----------------

**If you find a security vulnerability, do NOT open an issue!** Please email `opensource@avsystem.com <mailto:opensource@avsystem.com>`_ instead. We will address the issue as soon as possible and will give you an estimate for when we have a fix and release available for an eventual public disclosure.

Use GitHub issue tracker for reporting other bugs. A great bug report should include:

- Affected library version.
- Platform information:

  - processor architecture,
  - operating system,
  - compiler version.
- Minimal code example or a list of steps required to reproduce the bug.
- In case of run-time errors, logs from ``strace``, ``valgrind`` and PCAP network traffic dumps are greatly appreciated.


Feature requests
----------------

If you think some feature is missing, or that something can be improved, do not hesitate to suggest how we can make it better! When doing that, use GitHub issue tracker to post a description of the feature and some explanation why you need it.


Code contributions
------------------

If you never submitted a pull request before, take a look at `this fantastic tutorial <https://egghead.io/courses/how-to-contribute-to-an-open-source-project-on-github>`_.

#. Fork the project.
#. Work on your contribution - follow guidelines described below.
#. Push changes to your fork.
#. Make sure all ``make check`` tests still pass.
#. `Create a pull request <https://help.github.com/articles/creating-a-pull-request-from-a-fork/>`_.
#. Wait for someone from Anjay team to review your contribution and give feedback.


Code style
^^^^^^^^^^

All code should be fully C99-compliant and compile without warnings under ``gcc`` and ``clang``. Enable extra warnings by using ``cmake -DWITH_EXTRA_WARNINGS=ON`` or ``devconfig`` script. Compiling and testing the code on multiple architectures (e.g. 32/64-bit x86, ARM, MIPS) is not required, but welcome.


General guidelines
``````````````````
- Do not use GNU extensions.
- Use ``UPPER_CASE`` for constants and ``snake_case`` in all other cases.
- Prefer ``static`` functions. Use ``_anjay_`` prefix for private functions shared between translation units and ``anjay_`` prefix for types and public functions.
- Do not use global variables. Only constants are allowed in global scope.
- When using bitfields, make sure they are not saved to persistent storage nor sent over the network - their memory layout is implementation-defined, making them non-portable.
- Avoid recursion - when writing code for an embedded platform, it is important to determine a hard limit on the stack space used by a program.
- Include license information at the top of each file you add.
- Use visibility macros defined in ``anjay_init.h`` to prevent internal symbols from being exported when using a GCC-compatible compiler. See `Visibility macros`_ section for examples.


Visibility macros
`````````````````
- Public header files (``.h`` files inside ``include_public/`` directories): no visibility macros.
- Private header files (``.h`` files outside ``include_public/`` directories)::

    // ... includes

    VISIBILITY_PRIVATE_HEADER_BEGIN

    // ... code

    VISIBILITY_PRIVATE_HEADER_END


- Source files (``.c``)::

    #include <anjay_init.h>

    // ... includes

    VISIBILITY_SOURCE_BEGIN

    // ... code


Tests
^^^^^

Make use of the `coverage script <tools/coverage>`_ to generate a code coverage report. New code should be covered by tests.

Before submitting your code, run the whole test suite (``make check``) to ensure that it does not introduce regressions. Use ``valgrind`` and Address Sanitizer to check for memory corruption errors.

Running tests on Ubuntu 16.04 or later: ::

    # Install these for tests:
    sudo apt-get install libpython3-dev libssl-dev python3 python3-cryptography python3-jinja2 python3-sphinx python3-requests clang valgrind clang-tools
    # Configure and run check target
    ./devconfig && make check

Running tests on CentOS 7 or later: ::

    # Install these for tests:
    # (IUS is required for Python 3.5)
    sudo yum install -y https://repo.ius.io/ius-release-el7.rpm
    sudo yum install -y valgrind valgrind-devel openssl openssl-devel python35u python35u-devel python35u-pip clang-analyzer
    # Some test scripts expect Python >=3.5 to be available via `python3` command
    # Use update-alternatives to create a /usr/bin/python3 symlink with priority 0
    # (lowest possible)
    sudo update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.5 0
    sudo python3 -m pip install cryptography jinja2 requests sphinx sphinx_rtd_theme

    # Configure and run check target
    # NOTE: clang-3.4 static analyzer (default version for CentOS) gives false
    # positives. --without-analysis flag disables static analysis.
    ./devconfig --without-analysis -DPython_ADDITIONAL_VERSIONS=3.5 && make check

Running tests on macOS Sierra or later: ::

    # Install these for tests:
    brew install python3 openssl llvm
    pip3 install cryptography sphinx sphinx_rtd_theme requests

    # Configure and run check target:
    # if the scan-build script is located somewhere else, then you need to
    # specify a different SCAN_BUILD_BINARY. Below, we are assumming scan-build
    # comes from an llvm package, installed via homebrew.
    ./devconfig -DSCAN_BUILD_BINARY=/usr/local/Cellar/llvm/*/bin/scan-build && make check
