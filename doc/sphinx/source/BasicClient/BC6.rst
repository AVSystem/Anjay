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

Notifications support
=====================

Some Resources may represent values that change over time, like sensor readings.
A LwM2M Server may be interested in variance of such values and use the Observe
operation to request notifications when they change, or when their value meets
certain criteria.

.. seealso::
    More information about available notification criteria can be found under
    **<NOTIFICATION>** Class Attributes description in :ref:`lwm2m-attributes`.

When some part of the data model changes by means other than LwM2M, one has to
tell the library about it by calling an appropriate function:

- if a Resource value changed - ``anjay_notify_changed()``,
- if one or more Object Instances were created or removed -
  ``anjay_notify_instances_changed()``.

Anjay then decides if the notification shall be sent, based on the currently
assigned Attributes (to the part of the data model being changed) and LwM2M
Servers that are interested in seeing the change.

.. note::
    One should not call ``anjay_notify_changed()``/``anjay_notify_instances_changed()``
    when the value change was directly caused by LwM2M (e.g. by Write or Create
    request). Anjay handles these cases internally.

.. seealso::
    Detailed description of these functions can be found in `API docs
    <../../api>`_.

Calling ``anjay_notify_changed()``/``anjay_notify_instances_changed()`` does not
send notifications immediately, but schedules a task to be run on next
``anjay_sched_run()`` call. That way, notifications for multiple values can be
handled as a batch, for example in case where the server observes an entire
Object Instance.

LwM2M attributes
----------------

Correct handling of LwM2M Observe requests requires being able to store
Object/Instance/Resource attributes. For that, one needs to either implement
a set of attribute handlers, or use the pre-defined
:doc:`Attribute Storage module <../AdvancedTopics/AT-AttributeStorage>`. As
before, we use pre-defined Attribute Storage module here.

Example
-------

As an example we'll add notification support for the Time Object implemented
in previous :doc:`BC5` section. It contains a Current Time Resource, whose value
changes every second. We need to periodically notify the library about that
fact and for this purpose, we create a ``time_object_notify()`` function in
the ``time_object.c`` file, but first, we need to modify ``time_instance_t`` a
little. This will allow to store the last timestamp when the
``anjay_notify_changed()`` was successfully called, to avoid calling it twice
during one second. Although it will not result in sending two notifications with
the same Resource value, because Anjay checks it internally, it will lead to
perform some unnecessary actions (like calling read handler for example).

.. highlight:: c
.. snippet-source:: examples/tutorial/BC6/src/time_object.c
    :emphasize-lines: 5

    typedef struct time_instance_struct {
        anjay_iid_t iid;
        char application_type[64];
        char application_type_backup[64];
        uint64_t last_notify_timestamp;
    } time_instance_t;

.. highlight:: c
.. snippet-source:: examples/tutorial/BC6/src/time_object.c

    void time_object_notify(anjay_t *anjay, const anjay_dm_object_def_t **def) {
        if (!anjay || !def) {
            return;
        }
        time_object_t *obj = get_obj(def);

        uint64_t current_timestamp;
        if (avs_time_real_to_scalar(&current_timestamp, AVS_TIME_S,
                                    avs_time_real_now())) {
            return;
        }

        AVS_LIST(time_instance_t) it;
        AVS_LIST_FOREACH(it, obj->instances) {
            if (it->last_notify_timestamp != current_timestamp) {
                if (!anjay_notify_changed(anjay, 3333, it->iid, RID_CURRENT_TIME)) {
                    it->last_notify_timestamp = current_timestamp;
                }
            }
        }
    }

At last, we need to declare the function in the object's header file.

.. highlight:: c
.. snippet-source:: examples/tutorial/BC6/src/time_object.h
    :caption: time_object.h
    :emphasize-lines: 8

    #ifndef TIME_OBJECT_H
    #define TIME_OBJECT_H

    #include <anjay/dm.h>

    const anjay_dm_object_def_t **time_object_create(void);
    void time_object_release(const anjay_dm_object_def_t **def);
    void time_object_notify(anjay_t *anjay, const anjay_dm_object_def_t **def);

    #endif // TIME_OBJECT_H

Now we can call this function in our ``main_loop()`` function. Note that it will
be called every second or even more frequently if Anjay has some task scheduled,
but we added protection from calling ``anjay_notify_changed()`` if Time Resource
value was not changed.

.. highlight:: c
.. snippet-source:: examples/tutorial/BC6/src/main.c
    :caption: main.c
    :emphasize-lines: 11,49-50,158

    #include <anjay/anjay.h>
    #include <anjay/attr_storage.h>
    #include <anjay/security.h>
    #include <anjay/server.h>
    #include <avsystem/commons/avs_log.h>

    #include <poll.h>

    #include "time_object.h"

    int main_loop(anjay_t *anjay, const anjay_dm_object_def_t **time_object) {
        while (true) {
            // Obtain all network data sources
            AVS_LIST(avs_net_socket_t *const) sockets = anjay_get_sockets(anjay);

            // Prepare to poll() on them
            size_t numsocks = AVS_LIST_SIZE(sockets);
            struct pollfd pollfds[numsocks];
            size_t i = 0;
            AVS_LIST(avs_net_socket_t *const) sock;
            AVS_LIST_FOREACH(sock, sockets) {
                pollfds[i].fd = *(const int *) avs_net_socket_get_system(*sock);
                pollfds[i].events = POLLIN;
                pollfds[i].revents = 0;
                ++i;
            }

            const int max_wait_time_ms = 1000;
            // Determine the expected time to the next job in milliseconds.
            // If there is no job we will wait till something arrives for
            // at most 1 second (i.e. max_wait_time_ms).
            int wait_ms =
                    anjay_sched_calculate_wait_time_ms(anjay, max_wait_time_ms);

            // Wait for the events if necessary, and handle them.
            if (poll(pollfds, numsocks, wait_ms) > 0) {
                int socket_id = 0;
                AVS_LIST(avs_net_socket_t *const) socket = NULL;
                AVS_LIST_FOREACH(socket, sockets) {
                    if (pollfds[socket_id].revents) {
                        if (anjay_serve(anjay, *socket)) {
                            avs_log(tutorial, ERROR, "anjay_serve failed");
                        }
                    }
                    ++socket_id;
                }
            }

            // Notify the library about a Resource value change
            time_object_notify(anjay, time_object);

            // Finally run the scheduler
            anjay_sched_run(anjay);
        }
        return 0;
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
            result = main_loop(anjay, time_object);
        }

        anjay_delete(anjay);
        time_object_release(time_object);
        return result;
    }

That's all you need to make your client support LwM2M Observe/Notify operations!

.. note::

    Complete code of this example can be found in `examples/tutorial/BC6`
    subdirectory of main Anjay project repository.
