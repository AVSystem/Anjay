/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

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

#define _URI_PATH_MAX_LEN_STR sizeof("65534")
#define _OBSERVE_OPTION_MAX_LEN 3

static int get_uri_path(fluf_coap_options_t *options,
                        fluf_uri_path_t *uri,
                        bool *is_bs_uri) {
    size_t it = 0;
    size_t out_option_size;
    char buff[_URI_PATH_MAX_LEN_STR];

    *is_bs_uri = false;
    uri->uri_len = 0;

    while (true) {
        int res = _fluf_coap_options_get_data_iterate(
                options, _FLUF_COAP_OPTION_URI_PATH, &it, &out_option_size,
                buff, sizeof(buff));
        if (!res) {
            if (uri->uri_len == FLUF_URI_PATH_MAX_LENGTH) {
                return FLUF_ERR_MALFORMED_MESSAGE; // uri path too long
            }
            // if `bs` in first record -> BootstrapFinish operation
            if (!uri->uri_len && out_option_size == 2 && buff[0] == 'b'
                    && buff[1] == 's') {
                *is_bs_uri = true;
                return 0;
            }
            // empty option
            if (!out_option_size) {
                if (!uri->uri_len) { // empty path in first segment
                    return 0;
                } else {
                    return FLUF_ERR_MALFORMED_MESSAGE;
                }
            }
            // try to convert string value to int
            uint32_t converted_value;
            if (fluf_string_to_uint32_value(&converted_value, buff,
                                            out_option_size)) {
                return FLUF_ERR_MALFORMED_MESSAGE;
            }
            uri->ids[uri->uri_len] = (uint16_t) converted_value;
            uri->uri_len++;
        } else if (res == _FLUF_COAP_OPTION_MISSING) {
            return 0;
        } else {
            return FLUF_ERR_MALFORMED_MESSAGE;
        }
    }
}

static int get_location_path(fluf_coap_options_t *opt,
                             fluf_location_path_t *loc_path) {
    memset(loc_path, 0, sizeof(fluf_location_path_t));
    bool rd_flag_read = false;

    for (size_t i = 0; i < opt->options_number; i++) {
        if (opt->options[i].option_number == _FLUF_COAP_OPTION_LOCATION_PATH) {
            if (!rd_flag_read) {
                rd_flag_read = true;
                if (opt->options[i].payload_len != 2
                        || ((const char *) opt->options[i].payload)[0] != 'r'
                        || ((const char *) opt->options[i].payload)[1] != 'd') {
                    // first location path argument need to be "/rd"
                    return FLUF_ERR_MALFORMED_MESSAGE;
                }
            } else {
                if (loc_path->location_count
                        >= FLUF_MAX_ALLOWED_LOCATION_PATHS_NUMBER) {
                    return FLUF_ERR_LOCATION_PATHS_NUMBER;
                }
                loc_path->location_len[loc_path->location_count] =
                        opt->options[i].payload_len;
                loc_path->location[loc_path->location_count] =
                        (const char *) opt->options[i].payload;
                loc_path->location_count++;
            }
        }
    }
    return 0;
}

static int get_observe_option(fluf_coap_options_t *options,
                              bool *opt_present,
                              uint8_t *out_value) {
    uint8_t observe_buff[_OBSERVE_OPTION_MAX_LEN] = { 0 };
    size_t observe_option_size = 0;

    *out_value = 0;
    *opt_present = false;

    int res = _fluf_coap_options_get_data_iterate(
            options, _FLUF_COAP_OPTION_OBSERVE, NULL, &observe_option_size,
            observe_buff, sizeof(observe_buff));
    if (res == _FLUF_COAP_OPTION_MISSING) {
        return 0;
    } else if (res) {
        return FLUF_ERR_MALFORMED_MESSAGE;
    }

    *opt_present = true;
    for (size_t i = 0; i < observe_option_size; i++) {
        if (observe_buff[i]) {
            // only two possible values `0` or `1`
            // check RFC7641: https://datatracker.ietf.org/doc/rfc7641/
            *out_value = 1;
            break;
        }
    }
    return 0;
}

static int fluf_etag_decode(fluf_coap_options_t *opts, fluf_etag_t *etag) {
    size_t etag_size = 0;
    int res = _fluf_coap_options_get_data_iterate(opts, _FLUF_COAP_OPTION_ETAG,
                                                  NULL, &etag_size, etag->bytes,
                                                  FLUF_MAX_ETAG_LENGTH);
    if (res == _FLUF_COAP_OPTION_MISSING) {
        return 0;
    }
    etag->size = (uint8_t) etag_size;
    return res;
}

