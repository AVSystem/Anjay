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

#include <anjay_modules/time.h>

#include "common.h"
#include "../log.h"

VISIBILITY_SOURCE_BEGIN

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

static int add_observe_option(anjay_coap_msg_info_t *info,
                              bool observe) {
    if (observe) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        // A nearly-linear, strictly monotonic timestamp with a precision of
        // 32.768 us, wrapping every 512 seconds.
        // Should satisfy the requirements given in OBSERVE 3.4 and 4.4
        uint32_t value = (uint32_t) ((now.tv_sec & 0x1FF) << 15)
                | (uint32_t) (now.tv_nsec >> 15);
        return _anjay_coap_msg_info_opt_u32(info,
                                            ANJAY_COAP_OPT_OBSERVE, value);
    } else {
        return 0;
    }
}

int _anjay_coap_common_get_block_info(const anjay_coap_msg_t *msg,
                                      coap_block_type_t type,
                                      coap_block_info_t *out_info) {
    assert(msg);
    assert(out_info);
    uint16_t opt_number = type == COAP_BLOCK1
            ? ANJAY_COAP_OPT_BLOCK1
            : ANJAY_COAP_OPT_BLOCK2;
    const anjay_coap_opt_t *opt;
    memset(out_info, 0, sizeof(*out_info));
    if (_anjay_coap_msg_find_unique_opt(msg, opt_number, &opt)) {
        if (opt) {
            int num = opt_number == ANJAY_COAP_OPT_BLOCK1 ? 1 : 2;
            coap_log(ERROR, "multiple BLOCK%d options found", num);
            return -1;
        }
        return 0;
    }
    out_info->type = type;
    out_info->valid = !_anjay_coap_opt_block_seq_number(opt, &out_info->seq_num)
            && !_anjay_coap_opt_block_has_more(opt, &out_info->has_more)
            && !_anjay_coap_opt_block_size(opt, &out_info->size);

    return out_info->valid ? 0 : -1;
}

int _anjay_coap_common_fill_msg_info(anjay_coap_msg_info_t *info,
                                     const anjay_msg_details_t *details,
                                     const anjay_coap_msg_identity_t *identity,
                                     const coap_block_info_t *block_info) {
    assert(details);
    assert(identity);

    _anjay_coap_msg_info_reset(info);

    info->type = details->msg_type;
    info->code = details->msg_code;
    info->identity = *identity;
    if (add_observe_option(info, details->observe_serial)
            || add_string_options(info, ANJAY_COAP_OPT_LOCATION_PATH,
                                  details->location_path)
            || add_string_options(info, ANJAY_COAP_OPT_URI_PATH,
                                  details->uri_path)
            || _anjay_coap_msg_info_opt_content_format(info, details->format)
            || add_string_options(info, ANJAY_COAP_OPT_URI_QUERY,
                                  details->uri_query)) {
        return -1;
    }

    if (!block_info || !block_info->valid) {
        return 0;
    }

    return _anjay_coap_msg_info_opt_block(info, block_info);
}

bool _anjay_coap_common_token_matches(const anjay_coap_msg_t *msg,
                                      const anjay_coap_msg_identity_t *id) {
    anjay_coap_token_t msg_token;
    size_t msg_token_size = _anjay_coap_msg_get_token(msg, &msg_token);

    return _anjay_coap_common_tokens_equal(&msg_token, msg_token_size,
                                           &id->token, id->token_size);
}

int _anjay_coap_common_send_empty(anjay_coap_socket_t *socket,
                                  anjay_coap_msg_type_t msg_type,
                                  uint16_t msg_id) {
    anjay_coap_msg_info_t info = _anjay_coap_msg_info_init();

    info.type = msg_type;
    info.code = ANJAY_COAP_CODE_EMPTY;
    info.identity.msg_id = msg_id;

    union {
        uint8_t buffer[sizeof(anjay_coap_msg_t)];
        anjay_coap_msg_t force_align_;
    } aligned_buffer;
    const anjay_coap_msg_t *msg = _anjay_coap_msg_build_without_payload(
            _anjay_coap_ensure_aligned_buffer(&aligned_buffer),
            sizeof(aligned_buffer), &info);
    assert(msg);

    return _anjay_coap_socket_send(socket, msg);
}

