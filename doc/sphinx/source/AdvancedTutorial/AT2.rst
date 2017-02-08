Persistence support
===================

Anjay's persistence in general
------------------------------

Anjay supports persistence of data to ``avs_commons`` generic `stream`
(``avs_stream_abstract_t``). Underlying implementation of a stream may
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

Persistence of preimplemented objects
-------------------------------------

Anjay's preimplemented objects (Security, Server, Access Control) all
support persistence:

.. highlight:: c

.. snippet-source:: modules/security/include_public/anjay/security.h

    // ...
    int anjay_security_object_persist(const anjay_dm_object_def_t *const *obj,
                                      avs_stream_abstract_t *out_stream);
    // ...
    int anjay_security_object_restore(const anjay_dm_object_def_t *const *obj,
                                      avs_stream_abstract_t *in_stream);


.. snippet-source:: modules/server/include_public/anjay/server.h

    // ...
    int anjay_server_object_persist(const anjay_dm_object_def_t *const *obj,
                                    avs_stream_abstract_t *out_stream);
    // ...
    int anjay_server_object_restore(const anjay_dm_object_def_t *const *obj,
                                    avs_stream_abstract_t *in_stream);


.. snippet-source:: modules/access_control/include_public/anjay/access_control.h

    // ...
    int anjay_access_control_persist(const anjay_dm_object_def_t *const *ac_obj,
                                     avs_stream_abstract_t *out_stream);
    // ...
    int anjay_access_control_restore(const anjay_dm_object_def_t *const *ac_obj,
                                     avs_stream_abstract_t *in_stream);

.. warning::
    One should however note that every object handle MUST NOT point to a
    wrapped object (see :ref:`wrapping-objects` for example of the wrapped
    object).

.. note::
    All of the mentioned objects have complicated semantics, which is why you
    should refer to their `documentation <../../api>`_ for more details on how persistence
    functions behave under different conditions.

Example
-------

As an example we'll modify the code from the `previous tutorial <AT1>`_. We would like
to persist Object data when the LwM2M Client finishes its work and restore it on
startup (if a valid persistence file exists).

.. snippet-source:: examples/tutorial/AT2/src/main.c

    #define PERSISTENCE_FILENAME "at2-persistence.dat"

    int persist_objects(const anjay_dm_object_def_t **security_obj,
                        const anjay_dm_object_def_t **server_obj,
                        anjay_attr_storage_t *attr_storage) {
        avs_log(tutorial, INFO, "Persisting objects to %s", PERSISTENCE_FILENAME);

        avs_stream_abstract_t *file_stream =
                avs_stream_file_create(PERSISTENCE_FILENAME, AVS_STREAM_FILE_WRITE);

        if (!file_stream) {
            avs_log(tutorial, ERROR, "Could not open file for writing");
            return -1;
        }

        int result;

        if ((result = anjay_security_object_persist(security_obj, file_stream))) {
            avs_log(tutorial, ERROR, "Could not persist Security Object");
            goto finish;
        }

        if ((result = anjay_server_object_persist(server_obj, file_stream))) {
            avs_log(tutorial, ERROR, "Could not persist Server Object");
            goto finish;
        }

        if ((result = anjay_attr_storage_persist(attr_storage, file_stream))) {
            avs_log(tutorial, ERROR, "Could not persist Attr Storage Object");
            goto finish;
        }

    finish:
        avs_stream_cleanup(&file_stream);
        return result;
    }

.. snippet-source:: examples/tutorial/AT2/src/main.c

    int restore_objects_if_possible(
            const anjay_dm_object_def_t **security_obj,
            const anjay_dm_object_def_t **server_obj,
            anjay_attr_storage_t *attr_storage) {

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

        avs_stream_abstract_t *file_stream =
            avs_stream_file_create(PERSISTENCE_FILENAME, AVS_STREAM_FILE_READ);

        if (!file_stream) {
            return -1;
        }

        if ((result = anjay_security_object_restore(security_obj, file_stream))) {
            avs_log(tutorial, ERROR, "Could not restore Security Object");
            goto finish;
        }

        if ((result = anjay_server_object_restore(server_obj, file_stream))) {
            avs_log(tutorial, ERROR, "Could not restore Server Object");
            goto finish;
        }

        if ((result = anjay_attr_storage_restore(attr_storage, file_stream))) {
            avs_log(tutorial, ERROR, "Could not restore Attr Storage Object");
            goto finish;
        }

    finish:
        avs_stream_cleanup(&file_stream);
        return result;
    }

.. note::
    Persisting as well as restoring functions MUST be both called in the same
    order because objects' data is being stored sequentially.

Persistence API
---------------

Please refer to the `documentation of the Persistence module <../../api/persistence_8h.html>`_.
