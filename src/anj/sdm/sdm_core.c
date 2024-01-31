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

static int finish_ongoing_operation(sdm_data_model_t *dm) {
    sdm_op_result_t op_result;
    if (!dm->is_transactional) {
        op_result = SDM_OP_RESULT_SUCCESS_NOT_MODIFIED;
    } else {
        op_result = SDM_OP_RESULT_SUCCESS_MODIFIED;
        for (uint16_t idx = 0; idx < dm->objs_count && !dm->result; idx++) {
            sdm_obj_t *obj = dm->objs[idx];
            if (obj->in_transaction && obj->obj_handlers
                    && obj->obj_handlers->operation_validate) {
                dm->result = obj->obj_handlers->operation_validate(obj);
            }
        }
    }
    for (uint16_t idx = 0; idx < dm->objs_count; idx++) {
        sdm_obj_t *obj = dm->objs[idx];
        if (obj->in_transaction) {
            obj->in_transaction = false;
            if (obj->obj_handlers && obj->obj_handlers->operation_end) {
                if (dm->result) {
                    obj->obj_handlers->operation_end(obj,
                                                     SDM_OP_RESULT_FAILURE);
                } else {
                    dm->result =
                            obj->obj_handlers->operation_end(obj, op_result);
                }
            }
        }
    }
    dm->op_in_progress = false;
    return dm->result;
}

static inline sdm_obj_t *find_obj(sdm_data_model_t *dm, fluf_oid_t oid) {
    for (uint16_t idx = 0; idx < dm->objs_count; idx++) {
        if (dm->objs[idx]->oid == oid) {
            return dm->objs[idx];
        }
    }
    return NULL;
}

static inline sdm_obj_inst_t *find_inst(sdm_obj_t *obj, fluf_iid_t iid) {
    for (uint16_t idx = 0; idx < obj->inst_count; idx++) {
        if (obj->insts[idx]->iid == iid) {
            return obj->insts[idx];
        }
    }
    return NULL;
}

static inline sdm_res_t *find_res(sdm_obj_inst_t *inst, fluf_rid_t rid) {
    for (uint16_t idx = 0; idx < inst->res_count; idx++) {
        if (inst->resources[idx].res_spec->rid == rid) {
            return &inst->resources[idx];
        }
    }
    return NULL;
}

static inline sdm_res_inst_t *find_res_inst(sdm_res_t *res, fluf_riid_t riid) {
    for (uint16_t idx = 0; idx < res->value.res_inst.inst_count; idx++) {
        if (res->value.res_inst.insts[idx]->riid == riid) {
            return res->value.res_inst.insts[idx];
        }
    }
    return NULL;
}

static int call_operation_begin(sdm_obj_t *obj, fluf_op_t operation) {
    if (!obj->in_transaction) {
        obj->in_transaction = true;
        if (obj->obj_handlers && obj->obj_handlers->operation_begin) {
            return obj->obj_handlers->operation_begin(obj, operation);
        }
    }
    return 0;
}

int _sdm_get_obj_ptr_call_operation_begin(sdm_data_model_t *dm,
                                          fluf_oid_t oid,
                                          sdm_obj_t **out_obj) {
    *out_obj = NULL;
    *out_obj = find_obj(dm, oid);
    if (!*out_obj) {
        sdm_log(ERROR, "Object not found in data model");
        return SDM_ERR_NOT_FOUND;
    }
    return call_operation_begin(*out_obj, dm->operation);
}

int _sdm_get_obj_ptrs(sdm_obj_t *obj,
                      const fluf_uri_path_t *path,
                      _sdm_entity_ptrs_t *out_ptrs) {
    assert(fluf_uri_path_has(path, FLUF_ID_OID));
    assert(obj);

    sdm_obj_inst_t *inst = NULL;
    sdm_res_t *res = NULL;
    sdm_res_inst_t *res_inst = NULL;

    if (!fluf_uri_path_has(path, FLUF_ID_IID)) {
        goto finalize;
    }

    inst = find_inst(obj, path->ids[FLUF_ID_IID]);
    if (!inst) {
        goto not_found;
    }
    if (!fluf_uri_path_has(path, FLUF_ID_RID)) {
        goto finalize;
    }

    res = find_res(inst, path->ids[FLUF_ID_RID]);
    if (!res) {
        goto not_found;
    }
    if (!fluf_uri_path_has(path, FLUF_ID_RIID)) {
        goto finalize;
    }
    if (!_sdm_is_multi_instance_resource(res->res_spec->operation)) {
        sdm_log(ERROR, "Resource is not multi-instance");
        return SDM_ERR_NOT_FOUND;
    }

    res_inst = find_res_inst(res, path->ids[FLUF_ID_RIID]);
    if (!res_inst) {
        goto not_found;
    }

finalize:
    out_ptrs->obj = obj;
    out_ptrs->inst = inst;
    out_ptrs->res = res;
    out_ptrs->res_inst = res_inst;

    return 0;

not_found:
    sdm_log(ERROR, "Record not found in data model");
    return SDM_ERR_NOT_FOUND;
}

