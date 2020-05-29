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

Implementing standard Object
============================

This section describes implementation of a standard Object defined in
`OMA LwM2M Registry <https://www.openmobilealliance.org/wp/omna/lwm2m/lwm2mregistry.html>`_.
If you are interested in implementing your own Objects, please jump to
:doc:`/AdvancedTopics/AT-CustomObjects` section or prepare an Object definition
in XML on your own.

As an example, we are going to implement
`Time Object with ID 3333 <https://www.openmobilealliance.org/tech/profiles/lwm2m/3333.xml>`_.
It is one of the simplest Objects defined in OMA LwM2M Registry, which allows to
understand basics of operations on Objects.

.. note::

   For conformance with the LwM2M specification, the
   `Device Object <https://www.openmobilealliance.org/tech/profiles/LWM2M_Device-v1_0_3.xml>`_
   must be implemented. We are implementing Time Object instead for simplicity,
   because it contains less resources.

This objects contains definition of three resources. They are presented in the
table below with their most important attributes.

+------+------------------+------------+-----------+--------+----------------------------------------------------------------------------------------------------------+
| ID   | Name             | Operations | Mandatory | Type   | Description                                                                                              |
+======+==================+============+===========+========+==========================================================================================================+
| 5506 | Current Time     | RW         | Mandatory | Time   | Unix Time. A signed integer representing the number of seconds since Jan 1st, 1970 in the UTC time zone. |
+------+------------------+------------+-----------+--------+----------------------------------------------------------------------------------------------------------+
| 5507 | Fractional Time  | RW         | Optional  | Float  | Fractional part of the time when sub-second precision is used (e.g., 0.23 for 230 ms).                   |
+------+------------------+------------+-----------+--------+----------------------------------------------------------------------------------------------------------+
| 5750 | Application Type | RW         | Optional  | String | The application type of the sensor or actuator as a string depending on the use case.                    |
+------+------------------+------------+-----------+--------+----------------------------------------------------------------------------------------------------------+

* ID - number used to identify the particular Resource. Different Objects may
  use the same Resource IDs for different purposes.

* Operations - RW indicates, that Resource is Readable and Writable.

* Mandatory - not all Resources defined for standard object must be implemented
  to be compliant with specification. In this case only the Current Time
  resource is mandatory.

Although Current Time and Fractional Time resources are writable, we will not
focus on setting system time and not implement this operation for these two
resources.

Implementing the Object
-----------------------

**Generating base source code**

To generate layout of Object's implementation, we will use the ``anjay_codegen.py``
script, which is bundled with Anjay library. Without going into details, which
are described in :doc:`/Tools` section, we may just call
``./tools/lwm2m_object_registry.py --get-xml 3333 | ./tools/anjay_codegen.py -i - -o time_object.c``
from the Anjay root directory. This command downloads Object's definition from
OMA LwM2M registry and converts it to source code with a lot of TODOs. Now we
are going to replace them with actual code.

**Keeping Instance and Object state**

The actual data of a LwM2M Object sits in its Instances and Resources of that
Instance, so we must have at least one Instance to operate on some real data.

The state of our Time Object Instance will be placed in ``time_instance_t``
struct. The only thing we must keep there is Instance ID (`iid`) and a value of
Application Type Resource, because for Current Time Resource we will be using a
system clock source directly, whenever a read handler is called

Note that there is also a second array for keeping backup of Application Type -
this will be required for implementation of transactions. We will back to it
at the end of this tutorial.

There is also the ``time_object_t`` structure, but we do not need to keep
anything related to the entire Time Object, so we can leave the default
implementation, which just keeps the Object definition and the list of its
instances.

.. highlight:: c
.. snippet-source:: examples/tutorial/BC5/src/time_object.c
    :emphasize-lines: 3-4

    typedef struct time_instance_struct {
        anjay_iid_t iid;
        char application_type[64];
        char application_type_backup[64];
    } time_instance_t;

    typedef struct time_object_struct {
        const anjay_dm_object_def_t *def;
        AVS_LIST(time_instance_t) instances;
    } time_object_t;

**Initializing, releasing and resetting the instance**

Next we have to implement ``init_instance()`` and ``release_instance()``
functions. These functions are using during creation and deletion of instances,
performed by LwM2M Server for example.

In this case, all we have to do is to initialize Application Type with some
value. Since it is a C-string, it is enough to set the first byte to ``\0``.

.. highlight:: c
.. snippet-source:: examples/tutorial/BC5/src/time_object.c
    :emphasize-lines: 5

    static int init_instance(time_instance_t *inst, anjay_iid_t iid) {
        assert(iid != ANJAY_ID_INVALID);

        inst->iid = iid;
        inst->application_type[0] = '\0';

        return 0;
    }

