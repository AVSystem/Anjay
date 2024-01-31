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

static bool is_readable_resource(sdm_res_operation_t op, bool is_bootstrap) {
    return op == SDM_RES_R || op == SDM_RES_RM || op == SDM_RES_RW
           || op == SDM_RES_RWM || (is_bootstrap && op == SDM_RES_BS_RW);
}

static size_t get_readable_res_count_from_resource(sdm_res_t *res,
                                                   bool is_bootstrap) {
    if (!is_readable_resource(res->res_spec->operation, is_bootstrap)) {
        return 0;
    }
    if (!_sdm_is_multi_instance_resource(res->res_spec->operation)) {
        return 1;
    }
    return res->value.res_inst.inst_count;
}

static size_t get_readable_res_count_from_instance(sdm_obj_inst_t *inst,
                                                   bool is_bootstrap) {
    size_t count = 0;

    for (uint16_t idx = 0; idx < inst->res_count; idx++) {
        count += get_readable_res_count_from_resource(&inst->resources[idx],
                                                      is_bootstrap);
    }
    return count;
}

static size_t get_readable_res_count_from_object(sdm_obj_t *obj,
                                                 bool is_bootstrap) {
    size_t count = 0;

    for (uint16_t idx = 0; idx < obj->inst_count; idx++) {
        count += get_readable_res_count_from_instance(obj->insts[idx],
                                                      is_bootstrap);
    }
    return count;
}

static int get_readable_res_count_and_set_start_level(sdm_data_model_t *dm) {
    _sdm_read_ctx_t *read_ctx = &dm->op_ctx.read_ctx;
    _sdm_entity_ptrs_t *entity_ptrs = &dm->entity_ptrs;
    if (entity_ptrs->res_inst) {
        read_ctx->level = FLUF_ID_RIID;
        read_ctx->total_op_count =
                is_readable_resource(entity_ptrs->res->res_spec->operation,
                                     dm->boostrap_operation)
                        ? 1
                        : 0;
    } else if (entity_ptrs->res) {
        read_ctx->level = FLUF_ID_RID;
        read_ctx->total_op_count =
                get_readable_res_count_from_resource(entity_ptrs->res,
                                                     dm->boostrap_operation);
    } else if (entity_ptrs->inst) {
        read_ctx->level = FLUF_ID_IID;
        read_ctx->total_op_count =
                get_readable_res_count_from_instance(entity_ptrs->inst,
                                                     dm->boostrap_operation);
    } else {
        read_ctx->level = FLUF_ID_OID;
        read_ctx->total_op_count =
                get_readable_res_count_from_object(entity_ptrs->obj,
                                                   dm->boostrap_operation);
    }
    if (!read_ctx->total_op_count) {
        sdm_log(ERROR, "No readable resources");
        return SDM_ERR_NOT_FOUND;
    }

    dm->op_count = read_ctx->total_op_count;
    return 0;
}

static int get_read_value(fluf_io_out_entry_t *out_record,
                          sdm_obj_t *obj,
                          sdm_obj_inst_t *obj_inst,
                          sdm_res_t *res,
                          sdm_res_inst_t *res_inst) {
    out_record->type = res->res_spec->type;
    out_record->path =
            res_inst ? FLUF_MAKE_RESOURCE_INSTANCE_PATH(obj->oid, obj_inst->iid,
                                                        res->res_spec->rid,
                                                        res_inst->riid)
                     : FLUF_MAKE_RESOURCE_PATH(obj->oid, obj_inst->iid,
                                               res->res_spec->rid);
    if (res->res_handlers && res->res_handlers->res_read) {
        return res->res_handlers->res_read(obj, obj_inst, res, res_inst,
                                           &out_record->value);
    }
    out_record->value =
            res_inst ? res_inst->res_value.value : res->value.res_value.value;
    return 0;
}

