/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avs_coap_init.h>

#ifdef WITH_AVS_COAP_TCP

#    include <avsystem/commons/avs_errno.h>

#    include "options/avs_coap_option.h"

#    define MODULE_NAME coap_tcp
#    include <avs_coap_x_log_config.h>

#    include "avs_coap_tcp_header.h"

VISIBILITY_SOURCE_BEGIN

// As defined in RFC8323
#    define MIN_8BIT_EXT_LEN 13U
#    define MIN_16BIT_EXT_LEN 269U
#    define MIN_32BIT_EXT_LEN 65805U

#    define HEADER_LEN_MASK 0xF0
#    define HEADER_LEN_SHIFT 4
#    define HEADER_TKL_MASK 0x0F
#    define HEADER_TKL_SHIFT 0

#    define LEN_TKL_OFFSET 0
#    define EXT_LEN_OFFSET 1

#    define EXTENDED_LENGTH_UINT8 13
#    define EXTENDED_LENGTH_UINT16 14
#    define EXTENDED_LENGTH_UINT32 15

avs_coap_tcp_header_t _avs_coap_tcp_header_init(size_t payload_size,
                                                size_t options_size,
                                                uint8_t token_size,
                                                uint8_t code) {
    assert(token_size <= 8);
    assert((payload_size ? sizeof(AVS_COAP_PAYLOAD_MARKER) : 0) + payload_size
                   + options_size
           <= (uint64_t) UINT32_MAX + MIN_32BIT_EXT_LEN);
    avs_coap_tcp_header_t header = {
        // Length of the message in bytes, including options field, payload
        // marker and payload data, as defined in RFC8323.
        .opts_and_payload_len =
                (payload_size ? sizeof(AVS_COAP_PAYLOAD_MARKER) : 0)
                + payload_size + options_size,
        .token_len = token_size,
        .code = code
    };
    return header;
}

static void set_len_field(uint8_t *len_tkl, size_t value) {
    AVS_ASSERT(value < 16, "len field can't be set to value bigger than 15");
    _AVS_FIELD_SET(*len_tkl, HEADER_LEN_MASK, HEADER_LEN_SHIFT, value);
}

size_t _avs_coap_tcp_header_serialize(const avs_coap_tcp_header_t *header,
                                      uint8_t *buf,
                                      size_t buf_size) {
    assert(buf_size >= _AVS_COAP_TCP_MAX_HEADER_LENGTH);
    (void) buf_size;

    uint8_t len_tkl = 0;
    _AVS_FIELD_SET(len_tkl, HEADER_TKL_MASK, HEADER_TKL_SHIFT,
                   header->token_len);

    size_t code_offset = EXT_LEN_OFFSET;

    if (header->opts_and_payload_len < MIN_8BIT_EXT_LEN) {
        set_len_field(&len_tkl, header->opts_and_payload_len);
    } else if (header->opts_and_payload_len < MIN_16BIT_EXT_LEN) {
        set_len_field(&len_tkl, EXTENDED_LENGTH_UINT8);
        uint8_t ext_len =
                (uint8_t) (header->opts_and_payload_len - MIN_8BIT_EXT_LEN);
        buf[EXT_LEN_OFFSET] = ext_len;
        code_offset += sizeof(ext_len);
    } else if (header->opts_and_payload_len < MIN_32BIT_EXT_LEN) {
        set_len_field(&len_tkl, EXTENDED_LENGTH_UINT16);
        uint16_t ext_len = avs_convert_be16(
                (uint16_t) (header->opts_and_payload_len - MIN_16BIT_EXT_LEN));
        memcpy(buf + EXT_LEN_OFFSET, &ext_len, sizeof(ext_len));
        code_offset += sizeof(ext_len);
    } else {
        set_len_field(&len_tkl, EXTENDED_LENGTH_UINT32);
        uint32_t ext_len = avs_convert_be32(
                (uint32_t) (header->opts_and_payload_len - MIN_32BIT_EXT_LEN));
        memcpy(buf + EXT_LEN_OFFSET, &ext_len, sizeof(ext_len));
        code_offset += sizeof(ext_len);
    }

    buf[LEN_TKL_OFFSET] = len_tkl;
    buf[code_offset] = header->code;

    return code_offset + 1;
}

