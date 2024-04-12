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

#include "fluf_coap_udp_msg.h"
#include "fluf_options.h"

typedef struct {
    uint8_t *write_ptr;
    size_t bytes_left;
} bytes_appender_t;

typedef struct {
    uint8_t *read_ptr;
    size_t bytes_left;
} bytes_dispenser_t;

static int
bytes_append(bytes_appender_t *appender, const void *data, size_t size_bytes) {
    if (appender->bytes_left < size_bytes) {
        return FLUF_ERR_BUFF;
    }
    memcpy(appender->write_ptr, data ? data : "", size_bytes);
    appender->write_ptr += size_bytes;
    appender->bytes_left -= size_bytes;
    return 0;
}

static int
bytes_extract(bytes_dispenser_t *dispenser, void *out, size_t size_bytes) {
    if (dispenser->bytes_left < size_bytes) {
        return FLUF_ERR_MALFORMED_MESSAGE;
    }

    if (out) {
        memcpy(out, dispenser->read_ptr, size_bytes);
    }

    dispenser->read_ptr += size_bytes;
    dispenser->bytes_left -= size_bytes;
    return 0;
}

static uint8_t code_get_class(uint8_t code) {
    return (uint8_t) _FLUF_FIELD_GET(code, _FLUF_COAP_CODE_CLASS_MASK,
                                     _FLUF_COAP_CODE_CLASS_SHIFT);
}

static uint8_t code_get_detail(uint8_t code) {
    return (uint8_t) _FLUF_FIELD_GET(code, _FLUF_COAP_CODE_DETAIL_MASK,
                                     _FLUF_COAP_CODE_DETAIL_SHIFT);
}

static bool fluf_coap_code_is_request(uint8_t code) {
    return code_get_class(code) == 0 && code_get_detail(code) > 0;
}

static bool is_msg_header_valid(const fluf_coap_udp_header_t *hdr) {
    uint8_t version = _fluf_coap_udp_header_get_version(hdr);
    if (version != 1) {
        return false;
    }

    if (_fluf_coap_udp_header_get_token_length(hdr)
            > FLUF_COAP_MAX_TOKEN_LENGTH) {
        return false;
    }

    const fluf_coap_udp_type_t type = _fluf_coap_udp_header_get_type(hdr);

    switch (type) {
    case FLUF_COAP_UDP_TYPE_ACKNOWLEDGEMENT:
        if (fluf_coap_code_is_request(hdr->code)) {
            return false;
        }
        break;
    case FLUF_COAP_UDP_TYPE_NON_CONFIRMABLE:
        // FLUF_COAP_CODE_EMPTY with FLUF_COAP_UDP_TYPE_CONFIRMABLE
        // means "CoAP ping"
        if (hdr->code == FLUF_COAP_CODE_EMPTY) {
            return false;
        }
        break;
    case FLUF_COAP_UDP_TYPE_RESET:
        if (hdr->code != FLUF_COAP_CODE_EMPTY) {
            return false;
        }
        break;

    default:
        break;
    }

    return true;
}

static int decode_header(fluf_coap_udp_header_t *out_hdr,
                         bytes_dispenser_t *dispenser) {
    if (bytes_extract(dispenser, out_hdr, sizeof(*out_hdr))
            || !is_msg_header_valid(out_hdr)) {
        return FLUF_ERR_MALFORMED_MESSAGE;
    }

    if (out_hdr->code == FLUF_COAP_CODE_EMPTY && dispenser->bytes_left > 0) {
        return FLUF_ERR_MALFORMED_MESSAGE;
    }

    return 0;
}

static int decode_payload(void **out_payload,
                          size_t *out_payload_size,
                          bytes_dispenser_t *dispenser) {
    *out_payload = dispenser->read_ptr;
    *out_payload_size = dispenser->bytes_left;

    if (*out_payload_size == 0) {
        // no payload after options
        *out_payload = NULL;
        return 0;
    }

    // ensured by decode_options
    assert(*dispenser->read_ptr == _FLUF_COAP_PAYLOAD_MARKER);

    *out_payload = dispenser->read_ptr + 1;
    *out_payload_size -= 1;

    if (*out_payload_size == 0) {
        return FLUF_ERR_MALFORMED_MESSAGE;
    }

    return 0;
}

