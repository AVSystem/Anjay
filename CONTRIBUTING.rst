Contributing to Anjay
=====================

Thank you for considering contributing to Anjay!

Please take a moment to review this document in order to make the contribution process easy and effective for everyone involved.

Following these guidelines helps to communicate that you respect the time of the developers managing and developing this open source project. In return, they should reciprocate that respect in addressing your issue, assessing changes, and helping you finalize your pull requests.

Contributions to the project are governed by the `Open Code of Conduct <http://todogroup.org/opencodeofconduct/>`_.


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

#. Fork the project,
#. Work on your contribution - follow guidelines described below,
#. Push changes to your fork,
#. Make sure all ``make check`` tests still pass,
#. `Create a pull request <https://help.github.com/articles/creating-a-pull-request-from-a-fork/>`_,
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
- Use visibility macros defined in ``config.h`` to prevent internal symbols from being exported when using a GCC-compatible compiler. See `Visibility macros`_ section for examples.


Visibility macros
`````````````````

- Public header files (``.h`` files inside ``include_public/`` directories): no visibility macros.
- Private header files (``.h`` files outside ``include_public/`` directories)::

    // ... includes

    VISIBILITY_PRIVATE_HEADER_BEGIN

    // ... code

    VISIBILITY_PRIVATE_HEADER_END


- Source files (``.c``)::

    #include <config.h>

    // ... includes

    VISIBILITY_SOURCE_BEGIN

    // ... code


Tests
^^^^^

Make use of the `coverage script <tools/coverage>`_ to generate a code coverage report. New code should be covered by tests.

Before submitting your code, run the whole test suite (``make check``) to ensure that it does not introduce regressions. Use ``valgrind`` and Address Sanitizer to check for memory corruption errors.

