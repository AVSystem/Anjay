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

#include "client_internal.h"

#include <anjay_modules/time_defs.h>

#include <avsystem/commons/coap/msg_identity.h>
#include <avsystem/commons/coap/tx_params.h>

#include "../block/request.h"
#include "../coap_log.h"
#include "common.h"

#include <inttypes.h>

VISIBILITY_SOURCE_BEGIN

#ifdef WITH_BLOCK_SEND
#    define has_block_ctx(client) ((client)->block_ctx)
#else
#    define has_block_ctx(client) (false)
#endif

const avs_coap_msg_identity_t *
_anjay_coap_client_get_request_identity(const coap_client_t *client) {
    if (client->state >= COAP_CLIENT_STATE_HAS_REQUEST_HEADER) {
        return &client->last_request_identity;
    } else {
        return NULL;
    }
}

void _anjay_coap_client_reset(coap_client_t *client) {
    client->state = COAP_CLIENT_STATE_RESET;
    _anjay_coap_block_transfer_delete(&client->block_ctx);
}

int _anjay_coap_client_setup_request(coap_client_t *client,
                                     const anjay_msg_details_t *details,
                                     const avs_coap_msg_identity_t *identity) {
    if (client->state != COAP_CLIENT_STATE_RESET) {
        coap_log(TRACE, "unexpected client state: %d", client->state);
        return -1;
    }

    if (identity->token.size > AVS_COAP_MAX_TOKEN_LENGTH) {
        coap_log(ERROR, "invalid token size (must be <= 8)");
        return -1;
    }

    assert(_anjay_coap_out_is_reset(&client->common.out));
    _anjay_coap_out_setup_mtu(&client->common.out, client->common.socket);

    int result = _anjay_coap_out_setup_msg(&client->common.out, identity,
                                           details, NULL);
    if (result) {
        _anjay_coap_client_reset(client);
        _anjay_coap_out_reset(&client->common.out);
        return result;
    }

    client->last_request_identity = *identity;
    client->state = COAP_CLIENT_STATE_HAS_REQUEST_HEADER;

    assert(client->state == COAP_CLIENT_STATE_HAS_REQUEST_HEADER);
    assert(!_anjay_coap_out_is_reset(&client->common.out));
    return 0;
}

typedef enum check_result {
    CHECK_UNEXPECTED_CONFIRMABLE = -2,
    CHECK_INVALID_RESPONSE = -1,
    CHECK_OK,
    CHECK_RESET,
    CHECK_NEEDS_ACK, // Separate Response Confirmable response received

    _CHECK_FIRST = CHECK_INVALID_RESPONSE,
    _CHECK_LAST = CHECK_NEEDS_ACK
} check_result_t;

static check_result_t
req_sent_process_response(coap_client_t *client,
                          const avs_coap_msg_t *response) {
    assert(client->state == COAP_CLIENT_STATE_REQUEST_SENT);

    avs_coap_msg_type_t type = avs_coap_msg_get_type(response);

    switch (type) {
    case AVS_COAP_MSG_RESET:
        coap_log(DEBUG, "Reset response");
        return CHECK_RESET;

    case AVS_COAP_MSG_ACKNOWLEDGEMENT:
        if (avs_coap_msg_get_code(response) == AVS_COAP_CODE_EMPTY) {
            coap_log(DEBUG, "Separate Response: ACK");
            // request ACKed, response in a separate message
            client->state = COAP_CLIENT_STATE_HAS_SEPARATE_ACK;
            return CHECK_OK;
        } else if (!avs_coap_msg_token_matches(
                           response, &client->last_request_identity)) {
            coap_log(DEBUG, "invalid response: token mismatch");
            return CHECK_INVALID_RESPONSE;
        }

        client->state = COAP_CLIENT_STATE_HAS_RESPONSE_CONTENT;
        return CHECK_OK;

    default:
        coap_log(DEBUG, "invalid response: unexpected message");
        return CHECK_INVALID_RESPONSE;
    }
}

static check_result_t
process_separate_response(coap_client_t *client,
                          const avs_coap_msg_t *response) {
    assert(client->state == COAP_CLIENT_STATE_REQUEST_SENT
           || client->state == COAP_CLIENT_STATE_HAS_SEPARATE_ACK);

    avs_coap_msg_type_t type = avs_coap_msg_get_type(response);

    switch (type) {
    case AVS_COAP_MSG_CONFIRMABLE:
        if (!avs_coap_msg_token_matches(response,
                                        &client->last_request_identity)) {
            coap_log(DEBUG, "invalid response: token mismatch");
            return CHECK_UNEXPECTED_CONFIRMABLE;
        }

        client->state = COAP_CLIENT_STATE_HAS_RESPONSE_CONTENT;
        return CHECK_NEEDS_ACK;

    default:
        coap_log(DEBUG, "unexpected message of type %d", type);
        return CHECK_INVALID_RESPONSE;
    }
}

