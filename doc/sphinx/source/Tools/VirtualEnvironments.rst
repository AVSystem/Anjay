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
* :doc:`CliLwM2MServer`,
* :doc:`FactoryProvisioning`,
* :doc:`PackagesGenerator`,
* :doc:`StubGenerator`.

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
