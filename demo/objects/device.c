/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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

#include <avsystem/commons/avs_memory.h>
#include <avsystem/commons/avs_utils.h>

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

    time_t saved_current_time_offset;
    char saved_utc_offset[16];
    char saved_timezone[32];
} dev_repr_t;

static inline dev_repr_t *get_dev(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr);
    return AVS_CONTAINER_OF(obj_ptr, dev_repr_t, def);
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

static int dev_resources(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj_ptr,
                         anjay_iid_t iid,
                         anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;

    anjay_dm_emit_res(ctx, DEV_RES_MANUFACTURER, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, DEV_RES_MODEL_NUMBER, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, DEV_RES_SERIAL_NUMBER, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, DEV_RES_FIRMWARE_VERSION, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, DEV_RES_REBOOT, ANJAY_DM_RES_E,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, DEV_RES_FACTORY_RESET, ANJAY_DM_RES_E,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, DEV_RES_AVAILABLE_POWER_SOURCES, ANJAY_DM_RES_RM,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, DEV_RES_POWER_SOURCE_VOLTAGE, ANJAY_DM_RES_RM,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, DEV_RES_POWER_SOURCE_CURRENT, ANJAY_DM_RES_RM,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, DEV_RES_BATTERY_LEVEL, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, DEV_RES_MEMORY_FREE, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, DEV_RES_ERROR_CODE, ANJAY_DM_RES_RM,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, DEV_RES_CURRENT_TIME, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, DEV_RES_UTC_OFFSET, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, DEV_RES_TIMEZONE, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, DEV_RES_SUPPORTED_BINDING_AND_MODES, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, DEV_RES_DEVICE_TYPE, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, DEV_RES_HARDWARE_VERSION, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, DEV_RES_SOFTWARE_VERSION, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, DEV_RES_BATTERY_STATUS, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, DEV_RES_MEMORY_TOTAL, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, DEV_RES_EXTDEVINFO, ANJAY_DM_RES_RM,
                      ANJAY_DM_RES_PRESENT);
    return 0;
}

