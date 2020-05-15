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

Custom LwM2M objects
====================

.. include:: AT-CustomObjects/Anjay_codegen_note.rst

LwM2M Objects are described using the ``anjay_dm_object_def_t`` struct,
which holds:

- Object ID,
- a list of Resource IDs that the Object might contain,
- a set of handlers used by the library to access and possibly modify the Object.

.. highlight:: c
.. snippet-source:: include_public/anjay/dm.h

    /** A struct containing pointers to Object handlers. */
    typedef struct {
        /**
         * Get default Object attributes, @ref anjay_dm_object_read_default_attrs_t
         *
         * Required for handling *LwM2M Discover* and *LwM2M Observe* operations.
         *
         * Can be NULL when *Attribute Storage* module is installed. Non-NULL
         * handler overrides *Attribute Storage* logic.
         */
        anjay_dm_object_read_default_attrs_t *object_read_default_attrs;

        /**
         * Set default Object attributes,
         * @ref anjay_dm_object_write_default_attrs_t
         *
         * Required for handling *LwM2M Write-Attributes* operation.
         *
         * Can be NULL when *Attribute Storage* module is installed. Non-NULL
         * handler overrides *Attribute Storage* logic.
         */
        anjay_dm_object_write_default_attrs_t *object_write_default_attrs;

        /**
         * Enumerate available Object Instances, @ref anjay_dm_list_instances_t
         *
         * Required for every LwM2M operation.
         *
         * **Must not be NULL.** @ref anjay_dm_list_instances_SINGLE can be used
         * here.
         */
        anjay_dm_list_instances_t *list_instances;

        /**
         * Resets an Object Instance, @ref anjay_dm_instance_reset_t
         *
         * Required for handling *LwM2M Write* operation in *replace mode*.
         *
         * Can be NULL if the object does not contain writable resources.
         */
        anjay_dm_instance_reset_t *instance_reset;

        /**
         * Create an Object Instance, @ref anjay_dm_instance_create_t
         *
         * Required for handling *LwM2M Create* operation.
         *
         * Can be NULL for single instance objects.
         */
        anjay_dm_instance_create_t *instance_create;

        /**
         * Delete an Object Instance, @ref anjay_dm_instance_remove_t
         *
         * Required for handling *LwM2M Delete* operation.
         *
         * Can be NULL for single instance objects.
         */
        anjay_dm_instance_remove_t *instance_remove;

        /**
         * Get default Object Instance attributes,
         * @ref anjay_dm_instance_read_default_attrs_t
         *
         * Required for handling *LwM2M Discover* and *LwM2M Observe* operations.
         *
         * Can be NULL when *Attribute Storage* module is installed. Non-NULL
         * handler overrides *Attribute Storage* logic.
         */
        anjay_dm_instance_read_default_attrs_t *instance_read_default_attrs;

        /**
         * Set default Object Instance attributes,
         * @ref anjay_dm_instance_write_default_attrs_t
         *
         * Required for handling *LwM2M Write-Attributes* operation.
         *
         * Can be NULL when *Attribute Storage* module is installed. Non-NULL
         * handler overrides *Attribute Storage* logic.
         */
        anjay_dm_instance_write_default_attrs_t *instance_write_default_attrs;

        /**
         * Enumerate PRESENT Resources in a given Object Instance,
         * @ref anjay_dm_list_resources_t
         *
         * Required for every LwM2M operation.
         *
         * **Must not be NULL.**
         */
        anjay_dm_list_resources_t *list_resources;

        /**
         * Get Resource value, @ref anjay_dm_resource_read_t
         *
         * Required for *LwM2M Read* operation.
         *
         * Can be NULL if the object does not contain readable resources.
         */
        anjay_dm_resource_read_t *resource_read;

        /**
         * Set Resource value, @ref anjay_dm_resource_write_t
         *
         * Required for *LwM2M Write* operation.
         *
         * Can be NULL if the object does not contain writable resources.
         */
        anjay_dm_resource_write_t *resource_write;

        /**
         * Perform Execute action on a Resource, @ref anjay_dm_resource_execute_t
         *
         * Required for *LwM2M Execute* operation.
         *
         * Can be NULL if the object does not contain executable resources.
         */
        anjay_dm_resource_execute_t *resource_execute;

        /**
         * Remove all Resource Instances from a Multiple Resource,
         * @ref anjay_dm_resource_reset_t
         *
         * Required for *LwM2M Write* operation performed on multiple-instance
         * resources.
         *
         * Can be NULL if the object does not contain multiple writable resources.
         */
        anjay_dm_resource_reset_t *resource_reset;

        /**
         * Enumerate available Resource Instances,
         * @ref anjay_dm_list_resource_instances_t
         *
         * Required for *LwM2M Read*, *LwM2M Write* and *LwM2M Discover* operations
         * performed on multiple-instance resources..
         *
         * Can be NULL if the object does not contain multiple resources.
         */
        anjay_dm_list_resource_instances_t *list_resource_instances;

        /**
         * Get Resource attributes, @ref anjay_dm_resource_read_attrs_t
         *
         * Required for handling *LwM2M Discover* and *LwM2M Observe* operations.
         *
         * Can be NULL when *Attribute Storage* module is installed. Non-NULL
         * handler overrides *Attribute Storage* logic.
         */
        anjay_dm_resource_read_attrs_t *resource_read_attrs;

        /**
         * Set Resource attributes, @ref anjay_dm_resource_write_attrs_t
         *
         * Required for handling *LwM2M Write-Attributes* operation.
         *
         * Can be NULL when *Attribute Storage* module is installed. Non-NULL
         * handler overrides *Attribute Storage* logic.
         */
        anjay_dm_resource_write_attrs_t *resource_write_attrs;

        /**
         * Begin a transaction on this Object, @ref anjay_dm_transaction_begin_t
         *
         * Required for handling modifying operation: *LwM2M Write*, *LwM2M Create*
         * or *LwM2M Delete*.
         *
         * Can be NULL for read-only objects. @ref anjay_dm_transaction_NOOP can be
         * used here.
         */
        anjay_dm_transaction_begin_t *transaction_begin;

        /**
         * Validate whether a transaction on this Object can be cleanly committed.
         * See @ref anjay_dm_transaction_validate_t
         *
         * Required for handling modifying operation: *LwM2M Write*, *LwM2M Create*
         * or *LwM2M Delete*.
         *
         * Can be NULL for read-only objects. @ref anjay_dm_transaction_NOOP can be
         * used here.
         */
        anjay_dm_transaction_validate_t *transaction_validate;

        /**
         * Commit changes made in a transaction, @ref anjay_dm_transaction_commit_t
         *
         * Required for handling modifying operation: *LwM2M Write*, *LwM2M Create*
         * or *LwM2M Delete*.
         *
         * Can be NULL for read-only objects. @ref anjay_dm_transaction_NOOP can be
         * used here.
         */
        anjay_dm_transaction_commit_t *transaction_commit;

        /**
         * Rollback changes made in a transaction,
         * @ref anjay_dm_transaction_rollback_t
         *
         * Required for handling modifying operation: *LwM2M Write*, *LwM2M Create*
         * or *LwM2M Delete*.
         *
         * Can be NULL for read-only objects. @ref anjay_dm_transaction_NOOP can be
         * used here.
         */
        anjay_dm_transaction_rollback_t *transaction_rollback;

    } anjay_dm_handlers_t;

    /** A struct defining a LwM2M Object. */
    struct anjay_dm_object_def_struct {
        /** Object ID; MUST not be <c>ANJAY_ID_INVALID</c> (65535) */
        anjay_oid_t oid;

        /**
         * Object version: a string with static lifetime, containing two digits
         * separated by a dot (for example: "1.1").
         * If left NULL, client will not include the "ver=" attribute in Register
         * and Discover messages, which implies version 1.0.
         */
        const char *version;

        /** Handler callbacks for this object. */
        anjay_dm_handlers_t handlers;
    };

See `API docs <../api/structanjay__dm__object__def__struct.html>`_ for detailed
information about each of those fields.

These structures themselves may seem intimidating at the first glance. In
reality, most use cases will not require setting up all possible handlers - this
tutorial will show multiple possible implementations, from the simplest cases to
the most complex ones.

.. toctree::
   :glob:
   :titlesonly:

   AT-CustomObjects/AT_*
