/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

#include <anjay_config.h>

#define ANJAY_COAP_STREAM_INTERNALS

#include <anjay_modules/time_defs.h>

#include <avsystem/commons/coap/msg_opt.h>

#include "../coap_log.h"
#include "common.h"

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

uint32_t _anjay_coap_common_timestamp(void) {
    avs_time_monotonic_t now = avs_time_monotonic_now();
    // A nearly-linear, strictly monotonic timestamp with a precision of
    // 32.768 us, wrapping every 512 seconds.
    // Should satisfy the requirements given in OBSERVE 3.4 and 4.4
    return (uint32_t) ((now.since_monotonic_epoch.seconds & 0x1FF) << 15)
           | (uint32_t) (now.since_monotonic_epoch.nanoseconds >> 15);
}

static int add_observe_option(avs_coap_msg_info_t *info, bool observe) {
    if (observe) {
        return avs_coap_msg_info_opt_u32(info, AVS_COAP_OPT_OBSERVE,
                                         _anjay_coap_common_timestamp());
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
                               avs_time_duration_t timeout) {
    if (avs_net_socket_set_opt(socket, AVS_NET_SOCKET_OPT_RECV_TIMEOUT,
                               (avs_net_socket_opt_value_t) {
                                   .recv_timeout = timeout
                               })) {
        AVS_UNREACHABLE("could not set socket recv timeout");
        coap_log(ERROR, "could not set socket recv timeout");
    }
}

int _anjay_coap_common_recv_msg_with_timeout(avs_coap_ctx_t *ctx,
                                             avs_net_abstract_socket_t *socket,
                                             coap_input_buffer_t *in,
                                             avs_time_duration_t *inout_timeout,
                                             recv_msg_handler_t *handle_msg,
                                             void *handle_msg_data,
                                             int *out_handler_result) {
    avs_net_socket_opt_value_t original_recv_timeout;
    if (avs_net_socket_get_opt(socket, AVS_NET_SOCKET_OPT_RECV_TIMEOUT,
                               &original_recv_timeout)) {
        AVS_UNREACHABLE("could not get socket recv timeout");
        coap_log(ERROR, "could not get socket recv timeout");
        return -1;
    }

    avs_time_monotonic_t start_time = avs_time_monotonic_now();

    const avs_time_duration_t initial_timeout = *inout_timeout;
    int result = 0;

    while (avs_time_duration_less(AVS_TIME_DURATION_ZERO, *inout_timeout)) {
        set_socket_timeout(socket, *inout_timeout);

        result = _anjay_coap_in_get_next_message(in, ctx, socket);
        switch (result) {
        case AVS_COAP_CTX_ERR_TIMEOUT:
            *inout_timeout = AVS_TIME_DURATION_ZERO;
            // fall-through
        default:
            goto exit;

        case AVS_COAP_CTX_ERR_MSG_MALFORMED:
        case AVS_COAP_CTX_ERR_DUPLICATE:
        case AVS_COAP_CTX_ERR_MSG_WAS_PING:
        case 0:
            break;
        }

        avs_time_duration_t time_elapsed =
                avs_time_monotonic_diff(avs_time_monotonic_now(), start_time);
        *inout_timeout = avs_time_duration_diff(initial_timeout, time_elapsed);

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
                                                      *inout_timeout);
            } else {
                avs_coap_ctx_send_error(ctx, socket, msg, error_code);
            }
        }
    }

    *inout_timeout = AVS_TIME_DURATION_ZERO;
    result = AVS_COAP_CTX_ERR_TIMEOUT;

exit:
    set_socket_timeout(socket, original_recv_timeout.recv_timeout);

    assert(result <= 0);
    return result;
}
