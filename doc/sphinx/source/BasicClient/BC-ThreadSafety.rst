..
   Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Thread safety in Anjay
======================

All the examples in this tutorial so far have been single-threaded applications.
Some degree of asynchronicity has been provided through use of the internal
scheduler. However, in some cases, this approach might not be flexible enough.

Starting with Anjay 2.13, the library includes internal provisions for thread
safety. This means that Anjay APIs can be safely called from concurrent threads
and the library will take care of necessary synchronization.

In this tutorial, we will modify the application written in the
:doc:`BC-Notifications` chapter so that the notifications are controlled from a
separate thread instead of a scheduler job.

Making sure that thread safety is enabled
-----------------------------------------

Locking and unlocking mutexes in every public API function takes a sizable
amount of code - this can take even up to 20 KiB of the executable binary when
the library is in a full-featured configuration. For this reason, all the thread
safety features are optional, controlled by a compile-time setting.

This setting is on by default on full-featured operating systems such as Linux
or Windows, and off by default on other platforms.

To control whether Anjay is compiled with thread safety enabled or disabled,
please add the ``-DWITH_THREAD_SAFETY=<ON|OFF>`` to the CMake invocation
command. By default, it will control the thread safety features both in Anjay
API itself, and in the ``avs_sched`` component. The latter can be overridden
using the ``-DWITH_SCHEDULER_THREAD_SAFE=<ON|OFF>`` flag.

If you are compiling Anjay without using CMake, these features are controlled
independently by the ``ANJAY_WITH_THREAD_SAFETY`` flag in ``anjay_config.h``,
and the ``AVS_COMMONS_SCHED_THREAD_SAFE`` flag in ``avs_commons_config.h``,
respectively.

Relying on thread safety of an API while it is not actually assured may result
in critical bugs that could be very hard to debug. If you are writing an
application that depends on the thread safety features of Anjay, you can add a
check like the following to make sure that it is enabled.

.. highlight:: c
.. snippet-source:: examples/tutorial/BC-ThreadSafety/src/main.c

    #if !defined(ANJAY_WITH_THREAD_SAFETY) \
            || !defined(AVS_COMMONS_SCHED_THREAD_SAFE)
    #    error "This example requires Anjay compiled with thread safety enabled"
    #endif // !defined(ANJAY_WITH_THREAD_SAFETY) ||
           // !defined(AVS_COMMONS_SCHED_THREAD_SAFE)

Updating your own code to be thread-safe
----------------------------------------

Even if Anjay APIs have thread safety guarantees, you still need to ensure
thread safety for your own code.

For the purpose of this example, we will use the POSIX Threads API that is
widely available on modern Unix-like systems such as Linux and macOS, as well as
through compatibility layers on other systems, including Windows (e.g. MinGW's
winpthreads), Zephyr and ESP-IDF. Anjay itself is agnostic with regards to the
underlying threading API (see also:
:doc:`../PortingGuideForNonPOSIXPlatforms/ThreadingAPI`), so the same concepts
shall apply on other platforms.

Using the POSIX Threads API requires minor adjustments in the ``CMakeLists.txt``
file so that the application is properly linked with the appropriate threading
library:

.. highlight:: cmake
.. snippet-source:: examples/tutorial/BC-ThreadSafety/CMakeLists.txt
    :caption: CMakeLists.txt
    :emphasize-lines: 5,10,16

    cmake_minimum_required(VERSION 3.1)
    project(anjay-bc-thread-safety C)

    set(CMAKE_C_STANDARD 99)
    set(CMAKE_C_EXTENSIONS ON)

    add_compile_options(-Wall -Wextra)

    find_package(anjay REQUIRED)
    find_package(Threads REQUIRED)

    add_executable(${PROJECT_NAME}
                   src/main.c
                   src/time_object.h
                   src/time_object.c)
    target_link_libraries(${PROJECT_NAME} PRIVATE anjay ${CMAKE_THREAD_LIBS_INIT})

