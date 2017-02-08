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

#define CM_RES_NETWORK_BEARER           0 /* int */
#define CM_RES_AVAILABLE_NETWORK_BEARER 1 /* array<int> */
#define CM_RES_RADIO_SIGNAL_STRENGTH    2 /* int */
#define CM_RES_LINK_QUALITY             3 /* int */
#define CM_RES_IP_ADDRESSES             4 /* array<string> */
#define CM_RES_ROUTER_IP_ADDRESSES      5 /* array<string> */
#define CM_RES_LINK_UTILIZATION         6 /* int */
#define CM_RES_APN                      7 /* array<string> */
#define CM_RES_CELL_ID                  8 /* int */
#define CM_RES_SMNC                     9 /* int */
#define CM_RES_SMCC                     10 /* int */
#define CM_RID_BOUND_                   11

typedef struct {
    const anjay_dm_object_def_t *def;
} conn_monitoring_repr_t;

static inline conn_monitoring_repr_t *
get_cm(const anjay_dm_object_def_t *const *obj_ptr) {
    if (!obj_ptr) {
        return NULL;
    }
    return container_of(obj_ptr, conn_monitoring_repr_t, def);
}

static int cm_resource_supported(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj_ptr,
                                 anjay_rid_t rid) {
    (void) anjay;
    (void) obj_ptr;

    switch (rid) {
    case CM_RES_NETWORK_BEARER:
    case CM_RES_AVAILABLE_NETWORK_BEARER:
    case CM_RES_RADIO_SIGNAL_STRENGTH:
    case CM_RES_LINK_QUALITY:
    case CM_RES_IP_ADDRESSES:
    case CM_RES_ROUTER_IP_ADDRESSES:
    case CM_RES_LINK_UTILIZATION:
    case CM_RES_APN:
    case CM_RES_CELL_ID:
    case CM_RES_SMNC:
    case CM_RES_SMCC:
        return 1;
    default:
        return 0;
    }
}

static int cm_resource_present(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_rid_t rid) {
    (void) iid;
    return cm_resource_supported(anjay, obj_ptr, rid);
}

static int signal_strength_dbm(void) {
    // range should be -110 .. -48
    // RNG-like predictable generation in range -80 .. -65
    return (int) (time_to_rand() % 16) - 80;
}

static int cm_resource_read(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid,
                            anjay_rid_t rid,
                            anjay_output_ctx_t *ctx) {
    (void) anjay; (void) obj_ptr; (void) iid;

    enum {
        NB_CELLULAR_GSM = 0,
        NB_CELLULAR_TD_SCDMA,
        NB_CELLULAR_WCDMA,
        NB_CELLULAR_CDMA2000,
        NB_CELLULAR_WIMAX,
        NB_CELLULAR_LTE_TDD,
        NB_CELLULAR_LTE_FDD,

        NB_WIRELESS_WLAN = 21,
        NB_WIRELESS_BLUETOOTH,
        NB_WIRELESS_802_15_4,

        NB_WIRED_ETHERNET = 41,
        NB_WIRED_DSL,
        NB_WIRED_PLC
    };

    anjay_output_ctx_t *array;
    switch (rid) {
    case CM_RES_NETWORK_BEARER:
        return anjay_ret_i32(ctx, NB_CELLULAR_WCDMA);
    case CM_RES_AVAILABLE_NETWORK_BEARER:
        return (!(array = anjay_ret_array_start(ctx))
                || anjay_ret_array_index(array, 0)
                || anjay_ret_i32(array, NB_CELLULAR_GSM)
                || anjay_ret_array_index(array, 1)
                || anjay_ret_i32(array, NB_CELLULAR_WCDMA)
                || anjay_ret_array_index(array, 2)
                || anjay_ret_i32(array, NB_CELLULAR_LTE_FDD)
                || anjay_ret_array_index(array, 3)
                || anjay_ret_i32(array, NB_WIRELESS_WLAN)
                || anjay_ret_array_index(array, 4)
                || anjay_ret_i32(array, NB_WIRELESS_BLUETOOTH)
                || anjay_ret_array_finish(array))
                ? -1 : 0;
    case CM_RES_RADIO_SIGNAL_STRENGTH:
        return anjay_ret_i32(ctx, signal_strength_dbm());
    case CM_RES_LINK_QUALITY:
        return anjay_ret_i32(ctx, 255);
    case CM_RES_IP_ADDRESSES:
        return (!(array = anjay_ret_array_start(ctx))
                || anjay_ret_array_index(array, 0)
                || anjay_ret_string(array, "10.10.53.53")
                || anjay_ret_array_finish(array))
                ? -1 : 0;
    case CM_RES_ROUTER_IP_ADDRESSES:
        return (!(array = anjay_ret_array_start(ctx))
                || anjay_ret_array_index(array, 0)
                || anjay_ret_string(array, "10.10.0.1")
                || anjay_ret_array_finish(array))
                ? -1 : 0;
    case CM_RES_LINK_UTILIZATION:
        return anjay_ret_i32(ctx, 50);
    case CM_RES_APN:
        return anjay_ret_string(ctx, "");
    case CM_RES_CELL_ID:
        return anjay_ret_i32(ctx, 12345);
    case CM_RES_SMNC:
        return anjay_ret_i32(ctx, 0);
    case CM_RES_SMCC:
        return anjay_ret_i32(ctx, 0);
    default:
        return 0;
    }
}

static int cm_resource_dim(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj_ptr,
                           anjay_iid_t iid,
                           anjay_rid_t rid) {
    (void) anjay; (void) obj_ptr; (void) iid;

    switch (rid) {
    case CM_RES_AVAILABLE_NETWORK_BEARER:
        return 5;
    case CM_RES_IP_ADDRESSES:
        return 1;
    case CM_RES_ROUTER_IP_ADDRESSES:
        return 1;
    default:
        return ANJAY_DM_DIM_INVALID;
    }
}

static const anjay_dm_object_def_t CONN_MONITORING = {
    .oid = 4,
    .rid_bound = CM_RID_BOUND_,
    .instance_it = anjay_dm_instance_it_SINGLE,
    .instance_present = anjay_dm_instance_present_SINGLE,
    .resource_present = cm_resource_present,
    .resource_supported = cm_resource_supported,
    .resource_read = cm_resource_read,
    .resource_dim = cm_resource_dim
};

const anjay_dm_object_def_t **cm_object_create(void) {
    conn_monitoring_repr_t *repr = (conn_monitoring_repr_t *)
            calloc(1, sizeof(conn_monitoring_repr_t));
    if (!repr) {
        return NULL;
    }

    repr->def = &CONN_MONITORING;

    return &repr->def;
}

void cm_object_release(const anjay_dm_object_def_t **def) {
    free(get_cm(def));
}

void cm_notify_time_dependent(anjay_t *anjay,
                              const anjay_dm_object_def_t **def) {
    anjay_notify_changed(anjay, (*def)->oid, 0, CM_RES_RADIO_SIGNAL_STRENGTH);
}
