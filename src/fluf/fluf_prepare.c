/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avsystem/commons/avs_utils.h>

#include <fluf/fluf.h>
#include <fluf/fluf_config.h>
#include <fluf/fluf_utils.h>

#include "fluf_attributes.h"
#include "fluf_block.h"
#include "fluf_coap_udp_msg.h"

#define _RET_IF_ERROR(Val) \
    if (Val) {             \
        return Val;        \
    }

static uint16_t g_fluf_msg_id;
static avs_rand_seed_t g_rand_seed;

static int add_uri_path(fluf_coap_options_t *opts, fluf_data_t *data) {
    int res = 0;

    if (data->operation == FLUF_OP_BOOTSTRAP_REQ) {
        res = _fluf_coap_options_add_string(opts, _FLUF_COAP_OPTION_URI_PATH,
                                            "bs");
    } else if (data->operation == FLUF_OP_BOOTSTRAP_PACK_REQ) {
        res = _fluf_coap_options_add_string(opts, _FLUF_COAP_OPTION_URI_PATH,
                                            "bspack");
    } else if (data->operation == FLUF_OP_REGISTER) {
        res = _fluf_coap_options_add_string(opts, _FLUF_COAP_OPTION_URI_PATH,
                                            "rd");
    } else if (data->operation == FLUF_OP_INF_CON_SEND
               || data->operation == FLUF_OP_INF_NON_CON_SEND) {
        res = _fluf_coap_options_add_string(opts, _FLUF_COAP_OPTION_URI_PATH,
                                            "dp");
    } else if (data->operation == FLUF_OP_UPDATE
               || data->operation == FLUF_OP_DEREGISTER) {
        res = _fluf_coap_options_add_string(opts, _FLUF_COAP_OPTION_URI_PATH,
                                            "rd");
        if (res) {
            return res;
        }
        for (size_t i = 0; i < data->location_path.location_count; i++) {
            res = _fluf_coap_options_add_data(
                    opts, _FLUF_COAP_OPTION_URI_PATH,
                    data->location_path.location[i],
                    data->location_path.location_len[i]);
            if (res) {
                return res;
            }
        }
    }

    if (res) {
        return res;
    }
    return 0;
}

