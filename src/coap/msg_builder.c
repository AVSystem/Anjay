/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <config.h>

#include <endian.h>

#include "log.h"
#include "msg_builder.h"
#include "msg_internal.h"
#include "../utils.h"

VISIBILITY_SOURCE_BEGIN

#define builder_log(...) _anjay_log(coap_builder, __VA_ARGS__)

static void append_header(anjay_coap_msg_buffer_t *buffer,
                          anjay_coap_msg_type_t msg_type,
                          uint8_t msg_code,
                          uint16_t msg_id) {
    _anjay_coap_msg_header_set_type(&buffer->msg->header, msg_type);
    _anjay_coap_msg_header_set_version(&buffer->msg->header, 1);
    _anjay_coap_msg_header_set_token_length(&buffer->msg->header, 0);

    buffer->msg->header.code = msg_code;
    memcpy(buffer->msg->header.message_id, &(uint16_t){htons(msg_id)},
           sizeof(buffer->msg->header.message_id));
}

static uint8_t *msg_end_ptr(const anjay_coap_msg_buffer_t *buffer) {
    return &buffer->msg->content[buffer->msg->length
                                  - sizeof(buffer->msg->header)];
}

static size_t bytes_remaining(const anjay_coap_msg_buffer_t *buffer) {
    return buffer->capacity
           - sizeof(buffer->msg->length) - buffer->msg->length;
}

static int append_data(anjay_coap_msg_buffer_t *buffer,
                       const void *data,
                       size_t data_size) {
    if (data_size > bytes_remaining(buffer)) {
        builder_log(ERROR, "cannot append %u bytes, only %u available",
                    (unsigned)data_size, (unsigned)bytes_remaining(buffer));
        return -1;
    }

    memcpy(msg_end_ptr(buffer), data, data_size);
    buffer->msg->length += (uint32_t)data_size;
    return 0;
}

static int append_byte(anjay_coap_msg_buffer_t *buffer,
                       uint8_t value) {
    return append_data(buffer, &value, sizeof(value));
}

static int append_token(anjay_coap_msg_buffer_t *buffer,
                         const anjay_coap_token_t *token,
                         size_t token_length) {
    assert(token_length <= ANJAY_COAP_MAX_TOKEN_LENGTH);

    if (buffer->msg->header.code == ANJAY_COAP_CODE_EMPTY
            && token_length > 0) {
        builder_log(ERROR, "0.00 Empty message must not contain a token");
        return -1;
    }

    _anjay_coap_msg_header_set_token_length(&buffer->msg->header,
                                            (uint8_t)token_length);
    if (append_data(buffer, token, token_length)) {
        builder_log(ERROR, "could not append token");
        return -1;
    }

    return 0;
}

static inline size_t encode_ext_value(uint8_t *ptr,
                                      uint16_t ext_value) {
    if (ext_value >= ANJAY_COAP_EXT_U16_BASE) {
        uint16_t value_net_byte_order =
            htons((uint16_t)(ext_value - ANJAY_COAP_EXT_U16_BASE));

        memcpy(ptr, &value_net_byte_order, sizeof(value_net_byte_order));
        return sizeof(value_net_byte_order);
    } else if (ext_value >= ANJAY_COAP_EXT_U8_BASE) {
        *ptr = (uint8_t)(ext_value - ANJAY_COAP_EXT_U8_BASE);
        return 1;
    }

    return 0;
}

static inline size_t opt_write_header(uint8_t *ptr,
                                      uint16_t opt_number_delta,
                                      uint16_t opt_length) {
    anjay_coap_opt_t *opt = (anjay_coap_opt_t *)ptr;
    ptr = opt->content;

    if (opt_number_delta >= ANJAY_COAP_EXT_U16_BASE) {
        _anjay_coap_opt_set_short_delta(opt, ANJAY_COAP_EXT_U16);
    } else if (opt_number_delta >= ANJAY_COAP_EXT_U8_BASE) {
        _anjay_coap_opt_set_short_delta(opt, ANJAY_COAP_EXT_U8);
    } else {
        _anjay_coap_opt_set_short_delta(opt, (uint8_t)(opt_number_delta & 0xF));
    }

    if (opt_length >= ANJAY_COAP_EXT_U16_BASE) {
        _anjay_coap_opt_set_short_length(opt, ANJAY_COAP_EXT_U16);
    } else if (opt_length >= ANJAY_COAP_EXT_U8_BASE) {
        _anjay_coap_opt_set_short_length(opt, ANJAY_COAP_EXT_U8);
    } else {
        _anjay_coap_opt_set_short_length(opt, (uint8_t)(opt_length & 0xF));
    }

    ptr += encode_ext_value(ptr, opt_number_delta);
    ptr += encode_ext_value(ptr, opt_length);

    return (size_t)(ptr - (uint8_t *)opt);
}


