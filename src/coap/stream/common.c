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
#include <posix-config.h>

#define ANJAY_COAP_STREAM_INTERNALS

#include <anjay_modules/time.h>

#include <avsystem/commons/coap/msg_opt.h>

#include "common.h"
#include "../log.h"

VISIBILITY_SOURCE_BEGIN

static int add_string_options(avs_coap_msg_info_t *info,
                              uint16_t option_number,
                              AVS_LIST(const anjay_string_t) values) {
    AVS_LIST(const anjay_string_t) it;
    AVS_LIST_FOREACH(it, values) {
        if (avs_coap_msg_info_opt_string(info, option_number, it->c_str)) {
            return -1;
        }
    }

    return 0;
}

static int add_observe_option(avs_coap_msg_info_t *info,
                              bool observe) {
    if (observe) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        // A nearly-linear, strictly monotonic timestamp with a precision of
        // 32.768 us, wrapping every 512 seconds.
        // Should satisfy the requirements given in OBSERVE 3.4 and 4.4
        uint32_t value = (uint32_t) ((now.tv_sec & 0x1FF) << 15)
                | (uint32_t) (now.tv_nsec >> 15);
        return avs_coap_msg_info_opt_u32(info, AVS_COAP_OPT_OBSERVE, value);
    } else {
        return 0;
    }
}

int _anjay_coap_common_fill_msg_info(avs_coap_msg_info_t *info,
                                     const anjay_msg_details_t *details,
                                     const avs_coap_msg_identity_t *identity,
                                     const avs_coap_block_info_t *block_info) {
    assert(details);
    assert(identity);

    avs_coap_msg_info_reset(info);

    info->type = details->msg_type;
    info->code = details->msg_code;
    info->identity = *identity;
    if (add_observe_option(info, details->observe_serial)
            || add_string_options(info, AVS_COAP_OPT_LOCATION_PATH,
                                  details->location_path)
            || add_string_options(info, AVS_COAP_OPT_URI_PATH,
                                  details->uri_path)
            || avs_coap_msg_info_opt_content_format(info, details->format)
            || add_string_options(info, AVS_COAP_OPT_URI_QUERY,
                                  details->uri_query)) {
        return -1;
    }

    if (!block_info || !block_info->valid) {
        return 0;
    }

    return avs_coap_msg_info_opt_block(info, block_info);
}

static void set_socket_timeout(avs_net_abstract_socket_t *socket,
                               avs_net_timeout_t timeout_ms) {
    if (avs_net_socket_set_opt(socket, AVS_NET_SOCKET_OPT_RECV_TIMEOUT,
                               (avs_net_socket_opt_value_t) {
                                   .recv_timeout = timeout_ms
                               })) {
        assert(0 && "could not set socket recv timeout");
        coap_log(ERROR, "could not set socket recv timeout");
    }
}

int _anjay_coap_common_recv_msg_with_timeout(avs_coap_ctx_t *ctx,
                                             avs_net_abstract_socket_t *socket,
                                             coap_input_buffer_t *in,
                                             int32_t *inout_timeout_ms,
                                             recv_msg_handler_t *handle_msg,
                                             void *handle_msg_data,
                                             int *out_handler_result) {
    avs_net_socket_opt_value_t original_recv_timeout;
    if (avs_net_socket_get_opt(socket, AVS_NET_SOCKET_OPT_RECV_TIMEOUT,
                               &original_recv_timeout)) {
        assert(0 && "could not get socket recv timeout");
        coap_log(ERROR, "could not get socket recv timeout");
        return -1;
    }

    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    const int32_t initial_timeout_ms = *inout_timeout_ms;
    int result = 0;

    while (*inout_timeout_ms > 0) {
        AVS_STATIC_ASSERT(sizeof(avs_net_timeout_t) >= sizeof(int32_t),
                          timeout_limit_too_small_to_be_coap_compliant_t);

        set_socket_timeout(socket, *inout_timeout_ms);

        result = _anjay_coap_in_get_next_message(in, ctx, socket);
        switch (result) {
        case AVS_COAP_CTX_ERR_TIMEOUT:
            *inout_timeout_ms = 0;
            // fall-through
        default:
            goto exit;

        case AVS_COAP_CTX_ERR_MSG_MALFORMED:
        case AVS_COAP_CTX_ERR_DUPLICATE:
        case AVS_COAP_CTX_ERR_MSG_WAS_PING:
        case 0:
            break;
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        ssize_t time_elapsed_ms;
        if (avs_time_diff_ms(&time_elapsed_ms, &now, &start_time)) {
            time_elapsed_ms = 0;
        }
        *inout_timeout_ms = (int32_t)(initial_timeout_ms - time_elapsed_ms);

        if (!result) {
            const avs_coap_msg_t *msg = _anjay_coap_in_get_message(in);
            bool wait_for_next = true;
            uint8_t error_code = 0;

            *out_handler_result = handle_msg(msg, handle_msg_data,
                                             &wait_for_next, &error_code);
            if (!wait_for_next) {
                result = -error_code;
                goto exit;
            }

            if (!error_code) {
                if (avs_coap_msg_get_type(msg) == AVS_COAP_MSG_CONFIRMABLE) {
                    avs_coap_ctx_send_empty(ctx, socket, AVS_COAP_MSG_RESET,
                                            avs_coap_msg_get_id(msg));
                }
            } else if (error_code == AVS_COAP_CODE_SERVICE_UNAVAILABLE) {
                avs_coap_ctx_send_service_unavailable(ctx, socket, msg,
                                                      *inout_timeout_ms);
            } else {
                avs_coap_ctx_send_error(ctx, socket, msg, error_code);
            }
        }
    }

    *inout_timeout_ms = 0;
    result = AVS_COAP_CTX_ERR_TIMEOUT;

exit:
    set_socket_timeout(socket, original_recv_timeout.recv_timeout);

    assert(result <= 0);
    return result;
}
