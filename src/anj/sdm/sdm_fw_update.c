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

#include <anj/anj_config.h>
#include <anj/sdm.h>
#include <anj/sdm_fw_update.h>
#include <anj/sdm_io.h>

#ifdef ANJ_WITH_FOTA_OBJECT

/* FOTA method support */
#    ifndef ANJ_FOTA_PUSH_METHOD_SUPPORTED
// if push method if not supported, it's safe to assume that pull is -
// guaranteed by a condition check in fluf_config.h 0 -> pull only
#        define METHODS_SUPPORTED 0
#    else // ANJ_FOTA_PUSH_METHOD_SUPPORTED
#        ifdef ANJ_FOTA_PULL_METHOD_SUPPORTED
// this means push is supported as well
// 2 -> pull & push
#            define METHODS_SUPPORTED 2
#        else // ANJ_FOTA_PULL_METHOD_SUPPORTED
// only push enabled
// 1 -> push
#            define METHODS_SUPPORTED 1
#        endif // ANJ_FOTA_PULL_METHOD_SUPPORTED
#    endif     // ANJ_FOTA_PUSH_METHOD_SUPPORTED

enum {
    PACKAGE_RES_IDX,
    PACKAGE_URI_RES_IDX,
    UPDATE_RES_IDX,
    STATE_RES_IDX,
    UPDATE_RESULT_RES_IDX,
    PKG_NAME_RES_IDX,
    PKG_VER_RES_IDX,
    SUPORTED_PROTOCOLS_RES_IDX,
    DELIVERY_METHOD_RES_IDX,
    _RESOURCES_COUNT
};

AVS_STATIC_ASSERT(_RESOURCES_COUNT == SDM_FW_UPDATE_RESOURCES_COUNT,
                  resources_count_consistent);

static inline bool writing_last_data_chunk(const fluf_res_value_t *value) {
    return value->bytes_or_string.chunk_length + value->bytes_or_string.offset
           == value->bytes_or_string.full_length_hint;
}

static inline bool is_reset_request_package(const fluf_res_value_t *value) {
    if (value->bytes_or_string.full_length_hint == 1
            && value->bytes_or_string.offset == 0
            && ((char *) value->bytes_or_string.data)[0] == '\0') {
        return true;
    }
    return false;
}

static inline bool is_reset_request_uri(const fluf_res_value_t *value) {
    if (value->bytes_or_string.full_length_hint == 0
            && value->bytes_or_string.offset == 0
            && value->bytes_or_string.chunk_length == 0) {
        return true;
    }
    return false;
}

static void reset(sdm_fw_update_repr_t *repr) {
    repr->user_handlers->reset_handler(repr->user_ptr);
    repr->state = SDM_FW_UPDATE_STATE_IDLE;

#    if defined(ANJ_FOTA_PUSH_METHOD_SUPPORTED)
    repr->write_start_called = false;
    repr->package_bytes_written = 0;
#    endif // !defined (ANJ_FOTA_PUSH_METHOD_SUPPORTED)

#    if defined(ANJ_FOTA_PULL_METHOD_SUPPORTED)
    repr->uri[0] = '\0';
    repr->uri_bytes_written = 0;
#    endif // !defined (ANJ_FOTA_PULL_METHOD_SUPPORTED)
}

