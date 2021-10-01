..
   Copyright 2017-2021 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

Send method
===========

.. note::
   A version that includes support for Send method as well as version 1.1 of the
   specification, including Composite operations, SenML JSON and CBOR data
   formats, TCP, SMS and NIDD bindings, is :doc:`available commercially
   <../Commercial_support>`.

The "Send" operation is used by the LwM2M Client to send data to the LwM2M
Server without explicit request by that LwM2M Server. Messages are created using
``anjay_send_batch_builder`` which allows to build a payload with the data to be
sent to the LwM2M Server. Payload can consist of multiple values from different
resources. Calling ``anjay_send()`` does not send batch immediately, but
schedules a task to be run on next iteration of the event loop.

Example
-------

As an example we'll add send method support for the Time Object implemented
previously in :doc:`BC-Notifications` section. It contains Application Type and
Current Time resources, which we will send to the server for demonstration
purposes. We create ``send_finished_handler()`` and ``time_object_send()``
functions in the ``time_object.c`` file.

.. highlight:: c
.. snippet-source:: examples/tutorial/BC-Send/src/time_object.c
    :caption: time_object.c
    :commercial:

    static void send_finished_handler(anjay_t *anjay,
                                    anjay_ssid_t ssid,
                                    const anjay_send_batch_t *batch,
                                    int result,
                                    void *data) {
        (void) anjay;
        (void) ssid;
        (void) batch;
        (void) data;

        if (result != ANJAY_SEND_SUCCESS) {
            avs_log(time_object, ERROR, "Send failed, result: %d", result);
        } else {
            avs_log(time_object, TRACE, "Send successful");
        }
    }

    void time_object_send(anjay_t *anjay, const anjay_dm_object_def_t **def) {
        if (!anjay || !def) {
            return;
        }
        time_object_t *obj = get_obj(def);
        const anjay_ssid_t server_ssid = 1;

        // Allocate new batch builder.
        anjay_send_batch_builder_t *builder = anjay_send_batch_builder_new();

        if (!builder) {
            avs_log(time_object, ERROR, "Failed to allocate batch builder");
            return;
        }

        int res = 0;

        AVS_LIST(time_instance_t) it;
        AVS_LIST_FOREACH(it, obj->instances) {
            // Add current values of resources from Time Object.
            if (anjay_send_batch_data_add_current(builder, anjay, obj->def->oid,
                                                it->iid, RID_CURRENT_TIME)
                    || anjay_send_batch_data_add_current(builder, anjay,
                                                        obj->def->oid, it->iid,
                                                        RID_APPLICATION_TYPE)) {
                anjay_send_batch_builder_cleanup(&builder);
                avs_log(time_object, ERROR, "Failed to add batch data, result: %d",
                        res);
                return;
            }
        }
        // After adding all values, compile our batch for sending.
        anjay_send_batch_t *batch = anjay_send_batch_builder_compile(&builder);

        if (!batch) {
            anjay_send_batch_builder_cleanup(&builder);
            avs_log(time_object, ERROR, "Batch compile failed");
            return;
        }

        // Schedule our send to be run on next `anjay_sched_run()` call.
        res = anjay_send(anjay, server_ssid, batch, send_finished_handler, NULL);

        if (res) {
            avs_log(time_object, ERROR, "Failed to send, result: %d", res);
        }

        // After scheduling, we can release our batch.
        anjay_send_batch_release(&batch);
    }


And include ``anjay/lwm2m_send.h`` and ``<avsystem/commons/avs_log.h>`` in
``time_object.c``.

.. highlight:: c
.. snippet-source:: examples/tutorial/BC-Send/src/time_object.c
    :caption: time_object.c
    :emphasize-lines: 5, 9
    :commercial:

    #include <assert.h>
    #include <stdbool.h>

    #include <anjay/anjay.h>
    #include <anjay/lwm2m_send.h>
    #include <avsystem/commons/avs_defs.h>
    #include <avsystem/commons/avs_list.h>
    #include <avsystem/commons/avs_log.h>
    #include <avsystem/commons/avs_memory.h>

At last, we need to declare the function in the object's header file.

.. highlight:: c
.. snippet-source:: examples/tutorial/BC-Send/src/time_object.h
    :caption: time_object.h
    :emphasize-lines: 9
    :commercial:

    #ifndef TIME_OBJECT_H
    #define TIME_OBJECT_H

    #include <anjay/dm.h>

    const anjay_dm_object_def_t **time_object_create(void);
    void time_object_release(const anjay_dm_object_def_t **def);
    void time_object_notify(anjay_t *anjay, const anjay_dm_object_def_t **def);
    void time_object_send(anjay_t *anjay, const anjay_dm_object_def_t **def);

    #endif // TIME_OBJECT_H

Now we can add another scheduler job that will call this function. In the
example, for test purposes, we create a ``send_job()`` function that will be set
up the same way as ``notify_job()``, but run every 10 seconds.

Please note that the ``notify_job_args_t`` has additionally been renamed to
``time_object_job_args_t`` because it is now shared between ``notify_job()`` and
``send_job()``.

.. highlight:: c
.. snippet-source:: examples/tutorial/BC-Send/src/main.c
    :caption: main.c
    :emphasize-lines: 26-37,145-148
    :commercial:

    #include <anjay/anjay.h>
    #include <anjay/attr_storage.h>
    #include <anjay/security.h>
    #include <anjay/server.h>
    #include <avsystem/commons/avs_log.h>

    #include "time_object.h"

    typedef struct {
        anjay_t *anjay;
        const anjay_dm_object_def_t **time_object;
    } time_object_job_args_t;

    // Periodically notifies the library about Resource value changes
    static void notify_job(avs_sched_t *sched, const void *args_ptr) {
        const time_object_job_args_t *args =
                (const time_object_job_args_t *) args_ptr;

        time_object_notify(args->anjay, args->time_object);

        // Schedule run of the same function after 1 second
        AVS_SCHED_DELAYED(sched, NULL, avs_time_duration_from_scalar(1, AVS_TIME_S),
                          notify_job, args, sizeof(*args));
    }

    // Periodically issues a Send message with application type and current time
    static void send_job(avs_sched_t *sched, const void *args_ptr) {
        const time_object_job_args_t *args =
                (const time_object_job_args_t *) args_ptr;

        time_object_send(args->anjay, args->time_object);

        // Schedule run of the same function after 10 seconds
        AVS_SCHED_DELAYED(sched, NULL,
                          avs_time_duration_from_scalar(10, AVS_TIME_S), send_job,
                          args, sizeof(*args));
    }

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
            .server_uri = "coaps://try-anjay.avsystem.com:5684",
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
            // Run notify_job and send_job the first time;
            // this will schedule periodic calls to themselves via the scheduler
            notify_job(anjay_get_scheduler(anjay), &(const time_object_job_args_t) {
                                                       .anjay = anjay,
                                                       .time_object = time_object
                                                   });
            send_job(anjay_get_scheduler(anjay), &(const time_object_job_args_t) {
                                                     .anjay = anjay,
                                                     .time_object = time_object
                                                 });

            result = anjay_event_loop_run(
                    anjay, avs_time_duration_from_scalar(1, AVS_TIME_S));
        }

        anjay_delete(anjay);
        time_object_release(time_object);
        return result;
    }


That's all you need to make your client support LwM2M Send operation!

.. note::
    Complete code of this example can be found in `examples/tutorial/BC-Send`
    subdirectory of the commercial Anjay release.