int _sdm_get_entity_ptrs(sdm_data_model_t *dm,
                         const fluf_uri_path_t *path,
                         _sdm_entity_ptrs_t *out_ptrs) {
    assert(fluf_uri_path_has(path, FLUF_ID_OID));
    sdm_obj_t *obj = NULL;

    obj = find_obj(dm, path->ids[FLUF_ID_OID]);
    if (!obj) {
        sdm_log(ERROR, "Object not found in data model");
        return SDM_ERR_NOT_FOUND;
    }
    return _sdm_get_obj_ptrs(obj, path, out_ptrs);
}

int sdm_operation_begin(sdm_data_model_t *dm,
                        fluf_op_t operation,
                        bool is_bootstrap_request,
                        const fluf_uri_path_t *path) {
    assert(dm);
    if (dm->op_in_progress) {
        sdm_log(ERROR, "Operation already underway");
        return SDM_ERR_LOGIC;
    }

    dm->operation = operation;
    dm->boostrap_operation = is_bootstrap_request;
    dm->is_transactional = false;
    dm->op_in_progress = true;
    dm->result = 0;

    switch (operation) {
    case FLUF_OP_DM_READ_COMP:
        dm->op_count = 0;
        dm->is_transactional = true;
        dm->op_ctx.read_ctx.path = FLUF_MAKE_ROOT_PATH();
        return 0;
    case FLUF_OP_DM_WRITE_COMP:
        sdm_log(ERROR, "Composite operations are not supported yet");
        return SDM_ERR_INPUT_ARG;
    case FLUF_OP_REGISTER:
    case FLUF_OP_UPDATE:
        return _sdm_begin_register_op(dm);
    case FLUF_OP_DM_DISCOVER:
        if (dm->boostrap_operation) {
            return _sdm_begin_bootstrap_discover_op(dm, path);
        } else {
            return _sdm_begin_discover_op(dm, path);
        }
    case FLUF_OP_DM_EXECUTE:
        return _sdm_begin_execute_op(dm, path);
    case FLUF_OP_DM_READ:
        return _sdm_begin_read_op(dm, path);
    case FLUF_OP_DM_WRITE_REPLACE:
    case FLUF_OP_DM_WRITE_PARTIAL_UPDATE:
        return _sdm_begin_write_op(dm, path);
    case FLUF_OP_DM_CREATE:
        return _sdm_begin_create_op(dm, path);
    case FLUF_OP_DM_DELETE:
        return _sdm_process_delete_op(dm, path);
    default:
        break;
    }

    sdm_log(ERROR, "Incorrect operation type");
    return SDM_ERR_INPUT_ARG;
}

int sdm_operation_end(sdm_data_model_t *dm) {
    assert(dm);
    _SDM_ONGOING_OP_ERROR_CHECK(dm);

    if (dm->operation == FLUF_OP_DM_CREATE && !dm->result) {
        // HACK: Create operation is ended without any record being added so we
        // create an instance without IID specified.
        if (!dm->op_ctx.write_ctx.instance_created) {
            dm->result = _sdm_create_object_instance(dm, FLUF_ID_INVALID);
        }
    }

    return finish_ongoing_operation(dm);
}

void sdm_initialize(sdm_data_model_t *dm,
                    sdm_obj_t **objs_array,
                    uint16_t objs_array_size) {
    assert(dm && objs_array && objs_array_size);

    memset(dm, 0, sizeof(*dm));
    dm->objs = objs_array;
    dm->max_allowed_objs_number = objs_array_size;
}

