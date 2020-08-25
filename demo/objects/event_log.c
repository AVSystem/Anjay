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

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>

#include <anjay/anjay.h>
#include <avsystem/commons/avs_defs.h>
#include <avsystem/commons/avs_memory.h>

#include "../objects.h"

/**
 * LogClass: RW, Single, Optional
 * type: integer, range: 255, unit: N/A
 * Define the Log Event Class: 0: generic (default)  1: system   2:
 * security  3: event   4: trace   5: panic   6: charging [7-99]:
 * reserved [100-255]: vendor specific
 */
#define RID_LOGCLASS 4010

/**
 * LogStart: E, Single, Optional
 * type: N/A, range: N/A, unit: N/A
 * Actions: a) Start data collection(DC) b) LogStatus is set to 0
 * (running) c) DC is emptied (default) or extended according arg'0'
 * value  Arguments definitions are described in the table below.
 */
#define RID_LOGSTART 4011

/**
 * LogStop: E, Single, Optional
 * type: N/A, range: N/A, unit: N/A
 * Actions: a) Stop data collection(DC) b)  1st LSB of LogStatus is set
 * to "1"(stopped) c) DC is kept (default) or emptied according arg'0'
 * value Arguments definitions are described in the table below.
 */
#define RID_LOGSTOP 4012

/**
 * LogStatus: R, Single, Optional
 * type: integer, range: 8-Bits, unit: N/A
 * Data Collection process status: Each bit of this Resource Instance
 * value defines specific status: 1st LSB 0=running, 1=stopped 2nd LSB
 * 1=LogData contains Valid Data 0=LogData doesn’t contain Valid Data 3rd
 * LSB 1=Error occurred during Data Collection 0=No error [4th -7th ]
 * LSB:reserved 8th LSB: vendor specific.
 */
#define RID_LOGSTATUS 4013

/**
 * LogData: R, Single, Mandatory
 * type: opaque, range: N/A, unit: N/A
 * Read Access on that Resource returns the Data Collection associated to
 * the current Object Instance.
 */
#define RID_LOGDATA 4014

/**
 * LogDataFormat: RW, Single, Optional
 * type: integer, range: 255, unit: N/A
 * when set by the Server, this Resource indicates to the Client, what is
 * the Server preferred data format to use when the LogData Resource is
 * returned . when retrieved by the Server, this Resource indicates which
 * specific data format is used when the LogData Resource is returned to
 * the Server  0  or Resource not present : no specific data format
 * (sequence of bytes) 1 : OMA-LwM2M TLV format 2 : OMA-LwM2M JSON format
 * 3:  OMA-LwM2M CBOR format [4..99] reserved [100..255] vendor specific
 * data format
 */
#define RID_LOGDATAFORMAT 4015

#define MIN_LOG_CLASS 0
#define MAX_LOG_CLASS 255

#define MIN_LOG_DATA_FORMAT 0
#define MAX_LOG_DATA_FORMAT 255

typedef struct event_log_struct {
    const anjay_dm_object_def_t *def;

    avs_sched_t *sched;
    avs_sched_handle_t stop_log_job_handle;

    bool log_running;
    bool log_data_valid;
    uint8_t log_class;
    uint8_t log_data[1024];
    size_t log_data_size;
} event_log_t;

static inline event_log_t *
get_obj(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr);
    return AVS_CONTAINER_OF(obj_ptr, event_log_t, def);
}

static int instance_reset(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid) {
    (void) anjay;
    (void) iid;

    event_log_t *obj = get_obj(obj_ptr);
    assert(obj);
    assert(iid == 0);

    obj->log_running = false;
    obj->log_data_valid = false;
    obj->log_class = 0;
    obj->log_data_size = 0;
    avs_sched_del(&obj->stop_log_job_handle);

    return 0;
}

