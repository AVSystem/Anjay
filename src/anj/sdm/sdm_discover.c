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

#define _SDM_OBJ_SERVER_SSID_RID 0
#define _SDM_OBJ_SECURITY_SERVER_URI_RID 0
#define _SDM_OBJ_SECURITY_BOOTSTRAP_SERVER_RID 1
#define _SDM_OBJ_SECURITY_SSID_RID 10
#define _SDM_OBJ_SECURITY_OSCORE_RID 17

static void get_security_obj_ssid_value(sdm_data_model_t *dm,
                                        sdm_obj_t *obj,
                                        sdm_obj_inst_t *inst,
                                        const uint16_t **ssid) {
    fluf_res_value_t value;
    fluf_data_type_t type;
    *ssid = NULL;
    _sdm_disc_ctx_t *disc_ctx = &dm->op_ctx.disc_ctx;
    if (!_sdm_get_resource_value(
                dm,
                &FLUF_MAKE_RESOURCE_PATH(
                        obj->oid,
                        inst->iid,
                        _SDM_OBJ_SECURITY_BOOTSTRAP_SERVER_RID),
                &value, &type)
            && type == FLUF_DATA_TYPE_BOOL && !value.bool_value
            && !_sdm_get_resource_value(
                       dm,
                       &FLUF_MAKE_RESOURCE_PATH(obj->oid, inst->iid,
                                                _SDM_OBJ_SECURITY_SSID_RID),
                       &value, &type)
            && type == FLUF_DATA_TYPE_INT) {
        disc_ctx->ssid = (uint16_t) value.uint_value;
        *ssid = &disc_ctx->ssid;
    }
}

static void get_security_instance_ssid_for_oscore_obj(sdm_data_model_t *dm,
                                                      fluf_iid_t iid,
                                                      const uint16_t **ssid) {
    // Instance have _SDM_OBJ_SECURITY_OSCORE_RID equal to given oid and iid,
    // it's not a bootstrap server instance and SSID value is present.
    fluf_res_value_t value;
    fluf_data_type_t type;
    for (uint16_t idx = 0; idx < dm->objs[0]->inst_count; idx++) {
        if (!_sdm_get_resource_value(
                    dm,
                    &FLUF_MAKE_RESOURCE_PATH(dm->objs[0]->oid,
                                             dm->objs[0]->insts[idx]->iid,
                                             _SDM_OBJ_SECURITY_OSCORE_RID),
                    &value, &type)
                && type == FLUF_DATA_TYPE_OBJLNK && value.objlnk.iid == iid) {
            assert(value.objlnk.oid == FLUF_OBJ_ID_OSCORE);
            get_security_obj_ssid_value(dm, dm->objs[0],
                                        dm->objs[0]->insts[idx], ssid);
        }
    }
}

static void get_ssid_and_uri(sdm_data_model_t *dm,
                             sdm_obj_t *obj,
                             sdm_obj_inst_t *inst,
                             const uint16_t **ssid,
                             const char **uri) {
    if (obj->oid != FLUF_OBJ_ID_SECURITY && obj->oid != FLUF_OBJ_ID_SERVER
            && obj->oid != FLUF_OBJ_ID_OSCORE) {
        return;
    }
    *ssid = NULL;
    *uri = NULL;

    _sdm_disc_ctx_t *disc_ctx = &dm->op_ctx.disc_ctx;
    fluf_res_value_t value;
    fluf_data_type_t type;
    // SSID and URI are added if instance is not related to Bootstrap-Server.
    // Resource /1 of Security Object is checked to determine SSID and URI
    // presence. If there are no resources needed in the operation, we will not
    // add the URI and SSID to the message, without any error returns.
    if (obj->oid == FLUF_OBJ_ID_SECURITY) {
        get_security_obj_ssid_value(dm, obj, inst, ssid);
        if (!*ssid) {
            return;
        }
        fluf_uri_path_t server_uri_path =
                FLUF_MAKE_RESOURCE_PATH(obj->oid, inst->iid,
                                        _SDM_OBJ_SECURITY_SERVER_URI_RID);
        // FLUF_DATA_TYPE_EXTERNAL_STRING is not allowed here
        if (!_sdm_get_resource_value(dm, &server_uri_path, &value, &type)
                && type == FLUF_DATA_TYPE_STRING) {
            *uri = (const char *) value.bytes_or_string.data;
        }
    } else if (obj->oid == FLUF_OBJ_ID_SERVER) {
        if (!_sdm_get_resource_value(
                    dm,
                    &FLUF_MAKE_RESOURCE_PATH(obj->oid, inst->iid,
                                             _SDM_OBJ_SERVER_SSID_RID),
                    &value, &type)
                && type == FLUF_DATA_TYPE_INT) {
            disc_ctx->ssid = (uint16_t) value.int_value;
            *ssid = &disc_ctx->ssid;
        }
    } else if (obj->oid == FLUF_OBJ_ID_OSCORE) {
        // find Security Object Instance related with this OSCORE Instance and
        // read SSID
        get_security_instance_ssid_for_oscore_obj(dm, inst->iid, ssid);
    }
}

