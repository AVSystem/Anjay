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

#include <anj/sdm.h>
#include <anj/sdm_fw_update.h>
#include <anj/sdm_io.h>

#include <anj/anj_config.h>

#include <fluf/fluf_defs.h>

#define FW_UPDATE_OID 5

/* FOTA method support */
#if !ANJ_FOTA_PUSH_METHOD_SUPPORTED
// if push method if not supported, it's safe to assume that pull is -
// guaranteed by a condition check in fluf_config.h 0 -> pull only
#    define METHODS_SUPPORTED 0
#else // !ANJ_FOTA_PUSH_METHOD_SUPPORTED
#    if ANJ_FOTA_PULL_METHOD_SUPPORTED
// this means push is supported as well
// 2 -> pull & push
#        define METHODS_SUPPORTED 2
#    else // ANJ_FOTA_PULL_METHOD_SUPPORTED
// only push enabled
// 1 -> push
#        define METHODS_SUPPORTED 1
#    endif // ANJ_FOTA_PULL_METHOD_SUPPORTED
#endif     // !ANJ_FOTA_PUSH_METHOD_SUPPORTED

enum {
    RID_PACKAGE = 0,
    RID_PACKAGE_URI = 1,
    RID_UPDATE = 2,
    RID_STATE = 3,
    RID_UPDATE_RESULT = 5,
    RID_PKG_NAME = 6,
    RID_PKG_VERSION = 7,
    RID_UPDATE_PROTOCOL_SUPPORT = 8,
    RID_UPDATE_DELIVERY_METHOD = 9
};

typedef enum {
    UPDATE_STATE_IDLE = 0,
    UPDATE_STATE_DOWNLOADING,
    UPDATE_STATE_DOWNLOADED,
    UPDATE_STATE_UPDATING
} fw_update_state_t;

#define PACKAGE_RES_IDX 0
#define PACKAGE_URI_RES_IDX 1
#define UPDATE_RES_IDX 2
#define STATE_RES_IDX 3
#define UPDATE_RESULT_RES_IDX 4
#define PKG_NAME_RES_IDX 5
#define PKG_VER_RES_IDX 6
#define SUPORTED_PROTOCOLS_RES_IDX 7
#define DELIVERY_METHOD_RES_IDX 8

static struct {
    fw_update_state_t state;
    sdm_fw_update_result_t result;
    char uri[256];
    sdm_fw_update_handlers_t *user_handlers;
    uint8_t supported_protocols;
    void *user_ptr;
    bool write_start_called;
} fw_update_repr;

static const sdm_res_spec_t package_spec = {
    .rid = RID_PACKAGE,
    .type = FLUF_DATA_TYPE_BYTES,
    .operation = SDM_RES_W
};

static const sdm_res_spec_t package_uri_spec = {
    .rid = RID_PACKAGE_URI,
    .type = FLUF_DATA_TYPE_STRING,
    .operation = SDM_RES_RW
};

static const sdm_res_spec_t update_spec = {
    .rid = RID_UPDATE,
    .operation = SDM_RES_E
};

static const sdm_res_spec_t state_spec = {
    .rid = RID_STATE,
    .type = FLUF_DATA_TYPE_INT,
    .operation = SDM_RES_R
};

static const sdm_res_spec_t update_result_spec = {
    .rid = RID_UPDATE_RESULT,
    .type = FLUF_DATA_TYPE_INT,
    .operation = SDM_RES_R
};

static const sdm_res_spec_t pkg_name_spec = {
    .rid = RID_PKG_NAME,
    .type = FLUF_DATA_TYPE_STRING,
    .operation = SDM_RES_R
};

static const sdm_res_spec_t pkg_ver_spec = {
    .rid = RID_PKG_VERSION,
    .type = FLUF_DATA_TYPE_STRING,
    .operation = SDM_RES_R
};

static const sdm_res_spec_t protocol_support_spec = {
    .rid = RID_UPDATE_PROTOCOL_SUPPORT,
    .type = FLUF_DATA_TYPE_INT,
    .operation = SDM_RES_RM
};

static const sdm_res_spec_t delivery_method_spec = {
    .rid = RID_UPDATE_DELIVERY_METHOD,
    .type = FLUF_DATA_TYPE_INT,
    .operation = SDM_RES_R
};