static int res_write(sdm_obj_t *obj,
                     sdm_obj_inst_t *obj_inst,
                     sdm_res_t *res,
                     sdm_res_inst_t *res_inst,
                     const fluf_res_value_t *value) {
    (void) obj;
    (void) obj_inst;
    (void) res_inst;

    sdm_fw_update_entity_ctx_t *entity =
            AVS_CONTAINER_OF(obj, sdm_fw_update_entity_ctx_t, obj);

    fluf_rid_t rid = res->res_spec->rid;

    switch (rid) {
    case SDM_FW_UPDATE_RID_PACKAGE: {
#    if !defined(ANJ_FOTA_PUSH_METHOD_SUPPORTED)
        return SDM_ERR_BAD_REQUEST;
#    else  // !defined (ANJ_FOTA_PUSH_METHOD_SUPPORTED)
           // any write in updating state is illegal
        if (entity->repr.state == SDM_FW_UPDATE_STATE_UPDATING) {
            return SDM_ERR_METHOD_NOT_ALLOWED;
        }

        // handle state machine reset with an empty write
        if (is_reset_request_package(value)) {
            reset(&entity->repr);
            return 0;
        }

        // non-empty writes can be performed only in IDLE state since for the
        // time of writing FW package in chunks the state does not change to
        // DOWNLOADING and goes directly from IDLE to DOWNLOADED on last chunk
        if (entity->repr.state != SDM_FW_UPDATE_STATE_IDLE) {
            return SDM_ERR_METHOD_NOT_ALLOWED;
        }

        sdm_fw_update_result_t result;

        // handle first chunk if needed
        if (!entity->repr.write_start_called) {
            result = entity->repr.user_handlers->package_write_start_handler(
                    entity->repr.user_ptr);
            if (result != SDM_FW_UPDATE_RESULT_SUCCESS) {
                entity->repr.result = result;
                return SDM_ERR_INTERNAL;
            }
            entity->repr.write_start_called = true;
        }

        // ensure it's a consecutive write
        if (entity->repr.package_bytes_written
                != value->bytes_or_string.offset) {
            return SDM_ERR_BAD_REQUEST;
            reset(&entity->repr);
        }

        // write actual data
        result = entity->repr.user_handlers->package_write_handler(
                entity->repr.user_ptr,
                value->bytes_or_string.data,
                value->bytes_or_string.chunk_length);
        if (result != SDM_FW_UPDATE_RESULT_SUCCESS) {
            entity->repr.result = result;
            reset(&entity->repr);
            return SDM_ERR_INTERNAL;
        }

        entity->repr.package_bytes_written +=
                value->bytes_or_string.chunk_length;

        // check if that's the last chunk (block)
        if (writing_last_data_chunk(value)) {
            result = entity->repr.user_handlers->package_write_finish_handler(
                    entity->repr.user_ptr);
            if (result != SDM_FW_UPDATE_RESULT_SUCCESS) {
                entity->repr.result = result;
                reset(&entity->repr);
                return SDM_ERR_INTERNAL;
            } else {
                entity->repr.state = SDM_FW_UPDATE_STATE_DOWNLOADED;
            }
        }

        return 0;
#    endif // !defined (ANJ_FOTA_PUSH_METHOD_SUPPORTED)
    }
    case SDM_FW_UPDATE_RID_PACKAGE_URI: {
#    if !defined(ANJ_FOTA_PULL_METHOD_SUPPORTED)
        return SDM_ERR_BAD_REQUEST;
#    else  // !defined (ANJ_FOTA_PULL_METHOD_SUPPORTED)
        if (entity->repr.state == SDM_FW_UPDATE_STATE_UPDATING) {
            return SDM_ERR_METHOD_NOT_ALLOWED;
        }

        // handle state machine reset with an empty write
        if (is_reset_request_uri(value)) {
            reset(&entity->repr);
            return 0;
        }

        // non-empty write can be handled only in IDLE state
        if (entity->repr.state != SDM_FW_UPDATE_STATE_IDLE) {
            return SDM_ERR_METHOD_NOT_ALLOWED;
        }

        // check if the write can fit
        if (value->bytes_or_string.offset + value->bytes_or_string.chunk_length
                        > SDM_FW_UPDATE_URI_MAX_LEN
                || value->bytes_or_string.full_length_hint
                               > SDM_FW_UPDATE_URI_MAX_LEN) {
            return SDM_ERR_BAD_REQUEST;
        }

        // ensure it's a consecutive write
        if (entity->repr.uri_bytes_written != value->bytes_or_string.offset) {
            return SDM_ERR_BAD_REQUEST;
            reset(&entity->repr);
        }

        // copy the chunk to server URI
        memcpy(&entity->repr.uri[value->bytes_or_string.offset],
               value->bytes_or_string.data,
               value->bytes_or_string.chunk_length);

        entity->repr.uri_bytes_written += value->bytes_or_string.chunk_length;

        if (!writing_last_data_chunk(value)) {
            return 0;
        }

        // terminate the string with NULL after last chunk write
        entity->repr.uri[value->bytes_or_string.full_length_hint] = '\0';

        sdm_fw_update_result_t result =
                entity->repr.user_handlers->uri_write_handler(
                        entity->repr.user_ptr, entity->repr.uri);
        if (result != SDM_FW_UPDATE_RESULT_SUCCESS) {
            entity->repr.result = result;
            return SDM_ERR_BAD_REQUEST;
        }

        entity->repr.state = SDM_FW_UPDATE_STATE_DOWNLOADING;
        return 0;
#    endif // !defined (ANJ_FOTA_PULL_METHOD_SUPPORTED)
    }
    default:
        return SDM_ERR_METHOD_NOT_ALLOWED;
    }
}

