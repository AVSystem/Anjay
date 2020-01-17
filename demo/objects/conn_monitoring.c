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

#define CM_RES_NETWORK_BEARER 0           /* int */
#define CM_RES_AVAILABLE_NETWORK_BEARER 1 /* array<int> */
#define CM_RES_RADIO_SIGNAL_STRENGTH 2    /* int */
#define CM_RES_LINK_QUALITY 3             /* int */
#define CM_RES_IP_ADDRESSES 4             /* array<string> */
#define CM_RES_ROUTER_IP_ADDRESSES 5      /* array<string> */
#define CM_RES_LINK_UTILIZATION 6         /* int */
#define CM_RES_APN 7                      /* array<string> */
#define CM_RES_CELL_ID 8                  /* int */
#define CM_RES_SMNC 9                     /* int */
#define CM_RES_SMCC 10                    /* int */

typedef struct {
    const anjay_dm_object_def_t *def;
} conn_monitoring_repr_t;

static inline conn_monitoring_repr_t *
get_cm(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr);
    return AVS_CONTAINER_OF(obj_ptr, conn_monitoring_repr_t, def);
}

static int signal_strength_dbm(void) {
    // range should be -110 .. -48
    // RNG-like predictable generation in range -80 .. -65
    return (int) (time_to_rand() % 16) - 80;
}

static int cm_list_resources(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr,
                             anjay_iid_t iid,
                             anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;

    anjay_dm_emit_res(
            ctx, CM_RES_NETWORK_BEARER, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx,
                      CM_RES_AVAILABLE_NETWORK_BEARER,
                      ANJAY_DM_RES_RM,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx,
                      CM_RES_RADIO_SIGNAL_STRENGTH,
                      ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(
            ctx, CM_RES_LINK_QUALITY, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(
            ctx, CM_RES_IP_ADDRESSES, ANJAY_DM_RES_RM, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx,
                      CM_RES_ROUTER_IP_ADDRESSES,
                      ANJAY_DM_RES_RM,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(
            ctx, CM_RES_LINK_UTILIZATION, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, CM_RES_APN, ANJAY_DM_RES_RM, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(
            ctx, CM_RES_CELL_ID, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, CM_RES_SMNC, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, CM_RES_SMCC, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    return 0;
}

static int cm_resource_read(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid,
                            anjay_rid_t rid,
                            anjay_riid_t riid,
                            anjay_output_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;

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

    switch (rid) {
    case CM_RES_NETWORK_BEARER:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i32(ctx, NB_CELLULAR_WCDMA);
    case CM_RES_AVAILABLE_NETWORK_BEARER:
        switch (riid) {
        case 0:
            return anjay_ret_i32(ctx, NB_CELLULAR_GSM);
        case 1:
            return anjay_ret_i32(ctx, NB_CELLULAR_WCDMA);
        case 2:
            return anjay_ret_i32(ctx, NB_CELLULAR_LTE_FDD);
        case 3:
            return anjay_ret_i32(ctx, NB_WIRELESS_WLAN);
        case 4:
            return anjay_ret_i32(ctx, NB_WIRELESS_BLUETOOTH);
        default:
            AVS_UNREACHABLE(
                    "unexpected RIID for CM_RES_AVAILABLE_NETWORK_BEARER");
            return ANJAY_ERR_NOT_FOUND;
        }
    case CM_RES_RADIO_SIGNAL_STRENGTH:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i32(ctx, signal_strength_dbm());
    case CM_RES_LINK_QUALITY:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i32(ctx, 255);
    case CM_RES_IP_ADDRESSES:
        assert(riid == 0);
        return anjay_ret_string(ctx, "10.10.53.53");
    case CM_RES_ROUTER_IP_ADDRESSES:
        assert(riid == 0);
        return anjay_ret_string(ctx, "10.10.0.1");
    case CM_RES_LINK_UTILIZATION:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i32(ctx, 50);
    case CM_RES_APN:
        assert(riid == 0);
        return anjay_ret_string(ctx, "internet");
    case CM_RES_CELL_ID:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i32(ctx, 12345);
    case CM_RES_SMNC:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i32(ctx, 0);
    case CM_RES_SMCC:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i32(ctx, 0);
    default:
        AVS_UNREACHABLE(
                "Read handler called on unknown or non-readable resource");
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int
cm_list_resource_instances(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj_ptr,
                           anjay_iid_t iid,
                           anjay_rid_t rid,
                           anjay_dm_list_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;

    switch (rid) {
    case CM_RES_AVAILABLE_NETWORK_BEARER:
        anjay_dm_emit(ctx, 0);
        anjay_dm_emit(ctx, 1);
        anjay_dm_emit(ctx, 2);
        anjay_dm_emit(ctx, 3);
        anjay_dm_emit(ctx, 4);
        return 0;
    case CM_RES_IP_ADDRESSES:
    case CM_RES_ROUTER_IP_ADDRESSES:
    case CM_RES_APN:
        anjay_dm_emit(ctx, 0);
        return 0;
    default:
        AVS_UNREACHABLE(
                "Attempted to list instances in a single-instance resource");
        return ANJAY_ERR_INTERNAL;
    }
}

static const anjay_dm_object_def_t CONN_MONITORING = {
    .oid = DEMO_OID_CONN_MONITORING,
    .handlers = {
        .list_instances = anjay_dm_list_instances_SINGLE,
        .list_resources = cm_list_resources,
        .resource_read = cm_resource_read,
        .list_resource_instances = cm_list_resource_instances
    }
};

const anjay_dm_object_def_t **cm_object_create(void) {
    conn_monitoring_repr_t *repr = (conn_monitoring_repr_t *) avs_calloc(
            1, sizeof(conn_monitoring_repr_t));
    if (!repr) {
        return NULL;
    }

    repr->def = &CONN_MONITORING;

    return &repr->def;
}

void cm_object_release(const anjay_dm_object_def_t **def) {
    if (def) {
        avs_free(get_cm(def));
    }
}

void cm_notify_time_dependent(anjay_t *anjay,
                              const anjay_dm_object_def_t **def) {
    anjay_notify_changed(anjay, (*def)->oid, 0, CM_RES_RADIO_SIGNAL_STRENGTH);
}