static int dev_read(anjay_t *anjay,
                    const anjay_dm_object_def_t *const *obj_ptr,
                    anjay_iid_t iid,
                    anjay_rid_t rid,
                    anjay_riid_t riid,
                    anjay_output_ctx_t *ctx) {
    (void) anjay;
    (void) iid;
    (void) riid;

    dev_repr_t *dev = get_dev(obj_ptr);

    switch (rid) {
    case DEV_RES_MANUFACTURER:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_string(ctx, dev->manufacturer);
    case DEV_RES_MODEL_NUMBER:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_string(ctx, "demo-client");
    case DEV_RES_SERIAL_NUMBER:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_string(ctx, dev->serial_number);
    case DEV_RES_FIRMWARE_VERSION:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_string(ctx, anjay_get_version());
    case DEV_RES_AVAILABLE_POWER_SOURCES:
        assert(riid == 0);
        return anjay_ret_i32(ctx, (int32_t) POWER_SOURCE_DC);
    case DEV_RES_POWER_SOURCE_VOLTAGE:
        assert(riid == 0);
        return anjay_ret_i32(ctx, get_dc_voltage_mv());
    case DEV_RES_POWER_SOURCE_CURRENT:
        assert(riid == 0);
        return anjay_ret_i32(ctx, get_dc_current_ma());
    case DEV_RES_BATTERY_LEVEL:
    case DEV_RES_MEMORY_FREE:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i32(ctx, 0);
    case DEV_RES_ERROR_CODE:
        assert(riid == 0);
        return anjay_ret_i32(ctx, (int32_t) dev->last_error);
    case DEV_RES_CURRENT_TIME:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i64(ctx, avs_time_real_now().since_real_epoch.seconds
                                          + dev->current_time_offset);
    case DEV_RES_UTC_OFFSET:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_string(ctx, dev->utc_offset);
    case DEV_RES_TIMEZONE:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_string(ctx, dev->timezone);
    case DEV_RES_DEVICE_TYPE:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_string(ctx, "");
    case DEV_RES_SUPPORTED_BINDING_AND_MODES:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_string(ctx, "UQ");
    case DEV_RES_EXTDEVINFO:
        assert(riid == 0);
        return anjay_ret_objlnk(ctx, DEMO_OID_EXT_DEV_INFO, 0);
    case DEV_RES_HARDWARE_VERSION:
    case DEV_RES_SOFTWARE_VERSION:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_string(ctx, "");
    case DEV_RES_BATTERY_STATUS:
    case DEV_RES_MEMORY_TOTAL:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i32(ctx, 0);
    default:
        AVS_UNREACHABLE(
                "Read handler called on unknown or non-readable resource");
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
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
                     anjay_riid_t riid,
                     anjay_input_ctx_t *ctx) {
    (void) anjay;
    (void) iid;
    (void) riid;

    dev_repr_t *dev = get_dev(obj_ptr);

    switch (rid) {
    case DEV_RES_CURRENT_TIME: {
        assert(riid == ANJAY_ID_INVALID);
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
        assert(riid == ANJAY_ID_INVALID);
        return read_string(ctx, dev->utc_offset, sizeof(dev->utc_offset));
    case DEV_RES_TIMEZONE:
        assert(riid == ANJAY_ID_INVALID);
        return read_string(ctx, dev->timezone, sizeof(dev->timezone));
    default:
        // Bootstrap Server may try to write to other resources,
        // so no AVS_UNREACHABLE() here
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
        execv(exe_path, argv_get());
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
        AVS_UNREACHABLE("Executable handler called on unknown or "
                        "non-executable resource");
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int dev_resource_instances(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid,
                                  anjay_dm_list_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;

    switch (rid) {
    case DEV_RES_AVAILABLE_POWER_SOURCES:
    case DEV_RES_POWER_SOURCE_VOLTAGE:
    case DEV_RES_POWER_SOURCE_CURRENT:
    case DEV_RES_ERROR_CODE:
    case DEV_RES_EXTDEVINFO:
        anjay_dm_emit(ctx, 0);
        return 0;
    default:
        AVS_UNREACHABLE(
                "Attempted to list instances in a single-instance resource");
        return ANJAY_ERR_INTERNAL;
    }
}

static int dev_transaction_begin(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    dev_repr_t *repr = get_dev(obj_ptr);
    repr->saved_current_time_offset = repr->current_time_offset;
    strcpy(repr->saved_utc_offset, repr->utc_offset);
    strcpy(repr->saved_timezone, repr->timezone);
    return 0;
}

static int
dev_transaction_rollback(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    dev_repr_t *repr = get_dev(obj_ptr);
    repr->current_time_offset = repr->saved_current_time_offset;
    strcpy(repr->utc_offset, repr->saved_utc_offset);
    strcpy(repr->timezone, repr->saved_timezone);
    return 0;
}

static const anjay_dm_object_def_t DEVICE = {
    .oid = DEMO_OID_DEVICE,
    .handlers = {
        .list_instances = anjay_dm_list_instances_SINGLE,
        .instance_reset = dev_instance_reset,
        .list_resources = dev_resources,
        .resource_read = dev_read,
        .resource_write = dev_write,
        .resource_execute = dev_execute,
        .list_resource_instances = dev_resource_instances,
        .transaction_begin = dev_transaction_begin,
        .transaction_validate = anjay_dm_transaction_NOOP,
        .transaction_commit = anjay_dm_transaction_NOOP,
        .transaction_rollback = dev_transaction_rollback
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
        strcpy(out_serial, at);
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

void device_notify_time_dependent(anjay_t *anjay,
                                  const anjay_dm_object_def_t **def) {
    anjay_notify_changed(anjay, (*def)->oid, 0, DEV_RES_POWER_SOURCE_VOLTAGE);
    anjay_notify_changed(anjay, (*def)->oid, 0, DEV_RES_POWER_SOURCE_CURRENT);
    anjay_notify_changed(anjay, (*def)->oid, 0, DEV_RES_CURRENT_TIME);
}
