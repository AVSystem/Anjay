/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "../demo_utils.h"
#include "../objects.h"

#include <sys/param.h>

#include <assert.h>
#include <string.h>
#include <unistd.h>

#include <avsystem/commons/memory.h>
#include <avsystem/commons/utils.h>

#define DEV_RES_MANUFACTURER 0     // string
#define DEV_RES_MODEL_NUMBER 1     // string
#define DEV_RES_SERIAL_NUMBER 2    // string
#define DEV_RES_FIRMWARE_VERSION 3 // string
#define DEV_RES_REBOOT 4
#define DEV_RES_FACTORY_RESET 5
#define DEV_RES_AVAILABLE_POWER_SOURCES 6 // array<int>
#define DEV_RES_POWER_SOURCE_VOLTAGE 7    // array<int>
#define DEV_RES_POWER_SOURCE_CURRENT 8    // array<int>
#define DEV_RES_BATTERY_LEVEL 9           // int
#define DEV_RES_MEMORY_FREE 10            // int
#define DEV_RES_ERROR_CODE 11             // int
#define DEV_RES_RESET_ERROR_CODE 12
#define DEV_RES_CURRENT_TIME 13                // time
#define DEV_RES_UTC_OFFSET 14                  // string
#define DEV_RES_TIMEZONE 15                    // string
#define DEV_RES_SUPPORTED_BINDING_AND_MODES 16 // string
#define DEV_RES_DEVICE_TYPE 17                 // string
#define DEV_RES_HARDWARE_VERSION 18            // string
#define DEV_RES_SOFTWARE_VERSION 19            // string
#define DEV_RES_BATTERY_STATUS 20              // int
#define DEV_RES_MEMORY_TOTAL 21                // int
#define DEV_RES_EXTDEVINFO 22                  // objlnk

typedef enum {
    DEV_ERR_NO_ERROR = 0,
    DEV_ERR_LOW_BATTERY_POWER,
    DEV_ERR_EXTERNAL_POWER_SUPPLY_OFF,
    DEV_ERR_GPS_MODULE_FAILURE,
    DEV_ERR_LOW_RECEIVED_SIGNAL_STRENGTH,
    DEV_ERR_OUT_OF_MEMORY,
    DEV_ERR_SMS_FAILURE,
    DEV_ERR_IP_CONNECTIVITY_FAILURE,
    DEV_ERR_PERIPHERAL_MALFUNCTION
} dev_error_t;

typedef enum {
    POWER_SOURCE_DC = 0,
    POWER_SOURCE_INTERNAL_BATTERY = 1,
    POWER_SOURCE_EXTERNAL_BATTERY = 2,
    POWER_SOURCE_ETHERNET = 3,
    POWER_SOURCE_USB = 4,
    POWER_SOURCE_AC = 5,
    POWER_SOURCE_SOLAR = 6
} power_source_type_t;

typedef struct {
    const anjay_dm_object_def_t *def;
    iosched_t *iosched;
    dev_error_t last_error;

    char manufacturer[256];
    char serial_number[256];

    time_t current_time_offset;
    char utc_offset[16];
    char timezone[32];
} dev_repr_t;

static inline dev_repr_t *get_dev(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr);
    return container_of(obj_ptr, dev_repr_t, def);
}

static int32_t randint_from_range(int32_t min_value, int32_t max_value) {
    assert(min_value <= max_value);

    // RNG-like predictable generation in range [min_value, max_value]
    int32_t diff = max_value - min_value;
    return min_value + (int32_t) time_to_rand() % (diff + 1);
}

static int32_t get_dc_voltage_mv(void) {
    return randint_from_range(32 * 1000 - 500, 32 * 1000 + 500);
}

static int32_t get_dc_current_ma(void) {
    return randint_from_range(10 - 1, 10 + 1);
}