static bool is_reset_request(const fluf_res_value_t *value) {
    if (((char *) value->bytes_or_string.data)[0] == '\0'
            && value->bytes_or_string.full_length_hint == 1) {
        return true;
    }
    return false;
}

static void reset(void) {
    fw_update_repr.user_handlers->reset_handler(fw_update_repr.user_ptr);
    fw_update_repr.state = UPDATE_STATE_IDLE;
    fw_update_repr.write_start_called = false;
}

static int res_write(sdm_obj_t *obj,
                     sdm_obj_inst_t *obj_inst,
                     sdm_res_t *res,
                     sdm_res_inst_t *res_inst,
                     const fluf_res_value_t *value) {
    (void) obj;
    (void) obj_inst;
    (void) res_inst;

    fluf_rid_t rid = res->res_spec->rid;

    switch (rid) {
    case RID_PACKAGE: {
        if (fw_update_repr.state == UPDATE_STATE_UPDATING) {
            return SDM_ERR_METHOD_NOT_ALLOWED;
        }

        int ret = 0;

        // handle state machine reset with an empty write
        if (is_reset_request(value)) {
            if (fw_update_repr.state == UPDATE_STATE_DOWNLOADING) {
                fw_update_repr.user_handlers->cancel_download_handler(
                        fw_update_repr.user_ptr, fw_update_repr.uri);
            }
            reset();
            return 0;
        }

        if (fw_update_repr.state == UPDATE_STATE_IDLE) {
            sdm_fw_update_result_t result;

            // handle first chunk if needed
            if (!fw_update_repr.write_start_called) {
                result = fw_update_repr.user_handlers
                                 ->package_write_start_handler(
                                         fw_update_repr.user_ptr);
                if (result != SDM_FW_UPDATE_RESULT_SUCCESS
                        && result != SDM_FW_UPDATE_RESULT_INITIAL) {
                    fw_update_repr.result = result;
                    return SDM_ERR_INTERNAL;
                }
                fw_update_repr.write_start_called = true;
            }

            // write actual data
            result = fw_update_repr.user_handlers->package_write_handler(
                    fw_update_repr.user_ptr,
                    value->bytes_or_string.data,
                    value->bytes_or_string.chunk_length);
            if (result != SDM_FW_UPDATE_RESULT_SUCCESS
                    && result != SDM_FW_UPDATE_RESULT_INITIAL) {
                fw_update_repr.result = result;
                reset();
                return SDM_ERR_INTERNAL;
            }

            // check if that's the last chunk (block)
            if (value->bytes_or_string.chunk_length
                            + value->bytes_or_string.offset
                    == value->bytes_or_string.full_length_hint) {
                result = fw_update_repr.user_handlers
                                 ->package_write_finish_handler(
                                         fw_update_repr.user_ptr);
                if (result != SDM_FW_UPDATE_RESULT_SUCCESS
                        && result != SDM_FW_UPDATE_RESULT_INITIAL) {
                    reset();
                    ret = SDM_ERR_INTERNAL;
                } else {
                    fw_update_repr.state = UPDATE_STATE_DOWNLOADED;
                }
            }

            return ret;
        }

        // unhandled cases are Write calls in a wrong state
        return SDM_ERR_BAD_REQUEST;
    }
    case RID_PACKAGE_URI: {
        // handle state machine reset with an empty write
        if (is_reset_request(value)) {
            if (fw_update_repr.state == UPDATE_STATE_UPDATING) {
                return SDM_ERR_METHOD_NOT_ALLOWED;
            }
            fw_update_repr.uri[0] = '\0';

            fw_update_repr.user_handlers->cancel_download_handler(
                    fw_update_repr.user_ptr, fw_update_repr.uri);
            reset();
            return 0;
        }

        // non-empty write can be handled only in IDLE state
        if (fw_update_repr.state != UPDATE_STATE_IDLE) {
            return SDM_ERR_BAD_REQUEST;
        }

        // apply and handle URI write
        strcpy(fw_update_repr.uri, (char *) value->bytes_or_string.data);
        sdm_fw_update_result_t result =
                fw_update_repr.user_handlers->uri_write_handler(
                        fw_update_repr.user_ptr, fw_update_repr.uri);
        if (result == SDM_FW_UPDATE_RESULT_SUCCESS
                || result == SDM_FW_UPDATE_RESULT_INITIAL) {
            fw_update_repr.state = UPDATE_STATE_DOWNLOADING;
            return 0;
        }
        fw_update_repr.result = result;
        return SDM_ERR_BAD_REQUEST;
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

    switch (rid) {
    case RID_PACKAGE_URI: {
        out_value->bytes_or_string.data = fw_update_repr.uri;
        return 0;
    }
    case RID_STATE: {
        out_value->int_value = fw_update_repr.state;
        return 0;
    }
    case RID_UPDATE_RESULT: {
        out_value->int_value = fw_update_repr.result;
        return 0;
    }
    case RID_PKG_NAME: {
        if (!fw_update_repr.user_handlers->get_name) {
            out_value->bytes_or_string.data = "";
            out_value->bytes_or_string.chunk_length = 0;
            return 0;
        }
        out_value->bytes_or_string.data =
                (char *) (intptr_t) fw_update_repr.user_handlers->get_name(
                        fw_update_repr.user_ptr);
        out_value->bytes_or_string.chunk_length =
                strlen((char *) out_value->bytes_or_string.data);
        return 0;
    }
    case RID_PKG_VERSION: {
        if (!fw_update_repr.user_handlers->get_version) {
            out_value->bytes_or_string.data = "";
            out_value->bytes_or_string.chunk_length = 0;
            return 0;
        }
        out_value->bytes_or_string.data =
                (char *) (intptr_t) fw_update_repr.user_handlers->get_version(
                        fw_update_repr.user_ptr);
        out_value->bytes_or_string.chunk_length =
                strlen((char *) out_value->bytes_or_string.data);
        return 0;
    }
    case RID_UPDATE_DELIVERY_METHOD: {
        out_value->int_value = METHODS_SUPPORTED;
        return 0;
    }
    default:
        return SDM_ERR_METHOD_NOT_ALLOWED;
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

    switch (rid) {
    case RID_UPDATE:
        if (fw_update_repr.state != UPDATE_STATE_DOWNLOADED) {
            return SDM_ERR_METHOD_NOT_ALLOWED;
        }
        sdm_fw_update_result_t result =
                fw_update_repr.user_handlers->update_start_handler(
                        fw_update_repr.user_ptr);
        if (result == SDM_FW_UPDATE_RESULT_SUCCESS
                || result == SDM_FW_UPDATE_RESULT_INITIAL) {
            fw_update_repr.state = UPDATE_STATE_UPDATING;
            return 0;
        }
        fw_update_repr.state = UPDATE_STATE_IDLE;
        return SDM_ERR_INTERNAL;
    default:
        return SDM_ERR_METHOD_NOT_ALLOWED;
    }
}

static const sdm_res_handlers_t update_handler = {
    .res_execute = res_execute
};

static const sdm_res_handlers_t res_handlers = {
    .res_read = res_read,
    .res_write = res_write,
};

#define PROCOTOLS_COUNT 6
sdm_res_inst_t protocol_support_instances_1[PROCOTOLS_COUNT];
sdm_res_inst_t *protocol_support_instances[PROCOTOLS_COUNT];

static sdm_res_t fw_update_res[] = {
    [PACKAGE_RES_IDX] = {
        .res_spec = &package_spec,
        .res_handlers = &res_handlers
    },
    [PACKAGE_URI_RES_IDX] = {
        .res_spec = &package_uri_spec,
        .res_handlers = &res_handlers
    },
    [UPDATE_RES_IDX] = {
        .res_spec = &update_spec,
        .res_handlers = &update_handler
    },
    [STATE_RES_IDX] = {
        .res_spec = &state_spec,
        .res_handlers = &res_handlers
    },
    [UPDATE_RESULT_RES_IDX] = {
        .res_spec = &update_result_spec,
        .res_handlers = &res_handlers
    },
    [PKG_NAME_RES_IDX] = {
        .res_spec = &pkg_name_spec,
        .res_handlers = &res_handlers
    },
    [PKG_VER_RES_IDX] = {
        .res_spec = &pkg_ver_spec,
        .res_handlers = &res_handlers
    },
    [SUPORTED_PROTOCOLS_RES_IDX] = {
        .res_spec = &protocol_support_spec,
        .value.res_inst.insts = protocol_support_instances,
        .value.res_inst.max_inst_count = PROCOTOLS_COUNT
    },
    [DELIVERY_METHOD_RES_IDX] = {
        .res_spec = &delivery_method_spec,
        .res_handlers = &res_handlers
    }
};

static sdm_obj_inst_t obj_inst = {
    .iid = 0,
    .resources = fw_update_res,
    .res_count = AVS_ARRAY_SIZE(fw_update_res)
};

static sdm_obj_inst_t *fw_update_obj_inst[1] = { &obj_inst };

static sdm_obj_t fw_update_obj = {
    .oid = FW_UPDATE_OID,
    .version = "1.0",
    .inst_count = 1,
    .max_inst_count = 1,
    .insts = fw_update_obj_inst
};

static inline bool fw_update_object_exists(sdm_data_model_t *dm) {
    for (size_t i = 0; i < dm->objs_count; i++) {
        if (dm->objs[i]->oid == FW_UPDATE_OID) {
            return true;
        }
    }

    return false;
}

int sdm_fw_update_object_install(sdm_data_model_t *dm,
                                 sdm_fw_update_handlers_t *handlers,
                                 void *user_ptr,
                                 uint8_t supported_protocols) {
    if (!dm || !handlers || !handlers->update_start_handler
            || !supported_protocols) {
        return -1;
    }

    if (fw_update_object_exists(dm)) {
        return -1;
    }
#ifdef FLUL_FOTA_PUSH_METHOD_SUPPORTED
    if (!handlers->package_write_start_handler
            || !handlers->package_write_handler
            || !handlers->package_write_finish_handler) {
        return -1;
    }
#endif // FLUL_FOTA_PUSH_METHOD_SUPPORTED
#ifdef FLUL_FOTA_PULL_METHOD_SUPPORTED
    if (!handlers->uri_write_handler || !handlers->cancel_download_handler) {
        return -1;
    }
#endif // FLUL_FOTA_PULL_METHOD_SUPPORTED

    fw_update_repr.user_ptr = user_ptr;
    fw_update_repr.user_handlers = handlers;
    fw_update_repr.supported_protocols = supported_protocols;
    fw_update_repr.state = UPDATE_STATE_IDLE;
    fw_update_repr.write_start_called = false;
    fw_update_repr.result = SDM_FW_UPDATE_RESULT_INITIAL;

    /**
     * create 5/0/8 Multi-Instance Resource instances "dynamically" - the
     * maximum size array of instances is pre-alocated and they are filled
     * with proper IID and protocol-coding constant integers.
     */
    fluf_riid_t iid = 0;
    for (size_t i = 0; i < PROCOTOLS_COUNT; i++) {
        if (supported_protocols & (1 << i)) {
            protocol_support_instances[iid] =
                    &protocol_support_instances_1[iid];
            protocol_support_instances_1[iid].riid = iid;
            protocol_support_instances_1[iid].res_value.value.int_value =
                    (int64_t) i;
            iid++;
        }
    }
    fw_update_res[SUPORTED_PROTOCOLS_RES_IDX].value.res_inst.inst_count = iid;

    return sdm_add_obj(dm, &fw_update_obj);
}

int sdm_fw_update_object_set_update_result(sdm_fw_update_result_t result) {
    switch (result) {
    case SDM_FW_UPDATE_RESULT_SUCCESS:
    case SDM_FW_UPDATE_RESULT_INTEGRITY_FAILURE:
    case SDM_FW_UPDATE_RESULT_FAILED:
        fw_update_repr.result = result;
        fw_update_repr.state = UPDATE_STATE_IDLE;
        fw_update_repr.write_start_called = false;
        return 0;
    default:
        return -1;
    }
}

int sdm_fw_update_object_set_download_result(sdm_fw_update_result_t result) {
    if (fw_update_repr.state != UPDATE_STATE_DOWNLOADING) {
        return -1;
    }
    fw_update_repr.result = result;
    if (result != SDM_FW_UPDATE_RESULT_SUCCESS) {
        reset();
        fw_update_repr.state = UPDATE_STATE_IDLE;
        return 0;
    }
    fw_update_repr.state = UPDATE_STATE_DOWNLOADED;

    return 0;
}