static check_result_t check_response(coap_client_t *client,
                                     const avs_coap_msg_t *response) {
    assert(client->state == COAP_CLIENT_STATE_REQUEST_SENT
           || client->state == COAP_CLIENT_STATE_HAS_SEPARATE_ACK);

    switch (client->state) {
    case COAP_CLIENT_STATE_REQUEST_SENT: {
        uint16_t msg_id = avs_coap_msg_get_id(response);
        if (msg_id != client->last_request_identity.msg_id) {
            // this may still be a Separate Response if Separate ACK
            // got lost
            return process_separate_response(client, response);
        }
        return req_sent_process_response(client, response);
    }

    case COAP_CLIENT_STATE_HAS_SEPARATE_ACK:
        return process_separate_response(client, response);

    default:
        AVS_UNREACHABLE("should never happen");
    }
    coap_log(ERROR, "Invalid response");
    return CHECK_INVALID_RESPONSE;
}

static int process_received(const avs_coap_msg_t *response,
                            void *client_,
                            bool *out_wait_for_next,
                            uint8_t *out_error_code) {
    (void) out_error_code;
    assert(response);

    coap_client_t *client = (coap_client_t *) client_;
    *out_wait_for_next = true;

    check_result_t result = check_response(client, response);

    switch (result) {
    case CHECK_UNEXPECTED_CONFIRMABLE:
        *out_error_code = AVS_COAP_CODE_SERVICE_UNAVAILABLE;
        break;

    case CHECK_INVALID_RESPONSE:
        break;

    case CHECK_OK:
    case CHECK_RESET:
    case CHECK_NEEDS_ACK:
        *out_wait_for_next = false;
        break;

    default:
        AVS_UNREACHABLE("invalid enum value");
    }

    return (int) result;
}

static int accept_response_with_timeout(coap_client_t *client,
                                        avs_time_duration_t timeout) {
    assert(client->state == COAP_CLIENT_STATE_REQUEST_SENT
           || client->state == COAP_CLIENT_STATE_HAS_SEPARATE_ACK);

    int recv_result = -1;
    int result = _anjay_coap_common_recv_msg_with_timeout(
            client->common.coap_ctx, client->common.socket, &client->common.in,
            &timeout, process_received, client, &recv_result);
    if (result) {
        return result;
    }

    assert(client->state == COAP_CLIENT_STATE_REQUEST_SENT
           || client->state == COAP_CLIENT_STATE_HAS_SEPARATE_ACK
           || client->state == COAP_CLIENT_STATE_HAS_RESPONSE_CONTENT);
    assert(recv_result >= _CHECK_FIRST && recv_result <= _CHECK_LAST);

    switch (recv_result) {
    case CHECK_RESET:
        return COAP_CLIENT_RECEIVE_RESET;

    case CHECK_NEEDS_ACK: {
        coap_log(TRACE, "Separate response received; sending ACK");

        const avs_coap_msg_t *msg =
                _anjay_coap_in_get_message(&client->common.in);
        avs_coap_ctx_send_empty(client->common.coap_ctx,
                                client->common.socket,
                                AVS_COAP_MSG_ACKNOWLEDGEMENT,
                                avs_coap_msg_get_id(msg));
        return 0;
    }

    case CHECK_OK:
        return 0;

    default:
        AVS_UNREACHABLE("should never happen");
        return -1;
    }
}

static int send_and_update_retry_state(coap_client_t *client,
                                       const avs_coap_msg_t *msg,
                                       avs_coap_retry_state_t *retry_state) {
    int result = avs_coap_ctx_send(client->common.coap_ctx,
                                   client->common.socket, msg);
    avs_coap_tx_params_t tx_params =
            avs_coap_ctx_get_tx_params(client->common.coap_ctx);
    avs_coap_update_retry_state(retry_state, &tx_params,
                                &client->common.in.rand_seed);
    return result;
}