static int res_read(sdm_obj_t *obj,
                    sdm_obj_inst_t *obj_inst,
                    sdm_res_t *res,
                    sdm_res_inst_t *res_inst,
                    fluf_res_value_t *out_value) {
    (void) obj;
    (void) obj_inst;
    (void) res_inst;
    (void) out_value;

    fluf_rid_t rid = res->res_spec->rid;
    sdm_fw_update_entity_ctx_t *entity =
            AVS_CONTAINER_OF(obj, sdm_fw_update_entity_ctx_t, obj);

    switch (rid) {
    case SDM_FW_UPDATE_RID_UPDATE_DELIVERY_METHOD:
        out_value->int_value = (int64_t) METHODS_SUPPORTED;
        return 0;
    case SDM_FW_UPDATE_RID_STATE:
        out_value->int_value = (int64_t) entity->repr.state;
        return 0;
    case SDM_FW_UPDATE_RID_UPDATE_RESULT:
        out_value->int_value = (int64_t) entity->repr.result;
        return 0;
    case SDM_FW_UPDATE_RID_PACKAGE_URI: {
        out_value->bytes_or_string.data = entity->repr.uri;
        return 0;
    }
    case SDM_FW_UPDATE_RID_PKG_NAME: {
        if (!entity->repr.user_handlers->get_name) {
            out_value->bytes_or_string.data = "";
            out_value->bytes_or_string.chunk_length = 0;
            return 0;
        }
        out_value->bytes_or_string.data =
                (char *) (intptr_t) entity->repr.user_handlers->get_name(
                        entity->repr.user_ptr);
        out_value->bytes_or_string.chunk_length =
                strlen((char *) out_value->bytes_or_string.data);
        return 0;
    }
    case SDM_FW_UPDATE_RID_PKG_VERSION: {
        if (!entity->repr.user_handlers->get_version) {
            out_value->bytes_or_string.data = "";
            out_value->bytes_or_string.chunk_length = 0;
            return 0;
        }
        out_value->bytes_or_string.data =
                (char *) (intptr_t) entity->repr.user_handlers->get_version(
                        entity->repr.user_ptr);
        out_value->bytes_or_string.chunk_length =
                strlen((char *) out_value->bytes_or_string.data);
        return 0;
    }
    default:
        return SDM_ERR_NOT_FOUND;
    }
}

static int res_execute(sdm_obj_t *obj,
                       sdm_obj_inst_t *obj_inst,
                       sdm_res_t *res,
                       const char *execute_arg,
                       size_t execute_arg_len) {
    (void) obj;
    (void) obj_inst;
    (void) execute_arg;
    (void) execute_arg_len;

    fluf_rid_t rid = res->res_spec->rid;
    sdm_fw_update_entity_ctx_t *entity =
            AVS_CONTAINER_OF(obj, sdm_fw_update_entity_ctx_t, obj);

    switch (rid) {
    case SDM_FW_UPDATE_RID_UPDATE:
        if (entity->repr.state != SDM_FW_UPDATE_STATE_DOWNLOADED) {
            return SDM_ERR_METHOD_NOT_ALLOWED;
        }
        int result = entity->repr.user_handlers->update_start_handler(
                entity->repr.user_ptr);
        if (result) {
            entity->repr.result = SDM_FW_UPDATE_RESULT_FAILED;
            entity->repr.state = SDM_FW_UPDATE_STATE_IDLE;
            return SDM_ERR_INTERNAL;
        }
        entity->repr.state = SDM_FW_UPDATE_STATE_UPDATING;
        return 0;
    default:
        return SDM_ERR_METHOD_NOT_ALLOWED;
    }
}

static const sdm_res_handlers_t res_handlers = {
    .res_read = res_read,
    .res_write = res_write,
    .res_execute = res_execute
};

static const sdm_res_spec_t package_spec = {
    .rid = SDM_FW_UPDATE_RID_PACKAGE,
    .type = FLUF_DATA_TYPE_BYTES,
    .operation = SDM_RES_W
};

static const sdm_res_spec_t package_uri_spec = {
    .rid = SDM_FW_UPDATE_RID_PACKAGE_URI,
    .type = FLUF_DATA_TYPE_STRING,
    .operation = SDM_RES_RW
};

static const sdm_res_spec_t update_spec = {
    .rid = SDM_FW_UPDATE_RID_UPDATE,
    .operation = SDM_RES_E
};