static int dev_read(anjay_t *anjay,
                    const anjay_dm_object_def_t *const *obj_ptr,
                    anjay_iid_t iid,
                    anjay_rid_t rid,
                    anjay_output_ctx_t *ctx) {
    (void) anjay;
    (void) iid;

    dev_repr_t *dev = get_dev(obj_ptr);

    switch (rid) {
    case DEV_RES_MANUFACTURER:
        return anjay_ret_string(ctx, dev->manufacturer);
    case DEV_RES_MODEL_NUMBER:
        return anjay_ret_string(ctx, "demo-client");
    case DEV_RES_SERIAL_NUMBER:
        return anjay_ret_string(ctx, dev->serial_number);
    case DEV_RES_FIRMWARE_VERSION:
        return anjay_ret_string(ctx, anjay_get_version());
    case DEV_RES_AVAILABLE_POWER_SOURCES: {
        anjay_output_ctx_t *array = anjay_ret_array_start(ctx);
        if (!array || anjay_ret_array_index(array, 0)
                || anjay_ret_i32(array, (int32_t) POWER_SOURCE_DC)) {
            return ANJAY_ERR_INTERNAL;
        }
        return anjay_ret_array_finish(array);
    }
    case DEV_RES_POWER_SOURCE_VOLTAGE: {
        anjay_output_ctx_t *array = anjay_ret_array_start(ctx);
        if (!array || anjay_ret_array_index(array, 0)
                || anjay_ret_i32(array, get_dc_voltage_mv())) {
            return ANJAY_ERR_INTERNAL;
        }
        return anjay_ret_array_finish(array);
    }
    case DEV_RES_POWER_SOURCE_CURRENT: {
        anjay_output_ctx_t *array = anjay_ret_array_start(ctx);
        if (!array || anjay_ret_array_index(array, 0)
                || anjay_ret_i32(array, get_dc_current_ma())) {
            return ANJAY_ERR_INTERNAL;
        }
        return anjay_ret_array_finish(array);
    }
    case DEV_RES_BATTERY_LEVEL:
    case DEV_RES_MEMORY_FREE:
        return anjay_ret_i32(ctx, 0);
    case DEV_RES_ERROR_CODE: {
        anjay_output_ctx_t *array = anjay_ret_array_start(ctx);
        if (!array || anjay_ret_array_index(array, 0)
                || anjay_ret_i32(array, (int32_t) dev->last_error)) {
            return ANJAY_ERR_INTERNAL;
        }
        return anjay_ret_array_finish(array);
    }
    case DEV_RES_CURRENT_TIME:
        return anjay_ret_i64(ctx, avs_time_real_now().since_real_epoch.seconds
                                          + dev->current_time_offset);
    case DEV_RES_UTC_OFFSET:
        return anjay_ret_string(ctx, dev->utc_offset);
    case DEV_RES_TIMEZONE:
        return anjay_ret_string(ctx, dev->timezone);
    case DEV_RES_DEVICE_TYPE:
        return anjay_ret_string(ctx, "");
    case DEV_RES_SUPPORTED_BINDING_AND_MODES:
        return anjay_ret_string(ctx, "UQ");
    case DEV_RES_EXTDEVINFO: {
        anjay_output_ctx_t *array = anjay_ret_array_start(ctx);
        if (!array || anjay_ret_array_index(array, 0)
                || anjay_ret_objlnk(array, DEMO_OID_EXT_DEV_INFO, 0)) {
            return ANJAY_ERR_INTERNAL;
        }
        return anjay_ret_array_finish(array);
    }
    case DEV_RES_HARDWARE_VERSION:
    case DEV_RES_SOFTWARE_VERSION:
        return anjay_ret_string(ctx, "");
    case DEV_RES_BATTERY_STATUS:
    case DEV_RES_MEMORY_TOTAL:
        return anjay_ret_i32(ctx, 0);
    case DEV_RES_REBOOT:
    case DEV_RES_FACTORY_RESET:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    default:
        return ANJAY_ERR_NOT_FOUND;
    }
}

static int
read_string(anjay_input_ctx_t *ctx, char *buffer, size_t buffer_size) {
    char *tmp = (char *) avs_malloc(buffer_size);
    if (!tmp) {
        demo_log(ERROR, "Out of memory");
        return -1;
    }
    int result = anjay_get_string(ctx, tmp, buffer_size);
    if (result < 0) {
        goto finish;
    } else if (result == ANJAY_BUFFER_TOO_SHORT) {
        demo_log(DEBUG, "buffer too short to fit full value");
        result = ANJAY_ERR_INTERNAL;
        goto finish;
    }

    memcpy(buffer, tmp, buffer_size);
    result = 0;
finish:
    avs_free(tmp);
    return result;
}

