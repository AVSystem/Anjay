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

#include "../demo.h"
#include "../demo_utils.h"
#include "../objects.h"

#include <assert.h>
#include <stdio.h>

#define CELL_RES_SMSC_ADDRESS 0               // string
#define CELL_RES_DISABLE_RADIO_PERIOD 1       // int[0:86400]
#define CELL_RES_MODULE_ACTIVATION_CODE 2     // string
#define CELL_RES_VENDOR_SPECIFIC_EXTENSIONS 3 // objlnk
#define CELL_RES_PSM_TIMER 4                  // int [600:85708800]
#define CELL_RES_ACTIVE_TIMER 5               // int [2:1860]
#define CELL_RES_SERVING_PLMN_RATE_CONTROL 6  // int
#define CELL_RES_EDRX_PARAMS_WBS1 8           // bytes
#define CELL_RES_EDRX_PARAMS_NBS1 9           // bytes
#define CELL_RES_ACTIVATED_PROFILE_NAMES 11   // objlnk[]
#define CELL_RES_POWER_SAVING_MODES 13        // int16_t
#define CELL_RES_ACTIVE_POWER_SAVING_MODES 14 // int16_t

#define PSM_TIMER_MIN 600      // seconds (10 min)
#define PSM_TIMER_MAX 85708800 // seconds (992 days)
#define ACTIVE_TIMER_MIN 2     // seconds
#define ACTIVE_TIMER_MAX 1860  // seconds (31 min)

typedef enum {
    PS_PSM = (1 << 0),
    PS_eDRX = (1 << 1),

    PS_ALL_AVILABLE_MODES = PS_PSM | PS_eDRX
} cell_power_saving_mode_t;

typedef struct {
    uint16_t active_power_saving_modes;
    int32_t psm_timer;
    int32_t active_timer;
    uint8_t edrx_wbs1;
    uint8_t edrx_nbs1;
} cell_connectivity_data_t;

typedef struct {
    const anjay_dm_object_def_t *def;
    anjay_demo_t *demo;

    cell_connectivity_data_t actual_data;
    cell_connectivity_data_t backup_data;
} cell_connectivity_repr_t;

static inline cell_connectivity_repr_t *
get_cell(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr);
    return AVS_CONTAINER_OF(obj_ptr, cell_connectivity_repr_t, def);
}

static int cell_instance_reset(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid) {
    (void) anjay;
    (void) iid;

    cell_connectivity_repr_t *cell = get_cell(obj_ptr);
    cell->actual_data = (cell_connectivity_data_t) {
        .psm_timer = PSM_TIMER_MIN,
        .active_timer = ACTIVE_TIMER_MIN
    };

    return 0;
}

