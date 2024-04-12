/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <avsystem/commons/avs_defs.h>

#include <fluf/fluf.h>
#include <fluf/fluf_defs.h>
#include <fluf/fluf_utils.h>

#include <anj/sdm_io.h>
#include <anj/sdm_security_object.h>

#include "sdm_core.h"

#ifdef ANJ_WITH_DEFAULT_SECURITY_OBJ

enum security_resources_idx {
    SERVER_URI_IDX = 0,
    BOOTSTRAP_SERVER_IDX,
    SECURITY_MODE_IDX,
    PUBLIC_KEY_OR_IDENTITY_IDX,
    SERVER_PUBLIC_KEY_IDX,
    SECRET_KEY_IDX,
    SSID_IDX,
    _SECURITY_OBJ_RESOURCES_COUNT
};

static void initialize_instance(sdm_security_instance_t *inst) {
    assert(inst);
    memset(inst, 0, sizeof(sdm_security_instance_t));
}

static fluf_iid_t find_free_iid(sdm_security_obj_t *security_obj_ctx) {
    for (uint16_t idx = 0; idx < ANJ_SECURITY_OBJ_ALLOWED_INSTANCES_NUMBER;
         idx++) {
        if (idx >= security_obj_ctx->obj.inst_count
                || idx != security_obj_ctx->inst_ptr[idx]->iid) {
            return idx;
        }
    }
    return 0;
}

static const char *URI_SCHEME[] = { "coap", "coaps", "coap+tcp", "coaps+tcp" };

bool valid_uri_scheme(const char *uri) {
    for (size_t i = 0; i < AVS_ARRAY_SIZE(URI_SCHEME); i++) {
        if (!strncmp(uri, URI_SCHEME[i], strlen(URI_SCHEME[i]))
                && uri[strlen(URI_SCHEME[i])] == ':') {
            return true;
        }
    }
    return false;
}

static bool valid_security_mode(int64_t mode) {
    /* LwM2M specification defines modes in range 0..4 */
    return mode >= SDM_SECURITY_PSK && mode <= SDM_SECURITY_EST;
}

static int validate_instance(sdm_security_instance_t *inst) {
    if (!inst) {
        return -1;
    }
    if (!valid_uri_scheme(inst->server_uri)) {
        return -1;
    }
    if (!valid_security_mode(inst->security_mode)) {
        return -1;
    }
    if (inst->ssid >= UINT16_MAX
            || (inst->ssid == 0 && !inst->bootstrap_server)) {
        return -1;
    }
    return 0;
}

static int res_write(sdm_obj_t *obj,
                     sdm_obj_inst_t *obj_inst,
                     sdm_res_t *res,
                     sdm_res_inst_t *res_inst,
                     const fluf_res_value_t *value) {
    (void) res_inst;

    sdm_security_obj_t *ctx = AVS_CONTAINER_OF(obj, sdm_security_obj_t, obj);
    sdm_security_instance_t *sec_inst =
            &ctx->security_instances[obj_inst - ctx->inst];

    switch (res->res_spec->rid) {
    case SDM_SECURITY_RID_SERVER_URI:
        SDM_RES_WRITE_HANDLING_STRING(value, sec_inst->server_uri,
                                      sizeof(sec_inst->server_uri));
        break;
    case SDM_SECURITY_RID_BOOTSTRAP_SERVER:
        sec_inst->bootstrap_server = value->bool_value;
        break;
    case SDM_SECURITY_RID_SECURITY_MODE:
        if (!valid_security_mode(value->int_value)) {
            return SDM_ERR_BAD_REQUEST;
        }
        sec_inst->security_mode = value->int_value;
        break;
    case SDM_SECURITY_RID_PUBLIC_KEY_OR_IDENTITY:
        SDM_RES_WRITE_HANDLING_BYTES(value, sec_inst->public_key_or_identity,
                                     sizeof(sec_inst->public_key_or_identity),
                                     sec_inst->public_key_or_identity_size);
        break;
    case SDM_SECURITY_RID_SERVER_PUBLIC_KEY:
        SDM_RES_WRITE_HANDLING_BYTES(value, sec_inst->server_public_key,
                                     sizeof(sec_inst->server_public_key),
                                     sec_inst->server_public_key_size);
        break;
    case SDM_SECURITY_RID_SECRET_KEY:
        SDM_RES_WRITE_HANDLING_BYTES(value, sec_inst->secret_key,
                                     sizeof(sec_inst->secret_key),
                                     sec_inst->secret_key_size);
        break;
    case SDM_SECURITY_RID_SSID:
        if (value->int_value <= 0 || value->int_value >= UINT16_MAX) {
            return SDM_ERR_BAD_REQUEST;
        }
        sec_inst->ssid = (uint16_t) value->int_value;
        break;
    default:
        return SDM_ERR_NOT_FOUND;
    }
    return 0;
}