static int list_resources(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid,
                          anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;

    anjay_dm_emit_res(ctx, RID_LOGCLASS, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_LOGSTART, ANJAY_DM_RES_E, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_LOGSTOP, ANJAY_DM_RES_E, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_LOGSTATUS, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_LOGDATA, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    return 0;
}

static int resource_read(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj_ptr,
                         anjay_iid_t iid,
                         anjay_rid_t rid,
                         anjay_riid_t riid,
                         anjay_output_ctx_t *ctx) {
    (void) anjay;
    (void) iid;
    (void) riid;

    event_log_t *obj = get_obj(obj_ptr);
    assert(obj);
    assert(iid == 0);

    switch (rid) {
    case RID_LOGCLASS:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i32(ctx, obj->log_class);

    case RID_LOGSTATUS: {
        assert(riid == ANJAY_ID_INVALID);
        const int32_t status = (obj->log_data_valid << 1) | !obj->log_running;
        return anjay_ret_i32(ctx, status);
    }

    case RID_LOGDATA:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_bytes(ctx, obj->log_data, obj->log_data_size);

    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int resource_write(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid,
                          anjay_rid_t rid,
                          anjay_riid_t riid,
                          anjay_input_ctx_t *ctx) {
    (void) anjay;
    (void) iid;
    (void) riid;

    event_log_t *obj = get_obj(obj_ptr);
    assert(obj);
    assert(iid == 0);

    switch (rid) {
    case RID_LOGCLASS: {
        assert(riid == ANJAY_ID_INVALID);
        int32_t value;
        int retval = anjay_get_i32(ctx, &value);
        if (retval) {
            return retval;
        }
        if (value < MIN_LOG_CLASS || value > MAX_LOG_CLASS) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        obj->log_class = (uint8_t) value;
        return 0;
    }

    case RID_LOGDATAFORMAT: {
        assert(riid == ANJAY_ID_INVALID);
        int32_t value;
        int retval = anjay_get_i32(ctx, &value);
        if (retval) {
            return retval;
        }
        if (value < MIN_LOG_DATA_FORMAT || value > MAX_LOG_DATA_FORMAT) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        return 0;
    }

    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int get_next_argument_value(anjay_execute_ctx_t *arg_ctx,
                                   int32_t *out_number,
                                   int64_t *out_value,
                                   bool *out_has_value) {
    int result = anjay_execute_get_next_arg(arg_ctx, out_number, out_has_value);
    if (result) {
        return result;
    }

    if (!*out_has_value) {
        return 0;
    }

    char value[AVS_INT_STR_BUF_SIZE(int64_t)];
    if ((result = anjay_execute_get_arg_value(arg_ctx, NULL, value,
                                              sizeof(value)))
            < 0) {
        return result;
    }

    int num_read;
    if (sscanf(value, "%" SCNd64 "%n", out_value, &num_read) != 1
            || value[num_read] != '\0' || *out_value < 0) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    return 0;
}

typedef struct {
    event_log_t *event_log_obj;
} disable_log_arg_t;

static void disable_log(avs_sched_t *sched, const void *arg_) {
    (void) sched;
    const disable_log_arg_t *arg = (const disable_log_arg_t *) arg_;
    arg->event_log_obj->log_running = false;
}

static int parse_logstart_arguments(anjay_execute_ctx_t *arg_ctx,
                                    bool *out_clear_log,
                                    int64_t *out_disable_log_delay) {
    // Logs are cleared by default.
    *out_clear_log = true;
    *out_disable_log_delay = 0;
    int32_t arg_number;
    int64_t arg_value;
    bool has_value;
    int result;
    while (!(result = get_next_argument_value(arg_ctx, &arg_number, &arg_value,
                                              &has_value))) {
        if (has_value) {
            switch (arg_number) {
            case 0:
                if (arg_value == 1) {
                    *out_clear_log = false;
                } else if (arg_value) {
                    return ANJAY_ERR_BAD_REQUEST;
                }
                break;

            case 1:
                if (arg_value > 0) {
                    *out_disable_log_delay = arg_value;
                } else if (arg_value < 0) {
                    return ANJAY_ERR_BAD_REQUEST;
                }
                break;

            default:
                return ANJAY_ERR_BAD_REQUEST;
            }
        }
    }
    return (result == ANJAY_EXECUTE_GET_ARG_END) ? 0 : result;
}

static int parse_logstop_arguments(anjay_execute_ctx_t *arg_ctx,
                                   bool *out_clear_log) {
    // Logs are not cleared by default.
    *out_clear_log = false;
    int32_t arg_number;
    int64_t arg_value;
    bool has_value;
    int result;
    while (!(result = get_next_argument_value(arg_ctx, &arg_number, &arg_value,
                                              &has_value))) {
        if (has_value) {
            switch (arg_number) {
            case 0:
                if (arg_value == 1) {
                    *out_clear_log = true;
                } else if (arg_value) {
                    return ANJAY_ERR_BAD_REQUEST;
                }
                break;

            default:
                return ANJAY_ERR_BAD_REQUEST;
            }
        }
    }
    return (result == ANJAY_EXECUTE_GET_ARG_END) ? 0 : result;
}

static int resource_execute(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid,
                            anjay_rid_t rid,
                            anjay_execute_ctx_t *arg_ctx) {
    (void) arg_ctx;
    (void) anjay;
    (void) iid;

    event_log_t *obj = get_obj(obj_ptr);
    assert(obj);
    assert(iid == 0);

    switch (rid) {
    case RID_LOGSTART: {
        bool clear_log;
        int64_t disable_log_delay;
        int result = parse_logstart_arguments(arg_ctx, &clear_log,
                                              &disable_log_delay);
        if (result) {
            return result;
        }
        avs_sched_del(&obj->stop_log_job_handle);
        if (disable_log_delay) {
            if (AVS_SCHED_DELAYED(obj->sched, &obj->stop_log_job_handle,
                                  avs_time_duration_from_scalar(
                                          disable_log_delay, AVS_TIME_S),
                                  disable_log,
                                  &(disable_log_arg_t) {
                                      .event_log_obj = obj
                                  },
                                  sizeof(disable_log_arg_t))) {
                return -1;
            }
        }
        if (clear_log) {
            obj->log_data_size = 0;
        }
        obj->log_running = true;
        return 0;
    }

    case RID_LOGSTOP: {
        bool clear_log;
        int result = parse_logstop_arguments(arg_ctx, &clear_log);
        if (result) {
            return result;
        }
        if (clear_log) {
            obj->log_data_size = 0;
        }
        avs_sched_del(&obj->stop_log_job_handle);
        obj->log_running = false;
        return 0;
    }

    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static const anjay_dm_object_def_t OBJ_DEF = {
    .oid = 20,
    .handlers = {
        .list_instances = anjay_dm_list_instances_SINGLE,
        .instance_reset = instance_reset,

        .list_resources = list_resources,
        .resource_read = resource_read,
        .resource_write = resource_write,
        .resource_execute = resource_execute,

        // TODO: implement these if transactional write/create is required
        .transaction_begin = anjay_dm_transaction_NOOP,
        .transaction_validate = anjay_dm_transaction_NOOP,
        .transaction_commit = anjay_dm_transaction_NOOP,
        .transaction_rollback = anjay_dm_transaction_NOOP
    }
};

const anjay_dm_object_def_t **event_log_object_create(void) {
    event_log_t *obj = (event_log_t *) avs_calloc(1, sizeof(event_log_t));
    if (!obj) {
        return NULL;
    }

    obj->sched = avs_sched_new("eventlog", NULL);
    if (!obj->sched) {
        avs_free(obj);
        return NULL;
    }

    obj->def = &OBJ_DEF;
    return &obj->def;
}

void event_log_object_release(const anjay_dm_object_def_t **def) {
    if (def) {
        event_log_t *obj = get_obj(def);

        avs_sched_cleanup(&obj->sched);
        avs_free(obj);
    }
}

avs_sched_t *event_log_get_sched(const anjay_dm_object_def_t *const *obj_ptr) {
    if (obj_ptr) {
        event_log_t *obj = get_obj(obj_ptr);
        return obj->sched;
    }
    return NULL;
}

int event_log_write_data(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj_ptr,
                         const void *data,
                         size_t data_size) {
    if (!obj_ptr) {
        return -1;
    }

    event_log_t *obj = get_obj(obj_ptr);
    if (sizeof(obj->log_data) < data_size) {
        return -1;
    }

    memcpy(obj->log_data, data, data_size);
    obj->log_data_size = data_size;
    obj->log_data_valid = true;
    const anjay_iid_t iid = 0;
    anjay_notify_changed(anjay, (*obj_ptr)->oid, iid, RID_LOGDATA);
    return 0;
}
