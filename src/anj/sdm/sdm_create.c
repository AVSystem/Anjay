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
    dm->op_ctx.write_ctx.instance_creation_attempted = false;

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

int sdm_create_object_instance(sdm_data_model_t *dm, fluf_iid_t iid) {
    assert(dm && !dm->result && dm->op_in_progress
           && (dm->operation == FLUF_OP_DM_CREATE
               || (dm->operation == FLUF_OP_DM_WRITE_REPLACE
                   && dm->boostrap_operation))
           && !dm->op_ctx.write_ctx.instance_creation_attempted);
    sdm_obj_t *obj = dm->entity_ptrs.obj;
    if (obj->inst_count == obj->max_inst_count) {
        sdm_log(ERROR, "Maximum number of instances reached");
        dm->result = SDM_ERR_MEMORY;
        return dm->result;
    }
    if (iid == FLUF_ID_INVALID) {
        iid = find_free_iid(obj);
    } else {
        for (uint16_t idx = 0; idx < obj->inst_count; idx++) {
            if (iid == obj->insts[idx]->iid) {
                sdm_log(ERROR, "Instance already exists");
                dm->result = SDM_ERR_METHOD_NOT_ALLOWED;
                return dm->result;
            }
        }
    }
    sdm_obj_inst_t *inst = NULL;

    if (!obj->obj_handlers || !obj->obj_handlers->inst_create) {
        sdm_log(ERROR, "inst_create handler not defined");
        dm->result = SDM_ERR_METHOD_NOT_ALLOWED;
        return dm->result;
    }

    dm->result = obj->obj_handlers->inst_create(obj, &inst, iid);
    if (dm->result || !inst) {
        sdm_log(ERROR, "inst_create failed");
        if (!dm->result) {
            // operation failed but inst_create didn't return error
            dm->result = SDM_ERR_INTERNAL;
        }
        return dm->result;
    }

    inst->iid = iid;
    assert(!_sdm_check_obj_instance(inst));

    uint16_t idx_to_write = obj->inst_count;
    for (uint16_t idx = 0; idx < obj->inst_count; idx++) {
        if (obj->insts[idx]->iid > iid) {
            idx_to_write = idx;
            break;
        }
    }
    for (uint16_t idx = obj->inst_count; idx > idx_to_write; idx--) {
        obj->insts[idx] = obj->insts[idx - 1];
    }
    obj->insts[idx_to_write] = inst;
    obj->inst_count++;
    dm->op_ctx.write_ctx.path.ids[FLUF_ID_IID] = iid;

    dm->op_ctx.write_ctx.instance_creation_attempted = true;
    dm->entity_ptrs.inst = inst;
    return 0;
}