static void increment_idx_starting_from_res(_sdm_read_ctx_t *read_ctx,
                                            uint16_t res_count) {
    read_ctx->res_idx++;
    if (read_ctx->res_idx == res_count) {
        read_ctx->res_idx = 0;
        read_ctx->inst_idx++;
    }
}

static void increment_idx_starting_from_res_inst(_sdm_read_ctx_t *read_ctx,
                                                 uint16_t res_count,
                                                 uint16_t res_inst_count) {
    read_ctx->res_inst_idx++;
    if (read_ctx->res_inst_idx == res_inst_count) {
        read_ctx->res_inst_idx = 0;
        increment_idx_starting_from_res(read_ctx, res_count);
    }
}

static void get_readable_resource(sdm_data_model_t *dm) {
    _sdm_read_ctx_t *read_ctx = &dm->op_ctx.read_ctx;
    _sdm_entity_ptrs_t *entity_ptrs = &dm->entity_ptrs;
    sdm_obj_t *obj = entity_ptrs->obj;
    sdm_res_t *res;
    bool found = false;
    while (!found) {
        if (read_ctx->level == FLUF_ID_OID) {
            assert(read_ctx->inst_idx < obj->inst_count);
            entity_ptrs->inst = obj->insts[read_ctx->inst_idx];
        }
        assert(read_ctx->res_idx < entity_ptrs->inst->res_count);
        res = &entity_ptrs->inst->resources[read_ctx->res_idx];
        if (is_readable_resource(res->res_spec->operation,
                                 dm->boostrap_operation)) {
            if (_sdm_is_multi_instance_resource(res->res_spec->operation)
                    && res->value.res_inst.inst_count) {
                assert(read_ctx->res_inst_idx < res->value.res_inst.inst_count);
                entity_ptrs->res_inst =
                        res->value.res_inst.insts[read_ctx->res_inst_idx];
                increment_idx_starting_from_res_inst(
                        read_ctx, entity_ptrs->inst->res_count,
                        res->value.res_inst.inst_count);
                found = true;
            } else {
                if (!_sdm_is_multi_instance_resource(
                            res->res_spec->operation)) {
                    found = true;
                    entity_ptrs->res_inst = NULL;
                }
                increment_idx_starting_from_res(read_ctx,
                                                entity_ptrs->inst->res_count);
            }
        } else {
            increment_idx_starting_from_res(read_ctx,
                                            entity_ptrs->inst->res_count);
        }
    }
    entity_ptrs->res = res;
}

int sdm_get_read_entry(sdm_data_model_t *dm, fluf_io_out_entry_t *out_record) {
    assert(dm && out_record);
    _sdm_read_ctx_t *read_ctx = &dm->op_ctx.read_ctx;
    _sdm_entity_ptrs_t *entity_ptrs = &dm->entity_ptrs;

    if (dm->operation != FLUF_OP_DM_READ) {
        sdm_log(ERROR, "Incorrect operation");
        dm->result = SDM_ERR_LOGIC;
        return dm->result;
    }
    _SDM_ONGOING_OP_ERROR_CHECK(dm);
    _SDM_ONGOING_OP_COUNT_ERROR_CHECK(dm);

    if (read_ctx->level == FLUF_ID_OID || read_ctx->level == FLUF_ID_IID) {
        get_readable_resource(dm);
    }
    // there is nothing to do on FLUF_ID_RIID level
    if (read_ctx->level == FLUF_ID_RID) {
        if (_sdm_is_multi_instance_resource(
                    entity_ptrs->res->res_spec->operation)) {
            assert(read_ctx->res_inst_idx
                   < entity_ptrs->res->value.res_inst.inst_count);
            entity_ptrs->res_inst = entity_ptrs->res->value.res_inst
                                            .insts[read_ctx->res_inst_idx++];
        }
        // there is nothing to do on FLUF_ID_RID level for single-instance case
    }

    dm->result = get_read_value(out_record, entity_ptrs->obj, entity_ptrs->inst,
                                entity_ptrs->res, entity_ptrs->res_inst);
    if (dm->result) {
        return dm->result;
    }

    dm->op_count--;
    return dm->op_count > 0 ? 0 : SDM_LAST_RECORD;
}

