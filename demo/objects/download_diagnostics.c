/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
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

#include "../objects.h"
#include "../utils.h"
#include "../wget_downloader.h"
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <time.h>

#define TEMP_FILE_FMT       "/tmp/diag-XXXXXX"
#define DOWNLOAD_DIAG_OID   12361

typedef enum {
    DOWNLOAD_DIAG_STATE         = 0,
    DOWNLOAD_DIAG_URL           = 1,
    DOWNLOAD_DIAG_ROM_TIME_US   = 2,
    /* begin of transmision time */
    DOWNLOAD_DIAG_BOM_TIME_US   = 3,
    /* end of transmission time */
    DOWNLOAD_DIAG_EOM_TIME_US   = 4,
    /* total number of bytes transmitted between BOM_TIME and EOM_TIME */
    DOWNLOAD_DIAG_TOTAL_BYTES   = 5,
    DOWNLOAD_DIAG_RUN           = 6
} download_diag_res_t;
#define DOWNLOAD_DIAG_RID_BOUND_ 7

typedef enum {
    DIAG_STATE_NONE             = 0,
    DIAG_STATE_REQUESTED        = 1,
    DIAG_STATE_COMPLETED        = 2,
    DIAG_STATE_TRANSFER_FAILED  = 3
} download_diag_state_t;


typedef struct {
    const anjay_dm_object_def_t *def;
    char download_url[1024];
    char temp_file[sizeof(TEMP_FILE_FMT)];
    wget_context_t *wget_ctx;
    wget_download_stats_t stats;
    download_diag_state_t state;
} download_diag_repr_t;

static int64_t timespec_as_us(struct timespec time) {
    // It should be enough for 292471 years from epoch time.
    return (int64_t) time.tv_sec * 1000000LL + time.tv_nsec / 1000;
}

static download_diag_repr_t *get_diag(const anjay_dm_object_def_t *const *obj_ptr) {
    if (!obj_ptr) {
        return NULL;
    }
    return container_of(obj_ptr, download_diag_repr_t, def);
}

static int make_temp_file(char *out_name) {
    int fd = mkstemp(out_name);
    if (fd == -1) {
        return -1;
    }
    close(fd);
    return 0;
}

static void set_state(anjay_t *anjay,
                      download_diag_repr_t *repr,
                      download_diag_state_t state) {
    (void) anjay;
    if (repr->state != state) {
        repr->state = state;
        anjay_notify_changed(anjay, DOWNLOAD_DIAG_OID, 0,
                             DOWNLOAD_DIAG_STATE);
    }
}

typedef struct {
    download_diag_repr_t *repr;
    anjay_t *anjay;
} wget_args_t;

static void reset_diagnostic(download_diag_repr_t *repr) {
    memset(repr->download_url, 0, sizeof(repr->download_url));
    strcpy(repr->temp_file, TEMP_FILE_FMT);
}

static void wget_finish_callback(wget_result_t result,
                                 const wget_download_stats_t *stats,
                                 void *data) {
    wget_args_t *args = (wget_args_t *) data;
    if (!stats || result != WGET_RESULT_OK) {
        set_state(args->anjay, args->repr, DIAG_STATE_TRANSFER_FAILED);
    } else {
        args->repr->stats = *stats;
        set_state(args->anjay, args->repr, DIAG_STATE_COMPLETED);
    }
    unlink(args->repr->temp_file);
}

static int diag_download_run(anjay_t *anjay,
                             download_diag_repr_t *repr) {

    if (make_temp_file(repr->temp_file)) {
        return ANJAY_ERR_INTERNAL;
    }

    wget_args_t *args = (wget_args_t *) malloc(sizeof(wget_args_t));
    if (!args) {
        goto error;
    }
    args->anjay = anjay;
    args->repr = repr;

    if (wget_register_finish_callback(repr->wget_ctx, wget_finish_callback,
                                      args, free)) {
        goto error;
    }

    if (wget_background_download(repr->wget_ctx, repr->download_url,
                                 repr->temp_file)) {
        set_state(anjay, repr, DIAG_STATE_TRANSFER_FAILED);
        demo_log(ERROR, "cannot schedule download diagnostic");
        goto error;
    }
    set_state(anjay, repr, DIAG_STATE_REQUESTED);
    return 0;
error:
    unlink(repr->temp_file);
    free(args);
    return -1;
}