static int prepare_udp_msg(uint8_t *buff,
                           size_t buff_size,
                           size_t *out_msg_size,
                           fluf_data_t *data) {
    if (data->operation == FLUF_OP_INF_CON_NOTIFY) {
        // new msg_id, token reuse
        data->coap.coap_udp.type = FLUF_COAP_UDP_TYPE_CONFIRMABLE;
        data->coap.coap_udp.message_id = ++g_fluf_msg_id;
    } else if (data->operation == FLUF_OP_INF_NON_CON_NOTIFY) {
        // new msg_id, token reuse
        data->coap.coap_udp.type = FLUF_COAP_UDP_TYPE_NON_CONFIRMABLE;
        data->coap.coap_udp.message_id = ++g_fluf_msg_id;
    } else if (data->operation == FLUF_OP_RESPONSE) {
        // msg_id and token reuse
        data->coap.coap_udp.type = FLUF_COAP_UDP_TYPE_ACKNOWLEDGEMENT;
    } else {
        // client request with new msg_id and token
        data->coap.coap_udp.type = (data->operation == FLUF_OP_INF_NON_CON_SEND)
                                           ? FLUF_COAP_UDP_TYPE_NON_CONFIRMABLE
                                           : FLUF_COAP_UDP_TYPE_CONFIRMABLE;
        data->coap.coap_udp.message_id = ++g_fluf_msg_id;
        data->coap.coap_udp.token.size = FLUF_COAP_MAX_TOKEN_LENGTH;

        assert(FLUF_COAP_MAX_TOKEN_LENGTH == 8);
        uint64_t token = avs_rand64_r(&g_rand_seed);
        memcpy(data->coap.coap_udp.token.bytes, &token, sizeof(token));
    }

    switch (data->operation) {
    case FLUF_OP_BOOTSTRAP_REQ:
        data->msg_code = FLUF_COAP_CODE_POST;
        break;
    case FLUF_OP_BOOTSTRAP_PACK_REQ:
        data->msg_code = FLUF_COAP_CODE_GET;
        break;
    case FLUF_OP_REGISTER:
        data->msg_code = FLUF_COAP_CODE_POST;
        break;
    case FLUF_OP_UPDATE:
        data->msg_code = FLUF_COAP_CODE_POST;
        break;
    case FLUF_OP_DEREGISTER:
        data->msg_code = FLUF_COAP_CODE_DELETE;
        break;
    case FLUF_OP_INF_CON_NOTIFY:
    case FLUF_OP_INF_NON_CON_NOTIFY:
        data->msg_code = FLUF_COAP_CODE_CONTENT;
        break;
    case FLUF_OP_INF_CON_SEND:
    case FLUF_OP_INF_NON_CON_SEND:
        data->msg_code = FLUF_COAP_CODE_POST;
        break;
    case FLUF_OP_RESPONSE:
        // msg code must be defined
        break;
    default:
        return -1;
    }

    _FLUF_COAP_OPTIONS_INIT_EMPTY(opts, FLUF_MAX_ALLOWED_OPTIONS_NUMBER);
    fluf_coap_udp_msg_t msg = {
        .header = _fluf_coap_udp_header_init(
                data->coap.coap_udp.type, data->coap.coap_udp.token.size,
                data->msg_code, data->coap.coap_udp.message_id),
        .options = &opts,
        .payload = data->payload,
        .payload_size = data->payload_size,
        .token = data->coap.coap_udp.token
    };
    int res = _fluf_coap_udp_header_serialize(&msg, buff, buff_size);
    _RET_IF_ERROR(res);

    // content-format
    if (data->payload_size) {
        if (data->content_format != FLUF_COAP_FORMAT_NOT_DEFINED) {
            res = _fluf_coap_options_add_u16(&opts,
                                             _FLUF_COAP_OPTION_CONTENT_FORMAT,
                                             data->content_format);
            _RET_IF_ERROR(res);
        } else {
            return FLUF_ERR_INPUT_ARG;
        }
    }

    // accept option: only for BootstrapPack-Request
    if (data->accept != FLUF_COAP_FORMAT_NOT_DEFINED
            && data->operation == FLUF_OP_BOOTSTRAP_PACK_REQ) {
        res = _fluf_coap_options_add_u16(&opts, _FLUF_COAP_OPTION_ACCEPT,
                                         data->accept);
        _RET_IF_ERROR(res);
    }

    // uri-path
    res = add_uri_path(&opts, data);
    _RET_IF_ERROR(res);

    // observe option: only for Notify
    if (data->operation == FLUF_OP_INF_CON_NOTIFY
            || data->operation == FLUF_OP_INF_NON_CON_NOTIFY) {
        res = _fluf_coap_options_add_u64(&opts, _FLUF_COAP_OPTION_OBSERVE,
                                         data->observe_number);
        _RET_IF_ERROR(res);
    }

    // block option
    if (data->block.block_type != FLUF_OPTION_BLOCK_NOT_DEFINED) {
        res = _fluf_block_prepare(&opts, &data->block);
        _RET_IF_ERROR(res);
    }

    // etag
    if (data->etag.size) {
        res = _fluf_coap_options_add_data(&opts, _FLUF_COAP_OPTION_ETAG,
                                          data->etag.bytes, data->etag.size);
        _RET_IF_ERROR(res);
    }

    // attributes: uri-query
    if (data->operation == FLUF_OP_REGISTER
            || data->operation == FLUF_OP_UPDATE) {
        res = fluf_attr_register_prepare(&opts, &data->attr.register_attr);
    } else if (data->operation == FLUF_OP_BOOTSTRAP_REQ) {
        res = fluf_attr_bootstrap_prepare(&opts, &data->attr.bootstrap_attr);
    }
    _RET_IF_ERROR(res);
    // msg serialize
    return _fluf_coap_udp_msg_serialize(&msg, buff, buff_size, out_msg_size);
}

int fluf_msg_prepare(fluf_data_t *data,
                     uint8_t *out_buff,
                     size_t out_buff_size,
                     size_t *out_msg_size) {
    int res;

    switch (data->binding) {
    case FLUF_BINDING_UDP:
    case FLUF_BINDING_DTLS_PSK:
        res = prepare_udp_msg(out_buff, out_buff_size, out_msg_size, data);
        break;
    default:
        res = FLUF_ERR_BINDING;
        break;
    }

    return res;
}

void fluf_init(uint32_t random_seed) {
    g_rand_seed = (avs_rand_seed_t) random_seed;
    g_fluf_msg_id = (uint16_t) avs_rand32_r(&g_rand_seed);
}