static const sdm_res_spec_t state_spec = {
    .rid = SDM_FW_UPDATE_RID_STATE,
    .type = FLUF_DATA_TYPE_INT,
    .operation = SDM_RES_R
};

static const sdm_res_spec_t update_result_spec = {
    .rid = SDM_FW_UPDATE_RID_UPDATE_RESULT,
    .type = FLUF_DATA_TYPE_INT,
    .operation = SDM_RES_R
};

static const sdm_res_spec_t pkg_name_spec = {
    .rid = SDM_FW_UPDATE_RID_PKG_NAME,
    .type = FLUF_DATA_TYPE_STRING,
    .operation = SDM_RES_R
};

static const sdm_res_spec_t pkg_ver_spec = {
    .rid = SDM_FW_UPDATE_RID_PKG_VERSION,
    .type = FLUF_DATA_TYPE_STRING,
    .operation = SDM_RES_R
};

static const sdm_res_spec_t protocol_support_spec = {
    .rid = SDM_FW_UPDATE_RID_UPDATE_PROTOCOL_SUPPORT,
    .type = FLUF_DATA_TYPE_INT,
    .operation = SDM_RES_RM
};

static const sdm_res_spec_t delivery_method_spec = {
    .rid = SDM_FW_UPDATE_RID_UPDATE_DELIVERY_METHOD,
    .type = FLUF_DATA_TYPE_INT,
    .operation = SDM_RES_R
};

static sdm_res_inst_t *supported_protocols_insts[] = {
#    ifdef ANJ_FOTA_PROTOCOL_COAP_SUPPORTED
    &SDM_MAKE_RES_INST(
            SDM_FW_UPDATE_PROTOCOL_COAP,
            &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(
                    0, SDM_INIT_RES_VAL_I64(SDM_FW_UPDATE_PROTOCOL_COAP))),
#    endif // ANJ_FOTA_PROTOCOL_COAP_SUPPORTED
#    ifdef ANJ_FOTA_PROTOCOL_COAPS_SUPPORTED
    &SDM_MAKE_RES_INST(
            SDM_FW_UPDATE_PROTOCOL_COAPS,
            &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(
                    0, SDM_INIT_RES_VAL_I64(SDM_FW_UPDATE_PROTOCOL_COAPS))),
#    endif // ANJ_FOTA_PROTOCOL_COAPS_SUPPORTED
#    ifdef ANJ_FOTA_PROTOCOL_HTTP_SUPPORTED
    &SDM_MAKE_RES_INST(
            SDM_FW_UPDATE_PROTOCOL_HTTP,
            &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(
                    0, SDM_INIT_RES_VAL_I64(SDM_FW_UPDATE_PROTOCOL_HTTP))),
#    endif // ANJ_FOTA_PROTOCOL_HTTP_SUPPORTED
#    ifdef ANJ_FOTA_PROTOCOL_HTTPS_SUPPORTED
    &SDM_MAKE_RES_INST(
            SDM_FW_UPDATE_PROTOCOL_HTTPS,
            &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(
                    0, SDM_INIT_RES_VAL_I64(SDM_FW_UPDATE_PROTOCOL_HTTPS))),
#    endif // ANJ_FOTA_PROTOCOL_HTTPS_SUPPORTED
#    ifdef ANJ_FOTA_PROTOCOL_COAP_TCP_SUPPORTED
    &SDM_MAKE_RES_INST(
            SDM_FW_UPDATE_PROTOCOL_COAP_TCP,
            &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(
                    0, SDM_INIT_RES_VAL_I64(SDM_FW_UPDATE_PROTOCOL_COAP_TCP))),
#    endif // ANJ_FOTA_PROTOCOL_COAP_TCP_SUPPORTED
#    ifdef ANJ_FOTA_PROTOCOL_COAP_TLS_SUPPORTED
    &SDM_MAKE_RES_INST(
            SDM_FW_UPDATE_PROTOCOL_COAP_TLS,
            &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(
                    0, SDM_INIT_RES_VAL_I64(SDM_FW_UPDATE_PROTOCOL_COAP_TLS))),
#    endif // ANJ_FOTA_PROTOCOL_COAP_TLS_SUPPORTED
};

