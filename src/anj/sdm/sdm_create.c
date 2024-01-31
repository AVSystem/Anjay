/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <avsystem/commons/avs_defs.h>

#include <fluf/fluf_defs.h>
#include <fluf/fluf_io.h>
#include <fluf/fluf_utils.h>

#include <anj/sdm.h>
#include <anj/sdm_io.h>

#include "sdm_core.h"

static fluf_iid_t find_free_iid(sdm_obj_t *obj) {
    for (uint16_t idx = 0; idx < UINT16_MAX; idx++) {
        if (idx >= obj->inst_count || idx != obj->insts[idx]->iid) {
            return idx;
        }
    }
    AVS_UNREACHABLE("object has more than 65534 instances");
    return 0;
}

int _sdm_begin_create_op(sdm_data_model_t *dm,
                         const fluf_uri_path_t *base_path) {
    assert(base_path && fluf_uri_path_is(base_path, FLUF_ID_OID));

    dm->is_transactional = true;
    dm->op_ctx.write_ctx.path = *base_path;
    dm->op_ctx.write_ctx.instance_created = false;

    sdm_obj_t *obj;
    dm->result = _sdm_get_obj_ptr_call_operation_begin(
            dm, base_path->ids[FLUF_ID_OID], &obj);
    if (dm->result) {
        return dm->result;
    }
    dm->result = _sdm_get_obj_ptrs(obj, base_path, &dm->entity_ptrs);
    if (dm->result) {
        return dm->result;
    }
    if (dm->entity_ptrs.obj->inst_count
            == dm->entity_ptrs.obj->max_inst_count) {
        sdm_log(ERROR, "Maximum number of instances reached");
        dm->result = SDM_ERR_MEMORY;
    }
    return dm->result;
}

int _sdm_create_object_instance(sdm_data_model_t *dm, fluf_iid_t iid) {
    sdm_obj_t *obj = dm->entity_ptrs.obj;
    if (iid == FLUF_ID_INVALID) {
        iid = find_free_iid(obj);
    } else {
        for (uint16_t idx = 0; idx < obj->inst_count; idx++) {
            if (iid == obj->insts[idx]->iid) {
                sdm_log(ERROR, "Instance already exists");
                return SDM_ERR_METHOD_NOT_ALLOWED;
            }
        }
    }
    dm->entity_ptrs.inst = NULL;

    if (!obj->obj_handlers || !obj->obj_handlers->inst_create) {
        sdm_log(ERROR, "inst_create handler not defined");
        return SDM_ERR_INTERNAL;
    }

    int res = obj->obj_handlers->inst_create(obj, &dm->entity_ptrs.inst, iid);
    if (res || !dm->entity_ptrs.inst) {
        sdm_log(ERROR, "inst_create failed");
        if (!res) {
            // operation failed but inst_create didn't return error
            res = SDM_ERR_INTERNAL;
        }
        return res;
    }
    dm->entity_ptrs.inst->iid = iid;
    assert(!_sdm_check_obj_instance(dm->entity_ptrs.inst));

    uint16_t idx_to_write = UINT16_MAX;
    for (uint16_t idx = 0; idx < obj->inst_count; idx++) {
        if (obj->insts[idx]->iid > iid) {
            idx_to_write = idx;
            break;
        }
    }
    if (idx_to_write == UINT16_MAX) {
        obj->insts[obj->inst_count] = dm->entity_ptrs.inst;
    } else {
        for (uint16_t idx = obj->inst_count; idx > idx_to_write; idx--) {
            obj->insts[idx] = obj->insts[idx - 1];
        }
        obj->insts[idx_to_write] = dm->entity_ptrs.inst;
    }
    obj->inst_count++;

    return 0;
}
