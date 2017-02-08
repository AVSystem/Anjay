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

#define ANJAY_COAP_STREAM_INTERNALS

#include "out.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "../msg_builder.h"
#include "../log.h"

VISIBILITY_SOURCE_BEGIN

coap_output_buffer_t _anjay_coap_out_init(uint8_t *buffer,
                                          size_t buffer_capacity) {
    return (coap_output_buffer_t){
        .buffer = buffer,
        .buffer_capacity = buffer_capacity,
        .dgram_layer_mtu = buffer_capacity,
        .info = _anjay_coap_msg_info_init(),
        .builder = ANJAY_COAP_MSG_BUILDER_UNINITIALIZED
    };
}

void _anjay_coap_out_reset(coap_output_buffer_t *out) {
    out->dgram_layer_mtu = out->buffer_capacity;
    _anjay_coap_msg_info_reset(&out->info);
    out->builder = ANJAY_COAP_MSG_BUILDER_UNINITIALIZED;
}

void _anjay_coap_out_setup_mtu(coap_output_buffer_t *out,
                               anjay_coap_socket_t *socket) {
    avs_net_abstract_socket_t *backend_socket =
            socket ? _anjay_coap_socket_get_backend(socket) : NULL;
    avs_net_socket_opt_value_t opt_value;
    if (backend_socket && !avs_net_socket_get_opt(backend_socket,
                                                  AVS_NET_SOCKET_OPT_INNER_MTU,
                                                  &opt_value)) {
        coap_log(INFO, "Buffer size: %u; socket MTU: %d",
                 (unsigned) out->buffer_capacity, opt_value.mtu);
        if (opt_value.mtu > 0) {
            out->dgram_layer_mtu = (size_t) opt_value.mtu;
        }
    } else {
        coap_log(INFO, "Buffer size: %u; socket MTU unknown",
                 (unsigned) out->buffer_capacity);
    }
}

static int add_string_options(anjay_coap_msg_info_t *info,
                              uint16_t option_number,
                              AVS_LIST(const anjay_string_t) values) {
    AVS_LIST(const anjay_string_t) it;
    AVS_LIST_FOREACH(it, values) {
        if (_anjay_coap_msg_info_opt_string(info, option_number, it->c_str)) {
            return -1;
        }
    }

    return 0;
}

static int add_observe_option(anjay_coap_msg_info_t *info) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    // A nearly-linear, strictly monotonic timestamp with a precision of
    // 32.768 us, wrapping every 512 seconds.
    // Should satisfy the requirements given in OBSERVE 3.4 and 4.4
    uint32_t value = (uint32_t) ((now.tv_sec & 0x1FF) << 15)
            | (uint32_t) (now.tv_nsec >> 15);
    return _anjay_coap_msg_info_opt_u32(info, ANJAY_COAP_OPT_OBSERVE,
                                        value);
}

static size_t effective_buffer_capacity(const coap_output_buffer_t *out) {
    return ANJAY_MIN(out->buffer_capacity, out->dgram_layer_mtu);
}

int _anjay_coap_out_setup_msg(coap_output_buffer_t *out,
                              const anjay_coap_msg_identity_t *id,
                              const anjay_msg_details_t *details,
                              const coap_block_info_t *block) {
    assert(_anjay_coap_out_is_reset(out));

    out->info.type = details->msg_type;
    out->info.code = details->msg_code;
    out->info.identity = *id;

    if ((details->observe_serial && add_observe_option(&out->info))
            || add_string_options(&out->info, ANJAY_COAP_OPT_LOCATION_PATH,
                                  details->location_path)
            || add_string_options(&out->info, ANJAY_COAP_OPT_URI_PATH,
                                  details->uri_path)
            || _anjay_coap_msg_info_opt_content_format(&out->info,
                                                       details->format)
            || add_string_options(&out->info, ANJAY_COAP_OPT_URI_QUERY,
                                  details->uri_query)
            || (block && _anjay_coap_msg_info_opt_block(&out->info, block))) {
        return -1;
    }

    return _anjay_coap_msg_builder_init(
            &out->builder, _anjay_coap_ensure_aligned_buffer(out->buffer),
            effective_buffer_capacity(out), &out->info);
}

int _anjay_coap_out_update_msg_header(coap_output_buffer_t *out,
                                      const anjay_coap_msg_identity_t *id,
                                      const coap_block_info_t *block) {
    assert(id->token_size <= 8);

    if (_anjay_coap_msg_builder_has_payload(&out->builder)) {
        coap_log(ERROR,
                 "header override not supported on messages with payload");
        return -1;
    }

    out->info.identity = *id;
    uint16_t option_num = _anjay_coap_opt_num_from_block_type(block->type);
    _anjay_coap_msg_info_opt_remove_by_number(&out->info, option_num);
    int result = _anjay_coap_msg_info_opt_block(&out->info, block);

    if (!result) {
        result = _anjay_coap_msg_builder_reset(&out->builder, &out->info);
    }
    return result;
}

size_t _anjay_coap_out_write(coap_output_buffer_t *out,
                             const void *data,
                             size_t data_length) {
    return _anjay_coap_msg_builder_payload(&out->builder, data, data_length);
}
