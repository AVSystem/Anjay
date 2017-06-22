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

Custom LwM2M objects
====================

LwM2M Objects are described using the ``anjay_dm_object_def_t`` struct,
which holds:

- Object ID,
- a list of Resource IDs that the Object might contain,
- a set of handlers used by the library to access and possibly modify the Object.

.. highlight:: c
.. snippet-source:: include_public/anjay/dm.h

    /** A struct containing pointers to Object handlers. */
    typedef struct {
        /** Get default Object attributes, @ref anjay_dm_object_read_default_attrs_t */
        anjay_dm_object_read_default_attrs_t *object_read_default_attrs;
        /** Set default Object attributes, @ref anjay_dm_object_write_default_attrs_t */
        anjay_dm_object_write_default_attrs_t *object_write_default_attrs;

        /** Enumerate available Object Instances, @ref anjay_dm_instance_it_t */
        anjay_dm_instance_it_t *instance_it;
        /** Check if an Object Instance exists, @ref anjay_dm_instance_present_t */
        anjay_dm_instance_present_t *instance_present;

        /** Resets an Object Instance, @ref anjay_dm_instance_reset_t */
        anjay_dm_instance_reset_t *instance_reset;
        /** Create an Object Instance, @ref anjay_dm_instance_create_t */
        anjay_dm_instance_create_t *instance_create;
        /** Delete an Object Instance, @ref anjay_dm_instance_remove_t */
        anjay_dm_instance_remove_t *instance_remove;

        /** Get default Object Instance attributes, @ref anjay_dm_instance_read_default_attrs_t */
        anjay_dm_instance_read_default_attrs_t *instance_read_default_attrs;
        /** Set default Object Instance attributes, @ref anjay_dm_instance_write_default_attrs_t */
        anjay_dm_instance_write_default_attrs_t *instance_write_default_attrs;

        /** Check if a Resource is present in given Object Instance, @ref anjay_dm_resource_present_t */
        anjay_dm_resource_present_t *resource_present;
        /** Returns a mask of supported operations on a given Resource, @ref anjay_dm_resource_operations_t */
        anjay_dm_resource_operations_t *resource_operations;

        /** Get Resource value, @ref anjay_dm_resource_read_t */
        anjay_dm_resource_read_t *resource_read;
        /** Set Resource value, @ref anjay_dm_resource_write_t */
        anjay_dm_resource_write_t *resource_write;
        /** Perform Execute action on a Resource, @ref anjay_dm_resource_execute_t */
        anjay_dm_resource_execute_t *resource_execute;

        /** Get number of Multiple Resource instances, @ref anjay_dm_resource_dim_t */
        anjay_dm_resource_dim_t *resource_dim;
        /** Get Resource attributes, @ref anjay_dm_resource_read_attrs_t */
        anjay_dm_resource_read_attrs_t *resource_read_attrs;
        /** Set Resource attributes, @ref anjay_dm_resource_write_attrs_t */
        anjay_dm_resource_write_attrs_t *resource_write_attrs;

        /** Begin a transaction on this Object, @ref anjay_dm_transaction_begin_t */
        anjay_dm_transaction_begin_t *transaction_begin;
        /** Validate whether a transaction on this Object can be cleanly committed. See @ref anjay_dm_transaction_validate_t */
        anjay_dm_transaction_validate_t *transaction_validate;
        /** Commit changes made in a transaction, @ref anjay_dm_transaction_commit_t */
        anjay_dm_transaction_commit_t *transaction_commit;
        /** Rollback changes made in a transaction, @ref anjay_dm_transaction_rollback_t */
        anjay_dm_transaction_rollback_t *transaction_rollback;
    } anjay_dm_handlers_t;

    /** A simple array-plus-size container for a list of supported Resource IDs. */
    typedef struct {
        /** Number of element in the array */
        size_t count;
        /** Pointer to an array of Resource IDs supported by the object. A Resource
         * is considered SUPPORTED if it may ever be present within the Object. The
         * array MUST be exactly <c>count</c> elements long and sorted in strictly
         * ascending order. */
        const uint16_t *rids;
    } anjay_dm_supported_rids_t;

    // ...
    /**
     * Convenience macro for initializing @ref anjay_dm_supported_rids_t objects.
     *
     * The parameters shall compose a properly sorted list of supported Resource
     * IDs. The result of the macro is an initializer list suitable for initializing
     * an object of type <c>anjay_dm_supported_rids_t</c>, like for example the
     * <c>supported_rids</c> field of @ref anjay_dm_object_def_t. The <c>count</c>
     * field will be automatically calculated.
     */
    #define ANJAY_DM_SUPPORTED_RIDS(...) \
            // ...

    /** A struct defining an LwM2M Object. */
    struct anjay_dm_object_def_struct {
        /** Object ID */
        anjay_oid_t oid;

        /** List of Resource IDs supported by the object. The
         * @ref ANJAY_DM_SUPPORTED_RIDS macro is the preferred way of initializing
         * it. */
        anjay_dm_supported_rids_t supported_rids;

        /** Handler callbacks for this object. */
        anjay_dm_handlers_t handlers;
    };

See `API docs <../../api/structanjay__dm__object__def__struct.html>`_ for detailed
information about each of those fields.

These structures themselves may seem intimidating at the first glance. In
reality, most use cases will not require setting up all possible handlers - this
tutorial will show multiple possible implementations, from the simplest cases to
the most complex ones.


.. toctree::
   :glob:
   :titlesonly:

   BT4_CustomObject/*

