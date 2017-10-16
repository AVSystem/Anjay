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

#include <assert.h>
#include <stdio.h>

#include "../demo.h"
#include "../objects.h"
#include "../demo_utils.h"

#define CELL_RES_SMSC_ADDRESS 0               // string
#define CELL_RES_DISABLE_RADIO_PERIOD 1       // int[0:86400]
#define CELL_RES_MODULE_ACTIVATION_CODE 2     // string
#define CELL_RES_VENDOR_SPECIFIC_EXTENSIONS 3 // objlnk

#define CELL_RES_ACTIVATED_PROFILE_NAMES 4000 // objlnk[]

typedef struct {
    const anjay_dm_object_def_t *def;
    anjay_demo_t *demo;
} cell_connectivity_repr_t;

static inline cell_connectivity_repr_t *
get_cell(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr);
    return container_of(obj_ptr, cell_connectivity_repr_t, def);
}

static int cell_resource_read(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid,
                              anjay_rid_t rid,
                              anjay_output_ctx_t *ctx) {
    (void) anjay; (void) obj_ptr; (void) iid;

    cell_connectivity_repr_t *cell = get_cell(obj_ptr);
    switch (rid) {
    case CELL_RES_ACTIVATED_PROFILE_NAMES:
        {
            int result = ANJAY_ERR_INTERNAL;
            AVS_LIST(anjay_iid_t) profile_iids = NULL;

            const anjay_dm_object_def_t **apn_conn_profile =
                    demo_find_object(cell->demo, DEMO_OID_APN_CONN_PROFILE);
            if (!apn_conn_profile) {
                goto cleanup;
            }

            profile_iids = apn_conn_profile_list_activated(apn_conn_profile);

            anjay_output_ctx_t *array = anjay_ret_array_start(ctx);
            if (!array) {
                goto cleanup;
            }

            AVS_LIST(anjay_iid_t) iid = NULL;
            AVS_LIST_FOREACH(iid, profile_iids) {
                if (anjay_ret_array_index(array, *iid)
                        || anjay_ret_objlnk(array,
                                            DEMO_OID_APN_CONN_PROFILE, *iid)) {
                    goto cleanup;
                }
            }

            result = anjay_ret_array_finish(array);
cleanup:
            AVS_LIST_CLEAR(&profile_iids);
            return result;
        }
    default:
        return ANJAY_ERR_NOT_FOUND;
    }
}

static int cell_resource_write(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_rid_t rid,
                               anjay_input_ctx_t *ctx) {
    (void) anjay; (void) obj_ptr; (void) iid; (void) ctx;

    switch (rid) {
    case CELL_RES_ACTIVATED_PROFILE_NAMES:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    default:
        return ANJAY_ERR_NOT_FOUND;
    }
}

static int cell_resource_dim(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr,
                             anjay_iid_t iid,
                             anjay_rid_t rid) {
    (void) anjay; (void) obj_ptr; (void) iid;

    cell_connectivity_repr_t *cell = get_cell(obj_ptr);
    switch (rid) {
    case CELL_RES_ACTIVATED_PROFILE_NAMES:
        {
            const anjay_dm_object_def_t **apn_conn_profile =
                    demo_find_object(cell->demo, DEMO_OID_APN_CONN_PROFILE);
            if (!apn_conn_profile) {
                return ANJAY_ERR_INTERNAL;
            }

            AVS_LIST(anjay_iid_t) profile_iids =
                    apn_conn_profile_list_activated(apn_conn_profile);
            size_t size = AVS_LIST_SIZE(profile_iids);
            AVS_LIST_CLEAR(&profile_iids);

            return (int)size;
        }
    default:
        return ANJAY_DM_DIM_INVALID;
    }
}

static const anjay_dm_object_def_t cell_connectivity = {
    .oid = DEMO_OID_CELL_CONNECTIVITY,
    .supported_rids = ANJAY_DM_SUPPORTED_RIDS(
            CELL_RES_ACTIVATED_PROFILE_NAMES),
    .handlers = {
        .instance_it = anjay_dm_instance_it_SINGLE,
        .instance_present = anjay_dm_instance_present_SINGLE,
        .resource_present = anjay_dm_resource_present_TRUE,
        .resource_read = cell_resource_read,
        .resource_write = cell_resource_write,
        .resource_dim = cell_resource_dim,
        .transaction_begin = anjay_dm_transaction_NOOP,
        .transaction_validate = anjay_dm_transaction_NOOP,
        .transaction_commit = anjay_dm_transaction_NOOP,
        .transaction_rollback = anjay_dm_transaction_NOOP
    }
};

const anjay_dm_object_def_t **
cell_connectivity_object_create(anjay_demo_t *demo) {
    cell_connectivity_repr_t *repr = (cell_connectivity_repr_t *)
            calloc(1, sizeof(cell_connectivity_repr_t));
    if (!repr) {
        return NULL;
    }

    repr->def = &cell_connectivity;
    repr->demo = demo;

    return &repr->def;
}

void cell_connectivity_object_release(const anjay_dm_object_def_t **def) {
    if (def) {
        free(get_cell(def));
    }
}
