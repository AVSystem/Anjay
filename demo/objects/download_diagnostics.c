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
#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef enum {
    DOWNLOAD_DIAG_STATE = 0,
    DOWNLOAD_DIAG_URL = 1,
    DOWNLOAD_DIAG_ROM_TIME_US = 2,
    /* begin of transmission time */
    DOWNLOAD_DIAG_BOM_TIME_US = 3,
    /* end of transmission time */
    DOWNLOAD_DIAG_EOM_TIME_US = 4,
    /* total number of bytes transmitted between BOM_TIME and EOM_TIME */
    DOWNLOAD_DIAG_TOTAL_BYTES = 5,
    DOWNLOAD_DIAG_RUN = 6
} download_diag_res_t;

typedef enum {
    DIAG_STATE_NONE = 0,
    DIAG_STATE_REQUESTED = 1,
    DIAG_STATE_COMPLETED = 2,
    DIAG_STATE_TRANSFER_FAILED = 3
} download_diag_state_t;

typedef struct {
    avs_time_real_t beg;
    avs_time_real_t end;
    size_t bytes_received;
} download_diag_stats_t;

typedef struct {
    const anjay_dm_object_def_t *def;
    char download_url[1024];
    anjay_download_handle_t dl_handle;
    download_diag_stats_t stats;
    download_diag_state_t state;
} download_diag_repr_t;

static download_diag_repr_t *
get_diag(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr);
    return AVS_CONTAINER_OF(obj_ptr, download_diag_repr_t, def);
}

static void set_state(anjay_t *anjay,
                      download_diag_repr_t *repr,
                      download_diag_state_t state) {
    (void) anjay;
    if (repr->state != state) {
        repr->state = state;
        anjay_notify_changed(anjay, DEMO_OID_DOWNLOAD_DIAG, 0,
                             DOWNLOAD_DIAG_STATE);
    }
}

static void reset_diagnostic(download_diag_repr_t *repr) {
    memset(repr->download_url, 0, sizeof(repr->download_url));
}

static void update_times(download_diag_repr_t *repr) {
    avs_time_real_t now = avs_time_real_now();
    if (!avs_time_real_valid(repr->stats.beg)) {
        repr->stats.beg = now;
    }
    repr->stats.end = now;
}

static avs_error_t dl_block_callback(anjay_t *anjay,
                                     const uint8_t *data,
                                     size_t data_size,
                                     const anjay_etag_t *etag,
                                     void *user_data) {
    (void) anjay;
    (void) data;
    (void) etag;
    download_diag_repr_t *repr = (download_diag_repr_t *) user_data;
    repr->stats.bytes_received += data_size;
    update_times(repr);
    return AVS_OK;
}

static void dl_finish_callback(anjay_t *anjay,
                               anjay_download_status_t status,
                               void *user_data) {
    download_diag_repr_t *repr = (download_diag_repr_t *) user_data;
    update_times(repr);
    if (status.result == ANJAY_DOWNLOAD_FINISHED) {
        set_state(anjay, repr, DIAG_STATE_COMPLETED);
    } else {
        set_state(anjay, repr, DIAG_STATE_TRANSFER_FAILED);
    }
}

static int diag_download_run(anjay_t *anjay, download_diag_repr_t *repr) {
    if (repr->dl_handle) {
        demo_log(ERROR, "download diagnostic already in progress");
        return -1;
    }

    const anjay_download_config_t config = {
        .url = repr->download_url,
        .on_next_block = dl_block_callback,
        .on_download_finished = dl_finish_callback,
        .user_data = repr
    };
    if (avs_is_err(anjay_download(anjay, &config, &repr->dl_handle))) {
        set_state(anjay, repr, DIAG_STATE_TRANSFER_FAILED);
        demo_log(ERROR, "cannot schedule download diagnostic");
        return -1;
    }
    repr->stats.beg = AVS_TIME_REAL_INVALID;
    repr->stats.end = AVS_TIME_REAL_INVALID;
    repr->stats.bytes_received = 0;
    set_state(anjay, repr, DIAG_STATE_REQUESTED);
    return 0;
}

static int diag_resource_execute(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj_ptr,
                                 anjay_iid_t iid,
                                 anjay_iid_t rid,
                                 anjay_execute_ctx_t *ctx) {
    (void) anjay;
    (void) iid;
    (void) rid;
    (void) ctx;
    assert(rid == DOWNLOAD_DIAG_RUN);
    return diag_download_run(anjay, get_diag(obj_ptr));
}

