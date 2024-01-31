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
        return FLUF_COAP_CODE_BAD_REQUEST;
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
        fluf_io_register_ctx_init(&ctx->fluf_io.register_ctx);
        return 0;
    case FLUF_OP_DM_DISCOVER:
        if (is_bootstrap_server_call) {
            return fluf_io_bootstrap_discover_ctx_init(
                    &ctx->fluf_io.bootstrap_discover_ctx, &msg->uri);
        } else {
            uint8_t depth = (uint8_t) msg->attr.discover_attr.depth;
            return fluf_io_discover_ctx_init(
                    &ctx->fluf_io.discover_ctx, &msg->uri,
                    msg->attr.discover_attr.has_depth ? &depth : NULL);
        }
    case FLUF_OP_DM_WRITE_REPLACE:
    case FLUF_OP_DM_WRITE_PARTIAL_UPDATE:
    case FLUF_OP_DM_CREATE:
        return fluf_io_in_ctx_init(&ctx->fluf_io.in_ctx, msg->operation,
                                   &msg->uri, msg->content_format);
    case FLUF_OP_DM_READ:
        if (sdm_get_readable_res_count(dm, &res_count)) {
            return -1;
        }
        return fluf_io_out_ctx_init(&ctx->fluf_io.out_ctx, FLUF_OP_DM_READ,
                                    &msg->uri, res_count, msg->accept);
    case FLUF_OP_DM_EXECUTE:
    case FLUF_OP_DM_DELETE:
        return 0;
    default:
        break;
    }
    return -1;
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

    in_out_msg->content_format = FLUF_COAP_FORMAT_LINK_FORMAT;

    while (1) {
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
        if (!ret_fluf) {
            ctx->data_to_copy = false;
        } else if (ret_fluf == FLUF_IO_NEED_NEXT_CALL) {
            ctx->data_to_copy = true;
            in_out_msg->payload = out_buff;
            in_out_msg->payload_size = offset;
            return SDM_IMPL_BLOCK_TRANSFER_NEEDED;
        } else {
            sdm_log(ERROR, "fluf_io register ctx error");
            return ret_fluf;
        }
        if (ret_sdm == SDM_LAST_RECORD) {
            in_out_msg->payload = out_buff;
            in_out_msg->payload_size = offset;
            return 0;
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

    in_out_msg->content_format = FLUF_COAP_FORMAT_LINK_FORMAT;
    in_out_msg->msg_code = FLUF_COAP_CODE_CONTENT;

    while (1) {
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
        if (!ret_fluf) {
            ctx->data_to_copy = false;
        } else if (ret_fluf == FLUF_IO_NEED_NEXT_CALL) {
            ctx->data_to_copy = true;
            in_out_msg->payload = out_buff;
            in_out_msg->payload_size = offset;
            return SDM_IMPL_BLOCK_TRANSFER_NEEDED;
        } else {
            sdm_log(ERROR, "fluf_io discover ctx error");
            in_out_msg->msg_code = FLUF_COAP_CODE_BAD_REQUEST;
            return ret_fluf;
        }
        if (ret_sdm == SDM_LAST_RECORD) {
            in_out_msg->payload = out_buff;
            in_out_msg->payload_size = offset;
            return 0;
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

    in_out_msg->content_format = FLUF_COAP_FORMAT_LINK_FORMAT;
    in_out_msg->msg_code = FLUF_COAP_CODE_CONTENT;

    while (1) {
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
        if (!ret_fluf) {
            ctx->data_to_copy = false;
        } else if (ret_fluf == FLUF_IO_NEED_NEXT_CALL) {
            ctx->data_to_copy = true;
            in_out_msg->payload = out_buff;
            in_out_msg->payload_size = offset;
            return SDM_IMPL_BLOCK_TRANSFER_NEEDED;
        } else {
            sdm_log(ERROR, "fluf_io bootstrap discover ctx error");
            in_out_msg->msg_code = FLUF_COAP_CODE_BAD_REQUEST;
            return ret_fluf;
        }
        if (ret_sdm == SDM_LAST_RECORD) {
            in_out_msg->payload = out_buff;
            in_out_msg->payload_size = offset;
            return 0;
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
    // HACK: fast drut
    memset(&record, 0, sizeof(record));
    size_t offset = 0;
    size_t copied_bytes;

    in_out_msg->content_format =
            fluf_io_out_ctx_get_format(&ctx->fluf_io.out_ctx);
    in_out_msg->msg_code = FLUF_COAP_CODE_CONTENT;

    while (1) {
        if (!ctx->data_to_copy) {
            ret_sdm = sdm_get_read_entry(dm, &record);
            if (ret_sdm && ret_sdm != SDM_LAST_RECORD) {
                in_out_msg->msg_code = map_sdm_err_to_coap_code(ret_sdm);
                return ret_sdm;
            }
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
        if (!ret_fluf) {
            ctx->data_to_copy = false;
        } else if (ret_fluf == FLUF_IO_NEED_NEXT_CALL) {
            ctx->data_to_copy = true;
            in_out_msg->payload = out_buff;
            in_out_msg->payload_size = offset;
            return SDM_IMPL_BLOCK_TRANSFER_NEEDED;
        } else {
            sdm_log(ERROR, "fluf_io out ctx error");
            in_out_msg->msg_code = FLUF_COAP_CODE_BAD_REQUEST;
            return ret_fluf;
        }
        if (ret_sdm == SDM_LAST_RECORD) {
            in_out_msg->payload = out_buff;
            in_out_msg->payload_size = offset;
            return 0;
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
    fluf_data_type_t type;
    const fluf_res_value_t *value;
    const fluf_uri_path_t *path;

    in_out_msg->content_format = FLUF_COAP_FORMAT_NOT_DEFINED;
    if (in_out_msg->operation == FLUF_OP_DM_CREATE) {
        in_out_msg->msg_code = FLUF_COAP_CODE_CREATED;
    } else {
        in_out_msg->msg_code = FLUF_COAP_CODE_CHANGED;
    }

    bool payload_finished =
            (in_out_msg->block.block_type == FLUF_OPTION_BLOCK_NOT_DEFINED)
            || (in_out_msg->block.more_flag == false);

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

    while (1) {
        type = FLUF_DATA_TYPE_ANY;
        ret_fluf = fluf_io_in_ctx_get_entry(&ctx->fluf_io.in_ctx, &type, &value,
                                            &path);
        if (ret_fluf == FLUF_IO_WANT_TYPE_DISAMBIGUATION) {
            ret_sdm = sdm_get_resource_type(dm, path, &type);
            if (ret_sdm) {
                in_out_msg->msg_code = map_sdm_err_to_coap_code(ret_sdm);
                return ret_sdm;
            }
            ret_fluf = fluf_io_in_ctx_get_entry(&ctx->fluf_io.in_ctx, &type,
                                                &value, &path);
        }
        if (!ret_fluf) {
            fluf_io_out_entry_t record = {
                .path = *path,
                .type = type,
                .value = *value
            };
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
    // No payload in incoming message, prepare for response.
    if (in_out_msg->operation != FLUF_OP_DM_WRITE_REPLACE
            && in_out_msg->operation != FLUF_OP_DM_WRITE_PARTIAL_UPDATE
            && in_out_msg->operation != FLUF_OP_DM_CREATE
            && in_out_msg->operation != FLUF_OP_DM_EXECUTE) {
        in_out_msg->payload = NULL;
        in_out_msg->payload_size = 0;
    }

    switch (in_out_msg->operation) {
    case FLUF_OP_DM_EXECUTE:
        return process_execute(dm, in_out_msg);
    case FLUF_OP_DM_DELETE:
        in_out_msg->msg_code = FLUF_COAP_CODE_DELETED;
        break;
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

    return 0;
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
    for (uint8_t i = 0; i < sizeof(allowed_block_sized_values); i++) {
        if (buff_size == allowed_block_sized_values[i]) {
            return true;
        }
    }
    return false;
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

    if (!ctx->in_progress) {
        ret_val = sdm_operation_begin(dm, operation, is_bootstrap_server_call,
                                      &in_out_msg->uri);
        if (ret_val) {
            in_out_msg->msg_code = map_sdm_err_to_coap_code(ret_val);
            goto finalize;
        }

        ret_val = initialize_fluf_io_ctx(ctx, dm, in_out_msg,
                                         is_bootstrap_server_call);
        if (ret_val) {
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
            sdm_log(ERROR, "Incorrect sdm_impl usage");
            ret_val = SDM_ERR_LOGIC;
            goto finalize;
        }

        if (in_out_msg->block.block_type != FLUF_OPTION_BLOCK_NOT_DEFINED
                && ctx->block_number != in_out_msg->block.number) {
            sdm_log(ERROR, "Block transfer - packet lost");
            ret_val = SDM_ERR_INPUT_ARG;
            in_out_msg->msg_code = FLUF_COAP_CODE_REQUEST_ENTITY_INCOMPLETE;
            goto finalize;
        }
    }

    ret_val = process_operation(ctx, dm, in_out_msg, is_bootstrap_server_call,
                                out_buff, out_buff_len);

    if (in_out_msg->block.block_type == FLUF_OPTION_BLOCK_2 && !ret_val) {
        in_out_msg->block.more_flag = false;
    } else if (ret_val == SDM_IMPL_BLOCK_TRANSFER_NEEDED) {
        if (!is_block_transfer_allowed(out_buff_len)) {
            sdm_log(ERROR, "out_buff_len doesn't allow block transfers");
            ret_val = SDM_ERR_INPUT_ARG;
            in_out_msg->msg_code = FLUF_COAP_CODE_BAD_REQUEST;
        }
        in_out_msg->block.size = (uint32_t) out_buff_len;
        in_out_msg->block.block_type = FLUF_OPTION_BLOCK_2;
        in_out_msg->block.more_flag = true;
        ctx->block_number++;
        set_response_operation(in_out_msg);
        return ret_val;
    } else if (ret_val == SDM_IMPL_WANT_NEXT_MSG) {
        ctx->block_number++;
        set_response_operation(in_out_msg);
        return ret_val;
    }

finalize:
    ctx->in_progress = false;
    set_response_operation(in_out_msg);
    if (ret_val) {
        sdm_operation_end(dm);
        return ret_val;
    }
    return sdm_operation_end(dm);
}

int sdm_process_stop(sdm_process_ctx_t *ctx, sdm_data_model_t *dm) {
    if (ctx->in_progress) {
        ctx->in_progress = false;
        return sdm_operation_end(dm);
    } else {
        sdm_log(ERROR, "No ongoing operation");
        return -1;
    }
}