int sdm_get_readable_res_count(sdm_data_model_t *dm, size_t *out_res_count) {
    assert(dm && out_res_count);
    if (dm->operation != FLUF_OP_DM_READ) {
        sdm_log(ERROR, "Incorrect operation");
        dm->result = SDM_ERR_LOGIC;
        return dm->result;
    }
    _SDM_ONGOING_OP_ERROR_CHECK(dm);
    *out_res_count = dm->op_ctx.read_ctx.total_op_count;
    return 0;
}

int sdm_get_composite_readable_res_count(sdm_data_model_t *dm,
                                         const fluf_uri_path_t *path,
                                         size_t *out_res_count) {
    assert(dm && path && out_res_count);
    if (dm->operation != FLUF_OP_DM_READ_COMP) {
        dm->result = SDM_ERR_LOGIC;
        return dm->result;
    }
    _SDM_ONGOING_OP_ERROR_CHECK(dm);

    _sdm_entity_ptrs_t ptrs;
    sdm_obj_t *obj;
    dm->result =
            _sdm_get_obj_ptr_call_operation_begin(dm, path->ids[FLUF_ID_OID],
                                                  &obj);
    if (dm->result) {
        return dm->result;
    }
    dm->result = _sdm_get_obj_ptrs(obj, path, &ptrs);
    if (dm->result) {
        return dm->result;
    }

    size_t count = 0;

    if (ptrs.res_inst) {
        count = is_readable_resource(ptrs.res->res_spec->operation, false) ? 1
                                                                           : 0;
    } else if (ptrs.res) {
        count = get_readable_res_count_from_resource(ptrs.res, false);
    } else if (ptrs.inst) {
        count = get_readable_res_count_from_instance(ptrs.inst, false);
    } else {
        count = get_readable_res_count_from_object(ptrs.obj, false);
    }

    *out_res_count = count;
    return 0;
}

int sdm_get_composite_read_entry(sdm_data_model_t *dm,
                                 const fluf_uri_path_t *path,
                                 fluf_io_out_entry_t *out_record) {
    assert(dm && out_record && path);

    if (dm->operation != FLUF_OP_DM_READ_COMP) {
        sdm_log(ERROR, "Incorrect operation");
        dm->result = SDM_ERR_LOGIC;
        return dm->result;
    }
    _SDM_ONGOING_OP_ERROR_CHECK(dm);

    _sdm_read_ctx_t *read_ctx = &dm->op_ctx.read_ctx;
    _sdm_entity_ptrs_t *entity_ptrs = &dm->entity_ptrs;
    // new path
    if (!fluf_uri_path_equal(path, &read_ctx->path) && dm->op_count == 0) {
        read_ctx->path = *path;

        sdm_obj_t *obj;
        dm->result = _sdm_get_obj_ptr_call_operation_begin(
                dm, path->ids[FLUF_ID_OID], &obj);
        if (dm->result) {
            return dm->result;
        }
        dm->result = _sdm_get_obj_ptrs(obj, path, &dm->entity_ptrs);
        if (dm->result) {
            return dm->result;
        }

        dm->result = get_readable_res_count_and_set_start_level(dm);
        if (dm->result) {
            return dm->result;
        }
        read_ctx->inst_idx = 0;
        read_ctx->res_idx = 0;
        read_ctx->res_inst_idx = 0;
    }

    _SDM_ONGOING_OP_COUNT_ERROR_CHECK(dm);

    if (read_ctx->level == FLUF_ID_OID || read_ctx->level == FLUF_ID_IID) {
        get_readable_resource(dm);
    }
    // there is nothing to do on FLUF_ID_RIID level
    if (read_ctx->level == FLUF_ID_RID) {
        if (_sdm_is_multi_instance_resource(
                    entity_ptrs->res->res_spec->operation)) {
            assert(read_ctx->res_inst_idx
                   < entity_ptrs->res->value.res_inst.inst_count);
            entity_ptrs->res_inst = entity_ptrs->res->value.res_inst
                                            .insts[read_ctx->res_inst_idx++];
        }
        // there is nothing to do on FLUF_ID_RID level for single-instance case
    }

    dm->result = get_read_value(out_record, entity_ptrs->obj, entity_ptrs->inst,
                                entity_ptrs->res, entity_ptrs->res_inst);
    if (dm->result) {
        return dm->result;
    }

    dm->op_count--;
    return dm->op_count > 0 ? 0 : SDM_LAST_RECORD;
}

