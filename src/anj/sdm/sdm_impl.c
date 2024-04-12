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
#include <anj/sdm_impl.h>
#include <anj/sdm_io.h>

#include "sdm_core.h"

// fluf_io_out_ctx doesn't support READ operation with no readable resources, so
// we need to handle it separately
#define _SDM_EMPTY_READ 117
#define _SDM_CONTINUE 118

static uint8_t map_sdm_err_to_coap_code(int error_code) {
    switch (error_code) {
    case SDM_ERR_INPUT_ARG:
    case SDM_ERR_MEMORY:
    case SDM_ERR_LOGIC:
        return FLUF_COAP_CODE_BAD_REQUEST;
    case SDM_ERR_BAD_REQUEST:
        return FLUF_COAP_CODE_BAD_REQUEST;
    case SDM_ERR_UNAUTHORIZED:
        return FLUF_COAP_CODE_UNAUTHORIZED;
    case SDM_ERR_NOT_FOUND:
        return FLUF_COAP_CODE_NOT_FOUND;
    case SDM_ERR_METHOD_NOT_ALLOWED:
        return FLUF_COAP_CODE_METHOD_NOT_ALLOWED;
    case SDM_ERR_INTERNAL:
        return FLUF_COAP_CODE_BAD_REQUEST;
    case SDM_ERR_NOT_IMPLEMENTED:
        return FLUF_COAP_CODE_NOT_IMPLEMENTED;
    case SDM_ERR_SERVICE_UNAVAILABLE:
        return FLUF_COAP_CODE_SERVICE_UNAVAILABLE;
    default:
        return FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
    }
}

static void resource_uri_trace_log(const fluf_uri_path_t *path) {
    if (fluf_uri_path_is(path, FLUF_ID_RID)) {
        sdm_log(TRACE, "/%" PRIu16 "/%" PRIu16 "/%" PRIu16, path->ids[0],
                path->ids[1], path->ids[2]);
    } else if (fluf_uri_path_is(path, FLUF_ID_RIID)) {
        sdm_log(TRACE, "/%" PRIu16 "/%" PRIu16 "/%" PRIu16 "/%" PRIu16,
                path->ids[0], path->ids[1], path->ids[2], path->ids[3]);
    }
}

static void uri_log(const fluf_uri_path_t *path) {
    if (fluf_uri_path_is(path, FLUF_ID_OID)) {
        sdm_log(DEBUG, "/%" PRIu16, path->ids[0]);
    } else if (fluf_uri_path_is(path, FLUF_ID_IID)) {
        sdm_log(DEBUG, "/%" PRIu16 "/%" PRIu16, path->ids[0], path->ids[1]);
    } else if (fluf_uri_path_is(path, FLUF_ID_RID)) {
        sdm_log(DEBUG, "/%" PRIu16 "/%" PRIu16 "/%" PRIu16, path->ids[0],
                path->ids[1], path->ids[2]);
    } else if (fluf_uri_path_is(path, FLUF_ID_RIID)) {
        sdm_log(DEBUG, "/%" PRIu16 "/%" PRIu16 "/%" PRIu16 "/%" PRIu16,
                path->ids[0], path->ids[1], path->ids[2], path->ids[3]);
    }
}

static int initialize_fluf_io_ctx(sdm_process_ctx_t *ctx,
                                  sdm_data_model_t *dm,
                                  fluf_data_t *msg,
                                  bool is_bootstrap_server_call) {
    size_t res_count = 0;
    switch (msg->operation) {
    case FLUF_OP_REGISTER:
    case FLUF_OP_UPDATE:
        sdm_log(DEBUG, "Register/update operation");
        fluf_io_register_ctx_init(&ctx->fluf_io.register_ctx);
        return 0;
    case FLUF_OP_DM_DISCOVER:
        sdm_log(DEBUG, "Discover operation");
        uri_log(&msg->uri);
        if (is_bootstrap_server_call) {
            return fluf_io_bootstrap_discover_ctx_init(
                    &ctx->fluf_io.bootstrap_discover_ctx, &msg->uri);
        }
        return fluf_io_discover_ctx_init(
                &ctx->fluf_io.discover_ctx, &msg->uri,
                msg->attr.discover_attr.has_depth
                        ? &msg->attr.discover_attr.depth
                        : NULL);
    case FLUF_OP_DM_WRITE_REPLACE:
    case FLUF_OP_DM_WRITE_PARTIAL_UPDATE:
    case FLUF_OP_DM_CREATE:
        sdm_log(DEBUG, "Write/create operation");
        uri_log(&msg->uri);
        return fluf_io_in_ctx_init(&ctx->fluf_io.in_ctx, msg->operation,
                                   &msg->uri, msg->content_format);
    case FLUF_OP_DM_READ:
        sdm_log(DEBUG, "Read operation");
        uri_log(&msg->uri);
        if (sdm_get_readable_res_count(dm, &res_count)) {
            return -1;
        }
        if (!res_count) {
            return _SDM_EMPTY_READ;
        }
        return fluf_io_out_ctx_init(&ctx->fluf_io.out_ctx, FLUF_OP_DM_READ,
                                    &msg->uri, res_count, msg->accept);
    case FLUF_OP_DM_EXECUTE:
        sdm_log(DEBUG, "Execute operation");
        uri_log(&msg->uri);
        return 0;
    case FLUF_OP_DM_DELETE:
        sdm_log(DEBUG, "Delete operation");
        uri_log(&msg->uri);
        return 0;
    default:
        break;
    }
    sdm_log(ERROR, "Operation not supported");
    return -1;
}

