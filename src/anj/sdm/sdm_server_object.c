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
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <avsystem/commons/avs_defs.h>

#include <fluf/fluf_defs.h>
#include <fluf/fluf_utils.h>

#include <anj/anj_config.h>
#include <anj/sdm.h>
#include <anj/sdm_io.h>
#include <anj/sdm_server_object.h>

#include "sdm_core.h"

#ifdef ANJ_WITH_DEFAULT_SERVER_OBJ

enum server_resources_idx {
    SSID_IDX,
    LIFETIME_IDX,
    DEFAULT_MIN_PERIOD_IDX,
    DEFAULT_MAX_PERIOD_IDX,
    NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE_IDX,
    BINDING_IDX,
    REGISTRATION_UPDATE_TRIGGER_IDX,
    BOOTSTRAP_REQUEST_TRIGGER_IDX,
    BOOTSTRAP_ON_REGISTRATION_FAILURE_IDX,
    MUTE_SEND_IDX,
    _SERVER_OBJ_RESOURCES_COUNT
};

// TODO: expand this array
static const char SUPPORTED_BINDING_MODES[] = "UT";

static void initialize_instance(server_instance_t *inst) {
    assert(inst);
    memset(inst, 0, sizeof(*inst));
    inst->bootstrap_on_registration_failure = true;
}

static fluf_iid_t find_free_iid(sdm_server_obj_t *server_obj_ctx) {
    for (uint16_t idx = 0; idx < ANJ_SERVER_OBJ_ALLOWED_INSTANCES_NUMBER;
         idx++) {
        if (idx >= server_obj_ctx->obj.inst_count
                || idx != server_obj_ctx->inst_ptr[idx]->iid) {
            return idx;
        }
    }
    return 0;
}

static int is_valid_binding_mode(const char *binding_mode) {
    if (!*binding_mode) {
        return -1;
    }
    for (const char *p = binding_mode; *p; ++p) {
        if (!strchr(SUPPORTED_BINDING_MODES, *p)) {
            return -1;
        }
        if (strchr(p + 1, *p)) {
            // duplications
            return -1;
        }
    }
    return 0;
}

static int validate_instance(server_instance_t *inst) {
    if (!inst) {
        return -1;
    }
    if (inst->ssid == 0 || inst->ssid == UINT16_MAX
            || inst->default_max_period < 0 || inst->default_min_period < 0
            || (inst->default_max_period != 0
                && inst->default_max_period < inst->default_min_period)
            || inst->lifetime <= 0 || is_valid_binding_mode(inst->binding)) {
        return -1;
    }
    return 0;
}

static int res_execute(sdm_obj_t *obj,
                       sdm_obj_inst_t *obj_inst,
                       sdm_res_t *res,
                       const char *execute_arg,
                       size_t execute_arg_len) {
    (void) execute_arg;
    (void) execute_arg_len;

    sdm_server_obj_t *ctx = AVS_CONTAINER_OF(obj, sdm_server_obj_t, obj);
    server_instance_t *serv_inst = &ctx->server_instance[obj_inst - ctx->inst];

    switch (res->res_spec->rid) {
    case SDM_SERVER_RID_REGISTRATION_UPDATE_TRIGGER:
        if (!ctx->server_obj_handlers.registration_update_trigger) {
            return SDM_ERR_METHOD_NOT_ALLOWED;
        }
        return ctx->server_obj_handlers.registration_update_trigger(
                serv_inst->ssid, ctx->server_obj_handlers.arg_ptr);
        break;
    case SDM_SERVER_RID_BOOTSTRAP_REQUEST_TRIGGER:
        if (!ctx->server_obj_handlers.bootstrap_request_trigger) {
            return SDM_ERR_METHOD_NOT_ALLOWED;
        }
        return ctx->server_obj_handlers.bootstrap_request_trigger(
                serv_inst->ssid, ctx->server_obj_handlers.arg_ptr);
        break;
    default:
        return SDM_ERR_NOT_FOUND;
    }

    return 0;
}