int _sdm_get_resource_value(sdm_data_model_t *dm,
                            const fluf_uri_path_t *path,
                            fluf_res_value_t *out_value,
                            fluf_data_type_t *out_type) {
    assert(dm && path && out_value);
    if (!fluf_uri_path_has(path, FLUF_ID_RID)) {
        goto path_error;
    }
    _sdm_entity_ptrs_t ptrs;
    int ret = _sdm_get_entity_ptrs(dm, path, &ptrs);
    if (ret) {
        return ret;
    }
    if (!is_readable_resource(ptrs.res->res_spec->operation, true)) {
        goto path_error;
    }

    bool is_multi_instance =
            _sdm_is_multi_instance_resource(ptrs.res->res_spec->operation);
    if (is_multi_instance != fluf_uri_path_has(path, FLUF_ID_RIID)) {
        goto path_error;
    }

    if (out_type) {
        *out_type = ptrs.res->res_spec->type;
    }

    if (ptrs.res->res_handlers && ptrs.res->res_handlers->res_read) {
        return ptrs.res->res_handlers->res_read(ptrs.obj, ptrs.inst, ptrs.res,
                                                ptrs.res_inst, out_value);
    }
    *out_value = is_multi_instance ? ptrs.res_inst->res_value.value
                                   : ptrs.res->value.res_value.value;

    return 0;

path_error:
    sdm_log(ERROR, "Incorect path");
    return SDM_ERR_NOT_FOUND;
}

int sdm_get_resource_type(sdm_data_model_t *dm,
                          const fluf_uri_path_t *path,
                          fluf_data_type_t *out_type) {
    assert(dm && path && out_type);
    if (!fluf_uri_path_has(path, FLUF_ID_RID)) {
        sdm_log(ERROR, "Incorect path");
        return SDM_ERR_INPUT_ARG;
    }
    _sdm_entity_ptrs_t ptrs;
    int ret = _sdm_get_entity_ptrs(dm, path, &ptrs);
    if (ret) {
        return ret;
    }
    *out_type = ptrs.res->res_spec->type;
    return 0;
}

int _sdm_begin_read_op(sdm_data_model_t *dm, const fluf_uri_path_t *base_path) {
    assert(dm && base_path && fluf_uri_path_has(base_path, FLUF_ID_OID));

    if (dm->boostrap_operation) {
        if (base_path->ids[FLUF_ID_OID] != FLUF_OBJ_ID_SERVER
                && base_path->ids[FLUF_ID_OID] != FLUF_OBJ_ID_ACCESS_CONTROL) {
            sdm_log(ERROR, "Bootstrap server can't access this object");
            dm->result = SDM_ERR_METHOD_NOT_ALLOWED;
            return dm->result;
        }
        if (fluf_uri_path_has(base_path, FLUF_ID_RID)) {
            sdm_log(ERROR, "Bootstrap read can't target resource");
            dm->result = SDM_ERR_METHOD_NOT_ALLOWED;
            return dm->result;
        }
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
    dm->result = get_readable_res_count_and_set_start_level(dm);
    if (dm->result) {
        return dm->result;
    }

    _sdm_read_ctx_t *read_ctx = &dm->op_ctx.read_ctx;
    read_ctx->inst_idx = 0;
    read_ctx->res_idx = 0;
    read_ctx->res_inst_idx = 0;
    return 0;
}