void _anjay_coap_common_send_error(anjay_coap_socket_t *socket,
                                   const anjay_coap_msg_t *msg,
                                   uint8_t error_code) {
    anjay_coap_msg_info_t info = _anjay_coap_msg_info_init();

    info.type = ANJAY_COAP_MSG_ACKNOWLEDGEMENT;
    info.code = error_code;
    info.identity.msg_id = _anjay_coap_msg_get_id(msg);
    info.identity.token_size = _anjay_coap_msg_get_token(msg,
                                                         &info.identity.token);

    union {
        uint8_t buffer[sizeof(anjay_coap_msg_t) + ANJAY_COAP_MAX_TOKEN_LENGTH];
        anjay_coap_msg_t force_align_;
    } aligned_buffer;
    const anjay_coap_msg_t *error = _anjay_coap_msg_build_without_payload(
            _anjay_coap_ensure_aligned_buffer(&aligned_buffer),
            sizeof(aligned_buffer), &info);
    assert(error);

    if (_anjay_coap_socket_send(socket, error)) {
        coap_log(ERROR, "failed to send error message");
    }
}

void _anjay_coap_common_reject_message(anjay_coap_socket_t *socket,
                                       const anjay_coap_msg_t *msg) {
    anjay_coap_msg_type_t type = _anjay_coap_msg_header_get_type(&msg->header);
    uint16_t msg_id = _anjay_coap_msg_get_id(msg);

    if (type != ANJAY_COAP_MSG_CONFIRMABLE) {
        coap_log(TRACE, "ignoring message: id = %u", msg_id);
        // ignore any non-confirmable requests
        return;
    }

    int result = _anjay_coap_common_send_empty(socket, ANJAY_COAP_MSG_RESET,
                                               msg_id);
    (void)result;

    coap_log(TRACE, "%sRESET: id = %u",
             result ? "could not send ": "", msg_id);

    // ignore any errors from send_empty
    // thanks to the power of UDP we can pretend we didn't get the request
}

int _anjay_coap_common_recv_msg_with_timeout(anjay_coap_socket_t *socket,
                                             coap_input_buffer_t *in,
                                             int32_t *inout_timeout_ms,
                                             recv_msg_handler_t *handle_msg,
                                             void *handle_msg_data,
                                             int *out_handler_result) {
    const int original_recv_timeout =
            _anjay_coap_socket_get_recv_timeout(socket);

    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    const int32_t initial_timeout_ms = *inout_timeout_ms;
    int result = 0;

    while (*inout_timeout_ms > 0) {
        AVS_STATIC_ASSERT(sizeof(int) >= sizeof(int32_t),
                          timeout_limit_too_small_to_be_coap_compliant_t);

        _anjay_coap_socket_set_recv_timeout(socket, (int)*inout_timeout_ms);

        result = _anjay_coap_in_get_next_message(in, socket);
        switch (result) {
        case ANJAY_COAP_SOCKET_RECV_ERR_TIMEOUT:
            *inout_timeout_ms = 0;
            result = COAP_RECV_MSG_WITH_TIMEOUT_EXPIRED;
            // fall-through
        default:
            goto exit;

        case ANJAY_COAP_SOCKET_RECV_ERR_MSG_MALFORMED:
        case 0:
            break;
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        *inout_timeout_ms = (int32_t)(initial_timeout_ms
                                      - _anjay_time_diff_ms(&now, &start_time));

        if (!result) {
            const anjay_coap_msg_t *msg = _anjay_coap_in_get_message(in);
            bool wait_for_next = true;
            uint8_t error_code = 0;

            *out_handler_result = handle_msg(msg, handle_msg_data,
                                             &wait_for_next, &error_code);
            if (!wait_for_next) {
                result = -error_code;
                goto exit;
            }

            if (!error_code) {
                _anjay_coap_common_reject_message(socket, msg);
            } else {
                _anjay_coap_common_send_error(socket, msg, error_code);
            }
        }
    }

    *inout_timeout_ms = 0;
    result = COAP_RECV_MSG_WITH_TIMEOUT_EXPIRED;

exit:
    _anjay_coap_socket_set_recv_timeout(socket, original_recv_timeout);

    assert(result <= 0);
    return result;
}

void _anjay_coap_common_update_retry_state(
        coap_retry_state_t *retry_state,
        const coap_transmission_params_t *tx_params,
        anjay_rand_seed_t *rand_seed) {
    ++retry_state->retry_count;
    if (retry_state->retry_count == 1) {
        uint32_t delta = (uint32_t) (tx_params->ack_timeout_ms *
                (tx_params->ack_random_factor - 1.0));
        retry_state->recv_timeout_ms = tx_params->ack_timeout_ms +
                (int32_t) (_anjay_rand32(rand_seed) % delta);
    } else {
        retry_state->recv_timeout_ms *= 2;
    }
}