static inline int decode_token(fluf_coap_udp_msg_t *out_msg,
                               bytes_dispenser_t *dispenser) {
    uint8_t token_size =
            _fluf_coap_udp_header_get_token_length(&out_msg->header);
    out_msg->token.size = token_size;

    assert(token_size <= sizeof(out_msg->token.bytes));

    if (bytes_extract(dispenser, out_msg->token.bytes, out_msg->token.size)) {
        return FLUF_ERR_MALFORMED_MESSAGE;
    }

    return 0;
}

static int decode_options(fluf_coap_udp_msg_t *out_msg,
                          bytes_dispenser_t *dispenser) {
    int res;
    size_t bytes_read;

    res = _fluf_coap_options_decode(out_msg->options, dispenser->read_ptr,
                                    dispenser->bytes_left, &bytes_read);

    dispenser->read_ptr += bytes_read;
    dispenser->bytes_left -= bytes_read;

    return res;
}

int _fluf_coap_udp_msg_decode(fluf_coap_udp_msg_t *out_msg,
                              void *packet,
                              size_t packet_size) {
    bytes_dispenser_t dispenser = {
        .read_ptr = (uint8_t *) packet,
        .bytes_left = packet_size
    };

    int res;
    res = decode_header(&out_msg->header, &dispenser);
    if (res) {
        return res;
    }
    res = decode_token(out_msg, &dispenser);
    if (res) {
        return res;
    }
    res = decode_options(out_msg, &dispenser);
    if (res) {
        return res;
    }
    res = decode_payload(&out_msg->payload, &out_msg->payload_size, &dispenser);

    return res;
}

int _fluf_coap_udp_header_serialize(fluf_coap_udp_msg_t *msg,
                                    uint8_t *buf,
                                    size_t buf_size) {
    assert(msg);
    assert(buf);

    assert(_fluf_coap_udp_header_get_token_length(&msg->header)
           == msg->token.size);

    bytes_appender_t appender = {
        .write_ptr = buf,
        .bytes_left = buf_size
    };

    if (bytes_append(&appender, &msg->header, sizeof(msg->header))
            || bytes_append(&appender, msg->token.bytes, msg->token.size)) {
        return FLUF_ERR_BUFF;
    }
    msg->occupied_buff_size = buf_size - appender.bytes_left;

    // prepare options buffer
    if (msg->options) {
        msg->options->buff_begin = buf + msg->occupied_buff_size;
        msg->options->buff_size = appender.bytes_left;
    }

    return 0;
}

int _fluf_coap_udp_msg_serialize(fluf_coap_udp_msg_t *msg,
                                 uint8_t *buf,
                                 size_t buf_size,
                                 size_t *out_bytes_written) {
    assert(msg);
    assert(buf);
    assert(out_bytes_written);
    assert(((int) buf_size - (int) msg->occupied_buff_size) >= 0);

    bytes_appender_t appender;

    if (msg->options && msg->options->options_number) {
        fluf_coap_option_t last_option =
                msg->options->options[msg->options->options_number - 1];
        msg->occupied_buff_size +=
                (size_t) ((last_option.payload + last_option.payload_len)
                          - msg->options->buff_begin);
    }

    appender.write_ptr = buf + msg->occupied_buff_size;
    appender.bytes_left = buf_size - msg->occupied_buff_size;

    if (msg->payload && msg->payload_size > 0) {
        if (bytes_append(&appender, &_FLUF_COAP_PAYLOAD_MARKER,
                         sizeof(_FLUF_COAP_PAYLOAD_MARKER))
                || bytes_append(&appender, msg->payload, msg->payload_size)) {
            return FLUF_ERR_BUFF;
        }
    }

    *out_bytes_written = buf_size - appender.bytes_left;

    return 0;
}