static int res_read(sdm_obj_t *obj,
                    sdm_obj_inst_t *obj_inst,
                    sdm_res_t *res,
                    sdm_res_inst_t *res_inst,
                    fluf_res_value_t *out_value) {
    (void) res_inst;

    sdm_security_obj_t *ctx = AVS_CONTAINER_OF(obj, sdm_security_obj_t, obj);
    sdm_security_instance_t *sec_inst =
            &ctx->security_instances[obj_inst - ctx->inst];

    switch (res->res_spec->rid) {
    case SDM_SECURITY_RID_SERVER_URI:
        out_value->bytes_or_string.data = sec_inst->server_uri;
        out_value->bytes_or_string.chunk_length = 0;
        out_value->bytes_or_string.offset = 0;
        out_value->bytes_or_string.full_length_hint = 0;
        break;
    case SDM_SECURITY_RID_BOOTSTRAP_SERVER:
        out_value->bool_value = sec_inst->bootstrap_server;
        break;
    case SDM_SECURITY_RID_SECURITY_MODE:
        out_value->int_value = sec_inst->security_mode;
        break;
    case SDM_SECURITY_RID_PUBLIC_KEY_OR_IDENTITY:
        out_value->bytes_or_string.data = sec_inst->public_key_or_identity;
        out_value->bytes_or_string.chunk_length =
                sec_inst->public_key_or_identity_size;
        out_value->bytes_or_string.offset = 0;
        out_value->bytes_or_string.full_length_hint = 0;
        break;
    case SDM_SECURITY_RID_SERVER_PUBLIC_KEY:
        out_value->bytes_or_string.data = sec_inst->server_public_key;
        out_value->bytes_or_string.chunk_length =
                sec_inst->server_public_key_size;
        out_value->bytes_or_string.offset = 0;
        out_value->bytes_or_string.full_length_hint = 0;
        break;
    case SDM_SECURITY_RID_SECRET_KEY:
        out_value->bytes_or_string.data = sec_inst->secret_key;
        out_value->bytes_or_string.chunk_length = sec_inst->secret_key_size;
        out_value->bytes_or_string.offset = 0;
        out_value->bytes_or_string.full_length_hint = 0;
        break;
    case SDM_SECURITY_RID_SSID:
        out_value->int_value = sec_inst->ssid;
        break;
    default:
        return SDM_ERR_NOT_FOUND;
    }
    return 0;
}

static const sdm_res_handlers_t RES_HANDLERS = {
    .res_read = res_read,
    .res_write = res_write,
};

