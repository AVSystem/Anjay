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

const anjay_coap_msg_t _ANJAY_COAP_EMPTY_MSG_TEMPLATE = {
    .length = sizeof(anjay_coap_msg_header_t)
};

void
_anjay_coap_msg_info_reset(anjay_coap_msg_info_t *info) {
    AVS_LIST_CLEAR(&info->options_);

    *info = _anjay_coap_msg_info_init();
}

static size_t
get_options_size_bytes(const AVS_LIST(anjay_coap_msg_info_opt_t) opts) {
    size_t size = 0;
    uint16_t prev_opt_num = 0;

    const anjay_coap_msg_info_opt_t *opt;
    AVS_LIST_FOREACH(opt, opts) {
        assert(opt->number >= prev_opt_num);

        uint16_t delta = (uint16_t)(opt->number - prev_opt_num);
        size += _anjay_coap_get_opt_header_size(delta, opt->data_size)
                + opt->data_size;
        prev_opt_num = opt->number;
    }

    return size;
}

size_t
_anjay_coap_msg_info_get_headers_size(const anjay_coap_msg_info_t *info) {
    return sizeof(anjay_coap_msg_header_t)
           + info->identity.token_size
           + get_options_size_bytes(info->options_);
}

size_t
_anjay_coap_msg_info_get_storage_size(const anjay_coap_msg_info_t *info) {
    return sizeof(anjay_coap_msg_t)
           + ANJAY_COAP_MAX_TOKEN_LENGTH
           + get_options_size_bytes(info->options_);
}

size_t
_anjay_coap_msg_info_get_packet_storage_size(const anjay_coap_msg_info_t *info,
                                             size_t payload_size) {
    return _anjay_coap_msg_info_get_storage_size(info)
           + (payload_size ? sizeof(ANJAY_COAP_PAYLOAD_MARKER) + payload_size
                           : 0);
}

void _anjay_coap_msg_info_opt_remove_by_number(anjay_coap_msg_info_t *info,
                                               uint16_t option_number) {
    anjay_coap_msg_info_opt_t **opt;
    anjay_coap_msg_info_opt_t *helper;
    AVS_LIST_DELETABLE_FOREACH_PTR(opt, helper, &info->options_) {
        if ((*opt)->number == option_number) {
            AVS_LIST_DELETE(opt);
        } else if ((*opt)->number > option_number) {
            return;
        }
    }
}

int _anjay_coap_msg_info_opt_content_format(anjay_coap_msg_info_t *info,
                                            uint16_t format) {
    if (format == ANJAY_COAP_FORMAT_NONE) {
        return 0;
    }

    return _anjay_coap_msg_info_opt_u16(info, ANJAY_COAP_OPT_CONTENT_FORMAT,
                                        format);
}

static int encode_block_size(uint16_t size,
                             uint8_t *out_size_exponent) {
    switch (size) {
    case 16:   *out_size_exponent = 0; break;
    case 32:   *out_size_exponent = 1; break;
    case 64:   *out_size_exponent = 2; break;
    case 128:  *out_size_exponent = 3; break;
    case 256:  *out_size_exponent = 4; break;
    case 512:  *out_size_exponent = 5; break;
    case 1024: *out_size_exponent = 6; break;
    default:
       coap_log(ERROR, "invalid block size: %d, expected power of 2 between 16 "
                "and 1024 (inclusive)", (int)size);
       return -1;
    }

    return 0;
}

static int add_block_opt(anjay_coap_msg_info_t *info,
                         uint16_t option_number,
                         uint32_t seq_number,
                         bool is_last_chunk,
                         uint16_t size) {
    uint8_t size_exponent;
    if (encode_block_size(size, &size_exponent)) {
        return -1;
    }

    AVS_STATIC_ASSERT(sizeof(int) >= sizeof(int32_t), int_type_too_small);
    if (seq_number >= (1 << 20)) {
        coap_log(ERROR, "block sequence number must be less than 2^20");
        return -1;
    }

    uint32_t value = ((seq_number & 0x000fffff) << 4)
                   | ((uint32_t)is_last_chunk << 3)
                   | (uint32_t)size_exponent;
    return _anjay_coap_msg_info_opt_u32(info, option_number, value);
}

int _anjay_coap_msg_info_opt_block(anjay_coap_msg_info_t *info,
                                   const coap_block_info_t *block) {
    if (!block->valid) {
        coap_log(ERROR, "could not add invalid BLOCK option");
        return -1;
    }

    return add_block_opt(info, _anjay_coap_opt_num_from_block_type(block->type),
                         block->seq_num, block->has_more, block->size);
}

int _anjay_coap_msg_info_opt_opaque(anjay_coap_msg_info_t *info,
                                    uint16_t opt_number,
                                    const void *opt_data,
                                    uint16_t opt_data_size) {
    anjay_coap_msg_info_opt_t *opt = (anjay_coap_msg_info_opt_t*)
            AVS_LIST_NEW_BUFFER(sizeof(*opt) + opt_data_size);
    if (!opt) {
        coap_log(ERROR, "out of memory");
        return -1;
    }

    opt->number = opt_number;
    opt->data_size = opt_data_size;
    memcpy(opt->data, opt_data, opt_data_size);

    anjay_coap_msg_info_opt_t **insert_ptr = NULL;
    AVS_LIST_FOREACH_PTR(insert_ptr, &info->options_) {
        if ((*insert_ptr)->number > opt->number) {
            break;
        }
    }

    AVS_LIST_INSERT(insert_ptr, opt);
    return 0;
}

int _anjay_coap_msg_info_opt_string(anjay_coap_msg_info_t *info,
                                    uint16_t opt_number,
                                    const char *opt_data) {
    size_t size = strlen(opt_data);
    if (size > UINT16_MAX) {
        return -1;
    }

    return _anjay_coap_msg_info_opt_opaque(info, opt_number,
                                           opt_data, (uint16_t)size);
}

int _anjay_coap_msg_info_opt_empty(anjay_coap_msg_info_t *info,
                                   uint16_t opt_number) {
    return _anjay_coap_msg_info_opt_opaque(info, opt_number, "", 0);
}

#define htobe8(X) (X)

#define BUILDER_OPT_INT(Bits) \
    int _anjay_coap_msg_info_opt_u##Bits(anjay_coap_msg_info_t *info, \
                                         uint16_t opt_number, \
                                         uint##Bits##_t value) { \
        union { \
            uint##Bits##_t val_net; \
            char bytes[Bits / 8]; \
        } converted; \
        converted.val_net = htobe##Bits(value); \
        size_t start = 0; \
        while (start < sizeof(converted.bytes) && !converted.bytes[start]) { \
            ++start; \
        } \
        return _anjay_coap_msg_info_opt_opaque( \
                info, opt_number, &converted.bytes[start], \
                (uint16_t) (sizeof(converted.bytes) - start)); \
    }

BUILDER_OPT_INT(8)
BUILDER_OPT_INT(16)
BUILDER_OPT_INT(32)
BUILDER_OPT_INT(64)
