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

#include "../demo.h"
#include "../demo_utils.h"
#include "../objects.h"

#include <assert.h>
#include <stdio.h>

#define CELL_RES_SMSC_ADDRESS 0               // string
#define CELL_RES_DISABLE_RADIO_PERIOD 1       // int[0:86400]
#define CELL_RES_MODULE_ACTIVATION_CODE 2     // string
#define CELL_RES_VENDOR_SPECIFIC_EXTENSIONS 3 // objlnk

#define CELL_RES_SERVING_PLMN_RATE_CONTROL 6 // int

#define CELL_RES_ACTIVATED_PROFILE_NAMES 11 // objlnk[]

#define CELL_RES_POWER_SAVING_MODES 13        // int16_t
#define CELL_RES_ACTIVE_POWER_SAVING_MODES 14 // int16_t

typedef enum {
    PS_PSM = (1 << 0),
    PS_eDRX = (1 << 1),

    PS_ALL_AVILABLE_MODES = PS_PSM | PS_eDRX
} cell_power_saving_mode_t;

typedef struct {
    const anjay_dm_object_def_t *def;
    anjay_demo_t *demo;
    uint16_t active_power_saving_modes;
    uint16_t backup_power_saving_modes;
} cell_connectivity_repr_t;

static inline cell_connectivity_repr_t *
get_cell(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr);
    return container_of(obj_ptr, cell_connectivity_repr_t, def);
}

static int cell_instance_reset(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid) {
    (void) anjay;
    (void) iid;

    get_cell(obj_ptr)->active_power_saving_modes = 0;

    return 0;
}

static int cell_resource_read(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid,
                              anjay_rid_t rid,
                              anjay_output_ctx_t *ctx) {
    (void) anjay;
    (void) iid;

    cell_connectivity_repr_t *cell = get_cell(obj_ptr);
    switch (rid) {
    case CELL_RES_SERVING_PLMN_RATE_CONTROL:
        return anjay_ret_i32(ctx, 0);
    case CELL_RES_ACTIVATED_PROFILE_NAMES: {
        int result = ANJAY_ERR_INTERNAL;
        AVS_LIST(anjay_iid_t) profile_iids = NULL;
        AVS_LIST(anjay_iid_t) iid = NULL;
        anjay_output_ctx_t *array = NULL;

        const anjay_dm_object_def_t **apn_conn_profile =
                demo_find_object(cell->demo, DEMO_OID_APN_CONN_PROFILE);
        if (!apn_conn_profile) {
            goto cleanup;
        }

        profile_iids = apn_conn_profile_list_activated(apn_conn_profile);

        array = anjay_ret_array_start(ctx);
        if (!array) {
            goto cleanup;
        }

        AVS_LIST_FOREACH(iid, profile_iids) {
            if (anjay_ret_array_index(array, *iid)
                    || anjay_ret_objlnk(array, DEMO_OID_APN_CONN_PROFILE,
                                        *iid)) {
                goto cleanup;
            }
        }

        result = anjay_ret_array_finish(array);
    cleanup:
        AVS_LIST_CLEAR(&profile_iids);
        return result;
    }
    case CELL_RES_POWER_SAVING_MODES:
        return anjay_ret_i32(ctx, PS_ALL_AVILABLE_MODES);
    case CELL_RES_ACTIVE_POWER_SAVING_MODES:
        return anjay_ret_i32(ctx, cell->active_power_saving_modes);
    default:
        return ANJAY_ERR_NOT_FOUND;
    }
}

static int cell_resource_write(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_rid_t rid,
                               anjay_input_ctx_t *ctx) {
    (void) anjay;
    (void) iid;

    cell_connectivity_repr_t *cell = get_cell(obj_ptr);

    switch (rid) {
    case CELL_RES_SERVING_PLMN_RATE_CONTROL:
    case CELL_RES_ACTIVATED_PROFILE_NAMES:
    case CELL_RES_POWER_SAVING_MODES:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    case CELL_RES_ACTIVE_POWER_SAVING_MODES: {
        int32_t i32_val;
        int result = anjay_get_i32(ctx, &i32_val);
        if (result) {
            return result;
        }
        if ((uint16_t) i32_val != i32_val
                || (uint16_t) i32_val & ~PS_ALL_AVILABLE_MODES) {
            return ANJAY_ERR_BAD_REQUEST;
        }

        cell->active_power_saving_modes = (uint16_t) i32_val;
        return 0;
    }
    default:
        return ANJAY_ERR_NOT_FOUND;
    }
}

static int cell_resource_dim(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr,
                             anjay_iid_t iid,
                             anjay_rid_t rid) {
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
        size_t size = AVS_LIST_SIZE(profile_iids);
        AVS_LIST_CLEAR(&profile_iids);

        return (int) size;
    }
    default:
        return ANJAY_DM_DIM_INVALID;
    }
}

static int cell_transaction_begin(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;

    cell_connectivity_repr_t *cell = get_cell(obj_ptr);
    cell->backup_power_saving_modes = cell->active_power_saving_modes;

    return 0;
}

static int
cell_transaction_rollback(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;

    cell_connectivity_repr_t *cell = get_cell(obj_ptr);
    cell->active_power_saving_modes = cell->backup_power_saving_modes;

    return 0;
}

static const anjay_dm_object_def_t cell_connectivity = {
    .oid = DEMO_OID_CELL_CONNECTIVITY,
    .version = "1.1",
    .supported_rids =
            ANJAY_DM_SUPPORTED_RIDS(CELL_RES_SERVING_PLMN_RATE_CONTROL,
                                    CELL_RES_ACTIVATED_PROFILE_NAMES,
                                    CELL_RES_POWER_SAVING_MODES,
                                    CELL_RES_ACTIVE_POWER_SAVING_MODES),
    .handlers = {
        .instance_it = anjay_dm_instance_it_SINGLE,
        .instance_present = anjay_dm_instance_present_SINGLE,
        .instance_reset = cell_instance_reset,
        .resource_present = anjay_dm_resource_present_TRUE,
        .resource_read = cell_resource_read,
        .resource_write = cell_resource_write,
        .resource_dim = cell_resource_dim,
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

    repr->def = &cell_connectivity;
    repr->demo = demo;

    return &repr->def;
}

void cell_connectivity_object_release(const anjay_dm_object_def_t **def) {
    if (def) {
        avs_free(get_cell(def));
    }
}
