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


.. note::

    This section describes in details the implementation of custom Objects in
    Anjay, either defined in `OMA LwM2M Object and Resource Registry
    <https://www.openmobilealliance.org/wp/OMNA/LwM2M/LwM2MRegistry.html>`_
    or designed by user.

    Although most of the Object's code can be generated using
    :ref:`anjay-object-stub-generator` if you have Object's definition in XML,
    it is recommended to read this section to have a clear understanding on what
    various parts of the LwM2M Object code are for.