static void initialize_payload(fluf_data_t *msg, uint16_t format) {
    msg->payload = NULL;
    msg->payload_size = 0;
    msg->content_format = format;
}

static int handle_get_payload_result(sdm_process_ctx_t *ctx,
                                     fluf_data_t *msg,
                                     int fluf_return_code,
                                     int sdm_return_code,
                                     size_t offset,
                                     char *out_buff,
                                     size_t out_buff_len) {
    if (!fluf_return_code) {
        ctx->data_to_copy = false;
    } else if (fluf_return_code == FLUF_IO_NEED_NEXT_CALL) {
        ctx->data_to_copy = true;
        msg->payload = out_buff;
        msg->payload_size = offset;
        assert(offset == out_buff_len);
        return SDM_IMPL_BLOCK_TRANSFER_NEEDED;
    } else {
        sdm_log(ERROR, "fluf_io ctx error");
        msg->msg_code = FLUF_COAP_CODE_BAD_REQUEST;
        return fluf_return_code;
    }
    if (sdm_return_code == SDM_LAST_RECORD) {
        msg->payload = out_buff;
        msg->payload_size = offset;
        return 0;
    }
    return _SDM_CONTINUE;
}

static int process_register(sdm_process_ctx_t *ctx,
                            sdm_data_model_t *dm,
                            fluf_data_t *in_out_msg,
                            char *out_buff,
                            size_t out_buff_len) {
    int ret_sdm = 0;
    int ret_fluf = 0;
    fluf_uri_path_t path;
    const char *version;
    size_t offset = 0;
    size_t copied_bytes;

    initialize_payload(in_out_msg, FLUF_COAP_FORMAT_LINK_FORMAT);

    while (true) {
        if (!ctx->data_to_copy) {
            ret_sdm = sdm_get_register_record(dm, &path, &version);
            if (ret_sdm && ret_sdm != SDM_LAST_RECORD) {
                // no msg_code for register or update
                return ret_sdm;
            }
            ret_fluf =
                    fluf_io_register_ctx_new_entry(&ctx->fluf_io.register_ctx,
                                                   &path, version);
            if (ret_fluf) {
                sdm_log(ERROR, "fluf_io register ctx error");
                return ret_fluf;
            }
        }
        ret_fluf = fluf_io_register_ctx_get_payload(&ctx->fluf_io.register_ctx,
                                                    &out_buff[offset],
                                                    out_buff_len - offset,
                                                    &copied_bytes);
        offset += copied_bytes;
        int ret = handle_get_payload_result(ctx, in_out_msg, ret_fluf, ret_sdm,
                                            offset, out_buff, out_buff_len);
        if (ret != _SDM_CONTINUE) {
            return ret;
        }
    }
}