We will update the previous example so that ``time_object_notify()`` will be
called from a different thread than the one running Anjay event loop. This means
that the Time object data structure will be accessed concurrently by both
threads - which means that the Time object implementation itself needs to be
properly guarded by a mutex:

.. highlight:: c
.. snippet-source:: examples/tutorial/BC-ThreadSafety/src/time_object.c
    :caption: time_object.c
    :emphasize-lines: 4,46,77,82,131-137,147,161,172,176,207,232,246,260,269,
                      274,283,288,312-318,326-333,338-344,352,356-357,367,381

    #include <assert.h>
    #include <stdbool.h>

    #include <pthread.h>

    #include <anjay/anjay.h>
    #include <avsystem/commons/avs_defs.h>
    #include <avsystem/commons/avs_list.h>
    #include <avsystem/commons/avs_memory.h>

    #include "time_object.h"

    /**
     * Current Time: RW, Single, Mandatory
     * type: time, range: N/A, unit: N/A
     * Unix Time. A signed integer representing the number of seconds since
     * Jan 1st, 1970 in the UTC time zone.
     */
    #define RID_CURRENT_TIME 5506

    /**
     * Fractional Time: RW, Single, Optional
     * type: float, range: 0..1, unit: s
     * Fractional part of the time when sub-second precision is used (e.g.,
     * 0.23 for 230 ms).
     */
    #define RID_FRACTIONAL_TIME 5507

    /**
     * Application Type: RW, Single, Optional
     * type: string, range: N/A, unit: N/A
     * The application type of the sensor or actuator as a string depending
     * on the use case.
     */
    #define RID_APPLICATION_TYPE 5750

    typedef struct time_instance_struct {
        anjay_iid_t iid;
        char application_type[64];
        char application_type_backup[64];
        int64_t last_notify_timestamp;
    } time_instance_t;

    typedef struct time_object_struct {
        const anjay_dm_object_def_t *def;
        pthread_mutex_t mutex;
        AVS_LIST(time_instance_t) instances;
    } time_object_t;

    static inline time_object_t *
    get_obj(const anjay_dm_object_def_t *const *obj_ptr) {
        assert(obj_ptr);
        return AVS_CONTAINER_OF(obj_ptr, time_object_t, def);
    }

    static time_instance_t *find_instance(const time_object_t *obj,
                                          anjay_iid_t iid) {
        AVS_LIST(time_instance_t) it;
        AVS_LIST_FOREACH(it, obj->instances) {
            if (it->iid == iid) {
                return it;
            } else if (it->iid > iid) {
                break;
            }
        }

        return NULL;
    }

    static int list_instances(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_dm_list_ctx_t *ctx) {
        (void) anjay;
        time_object_t *obj = get_obj(obj_ptr);
        assert(obj);

        pthread_mutex_lock(&obj->mutex);
        AVS_LIST(time_instance_t) it;
        AVS_LIST_FOREACH(it, obj->instances) {
            anjay_dm_emit(ctx, it->iid);
        }
        pthread_mutex_unlock(&obj->mutex);
        return 0;
    }

    static int init_instance(time_instance_t *inst, anjay_iid_t iid) {
        assert(iid != ANJAY_ID_INVALID);

        inst->iid = iid;
        inst->application_type[0] = '\0';

        return 0;
    }

    static void release_instance(time_instance_t *inst) {
        (void) inst;
    }

    static time_instance_t *add_instance(time_object_t *obj, anjay_iid_t iid) {
        assert(find_instance(obj, iid) == NULL);

        AVS_LIST(time_instance_t) created = AVS_LIST_NEW_ELEMENT(time_instance_t);
        if (!created) {
            return NULL;
        }

        int result = init_instance(created, iid);
        if (result) {
            AVS_LIST_CLEAR(&created);
            return NULL;
        }

        AVS_LIST(time_instance_t) *ptr;
        AVS_LIST_FOREACH_PTR(ptr, &obj->instances) {
            if ((*ptr)->iid > created->iid) {
                break;
            }
        }

        AVS_LIST_INSERT(ptr, created);
        return created;
    }

    static int instance_create(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid) {
        (void) anjay;
        time_object_t *obj = get_obj(obj_ptr);
        assert(obj);

        pthread_mutex_lock(&obj->mutex);
        int result = 0;
        if (add_instance(obj, iid)) {
            result = ANJAY_ERR_INTERNAL;
        }
        pthread_mutex_unlock(&obj->mutex);
        return result;
    }

    static int instance_remove(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid) {
        (void) anjay;
        time_object_t *obj = get_obj(obj_ptr);
        assert(obj);

        pthread_mutex_lock(&obj->mutex);
        int result = ANJAY_ERR_NOT_FOUND;
        AVS_LIST(time_instance_t) *it;
        AVS_LIST_FOREACH_PTR(it, &obj->instances) {
            if ((*it)->iid == iid) {
                release_instance(*it);
                AVS_LIST_DELETE(it);
                result = 0;
                break;
            } else if ((*it)->iid > iid) {
                break;
            }
        }
        assert(!result);
        pthread_mutex_unlock(&obj->mutex);
        return result;
    }

    static int instance_reset(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid) {
        (void) anjay;
        time_object_t *obj = get_obj(obj_ptr);
        assert(obj);

        pthread_mutex_lock(&obj->mutex);
        time_instance_t *inst = find_instance(obj, iid);
        assert(inst);
        inst->application_type[0] = '\0';
        pthread_mutex_unlock(&obj->mutex);
        return 0;
    }

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

    static int resource_read(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             anjay_riid_t riid,
                             anjay_output_ctx_t *ctx) {
        (void) anjay;
        time_object_t *obj = get_obj(obj_ptr);
        assert(obj);

        pthread_mutex_lock(&obj->mutex);
        time_instance_t *inst = find_instance(obj, iid);
        assert(inst);
        int result;
        switch (rid) {
        case RID_CURRENT_TIME: {
            assert(riid == ANJAY_ID_INVALID);
            int64_t timestamp;
            if (avs_time_real_to_scalar(&timestamp, AVS_TIME_S,
                                        avs_time_real_now())) {
                result = -1;
            } else {
                result = anjay_ret_i64(ctx, timestamp);
            }
            break;
        }

        case RID_APPLICATION_TYPE:
            assert(riid == ANJAY_ID_INVALID);
            result = anjay_ret_string(ctx, inst->application_type);
            break;

        default:
            result = ANJAY_ERR_METHOD_NOT_ALLOWED;
        }
        pthread_mutex_unlock(&obj->mutex);
        return result;
    }

    static int resource_write(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid,
                              anjay_rid_t rid,
                              anjay_riid_t riid,
                              anjay_input_ctx_t *ctx) {
        (void) anjay;
        time_object_t *obj = get_obj(obj_ptr);
        assert(obj);

        pthread_mutex_lock(&obj->mutex);
        time_instance_t *inst = find_instance(obj, iid);
        assert(inst);
        int result;
        switch (rid) {
        case RID_APPLICATION_TYPE:
            assert(riid == ANJAY_ID_INVALID);
            result = anjay_get_string(ctx, inst->application_type,
                                      sizeof(inst->application_type));
            break;

        default:
            result = ANJAY_ERR_METHOD_NOT_ALLOWED;
        }
        pthread_mutex_unlock(&obj->mutex);
        return result;
    }

    int transaction_begin(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr) {
        (void) anjay;
        time_object_t *obj = get_obj(obj_ptr);

        pthread_mutex_lock(&obj->mutex);
        time_instance_t *element;
        AVS_LIST_FOREACH(element, obj->instances) {
            strcpy(element->application_type_backup, element->application_type);
        }
        pthread_mutex_unlock(&obj->mutex);
        return 0;
    }

    int transaction_rollback(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr) {
        (void) anjay;
        time_object_t *obj = get_obj(obj_ptr);

        pthread_mutex_lock(&obj->mutex);
        time_instance_t *element;
        AVS_LIST_FOREACH(element, obj->instances) {
            strcpy(element->application_type, element->application_type_backup);
        }
        pthread_mutex_unlock(&obj->mutex);
        return 0;
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

    const anjay_dm_object_def_t **time_object_create(void) {
        pthread_mutexattr_t attr;
        if (pthread_mutexattr_init(&attr)) {
            return NULL;
        }
        // anjay_dm_emit() and anjay_dm_emit_res() may call other handlers,
        // so we need a recursive mutex
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

        time_object_t *obj = (time_object_t *) avs_calloc(1, sizeof(time_object_t));
        if (!obj) {
            return NULL;
        }
        obj->def = &OBJ_DEF;

        if (pthread_mutex_init(&obj->mutex, &attr)) {
            pthread_mutexattr_destroy(&attr);
            avs_free(obj);
            return NULL;
        }

        pthread_mutexattr_destroy(&attr);
        pthread_mutex_lock(&obj->mutex);
        time_instance_t *inst = add_instance(obj, 0);
        if (inst) {
            strcpy(inst->application_type, "Clock 0");
        }
        pthread_mutex_unlock(&obj->mutex);

        if (!inst) {
            pthread_mutex_destroy(&obj->mutex);
            avs_free(obj);
            return NULL;
        }

        return &obj->def;
    }

    void time_object_release(const anjay_dm_object_def_t **def) {
        if (def) {
            time_object_t *obj = get_obj(def);
            pthread_mutex_lock(&obj->mutex);
            AVS_LIST_CLEAR(&obj->instances) {
                release_instance(obj->instances);
            }
            pthread_mutex_unlock(&obj->mutex);
            pthread_mutex_destroy(&obj->mutex);
            avs_free(obj);
        }
    }

    void time_object_notify(anjay_t *anjay, const anjay_dm_object_def_t **def) {
        if (!anjay || !def) {
            return;
        }
        time_object_t *obj = get_obj(def);
        pthread_mutex_lock(&obj->mutex);
        int64_t current_timestamp;
        if (!avs_time_real_to_scalar(&current_timestamp, AVS_TIME_S,
                                     avs_time_real_now())) {
            AVS_LIST(time_instance_t) it;
            AVS_LIST_FOREACH(it, obj->instances) {
                if (it->last_notify_timestamp != current_timestamp) {
                    if (!anjay_notify_changed(anjay, 3333, it->iid,
                                              RID_CURRENT_TIME)) {
                        it->last_notify_timestamp = current_timestamp;
                    }
                }
            }
        }
        pthread_mutex_unlock(&obj->mutex);
    }

Most of the relevant changes are highlighted. Please note that some additional
refactoring has been made, mostly to move ``return`` calls out of the blocks
marked by ``pthread_mutex_lock()``/``pthread_mutex_unlock()`` call pairs.

Note that a recursive mutex is used here. This is because data model handlers
may be called recursively from ``anjay_dm_emit()`` and ``anjay_dm_emit_res()``
functions that are used to return data from the ``list_instances`` and
``list_resources`` callbacks. Using a simple mutex instead would result in
deadlocks in those scenarios.

Similar extra caution should be taken when using APIs such as
``anjay_send_batch_data_add_current()`` - that also invokes the relevant data
model callbacks.

Running the event loop in a separate thread
-------------------------------------------

Let's now refactor the ``main.c`` file so that it runs the event loop in a
separate thread - the main one will then be free to call
``time_object_notify()`` periodically:

.. highlight:: c
.. snippet-source:: examples/tutorial/BC-ThreadSafety/src/main.c
    :caption: main.c
    :emphasize-lines: 1-2,11-15,80-84,121-133

    #include <pthread.h>
    #include <unistd.h>

    #include <anjay/anjay.h>
    #include <anjay/security.h>
    #include <anjay/server.h>
    #include <avsystem/commons/avs_log.h>

    #include "time_object.h"

    #if !defined(ANJAY_WITH_THREAD_SAFETY) \
            || !defined(AVS_COMMONS_SCHED_THREAD_SAFE)
    #    error "This example requires Anjay compiled with thread safety enabled"
    #endif // !defined(ANJAY_WITH_THREAD_SAFETY) ||
           // !defined(AVS_COMMONS_SCHED_THREAD_SAFE)

    // Installs Security Object and adds and instance of it.
    // An instance of Security Object provides information needed to connect to
    // LwM2M server.
    static int setup_security_object(anjay_t *anjay) {
        if (anjay_security_object_install(anjay)) {
            return -1;
        }

        static const char PSK_IDENTITY[] = "identity";
        static const char PSK_KEY[] = "P4s$w0rd";

        anjay_security_instance_t security_instance = {
            .ssid = 1,
            .server_uri = "coaps://eu.iot.avsystem.cloud:5684",
            .security_mode = ANJAY_SECURITY_PSK,
            .public_cert_or_psk_identity = (const uint8_t *) PSK_IDENTITY,
            .public_cert_or_psk_identity_size = strlen(PSK_IDENTITY),
            .private_cert_or_psk_key = (const uint8_t *) PSK_KEY,
            .private_cert_or_psk_key_size = strlen(PSK_KEY)
        };

        // Anjay will assign Instance ID automatically
        anjay_iid_t security_instance_id = ANJAY_ID_INVALID;
        if (anjay_security_object_add_instance(anjay, &security_instance,
                                               &security_instance_id)) {
            return -1;
        }

        return 0;
    }

    // Installs Server Object and adds and instance of it.
    // An instance of Server Object provides the data related to a LwM2M Server.
    static int setup_server_object(anjay_t *anjay) {
        if (anjay_server_object_install(anjay)) {
            return -1;
        }

        const anjay_server_instance_t server_instance = {
            // Server Short ID
            .ssid = 1,
            // Client will send Update message often than every 60 seconds
            .lifetime = 60,
            // Disable Default Minimum Period resource
            .default_min_period = -1,
            // Disable Default Maximum Period resource
            .default_max_period = -1,
            // Disable Disable Timeout resource
            .disable_timeout = -1,
            // Sets preferred transport to UDP
            .binding = "U"
        };

        // Anjay will assign Instance ID automatically
        anjay_iid_t server_instance_id = ANJAY_ID_INVALID;
        if (anjay_server_object_add_instance(anjay, &server_instance,
                                             &server_instance_id)) {
            return -1;
        }

        return 0;
    }

    static void *event_loop_func(void *anjay) {
        intptr_t result = anjay_event_loop_run(
                (anjay_t *) anjay, avs_time_duration_from_scalar(100, AVS_TIME_MS));
        return (void *) result;
    }

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
        // Setup necessary objects
        if (setup_security_object(anjay) || setup_server_object(anjay)) {
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

        pthread_t event_loop_thread;
        if (!result) {
            result = pthread_create(&event_loop_thread, NULL, event_loop_func,
                                    anjay);
        }

        if (!result) {
            // Periodically notify the library about Resource value changes
            while (true) {
                sleep(1);
                time_object_notify(anjay, time_object);
            }
        }

        anjay_delete(anjay);
        time_object_release(time_object);
        return result;
    }

Note that ``anjay_event_loop_run()`` and ``time_object_notify()`` (which calls
``anjay_notify_changed()``) are called in concurrent threads without explicit
synchronization. This is entirely permitted as long as thread safety is enabled
in Anjay at compile time.

Please also note that the wait period passed to ``anjay_event_loop_run()`` has
been reduced from 1 second in all previous examples to 100 milliseconds here.
This is to make the application more responsive - ``anjay_notify_changed()``
creates a scheduler job for sending the notification if appropriate; because of
Anjay's limitations, this might not wake up the event loop thread immediately.
Reducing the wait period reduces the time after which the job will actually be
executed.

.. note::

    Complete code of this example can be found in
    `examples/tutorial/BC-ThreadSafety` subdirectory of main Anjay project
    repository.