static int diag_resource_execute(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj_ptr,
                                 anjay_iid_t iid,
                                 anjay_iid_t rid,
                                 anjay_execute_ctx_t *ctx) {
    (void) anjay;
    (void) iid;
    (void) ctx;
    switch ((download_diag_res_t) rid) {
    case DOWNLOAD_DIAG_RUN:
        return diag_download_run(anjay, get_diag(obj_ptr));
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
    return ANJAY_ERR_NOT_FOUND;
}

static int read_stats_resource(anjay_rid_t rid,
                               download_diag_repr_t *repr,
                               anjay_output_ctx_t *ctx) {
    if (repr->state != DIAG_STATE_COMPLETED) {
        return ANJAY_ERR_INTERNAL;
    }

    switch ((download_diag_res_t) rid) {
    case DOWNLOAD_DIAG_ROM_TIME_US:
    case DOWNLOAD_DIAG_BOM_TIME_US:
        return anjay_ret_i64(ctx, timespec_as_us(repr->stats.beg));
    case DOWNLOAD_DIAG_EOM_TIME_US:
        return anjay_ret_i64(ctx, timespec_as_us(repr->stats.end));
    case DOWNLOAD_DIAG_TOTAL_BYTES:
        {
            int64_t total_bytes = INT64_MAX;
            if (repr->stats.bytes_written < INT64_MAX) {
                total_bytes = (int64_t) repr->stats.bytes_written;
            }
            return anjay_ret_i64(ctx, total_bytes);
        }
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int diag_resource_present(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj_ptr,
                                 anjay_iid_t iid,
                                 anjay_rid_t rid) {
    (void) anjay;
    (void) iid;

    switch (rid) {
    case DOWNLOAD_DIAG_STATE:
    case DOWNLOAD_DIAG_URL:
        return 1;
    case DOWNLOAD_DIAG_ROM_TIME_US:
    case DOWNLOAD_DIAG_BOM_TIME_US:
    case DOWNLOAD_DIAG_EOM_TIME_US:
    case DOWNLOAD_DIAG_TOTAL_BYTES:
        // diagnostic results do not exist when diagnostic is not complete
        return get_diag(obj_ptr)->state == DIAG_STATE_COMPLETED;
    case DOWNLOAD_DIAG_RUN:
        return 1;
    }

    return 0;
}

static int diag_resource_read(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid,
                              anjay_rid_t rid,
                              anjay_output_ctx_t *ctx) {
    (void) anjay;
    (void) iid;
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
    case DOWNLOAD_DIAG_RUN:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
    return ANJAY_ERR_NOT_FOUND;
}

static int diag_resource_write(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_rid_t rid,
                               anjay_input_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;
    (void) ctx;
    download_diag_repr_t *repr = get_diag(obj_ptr);
    switch ((download_diag_res_t) rid) {
    case DOWNLOAD_DIAG_URL:
        {
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
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
    return ANJAY_ERR_NOT_FOUND;
}

static const anjay_dm_object_def_t DOWNLOAD_DIAG = {
    .oid = DOWNLOAD_DIAG_OID,
    .rid_bound = DOWNLOAD_DIAG_RID_BOUND_,
    .instance_it = anjay_dm_instance_it_SINGLE,
    .instance_present = anjay_dm_instance_present_SINGLE,
    .resource_present = diag_resource_present,
    .resource_supported = anjay_dm_resource_supported_TRUE,
    .resource_execute = diag_resource_execute,
    .resource_read = diag_resource_read,
    .resource_write = diag_resource_write,
    .transaction_begin = anjay_dm_transaction_NOOP,
    .transaction_validate = anjay_dm_transaction_NOOP,
    .transaction_commit = anjay_dm_transaction_NOOP,
    .transaction_rollback = anjay_dm_transaction_NOOP,
};

const anjay_dm_object_def_t **
download_diagnostics_object_create(iosched_t *iosched) {
    download_diag_repr_t *repr = (download_diag_repr_t *)
            calloc(1, sizeof(download_diag_repr_t));
    if (!repr) {
        return NULL;
    }
    repr->def = &DOWNLOAD_DIAG;
    repr->wget_ctx = wget_context_new(iosched);
    if (!repr->wget_ctx) {
        free(repr);
        return NULL;
    }
    return &repr->def;
}

void download_diagnostics_object_release(const anjay_dm_object_def_t **def) {
    download_diag_repr_t *repr = get_diag(def);
    wget_context_delete(&repr->wget_ctx);
    free(repr);
}