int _sdm_begin_bootstrap_discover_op(sdm_data_model_t *dm,
                                     const fluf_uri_path_t *base_path) {
    assert(dm);
    if (base_path && fluf_uri_path_has(base_path, FLUF_ID_IID)) {
        sdm_log(ERROR, "Bootstrap discover can't target object instance");
        dm->result = SDM_ERR_INPUT_ARG;
        return dm->result;
    }
    _sdm_disc_ctx_t *disc_ctx = &dm->op_ctx.disc_ctx;

    dm->op_count = 0;
    disc_ctx->obj_idx = 0;
    disc_ctx->inst_idx = 0;
    disc_ctx->level = FLUF_ID_OID;
    bool all_objects = true;
    if (base_path && fluf_uri_path_has(base_path, FLUF_ID_OID)) {
        all_objects = false;
    }
    for (uint16_t idx = 0; idx < dm->objs_count; idx++) {
        if (all_objects || dm->objs[idx]->oid == base_path->ids[FLUF_ID_OID]) {
            if (!all_objects) {
                disc_ctx->obj_idx = idx;
            }
            dm->objs[idx]->in_transaction = true;
            if (dm->objs[idx]->obj_handlers
                    && dm->objs[idx]->obj_handlers->operation_begin) {
                dm->result = dm->objs[idx]->obj_handlers->operation_begin(
                        dm->objs[idx], dm->operation);
                if (dm->result) {
                    return dm->result;
                }
            }
            dm->op_count = dm->op_count + 1 + dm->objs[idx]->inst_count;
        }
    }
    return 0;
}

int sdm_get_bootstrap_discover_record(sdm_data_model_t *dm,
                                      fluf_uri_path_t *out_path,
                                      const char **out_version,
                                      const uint16_t **ssid,
                                      const char **uri) {
    assert(dm && out_path && out_version && ssid && uri);

    _sdm_disc_ctx_t *disc_ctx = &dm->op_ctx.disc_ctx;
    assert(disc_ctx->obj_idx < dm->objs_count);

    if (dm->operation != FLUF_OP_DM_DISCOVER || !dm->boostrap_operation) {
        sdm_log(ERROR, "Incorrect operation");
        dm->result = SDM_ERR_LOGIC;
        return dm->result;
    }
    _SDM_ONGOING_OP_ERROR_CHECK(dm);
    _SDM_ONGOING_OP_COUNT_ERROR_CHECK(dm);

    *out_version = NULL;
    *ssid = NULL;
    *uri = NULL;

    sdm_obj_t *obj = dm->objs[disc_ctx->obj_idx];

    if (disc_ctx->level == FLUF_ID_OID) {
        *out_path = FLUF_MAKE_OBJECT_PATH(obj->oid);
        *out_version = obj->version;

        if (obj->inst_count) {
            disc_ctx->level = FLUF_ID_IID;
        } else {
            disc_ctx->obj_idx++;
        }
    } else {
        assert(disc_ctx->inst_idx < obj->inst_count);
        sdm_obj_inst_t *inst = obj->insts[disc_ctx->inst_idx];
        *out_path = FLUF_MAKE_INSTANCE_PATH(obj->oid, inst->iid);
        get_ssid_and_uri(dm, obj, inst, ssid, uri);

        disc_ctx->inst_idx++;
        if (disc_ctx->inst_idx == obj->inst_count) {
            disc_ctx->inst_idx = 0;
            disc_ctx->obj_idx++;
            disc_ctx->level = FLUF_ID_OID;
        }
    }

    dm->op_count--;
    return dm->op_count > 0 ? 0 : SDM_LAST_RECORD;
}