static int validate_uri_path(fluf_op_t operation, fluf_uri_path_t *uri) {
    switch (operation) {
    case FLUF_OP_DM_READ:
    case FLUF_OP_DM_WRITE_PARTIAL_UPDATE:
    case FLUF_OP_DM_WRITE_REPLACE:
    case FLUF_OP_INF_OBSERVE:
    case FLUF_OP_INF_CANCEL_OBSERVE:
        if (!fluf_uri_path_has(uri, FLUF_ID_OID)) {
            return FLUF_ERR_INPUT_ARG;
        }
        break;
    case FLUF_OP_DM_DISCOVER:
        if (fluf_uri_path_has(uri, FLUF_ID_RIID)) {
            return FLUF_ERR_INPUT_ARG;
        }
        break;
    case FLUF_OP_DM_EXECUTE:
        if (!fluf_uri_path_is(uri, FLUF_ID_RID)) {
            return FLUF_ERR_INPUT_ARG;
        }
        break;
    case FLUF_OP_DM_CREATE:
        if (!fluf_uri_path_is(uri, FLUF_ID_OID)) {
            return FLUF_ERR_INPUT_ARG;
        }
        break;
    case FLUF_OP_DM_DELETE:
        if (fluf_uri_path_is(uri, FLUF_ID_RID)) {
            return FLUF_ERR_INPUT_ARG;
        }
        break;
    default:
        break;
    }
    return 0;
}

static int decode_request_msg(fluf_coap_options_t *options,
                              fluf_data_t *data,
                              bool is_bs_uri,
                              fluf_coap_udp_type_t type) {
    uint8_t observe_value;
    bool observe_opt_present;
    if (get_observe_option(options, &observe_opt_present, &observe_value)) {
        return FLUF_ERR_MALFORMED_MESSAGE;
    }

    if (type == FLUF_COAP_UDP_TYPE_NON_CONFIRMABLE) {
        if (data->msg_code == FLUF_COAP_CODE_POST) {
            data->operation = FLUF_OP_DM_EXECUTE;
            return 0;
        }
        return FLUF_ERR_MALFORMED_MESSAGE;
    }

    switch (data->msg_code) {
    case FLUF_COAP_CODE_GET:
        if (observe_opt_present) {
            if (observe_value) {
                data->operation = FLUF_OP_INF_CANCEL_OBSERVE;
            } else {
                data->operation = FLUF_OP_INF_OBSERVE;
            }
        } else {
            if (data->accept == FLUF_COAP_FORMAT_LINK_FORMAT) {
                data->operation = FLUF_OP_DM_DISCOVER;
            } else {
                data->operation = FLUF_OP_DM_READ;
            }
        }
        break;

    case FLUF_COAP_CODE_POST:
        if (is_bs_uri) {
            data->operation = FLUF_OP_BOOTSTRAP_FINISH;
        } else if (fluf_uri_path_is(&data->uri, FLUF_ID_OID)) {
            data->operation = FLUF_OP_DM_CREATE;
        } else if (fluf_uri_path_is(&data->uri, FLUF_ID_IID)) {
            data->operation = FLUF_OP_DM_WRITE_PARTIAL_UPDATE;
        } else if (fluf_uri_path_is(&data->uri, FLUF_ID_RID)) {
            data->operation = FLUF_OP_DM_EXECUTE;
        } else {
            return FLUF_ERR_MALFORMED_MESSAGE;
        }
        break;

    case FLUF_COAP_CODE_FETCH:
        if (observe_opt_present) {
            if (observe_value) {
                data->operation = FLUF_OP_INF_CANCEL_OBSERVE_COMP;
            } else {
                data->operation = FLUF_OP_INF_OBSERVE_COMP;
            }
        } else {
            data->operation = FLUF_OP_DM_READ_COMP;
        }
        break;

    case FLUF_COAP_CODE_PUT:
        if (data->content_format != FLUF_COAP_FORMAT_NOT_DEFINED) {
            data->operation = FLUF_OP_DM_WRITE_REPLACE;
        } else {
            data->operation = FLUF_OP_DM_WRITE_ATTR;
        }
        break;

    case FLUF_COAP_CODE_IPATCH:
        data->operation = FLUF_OP_DM_WRITE_COMP;
        break;
    case FLUF_COAP_CODE_DELETE:
        data->operation = FLUF_OP_DM_DELETE;
        break;
    default:
        return FLUF_ERR_MALFORMED_MESSAGE;
    }

    return 0;
}