static int send_confirmable_with_retry(coap_client_t *client,
                                       const avs_coap_msg_t *msg) {
    assert(client->state == COAP_CLIENT_STATE_HAS_REQUEST_HEADER);

    avs_coap_retry_state_t retry_state = {
        .retry_count = 0,
        .recv_timeout = AVS_TIME_DURATION_ZERO
    };
    int result;
    do {
        if ((result = send_and_update_retry_state(client, msg, &retry_state))) {
            coap_log(DEBUG, "send failed");

            break;
        }
        client->state = COAP_CLIENT_STATE_REQUEST_SENT;

        result = accept_response_with_timeout(client, retry_state.recv_timeout);

        if (result != AVS_COAP_CTX_ERR_TIMEOUT) {
            break;
        }

        coap_log(DEBUG, "timeout reached after %" PRId64 ".%09" PRId32 " s",
                 retry_state.recv_timeout.seconds,
                 retry_state.recv_timeout.nanoseconds);
    } while (retry_state.retry_count
             <= avs_coap_ctx_get_tx_params(client->common.coap_ctx)
                        .max_retransmit);

    assert(result <= 0 || result == COAP_CLIENT_RECEIVE_RESET);
    if (result != 0) {
        client->state = COAP_CLIENT_STATE_HAS_REQUEST_HEADER;
    }

    assert(client->state == COAP_CLIENT_STATE_HAS_REQUEST_HEADER
           || client->state == COAP_CLIENT_STATE_HAS_SEPARATE_ACK
           || client->state == COAP_CLIENT_STATE_HAS_RESPONSE_CONTENT);
    return result;
}

int _anjay_coap_client_get_or_receive_msg(coap_client_t *client,
                                          const avs_coap_msg_t **out_msg) {
    if (client->state != COAP_CLIENT_STATE_HAS_SEPARATE_ACK
            && client->state != COAP_CLIENT_STATE_HAS_RESPONSE_CONTENT) {
        coap_log(TRACE, "unexpected client state: %d", client->state);
        return -1;
    }

    if (client->state == COAP_CLIENT_STATE_HAS_SEPARATE_ACK) {
        const avs_time_duration_t timeout = AVS_COAP_SEPARATE_RESPONSE_TIMEOUT;
        int result = accept_response_with_timeout(client, timeout);
        if (result) {
            assert(result <= 0 || result == COAP_CLIENT_RECEIVE_RESET);
            return result;
        }
    }

    assert(client->state == COAP_CLIENT_STATE_HAS_RESPONSE_CONTENT);
    if (out_msg) {
        *out_msg = _anjay_coap_in_get_message(&client->common.in);
    }
    return 0;
}

int _anjay_coap_client_finish_request(coap_client_t *client) {
    if (client->state != COAP_CLIENT_STATE_HAS_REQUEST_HEADER) {
        coap_log(TRACE, "unexpected client state: %d", client->state);
        return -1;
    }

    if (has_block_ctx(client)) {
        int result = _anjay_coap_block_transfer_finish(client->block_ctx);
        if (!result) {
            // Block-wise request finishes after the response to last block
            client->state = COAP_CLIENT_STATE_HAS_RESPONSE_CONTENT;
        }
        return result;
    }

    const avs_coap_msg_t *msg = _anjay_coap_out_build_msg(&client->common.out);
    avs_coap_msg_type_t type = avs_coap_msg_get_type(msg);

    if (type == AVS_COAP_MSG_CONFIRMABLE) {
        return send_confirmable_with_retry(client, msg);
    } else {
        return avs_coap_ctx_send(client->common.coap_ctx, client->common.socket,
                                 msg);
    }
}

int _anjay_coap_client_read(coap_client_t *client,
                            size_t *out_bytes_read,
                            char *out_message_finished,
                            void *buffer,
                            size_t buffer_length) {
    int result = _anjay_coap_client_get_or_receive_msg(client, NULL);
    if (result) {
        return result;
    }

    _anjay_coap_in_read(&client->common.in, out_bytes_read,
                        out_message_finished, buffer, buffer_length);
    return 0;
}

#ifdef WITH_BLOCK_SEND
static int block_write(coap_client_t *client,
                       coap_id_source_t *id_source,
                       const void *data,
                       size_t data_length) {
    if (!client->block_ctx) {
        client->block_ctx =
                _anjay_coap_block_request_new(AVS_COAP_MSG_BLOCK_MAX_SIZE,
                                              &client->common, id_source);
        if (!client->block_ctx) {
            return -1;
        }
    }
    int result = _anjay_coap_block_transfer_write(client->block_ctx, data,
                                                  data_length);
    if (result) {
        _anjay_coap_block_transfer_delete(&client->block_ctx);
    }
    return result;
}
#else
#    define block_write(...) \
        (coap_log(ERROR, "sending blockwise requests not supported"), -1)
#endif

int _anjay_coap_client_write(coap_client_t *client,
                             coap_id_source_t *id_source,
                             const void *data,
                             size_t data_length) {
    (void) client;
    (void) id_source;
    size_t bytes_written = 0;

    if (!has_block_ctx(client)) {
        bytes_written =
                _anjay_coap_out_write(&client->common.out, data, data_length);
        if (bytes_written == data_length) {
            return 0;
        } else {
            coap_log(TRACE, "response payload does not fit in the buffer "
                            "- initiating block-wise transfer");
        }
    }

    return block_write(client, id_source,
                       (const uint8_t *) data + bytes_written,
                       data_length - bytes_written);
}