static sdm_res_t g_server_obj_res[_SECURITY_OBJ_RESOURCES_COUNT] = {
    [SERVER_URI_IDX] = {
        .res_handlers = &RES_HANDLERS,
        .res_spec = &(const sdm_res_spec_t) {
            .rid = SDM_SECURITY_RID_SERVER_URI,
            .type = FLUF_DATA_TYPE_STRING,
            .operation = SDM_RES_RW
        }
    },
    [BOOTSTRAP_SERVER_IDX] = {
        .res_handlers = &RES_HANDLERS,
        .res_spec = &(const sdm_res_spec_t) {
            .rid = SDM_SECURITY_RID_BOOTSTRAP_SERVER,
            .type = FLUF_DATA_TYPE_BOOL,
            .operation = SDM_RES_RW
        }
    },
    [SECURITY_MODE_IDX] = {
        .res_handlers = &RES_HANDLERS,
        .res_spec = &(const sdm_res_spec_t) {
            .rid = SDM_SECURITY_RID_SECURITY_MODE,
            .type = FLUF_DATA_TYPE_INT,
            .operation = SDM_RES_RW
        }
    },
    [PUBLIC_KEY_OR_IDENTITY_IDX] = {
        .res_handlers = &RES_HANDLERS,
        .res_spec = &(const sdm_res_spec_t) {
            .rid = SDM_SECURITY_RID_PUBLIC_KEY_OR_IDENTITY,
            .type = FLUF_DATA_TYPE_BYTES,
            .operation = SDM_RES_RW
        }
    },
    [SERVER_PUBLIC_KEY_IDX] = {
        .res_handlers = &RES_HANDLERS,
        .res_spec = &(const sdm_res_spec_t) {
            .rid = SDM_SECURITY_RID_SERVER_PUBLIC_KEY,
            .type = FLUF_DATA_TYPE_BYTES,
            .operation = SDM_RES_RW
        }
    },
    [SECRET_KEY_IDX] = {
        .res_handlers = &RES_HANDLERS,
        .res_spec = &(const sdm_res_spec_t) {
            .rid = SDM_SECURITY_RID_SECRET_KEY,
            .type = FLUF_DATA_TYPE_BYTES,
            .operation = SDM_RES_RW
        }
    },
    [SSID_IDX] = {
        .res_handlers = &RES_HANDLERS,
        .res_spec = &(const sdm_res_spec_t) {
            .rid = SDM_SECURITY_RID_SSID,
            .type = FLUF_DATA_TYPE_INT,
            .operation = SDM_RES_RW
        }
    }
};

static int
inst_create(sdm_obj_t *obj, sdm_obj_inst_t **out_obj_inst, fluf_iid_t iid) {
    sdm_security_obj_t *ctx = AVS_CONTAINER_OF(obj, sdm_security_obj_t, obj);
    // find first free security_instance
    uint16_t free_idx = FLUF_ID_INVALID;
    for (uint16_t idx = 0; idx < ANJ_SECURITY_OBJ_ALLOWED_INSTANCES_NUMBER;
         idx++) {
        if (ctx->inst[idx].iid == FLUF_ID_INVALID) {
            free_idx = idx;
            break;
        }
    }
    assert(free_idx != FLUF_ID_INVALID);
    initialize_instance(&ctx->security_instances[free_idx]);
    *out_obj_inst = &ctx->inst[free_idx];
    ctx->new_instance_iid = iid;
    return 0;
}

static int inst_delete(sdm_obj_t *obj, sdm_obj_inst_t *obj_inst) {
    // sdm set the iid to FLUF_ID_INVALID and the instance is free to use
    return 0;
}

static int inst_reset(sdm_obj_t *obj, sdm_obj_inst_t *obj_inst) {
    sdm_security_obj_t *ctx = AVS_CONTAINER_OF(obj, sdm_security_obj_t, obj);
    initialize_instance(&ctx->security_instances[obj_inst - ctx->inst]);
    return 0;
}

static int operation_begin(sdm_obj_t *obj, fluf_op_t operation) {
    sdm_security_obj_t *ctx = AVS_CONTAINER_OF(obj, sdm_security_obj_t, obj);
    ctx->op = operation;
    // for all write operation temporary store the current state of the
    // instances
    if (operation == FLUF_OP_DM_CREATE || operation == FLUF_OP_DM_WRITE_REPLACE
            || operation == FLUF_OP_DM_WRITE_PARTIAL_UPDATE
            || operation == FLUF_OP_DM_WRITE_COMP) {
        memcpy(ctx->cache_security_instances,
               ctx->security_instances,
               sizeof(ctx->security_instances));
    }
    return 0;
}