static int res_write(sdm_obj_t *obj,
                     sdm_obj_inst_t *obj_inst,
                     sdm_res_t *res,
                     sdm_res_inst_t *res_inst,
                     const fluf_res_value_t *value) {
    (void) res_inst;

    sdm_server_obj_t *ctx = AVS_CONTAINER_OF(obj, sdm_server_obj_t, obj);
    server_instance_t *serv_inst = &ctx->server_instance[obj_inst - ctx->inst];

    switch (res->res_spec->rid) {
    case SDM_SERVER_RID_SSID:
        if (value->int_value <= 0 || value->int_value >= UINT16_MAX) {
            return SDM_ERR_BAD_REQUEST;
        }
        serv_inst->ssid = (uint16_t) value->int_value;
        break;
    case SDM_SERVER_RID_LIFETIME:
        serv_inst->lifetime = value->int_value;
        break;
    case SDM_SERVER_RID_DEFAULT_MIN_PERIOD:
        serv_inst->default_min_period = value->int_value;
        break;
    case SDM_SERVER_RID_DEFAULT_MAX_PERIOD:
        serv_inst->default_max_period = value->int_value;
        break;
    case SDM_SERVER_RID_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE:
        serv_inst->notification_storing = value->bool_value;
        break;
    case SDM_SERVER_RID_BINDING:
        SDM_RES_WRITE_HANDLING_STRING(
                value, serv_inst->binding, AVS_ARRAY_SIZE(serv_inst->binding));
        break;
    case SDM_SERVER_RID_BOOTSTRAP_ON_REGISTRATION_FAILURE:
        serv_inst->bootstrap_on_registration_failure = value->bool_value;
        break;
    case SDM_SERVER_RID_MUTE_SEND:
        serv_inst->mute_send = value->bool_value;
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

    sdm_server_obj_t *ctx = AVS_CONTAINER_OF(obj, sdm_server_obj_t, obj);
    server_instance_t *serv_inst = &ctx->server_instance[obj_inst - ctx->inst];

    switch (res->res_spec->rid) {
    case SDM_SERVER_RID_SSID:
        out_value->int_value = serv_inst->ssid;
        break;
    case SDM_SERVER_RID_LIFETIME:
        out_value->int_value = serv_inst->lifetime;
        break;
    case SDM_SERVER_RID_DEFAULT_MIN_PERIOD:
        out_value->int_value = serv_inst->default_min_period;
        break;
    case SDM_SERVER_RID_DEFAULT_MAX_PERIOD:
        out_value->int_value = serv_inst->default_max_period;
        break;
    case SDM_SERVER_RID_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE:
        out_value->bool_value = serv_inst->notification_storing;
        break;
    case SDM_SERVER_RID_BINDING:
        out_value->bytes_or_string.data = serv_inst->binding;
        out_value->bytes_or_string.chunk_length = strlen(serv_inst->binding);
        out_value->bytes_or_string.offset = 0;
        out_value->bytes_or_string.full_length_hint = 0;
        break;
    case SDM_SERVER_RID_BOOTSTRAP_ON_REGISTRATION_FAILURE:
        out_value->bool_value = serv_inst->bootstrap_on_registration_failure;
        break;
    case SDM_SERVER_RID_MUTE_SEND:
        out_value->bool_value = serv_inst->mute_send;
        break;
    default:
        return SDM_ERR_NOT_FOUND;
    }

    return 0;
}

static const sdm_res_handlers_t RES_HANDLERS = {
    .res_read = res_read,
    .res_write = res_write,
    .res_execute = res_execute
};

static sdm_res_t g_server_obj_res[_SERVER_OBJ_RESOURCES_COUNT] = {
    [SSID_IDX] = {
        .res_handlers = &RES_HANDLERS,
        .res_spec = &(const sdm_res_spec_t) {
            .rid = SDM_SERVER_RID_SSID,
            .type = FLUF_DATA_TYPE_INT,
            .operation = SDM_RES_R
        }
    },
    [LIFETIME_IDX] = {
        .res_handlers = &RES_HANDLERS,
        .res_spec = &(const sdm_res_spec_t) {
            .rid = SDM_SERVER_RID_LIFETIME,
            .type = FLUF_DATA_TYPE_INT,
            .operation = SDM_RES_RW
        }
    },
    [DEFAULT_MIN_PERIOD_IDX] = {
        .res_handlers = &RES_HANDLERS,
        .res_spec = &(const sdm_res_spec_t) {
            .rid = SDM_SERVER_RID_DEFAULT_MIN_PERIOD,
            .type = FLUF_DATA_TYPE_INT,
            .operation = SDM_RES_RW
        }
    },
    [DEFAULT_MAX_PERIOD_IDX] = {
        .res_handlers = &RES_HANDLERS,
        .res_spec = &(const sdm_res_spec_t) {
            .rid = SDM_SERVER_RID_DEFAULT_MAX_PERIOD,
            .type = FLUF_DATA_TYPE_INT,
            .operation = SDM_RES_RW
        }
    },
    [NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE_IDX] = {
        .res_handlers = &RES_HANDLERS,
        .res_spec = &(const sdm_res_spec_t) {
            .rid = SDM_SERVER_RID_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE,
            .type = FLUF_DATA_TYPE_BOOL,
            .operation = SDM_RES_RW
        }
    },
    [BINDING_IDX] = {
        .res_handlers = &RES_HANDLERS,
        .res_spec = &(const sdm_res_spec_t) {
            .rid = SDM_SERVER_RID_BINDING,
            .type = FLUF_DATA_TYPE_STRING,
            .operation = SDM_RES_RW
        }
    },
    [REGISTRATION_UPDATE_TRIGGER_IDX] = {
        .res_handlers = &RES_HANDLERS,
        .res_spec = &(const sdm_res_spec_t) {
            .rid = SDM_SERVER_RID_REGISTRATION_UPDATE_TRIGGER,
            .operation = SDM_RES_E
        }
    },
    [BOOTSTRAP_REQUEST_TRIGGER_IDX] = {
        .res_handlers = &RES_HANDLERS,
        .res_spec = &(const sdm_res_spec_t) {
            .rid = SDM_SERVER_RID_BOOTSTRAP_REQUEST_TRIGGER,
            .operation = SDM_RES_E
        }
    },
    [BOOTSTRAP_ON_REGISTRATION_FAILURE_IDX] = {
        .res_handlers = &RES_HANDLERS,
        .res_spec = &(const sdm_res_spec_t) {
            .rid = SDM_SERVER_RID_BOOTSTRAP_ON_REGISTRATION_FAILURE,
            .type = FLUF_DATA_TYPE_BOOL,
            .operation = SDM_RES_R
        }
    },
    [MUTE_SEND_IDX] = {
        .res_handlers = &RES_HANDLERS,
        .res_spec = &(const sdm_res_spec_t) {
            .rid = SDM_SERVER_RID_MUTE_SEND,
            .type = FLUF_DATA_TYPE_BOOL,
            .operation = SDM_RES_RW
        }
    }
};

static int
inst_create(sdm_obj_t *obj, sdm_obj_inst_t **out_obj_inst, fluf_iid_t iid) {
    sdm_server_obj_t *ctx = AVS_CONTAINER_OF(obj, sdm_server_obj_t, obj);
    // find first free server_instance
    uint16_t free_idx = UINT16_MAX;
    for (uint16_t idx = 0; idx < ANJ_SERVER_OBJ_ALLOWED_INSTANCES_NUMBER;
         idx++) {
        if (ctx->inst[idx].iid == FLUF_ID_INVALID) {
            free_idx = idx;
            break;
        }
    }
    assert(free_idx != UINT16_MAX);
    initialize_instance(&ctx->server_instance[free_idx]);
    *out_obj_inst = &ctx->inst[free_idx];
    ctx->new_instance_iid = iid;
    return 0;
}

static int inst_delete(sdm_obj_t *obj, sdm_obj_inst_t *obj_inst) {
    // sdm set the iid to FLUF_ID_INVALID and the instance is free to use
    return 0;
}

static int inst_reset(sdm_obj_t *obj, sdm_obj_inst_t *obj_inst) {
    sdm_server_obj_t *ctx = AVS_CONTAINER_OF(obj, sdm_server_obj_t, obj);
    initialize_instance(&ctx->server_instance[obj_inst - ctx->inst]);
    return 0;
}

static int operation_begin(sdm_obj_t *obj, fluf_op_t operation) {
    sdm_server_obj_t *ctx = AVS_CONTAINER_OF(obj, sdm_server_obj_t, obj);
    ctx->op = operation;
    // for all write operation temporary store the current state of the
    // instances
    if (operation == FLUF_OP_DM_CREATE || operation == FLUF_OP_DM_WRITE_REPLACE
            || operation == FLUF_OP_DM_WRITE_PARTIAL_UPDATE
            || operation == FLUF_OP_DM_WRITE_COMP) {
        memcpy(ctx->cache_server_instance,
               ctx->server_instance,
               sizeof(ctx->server_instance));
    }
    return 0;
}

static int operation_validate(sdm_obj_t *obj) {
    sdm_server_obj_t *ctx = AVS_CONTAINER_OF(obj, sdm_server_obj_t, obj);
    if (ctx->op == FLUF_OP_DM_CREATE || ctx->op == FLUF_OP_DM_WRITE_REPLACE
            || ctx->op == FLUF_OP_DM_WRITE_PARTIAL_UPDATE
            || ctx->op == FLUF_OP_DM_WRITE_COMP) {
        for (uint16_t idx = 0; idx < ANJ_SERVER_OBJ_ALLOWED_INSTANCES_NUMBER;
             idx++) {
            if (ctx->inst[idx].iid != FLUF_ID_INVALID) {
                if (validate_instance(&ctx->server_instance[idx])) {
                    return SDM_ERR_BAD_REQUEST;
                }
                // check for SSID duplications
                for (uint16_t i = 0; i < idx; i++) {
                    if (ctx->inst[i].iid != FLUF_ID_INVALID
                            && ctx->server_instance[idx].ssid
                                           == ctx->server_instance[i].ssid) {
                        return SDM_ERR_BAD_REQUEST;
                    }
                }
            }
        }
    }
    return 0;
}

static int operation_end(sdm_obj_t *obj, sdm_op_result_t result) {
    sdm_server_obj_t *ctx = AVS_CONTAINER_OF(obj, sdm_server_obj_t, obj);
    if (result == SDM_OP_RESULT_FAILURE) {
        if (ctx->op == FLUF_OP_DM_CREATE) {
            sdm_remove_obj_inst(&ctx->obj, ctx->new_instance_iid);
            ctx->new_instance_iid = FLUF_ID_INVALID;
        }
        // restore cached data
        if (ctx->op == FLUF_OP_DM_CREATE || ctx->op == FLUF_OP_DM_WRITE_REPLACE
                || ctx->op == FLUF_OP_DM_WRITE_PARTIAL_UPDATE
                || ctx->op == FLUF_OP_DM_WRITE_COMP) {
            memcpy(ctx->server_instance,
                   ctx->cache_server_instance,
                   sizeof(ctx->server_instance));
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

void sdm_server_obj_init(sdm_server_obj_t *server_obj_ctx) {
    assert(server_obj_ctx);
    memset(server_obj_ctx, 0, sizeof(*server_obj_ctx));
    server_obj_ctx->new_instance_iid = FLUF_ID_INVALID;

    server_obj_ctx->obj = (sdm_obj_t) {
        .oid = SDM_SERVER_OID,
        .version = "1.1",
        .inst_count = 0,
        .max_inst_count = ANJ_SERVER_OBJ_ALLOWED_INSTANCES_NUMBER,
        .insts = server_obj_ctx->inst_ptr,
        .obj_handlers = &OBJ_HANDLERS
    };

    for (uint16_t idx = 0; idx < ANJ_SERVER_OBJ_ALLOWED_INSTANCES_NUMBER;
         idx++) {
        server_obj_ctx->inst[idx].resources = g_server_obj_res;
        server_obj_ctx->inst[idx].res_count = _SERVER_OBJ_RESOURCES_COUNT;
        server_obj_ctx->inst[idx].iid = FLUF_ID_INVALID;
    }
}

int sdm_server_obj_add_instance(sdm_server_obj_t *server_obj_ctx,
                                const sdm_server_instance_init_t *instance) {
    assert(server_obj_ctx && instance && !server_obj_ctx->installed);
    assert(!instance->iid || *instance->iid != FLUF_ID_INVALID);
    assert(instance->binding);

    if (server_obj_ctx->obj.inst_count == server_obj_ctx->obj.max_inst_count) {
        sdm_log(ERROR, "Maximum number of instances reached");
        return -1;
    }

    for (uint16_t idx = 0; idx < server_obj_ctx->obj.inst_count; idx++) {
        if (instance->ssid == server_obj_ctx->server_instance[idx].ssid) {
            sdm_log(ERROR, "Given ssid already exists");
            return -1;
        }
        if (instance->iid && *instance->iid == server_obj_ctx->inst[idx].iid) {
            sdm_log(ERROR, "Given iid already exists");
            return -1;
        }
    }

    server_instance_t *serv_inst =
            &server_obj_ctx->server_instance[server_obj_ctx->obj.inst_count];
    if (strlen(instance->binding) > sizeof(serv_inst->binding) - 1) {
        sdm_log(ERROR, "Binding string too long");
        return -1;
    }
    initialize_instance(serv_inst);
    strcpy(serv_inst->binding, instance->binding);
    serv_inst->ssid = instance->ssid;
    if (instance->bootstrap_on_registration_failure) {
        serv_inst->bootstrap_on_registration_failure =
                *instance->bootstrap_on_registration_failure;
    }
    serv_inst->default_max_period = instance->default_max_period;
    serv_inst->default_min_period = instance->default_min_period;
    serv_inst->lifetime = instance->lifetime;
    serv_inst->mute_send = instance->mute_send;
    serv_inst->notification_storing = instance->notification_storing;
    if (validate_instance(serv_inst)) {
        serv_inst->ssid = FLUF_ID_INVALID;
        return -1;
    }
    server_obj_ctx->inst[server_obj_ctx->obj.inst_count].iid =
            instance->iid ? *instance->iid : find_free_iid(server_obj_ctx);
    server_obj_ctx->inst_ptr[server_obj_ctx->obj.inst_count] =
            &server_obj_ctx->inst[server_obj_ctx->obj.inst_count];

    // sort inst_ptr by iid
    if (server_obj_ctx->obj.inst_count) {
        sdm_obj_inst_t *temp =
                server_obj_ctx->inst_ptr[server_obj_ctx->obj.inst_count];
        for (int32_t idx = server_obj_ctx->obj.inst_count - 1; idx >= 0;
             idx--) {
            if (server_obj_ctx->inst_ptr[idx]->iid > temp->iid) {
                server_obj_ctx->inst_ptr[idx + 1] =
                        server_obj_ctx->inst_ptr[idx];
                server_obj_ctx->inst_ptr[idx] = temp;
            } else {
                break;
            }
        }
    }
    server_obj_ctx->obj.inst_count++;
    return 0;
}

int sdm_server_obj_install(sdm_data_model_t *dm,
                           sdm_server_obj_t *server_obj_ctx,
                           sdm_server_obj_handlers_t *handlers) {
    assert(dm && server_obj_ctx && !server_obj_ctx->installed);
    server_obj_ctx->installed = true;
    if (handlers) {
        server_obj_ctx->server_obj_handlers = *handlers;
    }
    return sdm_add_obj(dm, &server_obj_ctx->obj);
}

int sdm_server_find_instance_iid(sdm_server_obj_t *server_obj_ctx,
                                 uint16_t ssid,
                                 fluf_iid_t *out_iid) {
    assert(server_obj_ctx && out_iid);
    for (uint16_t idx = 0; idx < server_obj_ctx->obj.inst_count; idx++) {
        if (server_obj_ctx
                    ->server_instance[server_obj_ctx->inst_ptr[idx]
                                      - server_obj_ctx->inst]
                    .ssid
                == ssid) {
            *out_iid = server_obj_ctx->inst[idx].iid;
            return 0;
        }
    }
    return -1;
}

#endif // ANJ_WITH_DEFAULT_SERVER_OBJ