If you decide to allocate the memory for the Application Type dynamically
instead of using fixed-size buffers, then it should be freed in
``release_instance()`` function. In this case, ``release_instance()`` may do
nothing and the default implementation can be left.

The next function to implement is ``instance_reset()``, which should reset
the Instance to its default state, which means the empty Application Type in our
case.

.. highlight:: c
.. snippet-source:: examples/tutorial/BC5/src/time_object.c
    :emphasize-lines: 11

    static int instance_reset(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid) {
        (void) anjay;

        time_object_t *obj = get_obj(obj_ptr);
        assert(obj);
        time_instance_t *inst = find_instance(obj, iid);
        assert(inst);

        inst->application_type[0] = '\0';

        return 0;
    }

We can also disable the presence of one of the Resources in the
``list_resources()`` function. It is done by changing
``ANJAY_DM_RES_PRESENT`` to ``ANJAY_DM_RES_ABSENT`` in the
``anjay_dm_emit_res()`` call. This change will simplify implementation of Read
handler and Observe/Notifications support in the next section.

.. highlight:: c
.. snippet-source:: examples/tutorial/BC5/src/time_object.c
    :emphasize-lines: 11-12

    static int list_resources(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid,
                              anjay_dm_resource_list_ctx_t *ctx) {
        (void) anjay;
        (void) obj_ptr;
        (void) iid;

        anjay_dm_emit_res(ctx, RID_CURRENT_TIME, ANJAY_DM_RES_RW,
                          ANJAY_DM_RES_PRESENT);
        anjay_dm_emit_res(ctx, RID_FRACTIONAL_TIME, ANJAY_DM_RES_RW,
                          ANJAY_DM_RES_ABSENT);
        anjay_dm_emit_res(ctx, RID_APPLICATION_TYPE, ANJAY_DM_RES_RW,
                          ANJAY_DM_RES_PRESENT);
        return 0;
    }

.. note::

   Using ``-r`` command line option in ``anjay_codegen.py`` you can generate
   Object's stub with specified Resources only.

**Read and Write handlers**

Now we are ready to implement ``resource_read()`` and ``resource_write()``
handlers. These handlers will be called every time LwM2M Server performs Read
or Write operation.

We may use ``avs_time_real_now()`` to get the current time.

.. highlight:: c
.. snippet-source:: examples/tutorial/BC5/src/time_object.c
    :emphasize-lines: 15-27

    static int resource_read(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             anjay_riid_t riid,
                             anjay_output_ctx_t *ctx) {
        (void) anjay;

        time_object_t *obj = get_obj(obj_ptr);
        assert(obj);
        time_instance_t *inst = find_instance(obj, iid);
        assert(inst);

        switch (rid) {
        case RID_CURRENT_TIME: {
            assert(riid == ANJAY_ID_INVALID);
            int64_t timestamp;
            if (avs_time_real_to_scalar(&timestamp, AVS_TIME_S,
                                        avs_time_real_now())) {
                return -1;
            }
            return anjay_ret_i64(ctx, timestamp);
        }

        case RID_APPLICATION_TYPE:
            assert(riid == ANJAY_ID_INVALID);
            return anjay_ret_string(ctx, inst->application_type);

        default:
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        }
    }

As you remember, we do not want to set the system time, so Write operation is
allowed only on Application Type resource.

.. highlight:: c
.. snippet-source:: examples/tutorial/BC5/src/time_object.c
    :emphasize-lines: 15-18

    static int resource_write(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid,
                              anjay_rid_t rid,
                              anjay_riid_t riid,
                              anjay_input_ctx_t *ctx) {
        (void) anjay;

        time_object_t *obj = get_obj(obj_ptr);
        assert(obj);
        time_instance_t *inst = find_instance(obj, iid);
        assert(inst);

        switch (rid) {
        case RID_APPLICATION_TYPE:
            assert(riid == ANJAY_ID_INVALID);
            return anjay_get_string(ctx, inst->application_type,
                                    sizeof(inst->application_type));

        default:
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        }
    }

**Initialization of the Object**

There is one function left to implement to have the basic functionality:
``time_object_create()``. By default, there is no Object Instance created, so no
data could be read unless LwM2M Server creates it. However, we are able to
add an Instance right now, by calling ``add_instance()``.

.. highlight:: c
.. snippet-source:: examples/tutorial/BC5/src/time_object.c
    :emphasize-lines: 8-14

    const anjay_dm_object_def_t **time_object_create(void) {
        time_object_t *obj = (time_object_t *) avs_calloc(1, sizeof(time_object_t));
        if (!obj) {
            return NULL;
        }
        obj->def = &OBJ_DEF;

        time_instance_t *inst = add_instance(obj, 0);
        if (inst) {
            strcpy(inst->application_type, "Clock 0");
        } else {
            avs_free(obj);
            return NULL;
        }

        return &obj->def;
    }