static int operation_validate(sdm_obj_t *obj) {
    sdm_security_obj_t *ctx = AVS_CONTAINER_OF(obj, sdm_security_obj_t, obj);
    if (ctx->op == FLUF_OP_DM_CREATE || ctx->op == FLUF_OP_DM_WRITE_REPLACE
            || ctx->op == FLUF_OP_DM_WRITE_PARTIAL_UPDATE
            || ctx->op == FLUF_OP_DM_WRITE_COMP) {
        for (uint16_t idx = 0; idx < ANJ_SECURITY_OBJ_ALLOWED_INSTANCES_NUMBER;
             idx++) {
            if (ctx->inst[idx].iid != FLUF_ID_INVALID) {
                if (validate_instance(&ctx->security_instances[idx])) {
                    return SDM_ERR_BAD_REQUEST;
                }
                // check for duplications
                for (uint16_t i = 0; i < idx; i++) {
                    if (ctx->inst[i].iid != FLUF_ID_INVALID
                            && (ctx->security_instances[idx].ssid
                                        == ctx->security_instances[i].ssid
                                || !strcmp(ctx->security_instances[idx]
                                                   .server_uri,
                                           ctx->security_instances[i]
                                                   .server_uri))) {
                        return SDM_ERR_BAD_REQUEST;
                    }
                }
            }
        }
    }
    return 0;
}

static int operation_end(sdm_obj_t *obj, sdm_op_result_t result) {
    sdm_security_obj_t *ctx = AVS_CONTAINER_OF(obj, sdm_security_obj_t, obj);
    if (result == SDM_OP_RESULT_FAILURE) {
        if (ctx->op == FLUF_OP_DM_CREATE) {
            sdm_remove_obj_inst(&ctx->obj, ctx->new_instance_iid);
            ctx->new_instance_iid = FLUF_ID_INVALID;
        }
        // restore cached data
        if (ctx->op == FLUF_OP_DM_CREATE || ctx->op == FLUF_OP_DM_WRITE_REPLACE
                || ctx->op == FLUF_OP_DM_WRITE_PARTIAL_UPDATE
                || ctx->op == FLUF_OP_DM_WRITE_COMP) {
            memcpy(ctx->security_instances,
                   ctx->cache_security_instances,
                   sizeof(ctx->security_instances));
        }
    }
    return 0;
}

static const sdm_obj_handlers_t OBJ_HANDLERS = {
    .inst_create = inst_create,
    .inst_delete = inst_delete,
    .inst_reset = inst_reset,
    .operation_begin = operation_begin,
    .operation_validate = operation_validate,
    .operation_end = operation_end,
};

void sdm_security_obj_init(sdm_security_obj_t *security_obj_ctx) {
    assert(security_obj_ctx);
    memset(security_obj_ctx, 0, sizeof(*security_obj_ctx));
    security_obj_ctx->new_instance_iid = FLUF_ID_INVALID;

    security_obj_ctx->obj = (sdm_obj_t) {
        .oid = FLUF_OBJ_ID_SECURITY,
        .version = "1.0",
        .inst_count = 0,
        .max_inst_count = ANJ_SECURITY_OBJ_ALLOWED_INSTANCES_NUMBER,
        .insts = security_obj_ctx->inst_ptr,
        .obj_handlers = &OBJ_HANDLERS
    };

    for (uint16_t idx = 0; idx < ANJ_SECURITY_OBJ_ALLOWED_INSTANCES_NUMBER;
         idx++) {
        security_obj_ctx->inst[idx].resources = g_server_obj_res;
        security_obj_ctx->inst[idx].res_count = _SECURITY_OBJ_RESOURCES_COUNT;
        security_obj_ctx->inst[idx].iid = FLUF_ID_INVALID;
    }
}

