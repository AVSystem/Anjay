..
   Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Virtual environments
====================

There are several Python-based tools in the repository:

* integration tests framework,
* `LwM2M testing shell <https://avsystem.github.io/Anjay-doc/Tools/CliLwM2MServer.html>`_,
* `Factory provisioning tool <https://avsystem.github.io/Anjay-doc/Tools/FactoryProvisioning.html>`_,
* `Package generator <https://avsystem.github.io/Anjay-doc/Tools/PackagesGenerator.html>`_,
* `Object stub generator <https://avsystem.github.io/Anjay-doc/Tools/StubGenerator.html>`_.

To use them, create, activate and configure a Python vitrual environment by running

.. code-block:: bash

    python3 -m venv venv
    source venv/bin/activate
    python -m pip install --upgrade pip
    python -m pip install -r requirements.txt

or simply run

.. code-block:: bash
    
    ./devconfig
    source venv/bin/activate

in repository root directory.