static int process_discover(sdm_process_ctx_t *ctx,
                            sdm_data_model_t *dm,
                            fluf_data_t *in_out_msg,
                            char *out_buff,
                            size_t out_buff_len) {
    int ret_sdm = 0;
    int ret_fluf = 0;
    fluf_uri_path_t path;
    const char *version;
    const uint16_t *dim;
    size_t offset = 0;
    size_t copied_bytes;

    initialize_payload(in_out_msg, FLUF_COAP_FORMAT_LINK_FORMAT);
    in_out_msg->msg_code = FLUF_COAP_CODE_CONTENT;

    while (true) {
        if (!ctx->data_to_copy) {
            ret_sdm = sdm_get_discover_record(dm, &path, &version, &dim);
            if (ret_sdm && ret_sdm != SDM_LAST_RECORD) {
                in_out_msg->msg_code = map_sdm_err_to_coap_code(ret_sdm);
                return ret_sdm;
            }
            // TODO: attr not supported yet
            ret_fluf =
                    fluf_io_discover_ctx_new_entry(&ctx->fluf_io.discover_ctx,
                                                   &path, NULL, version, dim);
            if (ret_fluf == FLUF_IO_WARNING_DEPTH) {
                continue;
            } else if (ret_fluf) {
                sdm_log(ERROR, "fluf_io discover ctx error");
                in_out_msg->msg_code = FLUF_COAP_CODE_BAD_REQUEST;
                return ret_fluf;
            }
        }
        ret_fluf = fluf_io_discover_ctx_get_payload(&ctx->fluf_io.discover_ctx,
                                                    &out_buff[offset],
                                                    out_buff_len - offset,
                                                    &copied_bytes);
        offset += copied_bytes;
        int ret = handle_get_payload_result(ctx, in_out_msg, ret_fluf, ret_sdm,
                                            offset, out_buff, out_buff_len);
        if (ret != _SDM_CONTINUE) {
            return ret;
        }
    }
}

static int process_bootstrap_discover(sdm_process_ctx_t *ctx,
                                      sdm_data_model_t *dm,
                                      fluf_data_t *in_out_msg,
                                      char *out_buff,
                                      size_t out_buff_len) {
    int ret_sdm = 0;
    int ret_fluf = 0;
    fluf_uri_path_t path;
    const char *version;
    const uint16_t *ssid;
    const char *uri;
    size_t offset = 0;
    size_t copied_bytes;

    initialize_payload(in_out_msg, FLUF_COAP_FORMAT_LINK_FORMAT);
    in_out_msg->msg_code = FLUF_COAP_CODE_CONTENT;

    while (true) {
        if (!ctx->data_to_copy) {
            ret_sdm = sdm_get_bootstrap_discover_record(dm, &path, &version,
                                                        &ssid, &uri);
            if (ret_sdm && ret_sdm != SDM_LAST_RECORD) {
                in_out_msg->msg_code = map_sdm_err_to_coap_code(ret_sdm);
                return ret_sdm;
            }
            ret_fluf = fluf_io_bootstrap_discover_ctx_new_entry(
                    &ctx->fluf_io.bootstrap_discover_ctx, &path, version, ssid,
                    uri);
            if (ret_fluf) {
                sdm_log(ERROR, "fluf_io bootstrap discover ctx error");
                in_out_msg->msg_code = FLUF_COAP_CODE_BAD_REQUEST;
                return ret_fluf;
            }
        }
        ret_fluf = fluf_io_bootstrap_discover_ctx_get_payload(
                &ctx->fluf_io.bootstrap_discover_ctx,
                &out_buff[offset],
                out_buff_len - offset,
                &copied_bytes);
        offset += copied_bytes;
        int ret = handle_get_payload_result(ctx, in_out_msg, ret_fluf, ret_sdm,
                                            offset, out_buff, out_buff_len);
        if (ret != _SDM_CONTINUE) {
            return ret;
        }
    }
}

static int process_read(sdm_process_ctx_t *ctx,
                        sdm_data_model_t *dm,
                        fluf_data_t *in_out_msg,
                        char *out_buff,
                        size_t out_buff_len) {
    int ret_sdm = 0;
    int ret_fluf = 0;
    fluf_io_out_entry_t record;
    memset(&record, 0, sizeof(record));
    size_t offset = 0;
    size_t copied_bytes;

    initialize_payload(in_out_msg,
                       fluf_io_out_ctx_get_format(&ctx->fluf_io.out_ctx));
    in_out_msg->msg_code = FLUF_COAP_CODE_CONTENT;

    while (true) {
        if (!ctx->data_to_copy) {
            ret_sdm = sdm_get_read_entry(dm, &record);
            if (ret_sdm && ret_sdm != SDM_LAST_RECORD) {
                in_out_msg->msg_code = map_sdm_err_to_coap_code(ret_sdm);
                return ret_sdm;
            }
            sdm_log(TRACE, "Reading from:");
            resource_uri_trace_log(&record.path);
            ret_fluf =
                    fluf_io_out_ctx_new_entry(&ctx->fluf_io.out_ctx, &record);
            if (ret_fluf) {
                sdm_log(ERROR, "fluf_io out ctx error");
                in_out_msg->msg_code = FLUF_COAP_CODE_BAD_REQUEST;
                return ret_fluf;
            }
        }
        ret_fluf = fluf_io_out_ctx_get_payload(&ctx->fluf_io.out_ctx,
                                               &out_buff[offset],
                                               out_buff_len - offset,
                                               &copied_bytes);
        offset += copied_bytes;
        int ret = handle_get_payload_result(ctx, in_out_msg, ret_fluf, ret_sdm,
                                            offset, out_buff, out_buff_len);
        if (ret != _SDM_CONTINUE) {
            return ret;
        }
    }
}