static int dev_write(anjay_t *anjay,
                     const anjay_dm_object_def_t *const *obj_ptr,
                     anjay_iid_t iid,
                     anjay_rid_t rid,
                     anjay_input_ctx_t *ctx) {
    (void) anjay;
    (void) iid;

    dev_repr_t *dev = get_dev(obj_ptr);

    switch (rid) {
    case DEV_RES_CURRENT_TIME: {
        int64_t new_time;
        int result = anjay_get_i64(ctx, &new_time);
        if (result) {
            return result;
        }

        time_t now = time(NULL);
        dev->current_time_offset = (time_t) new_time - now;
        return 0;
    }
    case DEV_RES_UTC_OFFSET:
        return read_string(ctx, dev->utc_offset, sizeof(dev->utc_offset));
    case DEV_RES_TIMEZONE:
        return read_string(ctx, dev->timezone, sizeof(dev->timezone));
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static void dev_instance_reset_impl(dev_repr_t *dev) {
    dev->current_time_offset = 0;
    snprintf(dev->utc_offset, sizeof(dev->utc_offset), "+01:00");
    snprintf(dev->timezone, sizeof(dev->timezone), "Europe/Warsaw");
}

static int dev_instance_reset(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid) {
    (void) anjay;
    (void) iid;

    dev_instance_reset_impl(get_dev(obj_ptr));
    return 0;
}

static void perform_reboot(void *unused) {
    (void) unused;
    char exe_path[256];
#ifdef __APPLE__
    extern int _NSGetExecutablePath(char *buf, uint32_t *bufsize);
    if (_NSGetExecutablePath(exe_path, &(uint32_t) { sizeof(exe_path) })) {
        demo_log(ERROR, "could not get executable path");
    } else
#elif defined(BSD)
    strcpy(exe_path, "/proc/curproc/file");
#else // !__APPLE__ && !BSD
    strcpy(exe_path, "/proc/self/exe");
#endif
    {
        demo_log(INFO, "*** REBOOT ***");
        execv(exe_path, saved_argv);
    }
    demo_log(ERROR, "could not reboot");
}

static int dev_execute(anjay_t *anjay,
                       const anjay_dm_object_def_t *const *obj_ptr,
                       anjay_iid_t iid,
                       anjay_rid_t rid,
                       anjay_execute_ctx_t *ctx) {
    (void) anjay;
    (void) iid;
    (void) ctx;

    dev_repr_t *dev = get_dev(obj_ptr);

    switch (rid) {
    case DEV_RES_REBOOT:
    case DEV_RES_FACTORY_RESET:
        if (!iosched_instant_entry_new(dev->iosched, perform_reboot, NULL,
                                       NULL)) {
            return ANJAY_ERR_INTERNAL;
        }

        return 0;
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int dev_dim(anjay_t *anjay,
                   const anjay_dm_object_def_t *const *obj_ptr,
                   anjay_iid_t iid,
                   anjay_rid_t rid) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;

    switch (rid) {
    case DEV_RES_AVAILABLE_POWER_SOURCES:
    case DEV_RES_POWER_SOURCE_VOLTAGE:
    case DEV_RES_POWER_SOURCE_CURRENT:
    case DEV_RES_ERROR_CODE:
    case DEV_RES_EXTDEVINFO:
        return 1;
    default:
        return ANJAY_DM_DIM_INVALID;
    }
}

static const anjay_dm_object_def_t DEVICE = {
    .oid = DEMO_OID_DEVICE,
    .supported_rids =
            ANJAY_DM_SUPPORTED_RIDS(DEV_RES_MANUFACTURER,
                                    DEV_RES_MODEL_NUMBER,
                                    DEV_RES_SERIAL_NUMBER,
                                    DEV_RES_FIRMWARE_VERSION,
                                    DEV_RES_REBOOT,
                                    DEV_RES_FACTORY_RESET,
                                    DEV_RES_AVAILABLE_POWER_SOURCES,
                                    DEV_RES_POWER_SOURCE_VOLTAGE,
                                    DEV_RES_POWER_SOURCE_CURRENT,
                                    DEV_RES_BATTERY_LEVEL,
                                    DEV_RES_MEMORY_FREE,
                                    DEV_RES_ERROR_CODE,
                                    DEV_RES_CURRENT_TIME,
                                    DEV_RES_UTC_OFFSET,
                                    DEV_RES_TIMEZONE,
                                    DEV_RES_SUPPORTED_BINDING_AND_MODES,
                                    DEV_RES_DEVICE_TYPE,
                                    DEV_RES_HARDWARE_VERSION,
                                    DEV_RES_SOFTWARE_VERSION,
                                    DEV_RES_BATTERY_STATUS,
                                    DEV_RES_MEMORY_TOTAL,
                                    DEV_RES_EXTDEVINFO),
    .handlers = {
        .instance_it = anjay_dm_instance_it_SINGLE,
        .instance_present = anjay_dm_instance_present_SINGLE,
        .instance_reset = dev_instance_reset,
        .resource_present = anjay_dm_resource_present_TRUE,
        .resource_read = dev_read,
        .resource_write = dev_write,
        .resource_execute = dev_execute,
        .resource_dim = dev_dim,
        .transaction_begin = anjay_dm_transaction_NOOP,
        .transaction_validate = anjay_dm_transaction_NOOP,
        .transaction_commit = anjay_dm_transaction_NOOP,
        .transaction_rollback = anjay_dm_transaction_NOOP
    }
};

static void extract_device_info(const char *endpoint_name,
                                char *out_manufacturer,
                                size_t manufacturer_size,
                                char *out_serial,
                                size_t serial_size) {
    int result = 0;
    // skip everything up to (and including) last colon
    // this throws away urn:dev:os: prefix, if any
    const char *at = endpoint_name;
    const char *last_colon = strrchr(at, ':');
    at = last_colon ? last_colon + 1 : at;

    // anything before dash character is used as manufacturer name
    const char *dash = strchr(at, '-');
    if (!dash || at == dash) {
        demo_log(WARNING, "empty manufacturer part of endpoint name");
        result = avs_simple_snprintf(out_manufacturer, manufacturer_size,
                                     "Anjay");
        assert(result >= 0);
    } else {
        AVS_ASSERT((size_t) (dash - at) < manufacturer_size,
                   "manufacturer part of endpoint name too long");
        (void) manufacturer_size;
        strncpy(out_manufacturer, at, (size_t) (dash - at));
        at = dash + 1;
    }

    // everything after the dash becomes the serial number
    size_t serial_length = strlen(at);
    if (serial_length == 0) {
        demo_log(WARNING, "empty serial number part of endpoint name");
        result = avs_simple_snprintf(out_serial, serial_size, "000001");
        assert(result >= 0);
    } else {
        AVS_ASSERT(serial_length < serial_size,
                   "serial number part of endpoint name too long");
        (void) serial_size;
        strncpy(out_serial, at, serial_length);
    }

    demo_log(DEBUG, "manufacturer: %s; serial number: %s", out_manufacturer,
             out_serial);
    (void) result;
}

const anjay_dm_object_def_t **device_object_create(iosched_t *iosched,
                                                   const char *endpoint_name) {
    dev_repr_t *repr = (dev_repr_t *) avs_calloc(1, sizeof(dev_repr_t));
    if (!repr) {
        return NULL;
    }

    repr->def = &DEVICE;
    repr->iosched = iosched;
    repr->last_error = DEV_ERR_NO_ERROR;

    dev_instance_reset_impl(repr);
    extract_device_info(endpoint_name, repr->manufacturer,
                        sizeof(repr->manufacturer), repr->serial_number,
                        sizeof(repr->serial_number));

    return &repr->def;
}

void device_object_release(const anjay_dm_object_def_t **def) {
    if (def) {
        avs_free(get_dev(def));
    }
}
