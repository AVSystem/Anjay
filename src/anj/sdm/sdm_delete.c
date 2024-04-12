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
        return SDM_ERR_METHOD_NOT_ALLOWED;
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
        } else if (obj->insts[idx]->iid > dm->entity_ptrs.inst->iid) {
            assert(idx > 0);
            obj->insts[idx - 1] = obj->insts[idx];
        }
    }
    dm->entity_ptrs.inst->iid = FLUF_ID_INVALID;
    obj->inst_count--;
    return 0;
}

static bool is_oscore_bootstrap_instance(sdm_data_model_t *dm) {
    sdm_obj_t *security_object = _sdm_find_obj(dm, FLUF_OBJ_ID_SECURITY);
    if (!security_object
            || _sdm_call_operation_begin(security_object, FLUF_OP_DM_READ)) {
        return false;
    }
    fluf_res_value_t value;
    for (uint16_t idx = 0; idx < security_object->inst_count; idx++) {
        // first find Bootstrap Server Instance,
        // then read related OSCORE Instance
        if (!_sdm_get_resource_value(
                    dm,
                    &FLUF_MAKE_RESOURCE_PATH(
                            FLUF_OBJ_ID_SECURITY,
                            security_object->insts[idx]->iid,
                            _SDM_OBJ_SECURITY_BOOTSTRAP_SERVER_RID),
                    &value, NULL)
                && value.bool_value
                && !_sdm_get_resource_value(
                           dm,
                           &FLUF_MAKE_RESOURCE_PATH(
                                   FLUF_OBJ_ID_SECURITY,
                                   security_object->insts[idx]->iid,
                                   _SDM_OBJ_SECURITY_OSCORE_RID),
                           &value, NULL)
                && value.objlnk.iid == dm->entity_ptrs.inst->iid) {
            return true;
        }
    }
    return false;
}

static bool is_bootstrap_instance(sdm_data_model_t *dm) {
    if (dm->entity_ptrs.obj->oid == FLUF_OBJ_ID_SECURITY) {
        fluf_res_value_t value;
        int result = _sdm_get_resource_value(
                dm,
                &FLUF_MAKE_RESOURCE_PATH(
                        FLUF_OBJ_ID_SECURITY, dm->entity_ptrs.inst->iid,
                        _SDM_OBJ_SECURITY_BOOTSTRAP_SERVER_RID),
                &value, NULL);
        if (result) {
            return false;
        }
        return value.bool_value;
    }
    if (dm->entity_ptrs.obj->oid == FLUF_OBJ_ID_OSCORE) {
        return is_oscore_bootstrap_instance(dm);
    }
    return false;
}

static int process_bootstrap_delete_op(sdm_data_model_t *dm,
                                       const fluf_uri_path_t *base_path) {
    assert(!fluf_uri_path_has(base_path, FLUF_ID_RID));

    bool all_objects = !fluf_uri_path_has(base_path, FLUF_ID_OID);
    bool all_instances = !fluf_uri_path_has(base_path, FLUF_ID_IID);

    if (!all_objects && base_path->ids[FLUF_ID_OID] == FLUF_OBJ_ID_DEVICE) {
        sdm_log(ERROR, "Device Object Instance cannot be deleted");
        return SDM_ERR_BAD_REQUEST;
    }

    int result = 0;

    for (uint16_t idx = 0; idx < dm->objs_count; idx++) {
        // ignore Device Object
        if (dm->objs[idx]->oid == FLUF_OBJ_ID_DEVICE) {
            continue;
        }
        if (all_objects || base_path->ids[FLUF_ID_OID] == dm->objs[idx]->oid) {
            result =
                    _sdm_call_operation_begin(dm->objs[idx], FLUF_OP_DM_DELETE);
            if (result) {
                return result;
            }
            for (int32_t i = dm->objs[idx]->inst_count - 1; i >= 0; i--) {
                dm->entity_ptrs.obj = dm->objs[idx];
                dm->entity_ptrs.inst = dm->objs[idx]->insts[i];
                if (all_instances
                        || base_path->ids[FLUF_ID_IID]
                                       == dm->entity_ptrs.inst->iid) {
                    if (is_bootstrap_instance(dm)) {
                        if (!all_objects && !all_instances) {
                            sdm_log(ERROR,
                                    "Path points to Bootstrap-Server Account "
                                    "Instance or its associated OSCORE "
                                    "Instance. None of them can be deleted.");
                            return SDM_ERR_BAD_REQUEST;
                        }
                        continue;
                    }
                    result = delete_instance(dm);
                    if (result) {
                        return result;
                    }
                }
            }
        }
    }
    return result;
}

int _sdm_process_delete_op(sdm_data_model_t *dm,
                           const fluf_uri_path_t *base_path) {
    assert(dm->boostrap_operation || fluf_uri_path_is(base_path, FLUF_ID_IID)
           || fluf_uri_path_is(base_path, FLUF_ID_RIID));

    dm->is_transactional = true;

    if (dm->boostrap_operation) {
        dm->result = process_bootstrap_delete_op(dm, base_path);
        return dm->result;
    }
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
        return SDM_ERR_METHOD_NOT_ALLOWED;
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
                   > dm->entity_ptrs.res_inst->riid) {
            assert(idx > 0);
            res->value.res_inst.insts[idx - 1] = res->value.res_inst.insts[idx];
        }
    }
    dm->entity_ptrs.res_inst->riid = FLUF_ID_INVALID;
    res->value.res_inst.inst_count--;
    return 0;
}
