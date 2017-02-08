Custom LwM2M objects
====================

LwM2M Objects are described using the ``anjay_dm_object_def_t`` struct,
which holds:

- Object ID,
- an upper bound on Resource IDs that the Object might contain,
- a set of handlers used by the library to access and possibly modify the Object.

.. highlight:: c
.. snippet-source:: include_public/anjay/anjay.h

    /** A struct defining an LwM2M Object and available operations. */
    struct anjay_dm_object_def_struct {
        /** Object ID */
        anjay_oid_t oid;

        /** Smallest Resource ID that is invalid for this Object. All requests to
         * Resources with ID = @ref anjay_dm_object_def_struct#rid_bound
         * or bigger are discarded without calling the
         * @ref anjay_dm_object_def_struct#resource_present handler. */
        anjay_rid_t rid_bound;

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
        /** Check if a Resource is supported in given Object, @ref anjay_dm_resource_supported_t */
        anjay_dm_resource_supported_t *resource_supported;
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

        /** Perform additional registration operations, @ref anjay_dm_object_on_register_t */
        anjay_dm_object_on_register_t *on_register;

        /** Begin a transaction on this Object, @ref anjay_dm_transaction_begin_t */
        anjay_dm_transaction_begin_t *transaction_begin;
        /** Validate whether a transaction on this Object can be cleanly committed. See @ref anjay_dm_transaction_validate_t */
        anjay_dm_transaction_validate_t *transaction_validate;
        /** Commit changes made in a transaction, @ref anjay_dm_transaction_commit_t */
        anjay_dm_transaction_commit_t *transaction_commit;
        /** Rollback changes made in a transaction, @ref anjay_dm_transaction_rollback_t */
        anjay_dm_transaction_rollback_t *transaction_rollback;
    };

See `API docs <../../api/structanjay__dm__object__def__struct.html>`_ for detailed
information about each of those fields.

The structure itself may seem intimidating at the first glance. In reality, most
use cases will not require setting up all possible handlers - this tutorial
will show multiple possible implementations, from the simplest cases to the most
complex ones.


.. toctree::
   :glob:
   :titlesonly:

   BT4_CustomObject/*