static int process_execute(sdm_data_model_t *dm, fluf_data_t *in_out_msg) {
    in_out_msg->msg_code = FLUF_COAP_CODE_CHANGED;
    int ret_val = sdm_execute(dm, (const char *) in_out_msg->payload,
                              in_out_msg->payload_size);
    in_out_msg->payload = NULL;
    in_out_msg->payload_size = 0;
    if (ret_val) {
        in_out_msg->msg_code = map_sdm_err_to_coap_code(ret_val);
    }
    return ret_val;
}

static int process_write(sdm_process_ctx_t *ctx,
                         sdm_data_model_t *dm,
                         fluf_data_t *in_out_msg) {
    int ret_sdm;
    int ret_fluf;
    const fluf_res_value_t *value;
    const fluf_uri_path_t *path;
    fluf_io_out_entry_t record;

    in_out_msg->content_format = FLUF_COAP_FORMAT_NOT_DEFINED;
    if (in_out_msg->operation == FLUF_OP_DM_CREATE) {
        in_out_msg->msg_code = FLUF_COAP_CODE_CREATED;
    } else {
        in_out_msg->msg_code = FLUF_COAP_CODE_CHANGED;
    }

    bool payload_finished =
            in_out_msg->block.block_type == FLUF_OPTION_BLOCK_NOT_DEFINED
            || !in_out_msg->block.more_flag;

    ret_fluf = fluf_io_in_ctx_feed_payload(&ctx->fluf_io.in_ctx,
                                           in_out_msg->payload,
                                           in_out_msg->payload_size,
                                           payload_finished);
    in_out_msg->payload = NULL;
    in_out_msg->payload_size = 0;

    if (ret_fluf) {
        sdm_log(ERROR, "fluf_io in ctx error");
        in_out_msg->msg_code = FLUF_COAP_CODE_BAD_REQUEST;
        return ret_fluf;
    }

    while (true) {
        record.type = FLUF_DATA_TYPE_ANY;
        ret_fluf = fluf_io_in_ctx_get_entry(&ctx->fluf_io.in_ctx, &record.type,
                                            &value, &path);
        if (!ret_fluf || ret_fluf == FLUF_IO_WANT_TYPE_DISAMBIGUATION) {
            if (!path) {
                // SenML CBOR allows to build message with path on the end, so
                // record without path is technically possible for block
                // transfers
                sdm_log(ERROR, "fluf_io in ctx no path given");
                in_out_msg->msg_code = FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
                return -1;
            }
            if (ctx->op == FLUF_OP_DM_CREATE
                    && !dm->op_ctx.write_ctx.instance_creation_attempted) {
                ret_sdm =
                        sdm_create_object_instance(dm, path->ids[FLUF_ID_IID]);
                if (ret_sdm) {
                    in_out_msg->msg_code = map_sdm_err_to_coap_code(ret_sdm);
                    return ret_sdm;
                }
            }
        }
        if (ret_fluf == FLUF_IO_WANT_TYPE_DISAMBIGUATION) {
            record.path = *path;
            if (ctx->op == FLUF_OP_DM_CREATE) {
                record.path.ids[FLUF_ID_IID] =
                        dm->op_ctx.write_ctx.path.ids[FLUF_ID_IID];
            }
            ret_sdm = sdm_get_resource_type(dm, &record.path, &record.type);
            if (ret_sdm) {
                in_out_msg->msg_code = map_sdm_err_to_coap_code(ret_sdm);
                return ret_sdm;
            }
            ret_fluf = fluf_io_in_ctx_get_entry(&ctx->fluf_io.in_ctx,
                                                &record.type, &value, &path);
        }
        if (!ret_fluf) {
            record.value = *value;
            record.path = *path;
            sdm_log(TRACE, "Writing to:");
            resource_uri_trace_log(&record.path);
            ret_sdm = sdm_write_entry(dm, &record);
            if (ret_sdm) {
                in_out_msg->msg_code = map_sdm_err_to_coap_code(ret_sdm);
                return ret_sdm;
            }
        } else if (ret_fluf == FLUF_IO_WANT_NEXT_PAYLOAD && !payload_finished) {
            return SDM_IMPL_WANT_NEXT_MSG;
        } else if (ret_fluf == FLUF_IO_EOF) {
            return 0;
        } else {
            sdm_log(ERROR, "fluf_io in ctx error");
            in_out_msg->msg_code = FLUF_COAP_CODE_BAD_REQUEST;
            return ret_fluf;
        }
    }
}