static int read_stats_resource(anjay_rid_t rid,
                               download_diag_repr_t *repr,
                               anjay_output_ctx_t *ctx) {
    if (repr->state != DIAG_STATE_COMPLETED) {
        return ANJAY_ERR_INTERNAL;
    }

    int64_t tmp;
    switch ((download_diag_res_t) rid) {
    case DOWNLOAD_DIAG_ROM_TIME_US:
    case DOWNLOAD_DIAG_BOM_TIME_US:
        if (avs_time_real_to_scalar(&tmp, AVS_TIME_US, repr->stats.beg)) {
            return ANJAY_ERR_INTERNAL;
        }
        return anjay_ret_i64(ctx, tmp);
    case DOWNLOAD_DIAG_EOM_TIME_US:
        if (avs_time_real_to_scalar(&tmp, AVS_TIME_US, repr->stats.end)) {
            return ANJAY_ERR_INTERNAL;
        }
        return anjay_ret_i64(ctx, tmp);
    case DOWNLOAD_DIAG_TOTAL_BYTES:
        tmp = INT64_MAX;
        if (repr->stats.bytes_received < INT64_MAX) {
            tmp = (int64_t) repr->stats.bytes_received;
        }
        return anjay_ret_i64(ctx, tmp);
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int diag_list_resources(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay;
    (void) iid;

    anjay_dm_emit_res(ctx, DOWNLOAD_DIAG_STATE, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, DOWNLOAD_DIAG_URL, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_resource_presence_t diag_stat_presence =
            (get_diag(obj_ptr)->state == DIAG_STATE_COMPLETED)
                    ? ANJAY_DM_RES_PRESENT
                    : ANJAY_DM_RES_ABSENT;
    anjay_dm_emit_res(ctx, DOWNLOAD_DIAG_ROM_TIME_US, ANJAY_DM_RES_R,
                      diag_stat_presence);
    anjay_dm_emit_res(ctx, DOWNLOAD_DIAG_BOM_TIME_US, ANJAY_DM_RES_R,
                      diag_stat_presence);
    anjay_dm_emit_res(ctx, DOWNLOAD_DIAG_EOM_TIME_US, ANJAY_DM_RES_R,
                      diag_stat_presence);
    anjay_dm_emit_res(ctx, DOWNLOAD_DIAG_TOTAL_BYTES, ANJAY_DM_RES_R,
                      diag_stat_presence);
    anjay_dm_emit_res(ctx, DOWNLOAD_DIAG_RUN, ANJAY_DM_RES_E,
                      ANJAY_DM_RES_PRESENT);
    return 0;
}

static int diag_resource_read(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid,
                              anjay_rid_t rid,
                              anjay_riid_t riid,
                              anjay_output_ctx_t *ctx) {
    (void) anjay;
    (void) iid;
    (void) riid;
    assert(riid == ANJAY_ID_INVALID);
    download_diag_repr_t *repr = get_diag(obj_ptr);
    switch ((download_diag_res_t) rid) {
    case DOWNLOAD_DIAG_STATE:
        return anjay_ret_i32(ctx, (int32_t) repr->state);
    case DOWNLOAD_DIAG_URL:
        return anjay_ret_string(ctx, repr->download_url);
    case DOWNLOAD_DIAG_ROM_TIME_US:
    case DOWNLOAD_DIAG_BOM_TIME_US:
    case DOWNLOAD_DIAG_EOM_TIME_US:
    case DOWNLOAD_DIAG_TOTAL_BYTES:
        return read_stats_resource(rid, repr, ctx);
    default:
        AVS_UNREACHABLE(
                "Read handler called on unknown or non-readable resource");
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int diag_resource_write(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_rid_t rid,
                               anjay_riid_t riid,
                               anjay_input_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;
    (void) riid;
    assert(riid == ANJAY_ID_INVALID);
    download_diag_repr_t *repr = get_diag(obj_ptr);
    switch ((download_diag_res_t) rid) {
    case DOWNLOAD_DIAG_URL: {
        if (repr->state == DIAG_STATE_REQUESTED) {
            demo_log(ERROR,
                     "Cancelling a diagnostic in progress is not supported");
            return ANJAY_ERR_BAD_REQUEST;
        }
        reset_diagnostic(repr);
        int result = anjay_get_string(ctx, repr->download_url,
                                      sizeof(repr->download_url));
        if (result < 0 || result == ANJAY_BUFFER_TOO_SHORT) {
            reset_diagnostic(repr);
            result = result < 0 ? result : ANJAY_ERR_BAD_REQUEST;
        }
        return result;
    }
    default:
        // Bootstrap Server may try to write to other resources,
        // so no AVS_UNREACHABLE() here
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static const anjay_dm_object_def_t DOWNLOAD_DIAG = {
    .oid = DEMO_OID_DOWNLOAD_DIAG,
    .handlers = {
        .list_instances = anjay_dm_list_instances_SINGLE,
        .list_resources = diag_list_resources,
        .resource_execute = diag_resource_execute,
        .resource_read = diag_resource_read,
        .resource_write = diag_resource_write,
        .transaction_begin = anjay_dm_transaction_NOOP,
        .transaction_validate = anjay_dm_transaction_NOOP,
        .transaction_commit = anjay_dm_transaction_NOOP,
        .transaction_rollback = anjay_dm_transaction_NOOP
    }
};

const anjay_dm_object_def_t **download_diagnostics_object_create(void) {
    download_diag_repr_t *repr =
            (download_diag_repr_t *) avs_calloc(1,
                                                sizeof(download_diag_repr_t));
    if (!repr) {
        return NULL;
    }
    repr->def = &DOWNLOAD_DIAG;
    return &repr->def;
}

void download_diagnostics_object_release(const anjay_dm_object_def_t **def) {
    if (def) {
        download_diag_repr_t *repr = get_diag(def);
        assert(!repr->dl_handle);
        avs_free(repr);
    }
}
