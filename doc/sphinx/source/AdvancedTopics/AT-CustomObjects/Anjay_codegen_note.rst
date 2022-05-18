..
   Copyright 2017-2022 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.


.. note::

    This section describes in details the implementation of custom Objects in
    Anjay, either defined in `OMA LwM2M Object and Resource Registry
    <https://technical.openmobilealliance.org/OMNA/LwM2M/LwM2MRegistry.html>`_
    or designed by user.

    Although most of the Object's code can be generated using
    :ref:`anjay-object-stub-generator` if you have Object's definition in XML,
    it is recommended to read this section to have a clear understanding on what
    various parts of the LwM2M Object code are for.
