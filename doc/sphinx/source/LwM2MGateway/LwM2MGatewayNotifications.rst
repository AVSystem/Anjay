..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Observe and Notify support for LwM2M Gateway EIDs
=================================================

.. contents:: :local:

Overview
--------

LwM2M **Observe/Notify** operations allow a server to receive updates on
resource values whenever they change or meet specific conditions. In **Anjay**,
it is the user's responsibility to keep resource states up to date for
observations and to notify the library about state changes not triggered by
Write or Create operations.

To ensure that the LwM2M client meets the observation attributes configured
by the server, resource states must be updated frequently enough.

.. note::
   If you're unfamiliar with the **LwM2M Observe/Notify** mechanism and its
   use in Anjay for standard LwM2M clients, read the  
   :doc:`../../BasicClient/BC-Notifications` tutorial first.

How Observe/Notify works
^^^^^^^^^^^^^^^^^^^^^^^^

In most implementations, fulfilling this requirement is straightforward:

- The user **notifies the library** whenever a resource value changes
  (e.g., on every update or at a high frequency).
- The library then decides whether to **send a notification** to the server
  based on the configured observation attributes.
- The user does **not** need to explicitly handle observations or attributes
  in their code.

Observe/Notify for LwM2M Gateway EIDs
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The observation mechanism works the same way for **LwM2M Gateway EIDs** as it
does for regular LwM2M devices. However, three additional APIs are provided:

- ``anjay_lwm2m_gateway_notify_changed``
- ``anjay_lwm2m_gateway_notify_instances_changed``
- ``anjay_lwm2m_gateway_resource_observation_status``

These functions work similarly to their standard LwM2M client counterparts but
require an additional parameter: **the EID**. The EID is an integer ID assigned
when calling ``anjay_lwm2m_gateway_register_device()``,
:ref:`as shown here <lwm2m_gateway_register_device>`.

.. note::
   If an **EID is deregistered**, all active observations on its resources
   are **automatically canceled**.
   As per `RFC 7641 (CoAP Observe), section 4.2
   <https://www.rfc-editor.org/rfc/rfc7641.html#section-4.2>`_, the client
   will respond with **4.04 Not Found** for any observations on a removed EID.

Optimizing notifications for EIDs
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Notifying the library for every resource change is impractical for EIDs.  
Continuously sending updates would cause excessive communication overhead,
which is undesirable for low-power, battery-operated devices.

The following example outlines an optimized approach for handling observations,
specifically for **temperature measurements** using the **Temperature Object**.

The optimization includes:

- **A caching mechanism** to store recently read resource values, reducing
  unnecessary communication with EIDs.
- **Intelligent update logic** that:
  - Limits communication when resources are not observed.
  - Adjusts update frequency based on the ``epmax`` (Maximum Evaluation
  Period) observation attribute.

This approach ensures efficient resource monitoring while minimizing power
and communication costs.

Example
-------

.. note::
   Complete code of this example can be found in
   ``examples/commercial-features/CF-LwM2M-Gateway`` subdirectory of the
   main Anjay project repository.

Cache mechanism
^^^^^^^^^^^^^^^

In Anjay, it is transparent to the user whether a resource read occurs due to:

1. A **Read operation** from the LwM2M Server.
2. A **notification requirement** triggered by the library.

The ``resource_read`` callback must return a recent enough value to be useful.  
A basic approach would be to immediately query the EID for the latest value,
but this can cause unnecessary communication overhead.

To prevent redundant reads, the gateway should periodically read resource values
and notify Anjay of changes using ``anjay_lwm2m_gateway_notify_changed``.  
However, calling this API causes the library to **read the resource again**,
potentially leading to **duplicate queries**.

**Optimizing with a cache**

A caching mechanism can be implemented to store recently read resource values,
reducing unnecessary queries to the EID:

- If a resource read is triggered **twice in a short period** (first to check
  if the value has changed, then to fetch it), the cache prevents **multiple
  EID queries**.
- The cache stores each resource value along with a **timestamp** that
  determines its validity.

Cache data has the following structure:

.. highlight:: c
.. snippet-source:: examples/tutorial/LwM2M-Gateway/src/temperature_object.c
    :emphasize-lines: 1-4, 12-14

    typedef struct cached_value_struct {
        double value;
        avs_time_monotonic_t timestamp;
    } cached_value_t;

    typedef struct temperature_instance_struct {
        anjay_iid_t iid;

        char application_type[10];
        char application_type_backup[10];

        cached_value_t max_meas_cached_value;
        cached_value_t min_meas_cached_value;
        cached_value_t sensor_meas_cached_value;
    } temperature_instance_t;

**Fetching cached values efficiently**

All resource accesses are handled through the cache, using the following
function:

.. highlight:: c
.. snippet-source:: examples/tutorial/LwM2M-Gateway/src/temperature_object.c
    :emphasize-lines: 16-18, 22-26

    static int get_eid_resource_value(temperature_object_t *obj,
                                      anjay_rid_t rid,
                                      cached_value_t *cached_value,
                                      bool force_update) {
        avs_time_monotonic_t current_time = avs_time_monotonic_now();

        if (!force_update) {
            int64_t diff;
            if (avs_time_duration_to_scalar(
                        &diff, AVS_TIME_S,
                        avs_time_monotonic_diff(current_time,
                                                cached_value->timestamp))) {
                return ANJAY_ERR_INTERNAL;
            }

            if (diff < CACHE_VALID_PERIOD_S) {
                return 0;
            }
        }
        int res;
        char buffer[VALUE_MESSAGE_MAX_LEN];
        if ((res = gateway_request(obj->gateway_srv, obj->end_device_iid,
                                   rid_to_request_type(rid), buffer,
                                   VALUE_MESSAGE_MAX_LEN))) {
            return res;
        }

        cached_value->value = atof(buffer);
        cached_value->timestamp = current_time;
        return 0;
    }

This function first checks if the cached value is still valid. If so, it
returns the cached value. Otherwise, it queries the EID, updates the cache,
and returns the fresh value. The cache validity period is defined by the
``CACHE_VALID_PERIOD_S`` constant.

**Using the cache in refresh mechanism**

This caching mechanism is used in both the ``resource_read`` callback and in
the function that periodically refreshes observed resource values:

.. highlight:: c
.. snippet-source:: examples/tutorial/LwM2M-Gateway/src/temperature_object.c
    :emphasize-lines: 20

    static int resource_read(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             anjay_riid_t riid,
                             anjay_output_ctx_t *ctx) {
        (void) anjay;
        assert(riid == ANJAY_ID_INVALID);

        temperature_object_t *obj = get_obj(obj_ptr);
        assert(iid < AVS_ARRAY_SIZE(obj->instances));
        temperature_instance_t *inst = &obj->instances[iid];
        int res;

        switch (rid) {
        case RID_MIN_MEASURED_VALUE:
        case RID_MAX_MEASURED_VALUE:
        case RID_SENSOR_VALUE: {
            cached_value_t *cached_value = rid_to_cached_value(inst, rid);
            res = get_eid_resource_value(obj, rid, cached_value, false);
            return res ? ANJAY_ERR_INTERNAL
                       : anjay_ret_double(ctx, cached_value->value);
        }
        case RID_APPLICATION_TYPE:
            return anjay_ret_string(ctx, inst->application_type);

        default:
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        }
    }

Resource refresh mechanism
^^^^^^^^^^^^^^^^^^^^^^^^^^

The function that refreshes resource values and calls
``anjay_lwm2m_gateway_notify_changed`` follows a structure similar to the
approach described in the :doc:`../../BasicClient/BC-Notifications` tutorial:

.. highlight:: c
.. snippet-source:: examples/tutorial/LwM2M-Gateway/src/gateway_server.c

    static void notify_job(avs_sched_t *sched, const void *args_ptr) {
        const job_args_t *args = (const job_args_t *) args_ptr;

        temperature_object_update_value(args->anjay,
                                        args->end_device->temperature_object);

        AVS_SCHED_DELAYED(sched, &args->end_device->notify_job_handle,
                          avs_time_duration_from_scalar(
                                  args->end_device->evaluation_period, AVS_TIME_S),
                          notify_job, args, sizeof(*args));
    }

**Updating observed resource values**

The ``temperature_object_update_value`` function is responsible for updating the
resource values and notifying the library about the changes. Notice that this
function doesn't refresh values of resources that are not observed:

.. highlight:: c
.. snippet-source:: examples/tutorial/LwM2M-Gateway/src/temperature_object.c
    :emphasize-lines: 5-10

    static void update_resource(anjay_t *anjay,
                                temperature_object_t *obj,
                                temperature_instance_t *inst,
                                anjay_rid_t rid) {
        anjay_resource_observation_status_t status =
                anjay_lwm2m_gateway_resource_observation_status(anjay,
                                                                obj->end_device_iid,
                                                                OID_TEMPERATURE,
                                                                inst->iid, rid);
        if (status.is_observed) {
            cached_value_t *cached_value = rid_to_cached_value(inst, rid);
            double prev_value = cached_value->value;
            get_eid_resource_value(obj, rid, cached_value, true);

            if (prev_value != cached_value->value) {
                anjay_lwm2m_gateway_notify_changed(anjay, obj->end_device_iid,
                                                   OID_TEMPERATURE, inst->iid, rid);
            }
        }
    }

    // ...

    void temperature_object_update_value(anjay_t *anjay,
                                         const anjay_dm_object_def_t **def) {
        assert(anjay);
        temperature_object_t *obj = get_obj(def);

        for (size_t iid = 0; iid < AVS_ARRAY_SIZE(obj->instances); iid++) {
            update_resource(anjay, obj, &obj->instances[iid],
                            RID_MIN_MEASURED_VALUE);
            update_resource(anjay, obj, &obj->instances[iid],
                            RID_MAX_MEASURED_VALUE);
            update_resource(anjay, obj, &obj->instances[iid], RID_SENSOR_VALUE);
        }
    }