int _sdm_begin_discover_op(sdm_data_model_t *dm,
                           const fluf_uri_path_t *base_path) {
    assert(dm);
    assert(base_path && fluf_uri_path_has(base_path, FLUF_ID_OID)
           && !fluf_uri_path_has(base_path, FLUF_ID_RIID));
    _sdm_disc_ctx_t *disc_ctx = &dm->op_ctx.disc_ctx;
    dm->op_count = 0;
    bool all_instances = !fluf_uri_path_has(base_path, FLUF_ID_IID);
    bool all_resources =
            all_instances || !fluf_uri_path_has(base_path, FLUF_ID_RID);
    disc_ctx->inst_idx = 0;
    disc_ctx->res_idx = 0;
    disc_ctx->res_inst_idx = 0;
    if (all_instances) {
        disc_ctx->level = FLUF_ID_OID;
        dm->op_count++;
    } else if (all_resources) {
        disc_ctx->level = FLUF_ID_IID;
    } else {
        disc_ctx->level = FLUF_ID_RID;
    }

    dm->result = _sdm_get_obj_ptr_call_operation_begin(
            dm, base_path->ids[FLUF_ID_OID], &dm->entity_ptrs.obj);
    if (dm->result) {
        return dm->result;
    }

    sdm_obj_t *obj = dm->entity_ptrs.obj;

    for (uint16_t idx = 0; idx < obj->inst_count; idx++) {
        sdm_obj_inst_t *inst = obj->insts[idx];
        if (!all_instances && base_path->ids[FLUF_ID_IID] == inst->iid) {
            disc_ctx->inst_idx = idx;
        }
        if (all_instances || base_path->ids[FLUF_ID_IID] == inst->iid) {
            if (all_resources) {
                dm->op_count++;
            }
            for (uint16_t res_idx = 0; res_idx < inst->res_count; res_idx++) {
                sdm_res_t *res = &inst->resources[res_idx];
                if (!all_resources
                        && base_path->ids[FLUF_ID_RID] == res->res_spec->rid) {
                    disc_ctx->res_idx = res_idx;
                }
                if (all_resources
                        || base_path->ids[FLUF_ID_RID] == res->res_spec->rid) {
                    dm->op_count++;
                    if (_sdm_is_multi_instance_resource(
                                res->res_spec->operation)) {
                        dm->op_count += res->value.res_inst.inst_count;
                    }
                }
            }
        }
    }
    return 0;
}

static void get_inst_record(sdm_data_model_t *dm, fluf_uri_path_t *out_path) {
    _sdm_disc_ctx_t *disc_ctx = &dm->op_ctx.disc_ctx;
    sdm_obj_t *obj = dm->entity_ptrs.obj;
    assert(disc_ctx->inst_idx < obj->inst_count);
    sdm_obj_inst_t *inst = obj->insts[disc_ctx->inst_idx];
    *out_path = FLUF_MAKE_INSTANCE_PATH(obj->oid, inst->iid);
    if (inst->res_count) {
        disc_ctx->level = FLUF_ID_RID;
    } else {
        disc_ctx->inst_idx++;
    }
}

static void increment_idx_starting_from_res(_sdm_disc_ctx_t *disc_ctx,
                                            uint16_t res_count) {
    disc_ctx->res_idx++;
    if (disc_ctx->res_idx == res_count) {
        disc_ctx->res_idx = 0;
        disc_ctx->inst_idx++;
        disc_ctx->level = FLUF_ID_IID;
    }
}

