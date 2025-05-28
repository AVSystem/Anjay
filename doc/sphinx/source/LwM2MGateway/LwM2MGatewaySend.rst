..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Send method for LwM2M Gateway EIDs
==================================

.. contents:: :local:

Overview
--------

The LwM2M Gateway specification supports the **LwM2M Send method**, allowing
efficient data transmission from End IoT Devices (EIDs). Anjay fully supports
this feature, and it works similarly to the API used for standard LwM2M devices.

Why use the LwM2M Send method?
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

- **Batch Data Transmission** – You can send multiple resource values at once
  without needing Observations.
- **Multi-Device Support** – A single Send message can include data from
  multiple EIDs, reducing communication overhead.
- **Gateway & EID Data Combination** – You can send data from both the LwM2M
  Gateway (Client) and its connected EIDs in one message.

.. note::
   If you're new to the **LwM2M Send method** and how it works in Anjay for
   standard LwM2M clients, read the :doc:`../../BasicClient/BC-Send` tutorial
   first.

LwM2M Send for EIDs vs. standard Devices
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The main difference when using the Send method for EIDs is that the family of
``anjay_send_batch_[data_]add_*`` functions require an extra parameter—the
**EID**. This **EID parameter** is an integer ID assigned when calling
``anjay_lwm2m_gateway_register_device()``,
:ref:`as shown here <lwm2m_gateway_register_device>`.

Send method API comparison
^^^^^^^^^^^^^^^^^^^^^^^^^^

Below is a side-by-side comparison of the original **Send API** and its
counterpart for **LwM2M Gateway EIDs**:

.. list-table::
   :header-rows: 1

   * - Standard Send API
     - Send API for LwM2M Gateway EIDs
   * - ``anjay_send_batch_add_int()``
     - ``anjay_lwm2m_gateway_send_batch_add_int()``
   * - ``anjay_send_batch_add_uint()``
     - ``anjay_lwm2m_gateway_send_batch_add_uint()``
   * - ``anjay_send_batch_add_double()``
     - ``anjay_lwm2m_gateway_send_batch_add_double()``
   * - ``anjay_send_batch_add_bool()``
     - ``anjay_lwm2m_gateway_send_batch_add_bool()``
   * - ``anjay_send_batch_add_string()``
     - ``anjay_lwm2m_gateway_send_batch_add_string()``
   * - ``anjay_send_batch_add_bytes()``
     - ``anjay_lwm2m_gateway_send_batch_add_bytes()``
   * - ``anjay_send_batch_add_objlnk()``
     - ``anjay_lwm2m_gateway_send_batch_add_objlnk()``
   * - ``anjay_send_batch_data_add_current()``
     - ``anjay_lwm2m_gateway_send_batch_data_add_current()``
   * - ``anjay_send_batch_data_add_current_multiple()``
     - ``anjay_lwm2m_gateway_send_batch_data_add_current_multiple()``
   * - :small-literal:`anjay_send_batch_data_add_current_multiple_ignore_not_found()`
     - :small-literal:`anjay_lwm2m_gateway_send_batch_data_add_current_multiple_ignore_not_found()`

These APIs are meant to be used with other standard APIs for Send method, such as:

- ``anjay_send_batch_builder_new()`` – To build batch messages.
- ``anjay_send()`` – To transmit data efficiently.

Example
-------

.. note::
   Complete code of this example can be found in
   ``examples/tutorial/LwM2M-Gateway`` subdirectory of the main
   Anjay project repository.

   For specifics on how this example works, refer to the
   :doc:`LwM2MGatewayAPI` article.

How it works
^^^^^^^^^^^^

This example periodically sends the temperature measured by all EIDs.

The function ``temperature_object_send()`` (defined in ``temperature_object.c``):

- **Creates a new batch** for collecting data.
- **Iterates through the list of EIDs**, fetching their temperature values.
- **Compiles the batch** and schedules it for transmission.

