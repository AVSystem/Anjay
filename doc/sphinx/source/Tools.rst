..
   Copyright 2017 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

Tools
=====

.. _anjay-object-stub-generator:

Anjay Object stub generator
---------------------------

For easy implementation of custom objects, you can use the
`./tools/anjay_codegen.py` script. It parses an LwM2M Object Definition XML
and generates a skeleton of the LwM2M object code, requiring the user to only
fill in actual object logic.

You can use `./tools/lwm2m_object_registry.py` script to download the
Object Definition XML from `OMA LwM2M Object and Resource Registry
<http://www.openmobilealliance.org/wp/OMNA/LwM2M/LwM2MRegistry.html>`_.

Examples
~~~~~~~~

.. code-block:: bash

    # list registered LwM2M objects
    ./tools/lwm2m_object_registry.py --list

    # download Object Definition XML for object 3 (Device) to device.xml
    ./tools/lwm2m_object_registry.py --get-xml 3 > device.xml

    # generate object code stub from device.xml
    ./tools/anjay_codegen.py -i device.xml -o device.c

    # download Object Defintion XML for object 3 and generate code stub
    # without creating an intermediate file
    ./tools/lwm2m_object_registry.py --get-xml 3 | ./tools/anjay_codegen.py -i - -o device.c