static int process_operation(sdm_process_ctx_t *ctx,
                             sdm_data_model_t *dm,
                             fluf_data_t *in_out_msg,
                             bool is_bootstrap_server_call,
                             char *out_buff,
                             size_t out_buff_len) {
    switch (in_out_msg->operation) {
    case FLUF_OP_DM_EXECUTE:
        return process_execute(dm, in_out_msg);
    case FLUF_OP_DM_DELETE:
        in_out_msg->payload = NULL;
        in_out_msg->payload_size = 0;
        in_out_msg->msg_code = FLUF_COAP_CODE_DELETED;
        return 0;
    case FLUF_OP_REGISTER:
    case FLUF_OP_UPDATE:
        return process_register(ctx, dm, in_out_msg, out_buff, out_buff_len);
    case FLUF_OP_DM_DISCOVER:
        if (is_bootstrap_server_call) {
            return process_bootstrap_discover(ctx, dm, in_out_msg, out_buff,
                                              out_buff_len);
        } else {
            return process_discover(ctx, dm, in_out_msg, out_buff,
                                    out_buff_len);
        }
    case FLUF_OP_DM_READ:
        return process_read(ctx, dm, in_out_msg, out_buff, out_buff_len);
    case FLUF_OP_DM_WRITE_REPLACE:
    case FLUF_OP_DM_WRITE_PARTIAL_UPDATE:
    case FLUF_OP_DM_CREATE:
        return process_write(ctx, dm, in_out_msg);
    default:
        break;
    }
    return -1;
}

static inline void set_response_operation(fluf_data_t *in_out_msg) {
    if (in_out_msg->operation != FLUF_OP_REGISTER
            && in_out_msg->operation != FLUF_OP_UPDATE) {
        in_out_msg->operation = FLUF_OP_RESPONSE;
    }
}

static bool is_block_transfer_allowed(size_t buff_size) {
    const size_t allowed_block_sized_values[] = { 16,  32,  64,  128,
                                                  256, 512, 1024 };
    for (uint8_t i = 0; i < AVS_ARRAY_SIZE(allowed_block_sized_values); i++) {
        if (buff_size == allowed_block_sized_values[i]) {
            return true;
        }
    }
    return false;
}

static void handle_empty_read(fluf_data_t *in_out_msg, char *out_buff) {
    in_out_msg->msg_code = FLUF_COAP_CODE_CONTENT;
    sdm_log(DEBUG, "No readable resources");
    in_out_msg->msg_code = FLUF_COAP_CODE_CONTENT;
    if (in_out_msg->accept == FLUF_COAP_FORMAT_NOT_DEFINED) {
#ifdef FLUF_WITH_SENML_CBOR
        in_out_msg->content_format = FLUF_COAP_FORMAT_SENML_CBOR;
#else  // FLUF_WITH_SENML_CBOR
        in_out_msg->content_format = FLUF_COAP_FORMAT_OMA_LWM2M_CBOR;
#endif // FLUF_WITH_SENML_CBOR
    } else {
        in_out_msg->content_format = in_out_msg->accept;
    }
    // prepare payload
    if (in_out_msg->content_format == FLUF_COAP_FORMAT_SENML_CBOR) {
        out_buff[0] = 0x80;
        in_out_msg->payload = out_buff;
        in_out_msg->payload_size = 1;
    } else if (in_out_msg->content_format == FLUF_COAP_FORMAT_OMA_LWM2M_CBOR) {
        out_buff[0] = 0xBF;
        out_buff[1] = 0xFF;
        in_out_msg->payload = out_buff;
        in_out_msg->payload_size = 2;
    } else {
        in_out_msg->payload = NULL;
        in_out_msg->payload_size = 0;
    }
}

