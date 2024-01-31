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

static int update_res_val(sdm_data_model_t *dm, fluf_res_value_t *value) {
    _sdm_entity_ptrs_t *entity_ptrs = &dm->entity_ptrs;
    if (entity_ptrs->res->res_handlers
            && entity_ptrs->res->res_handlers->res_write) {
        return entity_ptrs->res->res_handlers->res_write(entity_ptrs->obj,
                                                         entity_ptrs->inst,
                                                         entity_ptrs->res,
                                                         entity_ptrs->res_inst,
                                                         value);
    }

    sdm_res_value_t *res_val;
    if (_sdm_is_multi_instance_resource(
                entity_ptrs->res->res_spec->operation)) {
        res_val = &entity_ptrs->res_inst->res_value;
    } else {
        res_val = &entity_ptrs->res->value.res_value;
    }

    switch (entity_ptrs->res->res_spec->type) {
    case FLUF_DATA_TYPE_INT:
    case FLUF_DATA_TYPE_DOUBLE:
    case FLUF_DATA_TYPE_BOOL:
    case FLUF_DATA_TYPE_OBJLNK:
    case FLUF_DATA_TYPE_UINT:
    case FLUF_DATA_TYPE_TIME:
        res_val->value = *value;
        break;
    case FLUF_DATA_TYPE_STRING:
    case FLUF_DATA_TYPE_BYTES:
        // check if this is last chunk
        if (value->bytes_or_string.chunk_length + value->bytes_or_string.offset
                == value->bytes_or_string.full_length_hint) {
            res_val->value.bytes_or_string.full_length_hint =
                    value->bytes_or_string.full_length_hint;
            res_val->value.bytes_or_string.chunk_length =
                    value->bytes_or_string.full_length_hint;
        }
        if (value->bytes_or_string.chunk_length + value->bytes_or_string.offset
                > res_val->resource_buffer_size) {
            sdm_log(ERROR, "Resource buffer to small");
            return SDM_ERR_MEMORY;
        }
        char *write_ptr = (char *) res_val->value.bytes_or_string.data;
        memcpy(&write_ptr[value->bytes_or_string.offset],
               value->bytes_or_string.data,
               value->bytes_or_string.chunk_length);
        break;
    default:
        break;
    }

    return 0;
}

static int resource_type_check(sdm_data_model_t *dm,
                               fluf_io_out_entry_t *record) {
    _sdm_entity_ptrs_t *entity_ptrs = &dm->entity_ptrs;
    if (entity_ptrs->res->res_spec->type != record->type) {
        // Bootstrap Server can try to modify
        // FLUF_DATA_TYPE_EXTERNAL_STRING/BYTES.
        if (dm->boostrap_operation
                && ((record->type == FLUF_DATA_TYPE_STRING
                     && entity_ptrs->res->res_spec->type
                                == FLUF_DATA_TYPE_EXTERNAL_STRING)
                    || (record->type == FLUF_DATA_TYPE_BYTES
                        && entity_ptrs->res->res_spec->type
                                   == FLUF_DATA_TYPE_EXTERNAL_BYTES))) {
            return 0;
        }
        return SDM_ERR_BAD_REQUEST;
    }
    return 0;
}

static bool is_writable_resource(sdm_res_operation_t op, bool is_bootstrap) {
    return op == SDM_RES_W || op == SDM_RES_RW || op == SDM_RES_WM
           || op == SDM_RES_RWM || (is_bootstrap && op == SDM_RES_BS_RW);
}

static int begin_write_replace_operation(sdm_data_model_t *dm) {
    fluf_uri_path_t *path = &dm->op_ctx.write_ctx.path;
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

    _sdm_entity_ptrs_t *entity_ptrs = &dm->entity_ptrs;
    sdm_res_t *res = entity_ptrs->res;
    if (fluf_uri_path_is(path, FLUF_ID_IID)) {
        if (!obj->obj_handlers || !obj->obj_handlers->inst_reset) {
            sdm_log(ERROR, "inst_reset handler not defined");
            dm->result = SDM_ERR_INTERNAL;
            return dm->result;
        }
        dm->result = obj->obj_handlers->inst_reset(obj, entity_ptrs->inst);
        if (dm->result) {
            sdm_log(ERROR, "inst_reset failed");
            return dm->result;
        }
    } else if (fluf_uri_path_is(path, FLUF_ID_RID)
               && _sdm_is_multi_instance_resource(res->res_spec->operation)) {
        // remove all res_insts
        while (res->value.res_inst.inst_count) {
            entity_ptrs->res_inst =
                    res->value.res_inst
                            .insts[res->value.res_inst.inst_count - 1];
            dm->result = _sdm_delete_res_instance(dm);
            if (dm->result) {
                return dm->result;
            }
        }
    }

    return 0;
}