/**
 * Returns length of extended length field and code.
 */
static inline size_t remaining_header_bytes(uint8_t len_value) {
    size_t ext_len_size = 0;
    switch (len_value) {
    case EXTENDED_LENGTH_UINT8:
        ext_len_size = sizeof(uint8_t);
        break;
    case EXTENDED_LENGTH_UINT16:
        ext_len_size = sizeof(uint16_t);
        break;
    case EXTENDED_LENGTH_UINT32:
        ext_len_size = sizeof(uint32_t);
        break;
    default:
        break;
    }
    return ext_len_size + sizeof(uint8_t); // add length of code
}

avs_error_t _avs_coap_tcp_header_parse(avs_coap_tcp_header_t *header,
                                       bytes_dispenser_t *dispenser,
                                       size_t *out_header_bytes_missing) {
    uint8_t len_tkl;
    if (dispenser->bytes_left < sizeof(len_tkl)) {
        *out_header_bytes_missing = sizeof(len_tkl);
        return _avs_coap_err(AVS_COAP_ERR_MORE_DATA_REQUIRED);
    }

    (void) _avs_coap_bytes_extract(dispenser, &len_tkl, sizeof(len_tkl));

    uint8_t short_len =
            _AVS_FIELD_GET(len_tkl, HEADER_LEN_MASK, HEADER_LEN_SHIFT);
    assert(short_len < 16);

    size_t remaining_bytes = remaining_header_bytes(short_len);
    if (remaining_bytes > dispenser->bytes_left) {
        *out_header_bytes_missing = remaining_bytes - dispenser->bytes_left;
        return _avs_coap_err(AVS_COAP_ERR_MORE_DATA_REQUIRED);
    }

    header->token_len =
            _AVS_FIELD_GET(len_tkl, HEADER_TKL_MASK, HEADER_TKL_SHIFT);
    if (header->token_len > AVS_COAP_MAX_TOKEN_LENGTH) {
        LOG(DEBUG, _("invalid token longer than ") "%u" _(" bytes"),
            (unsigned) AVS_COAP_MAX_TOKEN_LENGTH);
        return _avs_coap_err(AVS_COAP_ERR_MALFORMED_MESSAGE);
    }

    if (short_len < MIN_8BIT_EXT_LEN) {
        header->opts_and_payload_len = short_len;
    } else if (short_len == EXTENDED_LENGTH_UINT8) {
        uint8_t ext_len;
        (void) _avs_coap_bytes_extract(dispenser, &ext_len, sizeof(ext_len));
        header->opts_and_payload_len = (uint64_t) ext_len + MIN_8BIT_EXT_LEN;
    } else if (short_len == EXTENDED_LENGTH_UINT16) {
        uint16_t ext_len;
        (void) _avs_coap_bytes_extract(dispenser, &ext_len, sizeof(ext_len));
        header->opts_and_payload_len =
                (uint64_t) avs_convert_be16(ext_len) + MIN_16BIT_EXT_LEN;
    } else {
        uint32_t ext_len;
        (void) _avs_coap_bytes_extract(dispenser, &ext_len, sizeof(ext_len));
        header->opts_and_payload_len =
                (uint64_t) avs_convert_be32(ext_len) + MIN_32BIT_EXT_LEN;
    }

    *out_header_bytes_missing = 0;
    (void) _avs_coap_bytes_extract(dispenser, &header->code,
                                   sizeof(header->code));
    return AVS_OK;
}

#endif // WITH_AVS_COAP_TCP