static int append_option(anjay_coap_msg_buffer_t *buffer,
                         uint16_t opt_number_delta,
                         const void *opt_data,
                         uint16_t opt_data_size) {
    if (buffer->msg->header.code == ANJAY_COAP_CODE_EMPTY) {
        builder_log(ERROR, "0.00 Empty message must not contain options");
        return -1;
    }

    size_t header_size = _anjay_coap_get_opt_header_size(opt_number_delta,
                                                         opt_data_size);

    if (header_size + opt_data_size > bytes_remaining(buffer)) {
        builder_log(ERROR, "not enough space to serialize option");
        return -1;
    }

    buffer->msg->length += (uint32_t)opt_write_header(msg_end_ptr(buffer),
                                                      opt_number_delta,
                                                      opt_data_size);

    if (append_data(buffer, opt_data, opt_data_size)) {
        builder_log(ERROR, "could not serialize option");
        return -1;
    }

    return 0;
}

int _anjay_coap_msg_builder_init(anjay_coap_msg_builder_t *builder,
                                 anjay_coap_aligned_msg_buffer_t *buffer,
                                 size_t buffer_size_bytes,
                                 const anjay_coap_msg_info_t *header) {
    *builder = (anjay_coap_msg_builder_t){
        .has_payload_marker = false,
        .msg_buffer = {
            .msg = (anjay_coap_msg_t *)buffer,
            .capacity = buffer_size_bytes
        }
    };

    return _anjay_coap_msg_builder_reset(builder, header);
}

int
_anjay_coap_msg_builder_reset(anjay_coap_msg_builder_t *builder,
                              const anjay_coap_msg_info_t *header) {
    if (builder->msg_buffer.capacity
            < _anjay_coap_msg_info_get_headers_size(header)) {
        coap_log(ERROR, "message buffer too small: %u/%u B available",
                 (unsigned)builder->msg_buffer.capacity,
                 (unsigned)_anjay_coap_msg_info_get_storage_size(header));
        return -1;
    }

    builder->has_payload_marker = false;
    builder->msg_buffer.msg->length = sizeof(builder->msg_buffer.msg->header);

    append_header(&builder->msg_buffer,
                  header->type, header->code, header->identity.msg_id);
    if (append_token(&builder->msg_buffer,
                     &header->identity.token, header->identity.token_size)) {
        return -1;
    }

    anjay_coap_msg_info_opt_t *opt;
    uint16_t prev_opt_num = 0;
    AVS_LIST_FOREACH(opt, header->options_) {
        assert(prev_opt_num <= opt->number);

        uint16_t delta = (uint16_t)(opt->number - prev_opt_num);
        if (append_option(&builder->msg_buffer,
                          delta, opt->data, opt->data_size)) {
            return -1;
        }
        prev_opt_num = opt->number;
    }

    return 0;
}

size_t _anjay_coap_msg_builder_payload_remaining(
        const anjay_coap_msg_builder_t *builder) {
    size_t total_bytes_remaining = bytes_remaining(&builder->msg_buffer);
    if (total_bytes_remaining && !builder->has_payload_marker) {
        return --total_bytes_remaining;
    }
    return total_bytes_remaining;
}

size_t _anjay_coap_msg_builder_payload(anjay_coap_msg_builder_t *builder,
                                       const void *payload,
                                       size_t payload_size) {
    assert(_anjay_coap_msg_builder_is_initialized(builder)
           && "_anjay_coap_msg_builder_payload called on uninitialized builder");

    if (payload_size == 0) {
        return 0;
    }

    int result;
    size_t bytes_to_write = 0;
    {
        size_t msg_builder_payload_remaining =
                _anjay_coap_msg_builder_payload_remaining(builder);
        bytes_to_write = ANJAY_MIN(payload_size, msg_builder_payload_remaining);
    }
    if (!builder->has_payload_marker && bytes_to_write) {
        result = append_byte(&builder->msg_buffer, ANJAY_COAP_PAYLOAD_MARKER);
        assert(!result && "attempted to write an invalid amount of bytes");

        builder->has_payload_marker = true;
    }

    result = append_data(&builder->msg_buffer, payload, bytes_to_write);
    assert(!result && "attempted to write an invalid amount of bytes");
    (void)result;

    return bytes_to_write;
}

const anjay_coap_msg_t *
_anjay_coap_msg_builder_get_msg(const anjay_coap_msg_builder_t *builder) {
    return builder->msg_buffer.msg;
}

#ifdef ANJAY_TEST
#include "test/msg_builder.c"
#endif // ANJAY_TEST