**Dynamic adjustment based on epmax**

The periodic refresh of resource values for observed resources is dynamically
adjusted based on the ``epmax`` observation attribute. This attribute defines
the maximum interval between evaluations that determine whether a notification
should be sent.

The mechanism is implemented through a scheduled job that regularly queries
Anjay for a list of currently observed resources. If any of the resources in
this object is observed, the job responsible for refreshing resource values is
rescheduled to run at a higher frequency. Specifically, the interval is set to
the lowest ``epmax`` value among all observed resources.

.. highlight:: c
.. snippet-source:: examples/tutorial/LwM2M-Gateway/src/gateway_server.c
    :emphasize-lines: 15-18

    static void calculate_evaluation_period_job(avs_sched_t *sched,
                                                const void *args_ptr) {
        const job_args_t *args = (const job_args_t *) args_ptr;

        // Schedule run of the same function to track the evaluation period
        // continuously
        AVS_SCHED_DELAYED(sched, &args->end_device->evaluation_period_job_handle,
                          avs_time_duration_from_scalar(EVALUATION_CALC_JOB_PERIOD,
                                                        AVS_TIME_S),
                          calculate_evaluation_period_job, args, sizeof(*args));

        int32_t prev_evaluation_period = args->end_device->evaluation_period;
        int32_t new_evaluation_period = DEFAULT_MAXIMAL_EVALUATION_PERIOD;

        temperature_object_evaluation_period_update_value(
                args->anjay,
                args->end_device->temperature_object,
                &new_evaluation_period);
        if (new_evaluation_period == prev_evaluation_period) {
            return;
        }
        args->end_device->evaluation_period = new_evaluation_period;

        // if evaluation period has changed, notify job should be rescheduled
        // accordingly to new period
        avs_time_monotonic_t new_notify_instant = avs_time_monotonic_add(
                avs_time_monotonic_add(
                        avs_sched_time(&args->end_device->notify_job_handle),
                        avs_time_duration_from_scalar(-prev_evaluation_period,
                                                      AVS_TIME_S)),
                avs_time_duration_from_scalar(new_evaluation_period, AVS_TIME_S));
        AVS_RESCHED_AT(&args->end_device->notify_job_handle, new_notify_instant);
    }

The ``temperature_object_evaluation_period_update_value`` function is
responsible for finding the lowest ``epmax`` value for all observed resources
that depend on the temperature measured by an EID:

.. highlight:: c
.. snippet-source:: examples/tutorial/LwM2M-Gateway/src/temperature_object.c

    static void evaluation_period_update_value(anjay_t *anjay,
                                               temperature_object_t *obj,
                                               temperature_instance_t *inst,
                                               anjay_rid_t rid,
                                               int32_t *max_evaluation_period) {
        anjay_resource_observation_status_t status =
                anjay_lwm2m_gateway_resource_observation_status(anjay,
                                                                obj->end_device_iid,
                                                                OID_TEMPERATURE,
                                                                inst->iid, rid);

        if (status.is_observed && status.max_eval_period != ANJAY_ATTRIB_PERIOD_NONE
                && *max_evaluation_period > status.max_eval_period) {
            *max_evaluation_period = status.max_eval_period;
        }
    }

    void temperature_object_evaluation_period_update_value(
            anjay_t *anjay,
            const anjay_dm_object_def_t **def,
            int32_t *evaluation_period) {
        assert(anjay);
        temperature_object_t *obj = get_obj(def);

        for (size_t iid = 0; iid < AVS_ARRAY_SIZE(obj->instances); iid++) {
            evaluation_period_update_value(anjay, obj, &obj->instances[iid],
                                           RID_MIN_MEASURED_VALUE,
                                           evaluation_period);
            evaluation_period_update_value(anjay, obj, &obj->instances[iid],
                                           RID_MAX_MEASURED_VALUE,
                                           evaluation_period);
            evaluation_period_update_value(anjay, obj, &obj->instances[iid],
                                           RID_SENSOR_VALUE, evaluation_period);
        }
    }

Conclusion
----------

This approach shows how the LwM2M Gateway can efficiently manage
**Observe/Notify** operations for EIDs while keeping communication to a minimum.  
By optimizing when and how updates are sent, devices can stay responsive
without unnecessary data transfers.

However, the best implementation depends on your specific **use case**.  
Several factors should guide your design:

- **Update Frequency** – How often should resource values be refreshed?
- **Real-Time Accuracy** – How critical is immediate data reporting?
- **Power Constraints** – Can the End Device handle frequent updates,
  or does it need to conserve energy?

Developers should **fine-tune** their LwM2M Gateway implementation based on
these factors. The goal is to strike the right balance between **data freshness,
power efficiency, and network optimization**.