int sdm_process(sdm_process_ctx_t *ctx,
                sdm_data_model_t *dm,
                fluf_data_t *in_out_msg,
                bool is_bootstrap_server_call,
                char *out_buff,
                size_t out_buff_len) {
    assert(ctx && dm && in_out_msg && out_buff && out_buff_len);

    int ret_val = 0;
    fluf_op_t operation = in_out_msg->operation;

    if (!is_block_transfer_allowed(out_buff_len)) {
        sdm_log(ERROR, "out_buff_len doesn't allow block transfers");
        in_out_msg->msg_code = FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
        set_response_operation(in_out_msg);
        return SDM_ERR_INPUT_ARG;
    }

    if (!ctx->in_progress) {
        ret_val = sdm_operation_begin(dm, operation, is_bootstrap_server_call,
                                      &in_out_msg->uri);
        if (ret_val) {
            in_out_msg->msg_code = map_sdm_err_to_coap_code(ret_val);
            goto finalize;
        }

        ret_val = initialize_fluf_io_ctx(ctx, dm, in_out_msg,
                                         is_bootstrap_server_call);
        if (ret_val == _SDM_EMPTY_READ) {
            handle_empty_read(in_out_msg, out_buff);
            ret_val = 0;
            goto finalize;
        } else if (ret_val) {
            in_out_msg->msg_code = FLUF_COAP_CODE_BAD_REQUEST;
            sdm_log(ERROR, "fluf_io ctx initialization failed");
            goto finalize;
        }

        ctx->data_to_copy = false;
        ctx->in_progress = true;
        ctx->block_number = 0;
        ctx->op = operation;
    } else {
        if (ctx->op != operation) {
            in_out_msg->msg_code = FLUF_COAP_CODE_BAD_REQUEST;
            sdm_log(ERROR, "Previous operation has not been completed");
            ret_val = SDM_ERR_LOGIC;
            goto finalize;
        }

        if (ctx->block_number != in_out_msg->block.number) {
            if (ctx->block_number > in_out_msg->block.number) {
                sdm_log(ERROR, "Block transfer - packet lost");
            } else {
                sdm_log(ERROR, "Block transfer - packet duplicated");
            }
            ret_val = SDM_ERR_INPUT_ARG;
            in_out_msg->msg_code = FLUF_COAP_CODE_REQUEST_ENTITY_INCOMPLETE;
            goto finalize;
        } else if (ctx->block_buff_size != out_buff_len) {
            sdm_log(ERROR, "Buffer size has changed");
            in_out_msg->msg_code = FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
            goto finalize;
        }
    }

    ret_val = process_operation(ctx, dm, in_out_msg, is_bootstrap_server_call,
                                out_buff, out_buff_len);

    if (in_out_msg->block.block_type == FLUF_OPTION_BLOCK_2 && !ret_val) {
        in_out_msg->block.more_flag = false;
    } else if (ret_val == SDM_IMPL_BLOCK_TRANSFER_NEEDED) {
        in_out_msg->block.size = (uint32_t) out_buff_len;
        in_out_msg->block.block_type = FLUF_OPTION_BLOCK_2;
        in_out_msg->block.more_flag = true;
        set_response_operation(in_out_msg);
        sdm_log(DEBUG, "Block transfer, packet number %" PRIu32,
                ctx->block_number);
        ctx->block_number++;
        ctx->block_buff_size = out_buff_len;
        return ret_val;
    } else if (ret_val == SDM_IMPL_WANT_NEXT_MSG) {
        in_out_msg->msg_code = FLUF_COAP_CODE_CONTINUE;
        ctx->block_number++;
        ctx->block_buff_size = out_buff_len;
        set_response_operation(in_out_msg);
        return ret_val;
    }

finalize:
    ctx->in_progress = false;
    set_response_operation(in_out_msg);
    if (ret_val) {
        in_out_msg->payload_size = 0;
        sdm_operation_end(dm);
        return ret_val;
    }
    ret_val = sdm_operation_end(dm);
    if (ret_val) {
        in_out_msg->msg_code = map_sdm_err_to_coap_code(ret_val);
        in_out_msg->payload_size = 0;
    } else {
        sdm_log(TRACE, "Operation end with success");
    }
    return ret_val;
}

int sdm_process_stop(sdm_process_ctx_t *ctx, sdm_data_model_t *dm) {
    if (!ctx->in_progress) {
        sdm_log(ERROR, "No ongoing operation");
        return -1;
    }
    ctx->in_progress = false;
    sdm_log(TRACE, "Operation canceled");
    return sdm_operation_end(dm);
}