.. highlight:: c
.. snippet-source:: examples/tutorial/LwM2M-Gateway/src/temperature_object.c
    :emphasize-lines: 2, 21-33

    void temperature_object_send(anjay_t *anjay,
                                 AVS_LIST(end_device_t) end_devices) {
        if (!anjay) {
            return;
        }
        const anjay_ssid_t server_ssid = 1;

        if (!end_devices) {
            avs_log(temperature_object, TRACE,
                    "No end devices found, skipping sending data");
            return;
        }

        // Allocate new batch builder.
        anjay_send_batch_builder_t *builder = anjay_send_batch_builder_new();
        if (!builder) {
            avs_log(temperature_object, ERROR, "Failed to allocate batch builder");
            return;
        }

        AVS_LIST(end_device_t) it;
        AVS_LIST_FOREACH(it, end_devices) {
            // Add current values of resource from Temperature Object.
            temperature_object_t *obj = get_obj(it->temperature_object);
            temperature_instance_t *inst = &obj->instances[0];
            if (anjay_lwm2m_gateway_send_batch_data_add_current(
                        builder, anjay, obj->end_device_iid, obj->def->oid,
                        inst->iid, RID_SENSOR_VALUE)) {
                anjay_send_batch_builder_cleanup(&builder);
                avs_log(temperature_object, ERROR, "Failed to add batch data");
                return;
            }
        }

        // After adding all values, compile our batch for sending.
        anjay_send_batch_t *batch = anjay_send_batch_builder_compile(&builder);
        if (!batch) {
            anjay_send_batch_builder_cleanup(&builder);
            avs_log(temperature_object, ERROR, "Batch compile failed");
            return;
        }

        // Schedule our send to be run on next `anjay_sched_run()` call.
        int res =
                anjay_send(anjay, server_ssid, batch, send_finished_handler, NULL);
        if (res) {
            avs_log(temperature_object, ERROR, "Failed to send, result: %d", res);
        }

        // After scheduling, we can release our batch.
        anjay_send_batch_release(&batch);
    }

Differences from standard LwM2M Send
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Compared to the standard **LwM2M Send method** described in the
:doc:`../../BasicClient/BC-Send` tutorial, this implementation includes:

- **Support for Multiple EIDs** – Instead of sending data from a single device,
  this function accepts a list of EIDs and retrieves temperature readings from
  each one.  

- **New API Function with Extra Parameter:** – The function
  ``anjay_lwm2m_gateway_send_batch_data_add_current()`` is used to support EIDs.
  The function takes an additional parameter, ``obj->end_device_iid``, which:
  
  - Specifies the data source (the correct EID).  
  - Encodes a proper URI prefix retrieved internally from Gateway module.  

Scheduling periodic data transmission
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

To ensure temperature readings are **sent at regular intervals**, this function
is called periodically by the scheduler as shown below:

.. highlight:: c
.. snippet-source:: examples/tutorial/LwM2M-Gateway/src/main.c

    // Periodically issues a Send message with measured values of the temperature
    static void send_job(avs_sched_t *sched, const void *args_ptr) {
        gateway_srv_t *gateway_srv = *(gateway_srv_t *const *) args_ptr;

        temperature_object_send(gateway_srv->anjay, gateway_srv->end_devices);

        // Schedule run of the same function after 10 seconds
        AVS_SCHED_DELAYED(sched, NULL,
                        avs_time_duration_from_scalar(10, AVS_TIME_S), send_job,
                        &gateway_srv, sizeof(gateway_srv));
    }

This periodic behavior is set up in ``main()``, just before
``anjay_event_loop_run()`` is called:

.. highlight:: c
.. snippet-source:: examples/tutorial/LwM2M-Gateway/src/main.c
    :emphasize-lines: 5-7

    int main(int argc, char *argv[]) {
        // ...

        if (!result) {
            // Run send_job the first time;
            // this will schedule periodic calls to themselves via the scheduler
            send_job(anjay_get_scheduler(anjay), &gateway_srv_ptr);

            result = anjay_event_loop_run(
                    anjay, avs_time_duration_from_scalar(1, AVS_TIME_S));
        }

        // ...
    }