static void increment_idx_starting_from_res_inst(_sdm_disc_ctx_t *disc_ctx,
                                                 uint16_t res_count,
                                                 uint16_t res_inst_count) {
    disc_ctx->res_inst_idx++;
    if (disc_ctx->res_inst_idx == res_inst_count) {
        disc_ctx->res_inst_idx = 0;
        disc_ctx->level = FLUF_ID_RID;
        increment_idx_starting_from_res(disc_ctx, res_count);
    }
}

static void get_res_record(sdm_data_model_t *dm,
                           fluf_uri_path_t *out_path,
                           const uint16_t **out_dim) {
    _sdm_disc_ctx_t *disc_ctx = &dm->op_ctx.disc_ctx;
    sdm_obj_t *obj = dm->entity_ptrs.obj;
    sdm_obj_inst_t *inst = obj->insts[disc_ctx->inst_idx];
    assert(disc_ctx->res_idx < inst->res_count);
    sdm_res_t *res = &inst->resources[disc_ctx->res_idx];
    *out_path =
            FLUF_MAKE_RESOURCE_PATH(obj->oid, inst->iid, res->res_spec->rid);
    bool is_multi_instance =
            _sdm_is_multi_instance_resource(res->res_spec->operation);
    if (is_multi_instance) {
        *out_dim = &res->value.res_inst.inst_count;
        if (res->value.res_inst.inst_count) {
            disc_ctx->level = FLUF_ID_RIID;
        }
    }
    if (!is_multi_instance || !res->value.res_inst.inst_count) {
        increment_idx_starting_from_res(disc_ctx, inst->res_count);
    }
}

static void get_res_inst_record(sdm_data_model_t *dm,
                                fluf_uri_path_t *out_path) {
    _sdm_disc_ctx_t *disc_ctx = &dm->op_ctx.disc_ctx;
    sdm_obj_t *obj = dm->entity_ptrs.obj;
    sdm_obj_inst_t *inst = obj->insts[disc_ctx->inst_idx];
    sdm_res_t *res = &inst->resources[disc_ctx->res_idx];
    assert(disc_ctx->res_inst_idx < res->value.res_inst.inst_count);
    sdm_res_inst_t *res_inst =
            res->value.res_inst.insts[disc_ctx->res_inst_idx];
    *out_path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(
            obj->oid, inst->iid, res->res_spec->rid, res_inst->riid);
    increment_idx_starting_from_res_inst(disc_ctx, inst->res_count,
                                         res->value.res_inst.inst_count);
}

int sdm_get_discover_record(sdm_data_model_t *dm,
                            fluf_uri_path_t *out_path,
                            const char **out_version,
                            const uint16_t **out_dim) {
    assert(dm && out_path && out_version && out_dim);
    _sdm_disc_ctx_t *disc_ctx = &dm->op_ctx.disc_ctx;

    if (dm->operation != FLUF_OP_DM_DISCOVER || dm->boostrap_operation) {
        sdm_log(ERROR, "Incorrect operation");
        dm->result = SDM_ERR_LOGIC;
        return dm->result;
    }
    _SDM_ONGOING_OP_ERROR_CHECK(dm);
    _SDM_ONGOING_OP_COUNT_ERROR_CHECK(dm);

    *out_version = NULL;
    *out_dim = NULL;

    if (disc_ctx->level == FLUF_ID_OID) {
        *out_path = FLUF_MAKE_OBJECT_PATH(dm->entity_ptrs.obj->oid);
        *out_version = dm->entity_ptrs.obj->version;
        disc_ctx->level = FLUF_ID_IID;
    } else if (disc_ctx->level == FLUF_ID_IID) {
        get_inst_record(dm, out_path);
    } else if (disc_ctx->level == FLUF_ID_RID) {
        get_res_record(dm, out_path, out_dim);
    } else if (disc_ctx->level == FLUF_ID_RIID) {
        get_res_inst_record(dm, out_path);
    }

    dm->op_count--;
    return dm->op_count > 0 ? 0 : SDM_LAST_RECORD;
}