static int cell_list_resources(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;
    anjay_dm_emit_res(ctx, CELL_RES_PSM_TIMER, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, CELL_RES_ACTIVE_TIMER, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, CELL_RES_SERVING_PLMN_RATE_CONTROL, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, CELL_RES_EDRX_PARAMS_WBS1, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, CELL_RES_EDRX_PARAMS_NBS1, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, CELL_RES_ACTIVATED_PROFILE_NAMES, ANJAY_DM_RES_RM,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, CELL_RES_POWER_SAVING_MODES, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, CELL_RES_ACTIVE_POWER_SAVING_MODES, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    return 0;
}

static int cell_resource_read(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid,
                              anjay_rid_t rid,
                              anjay_riid_t riid,
                              anjay_output_ctx_t *ctx) {
    (void) anjay;
    (void) iid;

    cell_connectivity_repr_t *cell = get_cell(obj_ptr);
    switch (rid) {
    case CELL_RES_PSM_TIMER:
        return anjay_ret_i32(ctx, cell->actual_data.psm_timer);
    case CELL_RES_ACTIVE_TIMER:
        return anjay_ret_i32(ctx, cell->actual_data.active_timer);
    case CELL_RES_EDRX_PARAMS_WBS1: {
        anjay_ret_bytes_ctx_t *bytes_ctx =
                anjay_ret_bytes_begin(ctx, sizeof(cell->actual_data.edrx_wbs1));
        if (!bytes_ctx) {
            return -1;
        }
        return anjay_ret_bytes_append(bytes_ctx, &cell->actual_data.edrx_wbs1,
                                      sizeof(cell->actual_data.edrx_wbs1));
    }
    case CELL_RES_EDRX_PARAMS_NBS1: {
        anjay_ret_bytes_ctx_t *bytes_ctx =
                anjay_ret_bytes_begin(ctx, sizeof(cell->actual_data.edrx_nbs1));
        if (!bytes_ctx) {
            return -1;
        }
        return anjay_ret_bytes_append(bytes_ctx, &cell->actual_data.edrx_nbs1,
                                      sizeof(cell->actual_data.edrx_nbs1));
    }
    case CELL_RES_SERVING_PLMN_RATE_CONTROL:
        return anjay_ret_i32(ctx, 0);
    case CELL_RES_ACTIVATED_PROFILE_NAMES: {
        int result = ANJAY_ERR_NOT_FOUND;
        AVS_LIST(anjay_iid_t) profile_iids = NULL;
        AVS_LIST(anjay_iid_t) it = NULL;

        const anjay_dm_object_def_t **apn_conn_profile =
                demo_find_object(cell->demo, DEMO_OID_APN_CONN_PROFILE);
        if (!apn_conn_profile) {
            return ANJAY_ERR_INTERNAL;
        }

        profile_iids = apn_conn_profile_list_activated(apn_conn_profile);
        AVS_LIST_FOREACH(it, profile_iids) {
            // Activated Profile Names is an identity map (IID => IID),
            // that's why we're comparing IID to RIID.
            if (*it == riid) {
                result = anjay_ret_objlnk(ctx, DEMO_OID_APN_CONN_PROFILE, *it);
                break;
            }
        }
        AVS_LIST_CLEAR(&profile_iids);
        return result;
    }
    case CELL_RES_POWER_SAVING_MODES:
        return anjay_ret_i32(ctx, PS_ALL_AVILABLE_MODES);
    case CELL_RES_ACTIVE_POWER_SAVING_MODES:
        return anjay_ret_i32(ctx, cell->actual_data.active_power_saving_modes);
    default:
        AVS_UNREACHABLE("Read called on unknown resource");
        return ANJAY_ERR_NOT_FOUND;
    }
}

static int get_edrx_params(anjay_input_ctx_t *ctx, uint8_t *edrx_data) {
    bool message_finished;
    size_t bytes_read;
    int result = anjay_get_bytes(ctx, &bytes_read, &message_finished, edrx_data,
                                 sizeof(*edrx_data));
    if (result) {
        return result;
    }
    if (bytes_read != sizeof(*edrx_data) || !message_finished) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    return 0;
}

static int cell_resource_write(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_rid_t rid,
                               anjay_rid_t riid,
                               anjay_input_ctx_t *ctx) {
    (void) anjay;
    (void) iid;
    (void) riid;

    cell_connectivity_repr_t *cell = get_cell(obj_ptr);

    switch (rid) {
    case CELL_RES_PSM_TIMER: {
        int32_t value;
        int retval = anjay_get_i32(ctx, &value);
        if (retval) {
            return retval;
        }
        if (value < PSM_TIMER_MIN || value > PSM_TIMER_MAX) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        cell->actual_data.psm_timer = value;
        return 0;
    }
    case CELL_RES_ACTIVE_TIMER: {
        int32_t value;
        int retval = anjay_get_i32(ctx, &value);
        if (retval) {
            return retval;
        }
        if (value < ACTIVE_TIMER_MIN || value > ACTIVE_TIMER_MAX) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        cell->actual_data.active_timer = value;
        return 0;
    }
    case CELL_RES_EDRX_PARAMS_WBS1:
        return get_edrx_params(ctx, &cell->actual_data.edrx_wbs1);
    case CELL_RES_EDRX_PARAMS_NBS1:
        return get_edrx_params(ctx, &cell->actual_data.edrx_nbs1);
    case CELL_RES_ACTIVE_POWER_SAVING_MODES: {
        assert(riid == ANJAY_ID_INVALID);
        int32_t i32_val;
        int result = anjay_get_i32(ctx, &i32_val);
        if (result) {
            return result;
        }
        if ((uint16_t) i32_val != i32_val
                || (uint16_t) i32_val & ~PS_ALL_AVILABLE_MODES) {
            return ANJAY_ERR_BAD_REQUEST;
        }

        cell->actual_data.active_power_saving_modes = (uint16_t) i32_val;
        return 0;
    }
    default:
        // Bootstrap Server may try to write to other resources,
        // so no AVS_UNREACHABLE() here
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int
cell_list_resource_instances(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             anjay_dm_list_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;

    cell_connectivity_repr_t *cell = get_cell(obj_ptr);
    switch (rid) {
    case CELL_RES_ACTIVATED_PROFILE_NAMES: {
        const anjay_dm_object_def_t **apn_conn_profile =
                demo_find_object(cell->demo, DEMO_OID_APN_CONN_PROFILE);
        if (!apn_conn_profile) {
            return ANJAY_ERR_INTERNAL;
        }

        AVS_LIST(anjay_iid_t) profile_iids =
                apn_conn_profile_list_activated(apn_conn_profile);
        AVS_LIST_CLEAR(&profile_iids) {
            anjay_dm_emit(ctx, *profile_iids);
        }
        return 0;
    }
    default:
        AVS_UNREACHABLE(
                "Attempted to list instances in a single-instance resource");
        return ANJAY_ERR_INTERNAL;
    }
}

static int cell_transaction_begin(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;

    cell_connectivity_repr_t *cell = get_cell(obj_ptr);
    cell->backup_data = cell->actual_data;

    return 0;
}

static int
cell_transaction_rollback(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;

    cell_connectivity_repr_t *cell = get_cell(obj_ptr);
    cell->actual_data = cell->backup_data;

    return 0;
}

static const anjay_dm_object_def_t cell_connectivity = {
    .oid = DEMO_OID_CELL_CONNECTIVITY,
    .version = "1.1",
    .handlers = {
        .list_instances = anjay_dm_list_instances_SINGLE,
        .instance_reset = cell_instance_reset,
        .list_resources = cell_list_resources,
        .resource_read = cell_resource_read,
        .resource_write = cell_resource_write,
        .list_resource_instances = cell_list_resource_instances,
        .transaction_begin = cell_transaction_begin,
        .transaction_validate = anjay_dm_transaction_NOOP,
        .transaction_commit = anjay_dm_transaction_NOOP,
        .transaction_rollback = cell_transaction_rollback
    }
};

const anjay_dm_object_def_t **
cell_connectivity_object_create(anjay_demo_t *demo) {
    cell_connectivity_repr_t *repr = (cell_connectivity_repr_t *) avs_calloc(
            1, sizeof(cell_connectivity_repr_t));
    if (!repr) {
        return NULL;
    }

    repr->actual_data.active_timer = ACTIVE_TIMER_MIN;
    repr->actual_data.psm_timer = PSM_TIMER_MIN;

    repr->def = &cell_connectivity;
    repr->demo = demo;

    return &repr->def;
}

void cell_connectivity_object_release(const anjay_dm_object_def_t **def) {
    if (def) {
        avs_free(get_cell(def));
    }
}