int sdm_security_obj_add_instance(sdm_security_obj_t *security_obj_ctx,
                                  sdm_security_instance_init_t *instance) {
    assert(security_obj_ctx && instance && !security_obj_ctx->installed);
    assert(!instance->iid || *instance->iid == FLUF_ID_INVALID);
    assert(instance->server_uri);

    if (security_obj_ctx->obj.inst_count
            == security_obj_ctx->obj.max_inst_count) {
        sdm_log(ERROR, "Maximum number of instances reached");
        return -1;
    }

    for (uint16_t idx = 0; idx < security_obj_ctx->obj.inst_count; idx++) {
        if (instance->ssid
                && instance->ssid
                               == security_obj_ctx->security_instances[idx]
                                          .ssid) {
            sdm_log(ERROR, "Given ssid already exists");
            return -1;
        }
        if (instance->iid
                && *instance->iid == security_obj_ctx->inst[idx].iid) {
            sdm_log(ERROR, "Given iid already exists");
            return -1;
        }
        if (!strcmp(instance->server_uri,
                    security_obj_ctx->security_instances[idx].server_uri)) {
            sdm_log(ERROR, "Given uri already exists");
            return -1;
        }
    }

    sdm_security_instance_t *sec_inst =
            &security_obj_ctx
                     ->security_instances[security_obj_ctx->obj.inst_count];
    initialize_instance(sec_inst);
    if (strlen(instance->server_uri) > sizeof(sec_inst->server_uri) - 1) {
        sdm_log(ERROR, "Server URI too long");
        return -1;
    }
    if (instance->public_key_or_identity
            && instance->public_key_or_identity_size
                           > sizeof(sec_inst->public_key_or_identity)) {
        sdm_log(ERROR, "Public key or identity too long");
        return -1;
    }
    if (instance->server_public_key
            && instance->server_public_key_size
                           > sizeof(sec_inst->server_public_key)) {
        sdm_log(ERROR, "Server public key too long");
        return -1;
    }
    if (instance->secret_key
            && instance->secret_key_size > sizeof(sec_inst->secret_key)) {
        sdm_log(ERROR, "Secret key too long");
        return -1;
    }

    strcpy(sec_inst->server_uri, instance->server_uri);
    sec_inst->bootstrap_server = instance->bootstrap_server;
    sec_inst->security_mode = instance->security_mode;
    if (instance->public_key_or_identity) {
        memcpy(sec_inst->public_key_or_identity,
               instance->public_key_or_identity,
               instance->public_key_or_identity_size);
        sec_inst->public_key_or_identity_size =
                instance->public_key_or_identity_size;
    }
    if (instance->server_public_key) {
        memcpy(sec_inst->server_public_key, instance->server_public_key,
               instance->server_public_key_size);
        sec_inst->server_public_key_size = instance->server_public_key_size;
    }
    if (instance->secret_key) {
        memcpy(sec_inst->secret_key, instance->secret_key,
               instance->secret_key_size);
        sec_inst->secret_key_size = instance->secret_key_size;
    }
    sec_inst->ssid = instance->ssid;
    if (validate_instance(sec_inst)) {
        sec_inst->ssid = FLUF_ID_INVALID;
        return -1;
    }
    security_obj_ctx->inst[security_obj_ctx->obj.inst_count].iid =
            instance->iid ? *instance->iid : find_free_iid(security_obj_ctx);
    security_obj_ctx->inst_ptr[security_obj_ctx->obj.inst_count] =
            &security_obj_ctx->inst[security_obj_ctx->obj.inst_count];

    // sort inst_ptr by iid
    if (security_obj_ctx->obj.inst_count) {
        sdm_obj_inst_t *temp =
                security_obj_ctx->inst_ptr[security_obj_ctx->obj.inst_count];
        for (int32_t idx = security_obj_ctx->obj.inst_count - 1; idx >= 0;
             idx--) {
            if (security_obj_ctx->inst_ptr[idx]->iid > temp->iid) {
                security_obj_ctx->inst_ptr[idx + 1] =
                        security_obj_ctx->inst_ptr[idx];
                security_obj_ctx->inst_ptr[idx] = temp;
            } else {
                break;
            }
        }
    }
    security_obj_ctx->obj.inst_count++;
    return 0;
}

int sdm_security_obj_install(sdm_data_model_t *dm,
                             sdm_security_obj_t *security_obj_ctx) {
    assert(dm && security_obj_ctx && !security_obj_ctx->installed);
    int res = sdm_add_obj(dm, &security_obj_ctx->obj);
    if (res) {
        return res;
    }
    security_obj_ctx->installed = true;
    return 0;
}

#endif // ANJ_WITH_DEFAULT_SECURITY_OBJ