Since we do not allocate memory for anything else during object creation, we may
leave the default implementation of ``time_object_release()``, which will remove
the created instance.

.. _registering-objects:

**Registering the Object in Anjay**

The last things to do is to create header file for implemented object, register
it in Anjay and update ``CMakeLists.txt`` file.

.. highlight:: c
.. snippet-source:: examples/tutorial/BC5/src/time_object.h
    :caption: time.h

    #ifndef TIME_OBJECT_H
    #define TIME_OBJECT_H

    #include <anjay/dm.h>

    const anjay_dm_object_def_t **time_object_create(void);
    void time_object_release(const anjay_dm_object_def_t **def);

    #endif // TIME_OBJECT_H

.. highlight:: c
.. snippet-source:: examples/tutorial/BC5/src/main.c
    :caption: main.c
    :emphasize-lines: 27-35,42

    int main(int argc, char *argv[]) {
        if (argc != 2) {
            avs_log(tutorial, ERROR, "usage: %s ENDPOINT_NAME", argv[0]);
            return -1;
        }

        const anjay_configuration_t CONFIG = {
            .endpoint_name = argv[1],
            .in_buffer_size = 4000,
            .out_buffer_size = 4000,
            .msg_cache_size = 4000
        };

        anjay_t *anjay = anjay_new(&CONFIG);
        if (!anjay) {
            avs_log(tutorial, ERROR, "Could not create Anjay object");
            return -1;
        }

        int result = 0;
        // Install Attribute storage and setup necessary objects
        if (anjay_attr_storage_install(anjay) || setup_security_object(anjay)
                || setup_server_object(anjay)) {
            result = -1;
        }

        const anjay_dm_object_def_t **time_object = NULL;
        if (!result) {
            time_object = time_object_create();
            if (time_object) {
                result = anjay_register_object(anjay, time_object);
            } else {
                result = -1;
            }
        }

        if (!result) {
            result = main_loop(anjay);
        }

        anjay_delete(anjay);
        time_object_release(time_object);
        return result;
    }

.. highlight:: cmake
.. snippet-source:: examples/tutorial/BC5/CMakeLists.txt
   :caption: CMakeLists.txt
   :emphasize-lines: 11-12

    cmake_minimum_required(VERSION 3.1)
    project(anjay-bc5 C)

    set(CMAKE_C_STANDARD 99)
    set(CMAKE_C_EXTENSIONS OFF)

    find_package(anjay REQUIRED)

    add_executable(${PROJECT_NAME}
                   src/main.c
                   src/time_object.h
                   src/time_object.c)
    target_link_libraries(${PROJECT_NAME} PRIVATE anjay)

Now the client is ready to be built and connected to LwM2M Server, allowing it
to read the Time object.

.. important::

   Custom objects are not automatically managed by Anjay. Remember to release
   created object **after** deleting the Anjay object.

Supporting transactional writes
-------------------------------

Consider the following scenario: LwM2M Server tries to write to two or more
resources at once. The write on Application Type will probably succeed, but we
are sure, that in this case write on the Current Time will fail. Without
supporting transactions, the entire Write operation will fail, but the
Application Type resource will be changed.

By default, transaction handlers are set to ``anjay_dm_transaction_NOOP``
and do nothing. To properly support Writes on the object implemented in this
tutorial, it is enough to implement only two handlers: ``transaction_begin``,
which makes a backup of Application Type value and ``transaction_rollback``,
which reverts Application Type to its initial value (before Write is performed).
This is why we need ``application_type_backup`` array.

.. highlight:: c
.. snippet-source:: examples/tutorial/BC5/src/time_object.c
    :emphasize-lines: 1-23,37,40

    int transaction_begin(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr) {
        (void) anjay;

        time_object_t *obj = get_obj(obj_ptr);

        time_instance_t *element;
        AVS_LIST_FOREACH(element, obj->instances) {
            strcpy(element->application_type_backup, element->application_type);
        }
    }

    int transaction_rollback(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr) {
        (void) anjay;

        time_object_t *obj = get_obj(obj_ptr);

        time_instance_t *element;
        AVS_LIST_FOREACH(element, obj->instances) {
            strcpy(element->application_type, element->application_type_backup);
        }
    }

    static const anjay_dm_object_def_t OBJ_DEF = {
        .oid = 3333,
        .handlers = {
            .list_instances = list_instances,
            .instance_create = instance_create,
            .instance_remove = instance_remove,
            .instance_reset = instance_reset,

            .list_resources = list_resources,
            .resource_read = resource_read,
            .resource_write = resource_write,

            .transaction_begin = transaction_begin,
            .transaction_validate = anjay_dm_transaction_NOOP,
            .transaction_commit = anjay_dm_transaction_NOOP,
            .transaction_rollback = transaction_rollback
        }
    };