static int handle_res_instances(sdm_data_model_t *dm,
                                fluf_io_out_entry_t *record) {
    _sdm_entity_ptrs_t *entity_ptrs = &dm->entity_ptrs;
    sdm_res_t *res = entity_ptrs->res;

    // found res_inst or create new
    for (uint16_t idx = 0; idx < res->value.res_inst.inst_count; idx++) {
        if (res->value.res_inst.insts[idx]->riid
                == record->path.ids[FLUF_ID_RIID]) {
            entity_ptrs->res_inst = res->value.res_inst.insts[idx];
            return 0;
        }
    }

    if (res->value.res_inst.inst_count == res->value.res_inst.max_inst_count) {
        sdm_log(ERROR, "No space for new resource instance");
        return SDM_ERR_MEMORY;
    }
    if (!res->res_handlers || !res->res_handlers->res_inst_create) {
        sdm_log(ERROR, "res_inst_create handler not defined");
        return SDM_ERR_INTERNAL;
    }

    entity_ptrs->res_inst = NULL;
    int ret = entity_ptrs->res->res_handlers->res_inst_create(
            entity_ptrs->obj,
            entity_ptrs->inst,
            res,
            &entity_ptrs->res_inst,
            record->path.ids[FLUF_ID_RIID]);
    if (ret || !entity_ptrs->res_inst) {
        sdm_log(ERROR, "res_inst_create failed");
        if (!ret) {
            ret = SDM_ERR_INTERNAL;
        }
        sdm_log(ERROR, "res_inst_create failed");
        return ret;
    }
    entity_ptrs->res_inst->riid = record->path.ids[FLUF_ID_RIID];
    // udpate res_inst array
    uint16_t idx_to_write = UINT16_MAX;
    for (uint16_t idx = 0; idx < res->value.res_inst.inst_count; idx++) {
        if (res->value.res_inst.insts[idx]->riid
                > record->path.ids[FLUF_ID_RIID]) {
            idx_to_write = idx;
            break;
        }
    }
    if (idx_to_write == UINT16_MAX) {
        res->value.res_inst.insts[res->value.res_inst.inst_count] =
                entity_ptrs->res_inst;
    } else {
        for (uint16_t idx = res->value.res_inst.inst_count; idx > idx_to_write;
             idx--) {
            res->value.res_inst.insts[idx] = res->value.res_inst.insts[idx - 1];
        }
        res->value.res_inst.insts[idx_to_write] = entity_ptrs->res_inst;
    }
    res->value.res_inst.inst_count++;
    return 0;
}

static int verify_resource_before_writing(sdm_data_model_t *dm,
                                          fluf_io_out_entry_t *record) {
    if (!is_writable_resource(dm->entity_ptrs.res->res_spec->operation,
                              dm->boostrap_operation)) {
        sdm_log(ERROR, "Resource is not writable");
        return SDM_ERR_BAD_REQUEST;
    } else if (resource_type_check(dm, record)) {
        sdm_log(ERROR, "Invalid record type");
        return SDM_ERR_BAD_REQUEST;
    } else if (_sdm_is_multi_instance_resource(
                       dm->entity_ptrs.res->res_spec->operation)
               != fluf_uri_path_has(&record->path, FLUF_ID_RIID)) {
        sdm_log(ERROR, "Writing to invalid path");
        return SDM_ERR_METHOD_NOT_ALLOWED;
    }
    return 0;
}

int sdm_write_entry(sdm_data_model_t *dm, fluf_io_out_entry_t *record) {
    assert(dm && record);

    if (dm->operation != FLUF_OP_DM_CREATE
            && dm->operation != FLUF_OP_DM_WRITE_REPLACE
            && dm->operation != FLUF_OP_DM_WRITE_PARTIAL_UPDATE) {
        sdm_log(ERROR, "Incorrect operation");
        dm->result = SDM_ERR_LOGIC;
        return dm->result;
    }
    _SDM_ONGOING_OP_ERROR_CHECK(dm);

    if (!fluf_uri_path_has(&record->path, FLUF_ID_RID)) {
        sdm_log(ERROR, "Invalid path");
        dm->result = SDM_ERR_BAD_REQUEST;
        return dm->result;
    }
    if (fluf_uri_path_outside_base(&record->path, &dm->op_ctx.write_ctx.path)) {
        sdm_log(ERROR, "Write record outside of request path");
        dm->result = SDM_ERR_BAD_REQUEST;
        return dm->result;
    }

    if (dm->operation == FLUF_OP_DM_CREATE
            && !dm->op_ctx.write_ctx.instance_created) {
        dm->op_ctx.write_ctx.instance_created = true;
        // HACK: We can create instance now because we didn't know its ID
        // before.
        dm->result =
                _sdm_create_object_instance(dm, record->path.ids[FLUF_ID_IID]);
        if (dm->result) {
            return dm->result;
        }
    }

    // lack of resource instance is not the error
    dm->result = _sdm_get_obj_ptrs(
            dm->entity_ptrs.obj,
            &FLUF_MAKE_RESOURCE_PATH(record->path.ids[FLUF_ID_OID],
                                     record->path.ids[FLUF_ID_IID],
                                     record->path.ids[FLUF_ID_RID]),
            &dm->entity_ptrs);
    if (dm->result) {
        return dm->result;
    }

    dm->result = verify_resource_before_writing(dm, record);
    if (dm->result) {
        return dm->result;
    }

    if (_sdm_is_multi_instance_resource(
                dm->entity_ptrs.res->res_spec->operation)) {
        dm->result = handle_res_instances(dm, record);
        if (dm->result) {
            return dm->result;
        }
    }

    dm->result = update_res_val(dm, &record->value);
    return dm->result;
}

int _sdm_begin_write_op(sdm_data_model_t *dm,
                        const fluf_uri_path_t *base_path) {
    assert(dm && base_path && fluf_uri_path_has(base_path, FLUF_ID_IID));
    dm->is_transactional = true;
    dm->op_ctx.write_ctx.path = *base_path;

    if (dm->operation == FLUF_OP_DM_WRITE_REPLACE) {
        return begin_write_replace_operation(dm);
    } else {
        sdm_obj_t *obj;
        dm->result = _sdm_get_obj_ptr_call_operation_begin(
                dm, base_path->ids[FLUF_ID_OID], &obj);
        if (dm->result) {
            return dm->result;
        }
        dm->result = _sdm_get_obj_ptrs(obj, base_path, &dm->entity_ptrs);
        return dm->result;
    }
}
