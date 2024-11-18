/*
 * Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */
#include <anjay/anjay_config.h>
#ifdef ANJAY_WITH_LWM2M_GATEWAY

#    include <inttypes.h>

#    include <anjay/anjay.h>
#    include <anjay/lwm2m_gateway.h>

#    include <avsystem/commons/avs_sched.h>
#    include <avsystem/commons/avs_time.h>

#    include "demo_utils.h"
#    include "lwm2m_gateway.h"
#    include "objects/gateway_end_devices/binary_app_data_container.h"
#    include "objects/gateway_end_devices/push_button_object.h"
#    include "objects/gateway_end_devices/temperature_object.h"

static struct end_dev {
    const anjay_dm_object_def_t **push_button_object;
    const anjay_dm_object_def_t **temperature_object;
    const anjay_dm_object_def_t **binary_app_data_container;
    const char *device_id;
    // lwm2m_gateway_setup() sets the IDs equally to this array index, but
    // ANJAY_ID_INVALID set to this field helps determining whether the device
    // is initialized or not
    anjay_iid_t end_dev_iid;
    avs_sched_handle_t notify_job_handle;
} devs[] = {
    {
        .device_id = "urn:dev:001234",
        .end_dev_iid = ANJAY_ID_INVALID
    },
    {
        .device_id = "urn:dev:556789",
        .end_dev_iid = ANJAY_ID_INVALID
    }
};

AVS_STATIC_ASSERT(AVS_ARRAY_SIZE(devs) == LWM2M_GATEWAY_END_DEVICE_COUNT,
                  changing_dev_count_requires_setting_dev_id);
AVS_STATIC_ASSERT(LWM2M_GATEWAY_END_DEVICE_COUNT - 1
                          == LWM2M_GATEWAY_END_DEVICE_RANGE,
                  end_dev_range_equal_to_dev_count_minus_one);

typedef struct {
    anjay_t *anjay;
    struct end_dev *dev;
} notify_job_args_t;

// Periodically notifies the library about Resource value changes
static void notify_job(avs_sched_t *sched, const void *args_ptr) {
    const notify_job_args_t *args = (const notify_job_args_t *) args_ptr;

    temperature_object_update_value(args->anjay, args->dev->temperature_object);

    // Schedule run of the same function after 1 second
    AVS_SCHED_DELAYED(sched, &args->dev->notify_job_handle,
                      avs_time_duration_from_scalar(1, AVS_TIME_S), notify_job,
                      args, sizeof(*args));
}

static struct end_dev *get_dev_ptr(anjay_iid_t end_dev_iid) {
    for (uint16_t i = 0; i < AVS_ARRAY_SIZE(devs); i++) {
        if (devs[i].end_dev_iid == end_dev_iid) {
            return &devs[i];
        }
    }
    return NULL;
}

int lwm2m_gateway_setup_end_device(anjay_t *anjay, anjay_iid_t end_dev_iid) {
    if (end_dev_iid >= LWM2M_GATEWAY_END_DEVICE_COUNT) {
        // invalid iid
        return -1;
    }
    struct end_dev *dev = &devs[end_dev_iid];

    if (dev->end_dev_iid != ANJAY_ID_INVALID) {
        demo_log(ERROR, "End Device id = %" PRIu16 " already registered!",
                 end_dev_iid);
        return -1;
    }

    if (anjay_lwm2m_gateway_register_device(anjay, dev->device_id,
                                            &dev->end_dev_iid)) {
        demo_log(ERROR, "Failed to add End Device id = %" PRIu16, end_dev_iid);
        return -1;
    }

    dev->push_button_object = push_button_object_create(dev->end_dev_iid);
    if (!dev->push_button_object
            || anjay_lwm2m_gateway_register_object(anjay, dev->end_dev_iid,
                                                   dev->push_button_object)) {
        demo_log(ERROR, "Failed to create Push Button Object %" PRIu16,
                 end_dev_iid);
        return -1;
    }

    dev->temperature_object = temperature_object_create(dev->end_dev_iid);
    if (!dev->temperature_object
            || anjay_lwm2m_gateway_register_object(anjay, dev->end_dev_iid,
                                                   dev->temperature_object)) {
        demo_log(ERROR, "Failed to create Temperature Object %" PRIu16,
                 end_dev_iid);
        return -1;
    }

    dev->binary_app_data_container =
            gw_binary_app_data_container_object_create(dev->end_dev_iid);
    if (!dev->binary_app_data_container
            || anjay_lwm2m_gateway_register_object(
                       anjay, dev->end_dev_iid,
                       dev->binary_app_data_container)) {
        demo_log(ERROR,
                 "Failed to create Binary Data Container Object %" PRIu16,
                 end_dev_iid);
        return -1;
    }

    notify_job(anjay_get_scheduler(anjay),
               &(const notify_job_args_t) {
                   .anjay = anjay,
                   .dev = dev
               });
    return 0;
}

