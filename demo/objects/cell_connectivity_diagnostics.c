/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#if !defined(_POSIX_C_SOURCE) && !defined(__APPLE__)
#    define _POSIX_C_SOURCE 200809L
#endif

#include "../demo_utils.h"
#include "../objects.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <avsystem/commons/avs_utils.h>

#define CELL_CONNECTIVITY_DIAGNOSTICS_MCC 0
#define CELL_CONNECTIVITY_DIAGNOSTICS_MNC 1
#define CELL_CONNECTIVITY_DIAGNOSTICS_SERVING_CELL_ID 2
#define CELL_CONNECTIVITY_DIAGNOSTICS_OPERATOR_NAME 5
#define CELL_CONNECTIVITY_DIAGNOSTICS_ROAMING_STATUS 6
#define CELL_CONNECTIVITY_DIAGNOSTICS_RSRP 8
#define CELL_CONNECTIVITY_DIAGNOSTICS_RSRQ 9
#define CELL_CONNECTIVITY_DIAGNOSTICS_RSSI 10
#define CELL_CONNECTIVITY_DIAGNOSTICS_SINR 11

typedef struct {
    const anjay_dm_object_def_t *def;
    int64_t mcc;
    int64_t mnc;
    int64_t serving_cell_id;
    const char *operator_name;
    bool roaming_status;
    double rsrp;
    double rsrq;
    double rssi;
    double sinr;
} cell_connectivity_diagnostics_t;

static inline cell_connectivity_diagnostics_t *
get_cell_connectivity_diagnostics(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr);
    return AVS_CONTAINER_OF(obj_ptr, cell_connectivity_diagnostics_t, def);
}

static int cell_connectivity_diagnostics_list_resources(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;

    anjay_dm_emit_res(ctx, CELL_CONNECTIVITY_DIAGNOSTICS_MCC, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, CELL_CONNECTIVITY_DIAGNOSTICS_MNC, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, CELL_CONNECTIVITY_DIAGNOSTICS_SERVING_CELL_ID,
                      ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, CELL_CONNECTIVITY_DIAGNOSTICS_OPERATOR_NAME,
                      ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, CELL_CONNECTIVITY_DIAGNOSTICS_ROAMING_STATUS,
                      ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, CELL_CONNECTIVITY_DIAGNOSTICS_RSRP, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, CELL_CONNECTIVITY_DIAGNOSTICS_RSRQ, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, CELL_CONNECTIVITY_DIAGNOSTICS_RSSI, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, CELL_CONNECTIVITY_DIAGNOSTICS_SINR, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    return 0;
}

static int cell_connectivity_diagnostics_resource_read(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        anjay_output_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;
    (void) riid;
    assert(riid == ANJAY_ID_INVALID);
    cell_connectivity_diagnostics_t *cell_connectivity =
            get_cell_connectivity_diagnostics(obj_ptr);

    switch (rid) {
    case CELL_CONNECTIVITY_DIAGNOSTICS_MCC:
        return anjay_ret_i64(ctx, cell_connectivity->mcc);
    case CELL_CONNECTIVITY_DIAGNOSTICS_MNC:
        return anjay_ret_i64(ctx, cell_connectivity->mnc);
    case CELL_CONNECTIVITY_DIAGNOSTICS_SERVING_CELL_ID:
        return anjay_ret_i64(ctx, cell_connectivity->serving_cell_id);
    case CELL_CONNECTIVITY_DIAGNOSTICS_OPERATOR_NAME:
        return anjay_ret_string(ctx, cell_connectivity->operator_name
                                             ? cell_connectivity->operator_name
                                             : "");
    case CELL_CONNECTIVITY_DIAGNOSTICS_ROAMING_STATUS:
        return anjay_ret_bool(ctx, cell_connectivity->roaming_status);
    case CELL_CONNECTIVITY_DIAGNOSTICS_RSRP:
        return anjay_ret_double(ctx, cell_connectivity->rsrp);
    case CELL_CONNECTIVITY_DIAGNOSTICS_RSRQ:
        return anjay_ret_double(ctx, cell_connectivity->rsrq);
    case CELL_CONNECTIVITY_DIAGNOSTICS_RSSI:
        return anjay_ret_double(ctx, cell_connectivity->rssi);
    case CELL_CONNECTIVITY_DIAGNOSTICS_SINR:
        return anjay_ret_double(ctx, cell_connectivity->sinr);
    default:
        AVS_UNREACHABLE("Read called on unknown resource");
        return ANJAY_ERR_NOT_FOUND;
    }
}

static const anjay_dm_object_def_t CELL_CONNECTIVITY_DIAGNOSTICS = {
    .oid = DEMO_OID_CELL_CONNECTIVITY_DIAGNOSTICS,
    .handlers = {
        .list_instances = anjay_dm_list_instances_SINGLE,
        .list_resources = cell_connectivity_diagnostics_list_resources,
        .resource_read = cell_connectivity_diagnostics_resource_read
    }
};

const anjay_dm_object_def_t **cell_connectivity_diagnostics_object_create(
        cell_connectivity_diagnostics_args_t *args) {
    cell_connectivity_diagnostics_t *repr =
            (cell_connectivity_diagnostics_t *) avs_calloc(
                    1, sizeof(cell_connectivity_diagnostics_t));
    if (!repr) {
        return NULL;
    }

    repr->def = &CELL_CONNECTIVITY_DIAGNOSTICS;

    repr->mcc = args->mcc;
    repr->mnc = args->mnc;
    repr->serving_cell_id = args->serving_cell_id;
    repr->operator_name = args->operator_name;
    repr->roaming_status = args->roaming_status;
    repr->rsrp = args->rsrp;
    repr->rsrq = args->rsrq;
    repr->rssi = args->rssi;
    repr->sinr = args->sinr;

    return &repr->def;
}

void cell_connectivity_diagnostics_object_release(
        const anjay_dm_object_def_t **def) {
    if (def) {
        avs_free(get_cell_connectivity_diagnostics(def));
    }
}