#ifndef NDEBUG
static int check_res(sdm_res_t *res) {
    if (res->res_spec->operation == SDM_RES_E
            && (!res->res_handlers || !res->res_handlers->res_execute)) {
        goto res_error;
    }
    if (res->res_spec->operation != SDM_RES_E
            && !(res->res_spec->type == FLUF_DATA_TYPE_BYTES
                 || res->res_spec->type == FLUF_DATA_TYPE_STRING
                 || res->res_spec->type == FLUF_DATA_TYPE_INT
                 || res->res_spec->type == FLUF_DATA_TYPE_DOUBLE
                 || res->res_spec->type == FLUF_DATA_TYPE_BOOL
                 || res->res_spec->type == FLUF_DATA_TYPE_OBJLNK
                 || res->res_spec->type == FLUF_DATA_TYPE_UINT
                 || res->res_spec->type == FLUF_DATA_TYPE_TIME
                 || res->res_spec->type == FLUF_DATA_TYPE_EXTERNAL_BYTES
                 || res->res_spec->type == FLUF_DATA_TYPE_EXTERNAL_STRING)) {
        goto res_error;
    }
    if (_sdm_is_multi_instance_resource(res->res_spec->operation)
            && res->value.res_inst.inst_count) {
        if (res->value.res_inst.inst_count > res->value.res_inst.max_inst_count
                || res->value.res_inst.max_inst_count == UINT16_MAX) {
            goto res_error;
        }
        fluf_rid_t last_riid;
        for (uint16_t idx = 0; idx < res->value.res_inst.inst_count; idx++) {
            if (!res->value.res_inst.insts[idx]
                    || res->value.res_inst.insts[idx]->riid == FLUF_ID_INVALID
                    || (idx != 0
                        && res->value.res_inst.insts[idx]->riid <= last_riid)) {
                goto res_error;
            }
            last_riid = res->value.res_inst.insts[idx]->riid;
        }
    }
    return 0;

res_error:
    sdm_log(ERROR, "Incorrectly defined resource %" PRIu16, res->res_spec->rid);
    return SDM_ERR_INPUT_ARG;
}

int _sdm_check_obj(sdm_obj_t *obj) {
    if (obj->inst_count == 0) {
        return 0;
    }
    if (!obj->insts) {
        goto obj_error;
    }
    if (obj->max_inst_count < obj->inst_count
            || obj->max_inst_count == UINT16_MAX) {
        goto obj_error;
    }

    fluf_iid_t last_iid;
    for (uint16_t idx = 0; idx < obj->inst_count; idx++) {
        sdm_obj_inst_t *inst = obj->insts[idx];
        if (!inst || inst->iid == FLUF_ID_INVALID
                || (idx != 0 && inst->iid <= last_iid)
                || _sdm_check_obj_instance(inst)) {
            goto obj_error;
        }
        last_iid = inst->iid;
    }
    return 0;

obj_error:
    sdm_log(ERROR, "Incorrectly defined object %" PRIu16, obj->oid);
    return SDM_ERR_INPUT_ARG;
}

int _sdm_check_obj_instance(sdm_obj_inst_t *inst) {
    if (inst->res_count && !inst->resources) {
        goto instance_error;
    }
    if (inst->res_count == 0) {
        return 0;
    }

    fluf_rid_t last_rid;
    for (uint16_t res_idx = 0; res_idx < inst->res_count; res_idx++) {
        sdm_res_t *res = &inst->resources[res_idx];
        if (!res->res_spec || res->res_spec->rid == FLUF_ID_INVALID
                || (res_idx != 0 && res->res_spec->rid <= last_rid)
                || check_res(res)) {
            goto instance_error;
        }
        last_rid = res->res_spec->rid;
    }

    return 0;

instance_error:
    sdm_log(ERROR, "Incorrectly defined instance %" PRIu16, inst->iid);
    return SDM_ERR_INPUT_ARG;
}

#endif // NDEBUG

int sdm_add_obj(sdm_data_model_t *dm, sdm_obj_t *obj) {
    assert(dm && obj);
    assert(!fluf_validate_obj_version(obj->version));
    assert(!_sdm_check_obj(obj));

    if (dm->op_in_progress) {
        return SDM_ERR_LOGIC;
    }
    if (dm->max_allowed_objs_number == dm->objs_count) {
        sdm_log(ERROR, "No space for a new object");
        return SDM_ERR_MEMORY;
    }

    uint16_t idx;
    for (idx = 0; idx < dm->objs_count; idx++) {
        if (dm->objs[idx]->oid > obj->oid) {
            break;
        }
        if (dm->objs[idx]->oid == obj->oid) {
            sdm_log(ERROR, "Object %" PRIu16 " exists", obj->oid);
            return SDM_ERR_LOGIC;
        }
    }

    for (uint16_t i = dm->objs_count; i > idx; i--) {
        dm->objs[i] = dm->objs[i - 1];
    }
    dm->objs[idx] = obj;
    dm->objs_count++;

    obj->in_transaction = false;
    return 0;
}

int sdm_remove_obj(sdm_data_model_t *dm, fluf_oid_t oid) {
    assert(dm);
    if (dm->op_in_progress) {
        return SDM_ERR_LOGIC;
    }

    uint16_t idx;
    bool found = false;
    for (idx = 0; idx < dm->objs_count; idx++) {
        if (dm->objs[idx]->oid == oid) {
            found = true;
            break;
        }
    }
    if (!found) {
        sdm_log(ERROR, "Object %" PRIu16 " not found", oid);
        return SDM_ERR_NOT_FOUND;
    }
    dm->objs[idx] = NULL;
    for (uint16_t i = idx; i < dm->objs_count - 1; i++) {
        dm->objs[i] = dm->objs[i + 1];
    }
    dm->objs_count--;
    return 0;
}