void lwm2m_gateway_cleanup_end_device(anjay_t *anjay, anjay_iid_t end_dev_iid) {
    if (end_dev_iid >= LWM2M_GATEWAY_END_DEVICE_COUNT) {
        // invalid iid
        return;
    }
    struct end_dev *dev = &devs[end_dev_iid];

    if (dev->end_dev_iid == ANJAY_ID_INVALID) {
        // device already deregistered
        return;
    }

    if (anjay_lwm2m_gateway_unregister_object(anjay, dev->end_dev_iid,
                                              dev->push_button_object)) {
        AVS_UNREACHABLE("Failed to unregister Time Object");
    }

    if (anjay_lwm2m_gateway_unregister_object(anjay, dev->end_dev_iid,
                                              dev->temperature_object)) {
        AVS_UNREACHABLE("Failed to unregister Temperature Object");
    }

    if (anjay_lwm2m_gateway_unregister_object(anjay, dev->end_dev_iid,
                                              dev->binary_app_data_container)) {
        AVS_UNREACHABLE(
                "Failed to unregister Binary App Data Container Object");
    }

    if (anjay_lwm2m_gateway_deregister_device(anjay, dev->end_dev_iid)) {
        AVS_UNREACHABLE("Failed to deregister End Device");
    }

    avs_sched_del(&dev->notify_job_handle);
    push_button_object_release(dev->push_button_object);
    temperature_object_release(dev->temperature_object);
    gw_binary_app_data_container_object_release(dev->binary_app_data_container);
    dev->end_dev_iid = ANJAY_ID_INVALID;
}

int lwm2m_gateway_setup(anjay_t *anjay) {
    if (anjay_lwm2m_gateway_install(anjay)) {
        demo_log(ERROR, "Failed to add /25 Gateway Object");
        return -1;
    }

    for (anjay_iid_t end_dev_iid = 0; end_dev_iid < AVS_ARRAY_SIZE(devs);
         ++end_dev_iid) {
        if (lwm2m_gateway_setup_end_device(anjay, end_dev_iid)) {
            demo_log(ERROR, "Failed to setup End Device id = %" PRIu16,
                     end_dev_iid);
            return -1;
        }
    }

    return 0;
}

void lwm2m_gateway_cleanup(anjay_t *anjay) {
    for (anjay_iid_t end_dev_iid = 0; end_dev_iid <= AVS_ARRAY_SIZE(devs);
         ++end_dev_iid) {
        lwm2m_gateway_cleanup_end_device(anjay, end_dev_iid);
    }
}

void lwm2m_gateway_press_button_end_device(anjay_t *anjay,
                                           anjay_iid_t end_dev_iid) {
    struct end_dev *dev = get_dev_ptr(end_dev_iid);
    if (!dev) {
        return;
    }
    const anjay_dm_object_def_t **obj = dev->push_button_object;

    push_button_press(anjay, obj);
}

void lwm2m_gateway_release_button_end_device(anjay_t *anjay,
                                             anjay_iid_t end_dev_iid) {
    struct end_dev *dev = get_dev_ptr(end_dev_iid);
    if (!dev) {
        return;
    }
    const anjay_dm_object_def_t **obj = dev->push_button_object;

    push_button_release(anjay, obj);
}

void lwm2m_gateway_binary_app_data_container_write(anjay_t *anjay,
                                                   anjay_iid_t end_dev_iid,
                                                   anjay_iid_t iid,
                                                   anjay_riid_t riid,
                                                   const char *value) {
    struct end_dev *dev = get_dev_ptr(end_dev_iid);
    if (!dev) {
        return;
    }
    const anjay_dm_object_def_t **obj = dev->binary_app_data_container;

    gw_binary_app_data_container_write(anjay, obj, iid, riid, value);
}

#endif // ANJAY_WITH_LWM2M_GATEWAY