int sdm_fw_update_object_install(sdm_data_model_t *dm,
                                 sdm_fw_update_entity_ctx_t *entity_ctx,
                                 sdm_fw_update_handlers_t *handlers,
                                 void *user_ptr) {
    memset(entity_ctx, 0, sizeof(*entity_ctx));
    sdm_fw_update_repr_t *repr = &entity_ctx->repr;

    if (!dm || !handlers || !handlers->update_start_handler
            || !handlers->reset_handler) {
        return -1;
    }

#    ifdef ANJ_FOTA_PUSH_METHOD_SUPPORTED
    if (!handlers->package_write_start_handler
            || !handlers->package_write_handler
            || !handlers->package_write_finish_handler) {
        return -1;
    }
    repr->write_start_called = false;
    repr->package_bytes_written = 0;
#    endif // ANJ_FOTA_PUSH_METHOD_SUPPORTED

#    ifdef ANJ_FOTA_PULL_METHOD_SUPPORTED
    if (!handlers->uri_write_handler) {
        return -1;
    }
    repr->uri_bytes_written = 0;
#    endif // ANJ_FOTA_PULL_METHOD_SUPPORTED

    repr->user_ptr = user_ptr;
    repr->user_handlers = handlers;

    entity_ctx->obj.oid = SDM_FW_UPDATE_OID;
    entity_ctx->obj.version = "1.0";
    entity_ctx->obj.inst_count = 1;
    entity_ctx->obj.max_inst_count = 1;
    entity_ctx->obj.insts = &entity_ctx->inst_ptr;
    *entity_ctx->obj.insts = &entity_ctx->inst;

    entity_ctx->inst.iid = 0;
    entity_ctx->inst.resources = entity_ctx->res;
    entity_ctx->inst.res_count = SDM_FW_UPDATE_RESOURCES_COUNT;

    entity_ctx->inst.resources[PACKAGE_RES_IDX] =
            SDM_MAKE_RES(&package_spec, &res_handlers, NULL);
    entity_ctx->inst.resources[PACKAGE_URI_RES_IDX] =
            SDM_MAKE_RES(&package_uri_spec, &res_handlers, NULL);
    repr->uri[0] = '\0';
    entity_ctx->inst.resources[UPDATE_RES_IDX] =
            SDM_MAKE_RES(&update_spec, &res_handlers, NULL);
    entity_ctx->inst.resources[STATE_RES_IDX] =
            SDM_MAKE_RES(&state_spec, &res_handlers, NULL);
    repr->state = SDM_FW_UPDATE_STATE_IDLE;
    entity_ctx->inst.resources[UPDATE_RESULT_RES_IDX] =
            SDM_MAKE_RES(&update_result_spec, &res_handlers, NULL);
    repr->result = SDM_FW_UPDATE_RESULT_INITIAL;
    entity_ctx->inst.resources[PKG_NAME_RES_IDX] =
            SDM_MAKE_RES(&pkg_name_spec, &res_handlers, NULL);
    entity_ctx->inst.resources[PKG_VER_RES_IDX] =
            SDM_MAKE_RES(&pkg_ver_spec, &res_handlers, NULL);
    entity_ctx->inst.resources[SUPORTED_PROTOCOLS_RES_IDX] =
            SDM_MAKE_MULTI_RES(&protocol_support_spec,
                               NULL,
                               supported_protocols_insts,
                               AVS_ARRAY_SIZE(supported_protocols_insts),
                               AVS_ARRAY_SIZE(supported_protocols_insts));
    entity_ctx->inst.resources[DELIVERY_METHOD_RES_IDX] =
            SDM_MAKE_RES(&delivery_method_spec, &res_handlers, NULL);

    return sdm_add_obj(dm, &entity_ctx->obj);
}

void sdm_fw_update_object_set_update_result(
        sdm_fw_update_entity_ctx_t *entity_ctx, sdm_fw_update_result_t result) {
    entity_ctx->repr.result = result;
    entity_ctx->repr.state = SDM_FW_UPDATE_STATE_IDLE;
    entity_ctx->repr.write_start_called = false;
}

int sdm_fw_update_object_set_download_result(
        sdm_fw_update_entity_ctx_t *entity_ctx, sdm_fw_update_result_t result) {
    if (entity_ctx->repr.state != SDM_FW_UPDATE_STATE_DOWNLOADING) {
        return -1;
    }
    if (result != SDM_FW_UPDATE_RESULT_SUCCESS) {
        entity_ctx->repr.result = result;
        reset(&entity_ctx->repr);
        return 0;
    }
    entity_ctx->repr.state = SDM_FW_UPDATE_STATE_DOWNLOADED;

    return 0;
}

#endif // ANJ_WITH_FOTA_OBJECT
