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

Persistence support
===================

.. highlight:: c

Anjay's persistence in general
------------------------------

Anjay supports persistence of data to ``avs_commons`` generic `stream`
(``avs_stream_t``). Underlying implementation of a stream may
abstract any kind of storage access. ``avs_commons`` itself provides:

- file streams,
- network streams,
- memory buffered streams.

Any of them can be used with Anjay's persistence API. Additionally, one
can easily adapt other types of storages to be used by persistence API,
by implementing their own stream.

Persistence module is designed to abstract all endianness related issues,
which makes persisted chunk of information on arbitrary architecture
(having arbitrary endianness) easily restorable on any other architecture.

Persistence of preimplemented objects/modules
---------------------------------------------

Anjay's preimplemented objects (Security, Server, Access Control) and Attribute
storage module all support persistence, for which the following functions can be
used:

- Security Object:

   * ``anjay_security_object_persist()``
   * ``anjay_security_object_restore()``

- Server Object:

   * ``anjay_server_object_persist()``
   * ``anjay_server_object_restore()``

- Access Control Object:

   * ``anjay_access_control_persist()``
   * ``anjay_access_control_restore()``

- Attribute storage:

   * ``anjay_attr_storage_persist()``
   * ``anjay_attr_storage_restore()``

.. note::
    All of the mentioned objects have complicated semantics, which is why you
    should refer to their `documentation <../api/index.html>`_ for more details
    on how persistence functions behave under different conditions.

Example
-------

As an example we'll modify the code from the :doc:`../BasicClient/BC3` tutorial.
We would like to persist Object data when the LwM2M Client finishes its work and
restore it on startup (if a valid persistence file exists).

.. snippet-source:: examples/tutorial/AT-Persistence/src/main.c

    #define PERSISTENCE_FILENAME "at2-persistence.dat"

    int persist_objects(anjay_t *anjay) {
        avs_log(tutorial, INFO, "Persisting objects to %s", PERSISTENCE_FILENAME);

        avs_stream_t *file_stream =
                avs_stream_file_create(PERSISTENCE_FILENAME, AVS_STREAM_FILE_WRITE);

        if (!file_stream) {
            avs_log(tutorial, ERROR, "Could not open file for writing");
            return -1;
        }

        int result = -1;

        if (avs_is_err(anjay_security_object_persist(anjay, file_stream))) {
            avs_log(tutorial, ERROR, "Could not persist Security Object");
            goto finish;
        }

        if (avs_is_err(anjay_server_object_persist(anjay, file_stream))) {
            avs_log(tutorial, ERROR, "Could not persist Server Object");
            goto finish;
        }

        if (avs_is_err(anjay_attr_storage_persist(anjay, file_stream))) {
            avs_log(tutorial, ERROR, "Could not persist LwM2M attribute storage");
            goto finish;
        }

        result = 0;
    finish:
        avs_stream_cleanup(&file_stream);
        return result;
    }

.. snippet-source:: examples/tutorial/AT-Persistence/src/main.c

    int restore_objects_if_possible(anjay_t *anjay) {
        avs_log(tutorial, INFO, "Attempting to restore objects from persistence");
        int result;

        errno = 0;
        if ((result = access(PERSISTENCE_FILENAME, F_OK))) {
            switch (errno) {
            case ENOENT:
            case ENOTDIR:
                // no persistence file means there is nothing to restore
                return 1;
            default:
                // some other unpredicted error
                return result;
            }
        } else if ((result = access(PERSISTENCE_FILENAME, R_OK))) {
            // most likely file is just not readable
            return result;
        }

        avs_stream_t *file_stream =
                avs_stream_file_create(PERSISTENCE_FILENAME, AVS_STREAM_FILE_READ);

        if (!file_stream) {
            return -1;
        }

        result = -1;

        if (avs_is_err(anjay_security_object_restore(anjay, file_stream))) {
            avs_log(tutorial, ERROR, "Could not restore Security Object");
            goto finish;
        }

        if (avs_is_err(anjay_server_object_restore(anjay, file_stream))) {
            avs_log(tutorial, ERROR, "Could not restore Server Object");
            goto finish;
        }

        if (avs_is_err(anjay_attr_storage_restore(anjay, file_stream))) {
            avs_log(tutorial, ERROR, "Could not restore LwM2M attribute storage");
            goto finish;
        }

        result = 0;
    finish:
        avs_stream_cleanup(&file_stream);
        return result;
    }

.. note::
    Persisting as well as restoring functions MUST be both called in the same
    order because objects' data is being stored sequentially.

Persistence API
---------------

Please refer to the `documentation of the `avs_persistence component
<https://github.com/AVSystem/avs_commons/blob/master/include_public/avsystem/commons/avs_persistence.h>`_.