static int decode_udp_msg(uint8_t *msg, size_t msg_size, fluf_data_t *data) {
    // coap msg decode
    fluf_coap_udp_msg_t coap_msg;
    _FLUF_COAP_OPTIONS_INIT_EMPTY(opts, FLUF_MAX_ALLOWED_OPTIONS_NUMBER);
    coap_msg.options = &opts;
    int res = _fluf_coap_udp_msg_decode(&coap_msg, msg, msg_size);
    _RET_IF_ERROR(res);

    data->payload = coap_msg.payload;
    data->payload_size = coap_msg.payload_size;

    // copy token and message id
    data->coap.coap_udp.token.size = coap_msg.token.size;
    memcpy(data->coap.coap_udp.token.bytes, coap_msg.token.bytes,
           coap_msg.token.size);
    data->coap.coap_udp.message_id =
            _fluf_coap_udp_header_get_id(&coap_msg.header);
    data->coap.coap_udp.type = _fluf_coap_udp_header_get_type(&coap_msg.header);

    data->msg_code = coap_msg.header.code;

    // recognize operation
    if (data->coap.coap_udp.type == FLUF_COAP_UDP_TYPE_RESET) {
        data->operation = FLUF_OP_COAP_RESET;
    } else if (data->coap.coap_udp.type == FLUF_COAP_UDP_TYPE_CONFIRMABLE
               && data->msg_code == FLUF_COAP_CODE_EMPTY) {
        data->operation = FLUF_OP_COAP_PING;
    } else if (data->msg_code >= FLUF_COAP_CODE_GET
               && data->msg_code <= FLUF_COAP_CODE_IPATCH
               && (data->coap.coap_udp.type == FLUF_COAP_UDP_TYPE_CONFIRMABLE
                   || data->coap.coap_udp.type
                              == FLUF_COAP_UDP_TYPE_NON_CONFIRMABLE)) {
        // update content format and accept option if present in message
        _fluf_coap_options_get_u16_iterate(coap_msg.options,
                                           _FLUF_COAP_OPTION_CONTENT_FORMAT,
                                           NULL, &data->content_format);
        _fluf_coap_options_get_u16_iterate(coap_msg.options,
                                           _FLUF_COAP_OPTION_ACCEPT, NULL,
                                           &data->accept);

        // get uri path if present
        bool is_bs_uri;
        res = get_uri_path(coap_msg.options, &data->uri, &is_bs_uri);
        _RET_IF_ERROR(res);
        // server initiated exchange
        res = decode_request_msg(coap_msg.options, data, is_bs_uri,
                                 data->coap.coap_udp.type);
        _RET_IF_ERROR(res);
        res = validate_uri_path(data->operation, &data->uri);
    } else if (data->msg_code >= FLUF_COAP_CODE_CREATED
               && data->msg_code <= FLUF_COAP_CODE_PROXYING_NOT_SUPPORTED) {
        // server response
        data->operation = FLUF_OP_RESPONSE;
    } else if (data->msg_code == FLUF_COAP_CODE_EMPTY
               && data->coap.coap_udp.type
                          == FLUF_COAP_UDP_TYPE_ACKNOWLEDGEMENT) {
        data->operation = FLUF_OP_RESPONSE;
        return 0;
    } else {
        res = FLUF_ERR_COAP_BAD_MSG;
    }
    _RET_IF_ERROR(res);

    // decode attributes
    if (data->operation == FLUF_OP_DM_DISCOVER) {
        res = fluf_attr_discover_decode(coap_msg.options,
                                        &data->attr.discover_attr);
    } else if (data->operation == FLUF_OP_DM_WRITE_ATTR
               || data->operation == FLUF_OP_INF_OBSERVE
               || data->operation == FLUF_OP_INF_OBSERVE_COMP) {
        res = fluf_attr_notification_attr_decode(coap_msg.options,
                                                 &data->attr.notification_attr);
    }
    _RET_IF_ERROR(res);

    // decode block option if appears
    res = _fluf_block_decode(coap_msg.options, &data->block);
    _RET_IF_ERROR(res);

    // check etag presence
    res = fluf_etag_decode(coap_msg.options, &data->etag);
    _RET_IF_ERROR(res);

    // decode location-path if present
    if (data->operation == FLUF_OP_RESPONSE) {
        res = get_location_path(coap_msg.options, &data->location_path);
    }

    return res;
}

int fluf_msg_decode(uint8_t *msg,
                    size_t msg_size,
                    fluf_binding_type_t binding,
                    fluf_data_t *data) {
    int res;

    data->accept = FLUF_COAP_FORMAT_NOT_DEFINED;
    data->content_format = FLUF_COAP_FORMAT_NOT_DEFINED;
    data->binding = binding;

    switch (data->binding) {
    case FLUF_BINDING_UDP:
    case FLUF_BINDING_DTLS_PSK:
        res = decode_udp_msg(msg, msg_size, data);
        break;
    default:
        res = FLUF_ERR_BINDING;
        break;
    }

    return res;
}
