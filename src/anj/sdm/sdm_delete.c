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

#include <fluf/fluf_defs.h>
#include <fluf/fluf_io.h>
#include <fluf/fluf_utils.h>

#include <anj/sdm.h>
#include <anj/sdm_io.h>

#include "sdm_core.h"

static int delete_instance(sdm_data_model_t *dm) {
    sdm_obj_t *obj = dm->entity_ptrs.obj;
    if (!obj->obj_handlers || !obj->obj_handlers->inst_delete) {
        sdm_log(ERROR, "inst_delete handler not defined");
        return SDM_ERR_INTERNAL;
    }

    int ret = obj->obj_handlers->inst_delete(obj, dm->entity_ptrs.inst);
    if (ret) {
        sdm_log(ERROR, "inst_delete failed");
        return ret;
    }

    // udpate inst array
    for (uint16_t idx = 0; idx < obj->inst_count; idx++) {
        if (obj->insts[idx]->iid == dm->entity_ptrs.inst->iid) {
            obj->insts[idx] = NULL;
        } else if (obj->insts[idx]->iid > dm->entity_ptrs.inst->iid && idx) {
            obj->insts[idx - 1] = obj->insts[idx];
        }
    }
    obj->inst_count--;
    return 0;
}

int _sdm_process_delete_op(sdm_data_model_t *dm,
                           const fluf_uri_path_t *base_path) {
    assert(fluf_uri_path_is(base_path, FLUF_ID_IID)
           || fluf_uri_path_is(base_path, FLUF_ID_RIID));

    dm->is_transactional = true;

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

    dm->result = fluf_uri_path_is(base_path, FLUF_ID_IID)
                         ? delete_instance(dm)
                         : _sdm_delete_res_instance(dm);
    return dm->result;
}

int _sdm_delete_res_instance(sdm_data_model_t *dm) {
    sdm_res_t *res = dm->entity_ptrs.res;
    if (!res->res_handlers || !res->res_handlers->res_inst_delete) {
        sdm_log(ERROR, "res_inst_delete handler not defined");
        return SDM_ERR_BAD_REQUEST;
    }

    int ret = res->res_handlers->res_inst_delete(dm->entity_ptrs.obj,
                                                 dm->entity_ptrs.inst, res,
                                                 dm->entity_ptrs.res_inst);
    if (ret) {
        sdm_log(ERROR, "res_inst_delete failed");
        return ret;
    }

    // udpate res_inst array
    for (uint16_t idx = 0; idx < res->value.res_inst.inst_count; idx++) {
        if (res->value.res_inst.insts[idx]->riid
                == dm->entity_ptrs.res_inst->riid) {
            res->value.res_inst.insts[idx] = NULL;
        } else if (res->value.res_inst.insts[idx]->riid
                           > dm->entity_ptrs.res_inst->riid
                   && idx) {
            res->value.res_inst.insts[idx - 1] = res->value.res_inst.insts[idx];
        }
    }
    res->value.res_inst.inst_count--;
    return 0;
}
